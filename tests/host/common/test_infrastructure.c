/* SPDX-License-Identifier: MIT */
#include "test_harness.h"

#include <stddef.h>
#include <stdint.h>

/* Xorshift32: a deterministic, allocation-free PRNG for host tests only. */
typedef struct test_prng {
    uint32_t state;
} test_prng;

static void test_prng_seed(test_prng *prng, uint32_t seed)
{
    /* Zero is the absorbing state of xorshift; map it to a documented value. */
    prng->state = seed == 0u ? 1u : seed;
}

static uint32_t test_prng_next(test_prng *prng)
{
    uint32_t value = prng->state;

    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    prng->state = value;
    return value;
}

static void passing_case(void)
{
    CHECK(1 == 1);
    CHECK(2u > 1u);
}

static void failing_case(void)
{
    CHECK(1 == 0);
    CHECK(2u == 2u);
    CHECK(3u == 4u);
}

static void empty_case(void)
{
    /* No CHECK calls is a successful (if generally unhelpful) test case. */
}

static void harness_counts_successful_checks(void)
{
    oamr_test_context context = {0};

    context.suppress_output = 1;
    oamr_test_run(&context, passing_case, "probe_pass");

    CHECK(context.tests_run == 1u);
    CHECK(context.tests_failed == 0u);
    CHECK(context.checks_run == 2u);
    CHECK(context.checks_failed == 0u);
    CHECK(context.case_checks_run == 2u);
    CHECK(context.case_name == NULL);
    CHECK(oamr_test_finish(&context) == 0);
}

static void harness_counts_failed_checks_per_case(void)
{
    oamr_test_context context = {0};

    context.suppress_output = 1;
    oamr_test_run(&context, passing_case, "probe_pass");
    oamr_test_run(&context, failing_case, "probe_fail");
    oamr_test_run(&context, passing_case, "probe_pass_again");

    CHECK(context.tests_run == 3u);
    CHECK(context.tests_failed == 1u);
    CHECK(context.checks_run == 7u);
    CHECK(context.checks_failed == 2u);
    CHECK(oamr_test_finish(&context) == 1);
}

static void harness_resets_per_case_accounting(void)
{
    oamr_test_context context = {0};

    context.suppress_output = 1;
    oamr_test_run(&context, failing_case, "probe_fail");
    CHECK(context.case_checks_run == 3u);
    CHECK(context.case_checks_failed == 2u);

    oamr_test_run(&context, passing_case, "probe_pass");
    CHECK(context.case_checks_run == 2u);
    CHECK(context.case_checks_failed == 0u);
    CHECK(context.tests_run == 2u);
    CHECK(context.tests_failed == 1u);
}

static void harness_rejects_an_empty_case(void)
{
    oamr_test_context context = {0};

    context.suppress_output = 1;
    oamr_test_run(&context, empty_case, "probe_empty");
    CHECK(context.tests_run == 1u);
    CHECK(context.tests_failed == 1u);
    CHECK(context.checks_run == 0u);
    CHECK(context.case_checks_run == 0u);
    CHECK(context.case_checks_failed == 0u);
    CHECK(oamr_test_finish(&context) == 1);
}

static void prng_matches_a_reference_sequence(void)
{
    static const uint32_t expected[] = {
        0x00042021u,
        0x04080601u,
        0x9dcca8c5u,
        0x1255994fu,
        0x8ef917d1u,
        0x2c6f5bd0u,
        0x25b2331au,
        0x19f91cb2u
    };
    test_prng prng;
    size_t index;

    test_prng_seed(&prng, 1u);
    for (index = 0u; index < sizeof(expected) / sizeof(expected[0]); ++index) {
        CHECK(test_prng_next(&prng) == expected[index]);
    }
}

static void prng_replays_from_the_same_seed(void)
{
    test_prng first;
    test_prng second;
    size_t index;

    test_prng_seed(&first, 0xc001cafeu);
    test_prng_seed(&second, 0xc001cafeu);
    for (index = 0u; index < 1000u; ++index) {
        CHECK(test_prng_next(&first) == test_prng_next(&second));
    }

    test_prng_seed(&second, 0xc001cafeu);
    test_prng_seed(&first, 0xc001cafeu);
    for (index = 0u; index < 1000u; ++index) {
        CHECK(test_prng_next(&first) == test_prng_next(&second));
    }
}

static void prng_handles_the_zero_seed_deterministically(void)
{
    test_prng zero_seed;
    test_prng one_seed;

    test_prng_seed(&zero_seed, 0u);
    test_prng_seed(&one_seed, 1u);
    CHECK(zero_seed.state == 1u);
    CHECK(test_prng_next(&zero_seed) == test_prng_next(&one_seed));
}

int main(void)
{
    RUN(harness_counts_successful_checks);
    RUN(harness_counts_failed_checks_per_case);
    RUN(harness_resets_per_case_accounting);
    RUN(harness_rejects_an_empty_case);
    RUN(prng_matches_a_reference_sequence);
    RUN(prng_replays_from_the_same_seed);
    RUN(prng_handles_the_zero_seed_deterministically);

    return TEST_END();
}
