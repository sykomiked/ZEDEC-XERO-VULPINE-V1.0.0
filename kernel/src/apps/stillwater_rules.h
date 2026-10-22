/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* stillwater_rules.h — Stillwater's rules in C, the reference its Sutra,
 * KRON, Eve and MYRA blocks are checked against (blocks/stillwater). Pure
 * integer functions, no state: the model (stillwater_model.c) and the
 * language test (tests/stillwater_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 *   SW1 A WINDOW is minutes of the day, 0..1439, from `from` up to, not
 *       including, `to`. from < to: from <= now < to. from > to: it
 *       crosses midnight, now >= from or now < to. from == to: the whole
 *       day. A minute outside 0..1439 is in no window. zxvd keeps the
 *       same rule for an app's hours (zxv_screen.c).
 *   SW2 THROUGH: a Watchfire emergency (above 0) always goes through,
 *       whatever else is said. Otherwise a notice goes through when it is
 *       not quiet (quiet below 1), its sender is allowed (above 0), or the
 *       same caller called before within STILLWATER_REPEAT_S seconds (gap
 *       0..180; -1: no earlier call).
 *   SW3 A SCHEDULE may hold mode 0..2, days 0..127 and from, to 0..1439
 *       (blocks/stillwater/schedule.eve).
 *   SW4 A RECORD of ^ZXQUIET holds integers of at most the digits its
 *       pieces declare, sign aside (blocks/stillwater/schedule.myra). */
#ifndef STILLWATER_RULES_H
#define STILLWATER_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define STILLWATER_DAY_MIN  1440 /* minutes in a day */
#define STILLWATER_REPEAT_S 180  /* the same caller twice within 3 minutes */

enum { STILLWATER_OFF = 0, STILLWATER_QUIET = 1, STILLWATER_FOCUS = 2, STILLWATER_MODES = 3 };

static inline bool stillwater_minute_ok(int64_t m)
{
    return m >= 0 && m < STILLWATER_DAY_MIN;
}

/* SW1: is minute `now` inside the window from..to? */
static inline bool stillwater_window(int64_t from, int64_t to, int64_t now)
{
    if (!stillwater_minute_ok(from) || !stillwater_minute_ok(to) || !stillwater_minute_ok(now))
        return false;
    if (from == to) return true;
    if (from < to) return now >= from && now < to;
    return now >= from || now < to;
}

/* SW2: let this notice through? The emergency is read first and alone. */
static inline bool stillwater_through(int64_t quiet, int64_t allowed, int64_t gap,
                                      int64_t emergency)
{
    if (emergency > 0) return true;
    return quiet < 1 || allowed > 0 || (gap >= 0 && gap <= STILLWATER_REPEAT_S);
}

/* SW3: may a schedule say this? */
static inline bool stillwater_schedule_fits(int64_t mode, int64_t days, int64_t from, int64_t to)
{
    return mode >= 0 && mode < STILLWATER_MODES && days >= 0 && days <= 127 &&
           stillwater_minute_ok(from) && stillwater_minute_ok(to);
}

/* SW4: does x have at most `digits` digits, sign aside? */
static inline bool stillwater_digits_fit(int64_t x, uint32_t digits)
{
    int64_t lim = 1;
    for (uint32_t i = 0; i < digits && i < 18u; i++) lim *= 10;
    return x > -lim && x < lim;
}

static inline bool stillwater_record_fits(int64_t schedule, int64_t mode, int64_t days,
                                          int64_t from, int64_t to, int64_t allowed)
{
    return stillwater_digits_fit(schedule, 3) && stillwater_digits_fit(mode, 1) &&
           stillwater_digits_fit(days, 3) && stillwater_digits_fit(from, 4) &&
           stillwater_digits_fit(to, 4) && stillwater_digits_fit(allowed, 3);
}

#endif
