/* SPDX-License-Identifier: MIT */
/* 64-bit microsecond clock of the telemetry image: 1 ms ticks counted in the
 * SysTick interrupt plus the SysTick down-counter for the part of a tick. */
#include "main.h"
#include "tlm.h"

#define TLM_US_PER_TICK (1000000u / TLM_TICK_HZ)

static volatile uint64_t s_ticks;
static uint64_t s_last_us;

void tlm_time_tick_isr(void)
{
    s_ticks++; /* only writer; readers mask interrupts */
}

uint64_t tlm_time_us(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint64_t ticks = s_ticks;
    uint32_t val = SysTick->VAL;
    const uint32_t load = SysTick->LOAD;
    if ((SCB->ICSR & SCB_ICSR_PENDSTSET_Msk) != 0u) {
        /* The counter wrapped and the tick interrupt has not run yet. */
        ticks++;
        val = SysTick->VAL;
    }
    uint64_t us = ticks * TLM_US_PER_TICK;
    if (load != 0u) {
        us += ((uint64_t)(load - val) * TLM_US_PER_TICK) / ((uint64_t)load + 1u);
    }
    /* A caller that preempted the tick interrupt before its increment would
     * see the previous tick with a small counter: never step backwards. */
    if (us < s_last_us) {
        us = s_last_us;
    } else {
        s_last_us = us;
    }
    __set_PRIMASK(primask);
    return us;
}
