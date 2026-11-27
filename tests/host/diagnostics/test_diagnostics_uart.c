/* SPDX-License-Identifier: MIT */
#include "test_harness.h"

#include "diagnostics_uart.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TEST_TX_CALL_LIMIT ((size_t)512u)
#define TEST_DRAIN_CAPACITY ((size_t)16384u)
#define TEST_QUEUE_FILL_RECORDS ((size_t)128u)

typedef struct {
    HAL_StatusTypeDef status[TEST_TX_CALL_LIMIT];
    size_t calls;
    UART_HandleTypeDef *handle[TEST_TX_CALL_LIMIT];
    const uint8_t *data[TEST_TX_CALL_LIMIT];
    uint16_t size[TEST_TX_CALL_LIMIT];
    uint8_t drained[TEST_DRAIN_CAPACITY];
    size_t drained_size;
    bool active;
} fake_uart_t;

static RCC_TypeDef fake_rcc;
RCC_TypeDef *RCC = &fake_rcc;
uint32_t diagnostics_test_primask;
static fake_uart_t fake_uart;
static UART_HandleTypeDef uart;
static UART_HandleTypeDef other_uart;
static USART_TypeDef other_instance;

HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *huart,
                                       const uint8_t *data, uint16_t size)
{
    HAL_StatusTypeDef status;
    const size_t call = fake_uart.calls;

    CHECK(call < TEST_TX_CALL_LIMIT);
    if (call >= TEST_TX_CALL_LIMIT) {
        return HAL_ERROR;
    }
    fake_uart.handle[call] = huart;
    fake_uart.data[call] = data;
    fake_uart.size[call] = size;
    ++fake_uart.calls;
    status = fake_uart.status[call];
    huart->pTxBuffPtr = data;
    huart->TxXferSize = size;
    huart->TxXferCount = size;
    fake_uart.active = status == HAL_OK;
    huart->gState = fake_uart.active ? HAL_UART_STATE_BUSY : HAL_UART_STATE_READY;
    return status;
}

static void init_adapter(void)
{
    memset(&fake_uart, 0, sizeof(fake_uart));
    memset(&uart, 0, sizeof(uart));
    memset(&other_uart, 0, sizeof(other_uart));
    memset(&fake_rcc, 0, sizeof(fake_rcc));
    uart.Instance = USART3;
    uart.gState = HAL_UART_STATE_READY;
    other_uart.Instance = &other_instance;
    other_uart.gState = HAL_UART_STATE_READY;
    diagnostics_test_primask = 0u;
    CHECK(diagnostics_uart_init(&uart, 0u));
}

/* Retire exactly the prefix that the fake HAL reports as transmitted. */
static void finish_active(size_t sent, bool error)
{
    size_t previous_calls;

    CHECK(fake_uart.active);
    CHECK(sent <= uart.TxXferSize);
    if (!fake_uart.active || (sent > uart.TxXferSize)) {
        return;
    }
    CHECK(fake_uart.drained_size + sent <= sizeof(fake_uart.drained));
    if (fake_uart.drained_size + sent <= sizeof(fake_uart.drained)) {
        memcpy(fake_uart.drained + fake_uart.drained_size, uart.pTxBuffPtr, sent);
        fake_uart.drained_size += sent;
    }
    uart.TxXferCount = (uint16_t)(uart.TxXferSize - (uint16_t)sent);
    uart.gState = HAL_UART_STATE_READY;
    previous_calls = fake_uart.calls;
    if (error) {
        diagnostics_uart_error(&uart);
    } else {
        HAL_UART_TxCpltCallback(&uart);
    }
    if (fake_uart.calls == previous_calls) {
        fake_uart.active = false;
    }
}

static void drain_all(void)
{
    size_t guard = 0u;

    while (fake_uart.active && (guard < TEST_TX_CALL_LIMIT)) {
        finish_active(uart.TxXferSize, false);
        ++guard;
    }
    CHECK(guard < TEST_TX_CALL_LIMIT);
    CHECK(!fake_uart.active);
}

static void expect_stream(const char *expected)
{
    const size_t length = strlen(expected);

    CHECK(fake_uart.drained_size == length);
    if (length <= sizeof(fake_uart.drained)) {
        CHECK(memcmp(fake_uart.drained, expected, length) == 0);
    }
}

