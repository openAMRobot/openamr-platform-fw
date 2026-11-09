# SPDX-License-Identifier: MIT
"""End-to-end SIL scenarios (WP sil-chain, scope item 4).

Units: SIL commissioning (boards/host/config/sil_commissioning.conf) uses a
0.1 m wheel radius, so 1 m/s at the wheel = 10 rad/s = 95.49 rpm on 60FFh.
These geometry values are SIL-only, not robot values (TODO(Evans)).
"""
import math
import time

import can
import pytest

from conftest import CAN1, fault_codes

RPM_PER_M_S = 60.0 / (2.0 * math.pi * 0.1)

CMD_TIMEOUT = 18001
HOST_LINK_LOST = 18002
DRIVE_COMM_LOST = 18003
CAN1_BUS_OFF = 18006
BMS_STALE = 18010
BMS_FAULT = 18011
IMU_INVALID = 18012
SAFETY_INHIBIT = 18014
NOT_COMMISSIONED = 18015


def wheels_zero(c):
    return all(abs(r) < 0.5 for r in c.zlac_target_rpm())


# --- default (no override): LOCKED, nothing moves ----------------------------

def test_default_build_stays_locked(chain_factory):
    c = chain_factory(override=False)
    c.wait_status()
    c.drive(0.2, 0.0, 1.5)
    s = c.node.snapshot()
    assert s["safety_state"] == "LOCKED"
    assert s["motion_allowed"] == "false"
    assert SAFETY_INHIBIT in fault_codes(s)
    assert wheels_zero(c)
    # The GPIO override is refused without --sil-safety-override.
    assert c.ctl("safety_ok 1")[0].startswith("error")
    assert c.state()["safety"] == "LOCKED"


def test_not_commissioned_never_moves(chain_factory):
    c = chain_factory(commissioned=False)
    c.wait_status()
    # Full operator sequence, but the incomplete record holds the software
    # stop (oamr_commissioning_sw_stop), so RUN is never reached.
    c.ctl("column released", "estop 0", "safety_ok 0")
    time.sleep(0.4)
    c.ctl("reset 1")
    time.sleep(0.25)
    c.ctl("reset 0", "safety_ok 1")
    time.sleep(0.1)
    c.ctl("sw_stop 0")
    c.drive(0.2, 0.0, 1.0)
    s = c.node.snapshot()
    assert s["safety_state"] == "LOCKED"
    assert int(s["safety_reasons"], 16) & 0x400  # OAMR_SC_R_SOFTWARE_STOP
    assert s["applied_state"] in ("NOT_COMMISSIONED", "INHIBITED")
    assert NOT_COMMISSIONED in fault_codes(s)
    assert s["drive_state"] == "INIT"  # adapter not started: nothing on CAN1
    assert wheels_zero(c)


def test_locked_until_reset_then_run(chain):
    c = chain
    c.drive(0.2, 0.0, 0.5)
    assert c.node.snapshot()["safety_state"] == "LOCKED"
    assert wheels_zero(c)
    c.arm()
    c.wait_for(lambda: c.node.snapshot().get("safety_state") == "RUN", 1.0, what="status RUN")


# --- command path -------------------------------------------------------------

def test_cmd_vel_reaches_wheel_targets_within_2_percent(chain):
    c = chain
    c.arm()
    c.drive(0.2, 0.0, 2.0)
    expected = 0.2 * RPM_PER_M_S  # 19.1 rpm
    # The adapter writes 60FFh sub 1 and sub 2 as two SDOs in one cycle; read
    # the pair while the command is still being sent, once both are updated.
    end = time.monotonic() + 1.0
    while True:
        c.node.cmd(0.2, 0.0)
        left, right = c.zlac_target_rpm()
        if (abs(left) > 0.5 and abs(right) > 0.5) or time.monotonic() > end:
            break
        time.sleep(0.02)
    # 60FFh carries whole rpm (zlac8015d.c rounds); 2 % of 19.1 rpm is below
    # one count, so the bound is max(2 %, half a count).
    tol = max(0.02 * expected, 0.5)
    assert abs(abs(left) - expected) <= tol, (left, expected)
    assert abs(abs(right) - expected) <= tol, (right, expected)
    s = c.node.snapshot()
    assert s["safety_state"] == "RUN"
    assert s["applied_state"] == "OK"
    assert abs(float(s["applied_left_m_s"]) - 0.2) < 1e-3
    assert s["controller_state"] == "READY"
    # The emulated wheels follow and the odometry integrates them.
    # Odometry followed the measured wheel speed (606Ch) while driving.
    assert c.node.odom_max_v > 0.15, c.node.odom_max_v
    o = c.node.odom
    assert o.header.frame_id == "odom" and o.child_frame_id == "base_footprint"
    assert o.pose.pose.position.x > 0.0


