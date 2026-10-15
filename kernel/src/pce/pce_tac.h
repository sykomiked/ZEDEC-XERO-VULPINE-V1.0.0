/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pce_tac.h — the tactics engine: the next move, with every evaluator heard.
 *
 * A game-tree search (negamax with alpha-beta) over any two-sided game the
 * caller describes with three callbacks: board and card games, routing or
 * scheduling against bad luck, a business move against a competitor's
 * reply. The other side can be a person, a market or chance played as an
 * adversary; the search is the same.
 *
 *   T1 SEVERAL EVALUATORS. A position can be scored by up to PCE_TAC_EVALS
 *      evaluators (a cautious and an aggressive one, two models, two
 *      teams). The search runs once per evaluator, and once more with the
 *      ROBUST leaf: the worst score any evaluator gives the side that is
 *      choosing at the root. The robust move is the one that does best
 *      against every evaluator's view.
 *   T2 NO SILENT WINNER. Every evaluator picks the same move: TRUE. They
 *      pick different moves: BOTH, with each evaluator's move kept and the
 *      robust move offered as the safe choice; a person or the caller's
 *      rule decides. The node budget ran out, or the root has no move:
 *      NEITHER, and no move is offered (PCE_TAC_NONE).
 *   T3 TREND. With depth D >= 2 the whole run is done at D - 1 first. The
 *      count of evaluators that agree with the robust move, then and now,
 *      gives the polarity (lpx_polarity): agreement growing with depth is
 *      converging (a BOTH waits), shrinking is diverging (a person decides).
 *   T4 DETERMINISTIC. Moves are tried in the order the callback lists them;
 *      ties keep the first. The node count covers the whole run (both
 *      depths, every evaluator) against one budget. The same game, root and
 *      options give the same result and digest on every target.
 *   T5 BOUNDED. Depth <= PCE_TAC_DEPTH, moves per position <= PCE_TAC_MOVES,
 *      state <= PCE_TAC_STATE bytes (the search keeps one state per ply on
 *      the stack), scores within +-PCE_TAC_SCORE_MAX. A callback that
 *      breaks these (too many moves, an illegal move, a score out of range)
 *      ends the run with PCE_E_ARG.
 *   T6 PURPOSE (pce.h E3) is named on every run; work that moves devices or
 *      money is advisory (E2), so its TRUE escalates to a person.
 */
#ifndef PCE_TAC_H
#define PCE_TAC_H

#include "pce.h"

#define PCE_TAC_EVALS     4u
#define PCE_TAC_MOVES     64u
#define PCE_TAC_DEPTH     16u
#define PCE_TAC_STATE     256u
#define PCE_TAC_SCORE_MAX ((int64_t) 1 << 60)
#define PCE_TAC_NONE      0xFFFFFFFFu

typedef struct {
    uint32_t state_size; /* 1..PCE_TAC_STATE */
    uint32_t nevals;     /* 1..PCE_TAC_EVALS */
    void *ctx;
    /* The moves from state, in a fixed order; 0 when the game is over. */
    uint32_t (*moves)(void *ctx, const uint8_t *state, uint32_t *out, uint32_t cap);
    /* The state after move; false when the move is illegal. */
    bool (*apply)(void *ctx, const uint8_t *state, uint32_t move, uint8_t *next);
    /* Evaluator e's score of state for the side to move (higher is better
     * for that side), within +-PCE_TAC_SCORE_MAX. */
    int64_t (*score)(void *ctx, const uint8_t *state, uint32_t e);
} pce_game_t;

typedef struct {
    uint32_t purpose;  /* pce_purpose_t; FINANCE is not a tactics purpose */
    uint32_t depth;    /* plies, 1..PCE_TAC_DEPTH */
    uint64_t budget;   /* nodes for the whole run (> 0) */
    uint32_t advisory; /* PCE_ADV_* */
    uint8_t phase;     /* the work's lifecycle phase for the tag */
} pce_tac_opts_t;

typedef struct {
    uint32_t nevals;
    uint32_t best[PCE_TAC_EVALS]; /* each evaluator's move              */
    int64_t value[PCE_TAC_EVALS]; /* and its value for the root side    */
    uint32_t robust;              /* the robust move (T1), or NONE      */
    int64_t robust_value;
    uint32_t agree;      /* evaluators whose move is the robust one */
    uint32_t agree_prev; /* the same at depth - 1 (T3)              */
    uint64_t nodes;      /* nodes searched over the whole run       */
    pce_verdict_t v;
    uint8_t in[PDX_HASH], out[PDX_HASH]; /* E4 digests */
} pce_tac_result_t;

/* Search from root. PCE_OK with a verdict (TRUE, BOTH or NEITHER, T2);
 * PCE_E_PURPOSE, PCE_E_ARG (bad options or a callback broke T5) or
 * PCE_E_LOG write nothing to out. Logged as PCE_OP_TAC, aux = nodes
 * (saturated at 2^32 - 1). */
int pce_tac_search(const pce_game_t *g, const uint8_t *root, const pce_tac_opts_t *o,
                   pdx_log_t *log, pce_tac_result_t *out);

#endif /* PCE_TAC_H */
