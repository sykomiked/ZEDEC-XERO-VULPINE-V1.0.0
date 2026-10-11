/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* paradox_graph.c — complex paradoxes (P0.4): a dependency graph of
 * paradoxes resolved in a fixed order. See paradox.h.
 *
 * Every node is addressed by its canonical position: its rank when the
 * nodes are sorted by id. Dependencies are bit masks over those positions,
 * so nothing a pass computes depends on the order the inputs arrived in. */
#include "paradox.h"
#include "../robin_debanks/sha256.h"

static int cmp32(const uint8_t *a, const uint8_t *b)
{
    for (uint32_t i = 0; i < PDX_HASH; i++)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

static const uint8_t *node_id(const pdx_graph_t *g, uint32_t p)
{
    return g->node[g->ord[p]].id;
}

static int32_t find_pos(const pdx_graph_t *g, const uint8_t id[PDX_HASH])
{
    uint32_t lo = 0, hi = g->n;
    while (lo < hi) {
        uint32_t mid = lo + ((hi - lo) >> 1);
        int c = cmp32(node_id(g, mid), id);
        if (c == 0) return (int32_t) mid;
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return -1;
}

static bool final_state(uint8_t s)
{
    return s == PDX_NS_CLEAR || s == PDX_NS_CONTAINED || s == PDX_NS_ROLLED_BACK ||
           s == PDX_NS_TAINTED || s == PDX_NS_APPLIED;
}

/* contained work never comes back within a graph */
static bool sticky(uint8_t s)
{
    return s == PDX_NS_CONTAINED || s == PDX_NS_ROLLED_BACK || s == PDX_NS_TAINTED;
}

bool pdx_graph_load(pdx_graph_t *g, const pdx_node_t *nodes, uint32_t n, uint32_t step_limit)
{
    if (!g || n > PDX_GRAPH_MAX || (n && !nodes)) return false;
    g->node = nodes;
    g->n = n;
    /* insertion sort of indices by id */
    for (uint32_t i = 0; i < n; i++) {
        uint32_t j = i;
        while (j > 0 && cmp32(nodes[g->ord[j - 1]].id, nodes[i].id) > 0) {
            g->ord[j] = g->ord[j - 1];
            j--;
        }
        g->ord[j] = (uint8_t) i;
    }
    for (uint32_t p = 0; p < n; p++) {
        const pdx_node_t *x = &nodes[g->ord[p]];
        if ((p > 0 && cmp32(node_id(g, p - 1), x->id) == 0) || x->ndeps > PDX_MAX_DEPS ||
            x->kind > PDX_KIND_NONE)
            return false;
        g->dep[p] = 0;
        for (uint32_t d = 0; d < x->ndeps; d++) {
            int32_t q = find_pos(g, x->deps[d]);
            if (q < 0) return false;
            g->dep[p] |= 1ull << q;
        }
        g->kind[p] = x->kind;
        g->forb[p] = x->forbidden;
        g->t[p] = x->t;
        g->st[p] = PDX_NS_OPEN;
    }
    g->pass = 0;
    g->step_limit = step_limit;
    g->total.mag = 0;
    g->total.dir = 0;
    for (uint32_t i = 0; i < PDX_HASH; i++) g->chain[i] = 0;
    return true;
}

/* worst-wins rank: destructive > special > neutral > constructive > none */
static uint8_t kind_rank(uint8_t k)
{
    static const uint8_t r[5] = {1u, 4u, 2u, 3u, 0u};
    return r[k];
}

bool pdx_graph_update(pdx_graph_t *g, const pdx_update_t *u, uint32_t n)
{
    if (!g || (n && !u)) return false;
    bool ok = true;
    uint64_t touched = 0;
    uint8_t mk[PDX_GRAPH_MAX], mf[PDX_GRAPH_MAX];
    pdx_tension_t mt[PDX_GRAPH_MAX];
    for (uint32_t i = 0; i < n; i++) {
        int32_t p = find_pos(g, u[i].id);
        if (p < 0 || u[i].kind > PDX_KIND_NONE) {
            ok = false;
            continue;
        }
        uint64_t bit = 1ull << p;
        if (!(touched & bit)) {
            touched |= bit;
            mk[p] = u[i].kind;
            mf[p] = u[i].forbidden;
            mt[p] = u[i].t;
            continue;
        }
        if (kind_rank(u[i].kind) > kind_rank(mk[p])) mk[p] = u[i].kind;
        mf[p] |= u[i].forbidden;
        if (u[i].t.mag > mt[p].mag || (u[i].t.mag == mt[p].mag && u[i].t.dir > mt[p].dir))
            mt[p] = u[i].t;
    }
    for (uint32_t p = 0; p < g->n; p++) {
        if (!(touched >> p & 1u)) continue;
        g->t[p] = mt[p];
        g->forb[p] |= mf[p];
        if (!sticky(g->st[p])) g->kind[p] = mk[p];
    }
    return ok;
}

/* every node that depends, directly or through others, on one in `seed`
 * (seed included) */
static uint64_t closure(const pdx_graph_t *g, uint64_t seed)
{
    uint64_t r = seed;
    bool grew = true;
    while (grew) {
        grew = false;
        for (uint32_t p = 0; p < g->n; p++)
            if (!(r >> p & 1u) && (g->dep[p] & r)) {
                r |= 1ull << p;
                grew = true;
            }
    }
    return r;
}

static void put_u32(sha256_ctx_t *c, uint32_t v)
{
    uint8_t b[4];
    for (uint32_t i = 0; i < 4; i++) b[i] = (uint8_t) (v >> (8 * i));
    sha256_update(c, b, 4);
}

pdx_out_t pdx_graph_pass(pdx_graph_t *g, pdx_log_t *log)
{
    if (!g || !log) return PDX_OUT_REFUSED;
    if (log->n >= log->cap) return PDX_OUT_LOG_FULL;
    uint32_t n = g->n;
    /* 1. destructive and forbidden: contain, then taint every dependant */
    uint64_t bad = 0;
    for (uint32_t p = 0; p < n; p++) {
        if (!sticky(g->st[p])) {
            if (g->forb[p])
                g->st[p] = PDX_NS_ROLLED_BACK;
            else if (g->kind[p] == PDX_KIND_DESTRUCTIVE)
                g->st[p] = PDX_NS_CONTAINED;
        }
        if (sticky(g->st[p])) bad |= 1ull << p;
    }
    uint64_t tainted = closure(g, bad);
    /* 2. special: sandbox; dependants wait */
    uint64_t sand = 0;
    for (uint32_t p = 0; p < n; p++) {
        uint64_t bit = 1ull << p;
        if (tainted & bit) {
            if (!(bad & bit)) g->st[p] = PDX_NS_TAINTED;
        } else if (g->st[p] != PDX_NS_APPLIED && g->kind[p] == PDX_KIND_SPECIAL) {
            g->st[p] = PDX_NS_SANDBOXED;
            sand |= bit;
        }
    }
    uint64_t waiting = closure(g, sand) & ~tainted;
    /* 3. neutral: hold */
    uint64_t held = 0;
    for (uint32_t p = 0; p < n; p++) {
        uint64_t bit = 1ull << p;
        if ((tainted | sand) & bit || g->st[p] == PDX_NS_APPLIED) continue;
        if (waiting & bit)
            g->st[p] = PDX_NS_WAITING;
        else if (g->kind[p] == PDX_KIND_NEUTRAL) {
            g->st[p] = PDX_NS_HELD;
            held |= bit;
        } else if (g->kind[p] == PDX_KIND_NONE)
            g->st[p] = PDX_NS_CLEAR;
    }
    /* 4. constructive: only where nothing contained, sandboxed, waiting or
     * held reaches */
    uint64_t blocked = closure(g, tainted | waiting | held);
    for (uint32_t p = 0; p < n; p++)
        if (g->kind[p] == PDX_KIND_CONSTRUCTIVE && !((tainted | waiting) >> p & 1u) &&
            g->st[p] != PDX_NS_APPLIED)
            g->st[p] = (blocked >> p & 1u) ? PDX_NS_BLOCKED : PDX_NS_APPLIED;
    /* 5. tally, digest, chain */
    uint32_t open = 0;
    int64_t net = 0;
    for (uint32_t p = 0; p < n; p++) {
        if (final_state(g->st[p])) continue;
        open++;
        net += g->t[p].dir > 0   ? (int64_t) g->t[p].mag
               : g->t[p].dir < 0 ? -(int64_t) g->t[p].mag
                                 : 0;
    }
    uint64_t mag = net < 0 ? (uint64_t) -net : (uint64_t) net;
    g->total.mag = mag > UINT32_MAX ? UINT32_MAX : (uint32_t) mag;
    g->total.dir = (int8_t) (net > 0 ? 1 : net < 0 ? -1 : 0);
    g->pass++;
    sha256_ctx_t c;
    uint8_t digest[PDX_HASH];
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) "zxv/pdx-pass/v1", 15);
    put_u32(&c, g->pass);
    put_u32(&c, n);
    for (uint32_t p = 0; p < n; p++) {
        sha256_update(&c, node_id(g, p), PDX_HASH);
        uint8_t f[4] = {g->kind[p], g->forb[p], g->st[p], (uint8_t) g->t[p].dir};
        sha256_update(&c, f, 4);
        put_u32(&c, g->t[p].mag);
    }
    put_u32(&c, g->total.mag);
    uint8_t d = (uint8_t) g->total.dir;
    sha256_update(&c, &d, 1);
    sha256_final(&c, digest);
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) "zxv/pdx-chain/v1", 16);
    sha256_update(&c, g->chain, PDX_HASH);
    sha256_update(&c, digest, PDX_HASH);
    sha256_final(&c, g->chain);
    pdx_out_t o = open == 0                  ? PDX_OUT_RESOLVED
                  : g->pass >= g->step_limit ? PDX_OUT_ESCALATE_TO_OWNER
                                             : PDX_OUT_PENDING;
    pdx_log_append(log, PDX_OP_GRAPH, PDX_KIND_NONE, 0, (uint8_t) o, g->pass, digest, g->chain);
    return o;
}

int32_t pdx_graph_state(const pdx_graph_t *g, const uint8_t id[PDX_HASH])
{
    if (!g || !id) return -1;
    int32_t p = find_pos(g, id);
    return p < 0 ? -1 : (int32_t) g->st[p];
}