static void test_init_and_reset_capture(void)
{
    UART_HandleTypeDef handle;
    uint32_t captured;

    memset(&handle, 0, sizeof(handle));
    CHECK(!diagnostics_uart_init(NULL, 0u));
    handle.Instance = &other_instance;
    CHECK(!diagnostics_uart_init(&handle, 0u));
    CHECK(!diagnostics_uart_write("before init\r\n"));

    fake_rcc.RSR = RCC_RSR_PORRSTF | RCC_RSR_PINRSTF | RCC_RSR_IWDG1RSTF;
    captured = diagnostics_uart_capture_reset_flags();
    CHECK(captured == (RCC_RSR_PORRSTF | RCC_RSR_PINRSTF | RCC_RSR_IWDG1RSTF));
    CHECK((fake_rcc.RSR & RCC_RSR_RMVF) != 0u);
    CHECK((fake_rcc.RSR & captured) == captured);

    init_adapter();
    CHECK(!diagnostics_uart_write(NULL));
    CHECK(fake_uart.calls == 0u);
    CHECK(diagnostics_test_primask == 0u);
}

static void test_active_buffer_contiguous_and_wrapped_lengths(void)
{
    static const char message[] = "12345678";
    unsigned iteration;

    init_adapter();
    /* Advance both modulo indices to 1016 using only the public API. */
    for (iteration = 0u; iteration < 127u; ++iteration) {
        size_t call = fake_uart.calls;

        CHECK(diagnostics_uart_write(message));
        CHECK(fake_uart.calls == call + 1u);
        CHECK(fake_uart.size[call] == 8u);
        CHECK(memcmp(fake_uart.data[call], message, 8u) == 0);
        drain_all();
    }
    CHECK(fake_uart.drained_size == 1016u);
    fake_uart.drained_size = 0u;

    /* First write crosses write_index 1024 -> 0. Its active buffer is the
     * contiguous tail [1016,1024), even after a second write wraps to [0,8). */
    CHECK(diagnostics_uart_write("12345678"));
    CHECK(diagnostics_uart_write("abcdefgh"));
    CHECK(fake_uart.size[fake_uart.calls - 1u] == 8u);
    CHECK(memcmp(fake_uart.data[fake_uart.calls - 1u], "12345678", 8u) == 0);

    /* A four-byte completion leaves read_index=1020 and write_index=8.
     * The next HAL size must be 1024-1020=4, not an unwrapped/wrapped total. */
    finish_active(4u, false);
    CHECK(fake_uart.active);
    CHECK(uart.TxXferSize == 4u);
    CHECK(memcmp(uart.pTxBuffPtr, "5678", 4u) == 0);
    CHECK(diagnostics_uart_write("IJKL"));
    CHECK(fake_uart.calls == 129u);
    CHECK(uart.TxXferSize == 4u);

    /* read_index now wraps 1024 -> 0. The queued suffix is contiguous and
     * exactly 12 bytes; the TX callback must not skip or repeat either side. */
    finish_active(4u, false);
    CHECK(fake_uart.active);
    CHECK(uart.TxXferSize == 12u);
    CHECK(memcmp(uart.pTxBuffPtr, "abcdefghIJKL", 12u) == 0);
    drain_all();
    expect_stream("12345678abcdefghIJKL");
    CHECK(diagnostics_test_primask == 0u);
}

static void test_many_complete_wraps_preserve_fifo(void)
{
    char expected[65u][9];
    unsigned iteration;

    init_adapter();
    for (iteration = 0u; iteration < 65u; ++iteration) {
        size_t index;

        for (index = 0u; index < 8u; ++index) {
            expected[iteration][index] = (char)('A' + ((iteration + index) % 26u));
        }
        expected[iteration][8] = '\0';
        CHECK(diagnostics_uart_write(expected[iteration]));
        CHECK(uart.TxXferSize == 8u);
        drain_all();
    }
    CHECK(fake_uart.drained_size == 65u * 8u);
    for (iteration = 0u; iteration < 65u; ++iteration) {
        CHECK(memcmp(fake_uart.drained + (iteration * 8u), expected[iteration], 8u) == 0);
    }
    /* Both indices have returned to zero; another transfer still starts in bounds. */
    CHECK(diagnostics_uart_write("wrap-ok!"));
    CHECK(uart.TxXferSize == 8u);
    drain_all();
    CHECK(memcmp(fake_uart.drained + 520u, "wrap-ok!", 8u) == 0);
}

