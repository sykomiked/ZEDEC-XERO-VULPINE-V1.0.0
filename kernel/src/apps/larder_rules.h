/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* larder_rules.h — Larder's rules in C, the reference its Sutra, Trebago
 * and Eve blocks are checked against (blocks/larder). Pure integer
 * functions, no state: the model (larder_model.c) and the language test
 * (tests/larder_zxapp.inc) both include this, so the languages are tested
 * against the very C the model runs. Money is integer minor units of one
 * currency (cents for USD), never a float.
 *
 *   LR1 SPENT IN A CATEGORY. A category is a holder account of the
 *       owner's books (never below zero), so its balance at the month's
 *       start (opening) and now (closing) are each 0..LARDER_MINOR_MAX
 *       (18 digits, Sutra's 9(18)); what was spent in it this month is
 *       closing - opening, net of refunds, so it is below 0 when more came
 *       back than went in. A balance below 0 or past 18 digits is refused.
 *   LR2 OVER BUDGET. A category is over its budget when a budget is set
 *       (budget 1..LARDER_MINOR_MAX; 0 means none, never over) and the
 *       excess (spent - budget, coffer.balance) is 1..LARDER_MINOR_MAX.
 *       Each field is held to its own range, so Eve can say it too.
 *   LR3 UNSPENT CARRY-OVER. What is carried into the next month: the exact
 *       sum over the categories of what was left unspent, max(0, budget -
 *       spent) each, so an overspent category carries nothing and takes
 *       nothing from the others. A budget below 0 is refused, and so is a
 *       difference or a partial sum past 64 bits (Trebago's exact
 *       rationals refuse what does not fit int64), never wrapped.
 *
 * Anything else is refused (LARDER_REFUSED), never rounded or wrapped. */
#ifndef LARDER_RULES_H
#define LARDER_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define LARDER_REFUSED   INT64_MIN
#define LARDER_MINOR_MAX INT64_C(999999999999999999) /* 18 digits, Sutra's 9(18) */

/* LR1: closing - opening, or LARDER_REFUSED. */
static inline int64_t larder_category_spent(int64_t opening, int64_t closing)
{
    if (opening < 0 || opening > LARDER_MINOR_MAX || closing < 0 || closing > LARDER_MINOR_MAX)
        return LARDER_REFUSED;
    return closing - opening; /* both 18 digits, never below 0: fits */
}

/* LR2: a budget set and an excess of at least one minor unit. */
static inline bool larder_category_over_budget(int64_t budget, int64_t excess)
{
    return budget >= 1 && budget <= LARDER_MINOR_MAX && excess >= 1 && excess <= LARDER_MINOR_MAX;
}

/* LR3: one category's unspent part, max(0, budget - spent), or
 * LARDER_REFUSED when budget is below 0 or the difference passes 64 bits. */
static inline int64_t larder_unspent(int64_t budget, int64_t spent)
{
    if (budget < 0) return LARDER_REFUSED;
    if (spent < 0 && budget > INT64_MAX + spent) return LARDER_REFUSED;
    return budget > spent ? budget - spent : 0;
}

/* LR3: the exact sum of n unspent parts, or LARDER_REFUSED. */
static inline int64_t larder_unspent_carry(const int64_t *budget, const int64_t *spent, uint32_t n)
{
    int64_t s = 0;
    if (n && (!budget || !spent)) return LARDER_REFUSED;
    for (uint32_t i = 0; i < n; i++)
        if (budget[i] < 0) return LARDER_REFUSED;
    for (uint32_t i = 0; i < n; i++) {
        int64_t u = larder_unspent(budget[i], spent[i]);
        if (u == LARDER_REFUSED || s > INT64_MAX - u) return LARDER_REFUSED;
        s += u;
    }
    return s;
}

#endif
