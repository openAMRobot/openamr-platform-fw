"""Scenarios of the read-only chain proof (integration, do not merge).

Each test runs the whole chain on simulated time and checks the outcome at the
host gate of platform-sw #137, plus two invariants on every run:
  * nothing but SDO upload requests ever left the controller side;
  * odometry never integrated a stale or invalid wheel sample.
"""
import struct

import pytest

import cosim

T0 = cosim.Run.T0


def sdo_obj(data):
    return struct.unpack_from("<HB", data, 1)


def check_invariants(r):
    assert not r.errors, r.errors
    for f in r.tx:
        data = bytes.fromhex(f["data"])
        assert f["id"] == 0x600 + r.params["node_id"] and data[0] == 0x40, f
    max_age = r.params["odom_max_age_us"]
    for e in r.odom_events:
        if e["result"] == 0:
            assert e["v1"] == "VALID" and e["v2"] == "VALID", e
            assert e["fresh1"] and e["fresh2"], e
            assert e["age1_us"] <= max_age and e["age2_us"] <= max_age, e
    # one record per period, sequence +1 inside a boot
    for a, b in zip(r.records, r.records[1:]):
        if a["boot_id"] == b["boot_id"] and b["sequence"] != 0:
            assert b["monotonic_us"] - a["monotonic_us"] == r.params["status_period_us"] or \
                b["monotonic_us"] < a["monotonic_us"]
    # BMS never anything but UNKNOWN (Daly protocol unconfirmed)
    for rec in r.records:
        assert sub(rec, 4)["state"] == "UNKNOWN"


def sub(rec, sid):
    return next(s for s in rec["subsystems"] if s["id"] == sid)


def run(sil_bin, core, hooks=(), **kw):
    hooks = [(0, lambda r: r.set_wheels_rpm(30, -30))] + list(hooks)
    r = cosim.Run(sil_bin, core, hooks=hooks, **kw).run()
    check_invariants(r)
    return r


def ends_not_ready(r):
    assert r.final.locked, r.final


def test_nominal(sil_bin, gate_core):
    r = run(sil_bin, gate_core)
    assert r.resume_result[0], r.resume_result
    assert not r.final.locked and r.final.blockers == ()
    last = r.records[-1]
    assert last["state"] == "READY"
    assert last["odom"]["steps"] > 100 and last["odom"]["rejects"] == 0
    # 30 and -30 r/min with channel 1 = left, both signs +1: turning on the spot
    assert abs(last["odom"]["v_m_s"]) < 1e-6
    assert last["meas"]["sdo_unsolicited"] == 0
    m = r.measurements()
    assert all(p["n"] > 100 for p in m["wheel_refresh"])
    assert m["bus_load_percent"] < 50.0


def test_one_wheel_times_out(sil_bin, gate_core):
    def start(r):
        r.drop_response = lambda d: sdo_obj(d) == (0x606C, 2)
    r = run(sil_bin, gate_core, hooks=[(2_000_000, start)])
    ends_not_ready(r)
    last = r.records[-1]
    assert last["wheel2"]["validity"] != "VALID"
    assert sub(last, 2)["state"] == "STALE" and sub(last, 2)["detail"] == "channel2_not_fresh"
    # A response already on the wire at the switch may still land; after that,
    # no integration with one wheel.
    settle = T0 + 2_000_000 + r.params["sdo_timeout_us"]
    assert not [e for e in r.odom_events if e["t_us"] > settle and e["result"] == 0]


def test_heartbeat_stops(sil_bin, gate_core):
    def stop(r):
        r.emu.heartbeat_enabled = False
    r = run(sil_bin, gate_core, hooks=[(2_000_000, stop)])
    ends_not_ready(r)
    last = r.records[-1]
    assert sub(last, 1)["state"] == "STALE" and last["meas"]["hb_lost_events"] == 1
    assert any(f["code"] == 18003 and f["lifecycle"] == "LATCHED" for f in last["faults"])


