/* SPDX-License-Identifier: MIT */
/*
 * Reuse of the CubeMX-generated main.c of boards/stm32/OpenAMRobot, unchanged.
 *
 * The generated file is included verbatim so its static MX_*_Init functions,
 * SystemClock_Config(), MPU_Config(), Error_Handler() and the peripheral
 * handles are the single source of the board configuration; nothing is
 * copied. Its main() is renamed and never called: this image has its own
 * main() in tlm_main.c.
 *
 * What this image does NOT take from the generated main():
 *   - MX_FDCAN2_Init, MX_FDCAN3_Init, MX_SPI4_Init, MX_ETH_Init: not used here;
 *   - the USER CODE 2 block that turns PE1 into the LD2 lamp output: this
 *     image switches no lamp. PE1 is only released from UART8_TX.
 */
#define main openamr_cubemx_main_not_used
#include "../../OpenAMRobot/Core/Src/main.c"
#undef main

#include "tlm.h"

uint32_t tlm_board_init(void)
{
    MPU_Config();
    HAL_Init();
    SystemClock_Config();

    /* Reset flags first, before anything can cause another reset. */
    const uint32_t rsr = RCC->RSR;
    __HAL_RCC_CLEAR_RESET_FLAGS();

    /* Generated GPIO state: PE2, PE3, PG2, PG3 (MB7060 trigger lines) are
     * driven low and stay low; this image never pulses them. */
    MX_GPIO_Init();
    MX_FDCAN1_Init();
    MX_UART4_Init();
    MX_UART5_Init();
    MX_UART7_Init();
    MX_UART8_Init();
    MX_USART2_UART_Init();
    MX_USART6_UART_Init();

    /* Pin-map exceptions of the generated main() (NUCLEO-H723ZG pin map,
     * section 7): the TX pins of the receive-only MB7060 UARTs are released. */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_0);
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_12);
    HAL_GPIO_DeInit(GPIOF, GPIO_PIN_7);
    HAL_GPIO_DeInit(GPIOE, GPIO_PIN_1);
    return rsr;
}
