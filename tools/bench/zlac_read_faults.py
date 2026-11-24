#!/usr/bin/env python3
"""zlac_read_faults.py: read and decode the fault state of one ZLAC8015D. Read-only.

Proves: the drive answers SDO on CAN1, and reports no active fault before a motion test.
Reads 603Fh (fault code, per side: high 16 bits left, low 16 bits right), 6041h (status
word), 2000h (communication power-down protection time) and 1017h (heartbeat producer time).

Pass: 603Fh is 0 and the status word fault bit (bit 3) is clear (exit 0). Exit 1 otherwise,
with the decoded faults printed. Exit 2 when the drive does not answer.

Usage: zlac_read_faults.py can1 [--node-id 1]
"""
import argparse
import sys

from bench_common import SdoAbort, SdoClient, decode_603f, open_bus


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("channel")
    ap.add_argument("--node-id", type=int, default=1)
    args = ap.parse_args()
    bus = open_bus(args.channel)
    try:
        sdo = SdoClient(bus, args.node_id, timeout=0.5)
        try:
            faults = sdo.read(0x603F, 0)
            status = sdo.read(0x6041, 0)
            link_loss = sdo.read(0x2000, 0)
            hb = sdo.read(0x1017, 0)
        except (TimeoutError, SdoAbort) as exc:
            print("FAIL: %s" % exc)
            return 2
    finally:
        bus.shutdown()
    print("603Fh = 0x%08X" % faults)
    for item in decode_603f(faults):
        print("  fault: " + item)
    print("6041h = 0x%04X (fault bit %s)" % (status, "SET" if status & 0x0008 else "clear"))
    print("2000h = %d ms %s" % (link_loss, "(link-loss protection OFF)" if link_loss == 0 else ""))
    print("1017h = %d ms" % hb)
    ok = faults == 0 and not status & 0x0008
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
