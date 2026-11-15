# SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image.
"""Fails if any file under firmware/ or boards/ includes, links or names the simulation
reference. The only allowed mention is the post-link symbol guard (tools/ci/simref_symbol_guard.*),
which reads a board ELF and never adds anything to it."""
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1]).resolve()
pattern = re.compile(r"sim_reference|simref/|simref_[a-z]|libsimref|simref\.h")
allowed = re.compile(r"simref_symbol_guard")
bad = []
checked = 0
for top in ("firmware", "boards"):
    for p in (root / top).rglob("*"):
        if not p.is_file() or "/Drivers/" in str(p) or p.name.startswith("simref_symbol_guard"):
            continue
        checked += 1
        try:
            text = p.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        for n, line in enumerate(text.splitlines(), 1):
            if pattern.search(line) and not allowed.search(line):
                bad.append(f"{p.relative_to(root)}:{n}: {line.strip()}")
print(f"checked {checked} files under firmware/ and boards/")
if checked == 0:
    print("no files checked")
    sys.exit(1)
for b in bad:
    print("uses the simulation reference:", b)
sys.exit(1 if bad else 0)
