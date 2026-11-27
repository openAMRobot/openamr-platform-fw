/* SPDX-License-Identifier: MIT */
#include "test_harness.h"

#include "fdcan_adapter.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TEST_BUS_COUNT ((size_t)FDCAN_ADAPTER_BUS_COUNT)
#define TEST_RX_INJECTION_CAPACITY 32u
#define TEST_CALL_CAPACITY 256u
#define TEST_NOTIFICATION_MASK                                                 \
    (FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_RX_FIFO0_FULL |                  \
     FDCAN_IT_RX_FIFO0_MESSAGE_LOST | FDCAN_IT_ERROR_WARNING |                 \
     FDCAN_IT_ERROR_PASSIVE | FDCAN_IT_BUS_OFF |                               \
     FDCAN_IT_ERROR_LOGGING_OVERFLOW | FDCAN_IT_RAM_ACCESS_FAILURE |           \
     FDCAN_IT_RAM_WATCHDOG | FDCAN_IT_ARB_PROTOCOL_ERROR |                     \
     FDCAN_IT_DATA_PROTOCOL_ERROR | FDCAN_IT_RESERVED_ADDRESS_ACCESS)
#define TEST_TX_MASK (FDCAN_IT_TX_COMPLETE | FDCAN_IT_TX_ABORT_COMPLETE)

typedef enum {
    CALL_FILTER,
    CALL_GLOBAL,
    CALL_LINES,
    CALL_START,
    CALL_NOTIFY,
    CALL_STOP,
    CALL_TX
} call_type_t;

typedef struct {
    FDCAN_RxHeaderTypeDef header;
    uint8_t data[8];
} injected_frame_t;

typedef struct {
    call_type_t type;
    fdcan_adapter_bus_t bus;
    FDCAN_FilterTypeDef filter;
    uint32_t argument[4];
} recorded_call_t;

typedef struct {
    FDCAN_FilterTypeDef filter;
    uint32_t non_matching_std;
    uint32_t non_matching_ext;
    uint32_t reject_remote_std;
    uint32_t reject_remote_ext;
    uint32_t interrupt_lines;
    uint32_t interrupt_line;
    uint32_t notification_calls;
    uint32_t start_calls;
    uint32_t stop_calls;
    uint32_t tx_calls;
    uint32_t get_rx_calls;
    HAL_StatusTypeDef filter_status;
    HAL_StatusTypeDef global_status;
    HAL_StatusTypeDef lines_status;
    HAL_StatusTypeDef start_status;
    HAL_StatusTypeDef notify_status[2];
    HAL_StatusTypeDef stop_status;
    HAL_StatusTypeDef get_rx_status;
    HAL_StatusTypeDef tx_status;
    FDCAN_ProtocolStatusTypeDef protocol;
    bool hold_bus_off;
    FDCAN_ErrorCountersTypeDef counters;
    injected_frame_t rx[TEST_RX_INJECTION_CAPACITY];
    size_t rx_count;
    size_t rx_head;
    FDCAN_TxHeaderTypeDef tx_header;
    uint8_t tx_data[8];
} fake_bus_t;

FDCAN_HandleTypeDef hfdcan1;
FDCAN_HandleTypeDef hfdcan2;
FDCAN_HandleTypeDef hfdcan3;
uint32_t fdcan_test_primask;

static FDCAN_HandleTypeDef *const handles[TEST_BUS_COUNT] = {
    &hfdcan1, &hfdcan2, &hfdcan3
};
static FDCAN_GlobalTypeDef *const instances[TEST_BUS_COUNT] = {
    FDCAN1, FDCAN2, FDCAN3
};
static fake_bus_t fake_buses[TEST_BUS_COUNT];
static recorded_call_t calls[TEST_CALL_CAPACITY];
static size_t call_count;
static uint32_t fake_now_ms;

static fdcan_adapter_bus_t bus_from_handle(const FDCAN_HandleTypeDef *handle)
{
    size_t index;

    for (index = 0u; index < TEST_BUS_COUNT; ++index) {
        if (handle == handles[index]) {
            return (fdcan_adapter_bus_t)index;
        }
    }
    return FDCAN_ADAPTER_BUS_COUNT;
}

static void record_call(call_type_t type, fdcan_adapter_bus_t bus)
{
    recorded_call_t *call;

    CHECK(call_count < TEST_CALL_CAPACITY);
    CHECK(bus < FDCAN_ADAPTER_BUS_COUNT);
    if ((call_count >= TEST_CALL_CAPACITY) || (bus >= FDCAN_ADAPTER_BUS_COUNT)) {
        return;
    }
    call = &calls[call_count];
    memset(call, 0, sizeof(*call));
    call->type = type;
    call->bus = bus;
    ++call_count;
}

static void reset_handles_and_fakes(void)
{
    size_t index;

    memset(fake_buses, 0, sizeof(fake_buses));
    memset(calls, 0, sizeof(calls));
    call_count = 0u;
    fake_now_ms = 0u;
    fdcan_test_primask = 0u;

    for (index = 0u; index < TEST_BUS_COUNT; ++index) {
        fake_bus_t *fake = &fake_buses[index];

        memset(handles[index], 0, sizeof(*handles[index]));
        handles[index]->Instance = instances[index];
        fake->filter_status = HAL_OK;
        fake->global_status = HAL_OK;
        fake->lines_status = HAL_OK;
        fake->start_status = HAL_OK;
        fake->notify_status[0] = HAL_OK;
        fake->notify_status[1] = HAL_OK;
        fake->stop_status = HAL_OK;
        fake->get_rx_status = HAL_OK;
        fake->tx_status = HAL_OK;
    }
}

