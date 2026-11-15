#!/usr/bin/env python3
"""CAN1, CAN2 and CAN3 loopback through the controller on the bench harness.

Usage: can_loopback.py [--bus can1=can0 --bus can2=can1 --bus can3=can2] [--bitrates 500000,250000,500000]
For each bus the bench USB-CAN adapter sends one test frame (ID 0x7F0 + n, 8 random bytes) and
expects the bench-test firmware to echo it on ID 0x7F8 + n (proposed contract, docs/hil-bench.md).
Safety: it refuses unless HIL_BENCH=1 and HIL_CAN_HARNESS_ONLY=1 are set. The second variable is
the operator's statement that only the bench harness is on the buses (no ZLAC8015D traction
drivers, no BMS, no upper-body actuators). Only the test IDs are sent; no CANopen NMT, SDO or
PDO frame is ever sent. Needs python-can and SocketCAN interfaces on the bench runner.
"""
import argparse
import os
import sys

import hil_common as hil

TEST_ID, ECHO_ID = 0x7F0, 0x7F8


def main(argv=None):
    hil.require_bench("can_loopback.py")
    if os.environ.get("HIL_CAN_HARNESS_ONLY") != "1":
        print("REFUSED: set HIL_CAN_HARNESS_ONLY=1 only when the bench harness alone is on CAN1-3; nothing was sent.")
        return hil.REFUSED
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--bus", action="append", default=[], help="canN=<socketcan interface>")
    a = p.parse_args(argv)
    buses = dict(b.split("=", 1) for b in a.bus) or {"can1": "can0", "can2": "can1", "can3": "can2"}
    import can  # python-can, bench runner only
    rc = 0
    for n, (name, iface) in enumerate(sorted(buses.items()), start=1):
        payload = os.urandom(8)
        with can.Bus(interface="socketcan", channel=iface) as bus:
            bus.send(can.Message(arbitration_id=TEST_ID + n, data=payload, is_extended_id=False))
            reply = None
            for _ in range(20):
                msg = bus.recv(timeout=0.05)
                if msg and msg.arbitration_id == ECHO_ID + n:
                    reply = msg
                    break
        ok = reply is not None and bytes(reply.data) == payload
        rc |= hil.record(f"{name}-loopback", "PASS" if ok else "FAIL",
                         f"{iface}: 0x{TEST_ID + n:03X} -> 0x{ECHO_ID + n:03X} {'echo matches' if ok else 'no matching echo'}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
