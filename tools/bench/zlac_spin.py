#!/usr/bin/env python3
"""zlac_spin.py: turn both ZLAC8015D wheels slowly for a fixed time. MOTION COMMAND.

Proves: the drive accepts the enable sequence and a velocity command, and BOTH wheels turn at
the commanded speed, read back from 606Ch:01 and 606Ch:02 (0.1 r/min). Used as the load for
zlac_link_loss_test.md. The sub-indexes are reported as they are; no left or right meaning is
attached to either (the vendor documents contradict each other on that).

Needs the owner's bench procedure and a second person at the hardwired E-stop. It refuses to
run without --i-am-at-the-bench, --wheels-up-confirmed (both drive wheels off the ground) and
--wheel-diameter-m. It reads 2026h:05 first and refuses unless the speed resolution is 1 (D-24).
Speed up to 0.1 m/s is used as given, 0.1 to 0.2 m/s is capped to 0.1 m/s with a warning, and
above 0.2 m/s the script refuses (exit 2) unless --allow-high-speed is given (then the 0.1 m/s
cap is lifted, never above the 0.5 m/s ceiling), so a typo cannot start a ceiling-speed spin.
The command is refreshed every 100 ms. At the end, and on any error or Ctrl-C, it writes zero
target to both sub-indexes and the disable control word, each write tried twice and each
checked. The hardwired E-stop remains the stop; this script is not a safety function, and a
stop write the drive acknowledged is protocol evidence, not stop evidence.

Pass (exit 0), all of:
  - 606Ch:01 and 606Ch:02 both read, and both are within 10 percent of the commanded value
    (same sign) in the last sample, taken at least 1 s after the start;
  - the zero-target and disable writes of the cleanup were all acknowledged.
Exit 1: a wheel is outside the window or no sample was taken. Exit 2: refused, or the drive did
not answer or aborted during the run (including an unreadable 606Ch sub-index). Exit 3: the
cleanup could not confirm the zero-target or disable writes (takes precedence over 0 to 2).

Usage: zlac_spin.py can1 --i-am-at-the-bench --wheels-up-confirmed --wheel-diameter-m D
       --speed 0.05 --duration 5
"""
import argparse
import sys
import time

from bench_common import (SdoAbort, SdoClient, add_motion_args, open_bus, read_wheel_speeds,
                          require_motion_allowed, require_speed_resolution, speed_to_rpm,
                          speed_within)

CLEANUP_ATTEMPTS = 2
EXIT_CLEANUP = 3


def cleanup(sdo):
    """Write zero target to both sub-indexes, then disable. Every write is tried, whatever the
    earlier ones did. Returns the list of writes that were not acknowledged."""
    failed = []
    steps = (("60FFh:01 = 0", (0x60FF, 1, 0, 4, True)),
             ("60FFh:02 = 0", (0x60FF, 2, 0, 4, True)),
             ("6040h = 0x06", (0x6040, 0, 0x06, 2, False)))
    for label, (index, sub, value, size, signed) in steps:
        err = None
        for _ in range(CLEANUP_ATTEMPTS):
            try:
                sdo.write(index, sub, value, size, signed=signed)
                err = None
                break
            except (TimeoutError, SdoAbort) as exc:
                err = exc
        if err is not None:
            print("CLEANUP FAILED: %s not acknowledged: %s" % (label, err))
            failed.append(label)
    return failed


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("channel")
    ap.add_argument("--node-id", type=int, default=1)
    ap.add_argument("--speed", type=float, default=0.05, help="m/s, sign sets direction")
    ap.add_argument("--duration", type=float, default=5.0, help="seconds")
    add_motion_args(ap)
    args = ap.parse_args()
    require_motion_allowed(args)
    rpm = speed_to_rpm(args.speed, args.wheel_diameter_m, args.allow_high_speed)
    bus = open_bus(args.channel)
    sdo = SdoClient(bus, args.node_id, timeout=0.5)
    measured = None
    code = 0
    failed_cleanup = []
    try:
        require_speed_resolution(sdo)
        try:
            sdo.write(0x6060, 0, 3, 1, signed=True)
            for cw in (0x06, 0x07, 0x0F):
                sdo.write(0x6040, 0, cw, 2)
            end = time.monotonic() + args.duration
            t0 = time.monotonic()
            while time.monotonic() < end:
                sdo.write(0x60FF, 1, rpm, 4, signed=True)
                sdo.write(0x60FF, 2, rpm, 4, signed=True)
                if time.monotonic() - t0 > 1.0:
                    measured = read_wheel_speeds(sdo)
                time.sleep(0.1)
        except (TimeoutError, SdoAbort) as exc:
            print("FAIL: %s" % exc)
            code = 2
        finally:
            failed_cleanup = cleanup(sdo)
    finally:
        bus.shutdown()
    print("commanded %d r/min, measured 606Ch:01 and 606Ch:02 = %s r/min" % (
        rpm, "none" if measured is None else "%.1f and %.1f" % measured))
    if failed_cleanup:
        print("FAIL: stop not confirmed (%s): wheels may still be turning, use the E-stop"
              % ", ".join(failed_cleanup))
        return EXIT_CLEANUP
    if code:
        return code
    if measured is None or rpm == 0 or not all(speed_within(m, rpm) for m in measured):
        print("FAIL")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
