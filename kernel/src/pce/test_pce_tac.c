/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_pce_tac.c — the tactics engine (pce_tac.h).
 *
 * Nim with a sound and an unsound evaluator (they disagree at shallow depth,
 * agree once the search sees the end), tic-tac-toe searched to the end (a
 * draw), alpha-beta against a plain minimax written here on 400 random trees
 * for every evaluator and the robust leaf, the budget at its exact edge,
 * callbacks that break the rules, purpose refusals, logging and replay. */
#include <stdio.h>
#include <string.h>
#include "pce_tac.h"

static int *P, *F;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            (*P)++;                                                                                \
        else {                                                                                     \
            (*F)++;                                                                                \
            printf("[FAIL] %s (%s:%d)\n", msg, __FILE__, __LINE__);                                \
        }                                                                                          \
    } while (0)

/* ---- Nim: take 1-3, taking the last wins ---- */
static uint32_t nim_moves(void *ctx, const uint8_t *s, uint32_t *out, uint32_t cap)
{
    (void) ctx;
    uint32_t n = 0;
    for (uint32_t k = 1; k <= 3u && k <= s[0] && n < cap; k++) out[n++] = k;
    return n;
}
static bool nim_apply(void *ctx, const uint8_t *s, uint32_t m, uint8_t *next)
{
    (void) ctx;
    if (m < 1u || m > 3u || m > s[0]) return false;
    next[0] = (uint8_t) (s[0] - m);
    return true;
}
static int64_t nim_score(void *ctx, const uint8_t *s, uint32_t e)
{
    (void) ctx;
    if (s[0] == 0u) return -1000;                 /* the other side took the last */
    if (e == 0u) return s[0] % 4u == 0u ? -1 : 1; /* sound */
    return s[0] % 2u == 0u ? 1 : -1;              /* unsound */
}

/* ---- tic-tac-toe: s[0..8] cells (0 empty, 1 X, 2 O), s[9] side to move ---- */
static const uint8_t LINES[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6},
                                    {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};
static bool ttt_won(const uint8_t *s, uint8_t who)
{
    for (uint32_t i = 0; i < 8u; i++)
        if (s[LINES[i][0]] == who && s[LINES[i][1]] == who && s[LINES[i][2]] == who) return true;
    return false;
}
static uint32_t ttt_moves(void *ctx, const uint8_t *s, uint32_t *out, uint32_t cap)
{
    (void) ctx;
    if (ttt_won(s, 1) || ttt_won(s, 2)) return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < 9u && n < cap; i++)
        if (s[i] == 0u) out[n++] = i;
    return n;
}
static bool ttt_apply(void *ctx, const uint8_t *s, uint32_t m, uint8_t *next)
{
    (void) ctx;
    if (m > 8u || s[m] != 0u) return false;
    for (uint32_t i = 0; i < 10u; i++) next[i] = s[i];
    next[m] = s[9];
    next[9] = (uint8_t) (3u - s[9]);
    return true;
}
static int64_t ttt_score(void *ctx, const uint8_t *s, uint32_t e)
{
    (void) ctx;
    (void) e;
    if (ttt_won(s, (uint8_t) (3u - s[9]))) return -100;
    return 0;
}

/* ---- random trees, and a plain minimax to check alpha-beta against ---- */
static uint64_t mix(uint64_t x)
{
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdull;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ull;
    x ^= x >> 33;
    return x;
}
static uint64_t ld(const uint8_t *s)
{
    uint64_t v = 0;
    for (uint32_t i = 0; i < 8u; i++) v |= (uint64_t) s[i] << (8u * i);
    return v;
}
static void st(uint8_t *s, uint64_t v)
{
    for (uint32_t i = 0; i < 8u; i++) s[i] = (uint8_t) (v >> (8u * i));
}
static uint32_t rt_moves(void *ctx, const uint8_t *s, uint32_t *out, uint32_t cap)
{
    (void) ctx;
    uint32_t n = (uint32_t) (mix(ld(s)) % 5u);
    for (uint32_t i = 0; i < n && i < cap; i++) out[i] = i * 7u + 3u;
    return n;
}
static bool rt_apply(void *ctx, const uint8_t *s, uint32_t m, uint8_t *next)
{
    (void) ctx;
    st(next, mix(ld(s) * 31u + m + 1u));
    return true;
}
static int64_t rt_score(void *ctx, const uint8_t *s, uint32_t e)
{
    (void) ctx;
    return (int64_t) (mix(ld(s) ^ ((uint64_t) (e + 1u) * 0x9E3779B97F4A7C15ull)) % 41u) - 20;
}

