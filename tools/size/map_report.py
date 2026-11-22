#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Memory report per linker region from a GNU ld .map file.

Reads the "Memory Configuration" table and the output sections of a GNU ld
map file (arm-none-eabi-gcc -Wl,-Map) and prints, per region (FLASH, DTCM,
RAM_D1, RAM_D2, RAM_D3, ITCM, or whatever the linker script declares):
used bytes, size, percent, and the output sections placed in it. A section
with a load address in another region (for example .data, run in DTCM,
loaded from FLASH) counts in both, as GNU ld does in --print-memory-usage.

Optional: the largest contributors per region grouped by object file or
archive (--top N), and the linker symbols _Min_Heap_Size and _Min_Stack_Size
when the map defines them.

Pure standard library, no target dependency, so it runs on any CI runner.
Usage:
    map_report.py build/Release/OpenAMRobot.map            # markdown
    map_report.py --format json build/Release/OpenAMRobot.map
    map_report.py --top 10 build/Release/OpenAMRobot.map
Exit code 0 on success, 2 when the file is not a GNU ld map with a memory
configuration.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from typing import Dict, Iterable, List, Optional, Tuple

# Sections that the map lists but that never occupy target memory.
NON_ALLOC_PREFIXES = (
    ".debug",
    ".comment",
    ".ARM.attributes",
    ".gnu.attributes",
    ".stab",
    ".note.gnu.build-id.nonalloc",
    ".gnu_debuglink",
    ".line",
    ".zdebug",
)

_HEX = r"0x[0-9a-fA-F]+"
_MEM_LINE = re.compile(
    r"^(?P<name>\S+)\s+(?P<origin>" + _HEX + r")\s+(?P<length>" + _HEX + r")(?:\s+(?P<attr>\S+))?\s*$"
)
_OUT_FULL = re.compile(
    r"^(?P<name>\.[^\s]+|[A-Za-z_][^\s]*)\s+(?P<addr>" + _HEX + r")\s+(?P<size>" + _HEX + r")"
    r"(?:\s+load address\s+(?P<lma>" + _HEX + r"))?\s*$"
)
_OUT_NAME_ONLY = re.compile(r"^(?P<name>\.[^\s]+)\s*$")
_OUT_CONT = re.compile(
    r"^\s+(?P<addr>" + _HEX + r")\s+(?P<size>" + _HEX + r")"
    r"(?:\s+load address\s+(?P<lma>" + _HEX + r"))?\s*$"
)
_IN_FULL = re.compile(
    r"^ (?P<name>[^\s*][^\s]*)\s+(?P<addr>" + _HEX + r")\s+(?P<size>" + _HEX + r")\s+(?P<obj>\S.*)$"
)
_IN_NAME_ONLY = re.compile(r"^ (?P<name>[^\s*][^\s]*)\s*$")
_IN_CONT = re.compile(r"^\s+(?P<addr>" + _HEX + r")\s+(?P<size>" + _HEX + r")\s+(?P<obj>\S.*)$")
_FILL = re.compile(r"^ \*fill\*\s+(?P<addr>" + _HEX + r")\s+(?P<size>" + _HEX + r")")
_SYMBOL = re.compile(r"^\s+(?P<value>" + _HEX + r")\s+(?P<sym>_Min_Heap_Size|_Min_Stack_Size)\s*=")


class MapFormatError(ValueError):
    """The input is not a GNU ld map file with a memory configuration."""


@dataclass
class Region:
    name: str
    origin: int
    length: int
    used: int = 0
    sections: Dict[str, int] = field(default_factory=dict)
    contributors: Dict[str, int] = field(default_factory=dict)

    def contains(self, addr: int) -> bool:
        return self.origin <= addr < self.origin + self.length

    @property
    def percent(self) -> float:
        return 100.0 * self.used / self.length if self.length else 0.0


@dataclass
class OutputSection:
    name: str
    addr: int
    size: int
    lma: Optional[int]


@dataclass
class MapReport:
    regions: List[Region]
    sections: List[OutputSection]
    symbols: Dict[str, int]
    unplaced: List[OutputSection]


