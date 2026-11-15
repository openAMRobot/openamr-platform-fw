# PlatformIO post-link guard (extra_scripts = post:simref_symbol_guard_pio.py in
# platformio.ini, applies to every environment). The build fails if any symbol whose name
# starts with simref_ (the SIMULATION ONLY reference of openamr-platform-fw tests/sim_reference)
# is in the firmware ELF. It only reads the ELF. OpenAMRobot, MIT.
import re
import subprocess

PATTERN = re.compile(r"(?:^|\s)(simref_\w*)")


def find_simref_symbols(nm, elf):
    """Return the simref_ symbols nm lists for elf. Raises on a failing nm."""
    out = subprocess.run([nm, "-a", elf], check=True, capture_output=True, text=True).stdout
    return sorted({m.group(1) for line in out.splitlines() for m in [PATTERN.search(line)] if m})


def _guard(source, target, env):
    elf = str(target[0])
    nm = env.subst("$NM") or env.subst("${CC}").replace("gcc", "nm")
    hits = find_simref_symbols(nm, elf)
    if hits:
        print("simref symbol guard: %s contains simulation-reference symbols: %s" % (elf, ", ".join(hits)))
        env.Exit(1)
    print("simref symbol guard: %s: no simref_ symbol" % elf)


try:
    Import("env")  # noqa: F821 (SCons)
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", _guard)  # noqa: F821
except NameError:
    pass  # imported as a module by the host test
