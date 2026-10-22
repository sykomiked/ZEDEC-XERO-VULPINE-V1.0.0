/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* levin_rules.h — Levin's rules in C, the reference its Eve, KRON,
 * Trebago, Sutra and MYRA blocks are checked against (blocks/levin). Pure
 * integer functions, no state: the model (levin_model.c) and the language
 * test (tests/levin_zxapp.inc) both include this, so the languages are
 * tested against the very C the model runs.
 *
 *   LR1 A CIRCUIT WITHIN ITS BREAKER (circuit_breaker_ok), three
 *       margins, each against its own range:
 *         spare = 4 * breaker - 5 * load (mA): the load at most 80% of the
 *                 breaker's rating, 0..LEVIN_SPARE_MAX;
 *         cable = cable - breaker (mA): the cable rated at least the
 *                 breaker, 0..LEVIN_CABLE_MAX;
 *         slack = limit - drop (microvolts): the voltage drop at most the
 *                 owner's limit, 0..LEVIN_SLACK_MAX.
 *       TRUE only when all three fit. A margin past its range is no
 *       circuit Levin plans (Eve refuses it, KRON reads any margin at or
 *       above 0 as within: held).
 *   LR2 LOAD CURRENT (circuit_load_ma): 1000 * watts / volts milliamps,
 *       an exact reduced fraction, watts 0..LEVIN_WATTS_MAX and volts
 *       1..LEVIN_VOLTS_MAX; the division is tally.ratio_div's (Tally owns
 *       rational arithmetic). Outside it Trebago refuses, KRON answers any
 *       volts but 0: held.
 *   LR3 VOLTAGE DROP (circuit_drop_uv): 2 * metres * ma * mohm
 *       microvolts, out and back along a cable of `metres` (one way,
 *       0..LEVIN_METRES_MAX) carrying `ma` milliamps (0..LEVIN_MA_MAX) with
 *       `mohm` milliohms a metre per conductor (0..LEVIN_MOHM_MAX), exact.
 *       The current is LR2 rounded up (levin_ma_up): the drop is never
 *       under-stated. Outside it Trebago refuses, KRON answers: held.
 *   LR4 THE CIRCUIT MATRIX (circuit_room_watts), circuits by rooms, one
 *       load a row (circuit, room, watts): row i's cell, the watts of every
 *       row with its circuit and its room, itself included, exact.
 *   LR5 A WIRING PERMIT'S FEE (wiring_permit_fee), in minor units: base +
 *       inspections * visit, base 0..9999999 (9(7)), inspections 0..99
 *       (9(2)), visit 0..999999 (9(6)); a fee past 99999999 (9(8)) is an
 *       overflow, refused, never wrapped.
 *   LR6 A PANEL NODE (panel_circuit_ok) fits ^ZXLEVIN(panel, circuit) =
 *       parent ^ breaker ^ cable: panel at most 2 digits, circuit and
 *       parent 3, breaker and cable 6 (mA), sign aside (MYRA's i-fields).
 *   LR7 PLAN AGAINST READING. Of each circuit two sources say whether it
 *       carries a load: the plan (its planned watts above LEVIN_IDLE_W)
 *       and the person's reading (its measured watts above LEVIN_IDLE_W).
 *       lpres_kjoin joins them: a plan that puts a load on a circuit the
 *       reading shows idle (or one the plan leaves idle carrying it) is
 *       BOTH: miswired, never TRUE. A circuit with no reading is NEITHER.
 *
 * Anything else is refused (LEVIN_REFUSED), never rounded or wrapped. */
#ifndef LEVIN_RULES_H
#define LEVIN_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "lpres.h"
#include "tally_rules.h"

#define LEVIN_REFUSED     INT64_MIN
#define LEVIN_SPARE_MAX   INT64_C(999999)    /* LR1: mA */
#define LEVIN_CABLE_MAX   INT64_C(999999)    /* LR1: mA */
#define LEVIN_SLACK_MAX   INT64_C(999999999) /* LR1: microvolts */
#define LEVIN_BREAKER_MAX INT64_C(200000)    /* the largest breaker planned: 200 A */
#define LEVIN_WATTS_MAX   INT64_C(999999)    /* LR2 */
#define LEVIN_VOLTS_MAX   INT64_C(999)       /* LR2 */
#define LEVIN_METRES_MAX  INT64_C(9999)      /* LR3 */
#define LEVIN_MA_MAX      INT64_C(999999)    /* LR3 */
#define LEVIN_MOHM_MAX    INT64_C(99999)     /* LR3 */
#define LEVIN_BASE_MAX    INT64_C(9999999)   /* LR5: 9(7) */
#define LEVIN_VISITS_MAX  INT64_C(99)        /* LR5: 9(2) */
#define LEVIN_VISIT_MAX   INT64_C(999999)    /* LR5: 9(6) */
#define LEVIN_FEE_MAX     INT64_C(99999999)  /* LR5: 9(8) */
#define LEVIN_IDLE_W      INT64_C(25)        /* LR7: at or below, a circuit is idle */

static inline bool levin_in(int64_t v, int64_t lo, int64_t hi)
{
    return v >= lo && v <= hi;
}

/* LR1 */
static inline bool levin_within_breaker(int64_t spare, int64_t cable, int64_t slack)
{
    return levin_in(spare, 0, LEVIN_SPARE_MAX) && levin_in(cable, 0, LEVIN_CABLE_MAX) &&
           levin_in(slack, 0, LEVIN_SLACK_MAX);
}

/* LR2: false when refused; *r the exact current in mA (tally.ratio_div) */
static inline bool levin_load_ma(int64_t watts, int64_t volts, rat_t *r)
{
    if (!r || !levin_in(watts, 0, LEVIN_WATTS_MAX) || !levin_in(volts, 1, LEVIN_VOLTS_MAX))
        return false;
    return tally_ratio(TALLY_DIV, watts * 1000, 1, volts, 1, r);
}

/* LR3: the current rounded up to a whole mA (num / den, den > 0, num >= 0) */
static inline int64_t levin_ma_up(rat_t r)
{
    if (!r.valid || r.den <= 0 || r.num < 0) return LEVIN_REFUSED;
    return r.num / r.den + (r.num % r.den ? 1 : 0);
}

/* LR3 */
static inline int64_t levin_drop_uv(int64_t metres, int64_t ma, int64_t mohm)
{
    if (!levin_in(metres, 0, LEVIN_METRES_MAX) || !levin_in(ma, 0, LEVIN_MA_MAX) ||
        !levin_in(mohm, 0, LEVIN_MOHM_MAX))
        return LEVIN_REFUSED;
    return 2 * metres * ma * mohm; /* below 2 * 10^15 */
}

/* LR1: the margin 4 * breaker - 5 * load for an exact load in mA, floored
 * (its sign is the exact one: a load a fraction of a mA over 80% is -1) */
static inline int64_t levin_spare(int64_t breaker, rat_t load)
{
    if (!load.valid || load.den <= 0 || !levin_in(breaker, 0, LEVIN_BREAKER_MAX) || load.num < 0 ||
        load.num > INT64_MAX / 5 || load.den > INT64_MAX / (4 * LEVIN_BREAKER_MAX))
        return LEVIN_REFUSED;
    int64_t top = 4 * breaker * load.den - 5 * load.num; /* each term fits */
    int64_t q = top / load.den;
    if (top % load.den && top < 0) q--;
    return q;
}

/* LR4: row i's cell, or LEVIN_REFUSED when the sum passes 64 bits */
static inline int64_t levin_room_cell(const int64_t *circuit, const int64_t *room,
                                      const int64_t *watts, uint32_t n, uint32_t i)
{
    int64_t s = 0;
    if (!circuit || !room || !watts || i >= n) return LEVIN_REFUSED;
    for (uint32_t j = 0; j < n; j++) {
        if (circuit[j] != circuit[i] || room[j] != room[i]) continue;
        if ((watts[j] > 0 && s > INT64_MAX - watts[j]) ||
            (watts[j] < 0 && s < INT64_MIN + 1 - watts[j]))
            return LEVIN_REFUSED;
        s += watts[j];
    }
    return s;
}

/* LR5 */
static inline int64_t levin_permit_fee(int64_t base, int64_t inspections, int64_t visit)
{
    if (!levin_in(base, 0, LEVIN_BASE_MAX) || !levin_in(inspections, 0, LEVIN_VISITS_MAX) ||
        !levin_in(visit, 0, LEVIN_VISIT_MAX))
        return LEVIN_REFUSED;
    int64_t fee = base + inspections * visit; /* below 1.1 * 10^8 */
    return fee > LEVIN_FEE_MAX ? LEVIN_REFUSED : fee;
}

/* LR6: |v| has at most `digits` digits (1..18) */
static inline bool levin_digits(int64_t v, int digits)
{
    int64_t top = 1;
    for (int k = 0; k < digits && k < 18; k++) top *= 10;
    return v > -top && v < top;
}

static inline bool levin_node_ok(int64_t panel, int64_t circuit, int64_t parent, int64_t breaker,
                                 int64_t cable)
{
    return levin_digits(panel, 2) && levin_digits(circuit, 3) && levin_digits(parent, 3) &&
           levin_digits(breaker, 6) && levin_digits(cable, 6);
}

/* LR7: what one source says of a circuit: it carries a load */
static inline lpres_state_t levin_carries(int64_t watts)
{
    return watts > LEVIN_IDLE_W ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
}

/* LR7: the circuit, from the plan and the reading (has_reading false: no
 * reading, the plan alone proves nothing) */
static inline lpres_state_t levin_circuit_state(int64_t planned, bool has_reading, int64_t measured)
{
    if (!has_reading) return LPRES_STATE_NEITHER;
    return lpres_kjoin(levin_carries(planned), levin_carries(measured));
}

/* LR7: a circuit wired as planned: both sources agree (it carries, or it is
 * idle) is TRUE; BOTH stays BOTH and NEITHER stays NEITHER. The plan is
 * lpres_conjoin of its circuits, so one miswired circuit is never TRUE. */
static inline lpres_state_t levin_wired(lpres_state_t s)
{
    return s == LPRES_STATE_FALSE ? LPRES_STATE_TRUE : s;
}

#endif
