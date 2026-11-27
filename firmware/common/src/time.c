#include "oamr/time.h"

#include <stddef.h>

void oamr_time_validator_init(oamr_time_validator_t *validator)
{
    if (validator == NULL) {
        return;
    }

    validator->last_timestamp_us = 0u;
    validator->has_timestamp = false;
}

oamr_status_t oamr_time_validate_monotonic(oamr_time_validator_t *validator,
                                           uint64_t timestamp_us)
{
    if (validator == NULL) {
        return OAMR_STATUS_INVALID;
    }

    if (validator->has_timestamp &&
        timestamp_us < validator->last_timestamp_us) {
        return OAMR_STATUS_INVALID;
    }

    validator->last_timestamp_us = timestamp_us;
    validator->has_timestamp = true;
    return OAMR_STATUS_OK;
}
