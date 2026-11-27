/* SPDX-License-Identifier: MIT */
#include "test_harness.h"

#include "oamr/diagnostic_tx.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TEST_CALL_LIMIT ((size_t)64u)
#define TEST_WIRE_CAPACITY ((size_t)8192u)
#define TEST_GUARD ((size_t)8u)

typedef struct {
    oamr_transport_result_t result;
    size_t written;
} write_plan_t;

typedef struct {
    uint8_t expected_data[512];
    size_t expected_size;
    uint8_t observed_data[512];
    size_t observed_size;
} call_record_t;

typedef struct {
    write_plan_t plans[TEST_CALL_LIMIT];
    size_t plan_count;
    size_t calls;
    call_record_t history[TEST_CALL_LIMIT];
    bool broken;
    uint8_t wire[TEST_WIRE_CAPACITY];
    size_t wire_size;
} fake_transport_t;

static oamr_transport_result_t fake_write(void *context, const uint8_t *data,
                                          size_t data_size, size_t *transferred)
{
    fake_transport_t *fake = (fake_transport_t *)context;
    write_plan_t plan;
    call_record_t *record;

    CHECK(fake != NULL);
    CHECK(data != NULL);
    CHECK(transferred != NULL);
    CHECK(fake != NULL && fake->calls < TEST_CALL_LIMIT);
    CHECK(fake != NULL && fake->calls < fake->plan_count);
    if ((fake == NULL) || (transferred == NULL) || (data == NULL) ||
        (fake->calls >= TEST_CALL_LIMIT) || (fake->calls >= fake->plan_count)) {
        return OAMR_TRANSPORT_INVALID;
    }

    record = &fake->history[fake->calls];
    memset(record, 0, sizeof(*record));
    record->expected_size = data_size;
    CHECK(record->expected_size <= sizeof(record->expected_data));
    if (record->expected_size <= sizeof(record->expected_data)) {
        memcpy(record->expected_data, data, record->expected_size);
    }
    plan = fake->plans[fake->calls];
    ++fake->calls;
    *transferred = 0u;
    if (fake->broken) {
        *transferred = data_size + 1u;
        return OAMR_TRANSPORT_OK;
    }
    if (plan.result == OAMR_TRANSPORT_OK) {
        *transferred = plan.written;
        if (plan.written <= data_size) {
            record->observed_size = plan.written;
            if (plan.written <= sizeof(record->observed_data)) {
                memcpy(record->observed_data, data, plan.written);
            }
            if (fake->wire_size + plan.written <= sizeof(fake->wire)) {
                memcpy(fake->wire + fake->wire_size, data, plan.written);
                fake->wire_size += plan.written;
            }
        }
    }
    return plan.result;
}

static void fake_plan(fake_transport_t *fake, oamr_transport_result_t result,
                      size_t written)
{
    CHECK(fake->plan_count < TEST_CALL_LIMIT);
    if (fake->plan_count < TEST_CALL_LIMIT) {
        fake->plans[fake->plan_count].result = result;
        fake->plans[fake->plan_count].written = written;
        ++fake->plan_count;
    }
}

static void init_tx(oamr_diagnostic_tx_t *tx, void *storage, size_t capacity,
                    fake_transport_t *fake)
{
    oamr_transport_t transport;

    memset(fake, 0, sizeof(*fake));
    transport.context = fake;
    transport.read = NULL;
    transport.write = fake_write;
    memset(tx, 0xa5, sizeof(*tx));
    CHECK(oamr_diagnostic_tx_init(tx, storage, capacity, &transport) ==
          OAMR_STATUS_OK);
    CHECK(tx->transport.read == NULL);
    CHECK(tx->transport.context == fake);
}

static void expect_call(const fake_transport_t *fake, size_t index,
                        const char *expected)
{
    const size_t length = strlen(expected);

    CHECK(index < fake->calls);
    if (index < fake->calls) {
        CHECK(fake->history[index].observed_size == length);
        CHECK(fake->history[index].expected_size >= length);
        if ((fake->history[index].observed_size == length) &&
            (length <= sizeof(fake->history[index].observed_data))) {
            CHECK(memcmp(fake->history[index].observed_data, expected, length) == 0);
        }
    }
}

