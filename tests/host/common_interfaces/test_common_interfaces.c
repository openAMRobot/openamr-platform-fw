/* SPDX-License-Identifier: MIT */
#include "test_harness.h"

#include "oamr/bounded_queue.h"
#include "oamr/can_frame.h"
#include "oamr/freshness.h"
#include "oamr/sample.h"
#include "oamr/status.h"
#include "oamr/time.h"
#include "oamr/transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

typedef struct {
    uint8_t bytes[5];
    uint16_t tag;
} test_element_t;

static void queue_expect_zeroed(const oamr_bounded_queue_t *queue)
{
    CHECK(queue->storage == NULL);
    CHECK(queue->storage_size == 0u);
    CHECK(queue->element_size == 0u);
    CHECK(queue->capacity == 0u);
    CHECK(queue->head == 0u);
    CHECK(queue->tail == 0u);
    CHECK(queue->count == 0u);
    CHECK(queue->overflow_count == 0u);
    CHECK(!queue->initialized);
}

static void status_zero_is_not_success(void)
{
    oamr_status_t status = OAMR_STATUS_INVALID;
    oamr_sample_t sample = {0};

    memset(&status, 0, sizeof(status));
    CHECK(status == OAMR_STATUS_INVALID);
    CHECK(status != OAMR_STATUS_OK);
    CHECK(sample.status == OAMR_STATUS_INVALID);
}

static void time_initialization_and_first_timestamp(void)
{
    oamr_time_validator_t validator;

    memset(&validator, 0xa5, sizeof(validator));
    oamr_time_validator_init(NULL);
    oamr_time_validator_init(&validator);
    CHECK(!validator.has_timestamp);
    CHECK(validator.last_timestamp_us == 0u);

    CHECK(oamr_time_validate_monotonic(NULL, UINT64_C(123)) == OAMR_STATUS_INVALID);
    CHECK(!validator.has_timestamp);
    CHECK(validator.last_timestamp_us == 0u);

    CHECK(oamr_time_validate_monotonic(&validator, UINT64_C(0)) == OAMR_STATUS_OK);
    CHECK(validator.has_timestamp);
    CHECK(validator.last_timestamp_us == UINT64_C(0));

    oamr_time_validator_init(&validator);
    CHECK(oamr_time_validate_monotonic(&validator, UINT64_MAX) == OAMR_STATUS_OK);
    CHECK(validator.has_timestamp);
    CHECK(validator.last_timestamp_us == UINT64_MAX);
}

static void time_accepts_uint64_progress_beyond_32_bits(void)
{
    static const uint64_t timestamps[] = {
        UINT64_C(0),
        UINT64_C(0xfffffffe),
        UINT64_C(0xffffffff),
        UINT64_C(0x100000000),
        UINT64_C(0x100000001),
        UINT64_C(0x1ffffffff),
        UINT64_C(0x200000000),
        UINT64_MAX
    };
    oamr_time_validator_t validator;
    size_t index;

    oamr_time_validator_init(&validator);
    for (index = 0u; index < ARRAY_SIZE(timestamps); ++index) {
        CHECK(oamr_time_validate_monotonic(&validator, timestamps[index]) == OAMR_STATUS_OK);
        CHECK(validator.has_timestamp);
        CHECK(validator.last_timestamp_us == timestamps[index]);
        CHECK(oamr_time_validate_monotonic(&validator, timestamps[index]) == OAMR_STATUS_OK);
        CHECK(validator.last_timestamp_us == timestamps[index]);
    }
}

static void time_rejects_decrease_and_preserves_last_accepted(void)
{
    oamr_time_validator_t validator;

    oamr_time_validator_init(&validator);
    CHECK(oamr_time_validate_monotonic(&validator, UINT64_C(0x100000000)) == OAMR_STATUS_OK);
    CHECK(oamr_time_validate_monotonic(&validator, UINT64_C(0xffffffff)) == OAMR_STATUS_INVALID);
    CHECK(validator.has_timestamp);
    CHECK(validator.last_timestamp_us == UINT64_C(0x100000000));

    CHECK(oamr_time_validate_monotonic(&validator, UINT64_C(0x100000000)) == OAMR_STATUS_OK);
    CHECK(oamr_time_validate_monotonic(&validator, UINT64_MAX) == OAMR_STATUS_OK);
    CHECK(oamr_time_validate_monotonic(&validator, UINT64_MAX - UINT64_C(1)) == OAMR_STATUS_INVALID);
    CHECK(validator.last_timestamp_us == UINT64_MAX);
    CHECK(oamr_time_validate_monotonic(&validator, UINT64_C(0)) == OAMR_STATUS_INVALID);
    CHECK(validator.last_timestamp_us == UINT64_MAX);

    oamr_time_validator_init(&validator);
    CHECK(!validator.has_timestamp);
    CHECK(validator.last_timestamp_us == 0u);
    CHECK(oamr_time_validate_monotonic(&validator, UINT64_C(0)) == OAMR_STATUS_OK);
}

static void freshness_missing_future_and_zero_age(void)
{
    CHECK(oamr_freshness_check(UINT64_C(10), false, UINT64_C(10), UINT64_C(0)) == OAMR_STATUS_INVALID);
    CHECK(oamr_freshness_check(UINT64_C(10), false, UINT64_C(11), UINT64_MAX) == OAMR_STATUS_INVALID);
    CHECK(oamr_freshness_check(UINT64_C(10), false, UINT64_C(9), UINT64_C(0)) == OAMR_STATUS_INVALID);
    CHECK(oamr_freshness_check(UINT64_C(11), true, UINT64_C(10), UINT64_MAX) == OAMR_STATUS_INVALID);
    CHECK(oamr_freshness_check(UINT64_MAX, true, UINT64_C(0), UINT64_MAX) == OAMR_STATUS_INVALID);

    CHECK(oamr_freshness_check(UINT64_C(0), true, UINT64_C(0), UINT64_C(0)) == OAMR_STATUS_OK);
    CHECK(oamr_freshness_check(UINT64_C(10), true, UINT64_C(10), UINT64_C(0)) == OAMR_STATUS_OK);
    CHECK(oamr_freshness_check(UINT64_C(10), true, UINT64_C(11), UINT64_C(0)) == OAMR_STATUS_STALE);
}

