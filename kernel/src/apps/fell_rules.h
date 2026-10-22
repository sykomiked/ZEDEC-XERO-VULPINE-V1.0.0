/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fell_rules.h — Fell's rules in C, the reference its Trebago, KRON, Eve,
 * Sutra and MYRA blocks are checked against (blocks/fell). Pure integer
 * functions, no state: the model (fell_model.c) and the language test
 * (tests/fell_zxapp.inc) both include this, so the languages are tested
 * against the very C the model runs. Every answer is advice for the
 * incident commander, who decides.
 *
 *   FR1 SECTOR PRIORITY (sector_priority): poa(i), the sector's
 *       probability of area, and cov(i), its searched coverage, both in
 *       hundredths of a percent 0..FELL_X100. The weight left in a sector
 *       is w(i) = poa(i) * (10000 - cov(i)); with c(i) the running sum of
 *       the weights to sector i and t their total, the sector's priority
 *       is floor(10000 * c(i) / t) - floor(10000 * (c(i) - w(i)) / t): the
 *       priorities sum to exactly 10000 and each is within one of its
 *       exact share. Refused for no sector, a poa or cov outside 0..10000
 *       or a total weight of 0 (nowhere left that is likely).
 *   FR2 CREW SAFE (crew_shift_safe): the shift so far, worked minutes,
 *       0..FELL_SHIFT_MAX (12 hours), and the last check-in, since
 *       minutes ago, 0..FELL_CHECKIN_MAX. A crew that is not safe is
 *       overdue: the overdue is raised for the commander. A minute count
 *       below 0 is no count, never safe.
 *   FR3 THE SECTOR GRID (search_grid_sector): a point east and north of
 *       the grid's south-west corner, in millionths of a degree, on a grid
 *       of square cells of `cell` millionths and `cols` columns: column
 *       east / cell, row north / cell, sector 1 + column + cols * row.
 *       Refused for a point west or south of the corner, a cell below 1,
 *       cols below 1, or a column past the grid's last.
 *   FR4 UNSEARCHED (sector_unsearched): a sector whose coverage is under
 *       the board's bar, cov < bar.
 *   FR5 SIGHTINGS BY TIME (sightings_between): of the sightings at
 *       stamp(i), the count with from <= stamp(i) <= to.
 *   FR6 A CALLOUT CLAIM (callout_claim), minor units: hours * rate +
 *       miles * permile, hours 0..999, rate 0..999999, miles and permile
 *       0..9999 (Sutra's 9(3), 9(6), 9(4)); a claim past 999999999 (9(9))
 *       is refused, never wrapped. A claim is a record; Fell pays nothing.
 *   FR7 A CASUALTY RECORD of ^ZXFELL holds integers of at most the digits
 *       its pieces declare, sign aside (blocks/fell/casualty.myra):
 *       incident 6, casualty 3, sector 4, triage 1, stamp 13.
 *   FR8 SIGHTINGS DISAGREE. Each sighting of the last FELL_SIGHT_WINDOW
 *       seconds says of every sector: TRUE where it was seen, FALSE
 *       elsewhere. A sector's state is lpres_kjoin of what they say (one
 *       sighting in sector 4 and one in sector 9 make both sectors BOTH);
 *       the search's lead is lpres_disjoin over the sectors (the kernel's
 *       chain, F < N < B < T): TRUE when every sighting names one sector,
 *       BOTH when they disagree, NEITHER when there is none. A sector that
 *       is TRUE or BOTH is a lead: none is dropped, the commander decides.
 *
 * Anything else is refused (FELL_REFUSED), never rounded or wrapped. */
#ifndef FELL_RULES_H
#define FELL_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "lpres.h"

#define FELL_REFUSED      INT64_MIN
#define FELL_X100         INT64_C(10000) /* FR1: hundredths of a percent */
#define FELL_SECTOR_MAX   64u            /* FR1: sectors of one search */
#define FELL_SHIFT_MAX    INT64_C(720)   /* FR2: 12 hours, minutes */
#define FELL_CHECKIN_MAX  INT64_C(60)    /* FR2: minutes */
#define FELL_HOURS_MAX    INT64_C(999)   /* FR6: 9(3) */
#define FELL_RATE_MAX     INT64_C(999999)
#define FELL_MILES_MAX    INT64_C(9999)
#define FELL_CLAIM_MAX    INT64_C(999999999)
#define FELL_SIGHT_WINDOW INT64_C(900)     /* FR8: 15 minutes, seconds */
#define FELL_CELL_MIN     INT64_C(100)     /* FR3: about 11 m of latitude */
#define FELL_CELL_MAX     INT64_C(1000000) /* FR3: one degree */
#define FELL_COLS_MAX     INT64_C(64)

