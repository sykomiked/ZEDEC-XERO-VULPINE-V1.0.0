/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fairground_rules.h — Fairground's rules in C, the reference its Sutra,
 * Trebago and Eve blocks are checked against (blocks/fairground). Pure
 * integer functions, no state: the model (fairground_model.c) and the
 * language test (tests/fairground_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 *   FR1 THE ASSURANCE FEE on a sale of amount minor units (pay_assure.h
 *       F1): floor(amount * 8889 / 10^7), 0.08889%, for an amount of
 *       0..FAIRGROUND_AMOUNT_MAX (2^53 - 1, libzxv ZXV_AMOUNT_MAX); else
 *       refused. Worked out as h * 8889 + floor(l * 8889 / 10^7) with
 *       h = amount / 10^7 and l = amount mod 10^7, so nothing passes 64
 *       bits. The witnessed domain is 0..FAIRGROUND_FEE_WITNESSED (Sutra's
 *       9(15)); past it Sutra refuses and Trebago answers: held.
 *   FR2 AN OFFER ALLOWED: price and units 1..FAIRGROUND_NINE each, and
 *       the owner's limits not passed, each given as its margin
 *       0..FAIRGROUND_NINE: priceroom (the owner's ceiling minus the price
 *       for a want, the price minus the owner's floor for a sell) and
 *       unitsroom (the owner's most units for one offer minus the units).
 *   FR3 THE PRICE PER UNIT ACROSS OFFERS: the sum of the prices over the
 *       sum of the units, an exact reduced fraction, never rounded; each
 *       price 0 or more, each offer at least 1 unit, sums inside 64 bits,
 *       at least one offer; else refused.
 *   FR4 AN OFFER'S RECORD: offer 0..9999, kind 1..3 (share, want, sell),
 *       resource 1..2 (compute, storage), units and price
 *       0..FAIRGROUND_MINOR_MAX (18 digits), status 0..3 (waiting, sent,
 *       refused, cancelled).
 *   FR5 THE FEE'S FOUR PARTS (pay_assure.h B1): 50% reserve floor, 25%
 *       V-Bill dividend pool, 15% infrastructure and node bounties, 10%
 *       regenerative capital, each floor(fee * pct / 100), every unit
 *       left over to the reserve floor, so the parts sum to the fee. */
#ifndef FAIRGROUND_RULES_H
#define FAIRGROUND_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define FAIRGROUND_AMOUNT_MAX    INT64_C(9007199254740991)   /* FR1: 2^53 - 1 */
#define FAIRGROUND_FEE_WITNESSED INT64_C(999999999999999)    /* FR1: Sutra's 9(15) */
#define FAIRGROUND_NINE          INT64_C(999999999)          /* FR2: 9 digits */
#define FAIRGROUND_MINOR_MAX     INT64_C(999999999999999999) /* FR4: 18 digits */
#define FAIRGROUND_OFFER_MAX     INT64_C(9999)               /* FR4 */
#define FAIRGROUND_FEE_NUM       INT64_C(8889)               /* 0.08889% = 8889 / 10^7 */
#define FAIRGROUND_FEE_DEN       INT64_C(10000000)
#define FAIRGROUND_PARTS         4u
#define FAIRGROUND_REFUSED       INT64_MIN

/* FR1: the fee, or FAIRGROUND_REFUSED outside 0..FAIRGROUND_AMOUNT_MAX. */
static inline int64_t fairground_assurance_fee(int64_t amount)
{
    if (amount < 0 || amount > FAIRGROUND_AMOUNT_MAX) return FAIRGROUND_REFUSED;
    int64_t h = amount / FAIRGROUND_FEE_DEN, l = amount % FAIRGROUND_FEE_DEN;
    return h * FAIRGROUND_FEE_NUM + l * FAIRGROUND_FEE_NUM / FAIRGROUND_FEE_DEN;
}

/* FR2 */
static inline bool fairground_offer_allowed(int64_t price, int64_t units, int64_t priceroom,
                                            int64_t unitsroom)
{
    return price >= 1 && price <= FAIRGROUND_NINE && units >= 1 && units <= FAIRGROUND_NINE &&
           priceroom >= 0 && priceroom <= FAIRGROUND_NINE && unitsroom >= 0 &&
           unitsroom <= FAIRGROUND_NINE;
}

static inline int64_t fairground_gcd(int64_t a, int64_t b)
{
    while (b) {
        int64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/* FR3: *num / *den reduced; false (nothing written) when refused. */
static inline bool fairground_unit_price(const int64_t *price, const int64_t *units, uint32_t n,
                                         int64_t *num, int64_t *den)
{
    int64_t p = 0, u = 0;
    if (!price || !units || !num || !den || n == 0) return false;
    for (uint32_t i = 0; i < n; i++) {
        if (price[i] < 0 || units[i] < 1) return false;
        if (p > INT64_MAX - price[i] || u > INT64_MAX - units[i]) return false;
        p += price[i];
        u += units[i];
    }
    int64_t g = fairground_gcd(p, u); /* u >= 1, so g >= 1 */
    *num = p / g;
    *den = u / g;
    return true;
}

/* FR4 */
static inline bool fairground_record_fits(int64_t offer, int64_t kind, int64_t resource,
                                          int64_t units, int64_t price, int64_t status)
{
    return offer >= 0 && offer <= FAIRGROUND_OFFER_MAX && kind >= 1 && kind <= 3 && resource >= 1 &&
           resource <= 2 && units >= 0 && units <= FAIRGROUND_MINOR_MAX && price >= 0 &&
           price <= FAIRGROUND_MINOR_MAX && status >= 0 && status <= 3;
}

/* FR5: parts[0] reserve floor, [1] V-Bill dividend pool, [2] bounties,
 * [3] regenerative capital; false for a fee below 0 or past FR1's range. */
static inline bool fairground_fee_parts(int64_t fee, int64_t parts[FAIRGROUND_PARTS])
{
    static const int64_t PCT[FAIRGROUND_PARTS] = {50, 25, 15, 10};
    if (!parts || fee < 0 || fee > FAIRGROUND_AMOUNT_MAX) return false;
    int64_t left = fee;
    for (uint32_t i = 0; i < FAIRGROUND_PARTS; i++) {
        parts[i] = fee * PCT[i] / 100;
        left -= parts[i];
    }
    parts[0] += left;
    return true;
}

#endif
