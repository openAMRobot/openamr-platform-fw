"""Run the tools/bench scripts against the emulators on vcan."""
import json
import os
import re
import shutil
import subprocess
import sys

import pytest

import ctl017_emulator as c
from zlac8015d_emulator import Zlac8015dEmulator

BENCH = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "bench"))


sys.path.insert(0, BENCH)
BENCH_FLAG = "--i-am-at-the-bench"


def run(script, *args, timeout=30):
    return subprocess.run([sys.executable, os.path.join(BENCH, script), *args],
                          capture_output=True, text=True, timeout=timeout,
                          env={**os.environ, "PYTHONPATH": BENCH})


@pytest.fixture
def zlac(make_bus):
    node = Zlac8015dEmulator(make_bus(), node_id=1).start()
    yield node
    node.stop()


def test_nmt_scan_finds_both_nodes(vcan, zlac, make_bus):
    ctl = c.Ctl017Emulator(make_bus()).start()
    try:
        r = run("nmt_scan.py", vcan, "--expect", "1", "--expect", "0x10",
                "--expect-period-ms", "100", "--listen", "1.5")
    finally:
        ctl.stop()
    assert r.returncode == 0, r.stdout + r.stderr
    assert "node 0x01" in r.stdout and "node 0x10" in r.stdout


def test_nmt_scan_fails_when_node_missing(vcan, zlac):
    r = run("nmt_scan.py", vcan, "--expect", "2", "--listen", "0.6")
    assert r.returncode == 1 and "not heard" in r.stdout


def test_read_faults_clean_then_faulted(vcan, zlac):
    r = run("zlac_read_faults.py", vcan)
    assert r.returncode == 0, r.stdout
    assert "link-loss protection OFF" in r.stdout
    zlac.inject_over_current("high")
    r = run("zlac_read_faults.py", vcan)
    assert r.returncode == 1
    assert "603Fh[31:16]: over-current" in r.stdout


def test_read_faults_no_drive(vcan):
    r = run("zlac_read_faults.py", vcan)
    assert r.returncode == 2


def test_spin_refuses_without_confirmation(vcan, zlac):
    r = run("zlac_spin.py", vcan, BENCH_FLAG, "--wheel-diameter-m", "0.2", "--duration", "1")
    assert r.returncode != 0 and "wheels-up-confirmed" in r.stderr
    assert zlac.od.get(0x6040, 0) == 0  # nothing was sent to the drive


def test_spin_refuses_without_wheel_diameter(vcan, zlac):
    r = run("zlac_spin.py", vcan, BENCH_FLAG, "--wheels-up-confirmed", "--duration", "1")
    assert r.returncode != 0 and "wheel-diameter" in r.stderr
    assert zlac.od.get(0x6040, 0) == 0


def test_spin_caps_speed_between_cap_and_refusal_limit(vcan, zlac):
    d = 0.2
    r = run("zlac_spin.py", vcan, BENCH_FLAG, "--wheels-up-confirmed", "--wheel-diameter-m", str(d),
            "--speed", "0.15", "--duration", "2.5")
    assert "capped" in r.stderr
    assert r.returncode == 0, r.stdout + r.stderr
    cap_rpm = 0.1 / (3.14159265 * d) * 60
    assert zlac.od.get(0x60FF, 1) == 0  # stop written at the end
    assert zlac.od.get(0x6040, 0) == 0x06
    assert f"commanded {round(cap_rpm)} r/min" in r.stdout


@pytest.mark.parametrize("speed", ["0.5", "-0.5", "0.21", "2.0"])
def test_spin_refuses_above_0p2_without_flag(vcan, zlac, speed):
    r = run("zlac_spin.py", vcan, BENCH_FLAG, "--wheels-up-confirmed", "--wheel-diameter-m", "0.2",
            "--speed", speed, "--duration", "1")
    assert r.returncode == 2
    assert "refusing" in r.stderr
    if float(speed) <= 0.5:
        assert "--allow-high-speed" in r.stderr
    assert zlac.od.get(0x6040, 0) == 0  # nothing was sent to the drive
    assert zlac.od.get(0x60FF, 1) == 0 and zlac.rx_count == 0