static void test_backpressure_poll_and_busy_owner(void)
{
    init_adapter();
    fake_uart.status[0] = HAL_BUSY;
    fake_uart.status[1] = HAL_BUSY;
    CHECK(diagnostics_uart_write("busy"));
    CHECK(fake_uart.calls == 1u);
    CHECK(!fake_uart.active);
    diagnostics_uart_poll();
    CHECK(fake_uart.calls == 2u);
    CHECK(fake_uart.size[1] == 4u);
    CHECK(fake_uart.data[1] == fake_uart.data[0]);

    uart.gState = HAL_UART_STATE_BUSY;
    diagnostics_uart_poll();
    CHECK(fake_uart.calls == 2u);
    uart.gState = HAL_UART_STATE_READY;
    diagnostics_uart_poll();
    CHECK(fake_uart.calls == 3u);
    CHECK(fake_uart.size[2] == 4u);
    CHECK(fake_uart.data[2] == fake_uart.data[0]);
    diagnostics_uart_poll();
    CHECK(fake_uart.calls == 3u);
    drain_all();
    expect_stream("busy");
    CHECK(diagnostics_test_primask == 0u);
}

static void test_start_error_retires_buffer_and_counts(void)
{
    init_adapter();
    fake_uart.status[0] = HAL_ERROR;
    CHECK(diagnostics_uart_write("bad"));
    CHECK(fake_uart.calls == 1u);
    CHECK(!fake_uart.active);
    CHECK(diagnostics_uart_write("good"));
    CHECK(fake_uart.calls == 2u);
    CHECK(fake_uart.size[1] == 4u);
    drain_all();
    expect_stream("good");
}

static void test_tx_callback_partial_and_error_progression(void)
{
    init_adapter();
    CHECK(diagnostics_uart_write("abcdef"));
    CHECK(diagnostics_uart_write("XYZ"));
    finish_active(2u, false);
    CHECK(fake_uart.calls == 2u);
    /* Only two bytes retired: cdef remains and is contiguous with XYZ. */
    CHECK(fake_uart.size[1] == 7u);
    CHECK(memcmp(fake_uart.data[1], "cdefXYZ", 7u) == 0);
    drain_all();
    expect_stream("abcdefXYZ");

    CHECK(diagnostics_uart_write("error"));
    CHECK(diagnostics_uart_write("next"));
    finish_active(3u, false); /* ordinary completion first */
    drain_all();

    fake_uart.drained_size = 0u;
    CHECK(diagnostics_uart_write("error"));
    CHECK(diagnostics_uart_write("next"));
    finish_active(3u, true);
    CHECK(fake_uart.active);
    CHECK(memcmp(uart.pTxBuffPtr, "ornext", 6u) == 0);
    drain_all();
    expect_stream("errornext");
}

static void test_callback_handle_filtering(void)
{
    const uint8_t *active_data;
    uint16_t active_size;

    init_adapter();
    CHECK(diagnostics_uart_write("first"));
    CHECK(diagnostics_uart_write("second"));
    active_data = uart.pTxBuffPtr;
    active_size = uart.TxXferSize;
    diagnostics_uart_tx_complete(NULL);
    diagnostics_uart_tx_complete(&other_uart);
    diagnostics_uart_error(NULL);
    diagnostics_uart_error(&other_uart);
    CHECK(fake_uart.calls == 1u);
    CHECK(uart.pTxBuffPtr == active_data);
    CHECK(uart.TxXferSize == active_size);
    drain_all();
    diagnostics_uart_tx_complete(&uart);
    diagnostics_uart_error(&uart);
    CHECK(fake_uart.calls == 2u);
    expect_stream("firstsecond");
}

