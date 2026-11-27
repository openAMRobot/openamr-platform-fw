#ifndef OAMR_TIME_H
#define OAMR_TIME_H

#include "oamr/status.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Caller-supplied uint64_t microsecond timestamps are validated, never read
 * from a clock here. The first timestamp after initialization establishes the
 * baseline. Subsequent equal timestamps are valid; decreasing timestamps are
 * rejected without changing the last accepted timestamp.
 */
typedef struct {
    uint64_t last_timestamp_us;
    bool has_timestamp;
} oamr_time_validator_t;

/* A NULL validator is ignored. Initialization starts a new explicit epoch. */
void oamr_time_validator_init(oamr_time_validator_t *validator);

/*
 * Returns OAMR_STATUS_OK for an accepted timestamp and OAMR_STATUS_INVALID for
 * a NULL validator or a decrease. This helper does not clamp, repair, or make
 * a wrapping/nonmonotonic clock monotonic; a counter wrap appears as a
 * decrease and is rejected.
 */
oamr_status_t oamr_time_validate_monotonic(oamr_time_validator_t *validator,
                                           uint64_t timestamp_us);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_TIME_H */
