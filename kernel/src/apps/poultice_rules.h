/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* poultice_rules.h — Poultice's rules in C, the reference its Eve, KRON,
 * Trebago, Sutra and MYRA blocks are checked against (blocks/poultice),
 * with the rules it composes from nearer the core. Pure integer
 * functions, no state: the model (poultice_model.c) and the language test
 * (tests/poultice_zxapp.inc) both include this.
 *
 * Advisory and human-approved: these rules say whether a dose a carer
 * proposes is within the limits the plan declares (the person's clinician
 * sets them, a carer enters them). They never decide or change a dose and
 * diagnose nothing; Poultice is not a medical device.
 *
 *   PR1 MARGINS. A proposed dose of a medicine is measured against its
 *       three declared limits as three margins, so that each language can
 *       hold each field to its own range: dosegap = the per-dose maximum
 *       less the dose; daygap = the daily maximum less the doses in the 24
 *       hours up to it, itself counted; waitgap = the minutes since the
 *       last dose less the minimum interval (no earlier dose, or a wait
 *       longer than POULTICE_WAIT_MAX, is POULTICE_WAIT_MAX).
 *   PR2 DOSE WITHIN THE DECLARED LIMIT (dose_in_limit, a truth): every
 *       margin 0 or more, dosegap at most POULTICE_DOSE_MAX, daygap at
 *       most POULTICE_DAY_MAX, waitgap at most POULTICE_WAIT_MAX (Eve's
 *       ranges). KRON reads any margin from 0 up: a margin past its range
 *       is held (Eve FALSE, KRON TRUE: BOTH).
 *   PR3 THE SAME RULE AS A NUMBER (dose_in_limit_n), 1 within, 0 not, to
 *       count with. Trebago holds each margin to -max..max (daygap
 *       -POULTICE_DAY_LOW..POULTICE_DAY_MAX) and refuses outside; KRON
 *       answers any: held. The C below is the rule's third witness.
 *   PR4 CARE COST A MONTH (care_month_cost): an item's cost over a month
 *       in integer minor units, price (a unit's price) * perday (units a
 *       day) * days (the month's days, solstice.month_days), exact. price
 *       0..999999999, perday 0..999 and days 0..99 (Sutra's 9(9), 9(3),
 *       9(2), the cost 9(15)): else refused, never wrapped. Trebago
 *       answers any non-negative row to 64 bits: past Sutra's pictures it
 *       is held. The month's total is wellspring.week's exact total of the
 *       items' costs (refused past 64 bits).
 *   PR5 CARE NODE (care_node_ok) fits ^ZXPOULT(patient, kind, item, day,
 *       minute, carer) = value: patient 4 digits, kind 1, item 4, day
 *       (days since 1970) 6, minute (of the day) 4, carer 2, value 9, sign
 *       aside. The carer is in the key, so two carers' entries for one
 *       minute are two nodes: both are kept.
 *   PR6 ONE READING, ONE VALUE (lpres_state_t): the readings carers logged
 *       for one vital at one minute; each agreeing with the first is TRUE,
 *       one that disagrees FALSE evidence, joined with lpres_kjoin, so two
 *       values are BOTH: every reading kept, an escalation to every carer.
 *       No reading is NEITHER.
 *   Composed, as the parts' own rules: solstice_next and solstice_overlap
 *   (solstice_rules.h SO3, SO4), solstice_month_days (SO1),
 *   bowline_unskipped and bowline_overdue (bowline_rules.h BW1, BW2),
 *   watchfire_medical_fits and watchfire_contact_fits (watchfire_rules.h
 *   WF4, WF5), cartulary_cell (cartulary_rules.h CR6); wellspring.band,
 *   wellspring.week, wellspring.log_ok (wellspring_model.h) and
 *   weatherglass.week_temps (weatherglass_model.h WG3) are the model's. */
#ifndef POULTICE_RULES_H
#define POULTICE_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "bowline_rules.h"
#include "cartulary_rules.h"
#include "lpres.h"
#include "solstice_rules.h"
#include "watchfire_rules.h"

#define POULTICE_DOSE_MAX  INT64_C(999999999) /* PR2: 9 digits, the plan's own unit */
#define POULTICE_DAY_MAX   99                 /* PR2: doses in 24 h */
#define POULTICE_DAY_LOW   999                /* PR3: Trebago's least daygap, -999 */
#define POULTICE_WAIT_MAX  INT64_C(999999999) /* PR1: minutes */
#define POULTICE_PRICE_MAX INT64_C(999999999) /* PR4: 9(9) */
#define POULTICE_PER_DAY   999                /* PR4: 9(3) */
#define POULTICE_DAYS_MAX  99                 /* PR4: 9(2) */
#define POULTICE_REFUSED   (-1)
#define POULTICE_READING   1000 /* a vital in its own unit (weatherglass.week_temps) */

/* PR1: the three margins of a proposed dose */
static inline void poultice_margins(int64_t dose, int64_t perdose, int64_t taken, int64_t daymax,
                                    int64_t since, int64_t gap, int64_t out[3])
{
    out[0] = perdose - dose;
    out[1] = daymax - taken;
    out[2] = since >= POULTICE_WAIT_MAX || since - gap >= POULTICE_WAIT_MAX ? POULTICE_WAIT_MAX
             : since - gap < -POULTICE_WAIT_MAX                             ? -POULTICE_WAIT_MAX
                                                                            : since - gap;
}

/* PR2 */
static inline bool poultice_dose_in_limit(int64_t dosegap, int64_t daygap, int64_t waitgap)
{
    return dosegap >= 0 && dosegap <= POULTICE_DOSE_MAX && daygap >= 0 &&
           daygap <= POULTICE_DAY_MAX && waitgap >= 0 && waitgap <= POULTICE_WAIT_MAX;
}

/* PR3: 1 or 0, POULTICE_REFUSED outside Trebago's domain */
static inline int64_t poultice_dose_in_limit_n(int64_t dosegap, int64_t daygap, int64_t waitgap)
{
    if (dosegap < -POULTICE_DOSE_MAX || dosegap > POULTICE_DOSE_MAX || daygap < -POULTICE_DAY_LOW ||
        daygap > POULTICE_DAY_MAX || waitgap < -POULTICE_WAIT_MAX || waitgap > POULTICE_WAIT_MAX)
        return POULTICE_REFUSED;
    return dosegap >= 0 && daygap >= 0 && waitgap >= 0;
}

/* PR4: the cost, or POULTICE_REFUSED */
static inline int64_t poultice_month_cost(int64_t price, int64_t perday, int64_t days)
{
    if (price < 0 || price > POULTICE_PRICE_MAX || perday < 0 || perday > POULTICE_PER_DAY ||
        days < 0 || days > POULTICE_DAYS_MAX)
        return POULTICE_REFUSED;
    return price * perday * days; /* at most 98900999901099, 9(15) */
}

/* PR5 */
static inline bool poultice_node_ok(int64_t patient, int64_t kind, int64_t item, int64_t day,
                                    int64_t minute, int64_t carer, int64_t value)
{
    return watchfire_digits_fit(patient, 4) && watchfire_digits_fit(kind, 1) &&
           watchfire_digits_fit(item, 4) && watchfire_digits_fit(day, 6) &&
           watchfire_digits_fit(minute, 4) && watchfire_digits_fit(carer, 2) &&
           watchfire_digits_fit(value, 9);
}

/* PR6: the values n readings of one vital at one minute give */
static inline lpres_state_t poultice_reading_state(const int64_t *value, uint32_t n)
{
    lpres_state_t s = LPRES_STATE_NEITHER;
    if (!value) return s;
    for (uint32_t i = 0; i < n; i++)
        s = lpres_kjoin(s, value[i] == value[0] ? LPRES_STATE_TRUE : LPRES_STATE_FALSE);
    return s;
}

#endif
