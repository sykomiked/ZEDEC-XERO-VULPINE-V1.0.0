/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* orrery_rules.h — Orrery's rules in C, the reference its KRON, FORTRAN 77,
 * Pascal and MYRA blocks are checked against (blocks/orrery). Pure integer
 * functions and integer tables, no state: the model (orrery_model.c) and
 * the language test (tests/orrery_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 *   OR1 DAY NUMBER of a Gregorian date: the days since 1970-01-01 (a date
 *       before it is negative), years 1..9999, the day inside its month
 *       (Solstice's SO1). It is Solstice's own day count
 *       (solstice_day_of, days_from_civil), which Solstice keeps in C;
 *       Orrery gives it as an entry. ORRERY_NO_DAY when no such date.
 *   OR2 MEAN LONGITUDE of a planet, 1 Mercury .. 8 Neptune, heliocentric,
 *       in millidegrees 0..359999 on day `day` (days since 1970, years
 *       1..9999: ORRERY_DAY_MIN..ORRERY_DAY_MAX): its longitude at
 *       2000-01-01 0h (day ORRERY_DAY_2000) plus its rate in microdegrees
 *       a day times the days since, floored to millidegrees (toward minus
 *       infinity), taken mod 360000 (never negative). The table is the
 *       J2000 mean elements (Standish's approximate positions), each
 *       rounded once to an integer here: a mean longitude, not where the
 *       planet is to the arcminute. -1 when refused.
 *   OR3 ABOVE THE HORIZON: a body at declination `dec` seen from latitude
 *       `lat` at hour angle `hour`, whole degrees (lat and dec -90..90,
 *       hour 0..359), is above the horizon when
 *       sin(lat) sin(dec) + cos(lat) cos(dec) cos(hour) > 0, every sine
 *       and cosine from ORRERY_SIN's table of 10000 sin(k) rounded, each
 *       product exact: sn(lat) * sn(dec) * 10000 + cs(lat) * cs(dec) *
 *       cs(hour) above 0. On the horizon (0) is not above. Advisory: no
 *       refraction, no parallax.
 *   OR4 AN OBSERVATION RECORD of ^ZXOBS holds integers of at most the
 *       digits its pieces declare, sign aside: obs 9, body 4, time 12,
 *       alt 6, az 6 (blocks/orrery/observation.myra). The model's own
 *       check (orrery_observation_valid) is narrower.
 *
 * The local sidereal time (OR5) and the hour angle are kept in C: they are
 * the model's arithmetic between zxvd's clock and OR3, not a decision. */
#ifndef ORRERY_RULES_H
#define ORRERY_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "solstice_rules.h"

#define ORRERY_NO_DAY    INT64_MIN
#define ORRERY_REFUSED   (-1)
#define ORRERY_PLANETS   8
#define ORRERY_DAY_MIN   (-719162) /* 0001-01-01 */
#define ORRERY_DAY_MAX   2932896   /* 9999-12-31 (SOLSTICE_DAY_MAX) */
#define ORRERY_DAY_2000  10957     /* 2000-01-01 */
#define ORRERY_TURN_MDEG 360000
#define ORRERY_TURN_UDEG INT64_C(360000000)

/* OR2: longitude at 2000-01-01 0h in millidegrees, rate in microdegrees a
 * day; the same numbers as blocks/orrery/planet_longitude.{kron,f}. */
static const int64_t ORRERY_L0[ORRERY_PLANETS] = {250204, 181178, 99972,  355185,
                                                  34355,  49938,  313232, 304877};
static const int64_t ORRERY_RATE[ORRERY_PLANETS] = {4092339, 1602130, 985609, 524033,
                                                    83087,   33470,   11731,  5981};

/* 10000 sin(k) rounded, k = 0..90 degrees; the rest by symmetry (the same
 * 360 numbers as blocks/orrery/above_horizon.kron's table). */
static const int64_t ORRERY_SIN[91] = {
    0,    175,  349,  523,  698,  872,  1045, 1219, 1392, 1564, 1736, 1908, 2079, 2250, 2419, 2588,
    2756, 2924, 3090, 3256, 3420, 3584, 3746, 3907, 4067, 4226, 4384, 4540, 4695, 4848, 5000, 5150,
    5299, 5446, 5592, 5736, 5878, 6018, 6157, 6293, 6428, 6561, 6691, 6820, 6947, 7071, 7193, 7314,
    7431, 7547, 7660, 7771, 7880, 7986, 8090, 8192, 8290, 8387, 8480, 8572, 8660, 8746, 8829, 8910,
    8988, 9063, 9135, 9205, 9272, 9336, 9397, 9455, 9511, 9563, 9613, 9659, 9703, 9744, 9781, 9816,
    9848, 9877, 9903, 9925, 9945, 9962, 9976, 9986, 9994, 9998, 10000};

/* floor(a / b) and a mod b in 0..b-1, b > 0 */
static inline int64_t orrery_floor_div(int64_t a, int64_t b)
{
    int64_t q = a / b;
    return (a % b != 0 && a < 0) ? q - 1 : q;
}

static inline int64_t orrery_mod(int64_t a, int64_t b)
{
    int64_t r = a % b;
    return r < 0 ? r + b : r;
}

/* OR1 */
static inline int64_t orrery_civil_day(int64_t year, int64_t month, int64_t day)
{
    if (year < 1 || year > 9999) return ORRERY_NO_DAY;
    int64_t d = solstice_day_of(year, month, day);
    return d == INT64_MIN ? ORRERY_NO_DAY : d;
}

/* OR2 */
static inline int64_t orrery_planet_longitude(int64_t planet, int64_t day)
{
    if (planet < 1 || planet > ORRERY_PLANETS || day < ORRERY_DAY_MIN || day > ORRERY_DAY_MAX)
        return ORRERY_REFUSED;
    int64_t moved = ORRERY_RATE[planet - 1] * (day - ORRERY_DAY_2000); /* |.| < 2^44 */
    return orrery_mod(ORRERY_L0[planet - 1] + orrery_floor_div(moved, 1000), ORRERY_TURN_MDEG);
}

/* 10000 sin(deg) from the quarter table, any whole degree */
static inline int64_t orrery_sin(int64_t deg)
{
    int64_t k = orrery_mod(deg, 360);
    if (k <= 90) return ORRERY_SIN[k];
    if (k <= 180) return ORRERY_SIN[180 - k];
    if (k <= 270) return -ORRERY_SIN[k - 180];
    return -ORRERY_SIN[360 - k];
}

static inline int64_t orrery_cos(int64_t deg)
{
    return orrery_sin(deg + 90);
}

/* OR3 */
static inline bool orrery_above_horizon(int64_t lat, int64_t dec, int64_t hour)
{
    if (lat < -90 || lat > 90 || dec < -90 || dec > 90 || hour < 0 || hour > 359) return false;
    int64_t v = orrery_sin(lat) * orrery_sin(dec) * 10000 +
                orrery_cos(lat) * orrery_cos(dec) * orrery_cos(hour);
    return v > 0;
}

/* OR4: does x have at most `digits` digits, sign aside? */
static inline bool orrery_digits_fit(int64_t x, uint32_t digits)
{
    int64_t lim = 1;
    for (uint32_t i = 0; i < digits && i < 18u; i++) lim *= 10;
    return x > -lim && x < lim;
}

static inline bool orrery_record_fits(int64_t obs, int64_t body, int64_t time, int64_t alt,
                                      int64_t az)
{
    return orrery_digits_fit(obs, 9) && orrery_digits_fit(body, 4) && orrery_digits_fit(time, 12) &&
           orrery_digits_fit(alt, 6) && orrery_digits_fit(az, 6);
}

/* OR5: the local mean sidereal time in millidegrees 0..359999 at `seconds`
 * since 1970 (0..SOLSTICE_SECONDS_MAX) and east longitude `lon_udeg`
 * (microdegrees, -180000000..180000000): 280.460618 degrees at
 * 2000-01-01 12:00 UTC (946728000) plus 360.985647 degrees a day, each in
 * whole microdegrees, plus the longitude, floored to millidegrees. -1 when
 * refused. */
static inline int64_t orrery_sidereal(int64_t seconds, int64_t lon_udeg)
{
    if (seconds < 0 || seconds > SOLSTICE_SECONDS_MAX || lon_udeg < -180000000 ||
        lon_udeg > 180000000)
        return ORRERY_REFUSED;
    int64_t since = seconds - 946728000;
    int64_t days = orrery_floor_div(since, SOLSTICE_DAY_S);
    int64_t rem = since - days * SOLSTICE_DAY_S;
    int64_t u = 280460618 + orrery_mod(days * 360985647, ORRERY_TURN_UDEG) +
                rem * 360985647 / SOLSTICE_DAY_S + lon_udeg;
    return orrery_mod(u, ORRERY_TURN_UDEG) / 1000;
}

#endif
