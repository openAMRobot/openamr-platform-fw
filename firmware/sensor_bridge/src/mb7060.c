/* SPDX-License-Identifier: MIT */
#include "oamr/mb7060.h"

#include <string.h>

#define CHAR_R 0x52u
#define CHAR_CR 0x0Du

void oamr_mb_init(oamr_mb_t *p)
{
    memset(p, 0, sizeof(*p));
}

void oamr_mb_arm(oamr_mb_t *p)
{
    if (p->in_frame) {
        p->counters.bytes_discarded += p->len;
    }
    p->in_frame = false;
    p->len = 0u;
    p->armed = true;
}

void oamr_mb_disarm(oamr_mb_t *p)
{
    p->armed = false;
}

static bool is_digit(uint8_t c)
{
    return c >= 0x30u && c <= 0x39u;
}

oamr_mb_push_t oamr_mb_push(oamr_mb_t *p, uint8_t byte, oamr_mb_sample_t *out)
{
    if (byte == CHAR_R) {
        if (p->in_frame) {
            /* A new frame started before the old one ended. */
            p->counters.malformed++;
            p->counters.bytes_discarded += p->len;
            p->buf[0] = byte;
            p->len = 1u;
            return OAMR_MB_PUSH_MALFORMED;
        }
        p->in_frame = true;
        p->buf[0] = byte;
        p->len = 1u;
        return OAMR_MB_PUSH_NONE;
    }
    if (!p->in_frame) {
        p->counters.bytes_discarded++;
        return OAMR_MB_PUSH_NONE;
    }
    if (byte != CHAR_CR) {
        if (!is_digit(byte) || p->len >= 1u + OAMR_MB_DIGITS) {
            p->counters.malformed++;
            p->counters.bytes_discarded += (uint32_t)p->len + 1u;
            p->in_frame = false;
            p->len = 0u;
            return OAMR_MB_PUSH_MALFORMED;
        }
        p->buf[p->len++] = byte;
        return OAMR_MB_PUSH_NONE;
    }
    /* Carriage return: frame complete. */
    const uint8_t len = p->len;
    p->in_frame = false;
    p->len = 0u;
    if (len != 1u + OAMR_MB_DIGITS) {
        p->counters.malformed++;
        p->counters.bytes_discarded += (uint32_t)len + 1u;
        return OAMR_MB_PUSH_MALFORMED;
    }
    uint16_t v = 0u;
    for (uint8_t i = 1u; i < len; i++) {
        v = (uint16_t)(v * 10u + (uint16_t)(p->buf[i] - 0x30u));
    }
    if (v > OAMR_MB_NO_TARGET_CM) {
        p->counters.malformed++;
        return OAMR_MB_PUSH_MALFORMED;
    }
    if (!p->armed) {
        p->counters.unsolicited++;
        return OAMR_MB_PUSH_UNSOLICITED;
    }
    p->armed = false;
    memset(out, 0, sizeof(*out));
    out->range_cm = v;
    if (v == 0u) {
        out->status = OAMR_MB_FAULT;
    } else if (v == OAMR_MB_NO_TARGET_CM) {
        out->status = OAMR_MB_NO_TARGET;
    } else if (v <= OAMR_MB_MIN_REPORTED_CM) {
        out->status = OAMR_MB_AT_MIN;
    } else {
        out->status = OAMR_MB_OK;
    }
    out->valid = (out->status == OAMR_MB_OK);
    out->seq = ++p->seq;
    p->counters.frames_ok++;
    return OAMR_MB_PUSH_SAMPLE;
}

/* ---- sequencer ---- */

bool oamr_mbseq_init(oamr_mbseq_t *s, const oamr_mbseq_config_t *cfg)
{
    memset(s, 0, sizeof(*s));
    if (cfg == NULL || cfg->n_sensors == 0u || cfg->n_sensors > OAMR_MBSEQ_MAX_SENSORS ||
        cfg->reading_timeout_us == 0u || cfg->reading_timeout_us > 0x7FFFFFFFu ||
        !cfg->gap_set || cfg->gap_us > 0x7FFFFFFFu) {
        return false;
    }
    s->cfg = *cfg;
    s->configured = true;
    return true;
}

oamr_mbseq_decision_t oamr_mbseq_step(oamr_mbseq_t *s, uint32_t now_us)
{
    oamr_mbseq_decision_t d = {OAMR_MBSEQ_WAIT, 0u};
    if (!s->configured) {
        d.action = OAMR_MBSEQ_NOT_CONFIGURED;
        return d;
    }
    if (s->waiting) {
        if ((uint32_t)(now_us - s->trigger_us) >= s->cfg.reading_timeout_us) {
            s->waiting = false;
            s->done_us = now_us;
            s->timeouts[s->current]++;
            d.action = OAMR_MBSEQ_TIMED_OUT;
            d.sensor = s->current;
        }
        return d;
    }
    if (s->started && (uint32_t)(now_us - s->done_us) < s->cfg.gap_us) {
        return d;
    }
    d.action = OAMR_MBSEQ_TRIGGER;
    d.sensor = 0u;
    if (s->started) {
        d.sensor = (uint8_t)((s->current + 1u) % s->cfg.n_sensors);
    }
    return d;
}

void oamr_mbseq_triggered(oamr_mbseq_t *s, uint8_t sensor, uint32_t now_us)
{
    if (!s->configured || s->waiting || sensor >= s->cfg.n_sensors) {
        return;
    }
    s->current = sensor;
    s->trigger_us = now_us;
    s->waiting = true;
    s->started = true;
}

bool oamr_mbseq_reading(oamr_mbseq_t *s, uint8_t sensor, uint32_t now_us)
{
    if (!s->configured || !s->waiting || sensor != s->current) {
        s->late_frames++;
        return false;
    }
    s->waiting = false;
    s->done_us = now_us;
    return true;
}
