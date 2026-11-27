/* SPDX-License-Identifier: MIT */
#include "test_harness.h"

#include "fault_reporter.h"

#include <stdio.h>
#include <string.h>

#define WIRE_CAPACITY ((size_t)8192U)
#define CALL_CAPACITY ((size_t)64U)

typedef struct {
    oamr_transport_result_t result[CALL_CAPACITY];
    size_t accepted[CALL_CAPACITY];
    size_t calls;
    size_t requests[CALL_CAPACITY];
    uint8_t wire[WIRE_CAPACITY];
    size_t wire_size;
    unsigned retention_clear_calls;
} fake_transport_t;

static oamr_transport_result_t fake_write(void *context, const uint8_t *data,
                                          size_t data_size, size_t *transferred)
{
    fake_transport_t *fake = (fake_transport_t *)context;
    size_t call;
    size_t accepted;

    CHECK(fake != NULL);
    CHECK(data != NULL);
    CHECK(transferred != NULL);
    if (transferred == NULL) {
        return OAMR_TRANSPORT_INVALID;
    }
    *transferred = 0u;
    if (fake == NULL || data == NULL || fake->calls >= CALL_CAPACITY) {
        return OAMR_TRANSPORT_INVALID;
    }
    call = fake->calls++;
    fake->requests[call] = data_size;
    accepted = fake->accepted[call];
    if (accepted > data_size) {
        accepted = data_size;
    }
    if (fake->result[call] == OAMR_TRANSPORT_OK && accepted != 0u) {
        CHECK(fake->wire_size + accepted <= sizeof(fake->wire));
        if (fake->wire_size + accepted <= sizeof(fake->wire)) {
            memcpy(fake->wire + fake->wire_size, data, accepted);
            fake->wire_size += accepted;
        }
        *transferred = accepted;
    }
    return fake->result[call];
}

static void init_reporter(fault_reporter_t *reporter, fake_transport_t *fake,
                          oamr_transport_result_t result, size_t accepted)
{
    size_t index;

    memset(fake, 0, sizeof(*fake));
    for (index = 0u; index < CALL_CAPACITY; ++index) {
        fake->result[index] = result;
        fake->accepted[index] = accepted;
    }
    CHECK(fault_reporter_init(reporter, fake_write, fake));
}

static oamr_reset_flag_definitions_t definitions(void)
{
    oamr_reset_flag_definitions_t value;

    value.power_on = UINT32_C(0x00800000);
    value.pin = UINT32_C(0x00400000);
    value.software = UINT32_C(0x01000000);
    value.iwdg = UINT32_C(0x04000000);
    value.wwdg = UINT32_C(0x10000000);
    value.low_power = UINT32_C(0x40000000);
    value.bor = UINT32_C(0x00200000);
    value.other = UINT32_C(0x00020000);
    return value;
}

static oamr_fault_record_t sample_record(void)
{
    oamr_fault_record_t record;
    oamr_fault_record_data_t data;
    oamr_reset_flag_definitions_t reset_definitions = definitions();

    data.raw_reset_flags = UINT32_C(0x04c00000);
    data.stacked_pc = UINT32_C(0x08001235);
    data.stacked_lr = UINT32_C(0xfffffff9);
    data.stacked_xpsr = UINT32_C(0x01000000);
    data.cfsr = UINT32_C(0x00008200);
    data.hfsr = UINT32_C(0x40000000);
    data.bfar = UINT32_C(0x20000004);
    data.mmar = UINT32_C(0x30000008);
    CHECK(oamr_fault_record_init(&record, &data, &reset_definitions) == OAMR_STATUS_OK);
    return record;
}

static oamr_exception_capture_metadata_t metadata(void)
{
    oamr_exception_capture_metadata_t value;

    value.sequence = UINT32_C(0x10203040);
    value.exc_return = UINT32_C(0xffffffed);
    value.exception_kind = 1u;
    value.flags = UINT16_C(0x0007);
    return value;
}