def test_spin_high_speed_flag_allows_0p3_but_never_above_ceiling(vcan, zlac):
    d = 0.2
    r = run("zlac_spin.py", vcan, BENCH_FLAG, "--wheels-up-confirmed", "--wheel-diameter-m", str(d),
            "--speed", "0.3", "--duration", "2.5", "--allow-high-speed")
    assert r.returncode == 0, r.stdout + r.stderr
    assert f"commanded {round(0.3 / (3.14159265 * d) * 60)} r/min" in r.stdout
    zlac.rx_count = 0
    r = run("zlac_spin.py", vcan, BENCH_FLAG, "--wheels-up-confirmed", "--wheel-diameter-m", str(d),
            "--speed", "0.6", "--duration", "1", "--allow-high-speed")
    assert r.returncode == 2 and "ceiling" in r.stderr
    assert zlac.rx_count == 0


def test_speed_to_rpm_boundaries():
    import bench_common as b
    d = 0.2
    one = b.speed_to_rpm(0.1, d)
    assert b.speed_to_rpm(0.2, d) == one  # capped to 0.1
    assert b.speed_to_rpm(-0.15, d) == -one
    with pytest.raises(SystemExit) as e:
        b.speed_to_rpm(0.2001, d)
    assert e.value.code == 2
    assert b.speed_to_rpm(0.5, d, allow_high_speed=True) == round(0.5 / (3.14159265 * d) * 60)
    with pytest.raises(SystemExit):
        b.speed_to_rpm(0.5001, d, allow_high_speed=True)


def test_no_python_cache_is_tracked():
    out = subprocess.run(["git", "ls-files"], capture_output=True, text=True,
                         cwd=os.path.dirname(BENCH))
    if out.returncode != 0:
        pytest.skip("not a git checkout")
    tracked = [f for f in out.stdout.split() if "__pycache__" in f or f.endswith(".pyc")]
    assert tracked == []
    ignore = open(os.path.join(BENCH, "..", "..", ".gitignore")).read()
    assert "__pycache__/" in ignore


def test_spin_then_link_loss_protection_stops_wheels(vcan, zlac):
    """The link-loss bench procedure on the emulator: protection on, host disappears."""
    assert run("zlac_set_link_loss.py", vcan, BENCH_FLAG, "--link-loss-ms", "250",
               "--heartbeat-ms", "100").returncode == 0
    zlac.od.set(0x6083, 1, 50)
    zlac.od.set(0x6083, 2, 50)
    zlac.od.set(0x6084, 1, 50)
    zlac.od.set(0x6084, 2, 50)
    # script ends and writes 0, so emulate the unplug by commanding speed and going silent
    from canopen_common import SdoClient
    from conftest import wait_for as wf
    import can
    bus = can.Bus(interface="socketcan", channel=vcan)
    try:
        sdo = SdoClient(bus, 1)
        for cw in (0x06, 0x07, 0x0F):
            sdo.write(0x6040, 0, cw, 2)
        sdo.write(0x60FF, 1, 30, 4, signed=True)
        sdo.write(0x60FF, 2, 30, 4, signed=True)
    finally:
        bus.shutdown()
    assert wf(lambda: zlac.rpm[0] > 29)
    assert wf(lambda: zlac.link_lost, timeout=1.0)
    assert wf(lambda: abs(zlac.rpm[0]) < 0.5, timeout=1.0)


def test_can_up_dry_run():
    r = subprocess.run([os.path.join(BENCH, "can_up.sh"), "can1", "500000", "--dry-run"],
                       capture_output=True, text=True)
    assert r.returncode == 0
    assert "type can bitrate 500000 restart-ms 0" in r.stdout
    assert "txqueuelen 1000" in r.stdout


def test_can_up_rejects_bad_bitrate():
    r = subprocess.run([os.path.join(BENCH, "can_up.sh"), "can1", "123", "--dry-run"],
                       capture_output=True, text=True)
    assert r.returncode == 2


