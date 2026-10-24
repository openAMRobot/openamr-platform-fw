"""Tests for the nightly tools: write_evidence.py (copy of openamr-platform-hw) and renode_boot.py.
Run: python3 -m unittest discover -s tests/nightly -v
"""
import importlib.util
import io
import json
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

import jsonschema

ROOT = Path(__file__).resolve().parents[2]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / "ci" / f"{name}.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


ev = load("write_evidence")
rb = load("renode_boot")
SCHEMA = json.loads((ROOT / "tools" / "ci" / "ci-evidence.schema.json").read_text())


def evidence(checks):
    with tempfile.TemporaryDirectory() as tmp:
        t = Path(tmp)
        (t / "checks.tsv").write_text(checks)
        (t / "firmware.hex").write_bytes(b":00000001FF\n")
        with redirect_stdout(io.StringIO()):
            ev.main(["--out", str(t / "e.json"), "--repository", "openAMRobot/openamr-platform-fw", "--sha", "a" * 40,
                     "--ref", "main", "--workflow", "Firmware nightly", "--run-url", "local",
                     "--input", f"openAMRobot/openamr-platform-hw={'b' * 40}", "--checks", str(t / "checks.tsv"),
                     "--register-sha", "c" * 40, "--artifact", str(t / "firmware.hex")])
        return json.loads((t / "e.json").read_text())


class Evidence(unittest.TestCase):
    def test_firmware_record_validates_with_artifacts(self):
        r = evidence("teensy-build\tPASS\tok\nstm32-build-debug\tNOT RUN\tno STM32 project\n")
        jsonschema.validate(r, SCHEMA)
        self.assertEqual(r["result"], "INCOMPLETE")
        self.assertIsNone(r["contract"])
        self.assertEqual(len(r["artifacts"]), 1)

    def test_experimental_renode_failure_is_not_a_gate(self):
        r = evidence("teensy-build\tPASS\tok\nrenode-boot\tFAIL\tblocked\texperimental\n")
        self.assertEqual(r["result"], "PASS")

    def test_any_gating_fail_is_fail(self):
        self.assertEqual(evidence("a\tPASS\nram-d2-debug\tFAIL\tnot in RAM_D2\n")["result"], "FAIL")


RENODE_PASS = """
OPENAMR_PC_0
0x800804e
main
OPENAMR_PC_1
0x8008050
main
13:13:51.9 [WARNING] rcc: Unhandled read from offset 0x18. (10)
13:13:51.9 [WARNING] sysbus: [cpu: 0x80054DA] ReadDoubleWord from non existing peripheral at 0x4000D418, returning 0x0.
"""


class RenodeBoot(unittest.TestCase):
    def test_main_reached_is_pass(self):
        samples, unhandled, errors = rb.parse(RENODE_PASS)
        self.assertEqual(rb.verdict(samples, errors)[0], 0)
        self.assertEqual(unhandled["rcc Unhandled read from offset 0x18"], 1)
        self.assertEqual(unhandled["no peripheral at 0x4000D418"], 1)

    def test_error_handler_is_fail(self):
        samples, _, errors = rb.parse("OPENAMR_PC_0\n0x8008a6e\nError_Handler\n")
        code, message = rb.verdict(samples, errors)
        self.assertEqual(code, 1)
        self.assertIn("Error_Handler", message)

    def test_hardfault_anywhere_is_fail(self):
        samples, _, errors = rb.parse("OPENAMR_PC_0\n0x8008a78\nHardFault_Handler\nOPENAMR_PC_1\n0x800804e\nmain\n")
        self.assertEqual(rb.verdict(samples, errors)[0], 1)

    def test_startup_stall_is_fail(self):
        samples, _, errors = rb.parse("OPENAMR_PC_0\n0x8000996\nExitRun0Mode\n")
        code, message = rb.verdict(samples, errors)
        self.assertEqual(code, 1)
        self.assertIn("main not reached", message)

    def test_renode_error_is_blocked(self):
        samples, _, errors = rb.parse("There was an error executing command 'x'\n")
        self.assertEqual(rb.verdict(samples, errors)[0], 2)

    def test_script_loads_the_h723_additions(self):
        s = rb.script(Path("/x.elf"), "0.2", 2)
        self.assertIn("stm32h723-additions.repl", s)
        self.assertIn('sysbus Tag <0x58024804, 0x58024807> "CSR1" 0x2000', s)
        self.assertEqual(s.count("emulation RunFor"), 2)
        self.assertNotIn("stm32h723-additions", rb.script(Path("/x.elf"), "0.2", 1, bare=True))


if __name__ == "__main__":
    unittest.main()
