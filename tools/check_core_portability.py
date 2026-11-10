#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fail if portable firmware code includes a board, HAL or stack header.

I8 architecture rule: the core and the device adapters under firmware/ depend
only on interfaces the core defines. Board ports live under boards/.
Usage: check_core_portability.py <firmware dir>
"""
import pathlib
import re
import sys

FORBIDDEN = re.compile(
    r'^\s*#\s*include\s*[<"]'
    r'(stm32|main\.h|cmsis|core_cm|Arduino|rcl/|rclc/|rmw|uxr/|lwip|FreeRTOS|imxrt|teensy)',
    re.IGNORECASE,
)


def main() -> int:
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "firmware")
    files = sorted(p for p in root.rglob("*") if p.suffix in (".c", ".h"))
    if not files:
        print(f"no C sources under {root}")
        return 1
    bad = []
    for path in files:
        for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if FORBIDDEN.search(line):
                bad.append(f"{path}:{lineno}: {line.strip()}")
    for entry in bad:
        print(entry)
    print(f"checked {len(files)} files, {len(bad)} forbidden includes")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
