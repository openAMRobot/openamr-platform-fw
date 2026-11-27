/* SPDX-License-Identifier: MIT */
#include "test_harness.h"

#include "oamr/mb7060.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

static void expect_valid_frame(const char *frame, uint16_t expected_range,
                               uint32_t expected_sequence)
{
    oamr_mb_t parser;
    oamr_mb_sample_t out = {UINT16_C(0xaaaa), UINT32_C(0x55555555)};
    size_t index;
    unsigned int samples = 0u;

    oamr_mb_init(&parser);
    oamr_mb_arm(&parser);
    for (index = 0u; index < strlen(frame); ++index) {
        oamr_mb_push_t result = oamr_mb_push(&parser, (uint8_t)frame[index], &out);
        if (result == OAMR_MB_PUSH_SAMPLE) {
            ++samples;
            CHECK(index == strlen(frame) - 1u);
            CHECK(out.range_cm == expected_range);
            CHECK(out.sequence == expected_sequence);
        } else {
            CHECK(result == OAMR_MB_PUSH_NONE);
            CHECK(out.range_cm == UINT16_C(0xaaaa));
            CHECK(out.sequence == UINT32_C(0x55555555));
        }
    }
    CHECK(samples == 1u);
    CHECK(parser.counters.frames_ok == UINT64_C(1));
}

static void valid_ascii_frames_cover_documented_boundaries(void)
{
    expect_valid_frame("R0\r", UINT16_C(0), UINT32_C(1));
    expect_valid_frame("R001\r", UINT16_C(1), UINT32_C(1));
    expect_valid_frame("R20\r", UINT16_C(20), UINT32_C(1));
    expect_valid_frame("R765\r", UINT16_C(765), UINT32_C(1));
    expect_valid_frame("R999\r", UINT16_C(999), UINT32_C(1));
}

static void malformed_frames_never_produce_a_sample(void)
{
    static const char *const malformed[] = {
        "R\r",
        "RX\r",
        "R1X\r",
        "R12X\r",
        "R12\n\r",
        "R-1\r",
        "R 1\r",
        "R0000\r",
        "R1234\r",
        "R9999\r"
    };
    size_t case_index;

    for (case_index = 0u; case_index < ARRAY_SIZE(malformed); ++case_index) {
        oamr_mb_t parser;
        oamr_mb_sample_t out = {UINT16_C(0xaaaa), UINT32_C(0x55555555)};
        size_t index;
        unsigned int samples = 0u;
        unsigned int errors = 0u;

        oamr_mb_init(&parser);
        oamr_mb_arm(&parser);
        for (index = 0u; index < strlen(malformed[case_index]); ++index) {
            oamr_mb_push_t result = oamr_mb_push(
                &parser, (uint8_t)malformed[case_index][index], &out);
            if (result == OAMR_MB_PUSH_SAMPLE) {
                ++samples;
            }
            if (result == OAMR_MB_PUSH_MALFORMED) {
                ++errors;
            }
            CHECK(out.range_cm == UINT16_C(0xaaaa));
            CHECK(out.sequence == UINT32_C(0x55555555));
        }
        CHECK(samples == 0u);
        CHECK(errors == 1u);
        CHECK(parser.counters.frames_ok == UINT64_C(0));
        CHECK(parser.counters.malformed == UINT64_C(1));
        CHECK(parser.armed);
    }
}

static void every_split_point_yields_exactly_one_sample(void)
{
    static const char frame[] = "R765\r";
    size_t split;

    for (split = 1u; split < strlen(frame); ++split) {
        oamr_mb_t parser;
        oamr_mb_sample_t out = {UINT16_C(0xaaaa), UINT32_C(0x55555555)};
        size_t index;
        unsigned int samples = 0u;

        oamr_mb_init(&parser);
        oamr_mb_arm(&parser);
        for (index = 0u; index < split; ++index) {
            CHECK(oamr_mb_push(&parser, (uint8_t)frame[index], &out) ==
                  OAMR_MB_PUSH_NONE);
            CHECK(out.range_cm == UINT16_C(0xaaaa));
        }
        for (; index < strlen(frame); ++index) {
            oamr_mb_push_t result = oamr_mb_push(&parser, (uint8_t)frame[index], &out);
            if (result == OAMR_MB_PUSH_SAMPLE) {
                ++samples;
                CHECK(index == strlen(frame) - 1u);
                CHECK(out.range_cm == UINT16_C(765));
                CHECK(out.sequence == UINT32_C(1));
            } else {
                CHECK(result == OAMR_MB_PUSH_NONE);
            }
        }
        CHECK(samples == 1u);
        CHECK(parser.counters.frames_ok == UINT64_C(1));
    }
}

