/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* retort_rules.h — Retort's rules in C, the reference its Trebago, KRON,
 * Rust (wasm), Eve and MYRA blocks are checked against (blocks/retort).
 * Pure integer functions, no state: the model (retort_model.c) and the
 * language test (tests/retort_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 *   RT1 MODEL SCORE TOTAL: the exact total of the scores of the graded
 *       answers whose contestant (model) is want, added from the first;
 *       each score 0..RETORT_SCORE_MAX points. A score outside that range
 *       is no grade and refuses the whole total (Trebago divides by zero;
 *       KRON adds it: held). 0 when no answer is want's.
 *   RT2 LATENCY: the fastest and the slowest run so far, one fold step at
 *       a time: min(fastest, ms) and max(slowest, ms), each value
 *       0..RETORT_LATENCY_MAX milliseconds (the longest run the limits
 *       allow, RT3's seconds at most). A value outside is no latency:
 *       refused (the wasm module traps; KRON answers: held).
 *   RT3 RUN LIMITS: the owner's limits on one run are inside what zxvd
 *       and Retort allow: prompt 1..RETORT_PROMPT_MAX bytes (swarm.ask's
 *       maxLength in api/zxvd.openrpc.json), fuel 1..RETORT_FUEL_MAX
 *       (aipi.execute runs under a function's fuel_max, which zxvd needs
 *       above 0; Eve's ranges hold 18 digits) and seconds
 *       1..RETORT_SECONDS_MAX per run (zxvd sets no wall-clock limit on a
 *       run; an hour is the default taken here).
 *   RT4 ENERGY PER ANSWER: the exact energy one answer took, uj /
 *       answers as a reduced fraction of microjoules, never rounded; uj
 *       0..RETORT_UJ_MAX (power.status's total_uj, 18 digits) and answers
 *       1..RETORT_ANSWERS_MAX, else refused.
 *   RT5 A RUN RECORD of ^ZXRETORT holds integers of at most the digits its
 *       pieces declare, sign aside (blocks/retort/run.myra): run 3,
 *       model 2, kind 1, prompt 4, fuel 18, ms 7, score 3, uj 18.
 *   RT6 A CONTESTANT'S SHARE of all the points given (banker.share,
 *       share_of_whole, @num.banker_share, used as it is): floor(10000 *
 *       size / whole), a whole of 0 counted as 1. */
#ifndef RETORT_RULES_H
#define RETORT_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define RETORT_SCORE_MAX   INT64_C(100)                /* RT1: points of one graded answer */
#define RETORT_SECONDS_MAX INT64_C(3600)               /* RT3: one run's wall clock */
#define RETORT_LATENCY_MAX INT64_C(3600000)            /* RT2: RETORT_SECONDS_MAX in ms */
#define RETORT_PROMPT_MAX  INT64_C(4095)               /* RT3: swarm.ask's maxLength */
#define RETORT_FUEL_MAX    INT64_C(999999999999999999) /* RT3: Eve's 18 digits */
#define RETORT_UJ_MAX      INT64_C(999999999999999999) /* RT4: 18 digits */
#define RETORT_ANSWERS_MAX INT64_C(999999999)          /* RT4 */
#define RETORT_UNGRADED    INT64_C(-1)                 /* RT5: a score not yet given */
#define RETORT_REFUSED     INT64_MIN

static inline bool retort_score_ok(int64_t s)
{
    return s >= 0 && s <= RETORT_SCORE_MAX;
}

/* RT1: the total of the scores whose model is want, or RETORT_REFUSED. */
static inline int64_t retort_model_score_total(const int64_t *model, int64_t want,
                                               const int64_t *score, uint32_t n)
{
    int64_t s = 0;
    if (!model || !score) return RETORT_REFUSED;
    for (uint32_t i = 0; i < n; i++)
        if (!retort_score_ok(score[i])) return RETORT_REFUSED;
    for (uint32_t i = 0; i < n; i++)
        if (model[i] == want) s += score[i]; /* n * 100 stays far below int64 */
    return s;
}

static inline bool retort_latency_ok(int64_t ms)
{
    return ms >= 0 && ms <= RETORT_LATENCY_MAX;
}

/* RT2: one fold step, or RETORT_REFUSED when either value is no latency. */
static inline int64_t retort_latency_fastest(int64_t fastest, int64_t ms)
{
    if (!retort_latency_ok(fastest) || !retort_latency_ok(ms)) return RETORT_REFUSED;
    return ms < fastest ? ms : fastest;
}

static inline int64_t retort_latency_slowest(int64_t slowest, int64_t ms)
{
    if (!retort_latency_ok(slowest) || !retort_latency_ok(ms)) return RETORT_REFUSED;
    return ms > slowest ? ms : slowest;
}

/* RT3 */
static inline bool retort_run_limits_ok(int64_t prompt, int64_t fuel, int64_t seconds)
{
    return prompt >= 1 && prompt <= RETORT_PROMPT_MAX && fuel >= 1 && fuel <= RETORT_FUEL_MAX &&
           seconds >= 1 && seconds <= RETORT_SECONDS_MAX;
}

static inline int64_t retort_gcd(int64_t a, int64_t b)
{
    while (b) {
        int64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/* RT4: *num / *den, reduced; false (nothing written) when refused. */
static inline bool retort_energy_per_answer(int64_t uj, int64_t answers, int64_t *num, int64_t *den)
{
    if (!num || !den || uj < 0 || uj > RETORT_UJ_MAX || answers < 1 || answers > RETORT_ANSWERS_MAX)
        return false;
    int64_t g = retort_gcd(uj, answers); /* answers >= 1, so g >= 1 */
    *num = uj / g;
    *den = answers / g;
    return true;
}

/* RT5: v fits in `digits` decimal digits, sign aside (MYRA's iN). */
static inline bool retort_digits_fit(int64_t v, uint32_t digits)
{
    int64_t lim = 1;
    if (v == INT64_MIN) return false;
    for (uint32_t i = 0; i < digits && lim <= INT64_MAX / 10; i++) lim *= 10;
    return (v < 0 ? -v : v) < lim;
}

static inline bool retort_run_record_fits(int64_t run, int64_t model, int64_t kind, int64_t prompt,
                                          int64_t fuel, int64_t ms, int64_t score, int64_t uj)
{
    return retort_digits_fit(run, 3) && retort_digits_fit(model, 2) && retort_digits_fit(kind, 1) &&
           retort_digits_fit(prompt, 4) && retort_digits_fit(fuel, 18) &&
           retort_digits_fit(ms, 7) && retort_digits_fit(score, 3) && retort_digits_fit(uj, 18);
}

/* RT6: banker.share's meaning; RETORT_REFUSED when size is not 0..whole
 * (a whole of 0 counted as 1) or the product would pass 64 bits. */
static inline int64_t retort_share_x100(int64_t size, int64_t whole)
{
    int64_t w = whole == 0 ? 1 : whole;
    if (size < 0 || whole < 0 || size > w || size > INT64_MAX / 10000) return RETORT_REFUSED;
    return size * 10000 / w;
}

#endif
