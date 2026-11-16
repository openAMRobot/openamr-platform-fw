#!/usr/bin/env python3
"""zlac_set_link_loss.py: set the ZLAC8015D communication power-down protection time (2000h).

Proves: object 2000h holds the intended value (200 to 300 ms) after write and read-back, and
that the value is saved to EEPROM (2010h = 1) only on explicit confirmation. The drive default
is 0, which keeps the last speed when the host stops sending. Does not move a wheel.

Steps: read 2000h, write --timeout-ms, read back. If --save-confirmed is given, write 2010h = 1
and read 2000h again. Without the flag nothing is saved and a power cycle loses the value.
Run zlac_link_loss_test.md afterwards to prove the stop with wheels up.

Pass: read-back equals the written value (exit 0). Exit 1 on mismatch, exit 2 when the drive
does not answer. Values outside 200 to 300 ms are refused.

Usage: zlac_set_link_loss.py can1 --timeout-ms 250 [--save-confirmed] [--node-id 1]
"""
import argparse
import sys

from bench_common import SdoAbort, SdoClient, open_bus

MIN_MS, MAX_MS = 200, 300  # P-03 key facts: set 200 to 300 ms


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("channel")
    ap.add_argument("--timeout-ms", type=int, required=True)
    ap.add_argument("--save-confirmed", action="store_true",
                    help="also save to EEPROM with 2010h = 1 after the read-back matched")
    ap.add_argument("--node-id", type=int, default=1)
    args = ap.parse_args()
    if not MIN_MS <= args.timeout_ms <= MAX_MS:
        print("refusing: --timeout-ms must be %d to %d" % (MIN_MS, MAX_MS))
        return 2
    bus = open_bus(args.channel)
    try:
        sdo = SdoClient(bus, args.node_id, timeout=0.5)
        try:
            print("2000h before: %d ms" % sdo.read(0x2000, 0))
            sdo.write(0x2000, 0, args.timeout_ms, 2)
            readback = sdo.read(0x2000, 0)
            print("2000h written %d, read back %d" % (args.timeout_ms, readback))
            if readback != args.timeout_ms:
                print("FAIL: read-back mismatch, not saving")
                return 1
            if args.save_confirmed:
                sdo.write(0x2010, 0, 1, 2)
                print("2010h = 1 written (saved to EEPROM); 2000h now %d" % sdo.read(0x2000, 0))
            else:
                print("not saved (no --save-confirmed): value is lost at power off")
        except (TimeoutError, SdoAbort) as exc:
            print("FAIL: %s" % exc)
            return 2
    finally:
        bus.shutdown()
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