@pytest.mark.skipif(shutil.which("shellcheck") is None, reason="shellcheck not installed")
def test_shellcheck():
    r = subprocess.run(["shellcheck", os.path.join(BENCH, "can_up.sh"),
                        os.path.join(BENCH, "busload.sh")], capture_output=True, text=True)
    assert r.returncode == 0, r.stdout


# --- fw2-can-sil-fix: owner findings on platform-fw #38 and #39 --------------------------

SPIN = ("zlac_spin.py", "--wheels-up-confirmed", "--wheel-diameter-m", "0.2", "--speed", "0.05",
        "--duration", "2.5")


def spin(vcan, *extra):
    return run(SPIN[0], vcan, BENCH_FLAG, *SPIN[1:], *extra)


def test_spin_passes_only_when_both_606c_subindexes_match(vcan, zlac):
    r = spin(vcan)
    assert r.returncode == 0, r.stdout + r.stderr
    assert "606Ch:01 and 606Ch:02" in r.stdout


@pytest.mark.parametrize("sub", [1, 2])
def test_spin_fails_when_one_wheel_does_not_follow(vcan, zlac, sub):
    zlac.inject_speed_scale(sub, 0.5)  # this sub-index turns at half speed
    r = spin(vcan)
    assert r.returncode == 1, r.stdout + r.stderr
    assert "PASS" not in r.stdout


@pytest.mark.parametrize("sub", [1, 2])
def test_spin_fails_when_one_wheel_turns_the_wrong_way(vcan, zlac, sub):
    zlac.inject_speed_scale(sub, -1.0)
    assert spin(vcan).returncode == 1


@pytest.mark.parametrize("sub", [1, 2])
def test_spin_fails_when_one_wheel_does_not_turn(vcan, zlac, sub):
    zlac.inject_speed_scale(sub, 0.0)
    assert spin(vcan).returncode == 1


def test_spin_unreadable_606c_subindex_is_an_error_not_a_pass(vcan, zlac):
    zlac.od.entries[(0x606C, 2)]["rw"] = "wo"  # reads abort, the emulator keeps running
    r = spin(vcan)
    assert r.returncode == 2 and "PASS" not in r.stdout
    assert zlac.od.get(0x60FF, 1) == 0 and zlac.od.get(0x6040, 0) == 0x06  # cleanup still ran


def test_spin_refuses_without_bench_flag_and_prints_notice(vcan, zlac):
    r = run("zlac_spin.py", vcan, "--wheels-up-confirmed", "--wheel-diameter-m", "0.2")
    assert r.returncode == 2
    assert "--i-am-at-the-bench" in r.stderr and "second person" in r.stderr
    assert zlac.rx_count == 0


def test_spin_refuses_unless_speed_resolution_is_one(vcan, zlac):
    zlac.inject_speed_resolution(10)
    r = spin(vcan)
    assert r.returncode == 2 and "2026h:05" in r.stderr
    assert zlac.od.get(0x6040, 0) == 0 and zlac.od.get(0x60FF, 1) == 0


@pytest.mark.parametrize("mode", ["timeout", "abort"])
def test_spin_fails_when_the_stop_write_fails_after_a_good_measurement(vcan, zlac, mode):
    zlac.inject_stop_write_fault(mode)
    r = spin(vcan)
    assert r.returncode == 3, r.stdout + r.stderr
    assert "PASS" not in r.stdout
    assert "CLEANUP FAILED" in r.stdout and "stop not confirmed" in r.stdout
    # the emulator did not turn the lost stop into a stopped drive
    assert zlac.dropped_writes
    assert zlac.od.get(0x60FF, 1) != 0 and zlac.enabled and abs(zlac.rpm[0]) > 1


