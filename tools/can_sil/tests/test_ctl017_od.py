"""CTL-017 emulator against the object dictionary proposal of openamr-upperbody-fw #15.

The emulator follows that proposal pending Evans (TODO(Evans)). These tests pin the layout so a
change to the proposal shows up as a test failure here.
"""
import os
import struct
import subprocess
import sys
import time

import can
import pytest

import ctl017_emulator as c
import ctl017_od_v1
from canopen_common import SdoAbort, SdoClient
from conftest import collect, drain, wait_for

HERE = os.path.dirname(__file__)


@pytest.fixture
def node(make_bus):
    n = c.Ctl017Emulator(make_bus(), start_pos_mm=c.SENSOR_POS_MM[1]).start()  # not operational
    yield n
    n.stop()


def nmt(bus, cmd, node_id=c.NODE_ID):
    bus.send(can.Message(arbitration_id=0, data=[cmd, node_id], is_extended_id=False))


def test_od_table_is_the_generated_one(node):
    assert len(ctl017_od_v1.OD_V1) == 126
    for index, sub, _name, _typ, _acc, _pdo, default in ctl017_od_v1.OD_V1:
        assert (index, sub) in node.od.entries
    assert node.od.get(0x1017, 0) == 100 and node.od.get(0x1016, 1) == 0x0001012C
    assert node.od.get(0x1400, 5) == 300 and node.od.get(0x1018, 2) == 0x11


def test_generator_parses_the_proposal_table_format(tmp_path):
    import gen_ctl017_od as g
    text = ("<!-- BEGIN GENERATED OD TABLE -->\n| Index | Sub |\n|---|---|\n"
            "| 0x2000 | 1 | state | UNSIGNED8 | ro | yes | 0 |\n"
            "| 0x2101 | 9 | duty | UNSIGNED16 | rw (PRE-OP only) |  | 0x03E8 |\n"
            "<!-- END GENERATED OD TABLE -->")
    assert g.parse(text) == [(0x2000, 1, "state", "UNSIGNED8", "ro", True, 0),
                             (0x2101, 9, "duty", "UNSIGNED16", "rw (PRE-OP only)", False, 1000)]


def test_boots_pre_operational_with_boot_up_and_no_pdos(make_bus, vcan):
    master = make_bus()
    n = c.Ctl017Emulator(make_bus())
    drain(master)
    n.start()
    try:
        frames = collect(master, 0.5)
        assert frames[0].arbitration_id == 0x710 and bytes(frames[0].data) == b"\x00"
        assert not [f for f in frames if f.arbitration_id in (c.TPDO1, c.TPDO2)]
        beats = [f for f in frames if f.arbitration_id == 0x710 and f.data[0] == 0x7F]
        assert len(beats) >= 3
        nmt(master, 0x01)
        assert wait_for(lambda: n.nmt_state == 0x05)
        frames = collect(master, 0.3)
        assert {c.TPDO1, c.TPDO2} <= {f.arbitration_id for f in frames}
    finally:
        n.stop()


def test_tpdo2_layout(node, make_bus):
    master = make_bus()
    nmt(master, 0x01)
    assert wait_for(lambda: node.nmt_state == 0x05)
    node.set_estop2(True)
    node.spare_in[1] = True
    node.inject_current(1234)
    drain(master)
    frame = [f for f in collect(master, 0.5) if f.arbitration_id == c.TPDO2][-1]
    inputs, target, current, velocity, faults, aux = struct.unpack("<BBHhBB", bytes(frame.data))
    assert inputs == c.IN_STOP1 | c.IN_ESTOP2 | c.IN_HALL_SUPPLY | (1 << 6)
    assert target == 0xFF and current == 1234 and velocity == 0 and faults == 0 and aux == 0


def test_rpdo2_is_three_bytes_and_drives_aux_flags(node, make_bus):
    master = make_bus()
    nmt(master, 0x01)
    assert wait_for(lambda: node.nmt_state == 0x05)
    master.send(can.Message(arbitration_id=c.RPDO2, data=[1, 40, 1], is_extended_id=False))
    assert wait_for(lambda: node.head_light_on and node.getac_enable)
    drain(master)
    f = [f for f in collect(master, 0.4) if f.arbitration_id == c.TPDO2][-1]
    assert f.data[7] == c.AUX_LIGHT_ON | c.AUX_GETAC and node.head_light_dim == 40
    master.send(can.Message(arbitration_id=c.RPDO2, data=[0, 0], is_extended_id=False))  # short
    time.sleep(0.1)
    assert node.head_light_on == 1 and node.rpdo_len_errors == 1
    sdo = SdoClient(master, c.NODE_ID)
    assert sdo.read(0x2300, 3) == 1


def test_rpdo_ignored_outside_operational(node, make_bus):
    master = make_bus()
    master.send(can.Message(arbitration_id=c.RPDO2, data=[1, 40, 1], is_extended_id=False))
    time.sleep(0.1)
    assert node.head_light_on == 0 and node.getac_enable == 0


