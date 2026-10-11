/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* za_json.c — a strict RFC 8259 JSON reader that flattens a document into
 * named fields ("order.items.0.price"). Numbers are kept exact
 * (za_decimal_parse); nothing is rounded. Integer code only (R11).
 *
 * Refused (the whole record): anything that is not one JSON text, invalid
 * UTF-8, raw control characters in strings, unpaired surrogates, a BOM,
 * nesting deeper than ZA_MAX_DEPTH, more than ZA_MAX_FIELDS leaves, a path
 * longer than ZA_MAX_NAME, a path given twice (duplicate keys, or a key
 * containing '.' that collides with a nested path), an empty key (it
 * names no path segment; UNSUPPORTED), and numbers with no exact int64
 * rational form. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "za_internal.h"

typedef struct {
    const uint8_t *s;
    size_t n, pos;
    za_record *rec;
    char path[ZA_MAX_NAME + 2];
    size_t plen;
    int failed;
    uint8_t *buf; /* decoded string scratch */
    size_t blen, bcap;
} J;

static void jfail(J *j, int32_t code, const char *what) {
    if (j->failed) return;
    j->failed = 1;
    za_rec_refuse(j->rec, code, "JSON at byte %zu: %s", j->pos, what);
}

static void ws(J *j) {
    while (j->pos < j->n && (j->s[j->pos] == ' ' || j->s[j->pos] == '\t' || j->s[j->pos] == '\n' ||
                             j->s[j->pos] == '\r'))
        j->pos++;
}

static int bput(J *j, uint8_t c) {
    if (j->blen == j->bcap) {
        size_t nc = j->bcap ? j->bcap * 2u : 64u;
        uint8_t *nb = (uint8_t *) realloc(j->buf, nc);
        if (!nb) {
            j->rec->oom = 1;
            j->failed = 1;
            return -1;
        }
        j->buf = nb;
        j->bcap = nc;
    }
    j->buf[j->blen++] = c;
    return 0;
}

static int put_utf8(J *j, uint32_t cp) {
    if (cp < 0x80u) return bput(j, (uint8_t) cp);
    if (cp < 0x800u)
        return bput(j, (uint8_t) (0xC0u | (cp >> 6))) || bput(j, (uint8_t) (0x80u | (cp & 0x3Fu)));
    if (cp < 0x10000u)
        return bput(j, (uint8_t) (0xE0u | (cp >> 12))) ||
               bput(j, (uint8_t) (0x80u | ((cp >> 6) & 0x3Fu))) ||
               bput(j, (uint8_t) (0x80u | (cp & 0x3Fu)));
    return bput(j, (uint8_t) (0xF0u | (cp >> 18))) || bput(j, (uint8_t) (0x80u | ((cp >> 12) & 0x3Fu))) ||
           bput(j, (uint8_t) (0x80u | ((cp >> 6) & 0x3Fu))) || bput(j, (uint8_t) (0x80u | (cp & 0x3Fu)));
}

