#include "oamr/freshness.h"

oamr_status_t oamr_freshness_check(uint64_t sample_timestamp_us,
                                   bool timestamp_valid,
                                   uint64_t now_us,
                                   uint64_t max_age_us)
{
    uint64_t age_us;

    if (!timestamp_valid || now_us < sample_timestamp_us) {
        return OAMR_STATUS_INVALID;
    }

    age_us = now_us - sample_timestamp_us;
    return age_us > max_age_us ? OAMR_STATUS_STALE : OAMR_STATUS_OK;
}
