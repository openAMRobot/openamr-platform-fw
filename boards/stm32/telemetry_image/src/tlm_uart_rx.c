/* SPDX-License-Identifier: MIT */
#include "tlm_uart_rx.h"

#include <string.h>

void tlm_ring_init(tlm_ring_t *r)
{
    memset(r, 0, sizeof(*r));
}

void tlm_ring_put(tlm_ring_t *r, uint8_t byte)
{
    if (r->overflowed)
    {
        r->lost_isr++;
        return;
    }
    const uint32_t next = (r->head + 1u) % TLM_RING_CAP;
    if (next == r->tail)
    {
        r->overflowed = true;
        r->ovf_events++;
        r->lost_isr++;
        return;
    }
    r->buf[r->head] = byte;
    r->head = next;
}

bool tlm_ring_take_overflow(tlm_ring_t *r)
{
    if (!r->overflowed)
    {
        return false;
    }
    /* The interrupt stores nothing while the mark is set, so head is stable. */
    const uint32_t head = r->head;
    r->lost_discarded += (head + TLM_RING_CAP - r->tail) % TLM_RING_CAP;
    r->tail = head;
    r->overflowed = false; /* last: the interrupt may store again from here */
    return true;
}

bool tlm_ring_get(tlm_ring_t *r, uint8_t *byte)
{
    if (r->tail == r->head)
    {
        return false;
    }
    *byte = r->buf[r->tail];
    r->tail = (r->tail + 1u) % TLM_RING_CAP;
    return true;
}

uint32_t tlm_ring_lost(const tlm_ring_t *r)
{
    return r->lost_isr + r->lost_discarded;
}

void tlm_uart_rx_init(tlm_uart_rx_t *u, uint8_t max_attempts)
{
    memset(u, 0, sizeof(*u));
    u->state = TLM_UART_RX_STOPPED;
    u->max_attempts = max_attempts == 0u ? 1u : max_attempts;
}

void tlm_uart_rx_lost(tlm_uart_rx_t *u)
{
    u->start_failures++;
    if (u->state == TLM_UART_RX_OK)
    {
        u->state = TLM_UART_RX_STOPPED;
    }
}

bool tlm_uart_rx_needs_start(const tlm_uart_rx_t *u)
{
    return u->state == TLM_UART_RX_STOPPED;
}

void tlm_uart_rx_retry_result(tlm_uart_rx_t *u, bool ok)
{
    if (u->state != TLM_UART_RX_STOPPED)
    {
        return;
    }
    if (ok)
    {
        u->state = TLM_UART_RX_OK;
        u->attempts = 0u;
        u->starts++;
        return;
    }
    u->start_failures++;
    u->attempts++;
    if (u->attempts >= u->max_attempts)
    {
        u->state = TLM_UART_RX_FAILED;
    }
}

const char *tlm_uart_rx_state_name(tlm_uart_rx_state_t s)
{
    switch (s)
    {
    case TLM_UART_RX_OK:
        return "ok";
    case TLM_UART_RX_FAILED:
        return "failed";
    case TLM_UART_RX_STOPPED:
    default:
        return "stopped";
    }
}
