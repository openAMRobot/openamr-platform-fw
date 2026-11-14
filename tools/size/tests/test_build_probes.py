# SPDX-License-Identifier: MIT
"""Tests for tools/size/probe/build_probes.sh.

The full run needs the micro-ROS static library, LwIP and FreeRTOS-Kernel
checkouts (see the script header). It runs only when UROS_LIB, UROS_UTILS,
LWIP and FREERTOS are set and is reported as skipped otherwise. The other
tests run everywhere.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

import map_report  # noqa: E402

SCRIPT = HERE.parent / "probe" / "build_probes.sh"
PROBES = ("empty_baseline", "superloop_uros_serial", "superloop_lwip_uros_udp", "freertos_lwip_uros_udp")
ENV = ("UROS_LIB", "UROS_UTILS", "LWIP", "FREERTOS")


def test_script_syntax():
    res = subprocess.run(["bash", "-n", str(SCRIPT)], capture_output=True, text=True, check=False)
    assert res.returncode == 0, res.stderr


def test_script_refuses_to_run_without_inputs(tmp_path):
    env = {k: v for k, v in os.environ.items() if k not in ENV}
    env["OUT"] = str(tmp_path)
    res = subprocess.run(["bash", str(SCRIPT)], capture_output=True, text=True, env=env, check=False)
    assert res.returncode != 0
    assert "set UROS_LIB" in res.stderr


def test_probe_sources_are_marked_not_firmware():
    for src in (HERE.parent / "probe").rglob("*"):
        if src.suffix in (".c", ".h", ".sh"):
            assert "NOT FIRMWARE" in src.read_text(), src


@pytest.mark.skipif(any(k not in os.environ for k in ENV) or shutil.which("arm-none-eabi-gcc") is None,
                    reason="probe inputs (UROS_LIB, UROS_UTILS, LWIP, FREERTOS) or arm-none-eabi-gcc not available")
def test_full_probe_run(tmp_path):
    env = dict(os.environ, OUT=str(tmp_path))
    res = subprocess.run(["bash", str(SCRIPT)], capture_output=True, text=True, env=env, check=False, timeout=3000)
    assert res.returncode == 0, res.stdout + res.stderr
    used = {}
    for name in PROBES:
        rep = map_report.parse_map((tmp_path / f"{name}.map").read_text())
        assert rep.unplaced == [], name
        for r in rep.regions:
            assert r.used <= r.length, (name, r.name, r.used, r.length)
        used[name] = {r.name: r.used for r in rep.regions}
    # Each step adds code: serial < LwIP super-loop < FreeRTOS with sockets.
    flash = [used[p]["FLASH"] for p in PROBES]
    assert flash == sorted(flash)
    report = (tmp_path / "report.md").read_text()
    for name in PROBES:
        assert f"### {name}" in report
