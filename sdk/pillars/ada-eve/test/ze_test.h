/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ze_test.h — the check macro and helpers shared by the ada-eve C tests. */
#ifndef ZE_TEST_H
#define ZE_TEST_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zeve.h"

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
    if (ze_last_error(b, sizeof b, &n) != ZXV_OK) b[0] = '\0';
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

/* Parse Ada (ada = 1) or Eve; NULL on failure (the diagnostic to stderr). */
static inline ze_pkg *parse(int ada, const char *text) {
    ze_pkg *p = NULL;
    ze_diag d;
    int32_t st = ada ? ze_ada_parse(text, strlen(text), &p, &d) : ze_eve_parse(text, strlen(text), &p, &d);
    if (st != ZXV_OK) {
        fprintf(stderr, "refused (%s): %s\n", ze_status_name(st), d.msg);
        return NULL;
    }
    return p;
}

/* Parse expecting a refusal; returns the status, and the diagnostic. */
static inline int32_t refuse(int ada, const char *text, ze_diag *d) {
    ze_pkg *p = NULL;
    int32_t st = ada ? ze_ada_parse(text, strlen(text), &p, d) : ze_eve_parse(text, strlen(text), &p, d);
    if (st == ZXV_OK) ze_pkg_destroy(p);
    return st;
}

/* The canonical Eve text, in a static buffer. */
static inline const char *canon(const ze_pkg *p) {
    static char b[1 << 16];
    size_t n = 0;
    if (!p || ze_pkg_eve(p, b, sizeof b, &n) != ZXV_OK) return "<print failed>";
    return b;
}

/* The printed Ada, in a static buffer. */
static inline const char *as_ada(const ze_pkg *p) {
    static char b[1 << 16];
    size_t n = 0;
    if (!p || ze_pkg_ada(p, b, sizeof b, &n) != ZXV_OK) return "<print failed>";
    return b;
}

/* Bytes as lower-case hex, in one of two rotating static buffers. */
static inline const char *hex(const uint8_t *b, size_t n) {
    static char s[2][4096];
    static int k;
    char *o = s[k++ & 1];
    size_t m = 0;
    o[0] = '\0';
    for (size_t i = 0; i < n && m + 3 < sizeof s[0]; i++) m = append(o, sizeof s[0], m, "%02x", b[i]);
    return o;
}

static inline uint32_t type_of(const ze_pkg *p, const char *name) {
    uint32_t t = UINT32_MAX;
    if (ze_type_find(p, name, &t) != ZXV_OK) fprintf(stderr, "no type %s\n", name);
    return t;
}

static inline uint64_t size_of(const ze_pkg *p, uint32_t t) {
    ze_type ti;
    if (ze_type_at(p, t, &ti) != ZXV_OK) return 0;
    return ti.size;
}

/* A scalar as text, in a static buffer ("<status>" on failure). */
static inline const char *get(const ze_pkg *p, uint32_t t, const uint8_t *v, size_t len, const char *path) {
    static char b[2][256];
    static int k;
    char *o = b[k++ & 1];
    size_t n = 0;
    int32_t st = ze_get_value(p, t, v, len, path, o, sizeof b[0], &n);
    if (st != ZXV_OK) snprintf(o, sizeof b[0], "<%s>", ze_status_name(st));
    return o;
}

static inline int32_t put(const ze_pkg *p, uint32_t t, uint8_t *v, size_t len, const char *path,
                          const char *text) {
    return ze_put_value(p, t, v, len, path, text, strlen(text));
}

static inline int report(const char *name) {
    printf("%s: %d checks, %d failed\n", name, g_checks, g_fail);
    return g_fail ? 1 : 0;
}

#endif /* ZE_TEST_H */
