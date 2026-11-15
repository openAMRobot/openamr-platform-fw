/* SPDX-License-Identifier: MIT */
#include "tlm_sched.h"

uint32_t tlm_sched_late(tlm_sched_stats_t *st, uint32_t deadline, uint32_t now, uint32_t period)
{
    const uint32_t late = now - deadline; /* modular: tick counts wrap */
    const uint32_t p = period == 0u ? 1u : period;
    st->missed += 1u + late / p;
    st->late_events++;
    if (late > st->late_max_ticks)
    {
        st->late_max_ticks = late;
    }
    return now;
}
