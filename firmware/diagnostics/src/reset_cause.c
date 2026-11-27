#include "oamr/reset_cause.h"

static uint32_t oamr_reset_known_bits(
    const oamr_reset_flag_definitions_t *definitions)
{
    return definitions->power_on | definitions->pin | definitions->software |
           definitions->iwdg | definitions->wwdg | definitions->low_power |
           definitions->bor | definitions->other;
}

oamr_status_t oamr_reset_cause_decode(
    uint32_t raw_flags,
    const oamr_reset_flag_definitions_t *definitions,
    oamr_reset_cause_t *out_cause)
{
    uint32_t known_bits;
    uint32_t causes = OAMR_RESET_CAUSE_NONE;

    if (out_cause == NULL) {
        return OAMR_STATUS_INVALID;
    }

    out_cause->raw_flags = 0u;
    out_cause->causes = OAMR_RESET_CAUSE_NONE;
    out_cause->unknown_flags = 0u;

    if (definitions == NULL) {
        return OAMR_STATUS_INVALID;
    }

    if ((raw_flags & definitions->power_on) != 0u) {
        causes |= OAMR_RESET_CAUSE_POWER_ON;
    }
    if ((raw_flags & definitions->pin) != 0u) {
        causes |= OAMR_RESET_CAUSE_PIN;
    }
    if ((raw_flags & definitions->software) != 0u) {
        causes |= OAMR_RESET_CAUSE_SOFTWARE;
    }
    if ((raw_flags & definitions->iwdg) != 0u) {
        causes |= OAMR_RESET_CAUSE_IWDG;
    }
    if ((raw_flags & definitions->wwdg) != 0u) {
        causes |= OAMR_RESET_CAUSE_WWDG;
    }
    if ((raw_flags & definitions->low_power) != 0u) {
        causes |= OAMR_RESET_CAUSE_LOW_POWER;
    }
    if ((raw_flags & definitions->bor) != 0u) {
        causes |= OAMR_RESET_CAUSE_BOR;
    }
    if ((raw_flags & definitions->other) != 0u) {
        causes |= OAMR_RESET_CAUSE_OTHER;
    }

    known_bits = oamr_reset_known_bits(definitions);
    out_cause->raw_flags = raw_flags;
    out_cause->unknown_flags = raw_flags & ~known_bits;
    if (out_cause->unknown_flags != 0u) {
        causes |= OAMR_RESET_CAUSE_OTHER;
    }
    out_cause->causes = causes;
    return OAMR_STATUS_OK;
}
