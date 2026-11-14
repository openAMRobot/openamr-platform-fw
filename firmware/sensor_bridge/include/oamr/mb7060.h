/* SPDX-License-Identifier: MIT */
/*
 * MaxBotix MB7060 (XL-MaxSonar-WR) serial range parser and trigger sequencer.
 *
 * Frame source: MaxBotix XL-MaxSonar-WR/WRC Series datasheet, document
 * PD11838z (copyright 2005 to 2022), pin 5 description: an ASCII capital
 * "R", then ASCII digits with the range in centimetres up to a maximum of
 * 765, then a carriage return (ASCII 13); 9600 baud 8N1. The level is
 * RS-232 format at 0 to Vcc, so the UART must invert the line (RXINV); this
 * parser receives the bytes after that inversion.
 *
 * Range facts from the same datasheet:
 *   - minimum reported distance 20 cm; targets closer than 20 cm typically
 *     range as 20 cm ("Sensor Minimum Distance");
 *   - 765 is reported as "no detection" when no target is found;
 *   - "000" is a fail-safe output of the filtered variants; the MB7060 is
 *     not documented to send it, but it is decoded as a sensor fault;
 *   - pin 4 high for 20 us or more commands one range reading; the MB7060
 *     cycle is about 99 ms with serial data reported at about 94.3 ms.
 *
 * Classification (I8: a missing return is UNKNOWN, never free space):
 *   OK          20 < range < 765 cm, valid = true.
 *   AT_MIN      range <= 20 cm: something is at 20 cm or closer. valid = false
 *               because the distance is only an upper bound. A consumer must
 *               treat it as "obstacle at or inside min range" (REP 117 -Inf),
 *               never as clear.
 *   NO_TARGET   765: no detection. valid = false. Not free space.
 *   FAULT       "000".
 *   MALFORMED is never returned as a sample; it is counted.
 *
 * One sample per trigger: the parser accepts a frame only while armed. The
 * caller arms it when it pulses the trigger (oamr_mb_arm). The first good
 * frame disarms it; any later frame is counted as unsolicited and dropped,
 * so a stale or repeated frame is never returned as the answer to a new
 * trigger.
 */
#ifndef OAMR_MB7060_H
#define OAMR_MB7060_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OAMR_MB_MIN_REPORTED_CM 20u  /* datasheet: minimum reported distance */
#define OAMR_MB_NO_TARGET_CM 765u    /* datasheet: no-detection output */
#define OAMR_MB_DIGITS 3u            /* "R" + 3 digits (range up to 765) + CR */
#define OAMR_MB_FRAME_LEN 5u

typedef enum {
    OAMR_MB_UNKNOWN = 0,
    OAMR_MB_OK = 1,
    OAMR_MB_AT_MIN = 2,
    OAMR_MB_NO_TARGET = 3,
    OAMR_MB_FAULT = 4
} oamr_mb_status_t;

typedef struct {
    bool valid;            /* true only for OAMR_MB_OK */
    oamr_mb_status_t status;
    uint16_t range_cm;     /* as sent */
    uint32_t seq;
} oamr_mb_sample_t;

typedef struct {
    uint32_t frames_ok;    /* well-formed frames accepted as a sample */
    uint32_t malformed;    /* wrong length, non-digit, value above 765 */
    uint32_t unsolicited;  /* well-formed frames while not armed */
    uint32_t bytes_discarded;
} oamr_mb_counters_t;

typedef enum {
    OAMR_MB_PUSH_NONE = 0,
    OAMR_MB_PUSH_SAMPLE = 1,
    OAMR_MB_PUSH_MALFORMED = 2,
    OAMR_MB_PUSH_UNSOLICITED = 3
} oamr_mb_push_t;

typedef struct {
    uint8_t buf[OAMR_MB_FRAME_LEN];
    uint8_t len;
    bool in_frame;
    bool armed;
    uint32_t seq;
    oamr_mb_counters_t counters;
} oamr_mb_t;

void oamr_mb_init(oamr_mb_t *p);
/* The trigger for this sensor was pulsed: accept exactly one frame. Drops a
 * partial frame that started before the trigger. */
void oamr_mb_arm(oamr_mb_t *p);
/* The reading timed out: stop accepting a frame for that trigger. */
void oamr_mb_disarm(oamr_mb_t *p);
oamr_mb_push_t oamr_mb_push(oamr_mb_t *p, uint8_t byte, oamr_mb_sample_t *out);

/* ---- Trigger sequencer: pure decision helper, never touches a pin ---- */

#define OAMR_MBSEQ_MAX_SENSORS 4u

typedef struct {
    uint8_t n_sensors;         /* 1 to 4; order is index 0, 1, ... */
    uint32_t reading_timeout_us; /* trigger to frame; no default. TODO(Evans) */
    uint32_t gap_us;           /* end of one reading to next trigger; no default. TODO(Evans) */
    bool gap_set;              /* must be true: gap_us = 0 is a decision, not a default */
} oamr_mbseq_config_t;

typedef enum {
    OAMR_MBSEQ_NOT_CONFIGURED = 0,
    OAMR_MBSEQ_WAIT = 1,       /* nothing to do now */
    OAMR_MBSEQ_TRIGGER = 2,    /* pulse the trigger of .sensor now, then call _triggered */
    OAMR_MBSEQ_TIMED_OUT = 3   /* .sensor gave no frame in time: report it UNKNOWN, disarm */
} oamr_mbseq_action_t;

typedef struct {
    oamr_mbseq_action_t action;
    uint8_t sensor;
} oamr_mbseq_decision_t;

typedef struct {
    oamr_mbseq_config_t cfg;
    bool configured;
    bool waiting;          /* a trigger is out and no frame or timeout yet */
    bool started;          /* at least one trigger issued */
    uint8_t current;       /* sensor of the outstanding or last trigger */
    uint32_t trigger_us;
    uint32_t done_us;      /* time the last reading completed or timed out */
    uint32_t timeouts[OAMR_MBSEQ_MAX_SENSORS];
    uint32_t late_frames;  /* frames reported for a sensor that is not waited for */
} oamr_mbseq_t;

bool oamr_mbseq_init(oamr_mbseq_t *s, const oamr_mbseq_config_t *cfg);
/* What should happen at now_us. Call periodically. A TIMED_OUT decision is
 * returned once per trigger and moves the sequence on. */
oamr_mbseq_decision_t oamr_mbseq_step(oamr_mbseq_t *s, uint32_t now_us);
/* The caller pulsed the trigger of `sensor` at now_us (after a TRIGGER decision). */
void oamr_mbseq_triggered(oamr_mbseq_t *s, uint8_t sensor, uint32_t now_us);
/* A sample was received for `sensor`. Returns false if that sensor was not
 * being waited for (the frame must then be ignored). */
bool oamr_mbseq_reading(oamr_mbseq_t *s, uint8_t sensor, uint32_t now_us);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_MB7060_H */
