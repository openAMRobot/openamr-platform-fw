/* SPDX-License-Identifier: MIT */
/*
 * Benewake TF-Luna serial frame parser (byte stream in, validated sample out).
 *
 * Frame layout source: Benewake SJ-PM-TF-Luna A05 Product Manual, Appendix I,
 * formats "9-byte/cm (Default)" and "9-byte/mm":
 *
 *   byte  0     1     2       3       4      5      6       7       8
 *         0x59  0x59  Dist_L  Dist_H  Amp_L  Amp_H  Temp_L  Temp_H  Checksum
 *
 *   Checksum = low 8 bits of the sum of bytes 0 to 7.
 *   Temperature degC = Temp / 8 - 256.
 *   Dist is unreliable when Amp < 100 or Amp = 65535 (overexposure); Amp
 *   above 32768 means ambient-light overexposure (manual section 6.4).
 *   The sensor sets Dist to dummy_dist (default 0) when Amp is below the
 *   amp threshold (default 100), section 6.8. The ranging blind zone is
 *   20 cm, any output below 20 cm is unreliable (section 4); any value
 *   above 800 cm may be unreliable (section 6.9).
 *
 * Classification (I8: a missing return is UNKNOWN, never free space): only
 * OAMR_TFL_OK carries a usable distance. NO_RETURN, BELOW_MIN, ABOVE_MAX and
 * WEAK or OVEREXPOSED are reported with valid = false. The caller must never
 * read such a sample as "floor present" or "path clear".
 *
 * Stream handling: bytes that do not start a frame are discarded and counted.
 * A frame with a wrong checksum is counted and the parser resynchronises on
 * the next 0x59 0x59 inside the rejected bytes, so a garbage byte that looked
 * like a header cannot swallow the following real frame. Command responses
 * (header 0x5A) are not parsed; they are skipped as non-frame bytes.
 *
 * The parser holds no sample between calls: each completed frame is
 * returned exactly once by oamr_tfl_push(); there is no "latest value" that
 * could be returned again as new.
 */
#ifndef OAMR_TFLUNA_H
#define OAMR_TFLUNA_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OAMR_TFL_HEADER 0x59u
#define OAMR_TFL_FRAME_LEN 9u
/* Manual facts (SJ-PM-TF-Luna A05). */
#define OAMR_TFL_AMP_MIN_RELIABLE 100u     /* section 6.4 and 6.8 default threshold */
#define OAMR_TFL_AMP_OVEREXPOSED 65535u    /* section 6.4 */
#define OAMR_TFL_AMP_AMBIENT_LIMIT 32768u  /* section 6.4: above this, ambient overexposure */
#define OAMR_TFL_BLIND_ZONE_MM 200u        /* section 4: 20 cm blind zone */
#define OAMR_TFL_MAX_RELIABLE_MM 8000u     /* section 6.9: above 800 cm may be unreliable */

typedef enum {
    OAMR_TFL_UNIT_UNSET = 0, /* not configured */
    OAMR_TFL_UNIT_CM = 1,    /* "9-byte/cm", factory default output format */
    OAMR_TFL_UNIT_MM = 2     /* "9-byte/mm" */
} oamr_tfl_unit_t;

typedef struct {
    oamr_tfl_unit_t unit; /* must match the format set in the sensor; no default */
} oamr_tfl_config_t;

typedef enum {
    OAMR_TFL_UNKNOWN = 0,     /* never set on a returned sample */
    OAMR_TFL_OK = 1,          /* distance usable */
    OAMR_TFL_NO_RETURN = 2,   /* Dist 0 (dummy_dist): no target or signal too weak */
    OAMR_TFL_WEAK = 3,        /* Amp below 100 with a non-zero distance */
    OAMR_TFL_OVEREXPOSED = 4, /* Amp 65535 or above 32768 */
    OAMR_TFL_BELOW_MIN = 5,   /* distance inside the 20 cm blind zone */
    OAMR_TFL_ABOVE_MAX = 6    /* distance above 800 cm */
} oamr_tfl_status_t;

typedef struct {
    bool valid;               /* true only for OAMR_TFL_OK */
    oamr_tfl_status_t status;
    uint16_t raw_distance;    /* as sent, in the configured unit */
    uint32_t distance_mm;     /* raw_distance converted; meaningful only when valid */
    uint16_t strength;        /* Amp */
    float temperature_c;
    uint32_t seq;             /* increments for every returned frame */
} oamr_tfl_sample_t;

typedef struct {
    uint32_t frames_ok;       /* checksum-correct frames, any status */
    uint32_t checksum_errors;
    uint32_t bytes_discarded; /* bytes skipped while hunting for a header */
} oamr_tfl_counters_t;

typedef enum {
    OAMR_TFL_PUSH_NONE = 0,
    OAMR_TFL_PUSH_SAMPLE = 1,        /* *out holds a new frame (check out->valid) */
    OAMR_TFL_PUSH_CHECKSUM_ERROR = 2,
    OAMR_TFL_PUSH_NOT_CONFIGURED = 3
} oamr_tfl_push_t;

typedef struct {
    oamr_tfl_config_t cfg;
    bool configured;
    uint8_t buf[OAMR_TFL_FRAME_LEN];
    uint8_t len;
    uint32_t seq;
    oamr_tfl_counters_t counters;
} oamr_tfl_t;

bool oamr_tfl_init(oamr_tfl_t *p, const oamr_tfl_config_t *cfg);
/* Drop a partial frame (for example after a UART error or overrun). */
void oamr_tfl_flush(oamr_tfl_t *p);
oamr_tfl_push_t oamr_tfl_push(oamr_tfl_t *p, uint8_t byte, oamr_tfl_sample_t *out);
/* Pure decode of one complete 9-byte frame; returns false on header or
 * checksum error. Exposed for tests and for DMA block parsing. */
bool oamr_tfl_decode(const uint8_t frame[OAMR_TFL_FRAME_LEN], oamr_tfl_unit_t unit,
                     oamr_tfl_sample_t *out);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_TFLUNA_H */
