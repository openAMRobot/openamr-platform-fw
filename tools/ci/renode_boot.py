#!/usr/bin/env python3
"""Experimental: boot the STM32 Debug ELF in Renode and check it reaches main's idle loop.

Not a gate and not simulation evidence. Renode has no STM32H723 platform; this uses Renode's
platforms/cpus/stm32h743.repl plus tests/renode/stm32h723-additions.repl (FDCAN3) and one sysbus
tag (PWR CSR1 ACTVOSRDY, bit 13, read as set). The run passes when, after the given virtual
time, the CPU is in main() and never ended in a fault or error handler (those handlers are
endless loops, so a CPU that entered one is still there). Every register access the platform does
not model is counted and printed, so the summary shows exactly what the emulation skipped.

Usage: renode_boot.py --renode <renode binary> --elf <OpenAMRobot.elf> [--seconds 0.2] [--summary FILE]
Exit 0 PASS, 1 FAIL (fault or error handler, or main not reached), 2 blocked (Renode error).
"""
import argparse
import collections
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ADDITIONS = ROOT / "tests" / "renode" / "stm32h723-additions.repl"
TRAPS = ("HardFault_Handler", "MemManage_Handler", "BusFault_Handler", "UsageFault_Handler",
         "NMI_Handler", "Error_Handler", "Default_Handler")
ANSI = re.compile(r"\x1b\[[0-9;]*m")


def script(elf, seconds, rounds, bare=False):
    lines = ['mach create "openamr"', "machine LoadPlatformDescription @platforms/cpus/stm32h743.repl"]
    if not bare:
        lines += [
            f"machine LoadPlatformDescription @{ADDITIONS}",
            '# PWR CSR1: ACTVOSRDY (bit 13) read as set; ExitRun0Mode waits for it (SMPS start-up of the H723)',
            'sysbus Tag <0x58024804, 0x58024807> "CSR1" 0x2000',
        ]
    lines += [
        f"sysbus LoadELF @{elf}",
        "cpu VectorTableOffset 0x08000000",
        "logLevel 2",
    ]
    for i in range(rounds):
        lines += [f'emulation RunFor "{seconds}"', f'echo "OPENAMR_PC_{i}"', "cpu PC", "sysbus FindSymbolAt `cpu PC`"]
    lines.append("quit")
    return "\n".join(lines) + "\n"


def parse(output):
    text = ANSI.sub("", output)
    samples = re.findall(r"OPENAMR_PC_(\d+)\s*\n\s*(0x[0-9a-fA-F]+)\s*\n\s*(\S+)", text)
    unhandled = collections.Counter()
    for line in text.splitlines():
        m = re.search(r"\[WARNING\] (\S+): (Unhandled (?:read|write) (?:from|to) offset 0x[0-9A-Fa-f]+)", line)
        if m:
            unhandled[f"{m.group(1)} {m.group(2)}"] += 1
        m = re.search(r"(?:Read|Write)\w+ (?:from|to) non existing peripheral at (0x[0-9A-Fa-f]+)", line)
        if m:
            unhandled[f"no peripheral at {m.group(1)}"] += 1
    errors = [l for l in text.splitlines() if "There was an error" in l or "[ERROR]" in l]
    return samples, unhandled, errors


def verdict(samples, errors):
    if errors:
        return 2, "BLOCKED: Renode reported: " + errors[0].strip()
    if not samples:
        return 2, "BLOCKED: no PC sample in the Renode output"
    for _, pc, symbol in samples:
        if symbol in TRAPS:
            return 1, f"FAIL: CPU in {symbol} at {pc}"
    last_pc, last_symbol = samples[-1][1], samples[-1][2]
    if last_symbol == "main":
        return 0, f"PASS: CPU in main at {last_pc}, no fault or error handler"
    return 1, f"FAIL: main not reached; CPU in {last_symbol} at {last_pc}"


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--renode", required=True)
    p.add_argument("--elf", required=True)
    p.add_argument("--seconds", default="0.2", help="virtual time per round")
    p.add_argument("--rounds", type=int, default=3)
    p.add_argument("--summary", help="append a Markdown summary here")
    p.add_argument("--timeout", type=int, default=600)
    p.add_argument("--bare", action="store_true", help="Renode's stm32h743.repl only, without the H723 additions")
    a = p.parse_args(argv)
    with tempfile.TemporaryDirectory() as tmp:
        resc = Path(tmp) / "boot.resc"
        resc.write_text(script(Path(a.elf).resolve(), a.seconds, a.rounds, a.bare))
        r = subprocess.run([a.renode, "--disable-gui", "--console", "-e", f"include @{resc}"],
                           capture_output=True, text=True, timeout=a.timeout)
    output = r.stdout + r.stderr
    samples, unhandled, errors = parse(output)
    code, message = verdict(samples, errors)
    print(message)
    for i, pc, symbol in samples:
        print(f"  after {(int(i) + 1) * float(a.seconds):g} s virtual: PC {pc} in {symbol}")
    print("Not modelled by the platform (accesses ignored or stubbed):")
    for item, n in unhandled.most_common():
        print(f"  {n:4d} x {item}")
    if a.summary:
        with open(a.summary, "a", encoding="utf-8") as fh:
            fh.write("### Renode boot (experimental, not a gate)\n\n" + message + "\n\n")
            fh.write("Platform: Renode stm32h743.repl + tests/renode/stm32h723-additions.repl (FDCAN3) + PWR CSR1 ACTVOSRDY tag.\n\n")
            fh.write("| Not modelled | Accesses |\n|---|---|\n")
            for item, n in unhandled.most_common():
                fh.write(f"| {item} | {n} |\n")
    return code


if __name__ == "__main__":
    sys.exit(main())
