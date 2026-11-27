#ifndef OAMR_SAMPLE_H
#define OAMR_SAMPLE_H

#include "oamr/status.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Descriptive units only; no conversion or sensor-specific scale is implied. */
typedef enum {
    OAMR_SAMPLE_UNIT_UNSPECIFIED = 0,
    OAMR_SAMPLE_UNIT_RAW = 1,
    OAMR_SAMPLE_UNIT_METRE = 2,
    OAMR_SAMPLE_UNIT_MILLIMETRE = 3,
    OAMR_SAMPLE_UNIT_CENTIMETRE = 4,
    OAMR_SAMPLE_UNIT_SECOND = 5,
    OAMR_SAMPLE_UNIT_VOLT = 6,
    OAMR_SAMPLE_UNIT_AMPERE = 7,
    OAMR_SAMPLE_UNIT_CELSIUS = 8,
    OAMR_SAMPLE_UNIT_PERCENT = 9,
    OAMR_SAMPLE_UNIT_RADIAN = 10,
    OAMR_SAMPLE_UNIT_RADIAN_PER_SECOND = 11,
    OAMR_SAMPLE_UNIT_METRE_PER_SECOND = 12,
    OAMR_SAMPLE_UNIT_METRE_PER_SECOND_SQUARED = 13
} oamr_sample_unit_t;

/*
 * Generic producer-local sensor value. value is meaningful only when status is
 * OAMR_STATUS_OK. Even an OK value is not automatically fresh: pass
 * timestamp_us, timestamp_valid, the current time, and a caller-selected age
 * limit to oamr_freshness_check().
 *
 * timestamp_valid is deliberately independent of timestamp_us so zero can be
 * a real timestamp. sequence has producer-defined duplicate/wrap semantics;
 * this type imposes none. No unit conversion, calibration, safety classification,
 * or actuator meaning is attached to the value.
 */
typedef struct {
    float value;
    oamr_sample_unit_t unit;
    oamr_status_t status;
    uint64_t timestamp_us;
    uint32_t sequence;
    bool timestamp_valid;
} oamr_sample_t;

#ifdef __cplusplus
}
#endif

#endif /* OAMR_SAMPLE_H */
