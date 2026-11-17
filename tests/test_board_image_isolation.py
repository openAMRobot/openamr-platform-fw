"""Host tests for tools/ci/check_board_image_isolation.py (SAF-MCU-001).

Host ELFs and linker maps stand in for board images; the board builds run in CI.
Run: python3 -m unittest discover -s tests -v   (needs cc and nm)
"""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
CHECK = REPO / "tools" / "ci" / "check_board_image_isolation.py"


class BoardImageIsolation(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.main = self._obj("Core/Src/main.c", "int main(void){return 0;}\n")

    def tearDown(self):
        self._tmp.cleanup()

    def _obj(self, rel, source, opt="-Os"):
        c = self.tmp / rel
        c.parent.mkdir(parents=True, exist_ok=True)
        c.write_text(source)
        o = c.with_suffix(".c.obj")
        subprocess.run(["cc", opt, "-g0", "-ffunction-sections", "-fdata-sections", "-c", str(c), "-o", str(o)],
                       check=True)
        return o

    def _link(self, *objects):
        elf, m = self.tmp / "image.elf", self.tmp / "image.map"
        subprocess.run(["cc", "-Wl,--gc-sections", f"-Wl,-Map={m}", "-o", str(elf), *map(str, objects)], check=True)
        return elf, m

    def _check(self, *args):
        return subprocess.run([sys.executable, str(CHECK), "--nm", "nm", *map(str, args)],
                              capture_output=True, text=True)

    def test_clean_image_passes(self):
        elf, m = self._link(self.main)
        r = self._check("--elf", elf, "--map", m)
        self.assertEqual(r.returncode, 0, r.stdout)
        self.assertIn("no simulation-reference symbol or object", r.stdout)

    def test_linked_simref_symbol_fails(self):
        o = self._obj("Core/Src/main.c", "void simref_sm_tick(void); void simref_sm_tick(void){}\n"
                                         "int main(void){simref_sm_tick(); return 0;}\n", opt="-O0")
        elf, m = self._link(o)
        r = self._check("--elf", elf, "--map", m)
        self.assertEqual(r.returncode, 1, r.stdout)
        self.assertIn("simulation-reference symbol in board image", r.stdout)
        self.assertIn("simref_sm_tick", r.stdout)

    def test_discarded_reference_object_fails_on_the_map(self):
        dead = self._obj("tests/sim_reference/src/simref_sm.c", "int simref_dead(int x){return x+1;}\n")
        elf, m = self._link(self.main, dead)
        nm = subprocess.run(["nm", "-a", str(elf)], capture_output=True, text=True, check=True).stdout
        self.assertNotIn("simref_", nm)  # --gc-sections left no symbol: the ELF alone shows nothing
        self.assertEqual(self._check("--elf", elf, "--no-map").returncode, 0)
        r = self._check("--elf", elf, "--map", m)
        self.assertEqual(r.returncode, 1, r.stdout)
        self.assertIn("tests/sim_reference/src/simref_sm.c.obj", r.stdout)

    def test_missing_or_invalid_inputs_fail(self):
        elf, m = self._link(self.main)
        self.assertEqual(self._check("--elf", self.tmp / "none.elf", "--map", m).returncode, 2)
        self.assertEqual(self._check("--elf", elf, "--map", self.tmp / "none.map").returncode, 2)
        empty = self.tmp / "empty.map"
        empty.write_text("")
        self.assertEqual(self._check("--elf", elf, "--map", empty).returncode, 2)
        self.assertNotEqual(self._check("--elf", elf).returncode, 0)  # neither --map nor --no-map


if __name__ == "__main__":
    unittest.main()