static void freshness_exact_age_boundary(void)
{
    CHECK(oamr_freshness_check(UINT64_C(100), true, UINT64_C(199), UINT64_C(100)) == OAMR_STATUS_OK);
    CHECK(oamr_freshness_check(UINT64_C(100), true, UINT64_C(200), UINT64_C(100)) == OAMR_STATUS_OK);
    CHECK(oamr_freshness_check(UINT64_C(100), true, UINT64_C(201), UINT64_C(100)) == OAMR_STATUS_STALE);

    CHECK(oamr_freshness_check(UINT64_C(1), true, UINT64_C(2), UINT64_C(1)) == OAMR_STATUS_OK);
    CHECK(oamr_freshness_check(UINT64_C(0), true, UINT64_C(2), UINT64_C(1)) == OAMR_STATUS_STALE);
    CHECK(oamr_freshness_check(UINT64_C(1), true, UINT64_MAX, UINT64_MAX - UINT64_C(1)) == OAMR_STATUS_OK);
    CHECK(oamr_freshness_check(UINT64_C(0), true, UINT64_MAX, UINT64_MAX - UINT64_C(1)) == OAMR_STATUS_STALE);
}

static void freshness_uint64_extremes(void)
{
    CHECK(oamr_freshness_check(UINT64_C(0), true, UINT64_MAX, UINT64_MAX) == OAMR_STATUS_OK);
    CHECK(oamr_freshness_check(UINT64_MAX, true, UINT64_MAX, UINT64_C(0)) == OAMR_STATUS_OK);
    CHECK(oamr_freshness_check(UINT64_MAX - UINT64_C(1), true, UINT64_MAX, UINT64_C(1)) == OAMR_STATUS_OK);
    CHECK(oamr_freshness_check(UINT64_MAX - UINT64_C(2), true, UINT64_MAX, UINT64_C(1)) == OAMR_STATUS_STALE);
    CHECK(oamr_freshness_check(UINT64_MAX, true, UINT64_MAX - UINT64_C(1), UINT64_MAX) == OAMR_STATUS_INVALID);
    CHECK(oamr_freshness_check(UINT64_C(0), false, UINT64_MAX, UINT64_MAX) == OAMR_STATUS_INVALID);
}

static void queue_rejects_invalid_initialization_without_erasing_storage(void)
{
    oamr_bounded_queue_t queue;
    uint8_t storage[17];
    size_t index;

    for (index = 0u; index < ARRAY_SIZE(storage); ++index) {
        storage[index] = (uint8_t)(index + 1u);
    }
    memset(&queue, 0xa5, sizeof(queue));
    CHECK(oamr_bounded_queue_init(NULL, storage, sizeof(storage), 1u, 1u) == OAMR_STATUS_INVALID);

    CHECK(oamr_bounded_queue_init(&queue, NULL, sizeof(storage), 1u, 1u) == OAMR_STATUS_INVALID);
    queue_expect_zeroed(&queue);
    CHECK(oamr_bounded_queue_init(&queue, storage, 0u, 1u, 1u) == OAMR_STATUS_INVALID);
    queue_expect_zeroed(&queue);
    CHECK(oamr_bounded_queue_init(&queue, storage, sizeof(storage), 0u, 1u) == OAMR_STATUS_INVALID);
    queue_expect_zeroed(&queue);
    CHECK(oamr_bounded_queue_init(&queue, storage, sizeof(storage), 1u, 0u) == OAMR_STATUS_INVALID);
    queue_expect_zeroed(&queue);

    CHECK(oamr_bounded_queue_init(&queue, storage, sizeof(test_element_t) * 2u - 1u,
                                  sizeof(test_element_t), 2u) == OAMR_STATUS_INVALID);
    queue_expect_zeroed(&queue);
    CHECK(oamr_bounded_queue_init(&queue, storage, SIZE_MAX, 2u, SIZE_MAX / 2u + 1u) == OAMR_STATUS_INVALID);
    queue_expect_zeroed(&queue);
    CHECK(oamr_bounded_queue_init(&queue, storage, SIZE_MAX, SIZE_MAX, 2u) == OAMR_STATUS_INVALID);
    queue_expect_zeroed(&queue);
    CHECK(oamr_bounded_queue_init(&queue, storage, SIZE_MAX, SIZE_MAX / 2u + 1u, 2u) == OAMR_STATUS_INVALID);
    queue_expect_zeroed(&queue);
    CHECK(oamr_bounded_queue_init(&queue, storage, 1u, SIZE_MAX, SIZE_MAX) == OAMR_STATUS_INVALID);
    queue_expect_zeroed(&queue);
    CHECK(oamr_bounded_queue_init(&queue, storage, 1u, SIZE_MAX - 1u, 2u) == OAMR_STATUS_INVALID);
    queue_expect_zeroed(&queue);

    for (index = 0u; index < ARRAY_SIZE(storage); ++index) {
        CHECK(storage[index] == (uint8_t)(index + 1u));
    }
}

