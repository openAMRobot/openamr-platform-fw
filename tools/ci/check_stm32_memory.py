#!/usr/bin/env python3
"""Check the STM32H723 Ethernet descriptor, stack, and initialization policy.

The default board paths are resolved from this file, not from the caller's
working directory.  ``--nm-output`` and the individual source-path options are
intended for deterministic, stdlib-only test fixtures.

Profiles (``--profile``) select which layout rules apply. Only ``memory-init`` exists
in this stage (Ethernet descriptors, stack, SRAM split); later layouts add profiles
that are cumulative, so a later layout always satisfies the earlier ones.

This is static evidence only.  In particular, the vendor HAL call-order check
is source inspection, not runtime instrumentation or a hardware DMA test.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field
from pathlib import Path
import re
import subprocess
import sys
from typing import Sequence


REPO_ROOT = Path(__file__).resolve().parents[2]
BOARD_RELATIVE = Path("boards/stm32/OpenAMRobot")

RX_ADDRESS = 0x30000000
TX_ADDRESS = 0x30000060
DESCRIPTOR_SIZE = 0x60
DESCRIPTOR_ALIGNMENT = 32
SRAM1_START = 0x30000000
SRAM1_SIZE = 0x4000
SRAM1_END = SRAM1_START + SRAM1_SIZE
SRAM2_START = SRAM1_END
SRAM2_SIZE = 0x4000
SRAM2_END = SRAM2_START + SRAM2_SIZE
DTCM_START = 0x20000000
DTCM_SIZE = 0x20000
DTCM_END = DTCM_START + DTCM_SIZE
MIN_STACK_SIZE = 0x1000
DESCRIPTOR_SYMBOLS = {
    "DMARxDscrTab": RX_ADDRESS,
    "DMATxDscrTab": TX_ADDRESS,
}
STACK_SYMBOLS = ("_estack", "_sstack", "_Min_Stack_Size")
DESCRIPTOR_SECTIONS = (".RxDescripSection", ".TxDescripSection")


PROFILES = ("memory-init",)
DEFAULT_PROFILE = "memory-init"
_PROFILE_FEATURES = {
    "memory-init": frozenset(),
}


def _features(profile: str) -> frozenset[str]:
    try:
        return _PROFILE_FEATURES[profile]
    except KeyError:
        raise CheckerError(f"unknown profile {profile!r}; expected one of {', '.join(PROFILES)}") from None


def _required_symbols(profile: str) -> tuple[str, ...]:
    _features(profile)  # rejects unknown profiles
    return (
        *DESCRIPTOR_SYMBOLS,
        *STACK_SYMBOLS,
    )


class CheckerError(Exception):
    """An input file or tool could not be read or executed."""


@dataclass
class Report:
    """Human-readable successful checks and independently collected failures."""

    notes: list[str] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)

    def note(self, message: str) -> None:
        self.notes.append(message)

    def error(self, message: str) -> None:
        self.errors.append(message)

    def extend(self, other: "Report") -> None:
        self.notes.extend(other.notes)
        self.errors.extend(other.errors)

    @property
    def passed(self) -> bool:
        return not self.errors


@dataclass(frozen=True)
class Symbol:
    address: int
    size: int
    kind: str


@dataclass(frozen=True)
class BoardPaths:
    linker: Path
    ioc: Path
    msp_source: Path
    main_source: Path
    hal_eth_source: Path


def parse_nm_output(output: str, profile: str = DEFAULT_PROFILE) -> tuple[dict[str, Symbol], list[str]]:
    """Parse ``nm -S`` rows, retaining duplicates for a useful diagnostic."""
    rows: dict[str, list[Symbol]] = {}
    errors: list[str] = []
    required = set(_required_symbols(profile))
    for line_number, line in enumerate(output.splitlines(), 1):
        fields = line.split()
        if not fields or fields[-1] not in required:
            continue
        # Absolute linker symbols legitimately have no nm size column.
        if len(fields) not in (3, 4):
            errors.append(f"malformed nm row {line_number}: {line}")
            continue
        if not re.fullmatch(r"[0-9A-Fa-f]+", fields[0]):
            errors.append(f"malformed nm row {line_number}: {line}")
            continue
        if len(fields) == 3:
            symbol = Symbol(int(fields[0], 16), 0, fields[1])
            name = fields[2]
        else:
            if not re.fullmatch(r"[0-9A-Fa-f]+", fields[1]) or len(fields[2]) != 1:
                errors.append(f"malformed nm row {line_number}: {line}")
                continue
            symbol = Symbol(int(fields[0], 16), int(fields[1], 16), fields[2])
            name = fields[3]
        rows.setdefault(name, []).append(symbol)

    symbols: dict[str, Symbol] = {}
    for name, values in rows.items():
        if len(values) != 1:
            errors.append(f"{name} occurs {len(values)} times in nm output; expected exactly one")
            continue
        symbols[name] = values[0]
    return symbols, errors


def _extent_intersects(address: int, size: int, start: int, end: int) -> bool:
    return address < end and address + size > start


def check_elf_symbols(nm_output: str, profile: str = DEFAULT_PROFILE) -> Report:
    """Validate exact descriptor extents and absolute linker stack symbols."""
    report = Report()
    symbols, parse_errors = parse_nm_output(nm_output, profile)
    for error in parse_errors:
        report.error(error)

    for name in _required_symbols(profile):
        if name not in symbols:
            report.error(f"required symbol {name} not found exactly once in arm-none-eabi-nm -S output")

    descriptors: dict[str, Symbol] = {}
    for name, expected_address in DESCRIPTOR_SYMBOLS.items():
        symbol = symbols.get(name)
        if symbol is None:
            continue
        descriptors[name] = symbol
        before = len(report.errors)
        if symbol.address != expected_address:
            report.error(
                f"{name} starts at 0x{symbol.address:08X}; expected exactly 0x{expected_address:08X}"
            )
        if symbol.size != DESCRIPTOR_SIZE:
            report.error(
                f"{name} size is 0x{symbol.size:X}; expected exactly 0x{DESCRIPTOR_SIZE:X} (96 bytes)"
            )
        if symbol.address % DESCRIPTOR_ALIGNMENT != 0:
            report.error(f"{name} start 0x{symbol.address:08X} is not 32-byte aligned")
        if symbol.size % DESCRIPTOR_ALIGNMENT != 0:
            report.error(f"{name} size 0x{symbol.size:X} is not 32-byte aligned")

        end = symbol.address + symbol.size
        if symbol.address < SRAM1_START or end > SRAM1_END:
            report.error(
                f"{name} extent [0x{symbol.address:08X}, 0x{end:08X}) is not fully inside "
                f"SRAM1 [0x{SRAM1_START:08X}, 0x{SRAM1_END:08X}); DTCM is not Ethernet DMA memory"
            )
        if _extent_intersects(symbol.address, symbol.size, SRAM2_START, SRAM2_END):
            report.error(
                f"{name} extent enters SRAM2 [0x{SRAM2_START:08X}, 0x{SRAM2_END:08X}); "
                "descriptors must remain in SRAM1"
            )
        if len(report.errors) == before:
            report.note(
                f"{name}: [0x{symbol.address:08X}, 0x{end:08X}), 96 bytes, "
                "32-byte aligned, wholly in SRAM1 (not DTCM or SRAM2)"
            )

    if len(descriptors) == 2:
        rx = descriptors["DMARxDscrTab"]
        tx = descriptors["DMATxDscrTab"]
        if _extent_intersects(rx.address, rx.size, tx.address, tx.address + tx.size):
            report.error(
                "DMARxDscrTab and DMATxDscrTab full extents overlap: "
                f"RX [0x{rx.address:08X}, 0x{rx.address + rx.size:08X}), "
                f"TX [0x{tx.address:08X}, 0x{tx.address + tx.size:08X})"
            )
        else:
            report.note("RX/TX descriptor extents do not overlap; no packet-buffer placement is assumed")

    estack = symbols.get("_estack")
    sstack = symbols.get("_sstack")
    minimum = symbols.get("_Min_Stack_Size")
    if minimum is not None:
        if minimum.address < MIN_STACK_SIZE:
            report.error(
                f"_Min_Stack_Size is 0x{minimum.address:X}; minimum reservation is 0x{MIN_STACK_SIZE:X} (4 KiB)"
            )
        if minimum.size != 0:
            report.note("_Min_Stack_Size is an absolute linker value; its nm size field is ignored")
    if estack is not None and sstack is not None and minimum is not None:
        if estack.address - sstack.address != minimum.address:
            report.error(
                "stack symbols do not satisfy _sstack = _estack - _Min_Stack_Size: "
                f"_estack=0x{estack.address:08X}, _sstack=0x{sstack.address:08X}, "
                f"_Min_Stack_Size=0x{minimum.address:X}"
            )
        if sstack.address < DTCM_START or estack.address > DTCM_END or sstack.address >= estack.address:
            report.error(
                f"main stack [0x{sstack.address:08X}, 0x{estack.address:08X}) is not a valid extent in "
                f"DTCM [0x{DTCM_START:08X}, 0x{DTCM_END:08X})"
            )
        if (
            minimum.address >= MIN_STACK_SIZE
            and estack.address - sstack.address == minimum.address
            and DTCM_START <= sstack.address < estack.address <= DTCM_END
        ):
            report.note(
                f"Main stack: [0x{sstack.address:08X}, 0x{estack.address:08X}), "
                f"reserved 0x{minimum.address:X} bytes in DTCM; stack symbols are consistent"
            )
    return report


def strip_c_comments_and_literals(source: str) -> str:
    """Blank comments and literal contents while preserving offsets/newlines.

    Keeping offsets makes token ordering and balanced-brace extraction simple,
    while preventing commented-out clock enables or calls from counting.
    """
    output: list[str] = []
    index = 0
    state = "code"
    quote = ""
    while index < len(source):
        char = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""
        if state == "code":
            if char == "/" and following == "*":
                output.extend((" ", " "))
                index += 2
                state = "block_comment"
                continue
            if char == "/" and following == "/":
                output.extend((" ", " "))
                index += 2
                state = "line_comment"
                continue
            output.append(char)
            if char in ('"', "'"):
                quote = char
                state = "literal"
            index += 1
            continue
        if state == "line_comment":
            output.append(char if char == "\n" else " ")
            index += 1
            if char == "\n":
                state = "code"
            continue
        if state == "block_comment":
            if char == "*" and following == "/":
                output.extend((" ", " "))
                index += 2
                state = "code"
            else:
                output.append(char if char == "\n" else " ")
                index += 1
            continue

        # A quote itself is retained; its contents and escapes are blanked.
        if char == "\\" and following:
            output.extend((" ", "\n" if following == "\n" else " "))
            index += 2
        else:
            output.append(char if char == quote else ("\n" if char == "\n" else " "))
            index += 1
            if char == quote:
                state = "code"
    return "".join(output)


def _matching_delimiter(source: str, opening: int, left: str, right: str) -> int | None:
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == left:
            depth += 1
        elif source[index] == right:
            depth -= 1
            if depth == 0:
                return index
    return None


def extract_c_function(source: str, name: str) -> str | None:
    """Return one C function body without parsing comments as program text."""
    code = strip_c_comments_and_literals(source)
    for match in re.finditer(rf"\b{re.escape(name)}\s*\(", code):
        open_paren = code.find("(", match.start())
        close_paren = _matching_delimiter(code, open_paren, "(", ")")
        if close_paren is None:
            continue
        open_brace = close_paren + 1
        while open_brace < len(code) and code[open_brace].isspace():
            open_brace += 1
        if open_brace >= len(code) or code[open_brace] != "{":
            continue
        close_brace = _matching_delimiter(code, open_brace, "{", "}")
        if close_brace is not None:
            return code[open_brace + 1 : close_brace]
    return None


def _eth_instance_branch(function_body: str) -> str | None:
    """Return the body of ``if (heth->Instance == ETH) { ... }`` or None (comments already blanked)."""
    match = re.search(r"\bif\s*\(\s*heth\s*->\s*Instance\s*==\s*ETH\s*\)\s*\{", function_body)
    if match is None:
        return None
    opening = match.end() - 1
    closing = _matching_delimiter(function_body, opening, "{", "}")
    if closing is None:
        return None
    return function_body[opening + 1 : closing]


def check_source_policy(msp_source: str, main_source: str, hal_eth_source: str) -> Report:
    """Check application clock/call order and inspect the vendor HAL order."""
    report = Report()
    msp_body = extract_c_function(msp_source, "HAL_ETH_MspInit")
    if msp_body is None:
        report.error("HAL_ETH_MspInit definition with a balanced body was not found")
    else:
        sram1_re = r"__HAL_RCC_(?:D2SRAM1|AHBSRAM1)_CLK_ENABLE\s*\("
        branch = _eth_instance_branch(msp_body)
        if branch is None:
            report.error(
                "HAL_ETH_MspInit has no `if (heth->Instance == ETH)` branch; "
                "the SRAM1 and ETH clock enables must be inside it"
            )
        scope = branch or ""
        sram1 = re.search(sram1_re, scope)
        ethernet = re.search(r"__HAL_RCC_ETH1MAC_CLK_ENABLE\s*\(", scope)
        if ethernet is None:
            report.error("__HAL_RCC_ETH1MAC_CLK_ENABLE() not found inside HAL_ETH_MspInit")
        if sram1 is None:
            if re.search(sram1_re, msp_body) is not None:
                report.error(
                    "__HAL_RCC_D2SRAM1_CLK_ENABLE() is outside the ETH instance branch "
                    "(`if (heth->Instance == ETH)`) of HAL_ETH_MspInit"
                )
            else:
                report.error(
                    "explicit __HAL_RCC_D2SRAM1_CLK_ENABLE() not found inside HAL_ETH_MspInit "
                    "(comments and string literals are ignored)"
                )
        elif ethernet is not None and sram1.start() > ethernet.start():
            report.error("SRAM1 clock enable occurs after ETH1MAC clock enable in HAL_ETH_MspInit")
        elif ethernet is not None:
            report.note(
                "HAL_ETH_MspInit enables SRAM1 before ETH1MAC inside the ETH instance branch "
                "(active C tokens, comments ignored)"
            )

    main_body = extract_c_function(main_source, "main")
    if main_body is None:
        report.error("main definition with a balanced body was not found")
    elif re.search(r"\bMX_ETH_Init\s*\(", main_body) is None:
        report.error("main does not call MX_ETH_Init() (comments and string literals are ignored)")
    else:
        report.note("main calls MX_ETH_Init()")

    hal_body = extract_c_function(hal_eth_source, "HAL_ETH_Init")
    if hal_body is None:
        report.error("vendor HAL_ETH_Init definition with a balanced body was not found")
    else:
        msp_calls = list(
            re.finditer(
                r"(?:\bHAL_ETH_MspInit|\bheth\s*->\s*MspInitCallback)\s*\(\s*heth\s*\)",
                hal_body,
            )
        )
        tx_init = re.search(r"\bETH_DMATxDescListInit\s*\(\s*heth\s*\)", hal_body)
        rx_init = re.search(r"\bETH_DMARxDescListInit\s*\(\s*heth\s*\)", hal_body)
        if not msp_calls or tx_init is None or rx_init is None:
            report.error(
                "could not establish vendor HAL_ETH_Init order: expected an MSP callback/direct call, "
                "ETH_DMATxDescListInit(heth), and ETH_DMARxDescListInit(heth)"
            )
        else:
            first_descriptor = min(tx_init.start(), rx_init.start())
            if not any(call.start() < first_descriptor for call in msp_calls):
                report.error("vendor HAL_ETH_Init initializes descriptors before any visible MSP call")
            else:
                report.note(
                    "Vendor HAL_ETH_Init source calls MSP before TX/RX descriptor initialization "
                    "(source inspection only; not runtime instrumentation)"
                )
    return report


def _parse_linker_integer(token: str) -> int:
    match = re.fullmatch(r"(0[xX][0-9A-Fa-f]+|[0-9]+)([kKmM]?)", token.strip())
    if match is None:
        raise ValueError(f"unsupported integer {token!r}")
    value = int(match.group(1), 0)
    multiplier = {"": 1, "K": 1024, "M": 1024 * 1024}[match.group(2).upper()]
    return value * multiplier


def parse_linker_regions(linker_source: str) -> dict[str, tuple[int, int]]:
    code = strip_c_comments_and_literals(linker_source)
    pattern = re.compile(
        r"\b([A-Za-z_][A-Za-z0-9_]*)\s*(?:\([^)]*\))?\s*:\s*"
        r"ORIGIN\s*=\s*([^,\s]+)\s*,\s*LENGTH\s*=\s*([^\s;]+)",
        re.IGNORECASE,
    )
    regions: dict[str, tuple[int, int]] = {}
    for match in pattern.finditer(code):
        try:
            regions[match.group(1)] = (
                _parse_linker_integer(match.group(2)),
                _parse_linker_integer(match.group(3)),
            )
        except ValueError:
            continue
    return regions


def _single_assignment(source: str, name: str) -> str | None:
    code = strip_c_comments_and_literals(source)
    matches = re.findall(rf"(?m)^\s*{re.escape(name)}\s*=\s*([^;]+);", code)
    return matches[0].strip() if len(matches) == 1 else None


def _section_target(source: str, section: str) -> str | None:
    code = strip_c_comments_and_literals(source)
    match = re.search(
        rf"\.\s*{re.escape(section)}\b[^}}]*\}}\s*>\s*([A-Za-z_][A-Za-z0-9_]*)",
        code,
        re.DOTALL,
    )
    return match.group(1) if match else None


def parse_ioc(ioc_source: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in ioc_source.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#") or "=" not in stripped:
            continue
        key, value = stripped.split("=", 1)
        values[key.strip()] = value.strip()
    return values


def check_linker_and_ioc(linker_source: str, ioc_source: str, profile: str = DEFAULT_PROFILE) -> Report:
    """Require the explicit 16+16 KiB split and matching linker/IOC stacks.

    CubeMX's IOC does not normally represent custom GNU ld MEMORY declarations.
    The split itself is therefore checked in the linker; the IOC is checked for
    the stack value it does own, rather than inventing nonstandard IOC keys.
    """
    report = Report()
    _features(profile)  # rejects unknown profiles
    regions = parse_linker_regions(linker_source)
    sram_regions: dict[str, str] = {}
    for label, expected_origin in (("SRAM1", SRAM1_START), ("SRAM2", SRAM2_START)):
        candidates = sorted(name for name in regions if label in name.upper())
        if len(candidates) != 1:
            report.error(
                f"linker must declare exactly one explicit {label} MEMORY region; found {candidates or 'none'}"
            )
            continue
        name = candidates[0]
        sram_regions[label] = name
        origin, length = regions[name]
        if origin != expected_origin or length != SRAM1_SIZE:
            report.error(
                f"linker {name} is ORIGIN=0x{origin:08X}, LENGTH=0x{length:X}; "
                f"expected ORIGIN=0x{expected_origin:08X}, LENGTH=0x{SRAM1_SIZE:X}"
            )
        else:
            report.note(f"Linker {name}: ORIGIN=0x{origin:08X}, LENGTH=0x{length:X} ({label})")

    for section in ("RxDescripSection", "TxDescripSection"):
        target = _section_target(linker_source, section)
        expected = sram_regions.get("SRAM1")
        if not re.search(rf"\.\s*{re.escape(section)}\b[^}}]*\(NOLOAD\)", linker_source, re.DOTALL):
            report.error(f"linker .{section} does not retain its NOLOAD declaration")
        if target is None:
            report.error(f"linker .{section} output section or >SRAM1 target was not found")
        elif expected is not None and target != expected:
            report.error(f"linker .{section} targets {target}; expected {expected} (SRAM1)")
        else:
            report.note(f"Linker .{section} targets {target}")

    stack_text = _single_assignment(linker_source, "_Min_Stack_Size")
    stack_value: int | None = None
    if stack_text is None:
        report.error("linker must contain exactly one _Min_Stack_Size assignment")
    else:
        try:
            stack_value = _parse_linker_integer(stack_text)
        except ValueError:
            report.error(f"cannot parse linker _Min_Stack_Size value {stack_text!r}")
        else:
            if stack_value < MIN_STACK_SIZE:
                report.error(
                    f"linker _Min_Stack_Size=0x{stack_value:X}; expected at least 0x{MIN_STACK_SIZE:X}"
                )

    sstack = _single_assignment(linker_source, "_sstack")
    if sstack is None or re.fullmatch(r"_estack\s*-\s*_Min_Stack_Size", sstack) is None:
        report.error("linker must define _sstack = _estack - _Min_Stack_Size")
    estack = _single_assignment(linker_source, "_estack")
    if estack is None or re.fullmatch(
        r"ORIGIN\s*\(\s*DTCMRAM\s*\)\s*\+\s*LENGTH\s*\(\s*DTCMRAM\s*\)", estack, re.IGNORECASE
    ) is None:
        report.error("linker main stack top must be ORIGIN(DTCMRAM) + LENGTH(DTCMRAM)")

    ioc = parse_ioc(ioc_source)
    ioc_stack_text = ioc.get("ProjectManager.StackSize")
    if ioc_stack_text is None:
        report.error("IOC ProjectManager.StackSize is missing")
    else:
        try:
            ioc_stack = _parse_linker_integer(ioc_stack_text)
        except ValueError:
            report.error(f"cannot parse IOC ProjectManager.StackSize value {ioc_stack_text!r}")
        else:
            if ioc_stack < MIN_STACK_SIZE:
                report.error(f"IOC stack is 0x{ioc_stack:X}; expected at least 0x{MIN_STACK_SIZE:X}")
            if stack_value is not None and ioc_stack != stack_value:
                report.error(
                    f"IOC/linker stack mismatch: ProjectManager.StackSize=0x{ioc_stack:X}, "
                    f"_Min_Stack_Size=0x{stack_value:X}"
                )
            elif ioc_stack >= MIN_STACK_SIZE and stack_value is not None:
                report.note(f"IOC and linker stack reservations agree: 0x{ioc_stack:X} bytes")
    return report


def check_map_policy(map_source: str, profile: str = DEFAULT_PROFILE) -> Report:
    """Cross-check descriptor sections and reject allocations in reserved SRAM2."""
    report = Report()
    _features(profile)  # rejects unknown profiles
    section_re = re.compile(
        r"(?m)^(\.[A-Za-z_][A-Za-z0-9_.-]*)(?:[ \t]+\(NOLOAD\))?[ \t]*(?:\n[ \t]*)?"
        r"(0x[0-9A-Fa-f]+)\s+(0x[0-9A-Fa-f]+)(?:\s+load address\s+0x[0-9A-Fa-f]+)?\s*$"
    )
    sections = [(match.group(1), int(match.group(2), 16), int(match.group(3), 16))
                for match in section_re.finditer(map_source)]
    expected = {".RxDescripSection": RX_ADDRESS, ".TxDescripSection": TX_ADDRESS}
    for name, address in expected.items():
        matches = [item for item in sections if item[0] == name]
        if len(matches) != 1:
            report.error(f"map must contain exactly one {name} output section with address and size")
            continue
        _, actual_address, size = matches[0]
        if actual_address != address or size != DESCRIPTOR_SIZE:
            report.error(
                f"map {name} is 0x{actual_address:08X}/0x{size:X}; "
                f"expected 0x{address:08X}/0x{DESCRIPTOR_SIZE:X}"
            )
        else:
            report.note(f"Map {name}: address 0x{address:08X}, size 0x60, agrees with ELF policy")
        # GNU ld does not consistently print the script's (NOLOAD) attribute
        # in map output; the linker declaration and ELF symbols are authoritative.

    allocations = [(name, address, size) for name, address, size in sections
                   if size > 0 and _extent_intersects(address, size, SRAM2_START, SRAM2_END)]
    for name, address, size in allocations:
        report.error(
            f"allocated map section {name} at 0x{address:08X}/0x{size:X} enters reserved SRAM2"
        )
    if not allocations:
        report.note("Map contains no allocated output section in reserved SRAM2")

    return report


def run_nm(elf: Path, executable: str = "arm-none-eabi-nm") -> str:
    try:
        completed = subprocess.run(
            [executable, "-S", str(elf)],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
    except OSError as exc:
        raise CheckerError(f"could not execute {executable}: {exc}") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip() or "no diagnostic output"
        raise CheckerError(f"{executable} -S exited {completed.returncode}: {detail}")
    return completed.stdout


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except OSError as exc:
        raise CheckerError(f"could not read {path}: {exc}") from exc


def resolve_board_paths(source_root: Path) -> BoardPaths:
    board = source_root if (source_root / "OpenAMRobot.ioc").is_file() else source_root / BOARD_RELATIVE
    return BoardPaths(
        linker=board / "STM32H723xG_flash.ld",
        ioc=board / "OpenAMRobot.ioc",
        msp_source=board / "Core/Src/stm32h7xx_hal_msp.c",
        main_source=board / "Core/Src/main.c",
        hal_eth_source=board / "Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_eth.c",
    )


def run_checks(
    elf: Path,
    paths: BoardPaths,
    nm_output: str,
    map_path: Path | None = None,
    profile: str = DEFAULT_PROFILE,
) -> Report:
    report = Report()
    _features(profile)
    report.note(f"Policy profile: {profile}")
    report.extend(check_elf_symbols(nm_output, profile))
    if map_path is not None:
        report.extend(check_map_policy(read_text(map_path), profile))
    report.extend(check_linker_and_ioc(read_text(paths.linker), read_text(paths.ioc), profile))
    report.extend(
        check_source_policy(
            read_text(paths.msp_source),
            read_text(paths.main_source),
            read_text(paths.hal_eth_source),
        )
    )
    return report


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path, help="linked OpenAMRobot ELF image")
    parser.add_argument(
        "--profile",
        choices=PROFILES,
        default=DEFAULT_PROFILE,
        help="cumulative layout rules to enforce (default: %(default)s, the strictest)",
    )
    parser.add_argument(
        "--source-root",
        type=Path,
        default=REPO_ROOT,
        help="repository root (or an OpenAMRobot board fixture directory); defaults to the repository",
    )
    parser.add_argument(
        "--map",
        dest="map_path",
        type=Path,
        help="linker map to cross-check (default: ELF path with .map suffix)",
    )
    parser.add_argument("--linker", type=Path, help="override STM32H723xG_flash.ld path")
    parser.add_argument("--ioc", type=Path, help="override OpenAMRobot.ioc path")
    parser.add_argument("--msp-source", type=Path, help="override stm32h7xx_hal_msp.c path")
    parser.add_argument("--main-source", type=Path, help="override main.c path")
    parser.add_argument("--hal-eth-source", type=Path, help="override vendor stm32h7xx_hal_eth.c path")
    parser.add_argument("--nm", default="arm-none-eabi-nm", help="nm executable (default: arm-none-eabi-nm)")
    parser.add_argument(
        "--nm-output",
        type=Path,
        help="read saved nm -S output from this fixture instead of executing nm",
    )
    args = parser.parse_args(argv)
    defaults = resolve_board_paths(args.source_root.resolve())
    args.paths = BoardPaths(
        linker=(args.linker or defaults.linker).resolve(),
        ioc=(args.ioc or defaults.ioc).resolve(),
        msp_source=(args.msp_source or defaults.msp_source).resolve(),
        main_source=(args.main_source or defaults.main_source).resolve(),
        hal_eth_source=(args.hal_eth_source or defaults.hal_eth_source).resolve(),
    )
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        nm_output = read_text(args.nm_output) if args.nm_output else run_nm(args.elf, args.nm)
        map_path = args.map_path or args.elf.with_suffix(".map")
        report = run_checks(args.elf, args.paths, nm_output, map_path, args.profile)
    except CheckerError as exc:
        print(f"::error::{exc}", file=sys.stderr)
        return 2

    for note in report.notes:
        print(f"OK: {note}")
    for error in report.errors:
        print(f"::error::{error}")
    if report.passed:
        print(
            "PASS: STM32H723 descriptor/stack/linker/IOC/source policy checks passed. "
            "No packet buffers assumed; static checks are not hardware validation."
        )
        return 0
    print(f"FAIL: STM32H723 memory policy reported {len(report.errors)} error(s)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
