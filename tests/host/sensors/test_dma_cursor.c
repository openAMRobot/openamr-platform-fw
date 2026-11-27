/* SPDX-License-Identifier: MIT */
#include "test_harness.h"

#include "oamr/dma_cursor.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void expect_empty(const oamr_dma_cursor_t *cursor)
{
    oamr_dma_chunk_t chunk = {123u, 456u};

    CHECK(oamr_dma_cursor_peek(cursor, &chunk) == OAMR_STATUS_EMPTY);
    CHECK(chunk.offset == 0u);
    CHECK(chunk.length == 0u);
    CHECK(oamr_dma_cursor_available(cursor) == 0u);
}

static void expect_chunk(const oamr_dma_cursor_t *cursor, size_t offset,
                         size_t length)
{
    oamr_dma_chunk_t chunk = {123u, 456u};

    CHECK(oamr_dma_cursor_peek(cursor, &chunk) == OAMR_STATUS_OK);
    CHECK(chunk.offset == offset);
    CHECK(chunk.length == length);
    CHECK(oamr_dma_cursor_available(cursor) == length);
}

static void copy_and_consume(oamr_dma_cursor_t *cursor, const uint8_t *buffer,
                             uint8_t *output, size_t output_capacity,
                             size_t *output_length, size_t expected_length)
{
    oamr_dma_chunk_t chunk = {0u, 0u};

    CHECK(oamr_dma_cursor_peek(cursor, &chunk) == OAMR_STATUS_OK);
    CHECK(chunk.length == expected_length);
    CHECK(*output_length + chunk.length <= output_capacity);
    if (*output_length + chunk.length <= output_capacity) {
        memcpy(output + *output_length, buffer + chunk.offset, chunk.length);
        *output_length += chunk.length;
    }
    CHECK(oamr_dma_cursor_consume(cursor, chunk.length) == OAMR_STATUS_OK);
}

static void initialization_nulls_and_invalid_configuration(void)
{
    oamr_dma_cursor_t cursor;
    oamr_dma_chunk_t chunk = {123u, 456u};

    CHECK(oamr_dma_cursor_init(NULL, 8u, UINT64_C(0)) == OAMR_STATUS_INVALID);
    CHECK(oamr_dma_cursor_init(&cursor, 0u, UINT64_C(0)) == OAMR_STATUS_INVALID);
    CHECK(!cursor.configured);
    CHECK(cursor.buffer_size == 0u);
    CHECK(oamr_dma_cursor_peek(&cursor, &chunk) == OAMR_STATUS_INVALID);
    CHECK(chunk.offset == 0u);
    CHECK(chunk.length == 0u);
    CHECK(oamr_dma_cursor_available(&cursor) == 0u);
    chunk.offset = 123u;
    chunk.length = 456u;
    CHECK(oamr_dma_cursor_peek(NULL, &chunk) == OAMR_STATUS_INVALID);
    CHECK(chunk.offset == 0u);
    CHECK(chunk.length == 0u);
    CHECK(oamr_dma_cursor_peek(&cursor, NULL) == OAMR_STATUS_INVALID);
    CHECK(oamr_dma_cursor_update_ndtr(NULL, 0u) == OAMR_STATUS_INVALID);
    CHECK(oamr_dma_cursor_update_absolute(NULL, 0u) == OAMR_STATUS_INVALID);
    CHECK(oamr_dma_cursor_consume(NULL, 0u) == OAMR_STATUS_INVALID);
    CHECK(oamr_dma_cursor_flush(NULL) == OAMR_STATUS_INVALID);
    CHECK(oamr_dma_cursor_available(NULL) == 0u);

    CHECK(oamr_dma_cursor_init(&cursor, 8u, UINT64_C(17)) == OAMR_STATUS_OK);
    CHECK(cursor.configured);
    CHECK(cursor.read_absolute == UINT64_C(17));
    CHECK(cursor.write_absolute == UINT64_C(17));
    CHECK(cursor.observed_position == 1u);
    expect_empty(&cursor);
    CHECK(oamr_dma_cursor_peek(&cursor, NULL) == OAMR_STATUS_INVALID);
}

