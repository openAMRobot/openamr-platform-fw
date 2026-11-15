/* SPDX-License-Identifier: MIT */
/*
 * Reuse of the CubeMX-generated stm32h7xx_it.c of boards/stm32/OpenAMRobot,
 * unchanged. The fault handlers and every peripheral interrupt handler are
 * the generated ones. Three generated handlers are renamed and left unused,
 * because the vector-table names go to FreeRTOS in this image:
 *   SVC_Handler, PendSV_Handler -> the ARM_CM7 port (see FreeRTOSConfig.h)
 *   SysTick_Handler             -> below: HAL tick, image clock, RTOS tick
 * One generated handler is wrapped, not replaced:
 *   FDCAN1_IT0_IRQHandler       -> below: the generated handler (HAL dispatch
 *                                  to HAL_FDCAN_RxFifo0Callback) between the
 *                                  per-entry frame budget of tlm_can1.c
 */
#define SVC_Handler openamr_cubemx_SVC_Handler_not_used
#define PendSV_Handler openamr_cubemx_PendSV_Handler_not_used
#define SysTick_Handler openamr_cubemx_SysTick_Handler_not_used
#define FDCAN1_IT0_IRQHandler openamr_cubemx_FDCAN1_IT0_IRQHandler
#include "../../OpenAMRobot/Core/Src/stm32h7xx_it.c"
#undef SVC_Handler
#undef PendSV_Handler
#undef SysTick_Handler
#undef FDCAN1_IT0_IRQHandler

#include "FreeRTOS.h"
#include "task.h"
#include "tlm.h"

extern void xPortSysTickHandler(void);
void SysTick_Handler(void);

void SysTick_Handler(void)
{
    tlm_time_tick_isr();
    HAL_IncTick();
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xPortSysTickHandler();
    }
}

void FDCAN1_IT0_IRQHandler(void);

void FDCAN1_IT0_IRQHandler(void)
{
    tlm_can1_irq_begin();
    openamr_cubemx_FDCAN1_IT0_IRQHandler(); /* generated: HAL_FDCAN_IRQHandler(&hfdcan1) */
    tlm_can1_irq_end();
}
