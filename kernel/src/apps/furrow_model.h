/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* furrow_model.h — Furrow, spreadsheets, as a model (zxm.h): a sheet of
 * numbers, money and formulas kept as one text file in the Hoard, read
 * with files.read and written whole with files.write. The rules its
 * languages say are furrow_rules.h (FW1-FW5).
 *
 * ADVICE ONLY for money: a sheet shows and sums amounts, it never pays,
 * and nothing here reaches the ledger.
 *
 * A cell is at row 1..FURROW_ROW_MAX and column 1..FURROW_COL_MAX and holds
 * one of:
 *   FURROW_NUMBER   an integer, |value| below 10^18;
 *   FURROW_MONEY    integer minor units at a fixed scale of hundredths
 *                   (money_cell, FW2), |value| below 10^18;
 *   FURROW_FORMULA  the sum of a range (range_total, FW1): rows lo..hi of
 *                   column `line` (FURROW_DOWN) or columns lo..hi of row
 *                   `line` (FURROW_ACROSS). Its value is evaluated when
 *                   asked, never stored.
 * Only cells that hold something are kept (a sparse store, FW4), sorted by
 * row, then column. No allocation, no floats: a mean that is not whole is a
 * rat_t (kernel/src/rational).
 *
 *   FM1 The file's first line is "furrow 1".
 *   FM2 Then one line per cell, in row and then column order, each cell at
 *       most once: "<row> <col> 1 <value>\n" for a number, "<row> <col> 2
 *       <minor>\n" for money, "<row> <col> 3 <dir> <line> <lo> <hi>\n" for
 *       a formula. Numbers are plain decimal, a "-" before a negative one,
 *       no leading zero and no "-0".
 *   FM3 Anything else is not a sheet: the load is refused (ZXM_E_PROTOCOL)
 *       and every change is refused (ZXM_E_STATE) until a load succeeds,
 *       so a file the model cannot read is never overwritten.
 *   FM4 Changes are checked, written whole, and only then kept: a write
 *       zxvd refuses leaves the model as it was.
 *   FM5 EVALUATION. A range adds its cells from the first (FW1). A formula
 *       inside a range is evaluated one level deeper; every cell reached
 *       counts as one reference. The moment the depth or the references
 *       pass formula_ok (FW3) the evaluation is refused (FURROW_E_REACH),
 *       so a cycle is refused, never followed for ever. A range of numbers
 *       is a number and a range of money is money; a range that mixes the
 *       two is refused (FURROW_E_MIXED); an empty range is the number 0. */
#ifndef FURROW_MODEL_H
#define FURROW_MODEL_H

#include "zxm.h"
#include "furrow_rules.h"
#include "../rational/rational.h"

#define FURROW_CELLS    1024u
#define FURROW_PATH_MAX 200u
#define FURROW_TEXT_MAX 45056u   /* the whole sheet, every line full */
#define FURROW_E_FILES  (-32014) /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */
#define FURROW_E_REACH  (-20)    /* FM5: past formula_ok */
#define FURROW_E_MIXED  (-21)    /* FM5: numbers and money in one range */

enum {
    FURROW_NUMBER = 1,
    FURROW_MONEY = 2,
    FURROW_FORMULA = 3,
};

enum {
    FURROW_DOWN = 1,   /* a formula over a column: rows lo..hi */
    FURROW_ACROSS = 2, /* over a row: columns lo..hi */
};

typedef struct {
    int64_t value; /* a number or money; a formula's line */
    int32_t row, col;
    int32_t kind;
    int32_t dir, lo, hi; /* a formula only; 0 for a number or money */
} furrow_cell_t;         /* no padding: two copies compare with memcmp */

typedef struct {
    zxm_link_t *link;
    char path[FURROW_PATH_MAX + 1];
    int32_t sheet; /* its number in the record (FW4) */
    bool loaded;   /* a load succeeded: changes may be written (FM3) */
    furrow_cell_t c[FURROW_CELLS];
    uint32_t n;
} furrow_t;

/* Name the sheet: path 1..FURROW_PATH_MAX bytes, no "..", sheet
 * 1..9999; ZXM_E_ARG otherwise (and the model holds no path). */
int furrow_init(furrow_t *f, zxm_link_t *link, const char *path, int64_t sheet);
/* Read the sheet; a sheet not made yet is an empty sheet (ZXM_OK). */
int furrow_load(furrow_t *f);

int furrow_set_number(furrow_t *f, int64_t row, int64_t col, int64_t value);
/* Money from whole units and hundredths (money_cell, FW2). */
int furrow_set_money(furrow_t *f, int64_t row, int64_t col, int64_t units, int64_t cents);
int furrow_set_formula(furrow_t *f, int64_t row, int64_t col, int64_t dir, int64_t line, int64_t lo,
                       int64_t hi);
/* Empty a cell: its record goes (FW4); emptying an empty cell is ZXM_OK. */
int furrow_clear(furrow_t *f, int64_t row, int64_t col);

/* A cell's value and kind, a formula evaluated (FM5); an empty cell is the
 * number 0. */
int furrow_value(const furrow_t *f, int64_t row, int64_t col, int64_t *value, int32_t *kind);
/* A range's total and kind (FM5): rows lo..hi of column line, or columns
 * lo..hi of row line. */
int furrow_sum(const furrow_t *f, int64_t dir, int64_t line, int64_t lo, int64_t hi, int64_t *total,
               int32_t *kind);
/* The exact mean of the cells a range holds (its total over how many),
 * reduced; ZXM_E_STATE when it holds none. */
int furrow_mean(const furrow_t *f, int64_t dir, int64_t line, int64_t lo, int64_t hi, rat_t *mean);
/* The sheet's money in all: every money cell, coffer.holdings (FW5). */
int furrow_money_total(const furrow_t *f, int64_t *total);
/* Minor units as text, "-12.34"; the length, or -1 when it does not fit. */
int32_t furrow_money_text(int64_t minor, char *out, uint32_t cap);

#endif
