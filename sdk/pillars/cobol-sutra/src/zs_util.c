/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zs_util.c — status names, the last error, parse diagnostics, UTF-8 and
 * text buffers. Integer code only (R11). */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "zs_internal.h"

static _Thread_local char g_err[512];

void zs_set_error(const char *msg) {
    size_t n = msg ? strlen(msg) : 0;
    if (n >= sizeof g_err) n = sizeof g_err - 1u;
    if (n) memcpy(g_err, msg, n);
    g_err[n] = '\0';
}

void zs_set_errorf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof g_err, fmt, ap);
    va_end(ap);
}

int32_t zs_last_error(char *buf, size_t cap, size_t *out_len) {
    return zs_out_text(g_err, strlen(g_err), buf, cap, out_len);
}

const char *zs_status_name(int32_t status) {
    switch (status) {
    case ZS_E_SYNTAX: return "ZS_E_SYNTAX";
    case ZS_E_UNSUPPORTED: return "ZS_E_UNSUPPORTED";
    case ZS_E_DATA: return "ZS_E_DATA";
    case ZS_E_FIT: return "ZS_E_FIT";
    default: return zxv_status_name(status);
    }
}

int32_t zs_diag_set(zs_diag *d, int32_t status, const char *text, size_t len, uint64_t offset,
                    const char *fmt, ...) {
    uint32_t line = 1, col;
    uint64_t ls = 0;
    char body[400];
    va_list ap;
    if (offset > len) offset = len;
    for (uint64_t i = 0; i < offset; i++)
        if (text[i] == '\n') {
            line++;
            ls = i + 1u;
        }
    col = (uint32_t) (offset - ls + 1u);
    va_start(ap, fmt);
    vsnprintf(body, sizeof body, fmt, ap);
    va_end(ap);
    zs_set_errorf("line %u, column %u: %s", (unsigned) line, (unsigned) col, body);
    if (d) {
        d->line = line;
        d->col = col;
        d->offset = offset;
        size_t n = strlen(g_err);
        if (n >= sizeof d->msg) n = sizeof d->msg - 1u;
        memcpy(d->msg, g_err, n);
        d->msg[n] = '\0';
    }
    return status;
}

size_t zs_utf8_check(const uint8_t *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        uint8_t c = s[i];
        uint32_t cp;
        size_t k;
        if (c < 0x80u) {
            i++;
            continue;
        } else if ((c & 0xE0u) == 0xC0u) {
            k = 1;
            cp = c & 0x1Fu;
        } else if ((c & 0xF0u) == 0xE0u) {
            k = 2;
            cp = c & 0x0Fu;
        } else if ((c & 0xF8u) == 0xF0u) {
            k = 3;
            cp = c & 0x07u;
        } else {
            return i;
        }
        if (n - i <= k) return i;
        for (size_t j = 1; j <= k; j++) {
            if ((s[i + j] & 0xC0u) != 0x80u) return i;
            cp = (cp << 6) | (s[i + j] & 0x3Fu);
        }
        if ((k == 1 && cp < 0x80u) || (k == 2 && cp < 0x800u) || (k == 3 && cp < 0x10000u) ||
            cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu))
            return i;
        i += k + 1u;
    }
    return n;
}

int32_t zs_out_text(const char *src, size_t n, char *buf, size_t cap, size_t *out_len) {
    if (!out_len) return ZXV_E_ARG;
    *out_len = n;
    if (!buf || cap <= n) return ZXV_E_BUFFER;
    if (n) memcpy(buf, src, n);
    buf[n] = '\0';
    return ZXV_OK;
}

int zs_ieq(const char *a, const char *b) {
    for (;; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'a' && x <= 'z') x = (char) (x - 32);
        if (y >= 'a' && y <= 'z') y = (char) (y - 32);
        if (x != y) return 0;
        if (!x) return 1;
    }
}
