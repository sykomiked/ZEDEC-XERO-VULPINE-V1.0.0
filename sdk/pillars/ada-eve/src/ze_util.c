/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ze_util.c — status names, the last error, parse diagnostics, names,
 * UTF-8 and text buffers. Integer code only (R11). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ze_internal.h"

static _Thread_local char g_err[512];

void ze_set_error(const char *msg) {
    size_t n = msg ? strlen(msg) : 0;
    if (n >= sizeof g_err) n = sizeof g_err - 1u;
    if (n) memcpy(g_err, msg, n);
    g_err[n] = '\0';
}

void ze_set_errorf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof g_err, fmt, ap);
    va_end(ap);
}

int32_t ze_last_error(char *buf, size_t cap, size_t *out_len) {
    return ze_out_text(g_err, strlen(g_err), buf, cap, out_len);
}

const char *ze_status_name(int32_t status) {
    switch (status) {
    case ZE_E_SYNTAX: return "ZE_E_SYNTAX";
    case ZE_E_UNSUPPORTED: return "ZE_E_UNSUPPORTED";
    case ZE_E_NAME: return "ZE_E_NAME";
    case ZE_E_CONSTRAINT: return "ZE_E_CONSTRAINT";
    case ZE_E_DATA: return "ZE_E_DATA";
    case ZE_E_FIT: return "ZE_E_FIT";
    case ZE_E_LIMIT: return "ZE_E_LIMIT";
    default: return zxv_status_name(status);
    }
}

int32_t ze_status_class(int32_t status) {
    switch (status) {
    case ZE_E_SYNTAX:
    case ZE_E_UNSUPPORTED:
    case ZE_E_NAME:
    case ZE_E_CONSTRAINT: return ZXV_CLASS_USAGE;
    case ZE_E_DATA:
    case ZE_E_FIT:
    case ZE_E_LIMIT: return ZXV_CLASS_REFUSED;
    default: return zxv_status_class(status);
    }
}

int32_t ze_diag_set(ze_diag *d, int32_t status, const char *text, size_t len, uint64_t offset,
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
    ze_set_errorf("line %u, column %u: %s", (unsigned) line, (unsigned) col, body);
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

size_t ze_utf8_check(const uint8_t *s, size_t n) {
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

int32_t ze_out_text(const char *src, size_t n, char *buf, size_t cap, size_t *out_len) {
    if (!out_len) return ZXV_E_ARG;
    *out_len = n;
    if (!buf || cap <= n) return ZXV_E_BUFFER;
    if (n) memcpy(buf, src, n);
    buf[n] = '\0';
    return ZXV_OK;
}

static int word_in(const char *s, size_t n, const char *const *list, size_t count) {
    char w[24];
    if (n == 0 || n >= sizeof w) return 0;
    ze_lower(w, s, n);
    for (size_t i = 0; i < count; i++)
        if (strcmp(w, list[i]) == 0) return 1;
    return 0;
}

/* Ada 2022 reserved words (RM 2.9; "parallel" and "some" are new in 2022). */
static const char *const k_reserved[] = {
    "abort",     "abs",       "abstract",   "accept",    "access",    "aliased",  "all",
    "and",       "array",     "at",         "begin",     "body",      "case",     "constant",
    "declare",   "delay",     "delta",      "digits",    "do",        "else",     "elsif",
    "end",       "entry",     "exception",  "exit",      "for",       "function", "generic",
    "goto",      "if",        "in",         "interface", "is",        "limited",  "loop",
    "mod",       "new",       "not",        "null",      "of",        "or",       "others",
    "out",       "overriding", "package",   "parallel",  "pragma",    "private",  "procedure",
    "protected", "raise",     "range",      "record",    "rem",       "renames",  "requeue",
    "return",    "reverse",   "select",     "separate",  "some",      "subtype",  "synchronized",
    "tagged",    "task",      "terminate",  "then",      "type",      "until",    "use",
    "when",      "while",     "with",       "xor",
};

int ze_reserved(const char *s, size_t n) {
    return word_in(s, n, k_reserved, sizeof k_reserved / sizeof k_reserved[0]);
}

/* Names from package Standard (and Eve's two type words) that a package
 * may not declare as a type: every layout must print as Ada and as Eve
 * without one name meaning two things. */
static const char *const k_predefined[] = {
    "boolean",           "integer",          "natural",        "positive",
    "long_integer",      "long_long_integer", "short_integer", "short_short_integer",
    "float",             "long_float",       "short_float",    "long_long_float",
    "character",         "wide_character",   "wide_wide_character",
    "string",            "wide_string",      "wide_wide_string", "duration",
    "standard",          "true",             "false",          "dec",
    "str",
};

int ze_predefined(const char *s, size_t n) {
    return word_in(s, n, k_predefined, sizeof k_predefined / sizeof k_predefined[0]);
}

void ze_lower(char *dst, const char *src, size_t n) {
    for (size_t i = 0; i < n; i++) dst[i] = (src[i] >= 'A' && src[i] <= 'Z') ? (char) (src[i] + 32) : src[i];
    dst[n] = '\0';
}

int ze_ieq(const char *a, const char *b) {
    for (;; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char) (x + 32);
        if (y >= 'A' && y <= 'Z') y = (char) (y + 32);
        if (x != y) return 0;
        if (!x) return 1;
    }
}

void ze_ada_case(char *dst, const char *lower) {
    int up = 1;
    size_t i = 0;
    for (; lower[i]; i++) {
        char c = lower[i];
        dst[i] = (up && c >= 'a' && c <= 'z') ? (char) (c - 32) : c;
        up = (c == '_');
    }
    dst[i] = '\0';
}

/* ---- text buffers ---- */
void ze_sb_put(ze_sb *s, const char *t, size_t n) {
    if (s->failed) return;
    if (s->n + n + 1u > s->cap) {
        size_t nc = s->cap ? s->cap : 256u;
        char *np;
        while (nc < s->n + n + 1u) nc *= 2u;
        np = (char *) realloc(s->p, nc);
        if (!np) {
            s->failed = 1;
            return;
        }
        s->p = np;
        s->cap = nc;
    }
    if (n) memcpy(s->p + s->n, t, n);
    s->n += n;
    s->p[s->n] = '\0';
}

void ze_sb_str(ze_sb *s, const char *t) { ze_sb_put(s, t, strlen(t)); }

void ze_sb_i64(ze_sb *s, int64_t v) {
    char b[24];
    int k = 23;
    uint64_t u = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    b[k] = '\0';
    do {
        b[--k] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u);
    if (v < 0) b[--k] = '-';
    ze_sb_str(s, b + k);
}

void ze_sb_dec(ze_sb *s, int64_t units, uint32_t scale) {
    char b[48];
    int k = 47;
    uint64_t u = units < 0 ? (uint64_t) 0 - (uint64_t) units : (uint64_t) units;
    uint32_t d = 0;
    b[k] = '\0';
    do {
        if (scale && d == scale) b[--k] = '.';
        b[--k] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
        d++;
    } while (u || d <= scale);
    if (units < 0) b[--k] = '-';
    ze_sb_str(s, b + k);
}