static void queue_initializes_exact_and_extra_external_storage(void)
{
    oamr_bounded_queue_t queue;
    test_element_t storage[2] = {0};
    uint8_t bytes[6] = {0};
    uint8_t guard[2] = {0xa5u, 0x5au};
    uint8_t input[2] = {0x11u, 0x22u};
    uint8_t output[2] = {0u, 0u};

    CHECK(oamr_bounded_queue_init(&queue, storage, sizeof(storage), sizeof(storage[0]), 2u) == OAMR_STATUS_OK);
    CHECK(queue.storage == (uint8_t *)storage);
    CHECK(queue.storage_size == sizeof(storage));
    CHECK(queue.element_size == sizeof(storage[0]));
    CHECK(oamr_bounded_queue_capacity(&queue) == 2u);
    CHECK(oamr_bounded_queue_count(&queue) == 0u);
    CHECK(oamr_bounded_queue_overflow_count(&queue) == 0u);
    CHECK(oamr_bounded_queue_reset(&queue) == OAMR_STATUS_OK);

    CHECK(oamr_bounded_queue_init(&queue, bytes, sizeof(bytes), 2u, 2u) == OAMR_STATUS_OK);
    CHECK(oamr_bounded_queue_capacity(&queue) == 2u);
    CHECK(oamr_bounded_queue_push(&queue, input) == OAMR_STATUS_OK);
    CHECK(oamr_bounded_queue_push(&queue, input) == OAMR_STATUS_OK);
    CHECK(bytes[4] == 0u);
    CHECK(bytes[5] == 0u);
    CHECK(guard[0] == 0xa5u);
    CHECK(guard[1] == 0x5au);
    CHECK(oamr_bounded_queue_pop(&queue, output) == OAMR_STATUS_OK);
    CHECK(memcmp(input, output, sizeof(input)) == 0);
}

static void queue_reinitialization_resets_previous_valid_state(void)
{
    oamr_bounded_queue_t queue;
    uint8_t first_storage[2] = {0x11u, 0x22u};
    uint8_t second_storage[2] = {0x33u, 0x44u};
    const uint8_t input = 0x55u;
    uint8_t output = 0xa5u;

    CHECK(oamr_bounded_queue_init(&queue, first_storage, sizeof(first_storage), 1u, 2u) == OAMR_STATUS_OK);
    CHECK(oamr_bounded_queue_push(&queue, &input) == OAMR_STATUS_OK);
    CHECK(first_storage[0] == input);
    CHECK(oamr_bounded_queue_init(&queue, NULL, sizeof(second_storage), 1u, 2u) == OAMR_STATUS_INVALID);
    queue_expect_zeroed(&queue);
    CHECK(first_storage[0] == input);
    CHECK(first_storage[1] == 0x22u);
    CHECK(second_storage[0] == 0x33u);
    CHECK(second_storage[1] == 0x44u);
    CHECK(oamr_bounded_queue_pop(&queue, &output) == OAMR_STATUS_INVALID);
    CHECK(output == 0xa5u);

    CHECK(oamr_bounded_queue_init(&queue, second_storage, sizeof(second_storage), 1u, 2u) == OAMR_STATUS_OK);
    CHECK(oamr_bounded_queue_push(&queue, &input) == OAMR_STATUS_OK);
    CHECK(oamr_bounded_queue_init(&queue, second_storage, sizeof(second_storage), 1u, 1u) == OAMR_STATUS_OK);
    CHECK(queue.storage == second_storage);
    CHECK(queue.storage_size == sizeof(second_storage));
    CHECK(queue.capacity == 1u);
    CHECK(queue.element_size == 1u);
    CHECK(queue.head == 0u);
    CHECK(queue.tail == 0u);
    CHECK(queue.count == 0u);
    CHECK(queue.overflow_count == 0u);
    CHECK(queue.initialized);
    CHECK(second_storage[0] == input);
    CHECK(oamr_bounded_queue_pop(&queue, &output) == OAMR_STATUS_EMPTY);
    CHECK(output == 0xa5u);
}

static void queue_empty_and_invalid_calls_leave_output_untouched(void)
{
    oamr_bounded_queue_t queue;
    oamr_bounded_queue_t zeroed = {0};
    uint8_t storage[4] = {0};
    uint8_t output[2];
    const uint8_t value[2] = {1u, 2u};

    output[0] = 0xa5u;
    output[1] = 0x5au;
    CHECK(oamr_bounded_queue_count(NULL) == 0u);
    CHECK(oamr_bounded_queue_capacity(NULL) == 0u);
    CHECK(oamr_bounded_queue_overflow_count(NULL) == 0u);
    CHECK(oamr_bounded_queue_count(&zeroed) == 0u);
    CHECK(oamr_bounded_queue_push(NULL, value) == OAMR_STATUS_INVALID);
    CHECK(oamr_bounded_queue_pop(NULL, output) == OAMR_STATUS_INVALID);
    CHECK(oamr_bounded_queue_peek(NULL, output) == OAMR_STATUS_INVALID);
    CHECK(oamr_bounded_queue_reset(NULL) == OAMR_STATUS_INVALID);
    CHECK(oamr_bounded_queue_push(&zeroed, value) == OAMR_STATUS_INVALID);
    CHECK(oamr_bounded_queue_pop(&zeroed, output) == OAMR_STATUS_INVALID);
    CHECK(oamr_bounded_queue_reset(&zeroed) == OAMR_STATUS_INVALID);
    CHECK(output[0] == 0xa5u);
    CHECK(output[1] == 0x5au);

    CHECK(oamr_bounded_queue_init(&queue, storage, sizeof(storage), 2u, 2u) == OAMR_STATUS_OK);
    CHECK(oamr_bounded_queue_push(&queue, NULL) == OAMR_STATUS_INVALID);
    CHECK(oamr_bounded_queue_pop(&queue, NULL) == OAMR_STATUS_INVALID);
    CHECK(oamr_bounded_queue_peek(&queue, NULL) == OAMR_STATUS_INVALID);
    CHECK(oamr_bounded_queue_pop(&queue, output) == OAMR_STATUS_EMPTY);
    CHECK(output[0] == 0xa5u);
    CHECK(output[1] == 0x5au);
    CHECK(oamr_bounded_queue_peek(&queue, output) == OAMR_STATUS_EMPTY);
    CHECK(output[0] == 0xa5u);
    CHECK(output[1] == 0x5au);
    CHECK(oamr_bounded_queue_count(&queue) == 0u);
    CHECK(queue.head == 0u);
    CHECK(queue.tail == 0u);
    CHECK(oamr_bounded_queue_overflow_count(&queue) == 0u);
}

