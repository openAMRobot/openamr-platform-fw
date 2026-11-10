"""Tests for the static-analysis tools in tools/ci. Run: python3 -m unittest discover -s tests/static -v

Needs cppcheck and clang-format (Ubuntu 24.04 packages); the "Firmware host C/C++ tests"
workflow installs both. Fixtures are under tests/static/fixtures.
"""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools" / "ci"
FIX = Path(__file__).resolve().parent / "fixtures"


def run(*cmd, cwd=None, env=None):
    return subprocess.run(list(map(str, cmd)), cwd=cwd, env=env, capture_output=True, text=True)


class ResolveIfdef(unittest.TestCase):
    SRC = ("a\n#ifndef SYM\nkept\n#else\ndropped\n#endif\n#ifdef OTHER\nother\n#endif\n"
           "f(x,\n#ifndef SYM\n  \"raw\"\n#else\n  \"filtered\"\n#endif\n);\n")

    def resolve(self, *flags):
        with tempfile.TemporaryDirectory() as tmp:
            src, dst = Path(tmp) / "s.c", Path(tmp) / "d.c"
            src.write_text(self.SRC)
            r = run(sys.executable, TOOLS / "resolve_ifdef.py", src, dst, "SYM", *flags)
            self.assertEqual(r.returncode, 0, r.stderr)
            return dst.read_text()

    def test_undefined_keeps_ifndef_branch_and_line_numbers(self):
        out = self.resolve()
        self.assertEqual(out.count("\n"), self.SRC.count("\n"))
        self.assertIn("kept", out)
        self.assertNotIn("dropped", out)
        self.assertIn('"raw"', out)
        self.assertNotIn('"filtered"', out)
        self.assertIn("#ifdef OTHER\nother\n#endif", out, "other conditionals stay untouched")
        self.assertEqual(out.splitlines()[2], "kept", "kept line stays on its line number")

    def test_defined_keeps_else_branch(self):
        out = self.resolve("--defined")
        self.assertIn("dropped", out)
        self.assertNotIn("kept", out)


class RunCppcheck(unittest.TestCase):
    def cppcheck(self, fixture, suppressions=None):
        with tempfile.TemporaryDirectory() as tmp:
            sup = Path(tmp) / "sup.txt"
            sup.write_text(suppressions if suppressions is not None else "# none\n")
            summary = Path(tmp) / "summary.md"
            env = dict(os.environ, GITHUB_STEP_SUMMARY=str(summary))
            r = run(sys.executable, TOOLS / "run_cppcheck.py", "--root", FIX / "cppcheck" / fixture,
                    "--suppressions", sup, "--work", Path(tmp) / "work", env=env)
            return r, summary.read_text()

    def test_clean_core_passes_and_records_version(self):
        r, summary = self.cppcheck("clean")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("PASS firmware core: 1 files", r.stdout)
        self.assertRegex(summary, r"### cppcheck \(Cppcheck \d+\.\d+")
        self.assertIn("NOT RUN: teensy overlay: no C/C++ sources", summary)

    def test_new_finding_fails(self):
        r, summary = self.cppcheck("bug")
        self.assertEqual(r.returncode, 1)
        self.assertIn("[arrayIndexOutOfBounds]", r.stdout)
        self.assertIn("FAIL firmware core", summary)

    def test_suppression_with_reason_hides_the_finding(self):
        r, _ = self.cppcheck("bug", "# reason: fixture shows a suppressed finding\n*:*/core.c\n")
        self.assertEqual(r.returncode, 0, r.stdout)

    def test_suppression_without_reason_fails(self):
        r, _ = self.cppcheck("clean", "arrayIndexOutOfBounds:*/core.c\n")
        self.assertEqual(r.returncode, 1)
        self.assertIn("has no '# reason:' line above it", r.stdout)

    def test_repository_suppressions_all_have_reasons(self):
        sys.path.insert(0, str(TOOLS))
        import run_cppcheck
        entries, errors = run_cppcheck.load_suppressions(TOOLS / "cppcheck-suppressions.txt")
        self.assertEqual(errors, [])
        self.assertGreater(len(entries), 0)

    def test_repository_overlay_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            r = run(sys.executable, TOOLS / "run_cppcheck.py", "--root", ROOT, "--work", tmp)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("PASS teensy overlay", r.stdout)


class CheckFormat(unittest.TestCase):
    def check(self, fixture):
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / "firmware"
            shutil.copytree(FIX / "format" / fixture, target) if fixture else target.mkdir()
            shutil.copy(ROOT / "firmware" / ".clang-format", target / ".clang-format")
            return run("bash", TOOLS / "check_format.sh", target)

    def test_formatted_file_passes(self):
        r = self.check("good")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_unformatted_file_fails(self):
        r = self.check("bad")
        self.assertEqual(r.returncode, 1)
        self.assertIn("differ from", r.stdout)

    def test_no_files_is_not_run(self):
        r = self.check(None)
        self.assertEqual(r.returncode, 0)
        self.assertIn("NOT RUN: no C/C++ files", r.stdout)


class CheckSkips(unittest.TestCase):
    def test_skips_without_issue_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            for name in ("test_bad.py", "test_bad.cpp", "test_good.cpp"):
                shutil.copy(FIX / "skips" / name, Path(tmp) / name)
            r = run(sys.executable, TOOLS / "check_skips.py", tmp)
        self.assertEqual(r.returncode, 1)
        self.assertIn("3 skip(s) without a tracking issue", r.stdout)
        self.assertNotIn("test_good.cpp", r.stdout)

    def test_repository_tests_pass(self):
        r = run(sys.executable, TOOLS / "check_skips.py", ROOT / "tests")
        self.assertEqual(r.returncode, 0, r.stdout)


if __name__ == "__main__":
    unittest.main()
