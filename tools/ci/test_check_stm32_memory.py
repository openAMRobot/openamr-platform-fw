#!/usr/bin/env python3
"""Stdlib unittest fixtures for check_stm32_memory (no real board edits)."""

from __future__ import annotations

import contextlib
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

# Also support ``python3 -m unittest tools/ci/test_check_stm32_memory.py``.
sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_stm32_memory as memory


VALID_NM = """\
30000000 00000060 B DMARxDscrTab
30000060 00000060 B DMATxDscrTab
300000c0 00000100 B sensor_usart2_rx_buffer
300001c0 00000100 B sensor_usart6_rx_buffer
20020000 R _estack
2001f000 A _sstack
00001000 A _Min_Stack_Size
38800000 A __fault_retained_start
38800080 A __fault_retained_end
00000080 A __fault_retained_size
08000000 00000004 T unrelated_symbol
"""

VALID_LINKER = """\
MEMORY
{
  DTCMRAM (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
  RAM_D1 (xrw) : ORIGIN = 0x24000000, LENGTH = 320K
  RAM_D2_SRAM1 (xrw) : ORIGIN = 0x30000000, LENGTH = 16K
  RAM_D2_SRAM2 (xrw) : ORIGIN = 0x30004000, LENGTH = 16K
  BACKUP_SRAM (xrw) : ORIGIN = 0x38800000, LENGTH = 4K
}
_estack = ORIGIN(DTCMRAM) + LENGTH(DTCMRAM);
_sstack = _estack - _Min_Stack_Size;
_Min_Stack_Size = 0x1000;
SECTIONS
{
  .RxDescripSection (NOLOAD) : { . = ALIGN(32); *(.RxDescripSection) } >RAM_D2_SRAM1
  .TxDescripSection (NOLOAD) : { . = ALIGN(32); *(.TxDescripSection) } >RAM_D2_SRAM1
  .fault_retained (NOLOAD) : { . = ALIGN(8); *(.fault_retained) } >BACKUP_SRAM
  __fault_retained_start = ADDR(.fault_retained);
  __fault_retained_end = ADDR(.fault_retained) + SIZEOF(.fault_retained);
  __fault_retained_size = SIZEOF(.fault_retained);
}
"""
VALID_IOC = "ProjectManager.HeapSize=0x200\nProjectManager.StackSize=0x1000\n"
VALID_MSP = """\
void HAL_ETH_MspInit(ETH_HandleTypeDef *heth)
{
  if (heth->Instance == ETH)
  {
    __HAL_RCC_D2SRAM1_CLK_ENABLE();
    __HAL_RCC_ETH1MAC_CLK_ENABLE();
    __HAL_RCC_ETH1TX_CLK_ENABLE();
  }
}
"""
VALID_MAIN = "int main(void)\n{\n  MX_GPIO_Init();\n  MX_ETH_Init();\n  return 0;\n}\n"
VALID_HAL = """\
HAL_StatusTypeDef HAL_ETH_Init(ETH_HandleTypeDef *heth)
{
  HAL_ETH_MspInit(heth);
  ETH_DMATxDescListInit(heth);
  ETH_DMARxDescListInit(heth);
  return HAL_OK;
}
"""


def errors(report: memory.Report) -> str:
    return "\n".join(report.errors)


