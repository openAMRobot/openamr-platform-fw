# SPDX-License-Identifier: MIT
"""Tests for tools/size/map_report.py.

Two kinds:
- fixture tests on a small hand-written map with known numbers;
- an oracle test that links a small image against the board linker script
  with arm-none-eabi-gcc and checks every region against the linker's own
  --print-memory-usage table. It is skipped (and reported as skipped) when
  arm-none-eabi-gcc is not installed.
"""
from __future__ import annotations

import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

import map_report  # noqa: E402

FIXTURE = HERE / "fixtures" / "minimal_h723.map"
REPO = HERE.parents[2]
BOARD = REPO / "boards" / "stm32" / "OpenAMRobot"


def _region(report, name):
    return next(r for r in report.regions if r.name == name)


@pytest.fixture(scope="module")
def report():
    return map_report.parse_map(FIXTURE.read_text())


def test_regions_from_memory_configuration(report):
    assert [r.name for r in report.regions] == ["DTCMRAM", "RAM_D1", "RAM_D2", "RAM_D3", "ITCMRAM", "FLASH"]
    flash = _region(report, "FLASH")
    assert flash.origin == 0x08000000 and flash.length == 0x100000
    assert _region(report, "RAM_D2").length == 32 * 1024


def test_flash_counts_code_rodata_and_data_load_image_only(report):
    # .isr_vector + .text + .rodata + .data load image; .bss and the heap and
    # stack reservation have no contents and must not count in FLASH.
    assert _region(report, "FLASH").used == 0x2CC + 0x1000 + 0x34 + 0x10
    assert ".bss (load)" not in _region(report, "FLASH").sections
    assert "._user_heap_stack (load)" not in _region(report, "FLASH").sections
    assert _region(report, "FLASH").sections[".data (load)"] == 0x10


def test_dtcm_counts_data_bss_heap_and_stack(report):
    dtcm = _region(report, "DTCMRAM")
    assert dtcm.used == 0x10 + 0x40 + 0x608
    assert dtcm.sections == {".data": 0x10, ".bss": 0x40, "._user_heap_stack": 0x608}


def test_ethernet_descriptors_land_in_ram_d2(report):
    d2 = _region(report, "RAM_D2")
    assert d2.used == 192
    assert d2.sections == {".RxDescripSection": 96, ".TxDescripSection": 96}
    assert _region(report, "RAM_D1").used == 0
    assert _region(report, "RAM_D3").used == 0


def test_debug_and_comment_sections_are_ignored(report):
    itcm = _region(report, "ITCMRAM")  # origin 0, where .debug_* and .comment sit
    assert itcm.used == 0
    assert report.unplaced == []


def test_heap_and_stack_symbols(report):
    assert report.symbols == {"_Min_Heap_Size": 0x200, "_Min_Stack_Size": 0x400}


def test_contributors_grouped_by_object_and_archive(report):
    flash = _region(report, "FLASH").contributors
    assert flash["main.c.obj"] == 0x800 + 0x34
    assert flash["stm32h7xx_hal_eth.c.obj"] == 0x700
    assert flash["libc_nano.a"] == 0x100
    assert flash["startup_stm32h723xx.s.obj"] == 0x2CC
    dtcm = _region(report, "DTCMRAM").contributors
    assert dtcm["*fill*"] == 0x608
    assert dtcm["system_stm32h7xx.c.obj"] == 4


def test_markdown_and_json_output(report):
    md = map_report.to_markdown(report, top=3, title="fixture")
    assert "### fixture" in md
    assert re.search(r"\| RAM_D2 \| 0x30000000 \| 32,768 \| 192 \| 0\.59 \|", md)
    assert "`_Min_Stack_Size` = 0x400 (1024 bytes)" in md
    assert "Largest contributors in FLASH:" in md
    data = json.loads(map_report.to_json(report, top=2))
    flash = next(r for r in data["regions"] if r["name"] == "FLASH")
    assert flash["used"] == 4880
    assert flash["top"][0] == ["main.c.obj", 2100]


def test_rejects_non_map_input(tmp_path):
    bad = tmp_path / "x.map"
    bad.write_text("this is not a linker map\n")
    with pytest.raises(map_report.MapFormatError):
        map_report.parse_map(bad.read_text())
    assert map_report.main([str(bad)]) == 2


