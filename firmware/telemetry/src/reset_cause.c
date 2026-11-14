/* SPDX-License-Identifier: MIT */
#include "oamr/reset_cause.h"

static unsigned popcount32(uint32_t v)
{
    unsigned n = 0u;
    while (v != 0u) {
        v &= v - 1u;
        n++;
    }
    return n;
}

oamr_reset_decoded_t oamr_reset_cause_decode(uint32_t rcc_rsr)
{
    oamr_reset_decoded_t d;
    d.cause = OAMR_RESET_CAUSE_UNKNOWN;
    d.flags_raw = rcc_rsr;
    /* RMVF (bit 16) reads as zero; anything outside the known flags is unexpected. */
    d.unknown_bits = (rcc_rsr & ~OAMR_RSR_KNOWN_MASK) != 0u;

    /* Source flags that identify a cause on their own. */
    const uint32_t primary = rcc_rsr & (OAMR_RSR_IWDG1RSTF | OAMR_RSR_WWDG1RSTF | OAMR_RSR_SFTRSTF |
                                        OAMR_RSR_LPWRRSTF | OAMR_RSR_PORRSTF);
    d.multiple_sources = popcount32(primary) > 1u;

    if (rcc_rsr == 0u || d.unknown_bits) {
        return d;
    }
    if ((rcc_rsr & (OAMR_RSR_IWDG1RSTF | OAMR_RSR_WWDG1RSTF)) != 0u) {
        d.cause = OAMR_RESET_CAUSE_WATCHDOG;
    } else if ((rcc_rsr & OAMR_RSR_SFTRSTF) != 0u) {
        d.cause = OAMR_RESET_CAUSE_SOFTWARE;
    } else if ((rcc_rsr & OAMR_RSR_LPWRRSTF) != 0u) {
        d.cause = OAMR_RESET_CAUSE_UNKNOWN; /* no I8 value for a low-power error */
    } else if ((rcc_rsr & OAMR_RSR_PORRSTF) != 0u) {
        d.cause = OAMR_RESET_CAUSE_POWER_ON;
    } else if ((rcc_rsr & OAMR_RSR_BORRSTF) != 0u) {
        d.cause = OAMR_RESET_CAUSE_BROWNOUT;
    } else if ((rcc_rsr & OAMR_RSR_PINRSTF) != 0u) {
        d.cause = OAMR_RESET_CAUSE_PIN;
    }
    /* CPURSTF, D1RSTF or D2RSTF alone: UNKNOWN. */
    return d;
}