def test_sdo_access_rules(node, make_bus):
    master = make_bus()
    sdo = SdoClient(master, c.NODE_ID)
    time.sleep(0.2)  # parameters are refused until standstill is confirmed (#15: 100 ms quiet time)
    assert sdo.read(0x1018, 2) == 0x11
    with pytest.raises(SdoAbort) as e:
        sdo.write(0x2000, 1, 1, 1)  # read only
    assert e.value.code == 0x06010002
    with pytest.raises(SdoAbort) as e:
        sdo.write(0x2010, 2, 1, 1)  # RPDO only: no debug path around the seq rules
    assert e.value.code == 0x08000021
    sdo.write(0x2101, 9, 500, 2)    # PRE-OP write allowed
    assert node.od.get(0x2101, 9) == 500
    with pytest.raises(SdoAbort) as e:
        sdo.write(0x2100, 1, 400000, 4, signed=True)  # inconsistent stop order, reverted
    assert e.value.code == 0x06090030 and node.od.get(0x2100, 1) == 5000
    with pytest.raises(SdoAbort) as e:
        sdo.read(0x2000, 9)
    assert e.value.code == 0x06090011
    nmt(master, 0x01)
    assert wait_for(lambda: node.nmt_state == 0x05)
    with pytest.raises(SdoAbort) as e:
        sdo.write(0x2101, 9, 400, 2)  # parameters: PRE-OPERATIONAL only
    assert e.value.code == 0x08000022
    sdo.write(0x2011, 3, 1, 1)         # aux outputs stay writable
    assert node.getac_enable == 1


def test_no_sdo_in_stopped_and_stop_halts_lift(make_bus, node):
    master = make_bus()
    sdo = SdoClient(master, c.NODE_ID, timeout=0.2)
    nmt(master, 0x02)
    assert wait_for(lambda: node.nmt_state == 0x04)
    with pytest.raises(TimeoutError):
        sdo.read(0x1018, 2)
    nmt(master, 0x80)
    assert wait_for(lambda: node.nmt_state == 0x7F)
    assert sdo.read(0x1018, 2) == 0x11


def test_leaving_operational_stops_a_move(make_bus):
    n = c.Ctl017Emulator(make_bus(), start_pos_mm=c.SENSOR_POS_MM[0], operational=True).start()
    master = make_bus()
    try:
        time.sleep(0.3)
        for _ in range(3):
            master.send(can.Message(arbitration_id=0x701, data=[5], is_extended_id=False))
        master.send(can.Message(arbitration_id=c.RPDO1, data=[1, 1, 2], is_extended_id=False))
        assert wait_for(lambda: n.drive_on)
        nmt(master, 0x80)  # pre-operational
        assert wait_for(lambda: not n.drive_on)
        assert n.faults == 0  # a halt, not a fault
    finally:
        n.stop()


def test_emcy_layout_error_register_and_history(make_bus):
    n = c.Ctl017Emulator(make_bus(), start_pos_mm=c.SENSOR_POS_MM[1], operational=True).start()
    master = make_bus()
    try:
        time.sleep(0.3)
        drain(master)
        n.forced_sensors = 0b101  # F09
        assert wait_for(lambda: n.faults != 0)
        emcy = [f for f in collect(master, 0.3) if f.arbitration_id == c.EMCY][0]
        code, reg, num, mask = struct.unpack("<HBBH", bytes(emcy.data[:6]))
        assert code == 0xFF09 and reg == 0x81 and num == 9 and mask == 0x0100
        assert bytes(emcy.data[6:]) == b"\x00\x00"
        sdo = SdoClient(master, c.NODE_ID)
        assert sdo.read(0x1001, 0) == 0x81 and sdo.read(0x2002, 0) == 0x0100
        assert sdo.read(0x1003, 0) == 1
        assert sdo.read(0x1003, 1) & 0xFFFF == 0xFF09
        assert (sdo.read(0x1003, 1) >> 16) & 0xFF == 9
        assert sdo.read(0x2001, 6) == c.AUX_F09
        sdo.write(0x1003, 0, 0, 1)  # clear history
        assert sdo.read(0x1003, 0) == 0
        n.forced_sensors = 0
        master.send(can.Message(arbitration_id=c.RPDO1, data=[7, c.OP_CLEAR_FAULT, 0],
                                is_extended_id=False))
        assert wait_for(lambda: n.faults == 0)
        assert n.emcy_resets == 1
        assert n.cmd_result == c.R_OK
        assert not n.homed  # F09 invalidated the position: needs homing (D9)
    finally:
        n.stop()


def test_hall_supply_loss_at_standstill_clears_position_without_fault(make_bus):
    n = c.Ctl017Emulator(make_bus(), start_pos_mm=c.SENSOR_POS_MM[1], operational=True).start()
    try:
        time.sleep(0.3)
        assert n.position_valid
        n.set_hall_supply(False)
        assert wait_for(lambda: not n.position_valid)
        assert n.faults == 0 and n.state == c.UNHOMED
    finally:
        n.stop()


def test_core_heartbeat_consumer_is_programmable(make_bus):
    n = c.Ctl017Emulator(make_bus(), start_pos_mm=c.SENSOR_POS_MM[0], operational=True).start()
    master = make_bus()
    try:
        sdo = SdoClient(master, c.NODE_ID)
        time.sleep(0.2)
        nmt(master, 0x80)
        assert wait_for(lambda: n.nmt_state == 0x7F)
        sdo.write(0x1016, 1, (1 << 16) | 100, 4)  # 100 ms instead of 300 ms
        assert n.hbc == "waiting"
        sdo.write(0x1400, 5, 0x0190, 2)           # RPDO1 deadline 400 ms
        nmt(master, 0x01)
        assert wait_for(lambda: n.nmt_state == 0x05)
        master.send(can.Message(arbitration_id=0x701, data=[5], is_extended_id=False))
        assert wait_for(lambda: n.hbc == "active")
        master.send(can.Message(arbitration_id=c.RPDO1, data=[1, 1, 1], is_extended_id=False))
        assert wait_for(lambda: n.faults != 0, 1.0)  # no keep-alive, no heartbeat: F07
        assert n.faults == 1 << (c.F07 - 1)
    finally:
        n.stop()