static void initialize_adapter(void)
{
    reset_handles_and_fakes();
    /* Corrected generated-layout host fixture: offsets are 32-bit words. */
    hfdcan1.Init.MessageRAMOffset = 0u;
    hfdcan2.Init.MessageRAMOffset = 512u;
    hfdcan3.Init.MessageRAMOffset = 1024u;
    hfdcan1.Init.StdFiltersNbr = 1u;
    hfdcan1.Init.ExtFiltersNbr = 0u;
    hfdcan2.Init.StdFiltersNbr = 0u;
    hfdcan2.Init.ExtFiltersNbr = 1u;
    hfdcan3.Init.StdFiltersNbr = 1u;
    hfdcan3.Init.ExtFiltersNbr = 0u;
    hfdcan1.Init.RxFifo0ElmtsNbr = 16u;
    hfdcan2.Init.RxFifo0ElmtsNbr = 16u;
    hfdcan3.Init.RxFifo0ElmtsNbr = 16u;
    hfdcan1.Init.RxFifo0ElmtSize = FDCAN_DATA_BYTES_8;
    hfdcan2.Init.RxFifo0ElmtSize = FDCAN_DATA_BYTES_8;
    hfdcan3.Init.RxFifo0ElmtSize = FDCAN_DATA_BYTES_8;
    hfdcan1.Init.TxFifoQueueElmtsNbr = 0u;
    hfdcan2.Init.TxFifoQueueElmtsNbr = 8u;
    hfdcan3.Init.TxFifoQueueElmtsNbr = 0u;
    hfdcan1.Init.TxElmtSize = FDCAN_DATA_BYTES_8;
    hfdcan2.Init.TxElmtSize = FDCAN_DATA_BYTES_8;
    hfdcan3.Init.TxElmtSize = FDCAN_DATA_BYTES_8;
    hfdcan1.Init.Mode = FDCAN_MODE_BUS_MONITORING;
    hfdcan2.Init.Mode = FDCAN_MODE_NORMAL;
    hfdcan3.Init.Mode = FDCAN_MODE_BUS_MONITORING;
    hfdcan1.Init.NominalPrescaler = 6u;
    hfdcan2.Init.NominalPrescaler = 12u;
    hfdcan3.Init.NominalPrescaler = 6u;
    hfdcan1.Init.NominalSyncJumpWidth = 1u;
    hfdcan2.Init.NominalSyncJumpWidth = 1u;
    hfdcan3.Init.NominalSyncJumpWidth = 1u;
    hfdcan1.Init.NominalTimeSeg1 = 13u;
    hfdcan2.Init.NominalTimeSeg1 = 13u;
    hfdcan3.Init.NominalTimeSeg1 = 13u;
    hfdcan1.Init.NominalTimeSeg2 = 2u;
    hfdcan2.Init.NominalTimeSeg2 = 2u;
    hfdcan3.Init.NominalTimeSeg2 = 2u;
    CHECK(fdcan_adapter_init());
}

static void get_diagnostics(fdcan_adapter_bus_t bus,
                            fdcan_adapter_diagnostics_t *diagnostics)
{
    memset(diagnostics, 0xa5, sizeof(*diagnostics));
    CHECK(fdcan_adapter_diagnostics(bus, diagnostics));
}

static void expect_empty(fdcan_adapter_bus_t bus)
{
    oamr_can_frame_t frame;
    oamr_can_frame_t untouched;

    memset(&untouched, 0x5a, sizeof(untouched));
    frame = untouched;
    CHECK(fdcan_adapter_receive(bus, &frame) == OAMR_STATUS_EMPTY);
    CHECK(memcmp(&frame, &untouched, sizeof(frame)) == 0);
}

static void set_standard_header(FDCAN_RxHeaderTypeDef *header, uint32_t id,
                                uint32_t dlc)
{
    memset(header, 0, sizeof(*header));
    header->Identifier = id;
    header->IdType = FDCAN_STANDARD_ID;
    header->RxFrameType = FDCAN_DATA_FRAME;
    header->DataLength = dlc;
    header->FDFormat = FDCAN_CLASSIC_CAN;
}

static void set_daly_header(FDCAN_RxHeaderTypeDef *header, uint32_t dlc)
{
    set_standard_header(header, FDCAN_ADAPTER_DALY_RX_ID, dlc);
    header->IdType = FDCAN_EXTENDED_ID;
}

static void inject_frame(fdcan_adapter_bus_t bus,
                         const FDCAN_RxHeaderTypeDef *header,
                         const uint8_t data[8])
{
    fake_bus_t *fake = &fake_buses[bus];
    injected_frame_t *frame;

    CHECK(fake->rx_count < TEST_RX_INJECTION_CAPACITY);
    if (fake->rx_count >= TEST_RX_INJECTION_CAPACITY) {
        return;
    }
    frame = &fake->rx[fake->rx_count];
    frame->header = *header;
    memcpy(frame->data, data, sizeof(frame->data));
    ++fake->rx_count;
}

static void inject_standard(fdcan_adapter_bus_t bus, uint32_t id, uint32_t dlc,
                            uint8_t marker)
{
    FDCAN_RxHeaderTypeDef header;
    uint8_t data[8];
    size_t index;

    set_standard_header(&header, id, dlc);
    for (index = 0u; index < sizeof(data); ++index) {
        data[index] = (uint8_t)(marker + (uint8_t)index);
    }
    inject_frame(bus, &header, data);
}

static void inject_daly(uint32_t dlc, uint8_t marker)
{
    FDCAN_RxHeaderTypeDef header;
    uint8_t data[8];
    size_t index;

    set_daly_header(&header, dlc);
    for (index = 0u; index < sizeof(data); ++index) {
        data[index] = (uint8_t)(marker + (uint8_t)index);
    }
    inject_frame(FDCAN_ADAPTER_FDCAN2, &header, data);
}

static void expect_received_id(fdcan_adapter_bus_t bus, uint32_t id,
                               oamr_can_id_type_t id_type, uint8_t dlc)
{
    oamr_can_frame_t frame;

    memset(&frame, 0, sizeof(frame));
    CHECK(fdcan_adapter_receive(bus, &frame) == OAMR_STATUS_OK);
    CHECK(frame.id == id);
    CHECK(frame.id_type == id_type);
    CHECK(frame.dlc == dlc);
}

