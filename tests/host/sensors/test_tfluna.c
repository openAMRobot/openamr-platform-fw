/* SPDX-License-Identifier: MIT */
#include <string.h>

#include "oamr/tfluna.h"
#include "test_harness.h"

/* Build a valid 9-byte frame. */
static void make(uint8_t f[9], uint16_t dist, uint16_t amp, uint16_t temp)
{
    f[0] = 0x59u;
    f[1] = 0x59u;
    f[2] = (uint8_t)(dist & 0xFFu);
    f[3] = (uint8_t)(dist >> 8);
    f[4] = (uint8_t)(amp & 0xFFu);
    f[5] = (uint8_t)(amp >> 8);
    f[6] = (uint8_t)(temp & 0xFFu);
    f[7] = (uint8_t)(temp >> 8);
    unsigned s = 0u;
    for (int i = 0; i < 8; i++) {
        s += f[i];
    }
    f[8] = (uint8_t)(s & 0xFFu);
}

static int feed(oamr_tfl_t *p, const uint8_t *b, size_t n, oamr_tfl_sample_t *out, int *checksum_errs)
{
    int samples = 0;
    for (size_t i = 0; i < n; i++) {
        oamr_tfl_push_t r = oamr_tfl_push(p, b[i], &out[samples]);
        if (r == OAMR_TFL_PUSH_SAMPLE) {
            samples++;
        } else if (r == OAMR_TFL_PUSH_CHECKSUM_ERROR && checksum_errs) {
            (*checksum_errs)++;
        }
    }
    return samples;
}

static void golden(void)
{
    /* 59 59 2C 01 F4 01 00 09 checksum: dist 300 cm, amp 500, temp 0x0900 = 2304 -> 32 degC */
    const uint8_t g[9] = {0x59, 0x59, 0x2C, 0x01, 0xF4, 0x01, 0x00, 0x09, 0x00};
    uint8_t f[9];
    memcpy(f, g, 9);
    unsigned s = 0;
    for (int i = 0; i < 8; i++) {
        s += f[i];
    }
    f[8] = (uint8_t)s;
    CHECK(f[8] == 0xDDu); /* 0x1DD low byte, computed by hand */
    oamr_tfl_sample_t out;
    CHECK(oamr_tfl_decode(f, OAMR_TFL_UNIT_CM, &out));
    CHECK(out.valid);
    CHECK(out.status == OAMR_TFL_OK);
    CHECK(out.raw_distance == 300u);
    CHECK(out.distance_mm == 3000u);
    CHECK(out.strength == 500u);
    CHECK_NEAR(out.temperature_c, 32.0, 1e-6);
    CHECK(oamr_tfl_decode(f, OAMR_TFL_UNIT_MM, &out));
    CHECK(out.distance_mm == 300u);
    CHECK(out.status == OAMR_TFL_OK);
    CHECK(!oamr_tfl_decode(f, OAMR_TFL_UNIT_UNSET, &out));
}

static void not_configured(void)
{
    oamr_tfl_t p;
    oamr_tfl_config_t c = {OAMR_TFL_UNIT_UNSET};
    CHECK(!oamr_tfl_init(&p, &c));
    oamr_tfl_sample_t s;
    CHECK(oamr_tfl_push(&p, 0x59u, &s) == OAMR_TFL_PUSH_NOT_CONFIGURED);
    CHECK(!oamr_tfl_init(&p, NULL));
}

static void classification(void)
{
    uint8_t f[9];
    oamr_tfl_sample_t s;
    struct {
        uint16_t dist, amp;
        oamr_tfl_status_t st;
    } cases[] = {
        {0u, 50u, OAMR_TFL_NO_RETURN},     /* dummy_dist with weak signal */
        {0u, 500u, OAMR_TFL_NO_RETURN},    /* zero is never a distance */
        {150u, 99u, OAMR_TFL_WEAK},        /* amp just below threshold */
        {150u, 100u, OAMR_TFL_OK},         /* amp at threshold */
        {19u, 1000u, OAMR_TFL_BELOW_MIN},  /* 190 mm inside blind zone */
        {20u, 1000u, OAMR_TFL_OK},         /* exactly 200 mm */
        {800u, 1000u, OAMR_TFL_OK},        /* exactly 8000 mm */
        {801u, 1000u, OAMR_TFL_ABOVE_MAX},
        {150u, 65535u, OAMR_TFL_OVEREXPOSED},
        {150u, 32769u, OAMR_TFL_OVEREXPOSED},
        {150u, 32768u, OAMR_TFL_OK},
        {0u, 65535u, OAMR_TFL_OVEREXPOSED},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        make(f, cases[i].dist, cases[i].amp, 2048u);
        CHECK(oamr_tfl_decode(f, OAMR_TFL_UNIT_CM, &s));
        CHECK(s.status == cases[i].st);
        CHECK(s.valid == (cases[i].st == OAMR_TFL_OK));
    }
    /* temperature boundaries: Temp/8 - 256 */
    make(f, 100u, 1000u, 0u);
    CHECK(oamr_tfl_decode(f, OAMR_TFL_UNIT_CM, &s));
    CHECK_NEAR(s.temperature_c, -256.0, 1e-6);
    make(f, 100u, 1000u, 0xFFFFu);
    CHECK(oamr_tfl_decode(f, OAMR_TFL_UNIT_CM, &s));
    CHECK_NEAR(s.temperature_c, 65535.0 / 8.0 - 256.0, 1e-3);
}

