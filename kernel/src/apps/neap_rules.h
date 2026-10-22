/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* neap_rules.h — Neap's rules in C, the reference its Eve, Sutra,
 * Trebago, KRON and MYRA blocks are checked against (blocks/neap). Pure
 * integer functions, no state: the model (neap_model.c) and the language
 * test (tests/neap_zxapp.inc) both include this.
 *
 *   NR1 AN ENTRY MAY POST (entry_may_post): amount 1..NEAP_AMOUNT_MAX
 *       minor units (2^53 - 1, libzxv ZXV_AMOUNT_MAX), gap (the entry's
 *       debits minus its credits) exactly 0, shut (how many of the
 *       accounts it touches are closed in the chart) exactly 0 and period
 *       (the period it falls in is open) exactly 1. Witnessed while gap
 *       fits 18 digits either way, shut 0..9999 and period 0..9 (Sutra's
 *       pictures); outside them it is never yes (Eve answers no, Sutra
 *       refuses: held).
 *   NR2 THE ACCOUNTING EQUATION (equation_gap): assets - liabilities -
 *       equity, each the sum of its class's balances on its normal side
 *       in minor units, equity with income less expenses carried in; 0
 *       exactly when A = L + E. Each input and the answer fit 18 digits
 *       either way (Sutra's s9(18)), else refused (Trebago answers to 64
 *       bits: held).
 *   NR3 STRAIGHT LINE (straight_charge): what period `period` (1..life)
 *       of an asset's life of `life` periods (1..1200) is charged, cost
 *       and salvage in minor units, 0 <= salvage <= cost <=
 *       NEAP_COST_MAX: floor((cost - salvage) / life) in every period but
 *       the last, and the remainder, (cost - salvage) - (life - 1) *
 *       floor((cost - salvage) / life), in the last, so the charges sum
 *       to cost - salvage exactly. A cost past 12 digits is held to 15
 *       (Sutra's 9(12) refuses, Trebago answers), else refused.
 *   NR4 DECLINING BALANCE (declining_charge): one period's charge on an
 *       asset whose book value at the period's start is book, never
 *       taking it below salvage: the last period (last = 1) takes what is
 *       left above salvage, book - salvage; any other (last = 0)
 *       min(floor(book * rate / 10000), book - salvage), rate in
 *       hundredths of a percent 0..10000. 0 <= salvage <= book <=
 *       NEAP_COST_MAX; a book past 12 digits held to 15, as NR3.
 *   NR5 A GENERAL LEDGER LINE (gl_line_amount): what one line moves its
 *       account's balance on its normal side, (debit - credit) * normal,
 *       normal 1 for a debit-normal account (an asset, an expense) and -1
 *       for a credit-normal one (a liability, equity, income); debit and
 *       credit 0..NEAP_MINOR_MAX, else refused.
 *   NR6 THE ACCOUNT GRID (account_month_total): row i's cell of the grid
 *       of accounts by months, the exact sum of the amounts of every row
 *       with its account and its month, itself included.
 *   NR7 A CHART NODE (coa_node_ok) fits ^ZXNEAP(parent, code): parent and
 *       code at most 6 digits, kind 1 digit and open 1 digit, sign aside.
 *
 * Anything else is refused (NEAP_REFUSED), never rounded or wrapped. */
#ifndef NEAP_RULES_H
#define NEAP_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define NEAP_REFUSED     INT64_MIN
#define NEAP_AMOUNT_MAX  INT64_C(9007199254740991)   /* NR1: 2^53 - 1 */
#define NEAP_MINOR_MAX   INT64_C(999999999999999999) /* 18 digits, Sutra's s9(18) */
#define NEAP_COST_MAX    INT64_C(999999999999)       /* NR3, NR4: Sutra's 9(12) */
#define NEAP_HELD_MAX    INT64_C(999999999999999)    /* NR3, NR4: Trebago's 15 digits */
#define NEAP_LIFE_MAX    1200                        /* NR3: 100 years of months */
#define NEAP_RATE_ALL    INT64_C(10000)              /* NR4: 100.00 percent */
#define NEAP_SHUT_MAX    9999                        /* NR1: Sutra's 9(4) */
#define NEAP_PERIOD_MAX  9                           /* NR1: Sutra's 9(1) */
#define NEAP_CODE_DIGITS 6                           /* NR7 */

static inline bool neap_fits18(int64_t v)
{
    return v >= -NEAP_MINOR_MAX && v <= NEAP_MINOR_MAX;
}

/* NR1 */
static inline bool neap_entry_may_post(int64_t amount, int64_t gap, int64_t shut, int64_t period)
{
    return amount >= 1 && amount <= NEAP_AMOUNT_MAX && gap == 0 && shut == 0 && period == 1;
}

/* NR1: inside Sutra's pictures, where Eve and Sutra answer alike */
static inline bool neap_entry_witnessed(int64_t gap, int64_t shut, int64_t period)
{
    return neap_fits18(gap) && shut >= 0 && shut <= NEAP_SHUT_MAX && period >= 0 &&
           period <= NEAP_PERIOD_MAX;
}

/* NR2 */
static inline int64_t neap_equation_gap(int64_t assets, int64_t liabilities, int64_t equity)
{
    if (!neap_fits18(assets) || !neap_fits18(liabilities) || !neap_fits18(equity))
        return NEAP_REFUSED;
    int64_t g = assets - liabilities; /* each 18 digits: no 64-bit overflow */
    g -= equity;
    return neap_fits18(g) ? g : NEAP_REFUSED;
}

/* NR3 */
static inline int64_t neap_straight_charge(int64_t cost, int64_t salvage, int64_t life,
                                           int64_t period)
{
    if (cost < 0 || cost > NEAP_COST_MAX || salvage < 0 || salvage > cost || life < 1 ||
        life > NEAP_LIFE_MAX || period < 1 || period > life)
        return NEAP_REFUSED;
    int64_t base = cost - salvage, each = base / life;
    return period < life ? each : base - (life - 1) * each;
}

/* NR4 */
static inline int64_t neap_declining_charge(int64_t book, int64_t salvage, int64_t rate,
                                            int64_t last)
{
    if (book < 0 || book > NEAP_COST_MAX || salvage < 0 || salvage > book || rate < 0 ||
        rate > NEAP_RATE_ALL || last < 0 || last > 1)
        return NEAP_REFUSED;
    int64_t room = book - salvage, charge = book * rate / NEAP_RATE_ALL; /* below 10^16 */
    if (last == 1 || charge > room) return room;
    return charge;
}

/* NR5 */
static inline int64_t neap_gl_line_amount(int64_t debit, int64_t credit, int64_t normal)
{
    if (debit < 0 || debit > NEAP_MINOR_MAX || credit < 0 || credit > NEAP_MINOR_MAX ||
        (normal != 1 && normal != -1))
        return NEAP_REFUSED;
    return (debit - credit) * normal;
}

/* NR6: row i's cell, or NEAP_REFUSED when the sum passes 64 bits */
static inline int64_t neap_grid_cell(const int64_t *account, const int64_t *month,
                                     const int64_t *amount, uint32_t n, uint32_t i)
{
    int64_t s = 0;
    if (!account || !month || !amount || i >= n) return NEAP_REFUSED;
    for (uint32_t j = 0; j < n; j++) {
        if (account[j] != account[i] || month[j] != month[i]) continue;
        if ((amount[j] > 0 && s > INT64_MAX - amount[j]) ||
            (amount[j] < 0 && s < INT64_MIN + 1 - amount[j]))
            return NEAP_REFUSED;
        s += amount[j];
    }
    return s;
}

/* NR7: |v| has at most `digits` digits */
static inline bool neap_digits(int64_t v, int digits)
{
    int64_t top = 1;
    for (int k = 0; k < digits; k++) top *= 10;
    return v > -top && v < top;
}

static inline bool neap_node_ok(int64_t parent, int64_t code, int64_t kind, int64_t open)
{
    return neap_digits(parent, NEAP_CODE_DIGITS) && neap_digits(code, NEAP_CODE_DIGITS) &&
           neap_digits(kind, 1) && neap_digits(open, 1);
}

#endif