class ElfPolicyTests(unittest.TestCase):
    def assert_elf_failure(self, nm_output: str, text: str) -> None:
        report = memory.check_elf_symbols(nm_output)
        self.assertFalse(report.passed)
        self.assertIn(text, errors(report))

    def test_valid_absolute_and_sized_symbols(self) -> None:
        report = memory.check_elf_symbols(VALID_NM)
        self.assertTrue(report.passed, errors(report))
        self.assertIn("no packet-buffer placement is assumed", errors(report) + "\n".join(report.notes))

    def test_missing_symbols(self) -> None:
        for symbol in (
            "DMARxDscrTab",
            "DMATxDscrTab",
            "_estack",
            "_sstack",
            "_Min_Stack_Size",
        ):
            with self.subTest(symbol=symbol):
                filtered = "\n".join(
                    line for line in VALID_NM.splitlines() if not line.endswith(f" {symbol}")
                )
                self.assert_elf_failure(filtered, f"required symbol {symbol} not found")

    def test_wrong_rx_and_tx_addresses(self) -> None:
        self.assert_elf_failure(
            VALID_NM.replace("30000000 00000060 B DMARxDscrTab", "30000100 00000060 B DMARxDscrTab"),
            "DMARxDscrTab starts at 0x30000100; expected exactly 0x30000000",
        )
        self.assert_elf_failure(
            VALID_NM.replace("30000060 00000060 B DMATxDscrTab", "30000080 00000060 B DMATxDscrTab"),
            "DMATxDscrTab starts at 0x30000080; expected exactly 0x30000060",
        )

    def test_bad_size_and_resulting_overlap(self) -> None:
        self.assert_elf_failure(
            VALID_NM.replace("30000000 00000060 B DMARxDscrTab", "30000000 000000a0 B DMARxDscrTab"),
            "DMARxDscrTab size is 0xA0; expected exactly 0x60",
        )
        self.assert_elf_failure(
            VALID_NM.replace("30000000 00000060 B DMARxDscrTab", "30000000 000000a0 B DMARxDscrTab"),
            "full extents overlap",
        )

    def test_misalignment(self) -> None:
        changed = VALID_NM.replace("30000000 00000060 B DMARxDscrTab", "30000004 00000060 B DMARxDscrTab")
        self.assert_elf_failure(changed, "not 32-byte aligned")

    def test_sram2_and_dtcm_placement(self) -> None:
        sram2 = VALID_NM.replace("30000000 00000060 B DMARxDscrTab", "30004000 00000060 B DMARxDscrTab")
        self.assert_elf_failure(sram2, "enters SRAM2")
        self.assert_elf_failure(sram2, "not fully inside SRAM1")
        dtcm = VALID_NM.replace("30000000 00000060 B DMARxDscrTab", "20000000 00000060 B DMARxDscrTab")
        self.assert_elf_failure(dtcm, "DTCM is not Ethernet DMA memory")

    def test_full_extent_cannot_cross_sram1_boundary(self) -> None:
        changed = VALID_NM.replace("30000060 00000060 B DMATxDscrTab", "30003fc0 00000060 B DMATxDscrTab")
        self.assert_elf_failure(changed, "not fully inside SRAM1")

    def test_short_stack(self) -> None:
        changed = VALID_NM.replace("00001000 A _Min_Stack_Size", "00000800 A _Min_Stack_Size").replace(
            "2001f000 A _sstack", "2001f800 A _sstack"
        )
        self.assert_elf_failure(changed, "_Min_Stack_Size is 0x800")

    def test_stack_identity_and_dtcm_bounds(self) -> None:
        changed = VALID_NM.replace("2001f000 A _sstack", "2001e000 A _sstack")
        self.assert_elf_failure(changed, "do not satisfy _sstack = _estack - _Min_Stack_Size")
        outside = VALID_NM.replace("20020000 R _estack", "20021000 R _estack")
        self.assert_elf_failure(outside, "not a valid extent in DTCM")

    def test_duplicate_and_malformed_required_symbols(self) -> None:
        self.assert_elf_failure(
            VALID_NM + "30000000 00000060 B DMARxDscrTab\n",
            "DMARxDscrTab occurs 2 times",
        )
        self.assert_elf_failure(
            VALID_NM.replace("30000000 00000060 B DMARxDscrTab", "not-an-address B DMARxDscrTab"),
            "malformed nm row",
        )

    def test_missing_retained_symbols(self) -> None:
        for symbol in memory.RETAINED_SECTION_SYMBOLS:
            with self.subTest(symbol=symbol):
                filtered = "\n".join(
                    line for line in VALID_NM.splitlines() if not line.endswith(f" {symbol}")
                )
                self.assert_elf_failure(filtered, f"required symbol {symbol} not found")

    def test_retained_extent_is_checked(self) -> None:
        wrong_address = VALID_NM.replace("38800000 A __fault_retained_start", "38000000 A __fault_retained_start")
        self.assert_elf_failure(wrong_address, "expected Backup SRAM base")
        wrong_size = VALID_NM.replace("00000080 A __fault_retained_size", "00000040 A __fault_retained_size")
        self.assert_elf_failure(wrong_size, "size does not match")
        beyond_region = VALID_NM.replace("38800080 A __fault_retained_end", "38801001 A __fault_retained_end")
        beyond_region = beyond_region.replace("00000080 A __fault_retained_size", "00001001 A __fault_retained_size")
        self.assert_elf_failure(beyond_region, "exceeds the 4 KiB Backup SRAM")

    def test_undefined_symbols_are_not_silently_accepted(self) -> None:
        changed = VALID_NM.replace("30000000 00000060 B DMARxDscrTab", "         U DMARxDscrTab")
        self.assert_elf_failure(changed, "malformed nm row")