static int64_t ref_leaf(const pce_game_t *g, const uint8_t *s, uint32_t ply, uint32_t ev)
{
    if (ev < g->nevals) return g->score(g->ctx, s, ev);
    int64_t pick = g->score(g->ctx, s, 0);
    for (uint32_t e = 1; e < g->nevals; e++) {
        int64_t v = g->score(g->ctx, s, e);
        if ((ply % 2u == 0u) ? v < pick : v > pick) pick = v;
    }
    return pick;
}
static int64_t ref(const pce_game_t *g, const uint8_t *s, uint32_t d, uint32_t ply, uint32_t ev,
                   uint32_t *bm)
{
    uint32_t mv[PCE_TAC_MOVES];
    uint32_t n = g->moves(g->ctx, s, mv, PCE_TAC_MOVES);
    if (n == 0u || d == 0u) return ref_leaf(g, s, ply, ev);
    int64_t best = INT64_MIN;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t nx[PCE_TAC_STATE];
        g->apply(g->ctx, s, mv[i], nx);
        int64_t v = -ref(g, nx, d - 1u, ply + 1u, ev, NULL);
        if (v > best) {
            best = v;
            if (bm) *bm = mv[i];
        }
    }
    return best;
}

/* ---- callbacks that break the rules ---- */
static uint32_t bad_moves(void *ctx, const uint8_t *s, uint32_t *out, uint32_t cap)
{
    (void) ctx;
    (void) s;
    (void) out;
    return cap + 1u;
}
static bool bad_apply(void *ctx, const uint8_t *s, uint32_t m, uint8_t *next)
{
    (void) ctx;
    (void) s;
    (void) m;
    (void) next;
    return false;
}
static int64_t big_score(void *ctx, const uint8_t *s, uint32_t e)
{
    (void) ctx;
    (void) s;
    return e == 1u ? PCE_TAC_SCORE_MAX + 1 : 0;
}
static int64_t low_score(void *ctx, const uint8_t *s, uint32_t e)
{
    (void) ctx;
    (void) s;
    return e == 0u ? -PCE_TAC_SCORE_MAX - 1 : 0;
}
static int64_t edge_score(void *ctx, const uint8_t *s, uint32_t e)
{
    (void) ctx;
    return e == 0u ? (s[0] % 2u ? PCE_TAC_SCORE_MAX : -PCE_TAC_SCORE_MAX) : 0;
}

