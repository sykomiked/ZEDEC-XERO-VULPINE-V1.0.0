/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* assay_rules.h — Assay's rules in C, the reference its Trebago, KRON and
 * Eve blocks are checked against (blocks/assay). Pure integer functions,
 * no state: the model (assay_model.c) and the language test
 * (tests/assay_zxapp.inc) both include this, so the languages are tested
 * against the very C the model runs.
 *
 *   AY1 PASS SHARE: the share of a subject's results that passed, in
 *       hundredths of a percent, never rounded up: floor(10000 * passes /
 *       runs), runs 1..ASSAY_COUNT_MAX and passes 0..runs. Anything else
 *       is no count and is refused (Trebago divides by zero; KRON divides
 *       as it is: held).
 *   AY2 MEETS THE BAR: every margin inside its range, each given as what
 *       the subject has minus what the owner's bar asks: the pass margin
 *       (pass share - bar) 0..10000, the refusal margin (most refusals
 *       allowed - refusals) and the run margin (runs - fewest runs) each
 *       0..ASSAY_COUNT_MAX. A margin below 0 misses the bar; one past its
 *       range is no measurement (Eve refuses it; KRON reads any margin at
 *       or above 0 as met: held).
 *   AY3 RANK: a subject's place among the scores, best first, ties
 *       sharing the higher place: 1 + the number of scores strictly
 *       greater than its own (competition ranking, "1224").
 *   AY4 A RUN'S VERDICT is pce_prob.verdict on its four counts (TRUE,
 *       FALSE, BOTH, NEITHER) under the owner's thresholds, decide and
 *       conflict, each a fraction n/d with 0 < n <= d <= ASSAY_COUNT_MAX.
 *   AY5 REPEATED: the latest run joined with the one before it
 *       (pce_prob.join), then decided as AY4: a subject that passes a
 *       suite once and fails it the next time shows as conflict. */
#ifndef ASSAY_RULES_H
#define ASSAY_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define ASSAY_SHARE_MAX INT64_C(10000)     /* AY1: every result passed */
#define ASSAY_COUNT_MAX INT64_C(999999999) /* AY1, AY2: Eve's count range */
#define ASSAY_REFUSED   INT64_MIN

/* AY1: floor(10000 * passes / runs), or ASSAY_REFUSED. */
static inline int64_t assay_pass_share_x100(int64_t passes, int64_t runs)
{
    if (runs < 1 || runs > ASSAY_COUNT_MAX || passes < 0 || passes > runs) return ASSAY_REFUSED;
    return passes * ASSAY_SHARE_MAX / runs; /* 10^4 * 10^9 stays far below int64 */
}

/* AY2 */
static inline bool assay_meets_bar(int64_t passroom, int64_t refusalroom, int64_t runroom)
{
    return passroom >= 0 && passroom <= ASSAY_SHARE_MAX && refusalroom >= 0 &&
           refusalroom <= ASSAY_COUNT_MAX && runroom >= 0 && runroom <= ASSAY_COUNT_MAX;
}

/* AY3: the place of score[i] among score[0..n). */
static inline int64_t assay_score_rank(const int64_t *score, uint32_t n, uint32_t i)
{
    int64_t r = 1;
    if (!score || i >= n) return ASSAY_REFUSED;
    for (uint32_t j = 0; j < n; j++) r += score[j] > score[i];
    return r;
}

/* AY4: a threshold n/d inside (0, 1]. */
static inline bool assay_fraction_ok(int64_t num, int64_t den)
{
    return num >= 1 && den >= 1 && den <= ASSAY_COUNT_MAX && num <= den;
}

#endif
