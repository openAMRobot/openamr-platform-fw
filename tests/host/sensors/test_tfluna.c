/* SPDX-License-Identifier: MIT */
#include "test_harness.h"

#include "oamr/tfluna.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void make_frame(uint8_t frame[OAMR_TFL_FRAME_LEN], uint16_t distance,
                       uint16_t strength, uint16_t temperature)
{
    size_t index;
    uint8_t checksum = 0u;

    frame[0] = (uint8_t)OAMR_TFL_HEADER;
    frame[1] = (uint8_t)OAMR_TFL_HEADER;
    frame[2] = (uint8_t)(distance & UINT16_C(0x00ff));
    frame[3] = (uint8_t)(distance >> 8u);
    frame[4] = (uint8_t)(strength & UINT16_C(0x00ff));
    frame[5] = (uint8_t)(strength >> 8u);
    frame[6] = (uint8_t)(temperature & UINT16_C(0x00ff));
    frame[7] = (uint8_t)(temperature >> 8u);
    for (index = 0u; index < OAMR_TFL_FRAME_LEN - 1u; ++index) {
        checksum = (uint8_t)(checksum + frame[index]);
    }
    frame[8] = checksum;
}

static oamr_tfl_push_t push_stream(oamr_tfl_t *parser, const uint8_t *bytes,
                                   size_t length, oamr_tfl_sample_t *out,
                                   unsigned int *samples,
                                   unsigned int *checksum_errors)
{
    oamr_tfl_push_t last = OAMR_TFL_PUSH_NONE;
    size_t index;

    for (index = 0u; index < length; ++index) {
        last = oamr_tfl_push(parser, bytes[index], out);
        if (last == OAMR_TFL_PUSH_SAMPLE) {
            ++(*samples);
        }
        if (last == OAMR_TFL_PUSH_CHECKSUM_ERROR) {
            ++(*checksum_errors);
        }
    }
    return last;
}

static void golden_cm_and_mm_frames_decode_little_endian_fields(void)
{
    static const uint8_t golden[OAMR_TFL_FRAME_LEN] = {
        0x59u, 0x59u, 0x34u, 0x12u, 0x78u, 0x56u, 0x08u, 0x08u, 0xd6u
    };
    oamr_tfl_sample_t out = {0};
    uint8_t recomputed[OAMR_TFL_FRAME_LEN];

    make_frame(recomputed, UINT16_C(0x1234), UINT16_C(0x5678), UINT16_C(0x0808));
    CHECK(memcmp(golden, recomputed, sizeof(golden)) == 0);
    CHECK(oamr_tfl_decode(golden, OAMR_TFL_UNIT_CM, &out) == OAMR_STATUS_OK);
    CHECK(out.raw_distance == UINT16_C(0x1234));
    CHECK(out.distance_mm == UINT32_C(46600));
    CHECK(out.strength == UINT16_C(0x5678));
    CHECK(out.raw_temperature == UINT16_C(0x0808));
    CHECK(out.temperature_c_x8 == INT32_C(8));
    CHECK(out.sequence == UINT32_C(0));

    out.raw_distance = 0u;
    out.distance_mm = 0u;
    out.strength = 0u;
    out.raw_temperature = 0u;
    out.temperature_c_x8 = 0;
    CHECK(oamr_tfl_decode(golden, OAMR_TFL_UNIT_MM, &out) == OAMR_STATUS_OK);
    CHECK(out.raw_distance == UINT16_C(0x1234));
    CHECK(out.distance_mm == UINT32_C(4660));
    CHECK(out.strength == UINT16_C(0x5678));
    CHECK(out.raw_temperature == UINT16_C(0x0808));
    CHECK(out.temperature_c_x8 == INT32_C(8));
    CHECK(out.sequence == UINT32_C(0));
}