static void expect_empty(const oamr_diagnostic_tx_t *tx, size_t calls)
{
    oamr_diagnostic_tx_snapshot_t snapshot;

    memset(&snapshot, 0, sizeof(snapshot));
    CHECK(oamr_diagnostic_tx_get_snapshot(tx, &snapshot) == OAMR_STATUS_OK);
    CHECK(snapshot.queued_bytes == 0u);
    CHECK(snapshot.free_bytes == snapshot.capacity);
    CHECK(tx->transport.context != NULL);
    if (tx->transport.context != NULL) {
        const fake_transport_t *fake = (const fake_transport_t *)tx->transport.context;
        CHECK(fake->calls == calls);
    }
}

static void test_init_and_snapshot_misuse(void)
{
    oamr_diagnostic_tx_t tx;
    oamr_diagnostic_tx_snapshot_t snapshot;
    oamr_transport_t transport;
    fake_transport_t fake;
    uint8_t storage[8];

    memset(&fake, 0, sizeof(fake));
    transport.context = &fake;
    transport.read = NULL;
    transport.write = fake_write;
    CHECK(oamr_diagnostic_tx_init(NULL, storage, sizeof(storage), &transport) ==
          OAMR_STATUS_INVALID);
    CHECK(oamr_diagnostic_tx_init(&tx, NULL, sizeof(storage), &transport) ==
          OAMR_STATUS_INVALID);
    CHECK(!tx.initialized);
    CHECK(oamr_diagnostic_tx_init(&tx, storage, 0u, &transport) ==
          OAMR_STATUS_INVALID);
    CHECK(!tx.initialized);
    CHECK(oamr_diagnostic_tx_init(&tx, storage, sizeof(storage), NULL) ==
          OAMR_STATUS_INVALID);
    CHECK(!tx.initialized);
    transport.write = NULL;
    CHECK(oamr_diagnostic_tx_init(&tx, storage, sizeof(storage), &transport) ==
          OAMR_STATUS_INVALID);
    CHECK(!tx.initialized);

    init_tx(&tx, storage, sizeof(storage), &fake);
    CHECK(oamr_diagnostic_tx_get_snapshot(NULL, &snapshot) == OAMR_STATUS_INVALID);
    CHECK(oamr_diagnostic_tx_get_snapshot(&tx, NULL) == OAMR_STATUS_INVALID);
    memset(&snapshot, 0xa5, sizeof(snapshot));
    CHECK(oamr_diagnostic_tx_get_snapshot(NULL, &snapshot) == OAMR_STATUS_INVALID);
    CHECK(!snapshot.initialized);
    CHECK(snapshot.capacity == 0u);

    tx.head = tx.capacity;
    CHECK(oamr_diagnostic_tx_get_snapshot(&tx, &snapshot) == OAMR_STATUS_INVALID);
    CHECK(!snapshot.initialized);
    CHECK(oamr_diagnostic_tx_flush_once(&tx) == OAMR_TRANSPORT_INVALID);
    CHECK(oamr_diagnostic_tx_submit(&tx, "x", 1u) == OAMR_STATUS_INVALID);
    CHECK(oamr_diagnostic_tx_flush_once(NULL) == OAMR_TRANSPORT_INVALID);
    CHECK(oamr_diagnostic_tx_submit(NULL, "x", 1u) == OAMR_STATUS_INVALID);
    CHECK(fake.calls == 0u);
}

static void test_capacity_one_exact_fit_and_rejections(void)
{
    oamr_diagnostic_tx_t tx;
    fake_transport_t fake;
    uint8_t storage[1] = {0x5au};

    init_tx(&tx, storage, sizeof(storage), &fake);
    CHECK(oamr_diagnostic_tx_submit(&tx, NULL, 0u) == OAMR_STATUS_OVERFLOW);
    CHECK(oamr_diagnostic_tx_submit(&tx, NULL, 1u) == OAMR_STATUS_INVALID);
    CHECK(oamr_diagnostic_tx_submit(&tx, "ab", 2u) == OAMR_STATUS_OVERFLOW);
    CHECK(storage[0] == 0x5au);
    CHECK(tx.dropped_records == 2u);
    CHECK(tx.dropped_bytes == 2u);
    fake_plan(&fake, OAMR_TRANSPORT_OK, 0u);
    fake_plan(&fake, OAMR_TRANSPORT_OK, 1u);
    CHECK(oamr_diagnostic_tx_submit(&tx, "A", 1u) == OAMR_STATUS_OK);
    CHECK(storage[0] == (uint8_t)'A');
    CHECK(oamr_diagnostic_tx_submit(&tx, "B", 1u) == OAMR_STATUS_OVERFLOW);
    CHECK(storage[0] == (uint8_t)'A');
    CHECK(tx.dropped_records == 3u);
    CHECK(tx.dropped_bytes == 3u);
    CHECK(tx.enqueued_records == 1u);
    CHECK(tx.enqueued_bytes == 1u);
    CHECK(oamr_diagnostic_tx_flush_once(&tx) == OAMR_TRANSPORT_WOULD_BLOCK);
    CHECK(fake.calls == 1u);
    CHECK(oamr_diagnostic_tx_flush_once(&tx) == OAMR_TRANSPORT_OK);
    CHECK(fake.calls == 2u);
    CHECK(fake.wire_size == 1u);
    CHECK(fake.wire[0] == (uint8_t)'A');
    CHECK(tx.transmitted_bytes == 1u);
    expect_empty(&tx, 2u);
    CHECK(oamr_diagnostic_tx_flush_once(&tx) == OAMR_TRANSPORT_OK);
    CHECK(fake.calls == 2u);
}

