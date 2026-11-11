"""Tests for tools/ci/check_vendor_provenance.py (offline, with local mirrors).
Run: python -m unittest discover -s tests -v
"""
import importlib.util
import io
import shutil
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIX = ROOT / "tests" / "fixtures" / "vendor"
spec = importlib.util.spec_from_file_location("check_vendor_provenance", ROOT / "tools" / "ci" / "check_vendor_provenance.py")
chk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(chk)


class Workspace:
    def __init__(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        shutil.copytree(FIX / "project", self.root / "project")
        shutil.copytree(FIX / "upstream", self.root / "upstream")

    def run(self):
        up = self.root / "upstream"
        out = io.StringIO()
        with redirect_stdout(out):
            code = chk.main([str(self.root / "project"), "--mirror", f"{chk.CUBE}={up / 'cube'}",
                             "--mirror", f"{chk.HAL}={up / 'hal'}", "--mirror", f"{chk.CMSIS}={up / 'cmsis'}"])
        self.tmp.cleanup()
        return code, out.getvalue()


class Versions(unittest.TestCase):
    def test_versions_from_defines_and_ioc(self):
        self.assertEqual(chk.versions(FIX / "project"), {"package": "1.13.0", "hal": "1.11.6", "cmsis": "1.10.7"})

    def test_missing_define_is_an_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "x.c"
            p.write_text("#define __STM32H7xx_HAL_VERSION_MAIN (0x01UL)\n")
            with self.assertRaises(ValueError):
                chk.version_from(p, "__STM32H7xx_HAL_VERSION")


class Compare(unittest.TestCase):
    def test_identical_after_line_endings_passes_and_reports_not_checked(self):
        code, out = Workspace().run()
        self.assertEqual(code, 0, out)
        self.assertIn("PASS", out)
        self.assertIn("NOT CHECKED:", out)
        self.assertIn("Drivers/BSP/STM32H7xx_Nucleo/stm32h7xx_nucleo.h: outside the HAL driver and CMSIS", out)
        self.assertIn("Drivers/STM32H7xx_HAL_Driver/LICENSE.txt: only in the ST zip package", out)

    def test_edited_hal_file_fails_naming_it(self):
        ws = Workspace()
        f = ws.root / "project/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart.c"
        f.write_bytes(f.read_bytes().replace(b"return 0", b"return 1"))
        code, out = ws.run()
        self.assertEqual(code, 1)
        self.assertIn("Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart.c: differs from stm32h7xx-hal-driver", out)

    def test_edited_cmsis_core_file_fails(self):
        ws = Workspace()
        (ws.root / "project/Drivers/CMSIS/Include/core_cm7.h").write_text("/* edited */\n")
        code, out = ws.run()
        self.assertEqual(code, 1)
        self.assertIn("Drivers/CMSIS/Include/core_cm7.h: differs from STM32CubeH7", out)

    def test_file_unknown_upstream_fails(self):
        ws = Workspace()
        (ws.root / "project/Drivers/STM32H7xx_HAL_Driver/Src/extra.c").write_text("int x;\n")
        code, out = ws.run()
        self.assertEqual(code, 1)
        self.assertIn("Drivers/STM32H7xx_HAL_Driver/Src/extra.c: not in stm32h7xx-hal-driver", out)

    def test_no_project_is_not_run(self):
        with tempfile.TemporaryDirectory() as tmp, redirect_stdout(io.StringIO()) as out:
            import os
            cwd = os.getcwd()
            os.chdir(tmp)
            try:
                code = chk.main([])
            finally:
                os.chdir(cwd)
        self.assertEqual(code, 0)
        self.assertIn("NOT RUN: no STM32 project", out.getvalue())


if __name__ == "__main__":
    unittest.main()
