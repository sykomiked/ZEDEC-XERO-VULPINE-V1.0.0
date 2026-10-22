/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* tally_rules.h — Tally's rules in C, the reference its Trebago, KRON,
 * Sutra and ALGOL 60 blocks are checked against (blocks/tally). Pure
 * integer functions over kernel/src/rational, no state: the model
 * (tally_model.c) and the language test (tests/tally_zxapp.inc) both
 * include this, so the languages are tested against the very C the model
 * runs. Integers only: 1/3 stays 1/3.
 *
 *   TL1 RATIONAL ARITHMETIC. a = an / ad and b = bn / bd, each part
 *       -TALLY_PART_MAX .. TALLY_PART_MAX (18 digits, the ledger's s9(18)),
 *       added (TALLY_ADD), subtracted (TALLY_SUB), multiplied (TALLY_MUL)
 *       or divided (TALLY_DIV): the exact result, reduced, its denominator
 *       above 0. Refused when a part is outside, a denominator is 0, b is
 *       0 for a division, or the result does not fit int64 / int64 (never
 *       rounded, never wrapped).
 *   TL2 UNIT FACTORS. Unit u is tally_units[u].num / .den of its
 *       dimension's base unit (the metre, the gram, the millilitre, the
 *       second), exact (an inch is 127/5000 m, a pound 45359237/100000 g).
 *       A quantity in unit src is qty * factor(src) / factor(dst) in unit
 *       dst, exact and reduced; refused when either index is no unit, the
 *       two are of different dimensions, or the result does not fit.
 *   TL3 CURRENCY AMOUNT. An amount in minor units at an exchange rate
 *       rnum / rden (units of the other currency's minor unit for one of
 *       this one's): floor(amount * rnum / rden) whole minor units.
 *       amount 0 .. TALLY_AMOUNT_MAX, rnum and rden 1 .. TALLY_RATE_MAX
 *       (Sutra's 9(12) and 9(6)); anything else TALLY_REFUSED.
 *   TL4 CURRENCY REMAINDER. What TL3 leaves over, shown, never dropped:
 *       amount * rnum - TL3 * rden, in rden-ths of a minor unit, so
 *       amount * rnum / rden = TL3 + TL4 / rden exactly. The same domain;
 *       anything else TALLY_REFUSED. A conversion is shown, never posted. */
#ifndef TALLY_RULES_H
#define TALLY_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "../rational/rational.h"

#define TALLY_PART_MAX   INT64_C(999999999999999999)
#define TALLY_AMOUNT_MAX INT64_C(999999999999)
#define TALLY_RATE_MAX   INT64_C(999999)
#define TALLY_REFUSED    INT64_C(-1)

enum { TALLY_ADD = 1, TALLY_SUB = 2, TALLY_MUL = 3, TALLY_DIV = 4 };

/* TL2: the units, by index (blocks/tally/unit.kron holds the same three vectors) */
enum { TALLY_LENGTH = 1, TALLY_MASS = 2, TALLY_VOLUME = 3, TALLY_TIME = 4 };

typedef struct {
    const char *name;
    int64_t num, den;
    int32_t dim;
} tally_unit_t;

static const tally_unit_t tally_units[] = {
    {"mm", 1, 1000, TALLY_LENGTH},
    {"cm", 1, 100, TALLY_LENGTH},
    {"m", 1, 1, TALLY_LENGTH},
    {"km", 1000, 1, TALLY_LENGTH},
    {"in", 127, 5000, TALLY_LENGTH},
    {"ft", 381, 1250, TALLY_LENGTH},
    {"yd", 1143, 1250, TALLY_LENGTH},
    {"mi", 201168, 125, TALLY_LENGTH},
    {"mg", 1, 1000, TALLY_MASS},
    {"g", 1, 1, TALLY_MASS},
    {"kg", 1000, 1, TALLY_MASS},
    {"oz", 45359237, 1600000, TALLY_MASS},
    {"lb", 45359237, 100000, TALLY_MASS},
    {"ml", 1, 1, TALLY_VOLUME},
    {"l", 1000, 1, TALLY_VOLUME},
    {"fl oz", 473176473, 16000000, TALLY_VOLUME},
    {"gal", 473176473, 125000, TALLY_VOLUME},
    {"s", 1, 1, TALLY_TIME},
    {"min", 60, 1, TALLY_TIME},
    {"h", 3600, 1, TALLY_TIME},
    {"day", 86400, 1, TALLY_TIME},
};

#define TALLY_UNITS ((int64_t) (sizeof tally_units / sizeof tally_units[0]))

static inline bool tally_part_ok(int64_t v)
{
    return v >= -TALLY_PART_MAX && v <= TALLY_PART_MAX;
}

/* TL1: the result into *r (valid, reduced), or false. */
static inline bool tally_ratio(int64_t op, int64_t an, int64_t ad, int64_t bn, int64_t bd, rat_t *r)
{
    if (!tally_part_ok(an) || !tally_part_ok(ad) || !tally_part_ok(bn) || !tally_part_ok(bd))
        return false;
    rat_t a = rat_make(an, ad), b = rat_make(bn, bd), x;
    if (!a.valid || !b.valid) return false;
    switch (op) {
    case TALLY_ADD:
        x = rat_add(a, b);
        break;
    case TALLY_SUB:
        x = rat_sub(a, b);
        break;
    case TALLY_MUL:
        x = rat_mul(a, b);
        break;
    case TALLY_DIV:
        x = rat_div(a, b);
        break;
    default:
        return false;
    }
    if (!x.valid) return false;
    *r = x;
    return true;
}

/* TL2: qty in unit src as unit dst into *r, or false. */
static inline bool tally_unit_convert(int64_t qty, int64_t src, int64_t dst, rat_t *r)
{
    if (src < 0 || src >= TALLY_UNITS || dst < 0 || dst >= TALLY_UNITS) return false;
    const tally_unit_t *s = &tally_units[src], *d = &tally_units[dst];
    if (s->dim != d->dim) return false;
    rat_t x =
        rat_mul(rat_from_int(qty), rat_div(rat_make(s->num, s->den), rat_make(d->num, d->den)));
    if (!x.valid) return false;
    *r = x;
    return true;
}

static inline bool tally_fx_domain(int64_t amount, int64_t rnum, int64_t rden)
{
    return amount >= 0 && amount <= TALLY_AMOUNT_MAX && rnum >= 1 && rnum <= TALLY_RATE_MAX &&
           rden >= 1 && rden <= TALLY_RATE_MAX;
}

/* TL3 */
static inline int64_t tally_fx_amount(int64_t amount, int64_t rnum, int64_t rden)
{
    if (!tally_fx_domain(amount, rnum, rden)) return TALLY_REFUSED;
    return amount * rnum / rden;
}

/* TL4 */
static inline int64_t tally_fx_remainder(int64_t amount, int64_t rnum, int64_t rden)
{
    if (!tally_fx_domain(amount, rnum, rden)) return TALLY_REFUSED;
    return amount * rnum % rden;
}

#endif