static void test_guarded_wrap_fifo_short_writes_and_drain(void)
{
    oamr_diagnostic_tx_t tx;
    fake_transport_t fake;
    uint8_t guarded[TEST_GUARD + 8u + TEST_GUARD];
    uint8_t *storage = guarded + TEST_GUARD;
    size_t index;

    memset(guarded, 0xa5, sizeof(guarded));
    init_tx(&tx, storage, 8u, &fake);
    CHECK(oamr_diagnostic_tx_submit(&tx, "abcde", 5u) == OAMR_STATUS_OK);
    fake_plan(&fake, OAMR_TRANSPORT_OK, 3u);
    CHECK(oamr_diagnostic_tx_flush_once(&tx) == OAMR_TRANSPORT_OK);
    CHECK(tx.head == 3u);
    CHECK(tx.count == 2u);
    CHECK(oamr_diagnostic_tx_submit(&tx, "FGHIJ", 5u) == OAMR_STATUS_OK);
    CHECK(tx.head == 3u);
    CHECK(tx.count == 7u);
    CHECK(storage[3] == (uint8_t)'d');
    CHECK(storage[4] == (uint8_t)'e');
    CHECK(storage[5] == (uint8_t)'F');
    CHECK(storage[6] == (uint8_t)'G');
    CHECK(storage[7] == (uint8_t)'H');
    CHECK(storage[0] == (uint8_t)'I');
    CHECK(storage[1] == (uint8_t)'J');
    CHECK(oamr_diagnostic_tx_submit(&tx, "KL", 2u) == OAMR_STATUS_OVERFLOW);
    CHECK(tx.count == 7u);
    CHECK(storage[2] == (uint8_t)'c');

    fake_plan(&fake, OAMR_TRANSPORT_OK, 5u);
    fake_plan(&fake, OAMR_TRANSPORT_OK, 2u);
    CHECK(oamr_diagnostic_tx_flush_once(&tx) == OAMR_TRANSPORT_OK);
    CHECK(tx.head == 0u);
    CHECK(tx.count == 2u);
    CHECK(fake.history[1].expected_size == 5u);
    CHECK(memcmp(fake.history[1].observed_data, "deFGH", 5u) == 0);
    CHECK(oamr_diagnostic_tx_flush_once(&tx) == OAMR_TRANSPORT_OK);
    CHECK(tx.head == 2u);
    CHECK(tx.count == 0u);
    CHECK(oamr_diagnostic_tx_submit(&tx, "KLMNO", 5u) == OAMR_STATUS_OK);
    fake_plan(&fake, OAMR_TRANSPORT_OK, 5u);
    CHECK(oamr_diagnostic_tx_flush_once(&tx) == OAMR_TRANSPORT_OK);
    expect_empty(&tx, 4u);
    CHECK(tx.transmitted_bytes == 15u);
    CHECK(fake.wire_size == 15u);
    CHECK(memcmp(fake.wire, "abcdeFGHIJKLMNO", 15u) == 0);
    for (index = 0u; index < TEST_GUARD; ++index) {
        CHECK(guarded[index] == 0xa5u);
        CHECK(guarded[TEST_GUARD + 8u + index] == 0xa5u);
    }
}

