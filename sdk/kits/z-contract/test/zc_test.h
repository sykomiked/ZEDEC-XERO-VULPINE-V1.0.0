/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zc_test.h — the check macro and helpers shared by the z-contract tests. */
#ifndef ZC_TEST_H
#define ZC_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zcontract.h"

static int g_checks, g_fail;

static inline const char *last_err(void) {
    static char b[600];
    size_t n = 0;
    if (zc_last_error(b, sizeof b, &n) != ZXV_OK) b[0] = '\0';
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

/* Parse a NUL-terminated contract; NULL on failure. */
static inline zc_contract *parse_ok(const char *text) {
    zc_contract *c = NULL;
    if (zc_parse(text, strlen(text), &c, NULL, NULL, NULL, NULL, 0, NULL) != ZXV_OK) return NULL;
    return c;
}

/* The canonical text of a contract, in a static buffer. */
static inline const char *canon(const zc_contract *c) {
    static char b[8192];
    size_t n = 0;
    if (!c || zc_contract_format(c, b, sizeof b, &n) != ZXV_OK) return "<format failed>";
    return b;
}

#endif /* ZC_TEST_H */
