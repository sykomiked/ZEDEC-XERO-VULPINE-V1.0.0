/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* furrow_rules.h — Furrow's rules in C, the reference its Trebago, KRON,
 * Sutra, Eve and MYRA blocks are checked against (blocks/furrow). Pure
 * integer functions, no state: the model (furrow_model.c) and the language
 * test (tests/furrow_zxapp.inc) both include this, so the languages are
 * tested against the very C the model runs.
 *
 * A cell holds an integer: a plain number, or money in integer minor units
 * of one currency at a fixed scale of FURROW_SCALE (hundredths, cents for
 * USD). Never a float: a mean that is not whole is a rational (rational.h).
 *
 *   FW1 A RANGE'S TOTAL (range_total). One cell per row: its position
 *       along its line (a row number for a column, a column number for a
 *       row), the range lo..hi it is asked about (both ends inside) and
 *       its value. The exact sum of the values whose position lies inside
 *       their range, added from the first; refused when a partial sum
 *       passes 64 bits, never wrapped. A column's sum and a row's sum are
 *       the same rule along a different line.
 *   FW2 A MONEY CELL (money_cell). Whole units and hundredths to minor
 *       units: units * FURROW_SCALE + cents, the sign carried by both
 *       (-12.34 is units -12, cents -34, as C's / and % split -1234).
 *       units -FURROW_UNITS_MAX..FURROW_UNITS_MAX (16 digits, Sutra's
 *       s9(16)), cents -99..99 (s9(2)), else refused; the result always
 *       fits 18 digits.
 *   FW3 FORMULA LIMITS (formula_ok). Each value against its own range
 *       (Eve): depth, how many formulas deep an evaluation has gone,
 *       0..FURROW_DEPTH_MAX, and refs, how many cells it has referenced so
 *       far, 0..FURROW_REFS_MAX. A cycle never ends, so it passes the depth
 *       and is refused; a fan of formulas over formulas passes the refs.
 *   FW4 A CELL RECORD of ^ZXFURROW (cell_record_ok, MYRA): the sheet at
 *       most 4 digits, the row 7, the column 5, the kind 1 and the value
 *       18, sign aside. Only cells that hold something are kept: the store
 *       is sparse.
 *   FW5 A SHEET'S MONEY is coffer.holdings: the exact sum of every money
 *       cell, added from the first; refused when a partial sum passes 64
 *       bits. */
#ifndef FURROW_RULES_H
#define FURROW_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define FURROW_SCALE     100 /* minor units in a whole unit */
#define FURROW_UNITS_MAX INT64_C(9999999999999999)
#define FURROW_CENTS_MAX 99
#define FURROW_DEPTH_MAX 16
#define FURROW_REFS_MAX  65536
#define FURROW_ROW_MAX   1048576 /* the model's sheet: rows 1.. */
#define FURROW_COL_MAX   16384   /* columns 1.. */
#define FURROW_SHEET_DIG INT64_C(10000)
#define FURROW_ROW_DIG   INT64_C(10000000)
#define FURROW_COL_DIG   INT64_C(100000)
#define FURROW_KIND_DIG  INT64_C(10)
#define FURROW_VALUE_DIG INT64_C(1000000000000000000)

/* FW1: false when a partial sum passes 64 bits (or nothing to add to) */
static inline bool furrow_range_total(const int64_t *pos, const int64_t *lo, const int64_t *hi,
                                      const int64_t *cell, uint32_t n, int64_t *total)
{
    int64_t s = 0;
    if (!total || (n && (!pos || !lo || !hi || !cell))) return false;
    for (uint32_t i = 0; i < n; i++) {
        if (pos[i] < lo[i] || pos[i] > hi[i]) continue;
        if (cell[i] > 0 && s > INT64_MAX - cell[i]) return false;
        if (cell[i] < 0 && s < INT64_MIN - cell[i]) return false;
        s += cell[i];
    }
    *total = s;
    return true;
}

/* FW2: false when units or cents is past its picture */
static inline bool furrow_money_cell(int64_t units, int64_t cents, int64_t *minor)
{
    if (!minor || units < -FURROW_UNITS_MAX || units > FURROW_UNITS_MAX ||
        cents < -FURROW_CENTS_MAX || cents > FURROW_CENTS_MAX)
        return false;
    *minor = units * FURROW_SCALE + cents;
    return true;
}

/* FW3 */
static inline bool furrow_formula_ok(int64_t depth, int64_t refs)
{
    return depth >= 0 && depth <= FURROW_DEPTH_MAX && refs >= 0 && refs <= FURROW_REFS_MAX;
}

/* FW4: |v| below top */
static inline bool furrow_digits(int64_t v, int64_t top)
{
    return v > -top && v < top;
}

static inline bool furrow_cell_record_ok(int64_t sheet, int64_t row, int64_t col, int64_t kind,
                                         int64_t value)
{
    return furrow_digits(sheet, FURROW_SHEET_DIG) && furrow_digits(row, FURROW_ROW_DIG) &&
           furrow_digits(col, FURROW_COL_DIG) && furrow_digits(kind, FURROW_KIND_DIG) &&
           furrow_digits(value, FURROW_VALUE_DIG);
}

/* FW5 (coffer.holdings): false when a partial sum passes 64 bits */
static inline bool furrow_holdings(const int64_t *amount, uint32_t n, int64_t *total)
{
    int64_t s = 0;
    if (!total || (n && !amount)) return false;
    for (uint32_t i = 0; i < n; i++) {
        if (amount[i] > 0 && s > INT64_MAX - amount[i]) return false;
        if (amount[i] < 0 && s < INT64_MIN - amount[i]) return false;
        s += amount[i];
    }
    *total = s;
    return true;
}

#endif