static void ndtr_idle_half_full_and_exact_wrap_boundaries(void)
{
    oamr_dma_cursor_t cursor;

    CHECK(oamr_dma_cursor_init(&cursor, 8u, UINT64_C(0)) == OAMR_STATUS_OK);
    CHECK(oamr_dma_cursor_update_ndtr(&cursor, 8u) == OAMR_STATUS_OK);
    expect_empty(&cursor);

    /* NDTR=4 at half: producer is exactly four bytes ahead. */
    CHECK(oamr_dma_cursor_update_ndtr(&cursor, 4u) == OAMR_STATUS_OK);
    expect_chunk(&cursor, 0u, 4u);
    expect_chunk(&cursor, 0u, 4u);
    CHECK(cursor.write_absolute == UINT64_C(4));
    CHECK(cursor.read_absolute == UINT64_C(0));

    /* Consume exactly the half buffer; idle now means no duplicate bytes. */
    CHECK(oamr_dma_cursor_consume(&cursor, 4u) == OAMR_STATUS_OK);
    expect_empty(&cursor);
    CHECK(oamr_dma_cursor_update_ndtr(&cursor, 4u) == OAMR_STATUS_OK);
    expect_empty(&cursor);

    /* Four more bytes reach full buffer without changing observed position. */
    CHECK(oamr_dma_cursor_update_ndtr(&cursor, 0u) == OAMR_STATUS_OK);
    expect_chunk(&cursor, 4u, 4u);
    CHECK(cursor.write_absolute == UINT64_C(8));
    CHECK(oamr_dma_cursor_consume(&cursor, 4u) == OAMR_STATUS_OK);
    expect_empty(&cursor);

    /* NDTR=8 also aliases an unchanged wrapped zero: no duplicate bytes. */
    CHECK(oamr_dma_cursor_update_ndtr(&cursor, 8u) == OAMR_STATUS_OK);
    expect_empty(&cursor);
    CHECK(cursor.write_absolute == UINT64_C(8));
    CHECK(oamr_dma_cursor_update_ndtr(&cursor, 4u) == OAMR_STATUS_OK);
    expect_chunk(&cursor, 0u, 4u);
    CHECK(cursor.write_absolute == UINT64_C(12));
    CHECK(oamr_dma_cursor_update_ndtr(&cursor, 0u) == OAMR_STATUS_OK);
    expect_chunk(&cursor, 0u, 8u);
    CHECK(oamr_dma_cursor_consume(&cursor, 4u) == OAMR_STATUS_OK);
    expect_chunk(&cursor, 4u, 4u);
    CHECK(oamr_dma_cursor_consume(&cursor, 4u) == OAMR_STATUS_OK);
    expect_empty(&cursor);
    CHECK(cursor.read_absolute == UINT64_C(16));
    CHECK(cursor.write_absolute == UINT64_C(16));
}

static void wrapped_spans_are_contiguous_and_preserve_all_bytes(void)
{
    static const uint8_t source[] = {
        0x00u, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u,
        0x08u, 0x09u, 0x0au, 0x0bu, 0x0cu, 0x0du, 0x0eu, 0x0fu
    };
    oamr_dma_cursor_t cursor;
    uint8_t buffer[8] = {0};
    uint8_t output[12] = {0};
    size_t output_length = 0u;

    CHECK(oamr_dma_cursor_init(&cursor, sizeof(buffer), UINT64_C(0)) == OAMR_STATUS_OK);
    CHECK(oamr_dma_cursor_update_ndtr(&cursor, 2u) == OAMR_STATUS_OK);
    CHECK(oamr_dma_cursor_consume(&cursor, 6u) == OAMR_STATUS_OK);

    /* Absolute position 12 means six bytes at offsets 6,7,0,1,2,3. */
    CHECK(oamr_dma_cursor_update_ndtr(&cursor, 4u) == OAMR_STATUS_OK);
    CHECK(cursor.write_absolute == UINT64_C(12));
    CHECK(oamr_dma_cursor_available(&cursor) == 6u);
    buffer[6] = source[6];
    buffer[7] = source[7];
    buffer[0] = source[8];
    buffer[1] = source[9];
    buffer[2] = source[10];
    buffer[3] = source[11];
    copy_and_consume(&cursor, buffer, output, sizeof(output), &output_length, 2u);
    copy_and_consume(&cursor, buffer, output, sizeof(output), &output_length, 4u);
    CHECK(output_length == 6u);
    CHECK(memcmp(output, source + 6u, output_length) == 0);
    expect_empty(&cursor);

    CHECK(oamr_dma_cursor_update_ndtr(&cursor, 4u) == OAMR_STATUS_OK);
    expect_empty(&cursor);
}

