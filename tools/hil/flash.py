#!/usr/bin/env python3
"""Flash a firmware image on the bench (STM32 via ST-LINK, Teensy 4.0 via teensy_loader_cli).

Usage: flash.py --target stm32|teensy --image FILE --sha256 HEX
The image's SHA-256 must equal the one recorded by the nightly (SHA256SUMS in the ci-evidence
artifact), so only an image built by CI from a reviewed commit is flashed. Refuses without
HIL_BENCH=1. Tools: st-flash (stlink-tools) or STM32_Programmer_CLI (HIL_STM32_FLASHER=cubeprog),
teensy_loader_cli. Flashing writes program memory only; it drives no output of the robot.
"""
import argparse
import hashlib
import os
import subprocess
import sys

import hil_common as hil


def main(argv=None):
    hil.require_bench("flash.py")
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--target", choices=["stm32", "teensy"], required=True)
    p.add_argument("--image", required=True)
    p.add_argument("--sha256", required=True)
    a = p.parse_args(argv)
    digest = hashlib.sha256(open(a.image, "rb").read()).hexdigest()
    if digest != a.sha256.lower():
        return hil.record(f"flash-{a.target}", "FAIL", f"image SHA-256 {digest} is not the recorded {a.sha256}")
    if a.target == "stm32":
        if os.environ.get("HIL_STM32_FLASHER") == "cubeprog":
            cmd = ["STM32_Programmer_CLI", "-c", "port=SWD", "-w", a.image, "-v", "-rst"]
        else:
            cmd = ["st-flash", "--reset", "--format", "ihex", "write", a.image]
    else:
        cmd = ["teensy_loader_cli", "--mcu=TEENSY40", "-w", "-v", a.image]
    log = hil.log_dir() / f"flash-{a.target}.log"
    with open(log, "w") as fh:
        r = subprocess.run(cmd, stdout=fh, stderr=subprocess.STDOUT, timeout=180)
    status = "PASS" if r.returncode == 0 else "FAIL"
    return hil.record(f"flash-{a.target}", status, f"{' '.join(cmd[:1])} exit {r.returncode}, image {digest[:12]}", log)


if __name__ == "__main__":
    sys.exit(main())
