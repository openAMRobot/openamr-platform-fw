#!/usr/bin/env python3
"""nmt_scan.py: list the CANopen nodes alive on one CAN channel. Sends no motion command.

Proves: each expected node is present on the right channel at the right node ID and bitrate,
and produces heartbeats at its configured period.
Expected: CAN1 node 1 (ZLAC8015D), CAN3 node 0x10 (CTL-017).

Method: listen for heartbeat frames (0x700 + id) for --listen seconds, then read object 1000h
(device type) by SDO from every node ID that was heard, plus any ID in --probe. The only frames
sent are SDO reads.

Pass: every node in --expect is heard at least twice, its mean heartbeat period is within
--period-tolerance of --expect-period-ms, and (when probed) its SDO read answers (exit 0).
Otherwise exit 1.

Usage: nmt_scan.py can1 --expect 1 --expect-period-ms 100
"""
import argparse
import sys
import time

from bench_common import SdoClient, open_bus


def scan(bus, listen_s, probe=()):
    beats = {}
    end = time.monotonic() + listen_s
    while time.monotonic() < end:
        m = bus.recv(timeout=max(0.0, end - time.monotonic()))
        if m is not None and 0x701 <= m.arbitration_id <= 0x77F and not m.is_extended_id:
            beats.setdefault(m.arbitration_id - 0x700, []).append((m.timestamp or time.time(), m.data[0]))
    result = {}
    for node in sorted(set(beats) | set(probe)):
        ts = [t for t, _ in beats.get(node, [])]
        period = None
        if len(ts) >= 2:
            period = (ts[-1] - ts[0]) / (len(ts) - 1) * 1000.0
        device_type = None
        try:
            device_type = SdoClient(bus, node, timeout=0.3).read(0x1000, 0)
        except Exception:  # no answer or abort: reported as None
            pass
        result[node] = {"heartbeats": len(ts),
                        "state": beats[node][-1][1] if node in beats else None,
                        "period_ms": period, "device_type": device_type}
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("channel")
    ap.add_argument("--listen", type=float, default=2.0, help="seconds to listen")
    ap.add_argument("--expect", type=lambda s: int(s, 0), action="append", default=[],
                    help="node ID that must be present (repeatable)")
    ap.add_argument("--probe", type=lambda s: int(s, 0), action="append", default=[],
                    help="extra node ID to probe by SDO even if silent")
    ap.add_argument("--expect-period-ms", type=float, default=None)
    ap.add_argument("--period-tolerance", type=float, default=0.2, help="fraction, default 0.2")
    args = ap.parse_args()
    bus = open_bus(args.channel)
    try:
        found = scan(bus, args.listen, args.probe)
    finally:
        bus.shutdown()
    ok = True
    for node, info in found.items():
        period = "n/a" if info["period_ms"] is None else "%.0f ms" % info["period_ms"]
        dt = "no SDO answer" if info["device_type"] is None else "1000h=0x%08X" % info["device_type"]
        state = "n/a" if info["state"] is None else "0x%02X" % info["state"]
        print("node 0x%02X heartbeats=%d nmt=%s period=%s %s" % (
            node, info["heartbeats"], state, period, dt))
    for node in args.expect:
        info = found.get(node)
        if not info or info["heartbeats"] < 2:
            print("FAIL: expected node 0x%02X not heard" % node)
            ok = False
            continue
        if args.expect_period_ms and abs(info["period_ms"] - args.expect_period_ms) > \
                args.expect_period_ms * args.period_tolerance:
            print("FAIL: node 0x%02X period %.0f ms, expected %.0f ms" % (
                node, info["period_ms"], args.expect_period_ms))
            ok = False
        if info["device_type"] is None:
            print("FAIL: node 0x%02X did not answer SDO 1000h" % node)
            ok = False
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
