/* SPDX-License-Identifier: MIT */
/*
 * Freshness helper: age and STALE classification of one data source on a
 * monotonic microsecond clock supplied by the caller.
 *
 * Clock: application time, uint64_t monotonic microseconds (oamr/mono_time.h).
 * It does not wrap, so ages are plain differences. A sample stamped later
 * than "now" (a caller error) is classified STALE and has no age.
 *
 * oamr_fresh_classify() latches the expired state the first time it sees the
 * source stale; only a new sample (oamr_fresh_mark) makes it FRESH again.
 *
 * The stale limit has no default. An unset limit (0) leaves the tracker
 * NOT_CONFIGURED and every classification UNKNOWN.
 *
 * Status values follow the I8 rule that 0 is UNKNOWN.
 */
#ifndef OAMR_FRESHNESS_H
#define OAMR_FRESHNESS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        OAMR_FRESH_UNKNOWN = 0, /* tracker not configured */
        OAMR_FRESH_NEVER = 1,   /* configured, no sample since init or reset */
        OAMR_FRESH_STALE = 2,   /* last sample older than the limit, or in the future */
        OAMR_FRESH_FRESH = 3    /* last sample younger than the limit */
    } oamr_fresh_state_t;

    typedef struct
    {
        uint64_t stale_after_us; /* age at which the source is STALE; no default */
    } oamr_fresh_config_t;

    typedef struct
    {
        oamr_fresh_config_t cfg;
        bool configured;
        bool has_sample;
        bool expired;      /* latched STALE until the next mark */
        uint64_t stamp_us; /* receipt time of the last sample */
        uint32_t samples;  /* number of marks since init */
    } oamr_fresh_t;

    /* Returns true when the configuration is complete and valid. */
    bool oamr_fresh_init(oamr_fresh_t *f, const oamr_fresh_config_t *cfg);
    /* Forget the last sample (for example after a producer restart). */
    void oamr_fresh_reset(oamr_fresh_t *f);
    /* A new sample was received at stamp_us (local receipt time). */
    void oamr_fresh_mark(oamr_fresh_t *f, uint64_t stamp_us);
    /* Classify at now_us; latches the expired state. */
    oamr_fresh_state_t oamr_fresh_classify(oamr_fresh_t *f, uint64_t now_us);
    /* Age of the last sample at now_us. Returns false (and leaves *age_us
     * untouched) when there is no sample, the tracker is not configured, or the
     * sample is stamped later than now_us. */
    bool oamr_fresh_age(const oamr_fresh_t *f, uint64_t now_us, uint64_t *age_us);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_FRESHNESS_H */
