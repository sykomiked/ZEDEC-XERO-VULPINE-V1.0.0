/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* swarm_ledger.h — the witness and the triple ledger of the swarm economy.
 *
 * THE WITNESS
 * -----------
 *   W1  Every allotment is witnessed by a DIFFERENT model in the swarm. The
 *       witness of active model j (of n, in slot order) in cycle c is active
 *       model (j + 1 + c mod (n-1)) mod n: never itself, and the pairing
 *       rotates every cycle. A one-model swarm is witnessed by the kernel
 *       (witness id 0).
 *   W2  Allocation is exact integer arithmetic, so the witness does not run
 *       a model: it recomputes the cycle from the same inputs (microseconds,
 *       zero tokens) and compares.
 *   W3  The verdict is a trit: TRUE (agrees), FALSE (the cycle does not add
 *       up to T), GLUT (adds up, but this allotment differs: contested).
 *   W4  Each witnessing earns the witness 1 Social capital, which raises its
 *       cooperation dividend (swarm_market.h M6): checking each other pays.
 *
 * THE TRIPLE LEDGER (finance/triple_ledger.h)
 * -------------------------------------------
 *   Financial   (rational axis):  each model's real allotment and payment.
 *   Provenance  (logical axis):   each witness record and its trit.
 *   Externality (imaginary axis): each model's imaginary allotment.
 *   L1  A cycle SETTLES only if every allotment is witnessed TRUE, the
 *       allotments sum to T, the imaginary parts sum to the imaginary pool,
 *       and money is conserved.
 *
 * Freestanding: no libc, no allocation, no floating point.
 */
#ifndef SWARM_LEDGER_H
#define SWARM_LEDGER_H

#include <stdint.h>
#include <stdbool.h>
#include "swarm_budget.h"
#include "swarm_emotion.h"
#include "swarm_market.h"

typedef enum {
    SWARM_WIT_FALSE = 0,
    SWARM_WIT_TRUE  = 1,
    SWARM_WIT_GLUT  = 2
} swarm_verdict_t;

typedef enum {
    SWARM_LEDGER_FINANCIAL   = 0,
    SWARM_LEDGER_PROVENANCE  = 1,
    SWARM_LEDGER_EXTERNALITY = 2
} swarm_ledger_kind_t;

typedef struct {
    uint64_t            cycle;
    swarm_ledger_kind_t ledger;
    uint32_t            model_id;
    uint32_t            counterparty;   /* witness id on the provenance ledger */
    uint64_t            amount;         /* tokens (financial: real part; externality: imaginary) */
    uint64_t            paid;           /* financial ledger: Financial capital paid */
    swarm_verdict_t     verdict;        /* provenance ledger only */
} swarm_ledger_entry_t;

#define SWARM_LEDGER_CAP 512u

typedef struct {
    swarm_ledger_entry_t e[SWARM_LEDGER_CAP];   /* ring buffer */
    uint32_t             head;                  /* next write */
    uint64_t             posted;                /* entries ever posted */
    uint64_t             settled_cycles;
    uint64_t             held_cycles;           /* cycles that did not settle */
} swarm_ledger_t;

typedef struct {
    uint32_t        model_id;
    uint32_t        witness_id;
    swarm_verdict_t verdict;
} swarm_witness_t;

/* W1: witness model id for slot `slot` in the open cycle of `b`. */
uint32_t swarm_witness_for(const swarm_budget_t *b, uint32_t slot);

/* W2 + W3: recompute the cycle from the pre-cycle inputs (copies taken just
 * before swarm_market_begin_cycle; pre_e may be NULL) and witness every
 * active slot of `posted`. Writes one record per active slot into out[],
 * returns how many. */
uint32_t swarm_witness_cycle(const swarm_budget_t *posted,
                             const swarm_budget_t *pre_b,
                             const swarm_market_t *pre_m,
                             const swarm_emotion_state_t *pre_e,
                             swarm_witness_t *out);

/* W4: pay each witness 1 Social capital per record. */
void swarm_witness_reward(swarm_market_t *m, const swarm_witness_t *w, uint32_t n);

void swarm_ledger_init(swarm_ledger_t *l);

/* Post the open cycle to all three ledgers and decide L1. Returns true if
 * the cycle settles. imag_pool is the cycle's imaginary pool (0 if none). */
bool swarm_ledger_post_cycle(swarm_ledger_t *l, const swarm_budget_t *b,
                             const swarm_market_t *m, uint64_t imag_pool,
                             const swarm_witness_t *w, uint32_t nw);

#endif /* SWARM_LEDGER_H */
