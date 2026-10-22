/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* twelvefold_rules.h — Twelvefold's rules in C, the reference its Sutra,
 * KRON and Eve blocks are checked against (blocks/twelvefold), and the C
 * of the shared block it uses. Pure integer functions, no state: the model
 * (twelvefold_model.c) and the language test (tests/twelvefold_zxapp.inc)
 * both include this, so the languages are tested against the very C the
 * model runs.
 *
 * Value codes are the kernel's (lpres.h lpres_state_t): NEITHER 0, TRUE 1,
 * FALSE 2, BOTH 3. The connectives are never here: they are lpres's own
 * tables, called by the model.
 *
 *   TR1 DESIGNATED: TRUE and BOTH (codes 1 and 3), the values a conclusion
 *       must keep.
 *   TR2 KEPT (lp_entail_kept): an assignment keeps a consequence when the
 *       premises' conjunction is not designated or the conclusion is.
 *       A code outside 0..3 is no value: refused (TWELVEFOLD_REFUSED).
 *   TR3 LIMITS (lp_limits_ok): a formula has 0..TWELVEFOLD_VARS_MAX
 *       distinct variables and is 0..TWELVEFOLD_DEPTH_MAX connectives deep.
 *   TR4 COUNTS (witness.kind_count, witness_rules.h WR2): how many
 *       assignments give one value; a consequence follows when the count
 *       of assignments whose kept answer is FALSE (code 2) is 0. */
#ifndef TWELVEFOLD_RULES_H
#define TWELVEFOLD_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "witness_rules.h"

#define TWELVEFOLD_REFUSED   (-1)
#define TWELVEFOLD_VARS_MAX  6
#define TWELVEFOLD_DEPTH_MAX 16
#define TWELVEFOLD_CODE_N    0 /* NEITHER */
#define TWELVEFOLD_CODE_T    1 /* TRUE */
#define TWELVEFOLD_CODE_F    2 /* FALSE */
#define TWELVEFOLD_CODE_B    3 /* BOTH */

/* TR1 */
static inline bool twelvefold_designated(int64_t code)
{
    return code == TWELVEFOLD_CODE_T || code == TWELVEFOLD_CODE_B;
}

/* TR2: 1 kept, 0 broken, TWELVEFOLD_REFUSED for a code outside 0..3 */
static inline int64_t twelvefold_kept(int64_t premise, int64_t conclusion)
{
    if (premise < 0 || premise > 3 || conclusion < 0 || conclusion > 3) return TWELVEFOLD_REFUSED;
    return !twelvefold_designated(premise) || twelvefold_designated(conclusion);
}

/* TR3 */
static inline bool twelvefold_limits_ok(int64_t vars, int64_t depth)
{
    return vars >= 0 && vars <= TWELVEFOLD_VARS_MAX && depth >= 0 && depth <= TWELVEFOLD_DEPTH_MAX;
}

/* TR4: the count of n kept truths (ZXAPP truth codes: TRUE 1, FALSE 2)
 * that broke the consequence */
static inline int64_t twelvefold_breaks(const int64_t *kept_truth, uint32_t n)
{
    return witness_kind_count(kept_truth, n, TWELVEFOLD_CODE_F);
}

#endif
