/* SPDX-License-Identifier: MIT */
/*
 * Serial transport of the micro-ROS client: USART3 (PD8 TX, PD9 RX, 115200 8N1,
 * the ST-LINK virtual COM port of the NUCLEO-H723ZG), set up by tlm_console.c.
 *
 * SIMULATION AND BENCH ONLY. In this build USART3 carries the XRCE stream, so
 * the JSON console is not printed (it stays in every build without micro-ROS).
 * The robot target is UDP over Ethernet (tlm_uros_transport.h).
 *
 * Transmit: the console's blocking HAL write, bounded by its timeout.
 * Receive: the USART3 receive interrupt stores each byte in a ring of
 * TLM_UROS_RX_RING bytes; read() drains it and waits in 1 ms task delays.
 * A full ring drops the byte and counts it; XRCE framing resynchronises.
 */
#include "main.h"

#include "FreeRTOS.h"
#include "task.h"

#include "tlm.h"
#include "tlm_uros_transport.h"

#define TLM_UROS_RX_RING 1024u /* power of two; one slot stays free */

static uint8_t s_ring[TLM_UROS_RX_RING];
static volatile uint32_t s_head; /* interrupt */
static volatile uint32_t s_tail; /* task */
static volatile uint32_t s_dropped;
static volatile uint32_t s_errors;

void USART3_IRQHandler(void);

void USART3_IRQHandler(void)
{
    const uint32_t isr = USART3->ISR;
    if ((isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) != 0u)
    {
        USART3->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
        s_errors++;
    }
    while ((USART3->ISR & USART_ISR_RXNE_RXFNE) != 0u)
    {
        const uint8_t byte = (uint8_t)USART3->RDR;
        const uint32_t next = (s_head + 1u) & (TLM_UROS_RX_RING - 1u);
        if (next == s_tail)
        {
            s_dropped++;
        }
        else
        {
            s_ring[s_head] = byte;
            s_head = next;
        }
    }
}

static bool serial_open(void)
{
    s_tail = s_head;
    USART3->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
    USART3->CR1 |= USART_CR1_RXNEIE_RXFNEIE;
    HAL_NVIC_SetPriority(USART3_IRQn, 5u, 0u); /* as the generated peripheral interrupts */
    HAL_NVIC_EnableIRQ(USART3_IRQn);
    return true;
}

static bool serial_close(void)
{
    return true; /* reception stays armed: the next open() drops what is buffered */
}

static size_t serial_write(const uint8_t *buf, size_t len, uint8_t *err)
{
    if (!tlm_console_write((const char *)buf, len))
    {
        *err = 1u;
        return 0u;
    }
    return len;
}

static size_t serial_read(uint8_t *buf, size_t len, int timeout_ms, uint8_t *err)
{
    (void)err;
    TickType_t waited = 0u;
    for (;;)
    {
        size_t n = 0u;
        while (n < len && s_tail != s_head)
        {
            buf[n++] = s_ring[s_tail];
            s_tail = (s_tail + 1u) & (TLM_UROS_RX_RING - 1u);
        }
        if (n > 0u || timeout_ms <= 0 || waited >= pdMS_TO_TICKS((uint32_t)timeout_ms))
        {
            return n;
        }
        vTaskDelay(1u);
        waited++;
    }
}

static const tlm_uros_transport_t s_serial = {
    .name = "serial_usart3",
    .framing = true,
    .open = serial_open,
    .close = serial_close,
    .write = serial_write,
    .read = serial_read,
};

const tlm_uros_transport_t *tlm_uros_transport(void)
{
    return &s_serial;
}
