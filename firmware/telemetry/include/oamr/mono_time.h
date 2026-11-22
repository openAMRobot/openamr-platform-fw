/* SPDX-License-Identifier: MIT */
/*
 * Application time and the one place where a hardware counter becomes it.
 *
 * Application time: every module under firmware/ takes time as one monotonic
 * uint64_t count of microseconds (since an origin the board port chooses,
 * normally its start-up). It does not wrap in the life of the product
 * (2^64 us is about 584 000 years), so modules compare and subtract it
 * directly and never use modular arithmetic. A time earlier than a stored
 * one is a caller error; oamr_mono_elapsed_us() saturates it to 0 and each
 * module documents what it does with it.
 *
 * Hardware counters keep their native width (for example a 16-bit FDCAN
 * time stamp counter, a 32-bit general-purpose timer or the 32-bit DWT
 * cycle counter). The board port converts a counter reading to application
 * time here, with oamr_mono_ext_update(), and nowhere else:
 *
 *   - the counter counts up by one per tick and wraps from 2^width - 1 to 0;
 *   - ticks_per_us is an integer (1 for a 1 MHz timer);
 *   - oamr_mono_ext_update() must be called at least once per counter
 *     period (2^width ticks). A whole period between two calls cannot be
 *     seen in the counter and is lost; that is a property of every counter
 *     extension, not of this code. The caller must guarantee the call rate.
 *
 * The extension keeps the full tick count in 64 bits and divides only on
 * output, so no fraction of a microsecond is lost between calls.
 */
#ifndef OAMR_MONO_TIME_H
#define OAMR_MONO_TIME_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        bool configured;
        uint32_t mask;         /* 2^width - 1 */
        uint32_t ticks_per_us; /* >= 1 */
        uint32_t last_raw;     /* last counter reading, already masked */
        uint64_t ticks;        /* ticks since init, 64 bit */
    } oamr_mono_ext_t;

    /* width_bits 1..32, ticks_per_us >= 1. first_raw is the counter reading
     * that becomes application time 0. Returns false (and leaves the extension
     * not configured, every update returning 0) on an invalid parameter. */
    bool oamr_mono_ext_init(oamr_mono_ext_t *m, uint8_t width_bits, uint32_t ticks_per_us,
                            uint32_t first_raw);

    /* Feed one counter reading; returns application time in microseconds.
     * Bits above the counter width are ignored. */
    uint64_t oamr_mono_ext_update(oamr_mono_ext_t *m, uint32_t raw);

    /* now - then, or 0 when then is later than now. */
    static inline uint64_t oamr_mono_elapsed_us(uint64_t now_us, uint64_t then_us)
    {
        return now_us >= then_us ? now_us - then_us : 0u;
    }

#ifdef __cplusplus
}
#endif

#endif /* OAMR_MONO_TIME_H */
