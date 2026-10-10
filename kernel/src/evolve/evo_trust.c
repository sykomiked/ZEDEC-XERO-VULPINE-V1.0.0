/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* evo_trust.c — user-chosen trust over lineages, and opt-in, unlinkable
 * adoption counts (k-minimum-values sketches). */
#include "evo_util.h"
#include "keccak.h"
#include "zt.h"

/* ===== Trust ===== */
void evo_trust_init(evo_trust_t *t, evo_pub_trusted_fn fn, void *ctx)
{
    evo_zero(t, sizeof(*t));
    t->pub_trusted = fn;
    t->pub_ctx = ctx;
}

evo_status_t evo_trust_pin(evo_trust_t *t, const evo_cid_t *record)
{
    if (!t || !record) return EVO_ERR_ARG;
    for (uint32_t i = 0; i < t->n_pins; i++)
        if (evo_cid_eq(&t->pin[i], record)) return EVO_ERR_DUP;
    if (t->n_pins >= EVO_TRUST_PINS) return EVO_ERR_FULL;
    evo_cpy(&t->pin[t->n_pins++], record, sizeof(evo_cid_t));
    return EVO_OK;
}

static evo_status_t rate(evo_trust_t *t, uint8_t is_key, const uint8_t id[32], uint8_t stars)
{
    if (!t || !id || stars < 1 || stars > 5) return EVO_ERR_ARG;
    for (uint32_t i = 0; i < t->n_ratings; i++)
        if (t->r[i].is_key == is_key && evo_cmp(t->r[i].id, id, 32) == 0) {
            t->r[i].stars = stars;
            return EVO_OK;
        }
    if (t->n_ratings >= EVO_TRUST_RATINGS) return EVO_ERR_FULL;
    evo_rating_t *r = &t->r[t->n_ratings++];
    r->is_key = is_key;
    r->stars = stars;
    evo_cpy(r->id, id, 32);
    return EVO_OK;
}

static uint8_t stars(const evo_trust_t *t, uint8_t is_key, const uint8_t id[32])
{
    for (uint32_t i = 0; i < t->n_ratings; i++)
        if (t->r[i].is_key == is_key && evo_cmp(t->r[i].id, id, 32) == 0) return t->r[i].stars;
    return 0;
}

/* A record is rated by its digest (the 32 bytes after the CID prefix). */
evo_status_t evo_trust_rate_key(evo_trust_t *t, const uint8_t key_id[32], uint8_t s)
{
    return rate(t, 1, key_id, s);
}

evo_status_t evo_trust_rate_record(evo_trust_t *t, const evo_cid_t *record, uint8_t s)
{
    return record ? rate(t, 0, record->b + 4, s) : EVO_ERR_ARG;
}

uint8_t evo_trust_stars_key(const evo_trust_t *t, const uint8_t key_id[32])
{
    return stars(t, 1, key_id);
}

uint8_t evo_trust_stars_record(const evo_trust_t *t, const evo_cid_t *record)
{
    return stars(t, 0, record->b + 4);
}

static bool blocked(const evo_trust_t *t, uint8_t s)
{
    return s && s < t->min_stars;
}

evo_status_t evo_trust_build_ok(const evo_trust_t *t, const evo_dag_t *g, uint32_t idx,
                                uint32_t *why_idx)
{
    if (!t || !g || idx >= g->n) return EVO_ERR_ARG;
    uint64_t endorsed = 0;
    for (uint32_t p = 0; p < t->n_pins; p++) {
        int32_t k = evo_dag_find(g, &t->pin[p]);
        if (k >= 0) endorsed |= g->node[k].anc | ((uint64_t) 1 << k);
    }
    uint64_t set = g->node[idx].anc | ((uint64_t) 1 << idx);
    for (uint32_t i = 0; i < g->n; i++) {
        if (!((set >> i) & 1u)) continue;
        const evo_dag_node_t *n = &g->node[i];
        bool ok = !blocked(t, stars(t, 0, n->cid.b + 4));
        if (ok && !((endorsed >> i) & 1u))
            ok = t->pub_trusted && t->pub_trusted(n->f.author, t->pub_ctx) &&
                 !blocked(t, stars(t, 1, n->f.author));
        if (!ok) {
            if (why_idx) *why_idx = i;
            return EVO_ERR_UNTRUSTED;
        }
    }
    return EVO_OK;
}

