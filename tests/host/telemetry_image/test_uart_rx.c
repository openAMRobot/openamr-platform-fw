/* SPDX-License-Identifier: MIT */
/* Receive ring and restart policy of the sensor UARTs (src/tlm_uart_rx.c),
 * and the deadline accounting of the CAN tick (src/tlm_sched.c). Golden
 * values: ring size TLM_UART_RING_SIZE and TLM_UART_RX_RESTART_MAX of
 * tlm_params.h; the rest is hand arithmetic (no external source). */
#include "harness.h"
#include "tlm_sched.h"
#include "tlm_uart_rx.h"

static void ring_overflow_discards_and_counts(void)
{
    tlm_ring_t r;
    tlm_ring_init(&r);
    /* Fill to capacity (one slot stays free). */
    for (unsigned i = 0; i < TLM_RING_CAP - 1u; i++)
    {
        tlm_ring_put(&r, (uint8_t)i);
    }
    CHECK(!r.overflowed && r.ovf_events == 0u);
    /* The next byte overflows: the port is marked, the byte counted lost. */
    tlm_ring_put(&r, 0xAAu);
    CHECK(r.overflowed && r.ovf_events == 1u && r.lost_isr == 1u);
    /* While marked, every byte is dropped and counted, none is stored. */
    for (unsigned i = 0; i < 100u; i++)
    {
        tlm_ring_put(&r, 0x59u);
    }
    CHECK(r.lost_isr == 101u && r.ovf_events == 1u);
    /* The task discards what was buffered: nothing of it is parsed. */
    CHECK(tlm_ring_take_overflow(&r));
    CHECK(r.lost_discarded == TLM_RING_CAP - 1u);
    CHECK(tlm_ring_lost(&r) == 101u + TLM_RING_CAP - 1u);
    uint8_t b = 0u;
    CHECK(!tlm_ring_get(&r, &b));
    CHECK(!tlm_ring_take_overflow(&r)); /* handled once */
    /* Bytes after the overflow are stored again, in order. */
    tlm_ring_put(&r, 0x59u);
    tlm_ring_put(&r, 0x60u);
    CHECK(tlm_ring_get(&r, &b) && b == 0x59u);
    CHECK(tlm_ring_get(&r, &b) && b == 0x60u);
    CHECK(!tlm_ring_get(&r, &b));
    CHECK(r.ovf_events == 1u);
}

static void ring_wraps_without_loss(void)
{
    tlm_ring_t r;
    tlm_ring_init(&r);
    uint8_t b = 0u;
    for (unsigned i = 0; i < 10u * TLM_RING_CAP; i++)
    {
        tlm_ring_put(&r, (uint8_t)i);
        CHECK(tlm_ring_get(&r, &b) && b == (uint8_t)i);
    }
    CHECK(tlm_ring_lost(&r) == 0u && r.ovf_events == 0u);
}

static void restart_is_bounded(void)
{
    tlm_uart_rx_t u;
    tlm_uart_rx_init(&u, 3u);
    CHECK(tlm_uart_rx_needs_start(&u)); /* starts stopped: the first start uses the same path */
    tlm_uart_rx_retry_result(&u, true);
    CHECK(u.state == TLM_UART_RX_OK && u.starts == 1u && !tlm_uart_rx_needs_start(&u));
    /* The interrupt could not re-arm: stopped, counted. */
    tlm_uart_rx_lost(&u);
    CHECK(u.state == TLM_UART_RX_STOPPED && u.start_failures == 1u);
    /* Two failed restarts, then one that works: the count of attempts resets. */
    tlm_uart_rx_retry_result(&u, false);
    tlm_uart_rx_retry_result(&u, false);
    CHECK(u.state == TLM_UART_RX_STOPPED && u.attempts == 2u);
    tlm_uart_rx_retry_result(&u, true);
    CHECK(u.state == TLM_UART_RX_OK && u.attempts == 0u && u.starts == 2u);
    /* Stopped again; three failed restarts in a row: FAILED, and left alone. */
    tlm_uart_rx_lost(&u);
    tlm_uart_rx_retry_result(&u, false);
    tlm_uart_rx_retry_result(&u, false);
    CHECK(u.state == TLM_UART_RX_STOPPED);
    tlm_uart_rx_retry_result(&u, false);
    CHECK(u.state == TLM_UART_RX_FAILED && !tlm_uart_rx_needs_start(&u));
    CHECK(u.start_failures == 1u + 2u + 1u + 3u);
    tlm_uart_rx_retry_result(&u, true); /* no effect once failed */
    tlm_uart_rx_lost(&u);
    CHECK(u.state == TLM_UART_RX_FAILED);
    CHECK(tlm_uart_rx_state_name(u.state)[0] == 'f');
}

static void late_tick_is_counted_without_catch_up(void)
{
    tlm_sched_stats_t st = {0};
    /* Period 20 ticks. The deadline 1000 was missed and it is now 1000: the
     * run is late by 0 ticks (it did not block), one deadline missed. */
    CHECK(tlm_sched_late(&st, 1000u, 1000u, 20u) == 1000u);
    CHECK(st.missed == 1u && st.late_events == 1u && st.late_max_ticks == 0u);
    /* 65 ticks late: deadlines 2000, 2020, 2040, 2060 passed without a run. */
    CHECK(tlm_sched_late(&st, 2000u, 2065u, 20u) == 2065u);
    CHECK(st.missed == 1u + 4u && st.late_max_ticks == 65u);
    /* The new reference is "now": the next deadline is one period later, so
     * a caller of xTaskDelayUntil() does not run the missed ticks in a burst. */
    /* Across the 32-bit tick wrap. */
    CHECK(tlm_sched_late(&st, 0xFFFFFFF0u, 0x00000010u, 20u) == 0x10u);
    CHECK(st.missed == 5u + 2u && st.late_max_ticks == 65u && st.late_events == 3u);
}

int main(void)
{
    RUN(ring_overflow_discards_and_counts);
    RUN(ring_wraps_without_loss);
    RUN(restart_is_bounded);
    RUN(late_tick_is_counted_without_catch_up);
    TEST_END();
}