def test_sdo_abort(sil_bin, gate_core):
    def abort(r):
        def m(d):
            if sdo_obj(d) == (0x6041, 0):
                return bytes([0x80]) + d[1:4] + struct.pack("<I", 0x06020000)
            return d
        r.mutate_response = m
    r = run(sil_bin, gate_core, hooks=[(2_000_000, abort)])
    ends_not_ready(r)
    last = r.records[-1]
    assert sub(last, 1)["state"] == "STALE" and sub(last, 1)["detail"] == "READ_FAILED"
    assert any(f["code"] == 18005 for f in last["faults"])


def test_late_responses(sil_bin, gate_core):
    def late(r):
        r.delay_response_us = 25_000  # beyond the 20 ms SDO timeout
    r = run(sil_bin, gate_core, hooks=[(2_000_000, late)])
    ends_not_ready(r)
    last = r.records[-1]
    assert last["meas"]["sdo_unsolicited"] + last["meas"]["sdo_wrong_object"] > 0
    assert sub(last, 2)["state"] != "OK"


def test_duplicated_responses_are_tolerated(sil_bin, gate_core):
    """Duplicates alone are not a failure: they are counted and dropped, the
    values stay right and the gate stays released. Recorded as a deviation
    from 'every failure case ends not ready' in the document."""
    def dup(r):
        r.duplicate_responses = True
    r = run(sil_bin, gate_core, hooks=[(0, dup)])
    last = r.records[-1]
    assert last["meas"]["sdo_unsolicited"] + last["meas"]["sdo_wrong_object"] > 100
    assert last["wheel1"]["raw"] == 300 and last["wheel2"]["raw"] == -300
    assert not r.final.locked


def test_late_and_duplicated(sil_bin, gate_core):
    def both(r):
        r.delay_response_us = 25_000
        r.duplicate_responses = True
    r = run(sil_bin, gate_core, hooks=[(2_000_000, both)])
    ends_not_ready(r)


def test_can_silent(sil_bin, gate_core):
    def silent(r):
        r.emu.bus_off = True
    r = run(sil_bin, gate_core, hooks=[(2_000_000, silent)])
    ends_not_ready(r)
    last = r.records[-1]
    assert sub(last, 1)["state"] == "STALE" and sub(last, 2)["state"] == "STALE"


def test_emulator_restarts(sil_bin, gate_core):
    def restart(r):
        r.emu.power_cycle()     # sends the boot-up frame, speeds back to zero
    r = run(sil_bin, gate_core, hooks=[(2_000_000, restart)])
    ends_not_ready(r)
    last = r.records[-1]
    assert last["meas"]["drive_bootups"] == 1
    assert sub(last, 1)["detail"] == "drive_restarted"
    assert any(f["code"] == 18003 and f["lifecycle"] == "LATCHED" for f in last["faults"])


def test_controller_restart_with_latched_fault(sil_bin, gate_core):
    def hb_off(r):
        r.emu.heartbeat_enabled = False
    def hb_on(r):
        r.emu.heartbeat_enabled = True
    def reboot(r):
        r.chain_cmd("reboot 777 %d" % r.now)
    r = run(sil_bin, gate_core, resume_at_us=None,
            hooks=[(800_000, hb_off), (1_400_000, hb_on), (1_800_000, reboot)])
    before = [x for x in r.records if x["boot_id"] != 777]
    after = [x for x in r.records if x["boot_id"] == 777]
    assert any(f["code"] == 18003 and f["lifecycle"] == "LATCHED" for f in before[-1]["faults"])
    assert after[0]["sequence"] == 0 and after[-1]["faults"] == []
    assert r.gate.restarts >= 1
    # The new boot no longer reports the fault; the gate carries it until Resume.
    assert 18003 in r.gate.carried_faults
    ends_not_ready(r)
    ok, msg = r.gate.resume((T0 + r.duration_us + 1_000) / 1e6)
    assert ok and "18003" in msg


@pytest.mark.xfail(strict=True, reason="finding F-GATE-1: platform-sw #137 counts a forward "
                   "sequence gap (BaseGate.sequence_gaps) but does not block on it")
def test_sequence_gap_ends_not_ready(sil_bin, gate_core):
    r = run(sil_bin, gate_core, hooks=[(2_000_000, lambda r: r.chain_cmd("seqskip 5"))])
    assert r.gate.sequence_gaps == 1
    ends_not_ready(r)


