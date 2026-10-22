/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* whetstone_rules.h — Whetstone's rules in C, the reference its KRON, Eve,
 * Trebago, Sutra and MYRA blocks are checked against (blocks/whetstone).
 * Pure integer functions, no state: the model (whetstone_model.c) and the
 * language test (tests/whetstone_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 *   WR1 THE SUITE'S VERDICT: every result's four-valued verdict (lpres.h:
 *       NEITHER 0, TRUE 1, FALSE 2, BOTH 3) conjoined with lpres_conjoin,
 *       the kernel's chain order F < N < B < T: the lowest result wins, so
 *       BOTH or NEITHER is never TRUE. A verdict outside 0..3 is refused.
 *       No result at all is NEITHER: nothing ran, so nothing passed (the
 *       blocks are never asked about an empty suite).
 *   WR2 THE SUITE PASSED: WR1 as a truth over the counts of results by
 *       verdict: passed 1..WHETSTONE_COUNT_MAX and failed, held and
 *       unknown (NEITHER) each exactly 0.
 *   WR3 A RUN'S RESULTS COUNTED: passed + failed + held, each count
 *       0..WHETSTONE_COUNT_MAX (Sutra's 9(9)) and the total too; anything
 *       else refused, never wrapped.
 *   WR4 THE SUITE'S TIME: the exact sum of its runs' milliseconds, each
 *       run 0..WHETSTONE_RUN_MS_MAX (a first try and three retries of an
 *       hour each); a run outside refuses the whole total.
 *   WR5 RETRY LIMITS: 0..WHETSTONE_RETRIES_MAX retries after the first
 *       try and a timeout of WHETSTONE_TIMEOUT_MIN..WHETSTONE_TIMEOUT_MAX
 *       seconds for each try, each on its own range.
 *   WR6 THE RUN RECORD of ^ZXWHET: run 4 digits, target 3, verdict 1,
 *       passed, failed and held 9 each and ms 8, sign aside.
 *   WR7 THE PASS SHARE, assay.pass_share_x100's meaning: floor(10000 *
 *       passes / runs), runs 1..999999999 and passes 0..runs.
 *   WR8 RESULTS OF ONE KIND, witness.kind_count's meaning: how many
 *       verdicts equal want. */
#ifndef WHETSTONE_RULES_H
#define WHETSTONE_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "lpres.h"

#define WHETSTONE_REFUSED     INT64_C(-1)
#define WHETSTONE_COUNT_MAX   INT64_C(999999999) /* 9(9) */
#define WHETSTONE_RETRIES_MAX INT64_C(3)
#define WHETSTONE_TIMEOUT_MIN INT64_C(1)
#define WHETSTONE_TIMEOUT_MAX INT64_C(3600)     /* seconds: an hour a try */
#define WHETSTONE_RUN_MS_MAX  INT64_C(14400000) /* four tries of an hour, ms */

static inline bool whetstone_verdict_ok(int64_t v)
{
    return v >= LPRES_STATE_NEITHER && v <= LPRES_STATE_BOTH;
}

/* WR1: n verdicts conjoined; WHETSTONE_REFUSED for a verdict outside 0..3 */
static inline int64_t whetstone_suite_verdict(const int64_t *v, uint32_t n)
{
    lpres_state_t acc = LPRES_STATE_TRUE;
    if (n == 0) return LPRES_STATE_NEITHER;
    if (!v) return WHETSTONE_REFUSED;
    for (uint32_t i = 0; i < n; i++) {
        if (!whetstone_verdict_ok(v[i])) return WHETSTONE_REFUSED;
        acc = lpres_conjoin(acc, (lpres_state_t) v[i]);
    }
    return acc;
}

/* WR2 */
static inline bool whetstone_suite_passed(int64_t passed, int64_t failed, int64_t held,
                                          int64_t unknown)
{
    return passed >= 1 && passed <= WHETSTONE_COUNT_MAX && failed == 0 && held == 0 && unknown == 0;
}

static inline bool whetstone_count_ok(int64_t c)
{
    return c >= 0 && c <= WHETSTONE_COUNT_MAX;
}

/* WR3 */
static inline int64_t whetstone_results_total(int64_t passed, int64_t failed, int64_t held)
{
    if (!whetstone_count_ok(passed) || !whetstone_count_ok(failed) || !whetstone_count_ok(held))
        return WHETSTONE_REFUSED;
    int64_t total = passed + failed + held; /* three 9(9): no overflow */
    return total <= WHETSTONE_COUNT_MAX ? total : WHETSTONE_REFUSED;
}

/* WR4 */
static inline int64_t whetstone_ms_total(const int64_t *ms, uint32_t n)
{
    int64_t total = 0;
    if (n && !ms) return WHETSTONE_REFUSED;
    for (uint32_t i = 0; i < n; i++) {
        if (ms[i] < 0 || ms[i] > WHETSTONE_RUN_MS_MAX) return WHETSTONE_REFUSED;
        total += ms[i]; /* at most 2^32 runs of 14400000 ms: no overflow */
    }
    return total;
}

/* WR5 */
static inline bool whetstone_retry_ok(int64_t retries, int64_t timeout)
{
    return retries >= 0 && retries <= WHETSTONE_RETRIES_MAX && timeout >= WHETSTONE_TIMEOUT_MIN &&
           timeout <= WHETSTONE_TIMEOUT_MAX;
}

/* WR6: |v| has at most `digits` digits */
static inline bool whetstone_digits_fit(int64_t v, uint32_t digits)
{
    int64_t lim = 1;
    for (uint32_t i = 0; i < digits; i++) lim *= 10;
    return v > -lim && v < lim;
}

static inline bool whetstone_record_fits(int64_t run, int64_t target, int64_t verdict,
                                         int64_t passed, int64_t failed, int64_t held, int64_t ms)
{
    return whetstone_digits_fit(run, 4) && whetstone_digits_fit(target, 3) &&
           whetstone_digits_fit(verdict, 1) && whetstone_digits_fit(passed, 9) &&
           whetstone_digits_fit(failed, 9) && whetstone_digits_fit(held, 9) &&
           whetstone_digits_fit(ms, 8);
}

/* WR7: assay.pass_share_x100's meaning */
static inline int64_t whetstone_pass_x100(int64_t passes, int64_t runs)
{
    if (runs < 1 || runs > WHETSTONE_COUNT_MAX || passes < 0 || passes > runs)
        return WHETSTONE_REFUSED;
    return passes * 10000 / runs;
}

/* WR8: witness.kind_count's meaning */
static inline int64_t whetstone_kind_count(const int64_t *kind, uint32_t n, int64_t want)
{
    int64_t c = 0;
    if (n && !kind) return WHETSTONE_REFUSED;
    for (uint32_t i = 0; i < n; i++) c += kind[i] == want;
    return c;
}

#endif