static void test_multiple_wraps_and_full_drain(void)
{
    oamr_diagnostic_tx_t tx;
    fake_transport_t fake;
    uint8_t storage[5];
    static const char expected[] = "ABCDEFGHIJKLMNOPQRSTUVWX";
    static const size_t expected_lengths[] = {4u, 1u, 3u, 2u, 2u, 3u, 1u, 4u, 4u};
    size_t produced = 0u;
    size_t call;
    unsigned round;

    init_tx(&tx, storage, sizeof(storage), &fake);
    for (round = 0u; round < 6u; ++round) {
        char record[4];
        size_t record_index;
        size_t flushes = 0u;

        for (record_index = 0u; record_index < sizeof(record); ++record_index) {
            record[record_index] = expected[produced + record_index];
        }
        CHECK(oamr_diagnostic_tx_submit(&tx, record, sizeof(record)) ==
              OAMR_STATUS_OK);
        produced += sizeof(record);
        /* A wrapped record needs one bounded flush per contiguous prefix. */
        while (tx.count != 0u) {
            const size_t contiguous = (tx.capacity - tx.head < tx.count) ?
                                      tx.capacity - tx.head : tx.count;

            fake_plan(&fake, OAMR_TRANSPORT_OK, contiguous);
            CHECK(oamr_diagnostic_tx_flush_once(&tx) == OAMR_TRANSPORT_OK);
            ++flushes;
            CHECK(flushes <= 2u);
            if (flushes > 2u) {
                break;
            }
        }
    }
    CHECK(produced == 24u);
    CHECK(fake.wire_size == produced);
    CHECK(memcmp(fake.wire, expected, produced) == 0);
    CHECK(tx.head == 4u);
    CHECK(fake.calls == sizeof(expected_lengths) / sizeof(expected_lengths[0]));
    for (call = 0u; call < sizeof(expected_lengths) / sizeof(expected_lengths[0]); ++call) {
        CHECK(fake.history[call].expected_size == expected_lengths[call]);
    }
    expect_empty(&tx, 9u);
}

static void test_backpressure_has_one_bounded_callback(void)
{
    oamr_diagnostic_tx_t tx;
    fake_transport_t fake;
    uint8_t storage[8];
    uint8_t before[8];

    init_tx(&tx, storage, sizeof(storage), &fake);
    CHECK(oamr_diagnostic_tx_submit(&tx, "abcde", 5u) == OAMR_STATUS_OK);
    memcpy(before, storage, sizeof(storage));
    fake_plan(&fake, OAMR_TRANSPORT_WOULD_BLOCK, 0u);
    fake_plan(&fake, OAMR_TRANSPORT_OK, 0u);
    CHECK(oamr_diagnostic_tx_flush_once(&tx) == OAMR_TRANSPORT_WOULD_BLOCK);
    CHECK(fake.calls == 1u);
    CHECK(tx.would_block_calls == 1u);
    CHECK(tx.would_block_bytes == 5u);
    CHECK(oamr_diagnostic_tx_flush_once(&tx) == OAMR_TRANSPORT_WOULD_BLOCK);
    CHECK(fake.calls == 2u);
    CHECK(tx.would_block_calls == 2u);
    CHECK(tx.would_block_bytes == 10u);
    CHECK(tx.transport_errors == 0u);
    CHECK(tx.transmitted_bytes == 0u);
    CHECK(tx.head == 0u);
    CHECK(tx.count == 5u);
    CHECK(memcmp(before, storage, sizeof(storage)) == 0);
    fake_plan(&fake, OAMR_TRANSPORT_OK, 2u);
    CHECK(oamr_diagnostic_tx_flush_once(&tx) == OAMR_TRANSPORT_OK);
    CHECK(fake.calls == 3u);
    CHECK(tx.count == 3u);
    CHECK(tx.transmitted_bytes == 2u);
}

