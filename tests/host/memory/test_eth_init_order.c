/* SPDX-License-Identifier: MIT */
/*
 * Model the vendor HAL initialization path with a fake MSP callback. This
 * checks event ordering only; it is not STM32 hardware or descriptor evidence.
 */
#include "test_harness.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum fake_eth_event {
    FAKE_ETH_MSP_ENTRY,
    FAKE_ETH_SRAM1_ENABLED,
    FAKE_ETH_MAC_ENABLED,
    FAKE_ETH_TX_DESCRIPTOR_WRITE,
    FAKE_ETH_RX_DESCRIPTOR_WRITE
} fake_eth_event;

typedef struct fake_eth_trace {
    fake_eth_event events[8];
    size_t count;
    bool sram1_first;
} fake_eth_trace;

typedef void (*fake_eth_msp_callback)(fake_eth_trace *trace);

static void record(fake_eth_trace *trace, fake_eth_event event)
{
    CHECK(trace->count < sizeof(trace->events) / sizeof(trace->events[0]));
    if (trace->count < sizeof(trace->events) / sizeof(trace->events[0])) {
        trace->events[trace->count] = event;
        trace->count++;
    }
}

static void fake_msp_init(fake_eth_trace *trace)
{
    record(trace, FAKE_ETH_MSP_ENTRY);
    /* Models __HAL_RCC_D2SRAM1_CLK_ENABLE() in ETH_MspInit user code. */
    record(trace, FAKE_ETH_SRAM1_ENABLED);
    record(trace, FAKE_ETH_MAC_ENABLED);
}

static void fake_hal_eth_init(fake_eth_trace *trace, fake_eth_msp_callback msp_init)
{
    msp_init(trace);
    /* Models the subsequent vendor ETH_DMATx/RxDescListInit writes. */
    record(trace, FAKE_ETH_TX_DESCRIPTOR_WRITE);
    record(trace, FAKE_ETH_RX_DESCRIPTOR_WRITE);
}

static bool sram1_precedes_writes(const fake_eth_trace *trace)
{
    size_t enable = trace->count;
    size_t first_write = trace->count;

    for (size_t index = 0u; index < trace->count; ++index) {
        if (trace->events[index] == FAKE_ETH_SRAM1_ENABLED && enable == trace->count) {
            enable = index;
        }
        if ((trace->events[index] == FAKE_ETH_TX_DESCRIPTOR_WRITE
             || trace->events[index] == FAKE_ETH_RX_DESCRIPTOR_WRITE)
            && first_write == trace->count) {
            first_write = index;
        }
    }
    return enable < first_write && first_write < trace->count;
}

static void msp_enables_sram1_before_descriptor_writes(void)
{
    fake_eth_trace trace = {0};

    fake_hal_eth_init(&trace, fake_msp_init);
    CHECK(trace.count == 5u);
    CHECK(trace.events[0] == FAKE_ETH_MSP_ENTRY);
    CHECK(trace.events[1] == FAKE_ETH_SRAM1_ENABLED);
    CHECK(trace.events[3] == FAKE_ETH_TX_DESCRIPTOR_WRITE);
    CHECK(trace.events[4] == FAKE_ETH_RX_DESCRIPTOR_WRITE);
    CHECK(sram1_precedes_writes(&trace));
}

static void late_sram1_enable_is_rejected(void)
{
    fake_eth_trace trace = {0};

    record(&trace, FAKE_ETH_MSP_ENTRY);
    record(&trace, FAKE_ETH_MAC_ENABLED);
    record(&trace, FAKE_ETH_TX_DESCRIPTOR_WRITE);
    record(&trace, FAKE_ETH_RX_DESCRIPTOR_WRITE);
    record(&trace, FAKE_ETH_SRAM1_ENABLED);
    CHECK(!sram1_precedes_writes(&trace));
}

static void missing_sram1_enable_is_rejected(void)
{
    fake_eth_trace trace = {0};

    record(&trace, FAKE_ETH_MSP_ENTRY);
    record(&trace, FAKE_ETH_TX_DESCRIPTOR_WRITE);
    record(&trace, FAKE_ETH_RX_DESCRIPTOR_WRITE);
    CHECK(!sram1_precedes_writes(&trace));
}

static void missing_descriptor_writes_are_rejected(void)
{
    fake_eth_trace trace = {0};

    record(&trace, FAKE_ETH_MSP_ENTRY);
    record(&trace, FAKE_ETH_SRAM1_ENABLED);
    record(&trace, FAKE_ETH_MAC_ENABLED);
    CHECK(!sram1_precedes_writes(&trace));
}

int main(void)
{
    RUN(msp_enables_sram1_before_descriptor_writes);
    RUN(late_sram1_enable_is_rejected);
    RUN(missing_sram1_enable_is_rejected);
    RUN(missing_descriptor_writes_are_rejected);
    return TEST_END();
}