static uint32_t ram_layout_words(fdcan_adapter_bus_t bus)
{
    const FDCAN_HandleTypeDef *handle = handles[bus];
    const uint32_t element_words = 4u;

    return handle->Init.StdFiltersNbr + (2u * handle->Init.ExtFiltersNbr) +
           (handle->Init.RxFifo0ElmtsNbr * element_words) +
           (handle->Init.TxFifoQueueElmtsNbr * element_words);
}

uint32_t HAL_GetTick(void)
{
    return fake_now_ms;
}

HAL_StatusTypeDef HAL_FDCAN_ConfigFilter(FDCAN_HandleTypeDef *hfdcan,
                                         const FDCAN_FilterTypeDef *filter)
{
    const fdcan_adapter_bus_t bus = bus_from_handle(hfdcan);
    recorded_call_t *call;

    record_call(CALL_FILTER, bus);
    if (bus >= FDCAN_ADAPTER_BUS_COUNT) {
        return HAL_ERROR;
    }
    fake_buses[bus].filter = *filter;
    call = &calls[call_count - 1u];
    call->filter = *filter;
    return fake_buses[bus].filter_status;
}

HAL_StatusTypeDef HAL_FDCAN_ConfigGlobalFilter(
    FDCAN_HandleTypeDef *hfdcan, uint32_t non_matching_std,
    uint32_t non_matching_ext, uint32_t reject_remote_std,
    uint32_t reject_remote_ext)
{
    const fdcan_adapter_bus_t bus = bus_from_handle(hfdcan);
    recorded_call_t *call;
    fake_bus_t *fake;

    record_call(CALL_GLOBAL, bus);
    if (bus >= FDCAN_ADAPTER_BUS_COUNT) {
        return HAL_ERROR;
    }
    fake = &fake_buses[bus];
    fake->non_matching_std = non_matching_std;
    fake->non_matching_ext = non_matching_ext;
    fake->reject_remote_std = reject_remote_std;
    fake->reject_remote_ext = reject_remote_ext;
    call = &calls[call_count - 1u];
    call->argument[0] = non_matching_std;
    call->argument[1] = non_matching_ext;
    call->argument[2] = reject_remote_std;
    call->argument[3] = reject_remote_ext;
    return fake->global_status;
}

HAL_StatusTypeDef HAL_FDCAN_ConfigInterruptLines(FDCAN_HandleTypeDef *hfdcan,
                                                 uint32_t interrupts,
                                                 uint32_t line)
{
    const fdcan_adapter_bus_t bus = bus_from_handle(hfdcan);
    recorded_call_t *call;
    fake_bus_t *fake;

    record_call(CALL_LINES, bus);
    if (bus >= FDCAN_ADAPTER_BUS_COUNT) {
        return HAL_ERROR;
    }
    fake = &fake_buses[bus];
    fake->interrupt_lines = interrupts;
    fake->interrupt_line = line;
    call = &calls[call_count - 1u];
    call->argument[0] = interrupts;
    call->argument[1] = line;
    return fake->lines_status;
}

HAL_StatusTypeDef HAL_FDCAN_Start(FDCAN_HandleTypeDef *hfdcan)
{
    const fdcan_adapter_bus_t bus = bus_from_handle(hfdcan);

    record_call(CALL_START, bus);
    if (bus >= FDCAN_ADAPTER_BUS_COUNT) {
        return HAL_ERROR;
    }
    ++fake_buses[bus].start_calls;
    if (!fake_buses[bus].hold_bus_off)
    {
        fake_buses[bus].protocol.BusOff = 0u;
    }
    return fake_buses[bus].start_status;
}

HAL_StatusTypeDef HAL_FDCAN_Stop(FDCAN_HandleTypeDef *hfdcan)
{
    const fdcan_adapter_bus_t bus = bus_from_handle(hfdcan);

    record_call(CALL_STOP, bus);
    if (bus >= FDCAN_ADAPTER_BUS_COUNT) {
        return HAL_ERROR;
    }
    ++fake_buses[bus].stop_calls;
    return fake_buses[bus].stop_status;
}

HAL_StatusTypeDef HAL_FDCAN_ActivateNotification(FDCAN_HandleTypeDef *hfdcan,
                                                 uint32_t interrupts,
                                                 uint32_t buffer_indexes)
{
    const fdcan_adapter_bus_t bus = bus_from_handle(hfdcan);
    recorded_call_t *call;
    fake_bus_t *fake;
    size_t notification_index;

    record_call(CALL_NOTIFY, bus);
    if (bus >= FDCAN_ADAPTER_BUS_COUNT) {
        return HAL_ERROR;
    }
    fake = &fake_buses[bus];
    notification_index = fake->notification_calls;
    ++fake->notification_calls;
    call = &calls[call_count - 1u];
    call->argument[0] = interrupts;
    call->argument[1] = buffer_indexes;
    if (notification_index >= 2u) {
        return HAL_OK;
    }
    return fake->notify_status[notification_index];
}