class LinkerIocPolicyTests(unittest.TestCase):
    def test_valid_split_and_stack(self) -> None:
        report = memory.check_linker_and_ioc(VALID_LINKER, VALID_IOC)
        self.assertTrue(report.passed, errors(report))
        self.assertIn("IOC and linker stack reservations agree", "\n".join(report.notes))

    def test_short_linker_stack(self) -> None:
        linker = VALID_LINKER.replace("_Min_Stack_Size = 0x1000", "_Min_Stack_Size = 0x400")
        ioc = VALID_IOC.replace("StackSize=0x1000", "StackSize=0x400")
        report = memory.check_linker_and_ioc(linker, ioc)
        self.assertIn("linker _Min_Stack_Size=0x400", errors(report))

    def test_ioc_stack_mismatch(self) -> None:
        report = memory.check_linker_and_ioc(
            VALID_LINKER, VALID_IOC.replace("StackSize=0x1000", "StackSize=0x2000")
        )
        self.assertIn("IOC/linker stack mismatch", errors(report))

    def test_short_ioc_stack(self) -> None:
        report = memory.check_linker_and_ioc(
            VALID_LINKER, VALID_IOC.replace("StackSize=0x1000", "StackSize=0x400")
        )
        self.assertIn("IOC stack is 0x400", errors(report))

    def test_missing_or_merged_sram_regions(self) -> None:
        merged = VALID_LINKER.replace("RAM_D2_SRAM1", "RAM_D2").replace("RAM_D2_SRAM2", "OTHER_RAM")
        report = memory.check_linker_and_ioc(merged, VALID_IOC)
        self.assertIn("exactly one explicit SRAM1 MEMORY region", errors(report))
        self.assertIn("exactly one explicit SRAM2 MEMORY region", errors(report))

    def test_wrong_sram_split_and_descriptor_target(self) -> None:
        linker = VALID_LINKER.replace("RAM_D2_SRAM1 (xrw) : ORIGIN = 0x30000000, LENGTH = 16K", 
                                      "RAM_D2_SRAM1 (xrw) : ORIGIN = 0x30000000, LENGTH = 32K")
        linker = linker.replace("} >RAM_D2_SRAM1", "} >RAM_D2_SRAM2", 1)
        report = memory.check_linker_and_ioc(linker, VALID_IOC)
        self.assertIn("expected ORIGIN=0x30000000, LENGTH=0x4000", errors(report))
        self.assertIn(".RxDescripSection targets RAM_D2_SRAM2", errors(report))

    def test_linker_comments_do_not_supply_required_regions(self) -> None:
        linker = "/* SRAM1 (xrw) : ORIGIN = 0x30000000, LENGTH = 16K */\n" + VALID_LINKER.replace(
            "RAM_D2_SRAM1", "RAM_D2"
        )
        report = memory.check_linker_and_ioc(linker, VALID_IOC)
        self.assertIn("exactly one explicit SRAM1 MEMORY region", errors(report))

    def test_linker_stack_identity_is_required(self) -> None:
        linker = VALID_LINKER.replace("_sstack = _estack - _Min_Stack_Size", "_sstack = _estack")
        report = memory.check_linker_and_ioc(linker, VALID_IOC)
        self.assertIn("linker must define _sstack", errors(report))

    def test_linker_noload_is_required(self) -> None:
        report = memory.check_linker_and_ioc(VALID_LINKER.replace(" (NOLOAD)", ""), VALID_IOC)
        self.assertIn("does not retain its NOLOAD", errors(report))

    def test_retained_linker_policy(self) -> None:
        report = memory.check_linker_and_ioc(
            VALID_LINKER.replace("ORIGIN = 0x38800000, LENGTH = 4K", "ORIGIN = 0x38800000, LENGTH = 8K"),
            VALID_IOC,
        )
        self.assertIn("must reserve exactly 4 KiB", errors(report))
        report = memory.check_linker_and_ioc(
            VALID_LINKER.replace(">BACKUP_SRAM", ">RAM_D2_SRAM2"), VALID_IOC
        )
        self.assertIn(".fault_retained must target", errors(report))
        report = memory.check_linker_and_ioc(
            VALID_LINKER.replace("__fault_retained_size = SIZEOF(.fault_retained);", ""), VALID_IOC
        )
        self.assertIn("__fault_retained_size", errors(report))


