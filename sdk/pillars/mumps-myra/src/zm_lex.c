/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zm_lex.c — the lexer shared by the global-map and MYRA front ends.
 * Integer code only (R11). */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "zm_internal.h"

int32_t zm_lx_err(zm_lx *lx, int32_t st, size_t at, const char *fmt, ...) {
    char body[300];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof body, fmt, ap);
    va_end(ap);
    return zm_diag_set(lx->d, st, lx->t, lx->len, at, "%s", body);
}

int32_t zm_lx_start(zm_lx *lx, const char *text, size_t len, zm_diag *d) {
    size_t ok;
    lx->t = text;
    lx->len = len;
    lx->i = 0;
    lx->d = d;
    if (d) memset(d, 0, sizeof *d);
    if (!text && len) return ZXV_E_ARG;
    if (len > ZM_MAX_TEXT) {
        zm_set_errorf("the text is longer than %u bytes", (unsigned) ZM_MAX_TEXT);
        return ZXV_E_TOO_LONG;
    }
    ok = zm_utf8_check((const uint8_t *) text, len);
    if (ok != len) {
        zm_lx_err(lx, ZXV_E_UTF8, ok, "the text is not valid UTF-8");
        return ZXV_E_UTF8;
    }
    for (size_t i = 0; i < len; i++)
        if (text[i] == '\0') return zm_lx_err(lx, ZM_E_SYNTAX, i, "a NUL byte in the text");
    return ZXV_OK;
}

zm_pos zm_lx_pos(const zm_lx *lx, size_t at) {
    zm_pos p;
    size_t ls = 0;
    p.line = 1;
    for (size_t i = 0; i < at && i < lx->len; i++)
        if (lx->t[i] == '\n') {
            p.line++;
            ls = i + 1u;
        }
    p.col = (uint32_t) (at - ls + 1u);
    p.offset = at;
    return p;
}

void zm_lx_ws(zm_lx *lx) {
    while (lx->i < lx->len) {
        char c = lx->t[lx->i];
        if (c == ' ' || c == '\t') lx->i++;
        else if (c == '\r' && lx->i + 1u < lx->len && lx->t[lx->i + 1u] == '\n') lx->i++;
        else break;
    }
}

int zm_lx_eol(const zm_lx *lx) {
    return lx->i >= lx->len || lx->t[lx->i] == '\n' || lx->t[lx->i] == ';';
}

void zm_lx_next_line(zm_lx *lx) {
    while (lx->i < lx->len && lx->t[lx->i] != '\n') lx->i++;
    if (lx->i < lx->len) lx->i++;
}