def test_stop_cmd_vel_zero_within_200ms_plus_ramp(chain):
    c = chain
    c.arm()
    c.drive(0.2, 0.0, 1.5)
    assert not wheels_zero(c)
    t0 = time.monotonic()
    # No more /cmd_vel: the core times the command out after 200 ms and writes
    # zero at once (motion.h); the adapter sends it on its next SDO cycle.
    c.wait_for(lambda: wheels_zero(c), 1.0, period=0.005, what="zero wheel target")
    t_zero = time.monotonic() - t0
    assert t_zero <= 0.200 + 0.150, t_zero  # 200 ms timeout + one SDO cycle and transport
    # Then the drive's own deceleration ramp (6084h, SIL commissioning 100 ms)
    # brings the wheels to rest.
    c.wait_for(lambda: all(abs(r) < 0.5 for r in c.zlac_rpm()), 0.5, what="wheels at rest")
    s = c.node.snapshot()
    c.wait_for(lambda: c.node.snapshot().get("applied_state") == "TIMEOUT", 0.5, what="TIMEOUT")
    assert CMD_TIMEOUT in fault_codes(c.node.snapshot())


def test_explicit_zero_command_stops(chain):
    c = chain
    c.arm()
    c.drive(0.2, 0.0, 1.0)
    c.drive(0.0, 0.0, 0.3)
    # Zero within one adapter cycle per wheel (two SDOs), without a timeout.
    c.wait_for(lambda: wheels_zero(c), 0.3, what="zero targets")
    assert c.node.snapshot()["applied_state"] == "OK"


def test_commands_above_limit_are_clamped(chain):
    c = chain
    c.arm()
    c.drive(1.2, 0.0, 2.0)
    s = c.node.snapshot()
    assert abs(float(s["applied_left_m_s"]) - 0.5) < 1e-3
    assert abs(float(s["applied_right_m_s"]) - 0.5) < 1e-3
    assert s["applied_clamped"] == "true"
    for r in c.zlac_target_rpm():
        assert abs(r) <= round(0.5 * RPM_PER_M_S) + 0.5
    # Pure rotation is scaled, not cut: both wheels at the limit, opposite signs.
    c.drive(0.0, 5.0, 1.0)
    s = c.node.snapshot()
    assert abs(float(s["applied_left_m_s"]) + 0.5) < 1e-3
    assert abs(float(s["applied_right_m_s"]) - 0.5) < 1e-3


def test_deceleration_respects_1_5_m_s2(chain):
    c = chain
    c.arm()
    c.drive(0.5, 0.0, 1.5)
    # Step the command down to 0.05 m/s (a command, not a timeout: the core
    # ramps it). Sample the applied wheel speed through the control socket.
    samples = []
    end = time.monotonic() + 0.6
    while time.monotonic() < end:
        c.node.cmd(0.05, 0.0)
        st = c.state()
        samples.append((time.monotonic(), float(st["left_m_s"])))
        time.sleep(0.01)
    worst = 0.0
    for (t0, v0), (t1, v1) in zip(samples, samples[1:]):
        if t1 - t0 >= 0.02:
            worst = max(worst, (v0 - v1) / (t1 - t0))
    assert samples[0][1] > 0.4
    assert samples[-1][1] == pytest.approx(0.05, abs=1e-3)
    # 1.5 m/s^2 with margin for sampling jitter of the socket reads.
    assert worst <= 1.5 * 1.15, worst
    # The ramp took about (0.5 - 0.05) / 1.5 = 0.3 s, not one step.
    t_done = next(t for t, v in samples if v <= 0.051) - samples[0][0]
    assert t_done >= 0.2, t_done