class SourcePolicyTests(unittest.TestCase):
    def check(self, msp: str = VALID_MSP, main: str = VALID_MAIN, hal: str = VALID_HAL) -> memory.Report:
        return memory.check_source_policy(msp, main, hal)

    def test_valid_order_and_vendor_callback_order(self) -> None:
        report = self.check()
        self.assertTrue(report.passed, errors(report))
        self.assertIn("source inspection only; not runtime instrumentation", "\n".join(report.notes))

    def test_alternate_sram1_macro_is_accepted(self) -> None:
        report = self.check(msp=VALID_MSP.replace("D2SRAM1", "AHBSRAM1"))
        self.assertTrue(report.passed, errors(report))

    def test_missing_sram1_enable(self) -> None:
        report = self.check(msp=VALID_MSP.replace("    __HAL_RCC_D2SRAM1_CLK_ENABLE();\n", ""))
        self.assertIn("explicit __HAL_RCC_D2SRAM1_CLK_ENABLE() not found", errors(report))

    def test_misordered_sram1_enable(self) -> None:
        msp = VALID_MSP.replace(
            "    __HAL_RCC_D2SRAM1_CLK_ENABLE();\n    __HAL_RCC_ETH1MAC_CLK_ENABLE();",
            "    __HAL_RCC_ETH1MAC_CLK_ENABLE();\n    __HAL_RCC_D2SRAM1_CLK_ENABLE();",
        )
        report = self.check(msp=msp)
        self.assertIn("SRAM1 clock enable occurs after ETH1MAC", errors(report))

    def test_missing_eth_clock_enable(self) -> None:
        report = self.check(msp=VALID_MSP.replace("__HAL_RCC_ETH1MAC_CLK_ENABLE();", ""))
        self.assertIn("ETH1MAC_CLK_ENABLE() not found", errors(report))

    def test_missing_main_eth_call(self) -> None:
        report = self.check(main=VALID_MAIN.replace("  MX_ETH_Init();\n", ""))
        self.assertIn("main does not call MX_ETH_Init()", errors(report))

    def test_comments_and_strings_do_not_supply_calls(self) -> None:
        msp = """\
/* void HAL_ETH_MspInit(int x) { __HAL_RCC_ETH1MAC_CLK_ENABLE(); } */
void HAL_ETH_MspInit(ETH_HandleTypeDef *heth)
{
  const char *s = "__HAL_RCC_D2SRAM1_CLK_ENABLE()";
  // __HAL_RCC_D2SRAM1_CLK_ENABLE();
  __HAL_RCC_ETH1MAC_CLK_ENABLE();
}
"""
        main = "int main(void) { /* MX_ETH_Init(); */ return 0; }\n"
        report = self.check(msp=msp, main=main)
        self.assertIn("explicit __HAL_RCC_D2SRAM1_CLK_ENABLE() not found", errors(report))
        self.assertIn("main does not call MX_ETH_Init()", errors(report))

    def test_comments_between_tokens_do_not_break_real_order(self) -> None:
        msp = VALID_MSP.replace(
            "__HAL_RCC_D2SRAM1_CLK_ENABLE();",
            "__HAL_RCC_D2SRAM1_CLK_ENABLE /* why */ ();",
        )
        self.assertTrue(self.check(msp=msp).passed)

    def test_vendor_descriptor_init_before_msp_is_rejected(self) -> None:
        hal = VALID_HAL.replace(
            "  HAL_ETH_MspInit(heth);\n  ETH_DMATxDescListInit(heth);",
            "  ETH_DMATxDescListInit(heth);\n  HAL_ETH_MspInit(heth);",
        )
        report = self.check(hal=hal)
        self.assertIn("initializes descriptors before any visible MSP call", errors(report))

    def test_vendor_callback_registration_is_source_inspected(self) -> None:
        hal = VALID_HAL.replace("  HAL_ETH_MspInit(heth);", "  heth->MspInitCallback(heth);")
        self.assertTrue(self.check(hal=hal).passed)

    def test_unbalanced_function_does_not_fall_through_to_another_function(self) -> None:
        malformed = VALID_MSP + "\nvoid HAL_ETH_MspInit(void) { __HAL_RCC_D2SRAM1_CLK_ENABLE(); }\n"
        self.assertIsNotNone(memory.extract_c_function(malformed, "HAL_ETH_MspInit"))


    def test_sram1_enable_inside_the_eth_instance_branch_is_accepted(self) -> None:
        for header in ("if (heth->Instance == ETH)", "if(heth->Instance==ETH)", "if ( heth -> Instance == ETH )"):
            with self.subTest(header=header):
                report = self.check(msp=VALID_MSP.replace("if (heth->Instance == ETH)", header))
                self.assertTrue(report.passed, errors(report))
                self.assertIn("inside the ETH instance branch", "\n".join(report.notes))

    def test_sram1_enable_before_the_eth_branch_is_rejected(self) -> None:
        msp = VALID_MSP.replace("    __HAL_RCC_D2SRAM1_CLK_ENABLE();\n", "").replace(
            "{\n  if (heth", "{\n  __HAL_RCC_D2SRAM1_CLK_ENABLE();\n  if (heth", 1
        )
        report = self.check(msp=msp)
        self.assertFalse(report.passed)
        self.assertIn("outside the ETH instance branch", errors(report))

    def test_sram1_enable_after_the_eth_branch_is_rejected(self) -> None:
        msp = VALID_MSP.replace("    __HAL_RCC_D2SRAM1_CLK_ENABLE();\n", "").replace(
            "  }\n}", "  }\n  __HAL_RCC_D2SRAM1_CLK_ENABLE();\n}", 1
        )
        report = self.check(msp=msp)
        self.assertFalse(report.passed)
        self.assertIn("outside the ETH instance branch", errors(report))

    def test_sram1_enable_in_another_instance_branch_is_rejected(self) -> None:
        msp = VALID_MSP.replace("    __HAL_RCC_D2SRAM1_CLK_ENABLE();\n", "").replace(
            "  }\n}",
            "  }\n  if (heth->Instance == OTHER)\n  {\n    __HAL_RCC_D2SRAM1_CLK_ENABLE();\n  }\n}",
            1,
        )
        report = self.check(msp=msp)
        self.assertFalse(report.passed)
        self.assertIn("outside the ETH instance branch", errors(report))

    def test_missing_eth_instance_branch_is_rejected(self) -> None:
        report = self.check(msp=VALID_MSP.replace("if (heth->Instance == ETH)", "if (1)"))
        self.assertFalse(report.passed)
        self.assertIn("has no `if (heth->Instance == ETH)` branch", errors(report))

    def test_eth_clock_outside_the_eth_branch_does_not_count(self) -> None:
        msp = VALID_MSP.replace("    __HAL_RCC_ETH1MAC_CLK_ENABLE();\n", "").replace(
            "  }\n}", "  }\n  __HAL_RCC_ETH1MAC_CLK_ENABLE();\n}", 1
        )
        report = self.check(msp=msp)
        self.assertFalse(report.passed)
        self.assertIn("ETH1MAC_CLK_ENABLE() not found", errors(report))

    def test_sram1_enable_only_in_main_is_rejected(self) -> None:
        msp = VALID_MSP.replace("    __HAL_RCC_D2SRAM1_CLK_ENABLE();\n", "")
        main = "int main(void)\n{\n  __HAL_RCC_D2SRAM1_CLK_ENABLE();\n  MX_GPIO_Init();\n  MX_ETH_Init();\n  return 0;\n}\n"
        report = self.check(msp=msp, main=main)
        self.assertFalse(report.passed)
        self.assertIn("explicit __HAL_RCC_D2SRAM1_CLK_ENABLE() not found", errors(report))


