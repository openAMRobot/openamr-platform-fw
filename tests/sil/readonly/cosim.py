"""Co-simulator of the read-only chain (integration proof, do not merge).

Runs, in one process and on simulated time:

  ZLAC8015D emulator (tools/can_sil, platform-fw #39 line, unchanged)
    -> CAN1 frames with a modelled bus time and drive response time
    -> oamr_sil_readonly (C: #49 CANopen client and decoders, #48 odometry,
       sensors and freshness, compiled unchanged)
    -> status records as JSON lines
    -> BaseGate of platform-sw #137 (base_status_core.py, imported unchanged from
       a checkout outside this repository)

Nothing here commands the drive. The emulator is never sent a write, an NMT
command or a PDO: the only frames that reach it are the SDO upload requests the
C scheduler produces, and every one is checked to be an upload request. Wheel
speeds are placed in the emulator's object dictionary by the scenario (test
oracle), the same way a recorded trace would carry them.
"""
import json
import math
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools", "can_sil"))

import can  # noqa: E402  (python-can, used by the emulator)
import can_trace  # noqa: E402
import zlac8015d_emulator as zlac  # noqa: E402

CAN1_BITRATE = 500_000

# Scenario parameters. None of these is a drive or robot fact; each is a
# simulation choice or an owner proposal, named so the owner can replace it.
BASE_PARAMS = {
    "node_id": 1,                    # P-03: ZLAC8015D node 1 on CAN1
    "sdo_timeout_us": 20_000,        # TODO(Evans): per-request SDO timeout
    "sdo_retries": 1,                # TODO(Evans)
    "sched_tick_us": 1_000,          # TODO(Evans): scheduler tick on the STM32
    "hb_timeout_us": 300_000,        # D-24 proposal for the base-side consumer, not decided
    "tlm_max_age_us": 50_000,        # TODO(Evans): tie to the measured cycle time
    "resolution_required": 1,        # D-24: 2026h:05 must read 1
    "wheel_radius_m": 0.100,         # TODO(Evans): supplier radius 0.100 m, 0.098 loaded
    "track_width_m": 0.40,           # TODO(Evans): DRIVE-TRACK open; simulation value only
    "counts_per_rev": 4096,          # TODO(Evans): unused on the speed path; simulation value
    "max_count_delta": 100_000,      # TODO(Evans): unused on the speed path
    "speed_rad_s_per_unit": 2 * math.pi / 60 / 10,  # emulator convention 0.1 r/min; TODO(Evans)
    "left_channel": 1,               # TODO(Evans): wheels-up test; no left/right meaning known
    "left_sign": 1,                  # TODO(Evans)
    "right_sign": 1,                 # TODO(Evans)
    "odom_max_age_us": 50_000,       # TODO(Evans)
    "status_period_us": 100_000,     # I8 frozen: /base/status 10 Hz
    "aux_period_us": 200_000,        # I8 frozen: /base/aux_status 5 Hz
    "contract_version": 1,           # interfaces #38 CONTRACT_VERSION=1
    "boot_id": 0x1234ABCD,
    "operating_state": 2,            # RUN; scenario input standing in for the owner's state machine
    "bms_required": 0,               # TODO(Evans): BMS in READY once the Daly protocol is confirmed
    "tfl_unit": 1,                   # TF-Luna factory default 9-byte/cm
    "tfl_stale_us": 100_000,         # TODO(Evans)
    "mb_stale_us": 600_000,          # TODO(Evans): four sensors in sequence, about 99 ms each
    "mb_timeout_us": 150_000,        # TODO(Evans)
    "mb_gap_us": 0,                  # TODO(Evans)
    "imu_gyro_fs": 2,                # TODO(Evans): 1000 dps, simulation choice
    "imu_accel_fs": 3,               # TODO(Evans): 4 g, simulation choice
    "imu_odr": 7,                    # 200 Hz code; TODO(Evans)
    "imu_stale_us": 50_000,          # TODO(Evans)
    "daly_bms_addr": 0x01,           # TODO(Evans): Daly V1.0 document value, unconfirmed
    "daly_host_addr": 0x40,          # TODO(Evans): Daly V1.0 document value, unconfirmed
    "daly_cells": 8,                 # TODO(Evans): 8S
    "daly_temps": 2,                 # TODO(Evans)
}

SIM = {
    "step_us": 500,                  # simulation resolution
    "drive_response_us": 1_000,      # TODO(Evans): SDO response time of the real drive, from a capture
    "host_delay_us": 2_000,          # transport to the host; Ethernet/UDP not modelled
    "mb_reading_us": 99_000,         # pin map: about 99 ms per MB7060 reading
    "tfl_period_us": 10_000,         # TF-Luna default 100 Hz
    "imu_period_us": 5_000,          # 200 Hz
    "daly_period_us": 1_000_000,
}


