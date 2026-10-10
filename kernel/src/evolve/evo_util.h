/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* evo_util.h — private helpers for src/evolve: byte copies without libc and a
 * bounds-checked little-endian writer / reader.
 *
 * HONEST LIMITS. Internal only; not a public API. A writer that overflows
 * latches err and writes nothing further; a reader that underflows latches
 * err and returns zeros. Callers check err once at the end.
 */
#ifndef ZXV_EVO_UTIL_H
#define ZXV_EVO_UTIL_H

#include "evo.h"

static inline void evo_cpy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static inline void evo_zero(void *dst, uint32_t n)
{
    volatile uint8_t *d = (volatile uint8_t *) dst;
    for (uint32_t i = 0; i < n; i++) d[i] = 0;
}

static inline int evo_cmp(const void *a, const void *b, uint32_t n)
{
    const uint8_t *x = (const uint8_t *) a, *y = (const uint8_t *) b;
    for (uint32_t i = 0; i < n; i++)
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

static inline uint32_t evo_strlen(const char *s, uint32_t max)
{
    uint32_t n = 0;
    while (n <= max && s[n]) n++;
    return n;
}

/* Compare two length-prefixed names; shorter-is-smaller on equal prefix. */
static inline int evo_name_cmp(const char *a, uint32_t al, const char *b, uint32_t bl)
{
    uint32_t n = al < bl ? al : bl;
    int c = evo_cmp(a, b, n);
    if (c) return c;
    return al == bl ? 0 : (al < bl ? -1 : 1);
}

typedef struct {
    uint8_t *p;
    uint32_t cap, len;
    bool err;
} evo_w_t;

static inline void evo_w_bytes(evo_w_t *w, const void *src, uint32_t n)
{
    if (w->err || n > w->cap - w->len) {
        w->err = true;
        return;
    }
    evo_cpy(w->p + w->len, src, n);
    w->len += n;
}

static inline void evo_w_u8(evo_w_t *w, uint8_t v)
{
    evo_w_bytes(w, &v, 1);
}

static inline void evo_w_u16(evo_w_t *w, uint16_t v)
{
    uint8_t b[2] = {(uint8_t) v, (uint8_t) (v >> 8)};
    evo_w_bytes(w, b, 2);
}

static inline void evo_w_u32(evo_w_t *w, uint32_t v)
{
    uint8_t b[4];
    for (uint32_t i = 0; i < 4; i++) b[i] = (uint8_t) (v >> (8 * i));
    evo_w_bytes(w, b, 4);
}

static inline void evo_w_u64(evo_w_t *w, uint64_t v)
{
    uint8_t b[8];
    for (uint32_t i = 0; i < 8; i++) b[i] = (uint8_t) (v >> (8 * i));
    evo_w_bytes(w, b, 8);
}

typedef struct {
    const uint8_t *p;
    uint32_t len, pos;
    bool err;
} evo_r_t;

static inline const uint8_t *evo_r_bytes(evo_r_t *r, uint32_t n)
{
    if (r->err || n > r->len - r->pos) {
        r->err = true;
        return 0;
    }
    const uint8_t *q = r->p + r->pos;
    r->pos += n;
    return q;
}

static inline void evo_r_copy(evo_r_t *r, void *dst, uint32_t n)
{
    const uint8_t *q = evo_r_bytes(r, n);
    if (q)
        evo_cpy(dst, q, n);
    else
        evo_zero(dst, n);
}

static inline uint64_t evo_r_le(evo_r_t *r, uint32_t n)
{
    const uint8_t *q = evo_r_bytes(r, n);
    uint64_t v = 0;
    if (!q) return 0;
    for (uint32_t i = 0; i < n; i++) v |= (uint64_t) q[i] << (8 * i);
    return v;
}

#define EVO_MAGIC(a, b, c, d)                                                                      \
    ((uint32_t) (a) | ((uint32_t) (b) << 8) | ((uint32_t) (c) << 16) | ((uint32_t) (d) << 24))

/* Shared by evo_cap.c and evo_lineage.c: name from a C string. */
static inline bool evo_set_name(char *dst, uint8_t *dlen, const char *src, uint32_t max)
{
    if (!src) return false;
    uint32_t n = evo_strlen(src, max);
    if (n == 0 || n > max) return false;
    evo_zero(dst, max);
    evo_cpy(dst, src, n);
    *dlen = (uint8_t) n;
    return true;
}

#endif /* ZXV_EVO_UTIL_H */
