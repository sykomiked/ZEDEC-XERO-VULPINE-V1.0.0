/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* dm_internal.h — bounded big-endian writer and reader shared by the devmesh
 * sources. Not part of the public API. */
#ifndef ZXV_DM_INTERNAL_H
#define ZXV_DM_INTERNAL_H

#include "devmesh.h"

typedef struct {
    uint8_t *p;
    uint32_t cap, n;
    uint8_t err;
} dm_w_t;

typedef struct {
    const uint8_t *p;
    uint32_t len, off;
    uint8_t err;
} dm_r_t;

static inline void dm_w_init(dm_w_t *w, uint8_t *p, uint32_t cap)
{
    w->p = p;
    w->cap = cap;
    w->n = 0;
    w->err = 0;
}

static inline uint8_t *dm_w_reserve(dm_w_t *w, uint32_t k)
{
    if (w->err || k > w->cap - w->n) {
        w->err = 1;
        return 0;
    }
    uint8_t *q = w->p + w->n;
    w->n += k;
    return q;
}

static inline void dm_w_u8(dm_w_t *w, uint8_t v)
{
    uint8_t *q = dm_w_reserve(w, 1);
    if (q) q[0] = v;
}

static inline void dm_w_u16(dm_w_t *w, uint16_t v)
{
    uint8_t *q = dm_w_reserve(w, 2);
    if (q) {
        q[0] = (uint8_t) (v >> 8);
        q[1] = (uint8_t) v;
    }
}

static inline void dm_w_u32(dm_w_t *w, uint32_t v)
{
    uint8_t *q = dm_w_reserve(w, 4);
    if (q)
        for (int i = 0; i < 4; i++) q[i] = (uint8_t) (v >> (24 - 8 * i));
}

static inline void dm_w_u64(dm_w_t *w, uint64_t v)
{
    uint8_t *q = dm_w_reserve(w, 8);
    if (q)
        for (int i = 0; i < 8; i++) q[i] = (uint8_t) (v >> (56 - 8 * i));
}

static inline void dm_w_bytes(dm_w_t *w, const void *src, uint32_t k)
{
    uint8_t *q = dm_w_reserve(w, k);
    if (q) dm_mcpy(q, src, k);
}

static inline void dm_r_init(dm_r_t *r, const uint8_t *p, uint32_t len)
{
    r->p = p;
    r->len = len;
    r->off = 0;
    r->err = 0;
}

static inline const uint8_t *dm_r_take(dm_r_t *r, uint32_t k)
{
    if (r->err || k > r->len - r->off) {
        r->err = 1;
        return 0;
    }
    const uint8_t *q = r->p + r->off;
    r->off += k;
    return q;
}

static inline uint8_t dm_r_u8(dm_r_t *r)
{
    const uint8_t *q = dm_r_take(r, 1);
    return q ? q[0] : 0;
}

static inline uint16_t dm_r_u16(dm_r_t *r)
{
    const uint8_t *q = dm_r_take(r, 2);
    return q ? (uint16_t) ((q[0] << 8) | q[1]) : 0;
}

static inline uint32_t dm_r_u32(dm_r_t *r)
{
    const uint8_t *q = dm_r_take(r, 4);
    uint32_t v = 0;
    if (q)
        for (int i = 0; i < 4; i++) v = (v << 8) | q[i];
    return v;
}

static inline uint64_t dm_r_u64(dm_r_t *r)
{
    const uint8_t *q = dm_r_take(r, 8);
    uint64_t v = 0;
    if (q)
        for (int i = 0; i < 8; i++) v = (v << 8) | q[i];
    return v;
}

static inline void dm_r_bytes(dm_r_t *r, void *dst, uint32_t k)
{
    const uint8_t *q = dm_r_take(r, k);
    if (q) dm_mcpy(dst, q, k);
}

static inline bool dm_r_done(const dm_r_t *r)
{
    return !r->err && r->off == r->len;
}

#endif /* ZXV_DM_INTERNAL_H */
