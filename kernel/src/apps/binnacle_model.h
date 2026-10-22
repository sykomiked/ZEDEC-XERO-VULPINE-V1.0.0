/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* binnacle_model.h — Binnacle, offline navigation, as a model (zxm.h):
 * position, heading and routes worked out on the device from star sights,
 * the clock and saved places, with no outside service. No drawing, no
 * allocation, integers only; the rules it runs are binnacle_rules.h
 * (BR1-BR8) and its parts' (Waymark, Weatherglass, Orrery, Sundial,
 * Gnomon).
 *
 * zxvd methods used: sys.info, place.here, place.list and place.add.
 *
 *   BM1 THE CLOCK. binnacle_clock reads sys.info's time_ms; dead
 *       reckoning and the log's local time run from it.
 *   BM2 THE CHART. binnacle_places reads the saved places (place.list) as
 *       the waypoints, each a point waymark.place_ok accepts; one that is
 *       not is left off. binnacle_add_waypoint puts one on the device's
 *       map (place.add) only on the owner's word, the point checked
 *       first. binnacle_here reads the device's own place (place.here).
 *   BM3 ROUTES are waypoints in order, each kept in the waypoint tree only
 *       when it fits ^ZXBINNACLE (BR7); at most BINNACLE_ROUTES routes of
 *       BINNACLE_ROUTE_WPTS waypoints.
 *   BM4 A STAR FIX. Two sights, each a body (0 a star, 1 .. 8 a planet),
 *       its declaration and hour angle in whole degrees, its azimuth in
 *       whole degrees and its intercept toward the body in millionths of
 *       a degree (under one degree), reduced by the navigator. Each body
 *       must have been above the horizon at the assumed position
 *       (orrery.sky_above_horizon); a planet's mean longitude on the day
 *       of the sight (orrery.civil_day, orrery.planet_longitude) is kept
 *       with it. The two lines of position meet at the fix (BR1, an exact
 *       fraction), plotted at its floor; the fix must be a place.
 *   BM5 A CLOCK FIX. Dead reckoning from the last fix: the run since then
 *       by the clock at the speed made good (BR6), along the course
 *       steered, on a plane chart.
 *   BM6 WHERE THE BOAT IS. The star fix and the clock fix joined (BR8):
 *       within the tolerance TRUE, at the star fix; farther apart BOTH,
 *       the box holding both, never one point. Bearings, distances and a
 *       safe heading are worked from a TRUE position only.
 *   BM7 THE WAY ON. binnacle_to_waypoints gives the bearing (BR2) and the
 *       distance (BR3) to every waypoint; binnacle_heading_check whether a
 *       heading is outside every marked hazard sector (BR4) with the
 *       cross-track distance off the leg (gnomon.triangle_area2 over the
 *       leg's length) within the declared limit (BR5).
 *   BM8 THE LEG'S NOTICE: the wind in km/h and its warnings
 *       (weatherglass.wind_kmh_x10, weatherglass.warn_flags) and the
 *       local time of the zone (sundial.zone_minute), composed for the
 *       view: zxvd has no method to post a notice.
 *
 * Advisory navigation aid, not a certified navigation system: a person
 * steers and keeps official charts (BINNACLE_ADVISORY ends every notice). */
#ifndef BINNACLE_MODEL_H
#define BINNACLE_MODEL_H

#include "binnacle_rules.h"
#include "zxm.h"

#define BINNACLE_WPTS       64u
#define BINNACLE_ROUTES     8u
#define BINNACLE_ROUTE_WPTS 32u
#define BINNACLE_HAZARDS    16u
#define BINNACLE_NAME_MAX   63u /* zxv_place.h ZPL_NAME_MAX */
#define BINNACLE_NOTE_MAX   255u
#define BINNACLE_NOTICE_MAX 384u
#define BINNACLE_P_SIGHT    INT64_C(999999) /* BM4: under a degree */

#define BINNACLE_ADVISORY                                                                          \
    "Advisory navigation aid, not a certified navigation system: a person steers and keeps "       \
    "official charts."

typedef struct {
    int64_t body;      /* 0 a star, 1 .. 8 a planet (Orrery's numbers) */
    int64_t dec, hour; /* whole degrees: -90 .. 90, 0 .. 359 */
    int64_t az;        /* azimuth, whole degrees 0 .. 359 */
    int64_t p;         /* intercept toward the body, millionths of a degree */
} binnacle_sight_t;

typedef struct {
    char name[BINNACLE_NAME_MAX + 1];
    int64_t lat, lon;
} binnacle_wpt_t;

typedef struct {
    int64_t lo, hi; /* a hazard sector, centidegrees, clockwise lo to hi */
} binnacle_hazard_t;

typedef struct {
    zxm_link_t *link;
    bool clock_known;
    int64_t now_s; /* sys.info, seconds since 1970 */
    bool here_known;
    int64_t here_lat, here_lon;
    binnacle_wpt_t wpt[BINNACLE_WPTS];
    uint32_t nwpt;
    uint32_t route[BINNACLE_ROUTES][BINNACLE_ROUTE_WPTS]; /* places in wpt[] */
    uint32_t nroute[BINNACLE_ROUTES];
    binnacle_hazard_t hz[BINNACLE_HAZARDS];
    uint32_t nhz;
    /* BM4 */
    bool star_known;
    int64_t star_lat, star_lon, star_time;
    rat_t star_east, star_north; /* off the assumed position, exact */
    int64_t planet_mdeg[2];      /* a planet sight's mean longitude, -1 a star */
    /* BM5 */
    bool clock_fix_known;
    int64_t dr_lat, dr_lon;
    /* BM6 */
    bool pos_known;
    binnacle_box_t pos;
} binnacle_t;

void binnacle_init(binnacle_t *b, zxm_link_t *link);
/* BM1 */
int binnacle_clock(binnacle_t *b);
/* BM2 */
int binnacle_here(binnacle_t *b);
int binnacle_places(binnacle_t *b);
int binnacle_add_waypoint(binnacle_t *b, const char *name, int64_t lat, int64_t lon,
                          const char *note, bool owner_confirmed);
/* BM3: put waypoint `wpt` (its place in b->wpt) last on route 1 .. */
int binnacle_route_add(binnacle_t *b, uint32_t route, uint32_t wpt);
/* BR4: mark a hazard sector */
int binnacle_mark_hazard(binnacle_t *b, int64_t lo, int64_t hi);
/* BM4: from the assumed position (ap_lat, ap_lon) at the clock's time */
int binnacle_star_fix(binnacle_t *b, int64_t ap_lat, int64_t ap_lon,
                      const binnacle_sight_t sight[2]);
/* BM5: from (lat, lon) at time `since` (seconds since 1970), course in
 * centidegrees, speed in metres an hour */
int binnacle_clock_fix(binnacle_t *b, int64_t lat, int64_t lon, int64_t since, int64_t course,
                       int64_t speed);
/* BM6 */
int binnacle_position(binnacle_t *b, int64_t tol_m);
int32_t binnacle_position_text(const binnacle_t *b, char *out, uint32_t cap);
/* BM7: bearing (centidegrees) and distance (metres) to each waypoint;
 * the number given in *n */
int binnacle_to_waypoints(const binnacle_t *b, int64_t *bearing, int64_t *range, uint32_t cap,
                          uint32_t *n);
int binnacle_heading_check(const binnacle_t *b, int64_t heading, uint32_t from, uint32_t to,
                           int64_t xtk_limit, bool *safe);
/* BM8: wind in tenths of a metre a second, temp in tenths of a degree C,
 * the zone's offset in minutes; the notice's length or a ZXM_E_* code */
int32_t binnacle_leg_notice(const binnacle_t *b, int64_t wind, int64_t temp, int64_t offset,
                            char *out, uint32_t cap);

#endif
