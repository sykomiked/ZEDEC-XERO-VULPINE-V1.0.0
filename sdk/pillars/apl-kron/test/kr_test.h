/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* kr_test.h — the check macro and helpers shared by the apl-kron C tests. */
#ifndef KR_TEST_H
#define KR_TEST_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zkron.h"

static int g_checks, g_fail;

static inline const char *last_err(void) {
    static char b[600];
    size_t n = 0;
    if (kr_last_error(b, sizeof b, &n) != ZXV_OK) b[0] = '\0';
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
static inline kr_program *prog_s(const char *text, int32_t spell) {
    kr_program *p = NULL;
    kr_diag d;
    if (kr_parse(text, strlen(text), spell, &p, &d) != ZXV_OK) {
        fprintf(stderr, "refused: %s: %s\n", text, d.msg);
        return NULL;
    }
    return p;
}

static inline kr_program *prog(const char *text) { return prog_s(text, KR_SPELL_AUTO); }

/* The canonical text of a program, in a static buffer. */
static inline const char *canon(const kr_program *p) {
    static char b[1 << 16];
    size_t n = 0;
    if (!p || kr_program_text(p, b, sizeof b, &n) != ZXV_OK) return "<print failed>";
    return b;
}

/* The value text of a value, in one of four rotating static buffers. */
static inline const char *vtext(const kr_value *v) {
    static char b[4][1 << 14];
    static int k;
    char *s = b[k++ & 3];
    size_t n = 0;
    if (!v || kr_value_text(v, s, sizeof b[0], &n) != ZXV_OK) return "<no value>";
    return s;
}

/* Evaluate text with no bindings; the value text, or the status name. */
static inline const char *ev(const char *text, int32_t spell) {
    static char b[4][1 << 14];
    static int k;
    char *s = b[k++ & 3];
    size_t n = 0;
    int32_t st = kr_run_text(text, strlen(text), spell, KR_TEXT_VALUE, s, sizeof b[0], &n);
    if (st != ZXV_OK) snprintf(s, sizeof b[0], "%s", kr_status_name(st));
    return s;
}

static inline int same_digest(const char *a, int32_t sa, const char *b, int32_t sb) {
    kr_program *p = prog_s(a, sa), *q = prog_s(b, sb);
    uint8_t da[32], db[32];
    int r = -1;
    if (p && q) {
        kr_program_digest(p, da);
        kr_program_digest(q, db);
        r = memcmp(da, db, 32) == 0;
    }
    kr_program_destroy(p);
    kr_program_destroy(q);
    return r;
}

static inline int report(const char *name) {
    printf("%s: %d checks, %d failed\n", name, g_checks, g_fail);
    return g_fail ? 1 : 0;
}

#endif /* KR_TEST_H */
