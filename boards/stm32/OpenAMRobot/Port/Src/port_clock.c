/* SPDX-License-Identifier: MIT */
/*
 * Monotonic 64-bit microsecond clock (I8 baseline item 5) from TIM2, a 32-bit
 * timer counting at 1 MHz. The 32-bit counter wraps every 71.6 minutes; the
 * wrap is folded into the upper word on every read. The control tick reads
 * the clock every 20 ms, so no wrap is ever missed. Reads are made with
 * interrupts masked so the ISR and the background loop see one sequence.
 */
#include "port_internal.h"

static uint32_t s_last;
static uint64_t s_high;

/* APB1 timers run at twice PCLK1 when the APB1 prescaler is not 1. */
static uint32_t tim_apb1_clock_hz(void)
{
    const uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    const uint32_t d2ppre1 = (RCC->D2CFGR & RCC_D2CFGR_D2PPRE1) >> RCC_D2CFGR_D2PPRE1_Pos;
    return (d2ppre1 < 4u) ? pclk1 : 2u * pclk1;
}

void port_clock_init(void)
{
    __HAL_RCC_TIM2_CLK_ENABLE();
    TIM2->CR1 = 0;
    TIM2->PSC = (tim_apb1_clock_hz() / 1000000u) - 1u;
    TIM2->ARR = 0xFFFFFFFFu;
    TIM2->CNT = 0;
    TIM2->EGR = TIM_EGR_UG;  /* load the prescaler */
    TIM2->SR = 0;
    TIM2->CR1 = TIM_CR1_CEN;
    s_last = 0;
    s_high = 0;
}

uint64_t port_clock_now_us(void *ctx)
{
    (void)ctx;
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    const uint32_t cnt = TIM2->CNT;
    if (cnt < s_last) {
        s_high += (uint64_t)1u << 32;
    }
    s_last = cnt;
    const uint64_t now = s_high | cnt;
    __set_PRIMASK(primask);
    return now;
}

const oamr_clock_t port_clock = {port_clock_now_us, NULL};

/* TIM6: control tick interrupt. */
void port_tick_timer_start(uint32_t period_us)
{
    __HAL_RCC_TIM6_CLK_ENABLE();
    TIM6->CR1 = 0;
    TIM6->PSC = (tim_apb1_clock_hz() / 1000000u) - 1u;
    TIM6->ARR = period_us - 1u;
    TIM6->CNT = 0;
    TIM6->EGR = TIM_EGR_UG;
    TIM6->SR = 0;
    TIM6->DIER = TIM_DIER_UIE;
    /* Highest application priority (I8 baseline item 6). */
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
    TIM6->CR1 = TIM_CR1_CEN;
}
