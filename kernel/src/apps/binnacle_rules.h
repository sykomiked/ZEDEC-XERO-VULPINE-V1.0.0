/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* binnacle_rules.h — Binnacle's rules in C, the reference its Trebago,
 * KRON, Eve, Sutra and MYRA blocks are checked against (blocks/binnacle).
 * Pure integer functions, no state: the model (binnacle_model.c) and the
 * language test (tests/binnacle_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs. Places are in
 * millionths of a degree (zxv_place.h), distances in whole metres,
 * bearings and headings in hundredths of a degree (centidegrees) clockwise
 * from true north, 0 .. 35999.
 *
 *   BR1 A FIX FROM TWO LINES OF POSITION (lop_fix_offset). Each line is
 *       s * east + c * north = p: s and c are 10000 times the sine and
 *       cosine of the body's azimuth (-10000 .. 10000), p 10000 times the
 *       intercept toward the body in millionths of a degree of arc
 *       (-9999999999 .. 9999999999, under a degree). The fix is the 2x2
 *       system solved by Cramer's rule: with d = sa * cb - sb * ca,
 *       east = (pa * cb - pb * ca) / d and north = (sa * pb - sb * pa) / d,
 *       millionths of a degree of arc off the assumed position, an exact
 *       reduced fraction (kernel/src/rational), never rounded. axis 0
 *       answers east, 1 north. Parallel lines (d = 0) give no fix, and a
 *       value outside its range is refused.
 *   BR2 BEARING TO A WAYPOINT (wpt_bearing_cdeg): from the fix to a
 *       waypoint dn metres north and de east (each -20000000 .. 20000000,
 *       not both 0), to the nearest whole degree, given in centidegrees:
 *       the angle off the north-south axis is counted against the table
 *       of 1000000 tan(k + 1/2 degree), k = 0 .. 44 (BINNACLE_TAN), then
 *       unfolded into its quadrant. A plane chart: advisory.
 *   BR3 DISTANCE TO A WAYPOINT (wpt_range_m): floor of the root of
 *       dn^2 + de^2, the same domain, worked bit by bit (2^24 down to 1),
 *       so the root is exact and never rounded up.
 *   BR4 HAZARD SECTOR MARGIN (hazard_margin): a marked hazard is
 *       the sector of bearings from lo clockwise to hi (both inside it);
 *       a heading's margin is ((heading - lo) mod 36000) - ((hi - lo) mod
 *       36000): 1 or more outside the sector, 0 or less inside. Each of
 *       heading, lo and hi 0 .. 35999, else refused.
 *   BR5 SAFE HEADING (heading_safe): heading 0 .. 35999, margin (the least
 *       BR4 margin over every marked sector; 35999 when none is marked)
 *       1 .. 35999, and xtkroom (the declared cross-track limit less the
 *       cross-track distance, metres) 0 .. 1000000. Each field against
 *       its own range.
 *   BR6 DISTANCE RUN ON A LEG (leg_run_m): floor(speed * secs / 3600)
 *       metres, speed in metres an hour (a knot is 1852) and secs the
 *       time on the leg, each 0 .. 999999; a run past 99999999 m (the
 *       Sutra picture 9(8)) is an overflow, refused.
 *   BR7 THE WAYPOINT TREE (waypoint_node_ok) fits ^ZXBINNACLE(route, wpt)
 *       = lat ^ lon: route and wpt at most 3 digits, lat 8 and lon 9,
 *       sign aside.
 *   BR8 WHERE THE BOAT IS (binnacle_position_join, kept in C): a star fix
 *       and a clock fix (dead reckoning from the last fix by the clock)
 *       joined four-valued (lpres_kjoin): within the tolerance of each
 *       other TRUE, farther apart BOTH, and a BOTH position is the box
 *       holding both fixes, never one point.
 *
 * Anything outside is refused (BINNACLE_REFUSED), never rounded or
 * wrapped. Advisory navigation aid, not a certified navigation system: a
 * person steers and keeps official charts. */
#ifndef BINNACLE_RULES_H
#define BINNACLE_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "../rational/rational.h"
#include "gnomon_rules.h"
#include "lpres.h"
#include "orrery_rules.h"
#include "sundial_rules.h"

#define BINNACLE_REFUSED   INT64_MIN
#define BINNACLE_UNIT      INT64_C(10000)      /* BR1: sine and cosine scale */
#define BINNACLE_P_MAX     INT64_C(9999999999) /* BR1 */
#define BINNACLE_D_MAX     INT64_C(20000000)   /* BR2, BR3: metres */
#define BINNACLE_TURN      INT64_C(36000)      /* centidegrees */
#define BINNACLE_ROOM_MAX  INT64_C(1000000)    /* BR5: metres */
#define BINNACLE_SPEED_MAX INT64_C(999999)     /* BR6: metres an hour */
#define BINNACLE_SECS_MAX  INT64_C(999999)     /* BR6 */
#define BINNACLE_RUN_MAX   INT64_C(99999999)   /* BR6: Sutra's 9(8) */
#define BINNACLE_LAT_MAX   INT64_C(90000000)
#define BINNACLE_LON_MAX   INT64_C(180000000)
#define BINNACLE_M_PER_DEG INT64_C(111320) /* metres in a degree of arc */
#define BINNACLE_KNOT_MH   INT64_C(1852)   /* metres an hour in a knot */

