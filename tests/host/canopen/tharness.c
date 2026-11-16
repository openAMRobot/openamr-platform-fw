/* SPDX-License-Identifier: MIT */
#include "tharness.h"

int th_failures = 0;
int th_checks = 0;

int th_run(const char *suite, const th_case *cases, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        const int before = th_failures;
        cases[i].fn();
        printf("[%s] %-48s %s\n", suite, cases[i].name, th_failures == before ? "ok" : "FAIL");
    }
    printf("[%s] %zu cases, %d checks, %d failures\n", suite, n, th_checks, th_failures);
    return th_failures == 0 ? 0 : 1;
}