void test_pce_tac(int *pass_, int *fail_)
{
    P = pass_;
    F = fail_;
    pce_game_t nim = {1, 2, NULL, nim_moves, nim_apply, nim_score};
    pce_tac_opts_t o = {PCE_PURPOSE_GAME, 1, 100000, 0, LPX_PH_MANIFEST};
    pce_tac_result_t r;
    uint8_t five[1] = {5};

    CHECK(pce_tac_search(&nim, five, &o, NULL, &r) == PCE_OK, "nim depth 1");
    CHECK(r.best[0] == 1u && r.best[1] == 2u, "sound takes 1, unsound takes 2");
    CHECK(lpx_base(r.v.tag) == LPX_B_BOTH, "evaluators disagree: BOTH");
    CHECK(r.robust == 1u && r.robust_value == -1 && r.agree == 1u, "robust move offered");
    CHECK(r.value[0] == 1 && r.value[1] == 1, "values for the root side");
    CHECK(lpx_pol(r.v.tag) == LPX_P_NEUTRAL && r.agree_prev == 0u, "depth 1 has no trend");
    CHECK(r.v.act == LPX_ACT_ESCALATE && r.v.kind == PDX_KIND_NEUTRAL,
          "steady disagreement goes to a person");
    CHECK(r.nevals == 2u && r.nodes > 0u, "counts");

    o.depth = 5;
    CHECK(pce_tac_search(&nim, five, &o, NULL, &r) == PCE_OK, "nim to the end");
    CHECK(lpx_base(r.v.tag) == LPX_B_TRUE && r.best[0] == 1u && r.best[1] == 1u,
          "seeing the end, both agree: take 1");
    CHECK(r.value[0] == 1000 && r.robust_value == 1000, "a forced win");
    CHECK(r.agree == 2u, "both agree with the robust move");
    CHECK(r.v.act == LPX_ACT_PROCEED, "proceeds");
    o.advisory = PCE_ADV_DEVICE;
    CHECK(pce_tac_search(&nim, five, &o, NULL, &r) == PCE_OK && r.v.act == LPX_ACT_ESCALATE,
          "a device move needs a person");
    o.advisory = 0;
    /* trend matches lpx_polarity on the agreement counts at every depth */
    for (uint32_t d = 2; d <= 6u; d++) {
        for (uint8_t pile = 1; pile <= 9u; pile++) {
            uint8_t s[1] = {pile};
            o.depth = d;
            CHECK(pce_tac_search(&nim, s, &o, NULL, &r) == PCE_OK, "nim run");
            CHECK(lpx_pol(r.v.tag) ==
                      lpx_polarity(r.agree_prev, 2u - r.agree_prev, r.agree, 2u - r.agree, false),
                  "polarity from agreement trend");
        }
    }
    o.depth = 1;
    uint8_t zero[1] = {0};
    CHECK(pce_tac_search(&nim, zero, &o, NULL, &r) == PCE_OK, "terminal root");
    CHECK(lpx_base(r.v.tag) == LPX_B_NEITHER && r.robust == PCE_TAC_NONE &&
              r.best[0] == PCE_TAC_NONE && r.v.act == LPX_ACT_WAIT,
          "no move at the root: NEITHER, nothing offered");
    o.depth = 2;
    CHECK(pce_tac_search(&nim, zero, &o, NULL, &r) == PCE_OK && lpx_base(r.v.tag) == LPX_B_NEITHER,
          "terminal root at depth 2");
    o.depth = 3;
    uint8_t one[1] = {1};
    CHECK(pce_tac_search(&nim, one, &o, NULL, &r) == PCE_OK && r.robust == 1u &&
              lpx_base(r.v.tag) == LPX_B_TRUE,
          "one stone: take it");

    /* tic-tac-toe */
    pce_game_t ttt = {10, 1, NULL, ttt_moves, ttt_apply, ttt_score};
    uint8_t board[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    pce_tac_opts_t to = {PCE_PURPOSE_GAME, 9, 2000000, 0, LPX_PH_MANIFEST};
    CHECK(pce_tac_search(&ttt, board, &to, NULL, &r) == PCE_OK, "tic-tac-toe searched");
    CHECK(r.value[0] == 0 && lpx_base(r.v.tag) == LPX_B_TRUE, "perfect play is a draw");
    uint8_t win[10] = {1, 1, 0, 2, 2, 0, 0, 0, 0, 1};
    to.depth = 3;
    CHECK(pce_tac_search(&ttt, win, &to, NULL, &r) == PCE_OK && r.robust == 2u &&
              r.robust_value == 100,
          "takes the winning square");
    uint8_t block[10] = {1, 0, 0, 2, 2, 0, 1, 0, 0, 1};
    to.depth = 4;
    CHECK(pce_tac_search(&ttt, block, &to, NULL, &r) == PCE_OK && r.robust == 5u,
          "blocks the other side's line");

    /* alpha-beta = plain minimax, for each evaluator and the robust leaf */
    pce_game_t rt = {8, 3, NULL, rt_moves, rt_apply, rt_score};
    for (uint32_t k = 0; k < 400u; k++) {
        uint8_t s[8];
        st(s, mix(k + 77u));
        pce_tac_opts_t ro = {PCE_PURPOSE_SCHEDULING, 1u + k % 6u, 10000000, 0, LPX_PH_MANIFEST};
        if (pce_tac_search(&rt, s, &ro, NULL, &r) != PCE_OK) {
            CHECK(0, "random tree search");
            continue;
        }
        if (r.robust == PCE_TAC_NONE) {
            CHECK(rt_moves(NULL, s, NULL, 0) == 0u && lpx_base(r.v.tag) == LPX_B_NEITHER,
                  "only a terminal root has no move");
            continue;
        }
        bool same = true;
        for (uint32_t e = 0; e <= 3u; e++) {
            uint32_t bm = PCE_TAC_NONE;
            int64_t v = ref(&rt, s, ro.depth, 0, e, &bm);
            uint32_t got = e < 3u ? r.best[e] : r.robust;
            int64_t gv = e < 3u ? r.value[e] : r.robust_value;
            CHECK(v == gv, "alpha-beta value = minimax value");
            CHECK(bm == got, "alpha-beta move = minimax move (first best)");
            if (e < 3u && r.best[e] != r.best[0]) same = false;
        }
        CHECK(lpx_base(r.v.tag) == (same ? LPX_B_TRUE : LPX_B_BOTH), "base from agreement");
        uint32_t ag = 0;
        for (uint32_t e = 0; e < 3u; e++) ag += r.best[e] == r.robust;
        CHECK(ag == r.agree, "agree count");
    }

    /* the budget at its exact edge */
    o.depth = 4;
    uint8_t seven[1] = {7};
    CHECK(pce_tac_search(&nim, seven, &o, NULL, &r) == PCE_OK, "full budget");
    uint64_t need = r.nodes;
    o.budget = need;
    CHECK(pce_tac_search(&nim, seven, &o, NULL, &r) == PCE_OK &&
              lpx_base(r.v.tag) != LPX_B_NEITHER && r.nodes == need,
          "exactly enough budget");
    o.budget = need - 1u;
    CHECK(pce_tac_search(&nim, seven, &o, NULL, &r) == PCE_OK &&
              lpx_base(r.v.tag) == LPX_B_NEITHER && r.robust == PCE_TAC_NONE &&
              r.v.act == LPX_ACT_WAIT && r.best[0] == PCE_TAC_NONE && r.best[1] == PCE_TAC_NONE,
          "one node short: NEITHER, no move");
    o.budget = 1;
    CHECK(pce_tac_search(&nim, seven, &o, NULL, &r) == PCE_OK &&
              lpx_base(r.v.tag) == LPX_B_NEITHER && r.nodes == 1u,
          "budget 1 stops in the first pass");
    o.budget = 100000;

    /* broken callbacks and bad options */
    pce_tac_result_t keep = r;
    pce_game_t g = nim;
    g.moves = bad_moves;
    CHECK(pce_tac_search(&g, seven, &o, NULL, &r) == PCE_E_ARG, "too many moves");
    g = nim;
    g.apply = bad_apply;
    CHECK(pce_tac_search(&g, seven, &o, NULL, &r) == PCE_E_ARG, "illegal move");
    g = nim;
    g.score = big_score;
    CHECK(pce_tac_search(&g, seven, &o, NULL, &r) == PCE_E_ARG, "score too high");
    g.score = low_score;
    CHECK(pce_tac_search(&g, seven, &o, NULL, &r) == PCE_E_ARG, "score too low");
    g.nevals = 1;
    g.score = big_score;
    CHECK(pce_tac_search(&g, seven, &o, NULL, &r) == PCE_OK, "evaluator 1 unused: fine");
    g = nim;
    g.score = edge_score;
    CHECK(pce_tac_search(&g, seven, &o, NULL, &r) == PCE_OK, "scores at the edge are fine");
    o.depth = 1;
    g.nevals = 1;
    CHECK(pce_tac_search(&g, seven, &o, NULL, &r) == PCE_OK && r.value[0] == PCE_TAC_SCORE_MAX,
          "edge value carried exactly");
    o.depth = 4;
    g = nim;
    g.moves = NULL;
    CHECK(pce_tac_search(&g, seven, &o, NULL, &r) == PCE_E_ARG, "NULL moves");
    g = nim;
    g.apply = NULL;
    CHECK(pce_tac_search(&g, seven, &o, NULL, &r) == PCE_E_ARG, "NULL apply");
    g = nim;
    g.score = NULL;
    CHECK(pce_tac_search(&g, seven, &o, NULL, &r) == PCE_E_ARG, "NULL score");
    g = nim;
    g.state_size = 0;
    CHECK(pce_tac_search(&g, seven, &o, NULL, &r) == PCE_E_ARG, "state size 0");
    g.state_size = PCE_TAC_STATE + 1u;
    CHECK(pce_tac_search(&g, seven, &o, NULL, &r) == PCE_E_ARG, "state too big");
    g.state_size = PCE_TAC_STATE;
    uint8_t wide[PCE_TAC_STATE] = {7};
    CHECK(pce_tac_search(&g, wide, &o, NULL, &r) == PCE_OK, "state at capacity");
    g = nim;
    g.nevals = 0;
    CHECK(pce_tac_search(&g, seven, &o, NULL, &r) == PCE_E_ARG, "no evaluator");
    g.nevals = PCE_TAC_EVALS + 1u;
    CHECK(pce_tac_search(&g, seven, &o, NULL, &r) == PCE_E_ARG, "too many evaluators");
    g.nevals = PCE_TAC_EVALS;
    CHECK(pce_tac_search(&g, seven, &o, NULL, &r) == PCE_OK && r.nevals == PCE_TAC_EVALS,
          "evaluators at capacity");
    pce_tac_opts_t bo = o;
    bo.depth = 0;
    CHECK(pce_tac_search(&nim, seven, &bo, NULL, &r) == PCE_E_ARG, "depth 0");
    bo.depth = PCE_TAC_DEPTH + 1u;
    CHECK(pce_tac_search(&nim, seven, &bo, NULL, &r) == PCE_E_ARG, "depth too deep");
    bo.depth = PCE_TAC_DEPTH;
    CHECK(pce_tac_search(&nim, seven, &bo, NULL, &r) == PCE_OK, "depth at capacity");
    bo = o;
    bo.budget = 0;
    CHECK(pce_tac_search(&nim, seven, &bo, NULL, &r) == PCE_E_ARG, "budget 0");
    bo = o;
    bo.phase = 4;
    CHECK(pce_tac_search(&nim, seven, &bo, NULL, &r) == PCE_E_ARG, "phase out of range");
    bo = o;
    bo.purpose = 0;
    CHECK(pce_tac_search(&nim, seven, &bo, NULL, &r) == PCE_E_PURPOSE, "no purpose");
    bo.purpose = 8;
    CHECK(pce_tac_search(&nim, seven, &bo, NULL, &r) == PCE_E_PURPOSE, "unlisted purpose");
    bo.purpose = PCE_PURPOSE_FINANCE;
    CHECK(pce_tac_search(&nim, seven, &bo, NULL, &r) == PCE_E_PURPOSE,
          "finance is not a tactics purpose");
    for (uint32_t pu = PCE_PURPOSE_GAME; pu <= PCE_PURPOSE_MAPPING; pu++) {
        bo.purpose = pu;
        CHECK(pce_tac_search(&nim, seven, &bo, NULL, &r) == PCE_OK, "listed purpose runs");
    }
    CHECK(pce_tac_search(NULL, seven, &o, NULL, &r) == PCE_E_ARG, "NULL game");
    CHECK(pce_tac_search(&nim, NULL, &o, NULL, &r) == PCE_E_ARG, "NULL root");
    CHECK(pce_tac_search(&nim, seven, NULL, NULL, &r) == PCE_E_ARG, "NULL opts");
    CHECK(pce_tac_search(&nim, seven, &o, NULL, NULL) == PCE_E_ARG, "NULL out");
    keep = r;

    /* logging and replay */
    pdx_rec_t sa[2], sb[2];
    pdx_log_t la, lb;
    pdx_log_init(&la, sa, 2);
    pdx_log_init(&lb, sb, 2);
    pce_tac_result_t r2;
    CHECK(pce_tac_search(&nim, seven, &o, &la, &r) == PCE_OK && la.n == 1u, "logged");
    CHECK(pce_tac_search(&nim, seven, &o, &lb, &r2) == PCE_OK, "replayed");
    CHECK(memcmp(r.in, r2.in, 32) == 0 && memcmp(r.out, r2.out, 32) == 0, "same digests");
    CHECK(memcmp(sa[0].a, r.in, 32) == 0 && memcmp(sa[0].b, r.out, 32) == 0,
          "the record holds the digests");
    CHECK(sa[0].op == PCE_OP_TAC && sa[0].aux == (uint32_t) r.nodes, "op and node count");
    CHECK(pce_tac_search(&nim, five, &o, &lb, &r2) == PCE_OK && memcmp(r.in, r2.in, 32) != 0,
          "another root, another digest");
    CHECK(pdx_log_verify(&lb), "chain verifies");
    keep = r2;
    CHECK(pce_tac_search(&nim, seven, &o, &lb, &r2) == PCE_E_LOG, "full log fails closed");
    CHECK(memcmp(&keep, &r2, sizeof keep) == 0, "full log leaves out");
}
