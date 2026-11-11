#!/usr/bin/env python3
"""Check the ICM-42688-P WHO_AM_I reported at boot (register BASE-CONTROLLER-GATES: 0x47).

Usage: imu_whoami.py [--log build/hil/logs/boot-banner.log]
Reads the banner captured by boot_banner.py (the firmware reads WHO_AM_I over SPI4 and prints
"OPENAMR IMU whoami=0x47"). Refuses without HIL_BENCH=1. Reads only; drives nothing.
"""
import argparse
import sys
from pathlib import Path

import hil_common as hil

EXPECTED = 0x47


def main(argv=None):
    hil.require_bench("imu_whoami.py")
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--log", default=str(hil.log_dir() / "boot-banner.log"))
    a = p.parse_args(argv)
    if not Path(a.log).is_file():
        return hil.record("imu-whoami", "NOT RUN", f"no boot log {a.log}; run boot_banner.py first")
    info = hil.parse_banner(Path(a.log).read_text(encoding="ascii", errors="replace").splitlines())
    if "whoami" not in info:
        return hil.record("imu-whoami", "FAIL", "no OPENAMR IMU line in the boot log", a.log)
    status = "PASS" if info["whoami"] == EXPECTED else "FAIL"
    return hil.record("imu-whoami", status, f"WHO_AM_I 0x{info['whoami']:02X}, expected 0x{EXPECTED:02X}", a.log)


if __name__ == "__main__":
    sys.exit(main())
