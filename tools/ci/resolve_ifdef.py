#!/usr/bin/env python3
"""Write an analysis copy of a C/C++ source with #ifdef/#ifndef blocks on one symbol resolved.

Usage: resolve_ifdef.py SRC DST SYMBOL [--defined]

Only blocks whose condition is exactly `#ifdef SYMBOL` or `#ifndef SYMBOL` are resolved; every
other line is copied unchanged. Dropped lines and the resolved directives become empty lines, so
line numbers in DST match SRC. Used for cppcheck, which cannot preprocess a directive inside a
macro argument (firmware.ino, RCCHECK(... #ifndef USE_FAKE_MAG ...), inherited from linorobot2).
The source file itself is never changed.
"""
import re
import sys
from pathlib import Path


def resolve(lines, symbol, defined):
    out, stack = [], []  # stack entries: None (other block) or [keep_now]
    directive = re.compile(r"^\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b\s*(\S*)")
    for line in lines:
        m = directive.match(line)
        kind, arg = (m.group(1), m.group(2)) if m else (None, None)
        keeping = all(s is None or s[0] for s in stack)
        if kind in ("ifdef", "ifndef") and arg == symbol:
            stack.append([defined if kind == "ifdef" else not defined])
            out.append("\n")
            continue
        if kind in ("if", "ifdef", "ifndef"):
            stack.append(None)
        elif kind == "else" and stack and stack[-1] is not None:
            stack[-1][0] = not stack[-1][0]
            out.append("\n")
            continue
        elif kind == "elif" and stack and stack[-1] is not None:
            raise SystemExit(f"#elif in a {symbol} block is not supported")
        elif kind == "endif" and stack:
            if stack.pop() is not None:
                out.append("\n")
                continue
        out.append(line if keeping else "\n")
    if stack:
        raise SystemExit("unbalanced conditionals")
    return out


def main(argv):
    if len(argv) < 3:
        raise SystemExit(__doc__)
    src, dst, symbol = argv[:3]
    lines = Path(src).read_text(encoding="utf-8").splitlines(keepends=True)
    Path(dst).parent.mkdir(parents=True, exist_ok=True)
    Path(dst).write_text("".join(resolve(lines, symbol, "--defined" in argv[3:])), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