static int hex4(J *j, uint32_t *out) {
    uint32_t v = 0;
    if (j->n - j->pos < 4u) return -1;
    for (int k = 0; k < 4; k++) {
        uint8_t c = j->s[j->pos + (size_t) k];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (uint32_t) (c - '0');
        else if (c >= 'a' && c <= 'f') v |= (uint32_t) (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (uint32_t) (c - 'A' + 10);
        else return -1;
    }
    j->pos += 4;
    *out = v;
    return 0;
}

/* A string at j->pos (the opening quote); decoded into j->buf / j->blen. */
static int string(J *j) {
    j->blen = 0;
    j->pos++;
    for (;;) {
        if (j->pos >= j->n) {
            jfail(j, ZA_R_MALFORMED, "unterminated string");
            return -1;
        }
        uint8_t c = j->s[j->pos];
        if (c == '"') {
            j->pos++;
            return 0;
        }
        if (c < 0x20u) {
            jfail(j, ZA_R_MALFORMED, "control character in a string");
            return -1;
        }
        if (c != '\\') {
            if (bput(j, c)) return -1;
            j->pos++;
            continue;
        }
        j->pos++;
        if (j->pos >= j->n) {
            jfail(j, ZA_R_MALFORMED, "unterminated escape");
            return -1;
        }
        c = j->s[j->pos++];
        uint8_t out = 0;
        switch (c) {
        case '"': out = '"'; break;
        case '\\': out = '\\'; break;
        case '/': out = '/'; break;
        case 'b': out = 0x08; break;
        case 'f': out = 0x0C; break;
        case 'n': out = 0x0A; break;
        case 'r': out = 0x0D; break;
        case 't': out = 0x09; break;
        case 'u': {
            uint32_t cp, lo;
            if (hex4(j, &cp)) {
                jfail(j, ZA_R_MALFORMED, "bad \\u escape");
                return -1;
            }
            if (cp >= 0xD800u && cp <= 0xDBFFu) {
                if (j->n - j->pos < 6u || j->s[j->pos] != '\\' || j->s[j->pos + 1u] != 'u') {
                    jfail(j, ZA_R_UTF8, "unpaired surrogate");
                    return -1;
                }
                j->pos += 2;
                if (hex4(j, &lo) || lo < 0xDC00u || lo > 0xDFFFu) {
                    jfail(j, ZA_R_UTF8, "unpaired surrogate");
                    return -1;
                }
                cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
            } else if (cp >= 0xDC00u && cp <= 0xDFFFu) {
                jfail(j, ZA_R_UTF8, "unpaired surrogate");
                return -1;
            }
            if (put_utf8(j, cp)) return -1;
            continue;
        }
        default: jfail(j, ZA_R_MALFORMED, "unknown escape"); return -1;
        }
        if (bput(j, out)) return -1;
    }
}

/* Push a path segment; returns the old length to restore, or -1. */
static long push_seg(J *j, const char *seg, size_t len) {
    size_t old = j->plen, need = old + (old ? 1u : 0u) + len;
    if (need > ZA_MAX_NAME) {
        jfail(j, ZA_R_LIMIT, "a field path is longer than 255 bytes");
        return -1;
    }
    if (old) j->path[j->plen++] = '.';
    if (len) memcpy(j->path + j->plen, seg, len);
    j->plen += len;
    j->path[j->plen] = '\0';
    return (long) old;
}

static void leaf(J *j, int32_t kind, int64_t num, int64_t den, const uint8_t *text, size_t len) {
    const char *name = j->plen ? j->path : "value";
    size_t nl = j->plen ? j->plen : 5u;
    int r = za_rec_add(j->rec, name, nl, kind, num, den, text, len);
    if (r == ZA_R_DUPLICATE) jfail(j, ZA_R_DUPLICATE, "a field path is given twice");
    else if (r == ZA_R_LIMIT) jfail(j, ZA_R_LIMIT, "more than 1024 fields");
    else if (r < 0) j->failed = 1;
}

static int lit(J *j, const char *w) {
    size_t l = strlen(w);
    if (j->n - j->pos < l || memcmp(j->s + j->pos, w, l) != 0) return -1;
    j->pos += l;
    return 0;
}

static void number(J *j) {
    size_t a = j->pos;
    int64_t num = 0, den = 1;
    if (j->pos < j->n && j->s[j->pos] == '-') j->pos++;
    if (j->pos >= j->n || j->s[j->pos] < '0' || j->s[j->pos] > '9') {
        jfail(j, ZA_R_MALFORMED, "bad number");
        return;
    }
    if (j->s[j->pos] == '0') {
        j->pos++;
    } else {
        while (j->pos < j->n && j->s[j->pos] >= '0' && j->s[j->pos] <= '9') j->pos++;
    }
    if (j->pos < j->n && j->s[j->pos] == '.') {
        j->pos++;
        size_t f = j->pos;
        while (j->pos < j->n && j->s[j->pos] >= '0' && j->s[j->pos] <= '9') j->pos++;
        if (j->pos == f) {
            jfail(j, ZA_R_MALFORMED, "bad number: no digit after '.'");
            return;
        }
    }
    if (j->pos < j->n && (j->s[j->pos] == 'e' || j->s[j->pos] == 'E')) {
        j->pos++;
        if (j->pos < j->n && (j->s[j->pos] == '+' || j->s[j->pos] == '-')) j->pos++;
        size_t f = j->pos;
        while (j->pos < j->n && j->s[j->pos] >= '0' && j->s[j->pos] <= '9') j->pos++;
        if (j->pos == f) {
            jfail(j, ZA_R_MALFORMED, "bad number: no exponent digits");
            return;
        }
    }
    if (za_decimal_parse((const char *) j->s + a, j->pos - a, &num, &den) != ZXV_OK) {
        size_t save = j->pos;
        j->pos = a;
        jfail(j, ZA_R_NUMBER, "a number with no exact int64 rational form (nothing is rounded)");
        j->pos = save;
        return;
    }
    leaf(j, ZA_F_NUMBER, num, den, j->s + a, j->pos - a);
}

static void value(J *j, uint32_t depth) {
    ws(j);
    if (j->failed) return;
    if (j->pos >= j->n) {
        jfail(j, ZA_R_MALFORMED, "expected a value");
        return;
    }
    uint8_t c = j->s[j->pos];
    if (c == '{' || c == '[') {
        int obj = c == '{';
        uint32_t idx = 0;
        if (depth >= ZA_MAX_DEPTH) {
            jfail(j, ZA_R_LIMIT, "nested deeper than 32");
            return;
        }
        j->pos++;
        ws(j);
        if (j->pos < j->n && j->s[j->pos] == (obj ? '}' : ']')) {
            j->pos++;
            return;
        }
        for (;;) {
            long old;
            ws(j);
            if (obj) {
                if (j->pos >= j->n || j->s[j->pos] != '"') {
                    jfail(j, ZA_R_MALFORMED, "expected a key string");
                    return;
                }
                if (string(j)) return;
                if (j->blen == 0) { /* "" would name no path segment */
                    jfail(j, ZA_R_UNSUPPORTED, "an empty key cannot name a field");
                    return;
                }
                if (memchr(j->buf, 0, j->blen)) {
                    jfail(j, ZA_R_MALFORMED, "a key contains NUL");
                    return;
                }
                old = push_seg(j, (const char *) j->buf, j->blen);
                if (old < 0) return;
                ws(j);
                if (j->pos >= j->n || j->s[j->pos] != ':') {
                    jfail(j, ZA_R_MALFORMED, "expected ':'");
                    return;
                }
                j->pos++;
            } else {
                char seg[16];
                int sl = snprintf(seg, sizeof seg, "%u", (unsigned) idx);
                old = push_seg(j, seg, (size_t) sl);
                if (old < 0) return;
            }
            idx++;
            value(j, depth + 1u);
            if (j->failed) return;
            j->plen = (size_t) old;
            j->path[j->plen] = '\0';
            ws(j);
            if (j->pos < j->n && j->s[j->pos] == ',') {
                j->pos++;
                continue;
            }
            if (j->pos < j->n && j->s[j->pos] == (obj ? '}' : ']')) {
                j->pos++;
                return;
            }
            jfail(j, ZA_R_MALFORMED, obj ? "expected ',' or '}'" : "expected ',' or ']'");
            return;
        }
    }
    if (c == '"') {
        if (string(j) == 0) leaf(j, ZA_F_TEXT, 0, 1, j->buf ? j->buf : (const uint8_t *) "", j->blen);
        return;
    }
    if (c == 't' || c == 'f' || c == 'n') {
        if (lit(j, "true") == 0) leaf(j, ZA_F_BOOL, 1, 1, (const uint8_t *) "true", 4);
        else if (lit(j, "false") == 0) leaf(j, ZA_F_BOOL, 0, 1, (const uint8_t *) "false", 5);
        else if (lit(j, "null") == 0) leaf(j, ZA_F_NULL, 0, 1, (const uint8_t *) "null", 4);
        else jfail(j, ZA_R_MALFORMED, "unknown literal");
        return;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        number(j);
        return;
    }
    jfail(j, ZA_R_MALFORMED, "unexpected character");
}

void za_json_into(za_record *rec, const uint8_t *s, size_t n) {
    J j;
    memset(&j, 0, sizeof j);
    j.s = s;
    j.n = n;
    j.rec = rec;
    if (!za_utf8_ok(s, n)) {
        za_rec_refuse(rec, ZA_R_UTF8, "JSON: the payload is not valid UTF-8");
        return;
    }
    if (n >= 3u && s[0] == 0xEF && s[1] == 0xBB && s[2] == 0xBF) {
        za_rec_refuse(rec, ZA_R_MALFORMED, "JSON at byte 0: a byte order mark is not allowed");
        return;
    }
    value(&j, 0);
    if (!j.failed) {
        ws(&j);
        if (j.pos != j.n) jfail(&j, ZA_R_MALFORMED, "text after the JSON value");
    }
    free(j.buf);
}
