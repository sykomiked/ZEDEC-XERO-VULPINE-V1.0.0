/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* ct_gate.h — co-processor tier 1: the geometric surplus gate that runs on
 * the integer core BEFORE an accelerator (tier 2) computes softmax and the
 * value projection. See docs/COPROCESSOR_TIERS.md.
 *
 *   G1  THE TEST.  For a query direction x and a key direction y,
 *       u = 1 - (x.y)^2 (unit vectors) and F(u) = ln(1 + (N-1) u), the ISF
 *       functional of zt.h T9, computed by zt_surplus_u / zt_surplus_f in
 *       Q16. In CT_GATE_SURPLUS mode an edge is kept when F(u) >= floor.
 *       That is the user's "bridge principle" taken literally, and it is a
 *       HEURISTIC: F(u) is large for ORTHOGONAL pairs and zero for both
 *       perfectly aligned and perfectly anti-aligned pairs, so it can drop
 *       the very edges softmax weighs most. What that costs is measured per
 *       model by test_ct_measure.c, never assumed. CT_GATE_ALIGN keeps an
 *       edge when the signed cosine is >= floor (the conventional
 *       similarity-pruning baseline), for comparison.
 *   G2  COARSE.  The test is not run per token pair. Queries and keys are
 *       each grouped into at most CT_GATE_MAX_CLUSTERS clusters by a few
 *       passes of spherical k-means in integers (centroids are unit vectors
 *       in Q14; seeds are evenly spaced tokens, so the result is
 *       deterministic). The test runs once per (query cluster, key
 *       cluster) pair. Cost is about (n_q + n_k) * C * d * (iters + 1) +
 *       C^2 * d multiply-adds against n_q * n_k * d for the scores alone.
 *   G3  NEVER EVERYTHING.  The diagonal (key position == query position)
 *       and a local window (q_pos - k_pos < window) are always kept, so
 *       every query keeps at least itself and softmax never sees an empty
 *       row. Keys after the query (causal mask) are never edges.
 *   G4  OUTPUTS.  mask[a] bit b: query cluster a keeps key cluster b. A
 *       per-head keep list: the keys at least one query keeps (what the
 *       accelerator must load). A per-row keep byte mask for one query.
 *   G5  THE HOOK.  ct_attn_gate_fn is the callback the integer forward pass
 *       (zt_model) or a tier-2 dispatcher can call once per attention block
 *       and query head; ct_gate_attn implements it. It is NOT wired into
 *       zt_model: the tensor owner wires it (docs/COPROCESSOR_TIERS.md says
 *       where). A negative return means "gate unavailable: keep all".
 * Freestanding: no libc, no malloc, no floating point, no 64-bit division.
 */
#ifndef CT_GATE_H
#define CT_GATE_H

#include <stdint.h>
#include <stdbool.h>
#include "zt.h"

#define CT_GATE_MAX_CLUSTERS 16u
#define CT_GATE_MAX_DIM      256u
#define CT_GATE_MAX_ITERS    4u

enum { CT_GATE_SURPLUS = 0, CT_GATE_ALIGN = 1, CT_GATE_OFF = 2 };
enum { CT_GATE_EARG = -1, CT_GATE_ESPACE = -2 };

typedef struct {
    uint32_t mode;       /* CT_GATE_* */
    uint32_t n_clusters; /* 1..CT_GATE_MAX_CLUSTERS, for queries and for keys */
    uint32_t iters;      /* k-means passes after the seed assignment, 0..4 */
    uint32_t N;          /* ISF block count of F(u); 0 means head_dim */
    zt_fx floor;         /* SURPLUS: F(u) in Q16 (0..ln N). ALIGN: cosine in Q16 (-1..1) */
    uint32_t window;     /* keep q_pos - k_pos < window; the diagonal is kept even at 0 */
} ct_gate_cfg_t;