static void make_expected(char *output, size_t capacity,
                          const oamr_fault_record_t *record,
                          const oamr_exception_capture_metadata_t *capture)
{
    oamr_diagnostic_tx_t tx;
    oamr_transport_t transport;
    uint8_t formatted[512];
    int length;
    oamr_transport_result_t (*unused_write)(void *, const uint8_t *, size_t, size_t *) = fake_write;
    fake_transport_t unused;

    memset(&unused, 0, sizeof(unused));
    transport.context = &unused;
    transport.read = NULL;
    transport.write = unused_write;
    CHECK(oamr_diagnostic_tx_init(&tx, formatted, sizeof(formatted), &transport) ==
          OAMR_STATUS_OK);
    CHECK(oamr_diagnostic_tx_report_fault(&tx, record) == OAMR_STATUS_OK);
    length = snprintf(output, capacity,
                      "OAMR CAPTURE sequence=%lu,kind=%u,exc_return=0x%08lX,flags=0x%04X\r\n",
                      (unsigned long)capture->sequence, (unsigned)capture->exception_kind,
                      (unsigned long)capture->exc_return, (unsigned)capture->flags);
    CHECK(length > 0);
    CHECK((size_t)length + tx.count < capacity);
    if (length > 0 && (size_t)length + tx.count < capacity) {
        memcpy(output + length, tx.storage, tx.count);
        output[length + (int)tx.count] = '\0';
    }
}

static void test_init_absent_invalid_and_encoded_validation(void)
{
    fault_reporter_t reporter;
    fake_transport_t fake;
    oamr_fault_record_t record = sample_record();
    oamr_exception_capture_metadata_t capture = metadata();
    oamr_reset_flag_definitions_t reset_definitions = definitions();
    uint8_t encoded[OAMR_FAULT_RECORD_SERIALIZED_SIZE];
    size_t encoded_size = 0u;

    memset(&fake, 0, sizeof(fake));
    CHECK(!fault_reporter_init(NULL, fake_write, &fake));
    memset(&reporter, 0, sizeof(reporter));
    CHECK(!fault_reporter_init(&reporter, NULL, &fake));
    init_reporter(&reporter, &fake, OAMR_TRANSPORT_OK, 1024u);
    CHECK(!fault_reporter_pending(&reporter));
    CHECK(!fault_reporter_enqueued(&reporter));
    CHECK(fault_reporter_poll(&reporter) == FAULT_REPORTER_EMPTY);
    CHECK(!fault_reporter_submit(&reporter, NULL, &capture));
    CHECK(!fault_reporter_submit(&reporter, &record, NULL));
    capture.exception_kind = 0u;
    CHECK(!fault_reporter_submit(&reporter, &record, &capture));
    CHECK(!fault_reporter_submit_encoded(&reporter, encoded, 0u,
                                         &reset_definitions, &capture));
    CHECK(!fault_reporter_submit_encoded(&reporter, encoded,
                                         sizeof(encoded), NULL, &capture));

    capture = metadata();
    CHECK(oamr_fault_record_encode(&record, encoded, sizeof(encoded), &encoded_size) ==
          OAMR_STATUS_OK);
    encoded[10] ^= 1u;
    CHECK(!fault_reporter_submit_encoded(&reporter, encoded, encoded_size,
                                         &reset_definitions, &capture));
    encoded[10] ^= 1u;
    CHECK(fault_reporter_submit_encoded(&reporter, encoded, encoded_size,
                                        &reset_definitions, &capture));
    CHECK(reporter.pending);
    CHECK(reporter.record.reset_definitions.power_on == reset_definitions.power_on);
    CHECK(!fault_reporter_submit(&reporter, &record, &capture));
    CHECK(fault_reporter_pending(NULL) == false);
    CHECK(fault_reporter_enqueued(NULL) == false);
    CHECK(fault_reporter_poll(NULL) == FAULT_REPORTER_INVALID);
    CHECK(fake.retention_clear_calls == 0u);
}

