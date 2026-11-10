/* SPDX-License-Identifier: MIT */
/*
 * Host transport hook: USART3 on the ST-LINK virtual COM port (PD8 TX,
 * PD9 RX, pin map: reserved for the debug console). First bench transport is
 * serial (findings on micro-ROS over Ethernet on this board; platform-fw #21
 * keeps Ethernet UDP as the 2.0 target). The four functions map one-to-one
 * onto the micro-ROS custom transport callbacks; the micro-ROS client itself
 * is not part of this change. Serviced from the background loop only.
 *
 * USART3 is not in the .ioc; it is set up here so the generated code stays
 * untouched. TODO(Evans): add USART3 (asynchronous, PD8/PD9) to the .ioc so
 * CubeMX owns it, then drop the manual init below.
 */
#include "port_internal.h"

#define SERIAL_BAUD 115200u          /* matches the Gate A micro_ros_agent serial setting */
#define SERIAL_TX_TIMEOUT_MS 5u

static UART_HandleTypeDef s_uart;
static bool s_open;

static bool serial_open(void *ctx)
{
    (void)ctx;
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOD, &g);

    s_uart.Instance = USART3;
    s_uart.Init.BaudRate = SERIAL_BAUD;
    s_uart.Init.WordLength = UART_WORDLENGTH_8B;
    s_uart.Init.StopBits = UART_STOPBITS_1;
    s_uart.Init.Parity = UART_PARITY_NONE;
    s_uart.Init.Mode = UART_MODE_TX_RX;
    s_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    s_uart.Init.OverSampling = UART_OVERSAMPLING_16;
    s_uart.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    s_uart.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    s_uart.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    s_open = (HAL_UART_Init(&s_uart) == HAL_OK);
    return s_open;
}

static bool serial_close(void *ctx)
{
    (void)ctx;
    s_open = false;
    return HAL_UART_DeInit(&s_uart) == HAL_OK;
}

static size_t serial_write(void *ctx, const uint8_t *buf, size_t len)
{
    (void)ctx;
    if (!s_open || len > 0xFFFFu) {
        return 0;
    }
    return (HAL_UART_Transmit(&s_uart, buf, (uint16_t)len, SERIAL_TX_TIMEOUT_MS) == HAL_OK) ? len : 0u;
}

static size_t serial_read(void *ctx, uint8_t *buf, size_t len, uint32_t timeout_ms)
{
    (void)ctx;
    if (!s_open) {
        return 0;
    }
    size_t n = 0;
    const uint32_t start = HAL_GetTick();
    while (n < len) {
        if (__HAL_UART_GET_FLAG(&s_uart, UART_FLAG_ORE)) {
            __HAL_UART_CLEAR_FLAG(&s_uart, UART_CLEAR_OREF);
        }
        if (__HAL_UART_GET_FLAG(&s_uart, UART_FLAG_RXNE)) {
            buf[n++] = (uint8_t)(s_uart.Instance->RDR & 0xFFu);
        } else if (HAL_GetTick() - start >= timeout_ms) {
            break;
        }
    }
    return n;
}

oamr_transport_t port_serial_transport(void)
{
    oamr_transport_t t = {serial_open, serial_close, serial_write, serial_read, NULL};
    return t;
}
