/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* ct_gate.c — tier-1 geometric surplus gate (see ct_gate.h). */
#include "ct_gate.h"

static uint64_t uabs64(int64_t v)
{
    return v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
}

static uint32_t top_bit64(uint64_t m)
{
    uint32_t t = 0;
    while (m >> t > 1u) t++;
    return t;
}

/* out = s / |s| in Q14 (each entry within +-2^14), or zeros when s is zero.
 * s is first scaled so its largest entry has bit 14 set: the squares then
 * sum below dim * 2^28, and each quotient is a 32-bit division. */
static void unit14(const int64_t *s, uint32_t dim, zt_fx *out)
{
    uint64_t m = 0;
    for (uint32_t i = 0; i < dim; i++)
        if (uabs64(s[i]) > m) m = uabs64(s[i]);
    if (!m) {
        for (uint32_t i = 0; i < dim; i++) out[i] = 0;
        return;
    }
    uint32_t t = top_bit64(m);
    int64_t nn = 0;
    for (uint32_t i = 0; i < dim; i++) {
        uint64_t a = uabs64(s[i]);
        a = t >= 14 ? a >> (t - 14) : a << (14 - t);
        int32_t v = (int32_t) a;
        out[i] = s[i] < 0 ? -v : v;
        nn += (int64_t) v * v;
    }
    int32_t r = (int32_t) zt_isqrt64((uint64_t) nn);
    if (r <= 0) r = 1;
    for (uint32_t i = 0; i < dim; i++) out[i] = (zt_fx) ((out[i] * 16384) / r);
}

static void unit14_row(const zt_fx *x, uint32_t dim, zt_fx *out)
{
    int64_t s[CT_GATE_MAX_DIM];
    for (uint32_t i = 0; i < dim; i++) s[i] = x[i];
    unit14(s, dim, out);
}

static int64_t dot14(const zt_fx *a, const zt_fx *b, uint32_t dim)
{
    int64_t d = 0;
    for (uint32_t i = 0; i < dim; i++) d += (int64_t) a[i] * b[i];
    return d;
}

/* Spherical k-means with nc clusters over n rows; cent[c] are unit Q14
 * centroids, assign[t] the cluster of row t. Returns multiply-adds used. */
static uint64_t cluster(const zt_fx *x, uint32_t n, uint32_t stride, uint32_t dim, uint32_t nc,
                        uint32_t iters, zt_fx (*cent)[CT_GATE_MAX_DIM], uint8_t *assign)
{
    zt_fx u[CT_GATE_MAX_DIM];
    uint64_t macs = 0;
    for (uint32_t c = 0; c < nc; c++) {
        uint32_t t = (c * n) / nc; /* 32-bit: c < 16, n < 2^27 */
        unit14_row(x + (uint64_t) t * stride, dim, cent[c]);
    }
    for (uint32_t it = 0; it <= iters; it++) {
        for (uint32_t t = 0; t < n; t++) {
            unit14_row(x + (uint64_t) t * stride, dim, u);
            uint32_t best = 0;
            int64_t bd = INT64_MIN;
            for (uint32_t c = 0; c < nc; c++) {
                int64_t d = dot14(u, cent[c], dim);
                if (d > bd) {
                    bd = d;
                    best = c;
                }
            }
            assign[t] = (uint8_t) best;
        }
        macs += (uint64_t) n * nc * dim;
        if (it == iters) break;
        for (uint32_t c = 0; c < nc; c++) {
            int64_t acc[CT_GATE_MAX_DIM];
            uint32_t members = 0;
            for (uint32_t i = 0; i < dim; i++) acc[i] = 0;
            for (uint32_t t = 0; t < n; t++) {
                if (assign[t] != c) continue;
                unit14_row(x + (uint64_t) t * stride, dim, u);
                for (uint32_t i = 0; i < dim; i++) acc[i] += u[i];
                members++;
            }
            if (members) unit14(acc, dim, cent[c]); /* an empty cluster keeps its centroid */
        }
        macs += (uint64_t) n * dim;
    }
    return macs;
}

