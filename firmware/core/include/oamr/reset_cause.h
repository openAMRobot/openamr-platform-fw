/* SPDX-License-Identifier: MIT */
/*
 * Reset cause as reported in /base/status (I8 rev 3 section 5.4). Several
 * flags may be set at once, so this is a bit mask. The board port captures
 * the hardware flags before it clears them and maps them here; a cause it
 * cannot identify is reported as OAMR_RESET_UNKNOWN, never as power-on.
 * The final enum is Evans's decision (I8 rev 3 section 8, decision 6).
 */
#ifndef OAMR_RESET_CAUSE_H
#define OAMR_RESET_CAUSE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OAMR_RESET_UNKNOWN = 1u << 0,
    OAMR_RESET_POWER_ON = 1u << 1,
    OAMR_RESET_PIN = 1u << 2,
    OAMR_RESET_BROWNOUT = 1u << 3,
    OAMR_RESET_INDEPENDENT_WATCHDOG = 1u << 4,
    OAMR_RESET_WINDOW_WATCHDOG = 1u << 5,
    OAMR_RESET_SOFTWARE = 1u << 6,
    OAMR_RESET_LOW_POWER = 1u << 7,
    OAMR_RESET_CPU = 1u << 8,
    OAMR_RESET_DOMAIN = 1u << 9
};

typedef uint32_t oamr_reset_cause_t;

#ifdef __cplusplus
}
#endif

#endif /* OAMR_RESET_CAUSE_H */
