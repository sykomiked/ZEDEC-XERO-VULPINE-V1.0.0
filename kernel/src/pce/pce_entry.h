/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pce_entry.h — the engines as block entries: integer vectors in, exact
 * numbers or truth values out.
 *
 * The app model (docs/APP_MODEL.md) builds apps from blocks whose entries
 * take named integer vectors and answer exact numbers (num/den, den > 0) or
 * truths (den = 0, num one of NEITHER 0, TRUE 1, FALSE 2, BOTH 3: lpres's
 * own codes and libzxv's ZXV_LP_*). This table gives the engines exactly
 * that shape, so a "c" runtime for the app model binds each entry by name
 * with no glue, and a block manifest's provides lines are pce_entries().
 *
 *   B1 CONTRACTS. Every entry has a contract: its inputs by name, in order,
 *      and whether it answers numbers or a truth. A call whose arguments are
 *      not those inputs, in that order, is PCE_E_ARG before anything runs.
 *   B2 ENTRIES (rationals are passed as numerators over a shared den):
 *        prob.join     num   a b              a, b: counts [t f b n]; the
 *                                             joined mass [T F B N]
 *        prob.verdict  truth m th             m: counts [t f b n]; th:
 *                                             [decide_num decide_den
 *                                             conflict_num conflict_den]
 *        prob.bayes    num   prior like den   prior: weights; like[i]/den
 *        strat.decide  truth payoff nopt lo hi den
 *        strat.leader  num   payoff nopt lo hi den    (-1 when none)
 *        strat.zopa    truth seller_min buyer_max
 *        fin.npv       num   cf rate_num rate_den     one NPV per rate
 *        fin.npv_sign  truth cf rate_num rate_den
 *      Strategy entries run with purpose BUSINESS; finance stays advisory.
 *   B3 TACTICS is not in this table: a game is three callbacks, not a
 *      vector. C and SDK callers use pce_tac.h directly.
 */
#ifndef PCE_ENTRY_H
#define PCE_ENTRY_H

#include "pce.h"

#define PCE_ENTRY_INPUTS 5u

enum { PCE_E_NAME = -7, PCE_E_BUFFER = -8 };
enum { PCE_KIND_NUM = 1, PCE_KIND_TRUTH = 2 }; /* the app model's ZXAPP_KIND_* */

typedef struct {
    const char *name;
    const int64_t *v;
    uint32_t n;
} pce_arg_t;

typedef struct {
    int64_t num, den; /* den = 0: a truth (lpres code in num) */
} pce_num_t;

typedef struct {
    const char *entry;
    uint8_t kind;
    const char *input[PCE_ENTRY_INPUTS];
    uint32_t ninputs;
} pce_contract_t;

/* B1: every entry's contract; *n gets the count. */
const pce_contract_t *pce_entries(uint32_t *n);
const pce_contract_t *pce_entry_find(const char *entry);

/* Run entry. PCE_E_NAME no such entry; PCE_E_ARG a broken contract or bad
 * values; PCE_E_BUFFER more results than cap (*n_out = needed); the
 * engine's own codes otherwise. out is written only on PCE_OK. */
int pce_entry_call(const char *entry, const pce_arg_t *args, uint32_t nargs, pce_num_t *out,
                   uint32_t cap, uint32_t *n_out);

#endif /* PCE_ENTRY_H */
