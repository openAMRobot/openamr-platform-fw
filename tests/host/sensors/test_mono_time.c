/* SPDX-License-Identifier: MIT */
/* Conversion of a native-width hardware counter to application time
 * (oamr/mono_time.h). Golden values: counter widths, rates and readings are
 * test inputs chosen here; the expected microseconds are hand arithmetic
 * (no external source). */
#include "oamr/mono_time.h"
#include "test_harness.h"

static void invalid_parameters(void)
{
    oamr_mono_ext_t m;
    CHECK(!oamr_mono_ext_init(&m, 0u, 1u, 0u));
    CHECK(!oamr_mono_ext_init(&m, 33u, 1u, 0u));
    CHECK(!oamr_mono_ext_init(&m, 32u, 0u, 0u));
    CHECK(oamr_mono_ext_update(&m, 1234u) == 0u);
}

static void rollover_32bit_1mhz(void)
{
    /* 32-bit timer at 1 MHz, started 256 us before its rollover. */
    oamr_mono_ext_t m;
    CHECK(oamr_mono_ext_init(&m, 32u, 1u, 0xFFFFFF00u));
    CHECK(oamr_mono_ext_update(&m, 0xFFFFFF00u) == 0u);
    CHECK(oamr_mono_ext_update(&m, 0xFFFFFFFFu) == 255u);
    CHECK(oamr_mono_ext_update(&m, 0x00000000u) == 256u); /* the rollover itself */
    CHECK(oamr_mono_ext_update(&m, 0x00000100u) == 512u);
    /* Several further periods, each observed at least once per period. */
    uint64_t expect = 512u;
    uint32_t raw = 0x00000100u;
    for (int i = 0; i < 6; i++) {
        raw += 0x80000000u; /* half a period per call */
        expect += 0x80000000u;
        CHECK(oamr_mono_ext_update(&m, raw) == expect);
    }
    CHECK(expect > 0xFFFFFFFFull); /* application time passed 2^32 us */
}

static void rollover_16bit_counter(void)
{
    /* 16-bit counter (for example an FDCAN time stamp counter) at 4 ticks
     * per microsecond; bits above the width are ignored. */
    oamr_mono_ext_t m;
    CHECK(oamr_mono_ext_init(&m, 16u, 4u, 0xFFF0u));
    CHECK(oamr_mono_ext_update(&m, 0xFFFCu) == 3u);   /* 12 ticks */
    CHECK(oamr_mono_ext_update(&m, 0x0004u) == 5u);   /* 20 ticks, across the rollover */
    CHECK(oamr_mono_ext_update(&m, 0xABCD0006u) == 5u); /* 22 ticks: fraction kept */
    CHECK(oamr_mono_ext_update(&m, 0x0008u) == 6u);   /* 24 ticks */
    CHECK(m.ticks == 24u);
}

static void unchanged_reading_does_not_advance(void)
{
    oamr_mono_ext_t m;
    CHECK(oamr_mono_ext_init(&m, 32u, 1u, 100u));
    CHECK(oamr_mono_ext_update(&m, 150u) == 50u);
    CHECK(oamr_mono_ext_update(&m, 150u) == 50u);
}

static void elapsed_saturates(void)
{
    CHECK(oamr_mono_elapsed_us(10u, 4u) == 6u);
    CHECK(oamr_mono_elapsed_us(4u, 10u) == 0u);
    CHECK(oamr_mono_elapsed_us(0x100000000ull, 0xFFFFFFFFull) == 1u);
}

int main(void)
{
    RUN(invalid_parameters);
    RUN(rollover_32bit_1mhz);
    RUN(rollover_16bit_counter);
    RUN(unchanged_reading_does_not_advance);
    RUN(elapsed_saturates);
    TEST_MAIN_END();
}
