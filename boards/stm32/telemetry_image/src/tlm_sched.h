/* SPDX-License-Identifier: MIT */
/*
 * Deadline accounting of a periodic task, as pure C for the host tests.
 *
 * A task that runs on vTaskDelayUntil() timing and finds its deadline
 * already passed (xTaskDelayUntil() returned pdFALSE) calls
 * tlm_sched_late(). It counts every deadline that passed without a run,
 * the one just missed included, records the lateness, and returns the new
 * reference wake time: "now". The next run is one period after now, so a
 * late task runs once and is not followed by a burst of catch-up runs.
 */
#ifndef TLM_SCHED_H
#define TLM_SCHED_H

#include <stdint.h>

typedef struct
{
    uint32_t runs;          /* periodic runs */
    uint32_t missed;        /* deadlines that passed without a run on time */
    uint32_t late_events;   /* runs that started late */
    uint32_t late_max_ticks; /* largest lateness seen */
} tlm_sched_stats_t;

/* deadline: the wake time xTaskDelayUntil() just computed (already past);
 * now: the current tick count; period >= 1. Wrap-safe on 32-bit ticks.
 * Returns the wake reference to use from now on (now). */
uint32_t tlm_sched_late(tlm_sched_stats_t *st, uint32_t deadline, uint32_t now, uint32_t period);

#endif /* TLM_SCHED_H */
