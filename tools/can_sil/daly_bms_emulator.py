#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Daly BMS emulator for the software-in-the-loop chain (CAN2, vcan1 by default).

Answers the base controller's request/response polling exactly as the firmware
adapter (firmware/battery_monitor) expects it: 29-bit IDs 0x18|DataID|dst|src,
host 0x40, BMS 0x01, Data IDs 0x90 to 0x93 and 0x98, 8 data bytes big endian.
The layouts are the adapter's documented assumptions, UNCONFIRMED for the
delivered Daly unit (TODO(Evans), see daly_bms.h); this emulator checks the
chain, not the Daly protocol.

Fault injection: `silent` (no answers: BMS stale), `fault` (level-2 alarm in
the failure bytes), `ok` (back to nominal).
"""
import argparse
import threading
import time

import can

HOST = 0x40
BMS = 0x01


def request_id(data_id):
    return (0x18 << 24) | (data_id << 16) | (BMS << 8) | HOST


def response_id(data_id):
    return (0x18 << 24) | (data_id << 16) | (HOST << 8) | BMS


class DalyBmsEmulator:
    def __init__(self, bus, voltage_v=25.6, current_a=0.0, soc_pct=90.0):
        self.bus = bus
        self.voltage_v = voltage_v
        self.current_a = current_a
        self.soc_pct = soc_pct
        self.silent = False
        self.failure = bytes(8)
        self.requests = 0
        self._running = False
        self._thread = None

    def payload(self, data_id):
        if data_id == 0x90:
            v = int(round(self.voltage_v * 10))
            i = int(round(self.current_a * 10)) + 30000
            s = int(round(self.soc_pct * 10))
            return bytes([v >> 8, v & 0xFF, 0, 0, i >> 8, i & 0xFF, s >> 8, s & 0xFF])
        if data_id == 0x91:
            return bytes([0x0C, 0xE4, 3, 0x0C, 0xD0, 7, 0, 0])  # 3300 / 3280 mV
        if data_id == 0x92:
            return bytes([65, 1, 60, 2, 0, 0, 0, 0])  # 25 / 20 degC
        if data_id == 0x93:
            return bytes([0, 1, 1, 7, 0, 0, 0x27, 0x10])
        if data_id == 0x98:
            return self.failure
        return None

    def inject_fault(self, on=True):
        # Level-2 alarm: cell over-voltage level 2 (bit 1 of byte 0) under the
        # adapter's mask convention (level 1 even bits, level 2 odd bits).
        self.failure = bytes([0x02, 0, 0, 0, 0, 0, 0, 0]) if on else bytes(8)

    def handle(self, msg):
        if not msg.is_extended_id or msg.is_error_frame:
            return
        if (msg.arbitration_id & 0xFF00FFFF) != request_id(0) & 0xFF00FFFF:
            return
        data_id = (msg.arbitration_id >> 16) & 0xFF
        self.requests += 1
        data = self.payload(data_id)
        if data is None or self.silent:
            return
        self.bus.send(can.Message(arbitration_id=response_id(data_id), is_extended_id=True,
                                  data=data))

    def _run(self):
        while self._running:
            try:
                msg = self.bus.recv(timeout=0.05)
            except can.CanError:
                time.sleep(0.05)
                continue
            if msg is not None:
                try:
                    self.handle(msg)
                except can.CanError:
                    pass

    def start(self):
        self._running = True
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()
        return self

    def stop(self):
        self._running = False
        if self._thread:
            self._thread.join(timeout=1.0)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--channel", default="vcan1")
    args = ap.parse_args()
    bus = can.Bus(interface="socketcan", channel=args.channel)
    node = DalyBmsEmulator(bus).start()
    cmds = {"silent": lambda: setattr(node, "silent", True),
            "ok": lambda: (setattr(node, "silent", False), node.inject_fault(False)),
            "fault": lambda: node.inject_fault(True)}

    def console():
        try:
            for line in iter(input, None):
                fn = cmds.get(line.strip())
                if fn:
                    fn()
        except EOFError:
            pass
    threading.Thread(target=console, daemon=True).start()
    try:
        while True:
            time.sleep(1.0)
    except KeyboardInterrupt:
        pass
    finally:
        node.stop()
        bus.shutdown()


if __name__ == "__main__":
    main()
