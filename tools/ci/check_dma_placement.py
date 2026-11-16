#!/usr/bin/env python3
"""Fail if Ethernet DMA descriptors are not in D2 SRAM (0x30000000-0x30007FFF)."""
import subprocess, sys
elf = sys.argv[1]
out = subprocess.check_output(["arm-none-eabi-nm", elf], text=True)
syms = {l.split()[2]: int(l.split()[0], 16) for l in out.splitlines() if len(l.split()) == 3}
ok = True
for name in ("DMARxDscrTab", "DMATxDscrTab"):
    if name not in syms:
        print(f"::error::{name} not found in {elf}"); ok = False; continue
    addr = syms[name]
    inside = 0x30000000 <= addr < 0x30008000
    print(f"{name} at 0x{addr:08X} {'OK (RAM_D2)' if inside else 'NOT in RAM_D2'}")
    if not inside:
        print(f"::error::{name} at 0x{addr:08X}: Ethernet DMA cannot reach this address; place it in RAM_D2"); ok = False
sys.exit(0 if ok else 1)
