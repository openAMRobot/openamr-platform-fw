"""Tests for tools/ci/check_ioc_contract.py. Run: python -m unittest discover -s tests -v

Fixtures in tests/fixtures/ioc: pass.ioc (derived from the #22 .ioc without the four
not_required UART TX pins), one fail_*.ioc per error class, an MSP fragment pair for the AF
comparison, and a copy of the openamr-platform-hw contract.
"""
import importlib.util
import io
import json
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[1]
FIX = ROOT / "tests" / "fixtures" / "ioc"
spec = importlib.util.spec_from_file_location("check_ioc_contract", ROOT / "tools" / "ci" / "check_ioc_contract.py")
chk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(chk)
CONTRACT = yaml.safe_load((FIX / "base-controller-io.yaml").read_text(encoding="utf-8"))


def run(name, msp=None):
    text = (FIX / name).read_text(encoding="utf-8")
    msp_text = (FIX / msp).read_text(encoding="utf-8") if msp else None
    findings, warnings, _ = chk.check(text, CONTRACT, msp_text)
    return findings, warnings


class Pass(unittest.TestCase):
    def test_pass_fragment_has_no_errors(self):
        findings, warnings = run("pass.ioc")
        self.assertEqual(findings, [])
        self.assertEqual(warnings, [])

    def test_pass_fragment_with_matching_afs(self):
        findings, _ = run("pass.ioc", "pass_msp.c")
        self.assertEqual(findings, [])

    def test_cli_exit_codes_and_json(self):
        with tempfile.TemporaryDirectory() as tmp:
            report = Path(tmp) / "r.json"
            with redirect_stdout(io.StringIO()):
                ok = chk.main([str(FIX / "pass.ioc"), "--contract", str(FIX / "base-controller-io.yaml"),
                               "--msp", str(FIX / "pass_msp.c"), "--json", str(report)])
            self.assertEqual(ok, 0)
            self.assertEqual(json.loads(report.read_text())["result"], "PASS")
            out = io.StringIO()
            with redirect_stdout(out):
                bad = chk.main([str(FIX / "fail_baud.ioc"), "--contract", str(FIX / "base-controller-io.yaml")])
            self.assertEqual(bad, 1)
            self.assertIn("::error", out.getvalue())


class Parse(unittest.TestCase):
    def test_pin_names_with_suffixes(self):
        _, pins = chk.parse_ioc("PA13(JTMS/SWDIO).Signal=DEBUG_JTMS-SWDIO\nPC14-OSC32_IN.Signal=RCC_OSC32_IN\n")
        self.assertEqual(pins["PA13"]["Signal"], "DEBUG_JTMS-SWDIO")
        self.assertEqual(pins["PC14"]["Signal"], "RCC_OSC32_IN")

    def test_cubemx_defaults(self):
        params, _ = chk.parse_ioc("USART2.VirtualMode-Asynchronous=VM_ASYNC\n")
        self.assertEqual(chk.uart_settings(params, "USART2"),
                         {"baud": 115200, "frame": "8N1", "rx_invert": False, "mode": "tx_rx"})
        self.assertEqual(chk.spi_settings(params, "SPI4")["spi_mode"], 0)

    def test_fdcan_bitrates_of_the_pass_fragment(self):
        params, _ = chk.parse_ioc((FIX / "pass.ioc").read_text())
        self.assertEqual(chk.fdcan_bitrate(params, "FDCAN1"), 500000)
        self.assertEqual(chk.fdcan_bitrate(params, "FDCAN2"), 250000)
        self.assertEqual(chk.fdcan_bitrate(params, "FDCAN3"), 500000)

    def test_msp_afs(self):
        self.assertEqual(chk.parse_msp((FIX / "pass_msp.c").read_text()), {"PB8": 8})


class ErrorClasses(unittest.TestCase):
    def assertRule(self, name, rule, text, msp=None):
        findings, _ = run(name, msp)
        rules = {r for r, _ in findings}
        self.assertIn(rule, rules, findings)
        self.assertIn(text, " | ".join(m for r, m in findings if r == rule))

    def test_signal_on_a_different_pin(self):
        self.assertRule("fail_pin.ioc", "pin", "UART4_RX is on PC11 in the .ioc, contract PB8")

    def test_signal_with_a_different_af(self):
        self.assertRule("pass.ioc", "pin", "UART4_RX on PB8 uses AF4, contract AF8", msp="fail_af_msp.c")

    def test_not_required_pin_with_af(self):
        self.assertRule("fail_not_required.ioc", "not-required", "PA0 carries UART4_TX")

    def test_reserved_pin_used(self):
        self.assertRule("fail_reserved.ioc", "reserved", "PB14 carries SPI2_MISO; the pin is reserved for led_ld3")

    def test_baud_mismatch(self):
        self.assertRule("fail_baud.ioc", "setting", "UART4: baud 19200 in the .ioc, contract 9600")

    def test_inversion_mismatch(self):
        self.assertRule("fail_inversion.ioc", "setting", "UART5: rx_invert False in the .ioc, contract True")

    def test_spi_mode_mismatch(self):
        self.assertRule("fail_spi_mode.ioc", "setting", "SPI4: SPI mode 2 in the .ioc, contract Mode 0")

    def test_spi_clock_above_maximum(self):
        self.assertRule("fail_spi_clock.ioc", "setting", "SPI4: SPI clock 24000000 Hz above the contract maximum 8000000 Hz")

    def test_bitrate_mismatch(self):
        self.assertRule("fail_bitrate.ioc", "setting", "FDCAN2: bitrate 500000 bit/s in the .ioc, contract 250000")

    def test_configured_signal_missing_from_contract(self):
        self.assertRule("fail_missing.ioc", "missing", "PA3 carries ADCx_INP15 in the .ioc, which is not in the contract")

    def test_every_fail_fixture_fails(self):
        for f in sorted(FIX.glob("fail_*.ioc")):
            with self.subTest(fixture=f.name):
                self.assertTrue(run(f.name)[0], f.name)


if __name__ == "__main__":
    unittest.main()
