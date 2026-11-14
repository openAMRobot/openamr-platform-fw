/* SPDX-License-Identifier: MIT */
/*
 * Reuse of the CubeMX-generated stm32h7xx_it.c of boards/stm32/OpenAMRobot,
 * unchanged. The fault handlers and every peripheral interrupt handler are
 * the generated ones. Three generated handlers are renamed and left unused,
 * because the vector-table names go to FreeRTOS in this image:
 *   SVC_Handler, PendSV_Handler -> the ARM_CM7 port (see FreeRTOSConfig.h)
 *   SysTick_Handler             -> below: HAL tick, image clock, RTOS tick
 */
#define SVC_Handler openamr_cubemx_SVC_Handler_not_used
#define PendSV_Handler openamr_cubemx_PendSV_Handler_not_used
#define SysTick_Handler openamr_cubemx_SysTick_Handler_not_used
#include "../../OpenAMRobot/Core/Src/stm32h7xx_it.c"
#undef SVC_Handler
#undef PendSV_Handler
#undef SysTick_Handler

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
