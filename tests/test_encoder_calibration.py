"""Host tests for tools/encoder-calibration and its contract with the Gate A Teensy firmware.

No ROS needed: rclpy and the message packages are replaced by stubs before import.
Run: python3 -m unittest discover -s tests -v   (needs numpy)
"""
import importlib.util
import io
import json
import math
import re
import sys
import tempfile
import types
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

import numpy as np

REPO = Path(__file__).resolve().parents[1]
TOOLS = REPO / "tools" / "encoder-calibration"
FIRMWARE = REPO / "boards" / "teensy_4_0" / "linorobot2_overlay" / "src" / "firmware.ino"
CONFIG = REPO / "boards" / "teensy_4_0" / "linorobot2_overlay" / "config" / "lino_base_config.h"


def _stub_ros():
    """Minimal stand-ins so the tools import without ROS. Any real ROS call fails the test."""
    def forbidden(*_a, **_k):
        raise AssertionError("ROS was started; the tool must not touch ROS here")
    rclpy = types.ModuleType("rclpy")
    rclpy.init = forbidden
    rclpy.spin_once = forbidden
    rclpy.shutdown = forbidden
    node = types.ModuleType("rclpy.node")
    node.Node = type("Node", (), {"__init__": forbidden})
    qos = types.ModuleType("rclpy.qos")
    qos.QoSProfile = lambda **k: k
    qos.ReliabilityPolicy = types.SimpleNamespace(BEST_EFFORT=1)
    qos.HistoryPolicy = types.SimpleNamespace(KEEP_LAST=1)
    geo = types.ModuleType("geometry_msgs")
    geo_msg = types.ModuleType("geometry_msgs.msg")
    geo_msg.Twist = geo_msg.Vector3 = type("Msg", (), {})
    std = types.ModuleType("std_msgs")
    std_msg = types.ModuleType("std_msgs.msg")
    std_msg.Float32MultiArray = type("Msg", (), {})
    sys.modules.update({"rclpy": rclpy, "rclpy.node": node, "rclpy.qos": qos,
                        "geometry_msgs": geo, "geometry_msgs.msg": geo_msg,
                        "std_msgs": std, "std_msgs.msg": std_msg})


