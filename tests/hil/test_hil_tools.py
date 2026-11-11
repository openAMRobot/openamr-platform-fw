"""Host tests for tools/hil (no hardware). Run: python3 -m unittest discover -s tests/hil -v

Covers the refusal without HIL_BENCH=1 for every bench script, the frame and banner parsers
against datasheet formats, and the bench record writer and schema.
"""
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import jsonschema

ROOT = Path(__file__).resolve().parents[2]
HIL = ROOT / "tools" / "hil"
sys.path.insert(0, str(HIL))
import hil_common as hil  # noqa: E402

BENCH_SCRIPTS = ["flash.py", "boot_banner.py", "imu_whoami.py", "eth_check.py", "can_loopback.py", "sensor_frames.py"]


def tfluna_frame(dist, amp, temp_raw, bad=False):
    body = bytes([0x59, 0x59, dist & 0xFF, dist >> 8, amp & 0xFF, amp >> 8, temp_raw & 0xFF, temp_raw >> 8])
    return body + bytes([(sum(body) + (1 if bad else 0)) & 0xFF])


class Refusal(unittest.TestCase):
    def test_every_bench_script_refuses_without_hil_bench(self):
        env = {k: v for k, v in os.environ.items() if k != "HIL_BENCH"}
        for script in BENCH_SCRIPTS:
            with self.subTest(script=script), tempfile.TemporaryDirectory() as tmp:
                r = subprocess.run([sys.executable, str(HIL / script), "--help"], cwd=tmp, env=env,
                                   capture_output=True, text=True)
                self.assertEqual(r.returncode, hil.REFUSED, r.stdout + r.stderr)
                self.assertIn("REFUSED", r.stdout)
                self.assertEqual(list(Path(tmp).iterdir()), [], "a refused script must not write anything")

    def test_hil_bench_zero_also_refuses(self):
        r = subprocess.run([sys.executable, str(HIL / "flash.py"), "--target", "stm32", "--image", "x", "--sha256", "0"],
                           env=dict(os.environ, HIL_BENCH="0"), capture_output=True, text=True)
        self.assertEqual(r.returncode, hil.REFUSED)

    def test_can_needs_the_harness_statement(self):
        env = dict(os.environ, HIL_BENCH="1")
        env.pop("HIL_CAN_HARNESS_ONLY", None)
        r = subprocess.run([sys.executable, str(HIL / "can_loopback.py")], env=env, capture_output=True, text=True)
        self.assertEqual(r.returncode, hil.REFUSED)
        self.assertIn("HIL_CAN_HARNESS_ONLY", r.stdout)

    def test_no_script_names_a_safety_output(self):
        # the bench scripts never reference safety-chain, brake, contactor, lift or charger outputs
        forbidden = ("KCHG", "kchg", "contactor", "brake", "reset_lamp", "lift_power", "charger_enable", "estop_out")
        for script in BENCH_SCRIPTS:
            code = [l for l in (HIL / script).read_text().splitlines() if not l.lstrip().startswith(("#", '"""'))]
            body = "\n".join(code).split('"""', 2)[-1]
            for word in forbidden:
                self.assertNotIn(word, body, f"{script} references {word}")


class Parsers(unittest.TestCase):
    def test_mb7060_frames(self):
        self.assertEqual(hil.parse_mb7060(b"R123\rR0456\rR12\rjunkR789\r"), [123, 456, 789])

    def test_tfluna_frames_and_checksums(self):
        data = b"\x00" + tfluna_frame(120, 900, 2400) + tfluna_frame(5, 50, 2400, bad=True) + tfluna_frame(300, 4000, 2416)
        frames, bad = hil.parse_tfluna(data)
        self.assertEqual(frames, [(120, 900, 2400 / 8 - 256), (300, 4000, 2416 / 8 - 256)])
        self.assertEqual(bad, 1)

    def test_banner(self):
        info = hil.parse_banner(["noise", "OPENAMR BOOT fw=3cb8557 board=NUCLEO-H723ZG",
                                 "OPENAMR RESET reason=PIN,BOR", "OPENAMR IMU whoami=0x47"])
        self.assertEqual(info, {"fw": "3cb8557", "board": "NUCLEO-H723ZG", "reset": ["PIN", "BOR"], "whoami": 0x47})

    def test_banner_absent(self):
        self.assertEqual(hil.parse_banner(["hello"]), {})


class BenchRecord(unittest.TestCase):
    def test_record_is_valid_unsigned_with_pending_checklist(self):
        with tempfile.TemporaryDirectory() as tmp:
            t = Path(tmp)
            (t / "logs").mkdir()
            (t / "logs" / "boot-banner.log").write_text("OPENAMR BOOT fw=abc1234 board=NUCLEO-H723ZG\n")
            (t / "results.jsonl").write_text(json.dumps({"test": "boot-banner", "status": "PASS", "detail": "ok",
                                                          "log": "logs/boot-banner.log"}) + "\n")
            r = subprocess.run([sys.executable, str(HIL / "write_bench_record.py"), "--out", str(t / "rec.json"),
                                "--firmware-sha", "a" * 40, "--hardware-revision", "MB1364 E-01 / harness A",
                                "--bom-issue", "Issue 7.10", "--p03-revision", "rev19.6", "--fixture-revision", "A",
                                "--operator", "bench-operator", "--run-url", "local", "--image-sha256", "b" * 64,
                                "--results", str(t / "results.jsonl"), "--logs", str(t / "logs")],
                               capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            rec = json.loads((t / "rec.json").read_text())
        schema = json.loads((HIL / "bench-record.schema.json").read_text())
        jsonschema.validate(rec, schema)
        self.assertIsNone(rec["signature"])
        self.assertEqual({i["status"] for i in rec["manual_checklist"]}, {"PENDING"})
        self.assertTrue(any("E-stop" in i["item"] for i in rec["manual_checklist"]))
        self.assertEqual(rec["tests"][0]["status"], "PASS")

    def test_schema_rejects_skip_and_short_sha(self):
        schema = json.loads((HIL / "bench-record.schema.json").read_text())
        rec = {"schema_version": 1, "firmware_sha": "abc", "hardware_revision": "r", "bom_issue": "b",
               "p03_revision": "p", "fixture_revision": "f", "operator": "o", "date": "2026-10-24T00:00:00Z",
               "run_url": "local", "tests": [{"test": "t", "status": "SKIP", "detail": ""}], "raw_logs": [],
               "manual_checklist": [{"item": "x", "status": "PENDING"}], "signature": None}
        with self.assertRaises(jsonschema.ValidationError):
            jsonschema.validate(rec, schema)


if __name__ == "__main__":
    unittest.main()
