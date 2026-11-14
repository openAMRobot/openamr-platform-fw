/* SPDX-License-Identifier: MIT */
#include <string.h>

#include "oamr/mb7060.h"
#include "test_harness.h"

static oamr_mb_push_t feed_str(oamr_mb_t *p, const char *s, oamr_mb_sample_t *out, int *samples)
{
    oamr_mb_push_t last = OAMR_MB_PUSH_NONE;
    for (size_t i = 0; s[i] != '\0'; i++) {
        oamr_mb_push_t r = oamr_mb_push(p, (uint8_t)s[i], out);
        if (r != OAMR_MB_PUSH_NONE) {
            last = r;
        }
        if (r == OAMR_MB_PUSH_SAMPLE && samples) {
            (*samples)++;
        }
    }
    return last;
}

static void golden_and_classes(void)
{
    struct {
        const char *frame;
        oamr_mb_status_t st;
        uint16_t cm;
    } cases[] = {
        {"R123\r", OAMR_MB_OK, 123u},
        {"R021\r", OAMR_MB_OK, 21u},
        {"R764\r", OAMR_MB_OK, 764u},
        {"R020\r", OAMR_MB_AT_MIN, 20u},   /* minimum reported distance */
        {"R019\r", OAMR_MB_AT_MIN, 19u},   /* not documented, still at/inside min */
        {"R765\r", OAMR_MB_NO_TARGET, 765u},
        {"R000\r", OAMR_MB_FAULT, 0u},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        oamr_mb_t p;
        oamr_mb_init(&p);
        oamr_mb_arm(&p);
        oamr_mb_sample_t s;
        memset(&s, 0, sizeof(s));
        CHECK(feed_str(&p, cases[i].frame, &s, NULL) == OAMR_MB_PUSH_SAMPLE);
        CHECK(s.status == cases[i].st);
        CHECK(s.range_cm == cases[i].cm);
        CHECK(s.valid == (cases[i].st == OAMR_MB_OK));
    }
}

static void malformed(void)
{
    const char *bad[] = {"R1234\r", "R12\r", "R\r", "R12a\r", "R766\r", "R999\r", "R 12\r"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        oamr_mb_t p;
        oamr_mb_init(&p);
        oamr_mb_arm(&p);
        oamr_mb_sample_t s;
        int n = 0;
        feed_str(&p, bad[i], &s, &n);
        CHECK(n == 0);
        CHECK(p.counters.malformed >= 1u);
        CHECK(p.armed); /* still waiting for a good frame for this trigger */
        /* and the parser recovers on the next good frame */
        CHECK(feed_str(&p, "R100\r", &s, &n) == OAMR_MB_PUSH_SAMPLE);
        CHECK(n == 1 && s.range_cm == 100u);
    }
}

static void restart_mid_frame(void)
{
    oamr_mb_t p;
    oamr_mb_init(&p);
    oamr_mb_arm(&p);
    oamr_mb_sample_t s;
    int n = 0;
    feed_str(&p, "R12R345\r", &s, &n);
    CHECK(n == 1);
    CHECK(s.range_cm == 345u);
    CHECK(p.counters.malformed == 1u);
}

static void one_sample_per_trigger(void)
{
    oamr_mb_t p;
    oamr_mb_init(&p);
    oamr_mb_sample_t s;
    int n = 0;
    /* not armed: frame is unsolicited */
    CHECK(feed_str(&p, "R100\r", &s, &n) == OAMR_MB_PUSH_UNSOLICITED);
    CHECK(n == 0);
    oamr_mb_arm(&p);
    CHECK(feed_str(&p, "R101\r", &s, &n) == OAMR_MB_PUSH_SAMPLE);
    CHECK(feed_str(&p, "R102\r", &s, &n) == OAMR_MB_PUSH_UNSOLICITED);
    CHECK(n == 1 && s.range_cm == 101u && s.seq == 1u);
    CHECK(p.counters.unsolicited == 2u);
    /* a partial frame before the trigger is dropped by arm */
    feed_str(&p, "R10", &s, &n);
    oamr_mb_arm(&p);
    CHECK(feed_str(&p, "3\r", &s, &n) == OAMR_MB_PUSH_NONE);
    CHECK(n == 1);
    CHECK(feed_str(&p, "R200\r", &s, &n) == OAMR_MB_PUSH_SAMPLE);
    CHECK(s.range_cm == 200u && s.seq == 2u);
    /* disarm after timeout: late frame is not a sample */
    oamr_mb_arm(&p);
    oamr_mb_disarm(&p);
    CHECK(feed_str(&p, "R300\r", &s, &n) == OAMR_MB_PUSH_UNSOLICITED);
}