/* ===== Adoption sketches ===== */
void evo_adopt_init(evo_adopt_t *a, const evo_cid_t *variant, uint32_t epoch)
{
    evo_zero(a, sizeof(*a));
    evo_cpy(&a->variant, variant, sizeof(evo_cid_t));
    a->epoch = epoch;
}

static void kmv_insert(evo_adopt_t *a, uint64_t h)
{
    uint32_t i = 0;
    while (i < a->n && a->h[i] < h) i++;
    if (i < a->n && a->h[i] == h) return; /* idempotent */
    if (i >= EVO_KMV_K) return;           /* larger than all kept */
    uint32_t end = a->n < EVO_KMV_K ? a->n : EVO_KMV_K - 1;
    for (uint32_t j = end; j > i; j--) a->h[j] = a->h[j - 1];
    a->h[i] = h;
    if (a->n < EVO_KMV_K) a->n++;
}

evo_status_t evo_adopt_contribute(evo_adopt_t *a, bool opted_in, const uint8_t secret[32])
{
    if (!a || !secret || !opted_in) return EVO_ERR_ARG;
    uint8_t buf[13 + 32 + EVO_CID_LEN + 4], d[32];
    evo_w_t w = {buf, sizeof(buf), 0, false};
    evo_w_bytes(&w, "ZXV-EVO-adopt", 13);
    evo_w_bytes(&w, secret, 32);
    evo_w_bytes(&w, a->variant.b, EVO_CID_LEN);
    evo_w_u32(&w, a->epoch);
    sha3_256(buf, w.len, d);
    uint64_t h = 0;
    for (uint32_t i = 0; i < 8; i++) h |= (uint64_t) d[i] << (8 * i);
    kmv_insert(a, h);
    evo_zero(buf, sizeof(buf));
    return EVO_OK;
}

evo_status_t evo_adopt_merge(evo_adopt_t *a, const evo_adopt_t *b)
{
    if (!a || !b || a->epoch != b->epoch || !evo_cid_eq(&a->variant, &b->variant))
        return EVO_ERR_ARG;
    for (uint32_t i = 0; i < b->n && i < EVO_KMV_K; i++) kmv_insert(a, b->h[i]);
    return EVO_OK;
}

uint64_t evo_adopt_estimate(const evo_adopt_t *a)
{
    if (a->n < EVO_KMV_K) return a->n;
    uint64_t hk = a->h[EVO_KMV_K - 1];
    if (hk == 0) return a->n;
    /* (k-1) * 2^64 / hk ~= (k-1) * (2^64-1) / hk */
    uint64_t q = zt_udiv64(~(uint64_t) 0, hk, 0);
    uint64_t lim = zt_udiv64(~(uint64_t) 0, EVO_KMV_K - 1, 0);
    return q > lim ? ~(uint64_t) 0 : q * (EVO_KMV_K - 1);
}

#define ADOPT_MAGIC EVO_MAGIC('E', 'V', 'A', '1')

uint32_t evo_adopt_encode(const evo_adopt_t *a, uint8_t *out, uint32_t cap)
{
    if (!a || !out || a->n > EVO_KMV_K) return 0;
    evo_w_t w = {out, cap, 0, false};
    evo_w_u32(&w, ADOPT_MAGIC);
    evo_w_bytes(&w, a->variant.b, EVO_CID_LEN);
    evo_w_u32(&w, a->epoch);
    evo_w_u8(&w, (uint8_t) a->n);
    for (uint32_t i = 0; i < a->n; i++) evo_w_u64(&w, a->h[i]);
    return w.err ? 0 : w.len;
}

evo_status_t evo_adopt_decode(const uint8_t *in, uint32_t len, evo_adopt_t *a)
{
    if (!in || !a) return EVO_ERR_ARG;
    evo_zero(a, sizeof(*a));
    evo_r_t r = {in, len, 0, false};
    if (evo_r_le(&r, 4) != ADOPT_MAGIC) return EVO_ERR_PARSE;
    evo_r_copy(&r, a->variant.b, EVO_CID_LEN);
    a->epoch = (uint32_t) evo_r_le(&r, 4);
    uint32_t n = (uint32_t) evo_r_le(&r, 1);
    if (r.err || n > EVO_KMV_K) return EVO_ERR_PARSE;
    for (uint32_t i = 0; i < n; i++) {
        a->h[i] = evo_r_le(&r, 8);
        if (i && a->h[i - 1] >= a->h[i]) return EVO_ERR_PARSE;
    }
    a->n = n;
    if (r.err || r.pos != len) return EVO_ERR_PARSE;
    return EVO_OK;
}