def test_emulator_never_treats_a_dropped_stop_as_stopped(vcan, zlac, master):
    from canopen_common import SdoClient
    sdo = SdoClient(master, 1, timeout=0.2)
    for cw in (0x06, 0x07, 0x0F):
        sdo.write(0x6040, 0, cw, 2)
    sdo.write(0x60FF, 1, 40, 4, signed=True)
    zlac.inject_stop_write_fault("timeout")
    with pytest.raises(TimeoutError):
        sdo.write(0x60FF, 1, 0, 4, signed=True)
    with pytest.raises(TimeoutError):
        sdo.write(0x6040, 0, 0x06, 2)
    from conftest import wait_for as wf
    assert wf(lambda: zlac.rpm[0] > 30)
    assert zlac.od.get(0x60FF, 1) == 40 and zlac.od.get(0x6040, 0) == 0x0F
    zlac.inject_stop_write_fault(None)
    sdo.write(0x60FF, 1, 0, 4, signed=True)
    assert wf(lambda: abs(zlac.rpm[0]) < 1)


def test_spin_cleanup_retries_a_single_lost_write(vcan, zlac):
    """One lost stop write is retried and acknowledged: the run is judged on the measurement."""
    drops = {"n": 0}
    original = zlac.swallow_write

    def flaky(index, sub, value):
        if index == 0x60FF and sub == 1 and value == 0 and zlac.enabled and drops["n"] == 0:
            drops["n"] += 1
            return True
        return original(index, sub, value)
    zlac.swallow_write = flaky
    r = spin(vcan)
    assert drops["n"] == 1 and r.returncode == 0, r.stdout + r.stderr
    assert zlac.od.get(0x60FF, 1) == 0 and zlac.od.get(0x6040, 0) == 0x06


@pytest.mark.parametrize("script,extra", [
    ("zlac_set_link_loss.py", ["--link-loss-ms", "250", "--heartbeat-ms", "100"]),
    ("zlac_spin.py", ["--wheels-up-confirmed", "--wheel-diameter-m", "0.2"]),
])
def test_drive_commanding_scripts_need_the_bench_flag(vcan, zlac, script, extra):
    r = run(script, vcan, *extra)
    assert r.returncode == 2
    assert "--i-am-at-the-bench" in r.stderr and "E-stop" in r.stderr
    assert zlac.rx_count == 0


# --- link-loss procedure, D-24 ---------------------------------------------------------

LL = ("--link-loss-ms", "250", "--heartbeat-ms", "100")


def link_loss(vcan, *extra, args=LL):
    return run("zlac_set_link_loss.py", vcan, BENCH_FLAG, *args, *extra)


def good_record(**over):
    rec = {"accepted_by": "bench lead", "date": "2026-11-24", "link_loss_ms": 250,
           "heartbeat_ms": 100, "runs_passed": 5, "drive_behaviour": "ramp",
           "wheel_sub1_observed": True, "wheel_sub2_observed": True,
           "stop_time_ms": {"sub1": 310, "sub2": 305},
           "stop_distance_mm": {"sub1": 12, "sub2": 11}}
    rec.update(over)
    return rec


@pytest.fixture
def record(tmp_path):
    def make(**over):
        p = tmp_path / "record.json"
        p.write_text(json.dumps(good_record(**over)))
        return str(p)
    return make


def test_link_loss_values_are_parameters_without_defaults(vcan, zlac):
    r = run("zlac_set_link_loss.py", vcan, BENCH_FLAG, "--link-loss-ms", "250")
    assert r.returncode == 2 and "--heartbeat-ms" in r.stderr
    r = run("zlac_set_link_loss.py", vcan, BENCH_FLAG, "--heartbeat-ms", "100")
    assert r.returncode == 2 and "--link-loss-ms" in r.stderr
    assert zlac.rx_count == 0


@pytest.mark.parametrize("ms", ["0", "199", "301"])
def test_link_loss_refuses_out_of_range(vcan, zlac, ms):
    r = link_loss(vcan, args=("--link-loss-ms", ms, "--heartbeat-ms", "100"))
    assert r.returncode == 2
    assert zlac.od.get(0x2000, 0) == 0 and zlac.rx_count == 0


