/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zf_test.h — the check macro shared by the z-ffi tests. */
#ifndef ZF_TEST_H
#define ZF_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zffi.h"

static int g_checks, g_fail;

static inline const char *last_err(void) {
    static char b[600];
    size_t n = 0;
    if (zf_last_error(b, sizeof b, &n) != ZXV_OK) b[0] = '\0';
    return b;
}

#define CHECK(cond, ...)                                                                       \
    do {                                                                                       \
        g_checks++;                                                                            \
        if (!(cond)) {                                                                         \
            g_fail++;                                                                          \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                               \
            fprintf(stderr, __VA_ARGS__);                                                      \
            fprintf(stderr, " [last error: %s]\n", last_err());                                \
        }                                                                                      \
    } while (0)

/* The last error contains `needle`. */
static inline int err_has(const char *needle) { return strstr(last_err(), needle) != NULL; }

#endif /* ZF_TEST_H */
