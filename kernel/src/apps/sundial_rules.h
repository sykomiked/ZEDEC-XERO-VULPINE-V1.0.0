/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sundial_rules.h — the rules Sundial's languages witness, in C: what
 * sundial_model.c runs and what tests/sundial_zxapp.inc checks the blocks
 * against (blocks/sundial/). Integers only; every input outside its domain
 * is refused (SUNDIAL_REFUSED), never wrapped or rounded.
 *
 *   SD1 LOCAL MINUTE. The minute of the day in a zone: utc + offset wrapped
 *       into 0..1439, utc the minute of the day in UTC (0..1439) and offset
 *       the zone's offset from UTC in minutes (-720..840, UTC-12:00 to
 *       UTC+14:00). Trebago and KRON (zone_minute).
 *   SD2 MINUTES UNTIL AN ALARM. From minute now (0..1439) of day today (1
 *       Monday .. 7 Sunday, ISO 8601, solstice.weekday's numbering) to the
 *       first ring at or after now of an alarm at minute at (0..1439) on
 *       the days of mask (1..127, bit 0 Monday .. bit 6 Sunday): 0 when it
 *       rings this minute, at most 7 * 1440 - 1. A mask of 0 has no day and
 *       is refused. Trebago and KRON (alarm_wait).
 *   SD3 LIMITS. At most 64 alarms, a snooze of 1..60 minutes and a timer
 *       of 0..5999 minutes, never below zero: clock_limits_ok (Eve and
 *       Sutra) says whether a setting is inside them, clock_limits_fault
 *       (Rust as WebAssembly, and KRON) which limit it passes first: 1 the
 *       alarms, 2 the snooze, 3 the timer, 0 none. A count below zero is no
 *       measurement: refused.
 *   SD4 ZONE OFFSETS. The 38 standard offsets from UTC in use, ascending,
 *       by zone index 0..37 (14 is UTC). Daylight saving is not applied:
 *       zxvd has no time-zone method. KRON (zone_offset). */
#ifndef SUNDIAL_RULES_H
#define SUNDIAL_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define SUNDIAL_REFUSED    INT64_MIN
#define SUNDIAL_DAY_MIN    1440
#define SUNDIAL_WEEK_MIN   10080
#define SUNDIAL_OFFSET_MIN (-720)
#define SUNDIAL_OFFSET_MAX 840
#define SUNDIAL_ZONES      38
#define SUNDIAL_ZONE_UTC   14
#define SUNDIAL_ALARMS_MAX 64
#define SUNDIAL_SNOOZE_MIN 1
#define SUNDIAL_SNOOZE_MAX 60
#define SUNDIAL_TIMER_MAX  5999 /* minutes: 99:59 in hours and minutes */

/* SD4 */
static inline int64_t sundial_zone_offset(int64_t zone)
{
    static const int16_t OFF[SUNDIAL_ZONES] = {
        -720, -660, -600, -570, -540, -480, -420, -360, -300, -240, -210, -180, -120,
        -60,  0,    60,   120,  180,  210,  240,  270,  300,  330,  345,  360,  390,
        420,  480,  525,  540,  570,  600,  630,  660,  720,  765,  780,  840};
    if (zone < 0 || zone >= SUNDIAL_ZONES) return SUNDIAL_REFUSED;
    return OFF[zone];
}

/* SD1 */
static inline int64_t sundial_zone_minute(int64_t utc, int64_t offset)
{
    if (utc < 0 || utc >= SUNDIAL_DAY_MIN || offset < SUNDIAL_OFFSET_MIN ||
        offset > SUNDIAL_OFFSET_MAX)
        return SUNDIAL_REFUSED;
    int64_t t = utc + offset;
    if (t < 0) return t + SUNDIAL_DAY_MIN;
    if (t >= SUNDIAL_DAY_MIN) return t - SUNDIAL_DAY_MIN;
    return t;
}

/* SD2 */
static inline int64_t sundial_alarm_wait(int64_t now, int64_t today, int64_t mask, int64_t at)
{
    if (now < 0 || now >= SUNDIAL_DAY_MIN || at < 0 || at >= SUNDIAL_DAY_MIN || today < 1 ||
        today > 7 || mask < 1 || mask > 127)
        return SUNDIAL_REFUSED;
    for (int64_t d = 0; d <= 7; d++) {
        int64_t wait = d * SUNDIAL_DAY_MIN + at - now;
        if (wait < 0 || !(mask >> ((today - 1 + d) % 7) & 1)) continue;
        return wait;
    }
    return SUNDIAL_REFUSED; /* not reached: day 7 is today again, and its bit is set by then */
}

/* SD3 */
static inline bool sundial_limits_ok(int64_t alarms, int64_t snooze, int64_t timer)
{
    return alarms >= 0 && alarms <= SUNDIAL_ALARMS_MAX && snooze >= SUNDIAL_SNOOZE_MIN &&
           snooze <= SUNDIAL_SNOOZE_MAX && timer >= 0 && timer <= SUNDIAL_TIMER_MAX;
}

static inline int64_t sundial_limits_fault(int64_t alarms, int64_t snooze, int64_t timer)
{
    if (alarms < 0 || snooze < 0 || timer < 0) return SUNDIAL_REFUSED;
    if (alarms > SUNDIAL_ALARMS_MAX) return 1;
    if (snooze < SUNDIAL_SNOOZE_MIN || snooze > SUNDIAL_SNOOZE_MAX) return 2;
    if (timer > SUNDIAL_TIMER_MAX) return 3;
    return 0;
}

#endif