static void fuzz(void)
{
    oamr_mb_t p;
    oamr_mb_init(&p);
    oamr_mb_sample_t s;
    const uint8_t alphabet[] = {'R', '\r', '0', '1', '2', '5', '6', '7', '9', 'x', 0x00, 0xFF};
    for (int i = 0; i < 200000; i++) {
        if ((i % 50) == 0) {
            oamr_mb_arm(&p);
        }
        uint8_t b = alphabet[rng_u8() % sizeof(alphabet)];
        if (oamr_mb_push(&p, b, &s) == OAMR_MB_PUSH_SAMPLE) {
            CHECK(s.range_cm <= 765u);
            CHECK(s.status != OAMR_MB_UNKNOWN);
            CHECK(!p.armed);
        }
        CHECK(p.len <= OAMR_MB_FRAME_LEN - 1u);
    }
}

static oamr_mbseq_config_t seq_cfg(void)
{
    oamr_mbseq_config_t c;
    memset(&c, 0, sizeof(c));
    c.n_sensors = 3u;
    c.reading_timeout_us = 120000u; /* test values only */
    c.gap_us = 10000u;
    c.gap_set = true;
    return c;
}

static void seq_not_configured(void)
{
    oamr_mbseq_t s;
    oamr_mbseq_config_t c = seq_cfg();
    c.gap_set = false;
    CHECK(!oamr_mbseq_init(&s, &c));
    CHECK(oamr_mbseq_step(&s, 0u).action == OAMR_MBSEQ_NOT_CONFIGURED);
    c = seq_cfg();
    c.n_sensors = 0u;
    CHECK(!oamr_mbseq_init(&s, &c));
    c = seq_cfg();
    c.n_sensors = 5u;
    CHECK(!oamr_mbseq_init(&s, &c));
    c = seq_cfg();
    c.reading_timeout_us = 0u;
    CHECK(!oamr_mbseq_init(&s, &c));
}

static void seq_round_robin_and_timeout(void)
{
    oamr_mbseq_t s;
    oamr_mbseq_config_t c = seq_cfg();
    CHECK(oamr_mbseq_init(&s, &c));
    uint32_t t = 1000u;
    oamr_mbseq_decision_t d = oamr_mbseq_step(&s, t);
    CHECK(d.action == OAMR_MBSEQ_TRIGGER && d.sensor == 0u);
    oamr_mbseq_triggered(&s, 0u, t);
    CHECK(oamr_mbseq_step(&s, t + 119999u).action == OAMR_MBSEQ_WAIT);
    CHECK(oamr_mbseq_reading(&s, 0u, t + 95000u));
    /* reading for a sensor not waited for is rejected */
    CHECK(!oamr_mbseq_reading(&s, 0u, t + 96000u));
    CHECK(s.late_frames == 1u);
    /* gap */
    CHECK(oamr_mbseq_step(&s, t + 95000u + 9999u).action == OAMR_MBSEQ_WAIT);
    d = oamr_mbseq_step(&s, t + 95000u + 10000u);
    CHECK(d.action == OAMR_MBSEQ_TRIGGER && d.sensor == 1u);
    t = t + 105000u;
    oamr_mbseq_triggered(&s, 1u, t);
    CHECK(!oamr_mbseq_reading(&s, 2u, t + 1000u)); /* wrong sensor */
    d = oamr_mbseq_step(&s, t + 120000u);
    CHECK(d.action == OAMR_MBSEQ_TIMED_OUT && d.sensor == 1u);
    CHECK(s.timeouts[1] == 1u);
    /* timeout reported once */
    CHECK(oamr_mbseq_step(&s, t + 120001u).action == OAMR_MBSEQ_WAIT);
    /* late frame after timeout is rejected */
    CHECK(!oamr_mbseq_reading(&s, 1u, t + 125000u));
    d = oamr_mbseq_step(&s, t + 130000u);
    CHECK(d.action == OAMR_MBSEQ_TRIGGER && d.sensor == 2u);
    oamr_mbseq_triggered(&s, 2u, t + 130000u);
    CHECK(oamr_mbseq_reading(&s, 2u, t + 200000u));
    d = oamr_mbseq_step(&s, t + 300000u);
    CHECK(d.action == OAMR_MBSEQ_TRIGGER && d.sensor == 0u);
}

static void seq_clock_wrap(void)
{
    oamr_mbseq_t s;
    oamr_mbseq_config_t c = seq_cfg();
    CHECK(oamr_mbseq_init(&s, &c));
    const uint32_t t = 0xFFFFF000u;
    oamr_mbseq_triggered(&s, 0u, t);
    CHECK(oamr_mbseq_step(&s, t + 1000u).action == OAMR_MBSEQ_WAIT);
    CHECK(oamr_mbseq_step(&s, (uint32_t)(t + 120000u)).action == OAMR_MBSEQ_TIMED_OUT);
}

int main(void)
{
    RUN(golden_and_classes);
    RUN(malformed);
    RUN(restart_mid_frame);
    RUN(one_sample_per_trigger);
    RUN(fuzz);
    RUN(seq_not_configured);
    RUN(seq_round_robin_and_timeout);
    RUN(seq_clock_wrap);
    TEST_MAIN_END();
}
