#!/usr/bin/env python3
"""Fail when a test skip or expected failure does not name an open tracking issue on its line.

Scans tests/ (Python and C/C++) for GTEST_SKIP, DISABLED_ test names, unittest skip / skipIf /
skipUnless / expectedFailure, pytest skip / xfail / importorskip. Each line must contain an issue
reference: a github.com/<owner>/<repo>/issues/<n> URL. Fixture folders are not scanned.
"""
import argparse
import re
import sys
from pathlib import Path

MARKER = re.compile(r"\bGTEST_SKIP\b|\bTEST(?:_F|_P)?\s*\(\s*\w+\s*,\s*DISABLED_|\bDISABLED_\w+\s*,|"
                    r"(?:@|\bunittest\.)skip(?:If|Unless)?\s*\(|\bexpectedFailure\b|\bpytest\.(?:skip|xfail|importorskip)\b|"
                    r"\bself\.skipTest\s*\(")
ISSUE = re.compile(r"github\.com/[\w.-]+/[\w.-]+/issues/\d+")
SUFFIXES = {".py", ".c", ".cc", ".cpp", ".h", ".hpp"}


def findings(root):
    out = []
    for path in sorted(Path(root).rglob("*")):
        if path.suffix not in SUFFIXES or "fixtures" in path.parts or not path.is_file():
            continue
        for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            if MARKER.search(line) and not ISSUE.search(line) and "check_skips: ignore" not in line:
                out.append(f"{path}:{number}: skip without a tracking issue on the same line: {line.strip()}")
    return out


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("root", nargs="?", default="tests")
    a = p.parse_args(argv)
    found = findings(a.root)
    for f in found:
        print(f"::error::{f}")
    print(f"{len(found)} skip(s) without a tracking issue under {a.root}")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