def _load(name):
    _stub_ros()
    spec = importlib.util.spec_from_file_location(name, TOOLS / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


calib = _load("encoder_calib")
align = _load("align_enc_cal")
apply_cal = _load("apply_enc_cal")


def _define(text, name):
    m = re.search(rf"^\s*#define\s+{name}\s+([^\s/]+)", text, re.M)
    if not m:
        raise AssertionError(f"#define {name} not found")
    return m.group(1)


class ArmingGate(unittest.TestCase):
    """The powered calibration spins the wheels; it must refuse to run without --arm."""

    def test_refuses_without_arm_and_never_starts_ros(self):
        out = io.StringIO()
        with mock.patch.object(sys, "argv", ["encoder_calib.py", "150,200"]), redirect_stdout(out):
            calib.main()
        self.assertIn("REFUSED", out.getvalue())

    def test_pwm_limit_does_not_exceed_firmware_open_loop_clamp(self):
        cfg = CONFIG.read_text()
        bits = int(_define(cfg, "PWM_BITS"))
        firmware_clamp = 0.7 * (2 ** bits - 1)       # OPENLOOP_PWM_LIMIT in firmware.ino
        self.assertIn("#define OPENLOOP_PWM_LIMIT (0.7 * PWM_MAX)", FIRMWARE.read_text())
        self.assertLessEqual(calib.PWM_LIMIT, firmware_clamp)
        self.assertLessEqual(align.PWM_LIMIT, firmware_clamp)


class Profile(unittest.TestCase):
    def test_bins_by_wheel_angle_and_returns_mean(self):
        samples = [(c, 100.0 + (10.0 if c < 512 else -10.0)) for c in range(0, 1024, 4)]
        means, mean = calib.profile(samples)
        self.assertEqual(len(means), calib.NBINS)
        self.assertAlmostEqual(mean, 100.0)
        self.assertAlmostEqual(means[0], 110.0)
        self.assertAlmostEqual(means[-1], 90.0)

    def test_counts_wrap_every_revolution(self):
        means, _ = calib.profile([(1024 * 5 + 10, 50.0)])
        self.assertEqual(means[0], 50.0)

    def test_empty_bins_are_nan_not_zero(self):
        means, _ = calib.profile([(0, 1.0)])
        self.assertTrue(math.isnan(means[5]))

    def test_align_profile_fills_gaps_and_normalises(self):
        prof = align.profile([(c, 200.0) for c in range(0, 512, 4)])   # half a revolution only
        self.assertEqual(len(prof), align.NBINS)
        self.assertFalse(np.isnan(prof).any())
        np.testing.assert_allclose(prof, 1.0)


class BestShift(unittest.TestCase):
    def test_recovers_a_fractional_offset(self):
        x = np.arange(align.NBINS)
        ref = 1.0 + 0.05 * np.cos(2 * 2 * np.pi * x / align.NBINS) + 0.02 * np.sin(2 * np.pi * x / align.NBINS)
        offset_bins = 7.5
        cur = np.interp((x - offset_bins) % align.NBINS, x, ref, period=align.NBINS)
        deg, placed = align.best_shift(cur, ref)
        self.assertAlmostEqual(deg, offset_bins * 360.0 / align.NBINS, delta=1.0)
        np.testing.assert_allclose(placed, cur, atol=2e-3)


class LoadTable(unittest.TestCase):
    def _write(self, data):
        tmp = tempfile.NamedTemporaryFile("w", suffix=".json", delete=False)
        json.dump(data, tmp)
        tmp.close()
        self.addCleanup(Path(tmp.name).unlink)
        return tmp.name

    def test_flat_table_is_all_ones(self):
        left, right = apply_cal.load_table(["--flat"])
        self.assertEqual(left, [1.0] * 36)
        self.assertEqual(right, [1.0] * 36)

    def test_wrong_bin_count_is_rejected(self):
        path = self._write({"left_ratio": [1.0] * 35, "right_ratio": [1.0] * 36})
        with self.assertRaises(SystemExit):
            apply_cal.load_table([path])

    def test_nan_and_near_zero_ratios_become_passthrough(self):
        left = [1.0] * 36
        left[3], left[4], left[5] = float("nan"), 0.0, 0.01
        path = self._write({"left_ratio": left, "right_ratio": [1.02] * 36})
        out_left, out_right = apply_cal.load_table([path])
        self.assertEqual(out_left[3:6], [1.0, 1.0, 1.0])
        self.assertEqual(out_right, [1.02] * 36)

    def test_committed_reference_table_is_usable(self):
        data = json.loads((TOOLS / "encoder_ref_table.json").read_text())
        self.assertEqual(data["nbins"], 36)
        for side in ("left_ratio", "right_ratio"):
            self.assertEqual(len(data[side]), 36)
            for v in data[side]:
                self.assertTrue(math.isfinite(v) and 0.5 < v < 1.5, f"{side} ratio {v} out of range")


class FirmwareContract(unittest.TestCase):
    """Host tools and the deployed Teensy firmware must agree on the table layout."""

    def setUp(self):
        self.fw = FIRMWARE.read_text()
        self.cfg = CONFIG.read_text()

    def test_bin_count_and_cpr_match_firmware(self):
        self.assertEqual(int(_define(self.fw, "ENC_CAL_NBINS")), calib.NBINS)
        self.assertEqual(int(_define(self.fw, "ENC_CAL_NBINS")), align.NBINS)
        self.assertEqual(int(_define(self.fw, "ENC_CAL_NBINS")), apply_cal.NBINS)
        self.assertEqual(int(_define(self.fw, "ENC_CAL_CPR")), calib.CPR)
        self.assertEqual(int(_define(self.cfg, "COUNTS_PER_REV1")), calib.CPR)
        self.assertEqual(int(_define(self.cfg, "COUNTS_PER_REV2")), calib.CPR)

    def test_reference_table_cpr_matches_firmware(self):
        data = json.loads((TOOLS / "encoder_ref_table.json").read_text())
        self.assertEqual(data["cpr"], int(_define(self.fw, "ENC_CAL_CPR")))

    def test_firmware_reads_left_then_right_from_the_same_topic(self):
        self.assertIn("LEFT_CAL[i]  = m->data.data[i];", self.fw)
        self.assertIn("RIGHT_CAL[i] = m->data.data[ENC_CAL_NBINS + i];", self.fw)
        self.assertIn('"/debug/enc_cal"', (TOOLS / "apply_enc_cal.py").read_text())
        self.assertIn('"debug/enc_cal"', self.fw)


if __name__ == "__main__":
    unittest.main()