/* BR2: 1000000 tan(k + 1/2 degree), k = 0 .. 44, rounded once; the same
 * numbers as blocks/binnacle/bearing.kron */
static const int64_t BINNACLE_TAN[45] = {
    8727,   26186,  43661,  61163,  78702,  96289,  113936, 131652, 149451, 167343, 185339, 203452,
    221695, 240079, 258618, 277325, 296213, 315299, 334595, 354119, 373885, 393910, 414214, 434812,
    455726, 476976, 498582, 520567, 542956, 565773, 589045, 612801, 637070, 661886, 687281, 713293,
    739961, 767327, 795436, 824336, 854081, 884725, 916331, 948965, 982697};

/* floor(a / b) and a mod b in 0 .. b - 1, b > 0 */
static inline int64_t binnacle_floor_div(int64_t a, int64_t b)
{
    int64_t q = a / b;
    return (a % b != 0 && a < 0) ? q - 1 : q;
}

static inline int64_t binnacle_mod(int64_t a, int64_t b)
{
    int64_t r = a % b;
    return r < 0 ? r + b : r;
}

static inline bool binnacle_in(int64_t v, int64_t lo, int64_t hi)
{
    return v >= lo && v <= hi;
}

/* BR1: the fix as an exact fraction; .valid false when refused */
static inline rat_t binnacle_lop_fix(int64_t sa, int64_t ca, int64_t pa, int64_t sb, int64_t cb,
                                     int64_t pb, int64_t axis)
{
    rat_t no = {0, 1, false};
    if (!binnacle_in(sa, -BINNACLE_UNIT, BINNACLE_UNIT) ||
        !binnacle_in(ca, -BINNACLE_UNIT, BINNACLE_UNIT) ||
        !binnacle_in(sb, -BINNACLE_UNIT, BINNACLE_UNIT) ||
        !binnacle_in(cb, -BINNACLE_UNIT, BINNACLE_UNIT) ||
        !binnacle_in(pa, -BINNACLE_P_MAX, BINNACLE_P_MAX) ||
        !binnacle_in(pb, -BINNACLE_P_MAX, BINNACLE_P_MAX) || axis < 0 || axis > 1)
        return no;
    int64_t d = sa * cb - sb * ca; /* |d| <= 2 * 10^8 */
    if (d == 0) return no;
    int64_t n = axis ? sa * pb - sb * pa : pa * cb - pb * ca; /* |n| < 2 * 10^14 */
    return rat_make(n, d);
}

/* BR3: floor of the root of s, 0 <= s < 2^50, bit by bit as wpt_range.kron */
static inline int64_t binnacle_root(int64_t s)
{
    int64_t r = 0;
    if (s < 0 || s >= (INT64_C(1) << 50)) return BINNACLE_REFUSED;
    for (int64_t b = INT64_C(1) << 24; b > 0; b >>= 1)
        if ((r + b) * (r + b) <= s) r += b;
    return r;
}

static inline bool binnacle_offset_ok(int64_t dn, int64_t de)
{
    return binnacle_in(dn, -BINNACLE_D_MAX, BINNACLE_D_MAX) &&
           binnacle_in(de, -BINNACLE_D_MAX, BINNACLE_D_MAX);
}

/* BR2 */
static inline int64_t binnacle_bearing(int64_t dn, int64_t de)
{
    if (!binnacle_offset_ok(dn, de) || (dn == 0 && de == 0)) return BINNACLE_REFUSED;
    int64_t ay = dn < 0 ? -dn : dn, ax = de < 0 ? -de : de;
    int64_t lo = ax < ay ? ax : ay, hi = ax < ay ? ay : ax, c = 0;
    for (uint32_t k = 0; k < 45u; k++) c += lo * 1000000 > BINNACLE_TAN[k] * hi;
    int64_t off = ay >= ax ? c : 90 - c; /* degrees off the north-south axis */
    int64_t deg;
    if (dn >= 0)
        deg = de >= 0 ? off : 360 - off;
    else
        deg = de >= 0 ? 180 - off : 180 + off;
    return 100 * binnacle_mod(deg, 360);
}

/* BR3 */
static inline int64_t binnacle_range(int64_t dn, int64_t de)
{
    if (!binnacle_offset_ok(dn, de)) return BINNACLE_REFUSED;
    return binnacle_root(dn * dn + de * de);
}

