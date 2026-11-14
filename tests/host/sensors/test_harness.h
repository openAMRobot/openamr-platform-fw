/* SPDX-License-Identifier: MIT */
/* Minimal test harness: CHECK records a failure and continues; main returns
 * non-zero if any check failed. */
#ifndef OAMR_TEST_HARNESS_H
#define OAMR_TEST_HARNESS_H

#include <math.h>
#include <stdio.h>

static int g_failures;
static int g_checks;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        g_checks++;                                                                    \
        if (!(cond)) {                                                                 \
            g_failures++;                                                              \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                              \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                          \
    do {                                                                               \
        g_checks++;                                                                    \
        double a_ = (double)(a), b_ = (double)(b);                                     \
        if (!(fabs(a_ - b_) <= (double)(tol))) {                                       \
            g_failures++;                                                              \
            fprintf(stderr, "%s:%d: CHECK_NEAR failed: %s = %.9g, %s = %.9g\n",       \
                    __FILE__, __LINE__, #a, a_, #b, b_);                               \
        }                                                                              \
    } while (0)

#define RUN(test)                                                                      \
    do {                                                                               \
        int before_ = g_failures;                                                      \
        test();                                                                        \
        printf("[%s] %s\n", g_failures == before_ ? "PASS" : "FAIL", #test);          \
    } while (0)

#define TEST_MAIN_END()                                                                \
    do {                                                                               \
        printf("%d checks, %d failures\n", g_checks, g_failures);                      \
        return g_failures == 0 ? 0 : 1;                                                \
    } while (0)

/* Small deterministic PRNG for fuzz-style cases. */
static unsigned long long g_rng = 0x9E3779B97F4A7C15ull;
static inline unsigned rng_u8(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return (unsigned)(g_rng & 0xFFu);
}

#endif
