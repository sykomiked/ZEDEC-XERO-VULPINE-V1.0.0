/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxv_budget_gate.h — the swarm's tokens-per-cycle budget governs the
 * model's generation (hosted app, kernel/arch/hosted).
 *
 *   B1 READ.   Before generating, the gate reads the agent's remaining
 *      allotment for the open cycle (swarm_budget_remaining).
 *   B2 LIMIT.  The generator is called with max_new = min(requested,
 *      remaining). It may stop earlier (end of text); it may not go further.
 *   B3 CHARGE. The tokens actually generated are charged to the agent
 *      (swarm_budget_consume), so the market, the ledger and the frugality
 *      credit see real use.
 *   B4 EXHAUSTED. With nothing left (or no open cycle) the generator is NOT
 *      called and ZXV_GATE_EXHAUSTED is returned; the caller says so in the
 *      reply. The next market cycle refills the allotment (swarm_budget.h R6:
 *      unspent tokens are not carried over).
 *
 * Prompt tokens (the prefill) are not charged: the budget counts tokens the
 * model writes. Single-threaded, like the rest of the host.
 */
#ifndef ZXV_BUDGET_GATE_H
#define ZXV_BUDGET_GATE_H

#include <stdbool.h>
#include <stdint.h>

#include "swarm_budget.h"

#define ZXV_GATE_EXHAUSTED (-1000)

/* Generate at most max_new tokens. Returns the number generated (<= max_new)
 * or a negative error. */
#ifndef ZXV_GEN_FN_DEFINED
#    define ZXV_GEN_FN_DEFINED
typedef int32_t (*zxv_gen_fn)(void *ctx, uint32_t max_new);
#endif

typedef struct {
    uint64_t remaining_before; /* B1 */
    uint32_t requested;
    uint32_t max_new;   /* B2: what the generator was allowed */
    uint32_t generated; /* what it wrote */
    uint64_t charged;   /* B3: what the budget took (== generated) */
    uint64_t remaining_after;
    bool exhausted; /* B4 */
} zxv_gate_t;

/* Run fn under model_id's budget. Returns the tokens generated (>= 0),
 * ZXV_GATE_EXHAUSTED, or fn's negative error (nothing is charged then).
 * out may be NULL. */
int32_t zxv_budget_gate(swarm_budget_t *b, uint32_t model_id, uint32_t requested, zxv_gen_fn fn,
                        void *ctx, zxv_gate_t *out);

#endif /* ZXV_BUDGET_GATE_H */
