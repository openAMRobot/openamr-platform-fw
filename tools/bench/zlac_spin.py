#!/usr/bin/env python3
"""zlac_spin.py: turn both ZLAC8015D wheels slowly for a fixed time. MOTION COMMAND.

Proves: the drive accepts the enable sequence and a velocity command, and the wheels turn at
the commanded speed (read back from 606Ch). Used as the load for zlac_link_loss_test.md.

Safety: refuses to run without --wheels-up-confirmed (both drive wheels off the ground) and
--wheel-diameter-m. Speed above 0.1 m/s is capped to 0.1 m/s. The command is refreshed every
100 ms. At the end, and on any error or Ctrl-C, it writes 0 speed and 6040h = 0x06 (disable).
Hardwired E-stop remains the stop; this script is not a safety function.

Pass: the measured speed (606Ch, 0.1 r/min) is within 10 percent of the commanded value
after 1 s (exit 0). Exit 1 on mismatch, exit 2 when refused or the drive does not answer.

Usage: zlac_spin.py can1 --wheels-up-confirmed --wheel-diameter-m D --speed 0.05 --duration 5
"""
import argparse
import sys
import time

from bench_common import (SdoAbort, SdoClient, add_motion_args, open_bus,
                          require_motion_allowed, speed_to_rpm)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("channel")
    ap.add_argument("--node-id", type=int, default=1)
    ap.add_argument("--speed", type=float, default=0.05, help="m/s, sign sets direction")
    ap.add_argument("--duration", type=float, default=5.0, help="seconds")
    add_motion_args(ap)
    args = ap.parse_args()
    require_motion_allowed(args)
    rpm = speed_to_rpm(args.speed, args.wheel_diameter_m)
    bus = open_bus(args.channel)
    sdo = SdoClient(bus, args.node_id, timeout=0.5)
    measured = None
    try:
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
                    measured = sdo.read(0x606C, 1, signed=True) / 10.0
                time.sleep(0.1)
        except (TimeoutError, SdoAbort) as exc:
            print("FAIL: %s" % exc)
            return 2
        finally:
            try:
                sdo.write(0x60FF, 1, 0, 4, signed=True)
                sdo.write(0x60FF, 2, 0, 4, signed=True)
                sdo.write(0x6040, 0, 0x06, 2)
            except (TimeoutError, SdoAbort) as exc:
                print("WARNING: could not write stop and disable: %s" % exc)
    finally:
        bus.shutdown()
    print("commanded %d r/min, measured %s r/min" % (rpm, measured))
    if measured is None or rpm == 0 or abs(measured - rpm) > abs(rpm) * 0.1:
        print("FAIL")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