static void invalid_and_backwards_cursors_change_no_positions(void)
{
    oamr_dma_cursor_t cursor;
    oamr_dma_cursor_t before;

    CHECK(oamr_dma_cursor_init(&cursor, 8u, UINT64_C(100)) == OAMR_STATUS_OK);
    CHECK(oamr_dma_cursor_update_absolute(&cursor, UINT64_C(104)) == OAMR_STATUS_OK);
    before = cursor;
    CHECK(oamr_dma_cursor_update_ndtr(&cursor, 9u) == OAMR_STATUS_INVALID);
    CHECK(cursor.observed_position == before.observed_position);
    CHECK(cursor.read_absolute == before.read_absolute);
    CHECK(cursor.write_absolute == before.write_absolute);
    CHECK(cursor.counters.invalid_updates == before.counters.invalid_updates + UINT64_C(1));

    before = cursor;
    CHECK(oamr_dma_cursor_update_absolute(&cursor, UINT64_C(99)) == OAMR_STATUS_INVALID);
    CHECK(cursor.observed_position == before.observed_position);
    CHECK(cursor.read_absolute == before.read_absolute);
    CHECK(cursor.write_absolute == before.write_absolute);
    CHECK(cursor.counters.invalid_updates == before.counters.invalid_updates + UINT64_C(1));
    expect_chunk(&cursor, 4u, 4u);

    CHECK(oamr_dma_cursor_update_absolute(&cursor, UINT64_C(104)) == OAMR_STATUS_OK);
    expect_chunk(&cursor, 4u, 4u);
    CHECK(oamr_dma_cursor_update_ndtr(&cursor, 8u) == OAMR_STATUS_OK);
    CHECK(cursor.counters.invalid_updates == before.counters.invalid_updates + UINT64_C(1));
}

static void consume_is_explicit_and_bounded_by_contiguous_span(void)
{
    oamr_dma_cursor_t cursor;
    oamr_dma_cursor_t before;

    CHECK(oamr_dma_cursor_init(&cursor, 8u, UINT64_C(0)) == OAMR_STATUS_OK);
    CHECK(oamr_dma_cursor_consume(&cursor, 0u) == OAMR_STATUS_INVALID);
    CHECK(oamr_dma_cursor_consume(&cursor, 1u) == OAMR_STATUS_INVALID);
    CHECK(oamr_dma_cursor_update_absolute(&cursor, UINT64_C(6)) == OAMR_STATUS_OK);
    expect_chunk(&cursor, 0u, 6u);
    before = cursor;
    CHECK(oamr_dma_cursor_consume(&cursor, 7u) == OAMR_STATUS_INVALID);
    CHECK(cursor.read_absolute == before.read_absolute);
    CHECK(cursor.write_absolute == before.write_absolute);
    CHECK(oamr_dma_cursor_consume(&cursor, 2u) == OAMR_STATUS_OK);
    CHECK(cursor.read_absolute == UINT64_C(2));
    CHECK(cursor.write_absolute == UINT64_C(6));
    expect_chunk(&cursor, 2u, 4u);
    CHECK(oamr_dma_cursor_consume(&cursor, 4u) == OAMR_STATUS_OK);
    expect_empty(&cursor);
}

static void overwrite_detects_gap_and_flush_discards_through_latest(void)
{
    oamr_dma_cursor_t cursor;
    oamr_dma_chunk_t chunk = {123u, 456u};

    CHECK(oamr_dma_cursor_init(&cursor, 8u, UINT64_C(0)) == OAMR_STATUS_OK);
    CHECK(oamr_dma_cursor_update_absolute(&cursor, UINT64_C(4)) == OAMR_STATUS_OK);
    expect_chunk(&cursor, 0u, 4u);

    /* Producer moves ten bytes: positions 0..1 were overwritten unread. */
    CHECK(oamr_dma_cursor_update_absolute(&cursor, UINT64_C(14)) == OAMR_STATUS_OVERFLOW);
    CHECK(cursor.resync_required);
    CHECK(cursor.read_absolute == UINT64_C(14));
    CHECK(cursor.write_absolute == UINT64_C(14));
    CHECK(cursor.counters.gaps == UINT64_C(1));
    CHECK(cursor.counters.discarded_bytes == UINT64_C(14));
    CHECK(oamr_dma_cursor_available(&cursor) == 0u);
    CHECK(oamr_dma_cursor_peek(&cursor, &chunk) == OAMR_STATUS_OVERFLOW);
    CHECK(chunk.offset == 0u);
    CHECK(chunk.length == 0u);
    CHECK(oamr_dma_cursor_consume(&cursor, 1u) == OAMR_STATUS_OVERFLOW);
    CHECK(cursor.read_absolute == UINT64_C(14));

    /* No retained suffix may leak while resync is latched. */
    CHECK(oamr_dma_cursor_update_absolute(&cursor, UINT64_C(17)) == OAMR_STATUS_OVERFLOW);
    CHECK(cursor.read_absolute == UINT64_C(17));
    CHECK(cursor.write_absolute == UINT64_C(17));
    CHECK(cursor.counters.gaps == UINT64_C(1));
    CHECK(oamr_dma_cursor_peek(&cursor, &chunk) == OAMR_STATUS_OVERFLOW);
    CHECK(oamr_dma_cursor_flush(&cursor) == OAMR_STATUS_OK);
    CHECK(!cursor.resync_required);
    CHECK(cursor.read_absolute == UINT64_C(17));
    CHECK(cursor.write_absolute == UINT64_C(17));
    expect_empty(&cursor);
    CHECK(oamr_dma_cursor_flush(&cursor) == OAMR_STATUS_OK);
}

