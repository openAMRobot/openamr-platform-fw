/* SPDX-License-Identifier: MIT */
/* Golden values in this file: all limits and stamps are test inputs chosen
 * here; expected states follow from the rule in freshness.h (no external
 * source). */
#include "oamr/freshness.h"
#include "test_harness.h"

static void not_configured(void)
{
    oamr_fresh_t f;
    oamr_fresh_config_t c = {0u};
    CHECK(!oamr_fresh_init(&f, &c));
    CHECK(!oamr_fresh_init(&f, NULL));
    oamr_fresh_mark(&f, 10u);
    CHECK(oamr_fresh_classify(&f, 10u) == OAMR_FRESH_UNKNOWN);
    uint64_t age = 7u;
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
    uint64_t age = 0u;
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

static void past_32_bits(void)
{
    /* Application time is 64 bit: a sample just below 2^32 us and "now"
     * just above it are 512 us apart, with no wrap involved. */
    oamr_fresh_t f;
    oamr_fresh_config_t c = {200000u};
    CHECK(oamr_fresh_init(&f, &c));
    oamr_fresh_mark(&f, 0xFFFFFF00ull);
    CHECK(oamr_fresh_classify(&f, 0x100000100ull) == OAMR_FRESH_FRESH);
    uint64_t age = 0u;
    CHECK(oamr_fresh_age(&f, 0x100000100ull, &age));
    CHECK(age == 512u);
    CHECK(oamr_fresh_classify(&f, 0xFFFFFF00ull + 200000u) == OAMR_FRESH_STALE);
    /* A large limit above 2^32 us is accepted. */
    oamr_fresh_config_t big = {0x200000000ull};
    CHECK(oamr_fresh_init(&f, &big));
    oamr_fresh_mark(&f, 0u);
    CHECK(oamr_fresh_classify(&f, 0x1FFFFFFFFull) == OAMR_FRESH_FRESH);
}

static void future_stamp_is_stale(void)
{
    oamr_fresh_t f;
    oamr_fresh_config_t c = {200000u};
    CHECK(oamr_fresh_init(&f, &c));
    oamr_fresh_mark(&f, 5000u);
    /* "now" before the stamp: caller error, STALE, age not reported */
    CHECK(oamr_fresh_classify(&f, 4000u) == OAMR_FRESH_STALE);
    uint64_t age = 0u;
    CHECK(!oamr_fresh_age(&f, 4000u, &age));
}

static void long_gap_stays_stale(void)
{
    oamr_fresh_t f;
    oamr_fresh_config_t c = {200000u};
    CHECK(oamr_fresh_init(&f, &c));
    oamr_fresh_mark(&f, 0u);
    /* 2^32 us later (where a 32-bit clock would read a small age again) */
    CHECK(oamr_fresh_classify(&f, 0x100000064ull) == OAMR_FRESH_STALE);
}

int main(void)
{
    RUN(not_configured);
    RUN(boundaries);
    RUN(past_32_bits);
    RUN(future_stamp_is_stale);
    RUN(long_gap_stays_stale);
    TEST_MAIN_END();
}