static void arming_discards_partial_data_and_allows_one_reading(void)
{
    oamr_mb_t parser;
    oamr_mb_sample_t out = {0u, 0u};
    static const char first[] = "R12\r";
    static const char duplicate[] = "R13\r";
    size_t index;

    oamr_mb_init(&parser);
    oamr_mb_arm(&parser);
    CHECK(oamr_mb_push(&parser, (uint8_t)'R', &out) == OAMR_MB_PUSH_NONE);
    CHECK(oamr_mb_push(&parser, (uint8_t)'9', &out) == OAMR_MB_PUSH_NONE);
    oamr_mb_arm(&parser);
    CHECK(parser.length == 0u);
    CHECK(!parser.in_frame);

    for (index = 0u; index < strlen(first); ++index) {
        oamr_mb_push_t result = oamr_mb_push(&parser, (uint8_t)first[index], &out);
        if (index + 1u == strlen(first)) {
            CHECK(result == OAMR_MB_PUSH_SAMPLE);
            CHECK(out.range_cm == UINT16_C(12));
            CHECK(out.sequence == UINT32_C(1));
        } else {
            CHECK(result == OAMR_MB_PUSH_NONE);
        }
    }
    CHECK(!parser.armed);
    CHECK(parser.counters.frames_ok == UINT64_C(1));

    out.range_cm = UINT16_C(0xaaaa);
    out.sequence = UINT32_C(0x55555555);
    for (index = 0u; index < strlen(duplicate); ++index) {
        oamr_mb_push_t result = oamr_mb_push(&parser, (uint8_t)duplicate[index], &out);
        CHECK(result != OAMR_MB_PUSH_SAMPLE);
        CHECK(out.range_cm == UINT16_C(0xaaaa));
        CHECK(out.sequence == UINT32_C(0x55555555));
    }
    CHECK(parser.counters.frames_ok == UINT64_C(1));
    CHECK(parser.counters.unsolicited == UINT64_C(1));

    oamr_mb_arm(&parser);
    CHECK(oamr_mb_push(&parser, (uint8_t)'R', &out) == OAMR_MB_PUSH_NONE);
    CHECK(oamr_mb_push(&parser, (uint8_t)'2', &out) == OAMR_MB_PUSH_NONE);
    oamr_mb_flush(&parser);
    CHECK(parser.armed);
    CHECK(parser.length == 0u);
    CHECK(!parser.in_frame);
    CHECK(oamr_mb_push(&parser, (uint8_t)'R', &out) == OAMR_MB_PUSH_NONE);
    CHECK(oamr_mb_push(&parser, (uint8_t)'3', &out) == OAMR_MB_PUSH_NONE);
    CHECK(oamr_mb_push(&parser, (uint8_t)'4', &out) == OAMR_MB_PUSH_NONE);
    CHECK(oamr_mb_push(&parser, (uint8_t)'\r', &out) == OAMR_MB_PUSH_SAMPLE);
    CHECK(out.range_cm == UINT16_C(34));
    CHECK(out.sequence == UINT32_C(2));
    CHECK(parser.counters.frames_ok == UINT64_C(2));
}