static inline bool fell_in(int64_t v, int64_t lo, int64_t hi)
{
    return v >= lo && v <= hi;
}

/* FR1: false when refused; pr[0..n) the priorities */
static inline bool fell_sector_priority(const int64_t *poa, const int64_t *cov, uint32_t n,
                                        int64_t *pr)
{
    int64_t t = 0, c = 0;
    if (!poa || !cov || !pr || n < 1u || n > FELL_SECTOR_MAX) return false;
    for (uint32_t i = 0; i < n; i++) {
        if (!fell_in(poa[i], 0, FELL_X100) || !fell_in(cov[i], 0, FELL_X100)) return false;
        t += poa[i] * (FELL_X100 - cov[i]); /* at most 64 * 10^8 */
    }
    if (t <= 0) return false;
    for (uint32_t i = 0; i < n; i++) {
        int64_t w = poa[i] * (FELL_X100 - cov[i]);
        c += w;
        pr[i] = FELL_X100 * c / t - FELL_X100 * (c - w) / t; /* both >= 0: floor */
    }
    return true;
}

/* FR2 */
static inline bool fell_crew_safe(int64_t worked, int64_t since)
{
    return fell_in(worked, 0, FELL_SHIFT_MAX) && fell_in(since, 0, FELL_CHECKIN_MAX);
}

/* FR3 */
static inline int64_t fell_grid_sector(int64_t east, int64_t north, int64_t cell, int64_t cols)
{
    if (east < 0 || north < 0 || cell < 1 || cols < 1 || east / cell >= cols) return FELL_REFUSED;
    int64_t row = north / cell;
    if (row > (INT64_MAX - 1 - cols) / cols) return FELL_REFUSED;
    return 1 + east / cell + cols * row;
}

/* FR4 */
static inline bool fell_unsearched(int64_t cov, int64_t bar)
{
    return cov < bar;
}

/* FR5 */
static inline int64_t fell_sightings_between(const int64_t *stamp, uint32_t n, int64_t from,
                                             int64_t to)
{
    int64_t k = 0;
    if (!stamp) return FELL_REFUSED;
    for (uint32_t i = 0; i < n; i++) k += stamp[i] >= from && stamp[i] <= to ? 1 : 0;
    return k;
}

/* FR6 */
static inline int64_t fell_callout_claim(int64_t hours, int64_t rate, int64_t miles,
                                         int64_t permile)
{
    if (!fell_in(hours, 0, FELL_HOURS_MAX) || !fell_in(rate, 0, FELL_RATE_MAX) ||
        !fell_in(miles, 0, FELL_MILES_MAX) || !fell_in(permile, 0, FELL_MILES_MAX))
        return FELL_REFUSED;
    int64_t claim = hours * rate + miles * permile; /* at most about 1.1 * 10^9 */
    return claim > FELL_CLAIM_MAX ? FELL_REFUSED : claim;
}

/* FR7: |v| has at most `digits` digits (1..18) */
static inline bool fell_digits(int64_t v, int digits)
{
    int64_t top = 1;
    for (int k = 0; k < digits && k < 18; k++) top *= 10;
    return v > -top && v < top;
}

static inline bool fell_casualty_fits(int64_t incident, int64_t casualty, int64_t sector,
                                      int64_t triage, int64_t stamp)
{
    return fell_digits(incident, 6) && fell_digits(casualty, 3) && fell_digits(sector, 4) &&
           fell_digits(triage, 1) && fell_digits(stamp, 13);
}

/* FR8: what one sighting says of one sector */
static inline lpres_state_t fell_seen_says(int64_t seen_sector, int64_t sector)
{
    return seen_sector == sector ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
}

/* FR8: a sector's state over the sightings sector[0..n) */
static inline lpres_state_t fell_sector_state(const int64_t *seen, uint32_t n, int64_t sector)
{
    lpres_state_t s = LPRES_STATE_NEITHER;
    for (uint32_t i = 0; seen && i < n; i++) s = lpres_kjoin(s, fell_seen_says(seen[i], sector));
    return s;
}

/* FR8: a lead the commander is shown, never dropped */
static inline bool fell_is_lead(lpres_state_t s)
{
    return s == LPRES_STATE_TRUE || s == LPRES_STATE_BOTH;
}

#endif