static void decode_rejects_bad_headers_checksums_units_and_nulls(void)
{
    uint8_t frame[OAMR_TFL_FRAME_LEN];
    uint8_t original[OAMR_TFL_FRAME_LEN];
    oamr_tfl_sample_t out = {
        UINT16_C(7), UINT32_C(70), UINT16_C(9),
        UINT16_C(2056), INT32_C(8), UINT32_C(77)
    };
    size_t index;

    make_frame(frame, UINT16_C(123), UINT16_C(456), UINT16_C(2056));
    memcpy(original, frame, sizeof(frame));
    CHECK(oamr_tfl_decode(NULL, OAMR_TFL_UNIT_CM, &out) == OAMR_STATUS_INVALID);
    CHECK(out.raw_distance == UINT16_C(7));
    CHECK(oamr_tfl_decode(frame, OAMR_TFL_UNIT_CM, NULL) == OAMR_STATUS_INVALID);
    CHECK(oamr_tfl_decode(frame, OAMR_TFL_UNIT_UNSET, &out) == OAMR_STATUS_INVALID);
    CHECK(oamr_tfl_decode(frame, (oamr_tfl_unit_t)99, &out) == OAMR_STATUS_INVALID);
    CHECK(memcmp(frame, original, sizeof(frame)) == 0);
    CHECK(out.raw_distance == UINT16_C(7));
    CHECK(out.distance_mm == UINT32_C(70));
    CHECK(out.sequence == UINT32_C(77));

    for (index = 0u; index < 2u; ++index) {
        uint8_t saved = frame[index];
        frame[index] = (uint8_t)(saved + 1u);
        CHECK(oamr_tfl_decode(frame, OAMR_TFL_UNIT_CM, &out) == OAMR_STATUS_INVALID);
        CHECK(out.raw_distance == UINT16_C(7));
        frame[index] = saved;
    }
    ++frame[8];
    CHECK(oamr_tfl_decode(frame, OAMR_TFL_UNIT_CM, &out) == OAMR_STATUS_INVALID);
    CHECK(out.raw_distance == UINT16_C(7));
    CHECK(out.distance_mm == UINT32_C(70));
    CHECK(memcmp(frame, original, sizeof(frame)) != 0);
}

static void configured_stream_accepts_frames_and_assigns_sequences(void)
{
    oamr_tfl_config_t config = {OAMR_TFL_UNIT_CM};
    oamr_tfl_t parser;
    oamr_tfl_sample_t out = {0};
    uint8_t frame[OAMR_TFL_FRAME_LEN];
    size_t split;

    make_frame(frame, UINT16_C(0x0102), UINT16_C(0x0304), UINT16_C(2048));
    for (split = 0u; split <= OAMR_TFL_FRAME_LEN; ++split) {
        unsigned int samples = 0u;
        unsigned int errors = 0u;

        CHECK(oamr_tfl_init(&parser, &config) == OAMR_STATUS_OK);
        CHECK(parser.configured);
        (void)push_stream(&parser, frame, split, &out, &samples, &errors);
        CHECK(samples == (split == OAMR_TFL_FRAME_LEN ? 1u : 0u));
        (void)push_stream(&parser, frame + split, OAMR_TFL_FRAME_LEN - split,
                          &out, &samples, &errors);
        CHECK(samples == 1u);
        CHECK(errors == 0u);
        CHECK(out.raw_distance == UINT16_C(0x0102));
        CHECK(out.distance_mm == UINT32_C(2580));
        CHECK(out.strength == UINT16_C(0x0304));
        CHECK(out.sequence == UINT32_C(1));
        CHECK(parser.counters.frames_ok == UINT64_C(1));
        CHECK(parser.counters.checksum_errors == UINT64_C(0));

        out.raw_distance = 0u;
        (void)push_stream(&parser, frame, OAMR_TFL_FRAME_LEN, &out,
                          &samples, &errors);
        CHECK(samples == 2u);
        CHECK(out.sequence == UINT32_C(2));
    }
}

static void noise_and_truncated_input_do_not_emit_measurements(void)
{
    static const uint8_t noise[] = {
        0x00u, 0x12u, 0x59u, 0x58u, 0x59u, 0x01u, 0xffu
    };
    oamr_tfl_config_t config = {OAMR_TFL_UNIT_MM};
    oamr_tfl_t parser;
    oamr_tfl_sample_t out = {0};
    uint8_t frame[OAMR_TFL_FRAME_LEN];
    unsigned int samples = 0u;
    unsigned int errors = 0u;

    CHECK(oamr_tfl_init(&parser, &config) == OAMR_STATUS_OK);
    (void)push_stream(&parser, noise, sizeof(noise), &out, &samples, &errors);
    CHECK(samples == 0u);
    CHECK(errors == 0u);
    CHECK(parser.counters.frames_ok == UINT64_C(0));

    make_frame(frame, UINT16_C(42), UINT16_C(500), UINT16_C(2048));
    (void)push_stream(&parser, frame, OAMR_TFL_FRAME_LEN - 1u, &out,
                      &samples, &errors);
    CHECK(samples == 0u);
    CHECK(parser.counters.frames_ok == UINT64_C(0));

    oamr_tfl_flush(&parser);
    CHECK(parser.length == 0u);
    CHECK(parser.configured);
    (void)push_stream(&parser, frame, OAMR_TFL_FRAME_LEN, &out, &samples, &errors);
    CHECK(samples == 1u);
    CHECK(out.raw_distance == UINT16_C(42));
    CHECK(out.distance_mm == UINT32_C(42));
}