def test_link_loss_writes_and_reads_back_both_objects_and_saves_nothing(vcan, zlac):
    r = link_loss(vcan)
    assert r.returncode == 0, r.stdout + r.stderr
    assert "2000h written 250, read back 250" in r.stdout
    assert "1017h written 100, read back 100" in r.stdout
    assert "not saved" in r.stdout
    assert zlac.od.get(0x2000, 0) == 250 and zlac.od.get(0x1017, 0) == 100 and zlac.saves == 0


def test_link_loss_values_are_lost_at_power_cycle_when_not_saved(vcan, zlac):
    zlac.od.set(0x1017, 0, 90)  # the drive's own value, not part of the script
    assert link_loss(vcan).returncode == 0
    zlac.power_cycle()
    assert zlac.od.get(0x2000, 0) == 0 and zlac.od.get(0x1017, 0) == 100  # factory value kept


@pytest.mark.parametrize("value", [0, 10, 2, 255])
def test_link_loss_refuses_unless_speed_resolution_is_one(vcan, zlac, value):
    zlac.inject_speed_resolution(value)
    r = link_loss(vcan)
    assert r.returncode == 2 and "2026h:05" in r.stderr
    assert zlac.od.get(0x2000, 0) == 0 and zlac.od.get(0x1017, 0) == 100 and zlac.saves == 0


def test_link_loss_refuses_when_speed_resolution_unreadable(vcan, zlac):
    del zlac.od.entries[(0x2026, 5)]
    r = link_loss(vcan)
    assert r.returncode == 2 and "2026h:05" in r.stderr
    assert zlac.od.get(0x2000, 0) == 0


def test_link_loss_old_save_flag_is_gone(vcan, zlac):
    r = link_loss(vcan, "--save-confirmed")
    assert r.returncode == 2 and zlac.saves == 0 and zlac.od.get(0x2000, 0) == 0


def test_link_loss_readback_mismatch_fails_and_does_not_save(vcan, zlac, record):
    zlac.od.entries[(0x1017, 0)]["rw"] = "ro"  # the drive refuses the 1017h write
    r = link_loss(vcan, "--acceptance-record", record())
    assert r.returncode == 2 and zlac.saves == 0  # abort: no save


def test_link_loss_save_only_with_acceptance_record_then_power_cycle_readback(vcan, zlac, record):
    r = link_loss(vcan, "--acceptance-record", record())
    assert r.returncode == 0, r.stdout + r.stderr
    assert zlac.saves == 1 and "2010h = 1 written" in r.stdout
    zlac.power_cycle()
    assert zlac.od.get(0x2000, 0) == 250 and zlac.od.get(0x1017, 0) == 100
    r = run("zlac_set_link_loss.py", vcan, BENCH_FLAG, *LL, "--verify-power-cycle",
            "--boot-wait-s", "0.5")
    # the boot-up frame was sent before this process listened: no boot-up seen, so it fails
    assert r.returncode == 1 and "not power-cycled" in r.stdout


def test_verify_power_cycle_passes_when_drive_boots_during_the_wait(vcan, zlac, record):
    import threading
    assert link_loss(vcan, "--acceptance-record", record()).returncode == 0
    t = threading.Timer(1.0, zlac.power_cycle)
    t.start()
    r = run("zlac_set_link_loss.py", vcan, BENCH_FLAG, *LL, "--verify-power-cycle",
            "--boot-wait-s", "5")
    t.join()
    assert r.returncode == 0, r.stdout + r.stderr
    assert "after power cycle" in r.stdout and zlac.saves == 1


def test_verify_power_cycle_fails_when_values_were_not_saved(vcan, zlac):
    import threading
    assert link_loss(vcan).returncode == 0  # not saved
    t = threading.Timer(1.0, zlac.power_cycle)
    t.start()
    r = run("zlac_set_link_loss.py", vcan, BENCH_FLAG, *LL, "--verify-power-cycle",
            "--boot-wait-s", "5")
    t.join()
    assert r.returncode == 1 and "differ" in r.stdout