static void test_transport_errors_and_malformed_transfers(void)
{
    static const struct {
        oamr_transport_result_t result;
        size_t written;
        oamr_transport_result_t expected;
    } cases[] = {
        {OAMR_TRANSPORT_DISCONNECTED, 0u, OAMR_TRANSPORT_DISCONNECTED},
        {OAMR_TRANSPORT_TIMEOUT, 0u, OAMR_TRANSPORT_TIMEOUT},
        {OAMR_TRANSPORT_ERROR, 0u, OAMR_TRANSPORT_ERROR},
        {OAMR_TRANSPORT_INVALID, 0u, OAMR_TRANSPORT_INVALID},
        {OAMR_TRANSPORT_OK, 5u, OAMR_TRANSPORT_ERROR}
    };
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        oamr_diagnostic_tx_t tx;
        fake_transport_t fake;
        uint8_t storage[4];
        uint8_t before[4];

        init_tx(&tx, storage, sizeof(storage), &fake);
        CHECK(oamr_diagnostic_tx_submit(&tx, "wxyz", 4u) == OAMR_STATUS_OK);
        memcpy(before, storage, sizeof(storage));
        fake_plan(&fake, cases[index].result, cases[index].written);
        CHECK(oamr_diagnostic_tx_flush_once(&tx) == cases[index].expected);
        CHECK(fake.calls == 1u);
        CHECK(tx.transport_errors == 1u);
        CHECK(tx.error_bytes == 4u);
        CHECK(tx.would_block_calls == 0u);
        CHECK(tx.dropped_records == 0u);
        CHECK(tx.transmitted_bytes == 0u);
        CHECK(tx.count == 4u);
        CHECK(tx.head == 0u);
        CHECK(memcmp(storage, before, sizeof(storage)) == 0);
    }
}

static void test_counter_saturation(void)
{
    oamr_diagnostic_tx_t tx;
    fake_transport_t fake;
    uint8_t storage[4];

    init_tx(&tx, storage, sizeof(storage), &fake);
    tx.dropped_records = UINT64_MAX;
    tx.dropped_bytes = UINT64_MAX - 1u;
    CHECK(oamr_diagnostic_tx_submit(&tx, "abcde", 5u) == OAMR_STATUS_OVERFLOW);
    CHECK(tx.dropped_records == UINT64_MAX);
    CHECK(tx.dropped_bytes == UINT64_MAX);
    tx.enqueued_records = UINT64_MAX;
    tx.enqueued_bytes = UINT64_MAX - 1u;
    CHECK(oamr_diagnostic_tx_submit(&tx, "xy", 2u) == OAMR_STATUS_OK);
    CHECK(tx.enqueued_records == UINT64_MAX);
    CHECK(tx.enqueued_bytes == UINT64_MAX);

    fake_plan(&fake, OAMR_TRANSPORT_WOULD_BLOCK, 0u);
    tx.would_block_calls = UINT64_MAX;
    tx.would_block_bytes = UINT64_MAX;
    CHECK(oamr_diagnostic_tx_flush_once(&tx) == OAMR_TRANSPORT_WOULD_BLOCK);
    CHECK(tx.would_block_calls == UINT64_MAX);
    CHECK(tx.would_block_bytes == UINT64_MAX);
    fake_plan(&fake, OAMR_TRANSPORT_ERROR, 0u);
    tx.transport_errors = UINT64_MAX;
    tx.error_bytes = UINT64_MAX - 1u;
    CHECK(oamr_diagnostic_tx_flush_once(&tx) == OAMR_TRANSPORT_ERROR);
    CHECK(tx.transport_errors == UINT64_MAX);
    CHECK(tx.error_bytes == UINT64_MAX);
    fake_plan(&fake, OAMR_TRANSPORT_OK, 2u);
    tx.transmitted_bytes = UINT64_MAX - 1u;
    CHECK(oamr_diagnostic_tx_flush_once(&tx) == OAMR_TRANSPORT_OK);
    CHECK(tx.transmitted_bytes == UINT64_MAX);
}