static void unsolicited_and_disarmed_frames_are_rejected(void)
{
    static const char frame[] = "R20\r";
    oamr_mb_t parser;
    oamr_mb_sample_t out = {UINT16_C(0xaaaa), UINT32_C(0x55555555)};
    size_t index;
    unsigned int unsolicited = 0u;

    oamr_mb_init(&parser);
    for (index = 0u; index < strlen(frame); ++index) {
        oamr_mb_push_t result = oamr_mb_push(&parser, (uint8_t)frame[index], &out);
        CHECK(result != OAMR_MB_PUSH_SAMPLE);
        if (result == OAMR_MB_PUSH_UNSOLICITED) {
            ++unsolicited;
        }
        CHECK(out.range_cm == UINT16_C(0xaaaa));
        CHECK(out.sequence == UINT32_C(0x55555555));
    }
    CHECK(unsolicited == 1u);
    CHECK(parser.counters.unsolicited == UINT64_C(1));
    CHECK(parser.counters.frames_ok == UINT64_C(0));

    oamr_mb_arm(&parser);
    CHECK(parser.armed);
    oamr_mb_disarm(&parser);
    CHECK(!parser.armed);
    for (index = 0u; index < strlen(frame); ++index) {
        CHECK(oamr_mb_push(&parser, (uint8_t)frame[index], &out) !=
              OAMR_MB_PUSH_SAMPLE);
    }
    CHECK(parser.counters.unsolicited == UINT64_C(2));
    CHECK(parser.counters.frames_ok == UINT64_C(0));
}

static void restarted_partial_frame_does_not_leak_old_digits(void)
{
    oamr_mb_t parser;
    oamr_mb_sample_t out = {0u, 0u};
    static const char stream[] = "R98R7\r";
    size_t index;
    unsigned int samples = 0u;
    unsigned int malformed = 0u;

    oamr_mb_init(&parser);
    oamr_mb_arm(&parser);
    for (index = 0u; index < strlen(stream); ++index) {
        oamr_mb_push_t result = oamr_mb_push(&parser, (uint8_t)stream[index], &out);
        if (result == OAMR_MB_PUSH_SAMPLE) {
            ++samples;
            CHECK(out.range_cm == UINT16_C(7));
        }
        if (result == OAMR_MB_PUSH_MALFORMED) {
            ++malformed;
        }
    }
    CHECK(samples == 1u);
    CHECK(malformed == 1u);
    CHECK(parser.counters.frames_ok == UINT64_C(1));
}

static void null_calls_do_not_consume_or_write(void)
{
    oamr_mb_t parser;
    oamr_mb_sample_t out = {UINT16_C(0xaaaa), UINT32_C(0x55555555)};

    oamr_mb_init(NULL);
    oamr_mb_arm(NULL);
    oamr_mb_disarm(NULL);
    oamr_mb_flush(NULL);
    CHECK(oamr_mb_push(NULL, (uint8_t)'R', &out) == OAMR_MB_PUSH_INVALID);
    CHECK(out.range_cm == UINT16_C(0xaaaa));
    CHECK(out.sequence == UINT32_C(0x55555555));

    oamr_mb_init(&parser);
    oamr_mb_arm(&parser);
    CHECK(oamr_mb_push(&parser, (uint8_t)'R', NULL) == OAMR_MB_PUSH_INVALID);
    CHECK(!parser.in_frame);
    CHECK(parser.length == 0u);
    CHECK(parser.counters.frames_ok == UINT64_C(0));
    CHECK(parser.counters.malformed == UINT64_C(0));
    CHECK(parser.counters.unsolicited == UINT64_C(0));
    CHECK(parser.counters.bytes_discarded == UINT64_C(0));

    CHECK(oamr_mb_push(&parser, (uint8_t)'R', &out) == OAMR_MB_PUSH_NONE);
    CHECK(oamr_mb_push(&parser, (uint8_t)'0', &out) == OAMR_MB_PUSH_NONE);
    CHECK(oamr_mb_push(&parser, (uint8_t)'\r', &out) == OAMR_MB_PUSH_SAMPLE);
    CHECK(out.range_cm == UINT16_C(0));
    CHECK(out.sequence == UINT32_C(1));
}

int main(void)
{
    RUN(valid_ascii_frames_cover_documented_boundaries);
    RUN(malformed_frames_never_produce_a_sample);
    RUN(every_split_point_yields_exactly_one_sample);
    RUN(arming_discards_partial_data_and_allows_one_reading);
    RUN(unsolicited_and_disarmed_frames_are_rejected);
    RUN(restarted_partial_frame_does_not_leak_old_digits);
    RUN(null_calls_do_not_consume_or_write);
    return TEST_END();
}