# Output sections that occupy no bytes at their load address. GNU ld prints a
# "load address" for them but --print-memory-usage does not count it, because
# they have no contents (NOBITS or NOLOAD). The map file does not carry the
# section type, so this is a name rule; check it against the ld summary.
_NO_CONTENT = re.compile(r"^\.(t?bss|noinit|heap|stack|_user_heap_stack)(\b|[._])|^\._?user_heap_stack$")


def _has_load_contents(name: str) -> bool:
    return _NO_CONTENT.match(name) is None


def _is_alloc(name: str) -> bool:
    return not any(name.startswith(p) for p in NON_ALLOC_PREFIXES)


def _region_for(regions: Iterable[Region], addr: int) -> Optional[Region]:
    for region in regions:
        if region.name == "*default*":
            continue
        if region.contains(addr):
            return region
    return None


def _contributor_key(obj: str) -> str:
    """Group by archive (lib.a) when the object is an archive member, else by file name."""
    obj = obj.strip()
    m = re.match(r"^(?P<archive>.*?\.a)\((?P<member>[^)]*)\)$", obj)
    if m:
        return m.group("archive").rsplit("/", 1)[-1]
    return obj.rsplit("/", 1)[-1]


def parse_map(text: str) -> MapReport:
    lines = text.splitlines()
    try:
        start = next(i for i, l in enumerate(lines) if l.strip() == "Memory Configuration")
    except StopIteration as exc:
        raise MapFormatError("no 'Memory Configuration' section") from exc

    regions: List[Region] = []
    i = start + 1
    while i < len(lines) and not lines[i].startswith("Linker script and memory map"):
        m = _MEM_LINE.match(lines[i])
        if m and m.group("name") != "Name" and m.group("name") != "*default*":
            regions.append(Region(m.group("name"), int(m.group("origin"), 16), int(m.group("length"), 16)))
        i += 1
    if not regions:
        raise MapFormatError("memory configuration lists no regions")
    if i >= len(lines):
        raise MapFormatError("no 'Linker script and memory map' section")

    sections: List[OutputSection] = []
    unplaced: List[OutputSection] = []
    symbols: Dict[str, int] = {}
    current: Optional[OutputSection] = None
    pending_out: Optional[str] = None
    pending_in: Optional[str] = None

    def add_contrib(addr: int, size: int, obj: str) -> None:
        if current is None or size == 0 or not _is_alloc(current.name):
            return
        region = _region_for(regions, addr)
        if region is None:
            return
        key = _contributor_key(obj)
        region.contributors[key] = region.contributors.get(key, 0) + size

    for line in lines[i + 1:]:
        sm = _SYMBOL.match(line)
        if sm:
            symbols[sm.group("sym")] = int(sm.group("value"), 16)
            continue
        if pending_out is not None:
            pm = _OUT_CONT.match(line)
            name = pending_out
            pending_out = None
            if pm:
                current = OutputSection(name, int(pm.group("addr"), 16), int(pm.group("size"), 16),
                                        int(pm.group("lma"), 16) if pm.group("lma") else None)
                sections.append(current)
                continue
        if pending_in is not None:
            pm = _IN_CONT.match(line)
            pending_in = None
            if pm:
                add_contrib(int(pm.group("addr"), 16), int(pm.group("size"), 16), pm.group("obj"))
                continue
        if line and not line[0].isspace():
            m = _OUT_FULL.match(line)
            if m and (m.group("name").startswith(".") or m.group("name").startswith("_")):
                current = OutputSection(m.group("name"), int(m.group("addr"), 16), int(m.group("size"), 16),
                                        int(m.group("lma"), 16) if m.group("lma") else None)
                sections.append(current)
                continue
            m = _OUT_NAME_ONLY.match(line)
            if m:
                pending_out = m.group("name")
                continue
            # Any other column-0 line (LOAD, OUTPUT, /DISCARD/, START GROUP...) ends the section.
            current = None
            continue
        m = _IN_FULL.match(line)
        if m:
            add_contrib(int(m.group("addr"), 16), int(m.group("size"), 16), m.group("obj"))
            continue
        m = _FILL.match(line)
        if m:
            add_contrib(int(m.group("addr"), 16), int(m.group("size"), 16), "*fill*")
            continue
        m = _IN_NAME_ONLY.match(line)
        if m:
            pending_in = m.group("name")
            continue

    for sec in sections:
        if sec.size == 0 or not _is_alloc(sec.name):
            continue
        vma_region = _region_for(regions, sec.addr)
        if vma_region is None:
            unplaced.append(sec)
            continue
        vma_region.used += sec.size
        vma_region.sections[sec.name] = vma_region.sections.get(sec.name, 0) + sec.size
        if sec.lma is not None and sec.lma != sec.addr and _has_load_contents(sec.name):
            lma_region = _region_for(regions, sec.lma)
            if lma_region is not None and lma_region is not vma_region:
                lma_region.used += sec.size
                key = sec.name + " (load)"
                lma_region.sections[key] = lma_region.sections.get(key, 0) + sec.size
    return MapReport(regions, sections, symbols, unplaced)


