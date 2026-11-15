/* SPDX-License-Identifier: MIT */
#include "oamr/reset_cause.h"
#include "test_harness.h"

/*
 * Golden values in this file and their sources.
 *
 * [CMSIS] RCC_RSR bit positions: boards/stm32/OpenAMRobot/Drivers/CMSIS/
 *         Device/ST/STM32H7xx/Include/stm32h723xx.h (STM32Cube FW_H7
 *         V1.13.0, platform-fw #22), RCC_RSR_*_Pos at lines 16393 to 16421.
 *         Checked against that file in this change. The reference manual
 *         RM0468 itself was not consulted: source not verified.
 * [RM]    which flags a reset sets together (PINRSTF with every internal
 *         reset, BORRSTF with power-on): RM0468 reset section, as stated in
 *         reset_cause.h. Revision and section number not recorded. Source
 *         not verified; reset_cause.h asks for a bench check.
 * [I8]    expected enum values and their priority: the proposal in
 *         reset_cause.h for I8 decision 6 (open), BaseStatus.msg of
 *         interfaces #38 rev 4. Source not verified in this change.
 */

static void table(void)
{
    struct {
        uint32_t rsr;
        oamr_reset_cause_t cause;
    } cases[] = {
        {0u, OAMR_RESET_CAUSE_UNKNOWN},
        /* [RM] power-on: POR + BOR + PIN (+ CPU, D1, D2); [I8] expected causes */
        {OAMR_RSR_PORRSTF | OAMR_RSR_BORRSTF | OAMR_RSR_PINRSTF | OAMR_RSR_CPURSTF | OAMR_RSR_D1RSTF |
             OAMR_RSR_D2RSTF,
         OAMR_RESET_CAUSE_POWER_ON},
        {OAMR_RSR_BORRSTF | OAMR_RSR_PINRSTF | OAMR_RSR_CPURSTF, OAMR_RESET_CAUSE_BROWNOUT},
        {OAMR_RSR_PINRSTF | OAMR_RSR_CPURSTF, OAMR_RESET_CAUSE_PIN},
        {OAMR_RSR_PINRSTF, OAMR_RESET_CAUSE_PIN},
        {OAMR_RSR_SFTRSTF | OAMR_RSR_PINRSTF | OAMR_RSR_CPURSTF, OAMR_RESET_CAUSE_SOFTWARE},
        {OAMR_RSR_IWDG1RSTF | OAMR_RSR_PINRSTF | OAMR_RSR_CPURSTF, OAMR_RESET_CAUSE_WATCHDOG},
        {OAMR_RSR_WWDG1RSTF | OAMR_RSR_PINRSTF, OAMR_RESET_CAUSE_WATCHDOG},
        /* [I8] watchdog wins over software if flags were not cleared in between */
        {OAMR_RSR_IWDG1RSTF | OAMR_RSR_SFTRSTF | OAMR_RSR_PINRSTF, OAMR_RESET_CAUSE_WATCHDOG},
        {OAMR_RSR_LPWRRSTF | OAMR_RSR_PINRSTF, OAMR_RESET_CAUSE_UNKNOWN},
        {OAMR_RSR_CPURSTF, OAMR_RESET_CAUSE_UNKNOWN},
        {OAMR_RSR_D1RSTF, OAMR_RESET_CAUSE_UNKNOWN},
        /* undocumented bits: never guess */
        {OAMR_RSR_PINRSTF | (1u << 25), OAMR_RESET_CAUSE_UNKNOWN},
        {OAMR_RSR_PORRSTF | (1u << 16), OAMR_RESET_CAUSE_UNKNOWN},
        {0xFFFFFFFFu, OAMR_RESET_CAUSE_UNKNOWN},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        oamr_reset_decoded_t d = oamr_reset_cause_decode(cases[i].rsr);
        CHECK(d.cause == cases[i].cause);
        CHECK(d.flags_raw == cases[i].rsr);
        if (d.cause != cases[i].cause) {
            fprintf(stderr, "  case %zu rsr=0x%08x got %d\n", i, (unsigned)cases[i].rsr, (int)d.cause);
        }
    }
}

static void bit_positions(void)
{
    /* [CMSIS] positions, verified against stm32h723xx.h in this repository */
    CHECK(OAMR_RSR_CPURSTF == 0x00020000u);
    CHECK(OAMR_RSR_D1RSTF == 0x00080000u);
    CHECK(OAMR_RSR_D2RSTF == 0x00100000u);
    CHECK(OAMR_RSR_BORRSTF == 0x00200000u);
    CHECK(OAMR_RSR_PINRSTF == 0x00400000u);
    CHECK(OAMR_RSR_PORRSTF == 0x00800000u);
    CHECK(OAMR_RSR_SFTRSTF == 0x01000000u);
    CHECK(OAMR_RSR_IWDG1RSTF == 0x04000000u);
    CHECK(OAMR_RSR_WWDG1RSTF == 0x10000000u);
    CHECK(OAMR_RSR_LPWRRSTF == 0x40000000u);
}

static void flags(void)
{
    oamr_reset_decoded_t d = oamr_reset_cause_decode(OAMR_RSR_IWDG1RSTF | OAMR_RSR_SFTRSTF);
    CHECK(d.multiple_sources);
    CHECK(!d.unknown_bits);
    d = oamr_reset_cause_decode(OAMR_RSR_PINRSTF | (1u << 0));
    CHECK(d.unknown_bits);
    d = oamr_reset_cause_decode(OAMR_RSR_PORRSTF | OAMR_RSR_BORRSTF | OAMR_RSR_PINRSTF);
    CHECK(!d.multiple_sources);
}

static void exhaustive_known_bits(void)
{
    /* every combination of the documented bits yields a defined enum value */
    const uint32_t bits[] = {OAMR_RSR_CPURSTF, OAMR_RSR_D1RSTF,   OAMR_RSR_D2RSTF,    OAMR_RSR_BORRSTF,
                             OAMR_RSR_PINRSTF, OAMR_RSR_PORRSTF,  OAMR_RSR_SFTRSTF,   OAMR_RSR_IWDG1RSTF,
                             OAMR_RSR_WWDG1RSTF, OAMR_RSR_LPWRRSTF};
    for (uint32_t m = 0u; m < (1u << 10); m++) {
        uint32_t v = 0u;
        for (unsigned b = 0u; b < 10u; b++) {
            if ((m >> b) & 1u) {
                v |= bits[b];
            }
        }
        oamr_reset_decoded_t d = oamr_reset_cause_decode(v);
        CHECK(d.cause <= OAMR_RESET_CAUSE_PIN);
        if ((v & (OAMR_RSR_IWDG1RSTF | OAMR_RSR_WWDG1RSTF)) != 0u) {
            CHECK(d.cause == OAMR_RESET_CAUSE_WATCHDOG);
        }
    }
}

int main(void)
{
    RUN(table);
    RUN(bit_positions);
    RUN(flags);
    RUN(exhaustive_known_bits);
    TEST_MAIN_END();
}
