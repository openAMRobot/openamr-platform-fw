/* SPDX-License-Identifier: MIT */
#include "oamr/freshness.h"
#include "test_harness.h"

static void not_configured(void)
{
    oamr_fresh_t f;
    oamr_fresh_config_t c = {0u};
    CHECK(!oamr_fresh_init(&f, &c));
    CHECK(!oamr_fresh_init(&f, NULL));
    c.stale_after_us = 0x80000000u;
    CHECK(!oamr_fresh_init(&f, &c));
    oamr_fresh_mark(&f, 10u);
    CHECK(oamr_fresh_classify(&f, 10u) == OAMR_FRESH_UNKNOWN);
    uint32_t age = 7u;
    CHECK(!oamr_fresh_age(&f, 10u, &age));
    CHECK(age == 7u);
}

static void boundaries(void)
{
    oamr_fresh_t f;
    oamr_fresh_config_t c = {500000u};
    CHECK(oamr_fresh_init(&f, &c));
    CHECK(oamr_fresh_classify(&f, 0u) == OAMR_FRESH_NEVER);
    oamr_fresh_mark(&f, 1000u);
    CHECK(oamr_fresh_classify(&f, 1000u) == OAMR_FRESH_FRESH);
    CHECK(oamr_fresh_classify(&f, 1000u + 499999u) == OAMR_FRESH_FRESH);
    CHECK(oamr_fresh_classify(&f, 1000u + 500000u) == OAMR_FRESH_STALE);
    uint32_t age = 0u;
    CHECK(oamr_fresh_age(&f, 1000u + 500000u, &age));
    CHECK(age == 500000u);
    /* latched: going back in time does not revive it */
    CHECK(oamr_fresh_classify(&f, 1000u) == OAMR_FRESH_STALE);
    oamr_fresh_mark(&f, 1600000u);
    CHECK(oamr_fresh_classify(&f, 1600001u) == OAMR_FRESH_FRESH);
    CHECK(f.samples == 2u);
    oamr_fresh_reset(&f);
    CHECK(oamr_fresh_classify(&f, 1600001u) == OAMR_FRESH_NEVER);
}

static void clock_wrap(void)
{
    oamr_fresh_t f;
    oamr_fresh_config_t c = {200000u};
    CHECK(oamr_fresh_init(&f, &c));
    oamr_fresh_mark(&f, 0xFFFFFF00u);
    CHECK(oamr_fresh_classify(&f, 0x00000100u) == OAMR_FRESH_FRESH); /* age 512 us across wrap */
    uint32_t age = 0u;
    CHECK(oamr_fresh_age(&f, 0x00000100u, &age));
    CHECK(age == 512u);
    CHECK(oamr_fresh_classify(&f, (uint32_t)(0xFFFFFF00u + 200000u)) == OAMR_FRESH_STALE);
}

static void future_stamp_is_stale(void)
{
    oamr_fresh_t f;
    oamr_fresh_config_t c = {200000u};
    CHECK(oamr_fresh_init(&f, &c));
    oamr_fresh_mark(&f, 5000u);
    /* "now" before the stamp: age wraps to > 2^31 -> STALE, age not reported */
    CHECK(oamr_fresh_classify(&f, 4000u) == OAMR_FRESH_STALE);
    uint32_t age = 0u;
    CHECK(!oamr_fresh_age(&f, 4000u, &age));
}

static void long_gap_does_not_revive(void)
{
    oamr_fresh_t f;
    oamr_fresh_config_t c = {200000u};
    CHECK(oamr_fresh_init(&f, &c));
    oamr_fresh_mark(&f, 0u);
    CHECK(oamr_fresh_classify(&f, 300000u) == OAMR_FRESH_STALE);
    /* one full clock period later the raw age would be small again */
    CHECK(oamr_fresh_classify(&f, 100u) == OAMR_FRESH_STALE);
}

int main(void)
{
    RUN(not_configured);
    RUN(boundaries);
    RUN(clock_wrap);
    RUN(future_stamp_is_stale);
    RUN(long_gap_does_not_revive);
    TEST_MAIN_END();
}
