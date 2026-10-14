/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pce_tac.c — the tactics engine (pce_tac.h T1-T6). */
#include "pce_tac.h"

#define INF (PCE_TAC_SCORE_MAX + 1)

typedef struct {
    const pce_game_t *g;
    uint32_t eval; /* an evaluator, or g->nevals for the robust leaf */
    uint64_t nodes, budget;
    bool out_of_budget, broken;
} run_t;

static int64_t leaf(run_t *r, const uint8_t *s, uint32_t ply)
{
    const pce_game_t *g = r->g;
    if (r->eval < g->nevals) {
        int64_t v = g->score(g->ctx, s, r->eval);
        if (v > PCE_TAC_SCORE_MAX || v < -PCE_TAC_SCORE_MAX) r->broken = true;
        return v;
    }
    /* T1 robust: the worst any evaluator gives the root side. The side to
     * move at an odd ply is the other side, so its best is the root's worst. */
    bool root_side = (ply & 1u) == 0u;
    int64_t pick = 0;
    for (uint32_t e = 0; e < g->nevals; e++) {
        int64_t v = g->score(g->ctx, s, e);
        if (v > PCE_TAC_SCORE_MAX || v < -PCE_TAC_SCORE_MAX) {
            r->broken = true;
            return 0;
        }
        if (e == 0u || (root_side ? v < pick : v > pick)) pick = v;
    }
    return pick;
}

static int64_t negamax(run_t *r, const uint8_t *s, uint32_t depth, uint32_t ply, int64_t alpha,
                       int64_t beta, uint32_t *best_move)
{
    if (r->out_of_budget || r->broken) return 0;
    if (r->nodes >= r->budget) {
        r->out_of_budget = true;
        return 0;
    }
    r->nodes++;
    const pce_game_t *g = r->g;
    uint32_t mv[PCE_TAC_MOVES];
    uint32_t n = g->moves(g->ctx, s, mv, PCE_TAC_MOVES);
    if (n > PCE_TAC_MOVES) {
        r->broken = true;
        return 0;
    }
    if (n == 0u || depth == 0u) return leaf(r, s, ply);
    uint8_t next[PCE_TAC_STATE];
    int64_t best = -INF;
    for (uint32_t i = 0; i < n; i++) {
        if (!g->apply(g->ctx, s, mv[i], next)) {
            r->broken = true;
            return 0;
        }
        int64_t v = -negamax(r, next, depth - 1u, ply + 1u, -beta, -alpha, NULL);
        if (r->out_of_budget || r->broken) return 0;
        if (v > best) {
            best = v;
            if (best_move) *best_move = mv[i];
        }
        if (best > alpha) alpha = best;
        if (alpha >= beta) break;
    }
    return best;
}

typedef struct {
    uint32_t best[PCE_TAC_EVALS + 1u];
    int64_t value[PCE_TAC_EVALS + 1u];
    bool out_of_budget, broken, terminal;
} pass_t;

/* Every evaluator and the robust leaf at one depth. */
static void one_pass(const pce_game_t *g, const uint8_t *root, uint32_t depth, uint64_t budget,
                     uint64_t *nodes, pass_t *p)
{
    p->out_of_budget = p->broken = p->terminal = false;
    for (uint32_t e = 0; e <= g->nevals; e++) {
        run_t r = {g, e, *nodes, budget, false, false};
        p->best[e] = PCE_TAC_NONE;
        p->value[e] = negamax(&r, root, depth, 0u, -INF, INF, &p->best[e]);
        *nodes = r.nodes;
        if (r.broken) p->broken = true;
        if (r.out_of_budget) p->out_of_budget = true;
        if (p->broken || p->out_of_budget) return;
        if (p->best[e] == PCE_TAC_NONE) p->terminal = true; /* no move at the root */
    }
}

static uint32_t agreeing(const pce_game_t *g, const pass_t *p)
{
    uint32_t a = 0;
    for (uint32_t e = 0; e < g->nevals; e++)
        if (p->best[e] == p->best[g->nevals]) a++;
    return a;
}