static void test_queue_bound_and_truncation(void)
{
    char oversized[DIAGNOSTICS_UART_MESSAGE_MAX + 2u];
    char expected[DIAGNOSTICS_UART_TX_CAPACITY];
    size_t index;

    init_adapter();
    fake_uart.status[0] = HAL_BUSY;
    memset(oversized, 'q', sizeof(oversized));
    oversized[sizeof(oversized) - 1u] = '\0';
    CHECK(!diagnostics_uart_write(oversized));
    CHECK(fake_uart.size[0] == DIAGNOSTICS_UART_MESSAGE_MAX);
    for (index = 0u; index < 86u; ++index) {
        CHECK(diagnostics_uart_write("1234567890"));
    }
    /* 160 + 860 = 1020 bytes; three remain but a complete message cannot fit. */
    CHECK(diagnostics_uart_write("xyz"));
    CHECK(!diagnostics_uart_write("1234"));
    CHECK(!diagnostics_uart_write("!"));
    CHECK(diagnostics_test_primask == 0u);

    uart.gState = HAL_UART_STATE_READY;
    diagnostics_uart_poll();
    drain_all();
    CHECK(fake_uart.drained_size == DIAGNOSTICS_UART_TX_CAPACITY - 1u);
    memset(expected, 'q', DIAGNOSTICS_UART_MESSAGE_MAX);
    for (index = DIAGNOSTICS_UART_MESSAGE_MAX; index < 1020u; index += 10u) {
        memcpy(expected + index, "1234567890", 10u);
    }
    memcpy(expected + 1020u, "xyz", 3u);
    CHECK(memcmp(fake_uart.drained, expected, DIAGNOSTICS_UART_TX_CAPACITY - 1u) == 0);
}

static void test_raw_binary_record_wraps_atomically(void)
{
    uint8_t first[900];
    uint8_t wrapped[300];
    size_t index;

    for (index = 0u; index < sizeof(first); ++index) {
        first[index] = (uint8_t)index;
    }
    for (index = 0u; index < sizeof(wrapped); ++index) {
        wrapped[index] = (uint8_t)(index * 37u);
    }
    wrapped[17] = 0u;
    wrapped[150] = 0u;

    init_adapter();
    fake_uart.status[0] = HAL_BUSY;
    CHECK(diagnostics_uart_write_bytes(first, sizeof(first)));
    diagnostics_uart_poll();
    drain_all();
    fake_uart.drained_size = 0u;

    CHECK(diagnostics_uart_write_bytes(wrapped, sizeof(wrapped)));
    CHECK(uart.TxXferSize == 124u);
    drain_all();
    CHECK(fake_uart.drained_size == sizeof(wrapped));
    CHECK(memcmp(fake_uart.drained, wrapped, sizeof(wrapped)) == 0);
    CHECK(diagnostics_test_primask == 0u);
}

static void test_raw_exact_fit_and_oversize_rejection(void)
{
    uint8_t record[DIAGNOSTICS_UART_TX_CAPACITY];
    size_t index;

    for (index = 0u; index < sizeof(record); ++index) {
        record[index] = (uint8_t)(index ^ 0xa5u);
    }
    init_adapter();
    CHECK(!diagnostics_uart_write_bytes(NULL, 1u));
    CHECK(!diagnostics_uart_write_bytes(record, 0u));
    CHECK(!diagnostics_uart_write_bytes(record, sizeof(record)));
    CHECK(fake_uart.calls == 0u);

    CHECK(diagnostics_uart_write_bytes(record, DIAGNOSTICS_UART_TX_CAPACITY - 1u));
    CHECK(uart.TxXferSize == DIAGNOSTICS_UART_TX_CAPACITY - 1u);
    drain_all();
    CHECK(fake_uart.drained_size == DIAGNOSTICS_UART_TX_CAPACITY - 1u);
    CHECK(memcmp(fake_uart.drained, record, DIAGNOSTICS_UART_TX_CAPACITY - 1u) == 0);
}