static void test_valid_exact_stream_and_complete_without_duplicates(void)
{
    fault_reporter_t reporter;
    fake_transport_t fake;
    oamr_fault_record_t record = sample_record();
    oamr_exception_capture_metadata_t capture = metadata();
    char expected[1024];

    init_reporter(&reporter, &fake, OAMR_TRANSPORT_OK, 1024u);
    make_expected(expected, sizeof(expected), &record, &capture);
    CHECK(fault_reporter_submit(&reporter, &record, &capture));
    CHECK(fault_reporter_poll(&reporter) == FAULT_REPORTER_OK);
    CHECK(reporter.metadata_enqueued);
    CHECK(!reporter.payload_enqueued);
    CHECK(fault_reporter_pending(&reporter));
    CHECK(!fault_reporter_enqueued(&reporter));
    CHECK(fault_reporter_poll(&reporter) == FAULT_REPORTER_OK);
    CHECK(reporter.payload_enqueued);
    CHECK(!fault_reporter_pending(&reporter));
    CHECK(!fault_reporter_enqueued(&reporter));
    CHECK(fake.wire_size == strlen(expected));
    CHECK(memcmp(fake.wire, expected, strlen(expected)) == 0);
    CHECK(strstr((const char *)fake.wire, "OAMR FAULT ") != NULL);
    CHECK(fake.calls == 2u);
    CHECK(fault_reporter_poll(&reporter) == FAULT_REPORTER_EMPTY);
    CHECK(fake.calls == 2u);
    CHECK(fake.wire_size == strlen(expected));
    CHECK(fake.retention_clear_calls == 0u);
}

static void test_queue_full_retry_does_not_duplicate_metadata(void)
{
    fault_reporter_t reporter;
    fake_transport_t fake;
    oamr_fault_record_t record = sample_record();
    oamr_exception_capture_metadata_t capture = metadata();
    char expected[1024];
    size_t expected_size;
    size_t calls_after_metadata;

    init_reporter(&reporter, &fake, OAMR_TRANSPORT_OK, 0u);
    make_expected(expected, sizeof(expected), &record, &capture);
    expected_size = strlen(expected);
    CHECK(fault_reporter_submit(&reporter, &record, &capture));
    CHECK(fault_reporter_poll(&reporter) == FAULT_REPORTER_WOULD_BLOCK);
    calls_after_metadata = fake.calls;

    CHECK(fault_reporter_poll(&reporter) == FAULT_REPORTER_WOULD_BLOCK);
    CHECK(reporter.metadata_enqueued);
    CHECK(reporter.payload_enqueued);
    CHECK(fake.calls == calls_after_metadata + 1u);

    fake.result[2] = OAMR_TRANSPORT_OK;
    fake.accepted[2] = 1024u;
    CHECK(fault_reporter_poll(&reporter) == FAULT_REPORTER_OK);
    CHECK(reporter.payload_enqueued);
    CHECK(!fault_reporter_pending(&reporter));
    CHECK(fake.wire_size == expected_size);

    while (fault_reporter_pending(&reporter)) {
        fake.result[fake.calls] = OAMR_TRANSPORT_OK;
        fake.accepted[fake.calls] = 1024u;
        (void)fault_reporter_poll(&reporter);
    }
    CHECK(fake.wire_size == expected_size);
    CHECK(memcmp(fake.wire, expected, expected_size) == 0);
    CHECK(fake.retention_clear_calls == 0u);
}

static void test_would_block_then_retry_and_transport_errors(void)
{
    fault_reporter_t reporter;
    fake_transport_t fake;
    oamr_fault_record_t record = sample_record();
    oamr_exception_capture_metadata_t capture = metadata();

    init_reporter(&reporter, &fake, OAMR_TRANSPORT_WOULD_BLOCK, 0u);
    CHECK(fault_reporter_submit(&reporter, &record, &capture));
    CHECK(fault_reporter_poll(&reporter) == FAULT_REPORTER_WOULD_BLOCK);
    CHECK(fake.calls == 1u);
    CHECK(reporter.tx.count != 0u);
    CHECK(reporter.metadata_enqueued);
    CHECK(fault_reporter_poll(&reporter) == FAULT_REPORTER_WOULD_BLOCK);
    CHECK(fake.calls == 2u);
    CHECK(reporter.metadata_enqueued);

    fake.result[2] = OAMR_TRANSPORT_OK;
    fake.accepted[2] = 1024u;
    CHECK(fault_reporter_poll(&reporter) == FAULT_REPORTER_OK);
    CHECK(reporter.metadata_enqueued);
    CHECK(reporter.payload_enqueued);
    /* With both atomic reports accepted, polling is complete. The separately
     * registered transport-error test covers failure without data loss. */
    CHECK(!reporter.pending);
    CHECK(fault_reporter_poll(&reporter) == FAULT_REPORTER_EMPTY);
    CHECK(reporter.tx.transport_errors == 0u);
    CHECK(fake.retention_clear_calls == 0u);
}

