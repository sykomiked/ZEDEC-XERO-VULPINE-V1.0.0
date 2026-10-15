/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pce_prob.h — the probability engine: exact, four-valued, conflict kept.
 *
 * A one-number probability cannot tell "half, because the sources conflict"
 * from "half, because nobody knows". This engine keeps the two apart.
 *
 *   P1 A MASS is a belief about one claim spread over lpres's four values,
 *      in exact rationals: m[TRUE] (evidence it holds), m[FALSE] (evidence
 *      it does not), m[BOTH] (evidence for both at once: measured
 *      conflict), m[NEITHER] (no evidence: measured ignorance). Every entry
 *      is >= 0 and they sum to exactly 1. Indexes are lpres_state_t.
 *   P2 JOIN (pce_mass_join) combines two independent sources on the
 *      knowledge order: the value of a pair is the bitwise OR of their
 *      lpres codes (NEITHER 0, TRUE 1, FALSE 2, BOTH 3), so TRUE from one
 *      and FALSE from the other lands on BOTH. Conflict is never divided
 *      away. MEET (pce_mass_meet) is the AND: what both sources back.
 *      Dempster's rule (pce_mass_dempster) is here for comparison only: it
 *      divides the conflict away and can report a coin flip, or near
 *      certainty, where the sources flatly contradict each other.
 *   P3 TRUTH OPS on independent claims (pce_mass_and / _or / _not) use
 *      lpres_conjoin / lpres_disjoin / lpres_negate, one table for all.
 *   P4 READINGS. support = m[T] + m[B], against = m[F] + m[B], conflict =
 *      m[B], ignorance = m[N]; bounds = [m[T], 1 - m[F]], the interval the
 *      strategy engine takes as a scenario's likelihood.
 *   P5 VERDICT. Conflict first: m[B] >= the conflict threshold is BOTH,
 *      whatever else the mass says. Else m[T] >= the decision threshold is
 *      TRUE, m[F] >= it FALSE, and anything else NEITHER. Polarity is the
 *      trend of the margin |m[T] - m[F]| against the previous mass.
 *   P6 EXACT CLASSICS. Bayes over n hypotheses (evidence every hypothesis
 *      calls impossible is PCE_E_CONFLICT: the data contradicts the model,
 *      reported as BOTH, never a division by zero); Beta-binomial rates;
 *      expected values; total variation between two distributions, used as
 *      a witness (within tolerance TRUE, else BOTH, both kept).
 *
 * Every function returns PCE_OK or a PCE_E_* code and writes its output only
 * on PCE_OK. Overflow of the exact arithmetic is PCE_E_OVERFLOW (E1).
 */
#ifndef PCE_PROB_H
#define PCE_PROB_H

#include "pce.h"

#define PCE_N_MAX 64u /* hypotheses, outcomes, distribution entries */

typedef struct {
    rat_t m[4]; /* P1, indexed by lpres_state_t */
} pce_mass_t;

typedef struct {
    rat_t decide;   /* P5: mass a TRUE or FALSE needs (e.g. 9/10)     */
    rat_t conflict; /* P5: mass on BOTH that makes it BOTH (e.g. 1/10) */
} pce_thresh_t;

typedef struct {
    rat_t support, against, conflict, ignorance, lo, hi; /* P4 */
} pce_read_t;

/* P1: from observation counts (t for, f against, b both, n neither). All
 * zero is pure ignorance (m[N] = 1). A count above INT64_MAX, or a total
 * that overflows, is PCE_E_ARG. */
int pce_mass_make(pce_mass_t *out, uint64_t t, uint64_t f, uint64_t b, uint64_t n);
/* P1: from four exact values that must already be a mass. */
int pce_mass_set(pce_mass_t *out, rat_t t, rat_t f, rat_t b, rat_t n);
bool pce_mass_valid(const pce_mass_t *m);

/* P2 */
int pce_mass_join(const pce_mass_t *a, const pce_mass_t *b, pce_mass_t *out);
int pce_mass_meet(const pce_mass_t *a, const pce_mass_t *b, pce_mass_t *out);
/* P2 for comparison: a source with mass on BOTH has no Dempster reading
 * (PCE_E_ARG); total conflict is PCE_E_CONFLICT. *conflict gets K. */
int pce_mass_dempster(const pce_mass_t *a, const pce_mass_t *b, pce_mass_t *out, rat_t *conflict);

/* P3 */
int pce_mass_and(const pce_mass_t *a, const pce_mass_t *b, pce_mass_t *out);
int pce_mass_or(const pce_mass_t *a, const pce_mass_t *b, pce_mass_t *out);
int pce_mass_not(const pce_mass_t *a, pce_mass_t *out);

/* P4 */
int pce_mass_read(const pce_mass_t *m, pce_read_t *out);
/* |m[T] - m[F]|; invalid when m is not a mass. */
rat_t pce_mass_margin(const pce_mass_t *m);

/* P5: base only (LPX_B_*), or LPX_B_NEITHER when m or th is not valid. */
uint8_t pce_mass_base(const pce_mass_t *m, const pce_thresh_t *th);
/* P5: the full verdict, logged (PCE_OP_PROB, aux 0) when log is not NULL.
 * prev may be NULL (no trend: neutral). */
int pce_mass_decide(const pce_mass_t *prev, const pce_mass_t *cur, const pce_thresh_t *th,
                    bool stale, uint8_t phase, uint32_t advisory, pdx_log_t *log,
                    pce_verdict_t *out);

/* P6 */
int pce_dist_check(const rat_t *p, uint32_t n); /* PCE_OK when p is a distribution */
int pce_bayes(const rat_t *prior, const rat_t *like, uint32_t n, rat_t *post);
/* (a + s) / (a + b + s + f): the posterior mean rate after s successes and
 * f failures from a Beta(a, b) prior; a + b must be > 0. */
int pce_rate(uint64_t s, uint64_t f, uint64_t a, uint64_t b, rat_t *out);
int pce_expect(const rat_t *p, const int64_t *v, uint32_t n, rat_t *out);
int pce_tv(const rat_t *p, const rat_t *q, uint32_t n, rat_t *out);
/* Witness: LPX_B_TRUE when tv <= tol, LPX_B_BOTH when greater, LPX_B_NEITHER
 * when either input is not a distribution. *tv may be NULL. */
uint8_t pce_witness(const rat_t *p, const rat_t *q, uint32_t n, rat_t tol, rat_t *tv);

#endif /* PCE_PROB_H */
