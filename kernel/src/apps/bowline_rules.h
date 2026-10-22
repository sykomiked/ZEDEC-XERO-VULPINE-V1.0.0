/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* bowline_rules.h — Bowline's rules in C, the reference its BASIC, KRON,
 * Eve and MYRA blocks are checked against (blocks/bowline). Pure integer
 * functions, no state: the model (bowline_model.c) and the language test
 * (tests/bowline_zxapp.inc) both include this, so the languages are tested
 * against the very C the model runs. Days are days since 1970-01-01 (a
 * Thursday), 0 .. SOLSTICE_DAY_MAX (9999-12-31).
 *
 *   BW1 A DAY PAST THE SKIPPED WEEKDAYS: the first day on or after `day`
 *       whose weekday is not in `skip`, a mask with bit 0 Monday .. bit 6
 *       Sunday. day is 0 .. SOLSTICE_DAY_MAX and skip 0 .. 126 (a mask of
 *       every day leaves no day); the answer is at most 6 days later and
 *       may not pass SOLSTICE_DAY_MAX. Anything else is BOWLINE_REFUSED.
 *   BW2 OVERDUE: a task is overdue when it is still open and at least one
 *       day late, given as its margin late = today - due: late 1 ..
 *       SOLSTICE_DAY_MAX and open 1. A margin past SOLSTICE_DAY_MAX is no
 *       measurement, so it is not overdue.
 *   BW3 A TASK RECORD of ^ZXTASK holds integers of at most the digits its
 *       pieces declare, sign aside: task 9, list 3, start 7, due 7, every
 *       4, unit 1, skip 3, done 1 (blocks/bowline/task.myra). The model's
 *       own check (bowline_task_valid) is narrower.
 *
 * Reused, never written again: when a repeating task next falls is
 * Solstice's next occurrence (solstice_rules.h SO3), and how many tasks a
 * list holds is Witness's count of a kind (witness_rules.h WR2). */
#ifndef BOWLINE_RULES_H
#define BOWLINE_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "solstice_rules.h"
#include "witness_rules.h"

#define BOWLINE_REFUSED   (-1)
#define BOWLINE_SKIP_MAX  126  /* every weekday but one may be skipped */
#define BOWLINE_LIST_MAX  999  /* three digits (^ZXTASK) */
#define BOWLINE_EVERY_MAX 9999 /* four digits (^ZXTASK) */

/* The weekday of a day since 1970, 0 Monday .. 6 Sunday (day 0 is a Thursday). */
static inline int64_t bowline_weekday0(int64_t day)
{
    return (day + 3) % 7;
}

/* BW1 */
static inline int64_t bowline_unskipped(int64_t day, int64_t skip)
{
    if (day < 0 || day > SOLSTICE_DAY_MAX || skip < 0 || skip > BOWLINE_SKIP_MAX)
        return BOWLINE_REFUSED;
    for (int64_t k = 0; k < 7; k++) {
        if (((skip >> bowline_weekday0(day + k)) & 1) == 0)
            return day + k > SOLSTICE_DAY_MAX ? BOWLINE_REFUSED : day + k;
    }
    return BOWLINE_REFUSED; /* not reached: skip < 127 leaves a weekday */
}

/* BW2 */
static inline bool bowline_overdue(int64_t late, int64_t open)
{
    return late >= 1 && late <= SOLSTICE_DAY_MAX && open == 1;
}

/* BW3 */
static inline bool bowline_record_fits(int64_t task, int64_t list, int64_t start, int64_t due,
                                       int64_t every, int64_t unit, int64_t skip, int64_t done)
{
    return witness_digits_fit(task, 9) && witness_digits_fit(list, 3) &&
           witness_digits_fit(start, 7) && witness_digits_fit(due, 7) &&
           witness_digits_fit(every, 4) && witness_digits_fit(unit, 1) &&
           witness_digits_fit(skip, 3) && witness_digits_fit(done, 1);
}

#endif