static void test_payload_transport_error_retains_pending_bytes(void)
{
    fault_reporter_t reporter;
    fake_transport_t fake;
    oamr_fault_record_t record = sample_record();
    oamr_exception_capture_metadata_t capture = metadata();
    fault_reporter_result_t result;

    init_reporter(&reporter, &fake, OAMR_TRANSPORT_WOULD_BLOCK, 0u);
    CHECK(fault_reporter_submit(&reporter, &record, &capture));
    CHECK(fault_reporter_poll(&reporter) == FAULT_REPORTER_WOULD_BLOCK);
    CHECK(reporter.metadata_enqueued);
    CHECK(!reporter.payload_enqueued);
    CHECK(reporter.tx.count != 0u);

    /* The second poll stages the payload but cannot drain it yet. */
    fake.result[1] = OAMR_TRANSPORT_WOULD_BLOCK;
    fake.accepted[1] = 0u;
    CHECK(fault_reporter_poll(&reporter) == FAULT_REPORTER_WOULD_BLOCK);
    CHECK(reporter.payload_enqueued);
    CHECK(reporter.pending);

    /* Fail the payload drain, then successfully drain it on a later poll. */
    fake.result[2] = OAMR_TRANSPORT_ERROR;
    fake.accepted[2] = 0u;
    result = fault_reporter_poll(&reporter);
    CHECK(result == FAULT_REPORTER_ERROR);
    CHECK(reporter.tx.count != 0u);
    CHECK(reporter.tx.transport_errors == 1u);
    CHECK(reporter.pending);
    CHECK(fake.retention_clear_calls == 0u);

    fake.result[3] = OAMR_TRANSPORT_OK;
    fake.accepted[3] = 1024u;
    CHECK(fault_reporter_poll(&reporter) == FAULT_REPORTER_OK);
    CHECK(!reporter.pending);
    CHECK(fake.retention_clear_calls == 0u);
}

static void test_large_fault_line_waits_for_whole_record_capacity(void)
{
    fault_reporter_t reporter;
    fake_transport_t fake;
    oamr_fault_record_t record = sample_record();
    oamr_exception_capture_metadata_t capture = metadata();
    char expected[1024];
    size_t expected_size;

    init_reporter(&reporter, &fake, OAMR_TRANSPORT_WOULD_BLOCK, 0u);
    make_expected(expected, sizeof(expected), &record, &capture);
    expected_size = strlen(expected);
    CHECK(expected_size > 160u);
    CHECK(fault_reporter_submit(&reporter, &record, &capture));

    /* A long fault line never partially leaves staging under backpressure. */
    CHECK(fault_reporter_poll(&reporter) == FAULT_REPORTER_WOULD_BLOCK);
    CHECK(reporter.metadata_enqueued);
    CHECK(!reporter.payload_enqueued);
    CHECK(reporter.tx.count != 0u);
    CHECK(fake.wire_size == 0u);
    CHECK(reporter.pending);

    /* Complete lines are submitted once; a blocked second line is retried. */
    fake.result[1] = OAMR_TRANSPORT_WOULD_BLOCK;
    CHECK(fault_reporter_poll(&reporter) == FAULT_REPORTER_WOULD_BLOCK);
    CHECK(reporter.payload_enqueued);
    CHECK(reporter.tx.count != 0u);
    CHECK(reporter.pending);

    fake.result[2] = OAMR_TRANSPORT_OK;
    fake.accepted[2] = 1024u;
    CHECK(fault_reporter_poll(&reporter) == FAULT_REPORTER_OK);
    CHECK(!reporter.pending);
    CHECK(fake.wire_size == expected_size);
    CHECK(memcmp(fake.wire, expected, expected_size) == 0);
    CHECK(fake.retention_clear_calls == 0u);
}

int main(void)
{
    RUN(test_init_absent_invalid_and_encoded_validation);
    RUN(test_valid_exact_stream_and_complete_without_duplicates);
    RUN(test_queue_full_retry_does_not_duplicate_metadata);
    RUN(test_would_block_then_retry_and_transport_errors);
    RUN(test_payload_transport_error_retains_pending_bytes);
    RUN(test_large_fault_line_waits_for_whole_record_capacity);
    return TEST_END();
}
