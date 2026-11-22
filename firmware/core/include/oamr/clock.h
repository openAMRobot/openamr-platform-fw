/* SPDX-License-Identifier: MIT */
/*
 * Core clock interface: monotonic 64-bit microseconds (I8 baseline item 5).
 *
 * The core never reads a board timer itself. The board port supplies now_us(),
 * which must never go backwards and must not wrap in the lifetime of the robot.
 * Elapsed time is always computed as (now - then) in uint64_t.
 */
#ifndef OAMR_CLOCK_H
#define OAMR_CLOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t (*now_us)(void *ctx);
    void *ctx;
} oamr_clock_t;

static inline uint64_t oamr_clock_now_us(const oamr_clock_t *clk)
{
    return clk->now_us(clk->ctx);
}

/* Elapsed microseconds since `then_us`; 0 if `then_us` lies in the future. */
static inline uint64_t oamr_elapsed_us(uint64_t now_us, uint64_t then_us)
{
    return (now_us >= then_us) ? (now_us - then_us) : 0u;
}

#define OAMR_MS_TO_US(ms) ((uint64_t)(ms) * 1000u)

#ifdef __cplusplus
}
#endif

#endif /* OAMR_CLOCK_H */
