/* SPDX-License-Identifier: MIT */
/*
 * Size probe, NOT FIRMWARE: byte-stream transport (what USART3 on the ST-LINK
 * virtual COM port would use). A real port feeds a DMA ring buffer; here a
 * static ring stands in so that the code size is representative.
 */
#include "probe_transport.h"

#define RING 2048u
static uint8_t s_rx[RING];
static volatile uint32_t s_head;
static uint32_t s_tail;
volatile uint32_t g_probe_tx_bytes;

bool probe_transport_open(struct uxrCustomTransport *t) { (void)t; s_tail = s_head; return true; }
bool probe_transport_close(struct uxrCustomTransport *t) { (void)t; return true; }

size_t probe_transport_write(struct uxrCustomTransport *t, const uint8_t *buf, size_t len, uint8_t *err)
{
    (void)t; (void)err;
    for (size_t i = 0; i < len; i++) {
        g_probe_tx_bytes += buf[i];
    }
    return len;
}

size_t probe_transport_read(struct uxrCustomTransport *t, uint8_t *buf, size_t len, int timeout, uint8_t *err)
{
    (void)t; (void)timeout; (void)err;
    size_t n = 0;
    while (n < len && s_tail != s_head) {
        buf[n++] = s_rx[s_tail % RING];
        s_tail++;
    }
    return n;
}

void probe_set_allocator(void) {}
void probe_idle(void) {}