size_t zm_lx_word(zm_lx *lx, const char **out) {
    size_t b = lx->i;
    while (lx->i < lx->len) {
        char c = lx->t[lx->i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')
            lx->i++;
        else break;
    }
    *out = lx->t + b;
    return lx->i - b;
}

int32_t zm_lx_name(zm_lx *lx, char dst[ZM_MAX_NAME + 1], zm_pos *pos) {
    const char *w;
    size_t at = lx->i, n = zm_lx_word(lx, &w);
    char sh[64];
    if (pos) *pos = zm_lx_pos(lx, at);
    if (n == 0) return zm_lx_err(lx, ZM_E_SYNTAX, at, "expected a field name");
    if (!zm_valid_name(w, n))
        return zm_lx_err(lx, ZM_E_SYNTAX, at,
                         "\"%s\" is not a field name (1-30 letters, digits and '-', a letter "
                         "first, no '-' last)",
                         zm_show(w, n, sh, sizeof sh));
    zm_lower(dst, w, n);
    return ZXV_OK;
}

int32_t zm_lx_gname(zm_lx *lx, char dst[ZM_MAX_GNAME + 1]) {
    size_t at = lx->i, b;
    char sh[64];
    if (lx->i >= lx->len || lx->t[lx->i] != '^')
        return zm_lx_err(lx, ZM_E_SYNTAX, at, "expected \"^\" and a global name");
    b = ++lx->i;
    if (lx->i < lx->len && lx->t[lx->i] == '%') lx->i++;
    while (lx->i < lx->len) {
        char c = lx->t[lx->i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) lx->i++;
        else break;
    }
    if (!zm_valid_gname(lx->t + b, lx->i - b))
        return zm_lx_err(lx, ZM_E_SYNTAX, b,
                         "\"%s\" is not a global name ('%%' or a letter, then letters and "
                         "digits, at most 31)",
                         zm_show(lx->t + b, lx->i - b, sh, sizeof sh));
    memcpy(dst, lx->t + b, lx->i - b);
    dst[lx->i - b] = '\0';
    return ZXV_OK;
}

int32_t zm_lx_uint(zm_lx *lx, uint32_t max, uint32_t *out, const char *what) {
    size_t at = lx->i;
    uint64_t v = 0;
    while (lx->i < lx->len && lx->t[lx->i] >= '0' && lx->t[lx->i] <= '9') {
        v = v * 10u + (uint64_t) (lx->t[lx->i] - '0');
        if (v > max) {
            while (lx->i < lx->len && lx->t[lx->i] >= '0' && lx->t[lx->i] <= '9') lx->i++;
            return zm_lx_err(lx, ZM_E_SYNTAX, at, "%s is larger than %u", what, (unsigned) max);
        }
        lx->i++;
    }
    if (lx->i == at) return zm_lx_err(lx, ZM_E_SYNTAX, at, "expected %s", what);
    *out = (uint32_t) v;
    return ZXV_OK;
}

int zm_lx_at_literal(const zm_lx *lx) {
    char c;
    if (lx->i >= lx->len) return 0;
    c = lx->t[lx->i];
    return c == '"' || c == '-' || c == '.' || (c >= '0' && c <= '9');
}

int32_t zm_lx_literal(zm_lx *lx, zm_sub *out) {
    size_t at = lx->i;
    char sh[64];
    memset(out, 0, sizeof *out);
    out->kind = ZM_SUB_LIT;
    if (lx->i < lx->len && lx->t[lx->i] == '"') {
        size_t k = 0;
        lx->i++;
        for (;;) {
            char c;
            if (lx->i >= lx->len || lx->t[lx->i] == '\n')
                return zm_lx_err(lx, ZM_E_SYNTAX, at, "the string is not closed");
            c = lx->t[lx->i];
            if (c == '"') {
                if (lx->i + 1u < lx->len && lx->t[lx->i + 1u] == '"') {
                    lx->i++;
                } else {
                    lx->i++;
                    break;
                }
            } else if ((unsigned char) c < 0x20u || c == 0x7F) {
                return zm_lx_err(lx, ZM_E_SYNTAX, lx->i, "a control character in a literal");
            }
            if (k >= ZM_MAX_LITERAL)
                return zm_lx_err(lx, ZM_E_SYNTAX, at, "a literal subscript holds at most %u bytes",
                                 (unsigned) ZM_MAX_LITERAL);
            out->lit[k++] = c;
            lx->i++;
        }
        if (k == 0)
            return zm_lx_err(lx, ZM_E_SYNTAX, at,
                             "\"\" is the null subscript; M globals refuse it");
        out->lit[k] = '\0';
        out->litlen = (uint32_t) k;
        out->isnum = zm_is_canonic(out->lit, k); /* "10" is the number 10, as in M */
        return ZXV_OK;
    }
    while (lx->i < lx->len) {
        char c = lx->t[lx->i];
        /* the whole token, so "1x" or "1E3" is refused as one word */
        if (c == '-' || c == '.' || c == '+' || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z'))
            lx->i++;
        else break;
    }
    if (!zm_is_canonic(lx->t + at, lx->i - at))
        return zm_lx_err(lx, ZM_E_SYNTAX, at,
                         "%s is not a number in M canonical form (write it as M prints it, or "
                         "quote a string)",
                         zm_show(lx->t + at, lx->i - at, sh, sizeof sh));
    if (lx->i - at > ZM_MAX_LITERAL)
        return zm_lx_err(lx, ZM_E_SYNTAX, at, "a literal subscript holds at most %u bytes",
                         (unsigned) ZM_MAX_LITERAL);
    memcpy(out->lit, lx->t + at, lx->i - at);
    out->lit[lx->i - at] = '\0';
    out->litlen = (uint32_t) (lx->i - at);
    out->isnum = 1;
    return ZXV_OK;
}
