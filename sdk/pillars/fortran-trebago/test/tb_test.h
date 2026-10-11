/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* tb_test.h — the check macro and helpers shared by the fortran-trebago
 * C tests. */
#ifndef TB_TEST_H
#define TB_TEST_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ztrebago.h"

static int g_checks, g_fail;

/* Append to a test buffer; a test whose text would not fit is a bug in the
 * test, so stop rather than truncate. */
static size_t append(char *buf, size_t cap, size_t n, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 4, 5), unused))
#endif
    ;
static size_t append(char *buf, size_t cap, size_t n, const char *fmt, ...) {
    va_list ap;
    int r;
    if (n >= cap) abort();
    va_start(ap, fmt);
    r = vsnprintf(buf + n, cap - n, fmt, ap);
    va_end(ap);
    if (r < 0 || (size_t) r >= cap - n) abort();
    return n + (size_t) r;
}

static inline const char *last_err(void) {
    static char b[600];
    size_t n = 0;
    if (tb_last_error(b, sizeof b, &n) != ZXV_OK) b[0] = '\0';
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

/* Parse; NULL on failure (the diagnostic goes to stderr). */
static inline tb_program *prog(const char *text) {
    tb_program *p = NULL;
    tb_diag d;
    if (tb_parse(text, strlen(text), &p, &d) != ZXV_OK) {
        fprintf(stderr, "refused: %s\n", d.msg);
        return NULL;
    }
    return p;
}

/* The canonical text of a program, in a static buffer. */
static inline const char *canon(const tb_program *p) {
    static char b[1 << 16];
    size_t n = 0;
    if (!p || tb_program_text(p, b, sizeof b, &n) != ZXV_OK) return "<print failed>";
    return b;
}

/* A rational as text, in one of four rotating static buffers. */
static inline const char *rat(int64_t n, int64_t d) {
    static char b[4][96];
    static int k;
    size_t len = 0;
    char *s = b[k++ & 3];
    if (zxv_rat_format(n, d, s, 96, &len) != ZXV_OK) snprintf(s, 96, "%lld/%lld", (long long) n, (long long) d);
    return s;
}

static inline tb_array arr1(const char *name, int64_t *num, int64_t *den, int64_t n) {
    tb_array a;
    memset(&a, 0, sizeof a);
    a.name = name;
    a.num = num;
    a.den = den;
    a.rank = 1;
    a.extent[0] = n;
    return a;
}

static inline tb_array arr2(const char *name, int64_t *num, int64_t *den, int64_t m, int64_t n) {
    tb_array a = arr1(name, num, den, m);
    a.rank = 2;
    a.extent[1] = n;
    return a;
}

static inline tb_array arr0(const char *name, int64_t *num, int64_t *den) {
    tb_array a = arr1(name, num, den, 0);
    a.rank = 0;
    return a;
}

static inline int report(const char *name) {
    printf("%s: %d checks, %d failed\n", name, g_checks, g_fail);
    return g_fail ? 1 : 0;
}

#endif /* TB_TEST_H */
