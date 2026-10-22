/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sealwax_rules.h — Sealwax's rules in C, the reference its Eve, KRON,
 * Hatchling (BASIC) and MYRA blocks (blocks/sealwax) and the parts it uses
 * (furrow.money_cell, coffer.holdings) are checked against. Pure integer functions, no state: the
 * model (sealwax_model.c) and the language test (tests/sealwax_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 *   SR1 A FIELD has a kind (1 text, 2 amount, 3 number), a required flag
 *       (0 or 1) and bounds lo..hi: characters entered for text, minor
 *       units for an amount, the value itself for a number. Bounds hold at
 *       most 15 digits, sign aside, and lo <= hi; text bounds lie in
 *       0..SEALWAX_VALUE_MAX, amount bounds are never below zero.
 *   SR2 A FIELD IS FILLED when something is entered; a filled field is
 *       OUTSIDE when its length (text) or value (amount, number) is not in
 *       lo..hi. A required field that is empty is MISSING.
 *   SR3 READY TO SIGN (sign_ready): a form of 1..SEALWAX_FIELDS fields with
 *       no field missing and none outside. A count below zero is no count:
 *       not ready.
 *   SR4 AN AMOUNT AS ENTERED: whole units and cents at the fixed scale of
 *       100, units * 100 + cents, which is Furrow's money_cell
 *       (money_from_parts). Furrow's money is signed; a form's amount is
 *       not, so this C refuses anything but units 0..SEALWAX_UNITS_MAX
 *       (13 digits, so the amount fits a record's 15-digit bounds) and
 *       cents 0..99 before that rule applies.
 *   SR5 THE FILL ORDER (fill_order): the fields not yet filled, required
 *       ones first, each group in field order, numbered from 1. Bit k - 1 of
 *       `required` and `filled` speaks for field k; 1..SEALWAX_FIELDS
 *       fields, a mask below zero or past the fields refused. Nothing left
 *       is the one answer 0.
 *   SR6 A FIELD RECORD of ^ZXSEALWAX holds integers of at most the digits
 *       its pieces declare, sign aside (blocks/sealwax/field.myra): form 9,
 *       field 2, kind 1, required 1, lo 15, hi 15, len 3.
 *   SR7 THE TOTAL of a form's amount fields is coffer.holdings: the exact
 *       sum of minor units, refused past 64 bits, never wrapped. */
#ifndef SEALWAX_RULES_H
#define SEALWAX_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define SEALWAX_FIELDS    32
#define SEALWAX_VALUE_MAX 120
#define SEALWAX_UNITS_MAX 9999999999999 /* 13 digits: 9(13) */
#define SEALWAX_BOUND_MAX 999999999999999
#define SEALWAX_REFUSED   (-1)

enum { SEALWAX_TEXT = 1, SEALWAX_AMOUNT = 2, SEALWAX_NUMBER = 3 };

/* SR1 */
static inline bool sealwax_bounds_ok(int64_t kind, int64_t lo, int64_t hi)
{
    if (lo > hi || lo < -SEALWAX_BOUND_MAX || hi > SEALWAX_BOUND_MAX) return false;
    if (kind == SEALWAX_TEXT) return lo >= 0 && hi <= SEALWAX_VALUE_MAX;
    if (kind == SEALWAX_AMOUNT) return lo >= 0;
    return kind == SEALWAX_NUMBER;
}

/* SR3 */
static inline bool sealwax_sign_ready(int64_t fields, int64_t missing, int64_t outside)
{
    return fields >= 1 && fields <= SEALWAX_FIELDS && missing == 0 && outside == 0;
}

/* SR4 */
static inline int64_t sealwax_amount_minor(int64_t units, int64_t cents)
{
    if (units < 0 || units > SEALWAX_UNITS_MAX || cents < 0 || cents > 99) return SEALWAX_REFUSED;
    return units * 100 + cents;
}

/* SR5: the order into out (room for SEALWAX_FIELDS); how many, or
 * SEALWAX_REFUSED. */
static inline int32_t sealwax_fill_order(int64_t count, int64_t required, int64_t filled,
                                         int64_t *out)
{
    if (count < 1 || count > SEALWAX_FIELDS) return SEALWAX_REFUSED;
    int64_t top = ((int64_t) 1 << count) - 1;
    if (required < 0 || required > top || filled < 0 || filled > top) return SEALWAX_REFUSED;
    int32_t n = 0;
    for (int64_t want = 1; want >= 0; want--)
        for (int64_t k = 1; k <= count; k++) {
            int64_t bit = (int64_t) 1 << (k - 1);
            if (!(filled & bit) && ((required & bit) != 0) == (want == 1)) out[n++] = k;
        }
    if (!n) out[n++] = 0;
    return n;
}

/* SR6: does x have at most `digits` digits, sign aside? */
static inline bool sealwax_digits_fit(int64_t x, uint32_t digits)
{
    int64_t lim = 1;
    for (uint32_t i = 0; i < digits && i < 18u; i++) lim *= 10;
    return x > -lim && x < lim;
}

static inline bool sealwax_record_fits(int64_t form, int64_t field, int64_t kind, int64_t required,
                                       int64_t lo, int64_t hi, int64_t len)
{
    return sealwax_digits_fit(form, 9) && sealwax_digits_fit(field, 2) &&
           sealwax_digits_fit(kind, 1) && sealwax_digits_fit(required, 1) &&
           sealwax_digits_fit(lo, 15) && sealwax_digits_fit(hi, 15) && sealwax_digits_fit(len, 3);
}

/* SR7: coffer.holdings as the model works it out; false past 64 bits. */
static inline bool sealwax_total(const int64_t *minor, uint32_t n, int64_t *total)
{
    int64_t s = 0;
    if (!minor && n) return false;
    for (uint32_t i = 0; i < n; i++) {
        if ((minor[i] > 0 && s > INT64_MAX - minor[i]) ||
            (minor[i] < 0 && s < INT64_MIN - minor[i]))
            return false;
        s += minor[i];
    }
    *total = s;
    return true;
}

#endif