static void stream_and_resync(void)
{
    oamr_tfl_t p;
    oamr_tfl_config_t c = {OAMR_TFL_UNIT_CM};
    CHECK(oamr_tfl_init(&p, &c));
    uint8_t a[9], b[9];
    make(a, 120u, 900u, 2100u);
    make(b, 130u, 800u, 2100u);
    uint8_t stream[64];
    size_t n = 0;
    const uint8_t garbage[] = {0x00, 0x59, 0x13, 0xFF, 0x5A, 0x04, 0x04, 0x00};
    memcpy(&stream[n], garbage, sizeof(garbage));
    n += sizeof(garbage);
    memcpy(&stream[n], a, 9);
    n += 9;
    memcpy(&stream[n], b, 9);
    n += 9;
    oamr_tfl_sample_t out[8];
    int errs = 0;
    CHECK(feed(&p, stream, n, out, &errs) == 2);
    CHECK(errs == 0);
    CHECK(out[0].raw_distance == 120u && out[0].seq == 1u);
    CHECK(out[1].raw_distance == 130u && out[1].seq == 2u);
    CHECK(p.counters.bytes_discarded == sizeof(garbage));
}

static void bad_checksum_then_good(void)
{
    oamr_tfl_t p;
    oamr_tfl_config_t c = {OAMR_TFL_UNIT_CM};
    CHECK(oamr_tfl_init(&p, &c));
    uint8_t a[9], b[9];
    make(a, 120u, 900u, 2100u);
    make(b, 140u, 900u, 2100u);
    a[8] ^= 0x01u;
    uint8_t stream[18];
    memcpy(stream, a, 9);
    memcpy(&stream[9], b, 9);
    oamr_tfl_sample_t out[4];
    int errs = 0;
    CHECK(feed(&p, stream, sizeof(stream), out, &errs) == 1);
    CHECK(errs == 1);
    CHECK(out[0].raw_distance == 140u);
    CHECK(p.counters.checksum_errors == 1u);
}

static void truncated_frame_swallowed_header(void)
{
    /* A truncated frame (5 bytes) followed by a full frame: the parser must
     * recover the full frame via resync after the checksum failure. */
    oamr_tfl_t p;
    oamr_tfl_config_t c = {OAMR_TFL_UNIT_CM};
    CHECK(oamr_tfl_init(&p, &c));
    uint8_t a[9], b[9], d[9];
    make(a, 111u, 900u, 2100u);
    make(b, 222u, 900u, 2100u);
    make(d, 233u, 900u, 2100u);
    uint8_t stream[32];
    memcpy(stream, a, 5);
    memcpy(&stream[5], b, 9);
    memcpy(&stream[14], d, 9);
    oamr_tfl_sample_t out[4];
    int errs = 0;
    int n = feed(&p, stream, 23, out, &errs);
    CHECK(errs >= 1);
    CHECK(n == 2);
    CHECK(n >= 1 && out[0].raw_distance == 222u);
    CHECK(n >= 2 && out[1].raw_distance == 233u);
    /* flush drops a partial frame */
    oamr_tfl_push(&p, 0x59u, &out[0]);
    oamr_tfl_flush(&p);
    CHECK(p.len == 0u);
}

static void duplicate_frames_get_new_seq(void)
{
    oamr_tfl_t p;
    oamr_tfl_config_t c = {OAMR_TFL_UNIT_CM};
    CHECK(oamr_tfl_init(&p, &c));
    uint8_t a[9];
    make(a, 100u, 900u, 2100u);
    oamr_tfl_sample_t out[4];
    CHECK(feed(&p, a, 9, out, NULL) == 1);
    CHECK(feed(&p, a, 9, &out[1], NULL) == 1);
    CHECK(out[0].seq == 1u && out[1].seq == 2u);
    /* No byte -> no sample: nothing old is handed out again. */
    oamr_tfl_sample_t s;
    memset(&s, 0xAB, sizeof(s));
    CHECK(oamr_tfl_push(&p, 0x00u, &s) == OAMR_TFL_PUSH_NONE);
}

static void fuzz(void)
{
    oamr_tfl_t p;
    oamr_tfl_config_t c = {OAMR_TFL_UNIT_CM};
    CHECK(oamr_tfl_init(&p, &c));
    oamr_tfl_sample_t s;
    unsigned samples = 0u, bad = 0u;
    for (int i = 0; i < 200000; i++) {
        uint8_t b = (uint8_t)rng_u8();
        if ((i & 7) == 0) {
            b = 0x59u; /* bias towards headers */
        }
        oamr_tfl_push_t r = oamr_tfl_push(&p, b, &s);
        if (r == OAMR_TFL_PUSH_SAMPLE) {
            samples++;
            /* every accepted sample must have passed the checksum and be classified */
            CHECK(s.status != OAMR_TFL_UNKNOWN);
            if (s.valid) {
                CHECK(s.distance_mm >= 200u && s.distance_mm <= 8000u);
            }
        } else if (r == OAMR_TFL_PUSH_CHECKSUM_ERROR) {
            bad++;
        }
        CHECK(p.len < OAMR_TFL_FRAME_LEN);
    }
    CHECK(bad > 0u);
    CHECK(samples == p.counters.frames_ok);
}

int main(void)
{
    RUN(golden);
    RUN(not_configured);
    RUN(classification);
    RUN(stream_and_resync);
    RUN(bad_checksum_then_good);
    RUN(truncated_frame_swallowed_header);
    RUN(duplicate_frames_get_new_seq);
    RUN(fuzz);
    TEST_MAIN_END();
}
