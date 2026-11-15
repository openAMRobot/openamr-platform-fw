/* SPDX-License-Identifier: MIT */
#ifndef TLM_TEST_HARNESS_H
#define TLM_TEST_HARNESS_H
#include <stdio.h>
static int g_failures;
static int g_checks;
#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if (!(cond))                                                                               \
        {                                                                                          \
            g_failures++;                                                                          \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
        }                                                                                          \
    } while (0)
#define RUN(t)                                                                                     \
    do                                                                                             \
    {                                                                                              \
        int before_ = g_failures;                                                                  \
        t();                                                                                       \
        printf("[%s] %s\n", g_failures == before_ ? "PASS" : "FAIL", #t);                          \
    } while (0)
#define TEST_END()                                                                                 \
    do                                                                                             \
    {                                                                                              \
        printf("%d checks, %d failures\n", g_checks, g_failures);                                  \
        return g_failures == 0 ? 0 : 1;                                                            \
    } while (0)
#endif