class MapPolicyTests(unittest.TestCase):
    valid_map = """\
.RxDescripSection (NOLOAD)
                0x30000000       0x60
.TxDescripSection (NOLOAD)
                0x30000060       0x60
.data           0x20000000       0x10 load address 0x08000100
.fault_retained 0x38800000       0x80
"""

    def test_valid_sections_and_empty_sram2(self) -> None:
        report = memory.check_map_policy(self.valid_map)
        self.assertTrue(report.passed, errors(report))
        self.assertIn("no allocated output section in reserved SRAM2", "\n".join(report.notes))

    def test_wrong_section_extent_is_rejected(self) -> None:
        report = memory.check_map_policy(self.valid_map.replace("0x30000000       0x60", "0x30000020       0x61", 1))
        self.assertIn("map .RxDescripSection", errors(report))

    def test_sram2_allocation_is_rejected(self) -> None:
        report = memory.check_map_policy(self.valid_map + ".eth_packets   0x30004000     0x600\n")
        self.assertIn("enters reserved SRAM2", errors(report))

    def test_map_without_printed_noload_is_accepted(self) -> None:
        report = memory.check_map_policy(self.valid_map.replace(" (NOLOAD)", ""))
        self.assertTrue(report.passed, errors(report))

    def test_retained_map_extent_is_required(self) -> None:
        report = memory.check_map_policy(self.valid_map.replace(".fault_retained 0x38800000       0x80", ""))
        self.assertIn("exactly one .fault_retained", errors(report))
        report = memory.check_map_policy(
            self.valid_map.replace(".fault_retained 0x38800000       0x80", ".fault_retained 0x38800000       0x1001")
        )
        self.assertIn("does not fit", errors(report))


