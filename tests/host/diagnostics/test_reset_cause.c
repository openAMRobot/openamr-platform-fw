/* SPDX-License-Identifier: MIT */
#include "test_harness.h"

#include "oamr/reset_cause.h"

#include <stdint.h>
#include <string.h>

#define ALL_RESET_CAUSES (UINT32_C(0xff))

static oamr_reset_flag_definitions_t arbitrary_definitions(void)
{
    oamr_reset_flag_definitions_t definitions;

    /* Deliberately non-STM32, sparse masks. */
    definitions.power_on = UINT32_C(0x00000001);
    definitions.pin = UINT32_C(0x00000004);
    definitions.software = UINT32_C(0x00000020);
    definitions.iwdg = UINT32_C(0x00000100);
    definitions.wwdg = UINT32_C(0x00000800);
    definitions.low_power = UINT32_C(0x00010000);
    definitions.bor = UINT32_C(0x01000000);
    definitions.other = UINT32_C(0x80000000);
    return definitions;
}

static void check_decode(uint32_t raw,
                         const oamr_reset_flag_definitions_t *definitions,
                         uint32_t causes, uint32_t unknown)
{
    oamr_reset_cause_t cause;

    memset(&cause, 0xa5, sizeof(cause));
    CHECK(oamr_reset_cause_decode(raw, definitions, &cause) == OAMR_STATUS_OK);
    CHECK(cause.raw_flags == raw);
    CHECK(cause.causes == causes);
    CHECK(cause.unknown_flags == unknown);
}

static void test_zero_and_all_causes(void)
{
    oamr_reset_flag_definitions_t definitions = arbitrary_definitions();
    uint32_t all_raw = definitions.power_on | definitions.pin |
                       definitions.software | definitions.iwdg |
                       definitions.wwdg | definitions.low_power |
                       definitions.bor | definitions.other;

    check_decode(0u, &definitions, OAMR_RESET_CAUSE_NONE, 0u);
    check_decode(all_raw, &definitions, ALL_RESET_CAUSES, 0u);
}

static void test_each_cause_independently(void)
{
    oamr_reset_flag_definitions_t definitions = arbitrary_definitions();
    static const struct {
        uint32_t raw;
        uint32_t cause;
    } cases[] = {
        {UINT32_C(0x00000001), OAMR_RESET_CAUSE_POWER_ON},
        {UINT32_C(0x00000004), OAMR_RESET_CAUSE_PIN},
        {UINT32_C(0x00000020), OAMR_RESET_CAUSE_SOFTWARE},
        {UINT32_C(0x00000100), OAMR_RESET_CAUSE_IWDG},
        {UINT32_C(0x00000800), OAMR_RESET_CAUSE_WWDG},
        {UINT32_C(0x00010000), OAMR_RESET_CAUSE_LOW_POWER},
        {UINT32_C(0x01000000), OAMR_RESET_CAUSE_BOR},
        {UINT32_C(0x80000000), OAMR_RESET_CAUSE_OTHER}
    };
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        check_decode(cases[index].raw, &definitions, cases[index].cause, 0u);
    }
}

static void test_overlapping_masks_retain_every_match(void)
{
    oamr_reset_flag_definitions_t definitions = arbitrary_definitions();

    definitions.power_on |= UINT32_C(0x00000004);
    definitions.pin |= UINT32_C(0x00000020);
    definitions.software |= UINT32_C(0x80000000);
    definitions.other |= UINT32_C(0x00000100);

    check_decode(UINT32_C(0x00000007), &definitions,
                 OAMR_RESET_CAUSE_POWER_ON | OAMR_RESET_CAUSE_PIN |
                     OAMR_RESET_CAUSE_OTHER, UINT32_C(0x00000002));
    check_decode(UINT32_C(0x00000120), &definitions,
                 OAMR_RESET_CAUSE_PIN | OAMR_RESET_CAUSE_SOFTWARE |
                     OAMR_RESET_CAUSE_IWDG | OAMR_RESET_CAUSE_OTHER,
                 0u);
    check_decode(UINT32_C(0x80000020), &definitions,
                 OAMR_RESET_CAUSE_PIN | OAMR_RESET_CAUSE_SOFTWARE |
                     OAMR_RESET_CAUSE_OTHER, 0u);
}

static void test_unknown_and_known_plus_unknown(void)
{
    oamr_reset_flag_definitions_t definitions = arbitrary_definitions();

    check_decode(UINT32_C(0x40000002), &definitions,
                 OAMR_RESET_CAUSE_OTHER, UINT32_C(0x40000002));
    check_decode(UINT32_C(0x00000105), &definitions,
                 OAMR_RESET_CAUSE_POWER_ON | OAMR_RESET_CAUSE_PIN |
                     OAMR_RESET_CAUSE_IWDG,
                 0u);
    definitions.power_on = 0u;
    check_decode(UINT32_C(0x00000005), &definitions,
                 OAMR_RESET_CAUSE_PIN | OAMR_RESET_CAUSE_OTHER,
                 UINT32_C(0x00000001));
}

static void test_empty_definitions_and_invalid_arguments(void)
{
    oamr_reset_flag_definitions_t definitions;
    oamr_reset_cause_t cause;

    memset(&definitions, 0, sizeof(definitions));
    check_decode(UINT32_C(0xffffffff), &definitions, OAMR_RESET_CAUSE_OTHER,
                 UINT32_C(0xffffffff));

    memset(&cause, 0xa5, sizeof(cause));
    CHECK(oamr_reset_cause_decode(UINT32_C(0xffffffff), NULL, &cause) ==
          OAMR_STATUS_INVALID);
    CHECK(cause.raw_flags == 0u);
    CHECK(cause.causes == OAMR_RESET_CAUSE_NONE);
    CHECK(cause.unknown_flags == 0u);

    memset(&cause, 0xa5, sizeof(cause));
    CHECK(oamr_reset_cause_decode(UINT32_C(0xffffffff), &definitions, NULL) ==
          OAMR_STATUS_INVALID);
    CHECK(cause.raw_flags == UINT32_C(0xa5a5a5a5));
}

int main(void)
{
    RUN(test_zero_and_all_causes);
    RUN(test_each_cause_independently);
    RUN(test_overlapping_masks_retain_every_match);
    RUN(test_unknown_and_known_plus_unknown);
    RUN(test_empty_definitions_and_invalid_arguments);
    return TEST_END();
}
