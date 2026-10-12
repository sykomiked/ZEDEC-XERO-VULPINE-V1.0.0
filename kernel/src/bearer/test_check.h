/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_check.h — the tiny check harness shared by the bearer host tests:
 * one "[PASS] ..." line per check, a final "N checks" line, exit 1 on any
 * failure. Host tests only (-DTEST_HOST). */
#ifndef ZXV_BR_TEST_CHECK_H
#define ZXV_BR_TEST_CHECK_H

#include <stdio.h>
#include <stdlib.h>

static int g_checks, g_failed;

#define CHECK(name, cond)                                                                      \
    do {                                                                                       \
        g_checks++;                                                                            \
        if (cond) {                                                                            \
            printf("[PASS] %s\n", (name));                                                     \
        } else {                                                                               \
            g_failed++;                                                                        \
            printf("[FAIL] %s (%s:%d)\n", (name), __FILE__, __LINE__);                         \
        }                                                                                      \
    } while (0)

static int check_summary(void)
{
    if (g_failed) {
        printf("FAILED %d of %d checks\n", g_failed, g_checks);
        return 1;
    }
    printf("%d checks\n", g_checks);
    return 0;
}

#endif
