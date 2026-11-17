#!/usr/bin/env python3
"""SAF-MCU-001: fail if the SIMULATION ONLY reference (tests/sim_reference) reached a board image.

Reads a final board ELF and, where the board links with -Wl,-Map, its map file. Writes nothing.
  ELF: any symbol named simref_* (defined, undefined, local or file) in `nm -a`.
  map: any input object or archive from tests/sim_reference, libsimref*, or simref_*.o/.obj,
       including sections the linker discarded with --gc-sections (an object can be linked in
       and leave no symbol in the ELF).
A missing ELF or map, an ELF with no symbols, or a map without the linker's memory map section
also fails, so the check never passes on nothing.
Usage: check_board_image_isolation.py --elf IMAGE.elf [--map IMAGE.map | --no-map] [--nm NM]
"""
import argparse
import pathlib
import re
import subprocess
import sys

SYMBOL = re.compile(r"(?:^|\s)(simref_\w*)")
OBJECT = re.compile(r"(tests/sim_reference/\S*|\S*libsimref\w*\.(?:a|so)\S*|\S*\bsimref_\w+\.(?:c\.)?(?:o|obj)\b\S*)")
MAP_MARKER = "Linker script and memory map"


def elf_hits(nm, elf):
    out = subprocess.run([nm, "-a", str(elf)], check=True, capture_output=True, text=True).stdout
    lines = [l for l in out.splitlines() if l.strip()]
    if not lines:
        raise ValueError(f"{nm} listed no symbol in {elf}")
    hits = sorted({m.group(1) for l in lines for m in [SYMBOL.search(l)] if m})
    return len(lines), hits


def map_hits(path):
    text = pathlib.Path(path).read_text(encoding="utf-8", errors="replace").replace("\\", "/")
    if MAP_MARKER not in text:
        raise ValueError(f"{path}: not a linker map ('{MAP_MARKER}' missing)")
    lines = text.splitlines()
    hits = sorted({m.group(1) for l in lines for m in [OBJECT.search(l)] if m})
    return len(lines), hits


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", required=True)
    ap.add_argument("--map")
    ap.add_argument("--no-map", action="store_true", help="board links without a map file")
    ap.add_argument("--nm", default="arm-none-eabi-nm")
    a = ap.parse_args(argv)
    if not a.map and not a.no_map:
        ap.error("give --map or, for a board without a map file, --no-map")
    bad = False
    try:
        n, hits = elf_hits(a.nm, a.elf)
        print(f"{a.elf}: {n} symbols read, {len(hits)} simref_ symbol(s)")
        for h in hits:
            print(f"::error::simulation-reference symbol in board image {a.elf}: {h}")
        bad |= bool(hits)
        if a.map:
            n, hits = map_hits(a.map)
            print(f"{a.map}: {n} lines read, {len(hits)} simulation-reference object(s)")
            for h in hits:
                print(f"::error::simulation-reference object in board link {a.map}: {h}")
            bad |= bool(hits)
    except (OSError, ValueError, subprocess.CalledProcessError) as e:
        print(f"::error::board image isolation check could not run: {e}")
        return 2
    print("board image isolation: FAIL" if bad else "board image isolation: no simulation-reference symbol or object")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
