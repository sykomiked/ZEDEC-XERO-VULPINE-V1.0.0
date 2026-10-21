/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* solstice_rules.h — Solstice's rules in C, the reference its FORTRAN 77,
 * BASIC, Lingua IR, KRON, WebAssembly, Eve and MYRA blocks are checked
 * against (blocks/solstice). Pure integer functions, no state: the model
 * (solstice_model.c) and the language test (tests/solstice_zxapp.inc) both
 * include this, so the languages are tested against the very C the model
 * runs. Every rule answers SOLSTICE_REFUSED (-1) outside its domain.
 *
 *   SO1 DAYS IN A MONTH of the Gregorian calendar, year 1 ..
 *       SOLSTICE_YEAR_MAX, month 1..12: 31 for months 1, 3, 5, 7, 8, 10,
 *       12, 30 for 4, 6, 9, 11, and February 29 in a year divisible by 4
 *       except a century not divisible by 400, else 28.
 *   SO2 DAY OF THE WEEK of a date, 1 Monday .. 7 Sunday (ISO 8601), by
 *       Zeller's congruence: January and February are months 13 and 14
 *       of the year before, h = (day + floor(13 * (month + 1) / 5) + k +
 *       floor(k / 4) - floor(k / 100) + floor(k / 400)) mod 7 (0 is
 *       Saturday), and the weekday is (h + 5) mod 7 + 1. The day must be
 *       inside its month (SO1).
 *   SO3 NEXT OCCURRENCE of an event repeating every `every` days from day
 *       `start`, seen on day `today`, all days since 1970-01-01: the first
 *       start + k * every (k >= 0) on or after today. start and today are
 *       0 .. SOLSTICE_DAY_MAX, every 1 .. SOLSTICE_DAY_MAX.
 *   SO4 TWO EVENTS OVERLAP, each from its start up to, not including, its
 *       finish: given as margins, aover = the first's finish minus the
 *       second's start and bover = the second's finish minus the first's
 *       start, they overlap when both are 1 .. SOLSTICE_SECONDS_MAX.
 *   SO5 AN EVENT RECORD of ^ZXEVENT holds integers of at most the digits
 *       its pieces declare, sign aside: event 9, start 12, finish 12,
 *       every 7, alarm 4 (blocks/solstice/event.myra). The model's own
 *       check (solstice_event_valid) is narrower.
 *
 * Kept in C, since no runnable language owns calendar conversion: a day
 * number from a date and back (days_from_civil and its inverse, on the
 * proleptic Gregorian calendar). */
#ifndef SOLSTICE_RULES_H
#define SOLSTICE_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define SOLSTICE_REFUSED     (-1)
#define SOLSTICE_YEAR_MAX    INT64_C(999999999999999999) /* 18 digits: BASIC's numbers */
#define SOLSTICE_YEAR32_MAX  999999999 /* FORTRAN 77's 32-bit INTEGER: nine digits */
#define SOLSTICE_DAY_S       86400
#define SOLSTICE_DAY_MAX     2932896               /* 9999-12-31, in days since 1970 */
#define SOLSTICE_SECONDS_MAX INT64_C(253402300799) /* its last second */

/* SO1 */
static inline int64_t solstice_month_days(int64_t year, int64_t month)
{
    if (year < 1 || year > SOLSTICE_YEAR_MAX || month < 1 || month > 12) return SOLSTICE_REFUSED;
    if (month != 2) return 30 + (month + month / 8) % 2;
    if (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) return 29;
    return 28;
}

/* SO2 */
static inline int64_t solstice_weekday(int64_t year, int64_t month, int64_t day)
{
    int64_t last = solstice_month_days(year, month);
    if (last < 0 || day < 1 || day > last) return SOLSTICE_REFUSED;
    int64_t k = year;
    if (month < 3) {
        month += 12;
        k -= 1;
    }
    int64_t h = (day + 13 * (month + 1) / 5 + k + k / 4 - k / 100 + k / 400) % 7;
    return (h + 5) % 7 + 1;
}

/* SO3 */
static inline int64_t solstice_next(int64_t start, int64_t every, int64_t today)
{
    if (start < 0 || start > SOLSTICE_DAY_MAX || every < 1 || every > SOLSTICE_DAY_MAX ||
        today < 0 || today > SOLSTICE_DAY_MAX)
        return SOLSTICE_REFUSED;
    if (today <= start) return start;
    return start + (today - start + every - 1) / every * every;
}

/* SO4 */
static inline bool solstice_overlap(int64_t aover, int64_t bover)
{
    return aover >= 1 && aover <= SOLSTICE_SECONDS_MAX && bover >= 1 &&
           bover <= SOLSTICE_SECONDS_MAX;
}

/* SO5: does x have at most `digits` digits, sign aside? */
static inline bool solstice_digits_fit(int64_t x, uint32_t digits)
{
    int64_t lim = 1;
    for (uint32_t i = 0; i < digits && i < 18u; i++) lim *= 10;
    return x > -lim && x < lim;
}

static inline bool solstice_record_fits(int64_t event, int64_t start, int64_t finish, int64_t every,
                                        int64_t alarm)
{
    return solstice_digits_fit(event, 9) && solstice_digits_fit(start, 12) &&
           solstice_digits_fit(finish, 12) && solstice_digits_fit(every, 7) &&
           solstice_digits_fit(alarm, 4);
}

/* Days since 1970-01-01 of a date of years 1 .. 9999 (the range a time in
 * seconds holds, SOLSTICE_SECONDS_MAX), or INT64_MIN when it is no such
 * date (a date before 1970 is a negative day, never refused). */
static inline int64_t solstice_day_of(int64_t year, int64_t month, int64_t day)
{
    if (year > 9999 || solstice_weekday(year, month, day) < 0) return INT64_MIN;
    int64_t y = month <= 2 ? year - 1 : year;
    int64_t era = y / 400; /* y >= 0 */
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/* The date of a day 0 .. SOLSTICE_DAY_MAX since 1970-01-01. */
static inline bool solstice_date_of(int64_t days, int64_t *year, int64_t *month, int64_t *day)
{
    if (days < 0 || days > SOLSTICE_DAY_MAX || !year || !month || !day) return false;
    int64_t z = days + 719468;
    int64_t era = z / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    *day = doy - (153 * mp + 2) / 5 + 1;
    *month = mp < 10 ? mp + 3 : mp - 9;
    *year = yoe + era * 400 + (*month <= 2);
    return true;
}

#endif
