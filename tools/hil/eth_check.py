#!/usr/bin/env python3
"""Ethernet: ping the controller and check a UDP echo (micro-ROS runs over UDP on this link).

Usage: eth_check.py --ip 192.168.1.10 [--udp-port 7] [--count 5]
The UDP echo service on the controller is part of the proposed bench-test firmware contract
(docs/hil-bench.md). Refuses without HIL_BENCH=1. Sends only ICMP echo and UDP test payloads.
"""
import argparse
import os
import socket
import subprocess
import sys

import hil_common as hil


def main(argv=None):
    hil.require_bench("eth_check.py")
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--ip", default=os.environ.get("HIL_CONTROLLER_IP", "192.168.1.10"))
    p.add_argument("--udp-port", type=int, default=7)
    p.add_argument("--count", type=int, default=5)
    a = p.parse_args(argv)
    log = hil.log_dir() / "eth.log"
    r = subprocess.run(["ping", "-c", str(a.count), "-W", "1", a.ip], capture_output=True, text=True)
    log.write_text(r.stdout + r.stderr)
    rc = hil.record("eth-ping", "PASS" if r.returncode == 0 else "FAIL", f"ping {a.ip} x{a.count}, exit {r.returncode}", log)
    payload = os.urandom(16)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.settimeout(1.0)
        try:
            s.sendto(payload, (a.ip, a.udp_port))
            data, _ = s.recvfrom(64)
            ok = data == payload
            detail = "echo matches" if ok else f"echo differs ({len(data)} bytes)"
        except OSError as exc:
            ok, detail = False, f"no echo: {exc}"
    rc |= hil.record("eth-udp-echo", "PASS" if ok else "FAIL", f"UDP {a.ip}:{a.udp_port} {detail}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
