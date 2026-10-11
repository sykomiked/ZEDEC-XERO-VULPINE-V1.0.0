/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ti_test.h — the check macro shared by the Tri-Isolate tests. Test use
 * only. */
#ifndef TI_TEST_H
#define TI_TEST_H

#include <stdio.h>

#if defined(__GNUC__)
#define TI_UNUSED __attribute__((unused))
#else
#define TI_UNUSED
#endif

static int g_checks TI_UNUSED, g_fail TI_UNUSED;

#define CHECK(cond, ...)                                                                       \
    do {                                                                                       \
        g_checks++;                                                                            \
        if (!(cond)) {                                                                         \
            g_fail++;                                                                          \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                               \
            fprintf(stderr, __VA_ARGS__);                                                      \
            fprintf(stderr, "\n");                                                             \
        }                                                                                      \
    } while (0)

#endif /* TI_TEST_H */
