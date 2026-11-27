#!/usr/bin/env python3
"""Static STM32H723 FDCAN timing and message-RAM overlap checks.

This validates source configuration only; it is not a register or bus test.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT / "boards/stm32/OpenAMRobot/Core/Src/main.c"


def main() -> int:
    text = MAIN.read_text(encoding="utf-8")
    errors = []
    ranges = []
    expected = {
        1: (500000, 6, 0, 1, 0),
        2: (250000, 12, 512, 0, 1),
        3: (500000, 6, 1024, 1, 0),
    }
    for bus, (bitrate, prescaler, offset, std_filters, ext_filters) in expected.items():
        match = re.search(rf"static void MX_FDCAN{bus}_Init\(void\)\s*\{{(.*?)(?=\n/\*\*|\Z)", text, re.S)
        if not match:
            errors.append(f"FDCAN{bus} init not found")
            continue
        body = text[match.start():match.end()]
        def value(name):
            found = re.search(rf"hfdcan{bus}\.Init\.{name}\s*=\s*(\d+)(?:[Uu][Ll]?)?", body)
            return int(found.group(1)) if found else None
        if value("NominalPrescaler") != prescaler:
            errors.append(f"FDCAN{bus} wrong prescaler for {bitrate}")
        if value("NominalTimeSeg1") != 13 or value("NominalTimeSeg2") != 2 or value("NominalSyncJumpWidth") != 1:
            errors.append(f"FDCAN{bus} timing differs from 16 TQ, SJW1")
        if value("MessageRAMOffset") != offset:
            errors.append(f"FDCAN{bus} unexpected message RAM offset {value('MessageRAMOffset')}")
        if value("StdFiltersNbr") != std_filters or value("ExtFiltersNbr") != ext_filters:
            errors.append(f"FDCAN{bus} filter counts differ from approved scoped configuration")
        if value("RxFifo0ElmtsNbr") != 16:
            errors.append(f"FDCAN{bus} unexpected RX FIFO capacity")
        tx = value("TxFifoQueueElmtsNbr")
        expected_tx = 8 if bus == 2 else 0
        if tx != expected_tx:
            errors.append(f"FDCAN{bus} TX capacity {tx}, expected {expected_tx}")
        # Current 8-byte RX/TX elements: std 1, ext 2, RX 18, TX 18 words each.
        allocation = std_filters + 2 * ext_filters + 18 * 16 + 18 * tx
        ranges.append((offset, offset + allocation, bus))

    for index, (start, end, bus) in enumerate(sorted(ranges)):
        if start < 0 or end > 2560:
            errors.append(f"FDCAN{bus} message RAM extent [{start},{end}) outside 10 KiB")
        if index and start < sorted(ranges)[index - 1][1]:
            previous = sorted(ranges)[index - 1][2]
            errors.append(f"FDCAN{bus} message RAM overlaps FDCAN{previous}")
        print(f"FDCAN{bus}: words [{start},{end})")
    if errors:
        for error in errors:
            print(f"::error::{error}")
        return 1
    print("PASS: FDCAN timing and non-overlapping message RAM policy (source only)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
