/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* kr_util.c — status names, the last error, diagnostics, UTF-8 and the
 * growing text buffer. Integer code only (R11). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kr_internal.h"

static _Thread_local char g_err[512];
static _Thread_local uint64_t g_work;

void kr_set_error(const char *msg) {
    size_t n = msg ? strlen(msg) : 0;
    if (n >= sizeof g_err) n = sizeof g_err - 1u;
    if (n) memcpy(g_err, msg, n);
    g_err[n] = '\0';
}

void kr_set_errorf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof g_err, fmt, ap);
    va_end(ap);
}

void kr_set_work(uint64_t work) { g_work = work; }

int32_t kr_last_work(uint64_t *out_work) {
    if (!out_work) return ZXV_E_ARG;
    *out_work = g_work;
    return ZXV_OK;
}

int32_t kr_last_error(char *buf, size_t cap, size_t *out_len) {
    return kr_out_text(g_err, strlen(g_err), buf, cap, out_len);
}

const char *kr_status_name(int32_t status) {
    switch (status) {
    case KR_E_SYNTAX: return "KR_E_SYNTAX";
    case KR_E_NAME: return "KR_E_NAME";
    case KR_E_RANK: return "KR_E_RANK";
    case KR_E_LENGTH: return "KR_E_LENGTH";
    case KR_E_DOMAIN: return "KR_E_DOMAIN";
    case KR_E_INDEX: return "KR_E_INDEX";
    case KR_E_DIVZERO: return "KR_E_DIVZERO";
    case KR_E_LIMIT: return "KR_E_LIMIT";
    default: return zxv_status_name(status);
    }
}

int32_t kr_status_class(int32_t status) {
    switch (status) {
    case KR_E_SYNTAX:
    case KR_E_NAME:
    case KR_E_RANK:
    case KR_E_LENGTH:
    case KR_E_DOMAIN:
    case KR_E_INDEX:
    case KR_E_DIVZERO: return ZXV_CLASS_USAGE;
    case KR_E_LIMIT: return ZXV_CLASS_REFUSED;
    default: return zxv_status_class(status);
    }
}

int32_t kr_diag_at(kr_diag *d, int32_t status, uint32_t line, uint32_t col, uint64_t offset,
                   const char *fmt, ...) {
    char body[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof body, fmt, ap);
    va_end(ap);
    if (line)
        kr_set_errorf("line %u, column %u: %s [%s]", (unsigned) line, (unsigned) col, body,
                      kr_status_name(status));
    else
        kr_set_errorf("%s [%s]", body, kr_status_name(status));
    if (d) {
        size_t n = strlen(g_err);
        d->line = line;
        d->col = col;
        d->offset = offset;
        if (n >= sizeof d->msg) n = sizeof d->msg - 1u;
        memcpy(d->msg, g_err, n);
        d->msg[n] = '\0';
    }
    return status;
}

int32_t kr_out_text(const char *s, size_t n, char *buf, size_t cap, size_t *out_len) {
    if (!out_len) return ZXV_E_ARG;
    *out_len = n;
    if (!buf || cap <= n) return ZXV_E_BUFFER;
    memcpy(buf, s, n);
    buf[n] = '\0';
    return ZXV_OK;
}

/* Length of the valid UTF-8 prefix (== n when all of it is valid). */
size_t kr_utf8_check(const uint8_t *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        uint8_t c = s[i];
        uint32_t cp, min;
        size_t k;
        if (c < 0x80u) {
            i++;
            continue;
        } else if ((c & 0xE0u) == 0xC0u) {
            k = 1;
            cp = c & 0x1Fu;
            min = 0x80u;
        } else if ((c & 0xF0u) == 0xE0u) {
            k = 2;
            cp = c & 0x0Fu;
            min = 0x800u;
        } else if ((c & 0xF8u) == 0xF0u) {
            k = 3;
            cp = c & 0x07u;
            min = 0x10000u;
        } else {
            return i;
        }
        if (n - i - 1u < k) return i;
        for (size_t j = 1; j <= k; j++) {
            if ((s[i + j] & 0xC0u) != 0x80u) return i;
            cp = (cp << 6) | (s[i + j] & 0x3Fu);
        }
        if (cp < min || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) return i;
        i += k + 1u;
    }
    return n;
}

/* ===== The growing buffer: every append checks its room, so a write
 * can never run past the end; on allocation failure it stops growing
 * and sets oom. ===== */

void kr_buf_put(kr_buf *b, const char *s, size_t n) {
    if (b->oom) return;
    if (n + 1u > b->cap - b->n || !b->b) {
        size_t want = b->cap ? b->cap : 256u;
        char *nb;
        while (want - b->n < n + 1u) {
            if (want > SIZE_MAX / 2u) {
                b->oom = 1;
                return;
            }
            want *= 2u;
        }
        nb = (char *) realloc(b->b, want);
        if (!nb) {
            b->oom = 1;
            return;
        }
        b->b = nb;
        b->cap = want;
    }
    memcpy(b->b + b->n, s, n);
    b->n += n;
    b->b[b->n] = '\0';
}

void kr_buf_puts(kr_buf *b, const char *s) { kr_buf_put(b, s, strlen(s)); }

void kr_buf_putc(kr_buf *b, char c) { kr_buf_put(b, &c, 1u); }

void kr_buf_u64(kr_buf *b, uint64_t v) {
    char t[24];
    size_t k = sizeof t;
    do {
        t[--k] = (char) ('0' + (int) (v % 10u));
        v /= 10u;
    } while (v);
    kr_buf_put(b, t + k, sizeof t - k);
}

void kr_buf_i64(kr_buf *b, int64_t v) {
    if (v < 0) {
        kr_buf_putc(b, '-');
        kr_buf_u64(b, (uint64_t) 0 - (uint64_t) v);
    } else {
        kr_buf_u64(b, (uint64_t) v);
    }
}

void kr_buf_rat(kr_buf *b, int64_t num, int64_t den) {
    kr_buf_i64(b, num);
    if (den != 1) {
        kr_buf_putc(b, 'r');
        kr_buf_i64(b, den);
    }
}

const char *kr_rat_str(int64_t num, int64_t den) {
    static _Thread_local char s[4][48];
    static _Thread_local unsigned k;
    char *o = s[k++ & 3u];
    kr_buf b = {0, 0, 0, 0};
    kr_buf_rat(&b, num, den);
    if (b.oom || !b.b) {
        o[0] = '?';
        o[1] = '\0';
    } else {
        size_t n = b.n < 47u ? b.n : 47u;
        memcpy(o, b.b, n);
        o[n] = '\0';
    }
    free(b.b);
    return o;
}