def test_sequence_gap_is_seen(sil_bin, gate_core):
    r = run(sil_bin, gate_core, hooks=[(2_000_000, lambda r: r.chain_cmd("seqskip 5"))])
    seqs = [x["sequence"] for x in r.records]
    assert any(b - a == 6 for a, b in zip(seqs, seqs[1:]))
    assert r.gate.sequence_gaps == 1 and r.gate.restarts == 0


def test_tof_unplugged(sil_bin, gate_core):
    def unplug(r):
        r.tfl_alive[1] = False
    r = run(sil_bin, gate_core, hooks=[(2_000_000, unplug)])
    ends_not_ready(r)
    last = r.records[-1]
    assert sub(last, 10)["state"] == "STALE" and sub(last, 9)["state"] == "OK"
    assert last["state"] == "DEGRADED"


def test_ultrasonic_no_target_and_malformed_at_power_up(sil_bin, gate_core):
    # unit 0: two malformed answers at power-up, then good; unit 1: always no target
    r = run(sil_bin, gate_core, resume_at_us=150_000,
            hooks=[(0, lambda r: r.mb_text.update({
                0: lambda n: b"R1x3\r" if n < 2 else b"R123\r",
                1: lambda n: b"R765\r"}))])
    assert r.resume_result[0] is False
    early = r.records[0]
    assert sub(early, 5)["state"] == "UNKNOWN"
    last = r.records[-1]
    assert sub(last, 5)["state"] == "OK"
    assert sub(last, 6)["state"] == "OK" and sub(last, 6)["detail"] == "no_target"
    ends_not_ready(r)   # the refused resume was never repeated: the latch holds


def test_imu_identity_mismatch(sil_bin, gate_core):
    r = run(sil_bin, gate_core, imu_who=0x00)
    ends_not_ready(r)
    last = r.records[-1]
    assert sub(last, 3)["state"] == "DEGRADED" and sub(last, 3)["detail"] == "wrong_who_am_i"
    assert last["state"] == "FAULTED"


def test_host_clock_steps_back(sil_bin, gate_core):
    def step(r):
        r.host_offset_us = -1_000_000
    r = run(sil_bin, gate_core, hooks=[(2_000_000, step)])
    ends_not_ready(r)
    after = [s for s in r.gate_states if s[0] >= T0 + 2_000_000]
    assert after[0][1] and "STATUS_STALE" in [b.value for b in after[0][2]]


def test_status_stream_stops(sil_bin, gate_core):
    def stop(r):
        r.status_delivery = False
    r = run(sil_bin, gate_core, hooks=[(2_000_000, stop)])
    ends_not_ready(r)
    stale = [t for t, _, b in r.gate_states if "STATUS_STALE" in [x.value for x in b]]
    # the hook runs before the step at 2.0 s, so the last delivered record is the one before
    last_record = max(x["monotonic_us"] for x in r.records if x["monotonic_us"] < T0 + 2_000_000)
    first_stale = min(stale) - (last_record + cosim.SIM["host_delay_us"])
    assert 500_000 - cosim.SIM["step_us"] <= first_stale <= 500_000 + cosim.SIM["step_us"]


def test_retained_message_only(sil_bin, gate_core):
    r = run(sil_bin, gate_core, hooks=[(0, lambda r: setattr(r, "retained_only", True))])
    assert r.delivered_status == 1 and r.delivered_aux == 1
    assert r.resume_result[0] is False
    ends_not_ready(r)


def test_recorded_trace_replay(sil_bin, gate_core, tmp_path):
    """Same chain fed from a candump -L trace of a nominal run instead of the emulator."""
    import can_trace
    rec = run(sil_bin, gate_core, duration_us=1_500_000)
    path = tmp_path / "nominal.log"
    can_trace.write_trace(str(path), rec.trace)
    frames, _ = can_trace.read_trace(str(path))
    t_first = frames[0].t
    for f in frames:
        f.t = f.t - t_first + T0 / 1e6
    r = run(sil_bin, gate_core, duration_us=1_500_000, replay=frames)
    assert sub(r.records[-1], 1)["state"] == "OK"
    assert r.records[-1]["wheel1"]["raw"] == 300
