/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zm_test.h — the check macro and helpers shared by the mumps-myra tests.
 * The tests run from the pillar directory (make test). */
#ifndef ZM_TEST_H
#define ZM_TEST_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zmyra.h"

static int g_checks, g_fail;

static inline const char *last_err(void) {
    static char b[600];
    size_t n = 0;
    if (zm_last_error(b, sizeof b, &n) != ZXV_OK) b[0] = '\0';
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

/* Parse helpers: NULL on failure (the diagnostic goes to stderr). */
static inline zm_schema *myra(const char *text) {
    zm_schema *s = NULL;
    zm_diag d;
    if (zm_myra_parse(text, strlen(text), &s, &d) != ZXV_OK) {
        fprintf(stderr, "myra refused: %s\n", d.msg);
        return NULL;
    }
    return s;
}

static inline zm_schema *gmap(const char *text) {
    zm_schema *s = NULL;
    zm_diag d;
    if (zm_gmap_parse(text, strlen(text), &s, &d) != ZXV_OK) {
        fprintf(stderr, "gmap refused: %s\n", d.msg);
        return NULL;
    }
    return s;
}

/* The canonical MYRA text of a schema, in a static buffer. */
static inline const char *canon(const zm_schema *s) {
    static char b[1 << 16];
    size_t n = 0;
    if (!s || zm_schema_myra(s, b, sizeof b, &n) != ZXV_OK) return "<format failed>";
    return b;
}

/* A field's display text, in a static buffer ("<STATUS>" on failure). */
static inline const char *val(const zm_record *r, const char *name) {
    static char b[4096];
    size_t n = 0;
    int32_t st = zm_get_value(r, name, b, sizeof b, &n);
    if (st != ZXV_OK) snprintf(b, sizeof b, "<%s>", zm_status_name(st));
    return b;
}

/* The canonical value string of a record, in a static buffer. */
static inline const char *enc(const zm_record *r) {
    static char b[1 << 16];
    size_t n = 0;
    int32_t st = zm_record_encode(r, b, sizeof b, &n);
    if (st != ZXV_OK) snprintf(b, sizeof b, "<%s>", zm_status_name(st));
    return b;
}

/* Read a whole file (NULL on failure); the caller frees it. */
static inline char *slurp(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    char *b;
    long n;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    b = (char *) malloc((size_t) n + 1u);
    if (b && fread(b, 1, (size_t) n, f) != (size_t) n) {
        free(b);
        b = NULL;
    }
    fclose(f);
    if (b) {
        b[n] = '\0';
        if (out_len) *out_len = (size_t) n;
    }
    return b;
}

/* Append formatted text at b + *k; abort if it does not fit (never a
 * silently truncated test input). */
#if defined(__GNUC__)
__attribute__((format(printf, 4, 5)))
#endif
static inline void app(char *b, size_t cap, size_t *k, const char *fmt, ...) {
    va_list ap;
    int w;
    if (*k >= cap) abort();
    va_start(ap, fmt);
    w = vsnprintf(b + *k, cap - *k, fmt, ap);
    va_end(ap);
    if (w < 0 || (size_t) w >= cap - *k) {
        fprintf(stderr, "test buffer too small\n");
        abort();
    }
    *k += (size_t) w;
}

/* A fixed-seed xorshift64 stream. */
static unsigned long long g_rng = 0x9E3779B97F4A7C15ull;
static inline unsigned long long rnd(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return g_rng;
}

static inline int report(const char *name) {
    printf("%s: %d checks, %d failed\n", name, g_checks, g_fail);
    return g_fail ? 1 : 0;
}

/* The payment schema, in both forms (the files in examples/ hold the same). */
static const char k_pay_myra[] =
    "^ZXPAY(batch:i9,seq:i9)=ref:t16!^amount:n15.2!^ccy:t3!^from:i10!^to:i10!^value-date:d"
    "^status:t1^^memo:t40\n"
    "^ZXPAY(batch:i9)=opened:d!^count:i6^total:n18.2^name:t30\n"
    "^ZXPAY(\"B\",ref:t16,batch:i9,seq:i9)=\n";

static const char k_pay_gmap[] =
    "; payment journal (synthetic)\n"
    "GLOBAL ^ZXPAY\n"
    "NODE ^ZXPAY(BATCH,SEQ)\n"
    "  KEY BATCH INTEGER(9)\n"
    "  KEY SEQ INTEGER(9)\n"
    "  PIECE 1 REF TEXT(16) REQUIRED\n"
    "  PIECE 2 AMOUNT NUMBER(15,2) REQUIRED\n"
    "  PIECE 3 CCY TEXT(3) REQUIRED\n"
    "  PIECE 4 FROM INTEGER(10) REQUIRED\n"
    "  PIECE 5 TO INTEGER(10) REQUIRED\n"
    "  PIECE 6 VALUE-DATE DATE\n"
    "  PIECE 7 STATUS TEXT(1)\n"
    "  PIECE 9 MEMO TEXT(40)\n"
    "NODE ^ZXPAY(BATCH)\n"
    "  KEY BATCH INTEGER(9)\n"
    "  PIECE 1 OPENED DATE REQUIRED\n"
    "  PIECE 2 COUNT INTEGER(6)\n"
    "  PIECE 3 TOTAL NUMBER(18,2)\n"
    "  PIECE 4 NAME TEXT(30)\n"
    "NODE ^ZXPAY(\"B\",REF,BATCH,SEQ)\n"
    "  KEY REF TEXT(16)\n"
    "  KEY BATCH INTEGER(9)\n"
    "  KEY SEQ INTEGER(9)\n";

#endif /* ZM_TEST_H */
