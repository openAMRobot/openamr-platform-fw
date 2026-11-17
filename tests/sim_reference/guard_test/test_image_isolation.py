# SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image.
"""Host tests of the SAF-MCU-001 isolation checks: the compile-time guard
(include/simref/simref_host_only.h) and the board image check (tools/ci/check_board_image_isolation.py).
Host ELFs and maps stand in for board images here; the board builds run in CI."""
import pathlib
import subprocess
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[3]
SIMREF = ROOT / "tests" / "sim_reference"
CHECK = ROOT / "tools" / "ci" / "check_board_image_isolation.py"
SOURCES = sorted(p for d in ("src", "adapter", "test") for p in (SIMREF / d).glob("*.c"))


@pytest.mark.parametrize("source", SOURCES, ids=lambda p: p.name)
def test_every_reference_source_refuses_bare_metal_arm(source):
    # __arm__ without __linux__ is what arm-none-eabi-gcc predefines for the STM32 and Teensy targets
    r = subprocess.run(["cc", "-fsyntax-only", "-U__linux__", "-D__arm__", "-I", str(SIMREF / "include"), str(source)],
                       capture_output=True, text=True)
    assert r.returncode != 0
    assert "must not be compiled for a board target" in r.stderr


def test_reference_sources_found():
    assert len(SOURCES) >= 12


def test_hosted_build_is_not_refused():
    r = subprocess.run(["cc", "-fsyntax-only", "-I", str(SIMREF / "include"), str(SIMREF / "src" / "simref_sm.c")],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr


def _link(tmp_path, objects):
    out = tmp_path / "image.elf"
    m = tmp_path / "image.map"
    subprocess.run(["cc", "-Wl,--gc-sections", f"-Wl,-Map={m}", "-o", str(out), *map(str, objects)], check=True)
    return out, m


def _obj(tmp_path, rel, source, opt="-Os"):
    c = tmp_path / rel
    c.parent.mkdir(parents=True, exist_ok=True)
    c.write_text(source)
    o = c.with_suffix(".c.obj")
    subprocess.run(["cc", opt, "-g0", "-ffunction-sections", "-fdata-sections", "-c", str(c), "-o", str(o)], check=True)
    return o


def _check(*args):
    return subprocess.run([sys.executable, str(CHECK), "--nm", "nm", *map(str, args)], capture_output=True, text=True)


@pytest.fixture()
def main_obj(tmp_path):
    return _obj(tmp_path, "Core/Src/main.c", "int main(void){return 0;}\n")


def test_clean_image_passes(tmp_path, main_obj):
    elf, m = _link(tmp_path, [main_obj])
    r = _check("--elf", elf, "--map", m)
    assert r.returncode == 0, r.stdout
    assert "no simulation-reference symbol or object" in r.stdout


def test_linked_symbol_fails(tmp_path):
    o = _obj(tmp_path, "Core/Src/main.c", "void simref_sm_tick(void); void simref_sm_tick(void){}\n"
                                          "int main(void){simref_sm_tick(); return 0;}\n", opt="-O0")
    elf, m = _link(tmp_path, [o])
    r = _check("--elf", elf, "--map", m)
    assert r.returncode == 1
    assert "simulation-reference symbol in board image" in r.stdout and "simref_sm_tick" in r.stdout


def test_discarded_reference_object_fails_on_the_map(tmp_path, main_obj):
    # the gap the ELF symbol guard alone leaves: --gc-sections removes every simref_ symbol
    dead = _obj(tmp_path, "tests/sim_reference/src/simref_sm.c", "int simref_dead(int x){return x+1;}\n")
    elf, m = _link(tmp_path, [main_obj, dead])
    nm = subprocess.run(["nm", "-a", str(elf)], capture_output=True, text=True, check=True).stdout
    assert "simref_" not in nm  # the ELF alone shows nothing
    assert _check("--elf", elf, "--no-map").returncode == 0
    r = _check("--elf", elf, "--map", m)
    assert r.returncode == 1
    assert "simulation-reference object in board link" in r.stdout and "tests/sim_reference/src/simref_sm.c.obj" in r.stdout


def test_missing_inputs_fail(tmp_path, main_obj):
    elf, m = _link(tmp_path, [main_obj])
    assert _check("--elf", tmp_path / "none.elf", "--map", m).returncode == 2
    assert _check("--elf", elf, "--map", tmp_path / "none.map").returncode == 2
    assert _check("--elf", elf).returncode != 0  # neither --map nor --no-map


def test_a_file_that_is_not_a_map_fails(tmp_path, main_obj):
    elf, _ = _link(tmp_path, [main_obj])
    empty = tmp_path / "empty.map"
    empty.write_text("")
    assert _check("--elf", elf, "--map", empty).returncode == 2