static void test_formatter_exact_outputs_and_numeric_names(void)
{
    oamr_diagnostic_tx_t tx;
    fake_transport_t fake;
    uint8_t storage[2048];
    oamr_diagnostic_boot_t boot = {"image-A", "H723-test", 0u};
    oamr_diagnostic_version_t version = {"fw-1.2.3", "deadbeef", "debug"};
    oamr_diagnostic_clock_t clock = {0u, UINT32_MAX, 120000000u, 60000000u};
    oamr_diagnostic_peripheral_t peripheral = {"fdcan2", OAMR_STATUS_TIMEOUT,
                                                UINT32_C(0x89abcdef)};
    oamr_reset_cause_t reset = {
        UINT32_C(0x90000005),
        OAMR_RESET_CAUSE_POWER_ON | OAMR_RESET_CAUSE_SOFTWARE |
            OAMR_RESET_CAUSE_BOR | OAMR_RESET_CAUSE_OTHER,
        UINT32_C(0x80000001)
    };
    static const char *const lines[] = {
        "OAMR BOOT image=image-A,board=H723-test,boot_sequence=0\r\n",
        "OAMR VERSION firmware=fw-1.2.3,git=deadbeef,configuration=debug\r\n",
        "OAMR CLOCK sysclk_hz=0,hclk_hz=4294967295,pclk1_hz=120000000,pclk2_hz=60000000\r\n",
        "OAMR PERIPH name=fdcan2,status=timeout,detail=0x89abcdef\r\n",
        "OAMR RESET raw_flags=0x90000005,causes=0x000000c5,unknown_flags=0x80000001,names=power_on|software|bor|other\r\n"
    };
    size_t index;

    init_tx(&tx, storage, sizeof(storage), &fake);
    CHECK(oamr_diagnostic_tx_report_boot(&tx, &boot) == OAMR_STATUS_OK);
    CHECK(oamr_diagnostic_tx_report_version(&tx, &version) == OAMR_STATUS_OK);
    CHECK(oamr_diagnostic_tx_report_clock(&tx, &clock) == OAMR_STATUS_OK);
    CHECK(oamr_diagnostic_tx_report_peripheral(&tx, &peripheral) == OAMR_STATUS_OK);
    CHECK(oamr_diagnostic_tx_report_reset(&tx, &reset) == OAMR_STATUS_OK);
    for (index = 0u; index < sizeof(lines) / sizeof(lines[0]); ++index) {
        const size_t expected_line_size = strlen(lines[index]);
        size_t remaining = tx.count;

        fake_plan(&fake, OAMR_TRANSPORT_OK, expected_line_size);
        CHECK(oamr_diagnostic_tx_flush_once(&tx) == OAMR_TRANSPORT_OK);
        expect_call(&fake, index, lines[index]);
        CHECK(expected_line_size <= remaining);
        CHECK(tx.count == remaining - expected_line_size);
    }
    CHECK(tx.enqueued_records == 5u);
    CHECK(tx.enqueued_bytes == tx.transmitted_bytes);

    reset.causes = OAMR_RESET_CAUSE_NONE;
    reset.raw_flags = 0u;
    reset.unknown_flags = 0u;
    CHECK(oamr_diagnostic_tx_report_reset(&tx, &reset) == OAMR_STATUS_OK);
    fake_plan(&fake, OAMR_TRANSPORT_OK,
              strlen("OAMR RESET raw_flags=0x00000000,causes=0x00000000,unknown_flags=0x00000000,names=none\r\n"));
    CHECK(oamr_diagnostic_tx_flush_once(&tx) == OAMR_TRANSPORT_OK);
    expect_call(&fake, 5u,
                "OAMR RESET raw_flags=0x00000000,causes=0x00000000,unknown_flags=0x00000000,names=none\r\n");

    reset.causes = UINT32_C(0xff);
    CHECK(oamr_diagnostic_tx_report_reset(&tx, &reset) == OAMR_STATUS_OK);
    fake_plan(&fake, OAMR_TRANSPORT_OK,
              strlen("OAMR RESET raw_flags=0x00000000,causes=0x000000ff,unknown_flags=0x00000000,names=power_on|pin|software|iwdg|wwdg|low_power|bor|other\r\n"));
    CHECK(oamr_diagnostic_tx_flush_once(&tx) == OAMR_TRANSPORT_OK);
    expect_call(&fake, 6u,
                "OAMR RESET raw_flags=0x00000000,causes=0x000000ff,unknown_flags=0x00000000,names=power_on|pin|software|iwdg|wwdg|low_power|bor|other\r\n");

    peripheral.status = OAMR_STATUS_OK;
    peripheral.detail = 0u;
    CHECK(oamr_diagnostic_tx_report_peripheral(&tx, &peripheral) == OAMR_STATUS_OK);
    fake_plan(&fake, OAMR_TRANSPORT_OK,
              strlen("OAMR PERIPH name=fdcan2,status=ok,detail=0x00000000\r\n"));
    CHECK(oamr_diagnostic_tx_flush_once(&tx) == OAMR_TRANSPORT_OK);
    expect_call(&fake, 7u,
                "OAMR PERIPH name=fdcan2,status=ok,detail=0x00000000\r\n");
    peripheral.status = (oamr_status_t)99;
    CHECK(oamr_diagnostic_tx_report_peripheral(&tx, &peripheral) == OAMR_STATUS_OK);
    fake_plan(&fake, OAMR_TRANSPORT_OK,
              strlen("OAMR PERIPH name=fdcan2,status=unknown,detail=0x00000000\r\n"));
    CHECK(oamr_diagnostic_tx_flush_once(&tx) == OAMR_TRANSPORT_OK);
    expect_call(&fake, 8u,
                "OAMR PERIPH name=fdcan2,status=unknown,detail=0x00000000\r\n");
}