# --- host link -------------------------------------------------------------------

def test_agent_killed_base_stops_and_status_goes_stale(chain):
    c = chain
    c.arm()
    # Keep commanding through the whole scenario: the stop must come from
    # the link loss, not from a missing command.
    c.drive(0.2, 0.0, 1.0)
    assert not wheels_zero(c)
    last_status_before = c.node.status_rx
    t_kill = time.monotonic()
    c.kill_agent()
    end = t_kill + 1.2
    while time.monotonic() < end:
        c.node.cmd(0.2, 0.0)  # goes nowhere: the agent is gone
        time.sleep(0.05)
    # Host side: no status for more than the 0.5 s stale time (I8 rev 3).
    assert time.monotonic() - c.node.status_rx > 0.5
    assert c.node.status_rx - t_kill < 0.2 or c.node.status_rx == last_status_before
    # Base side: the host heartbeat stopped; zero wheels, safety LOCKED (the
    # core latches communication loss, E-01 rev 3).
    assert wheels_zero(c)
    st = c.state()
    assert st["safety"] == "LOCKED"
    assert st["applied"] in ("HOST_LOST", "INHIBITED")
    # Restarting the agent reconnects without a base restart; the base stays
    # LOCKED until the operator resets it.
    c.start_agent()
    n0 = c.node.status_count
    c.wait_for(lambda: c.node.status_count > n0, 20.0, what="status after reconnect")
    s = c.node.snapshot()
    assert s["safety_state"] == "LOCKED"
    assert s["host_link_ok"] == "true"
    assert wheels_zero(c)


def test_status_rates(chain):
    c = chain
    time.sleep(3.0)
    with c.node.lock:
        st = [t for t in c.node.status_times if t > time.monotonic() - 2.0]
        aux = [t for t in c.node.aux_times if t > time.monotonic() - 2.0]
        imu = c.node.imu_count
    # 10 Hz heartbeat (plus on-change publishes), never a gap over the 0.5 s stale time.
    assert len(st) >= 18, len(st)
    assert max(b - a for a, b in zip(st, st[1:])) < 0.5
    assert 8 <= len(aux) <= 12, len(aux)  # 5 Hz
    assert max(b - a for a, b in zip(aux, aux[1:])) < 0.6
    assert imu > 0
    assert c.node.aux["estop2_state"] in ("UNKNOWN", "RELEASED", "PRESSED")
    assert c.node.aux["lift_state"] == "UNKNOWN"


# --- drive link ---------------------------------------------------------------------

def test_emulator_stopped_heartbeat_loss_no_motion(chain):
    c = chain
    c.arm()
    c.drive(0.2, 0.0, 1.0)
    assert not wheels_zero(c)
    c.zlac.stop()  # the drive falls silent: no heartbeat, no SDO answers
    end = time.monotonic() + 1.5
    while time.monotonic() < end:
        c.node.cmd(0.2, 0.0)
        time.sleep(0.05)
    s = c.node.snapshot()
    assert DRIVE_COMM_LOST in fault_codes(s), s.get("faults")
    assert s["drive_state"] == "COMM_LOST"
    assert s["applied_state"] in ("DRIVER_NOT_READY", "INHIBITED")
    assert s["motion_allowed"] == "false" or s["applied_state"] != "OK"
    # Last thing the base wrote to the drive before it fell silent may be
    # non-zero; the drive's own 2000h link-loss protection stops it.
    c.zlac.tick(0.0)
    assert c.zlac.link_lost or wheels_zero(c)


