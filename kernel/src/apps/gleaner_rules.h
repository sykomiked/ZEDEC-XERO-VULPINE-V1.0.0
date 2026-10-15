/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* gleaner_rules.h — Gleaner's rules in C, the reference its C++, KRON, Eve,
 * Sutra and MYRA blocks are checked against (blocks/gleaner). Pure integer
 * functions, no state: the model (gleaner_model.c) and the language test
 * (tests/gleaner_zxapp.inc) both include this, so the languages are tested
 * against the very C the model runs. The count of rows per class is
 * Witness's (witness_rules.h WR2, witness.kind_count), never written here.
 *
 *   GR1 THE HOLDOUT. Row `row` (0, 1, 2... in file order) of a dataset goes
 *       to the test set when floor((row + 1) * share / 10000) is past
 *       floor(row * share / 10000), share being the test set's share in
 *       hundredths of a percent; every other row is a training row. Every
 *       row lands in exactly one set, the test rows are spread evenly
 *       through the file, and the first n rows hold exactly
 *       floor(n * share / 10000) test rows. 1 test, 0 train; a row outside
 *       0..999999999 or a share outside 0..10000 is refused.
 *   GR2 A ROW MATCHES A FILTER over its columns when its label class is
 *       lab (lab negative: any label), its source is src (src negative: any
 *       source) and its date is inside from..to, both ends included.
 *   GR3 A DATASET'S SIZE fits when it has 1..GLEANER_ROWS rows, 3..
 *       GLEANER_COLS columns (label, source and date at least) and
 *       1..GLEANER_BYTES bytes (zxvd's limit for one block put by aipi.put,
 *       ZXVD_AIPI_HEX_MAX).
 *   GR4 A ROW RECORD of ^ZXGLEAN holds integers of at most the digits its
 *       pieces declare, sign aside (blocks/gleaner/row.myra): row 3,
 *       label 2, source 2, date 8 (YYYYMMDD), pii 1.
 *   GR5 PERSONAL DATA LEAVES THE HOARD only with the owner's consent: what
 *       would leave may go when it holds no personal row (personal = 0) or
 *       the owner said yes (consent = 1); a negative count is no count. */
#ifndef GLEANER_RULES_H
#define GLEANER_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "witness_rules.h"

#define GLEANER_ROWS      1000      /* GR3 */
#define GLEANER_COLS      32        /* GR3 */
#define GLEANER_BYTES     8000      /* GR3: one aipi.put block */
#define GLEANER_SHARE_MAX 10000     /* GR1: 100.00 % */
#define GLEANER_ROW_MAX   999999999 /* GR1 */
#define GLEANER_REFUSED   (-1)

/* GR1 */
static inline int64_t gleaner_holdout(int64_t row, int64_t share)
{
    if (row < 0 || row > GLEANER_ROW_MAX || share < 0 || share > GLEANER_SHARE_MAX)
        return GLEANER_REFUSED;
    return (row + 1) * share / GLEANER_SHARE_MAX - row * share / GLEANER_SHARE_MAX;
}

/* GR2 */
static inline bool gleaner_row_match(int64_t label, int64_t source, int64_t date, int64_t lab,
                                     int64_t src, int64_t from, int64_t to)
{
    return (lab < 0 || label == lab) && (src < 0 || source == src) && date >= from && date <= to;
}

/* GR3 */
static inline bool gleaner_dataset_ok(int64_t rows, int64_t cols, int64_t bytes)
{
    return rows >= 1 && rows <= GLEANER_ROWS && cols >= 3 && cols <= GLEANER_COLS && bytes >= 1 &&
           bytes <= GLEANER_BYTES;
}

/* GR4 */
static inline bool gleaner_row_fits(int64_t row, int64_t label, int64_t source, int64_t date,
                                    int64_t pii)
{
    return witness_digits_fit(row, 3) && witness_digits_fit(label, 2) &&
           witness_digits_fit(source, 2) && witness_digits_fit(date, 8) &&
           witness_digits_fit(pii, 1);
}

/* GR5 */
static inline bool gleaner_leave_ok(int64_t personal, int64_t consent)
{
    return personal == 0 || (personal > 0 && consent == 1);
}

#endif