def _fmt_bytes(n: int) -> str:
    return f"{n:,}"


def to_markdown(report: MapReport, top: int = 0, title: Optional[str] = None) -> str:
    out: List[str] = []
    if title:
        out.append(f"### {title}")
        out.append("")
    out.append("| Region | Origin | Size (bytes) | Used (bytes) | Used % | Output sections |")
    out.append("|---|---|---:|---:|---:|---|")
    for r in report.regions:
        secs = ", ".join(f"{n} {s}" for n, s in sorted(r.sections.items(), key=lambda kv: -kv[1])) or "none"
        out.append(f"| {r.name} | 0x{r.origin:08X} | {_fmt_bytes(r.length)} | {_fmt_bytes(r.used)} | "
                   f"{r.percent:.2f} | {secs} |")
    if report.symbols:
        out.append("")
        for sym in sorted(report.symbols):
            out.append(f"- `{sym}` = 0x{report.symbols[sym]:X} ({report.symbols[sym]} bytes)")
    if report.unplaced:
        out.append("")
        out.append("Sections outside every declared region: " +
                   ", ".join(f"{s.name} at 0x{s.addr:08X} ({s.size})" for s in report.unplaced))
    if top > 0:
        for r in report.regions:
            if not r.contributors:
                continue
            out.append("")
            out.append(f"Largest contributors in {r.name}:")
            out.append("")
            out.append("| Object or archive | Bytes |")
            out.append("|---|---:|")
            for name, size in sorted(r.contributors.items(), key=lambda kv: (-kv[1], kv[0]))[:top]:
                out.append(f"| {name} | {_fmt_bytes(size)} |")
    return "\n".join(out) + "\n"


def to_json(report: MapReport, top: int = 0) -> str:
    data = {
        "regions": [
            {
                "name": r.name,
                "origin": r.origin,
                "length": r.length,
                "used": r.used,
                "percent": round(r.percent, 2),
                "sections": dict(sorted(r.sections.items())),
                **({"top": sorted(r.contributors.items(), key=lambda kv: (-kv[1], kv[0]))[:top]} if top else {}),
            }
            for r in report.regions
        ],
        "symbols": report.symbols,
        "unplaced": [{"name": s.name, "addr": s.addr, "size": s.size} for s in report.unplaced],
    }
    return json.dumps(data, indent=2, sort_keys=False) + "\n"


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("map", help="GNU ld .map file")
    ap.add_argument("--format", choices=("markdown", "json"), default="markdown")
    ap.add_argument("--top", type=int, default=0, help="list the N largest contributors per region")
    ap.add_argument("--title", default=None, help="markdown heading above the table")
    args = ap.parse_args(argv)
    try:
        with open(args.map, encoding="utf-8", errors="replace") as fh:
            report = parse_map(fh.read())
    except MapFormatError as exc:
        print(f"map_report: {args.map}: {exc}", file=sys.stderr)
        return 2
    if args.format == "json":
        sys.stdout.write(to_json(report, args.top))
    else:
        sys.stdout.write(to_markdown(report, args.top, args.title))
    return 0


if __name__ == "__main__":
    sys.exit(main())
