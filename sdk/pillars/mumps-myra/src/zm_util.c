/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zm_util.c — status names, the last error, parse diagnostics, UTF-8,
 * text buffers and the aborting text builder. Integer code only (R11). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zm_internal.h"

static _Thread_local char g_err[512];

void zm_set_error(const char *msg) {
    size_t n = msg ? strlen(msg) : 0;
    if (n >= sizeof g_err) n = sizeof g_err - 1u;
    if (n) memcpy(g_err, msg, n);
    g_err[n] = '\0';
}

void zm_set_errorf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof g_err, fmt, ap);
    va_end(ap);
}

int32_t zm_last_error(char *buf, size_t cap, size_t *out_len) {
    return zm_out_text(g_err, strlen(g_err), buf, cap, out_len);
}

const char *zm_status_name(int32_t status) {
    switch (status) {
    case ZM_E_SYNTAX: return "ZM_E_SYNTAX";
    case ZM_E_UNSUPPORTED: return "ZM_E_UNSUPPORTED";
    case ZM_E_DATA: return "ZM_E_DATA";
    case ZM_E_FIT: return "ZM_E_FIT";
    case ZM_E_KEY: return "ZM_E_KEY";
    default: return zxv_status_name(status);
    }
}

int32_t zm_diag_set(zm_diag *d, int32_t status, const char *text, size_t len, uint64_t offset,
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
    zm_set_errorf("line %u, column %u: %s", (unsigned) line, (unsigned) col, body);
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

size_t zm_utf8_check(const uint8_t *s, size_t n) {
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

int32_t zm_out_text(const char *src, size_t n, char *buf, size_t cap, size_t *out_len) {
    if (!out_len) return ZXV_E_ARG;
    *out_len = n;
    if (!buf || cap <= n) return ZXV_E_BUFFER;
    if (n) memcpy(buf, src, n);
    buf[n] = '\0';
    return ZXV_OK;
}

const char *zm_show(const char *s, size_t n, char *b, size_t cap) {
    static const char hex[] = "0123456789abcdef";
    size_t k = 0;
    if (cap < 8u) {
        if (cap) b[0] = '\0';
        return b;
    }
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char) s[i];
        if (k + 8u > cap || i >= 40u) { /* room for one escape and "...\0" */
            b[k++] = '.';
            b[k++] = '.';
            b[k++] = '.';
            break;
        }
        if (c < 0x20u || c == 0x7Fu) {
            b[k++] = '\\';
            b[k++] = 'x';
            b[k++] = hex[c >> 4];
            b[k++] = hex[c & 15u];
        } else {
            b[k++] = (char) c;
        }
    }
    b[k] = '\0';
    return b;
}

/* ---- the text builder ---- */

void zm_sb_init(zm_sb *sb, size_t limit) {
    sb->b = NULL;
    sb->n = sb->cap = 0;
    sb->limit = limit;
    sb->err = ZXV_OK;
}

void zm_sb_free(zm_sb *sb) {
    free(sb->b);
    sb->b = NULL;
    sb->n = sb->cap = 0;
}

void zm_sb_put(zm_sb *sb, const char *s, size_t n) {
    if (sb->err != ZXV_OK || n == 0) return;
    if (n > sb->limit || sb->n > sb->limit - n) {
        sb->err = ZXV_E_TOO_LONG;
        return;
    }
    if (sb->n + n + 1u > sb->cap) {
        size_t nc = sb->cap ? sb->cap : 256u;
        char *nb;
        while (nc < sb->n + n + 1u) nc *= 2u;
        nb = (char *) realloc(sb->b, nc);
        if (!nb) {
            sb->err = ZXV_E_NOMEM;
            return;
        }
        sb->b = nb;
        sb->cap = nc;
    }
    memcpy(sb->b + sb->n, s, n);
    sb->n += n;
    sb->b[sb->n] = '\0';
}

void zm_sb_str(zm_sb *sb, const char *s) { zm_sb_put(sb, s, strlen(s)); }

void zm_sb_chr(zm_sb *sb, char c) { zm_sb_put(sb, &c, 1); }

void zm_sb_u64(zm_sb *sb, uint64_t v) {
    char t[24];
    size_t k = sizeof t;
    do {
        t[--k] = (char) ('0' + (int) (v % 10u));
        v /= 10u;
    } while (v);
    zm_sb_put(sb, t + k, sizeof t - k);
}

int32_t zm_sb_out(zm_sb *sb, char *buf, size_t cap, size_t *out_len) {
    int32_t st = sb->err;
    if (st == ZXV_OK) st = zm_out_text(sb->b ? sb->b : "", sb->n, buf, cap, out_len);
    zm_sb_free(sb);
    return st;
}