def test_verify_power_cycle_writes_nothing(vcan, zlac):
    zlac.od.set(0x2000, 0, 250)
    r = run("zlac_set_link_loss.py", vcan, BENCH_FLAG, *LL, "--verify-power-cycle",
            "--boot-wait-s", "0.5")
    assert r.returncode == 1 and zlac.saves == 0
    assert zlac.od.get(0x2000, 0) == 250  # untouched


@pytest.mark.parametrize("mutation", [
    {"link_loss_ms": 200}, {"heartbeat_ms": 50}, {"runs_passed": 4}, {"runs_passed": True},
    {"accepted_by": ""}, {"date": None}, {"drive_behaviour": "stopped"},
    {"wheel_sub1_observed": False}, {"wheel_sub2_observed": None},
    {"stop_time_ms": {"sub1": 300}}, {"stop_distance_mm": {"sub1": 1, "sub2": 0}},
    {"stop_time_ms": 300},
])
def test_incomplete_acceptance_record_blocks_the_save_and_every_write(vcan, zlac, record,
                                                                        mutation):
    r = link_loss(vcan, "--acceptance-record", record(**mutation))
    assert r.returncode == 2 and "acceptance record" in r.stderr
    assert zlac.saves == 0 and zlac.od.get(0x2000, 0) == 0 and zlac.rx_count == 0


def test_missing_or_garbled_acceptance_record_blocks_the_save(vcan, zlac, tmp_path):
    r = link_loss(vcan, "--acceptance-record", str(tmp_path / "none.json"))
    assert r.returncode == 2 and zlac.saves == 0 and zlac.rx_count == 0
    bad = tmp_path / "bad.json"
    bad.write_text("[1, 2")
    r = link_loss(vcan, "--acceptance-record", str(bad))
    assert r.returncode == 2 and zlac.saves == 0 and zlac.rx_count == 0
    arr = tmp_path / "arr.json"
    arr.write_text("[]")
    assert link_loss(vcan, "--acceptance-record", str(arr)).returncode == 2


def test_acceptance_record_cannot_be_combined_with_verify(vcan, zlac, record):
    r = link_loss(vcan, "--verify-power-cycle", "--acceptance-record", record())
    assert r.returncode == 2 and zlac.saves == 0


def test_check_record_unit():
    import zlac_set_link_loss as m
    import tempfile
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as fh:
        json.dump(good_record(), fh)
    assert m.check_record(fh.name, 250, 100) == []
    assert m.check_record(fh.name, 251, 100)
    os.unlink(fh.name)


# --- no left or right meaning for 603Fh ------------------------------------------------

def test_decode_603f_names_bit_ranges_not_sides():
    import bench_common as b
    out = b.decode_603f(0x00040004)
    assert out == ["603Fh[15:0]: over-current", "603Fh[31:16]: over-current"]
    assert b.decode_603f(0x0002) == ["603Fh[15:0]: under-voltage"]


def test_no_side_meaning_for_603f_anywhere_in_tools():
    """No file in tools/bench or tools/can_sil may give 603Fh a left or right half, and no
    emulator or bench API may take a 'left' or 'right' fault side."""
    root = os.path.abspath(os.path.join(BENCH, ".."))
    sides = re.compile(r"\b(left|right)\b", re.I)
    ctx = re.compile(r"603f|inject_|half|16 bits|per side|per-side", re.I)
    bad = []
    for base, _dirs, files in os.walk(root):
        if "__pycache__" in base:
            continue
        for name in files:
            if not name.endswith((".py", ".md", ".sh")) or name == os.path.basename(__file__):
                continue
            path = os.path.join(base, name)
            if os.path.basename(path) in ("ctl017_emulator.py", "ctl017_od_v1.py"):
                continue  # CTL-017 has no 603Fh; "left/right" never refers to the drive there
            for n, line in enumerate(open(path, encoding="utf-8"), 1):
                if sides.search(line) and ctx.search(line):
                    bad.append("%s:%d: %s" % (os.path.relpath(path, root), n, line.strip()))
    assert bad == []