def test_vcan_deleted_heartbeat_loss_no_motion(chain):
    from conftest import vcan_delete, vcan_up
    c = chain
    c.arm()
    c.drive(0.2, 0.0, 1.0)
    # Unplug: the drive is gone and so is the interface. The emulator thread
    # is stopped first (its socket would raise ENETDOWN otherwise).
    c.zlac.stop()
    if not vcan_delete(CAN1):
        pytest.skip("cannot delete %s" % CAN1)
    try:
        end = time.monotonic() + 1.5
        while time.monotonic() < end:
            c.node.cmd(0.2, 0.0)
            time.sleep(0.05)
        s = c.node.snapshot()
        assert DRIVE_COMM_LOST in fault_codes(s), s.get("faults")
        assert s["applied_state"] != "OK"
        assert s["drive_ready"] == "false"
    finally:
        vcan_up(CAN1)


# --- CAN bus-off ------------------------------------------------------------------------

def send_error(bus, flags):
    bus.send(can.Message(arbitration_id=flags, is_error_frame=True, is_extended_id=False,
                         data=bytes(8)))


def test_can1_bus_off_reported_and_no_motion(chain):
    c = chain
    c.arm()
    c.drive(0.2, 0.0, 1.0)
    inj = can.Bus(interface="socketcan", channel=CAN1)
    try:
        send_error(inj, 0x040)  # CAN_ERR_BUSOFF
        c.zlac.set_bus_off(True)
        seen = {"fault": False, "zero_cmd": True}
        end = time.monotonic() + 0.6
        while time.monotonic() < end:
            c.node.cmd(0.2, 0.0)
            s = c.node.snapshot()
            if CAN1_BUS_OFF in fault_codes(s):
                seen["fault"] = True
                assert s["applied_state"] != "OK"
            time.sleep(0.03)
        assert seen["fault"]
        st = c.state()
        assert st["applied"] != "OK"
        assert float(st["left_m_s"]) == 0.0
        # Controller restart (CAN_ERR_RESTARTED) and the drive back on the bus.
        send_error(inj, 0x100)
        c.zlac.set_bus_off(False)
        c.wait_for(lambda: CAN1_BUS_OFF not in fault_codes(c.node.snapshot()), 3.0,
                   what="bus-off cleared")
        assert int(c.node.snapshot()["can1_bus_off_total"]) >= 1
    finally:
        inj.shutdown()


# --- BMS and IMU -------------------------------------------------------------------

def test_bms_stale_fault_and_no_motion(chain):
    c = chain
    c.arm()
    c.drive(0.2, 0.0, 0.5)
    c.bms.silent = True
    end = time.monotonic() + 3.6  # stale after 3 s (daly_bms.c, safety_core.c)
    while time.monotonic() < end:
        c.node.cmd(0.2, 0.0)
        time.sleep(0.05)
    s = c.node.snapshot()
    assert BMS_STALE in fault_codes(s)
    assert s["bms_fresh"] == "false"
    assert s["safety_state"] == "LOCKED"  # E-01 rev 3: BMS stale inhibits
    assert wheels_zero(c)


def test_bms_level2_alarm_fault_and_no_motion(chain):
    c = chain
    c.arm()
    c.drive(0.2, 0.0, 0.5)
    c.bms.inject_fault(True)
    end = time.monotonic() + 1.5
    while time.monotonic() < end:
        c.node.cmd(0.2, 0.0)
        time.sleep(0.05)
    s = c.node.snapshot()
    assert BMS_FAULT in fault_codes(s)
    assert s["safety_state"] == "LOCKED"
    assert wheels_zero(c)


def test_imu_invalid_sample_reported_never_published(chain):
    c = chain
    c.arm()
    c.wait_for(lambda: c.node.imu_count > 10, 3.0, what="imu samples")
    c.ctl("imu invalid 1")
    time.sleep(0.3)
    n0 = c.node.imu_count
    c.drive(0.2, 0.0, 1.0)
    s = c.node.snapshot()
    assert IMU_INVALID in fault_codes(s)
    assert int(s["imu_invalid_samples"]) > 0
    # Invalid samples are never published as data (I8 5.6).
    assert c.node.imu_count - n0 <= 1
    # The design does not make the IMU a motion inhibit (E-01 rev 3): reported
    # only, the command still applies. TODO(Evans): rule whether it should.
    assert s["applied_state"] == "OK"
    c.ctl("imu invalid 0")
    c.wait_for(lambda: IMU_INVALID not in fault_codes(c.node.snapshot()), 2.0, what="imu ok")
