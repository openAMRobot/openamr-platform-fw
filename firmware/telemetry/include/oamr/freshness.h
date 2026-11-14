/* SPDX-License-Identifier: MIT */
/*
 * Freshness helper: age and STALE classification of one data source on a
 * monotonic microsecond clock supplied by the caller.
 *
 * Clock: uint32_t microseconds that wrap every 2^32 us (about 71.6 min). Ages
 * are computed with modular arithmetic, so a wrap of the clock between the
 * sample and "now" is handled. An age of 2^31 us or more cannot be told
 * apart from a timestamp in the future; both are classified STALE.
 *
 * Wrap safety over long gaps: oamr_fresh_classify() latches the expired
 * state the first time it sees the source stale. A source that is classified
 * at least once per stale limit therefore never turns FRESH again by clock
 * wrap alone; only a new sample (oamr_fresh_mark) makes it FRESH again.
 *
 * The stale limit has no default. An unset limit (0) or a limit of 2^31 us or
 * more leaves the tracker NOT_CONFIGURED and every classification UNKNOWN.
 *
 * Status values follow the I8 rule that 0 is UNKNOWN.
 */
#ifndef OAMR_FRESHNESS_H
#define OAMR_FRESHNESS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OAMR_FRESH_MAX_LIMIT_US 0x7FFFFFFFu

typedef enum {
    OAMR_FRESH_UNKNOWN = 0, /* tracker not configured */
    OAMR_FRESH_NEVER = 1,   /* configured, no sample since init or reset */
    OAMR_FRESH_STALE = 2,   /* last sample older than the limit, or in the future */
    OAMR_FRESH_FRESH = 3    /* last sample younger than the limit */
} oamr_fresh_state_t;

typedef struct {
    uint32_t stale_after_us; /* age at which the source is STALE; no default */
} oamr_fresh_config_t;

typedef struct {
    oamr_fresh_config_t cfg;
    bool configured;
    bool has_sample;
    bool expired;      /* latched STALE until the next mark */
    uint32_t stamp_us; /* receipt time of the last sample */
    uint32_t samples;  /* number of marks since init */
} oamr_fresh_t;

/* Wrap-safe age: now - then modulo 2^32. */
static inline uint32_t oamr_age_us(uint32_t now_us, uint32_t then_us)
{
    return (uint32_t)(now_us - then_us);
}

/* Returns true when the configuration is complete and valid. */
bool oamr_fresh_init(oamr_fresh_t *f, const oamr_fresh_config_t *cfg);
/* Forget the last sample (for example after a producer restart). */
void oamr_fresh_reset(oamr_fresh_t *f);
/* A new sample was received at stamp_us (local receipt time). */
void oamr_fresh_mark(oamr_fresh_t *f, uint32_t stamp_us);
/* Classify at now_us; latches the expired state. */
oamr_fresh_state_t oamr_fresh_classify(oamr_fresh_t *f, uint32_t now_us);
/* Age of the last sample at now_us. Returns false (and leaves *age_us
 * untouched) when there is no sample, the tracker is not configured, or the
 * age is not representable (2^31 us or more). */
bool oamr_fresh_age(const oamr_fresh_t *f, uint32_t now_us, uint32_t *age_us);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_FRESHNESS_H */