typedef struct {
    ct_gate_cfg_t cfg;
    uint32_t dim, nq, nk, ncq, nck, N;
    zt_fx cq[CT_GATE_MAX_CLUSTERS][CT_GATE_MAX_DIM]; /* unit centroids, Q14 */
    zt_fx ck[CT_GATE_MAX_CLUSTERS][CT_GATE_MAX_DIM];
    zt_fx score[CT_GATE_MAX_CLUSTERS][CT_GATE_MAX_CLUSTERS]; /* F(u) or cosine, Q16 */
    uint16_t mask[CT_GATE_MAX_CLUSTERS];
    uint8_t *qa, *ka;   /* caller's per-token cluster ids (nq and nk entries) */
    uint64_t gate_macs; /* multiply-adds the gate spent */
} ct_gate_t;

/* Cluster the queries (nq rows, q_stride apart) and keys (nk rows), score
 * every cluster pair and set the mask. q_assign / k_assign hold nq / nk
 * bytes. Returns 0, or CT_GATE_EARG (bad config, dim 0 or > MAX_DIM). */
int32_t ct_gate_build(ct_gate_t *g, const ct_gate_cfg_t *cfg, const zt_fx *q, uint32_t nq,
                      uint32_t q_stride, const zt_fx *k, uint32_t nk, uint32_t k_stride,
                      uint32_t dim, uint8_t *q_assign, uint8_t *k_assign);

/* Is the edge query qi (at position q_pos) -> key kj (at k_pos) kept? */
bool ct_gate_keep(const ct_gate_t *g, uint32_t qi, uint32_t q_pos, uint32_t kj, uint32_t k_pos);

/* keep[j] = 1 for each kept key j of query qi (key j sits at k_pos0 + j).
 * Returns the number kept (at least 1 when the diagonal is among the keys). */
uint32_t ct_gate_row(const ct_gate_t *g, uint32_t qi, uint32_t q_pos, uint32_t k_pos0,
                     uint8_t *keep);

/* The per-head keep list: indices of keys that at least one query keeps
 * (query i at q_pos0 + i, key j at k_pos0 + j), ascending. Writes at most
 * cap; returns the full count. */
uint32_t ct_gate_keep_list(const ct_gate_t *g, uint32_t q_pos0, uint32_t k_pos0, uint32_t *list,
                           uint32_t cap);

/* ---- the per-attention-block hook (G5) ---- */

/* Called once per layer, per query head, per block of queries. q: n_q rows
 * of head_dim Q16 values (post-RoPE, post-QK-norm), q_stride apart, at
 * positions q_pos0..; k: n_k rows of the head's KV group, k_stride apart, at
 * positions k_pos0... Writes keep[i * n_k + j] = 1 (kept) or 0 (masked: the
 * caller gives that score -infinity before softmax). Returns the number of
 * kept edges, or < 0 for "keep everything". */
typedef int32_t (*ct_attn_gate_fn)(void *ctx, uint32_t layer, uint32_t head, const zt_fx *q,
                                   uint32_t n_q, uint32_t q_stride, uint32_t q_pos0, const zt_fx *k,
                                   uint32_t n_k, uint32_t k_stride, uint32_t k_pos0,
                                   uint32_t head_dim, uint8_t *keep);

typedef struct {
    ct_gate_t g;
    ct_gate_cfg_t cfg;
    uint8_t *qa, *ka; /* scratch for cluster ids */
    uint32_t cap;     /* entries in qa and in ka */
    /* running totals over every call */
    uint64_t calls, edges, kept, gate_macs, full_macs;
} ct_gate_ctx_t;

void ct_gate_ctx_init(ct_gate_ctx_t *c, const ct_gate_cfg_t *cfg, uint8_t *qa, uint8_t *ka,
                      uint32_t cap);
/* ctx is a ct_gate_ctx_t. */
int32_t ct_gate_attn(void *ctx, uint32_t layer, uint32_t head, const zt_fx *q, uint32_t n_q,
                     uint32_t q_stride, uint32_t q_pos0, const zt_fx *k, uint32_t n_k,
                     uint32_t k_stride, uint32_t k_pos0, uint32_t head_dim, uint8_t *keep);

#endif /* CT_GATE_H */
