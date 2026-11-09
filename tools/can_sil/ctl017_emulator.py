#!/usr/bin/env python3
"""Software-in-the-loop emulator of the upper-body controller CTL-017 (CANopen node 0x10, CAN3).

PROPOSAL, TODO(Evans): the object dictionary, PDO layouts and fault handling below follow the
CAN3 lift message set proposal with decisions D1 to D20 (platform-fw #12) and the v1 list
derived from it. The v1 document is not yet posted, so everything marked TODO(Evans) is a
placeholder to be replaced by the approved object dictionary. Not a safety function: the
hardwired E-stop remains the stop (P-03 item 30 A7).

Frames (CiA 301 predefined set, node 0x10):
  RPDO1 0x210  LiftCommand  seq u8, op u8 (0 STOP, 1 MOVE_TO_STOP, 2 CLEAR_FAULT, 3 START_HOMING), stop_id u8
  TPDO1 0x190  LiftStatus   state, flags, position u16 (0.1 mm, 0xFFFF unknown), active_stop_id,
                            last_seq, cmd_result, status_counter  (8 bytes, 100 ms and on change)
  TPDO2 0x290  TODO(Evans): sensors u8 (bit0..2 = L1000, L1175, L1350), target_stop u8 (0xFF none),
                            motor_current u16 (mA), fault_mask u16
  RPDO2 0x310  TODO(Evans): head_light duty u8 (0..100), getac_enable u8 (0/1)
  EMCY  0x090  error code u16 (0xFF00), error register u8, fault_id u8, severity u8, 3 x 0
  heartbeat 0x710 (node), core heartbeat 0x701 consumed (D12 alternative (a)).
SDO: 0x1003:1 latest fault bitmask, 0x2200 Hall count (u32), 0x2201 position 0.1 mm.

Usage: python3 ctl017_emulator.py --channel vcan0
As a library: see tests/test_ctl017.py.
"""
import argparse
import struct
import time

import can

from canopen_common import (HB_BASE, NMT_OPERATIONAL, CanopenNode, ObjectDict)

NODE_ID = 0x10
CORE_NODE_ID = 0x01
RPDO1 = 0x200 + NODE_ID
RPDO2 = 0x300 + NODE_ID
TPDO1 = 0x180 + NODE_ID
TPDO2 = 0x280 + NODE_ID
EMCY = 0x080 + NODE_ID

# states
INIT, UNHOMED, HOMING, IDLE, MOVING, STOPPING, FAULT = range(7)
# ops
OP_STOP, OP_MOVE_TO_STOP, OP_CLEAR_FAULT, OP_START_HOMING = range(4)
# flags
F_POSITION_VALID, F_AT_STOP, F_MOVING, F_LOWER, F_UPPER, F_FAULT, F_MOTOR_POWER, F_RECOVERY = (
    1 << i for i in range(8))
# cmd_result
(R_OK, R_BAD_STOP, R_POS_INVALID, R_FAULT, R_LIMIT, R_BUSY, R_RECOVERY, R_CLEAR_UNMET,
 R_HOMING_UNMET, R_BAD_OP, R_SEQ_CONFLICT, R_NO_MOTOR_POWER) = range(12)
R_NONE = 0xFF
# fault ids (bit n-1 in the mask)
F01, F02, F03, F04, F05, F06, F07, F08 = range(1, 9)
NON_CLEARABLE = (1 << (F05 - 1)) | (1 << (F06 - 1))

STROKE_MM = 350.0           # P-03 / work package
SPEED_MM_S = 18.0           # P-03 / work package
STOP_POS_MM = [5.0, 177.5, 345.0]  # TODO(Evans): stops 0 and 2 sit 5 mm inside the end switches
                                   # (platform-fw #12); L1175 midpoint is a placeholder.
STOP_TOLERANCE_MM = 2.0     # D3
MM_PER_HALL = 0.2092        # TODO(Evans): confirm for the ordered column model
SENSOR_WINDOW_MM = 1.0      # TODO(Evans): IME12 switching window as mounted
HEARTBEAT_MS = 100          # D15
SUPERVISION_MS = 300        # D15
TRAVEL_TIMEOUT_S = STROKE_MM / SPEED_MM_S * 1.5  # TODO(Evans): final value from the bench
STALL_MS = 500              # TODO(Evans): no Hall pulses while driving
MOVING_CURRENT_MA = 1000    # simulation placeholder only, TODO(Evans): measure on the motor