static void queue_peek_does_not_consume(void)
{
    oamr_bounded_queue_t queue;
    uint32_t storage[2] = {0u, 0u};
    const uint32_t first = UINT32_C(0x12345678);
    const uint32_t second = UINT32_C(0x90abcdef);
    uint32_t output = 0u;

    CHECK(oamr_bounded_queue_init(&queue, storage, sizeof(storage), sizeof(uint32_t), 2u) == OAMR_STATUS_OK);
    CHECK(oamr_bounded_queue_push(&queue, &first) == OAMR_STATUS_OK);
    CHECK(oamr_bounded_queue_push(&queue, &second) == OAMR_STATUS_OK);
    CHECK(oamr_bounded_queue_peek(&queue, &output) == OAMR_STATUS_OK);
    CHECK(output == first);
    CHECK(oamr_bounded_queue_peek(&queue, &output) == OAMR_STATUS_OK);
    CHECK(output == first);
    CHECK(oamr_bounded_queue_count(&queue) == 2u);
    CHECK(queue.head == 0u);
    CHECK(queue.tail == 0u);
    CHECK(oamr_bounded_queue_pop(&queue, &output) == OAMR_STATUS_OK);
    CHECK(output == first);
    CHECK(oamr_bounded_queue_pop(&queue, &output) == OAMR_STATUS_OK);
    CHECK(output == second);
}

static void queue_wrap_burst_and_overflow_preserve_fifo_data(void)
{
    oamr_bounded_queue_t queue;
    uint16_t storage[3] = {0u, 0u, 0u};
    uint16_t value;
    uint16_t output;
    size_t index;

    CHECK(oamr_bounded_queue_init(&queue, storage, sizeof(storage), sizeof(uint16_t), 3u) == OAMR_STATUS_OK);
    for (index = 0u; index < 2u; ++index) {
        value = (uint16_t)(100u + index);
        CHECK(oamr_bounded_queue_push(&queue, &value) == OAMR_STATUS_OK);
    }
    for (index = 0u; index < 2u; ++index) {
        output = 0u;
        CHECK(oamr_bounded_queue_pop(&queue, &output) == OAMR_STATUS_OK);
        CHECK(output == (uint16_t)(100u + index));
    }
    CHECK(queue.head == 2u);
    CHECK(queue.tail == 2u);

    for (index = 0u; index < 3u; ++index) {
        value = (uint16_t)(200u + index);
        CHECK(oamr_bounded_queue_push(&queue, &value) == OAMR_STATUS_OK);
        CHECK(oamr_bounded_queue_count(&queue) == index + 1u);
    }
    CHECK(queue.tail == 2u);
    CHECK(storage[2] == UINT16_C(200));
    CHECK(storage[0] == UINT16_C(201));
    CHECK(storage[1] == UINT16_C(202));

    value = UINT16_C(999);
    CHECK(oamr_bounded_queue_push(&queue, &value) == OAMR_STATUS_OVERFLOW);
    CHECK(oamr_bounded_queue_count(&queue) == 3u);
    CHECK(oamr_bounded_queue_overflow_count(&queue) == 1u);
    CHECK(storage[2] == UINT16_C(200));
    CHECK(storage[0] == UINT16_C(201));
    CHECK(storage[1] == UINT16_C(202));

    for (index = 0u; index < 3u; ++index) {
        output = 0u;
        CHECK(oamr_bounded_queue_peek(&queue, &output) == OAMR_STATUS_OK);
        CHECK(output == (uint16_t)(200u + index));
        CHECK(oamr_bounded_queue_pop(&queue, &output) == OAMR_STATUS_OK);
        CHECK(output == (uint16_t)(200u + index));
    }
    CHECK(queue.head == 2u);
    CHECK(queue.tail == 2u);
    CHECK(oamr_bounded_queue_count(&queue) == 0u);
    output = UINT16_C(0xbeef);
    CHECK(oamr_bounded_queue_pop(&queue, &output) == OAMR_STATUS_EMPTY);
    CHECK(output == UINT16_C(0xbeef));

    for (index = 0u; index < 300u; ++index) {
        value = (uint16_t)(300u + index);
        CHECK(oamr_bounded_queue_push(&queue, &value) == OAMR_STATUS_OK);
        CHECK(oamr_bounded_queue_count(&queue) == 1u);
        output = 0u;
        CHECK(oamr_bounded_queue_pop(&queue, &output) == OAMR_STATUS_OK);
        CHECK(output == value);
        CHECK(oamr_bounded_queue_count(&queue) == 0u);
        CHECK(queue.head == queue.tail);
    }
    CHECK(queue.head == 2u);
    CHECK(queue.tail == 2u);

    CHECK(oamr_bounded_queue_push(&queue, &value) == OAMR_STATUS_OK);
    CHECK(oamr_bounded_queue_pop(&queue, &output) == OAMR_STATUS_OK);
    CHECK(oamr_bounded_queue_push(&queue, &value) == OAMR_STATUS_OK);
    CHECK(oamr_bounded_queue_pop(&queue, &output) == OAMR_STATUS_OK);
    CHECK(queue.head == 1u);
    CHECK(queue.tail == 1u);
}

