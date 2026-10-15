/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pce_strat.h — the strategy engine: long-horizon choices and negotiation.
 *
 * A plan is a choice among options whose payoff depends on which scenario
 * comes true. A normal planner needs one probability per scenario and one
 * rule to rank by. This one takes what the evidence actually supports, an
 * exact interval per scenario (pce_prob.h P4: [m[T], 1 - m[F]]), and asks
 * several rules, keeping their disagreement.
 *
 *   S1 THE EVIDENCE. Scenario s has likelihood somewhere in [lo_s, hi_s]
 *      (exact, 0 <= lo <= hi <= 1, sum lo <= 1 <= sum hi). Every
 *      distribution inside those bounds is allowed. A scenario with hi = 0
 *      cannot happen and is ignored by every rule.
 *   S2 THREE RULES VOTE, each a witness:
 *        LOWER_EV  the best expected payoff in the worst distribution the
 *                  evidence allows (exact; the remaining mass goes to the
 *                  option's worst scenarios first);
 *        MAXIMIN   the best worst case over the possible scenarios;
 *        REGRET    the least maximum regret (shortfall against the best
 *                  option in each possible scenario).
 *      Ties keep the lowest option index.
 *   S3 DOMINANCE does not vote; it reports which options are still in
 *      contention: an option is out when another's lower expectation is
 *      above its upper one. The lower-EV pick is always in contention.
 *   S4 VERDICT. No option allowed: NEITHER. The three rules pick the same
 *      option: TRUE. Otherwise BOTH: every rule's pick and the bounds are
 *      kept, so a person sees the trade-off rather than one rule's answer.
 *      With the previous result for the same question, the leader's vote
 *      count then and now gives the polarity (lpx_polarity): votes
 *      gathering is converging, scattering is diverging.
 *   S5 LIMITS. An option flagged PCE_OPT_PROHIBITED (the caller's policy
 *      forbids it: weapons targeting, anything unlawful) is refused: it is
 *      never ranked, never a regret baseline, and reported in `refused`.
 *      A leader flagged life-safety, device or money makes the verdict
 *      advisory (pce.h E2). The purpose is named (E3); finance is not a
 *      strategy purpose (pce_fin.h is analytics only).
 *   S6 NEGOTIATION RANGE (pce_strat_zopa). Several estimates of each
 *      side's walk-away point. A deal is possible under an estimate when
 *      seller_min <= buyer_max. Possible under every estimate and one price
 *      range satisfies them all: TRUE, with that range. Possible under
 *      none: FALSE. Anything between, including "possible under each view,
 *      but no single price satisfies every view": BOTH. No estimate:
 *      NEITHER.
 */
#ifndef PCE_STRAT_H
#define PCE_STRAT_H

#include "pce.h"

#define PCE_STRAT_OPTS       16u
#define PCE_STRAT_SCEN       16u
#define PCE_STRAT_EST        16u
#define PCE_STRAT_PAYOFF_MAX ((int64_t) 1 << 61)
#define PCE_STRAT_NONE       0xFFFFFFFFu

/* option flags: PCE_ADV_* bits, and */
#define PCE_OPT_PROHIBITED 0x80u

enum { PCE_RULE_LOWER_EV = 0, PCE_RULE_MAXIMIN = 1, PCE_RULE_REGRET = 2, PCE_RULES = 3 };

typedef struct {
    uint32_t nopt, nscen;  /* 1..PCE_STRAT_OPTS, 1..PCE_STRAT_SCEN       */
    const int64_t *payoff; /* nopt x nscen, row per option, |v| <= MAX  */
    const rat_t *lo, *hi;  /* S1, nscen each                            */
    const uint32_t *flags; /* per option (PCE_ADV_*, PCE_OPT_*); NULL ok */
    uint32_t purpose;      /* pce_purpose_t, not FINANCE                 */
    uint8_t phase;
} pce_strat_in_t;

typedef struct {
    uint32_t pick[PCE_RULES]; /* S2: each rule's option, or NONE */
    rat_t lower[PCE_STRAT_OPTS], upper[PCE_STRAT_OPTS];
    int64_t worst[PCE_STRAT_OPTS], regret[PCE_STRAT_OPTS];
    uint32_t contention; /* S3: bit o set when option o is in contention */
    uint32_t refused;    /* S5: bit o set when option o is prohibited    */
    uint32_t leader;     /* the option most rules pick (ties: lowest), or NONE */
    uint32_t votes;      /* how many rules pick the leader               */
    pce_verdict_t v;
    uint8_t in[PDX_HASH], out[PDX_HASH];
} pce_strat_result_t;

/* S1-S5. prev (may be NULL) is the last result for the same question.
 * PCE_E_ARG, PCE_E_PURPOSE, PCE_E_OVERFLOW and PCE_E_LOG write nothing. */
int pce_strat_decide(const pce_strat_in_t *in, const pce_strat_result_t *prev, pdx_log_t *log,
                     pce_strat_result_t *out);

/* S1: the bounds on option o's expected payoff (exported for the finance
 * engine and the tests). */
int pce_strat_bounds(const int64_t *payoff, const rat_t *lo, const rat_t *hi, uint32_t nscen,
                     rat_t *lower, rat_t *upper);

typedef struct {
    int64_t seller_min, buyer_max;
} pce_est_t;

typedef struct {
    uint32_t yes, no; /* estimates with and without a deal range    */
    int64_t range_lo; /* the range every estimate allows (TRUE only) */
    int64_t range_hi;
    pce_verdict_t v;
    uint8_t in[PDX_HASH], out[PDX_HASH];
} pce_zopa_t;

/* S6. purpose NEGOTIATION or BUSINESS; advisory as pce.h E2. */
int pce_strat_zopa(const pce_est_t *est, uint32_t n, uint32_t purpose, uint32_t advisory,
                   uint8_t phase, pdx_log_t *log, pce_zopa_t *out);

#endif /* PCE_STRAT_H */