class ProfileFrameworkTests(unittest.TestCase):
    """Profiles are cumulative: a later layout satisfies every earlier profile."""

    @staticmethod
    def without(text: str, *needles: str) -> str:
        return "".join(
            line + "\n" for line in text.splitlines() if not any(needle in line for needle in needles)
        )

    def test_default_profile_is_the_strictest_available(self) -> None:
        self.assertEqual(memory.DEFAULT_PROFILE, memory.PROFILES[-1])

    def test_memory_init_accepts_the_base_layout(self) -> None:
        nm = self.without(VALID_NM, "sensor_usart", "__fault_retained")
        report = memory.check_elf_symbols(nm, "memory-init")
        self.assertTrue(report.passed, errors(report))

    def test_every_profile_accepts_the_full_layout_of_this_stage(self) -> None:
        for profile in memory.PROFILES:
            with self.subTest(profile=profile):
                self.assertTrue(memory.check_elf_symbols(VALID_NM, profile).passed)

    def test_a_broken_common_layout_is_rejected_by_every_profile(self) -> None:
        broken = VALID_NM.replace("30000000 00000060 B DMARxDscrTab", "30000020 00000060 B DMARxDscrTab")
        for profile in memory.PROFILES:
            with self.subTest(profile=profile):
                self.assertFalse(memory.check_elf_symbols(broken, profile).passed)

    def test_unknown_profile_is_rejected(self) -> None:
        with self.assertRaisesRegex(memory.CheckerError, "unknown profile"):
            memory.check_elf_symbols(VALID_NM, "bogus")
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            memory.parse_args(["fixture.elf", "--profile", "bogus"])

    def test_cli_profile_selects_rules_and_reports_it(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            files = {
                "STM32H723xG_flash.ld": self.without(VALID_LINKER, "BACKUP_SRAM", "fault_retained"),
                "OpenAMRobot.ioc": VALID_IOC,
                "Core/Src/stm32h7xx_hal_msp.c": VALID_MSP,
                "Core/Src/main.c": VALID_MAIN,
                "Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_eth.c": VALID_HAL,
            }
            for relative, content in files.items():
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content, encoding="utf-8")
            nm_output = root / "nm.txt"
            nm_output.write_text(self.without(VALID_NM, "sensor_usart", "__fault_retained"), encoding="utf-8")
            elf = root / "fixture.elf"
            elf.touch()
            elf.with_suffix(".map").write_text(self.without(MapPolicyTests.valid_map, "fault_retained"), encoding="utf-8")
            stdout, stderr = io.StringIO(), io.StringIO()
            with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                result = memory.main([
                    str(elf), "--source-root", str(root), "--nm-output", str(nm_output),
                    "--profile", "memory-init",
                ])
            self.assertEqual(result, 0, stdout.getvalue() + stderr.getvalue())
            self.assertIn("Policy profile: memory-init", stdout.getvalue())