int pce_tac_search(const pce_game_t *g, const uint8_t *root, const pce_tac_opts_t *o,
                   pdx_log_t *log, pce_tac_result_t *out)
{
    if (g == NULL || root == NULL || o == NULL || out == NULL) return PCE_E_ARG;
    if (!pce_purpose_ok(o->purpose) || o->purpose == PCE_PURPOSE_FINANCE) return PCE_E_PURPOSE;
    if (g->moves == NULL || g->apply == NULL || g->score == NULL) return PCE_E_ARG;
    if (g->state_size == 0u || g->state_size > PCE_TAC_STATE) return PCE_E_ARG;
    if (g->nevals == 0u || g->nevals > PCE_TAC_EVALS) return PCE_E_ARG;
    if (o->depth == 0u || o->depth > PCE_TAC_DEPTH || o->budget == 0u || o->phase > 3u)
        return PCE_E_ARG;

    uint64_t nodes = 0;
    pass_t prev, cur;
    prev.terminal = cur.terminal = false;
    bool have_prev = o->depth >= 2u;
    if (have_prev) one_pass(g, root, o->depth - 1u, o->budget, &nodes, &prev);
    if (have_prev && prev.broken) return PCE_E_ARG;
    bool short_budget = have_prev && prev.out_of_budget;
    if (!short_budget) {
        one_pass(g, root, o->depth, o->budget, &nodes, &cur);
        if (cur.broken) return PCE_E_ARG;
        short_budget = cur.out_of_budget;
    }

    pce_tac_result_t r;
    for (uint32_t e = 0; e < PCE_TAC_EVALS; e++) {
        r.best[e] = PCE_TAC_NONE;
        r.value[e] = 0;
    }
    r.nevals = g->nevals;
    r.robust = PCE_TAC_NONE;
    r.robust_value = 0;
    r.agree = r.agree_prev = 0;
    r.nodes = nodes;

    uint8_t base, pol = LPX_P_NEUTRAL;
    if (short_budget || cur.terminal) {
        base = LPX_B_NEITHER; /* T2: unknown, no move offered */
    } else {
        for (uint32_t e = 0; e < g->nevals; e++) {
            r.best[e] = cur.best[e];
            r.value[e] = cur.value[e];
        }
        r.robust = cur.best[g->nevals];
        r.robust_value = cur.value[g->nevals];
        r.agree = agreeing(g, &cur);
        bool same = true;
        for (uint32_t e = 1; e < g->nevals; e++)
            if (cur.best[e] != cur.best[0]) same = false;
        base = same ? LPX_B_TRUE : LPX_B_BOTH;
        if (have_prev && !prev.terminal) {
            r.agree_prev = agreeing(g, &prev);
            pol = lpx_polarity(r.agree_prev, g->nevals - r.agree_prev, r.agree, g->nevals - r.agree,
                               false);
        }
    }
    r.v = pce_verdict(base, pol, o->phase, o->advisory);

    pce_hash_t h;
    pce_hash_begin(&h, PCE_OP_TAC);
    pce_hash_u64(&h, o->purpose);
    pce_hash_u64(&h, o->depth);
    pce_hash_u64(&h, o->budget);
    pce_hash_u64(&h, o->advisory);
    pce_hash_u64(&h, ((uint64_t) g->nevals << 32) | g->state_size);
    pce_hash_u64(&h, o->phase);
    sha256_update(&h.c, root, g->state_size);
    pce_hash_end(&h, r.in);
    pce_hash_begin(&h, PCE_OP_TAC);
    for (uint32_t e = 0; e < g->nevals; e++) {
        pce_hash_u64(&h, r.best[e]);
        pce_hash_i64(&h, r.value[e]);
    }
    pce_hash_u64(&h, r.robust);
    pce_hash_i64(&h, r.robust_value);
    pce_hash_u64(&h, ((uint64_t) r.agree << 32) | r.agree_prev);
    pce_hash_u64(&h, r.nodes);
    pce_hash_u64(&h, ((uint64_t) r.v.tag << 16) | ((uint64_t) r.v.act << 8) | r.v.kind);
    pce_hash_end(&h, r.out);

    uint32_t aux = r.nodes > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t) r.nodes;
    int rc = pce_log(log, PCE_OP_TAC, &r.v, aux, r.in, r.out);
    if (rc != PCE_OK) return rc;
    pce_copy(out, &r, sizeof r);
    return PCE_OK;
}