static void queue_supports_byte_copied_multibyte_elements(void)
{
    oamr_bounded_queue_t queue;
    test_element_t storage[3] = {0};
    const test_element_t first = {{0u, 1u, 2u, 3u, 4u}, UINT16_C(0x1234)};
    const test_element_t second = {{9u, 8u, 7u, 6u, 5u}, UINT16_C(0xabcd)};
    test_element_t output;
    uint8_t raw[sizeof(test_element_t) * 3u];
    size_t index;

    memset(raw, 0, sizeof(raw));
    CHECK(sizeof(test_element_t) > sizeof(uint32_t));
    CHECK(oamr_bounded_queue_init(&queue, raw, sizeof(raw), sizeof(test_element_t), 3u) == OAMR_STATUS_OK);
    CHECK(oamr_bounded_queue_push(&queue, &first) == OAMR_STATUS_OK);
    CHECK(oamr_bounded_queue_push(&queue, &second) == OAMR_STATUS_OK);
    memset(&output, 0, sizeof(output));
    CHECK(oamr_bounded_queue_pop(&queue, &output) == OAMR_STATUS_OK);
    CHECK(memcmp(&output, &first, sizeof(output)) == 0);
    CHECK(output.tag == UINT16_C(0x1234));
    CHECK(oamr_bounded_queue_pop(&queue, &output) == OAMR_STATUS_OK);
    CHECK(memcmp(&output, &second, sizeof(output)) == 0);
    for (index = 0u; index < sizeof(output.bytes); ++index) {
        CHECK(output.bytes[index] == second.bytes[index]);
    }

    CHECK(oamr_bounded_queue_init(&queue, storage, sizeof(storage), sizeof(storage[0]), 3u) == OAMR_STATUS_OK);
    CHECK(oamr_bounded_queue_push(&queue, &second) == OAMR_STATUS_OK);
    CHECK(oamr_bounded_queue_pop(&queue, &output) == OAMR_STATUS_OK);
    CHECK(memcmp(&output, &second, sizeof(output)) == 0);
}

static void queue_overflow_count_saturates_without_overwrite(void)
{
    oamr_bounded_queue_t queue;
    uint8_t storage[1] = {0x42u};
    const uint8_t replacement = 0x99u;
    uint8_t output = 0u;

    CHECK(oamr_bounded_queue_init(&queue, storage, sizeof(storage), 1u, 1u) == OAMR_STATUS_OK);
    CHECK(oamr_bounded_queue_push(&queue, &storage[0]) == OAMR_STATUS_OK);
    queue.overflow_count = UINT64_MAX - UINT64_C(1);
    CHECK(oamr_bounded_queue_push(&queue, &replacement) == OAMR_STATUS_OVERFLOW);
    CHECK(queue.overflow_count == UINT64_MAX);
    CHECK(oamr_bounded_queue_push(&queue, &replacement) == OAMR_STATUS_OVERFLOW);
    CHECK(queue.overflow_count == UINT64_MAX);
    CHECK(storage[0] == 0x42u);
    CHECK(oamr_bounded_queue_push(&queue, NULL) == OAMR_STATUS_INVALID);
    CHECK(queue.overflow_count == UINT64_MAX);
    CHECK(oamr_bounded_queue_pop(&queue, &output) == OAMR_STATUS_OK);
    CHECK(output == 0x42u);
}

static void queue_reset_reuses_storage_and_clears_accounting(void)
{
    oamr_bounded_queue_t queue;
    uint8_t storage[3] = {0u, 0u, 0u};
    const uint8_t first = 0x11u;
    const uint8_t second = 0x22u;
    uint8_t output = 0xa5u;

    CHECK(oamr_bounded_queue_init(&queue, storage, sizeof(storage), 1u, 3u) == OAMR_STATUS_OK);
    CHECK(oamr_bounded_queue_push(&queue, &first) == OAMR_STATUS_OK);
    CHECK(oamr_bounded_queue_push(&queue, &second) == OAMR_STATUS_OK);
    CHECK(oamr_bounded_queue_pop(&queue, &output) == OAMR_STATUS_OK);
    CHECK(oamr_bounded_queue_push(&queue, &second) == OAMR_STATUS_OK);
    CHECK(oamr_bounded_queue_push(&queue, &second) == OAMR_STATUS_OK);
    CHECK(oamr_bounded_queue_push(&queue, &second) == OAMR_STATUS_OVERFLOW);
    CHECK(oamr_bounded_queue_reset(&queue) == OAMR_STATUS_OK);
    CHECK(queue.storage == storage);
    CHECK(queue.storage_size == sizeof(storage));
    CHECK(queue.element_size == 1u);
    CHECK(queue.capacity == 3u);
    CHECK(queue.head == 0u);
    CHECK(queue.tail == 0u);
    CHECK(oamr_bounded_queue_count(&queue) == 0u);
    CHECK(oamr_bounded_queue_overflow_count(&queue) == 0u);
    CHECK(storage[0] == second);
    CHECK(storage[1] == second);
    CHECK(storage[2] == second);
    output = 0xa5u;
    CHECK(oamr_bounded_queue_pop(&queue, &output) == OAMR_STATUS_EMPTY);
    CHECK(output == 0xa5u);
    CHECK(oamr_bounded_queue_push(&queue, &second) == OAMR_STATUS_OK);
    CHECK(oamr_bounded_queue_pop(&queue, &output) == OAMR_STATUS_OK);
    CHECK(output == second);
}

static void corrupted_queue_reports_zero_and_rejects_access(void)
{
    oamr_bounded_queue_t queue;
    uint8_t storage[8] = {0};
    uint8_t output = 0xa5u;
    const uint8_t input[2] = {0x5au, 0xa5u};

    CHECK(oamr_bounded_queue_init(&queue, storage, sizeof(storage), 2u, 4u) == OAMR_STATUS_OK);
    CHECK(oamr_bounded_queue_push(&queue, &input) == OAMR_STATUS_OK);
    queue.count = queue.capacity + 1u;
    CHECK(oamr_bounded_queue_count(&queue) == 0u);
    CHECK(oamr_bounded_queue_capacity(&queue) == 0u);
    CHECK(oamr_bounded_queue_overflow_count(&queue) == 0u);
    CHECK(oamr_bounded_queue_push(&queue, &input) == OAMR_STATUS_INVALID);
    CHECK(oamr_bounded_queue_peek(&queue, &output) == OAMR_STATUS_INVALID);
    CHECK(oamr_bounded_queue_pop(&queue, &output) == OAMR_STATUS_INVALID);
    CHECK(oamr_bounded_queue_reset(&queue) == OAMR_STATUS_INVALID);
    CHECK(output == 0xa5u);
}

