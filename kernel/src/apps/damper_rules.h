/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* damper_rules.h — Damper's rules in C, the reference its Eve, KRON,
 * Trebago and MYRA blocks are checked against (blocks/damper). Pure
 * integer functions, no state: the model (damper_model.c) and the
 * language test (tests/damper_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs. Temperatures
 * are tenths of a degree C (570 = 57.0 C), times are seconds.
 *
 *   DR1 ONE SENSOR QUIET (fire_alarm_quiet): its hottest reading of its
 *       latest second at or under its limit, as margin = limit - reading,
 *       0..DAMPER_MARGIN_MAX; no smoke reported (smoke 0); no reading of
 *       that second that is BOTH (split 0). THE ALARM IS RAISED when a
 *       sensor with readings is not quiet: over its limit OR smoke
 *       reported OR a reading that is BOTH. A margin past
 *       DAMPER_MARGIN_MAX or a flag below 0 is no reading (Eve refuses,
 *       KRON reads it as quiet: held).
 *   DR2 TEMPERATURE RISE (heat_rise_excess): over the last readings
 *       temp[0..n-1] at stamp[0..n-1], 60 * (temp[n-1] - temp[0]) /
 *       (stamp[n-1] - stamp[0]) - rate tenths of a degree a minute, an
 *       exact reduced fraction; above 0 it climbs faster than the declared
 *       rate. Refused for n outside 2..DAMPER_RISE_MAX, a last stamp not
 *       after the first, a rate outside DR3's, a reading outside
 *       DAMPER_TEMP_MIN..MAX or a stamp outside 0..DAMPER_STAMP_MAX.
 *   DR3 A SENSOR'S LIMITS (heat_sensor_ok): limit
 *       DAMPER_LIMIT_MIN..MAX (40.0..150.0 C), declared rate
 *       DAMPER_RATE_MIN..MAX (1.0..30.0 C a minute).
 *   DR4 THE SENSOR GRID (room_alarm_sensors), rooms by sensors, one
 *       sensor a row (room, raised): row i's cell, the raised count of
 *       every row in its room, itself included, exact.
 *   DR5 A ROOM OF THE BUILDING TREE (building_room_ok) fits
 *       ^ZXDAMPER(floor, room) = toward ^ exits: floor at most 2 digits,
 *       room and toward 3, exits 1, sign aside (MYRA's i-fields).
 *   DR6 ONE SECOND, MANY REPORTS. Each report of a sensor's latest second
 *       says of the sensor: over its limit (TRUE or FALSE), smoke (TRUE,
 *       FALSE, or NEITHER when the sensor has no smoke head) and fire
 *       (over OR smoke). lpres_kjoin joins the reports of that second: a
 *       report of smoke and one of none is BOTH, never resolved by picking
 *       one; split is 1 when the smoke or the heat join is BOTH. The
 *       building's fire state is lpres_disjoin of its sensors' (the
 *       kernel's chain, F < N < B < T): TRUE when one is on fire, BOTH
 *       when one is contradicted, so the alarm is raised for TRUE and
 *       BOTH alike.
 *
 * Anything else is refused (DAMPER_REFUSED), never rounded or wrapped. */
#ifndef DAMPER_RULES_H
#define DAMPER_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "../rational/rational.h"
#include "lpres.h"

#define DAMPER_REFUSED    INT64_MIN
#define DAMPER_TEMP_MIN   INT64_C(-400) /* a reading: -40.0 C */
#define DAMPER_TEMP_MAX   INT64_C(1500) /* 150.0 C */
#define DAMPER_LIMIT_MIN  INT64_C(400)  /* DR3 */
#define DAMPER_LIMIT_MAX  INT64_C(1500)
#define DAMPER_RATE_MIN   INT64_C(10) /* DR3: tenths of a degree a minute */
#define DAMPER_RATE_MAX   INT64_C(300)
#define DAMPER_MARGIN_MAX (DAMPER_LIMIT_MAX - DAMPER_TEMP_MIN) /* DR1: 1900 */
#define DAMPER_RISE_MAX   64u                                  /* DR2: readings */
#define DAMPER_STAMP_MAX  INT64_C(999999999999)                /* DR2: seconds */
#define DAMPER_SMOKE_NONE (-1) /* DR6: a report with no smoke head */

static inline bool damper_in(int64_t v, int64_t lo, int64_t hi)
{
    return v >= lo && v <= hi;
}

/* DR1 */
static inline bool damper_quiet(int64_t margin, int64_t smoke, int64_t split)
{
    return damper_in(margin, 0, DAMPER_MARGIN_MAX) && smoke == 0 && split == 0;
}

/* DR3 */
static inline bool damper_limits_ok(int64_t limit, int64_t rate)
{
    return damper_in(limit, DAMPER_LIMIT_MIN, DAMPER_LIMIT_MAX) &&
           damper_in(rate, DAMPER_RATE_MIN, DAMPER_RATE_MAX);
}

/* DR2: false when refused; *r the excess, exact */
static inline bool damper_rise_excess(const int64_t *temp, const int64_t *stamp, uint32_t n,
                                      int64_t rate, rat_t *r)
{
    if (!temp || !stamp || !r || n < 2u || n > DAMPER_RISE_MAX ||
        !damper_in(rate, DAMPER_RATE_MIN, DAMPER_RATE_MAX))
        return false;
    for (uint32_t i = 0; i < n; i++)
        if (!damper_in(temp[i], DAMPER_TEMP_MIN, DAMPER_TEMP_MAX) ||
            !damper_in(stamp[i], 0, DAMPER_STAMP_MAX))
            return false;
    int64_t span = stamp[n - 1u] - stamp[0];
    if (span <= 0) return false;
    /* each term fits: 60 * 1900 and 300 * 10^12 */
    *r = rat_make(60 * (temp[n - 1u] - temp[0]) - rate * span, span);
    return r->valid;
}

/* DR4: row i's cell, or DAMPER_REFUSED when the sum passes 64 bits */
static inline int64_t damper_room_cell(const int64_t *room, const int64_t *raised, uint32_t n,
                                       uint32_t i)
{
    int64_t s = 0;
    if (!room || !raised || i >= n) return DAMPER_REFUSED;
    for (uint32_t j = 0; j < n; j++) {
        if (room[j] != room[i]) continue;
        if ((raised[j] > 0 && s > INT64_MAX - raised[j]) ||
            (raised[j] < 0 && s < INT64_MIN + 1 - raised[j]))
            return DAMPER_REFUSED;
        s += raised[j];
    }
    return s;
}

/* DR5: |v| has at most `digits` digits (1..18) */
static inline bool damper_digits(int64_t v, int digits)
{
    int64_t top = 1;
    for (int k = 0; k < digits && k < 18; k++) top *= 10;
    return v > -top && v < top;
}

static inline bool damper_room_ok(int64_t floor, int64_t room, int64_t toward, int64_t exits)
{
    return damper_digits(floor, 2) && damper_digits(room, 3) && damper_digits(toward, 3) &&
           damper_digits(exits, 1);
}

/* DR6: what one report says */
static inline lpres_state_t damper_said(bool yes)
{
    return yes ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
}

static inline lpres_state_t damper_smoke_said(int64_t smoke)
{
    if (smoke == DAMPER_SMOKE_NONE) return LPRES_STATE_NEITHER;
    return damper_said(smoke == 1);
}

/* DR6: the alarm, from a sensor's or the building's fire state */
static inline bool damper_raised(lpres_state_t fire)
{
    return fire == LPRES_STATE_TRUE || fire == LPRES_STATE_BOTH;
}

#endif