static void test_formatter_rejects_absent_and_excessive_data(void)
{
    oamr_diagnostic_tx_t tx;
    fake_transport_t fake;
    uint8_t storage[32];
    oamr_diagnostic_boot_t boot = {"image", "board", 1u};
    oamr_diagnostic_version_t version = {"fw", "git", "config"};
    oamr_diagnostic_peripheral_t peripheral = {"uart", OAMR_STATUS_OK, 0u};
    oamr_reset_cause_t reset = {0u, 0u, 0u};
    oamr_fault_record_t fault;
    char long_string[520];
    uint64_t accepted_records;
    uint64_t accepted_bytes;

    memset(long_string, 'x', sizeof(long_string));
    long_string[sizeof(long_string) - 1u] = '\0';
    memset(&fault, 0, sizeof(fault));
    init_tx(&tx, storage, sizeof(storage), &fake);
    CHECK(oamr_diagnostic_tx_report_boot(&tx, NULL) == OAMR_STATUS_INVALID);
    boot.image = NULL;
    CHECK(oamr_diagnostic_tx_report_boot(&tx, &boot) == OAMR_STATUS_INVALID);
    boot.image = "image";
    boot.board = NULL;
    CHECK(oamr_diagnostic_tx_report_boot(&tx, &boot) == OAMR_STATUS_INVALID);
    CHECK(oamr_diagnostic_tx_report_version(&tx, NULL) == OAMR_STATUS_INVALID);
    version.git_revision = NULL;
    CHECK(oamr_diagnostic_tx_report_version(&tx, &version) == OAMR_STATUS_INVALID);
    CHECK(oamr_diagnostic_tx_report_clock(&tx, NULL) == OAMR_STATUS_INVALID);
    CHECK(oamr_diagnostic_tx_report_peripheral(&tx, NULL) == OAMR_STATUS_INVALID);
    peripheral.name = NULL;
    CHECK(oamr_diagnostic_tx_report_peripheral(&tx, &peripheral) == OAMR_STATUS_INVALID);
    CHECK(oamr_diagnostic_tx_report_reset(&tx, NULL) == OAMR_STATUS_INVALID);
    reset.causes = UINT32_C(0x100);
    CHECK(oamr_diagnostic_tx_report_reset(&tx, &reset) == OAMR_STATUS_INVALID);
    CHECK(oamr_diagnostic_tx_report_fault(&tx, NULL) == OAMR_STATUS_INVALID);
    CHECK(tx.formatting_errors == 11u);
    CHECK(tx.count == 0u);
    CHECK(fake.calls == 0u);

    boot.board = long_string;
    CHECK(oamr_diagnostic_tx_report_boot(&tx, &boot) == OAMR_STATUS_OVERFLOW);
    CHECK(tx.formatting_errors == 12u);
    CHECK(tx.count == 0u);
    boot.board = "board";
    boot.image = long_string;
    CHECK(oamr_diagnostic_tx_report_boot(&tx, &boot) == OAMR_STATUS_OVERFLOW);
    CHECK(tx.formatting_errors == 13u);
    CHECK(tx.count == 0u);
    version.git_revision = long_string;
    CHECK(oamr_diagnostic_tx_report_version(&tx, &version) == OAMR_STATUS_OVERFLOW);
    CHECK(tx.formatting_errors == 14u);
    CHECK(tx.count == 0u);

    /* Valid formatting followed by a capacity failure is not a formatting error. */
    boot.image = "image";
    accepted_records = tx.enqueued_records;
    accepted_bytes = tx.enqueued_bytes;
    CHECK(oamr_diagnostic_tx_report_boot(&tx, &boot) == OAMR_STATUS_OVERFLOW);
    CHECK(tx.formatting_errors == 14u);
    CHECK(tx.dropped_records == 1u);
    CHECK(tx.enqueued_records == accepted_records);
    CHECK(tx.enqueued_bytes == accepted_bytes);
    CHECK(tx.count == 0u);
    CHECK(fake.calls == 0u);
}

