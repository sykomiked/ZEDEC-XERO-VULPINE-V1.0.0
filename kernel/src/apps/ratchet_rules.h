/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ratchet_rules.h — Ratchet's rules in C, the reference its Eve, KRON,
 * BASIC (Hatchling), Trebago, Sutra and MYRA blocks are checked against
 * (blocks/ratchet). Pure integer functions, no state: the model
 * (ratchet_model.c) and the language test (tests/ratchet_zxapp.inc) both
 * include this, so the languages are tested against the very C the model
 * runs.
 *
 *   RR1 A READING INSIDE ITS SAFE OPERATING LIMITS (machine_limits_ok):
 *       a reading of a part against the limits lo..hi of one measure,
 *       given as its two margins above = reading - lo and below = hi -
 *       reading, each 0..RATCHET_MARGIN_MAX (a range at most 999999 wide
 *       in the measure's own unit). TRUE is inside; FALSE is a fault. A
 *       margin past 999999 is no measurement (Eve refuses it, KRON reads
 *       any margin at or above 0 as inside: held).
 *   RR2 THE NEXT REPAIR STEP (machine_repair_step) from the check
 *       results, the first that applies: 0 nothing to repair (fault 0);
 *       1 isolate the power (not isolated, part not fixed); 2 repair or
 *       replace the part (power isolated, not fixed); 3 restore the power
 *       and run the self-check (fixed, not tested); 4 record the repair
 *       and schedule the next service (fixed and tested). Each result is
 *       a flag 0 or 1; anything else is refused (BASIC refuses, KRON
 *       reads a flag above 0 as 1 and below 0 as 0: held). Advice only:
 *       the person sees one step at a time and confirms it.
 *   RR3 WEAR (machine_wear): the exact sum over a part's run log of hours
 *       run times load share, hours 0..RATCHET_HOURS_MAX a row and load
 *       0..RATCHET_LOAD_FULL hundredths of a percent, in load-hours
 *       (10000 is one hour at full load); any row outside refuses the sum.
 *   RR4 HOURS TO THE NEXT SERVICE (service_hours_left): with a service
 *       due every `interval` full-load hours (1..999999), `worn` load-hours
 *       (RR3, 0..RATCHET_WORN_MAX) since the last and the part running at
 *       `load` (1..10000), the hours left at that load, exact:
 *       (interval * 10000 - worn) / load, a reduced fraction; below 0 the
 *       service is past due by that many hours.
 *   RR5 A REPAIR JOB LINE'S COST (repair_line_cost), in minor units:
 *       qty * price for the parts plus minutes * rate for the labour (the
 *       rate per minute), exact. Sutra's pictures: qty 9(4), price 9(9),
 *       minutes 9(5), rate 9(7); Trebago answers each field to 9 digits,
 *       so past Sutra's pictures the two disagree (held). A negative is
 *       refused by both.
 *   RR6 THE SENSOR GRID (sensor_hour_faults): row i's cell of the grid of
 *       sensors by hours (one reading a row: sensor, hour, fault flag),
 *       the faults of every row with its sensor and its hour, itself
 *       included, exact.
 *   RR7 A MACHINE PART NODE (machine_part_ok) fits ^ZXRATCHET(machine,
 *       part) = parent ^ kind ^ hours: machine at most 4 digits, part 6,
 *       parent 6, kind 2 and hours 9, sign aside (MYRA's i-fields).
 *   RR8 ONE PART, SEVERAL MEASURES. Each measure with a limit says of the
 *       part "sound" (RR1 TRUE) or "faulty" (FALSE); the part is
 *       lpres_kjoin of what its measures say: TRUE with FALSE is BOTH (a
 *       part within limits on vibration and over them on temperature),
 *       never TRUE, and raises an inspection; NEITHER when no measure has
 *       a reading. The machine is lpres_conjoin across its parts.
 *
 * Anything else is refused (RATCHET_REFUSED), never rounded or wrapped. */
#ifndef RATCHET_RULES_H
#define RATCHET_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "lpres.h"

#define RATCHET_REFUSED      INT64_MIN
#define RATCHET_MARGIN_MAX   INT64_C(999999)          /* RR1 */
#define RATCHET_HOURS_MAX    INT64_C(99999)           /* RR3: hours in one row */
#define RATCHET_LOAD_FULL    INT64_C(10000)           /* RR3: 100.00% */
#define RATCHET_INTERVAL_MAX INT64_C(999999)          /* RR4: full-load hours */
#define RATCHET_WORN_MAX     INT64_C(999999999999999) /* RR4: 15 digits */
#define RATCHET_QTY_MAX      INT64_C(9999)            /* RR5: Sutra's 9(4) */
#define RATCHET_PRICE_MAX    INT64_C(999999999)       /* RR5: 9(9) */
#define RATCHET_MINUTES_MAX  INT64_C(99999)           /* RR5: 9(5) */
#define RATCHET_RATE_MAX     INT64_C(9999999)         /* RR5: 9(7) */
#define RATCHET_HELD_MAX     INT64_C(999999999)       /* RR5: Trebago's 9 digits */

enum {
    RATCHET_STEP_NONE = 0,
    RATCHET_STEP_ISOLATE = 1,
    RATCHET_STEP_REPAIR = 2,
    RATCHET_STEP_RESTORE = 3,
    RATCHET_STEP_RECORD = 4,
};

static inline bool ratchet_in(int64_t v, int64_t lo, int64_t hi)
{
    return v >= lo && v <= hi;
}

/* RR1 */
static inline bool ratchet_limits_ok(int64_t above, int64_t below)
{
    return ratchet_in(above, 0, RATCHET_MARGIN_MAX) && ratchet_in(below, 0, RATCHET_MARGIN_MAX);
}

/* RR1: a reading's two margins; false when lo..hi is no range or a margin
 * does not fit 64 bits */
static inline bool ratchet_margins(int64_t reading, int64_t lo, int64_t hi, int64_t *above,
                                   int64_t *below)
{
    if (!above || !below || lo > hi) return false;
    if ((lo < 0 && reading > INT64_MAX + lo) || (lo > 0 && reading < INT64_MIN + lo)) return false;
    if ((reading < 0 && hi > INT64_MAX + reading) || (reading > 0 && hi < INT64_MIN + reading))
        return false;
    *above = reading - lo;
    *below = hi - reading;
    return true;
}

/* RR2 */
static inline int64_t ratchet_repair_step(int64_t fault, int64_t isolated, int64_t fixed,
                                          int64_t tested)
{
    if (!ratchet_in(fault, 0, 1) || !ratchet_in(isolated, 0, 1) || !ratchet_in(fixed, 0, 1) ||
        !ratchet_in(tested, 0, 1))
        return RATCHET_REFUSED;
    if (fault == 0) return RATCHET_STEP_NONE;
    if (fixed == 0) return isolated ? RATCHET_STEP_REPAIR : RATCHET_STEP_ISOLATE;
    return tested ? RATCHET_STEP_RECORD : RATCHET_STEP_RESTORE;
}

/* RR2: the step does something to the machine itself */
static inline bool ratchet_step_touches(int64_t step)
{
    return step >= RATCHET_STEP_ISOLATE && step <= RATCHET_STEP_RESTORE;
}

/* RR3 */
static inline int64_t ratchet_wear(const int64_t *hours, const int64_t *load, uint32_t n)
{
    int64_t s = 0;
    if ((!hours || !load) && n) return RATCHET_REFUSED;
    for (uint32_t i = 0; i < n; i++) {
        if (!ratchet_in(hours[i], 0, RATCHET_HOURS_MAX) ||
            !ratchet_in(load[i], 0, RATCHET_LOAD_FULL))
            return RATCHET_REFUSED;
        int64_t w = hours[i] * load[i]; /* below 10^9 */
        if (s > INT64_MAX - w) return RATCHET_REFUSED;
        s += w;
    }
    return s;
}

static inline int64_t ratchet_gcd(int64_t a, int64_t b)
{
    if (a < 0) a = -a;
    while (b) {
        int64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/* RR4: false when refused; *num / *den reduced, den > 0 */
static inline bool ratchet_service_left(int64_t interval, int64_t worn, int64_t load, int64_t *num,
                                        int64_t *den)
{
    if (!num || !den || !ratchet_in(interval, 1, RATCHET_INTERVAL_MAX) ||
        !ratchet_in(worn, 0, RATCHET_WORN_MAX) || !ratchet_in(load, 1, RATCHET_LOAD_FULL))
        return false;
    int64_t n = interval * RATCHET_LOAD_FULL - worn; /* |n| below 10^15 */
    int64_t g = ratchet_gcd(n, load);
    if (g == 0) g = 1;
    *num = n / g;
    *den = load / g;
    return true;
}

/* RR5 */
static inline int64_t ratchet_line_cost(int64_t qty, int64_t price, int64_t minutes, int64_t rate)
{
    if (!ratchet_in(qty, 0, RATCHET_QTY_MAX) || !ratchet_in(price, 0, RATCHET_PRICE_MAX) ||
        !ratchet_in(minutes, 0, RATCHET_MINUTES_MAX) || !ratchet_in(rate, 0, RATCHET_RATE_MAX))
        return RATCHET_REFUSED;
    return qty * price + minutes * rate; /* below 1.1 * 10^13 */
}

/* RR5, as Trebago holds it: each field 0..999999999 (the held domain) */
static inline int64_t ratchet_line_cost_held(int64_t qty, int64_t price, int64_t minutes,
                                             int64_t rate)
{
    if (!ratchet_in(qty, 0, RATCHET_HELD_MAX) || !ratchet_in(price, 0, RATCHET_HELD_MAX) ||
        !ratchet_in(minutes, 0, RATCHET_HELD_MAX) || !ratchet_in(rate, 0, RATCHET_HELD_MAX))
        return RATCHET_REFUSED;
    return qty * price + minutes * rate; /* below 2 * 10^18 */
}

/* RR6: row i's cell, or RATCHET_REFUSED when the sum passes 64 bits */
static inline int64_t ratchet_grid_cell(const int64_t *sensor, const int64_t *hour,
                                        const int64_t *fault, uint32_t n, uint32_t i)
{
    int64_t s = 0;
    if (!sensor || !hour || !fault || i >= n) return RATCHET_REFUSED;
    for (uint32_t j = 0; j < n; j++) {
        if (sensor[j] != sensor[i] || hour[j] != hour[i]) continue;
        if ((fault[j] > 0 && s > INT64_MAX - fault[j]) ||
            (fault[j] < 0 && s < INT64_MIN + 1 - fault[j]))
            return RATCHET_REFUSED;
        s += fault[j];
    }
    return s;
}

/* RR7: |v| has at most `digits` digits (1..18) */
static inline bool ratchet_digits(int64_t v, int digits)
{
    int64_t top = 1;
    for (int k = 0; k < digits && k < 18; k++) top *= 10;
    return v > -top && v < top;
}

static inline bool ratchet_node_ok(int64_t machine, int64_t part, int64_t parent, int64_t kind,
                                   int64_t hours)
{
    return ratchet_digits(machine, 4) && ratchet_digits(part, 6) && ratchet_digits(parent, 6) &&
           ratchet_digits(kind, 2) && ratchet_digits(hours, 9);
}

/* RR8: what one measure says of its part */
static inline lpres_state_t ratchet_measure_says(int64_t above, int64_t below)
{
    return ratchet_limits_ok(above, below) ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
}

/* RR8: the part, from what each of its n measures says */
static inline lpres_state_t ratchet_part_state(const lpres_state_t *says, uint32_t n)
{
    lpres_state_t s = LPRES_STATE_NEITHER;
    for (uint32_t i = 0; says && i < n; i++) s = lpres_kjoin(s, says[i]);
    return s;
}

#endif
