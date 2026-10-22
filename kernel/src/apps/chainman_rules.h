/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* chainman_rules.h — Chainman's rules in C, the reference its Trebago,
 * KRON, Eve, Sutra and MYRA blocks are checked against (blocks/chainman),
 * with the rules it composes from nearer the core. Pure integer functions,
 * no state: the model (chainman_model.c) and the language test
 * (tests/chainman_zxapp.inc) both include this. Lengths are millimetres
 * on the survey's own grid (x east, y north), angles never: a traverse leg
 * is its run east (de) and north (dn) and its taped length.
 *
 *   CH1 CLOSURE SLACK (closure_slack). A traverse closes within tolerance
 *       when its closure error, the length of the misclosure (dx, dy), is
 *       at most tol millimetres for every 100 m of its perimeter (perim, in
 *       whole metres): sqrt(dx^2 + dy^2) <= tol * perim / 100. No root is
 *       taken: both sides are multiplied by 100 and squared, so the slack
 *       (tol * perim)^2 - 10000 * (dx^2 + dy^2) is 0 or more exactly when
 *       it closes. dx and dy -999999..999999, perim 1..99999 (100 km) and
 *       tol 0..9999, anything else refused; the widest slack is under
 *       10^18, so nothing can overflow.
 *   CH2 CLOSES (traverse_closes): the slack is 0..CHAINMAN_SLACK_MAX (18
 *       digits); a negative slack does not close, and one past 18 digits
 *       is no measurement (CH1 never makes one).
 *   CH3 TITLE RECORD (title_record_ok): a title register line is a fixed
 *       business record, title number 9(9), parcel 9(6) and area in square
 *       metres 9(12); the title and the parcel are 1 or more and the area
 *       too. A negative or longer value is refused outright.
 *   CH4 RIVAL CLAIMS (cell_rival_claims): one claim a row, a grid cell
 *       (cx, cy) and the parcel claiming it; row i's count of rows that
 *       claim its cell for another parcel. 0 is a cell held alone.
 *   CH5 CORNER NODE (corner_node_ok) fits ^ZXCHAIN(survey, parcel, corner)
 *       = x ^ y: survey and parcel at most 6 digits, corner 4, x and y 7
 *       (mm, about 10 km), sign aside.
 *   CH6 CELL STATE (lpres_state_t): no claim on a cell is NEITHER, claims
 *       by one parcel TRUE, claims by two parcels or more BOTH: the cell is
 *       disputed, every claim is kept, none overwrites another, and only a
 *       person and the land registry settle it.
 *   CH7 CELL INSIDE. A grid cell of CHAINMAN_CELL_MM on a side is claimed
 *       by a parcel when its centre lies strictly inside the parcel's
 *       outline (crossing number, exact on doubled coordinates); a cell
 *       is floor(x / CHAINMAN_CELL_MM), toward minus infinity. Corners
 *       inside gnomon's domain and cells within CHAINMAN_GRID_MAX either
 *       way, so every product stays under 10^16.
 *   Composed, as the parts' own rules: gnomon.polygon_area2 (GR1, the
 *   parcel's twice area), furrow.range_total (FW1, a field book column's
 *   sum), burin.bezier_at (BR1, a curved boundary flattened) and
 *   burin.drawing_record_ok (BR5, the outline's path record),
 *   understory.land_entry_ok (UR5, the parcel as a land entry) and
 *   waymark.place_ok (a corner's latitude and longitude, in millionths of
 *   a degree). */
#ifndef CHAINMAN_RULES_H
#define CHAINMAN_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "burin_rules.h"
#include "furrow_rules.h"
#include "gnomon_rules.h"
#include "lpres.h"
#include "understory_rules.h"

#define CHAINMAN_MISS_MAX   INT64_C(999999)             /* CH1: dx, dy */
#define CHAINMAN_PERIM_MAX  INT64_C(99999)              /* CH1: metres */
#define CHAINMAN_TOL_MAX    INT64_C(9999)               /* CH1: mm per 100 m */
#define CHAINMAN_SLACK_MAX  INT64_C(999999999999999999) /* CH2 */
#define CHAINMAN_TITLE_MAX  INT64_C(999999999)          /* CH3: 9(9) */
#define CHAINMAN_PARCEL_MAX INT64_C(999999)             /* CH3: 9(6) */
#define CHAINMAN_AREA_MAX   INT64_C(999999999999)       /* CH3: 9(12) square metres */
#define CHAINMAN_CELL_MM    INT64_C(10000)              /* CH7: a 10 m cell */
#define CHAINMAN_GRID_MAX   INT64_C(100000)             /* CH7: a cell's index either way */
#define CHAINMAN_STEPS_MAX  INT64_C(32)                 /* chords of one curved leg */
#define CHAINMAN_STROKE     INT64_C(100)                /* the path record's stroke: one point */
#define CHAINMAN_LAT_MAX    INT64_C(90000000)
#define CHAINMAN_LON_MAX    INT64_C(180000000)

static inline bool chainman_in(int64_t v, int64_t lo, int64_t hi)
{
    return v >= lo && v <= hi;
}

/* CH1 */
static inline bool chainman_closure_slack(int64_t dx, int64_t dy, int64_t perim, int64_t tol,
                                          int64_t *out)
{
    if (!out || !chainman_in(dx, -CHAINMAN_MISS_MAX, CHAINMAN_MISS_MAX) ||
        !chainman_in(dy, -CHAINMAN_MISS_MAX, CHAINMAN_MISS_MAX) ||
        !chainman_in(perim, 1, CHAINMAN_PERIM_MAX) || !chainman_in(tol, 0, CHAINMAN_TOL_MAX))
        return false;
    int64_t a = tol * perim;
    *out = a * a - 10000 * (dx * dx + dy * dy);
    return true;
}

/* CH2 */
static inline bool chainman_traverse_closes(int64_t slack)
{
    return chainman_in(slack, 0, CHAINMAN_SLACK_MAX);
}

/* CH3: false for a value that fits its picture but means nothing; the
 * caller refuses one past its picture first (chainman_title_fits). */
static inline bool chainman_title_fits(int64_t title, int64_t parcel, int64_t area)
{
    return chainman_in(title, 0, CHAINMAN_TITLE_MAX) &&
           chainman_in(parcel, 0, CHAINMAN_PARCEL_MAX) && chainman_in(area, 0, CHAINMAN_AREA_MAX);
}

static inline bool chainman_title_record_ok(int64_t title, int64_t parcel, int64_t area)
{
    return chainman_title_fits(title, parcel, area) && title >= 1 && parcel >= 1 && area >= 1;
}

/* CH4 */
static inline int64_t chainman_cell_rivals(const int64_t *cx, const int64_t *cy,
                                           const int64_t *parcel, uint32_t n, uint32_t i)
{
    int64_t r = 0;
    if (!cx || !cy || !parcel || i >= n) return 0;
    for (uint32_t j = 0; j < n; j++)
        r += cx[j] == cx[i] && cy[j] == cy[i] && parcel[j] != parcel[i];
    return r;
}

/* CH5 */
static inline bool chainman_corner_node_ok(int64_t survey, int64_t parcel, int64_t corner,
                                           int64_t x, int64_t y)
{
    return gnomon_fits_digits(survey, 6) && gnomon_fits_digits(parcel, 6) &&
           gnomon_fits_digits(corner, 4) && gnomon_fits_digits(x, 7) && gnomon_fits_digits(y, 7);
}

/* CH6: claims on one cell, and rivals among them (CH4) */
static inline lpres_state_t chainman_cell_state(int64_t claims, int64_t rivals)
{
    if (claims <= 0) return LPRES_STATE_NEITHER;
    return rivals > 0 ? LPRES_STATE_BOTH : LPRES_STATE_TRUE;
}

/* CH7 */
static inline int64_t chainman_cell_of(int64_t v)
{
    int64_t q = v / CHAINMAN_CELL_MM;
    if (v % CHAINMAN_CELL_MM != 0 && v < 0) q--;
    return q;
}

static inline bool chainman_inside(const int64_t *x, const int64_t *y, uint32_t n, int64_t cx,
                                   int64_t cy)
{
    bool in = false;
    if (!x || !y || n < 3u || !chainman_in(cx, -CHAINMAN_GRID_MAX, CHAINMAN_GRID_MAX) ||
        !chainman_in(cy, -CHAINMAN_GRID_MAX, CHAINMAN_GRID_MAX))
        return false;
    for (uint32_t i = 0; i < n; i++)
        if (!gnomon_coord_ok(x[i]) || !gnomon_coord_ok(y[i])) return false;
    int64_t px = (2 * cx + 1) * CHAINMAN_CELL_MM, py = (2 * cy + 1) * CHAINMAN_CELL_MM;
    for (uint32_t i = 0, j = n - 1u; i < n; j = i++) {
        int64_t xi = 2 * x[i], yi = 2 * y[i], xj = 2 * x[j], yj = 2 * y[j];
        if ((yi > py) == (yj > py)) continue;
        /* px < xi + (py - yi) * (xj - xi) / (yj - yi), multiplied out by yj - yi */
        int64_t l = (px - xi) * (yj - yi), r = (py - yi) * (xj - xi);
        if (yj > yi ? l < r : l > r) in = !in;
    }
    return in;
}

/* waymark.place_ok */
static inline bool chainman_place_ok(int64_t lat, int64_t lon)
{
    return chainman_in(lat, -CHAINMAN_LAT_MAX, CHAINMAN_LAT_MAX) &&
           chainman_in(lon, -CHAINMAN_LON_MAX, CHAINMAN_LON_MAX);
}

#endif