static void test_fault_formatter_golden_line(void)
{
    oamr_diagnostic_tx_t tx;
    fake_transport_t fake;
    uint8_t storage[512];
    oamr_fault_record_t fault;
    oamr_fault_record_data_t data;
    oamr_reset_flag_definitions_t definitions;
    data.raw_reset_flags = 0u;
    data.stacked_pc = UINT32_C(0x80000001);
    data.stacked_lr = UINT32_MAX;
    data.stacked_xpsr = UINT32_C(0x01000000);
    data.cfsr = UINT32_C(0x00008200);
    data.hfsr = UINT32_C(0x40000000);
    data.bfar = 0u;
    data.mmar = UINT32_MAX;
    memset(&definitions, 0, sizeof(definitions));
    definitions.power_on = 1u;
    definitions.pin = 2u;
    definitions.software = 4u;
    definitions.iwdg = 8u;
    definitions.wwdg = 16u;
    definitions.low_power = 32u;
    definitions.bor = 64u;
    definitions.other = 128u;
    init_tx(&tx, storage, sizeof(storage), &fake);
    CHECK(oamr_fault_record_init(&fault, &data, &definitions) == OAMR_STATUS_OK);
    CHECK(oamr_diagnostic_tx_report_fault(&tx, &fault) == OAMR_STATUS_OK);

    /* Full expected line, with the CRC computed by the already-tested encoder. */
    {
        static const char prefix[] =
            "OAMR FAULT magic=0x4f414652,version=1,length=44,"
            "raw_reset_flags=0x00000000,stacked_pc=0x80000001,"
            "stacked_lr=0xffffffff,stacked_xpsr=0x01000000,"
            "cfsr=0x00008200,hfsr=0x40000000,bfar=0x00000000,"
            "mmar=0xffffffff,reset_power_on=0x00000001,"
            "reset_pin=0x00000002,reset_software=0x00000004,"
            "reset_iwdg=0x00000008,reset_wwdg=0x00000010,"
            "reset_low_power=0x00000020,reset_bor=0x00000040,"
            "reset_other=0x00000080,crc32=0x";
        static const char digits[] = "0123456789abcdef";
        char golden[sizeof(prefix) + 12u];
        size_t size = sizeof(prefix) - 1u;
        unsigned shift;

        memcpy(golden, prefix, size);
        for (shift = 32u; shift > 0u; shift -= 4u) {
            golden[size] = digits[(fault.crc32 >> (shift - 4u)) & 0x0fu];
            ++size;
        }
        golden[size] = '\r';
        golden[size + 1u] = '\n';
        golden[size + 2u] = '\0';
        size += 2u;
        CHECK(size < 512u);
        fake_plan(&fake, OAMR_TRANSPORT_OK, size);
        CHECK(oamr_diagnostic_tx_flush_once(&tx) == OAMR_TRANSPORT_OK);
        expect_call(&fake, 0u, golden);
        CHECK(tx.count == 0u);
    }
    expect_empty(&tx, 1u);
}

static void test_broken_mock_is_detectable(void)
{
    oamr_diagnostic_tx_t tx;
    fake_transport_t fake;
    uint8_t storage[4];

    init_tx(&tx, storage, sizeof(storage), &fake);
    fake.broken = true;
    fake_plan(&fake, OAMR_TRANSPORT_OK, 1u);
    CHECK(oamr_diagnostic_tx_submit(&tx, "abc", 3u) == OAMR_STATUS_OK);
    CHECK(oamr_diagnostic_tx_flush_once(&tx) == OAMR_TRANSPORT_ERROR);
    CHECK(tx.count == 3u);
    CHECK(tx.transport_errors == 1u);
    CHECK(fake.calls == 1u);
    /* A broken fixture cannot masquerade as successful progress. */
    CHECK(fake.wire_size == 0u);
}

int main(void)
{
    RUN(test_init_and_snapshot_misuse);
    RUN(test_capacity_one_exact_fit_and_rejections);
    RUN(test_guarded_wrap_fifo_short_writes_and_drain);
    RUN(test_multiple_wraps_and_full_drain);
    RUN(test_backpressure_has_one_bounded_callback);
    RUN(test_transport_errors_and_malformed_transfers);
    RUN(test_counter_saturation);
    RUN(test_formatter_exact_outputs_and_numeric_names);
    RUN(test_fault_formatter_golden_line);
    RUN(test_formatter_rejects_absent_and_excessive_data);
    RUN(test_broken_mock_is_detectable);
    return TEST_END();
}
