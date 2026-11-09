"""Run the tools/bench scripts against the emulators on vcan."""
import os
import shutil
import subprocess
import sys

import pytest

import ctl017_emulator as c
from zlac8015d_emulator import Zlac8015dEmulator

BENCH = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "bench"))


sys.path.insert(0, BENCH)


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
    zlac.inject_over_current("left")
    r = run("zlac_read_faults.py", vcan)
    assert r.returncode == 1
    assert "left: over-current" in r.stdout


def test_read_faults_no_drive(vcan):
    r = run("zlac_read_faults.py", vcan)
    assert r.returncode == 2


def test_set_link_loss_refuses_out_of_range(vcan, zlac):
    r = run("zlac_set_link_loss.py", vcan, "--timeout-ms", "0")
    assert r.returncode == 2
    assert zlac.od.get(0x2000, 0) == 0


def test_set_link_loss_without_confirm_does_not_save(vcan, zlac):
    r = run("zlac_set_link_loss.py", vcan, "--timeout-ms", "250")
    assert r.returncode == 0, r.stdout
    assert zlac.od.get(0x2000, 0) == 250 and zlac.saves == 0
    zlac.power_cycle()
    assert zlac.od.get(0x2000, 0) == 0


def test_set_link_loss_with_confirm_saves(vcan, zlac):
    r = run("zlac_set_link_loss.py", vcan, "--timeout-ms", "250", "--save-confirmed")
    assert r.returncode == 0, r.stdout
    assert zlac.saves == 1
    zlac.power_cycle()
    assert zlac.od.get(0x2000, 0) == 250


def test_spin_refuses_without_confirmation(vcan, zlac):
    r = run("zlac_spin.py", vcan, "--wheel-diameter-m", "0.2", "--duration", "1")
    assert r.returncode != 0 and "wheels-up-confirmed" in r.stderr
    assert zlac.od.get(0x6040, 0) == 0  # nothing was sent to the drive


def test_spin_refuses_without_wheel_diameter(vcan, zlac):
    r = run("zlac_spin.py", vcan, "--wheels-up-confirmed", "--duration", "1")
    assert r.returncode != 0 and "wheel-diameter" in r.stderr
    assert zlac.od.get(0x6040, 0) == 0


def test_spin_caps_speed_between_cap_and_refusal_limit(vcan, zlac):
    d = 0.2
    r = run("zlac_spin.py", vcan, "--wheels-up-confirmed", "--wheel-diameter-m", str(d),
            "--speed", "0.15", "--duration", "2.5")
    assert "capped" in r.stderr
    assert r.returncode == 0, r.stdout + r.stderr
    cap_rpm = 0.1 / (3.14159265 * d) * 60
    assert zlac.od.get(0x60FF, 1) == 0  # stop written at the end
    assert zlac.od.get(0x6040, 0) == 0x06
    assert f"commanded {round(cap_rpm)} r/min" in r.stdout


@pytest.mark.parametrize("speed", ["0.5", "-0.5", "0.21", "2.0"])
def test_spin_refuses_above_0p2_without_flag(vcan, zlac, speed):
    r = run("zlac_spin.py", vcan, "--wheels-up-confirmed", "--wheel-diameter-m", "0.2",
            "--speed", speed, "--duration", "1")
    assert r.returncode == 2
    assert "refusing" in r.stderr
    if float(speed) <= 0.5:
        assert "--allow-high-speed" in r.stderr
    assert zlac.od.get(0x6040, 0) == 0  # nothing was sent to the drive
    assert zlac.od.get(0x60FF, 1) == 0 and zlac.rx_count == 0


def test_spin_high_speed_flag_allows_0p3_but_never_above_ceiling(vcan, zlac):
    d = 0.2
    r = run("zlac_spin.py", vcan, "--wheels-up-confirmed", "--wheel-diameter-m", str(d),
            "--speed", "0.3", "--duration", "2.5", "--allow-high-speed")
    assert r.returncode == 0, r.stdout + r.stderr
    assert f"commanded {round(0.3 / (3.14159265 * d) * 60)} r/min" in r.stdout
    zlac.rx_count = 0
    r = run("zlac_spin.py", vcan, "--wheels-up-confirmed", "--wheel-diameter-m", str(d),
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
    assert run("zlac_set_link_loss.py", vcan, "--timeout-ms", "250").returncode == 0
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
