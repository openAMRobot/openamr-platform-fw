#!/usr/bin/env python3
"""Read the boot banner on the ST-LINK virtual COM port and check firmware SHA and reset reason.

Usage: boot_banner.py --expect-sha SHA [--port /dev/ttyACM0] [--seconds 10]
Resets the target with st-flash --connect-under-reset reset (ST-LINK), then reads USART3 at
115200 baud. Records two tests: boot-banner (banner present, firmware SHA and board match) and
reset-reason (a reset reason line with known flags; PIN expected after an ST-LINK reset). The
banner is a proposed firmware contract (docs/hil-bench.md); until the firmware prints it both
tests FAIL. Refuses without HIL_BENCH=1. Needs pyserial on the bench runner.
"""
import argparse
import subprocess
import sys
import time

import hil_common as hil


def main(argv=None):
    hil.require_bench("boot_banner.py")
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--expect-sha", required=True)
    p.add_argument("--port", default="/dev/ttyACM0")
    p.add_argument("--seconds", type=float, default=10)
    a = p.parse_args(argv)
    import serial  # pyserial, bench runner only
    log = hil.log_dir() / "boot-banner.log"
    with serial.Serial(a.port, 115200, timeout=0.2) as port, open(log, "wb") as fh:
        port.reset_input_buffer()
        subprocess.run(["st-flash", "--connect-under-reset", "reset"], check=False, timeout=30)
        end, data = time.time() + a.seconds, b""
        while time.time() < end:
            data += port.read(256)
        fh.write(data)
    info = hil.parse_banner(data.decode("ascii", "replace").splitlines())
    if "fw" not in info:
        rc = hil.record("boot-banner", "FAIL", "no OPENAMR BOOT line on the virtual COM port", log)
    elif not a.expect_sha.startswith(info["fw"]) or info.get("board") != "NUCLEO-H723ZG":
        rc = hil.record("boot-banner", "FAIL", f"banner fw={info['fw']} board={info.get('board')}, expected {a.expect_sha[:12]}", log)
    else:
        rc = hil.record("boot-banner", "PASS", f"fw={info['fw']} board={info['board']}", log)
    reasons = info.get("reset", [])
    if not reasons:
        rc |= hil.record("reset-reason", "FAIL", "no OPENAMR RESET line", log)
    elif set(reasons) - hil.RESET_REASONS:
        rc |= hil.record("reset-reason", "FAIL", f"unknown reset flags {reasons}", log)
    else:
        rc |= hil.record("reset-reason", "PASS" if "PIN" in reasons else "FAIL", f"reset reason {','.join(reasons)}", log)
    return rc


if __name__ == "__main__":
    sys.exit(main())
