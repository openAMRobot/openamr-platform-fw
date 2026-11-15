/* SPDX-License-Identifier: MIT */
/*
 * Receive side of one sensor UART, as pure C (no HAL, no RTOS) so the host
 * tests can drive it: a byte ring filled by the interrupt and drained by the
 * sensors task, and the restart policy of the reception.
 *
 * Ring overflow: when the interrupt finds the ring full, it marks the port
 * overflowed, counts the event and drops every further byte (counted) until
 * the task has handled it. The task then discards everything still buffered
 * (counted), resets the port's parser and clears the mark; the parser
 * resynchronises on the next frame header. A frame cut by the overflow is
 * never glued to bytes received after it.
 *
 * Restart policy: every call of HAL_UART_Receive_IT is checked. A call that
 * does not return HAL_OK stops reception of the port (counted). The task
 * then restarts it; after TLM_UART_RX_RESTART_MAX failed attempts in a row
 * the port is FAILED, reported as such and left alone until the next reset.
 * A port starts STOPPED, so the first start goes through the same path.
 *
 * Concurrency contract: tlm_ring_put() and tlm_uart_rx_lost() run in the
 * UART interrupt; the other functions run in the sensors task, which calls
 * tlm_uart_rx_retry_result() with that interrupt masked.
 */
#ifndef TLM_UART_RX_H
#define TLM_UART_RX_H

#include <stdbool.h>
#include <stdint.h>

#include "tlm_params.h"

/* Bytes; one slot stays free to tell full from empty. */
#define TLM_RING_CAP TLM_UART_RING_SIZE

typedef struct
{
    uint8_t buf[TLM_RING_CAP];
    volatile uint32_t head;        /* written by the interrupt */
    volatile uint32_t tail;        /* written by the task */
    volatile bool overflowed;      /* set by the interrupt, cleared by the task */
    volatile uint32_t ovf_events;  /* overflows (one per full ring) */
    volatile uint32_t lost_isr;    /* bytes dropped by the interrupt while overflowed */
    uint32_t lost_discarded;       /* buffered bytes the task discarded */
} tlm_ring_t;

void tlm_ring_init(tlm_ring_t *r);
/* Interrupt: store one byte, or drop and count it. */
void tlm_ring_put(tlm_ring_t *r, uint8_t byte);
/* Task: if the ring overflowed, discard what is buffered, clear the mark and
 * return true (the caller resets the parser). */
bool tlm_ring_take_overflow(tlm_ring_t *r);
/* Task: next byte, false when empty. */
bool tlm_ring_get(tlm_ring_t *r, uint8_t *byte);
/* Bytes lost to overflows in total. */
uint32_t tlm_ring_lost(const tlm_ring_t *r);

typedef enum
{
    TLM_UART_RX_STOPPED = 0, /* not receiving; the task restarts it */
    TLM_UART_RX_OK = 1,      /* reception armed */
    TLM_UART_RX_FAILED = 2   /* restart attempts used up; reported, left alone */
} tlm_uart_rx_state_t;

typedef struct
{
    volatile tlm_uart_rx_state_t state;
    uint8_t max_attempts;            /* failed restarts in a row before FAILED, >= 1 */
    uint8_t attempts;                /* failed restarts in a row so far */
    volatile uint32_t start_failures; /* calls that did not return HAL_OK */
    uint32_t starts;                 /* successful starts, the first one included */
} tlm_uart_rx_t;

void tlm_uart_rx_init(tlm_uart_rx_t *u, uint8_t max_attempts);
/* Interrupt: re-arming after a byte or an error failed. */
void tlm_uart_rx_lost(tlm_uart_rx_t *u);
/* Task: true when the port needs a (re)start call now. */
bool tlm_uart_rx_needs_start(const tlm_uart_rx_t *u);
/* Task: result of that call. */
void tlm_uart_rx_retry_result(tlm_uart_rx_t *u, bool ok);
const char *tlm_uart_rx_state_name(tlm_uart_rx_state_t s);

#endif /* TLM_UART_RX_H */