class SensorsProfileTests(unittest.TestCase):
    without = staticmethod(ProfileFrameworkTests.without)

    def test_sensors_profile_accepts_its_layout(self) -> None:
        report = memory.check_elf_symbols(self.without(VALID_NM, "__fault_retained"), "sensors")
        self.assertTrue(report.passed, errors(report))

    def test_sensors_profile_requires_the_sensor_buffers(self) -> None:
        nm = self.without(VALID_NM, "sensor_usart", "__fault_retained")
        report = memory.check_elf_symbols(nm, "sensors")
        self.assertIn("required symbol sensor_usart2_rx_buffer not found", errors(report))
        self.assertTrue(memory.check_elf_symbols(nm, "memory-init").passed)

    def test_sensor_buffer_extent_is_checked_only_by_the_sensors_profile(self) -> None:
        wrong = VALID_NM.replace("300000c0 00000100 B sensor_usart2_rx_buffer",
                                 "300000e0 00000100 B sensor_usart2_rx_buffer")
        self.assertIn("sensor_usart2_rx_buffer is", errors(memory.check_elf_symbols(wrong, "sensors")))
        self.assertTrue(memory.check_elf_symbols(wrong, "memory-init").passed)

    def test_default_profile_rejects_a_layout_without_sensor_buffers(self) -> None:
        nm = self.without(VALID_NM, "sensor_usart")
        self.assertFalse(memory.check_elf_symbols(nm).passed)