static void checksum_failure_recovers_at_overlapping_headers(void)
{
    oamr_tfl_config_t config = {OAMR_TFL_UNIT_MM};
    oamr_tfl_t parser;
    oamr_tfl_sample_t out = {0};
    uint8_t stream[12] = {
        0x59u, 0x59u, 0x01u,
        0x59u, 0x59u, 0x01u, 0x00u, 0x00u, 0x00u,
        0x10u, 0x00u, 0xc3u
    };
    unsigned int samples = 0u;
    unsigned int errors = 0u;
    oamr_tfl_push_t result;

    CHECK(oamr_tfl_init(&parser, &config) == OAMR_STATUS_OK);
    result = push_stream(&parser, stream, sizeof(stream), &out,
                         &samples, &errors);
    CHECK(result == OAMR_TFL_PUSH_SAMPLE);
    CHECK(samples == 1u);
    CHECK(errors == 1u);
    CHECK(out.raw_distance == UINT16_C(1));
    CHECK(out.distance_mm == UINT32_C(1));
    CHECK(out.strength == UINT16_C(0));
    CHECK(out.raw_temperature == UINT16_C(16));
    CHECK(out.temperature_c_x8 == INT32_C(-2032));
    CHECK(parser.counters.checksum_errors == UINT64_C(1));
    CHECK(parser.counters.frames_ok == UINT64_C(1));
}

static void trailing_header_after_bad_checksum_is_preserved(void)
{
    oamr_tfl_config_t config = {OAMR_TFL_UNIT_CM};
    oamr_tfl_t parser;
    oamr_tfl_sample_t out = {0};
    uint8_t frame[OAMR_TFL_FRAME_LEN];
    unsigned int samples = 0u;
    unsigned int errors = 0u;

    make_frame(frame, UINT16_C(5), UINT16_C(6), UINT16_C(2048));
    ++frame[2];
    frame[8] = (uint8_t)OAMR_TFL_HEADER;
    CHECK(oamr_tfl_init(&parser, &config) == OAMR_STATUS_OK);
    (void)push_stream(&parser, frame, OAMR_TFL_FRAME_LEN, &out,
                      &samples, &errors);
    CHECK(samples == 0u);
    CHECK(errors == 1u);
    CHECK(parser.length == 1u);
    CHECK(parser.frame[0] == (uint8_t)OAMR_TFL_HEADER);

    make_frame(frame, UINT16_C(77), UINT16_C(6), UINT16_C(2048));
    (void)push_stream(&parser, frame + 1u, OAMR_TFL_FRAME_LEN - 1u,
                      &out, &samples, &errors);
    CHECK(samples == 1u);
    CHECK(out.raw_distance == UINT16_C(77));
    CHECK(out.distance_mm == UINT32_C(770));
}

static void unit_is_required_and_raw_extrema_get_no_health_label(void)
{
    oamr_tfl_t parser;
    oamr_tfl_config_t invalid_config = {OAMR_TFL_UNIT_UNSET};
    oamr_tfl_config_t cm_config = {OAMR_TFL_UNIT_CM};
    oamr_tfl_sample_t out = {0};
    uint8_t frame[OAMR_TFL_FRAME_LEN];
    unsigned int samples = 0u;
    unsigned int errors = 0u;

    CHECK(oamr_tfl_init(&parser, NULL) == OAMR_STATUS_INVALID);
    CHECK(!parser.configured);
    CHECK(parser.length == 0u);
    CHECK(oamr_tfl_init(NULL, &cm_config) == OAMR_STATUS_INVALID);
    CHECK(oamr_tfl_init(&parser, &invalid_config) == OAMR_STATUS_INVALID);
    CHECK(!parser.configured);

    make_frame(frame, UINT16_C(0), UINT16_C(0), UINT16_C(2048));
    (void)push_stream(&parser, frame, OAMR_TFL_FRAME_LEN, &out, &samples, &errors);
    CHECK(samples == 0u);
    CHECK(errors == 0u);
    CHECK(parser.counters.frames_ok == UINT64_C(0));
    CHECK(parser.counters.checksum_errors == UINT64_C(0));

    CHECK(oamr_tfl_init(&parser, &cm_config) == OAMR_STATUS_OK);
    (void)push_stream(&parser, frame, OAMR_TFL_FRAME_LEN, &out, &samples, &errors);
    CHECK(samples == 1u);
    CHECK(out.raw_distance == UINT16_C(0));
    CHECK(out.distance_mm == UINT32_C(0));

    make_frame(frame, UINT16_MAX, UINT16_MAX, UINT16_MAX);
    (void)push_stream(&parser, frame, OAMR_TFL_FRAME_LEN, &out, &samples, &errors);
    CHECK(samples == 2u);
    CHECK(out.raw_distance == UINT16_MAX);
    CHECK(out.distance_mm == UINT32_C(655350));
    CHECK(out.strength == UINT16_MAX);
    CHECK(out.raw_temperature == UINT16_MAX);
    CHECK(out.temperature_c_x8 == INT32_C(63487));
}

