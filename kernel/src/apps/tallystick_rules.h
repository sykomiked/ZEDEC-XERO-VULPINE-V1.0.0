/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* tallystick_rules.h — Tallystick's rules in C, the reference its Sutra,
 * Trebago and Eve blocks are checked against (blocks/tallystick). Pure
 * integer functions, no state: the model (tallystick_model.c) and the
 * language test (tests/tallystick_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 *   TR1 A TAXED LINE. One line of an invoice: net = qty * price, the
 *       price per unit in integer minor units; its tax at a rate given as
 *       taxn / taxd (a rate of at most 100 percent: 0 <= taxn <= taxd),
 *       rounded half up to a whole minor unit, floor((2 * net * taxn +
 *       taxd) / (2 * taxd)); the line's total is net + tax. qty is
 *       0..TSTK_QTY_MAX whole units (Sutra's 9(6)), price
 *       0..TSTK_PRICE_MAX (9(7)), taxd 1..TSTK_RATE_MAX (9(5)). The
 *       invoice's total is the exact sum of its lines' totals
 *       (coffer.holdings).
 *   TR2 PAID IN FULL. What was paid (paid) is at or above the invoice's
 *       total: over = paid - total (coffer.balance) is 0..TSTK_OVER_MAX
 *       (18 digits) and total is 0..TSTK_TOTAL_MAX (libzxv
 *       ZXV_AMOUNT_MAX, 2^53 - 1, the most one posting carries).
 *   TR3 THE NEXT DOCUMENT NUMBER. Invoices and receipts are numbered
 *       last + 1 from 1, last 0..TSTK_NUMBER_MAX - 1 (Sutra's 9(9)); past
 *       nine digits the sequence is refused, never wrapped or restarted.
 *
 * Anything else is refused (TSTK_REFUSED), never rounded or wrapped. */
#ifndef TALLYSTICK_RULES_H
#define TALLYSTICK_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define TSTK_REFUSED    INT64_MIN
#define TSTK_QTY_MAX    INT64_C(999999)             /* 9(6) whole units */
#define TSTK_PRICE_MAX  INT64_C(9999999)            /* 9(7) minor units a unit */
#define TSTK_RATE_MAX   INT64_C(99999)              /* 9(5): taxn and taxd */
#define TSTK_TOTAL_MAX  INT64_C(9007199254740991)   /* 2^53 - 1, libzxv ZXV_AMOUNT_MAX */
#define TSTK_OVER_MAX   INT64_C(999999999999999999) /* 18 digits, Sutra's s9(18) */
#define TSTK_NUMBER_MAX INT64_C(999999999)          /* 9(9) */

/* TR1: the line's net, qty * price, or TSTK_REFUSED. */
static inline int64_t tstk_line_net(int64_t qty, int64_t price)
{
    if (qty < 0 || qty > TSTK_QTY_MAX || price < 0 || price > TSTK_PRICE_MAX) return TSTK_REFUSED;
    return qty * price; /* at most 13 digits */
}

/* TR1: the tax on net at taxn / taxd, rounded half up, or TSTK_REFUSED. */
static inline int64_t tstk_line_tax(int64_t net, int64_t taxn, int64_t taxd)
{
    if (net < 0 || net > TSTK_QTY_MAX * TSTK_PRICE_MAX || taxd < 1 || taxd > TSTK_RATE_MAX ||
        taxn < 0 || taxn > taxd)
        return TSTK_REFUSED;
    int64_t twice = 2 * taxd;
    return (2 * net * taxn + taxd) / twice; /* at most 2 * 10^18: fits */
}

/* TR1: the line's total, net + tax, or TSTK_REFUSED. */
static inline int64_t tstk_line_total(int64_t qty, int64_t price, int64_t taxn, int64_t taxd)
{
    int64_t net = tstk_line_net(qty, price);
    if (net == TSTK_REFUSED) return TSTK_REFUSED;
    int64_t tax = tstk_line_tax(net, taxn, taxd);
    if (tax == TSTK_REFUSED) return TSTK_REFUSED;
    return net + tax; /* tax <= net: at most 14 digits */
}

/* TR2: paid in full; total and over each inside their range. */
static inline bool tstk_paid_in_full(int64_t total, int64_t over)
{
    return total >= 0 && total <= TSTK_TOTAL_MAX && over >= 0 && over <= TSTK_OVER_MAX;
}

/* TR3: last + 1, or TSTK_REFUSED. */
static inline int64_t tstk_next_number(int64_t last)
{
    if (last < 0 || last >= TSTK_NUMBER_MAX) return TSTK_REFUSED;
    return last + 1;
}

#endif
