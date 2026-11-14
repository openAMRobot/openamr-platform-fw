/* SPDX-License-Identifier: MIT */
/*
 * Diagnostic console of the telemetry image: USART3, PD8 (TX) and PD9 (RX),
 * 115200 8N1. USART3 is not part of the CubeMX project, so clock, pins and
 * the UART are set up here; the generated HAL_UART_MspInit() has no USART3
 * branch and does nothing for this handle. Transmit only, polling, no
 * interrupt.
 */
#include "main.h"
#include "tlm.h"

static UART_HandleTypeDef s_console;
static bool s_ready;

bool tlm_console_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_USART3_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOD, &gpio);

    s_console.Instance = USART3;
    s_console.Init.BaudRate = TLM_CONSOLE_BAUD;
    s_console.Init.WordLength = UART_WORDLENGTH_8B;
    s_console.Init.StopBits = UART_STOPBITS_1;
    s_console.Init.Parity = UART_PARITY_NONE;
    s_console.Init.Mode = UART_MODE_TX_RX;
    s_console.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    s_console.Init.OverSampling = UART_OVERSAMPLING_16;
    s_console.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    s_console.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    s_console.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    s_ready = HAL_UART_Init(&s_console) == HAL_OK;
    return s_ready;
}

bool tlm_console_write(const char *data, size_t len)
{
    if (!s_ready || len > 0xFFFFu) {
        return false;
    }
    return HAL_UART_Transmit(&s_console, (const uint8_t *)data, (uint16_t)len,
                             TLM_CONSOLE_TX_TIMEOUT_MS) == HAL_OK;
}

void tlm_console_write_raw(const char *text)
{
    if (!s_ready) {
        return;
    }
    while (*text != '\0') {
        uint32_t guard = 2000000u; /* bounded wait so a dead UART cannot hide the stop loop */
        while ((USART3->ISR & USART_ISR_TXE_TXFNF) == 0u && guard > 0u) {
            guard--;
        }
        USART3->TDR = (uint8_t)*text++;
    }
}