static void parser_state_and_counters_are_independent(void)
{
    oamr_tfl_config_t front_config = {OAMR_TFL_UNIT_CM};
    oamr_tfl_config_t rear_config = {OAMR_TFL_UNIT_MM};
    oamr_tfl_t front;
    oamr_tfl_t rear;
    oamr_tfl_sample_t front_out = {0};
    oamr_tfl_sample_t rear_out = {0};
    uint8_t frame[OAMR_TFL_FRAME_LEN];
    size_t index;

    make_frame(frame, UINT16_C(321), UINT16_C(1000), UINT16_C(2048));
    CHECK(oamr_tfl_init(&front, &front_config) == OAMR_STATUS_OK);
    CHECK(oamr_tfl_init(&rear, &rear_config) == OAMR_STATUS_OK);

    for (index = 0u; index < 4u; ++index) {
        CHECK(oamr_tfl_push(&front, frame[index], &front_out) == OAMR_TFL_PUSH_NONE);
        CHECK(rear.length == 0u);
    }
    for (; index < OAMR_TFL_FRAME_LEN; ++index) {
        oamr_tfl_push(&front, frame[index], &front_out);
    }
    CHECK(front_out.raw_distance == UINT16_C(321));
    CHECK(front_out.distance_mm == UINT32_C(3210));
    CHECK(front_out.sequence == UINT32_C(1));
    CHECK(rear.counters.frames_ok == UINT64_C(0));
    CHECK(rear_out.raw_distance == UINT16_C(0));

    ++frame[8];
    for (index = 0u; index < OAMR_TFL_FRAME_LEN; ++index) {
        oamr_tfl_push(&rear, frame[index], &rear_out);
    }
    CHECK(rear.counters.checksum_errors == UINT64_C(1));
    CHECK(rear.counters.frames_ok == UINT64_C(0));
    CHECK(front.counters.checksum_errors == UINT64_C(0));
    CHECK(front.counters.frames_ok == UINT64_C(1));
    CHECK(front_out.sequence == UINT32_C(1));
    CHECK(rear_out.raw_distance == UINT16_C(0));
}

static void streaming_nulls_and_flush_preserve_state_contract(void)
{
    oamr_tfl_config_t config = {OAMR_TFL_UNIT_MM};
    oamr_tfl_t parser;
    oamr_tfl_sample_t out = {0};

    oamr_tfl_flush(NULL);
    CHECK(oamr_tfl_push(NULL, 0u, &out) == OAMR_TFL_PUSH_INVALID);
    CHECK(out.raw_distance == 0u);
    CHECK(oamr_tfl_init(&parser, &config) == OAMR_STATUS_OK);
    CHECK(oamr_tfl_push(&parser, (uint8_t)OAMR_TFL_HEADER, NULL) ==
          OAMR_TFL_PUSH_INVALID);
    CHECK(parser.length == 0u);
    CHECK(oamr_tfl_push(&parser, (uint8_t)OAMR_TFL_HEADER, &out) ==
          OAMR_TFL_PUSH_NONE);
    CHECK(parser.length == 1u);
    oamr_tfl_flush(&parser);
    CHECK(parser.length == 0u);
    CHECK(parser.configured);
    CHECK(parser.config.unit == OAMR_TFL_UNIT_MM);
}

int main(void)
{
    RUN(golden_cm_and_mm_frames_decode_little_endian_fields);
    RUN(decode_rejects_bad_headers_checksums_units_and_nulls);
    RUN(configured_stream_accepts_frames_and_assigns_sequences);
    RUN(noise_and_truncated_input_do_not_emit_measurements);
    RUN(checksum_failure_recovers_at_overlapping_headers);
    RUN(trailing_header_after_bad_checksum_is_preserved);
    RUN(unit_is_required_and_raw_extrema_get_no_health_label);
    RUN(parser_state_and_counters_are_independent);
    RUN(streaming_nulls_and_flush_preserve_state_contract);
    return TEST_END();
}
