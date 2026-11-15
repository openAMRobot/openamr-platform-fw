# SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image.
"""Host tests of the two post-link guards (CMake for the STM32 board, PlatformIO for the
Teensy overlay): each must pass a clean ELF and fail one that carries a simref_ symbol."""
import configparser
import importlib.util
import pathlib
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[3]
CMAKE_GUARD = ROOT / "tools" / "ci" / "simref_symbol_guard.cmake"
PIO_DIR = ROOT / "boards" / "teensy_4_0" / "linorobot2_overlay" / "firmware"
PIO_GUARD = PIO_DIR / "simref_symbol_guard_pio.py"


def _elf(tmp_path, name, source):
    c = tmp_path / f"{name}.c"
    c.write_text(source)
    out = tmp_path / f"{name}.elf"
    subprocess.run(["cc", "-o", str(out), str(c)], check=True)
    return out


@pytest.fixture()
def elfs(tmp_path):
    clean = _elf(tmp_path, "clean", "int main(void){return 0;}\n")
    dirty = _elf(tmp_path, "dirty", "void simref_sm_tick(void); void simref_sm_tick(void){}\n"
                                    "int main(void){simref_sm_tick(); return 0;}\n")
    return clean, dirty


def _cmake_guard(elf, nm="nm"):
    return subprocess.run(["cmake", f"-DELF={elf}", f"-DNM={nm}", "-P", str(CMAKE_GUARD)],
                          capture_output=True, text=True)


def test_cmake_guard_passes_clean_elf(elfs):
    r = _cmake_guard(elfs[0])
    assert r.returncode == 0, r.stderr


def test_cmake_guard_fails_simref_elf(elfs):
    r = _cmake_guard(elfs[1])
    assert r.returncode != 0
    assert "simref_sm_tick" in r.stderr


def test_cmake_guard_fails_missing_elf(tmp_path):
    assert _cmake_guard(tmp_path / "none.elf").returncode != 0


def test_cmake_guard_fails_without_nm(elfs):
    assert _cmake_guard(elfs[0], nm="").returncode != 0


def _pio_module():
    spec = importlib.util.spec_from_file_location("simref_symbol_guard_pio", PIO_GUARD)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def test_pio_guard_detects(elfs):
    mod = _pio_module()
    assert mod.find_simref_symbols("nm", str(elfs[0])) == []
    assert mod.find_simref_symbols("nm", str(elfs[1])) == ["simref_sm_tick"]


def test_pio_guard_fails_the_build(elfs):
    mod = _pio_module()

    class Env:
        exit_code = None

        def subst(self, s):
            return "nm" if s == "$NM" else ""

        def Exit(self, code):  # noqa: N802 (SCons name)
            self.exit_code = code

    env = Env()
    mod._guard(None, [str(elfs[1])], env)
    assert env.exit_code == 1
    env = Env()
    mod._guard(None, [str(elfs[0])], env)
    assert env.exit_code is None


def test_pio_guard_applies_to_every_environment():
    cfg = configparser.ConfigParser(strict=False, interpolation=None)
    cfg.read(PIO_DIR / "platformio.ini")
    assert cfg.get("env", "extra_scripts").strip() == "post:simref_symbol_guard_pio.py"
    envs = [s for s in cfg.sections() if s.startswith("env:")]
    assert "env:teensy40" in envs
    for s in envs:  # no environment replaces the inherited extra_scripts
        assert "extra_scripts" not in cfg._sections[s], s


def test_stm32_board_runs_the_guard_post_build():
    text = (ROOT / "boards" / "stm32" / "OpenAMRobot" / "CMakeLists.txt").read_text()
    assert "POST_BUILD" in text and "tools/ci/simref_symbol_guard.cmake" in text


HEADER = "SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image."


def test_every_reference_file_starts_with_the_header():
    base = ROOT / "tests" / "sim_reference"
    files = [p for p in base.rglob("*") if p.is_file() and "__pycache__" not in p.parts]
    assert len(files) > 20
    for p in files:
        first = p.read_text(encoding="utf-8").splitlines()[0]
        assert HEADER in first, f"{p.relative_to(ROOT)}: first line is {first!r}"
