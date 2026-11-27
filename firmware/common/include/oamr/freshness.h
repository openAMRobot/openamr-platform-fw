#ifndef OAMR_FRESHNESS_H
#define OAMR_FRESHNESS_H

#include "oamr/status.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Check a caller-supplied timestamp against a caller-supplied clock reading.
 * There is no default age limit and no stored "last known healthy" state.
 *
 * - timestamp_valid == false: OAMR_STATUS_INVALID.
 * - now_us < sample_timestamp_us: OAMR_STATUS_INVALID (no wrap recovery).
 * - now_us - sample_timestamp_us > max_age_us: OAMR_STATUS_STALE.
 * - otherwise: OAMR_STATUS_OK.
 *
 * Thus a sample exactly max_age_us old is fresh. A zero max_age_us accepts only
 * an equal, explicitly valid timestamp. Timestamp zero is valid when
 * timestamp_valid is true. All arithmetic is guarded against underflow.
 */
oamr_status_t oamr_freshness_check(uint64_t sample_timestamp_us,
                                   bool timestamp_valid,
                                   uint64_t now_us,
                                   uint64_t max_age_us);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_FRESHNESS_H */
