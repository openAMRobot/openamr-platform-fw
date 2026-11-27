#ifndef OAMR_RESET_CAUSE_H
#define OAMR_RESET_CAUSE_H

#include "oamr/status.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Independent cause bits: more than one reset source may be present. */
typedef enum {
    OAMR_RESET_CAUSE_NONE = 0,
    OAMR_RESET_CAUSE_POWER_ON = (1u << 0),
    OAMR_RESET_CAUSE_PIN = (1u << 1),
    OAMR_RESET_CAUSE_SOFTWARE = (1u << 2),
    OAMR_RESET_CAUSE_IWDG = (1u << 3),
    OAMR_RESET_CAUSE_WWDG = (1u << 4),
    OAMR_RESET_CAUSE_LOW_POWER = (1u << 5),
    OAMR_RESET_CAUSE_BOR = (1u << 6),
    OAMR_RESET_CAUSE_OTHER = (1u << 7)
} oamr_reset_cause_mask_t;

/*
 * Board-specific raw flag values, not STM32 bit positions inferred here.
 * Each mask uses the same bit layout as the raw RCC value captured by the
 * adapter before it clears those flags. Zero denotes an absent definition.
 * The other mask is for board-defined sources outside the seven named classes.
 * A raw bit may intentionally match multiple masks; every matching cause is
 * reported rather than discarded by a priority rule.
 */
typedef struct {
    uint32_t power_on;
    uint32_t pin;
    uint32_t software;
    uint32_t iwdg;
    uint32_t wwdg;
    uint32_t low_power;
    uint32_t bor;
    uint32_t other;
} oamr_reset_flag_definitions_t;

typedef struct {
    uint32_t raw_flags;
    uint32_t causes;
    /* Raw bits absent from every adapter-supplied definition. */
    uint32_t unknown_flags;
} oamr_reset_cause_t;

/*
 * Decode all simultaneous sources without reading or clearing hardware.
 * OTHER is set when the adapter's other mask matches or unknown_flags is
 * nonzero. raw_flags therefore remains recoverable even for an incomplete
 * board definition. A zero raw value produces NONE and no unknown flags.
 *
 * Returns INVALID for NULL definitions/output. A non-NULL output is zeroed on
 * failure. The caller, not this decoder, is responsible for capturing raw
 * flags before any RCC clear operation and for supplying the correct masks.
 */
oamr_status_t oamr_reset_cause_decode(
    uint32_t raw_flags,
    const oamr_reset_flag_definitions_t *definitions,
    oamr_reset_cause_t *out_cause);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_RESET_CAUSE_H */
