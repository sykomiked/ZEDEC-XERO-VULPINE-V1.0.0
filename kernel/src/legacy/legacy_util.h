/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* legacy_util.h — small freestanding helpers shared across the legacy bridge.
 *
 * RULES. Freestanding C11: no libc, no malloc, no floating point, no 64-bit
 * division, no __int128. Header-only static inline helpers: big-endian
 * load/store, bounded byte copy/compare/length, a bounded output writer that
 * never overruns its buffer, and ASCII case helpers. Everything here is a
 * plain byte loop with an explicit capacity.
 *
 * HONEST LIMITS. These are plumbing helpers, not tuned memory routines and
 * not security primitives. Nothing here authenticates, encrypts or validates
 * anything on the wire; it only moves bytes within bounds we checked.
 */
#ifndef ZXV_LEGACY_UTIL_H
#define ZXV_LEGACY_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ===== big-endian load/store ===== */
static inline void lg_put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t) (v >> 8);
    p[1] = (uint8_t) v;
}
static inline void lg_put24(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 16);
    p[1] = (uint8_t) (v >> 8);
    p[2] = (uint8_t) v;
}
static inline void lg_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}
static inline uint16_t lg_get16(const uint8_t *p)
{
    return (uint16_t) (((uint16_t) p[0] << 8) | p[1]);
}
static inline uint32_t lg_get24(const uint8_t *p)
{
    return ((uint32_t) p[0] << 16) | ((uint32_t) p[1] << 8) | p[2];
}
static inline uint32_t lg_get32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

/* ===== bounded byte helpers ===== */
static inline void lg_copy(uint8_t *d, const uint8_t *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}
static inline void lg_fill(uint8_t *d, uint8_t v, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) d[i] = v;
}
static inline int lg_cmp(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}
/* bounded C-string length (scans at most max bytes). */
static inline uint32_t lg_strnlen(const char *s, uint32_t max)
{
    uint32_t i = 0;
    while (i < max && s[i]) i++;
    return i;
}

/* ===== ASCII helpers (no locale, no libc) ===== */
static inline uint8_t lg_lower(uint8_t c)
{
    return (c >= 'A' && c <= 'Z') ? (uint8_t) (c + 32) : c;
}
static inline bool lg_ascii_ieq(const char *a, const char *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (lg_lower((uint8_t) a[i]) != lg_lower((uint8_t) b[i])) return false;
    return true;
}
static inline bool lg_is_digit(uint8_t c)
{
    return c >= '0' && c <= '9';
}

/* ===== bounded output writer =====
 * Appends into a fixed buffer; once full it stops writing but keeps counting
 * so callers can detect truncation. Never writes past cap. */
typedef struct {
    uint8_t *buf;
    uint32_t cap;
    uint32_t len; /* bytes that would have been written; > cap means truncated */
} lg_writer;

static inline void lg_w_init(lg_writer *w, uint8_t *buf, uint32_t cap)
{
    w->buf = buf;
    w->cap = cap;
    w->len = 0;
}
static inline void lg_w_byte(lg_writer *w, uint8_t b)
{
    if (w->len < w->cap) w->buf[w->len] = b;
    w->len++;
}
static inline void lg_w_bytes(lg_writer *w, const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) lg_w_byte(w, p[i]);
}
static inline void lg_w_str(lg_writer *w, const char *s)
{
    while (*s) lg_w_byte(w, (uint8_t) *s++);
}
/* unsigned decimal, no libc. */
static inline void lg_w_u32(lg_writer *w, uint32_t v)
{
    uint8_t tmp[10];
    uint32_t n = 0;
    if (v == 0) {
        lg_w_byte(w, '0');
        return;
    }
    while (v) {
        tmp[n++] = (uint8_t) ('0' + v % 10);
        v /= 10;
    }
    while (n) lg_w_byte(w, tmp[--n]);
}
static inline bool lg_w_ok(const lg_writer *w)
{
    return w->len <= w->cap;
}

/* ===== 64-bit division by a small constant, without libgcc's __udivdi3 =====
 * Freestanding rule: no 64-bit division. We do schoolbook long division over
 * 16-bit limbs, so every operation is 32-bit. d must be <= 65536 (true for
 * every divisor we use: 10 for decimal digits, 16 for nibbles). */
static inline uint64_t lg_udiv64_small(uint64_t n, uint32_t d, uint32_t *rem)
{
    uint32_t r = 0;
    uint64_t q = 0;
    for (int shift = 48; shift >= 0; shift -= 16) {
        uint32_t limb = (uint32_t) ((n >> shift) & 0xFFFFu);
        uint32_t acc = (r << 16) | limb; /* fits in 32 bits for d <= 65536 */
        uint32_t ql = acc / d;           /* 32-bit division is permitted */
        r = acc % d;
        q = (q << 16) | ql;
    }
    if (rem) *rem = r;
    return q;
}

#endif /* ZXV_LEGACY_UTIL_H */