HAL_StatusTypeDef HAL_FDCAN_GetProtocolStatus(
    const FDCAN_HandleTypeDef *hfdcan, FDCAN_ProtocolStatusTypeDef *status)
{
    const fdcan_adapter_bus_t bus = bus_from_handle(hfdcan);

    if ((bus >= FDCAN_ADAPTER_BUS_COUNT) || (status == NULL)) {
        return HAL_ERROR;
    }
    *status = fake_buses[bus].protocol;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_FDCAN_GetErrorCounters(
    const FDCAN_HandleTypeDef *hfdcan, FDCAN_ErrorCountersTypeDef *counters)
{
    const fdcan_adapter_bus_t bus = bus_from_handle(hfdcan);

    if ((bus >= FDCAN_ADAPTER_BUS_COUNT) || (counters == NULL)) {
        return HAL_ERROR;
    }
    *counters = fake_buses[bus].counters;
    return HAL_OK;
}

uint32_t HAL_FDCAN_GetRxFifoFillLevel(const FDCAN_HandleTypeDef *hfdcan,
                                      uint32_t rx_fifo)
{
    const fdcan_adapter_bus_t bus = bus_from_handle(hfdcan);

    if ((bus >= FDCAN_ADAPTER_BUS_COUNT) || (rx_fifo != FDCAN_RX_FIFO0)) {
        return 0u;
    }
    return (uint32_t)(fake_buses[bus].rx_count - fake_buses[bus].rx_head);
}

HAL_StatusTypeDef HAL_FDCAN_GetRxMessage(FDCAN_HandleTypeDef *hfdcan,
                                         uint32_t rx_location,
                                         FDCAN_RxHeaderTypeDef *header,
                                         uint8_t *data)
{
    const fdcan_adapter_bus_t bus = bus_from_handle(hfdcan);
    fake_bus_t *fake;
    const injected_frame_t *frame;

    if ((bus >= FDCAN_ADAPTER_BUS_COUNT) || (header == NULL) || (data == NULL)) {
        return HAL_ERROR;
    }
    fake = &fake_buses[bus];
    ++fake->get_rx_calls;
    if ((rx_location != FDCAN_RX_FIFO0) || (fake->rx_head >= fake->rx_count)) {
        return HAL_ERROR;
    }
    if (fake->get_rx_status != HAL_OK) {
        hfdcan->ErrorCode = HAL_FDCAN_ERROR_FIFO_FULL;
        return fake->get_rx_status;
    }
    frame = &fake->rx[fake->rx_head];
    ++fake->rx_head;
    *header = frame->header;
    memcpy(data, frame->data, sizeof(frame->data));
    return HAL_OK;
}

HAL_StatusTypeDef HAL_FDCAN_AddMessageToTxFifoQ(
    FDCAN_HandleTypeDef *hfdcan, const FDCAN_TxHeaderTypeDef *header,
    const uint8_t *data)
{
    const fdcan_adapter_bus_t bus = bus_from_handle(hfdcan);
    recorded_call_t *call;
    fake_bus_t *fake;

    record_call(CALL_TX, bus);
    if ((bus >= FDCAN_ADAPTER_BUS_COUNT) || (header == NULL) || (data == NULL)) {
        return HAL_ERROR;
    }
    fake = &fake_buses[bus];
    ++fake->tx_calls;
    fake->tx_header = *header;
    memcpy(fake->tx_data, data, sizeof(fake->tx_data));
    call = &calls[call_count - 1u];
    call->argument[0] = header->Identifier;
    call->argument[1] = header->DataLength;
    return fake->tx_status;
}

static void common_initialization_order_filters_and_notifications(void)
{
    static const call_type_t monitor_order[] = {
        CALL_FILTER, CALL_GLOBAL, CALL_LINES, CALL_START, CALL_NOTIFY
    };
    static const call_type_t daly_order[] = {
        CALL_FILTER, CALL_GLOBAL, CALL_LINES, CALL_START, CALL_NOTIFY, CALL_NOTIFY
    };
    fdcan_adapter_diagnostics_t diagnostics;
    size_t bus_index;
    size_t call_index = 0u;

    initialize_adapter();
    for (bus_index = 0u; bus_index < TEST_BUS_COUNT; ++bus_index) {
        const fake_bus_t *fake = &fake_buses[bus_index];
        const bool is_daly = bus_index == (size_t)FDCAN_ADAPTER_FDCAN2;
        const call_type_t *order = is_daly ? daly_order : monitor_order;
        const size_t order_count = is_daly ?
            sizeof(daly_order) / sizeof(daly_order[0]) :
            sizeof(monitor_order) / sizeof(monitor_order[0]);
        size_t order_index;

        for (order_index = 0u; order_index < order_count; ++order_index) {
            CHECK(calls[call_index].bus == (fdcan_adapter_bus_t)bus_index);
            CHECK(calls[call_index].type == order[order_index]);
            ++call_index;
        }
        CHECK(fake->filter.FilterIndex == 0u);
        CHECK(fake->filter.FilterConfig == FDCAN_FILTER_TO_RXFIFO0);
        CHECK(fake->non_matching_std == FDCAN_REJECT);
        CHECK(fake->non_matching_ext == FDCAN_REJECT);
        CHECK(fake->reject_remote_std == FDCAN_REJECT_REMOTE);
        CHECK(fake->reject_remote_ext == FDCAN_REJECT_REMOTE);
        CHECK(fake->interrupt_line == FDCAN_INTERRUPT_LINE0);
        CHECK(fake->interrupt_lines ==
              (TEST_NOTIFICATION_MASK | (is_daly ? TEST_TX_MASK : 0u)));
        CHECK(fake->notification_calls == (is_daly ? 2u : 1u));
        CHECK(fake->start_calls == 1u);
        CHECK(fake->stop_calls == 0u);
        CHECK(fake->tx_calls == 0u);
        get_diagnostics((fdcan_adapter_bus_t)bus_index, &diagnostics);
        CHECK(diagnostics.started);
        CHECK(diagnostics.start_errors == 0u);
    }
    CHECK(call_index == 16u);

    CHECK(calls[4].argument[0] == TEST_NOTIFICATION_MASK);
    CHECK(calls[4].argument[1] == 0u);
    CHECK(calls[9].argument[0] == TEST_NOTIFICATION_MASK);
    CHECK(calls[9].argument[1] == 0u);
    CHECK(calls[10].argument[0] == TEST_TX_MASK);
    CHECK(calls[10].argument[1] == UINT32_C(0xff));
}

static void fdcan2_filter_is_exact_extended_mask_to_fifo0(void)
{
    const fake_bus_t *fake;

    initialize_adapter();
    fake = &fake_buses[FDCAN_ADAPTER_FDCAN2];
    CHECK(fake->filter.IdType == FDCAN_EXTENDED_ID);
    CHECK(fake->filter.FilterType == FDCAN_FILTER_MASK);
    CHECK(fake->filter.FilterID1 == UINT32_C(0x18104001));
    CHECK(fake->filter.FilterID2 == UINT32_C(0x1fffffff));
    CHECK((fake->filter.FilterID1 & fake->filter.FilterID2) ==
          FDCAN_ADAPTER_DALY_RX_ID);
    CHECK(((FDCAN_ADAPTER_DALY_RX_ID - 1u) & fake->filter.FilterID2) !=
          fake->filter.FilterID1);
    CHECK(((FDCAN_ADAPTER_DALY_RX_ID + 1u) & fake->filter.FilterID2) !=
          fake->filter.FilterID1);
    CHECK(hfdcan2.Init.StdFiltersNbr == 0u);
    CHECK(hfdcan2.Init.ExtFiltersNbr == 1u);
}

static void fdcan1_and_fdcan3_are_diagnostic_standard_receivers_only(void)
{
    size_t index;

    initialize_adapter();
    for (index = 0u; index < TEST_BUS_COUNT; ++index) {
        if (index == (size_t)FDCAN_ADAPTER_FDCAN2) {
            continue;
        }
        CHECK(fake_buses[index].filter.IdType == FDCAN_STANDARD_ID);
        CHECK(fake_buses[index].filter.FilterType == FDCAN_FILTER_RANGE);
        CHECK(fake_buses[index].filter.FilterID1 == 0u);
        CHECK(fake_buses[index].filter.FilterID2 == UINT32_C(0x7ff));
        CHECK(fake_buses[index].non_matching_ext == FDCAN_REJECT);
        CHECK(handles[index]->Init.Mode == FDCAN_MODE_BUS_MONITORING);
        CHECK(handles[index]->Init.TxFifoQueueElmtsNbr == 0u);
        CHECK((fake_buses[index].interrupt_lines & TEST_TX_MASK) == 0u);
        CHECK(fake_buses[index].notification_calls == 1u);
        CHECK(fake_buses[index].tx_calls == 0u);
    }
    /* There is deliberately no FDCAN1/FDCAN3 normal-TX application API. */
}

static void corrected_message_ram_word_layout_is_bounded(void)
{
    const uint32_t expected_offsets[TEST_BUS_COUNT] = {0u, 512u, 1024u};
    size_t index;

    initialize_adapter();
    for (index = 0u; index < TEST_BUS_COUNT; ++index) {
        const uint32_t start = handles[index]->Init.MessageRAMOffset;
        const uint32_t words = ram_layout_words((fdcan_adapter_bus_t)index);

        CHECK(start == expected_offsets[index]);
        CHECK(words == ((index == (size_t)FDCAN_ADAPTER_FDCAN2) ? 98u : 65u));
        CHECK(start <= 2560u);
        CHECK(words <= 2560u - start);
        if (index > 0u) {
            const uint32_t previous_start =
                handles[index - 1u]->Init.MessageRAMOffset;
            const uint32_t previous_words =
                ram_layout_words((fdcan_adapter_bus_t)(index - 1u));
            CHECK(previous_start + previous_words <= start);
        }
    }
}

static void receive_validates_arguments_and_empty_queue(void)
{
    oamr_can_frame_t frame = {0};

    initialize_adapter();
    expect_empty(FDCAN_ADAPTER_FDCAN1);
    CHECK(fdcan_adapter_receive(FDCAN_ADAPTER_BUS_COUNT, &frame) ==
          OAMR_STATUS_INVALID);
    CHECK(fdcan_adapter_receive(FDCAN_ADAPTER_FDCAN1, NULL) ==
          OAMR_STATUS_INVALID);
    CHECK(!fdcan_adapter_diagnostics(FDCAN_ADAPTER_BUS_COUNT,
                                     (fdcan_adapter_diagnostics_t *)&frame));
    CHECK(!fdcan_adapter_diagnostics(FDCAN_ADAPTER_FDCAN1, NULL));
}

static void daly_rx_rejects_malformed_fd_remote_and_long_dlc(void)
{
    FDCAN_RxHeaderTypeDef header;
    uint8_t data[8] = {0};
    fdcan_adapter_diagnostics_t diagnostics;

    initialize_adapter();
    inject_daly(FDCAN_DLC_BYTES_8, 0x10u);
    set_daly_header(&header, FDCAN_DLC_BYTES_8);
    header.Identifier = FDCAN_ADAPTER_DALY_RX_ID - 1u;
    inject_frame(FDCAN_ADAPTER_FDCAN2, &header, data);
    set_daly_header(&header, FDCAN_DLC_BYTES_8);
    header.IdType = FDCAN_STANDARD_ID;
    header.Identifier = 0x401u;
    inject_frame(FDCAN_ADAPTER_FDCAN2, &header, data);
    set_daly_header(&header, FDCAN_DLC_BYTES_8);
    header.RxFrameType = FDCAN_REMOTE_FRAME;
    inject_frame(FDCAN_ADAPTER_FDCAN2, &header, data);
    set_daly_header(&header, FDCAN_DLC_BYTES_8);
    header.FDFormat = FDCAN_FD_CAN;
    inject_frame(FDCAN_ADAPTER_FDCAN2, &header, data);
    inject_daly(7u, 0x20u);
    inject_daly(FDCAN_DLC_BYTES_12, 0x30u);
    set_daly_header(&header, FDCAN_DLC_BYTES_64 + 1u);
    inject_frame(FDCAN_ADAPTER_FDCAN2, &header, data);

    fdcan_adapter_rx_fifo0(&hfdcan2, FDCAN_IT_RX_FIFO0_NEW_MESSAGE);
    expect_received_id(FDCAN_ADAPTER_FDCAN2, FDCAN_ADAPTER_DALY_RX_ID,
                       OAMR_CAN_ID_EXTENDED, 8u);
    expect_empty(FDCAN_ADAPTER_FDCAN2);
    get_diagnostics(FDCAN_ADAPTER_FDCAN2, &diagnostics);
    CHECK(diagnostics.rx_events == 1u);
    CHECK(diagnostics.rx_frames == 1u);
    CHECK(diagnostics.rx_malformed_frames == 7u);
    CHECK(fake_buses[FDCAN_ADAPTER_FDCAN2].get_rx_calls == 8u);
}

static void diagnostic_rx_rejects_extended_and_preserves_standard_payload(void)
{
    FDCAN_RxHeaderTypeDef header;
    uint8_t data[8] = {0};
    oamr_can_frame_t frame;
    fdcan_adapter_diagnostics_t diagnostics;

    initialize_adapter();
    fake_now_ms = 7u;
    inject_standard(FDCAN_ADAPTER_FDCAN1, 0x123u, 3u, 0x40u);
    set_standard_header(&header, 0x123u, 3u);
    header.IdType = FDCAN_EXTENDED_ID;
    inject_frame(FDCAN_ADAPTER_FDCAN1, &header, data);
    fdcan_adapter_rx_fifo0(&hfdcan1, FDCAN_IT_RX_FIFO0_NEW_MESSAGE);

    memset(&frame, 0, sizeof(frame));
    CHECK(fdcan_adapter_receive(FDCAN_ADAPTER_FDCAN1, &frame) == OAMR_STATUS_OK);
    CHECK(frame.id == 0x123u);
    CHECK(frame.id_type == OAMR_CAN_ID_STANDARD);
    CHECK(frame.dlc == 3u);
    CHECK(frame.timestamp_us == UINT64_C(7000));
    CHECK(frame.data[0] == 0x40u);
    CHECK(frame.data[1] == 0x41u);
    CHECK(frame.data[2] == 0x42u);
    expect_empty(FDCAN_ADAPTER_FDCAN1);
    get_diagnostics(FDCAN_ADAPTER_FDCAN1, &diagnostics);
    CHECK(diagnostics.rx_frames == 1u);
    CHECK(diagnostics.rx_malformed_frames == 1u);
}

static void rx_queue_full_drops_newest_and_wraps_in_fifo_order(void)
{
    fdcan_adapter_diagnostics_t diagnostics;
    size_t index;

    initialize_adapter();
    CHECK(FDCAN_ADAPTER_RX_QUEUE_CAPACITY == 8u);
    CHECK(FDCAN_ADAPTER_POLL_LIMIT == 8u);
    for (index = 0u; index < 8u; ++index) {
        inject_standard(FDCAN_ADAPTER_FDCAN1, (uint32_t)index,
                        FDCAN_DLC_BYTES_8, (uint8_t)index);
    }
    fdcan_adapter_rx_fifo0(&hfdcan1, FDCAN_IT_RX_FIFO0_NEW_MESSAGE);
    CHECK(fake_buses[FDCAN_ADAPTER_FDCAN1].rx_head == 8u);
    inject_standard(FDCAN_ADAPTER_FDCAN1, 8u, FDCAN_DLC_BYTES_8, 8u);
    fdcan_adapter_rx_fifo0(&hfdcan1, FDCAN_IT_RX_FIFO0_NEW_MESSAGE);
    get_diagnostics(FDCAN_ADAPTER_FDCAN1, &diagnostics);
    CHECK(diagnostics.rx_frames == 8u);
    CHECK(diagnostics.rx_queue_overflows == 1u);
    CHECK(diagnostics.rx_fifo_overflows == 0u);
    for (index = 0u; index < 8u; ++index) {
        expect_received_id(FDCAN_ADAPTER_FDCAN1, (uint32_t)index,
                           OAMR_CAN_ID_STANDARD, 8u);
    }
    expect_empty(FDCAN_ADAPTER_FDCAN1);

    for (index = 8u; index < 16u; ++index) {
        inject_standard(FDCAN_ADAPTER_FDCAN1, (uint32_t)index,
                        FDCAN_DLC_BYTES_8, (uint8_t)index);
    }
    fdcan_adapter_rx_fifo0(&hfdcan1, FDCAN_IT_RX_FIFO0_NEW_MESSAGE);
    for (index = 8u; index < 12u; ++index) {
        expect_received_id(FDCAN_ADAPTER_FDCAN1, (uint32_t)index,
                           OAMR_CAN_ID_STANDARD, 8u);
    }
    for (index = 16u; index < 20u; ++index) {
        inject_standard(FDCAN_ADAPTER_FDCAN1, (uint32_t)index,
                        FDCAN_DLC_BYTES_8, (uint8_t)index);
    }
    fdcan_adapter_rx_fifo0(&hfdcan1, FDCAN_IT_RX_FIFO0_NEW_MESSAGE);
    for (index = 12u; index < 20u; ++index) {
        expect_received_id(FDCAN_ADAPTER_FDCAN1, (uint32_t)index,
                           OAMR_CAN_ID_STANDARD, 8u);
    }
    expect_empty(FDCAN_ADAPTER_FDCAN1);
    get_diagnostics(FDCAN_ADAPTER_FDCAN1, &diagnostics);
    CHECK(diagnostics.rx_frames == 20u);
    CHECK(diagnostics.rx_queue_overflows == 1u);
}

static void hardware_fifo_overflow_is_independent_of_queue_overflow(void)
{
    fdcan_adapter_diagnostics_t diagnostics;

    initialize_adapter();
    inject_standard(FDCAN_ADAPTER_FDCAN3, 0x456u, FDCAN_DLC_BYTES_8, 0x50u);
    fdcan_adapter_rx_fifo0(&hfdcan3,
                           FDCAN_IT_RX_FIFO0_FULL |
                               FDCAN_IT_RX_FIFO0_MESSAGE_LOST);
    expect_received_id(FDCAN_ADAPTER_FDCAN3, 0x456u,
                       OAMR_CAN_ID_STANDARD, 8u);
    get_diagnostics(FDCAN_ADAPTER_FDCAN3, &diagnostics);
    CHECK(diagnostics.rx_fifo_overflows == 1u);
    CHECK(diagnostics.rx_queue_overflows == 0u);
    CHECK(diagnostics.rx_frames == 1u);
}

static void rx_hal_error_does_not_enqueue_or_unbind_drain(void)
{
    fdcan_adapter_diagnostics_t diagnostics;

    initialize_adapter();
    fake_buses[FDCAN_ADAPTER_FDCAN2].get_rx_status = HAL_ERROR;
    inject_daly(FDCAN_DLC_BYTES_8, 0x60u);
    fdcan_adapter_rx_fifo0(&hfdcan2, FDCAN_IT_RX_FIFO0_NEW_MESSAGE);
    expect_empty(FDCAN_ADAPTER_FDCAN2);
    get_diagnostics(FDCAN_ADAPTER_FDCAN2, &diagnostics);
    CHECK(diagnostics.rx_hal_errors == 1u);
    CHECK(diagnostics.rx_frames == 0u);
    CHECK(fake_buses[FDCAN_ADAPTER_FDCAN2].get_rx_calls == 1u);
}

static void daly_tx_copies_exact_classical_extended_request(void)
{
    static const uint8_t request[8] = {
        0x01u, 0x90u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    fdcan_adapter_diagnostics_t diagnostics;
    const fake_bus_t *fake;

    initialize_adapter();
    CHECK(fdcan_adapter_send_daly_request(request) == OAMR_STATUS_OK);
    fake = &fake_buses[FDCAN_ADAPTER_FDCAN2];
    CHECK(fake->tx_calls == 1u);
    CHECK(fake->tx_header.Identifier == UINT32_C(0x18100140));
    CHECK(fake->tx_header.IdType == FDCAN_EXTENDED_ID);
    CHECK(fake->tx_header.TxFrameType == FDCAN_DATA_FRAME);
    CHECK(fake->tx_header.DataLength == FDCAN_DLC_BYTES_8);
    CHECK(fake->tx_header.ErrorStateIndicator == FDCAN_ESI_ACTIVE);
    CHECK(fake->tx_header.BitRateSwitch == FDCAN_BRS_OFF);
    CHECK(fake->tx_header.FDFormat == FDCAN_CLASSIC_CAN);
    CHECK(fake->tx_header.TxEventFifoControl == FDCAN_NO_TX_EVENTS);
    CHECK(memcmp(fake->tx_data, request, sizeof(request)) == 0);
    CHECK(fake_buses[FDCAN_ADAPTER_FDCAN1].tx_calls == 0u);
    CHECK(fake_buses[FDCAN_ADAPTER_FDCAN3].tx_calls == 0u);
    get_diagnostics(FDCAN_ADAPTER_FDCAN2, &diagnostics);
    CHECK(diagnostics.tx_submitted == 1u);
    CHECK(diagnostics.tx_rejected == 0u);
}

static void daly_tx_reports_busy_full_and_hal_failure(void)
{
    static const uint8_t request[8] = {0};
    fdcan_adapter_diagnostics_t diagnostics;

    initialize_adapter();
    fake_buses[FDCAN_ADAPTER_FDCAN2].tx_status = HAL_BUSY;
    CHECK(fdcan_adapter_send_daly_request(request) == OAMR_STATUS_TIMEOUT);
    get_diagnostics(FDCAN_ADAPTER_FDCAN2, &diagnostics);
    CHECK(diagnostics.tx_submitted == 0u);
    CHECK(diagnostics.tx_hal_errors == 0u);

    fake_buses[FDCAN_ADAPTER_FDCAN2].tx_status = HAL_ERROR;
    hfdcan2.ErrorCode = HAL_FDCAN_ERROR_FIFO_FULL;
    CHECK(fdcan_adapter_send_daly_request(request) == OAMR_STATUS_OVERFLOW);
    hfdcan2.ErrorCode = 0u;
    CHECK(fdcan_adapter_send_daly_request(request) == OAMR_STATUS_ERROR);
    get_diagnostics(FDCAN_ADAPTER_FDCAN2, &diagnostics);
    CHECK(diagnostics.tx_hal_errors == 2u);
    CHECK(diagnostics.tx_submitted == 0u);
    CHECK(fake_buses[FDCAN_ADAPTER_FDCAN2].tx_calls == 3u);

    CHECK(fdcan_adapter_send_daly_request(NULL) == OAMR_STATUS_INVALID);
    get_diagnostics(FDCAN_ADAPTER_FDCAN2, &diagnostics);
    CHECK(diagnostics.tx_rejected == 1u);
    CHECK(fake_buses[FDCAN_ADAPTER_FDCAN2].tx_calls == 3u);
}

static void tx_callbacks_are_scoped_to_fdcan2_and_nonzero_indices(void)
{
    fdcan_adapter_diagnostics_t diagnostics;

    initialize_adapter();
    fdcan_adapter_tx_complete(&hfdcan1, 1u);
    fdcan_adapter_tx_aborted(&hfdcan3, 1u);
    fdcan_adapter_tx_complete(&hfdcan2, 0u);
    fdcan_adapter_tx_aborted(&hfdcan2, 0u);
    get_diagnostics(FDCAN_ADAPTER_FDCAN2, &diagnostics);
    CHECK(diagnostics.tx_complete == 0u);
    CHECK(diagnostics.tx_aborted == 0u);
    fdcan_adapter_tx_complete(&hfdcan2, 1u);
    fdcan_adapter_tx_aborted(&hfdcan2, 2u);
    get_diagnostics(FDCAN_ADAPTER_FDCAN2, &diagnostics);
    CHECK(diagnostics.tx_complete == 1u);
    CHECK(diagnostics.tx_aborted == 1u);
}

static void failed_notification_start_stops_bus_and_isolates_other_buses(void)
{
    fdcan_adapter_diagnostics_t diagnostics;

    reset_handles_and_fakes();
    fake_buses[FDCAN_ADAPTER_FDCAN2].notify_status[0] = HAL_ERROR;
    hfdcan2.ErrorCode = 0x1234u;
    CHECK(!fdcan_adapter_init());
    get_diagnostics(FDCAN_ADAPTER_FDCAN1, &diagnostics);
    CHECK(diagnostics.started);
    get_diagnostics(FDCAN_ADAPTER_FDCAN2, &diagnostics);
    CHECK(!diagnostics.started);
    CHECK(diagnostics.start_errors == 1u);
    CHECK(diagnostics.last_hal_error == 0x1234u);
    CHECK(fake_buses[FDCAN_ADAPTER_FDCAN2].stop_calls == 1u);
    get_diagnostics(FDCAN_ADAPTER_FDCAN3, &diagnostics);
    CHECK(diagnostics.started);
}

static void begin_bus_off_episode(void)
{
    initialize_adapter();
    fake_buses[FDCAN_ADAPTER_FDCAN2].protocol.BusOff = 1u;
    fake_buses[FDCAN_ADAPTER_FDCAN2].protocol.Warning = 1u;
    fake_buses[FDCAN_ADAPTER_FDCAN2].protocol.ErrorPassive = 1u;
    fake_buses[FDCAN_ADAPTER_FDCAN2].counters.TxErrorCnt = 128u;
    fake_buses[FDCAN_ADAPTER_FDCAN2].counters.RxErrorCnt = 127u;
    fake_now_ms = 10u;
    fdcan_adapter_poll(fake_now_ms);
}

static void bus_off_immediately_gates_normal_daly_tx(void)
{
    static const uint8_t request[8] = {0xa5u};
    fdcan_adapter_diagnostics_t diagnostics;
    bool motion_permission = false;
    bool lift_permission = false;
    uint32_t safety_cookie = UINT32_C(0x51a7e123);

    begin_bus_off_episode();
    CHECK(fdcan_adapter_send_daly_request(request) == OAMR_STATUS_INVALID);
    CHECK(fake_buses[FDCAN_ADAPTER_FDCAN2].tx_calls == 0u);
    get_diagnostics(FDCAN_ADAPTER_FDCAN2, &diagnostics);
    CHECK(diagnostics.bus_off);
    CHECK(diagnostics.tx_rejected == 1u);
    CHECK(diagnostics.tx_error_count == 128u);
    CHECK(diagnostics.rx_error_count == 127u);
    CHECK(!motion_permission);
    CHECK(!lift_permission);
    CHECK(safety_cookie == UINT32_C(0x51a7e123));
}

static void recovery_restarts_at_exact_100_ms_without_early_attempt(void)
{
    const uint32_t stops_before = 0u;
    const uint32_t starts_before = 1u;
    bool motion_permission = false;
    bool lift_permission = false;
    uint32_t safety_cookie = UINT32_C(0x51a7e123);

    begin_bus_off_episode();
    fake_now_ms = 109u;
    fdcan_adapter_poll(fake_now_ms);
    CHECK(fake_buses[FDCAN_ADAPTER_FDCAN2].stop_calls == stops_before);
    CHECK(fake_buses[FDCAN_ADAPTER_FDCAN2].start_calls == starts_before);

    fake_now_ms = 110u;
    fdcan_adapter_poll(fake_now_ms);
    CHECK(fake_buses[FDCAN_ADAPTER_FDCAN2].stop_calls == stops_before + 1u);
    CHECK(fake_buses[FDCAN_ADAPTER_FDCAN2].start_calls == starts_before + 1u);
    CHECK(!motion_permission);
    CHECK(!lift_permission);
    CHECK(safety_cookie == UINT32_C(0x51a7e123));
}

static void successful_restart_reopens_transport_tx_not_safety(void)
{
    static const uint8_t request[8] = {0xa5u};
    bool motion_permission = false;
    bool lift_permission = false;
    uint32_t safety_cookie = UINT32_C(0x51a7e123);

    begin_bus_off_episode();
    fake_now_ms = 110u;
    fdcan_adapter_poll(fake_now_ms);
    CHECK(fdcan_adapter_send_daly_request(request) == OAMR_STATUS_OK);
    CHECK(fake_buses[FDCAN_ADAPTER_FDCAN2].tx_calls == 1u);
    CHECK(!motion_permission);
    CHECK(!lift_permission);
    CHECK(safety_cookie == UINT32_C(0x51a7e123));
}

static void receive_and_diagnostics_restore_primask(void)
{
    fdcan_adapter_diagnostics_t diagnostics;

    initialize_adapter();
    fdcan_test_primask = 1u;
    expect_empty(FDCAN_ADAPTER_FDCAN1);
    CHECK(fdcan_test_primask == 1u);
    fdcan_test_primask = 0u;
    get_diagnostics(FDCAN_ADAPTER_FDCAN1, &diagnostics);
    CHECK(fdcan_test_primask == 0u);
}

int main(void)
{
    RUN(common_initialization_order_filters_and_notifications);
    RUN(fdcan2_filter_is_exact_extended_mask_to_fifo0);
    RUN(fdcan1_and_fdcan3_are_diagnostic_standard_receivers_only);
    RUN(corrected_message_ram_word_layout_is_bounded);
    RUN(receive_validates_arguments_and_empty_queue);
    RUN(daly_rx_rejects_malformed_fd_remote_and_long_dlc);
    RUN(diagnostic_rx_rejects_extended_and_preserves_standard_payload);
    RUN(rx_queue_full_drops_newest_and_wraps_in_fifo_order);
    RUN(hardware_fifo_overflow_is_independent_of_queue_overflow);
    RUN(rx_hal_error_does_not_enqueue_or_unbind_drain);
    RUN(daly_tx_copies_exact_classical_extended_request);
    RUN(daly_tx_reports_busy_full_and_hal_failure);
    RUN(tx_callbacks_are_scoped_to_fdcan2_and_nonzero_indices);
    RUN(failed_notification_start_stops_bus_and_isolates_other_buses);
    RUN(bus_off_immediately_gates_normal_daly_tx);
    RUN(recovery_restarts_at_exact_100_ms_without_early_attempt);
    RUN(successful_restart_reopens_transport_tx_not_safety);
    RUN(receive_and_diagnostics_restore_primask);

    return TEST_END();
}
