/* SPDX-License-Identifier: MIT */
/* Application time of the telemetry image (firmware/telemetry/include/oamr/
 * mono_time.h): one monotonic uint64_t count of microseconds since HAL_Init().
 *
 * This file is the image's one place where a hardware counter becomes
 * application time. The hardware counter is SysTick, a 24-bit down-counter
 * that keeps its native width; its wraps are counted in software as 1 ms
 * ticks in a 64-bit variable, and the counter value gives the part of the
 * current tick. No other counter is read for time in this image: frame and
 * byte stamps are taken with tlm_time_us() on receipt. A future source with
 * its own counter (for example the 16-bit FDCAN time stamp) would be
 * converted with oamr_mono_ext_update(), not here. */
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