class RetainedProfileTests(unittest.TestCase):
    without = staticmethod(ProfileFrameworkTests.without)
    NO_RETAINED_LINKER = ProfileFrameworkTests.without(VALID_LINKER, "BACKUP_SRAM", "fault_retained")
    NO_RETAINED_MAP = ProfileFrameworkTests.without(MapPolicyTests.valid_map, "fault_retained")

    def test_retained_profile_requires_the_retention_symbols(self) -> None:
        nm = self.without(VALID_NM, "__fault_retained")
        report = memory.check_elf_symbols(nm, "retained")
        self.assertIn("required symbol __fault_retained_start not found", errors(report))
        self.assertTrue(memory.check_elf_symbols(nm, "sensors").passed)

    def test_backup_region_is_required_only_by_the_retained_profile(self) -> None:
        for profile in ("memory-init", "sensors"):
            with self.subTest(profile=profile):
                report = memory.check_linker_and_ioc(self.NO_RETAINED_LINKER, VALID_IOC, profile)
                self.assertTrue(report.passed, errors(report))
        report = memory.check_linker_and_ioc(self.NO_RETAINED_LINKER, VALID_IOC, "retained")
        self.assertIn("exactly one Backup SRAM region", errors(report))
        self.assertTrue(memory.check_linker_and_ioc(VALID_LINKER, VALID_IOC, "memory-init").passed)

    def test_retained_map_section_is_required_only_by_the_retained_profile(self) -> None:
        for profile in ("memory-init", "sensors"):
            with self.subTest(profile=profile):
                self.assertTrue(memory.check_map_policy(self.NO_RETAINED_MAP, profile).passed)
        report = memory.check_map_policy(self.NO_RETAINED_MAP, "retained")
        self.assertIn("exactly one .fault_retained", errors(report))

    def test_default_profile_rejects_a_layout_without_retention(self) -> None:
        self.assertFalse(memory.check_elf_symbols(self.without(VALID_NM, "__fault_retained")).passed)
        self.assertEqual(memory.DEFAULT_PROFILE, "retained")


class CommandFixtureTests(unittest.TestCase):
    def test_source_root_and_nm_output_cli(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            files = {
                "STM32H723xG_flash.ld": VALID_LINKER,
                "OpenAMRobot.ioc": VALID_IOC,
                "Core/Src/stm32h7xx_hal_msp.c": VALID_MSP,
                "Core/Src/main.c": VALID_MAIN,
                "Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_eth.c": VALID_HAL,
            }
            for relative, content in files.items():
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content, encoding="utf-8")
            nm_output = root / "nm.txt"
            nm_output.write_text(VALID_NM, encoding="utf-8")
            elf = root / "fixture.elf"
            elf.touch()
            elf.with_suffix(".map").write_text(MapPolicyTests.valid_map, encoding="utf-8")
            stdout = io.StringIO()
            stderr = io.StringIO()
            with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                result = memory.main([
                    str(elf),
                    "--source-root",
                    str(root),
                    "--nm-output",
                    str(nm_output),
                ])
            self.assertEqual(result, 0, stdout.getvalue() + stderr.getvalue())
            self.assertIn("PASS: STM32H723", stdout.getvalue())
            self.assertEqual(stderr.getvalue(), "")

    def test_repository_layout_is_discovered_from_fixture_root(self) -> None:
        paths = memory.resolve_board_paths(Path("/fixture"))
        self.assertEqual(paths.ioc, Path("/fixture/boards/stm32/OpenAMRobot/OpenAMRobot.ioc"))

    def test_run_nm_uses_size_output_and_propagates_failures(self) -> None:
        completed = subprocess.CompletedProcess(["nm"], 0, stdout=VALID_NM, stderr="")
        with mock.patch.object(memory.subprocess, "run", return_value=completed) as run:
            self.assertEqual(memory.run_nm(Path("OpenAMRobot.elf")), VALID_NM)
        self.assertEqual(run.call_args.args[0][1:3], ["-S", "OpenAMRobot.elf"])

        failed = subprocess.CompletedProcess(["nm"], 1, stdout="", stderr="bad image")
        with mock.patch.object(memory.subprocess, "run", return_value=failed):
            with self.assertRaisesRegex(memory.CheckerError, "bad image"):
                memory.run_nm(Path("bad.elf"))


if __name__ == "__main__":
    unittest.main()
