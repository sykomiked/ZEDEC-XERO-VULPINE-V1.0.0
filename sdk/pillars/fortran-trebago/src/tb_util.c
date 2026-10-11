/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* tb_util.c — status names, the last error, diagnostics, UTF-8 and text
 * buffers. Integer code only (R11). */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "tb_internal.h"

static _Thread_local char g_err[512];
static _Thread_local uint64_t g_work;

void tb_set_error(const char *msg) {
    size_t n = msg ? strlen(msg) : 0;
    if (n >= sizeof g_err) n = sizeof g_err - 1u;
    if (n) memcpy(g_err, msg, n);
    g_err[n] = '\0';
}

void tb_set_errorf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof g_err, fmt, ap);
    va_end(ap);
}

void tb_set_work(uint64_t work) { g_work = work; }

int32_t tb_last_work(uint64_t *out_work) {
    if (!out_work) return ZXV_E_ARG;
    *out_work = g_work;
    return ZXV_OK;
}

int32_t tb_last_error(char *buf, size_t cap, size_t *out_len) {
    return tb_out_text(g_err, strlen(g_err), buf, cap, out_len);
}

const char *tb_status_name(int32_t status) {
    switch (status) {
    case TB_E_SYNTAX: return "TB_E_SYNTAX";
    case TB_E_NAME: return "TB_E_NAME";
    case TB_E_SHAPE: return "TB_E_SHAPE";
    case TB_E_BOUNDS: return "TB_E_BOUNDS";
    case TB_E_DIVZERO: return "TB_E_DIVZERO";
    case TB_E_LIMIT: return "TB_E_LIMIT";
    default: return zxv_status_name(status);
    }
}

int32_t tb_status_class(int32_t status) {
    switch (status) {
    case TB_E_SYNTAX:
    case TB_E_NAME:
    case TB_E_SHAPE:
    case TB_E_BOUNDS:
    case TB_E_DIVZERO: return ZXV_CLASS_USAGE;
    case TB_E_LIMIT: return ZXV_CLASS_REFUSED;
    default: return zxv_status_class(status);
    }
}

int32_t tb_diag_at(tb_diag *d, int32_t status, uint32_t line, uint32_t col, uint64_t offset,
                   const char *fmt, ...) {
    char body[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof body, fmt, ap);
    va_end(ap);
    if (line)
        tb_set_errorf("line %u, column %u: %s [%s]", (unsigned) line, (unsigned) col, body,
                      tb_status_name(status));
    else
        tb_set_errorf("%s [%s]", body, tb_status_name(status));
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

int32_t tb_out_text(const char *s, size_t n, char *buf, size_t cap, size_t *out_len) {
    if (!out_len) return ZXV_E_ARG;
    *out_len = n;
    if (!buf || cap <= n) return ZXV_E_BUFFER;
    memcpy(buf, s, n);
    buf[n] = '\0';
    return ZXV_OK;
}

/* Length of the valid UTF-8 prefix (== n when all of it is valid). */
size_t tb_utf8_check(const uint8_t *s, size_t n) {
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
        if (n - i - 1u < k) return i; /* fewer than k continuation bytes remain */
        for (size_t j = 1; j <= k; j++) {
            if ((s[i + j] & 0xC0u) != 0x80u) return i;
            cp = (cp << 6) | (s[i + j] & 0x3Fu);
        }
        if (cp < min || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) return i;
        i += k + 1u;
    }
    return n;
}
