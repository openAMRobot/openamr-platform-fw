/* SPDX-License-Identifier: MIT */
#ifndef OAMR_TEST_HARNESS_H
#define OAMR_TEST_HARNESS_H

#include <stdio.h>

/*
 * Small C11 host-test harness. A failed CHECK records a diagnostic and lets the
 * current test continue. RUN and TEST_END are intended for use in main().
 */
typedef struct oamr_test_context {
    unsigned int tests_run;
    unsigned int tests_failed;
    unsigned int checks_run;
    unsigned int checks_failed;
    unsigned int case_checks_run;
    unsigned int case_checks_failed;
    const char *case_name;
    int suppress_output;
} oamr_test_context;

typedef void (*oamr_test_function)(void);

static oamr_test_context oamr_test_global = {0};
static oamr_test_context *oamr_test_active = &oamr_test_global;

static inline void oamr_test_check(oamr_test_context *context, int condition,
                                   const char *expression, const char *file,
                                   int line)
{
    context->checks_run += 1u;
    context->case_checks_run += 1u;

    if (condition == 0) {
        context->checks_failed += 1u;
        context->case_checks_failed += 1u;
        if (context->suppress_output == 0) {
            (void)fprintf(stderr, "%s:%d: %s: CHECK(%s) failed\n", file, line,
                          context->case_name != NULL ? context->case_name : "no active test",
                          expression);
        }
    }
}

static inline void oamr_test_run(oamr_test_context *context,
                                 oamr_test_function test_function,
                                 const char *name)
{
    oamr_test_context *previous_context = oamr_test_active;

    context->tests_run += 1u;
    context->case_checks_run = 0u;
    context->case_checks_failed = 0u;
    context->case_name = name;
    oamr_test_active = context;

    test_function();

    oamr_test_active = previous_context;
    if (context->case_checks_run == 0u) {
        context->tests_failed += 1u;
        if (context->suppress_output == 0) {
            (void)fprintf(stderr, "FAIL: %s (no checks executed)\n", name);
        }
    } else if (context->case_checks_failed != 0u) {
        context->tests_failed += 1u;
        if (context->suppress_output == 0) {
            (void)fprintf(stderr, "FAIL: %s (%u of %u checks failed)\n", name,
                          context->case_checks_failed, context->case_checks_run);
        }
    } else if (context->suppress_output == 0) {
        (void)printf("PASS: %s (%u checks)\n", name, context->case_checks_run);
    }
    context->case_name = NULL;
}

static inline int oamr_test_finish(oamr_test_context *context)
{
    if (context->suppress_output == 0) {
        (void)printf("Summary: %u tests run, %u tests failed; "
                     "%u checks run, %u checks failed\n",
                     context->tests_run, context->tests_failed,
                     context->checks_run, context->checks_failed);
    }
    return context->tests_failed == 0u ? 0 : 1;
}

#define CHECK(condition)                                                        \
    do {                                                                        \
        oamr_test_check(oamr_test_active, (condition) ? 1 : 0, #condition,      \
                        __FILE__, __LINE__);                                    \
    } while (0)

#define RUN(test_function)                                                      \
    do {                                                                        \
        oamr_test_run(&oamr_test_global, (test_function), #test_function);       \
    } while (0)

#define TEST_END() oamr_test_finish(&oamr_test_global)

#endif