static void corrupted_queue_variants_are_rejected(void)
{
    oamr_bounded_queue_t valid;
    oamr_bounded_queue_t queue;
    uint8_t storage[8] = {0};
    uint8_t output = 0xa5u;
    const uint8_t input[2] = {0x5au, 0xa5u};
    size_t variant;

    CHECK(oamr_bounded_queue_init(&valid, storage, sizeof(storage), 2u, 4u) == OAMR_STATUS_OK);
    CHECK(oamr_bounded_queue_push(&valid, &input) == OAMR_STATUS_OK);
    valid.overflow_count = UINT64_C(7);

    for (variant = 0u; variant < 9u; ++variant) {
        queue = valid;
        switch (variant) {
        case 0u:
            queue.initialized = false;
            break;
        case 1u:
            queue.storage = NULL;
            break;
        case 2u:
            queue.element_size = 0u;
            break;
        case 3u:
            queue.capacity = 0u;
            break;
        case 4u:
            queue.head = queue.capacity;
            break;
        case 5u:
            queue.tail = queue.capacity;
            break;
        case 6u:
            queue.storage_size = queue.element_size * queue.capacity - 1u;
            break;
        case 7u:
            queue.element_size = SIZE_MAX;
            queue.capacity = 2u;
            queue.storage_size = SIZE_MAX;
            break;
        case 8u:
            queue.storage_size = 0u;
            break;
        default:
            CHECK(false);
            break;
        }
        CHECK(oamr_bounded_queue_count(&queue) == 0u);
        CHECK(oamr_bounded_queue_capacity(&queue) == 0u);
        CHECK(oamr_bounded_queue_overflow_count(&queue) == 0u);
        CHECK(oamr_bounded_queue_push(&queue, &input) == OAMR_STATUS_INVALID);
        CHECK(oamr_bounded_queue_peek(&queue, &output) == OAMR_STATUS_INVALID);
        CHECK(oamr_bounded_queue_pop(&queue, &output) == OAMR_STATUS_INVALID);
        CHECK(oamr_bounded_queue_reset(&queue) == OAMR_STATUS_INVALID);
        CHECK(output == 0xa5u);
        CHECK(storage[0] == input[0]);
        CHECK(storage[1] == input[1]);
    }
}

static void can_id_boundaries_and_unknown_types(void)
{
    CHECK(OAMR_CAN_MAX_DLC == 8u);
    CHECK(OAMR_CAN_STANDARD_ID_MAX == UINT32_C(0x7ff));
    CHECK(OAMR_CAN_EXTENDED_ID_MAX == UINT32_C(0x1fffffff));
    CHECK(oamr_can_id_is_valid(OAMR_CAN_ID_STANDARD, UINT32_C(0)));
    CHECK(oamr_can_id_is_valid(OAMR_CAN_ID_STANDARD, OAMR_CAN_STANDARD_ID_MAX));
    CHECK(!oamr_can_id_is_valid(OAMR_CAN_ID_STANDARD, OAMR_CAN_STANDARD_ID_MAX + UINT32_C(1)));
    CHECK(oamr_can_id_is_valid(OAMR_CAN_ID_EXTENDED, UINT32_C(0)));
    CHECK(oamr_can_id_is_valid(OAMR_CAN_ID_EXTENDED, OAMR_CAN_EXTENDED_ID_MAX));
    CHECK(!oamr_can_id_is_valid(OAMR_CAN_ID_EXTENDED, OAMR_CAN_EXTENDED_ID_MAX + UINT32_C(1)));
    CHECK(!oamr_can_id_is_valid((oamr_can_id_type_t)(-1), UINT32_C(0)));
    CHECK(!oamr_can_id_is_valid((oamr_can_id_type_t)2, UINT32_C(0)));
    CHECK(!oamr_can_id_is_valid((oamr_can_id_type_t)255, UINT32_MAX));
}

static void can_frame_dlc_boundaries_and_structural_scope(void)
{
    oamr_can_frame_t frame = {0};
    oamr_can_error_t combined_errors = (oamr_can_error_t)(OAMR_CAN_ERROR_BIT |
                                                         OAMR_CAN_ERROR_CRC |
                                                         OAMR_CAN_ERROR_BUS_OFF);

    CHECK(!oamr_can_frame_is_valid(NULL));
    frame.id_type = OAMR_CAN_ID_STANDARD;
    frame.id = OAMR_CAN_STANDARD_ID_MAX;
    frame.dlc = 0u;
    CHECK(oamr_can_frame_is_valid(&frame));
    frame.dlc = OAMR_CAN_MAX_DLC;
    CHECK(oamr_can_frame_is_valid(&frame));
    frame.dlc = (uint8_t)(OAMR_CAN_MAX_DLC + 1u);
    CHECK(!oamr_can_frame_is_valid(&frame));
    frame.dlc = UINT8_MAX;
    CHECK(!oamr_can_frame_is_valid(&frame));

    frame.dlc = 8u;
    frame.timestamp_us = UINT64_MAX;
    frame.error_flags = combined_errors;
    CHECK(oamr_can_frame_is_valid(&frame));
    frame.id = OAMR_CAN_STANDARD_ID_MAX + UINT32_C(1);
    CHECK(!oamr_can_frame_is_valid(&frame));
    frame.id_type = OAMR_CAN_ID_EXTENDED;
    frame.id = OAMR_CAN_EXTENDED_ID_MAX;
    CHECK(oamr_can_frame_is_valid(&frame));
    frame.id = OAMR_CAN_EXTENDED_ID_MAX + UINT32_C(1);
    CHECK(!oamr_can_frame_is_valid(&frame));
    frame.id_type = (oamr_can_id_type_t)7;
    CHECK(!oamr_can_frame_is_valid(&frame));
}