/* BR4 */
static inline int64_t binnacle_hazard_margin(int64_t heading, int64_t lo, int64_t hi)
{
    if (!binnacle_in(heading, 0, BINNACLE_TURN - 1) || !binnacle_in(lo, 0, BINNACLE_TURN - 1) ||
        !binnacle_in(hi, 0, BINNACLE_TURN - 1))
        return BINNACLE_REFUSED;
    return binnacle_mod(heading - lo, BINNACLE_TURN) - binnacle_mod(hi - lo, BINNACLE_TURN);
}

/* BR5 */
static inline bool binnacle_heading_safe(int64_t heading, int64_t margin, int64_t xtkroom)
{
    return binnacle_in(heading, 0, BINNACLE_TURN - 1) &&
           binnacle_in(margin, 1, BINNACLE_TURN - 1) && binnacle_in(xtkroom, 0, BINNACLE_ROOM_MAX);
}

/* BR6 */
static inline int64_t binnacle_leg_run(int64_t speed, int64_t secs)
{
    if (!binnacle_in(speed, 0, BINNACLE_SPEED_MAX) || !binnacle_in(secs, 0, BINNACLE_SECS_MAX))
        return BINNACLE_REFUSED;
    int64_t run = speed * secs / 3600; /* both 0 or more: truncation is the floor */
    return run > BINNACLE_RUN_MAX ? BINNACLE_REFUSED : run;
}

/* |v| has at most `digits` decimal digits (MYRA's iN) */
static inline bool binnacle_digits(int64_t v, uint32_t digits)
{
    int64_t top = 1;
    for (uint32_t i = 0; i < digits && i < 18u; i++) top *= 10;
    return v > -top && v < top;
}

/* BR7 */
static inline bool binnacle_node_ok(int64_t route, int64_t wpt, int64_t lat, int64_t lon)
{
    return binnacle_digits(route, 3) && binnacle_digits(wpt, 3) && binnacle_digits(lat, 8) &&
           binnacle_digits(lon, 9);
}

/* waymark.place_ok, the C witness of the composed entry */
static inline bool binnacle_place_ok(int64_t lat, int64_t lon)
{
    return binnacle_in(lat, -BINNACLE_LAT_MAX, BINNACLE_LAT_MAX) &&
           binnacle_in(lon, -BINNACLE_LON_MAX, BINNACLE_LON_MAX);
}

/* 10000 cos of a latitude in millionths, to its nearest whole degree */
static inline int64_t binnacle_cos_lat(int64_t lat)
{
    return orrery_cos(binnacle_floor_div(lat + 500000, 1000000));
}

/* A plane chart about a latitude: (dn, de) metres from a to b, both
 * places; false when either is off the globe, the cosine there is under
 * 0.1 (past 84 degrees) or the offset is past BR2's domain. */
static inline bool binnacle_metres(int64_t alat, int64_t alon, int64_t blat, int64_t blon,
                                   int64_t *dn, int64_t *de)
{
    if (!binnacle_place_ok(alat, alon) || !binnacle_place_ok(blat, blon)) return false;
    int64_t cs = binnacle_cos_lat(alat);
    if (cs < 1000) return false;
    int64_t dlon = blon - alon; /* the short way round */
    if (dlon > BINNACLE_LON_MAX) dlon -= 2 * BINNACLE_LON_MAX;
    if (dlon < -BINNACLE_LON_MAX) dlon += 2 * BINNACLE_LON_MAX;
    int64_t n = binnacle_floor_div((blat - alat) * BINNACLE_M_PER_DEG, 1000000);
    int64_t e = binnacle_floor_div(dlon * cs / BINNACLE_UNIT * BINNACLE_M_PER_DEG, 1000000);
    if (!binnacle_offset_ok(n, e)) return false;
    *dn = n;
    *de = e;
    return true;
}

/* BR8: the box holding two places, south-west and north-east corners */
typedef struct {
    lpres_state_t said; /* TRUE the fixes agree, BOTH they do not */
    int64_t south, west, north, east;
} binnacle_box_t;

static inline bool binnacle_position_join(int64_t slat, int64_t slon, int64_t clat, int64_t clon,
                                          int64_t tol_m, binnacle_box_t *box)
{
    int64_t dn, de;
    if (!box || tol_m < 0 || tol_m > BINNACLE_ROOM_MAX ||
        !binnacle_metres(slat, slon, clat, clon, &dn, &de))
        return false;
    lpres_state_t star = LPRES_STATE_TRUE; /* the star fix says the boat is here */
    lpres_state_t clock = dn * dn + de * de <= tol_m * tol_m ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    int64_t lo = slon < clon ? slon : clon, hi = slon < clon ? clon : slon;
    bool wrap = hi - lo > BINNACLE_LON_MAX; /* the box crosses 180 degrees */
    box->said = lpres_kjoin(star, clock);
    box->south = slat < clat ? slat : clat;
    box->north = slat < clat ? clat : slat;
    box->west = wrap ? hi : lo;
    box->east = wrap ? lo : hi;
    return true;
}

#endif
