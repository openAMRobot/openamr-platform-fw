#ifndef OAMR_TFLUNA_H
#define OAMR_TFLUNA_H

#include "oamr/status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Portable Benewake TF-Luna nine-byte UART frame decoder. The configured unit
 * must match the sensor; there is no implicit unit. Only framing, checksum,
 * byte order, and unit conversion are validated. Distance, signal strength,
 * and temperature are exposed without health, range, target, or safety
 * classification.
 *
 * temperature_c_x8 is eight times the protocol temperature in degrees Celsius
 * (raw_temperature - 2048), avoiding floating point and libm. Both raw and
 * scaled temperature fields are informational protocol decodes, not health
 * judgments.
 */
#define OAMR_TFL_HEADER 0x59u
#define OAMR_TFL_FRAME_LEN 9u

typedef enum {
    OAMR_TFL_UNIT_UNSET = 0,
    OAMR_TFL_UNIT_CM = 1,
    OAMR_TFL_UNIT_MM = 2
} oamr_tfl_unit_t;

typedef struct {
    oamr_tfl_unit_t unit;
} oamr_tfl_config_t;

typedef struct {
    uint16_t raw_distance;
    uint32_t distance_mm;
    uint16_t strength;
    uint16_t raw_temperature;
    int32_t temperature_c_x8;
    uint32_t sequence;
} oamr_tfl_sample_t;

typedef struct {
    uint64_t frames_ok;
    uint64_t checksum_errors;
    uint64_t bytes_discarded;
} oamr_tfl_counters_t;

typedef enum {
    OAMR_TFL_PUSH_NONE = 0,
    OAMR_TFL_PUSH_SAMPLE = 1,
    OAMR_TFL_PUSH_CHECKSUM_ERROR = 2,
    OAMR_TFL_PUSH_NOT_CONFIGURED = 3,
    OAMR_TFL_PUSH_INVALID = 4
} oamr_tfl_push_t;

typedef struct {
    oamr_tfl_config_t config;
    bool configured;
    uint8_t frame[OAMR_TFL_FRAME_LEN];
    uint8_t length;
    uint32_t next_sequence;
    oamr_tfl_counters_t counters;
} oamr_tfl_t;

/*
 * Initialize a parser. A non-NULL parser is left cleared and unconfigured if
 * config is NULL or its unit is not CM/MM. No bytes in a sensor buffer are
 * touched.
 */
oamr_status_t oamr_tfl_init(oamr_tfl_t *parser,
                            const oamr_tfl_config_t *config);

/* Drop a partial frame after a receive error; configuration is preserved. */
void oamr_tfl_flush(oamr_tfl_t *parser);

/*
 * Consume one byte in application context. SAMPLE writes out exactly once;
 * every other result leaves out unchanged. On checksum failure, the parser
 * searches the rejected bytes for the earliest overlapping/new 0x59 0x59
 * header (including a trailing single 0x59) before accepting more input.
 * Counters saturate; sequence numbers start at 1 and wrap to 0.
 */
oamr_tfl_push_t oamr_tfl_push(oamr_tfl_t *parser,
                              uint8_t byte,
                              oamr_tfl_sample_t *out);

/*
 * Decode one complete frame without changing parser state. The output sequence
 * is zero; the streaming parser assigns sequence numbers. INVALID reports bad
 * pointers, unit, header, or checksum and leaves out unchanged.
 */
oamr_status_t oamr_tfl_decode(const uint8_t frame[OAMR_TFL_FRAME_LEN],
                              oamr_tfl_unit_t unit,
                              oamr_tfl_sample_t *out);

/*
 * Convenience for one already copied contiguous DMA span. Feed the segment
 * one byte at a time and return the first complete sample. byte_consumed is
 * the number of segment bytes read, including the final sample byte.
 * Bytes after that sample remain for a subsequent call; no sample is retained
 * for duplicate delivery. INVALID is returned for bad pointers/configuration.
 */
oamr_status_t oamr_tfl_consume(oamr_tfl_t *parser,
                               const uint8_t *data,
                               size_t length,
                               size_t *byte_consumed,
                               oamr_tfl_sample_t *out);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_TFLUNA_H */