static void producer_jump_over_one_buffer_is_detected_and_resynchronizes(void)
{
    oamr_dma_cursor_t cursor;

    CHECK(oamr_dma_cursor_init(&cursor, 8u, UINT64_C(0)) == OAMR_STATUS_OK);
    CHECK(oamr_dma_cursor_update_absolute(&cursor, UINT64_C(8)) == OAMR_STATUS_OK);
    expect_chunk(&cursor, 0u, 8u);
    CHECK(oamr_dma_cursor_consume(&cursor, 8u) == OAMR_STATUS_OK);
    expect_empty(&cursor);

    CHECK(oamr_dma_cursor_update_absolute(&cursor, UINT64_C(17)) == OAMR_STATUS_OVERFLOW);
    CHECK(cursor.counters.gaps == UINT64_C(1));
    CHECK(cursor.counters.discarded_bytes == UINT64_C(9));
    CHECK(cursor.read_absolute == UINT64_C(17));
    CHECK(cursor.write_absolute == UINT64_C(17));
    CHECK(oamr_dma_cursor_flush(&cursor) == OAMR_STATUS_OK);
    CHECK(oamr_dma_cursor_update_absolute(&cursor, UINT64_C(20)) == OAMR_STATUS_OK);
    expect_chunk(&cursor, 1u, 3u);
}

static void normal_large_absolute_progress_preserves_byte_stream(void)
{
    static const uint8_t bytes[] = {
        0xa0u, 0xa1u, 0xa2u, 0xa3u, 0xa4u, 0xa5u
    };
    oamr_dma_cursor_t cursor;
    uint8_t buffer[4];
    uint8_t output[sizeof(bytes)] = {0};
    size_t output_length = 0u;

    memcpy(buffer, bytes, sizeof(buffer));
    CHECK(oamr_dma_cursor_init(&cursor, sizeof(buffer), UINT64_C(0)) == OAMR_STATUS_OK);
    CHECK(oamr_dma_cursor_update_absolute(&cursor, UINT64_C(4)) == OAMR_STATUS_OK);
    copy_and_consume(&cursor, buffer, output, sizeof(output), &output_length, 4u);

    memcpy(buffer, bytes + 4u, 2u);
    CHECK(oamr_dma_cursor_update_absolute(&cursor, UINT64_C(6)) == OAMR_STATUS_OK);
    copy_and_consume(&cursor, buffer, output, sizeof(output), &output_length, 2u);
    CHECK(output_length == sizeof(bytes));
    CHECK(memcmp(output, bytes, sizeof(bytes)) == 0);
    CHECK(cursor.counters.gaps == UINT64_C(0));
    CHECK(cursor.counters.overruns == UINT64_C(0));
    CHECK(cursor.counters.discarded_bytes == UINT64_C(0));
    expect_empty(&cursor);
}

static void explicit_flush_discards_only_unread_bytes(void)
{
    oamr_dma_cursor_t cursor;

    CHECK(oamr_dma_cursor_init(&cursor, 8u, UINT64_C(0)) == OAMR_STATUS_OK);
    CHECK(oamr_dma_cursor_update_absolute(&cursor, UINT64_C(6)) == OAMR_STATUS_OK);
    CHECK(oamr_dma_cursor_consume(&cursor, 2u) == OAMR_STATUS_OK);
    CHECK(oamr_dma_cursor_flush(&cursor) == OAMR_STATUS_OK);
    CHECK(cursor.read_absolute == UINT64_C(6));
    CHECK(cursor.write_absolute == UINT64_C(6));
    CHECK(cursor.counters.discarded_bytes == UINT64_C(4));
    expect_empty(&cursor);
    CHECK(oamr_dma_cursor_update_absolute(&cursor, UINT64_C(7)) == OAMR_STATUS_OK);
    expect_chunk(&cursor, 6u, 1u);
}

int main(void)
{
    RUN(initialization_nulls_and_invalid_configuration);
    RUN(ndtr_idle_half_full_and_exact_wrap_boundaries);
    RUN(wrapped_spans_are_contiguous_and_preserve_all_bytes);
    RUN(invalid_and_backwards_cursors_change_no_positions);
    RUN(consume_is_explicit_and_bounded_by_contiguous_span);
    RUN(overwrite_detects_gap_and_flush_discards_through_latest);
    RUN(producer_jump_over_one_buffer_is_detected_and_resynchronizes);
    RUN(normal_large_absolute_progress_preserves_byte_stream);
    RUN(explicit_flush_discards_only_unread_bytes);
    return TEST_END();
}
