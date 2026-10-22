/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* orrery_model.h — Orrery, the sky from where the machine is, as a model
 * (zxm.h): the planets' mean longitudes today, which of the bright stars
 * are above the horizon now, and a log of the owner's observations. No
 * drawing, no allocation, integers only. The rules are orrery_rules.h
 * (OR1-OR5); the day of a time is Almanac's whole days from seconds and
 * the day count of a date is Solstice's (solstice_rules.h).
 *
 * zxvd has no sky data method, and none is invented: the planets' elements
 * and the stars' places are integer tables here and in the blocks. The
 * clock is sys.info's time_ms and the place is place.here.
 *
 *   OM1 REFRESH. orrery_refresh reads sys.info's time_ms, then place.here;
 *       both answers are checked whole before anything is kept: a time
 *       before 1970 or past year 9999, or a known place without a latitude
 *       of -90..90 degrees or a longitude of -180..180 degrees (in
 *       millionths), is refused and the model keeps what it had. An
 *       unknown place is kept as unknown.
 *   OM2 DAY. The day is floor(seconds / 86400), Almanac's whole days from
 *       seconds (almanac.days), counted from 1970-01-01.
 *   OM3 PLANETS. A planet's heliocentric mean longitude on that day (OR2).
 *   OM4 STARS. Each of the ORRERY_STARS bright stars (J2000 places in
 *       millidegrees) is above the horizon (OR3) at the place's latitude,
 *       its declination and its hour angle, the local sidereal time (OR5)
 *       less its right ascension, each floored to whole degrees. Needs the
 *       place: ZXM_E_STATE when it is not known.
 *   OM5 OBSERVATIONS. Up to ORRERY_OBS_MAX, each a body (1..8 a planet,
 *       101..100 + ORRERY_STARS a star), the time it was seen (the
 *       refreshed clock) and its altitude (-90000..90000) and azimuth
 *       (0..359999) in millidegrees, checked (orrery_observation_valid and
 *       the record's digits, OR4) before it is kept. A full log refuses
 *       (ZXM_E_STATE), never overwrites; forget removes one by number. */
#ifndef ORRERY_MODEL_H
#define ORRERY_MODEL_H

#include "orrery_rules.h"
#include "zxm.h"

#define ORRERY_STARS     8u
#define ORRERY_STAR_BASE 100
#define ORRERY_OBS_MAX   64u
#define ORRERY_OBS_ID    999999999 /* nine digits (OR4) */

typedef struct {
    const char *name;
    int64_t ra, dec; /* millidegrees, J2000 */
} orrery_star_t;

extern const orrery_star_t ORRERY_STAR[ORRERY_STARS];
extern const char *const ORRERY_PLANET_NAME[ORRERY_PLANETS];

typedef struct {
    int64_t id;   /* 1..ORRERY_OBS_ID */
    int64_t body; /* 1..8, or 101..100 + ORRERY_STARS */
    int64_t time; /* seconds since 1970 */
    int64_t alt;  /* millidegrees above the horizon, -90000..90000 */
    int64_t az;   /* millidegrees east of north, 0..359999 */
} orrery_obs_t;

typedef struct {
    zxm_link_t *link;
    bool have;      /* a refresh has succeeded (OM1) */
    int64_t now_s;  /* zxvd's clock, seconds since 1970 */
    int64_t day;    /* OM2 */
    bool place;     /* the place is known */
    int64_t lat_u6; /* millionths of a degree, north positive */
    int64_t lon_u6; /* millionths of a degree, east positive */
    orrery_obs_t obs[ORRERY_OBS_MAX];
    uint32_t nobs;
    int64_t next_id;
} orrery_t;

void orrery_init(orrery_t *o, zxm_link_t *link);
/* OM1 */
int orrery_refresh(orrery_t *o);

/* OM5's narrower check: a body Orrery knows, a time a clock shows, an
 * altitude and an azimuth in range. */
bool orrery_observation_valid(int64_t body, int64_t time, int64_t alt, int64_t az);

/* OM3: planet 1..8's mean longitude today, millidegrees, in *mdeg. */
int orrery_planet_now(const orrery_t *o, int64_t planet, int64_t *mdeg);
/* OR5 at the place now, millidegrees, in *mdeg. */
int orrery_sidereal_now(const orrery_t *o, int64_t *mdeg);
/* OM4: star 0..ORRERY_STARS-1 above the horizon now, in *up. */
int orrery_star_up(const orrery_t *o, uint32_t star, bool *up);
/* OM4: how many of the stars are up, and which (up may be NULL). */
int orrery_stars_up(const orrery_t *o, uint32_t *count, bool up[ORRERY_STARS]);

/* OM5: a new observation of body seen now; its number in *id. */
int orrery_observe(orrery_t *o, int64_t body, int64_t alt, int64_t az, int64_t *id);
int orrery_forget(orrery_t *o, int64_t id);
const orrery_obs_t *orrery_find(const orrery_t *o, int64_t id);

#endif
