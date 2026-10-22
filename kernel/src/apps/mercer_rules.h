/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mercer_rules.h — Mercer's rules in C, the reference its Sutra, KRON,
 * Trebago, Eve and MYRA blocks are checked against (blocks/mercer). Pure
 * integer functions, no state: the model (mercer_model.c) and the
 * language test (tests/mercer_zxapp.inc) both include this.
 *
 *   MR1 THE STOCK AFTER A SALE (stock_after_sale): stock - sold whole
 *       units, stock 0..MERCER_STOCK_MAX (Sutra's 9(9)) and sold
 *       1..stock; a sale of more than the stock would leave it below
 *       zero and is refused, never cut to zero.
 *   MR2 THE PRICE FROM COST AND MARGIN (cost_plus_price): cost * (mden +
 *       mnum) / mden in minor units, the margin mnum / mden over cost
 *       (25 / 100 is 25%), rounded half up to a whole minor unit:
 *       floor((2 * cost * (mden + mnum) + mden) / (2 * mden)); cost
 *       0..MERCER_COST_MAX (9(9)), mnum 0..MERCER_RATE_MAX and mden
 *       1..MERCER_RATE_MAX (9(5)). The task leaves the rounding unstated;
 *       half up is taken, as tallystick.taxed_line_total rounds a tax.
 *   MR3 A SALE ON CREDIT ALLOWED (credit_sale_ok): the customer's balance
 *       plus the sale at most the limit they declared, given as its
 *       margin room = limit - balance - sale (coffer.balance): the sale
 *       1..MERCER_AMOUNT_MAX (2^53 - 1, libzxv ZXV_AMOUNT_MAX), the limit
 *       0..MERCER_AMOUNT_MAX and room 0..MERCER_MINOR_MAX (18 digits).
 *   MR4 THE STOCK GRID (stock_grid_cell): row i's cell of the grid of
 *       products by places, the exact sum of the units of every row with
 *       its product and its place, itself included.
 *   MR5 A PRODUCT NODE (product_node_ok) fits ^ZXMERCER(category,
 *       product) = price ^ stock: category at most 4 digits, product 6,
 *       price 15 and stock 9, sign aside (MYRA's i-fields).
 *   MR6 ONE SALE, TWO SOURCES. The till's record of a sale says TRUE of
 *       itself; the buyer's receipt says TRUE when its units and amount
 *       are the till's, FALSE when they differ, NEITHER when there is
 *       none. The sale is lpres_kjoin of the two (TRUE with FALSE is
 *       BOTH); only TRUE may post.
 *
 * Anything else is refused (MERCER_REFUSED), never rounded or wrapped. */
#ifndef MERCER_RULES_H
#define MERCER_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "lpres.h"

#define MERCER_REFUSED    INT64_MIN
#define MERCER_STOCK_MAX  INT64_C(999999999)          /* MR1: Sutra's 9(9) */
#define MERCER_COST_MAX   INT64_C(999999999)          /* MR2: Sutra's 9(9) */
#define MERCER_HELD_COST  INT64_C(999999999999)       /* MR2: Trebago's 12 digits */
#define MERCER_RATE_MAX   INT64_C(99999)              /* MR2: 9(5) */
#define MERCER_AMOUNT_MAX INT64_C(9007199254740991)   /* MR3: 2^53 - 1 */
#define MERCER_MINOR_MAX  INT64_C(999999999999999999) /* MR3: 18 digits */

/* MR1 */
static inline int64_t mercer_stock_after(int64_t stock, int64_t sold)
{
    if (stock < 0 || stock > MERCER_STOCK_MAX || sold < 1 || sold > stock) return MERCER_REFUSED;
    return stock - sold;
}

/* MR2 */
static inline int64_t mercer_cost_plus(int64_t cost, int64_t mnum, int64_t mden)
{
    if (cost < 0 || cost > MERCER_COST_MAX || mnum < 0 || mnum > MERCER_RATE_MAX || mden < 1 ||
        mden > MERCER_RATE_MAX)
        return MERCER_REFUSED;
    return (2 * cost * (mden + mnum) + mden) / (2 * mden); /* below 4 * 10^14 */
}

/* MR3 */
static inline bool mercer_credit_ok(int64_t sale, int64_t limit, int64_t room)
{
    return sale >= 1 && sale <= MERCER_AMOUNT_MAX && limit >= 0 && limit <= MERCER_AMOUNT_MAX &&
           room >= 0 && room <= MERCER_MINOR_MAX;
}

/* MR4: row i's cell, or MERCER_REFUSED when the sum passes 64 bits */
static inline int64_t mercer_grid_cell(const int64_t *product, const int64_t *place,
                                       const int64_t *qty, uint32_t n, uint32_t i)
{
    int64_t s = 0;
    if (!product || !place || !qty || i >= n) return MERCER_REFUSED;
    for (uint32_t j = 0; j < n; j++) {
        if (product[j] != product[i] || place[j] != place[i]) continue;
        if ((qty[j] > 0 && s > INT64_MAX - qty[j]) || (qty[j] < 0 && s < INT64_MIN + 1 - qty[j]))
            return MERCER_REFUSED;
        s += qty[j];
    }
    return s;
}

/* MR5: |v| has at most `digits` digits (1..18) */
static inline bool mercer_digits(int64_t v, int digits)
{
    int64_t top = 1;
    for (int k = 0; k < digits; k++) top *= 10;
    return v > -top && v < top;
}

static inline bool mercer_node_ok(int64_t category, int64_t product, int64_t price, int64_t stock)
{
    return mercer_digits(category, 4) && mercer_digits(product, 6) && mercer_digits(price, 15) &&
           mercer_digits(stock, 9);
}

/* MR6: what the buyer's receipt says of the till's record (have false:
 * there is no receipt) */
static inline lpres_state_t mercer_receipt_says(bool have, int64_t qty, int64_t amount,
                                                int64_t rqty, int64_t ramount)
{
    if (!have) return LPRES_STATE_NEITHER;
    return qty == rqty && amount == ramount ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
}

/* MR6: the sale, the till's TRUE joined with the receipt's word */
static inline lpres_state_t mercer_sale_state(bool have, int64_t qty, int64_t amount, int64_t rqty,
                                              int64_t ramount)
{
    return lpres_kjoin(LPRES_STATE_TRUE, mercer_receipt_says(have, qty, amount, rqty, ramount));
}

#endif
