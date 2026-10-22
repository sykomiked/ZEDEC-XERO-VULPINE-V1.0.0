/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* escapement_rules.h — Escapement's rules in C, the reference its Trebago,
 * KRON, Eve, Sutra and MYRA blocks are checked against (blocks/escapement),
 * beside the rules it composes from nearer the core. Pure integer
 * functions, no state: the model (escapement_model.c) and the language test
 * (tests/escapement_zxapp.inc) both include this.
 *
 *   ER1 CLOCK OFFSET (offset_median): the offsets of n devices'
 *       clocks from this one's, in ms, read at one instant; the vote is
 *       their lower median, the smallest offset v with 2 * count(offset <=
 *       v) >= n: for an odd n the middle one, for an even n the lower of
 *       the two middle ones, so the vote is always an offset some clock
 *       really showed, never an average none did. n 1..256 (net.peers
 *       lists at most 256), each offset within a day either way
 *       (-86400000..86400000 ms), else refused.
 *   ER2 JOB MAY RUN (job_may_run): a timed job may run when it is inside
 *       its window and its deadline has not passed, given as margins from
 *       now in ms: opened (now - the window's opening) 0 or more, closes
 *       (the window's close - now) 1 or more, due (the deadline - now) 0
 *       or more, each at most ESCAPEMENT_SPAN_MAX (366 days); a margin
 *       past that is no window the owner set.
 *   ER3 TIMESHEET (timesheet_week): one shift of one person, minutes
 *       of the week (0 Monday 00:00 .. 10080, the week's end), worked
 *       finish - start - unpaid (the unpaid break, 0..999 minutes), never
 *       below 0, added to prior (the week's minutes so far, 0 or more):
 *       the week's minutes after it, refused past 10080, the minutes a
 *       week has (an overflow), never wrapped or cut.
 *   ER4 SCHEDULE (shift_clashes): row i's count of other rows of the same
 *       person whose shift overlaps its own, as solstice.events_overlap
 *       says two spans do (each from its start up to, not including, its
 *       finish; the margins in seconds); 0 means nobody is booked twice
 *       at once. Minutes of the week, -10^9..10^9.
 *   ER5 SHIFT NODE (shift_node_ok) fits ^ZXSHIFT: person 6 digits, week 4,
 *       shift 3, start, finish and worked 5, sign aside.
 *   ER6 CLOCK STATE (lpres_state_t): the clocks of n devices read at one
 *       instant, as offsets from this one's: NEITHER with none, TRUE when
 *       the earliest and the latest are within tol ms of each other, BOTH
 *       when they are not (two clocks say two times). A BOTH is never
 *       settled by overwriting a clock: with ESCAPEMENT_WITNESSES devices
 *       or more ER1's median is the vote, which the owner confirms; with
 *       fewer there is no vote and the time stays BOTH. An offset past
 *       ER1's day is no reading: NEITHER.
 *   Composed, as the parts' own rules: treadle.next_run, treadle.budget_ok
 *   and treadle.task_ok (treadle_rules.h TR1-TR3), solstice.weekday and
 *   solstice.events_overlap (solstice_rules.h SO2, SO4),
 *   tallystick.next_doc_number (tallystick_rules.h TR3) and
 *   sundial.zone_minute (sundial_rules.h SD1). */
#ifndef ESCAPEMENT_RULES_H
#define ESCAPEMENT_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "lpres.h"
#include "solstice_rules.h"

#define ESCAPEMENT_REFUSED     INT64_MIN
#define ESCAPEMENT_PEERS_MAX   256u                 /* ER1: net.peers lists at most 256 */
#define ESCAPEMENT_OFFSET_MAX  INT64_C(86400000)    /* ER1: a day, ms */
#define ESCAPEMENT_SPAN_MAX    INT64_C(31622400000) /* ER2: 366 days, ms */
#define ESCAPEMENT_WEEK_MIN    10080                /* ER3: minutes in a week */
#define ESCAPEMENT_UNPAID_MAX  999                  /* ER3: Sutra's 9(3) */
#define ESCAPEMENT_MINUTE_SPAN INT64_C(1000000000)  /* ER4: minutes either way */
#define ESCAPEMENT_WITNESSES   3u                   /* ER6: devices for a vote */

/* ER1: the lower median of n offsets into *out; false when refused. */
static inline bool escapement_offset_median(const int64_t *off, uint32_t n, int64_t *out)
{
    if (!off || !out || n < 1u || n > ESCAPEMENT_PEERS_MAX) return false;
    for (uint32_t i = 0; i < n; i++)
        if (off[i] < -ESCAPEMENT_OFFSET_MAX || off[i] > ESCAPEMENT_OFFSET_MAX) return false;
    bool found = false;
    int64_t best = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t le = 0;
        for (uint32_t k = 0; k < n; k++) le += off[k] <= off[i];
        if (2u * le >= n && (!found || off[i] < best)) {
            best = off[i];
            found = true;
        }
    }
    *out = best;
    return found;
}

/* ER2 */
static inline bool escapement_job_may_run(int64_t opened, int64_t closes, int64_t due)
{
    return opened >= 0 && opened <= ESCAPEMENT_SPAN_MAX && closes >= 1 &&
           closes <= ESCAPEMENT_SPAN_MAX && due >= 0 && due <= ESCAPEMENT_SPAN_MAX;
}

/* ER3: the week's minutes after this shift, or ESCAPEMENT_REFUSED. */
static inline int64_t escapement_week_after(int64_t start, int64_t finish, int64_t unpaid,
                                            int64_t prior)
{
    if (start < 0 || finish < 0 || finish > ESCAPEMENT_WEEK_MIN || unpaid < 0 ||
        unpaid > ESCAPEMENT_UNPAID_MAX || prior < 0 || prior > ESCAPEMENT_WEEK_MIN)
        return ESCAPEMENT_REFUSED;
    int64_t worked = finish - start - unpaid;
    if (worked < 0 || prior + worked > ESCAPEMENT_WEEK_MIN) return ESCAPEMENT_REFUSED;
    return prior + worked;
}

/* ER4: row i's clashes among n rows; -1 when refused. */
static inline int64_t escapement_clashes(const int64_t *person, const int64_t *start,
                                         const int64_t *finish, uint32_t n, uint32_t i)
{
    if (!person || !start || !finish || i >= n) return -1;
    for (uint32_t k = 0; k < n; k++)
        if (start[k] < -ESCAPEMENT_MINUTE_SPAN || start[k] > ESCAPEMENT_MINUTE_SPAN ||
            finish[k] < -ESCAPEMENT_MINUTE_SPAN || finish[k] > ESCAPEMENT_MINUTE_SPAN)
            return -1;
    int64_t c = 0;
    for (uint32_t j = 0; j < n; j++)
        if (j != i && person[j] == person[i] &&
            solstice_overlap((finish[i] - start[j]) * 60, (finish[j] - start[i]) * 60))
            c++;
    return c;
}

/* ER5: does x have at most `digits` digits, sign aside? */
static inline bool escapement_digits_fit(int64_t x, uint32_t digits)
{
    int64_t lim = 1;
    for (uint32_t i = 0; i < digits && i < 18u; i++) lim *= 10;
    return x > -lim && x < lim;
}

static inline bool escapement_node_ok(int64_t person, int64_t week, int64_t shift, int64_t start,
                                      int64_t finish, int64_t worked)
{
    return escapement_digits_fit(person, 6) && escapement_digits_fit(week, 4) &&
           escapement_digits_fit(shift, 3) && escapement_digits_fit(start, 5) &&
           escapement_digits_fit(finish, 5) && escapement_digits_fit(worked, 5);
}

/* ER6: the clocks' state; *vote and *voted the median when there is one. */
static inline lpres_state_t escapement_clock_state(const int64_t *off, uint32_t n, int64_t tol,
                                                   int64_t *vote, bool *voted)
{
    if (voted) *voted = false;
    if (!off || n == 0u || tol < 0 || !vote || !voted) return LPRES_STATE_NEITHER;
    for (uint32_t i = 0; i < n; i++)
        if (off[i] < -ESCAPEMENT_OFFSET_MAX || off[i] > ESCAPEMENT_OFFSET_MAX)
            return LPRES_STATE_NEITHER; /* no clock reads that far off */
    int64_t lo = off[0], hi = off[0];
    for (uint32_t i = 1; i < n; i++) {
        if (off[i] < lo) lo = off[i];
        if (off[i] > hi) hi = off[i];
    }
    if (n >= ESCAPEMENT_WITNESSES) *voted = escapement_offset_median(off, n, vote);
    if (hi - lo <= tol) return LPRES_STATE_TRUE;
    return LPRES_STATE_BOTH;
}

#endif
