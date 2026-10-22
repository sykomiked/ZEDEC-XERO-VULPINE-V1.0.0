/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* folio_rules.h — Folio's rules in C, the reference its KRON, C++, Sutra,
 * Eve and MYRA blocks are checked against (blocks/folio). Pure integer
 * functions, no state: the model (folio_model.c) and the language test
 * (tests/folio_zxapp.inc) both include this, so the languages are tested
 * against the very C the model runs.
 *
 * Lengths on a page are twips, 1440 to the inch (20 to the point); money
 * is integer minor units of one currency; a quantity is in thousandths of
 * a unit (1500 is one and a half).
 *
 *   FR1 LINES PER PAGE. floor((height - top - bottom) / leading): the
 *       lines of one leading that fit between the top and bottom margins.
 *       height, top and bottom 0..FOLIO_TWIPS_MAX, leading
 *       1..FOLIO_TWIPS_MAX and top + bottom at most height; anything else
 *       is refused (FOLIO_REFUSED).
 *   FR2 PAGES FOR LINES. ceil(lines / perpage), 0 for no lines: lines
 *       0..FOLIO_LINES_MAX, perpage 1..FOLIO_PER_MAX, else refused.
 *   FR3 AN AMOUNT AT A FIXED SCALE. A quantity in thousandths at a unit
 *       price: (qty * price + 500) / 1000, rounded half up to a whole
 *       minor unit; qty and price 0..999999999 (Sutra's 9(9)), else
 *       refused.
 *   FR4 A TABLE'S TOTAL is coffer.holdings: the exact sum of its amounts,
 *       added from the first; refused when a partial sum passes 64 bits.
 *   FR5 COLUMNS ACROSS is flint.cells: floor(span / cell), span 0..65535
 *       and cell 1..256, else refused.
 *   FR6 PAGE SETUP LIMITS. Each value against its own range (Eve): width
 *       and height FOLIO_SIDE_MIN..FOLIO_TWIPS_MAX (2 to 22 inches), each
 *       margin 0..FOLIO_MARGIN_MAX (3 inches), leading
 *       FOLIO_LEADING_MIN..FOLIO_LEADING_MAX (8 to 72 points).
 *   FR7 AN OUTLINE RECORD fits ^ZXFOLIO (MYRA): the document's number at
 *       most 9 digits, the entry's 4, its level 1, its page 6 and its line
 *       9, sign aside. */
#ifndef FOLIO_RULES_H
#define FOLIO_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define FOLIO_REFUSED     (-1)
#define FOLIO_TWIPS_MAX   31680 /* 22 inches */
#define FOLIO_SIDE_MIN    2880  /* 2 inches */
#define FOLIO_MARGIN_MAX  4320  /* 3 inches */
#define FOLIO_LEADING_MIN 160   /* 8 points */
#define FOLIO_LEADING_MAX 1440  /* 72 points */
#define FOLIO_LINES_MAX   999999999
#define FOLIO_PER_MAX     9999
#define FOLIO_QTY_MAX     999999999 /* thousandths: 999999.999 units */
#define FOLIO_PRICE_MAX   999999999 /* minor units */
#define FOLIO_SCALE       1000      /* a quantity's thousandths */
#define FOLIO_SPAN_MAX    65535     /* flint.cells */
#define FOLIO_CELL_MAX    256
#define FOLIO_CHAR_TWIPS  120 /* one 10-point Courier character: 6 points */

/* FR1 */
static inline int64_t folio_page_lines(int64_t height, int64_t top, int64_t bottom, int64_t leading)
{
    if (height < 0 || height > FOLIO_TWIPS_MAX || top < 0 || top > FOLIO_TWIPS_MAX || bottom < 0 ||
        bottom > FOLIO_TWIPS_MAX || leading < 1 || leading > FOLIO_TWIPS_MAX)
        return FOLIO_REFUSED;
    if (top + bottom > height) return FOLIO_REFUSED;
    return (height - top - bottom) / leading;
}

/* FR2 */
static inline int64_t folio_page_count(int64_t lines, int64_t perpage)
{
    if (lines < 0 || lines > FOLIO_LINES_MAX || perpage < 1 || perpage > FOLIO_PER_MAX)
        return FOLIO_REFUSED;
    return (lines + perpage - 1) / perpage;
}

/* FR3 */
static inline int64_t folio_scaled_amount(int64_t qty, int64_t price)
{
    if (qty < 0 || qty > FOLIO_QTY_MAX || price < 0 || price > FOLIO_PRICE_MAX)
        return FOLIO_REFUSED;
    return (qty * price + FOLIO_SCALE / 2) / FOLIO_SCALE; /* at most 999999998000000 */
}

/* FR4: false when a partial sum passes 64 bits (or nothing to add to) */
static inline bool folio_table_total(const int64_t *amount, uint32_t n, int64_t *total)
{
    int64_t s = 0;
    if (!amount || !total) return false;
    for (uint32_t i = 0; i < n; i++) {
        if (amount[i] > 0 && s > INT64_MAX - amount[i]) return false;
        if (amount[i] < 0 && s < INT64_MIN - amount[i]) return false;
        s += amount[i];
    }
    *total = s;
    return true;
}

/* FR5 */
static inline int64_t folio_cells(int64_t span, int64_t cell)
{
    if (span < 0 || span > FOLIO_SPAN_MAX || cell < 1 || cell > FOLIO_CELL_MAX)
        return FOLIO_REFUSED;
    return span / cell;
}

/* FR6 */
static inline bool folio_page_setup_ok(int64_t width, int64_t height, int64_t left, int64_t right,
                                       int64_t top, int64_t bottom, int64_t leading)
{
    return width >= FOLIO_SIDE_MIN && width <= FOLIO_TWIPS_MAX && height >= FOLIO_SIDE_MIN &&
           height <= FOLIO_TWIPS_MAX && left >= 0 && left <= FOLIO_MARGIN_MAX && right >= 0 &&
           right <= FOLIO_MARGIN_MAX && top >= 0 && top <= FOLIO_MARGIN_MAX && bottom >= 0 &&
           bottom <= FOLIO_MARGIN_MAX && leading >= FOLIO_LEADING_MIN &&
           leading <= FOLIO_LEADING_MAX;
}

/* FR7: |v| below 10^digits */
static inline bool folio_digits(int64_t v, int64_t top)
{
    return v > -top && v < top;
}

static inline bool folio_outline_ok(int64_t doc, int64_t entry, int64_t level, int64_t page,
                                    int64_t line)
{
    return folio_digits(doc, INT64_C(1000000000)) && folio_digits(entry, 10000) &&
           folio_digits(level, 10) && folio_digits(page, 1000000) &&
           folio_digits(line, INT64_C(1000000000));
}

#endif