static void sample_metadata_preserves_explicit_status_and_sequence(void)
{
    oamr_sample_t sample = {0};
    const oamr_sample_t invalid_with_numeric_value = {
        123.0f,
        OAMR_SAMPLE_UNIT_METRE,
        OAMR_STATUS_INVALID,
        UINT64_C(0),
        UINT32_C(7),
        true
    };
    const oamr_sample_t wrapped_sequence = {
        -1.5f,
        OAMR_SAMPLE_UNIT_RADIAN,
        OAMR_STATUS_STALE,
        UINT64_MAX,
        UINT32_MAX,
        false
    };

    CHECK(sample.status == OAMR_STATUS_INVALID);
    CHECK(!sample.timestamp_valid);
    CHECK(sample.timestamp_us == 0u);
    CHECK(sample.sequence == 0u);
    sample = invalid_with_numeric_value;
    CHECK(sample.value == 123.0f);
    CHECK(sample.status == OAMR_STATUS_INVALID);
    CHECK(sample.timestamp_us == 0u);
    CHECK(sample.timestamp_valid);
    CHECK(sample.sequence == 7u);
    CHECK(oamr_freshness_check(sample.timestamp_us, sample.timestamp_valid,
                               UINT64_C(0), UINT64_C(0)) == OAMR_STATUS_OK);
    CHECK(sample.status == OAMR_STATUS_INVALID);

    sample.status = OAMR_STATUS_OK;
    CHECK(oamr_freshness_check(sample.timestamp_us, sample.timestamp_valid,
                               UINT64_C(1), UINT64_C(0)) == OAMR_STATUS_STALE);
    CHECK(sample.status == OAMR_STATUS_OK);
    sample = wrapped_sequence;
    CHECK(sample.sequence == UINT32_MAX);
    CHECK(sample.status == OAMR_STATUS_STALE);
    CHECK(!sample.timestamp_valid);
    CHECK(oamr_freshness_check(sample.timestamp_us, sample.timestamp_valid,
                               sample.timestamp_us, UINT64_MAX) == OAMR_STATUS_INVALID);
    ++sample.sequence;
    CHECK(sample.sequence == 0u);
    CHECK(sample.status == OAMR_STATUS_STALE);
}

typedef struct {
    const uint8_t *read_data;
    size_t read_size;
    size_t write_limit;
    oamr_transport_result_t read_result;
    oamr_transport_result_t write_result;
    uint8_t written[8];
    size_t written_size;
    unsigned int read_calls;
    unsigned int write_calls;
    void *last_read_context;
    void *last_write_context;
    size_t last_read_capacity;
    size_t last_write_size;
} mock_transport_t;

static oamr_transport_result_t mock_read(void *context, uint8_t *data,
                                         size_t data_capacity, size_t *transferred)
{
    mock_transport_t *mock = (mock_transport_t *)context;
    size_t amount;

    ++mock->read_calls;
    mock->last_read_context = context;
    mock->last_read_capacity = data_capacity;
    if (transferred == NULL) {
        return OAMR_TRANSPORT_INVALID;
    }
    *transferred = 0u;
    if (data_capacity != 0u && data == NULL) {
        return OAMR_TRANSPORT_INVALID;
    }
    if (data_capacity == 0u) {
        return OAMR_TRANSPORT_OK;
    }
    if (mock->read_result != OAMR_TRANSPORT_OK) {
        return mock->read_result;
    }

    amount = mock->read_size < data_capacity ? mock->read_size : data_capacity;
    memcpy(data, mock->read_data, amount);
    *transferred = amount;
    return OAMR_TRANSPORT_OK;
}

static oamr_transport_result_t mock_write(void *context, const uint8_t *data,
                                          size_t data_size, size_t *transferred)
{
    mock_transport_t *mock = (mock_transport_t *)context;
    size_t amount;

    ++mock->write_calls;
    mock->last_write_context = context;
    mock->last_write_size = data_size;
    if (transferred == NULL) {
        return OAMR_TRANSPORT_INVALID;
    }
    *transferred = 0u;
    if (data_size != 0u && data == NULL) {
        return OAMR_TRANSPORT_INVALID;
    }
    if (data_size == 0u) {
        return OAMR_TRANSPORT_OK;
    }
    if (mock->write_result != OAMR_TRANSPORT_OK) {
        return mock->write_result;
    }

    amount = mock->write_limit < data_size ? mock->write_limit : data_size;
    memcpy(mock->written + mock->written_size, data, amount);
    mock->written_size += amount;
    *transferred = amount;
    return OAMR_TRANSPORT_OK;
}

static void transport_callbacks_report_short_transfers(void)
{
    static const uint8_t source[] = {0x10u, 0x20u, 0x30u, 0x40u};
    mock_transport_t mock = {0};
    oamr_transport_t transport = {&mock, mock_read, mock_write};
    uint8_t destination[6];
    size_t transferred = 123u;

    memset(destination, 0xa5, sizeof(destination));
    mock.read_data = source;
    mock.read_size = 2u;
    mock.read_result = OAMR_TRANSPORT_OK;
    mock.write_result = OAMR_TRANSPORT_OK;
    mock.write_limit = 3u;

    CHECK(transport.context == &mock);
    CHECK(transport.read != NULL);
    CHECK(transport.write != NULL);
    CHECK(transport.read(transport.context, destination, sizeof(destination), &transferred) == OAMR_TRANSPORT_OK);
    CHECK(transferred == 2u);
    CHECK(destination[0] == source[0]);
    CHECK(destination[1] == source[1]);
    CHECK(destination[2] == 0xa5u);
    CHECK(destination[3] == 0xa5u);
    CHECK(mock.read_calls == 1u);
    CHECK(mock.last_read_context == &mock);
    CHECK(mock.last_read_capacity == sizeof(destination));

    CHECK(transport.write(transport.context, source, sizeof(source), &transferred) == OAMR_TRANSPORT_OK);
    CHECK(transferred == 3u);
    CHECK(mock.written_size == 3u);
    CHECK(memcmp(mock.written, source, 3u) == 0);
    CHECK(mock.written[3] == 0u);
    CHECK(mock.write_calls == 1u);
    CHECK(mock.last_write_context == &mock);
    CHECK(mock.last_write_size == sizeof(source));
}