def test_rejects_map_without_regions():
    text = "Memory Configuration\n\nName Origin Length Attributes\n\nLinker script and memory map\n"
    with pytest.raises(map_report.MapFormatError):
        map_report.parse_map(text)


def test_rejects_truncated_map():
    text = FIXTURE.read_text().split("Linker script and memory map")[0]
    with pytest.raises(map_report.MapFormatError):
        map_report.parse_map(text)


def test_section_outside_regions_is_reported_not_counted():
    text = FIXTURE.read_text().replace(
        "/DISCARD/", ".stray          0x60000000       0x20\n\n/DISCARD/")
    rep = map_report.parse_map(text)
    assert [s.name for s in rep.unplaced] == [".stray"]
    base = map_report.parse_map(FIXTURE.read_text())
    assert sum(r.used for r in rep.regions) == sum(r.used for r in base.regions)


def test_cli_markdown(capsys):
    assert map_report.main([str(FIXTURE)]) == 0
    out = capsys.readouterr().out
    assert "| FLASH | 0x08000000 | 1,048,576 | 4,880 |" in out


# ---------------------------------------------------------------- oracle ---

_LD_USAGE = re.compile(r"^\s*(?P<name>\S+):\s+(?P<used>\d+)\s*(?P<unit>B|KB|MB|GB)\s+\d+\s*(?:B|KB|MB|GB)\s+[\d.]+%")
_UNIT = {"B": 1, "KB": 1024, "MB": 1024 * 1024, "GB": 1024 ** 3}

ORACLE_C = r"""
#include <stdint.h>
#include <string.h>
volatile uint32_t g_data[5] = {1, 2, 3, 4, 5};
uint8_t g_bss[777];
__attribute__((section(".RxDescripSection"))) uint32_t rx_desc[24];
__attribute__((section(".TxDescripSection"))) uint32_t tx_desc[24];
void SystemInit(void) {}
void ExitRun0Mode(void) {}
int main(void) {
    memset(g_bss, (int)g_data[1], sizeof g_bss);
    rx_desc[0] = g_data[0]; tx_desc[0] = g_data[2];
    for (;;) { g_data[4]++; }
}
"""


@pytest.mark.skipif(shutil.which("arm-none-eabi-gcc") is None, reason="arm-none-eabi-gcc not installed")
def test_matches_linker_memory_usage_with_board_linker_script(tmp_path):
    ld_script = BOARD / "STM32H723xG_flash.ld"
    startup = BOARD / "startup_stm32h723xx.s"
    if not ld_script.exists() or not startup.exists():
        pytest.skip("board linker script or startup file not present")
    src = tmp_path / "oracle.c"
    src.write_text(ORACLE_C)
    mapfile = tmp_path / "oracle.map"
    cmd = [
        "arm-none-eabi-gcc", "-mcpu=cortex-m7", "-mfpu=fpv5-d16", "-mfloat-abi=hard", "-mthumb", "-Os",
        "-ffunction-sections", "-fdata-sections", "--specs=nano.specs", "--specs=nosys.specs",
        "-x", "assembler-with-cpp", str(startup), "-x", "c", str(src),
        "-T", str(ld_script), f"-Wl,-Map={mapfile}", "-Wl,--print-memory-usage",
        "-o", str(tmp_path / "oracle.elf"),
    ]
    res = subprocess.run(cmd, capture_output=True, text=True, check=False)
    assert res.returncode == 0, res.stderr
    ld = {}
    for line in res.stdout.splitlines():
        m = _LD_USAGE.match(line)
        if m:
            ld[m.group("name")] = int(m.group("used")) * _UNIT[m.group("unit")]
    assert ld, res.stdout
    rep = map_report.parse_map(mapfile.read_text())
    ours = {r.name: r.used for r in rep.regions}
    for name, used in ld.items():
        # ld prints whole KB above 1 KB in some versions; compare at that resolution.
        if used % 1024 == 0 and used >= 1024:
            assert ours[name] // 1024 == used // 1024, (name, ours[name], used)
        else:
            assert ours[name] == used, (name, ours[name], used)
    assert ours["RAM_D2"] == 192
    assert ours["DTCMRAM"] > 777
