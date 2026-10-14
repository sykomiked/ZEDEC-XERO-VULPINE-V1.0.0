/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zind_util.c — status names, the thread-local last error, identifiers and
 * the canonical little-endian byte writer and reader. Integer code only
 * (R11). */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "zind_common.h"

static _Thread_local char g_err[512];

void zind_set_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    (void) vsnprintf(g_err, sizeof g_err, fmt, ap);
    va_end(ap);
}

int32_t zind_last_error(char *buf, size_t cap, size_t *out_len) {
    size_t n = strlen(g_err);
    if (out_len) *out_len = n;
    if (!buf) return cap == 0 ? ZXV_E_BUFFER : ZXV_E_ARG;
    if (cap < n + 1) return ZXV_E_BUFFER;
    memcpy(buf, g_err, n + 1);
    return ZXV_OK;
}

const char *zind_status_name(int32_t status) {
    switch (status) {
    case ZIND_E_HELD: return "ZIND_E_HELD";
    case ZIND_E_REPLAY: return "ZIND_E_REPLAY";
    case ZIND_E_CHAIN: return "ZIND_E_CHAIN";
    case ZIND_E_INEXACT: return "ZIND_E_INEXACT";
    case ZIND_E_NEGATIVE: return "ZIND_E_NEGATIVE";
    case ZIND_E_FROZEN: return "ZIND_E_FROZEN";
    case ZIND_E_UNTRUSTED: return "ZIND_E_UNTRUSTED";
    default: return zxv_status_name(status);
    }
}

int zind_ident_ok(const char *s, size_t max) {
    size_t n;
    if (!s) return 0;
    for (n = 0; s[n]; n++) {
        unsigned char c = (unsigned char) s[n];
        if (n >= max) return 0;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == ':' || c == '/' || c == '-'))
            return 0;
    }
    return n > 0;
}

/* ---- writer ---- */

void zind_wr_init(zind_wr *w, uint8_t *buf, size_t cap) {
    w->buf = buf;
    w->cap = buf ? cap : 0;
    w->len = 0;
    w->overflow = 0;
}

void zind_wr_bytes(zind_wr *w, const void *p, size_t n) {
    if (w->overflow) return;
    if (n > w->cap - w->len) {
        w->overflow = 1;
        return;
    }
    if (n) memcpy(w->buf + w->len, p, n);
    w->len += n;
}

void zind_wr_u8(zind_wr *w, uint8_t v) { zind_wr_bytes(w, &v, 1); }

void zind_wr_u16(zind_wr *w, uint16_t v) {
    uint8_t b[2] = {(uint8_t) v, (uint8_t) (v >> 8)};
    zind_wr_bytes(w, b, 2);
}

void zind_wr_u32(zind_wr *w, uint32_t v) {
    uint8_t b[4];
    for (int i = 0; i < 4; i++) b[i] = (uint8_t) (v >> (8 * i));
    zind_wr_bytes(w, b, 4);
}

void zind_wr_u64(zind_wr *w, uint64_t v) {
    uint8_t b[8];
    for (int i = 0; i < 8; i++) b[i] = (uint8_t) (v >> (8 * i));
    zind_wr_bytes(w, b, 8);
}

void zind_wr_str(zind_wr *w, const char *s) {
    size_t n = s ? strlen(s) : 0;
    if (n > 255) {
        w->overflow = 1;
        return;
    }
    zind_wr_u8(w, (uint8_t) n);
    zind_wr_bytes(w, s, n);
}

void zind_wr_qty(zind_wr *w, zind_qty q) {
    zind_wr_u64(w, (uint64_t) q.num);
    zind_wr_u64(w, (uint64_t) q.den);
}

/* ---- reader ---- */

void zind_rd_init(zind_rd *r, const uint8_t *buf, size_t len) {
    r->buf = buf;
    r->len = buf ? len : 0;
    r->pos = 0;
    r->bad = 0;
}

static const uint8_t *take(zind_rd *r, size_t n) {
    const uint8_t *p;
    if (r->bad || n > r->len - r->pos) {
        r->bad = 1;
        return NULL;
    }
    p = r->buf + r->pos;
    r->pos += n;
    return p;
}

uint8_t zind_rd_u8(zind_rd *r) {
    const uint8_t *p = take(r, 1);
    return p ? p[0] : 0;
}

uint16_t zind_rd_u16(zind_rd *r) {
    const uint8_t *p = take(r, 2);
    return p ? (uint16_t) (p[0] | (p[1] << 8)) : 0;
}

uint32_t zind_rd_u32(zind_rd *r) {
    const uint8_t *p = take(r, 4);
    uint32_t v = 0;
    if (!p) return 0;
    for (int i = 3; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

uint64_t zind_rd_u64(zind_rd *r) {
    const uint8_t *p = take(r, 8);
    uint64_t v = 0;
    if (!p) return 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

void zind_rd_str(zind_rd *r, char *out, size_t cap) {
    uint8_t n = zind_rd_u8(r);
    const uint8_t *p = take(r, n);
    if (!p || !out || (size_t) n + 1 > cap || memchr(p, 0, n)) {
        r->bad = 1;
        if (out && cap) out[0] = '\0';
        return;
    }
    memcpy(out, p, n);
    out[n] = '\0';
}

void zind_rd_qty(zind_rd *r, zind_qty *out) {
    zind_qty q;
    q.num = (int64_t) zind_rd_u64(r);
    q.den = (int64_t) zind_rd_u64(r);
    if (r->bad || !zind_qty_valid(q)) {
        r->bad = 1;
        q = ZIND_QTY_ZERO;
    }
    if (out) *out = q;
}