static void transport_zero_length_accepts_null_data(void)
{
    mock_transport_t mock = {0};
    oamr_transport_t transport = {&mock, mock_read, mock_write};
    size_t transferred = SIZE_MAX;

    CHECK(transport.read(transport.context, NULL, 0u, &transferred) == OAMR_TRANSPORT_OK);
    CHECK(transferred == 0u);
    transferred = SIZE_MAX;
    CHECK(transport.write(transport.context, NULL, 0u, &transferred) == OAMR_TRANSPORT_OK);
    CHECK(transferred == 0u);
    CHECK(mock.read_calls == 1u);
    CHECK(mock.write_calls == 1u);
    CHECK(mock.last_read_capacity == 0u);
    CHECK(mock.last_write_size == 0u);
}

static void transport_invalid_arguments_leave_no_transfer(void)
{
    static const uint8_t source[] = {1u, 2u};
    mock_transport_t mock = {0};
    oamr_transport_t transport = {&mock, mock_read, mock_write};
    oamr_transport_t unsupported = {NULL, NULL, NULL};
    uint8_t destination[2] = {0xa5u, 0x5au};
    size_t transferred = SIZE_MAX;

    mock.read_data = source;
    mock.read_size = sizeof(source);
    mock.read_result = OAMR_TRANSPORT_OK;
    mock.write_result = OAMR_TRANSPORT_OK;
    mock.write_limit = sizeof(source);
    CHECK(transport.read(transport.context, NULL, sizeof(destination), &transferred) == OAMR_TRANSPORT_INVALID);
    CHECK(transferred == 0u);
    CHECK(destination[0] == 0xa5u);
    CHECK(destination[1] == 0x5au);
    transferred = SIZE_MAX;
    CHECK(transport.write(transport.context, NULL, sizeof(source), &transferred) == OAMR_TRANSPORT_INVALID);
    CHECK(transferred == 0u);
    CHECK(mock.written_size == 0u);
    CHECK(transport.read(transport.context, destination, sizeof(destination), NULL) == OAMR_TRANSPORT_INVALID);
    CHECK(transport.write(transport.context, source, sizeof(source), NULL) == OAMR_TRANSPORT_INVALID);
    CHECK(destination[0] == 0xa5u);
    CHECK(destination[1] == 0x5au);
    CHECK(mock.written_size == 0u);
    CHECK(unsupported.read == NULL);
    CHECK(unsupported.write == NULL);
}

static void transport_would_block_and_disconnect_transfer_zero(void)
{
    static const uint8_t source[] = {1u, 2u, 3u};
    mock_transport_t mock = {0};
    oamr_transport_t transport = {&mock, mock_read, mock_write};
    oamr_transport_result_t results[] = {
        OAMR_TRANSPORT_WOULD_BLOCK,
        OAMR_TRANSPORT_DISCONNECTED
    };
    uint8_t destination[3];
    size_t transferred;
    size_t index;

    mock.read_data = source;
    mock.read_size = sizeof(source);
    mock.write_limit = sizeof(source);
    for (index = 0u; index < ARRAY_SIZE(results); ++index) {
        memset(destination, 0xa5, sizeof(destination));
        transferred = SIZE_MAX;
        mock.read_result = results[index];
        mock.write_result = results[index];
        CHECK(transport.read(transport.context, destination, sizeof(destination), &transferred) == results[index]);
        CHECK(transferred == 0u);
        CHECK(destination[0] == 0xa5u);
        CHECK(destination[1] == 0xa5u);
        CHECK(destination[2] == 0xa5u);
        transferred = SIZE_MAX;
        CHECK(transport.write(transport.context, source, sizeof(source), &transferred) == results[index]);
        CHECK(transferred == 0u);
        CHECK(mock.written_size == 0u);
    }
    CHECK(mock.read_calls == 2u);
    CHECK(mock.write_calls == 2u);
}

int main(void)
{
    RUN(status_zero_is_not_success);
    RUN(time_initialization_and_first_timestamp);
    RUN(time_accepts_uint64_progress_beyond_32_bits);
    RUN(time_rejects_decrease_and_preserves_last_accepted);
    RUN(freshness_missing_future_and_zero_age);
    RUN(freshness_exact_age_boundary);
    RUN(freshness_uint64_extremes);
    RUN(queue_rejects_invalid_initialization_without_erasing_storage);
    RUN(queue_initializes_exact_and_extra_external_storage);
    RUN(queue_reinitialization_resets_previous_valid_state);
    RUN(queue_empty_and_invalid_calls_leave_output_untouched);
    RUN(queue_peek_does_not_consume);
    RUN(queue_wrap_burst_and_overflow_preserve_fifo_data);
    RUN(queue_supports_byte_copied_multibyte_elements);
    RUN(queue_overflow_count_saturates_without_overwrite);
    RUN(queue_reset_reuses_storage_and_clears_accounting);
    RUN(corrupted_queue_reports_zero_and_rejects_access);
    RUN(corrupted_queue_variants_are_rejected);
    RUN(can_id_boundaries_and_unknown_types);
    RUN(can_frame_dlc_boundaries_and_structural_scope);
    RUN(sample_metadata_preserves_explicit_status_and_sequence);
    RUN(transport_callbacks_report_short_transfers);
    RUN(transport_zero_length_accepts_null_data);
    RUN(transport_invalid_arguments_leave_no_transfer);
    RUN(transport_would_block_and_disconnect_transfer_zero);
    return TEST_END();
}
