#!/usr/bin/env python3
"""cppcheck over project C/C++ code; any finding not in the suppressions file fails.

Analysed (when present): the Gate A overlay (boards/teensy_4_0/linorobot2_overlay: *.cpp and the
firmware.ino), boards/stm32/*/Core/Src, and firmware/ (the portable C core). Never analysed:
vendored STMicroelectronics files (Drivers/), upstream linorobot2 files and generated build
output. firmware.ino is analysed through a copy with USE_FAKE_MAG resolved (resolve_ifdef.py),
because cppcheck cannot preprocess the #ifndef inside the RCCHECK() argument; line numbers match.

Every suppression entry needs a "# reason:" line above it. The cppcheck version is printed and
appended to the job summary. A part with no files is reported as NOT RUN, never PASS.
Exit 1 on any unsuppressed finding or a suppression without a reason.
"""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
CHECKS = "warning,style,performance,portability"


def load_suppressions(path):
    """Return (entries, errors); every entry must follow a '# reason:' comment line."""
    entries, errors, reason = [], [], False
    for number, raw in enumerate(Path(path).read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line:
            reason = False
            continue
        if line.startswith("#"):
            reason = line.lower().startswith("# reason:") and len(line) > len("# reason:") + 3
            continue
        if not reason:
            errors.append(f"{path}:{number}: suppression '{line}' has no '# reason:' line above it")
        entries.append(line)
        reason = False
    return entries, errors


def sources(root):
    parts = {}
    overlay = root / "boards" / "teensy_4_0" / "linorobot2_overlay"
    parts["teensy overlay"] = sorted(p for p in overlay.rglob("*") if p.suffix in {".cpp", ".c", ".ino"}) \
        if overlay.is_dir() else []
    parts["stm32 Core"] = sorted(p for p in root.glob("boards/stm32/*/Core/Src/*") if p.suffix in {".c", ".cpp"})
    parts["firmware core"] = sorted(p for p in (root / "firmware").rglob("*") if p.suffix in {".c", ".cpp"}) \
        if (root / "firmware").is_dir() else []
    return parts


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--root", default=".")
    p.add_argument("--suppressions", default=str(HERE / "cppcheck-suppressions.txt"))
    p.add_argument("--work", default="build/cppcheck")
    p.add_argument("--cppcheck", default="cppcheck")
    a = p.parse_args(argv)
    root = Path(a.root).resolve()
    work = Path(a.work).resolve()
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    lines = []

    entries, errors = load_suppressions(a.suppressions)
    for e in errors:
        print(f"::error::{e}")
    version = subprocess.run([a.cppcheck, "--version"], capture_output=True, text=True, check=True).stdout.strip()
    print(version)
    lines.append(f"### cppcheck ({version})")

    failed = bool(errors)
    for part, files in sources(root).items():
        if not files:
            print(f"::warning title=cppcheck::NOT RUN: {part}: no C/C++ sources")
            lines.append(f"- NOT RUN: {part}: no C/C++ sources")
            continue
        targets, includes = [], set()
        for f in files:
            if f.name == "firmware.ino":
                copy = work / f.relative_to(root)
                subprocess.run([sys.executable, str(HERE / "resolve_ifdef.py"), str(f), str(copy), "USE_FAKE_MAG"],
                               check=True)
                targets.append(copy)
            else:
                targets.append(f)
            for inc in ("Inc", "config", "lib/pid", "include"):
                for base in (f.parent, f.parent.parent):
                    if (base / inc).is_dir():
                        includes.add(base / inc)
        language = ["--language=c++", "--std=c++17"] if part == "teensy overlay" else ["--std=c11"]
        defines = ["-DSTM32H723xx", "-DUSE_HAL_DRIVER"] if part == "stm32 Core" else []
        cmd = [a.cppcheck, f"--enable={CHECKS}", "--inline-suppr", "--error-exitcode=1", "--quiet",
               f"--suppressions-list={a.suppressions}",
               "--template={file}:{line}:{column}: {severity}: {message} [{id}]",
               *language, *defines, *[f"-I{i}" for i in sorted(includes)], *map(str, targets)]
        r = subprocess.run(cmd, capture_output=True, text=True)
        output = (r.stdout + r.stderr).replace(str(work) + "/", "").replace(str(root) + "/", "")
        for out_line in output.splitlines():
            m = re.match(r"^(.+?):(\d+):(\d+): (.*)$", out_line)
            if m:
                print(f"::error file={m.group(1)},line={m.group(2)},col={m.group(3)}::cppcheck: {m.group(4)}")
            elif out_line.strip():
                print(out_line)
        status = "FAIL" if r.returncode else "PASS"
        failed |= bool(r.returncode)
        print(f"{status} {part}: {len(files)} files")
        lines.append(f"- {status} {part}: {len(files)} files")
    lines.append(f"- suppressions: {len(entries)} entries, each with a reason")
    if summary:
        with open(summary, "a", encoding="utf-8") as fh:
            fh.write("\n".join(lines) + "\n")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