class Ctl017Emulator(CanopenNode):
    def __init__(self, bus, node_id=NODE_ID, start_pos_mm=None):
        super().__init__(bus, node_id, HEARTBEAT_MS, ObjectDict())
        self.od.add(0x1000, 0, 4, 0, rw="ro")  # device type, TODO(Evans): custom profile number
        self.od.add(0x1003, 0, 1, 0, rw="ro")
        self.od.add(0x1003, 1, 4, 0, rw="ro")
        self.od.add(0x2200, 0, 4, 0, rw="ro")
        self.od.add(0x2201, 0, 2, 0xFFFF, rw="ro")
        self.true_pos = 0.0 if start_pos_mm is None else start_pos_mm
        self.counted = self.true_pos
        self.hall_dead = False           # injected: no Hall pulses (stall)
        self.forced_sensors = 0          # injected sensor bits (OR-ed in)
        self.motor_power = True
        self.motor_current_ma = 0
        self.head_light = 0
        self.getac_enable = 0
        self.last_seq = 0
        self.cmd_result = R_NONE
        self._cmd = None                 # (seq, op, stop) of the last accepted command
        self.faults = 0
        self.state = INIT
        self.position_valid = False
        self.recovery_required = True
        self.target_mm = None
        self.target_stop = 0xFF
        self.counter = 0
        self.last_tpdo = 0.0
        self.last_status = None
        self.last_keepalive = time.monotonic()
        self.last_core_hb = time.monotonic()
        self.move_time = 0.0
        self.stall_time = 0.0
        self.prev_sensors = 0
        self.emcy_log = []
        self._startup_checks()
        self._update_od()

    # --- helpers ---------------------------------------------------------
    @property
    def hall_count(self):
        return int(round(self.counted / MM_PER_HALL))

    def sensors(self):
        bits = self.forced_sensors
        for i, p in enumerate(STOP_POS_MM):
            if abs(self.true_pos - p) <= SENSOR_WINDOW_MM:
                bits |= 1 << i
        return bits

    def lower_limit(self):
        return self.true_pos <= 0.0

    def upper_limit(self):
        return self.true_pos >= STROKE_MM

    def active_stop(self):
        if not self.position_valid or self.state in (MOVING, HOMING, STOPPING):
            return 0xFF
        for i, p in enumerate(STOP_POS_MM):
            if abs(self.counted - p) <= STOP_TOLERANCE_MM:
                return i
        return 0xFF

    def _startup_checks(self):
        s = self.sensors()
        if bin(s).count("1") > 1:
            self._raise(F02)
        elif s:  # power-up at a sensor means position valid without homing
            idx = [i for i in range(3) if s & (1 << i)][0]
            self.counted = STOP_POS_MM[idx]
            self.position_valid = True
            self.recovery_required = False
        self._settle_state()

    def _settle_state(self):
        if self.faults:
            self.state = FAULT
        elif self.position_valid and not self.recovery_required:
            self.state = IDLE
        else:
            self.state = UNHOMED

    def _raise(self, fault_id, severity=2):
        bit = 1 << (fault_id - 1)
        newly = not (self.faults & bit)
        self.faults |= bit
        self._stop_motor()
        if fault_id in (F02, F05, F06):
            self.position_valid = False
            self.recovery_required = True
        self.state = FAULT
        if newly:
            self.emcy_log.append(fault_id)
            self._send(EMCY, struct.pack("<HBBB3x", 0xFF00, 0x01, fault_id, severity))
        self.od.set(0x1003, 1, self.faults)
        self.od.set(0x1003, 0, 1)

    def _stop_motor(self):
        self.target_mm = None
        self.target_stop = 0xFF
        self.motor_current_ma = 0

    # --- injection API ---------------------------------------------------
    def inject_stall(self, on=True):
        self.hall_dead = on

    def inject_hall_slip(self, mm):
        """Hall count drifts from the true position (lost or extra pulses)."""
        with self.lock:
            self.counted += mm

    def set_motor_power(self, present):
        with self.lock:
            self.motor_power = present

    def power_cycle(self, start_pos_mm=None):
        """Node power loss: faults cleared, position unknown, re-home required (D4, D9)."""
        with self.lock:
            if start_pos_mm is not None:
                self.true_pos = start_pos_mm
            self.faults = 0
            self.position_valid = False
            self.recovery_required = True
            self.counted = self.true_pos
            self.last_seq, self.cmd_result, self._cmd = 0, R_NONE, None
            self._stop_motor()
            self.od.set(0x1003, 0, 0)
            self.od.set(0x1003, 1, 0)
            self.nmt_state = NMT_OPERATIONAL
            self._send_boot()
            self._startup_checks()
            self._update_od()

    # --- protocol --------------------------------------------------------
    def on_rx_frame(self, msg):
        if msg.arbitration_id == RPDO1 and len(msg.data) >= 3:
            self._command(*msg.data[:3])
        elif msg.arbitration_id == RPDO2 and len(msg.data) >= 2:
            self.head_light = min(100, msg.data[0])
            self.getac_enable = 1 if msg.data[1] else 0
        elif msg.arbitration_id == HB_BASE + CORE_NODE_ID:
            self.last_core_hb = time.monotonic()

    def _busy(self):
        return self.state in (MOVING, HOMING, STOPPING)

    def _command(self, seq, op, stop):
        self.last_keepalive = time.monotonic()
        if self._cmd is not None and seq == self._cmd[0]:
            if (seq, op, stop) == self._cmd:
                return  # keep-alive, changes nothing else
            self.last_seq = seq
            self.cmd_result = R_SEQ_CONFLICT
            return
        self.last_seq = seq
        self.cmd_result = self._execute(op, stop)
        self._cmd = (seq, op, stop)

    def _execute(self, op, stop):
        if op == OP_STOP:
            if self._busy():
                self._stop_motor()
                self.state = STOPPING
            return R_OK
        if op == OP_CLEAR_FAULT:
            if self._busy():
                return R_BUSY
            if not self.faults:
                return R_OK
            if (self.faults & NON_CLEARABLE) or (self.lower_limit() and self.upper_limit()):
                return R_CLEAR_UNMET
            self.faults = 0
            self.od.set(0x1003, 1, 0)
            self.od.set(0x1003, 0, 0)
            self._settle_state()
            return R_OK
        if op == OP_MOVE_TO_STOP:
            if stop > 2:
                return R_BAD_STOP
            if self.faults:
                return R_FAULT
            if self.recovery_required:
                return R_RECOVERY
            if not self.position_valid:
                return R_POS_INVALID
            if not self.motor_power:
                return R_NO_MOTOR_POWER
            if self._busy():
                return R_BUSY
            if self.active_stop() == stop:
                return R_OK  # no-op
            going_up = STOP_POS_MM[stop] > self.counted
            if (going_up and self.upper_limit()) or (not going_up and self.lower_limit()):
                return R_LIMIT
            self.target_mm, self.target_stop = STOP_POS_MM[stop], stop
            self.state = MOVING
            self.move_time = 0.0
            return R_OK
        if op == OP_START_HOMING:
            if self.faults:
                return R_FAULT
            if not self.motor_power:
                return R_NO_MOTOR_POWER
            if self._busy():
                return R_BUSY
            self.state = HOMING
            self.target_mm = -1.0e6  # down to the lower switch (D10)
            self.move_time = 0.0
            return R_OK
        return R_BAD_OP

    # --- model -----------------------------------------------------------
    def tick(self, dt):
        now = time.monotonic()
        driving = self.state in (MOVING, HOMING) and self.target_mm is not None
        if driving and not self.motor_power:  # motor stage unpowered: lift halts, count kept
            self._stop_motor()
            self.state = STOPPING
            driving = False
        if driving:
            self.move_time += dt
            direction = 1.0 if self.target_mm > self.counted else -1.0
            step = min(SPEED_MM_S * dt, abs(self.target_mm - self.counted))
            new_true = min(STROKE_MM, max(0.0, self.true_pos + direction * step))
            actual = new_true - self.true_pos
            self.true_pos = new_true
            if not self.hall_dead:
                self.counted += actual
                self.stall_time = 0.0
            else:
                self.stall_time += dt
            self.motor_current_ma = MOVING_CURRENT_MA
            if self.stall_time * 1000 >= STALL_MS:
                self._raise(F03)
            elif self.move_time > TRAVEL_TIMEOUT_S:
                self._raise(F04)
            elif self.state == HOMING and self.lower_limit():
                self.counted = 0.0
                self.position_valid = True
                self.recovery_required = False
                self._stop_motor()
                self.state = STOPPING
            elif self.state == MOVING and abs(self.counted - self.target_mm) < 1e-6:
                self._stop_motor()
                self.state = STOPPING
            elif self.state == MOVING and (self.upper_limit() or self.lower_limit()):
                self._raise(F03)  # blocked at an end switch short of target
        elif self.state == STOPPING:
            self._settle_state()
            self.motor_current_ma = 0
        # supervision during motion or homing (D12 alternative (a))
        if self.state in (MOVING, HOMING):
            ka_lost = (now - self.last_keepalive) * 1000 > SUPERVISION_MS
            hb_lost = (now - self.last_core_hb) * 1000 > SUPERVISION_MS
            if ka_lost or hb_lost:
                self._raise(F07)
        # Hall count cross-check at each stop sensor edge (F02); two sensors = fault
        s = self.sensors()
        if s != self.prev_sensors and s:
            if bin(s).count("1") > 1:
                self._raise(F02)
            elif self.position_valid:
                idx = [i for i in range(3) if s & (1 << i)][0]
                if abs(self.counted - STOP_POS_MM[idx]) > STOP_TOLERANCE_MM:
                    self._raise(F02)
        self.prev_sensors = s
        if self.lower_limit() and self.upper_limit():
            self._raise(F01)
        self._update_od()
        self._publish(now)

    def _update_od(self):
        self.od.set(0x2200, 0, self.hall_count)
        self.od.set(0x2201, 0, self._pos_raw())

    def _pos_raw(self):
        if not self.position_valid:
            return 0xFFFF
        return int(round(max(0.0, self.counted) * 10))

    def flags(self):
        f = 0
        if self.position_valid:
            f |= F_POSITION_VALID
        if self.active_stop() != 0xFF:
            f |= F_AT_STOP
        if self.state in (MOVING, HOMING, STOPPING):
            f |= F_MOVING
        if self.lower_limit():
            f |= F_LOWER
        if self.upper_limit():
            f |= F_UPPER
        if self.faults:
            f |= F_FAULT
        if self.motor_power:
            f |= F_MOTOR_POWER
        if self.recovery_required:
            f |= F_RECOVERY
        return f

    def status_payload(self, counter):
        return struct.pack("<BBHBBBB", self.state, self.flags(), self._pos_raw(),
                           self.active_stop(), self.last_seq, self.cmd_result, counter)

    def _publish(self, now):
        key = self.status_payload(0)[:7]
        if key != self.last_status or (now - self.last_tpdo) * 1000 >= HEARTBEAT_MS:
            self.counter = (self.counter + 1) & 0xFF
            self._send(TPDO1, self.status_payload(self.counter))
            self._send(TPDO2, struct.pack("<BBHH", self.sensors(), self.target_stop,
                                          self.motor_current_ma, self.faults & 0xFFFF))
            self.last_status = key
            self.last_tpdo = now

    def state_line(self):
        with self.lock:
            return ("ctl017 state=%d flags=0x%02X pos=%.1f mm hall=%d sensors=0x%X faults=0x%04X "
                    "motor_power=%s light=%d getac=%d" % (
                        self.state, self.flags(), self.counted, self.hall_count, self.sensors(),
                        self.faults, self.motor_power, self.head_light, self.getac_enable))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--channel", default="vcan0")
    ap.add_argument("--start-pos-mm", type=float, default=None)
    args = ap.parse_args()
    bus = can.Bus(interface="socketcan", channel=args.channel)
    node = Ctl017Emulator(bus, start_pos_mm=args.start_pos_mm).start()
    try:
        while True:
            print(node.state_line(), flush=True)
            time.sleep(1.0)
    except KeyboardInterrupt:
        pass
    finally:
        node.stop()
        bus.shutdown()


if __name__ == "__main__":
    main()