static void test_raw_backpressure_exact_remaining_space_and_drop_accounting(void)
{
    uint8_t first[100];
    uint8_t rejected[924];
    uint8_t exact[923];

    memset(first, 0x11, sizeof(first));
    memset(rejected, 0x22, sizeof(rejected));
    memset(exact, 0x33, sizeof(exact));
    init_adapter();
    fake_uart.status[0] = HAL_BUSY;
    CHECK(diagnostics_uart_write_bytes(first, sizeof(first)));
    CHECK(!diagnostics_uart_write_bytes(rejected, sizeof(rejected)));
    CHECK(diagnostics_uart_write_bytes(exact, sizeof(exact)));
    CHECK(fake_uart.calls == 2u);

    drain_all();
    drain_all();
    CHECK(fake_uart.drained_size == 1023u);
    CHECK(memcmp(fake_uart.drained, first, sizeof(first)) == 0);
    CHECK(memcmp(fake_uart.drained + sizeof(first), exact, sizeof(exact)) == 0);

    fake_uart.drained_size = 0u;
    diagnostics_uart_report_boot(true, true);
    drain_all();
    expect_stream("\r\nOAMR reset raw=0x00000000 causes=0x00 unknown=0x00000000\r\n"
                  "OAMR init sensors=ok fdcan=ok uart_dropped=924 tx_errors=0\r\n");
}

static void test_raw_transport_callback_contract(void)
{
    static const uint8_t record[] = {'a', 0u, 'b'};
    size_t transferred = 99u;

    init_adapter();
    CHECK(diagnostics_uart_transport_write(NULL, NULL, 0u, NULL) ==
          OAMR_TRANSPORT_INVALID);
    CHECK(diagnostics_uart_transport_write(NULL, NULL, 0u, &transferred) ==
          OAMR_TRANSPORT_INVALID);
    CHECK(transferred == 0u);
    transferred = 99u;
    CHECK(diagnostics_uart_transport_write(NULL, record, sizeof(record),
                                           &transferred) == OAMR_TRANSPORT_OK);
    CHECK(transferred == sizeof(record));
    drain_all();
    CHECK(fake_uart.drained_size == sizeof(record));
    CHECK(memcmp(fake_uart.drained, record, sizeof(record)) == 0);

    init_adapter();
    fake_uart.status[0] = HAL_BUSY;
    fake_uart.status[1] = HAL_BUSY;
    CHECK(diagnostics_uart_write_bytes(record, 1u));
    /* Fill all remaining space with text, then the transport must reject the
     * whole new record rather than report a short write. */
    {
        char fill[DIAGNOSTICS_UART_TX_CAPACITY];
        memset(fill, 'x', sizeof(fill));
        CHECK(diagnostics_uart_write_bytes(fill, DIAGNOSTICS_UART_TX_CAPACITY - 2u));
    }
    transferred = 99u;
    CHECK(diagnostics_uart_transport_write(NULL, record, sizeof(record),
                                           &transferred) == OAMR_TRANSPORT_WOULD_BLOCK);
    CHECK(transferred == 0u);
}

static void test_boot_and_failure_report_golden_output(void)
{
    init_adapter();
    CHECK(diagnostics_uart_init(&uart,
                                RCC_RSR_PORRSTF | RCC_RSR_IWDG1RSTF |
                                    RCC_RSR_PINRSTF));
    diagnostics_uart_report_boot(false, true);
    drain_all();
    expect_stream("\r\nOAMR reset raw=0x04C00000 causes=0x0B unknown=0x00000000\r\n"
                  "OAMR init sensors=fail fdcan=ok uart_dropped=0 tx_errors=0\r\n");

    fake_uart.drained_size = 0u;
    diagnostics_uart_report_startup_failure("fdcan2", UINT32_C(0x89abcdef));
    drain_all();
    expect_stream("OAMR startup failure fdcan2 code=0x89ABCDEF\r\n");

    fake_uart.drained_size = 0u;
    diagnostics_uart_report_startup_failure(NULL, 0u);
    drain_all();
    expect_stream("OAMR startup failure unknown code=0x00000000\r\n");
}

int main(void)
{
    RUN(test_init_and_reset_capture);
    RUN(test_active_buffer_contiguous_and_wrapped_lengths);
    RUN(test_many_complete_wraps_preserve_fifo);
    RUN(test_backpressure_poll_and_busy_owner);
    RUN(test_start_error_retires_buffer_and_counts);
    RUN(test_tx_callback_partial_and_error_progression);
    RUN(test_callback_handle_filtering);
    RUN(test_queue_bound_and_truncation);
    RUN(test_raw_binary_record_wraps_atomically);
    RUN(test_raw_exact_fit_and_oversize_rejection);
    RUN(test_raw_backpressure_exact_remaining_space_and_drop_accounting);
    RUN(test_raw_transport_callback_contract);
    RUN(test_boot_and_failure_report_golden_output);
    return TEST_END();
}
