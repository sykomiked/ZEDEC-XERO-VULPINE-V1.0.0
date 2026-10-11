/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zs_test.h — the check macro and helpers shared by the cobol-sutra tests. */
#ifndef ZS_TEST_H
#define ZS_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zsutra.h"

static int g_checks, g_fail;

static inline const char *last_err(void) {
    static char b[600];
    size_t n = 0;
    if (zs_last_error(b, sizeof b, &n) != ZXV_OK) b[0] = '\0';
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
static inline zs_layout *cpy(const char *text) {
    zs_layout *l = NULL;
    zs_diag d;
    if (zs_copybook_parse(text, strlen(text), ZS_CB_FIXED, &l, &d) != ZXV_OK) {
        fprintf(stderr, "copybook refused: %s\n", d.msg);
        return NULL;
    }
    return l;
}

static inline zs_layout *sutra(const char *text) {
    zs_layout *l = NULL;
    zs_diag d;
    if (zs_sutra_parse(text, strlen(text), &l, &d) != ZXV_OK) {
        fprintf(stderr, "sutra refused: %s\n", d.msg);
        return NULL;
    }
    return l;
}

/* The canonical Sutra text of a layout, in a static buffer. */
static inline const char *canon(const zs_layout *l) {
    static char b[1 << 16];
    size_t n = 0;
    if (!l || zs_layout_sutra(l, b, sizeof b, &n) != ZXV_OK) return "<format failed>";
    return b;
}

/* A field's value as text, in a static buffer ("<STATUS>" on failure). */
static inline const char *val(const zs_layout *l, const uint8_t *rec, size_t len, const char *ref) {
    static char b[4096];
    size_t n = 0;
    int32_t st = zs_get_value(l, rec, len, ref, b, sizeof b, &n);
    if (st != ZXV_OK) snprintf(b, sizeof b, "<%s>", zs_status_name(st));
    return b;
}

/* Hex text -> bytes; returns the count. */
static inline size_t unhex(const char *h, uint8_t *out, size_t cap) {
    size_t n = 0;
    int hi = -1;
    for (; *h; h++) {
        int v = (*h >= '0' && *h <= '9') ? *h - '0'
              : (*h >= 'a' && *h <= 'f') ? *h - 'a' + 10
              : (*h >= 'A' && *h <= 'F') ? *h - 'A' + 10 : -1;
        if (v < 0) continue;
        if (hi < 0) hi = v;
        else {
            if (n < cap) out[n] = (uint8_t) (hi << 4 | v);
            n++;
            hi = -1;
        }
    }
    return n;
}

static inline int64_t zs_test_pow10(uint32_t n) {
    int64_t p = 1;
    while (n--) p *= 10;
    return p;
}

static inline int report(const char *name) {
    printf("%s: %d checks, %d failed\n", name, g_checks, g_fail);
    return g_fail ? 1 : 0;
}

/* The two example records, as text (the files in examples/ hold the same). */
static const char k_payment_cpy[] =
    "      * PAYMENT.CPY\n"
    "       01  PAYMENT-REC.\n"
    "           05  PAY-REF             PIC X(16).\n"
    "           05  PAY-AMOUNT          PIC S9(13)V99 COMP-3.\n"
    "           05  PAY-CCY             PIC X(3).\n"
    "           05  PAY-FROM            PIC 9(9) COMP.\n"
    "           05  PAY-TO              PIC 9(9) COMP.\n"
    "           05  PAY-VALUE-DATE      PIC 9(8).\n"
    "           05  PAY-FEE             PIC S9(5)V99.\n"
    "           05  PAY-STATUS          PIC X.\n"
    "               88  PAY-NEW             VALUE 'N'.\n"
    "               88  PAY-POSTED          VALUE 'P'.\n"
    "           05  FILLER              PIC X(4).\n";

static const char k_payment_sutra[] =
    "# payment.sutra\n"
    "record payment-rec\n"
    "  pay-ref: x(16)\n"
    "  pay-amount: s9(13)v99 comp-3\n"
    "  pay-ccy: x(3)\n"
    "  pay-from: 9(9) comp\n"
    "  pay-to: 9(9) comp\n"
    "  pay-value-date: 9(8)\n"
    "  pay-fee: s9(5)v99\n"
    "  pay-status: x\n"
    "  filler: x(4)\n";

#endif /* ZS_TEST_H */
