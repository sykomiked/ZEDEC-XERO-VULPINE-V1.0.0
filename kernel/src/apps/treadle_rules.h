/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* treadle_rules.h — Treadle's rules in C, the reference its Sutra, KRON,
 * C++ (WebAssembly), Eve and MYRA blocks are checked against
 * (blocks/treadle). Pure integer functions, no state: the model
 * (treadle_model.c) and the language test (tests/treadle_zxapp.inc) both
 * include this, so the languages are tested against the very C the model
 * runs.
 *
 *   TR1 NEXT RUN: a task runs every `period` minutes (1..1440) from minute
 *       `start` of the day (0..1439), the day over. At minute `now`
 *       (0..1439) its next run is the first start + k * period (k >= 0)
 *       at or after now; when that is past the day's last minute, the next
 *       run is tomorrow's first, start + 1440. The answer counts minutes
 *       from today's midnight, so 1440 and past is tomorrow. Anything
 *       outside the domain is refused (-1 here; C++ traps, Sutra and KRON
 *       answer: held).
 *   TR2 WITHIN BUDGET: a run that costs `cost` tokens (0..999999999) fits
 *       the owner's budget when what is left after it, `left` (the cap
 *       minus what was spent today minus the cost), is 0..999999999. A
 *       value past nine digits is no budget the owner set: Eve refuses it,
 *       KRON reads only the floors (held).
 *   TR3 A RECORD of ^ZXTREADLE holds integers of at most the digits its
 *       pieces declare, sign aside (blocks/treadle/task.myra). */
#ifndef TREADLE_RULES_H
#define TREADLE_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define TREADLE_DAY_MIN    1440      /* minutes in a day */
#define TREADLE_TOKENS_MAX 999999999 /* nine digits: the budget's picture */

/* TR1: the next run, in minutes from today's midnight; -1 outside the domain. */
static inline int64_t treadle_next_run(int64_t start, int64_t period, int64_t now)
{
    if (start < 0 || start >= TREADLE_DAY_MIN || period < 1 || period > TREADLE_DAY_MIN ||
        now < 0 || now >= TREADLE_DAY_MIN)
        return -1;
    if (now <= start) return start;
    int64_t due = start + (now - start + period - 1) / period * period;
    if (due >= TREADLE_DAY_MIN) return start + TREADLE_DAY_MIN;
    return due;
}

/* TR2: does a run of `cost` tokens leave `left` of the owner's budget? */
static inline bool treadle_budget_ok(int64_t cost, int64_t left)
{
    return cost >= 0 && cost <= TREADLE_TOKENS_MAX && left >= 0 && left <= TREADLE_TOKENS_MAX;
}

/* TR3: does x have at most `digits` digits, sign aside? */
static inline bool treadle_digits_fit(int64_t x, uint32_t digits)
{
    int64_t lim = 1;
    for (uint32_t i = 0; i < digits && i < 18u; i++) lim *= 10;
    return x > -lim && x < lim;
}

static inline bool treadle_record_fits(int64_t task, int64_t kind, int64_t start, int64_t period,
                                       int64_t cost, int64_t approved)
{
    return treadle_digits_fit(task, 3) && treadle_digits_fit(kind, 1) &&
           treadle_digits_fit(start, 4) && treadle_digits_fit(period, 4) &&
           treadle_digits_fit(cost, 9) && treadle_digits_fit(approved, 1);
}

#endif
