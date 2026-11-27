#ifndef OAMR_MB7060_H
#define OAMR_MB7060_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Portable MaxBotix MB7060 ASCII receiver. The input is the UART byte stream
 * after any required electrical/UART inversion; this module performs no I/O.
 *
 * The complete grammar is exactly 'R', one to three ASCII decimal digits,
 * then carriage return. Consequently every structurally valid value from 0
 * through 999 is returned unchanged as range_cm. This module deliberately
 * does not classify target presence, measurement health, range, or safety.
 *
 * A structurally valid frame is returned only while armed. Arming accepts one
 * such frame and discards any partial frame left by the preceding acquisition.
 * Well-formed frames received while disarmed are counted and rejected. A bad
 * frame does not disarm the receiver, so a later good frame can still satisfy
 * the outstanding acquisition.
 */
#define OAMR_MB_MAX_DIGITS 3u
#define OAMR_MB_MAX_FRAME_LEN 6u

typedef struct {
    uint16_t range_cm;
    uint32_t sequence;
} oamr_mb_sample_t;

typedef struct {
    uint64_t frames_ok;
    uint64_t malformed;
    uint64_t unsolicited;
    uint64_t bytes_discarded;
} oamr_mb_counters_t;

typedef enum {
    OAMR_MB_PUSH_NONE = 0,
    OAMR_MB_PUSH_SAMPLE = 1,
    OAMR_MB_PUSH_MALFORMED = 2,
    OAMR_MB_PUSH_UNSOLICITED = 3,
    OAMR_MB_PUSH_INVALID = 4
} oamr_mb_push_t;

typedef struct {
    uint8_t frame[OAMR_MB_MAX_FRAME_LEN];
    uint8_t length;
    bool in_frame;
    bool armed;
    uint32_t next_sequence;
    oamr_mb_counters_t counters;
} oamr_mb_t;

/* A NULL parser is ignored. Initialization also clears sequence and counters. */
void oamr_mb_init(oamr_mb_t *parser);

/* Discard a partial frame and arm exactly one subsequent valid frame. */
void oamr_mb_arm(oamr_mb_t *parser);

/* Stop accepting samples. Any later well-formed frame is unsolicited. */
void oamr_mb_disarm(oamr_mb_t *parser);

/* Discard a partial frame after a receive error; armed state is preserved. */
void oamr_mb_flush(oamr_mb_t *parser);

/*
 * Consume one byte in application context. SAMPLE writes out exactly once;
 * every other result leaves out unchanged. A new 'R' inside a partial frame
 * rejects that partial frame and starts a new candidate. A NULL parser or out
 * pointer returns INVALID and consumes nothing. Sequence numbers start at 1
 * and wrap to 0 after UINT32_MAX; diagnostic counters saturate at UINT64_MAX.
 */
oamr_mb_push_t oamr_mb_push(oamr_mb_t *parser,
                            uint8_t byte,
                            oamr_mb_sample_t *out);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_MB7060_H */