int32_t ct_gate_build(ct_gate_t *g, const ct_gate_cfg_t *cfg, const zt_fx *q, uint32_t nq,
                      uint32_t q_stride, const zt_fx *k, uint32_t nk, uint32_t k_stride,
                      uint32_t dim, uint8_t *q_assign, uint8_t *k_assign)
{
    if (!g || !cfg || !dim || dim > CT_GATE_MAX_DIM || cfg->mode > CT_GATE_OFF ||
        !cfg->n_clusters || cfg->n_clusters > CT_GATE_MAX_CLUSTERS ||
        cfg->iters > CT_GATE_MAX_ITERS || (nq && (!q || !q_assign)) || (nk && (!k || !k_assign)) ||
        nq >= (1u << 27) || nk >= (1u << 27) || (nq && q_stride < dim) || (nk && k_stride < dim))
        return CT_GATE_EARG;
    g->cfg = *cfg;
    g->dim = dim;
    g->nq = nq;
    g->nk = nk;
    g->qa = q_assign;
    g->ka = k_assign;
    g->N = cfg->N ? cfg->N : dim;
    g->ncq = nq < cfg->n_clusters ? nq : cfg->n_clusters;
    g->nck = nk < cfg->n_clusters ? nk : cfg->n_clusters;
    g->gate_macs = 0;
    for (uint32_t a = 0; a < CT_GATE_MAX_CLUSTERS; a++) g->mask[a] = 0;
    if (cfg->mode == CT_GATE_OFF) {
        for (uint32_t t = 0; t < nq; t++) q_assign[t] = 0;
        for (uint32_t t = 0; t < nk; t++) k_assign[t] = 0;
        g->ncq = g->nck = 1;
        g->mask[0] = 1;
        return 0;
    }
    if (g->ncq) g->gate_macs += cluster(q, nq, q_stride, dim, g->ncq, cfg->iters, g->cq, q_assign);
    if (g->nck) g->gate_macs += cluster(k, nk, k_stride, dim, g->nck, cfg->iters, g->ck, k_assign);
    for (uint32_t a = 0; a < g->ncq; a++) {
        for (uint32_t b = 0; b < g->nck; b++) {
            zt_fx s;
            if (cfg->mode == CT_GATE_SURPLUS)
                s = zt_surplus_f(zt_surplus_u(g->cq[a], g->ck[b], dim), g->N);
            else
                s = (zt_fx) (dot14(g->cq[a], g->ck[b], dim) >> 12); /* Q28 -> Q16 cosine */
            g->score[a][b] = s;
            if (s >= cfg->floor) g->mask[a] |= (uint16_t) (1u << b);
        }
    }
    g->gate_macs += (uint64_t) g->ncq * g->nck * dim * 2u;
    return 0;
}

bool ct_gate_keep(const ct_gate_t *g, uint32_t qi, uint32_t q_pos, uint32_t kj, uint32_t k_pos)
{
    if (k_pos > q_pos) return false; /* causal: not an edge */
    if (q_pos - k_pos < g->cfg.window || k_pos == q_pos) return true;
    if (qi >= g->nq || kj >= g->nk) return true; /* outside what was clustered: keep */
    return (g->mask[g->qa[qi]] >> g->ka[kj]) & 1u;
}

uint32_t ct_gate_row(const ct_gate_t *g, uint32_t qi, uint32_t q_pos, uint32_t k_pos0,
                     uint8_t *keep)
{
    uint32_t n = 0;
    for (uint32_t j = 0; j < g->nk; j++) {
        keep[j] = ct_gate_keep(g, qi, q_pos, j, k_pos0 + j) ? 1u : 0u;
        n += keep[j];
    }
    return n;
}

uint32_t ct_gate_keep_list(const ct_gate_t *g, uint32_t q_pos0, uint32_t k_pos0, uint32_t *list,
                           uint32_t cap)
{
    uint32_t n = 0;
    for (uint32_t j = 0; j < g->nk; j++) {
        bool any = false;
        for (uint32_t i = 0; i < g->nq && !any; i++)
            any = ct_gate_keep(g, i, q_pos0 + i, j, k_pos0 + j);
        if (!any) continue;
        if (n < cap) list[n] = j;
        n++;
    }
    return n;
}

void ct_gate_ctx_init(ct_gate_ctx_t *c, const ct_gate_cfg_t *cfg, uint8_t *qa, uint8_t *ka,
                      uint32_t cap)
{
    c->cfg = *cfg;
    c->qa = qa;
    c->ka = ka;
    c->cap = cap;
    c->calls = c->edges = c->kept = c->gate_macs = c->full_macs = 0;
}

int32_t ct_gate_attn(void *ctx, uint32_t layer, uint32_t head, const zt_fx *q, uint32_t n_q,
                     uint32_t q_stride, uint32_t q_pos0, const zt_fx *k, uint32_t n_k,
                     uint32_t k_stride, uint32_t k_pos0, uint32_t head_dim, uint8_t *keep)
{
    ct_gate_ctx_t *c = (ct_gate_ctx_t *) ctx;
    (void) layer;
    (void) head;
    if (!c || !keep || n_q > c->cap || n_k > c->cap) return CT_GATE_ESPACE;
    int32_t r =
        ct_gate_build(&c->g, &c->cfg, q, n_q, q_stride, k, n_k, k_stride, head_dim, c->qa, c->ka);
    if (r) return r;
    uint64_t kept = 0, edges = 0;
    for (uint32_t i = 0; i < n_q; i++) {
        uint32_t qp = q_pos0 + i;
        kept += ct_gate_row(&c->g, i, qp, k_pos0, keep + (uint64_t) i * n_k);
        if (qp >= k_pos0) edges += (qp - k_pos0 + 1u) < n_k ? (qp - k_pos0 + 1u) : n_k;
    }
    c->calls++;
    c->edges += edges;
    c->kept += kept;
    c->gate_macs += c->g.gate_macs;
    c->full_macs += edges * head_dim * 2u; /* Q.K and the weighted V sum */
    return kept > INT32_MAX ? INT32_MAX : (int32_t) kept;
}