def frame_bits(dlc, extended=False, worst_stuffing=False):
    """Bits on the wire for a classic data frame, with 3 bits interframe space."""
    base = (67 if extended else 47) + 8 * dlc
    if worst_stuffing:
        stuffable = (54 if extended else 34) + 8 * dlc
        base += (stuffable - 1) // 4
    return base


def frame_time_us(dlc, extended=False):
    return frame_bits(dlc, extended) * 1e6 / CAN1_BITRATE


class _CaptureBus:
    """Stand-in for a python-can bus: the emulator's sends land in a list."""

    def __init__(self):
        self.sent = []

    def send(self, msg):
        self.sent.append(msg)


def tfl_frame(dist_cm, amp=1000, temp_raw=2200):
    b = struct.pack("<BBHHH", 0x59, 0x59, dist_cm, amp, temp_raw)
    return b + bytes([sum(b) & 0xFF])


def imu_bytes(t_us):
    # temperature, accel xyz, gyro xyz, big endian, never the invalid code
    vals = [100, 0, 0, 8192, int(50 * math.sin(t_us * 1e-6)), 0, 0]
    return b"".join(struct.pack(">h", v) for v in vals)


class Chain:
    """The C program in lock step: one command, then read until 'done'."""

    def __init__(self, binary):
        self.p = subprocess.Popen([binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  text=True, bufsize=1)

    def cmd(self, line):
        self.p.stdin.write(line + "\n")
        self.p.stdin.flush()
        out = []
        while True:
            raw = self.p.stdout.readline()
            if not raw:
                raise RuntimeError("chain program exited (sanitizer report?) after %r" % line)
            obj = json.loads(raw)
            if obj["type"] == "done":
                return out
            out.append(obj)

    def close(self):
        self.p.stdin.close()
        rc = self.p.wait(timeout=30)
        self.p.stdout.close()
        if rc != 0:
            raise RuntimeError("chain program exit code %d" % rc)


def snapshot_from_record(core, rec):
    """Status record (JSON) -> BaseSnapshot of platform-sw #137, field by field.

    The only translation is the lifecycle of a fault: #137 wants `active` and
    `latched` booleans, interfaces #38 carries `lifecycle` and `condition_active`.
    A LATCHED entry is treated as active here (it still inhibits motion).
    """
    faults = tuple(core.Fault(code=f["code"], severity="FAULT",
                              motion_inhibit=bool(f["inhibits_motion"]),
                              active=f["lifecycle"] in ("ACTIVE", "LATCHED"),
                              latched=f["lifecycle"] == "LATCHED")
                   for f in rec["faults"])
    return core.BaseSnapshot(contract_version=rec["contract_version"], boot_id=rec["boot_id"],
                             sequence=rec["sequence"], controller_state=rec["state"],
                             safety_state=rec["operating_state"],
                             recovery_required=rec["recovery"] == "REQUIRED", faults=faults)


def aux_from_record(core, rec):
    return core.AuxSnapshot(contract_version=rec["contract_version"], boot_id=rec["boot_id"],
                            sequence=rec["sequence"])


class Run:
    """One scenario run. Hooks are (time_us, callable(run)) pairs."""

    T0 = 1_000_000

    def __init__(self, binary, core, params=None, hooks=(), duration_us=3_500_000,
                 resume_at_us=1_500_000, imu_who=0x47, replay=None):
        self.core = core
        self.params = dict(BASE_PARAMS, **(params or {}))
        self.hooks = sorted(hooks, key=lambda h: h[0])
        self.duration_us = duration_us
        self.resume_at_us = resume_at_us
        self.replay = replay            # list of can_trace.Frame to feed instead of the emulator
        self.chain = Chain(binary)
        self.gate = core.BaseGate(require_aux=True, supported_versions=(1,))
        self.capture = _CaptureBus()
        self.emu = zlac.Zlac8015dEmulator(self.capture, node_id=self.params["node_id"])
        self.emu.rpm = [0.0, 0.0]
        self.emu.tick(0.0)
        # bus model
        self.bus_free_us = 0.0
        self.bus_busy_us = 0.0
        self.bus_frames = 0
        self.in_flight = []             # (deliver_us, can_trace.Frame, from_host)
        self.trace = []                 # every CAN1 frame as delivered, candump -L
        # failure switches used by hooks
        self.drop_response = lambda data: False
        self.mutate_response = lambda data: data
        self.delay_response_us = 0
        self.duplicate_responses = False
        self.tfl_alive = [True, True]
        self.mb_text = {}               # unit -> callable(n) -> bytes for its n-th reading
        self.status_delivery = True
        self.retained_only = False
        self.host_offset_us = 0
        self.imu_who = imu_who
        # outputs
        self.records, self.aux, self.odom_events, self.tx, self.errors = [], [], [], [], []
        self.host_q = []                # (deliver_us, kind, record)
        self.mb_pending = []            # (due_us, unit)
        self.mb_count = [0, 0, 0, 0]
        self.gate_states = []
        self.resume_result = None
        self.delivered_status = 0
        self.delivered_aux = 0

    # ---- bus ------------------------------------------------------------
    def put_on_bus(self, t_us, arb_id, data, from_host):
        start = max(float(t_us), self.bus_free_us)
        dur = frame_time_us(len(data))
        self.bus_free_us = start + dur
        self.bus_busy_us += dur
        self.bus_frames += 1
        self.in_flight.append((start + dur, can_trace.Frame(0, "can1", arb_id, data), from_host))

    def emulator_send_out(self, t_us):
        sent, self.capture.sent = self.capture.sent, []
        for m in sent:
            data = bytes(m.data)
            if 0x580 <= m.arbitration_id <= 0x5FF:
                if self.drop_response(data):
                    continue
                data = self.mutate_response(data)
                due = t_us + SIM["drive_response_us"] + self.delay_response_us
                self.put_on_bus(due, m.arbitration_id, data, False)
                if self.duplicate_responses:
                    self.put_on_bus(due, m.arbitration_id, data, False)
            else:
                self.put_on_bus(t_us, m.arbitration_id, data, False)

    def to_emulator(self, frame):
        # Read-only proof: the only frame type that may reach the drive.
        assert frame.arb_id == 0x600 + self.params["node_id"], frame
        assert len(frame.data) == 8 and frame.data[0] == 0x40, frame
        self.emu._handle(can.Message(arbitration_id=frame.arb_id, data=frame.data,
                                     is_extended_id=False))

    def chain_cmd(self, line):
        """Hook helper: a command to the chain at the current time."""
        self._collect(self.chain.cmd(line), self.now)

    # ---- one run ----------------------------------------------------------
    def set_wheels_rpm(self, rpm1, rpm2):
        """Test oracle: the actual speed the emulator will report on 606Ch:01/02."""
        self.emu.rpm = [float(rpm1), float(rpm2)]
        self.emu.tick(0.0)

    def _deliver_host(self, now_host):
        keep = []
        for due, kind, rec in self.host_q:
            if due > now_host:
                keep.append((due, kind, rec))
                continue
            if kind == "status":
                self.delivered_status += 1
                self.gate.on_status(snapshot_from_record(self.core, rec), due / 1e6)
            else:
                self.delivered_aux += 1
                self.gate.on_aux(aux_from_record(self.core, rec), due / 1e6)
        self.host_q = keep

    def _collect(self, outs, t_us):
        for o in outs:
            kind = o["type"]
            if kind == "tx":
                data = bytes.fromhex(o["data"])
                self.tx.append(o)
                self.put_on_bus(t_us, o["id"], data, True)
            elif kind in ("status", "aux"):
                (self.records if kind == "status" else self.aux).append(o)
                # retained_only: the host sees one message per topic, the one a
                # late-joining subscriber would get from a retained publisher.
                n_kind = len(self.records) if kind == "status" else len(self.aux)
                deliver = self.status_delivery and not (self.retained_only and n_kind > 1)
                if deliver:
                    self.host_q.append((t_us + SIM["host_delay_us"] + self.host_offset_us, kind, o))
            elif kind == "odom":
                o["t_us"] = t_us
                self.odom_events.append(o)
            elif kind == "mb_trigger":
                self.mb_pending.append((t_us + SIM["mb_reading_us"], o["unit"]))
            elif kind == "error":
                self.errors.append(o)

    def run(self):
        try:
            return self._run()
        except BaseException:
            self.chain.p.kill()
            self.chain.p.wait()
            self.chain.p.stdin.close()
            self.chain.p.stdout.close()
            raise

    def _run(self):
        c = self.chain
        for k, v in self.params.items():
            assert not c.cmd("set %s %r" % (k, float(v))), k
        c.cmd("imu_who %02X" % self.imu_who)
        self._collect(c.cmd("start %d" % self.T0), self.T0)
        hb_period = None
        last_hb = -1e18
        next_tfl = next_imu = next_daly = self.T0
        hooks = list(self.hooks)
        t = self.T0
        end = self.T0 + self.duration_us
        replay = list(self.replay or [])
        while t <= end:
            self.now = t
            while hooks and hooks[0][0] + self.T0 <= t:
                hooks.pop(0)[1](self)
            # drive side: heartbeat in simulated time
            if self.replay is None:
                hb_period = self.emu.heartbeat_ms * 1000
                if (self.emu.heartbeat_enabled and not self.emu.bus_off and hb_period > 0 and
                        t - last_hb >= hb_period):
                    last_hb = t
                    self.put_on_bus(t, 0x700 + self.emu.node_id, bytes([self.emu.nmt_state]), False)
                self.emulator_send_out(t)
            else:
                while replay and replay[0].t * 1e6 <= t:
                    f = replay.pop(0)
                    if not (0x600 <= f.arb_id <= 0x67F):   # host frames of the trace are not replayed
                        self.put_on_bus(t, f.arb_id, f.data, False)
            # deliver frames whose transmission has ended
            due = [x for x in self.in_flight if x[0] <= t]
            self.in_flight = [x for x in self.in_flight if x[0] > t]
            for _, frame, from_host in sorted(due, key=lambda x: x[0]):
                frame.t = t / 1e6
                self.trace.append(frame)
                if from_host:
                    if self.replay is None and not self.emu.bus_off:
                        self.to_emulator(frame)
                else:
                    self._collect(c.cmd("can1 %03X %s" % (frame.arb_id, frame.data.hex())), t)
            # sensors
            if t >= next_tfl:
                next_tfl += SIM["tfl_period_us"]
                for u in (0, 1):
                    if self.tfl_alive[u]:
                        c.cmd("tfl %d %s" % (u, tfl_frame(35 if u == 0 else 40).hex()))
            for item in [x for x in self.mb_pending if x[0] <= t]:
                self.mb_pending.remove(item)
                unit = item[1]
                text = self.mb_text.get(unit, lambda n: b"R123\r")(self.mb_count[unit])
                self.mb_count[unit] += 1
                if text:
                    c.cmd("mb %d %s" % (unit, text.hex()))
            if t >= next_imu:
                next_imu += SIM["imu_period_us"]
                c.cmd("imu %s" % imu_bytes(t).hex())
            if t >= next_daly:
                next_daly += SIM["daly_period_us"]
                # A Daly 0x90 answer as the V1.0 document lays it out; receive only.
                c.cmd("can2 18904001 %s" % bytes([0x01, 0x00, 0, 0, 0x75, 0x30, 0x03, 0xE8]).hex())
            self._collect(c.cmd("t %d" % t), t)
            now_host = t + self.host_offset_us
            self._deliver_host(now_host)
            if self.resume_at_us is not None and self.resume_result is None and \
                    t >= self.T0 + self.resume_at_us:
                self.resume_result = self.gate.resume(now_host / 1e6)
            st = self.gate.evaluate(now_host / 1e6)
            self.gate_states.append((t, st.locked, st.blockers))
            t += SIM["step_us"]
        c.close()
        self.final = self.gate.evaluate((t + self.host_offset_us) / 1e6)
        return self

    # ---- measurements ------------------------------------------------------
    def measurements(self):
        last = self.records[-1]["meas"] if self.records else {}
        dur_s = self.duration_us / 1e6
        periods = []
        for i in (0, 1):
            n = last.get("wheel_period_n", [0, 0])[i]
            periods.append({
                "mean_us": (last["wheel_period_sum_us"][i] / n) if n else None,
                "max_us": last.get("wheel_period_max_us", [0, 0])[i] if n else None,
                "n": n})
        ages = []
        for r in self.records:
            for s in r["subsystems"]:
                if s["data_age_us"] is not None and s["state"] == "OK":
                    ages.append((s["id"], s["data_age_us"]))
        worst = {}
        for sid, a in ages:
            worst[sid] = max(worst.get(sid, 0), a)
        worst_bits = max((frame_bits(8, worst_stuffing=True) for _ in [0]), default=0)
        return {
            "wheel_refresh": periods,
            "bus_load_percent": 100.0 * self.bus_busy_us / (dur_s * 1e6),
            "bus_frames_per_s": self.bus_frames / dur_s,
            "worst_case_frame_us_stuffed": worst_bits * 1e6 / CAN1_BITRATE,
            "worst_age_us_by_subsystem": worst,
            "pair_skew_max_us": last.get("pair_skew_max_us"),
            "odom_steps": self.records[-1]["odom"]["steps"] if self.records else 0,
        }
