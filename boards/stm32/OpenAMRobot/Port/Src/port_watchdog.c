/* SPDX-License-Identifier: MIT */
/*
 * Reset cause capture and the independent watchdog (I8 baseline item 4:
 * IWDG fed only by the control loop; reset reason in the status).
 *
 * RCC_RSR is read once, before anything clears it, then cleared with RMVF so
 * the next boot reports only its own cause. Several flags can be set at once
 * (a pin reset accompanies most others), so all are mapped. The mapping is a
 * proposal for I8 rev 3 decision 6; flags not mapped give UNKNOWN.
 * Note: brown-out reset is enabled by the option bytes (BOR level), not by
 * this code. TODO(Evans): BOR level.
 */
#include "port_internal.h"

static oamr_reset_cause_t s_cause;
static uint32_t s_raw_rsr;

void port_capture_reset_cause(void)
{
    const uint32_t rsr = RCC->RSR;
    s_raw_rsr = rsr;
    oamr_reset_cause_t c = 0;
    if (rsr & RCC_RSR_PORRSTF) c |= OAMR_RESET_POWER_ON;
    if (rsr & RCC_RSR_PINRSTF) c |= OAMR_RESET_PIN;
    if (rsr & RCC_RSR_BORRSTF) c |= OAMR_RESET_BROWNOUT;
    if (rsr & RCC_RSR_IWDG1RSTF) c |= OAMR_RESET_INDEPENDENT_WATCHDOG;
    if (rsr & RCC_RSR_WWDG1RSTF) c |= OAMR_RESET_WINDOW_WATCHDOG;
    if (rsr & RCC_RSR_SFTRSTF) c |= OAMR_RESET_SOFTWARE;
    if (rsr & RCC_RSR_LPWRRSTF) c |= OAMR_RESET_LOW_POWER;
    if (rsr & RCC_RSR_CPURSTF) c |= OAMR_RESET_CPU;
    if (rsr & (RCC_RSR_D1RSTF | RCC_RSR_D2RSTF)) c |= OAMR_RESET_DOMAIN;
    if (c == 0u) c = OAMR_RESET_UNKNOWN;
    s_cause = c;
    RCC->RSR |= RCC_RSR_RMVF;
}

oamr_reset_cause_t port_reset_cause(void)
{
    return s_cause;
}

#define IWDG_KEY_ENABLE 0xCCCCu
#define IWDG_KEY_RELOAD 0xAAAAu
#define IWDG_KEY_WRITE_ACCESS 0x5555u

void port_iwdg_start(uint32_t timeout_ms)
{
#ifdef DEBUG
    /* Stop the watchdog while the core is halted by the debugger. */
    DBGMCU->APB4FZ1 |= DBGMCU_APB4FZ1_DBG_IWDG1;
#endif
    /* Prescaler /32 gives 1 kHz from the 32 kHz LSI: reload = timeout in ms. */
    uint32_t reload = timeout_ms;
    if (reload > 0x0FFFu) {
        reload = 0x0FFFu;
    }
    IWDG1->KR = IWDG_KEY_ENABLE;      /* starts the LSI and the counter */
    IWDG1->KR = IWDG_KEY_WRITE_ACCESS;
    IWDG1->PR = 3u;                   /* /32 */
    IWDG1->RLR = reload;
    while (IWDG1->SR != 0u) {
    }
    IWDG1->KR = IWDG_KEY_RELOAD;
}

void port_iwdg_feed(void)
{
    IWDG1->KR = IWDG_KEY_RELOAD;
}
