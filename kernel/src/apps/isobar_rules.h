/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* isobar_rules.h — Isobar's rules in C, the reference its Eve, KRON,
 * Trebago and MYRA blocks are checked against (blocks/isobar). Pure
 * integer functions, no state: the model (isobar_model.c) and the language
 * test (tests/isobar_zxapp.inc) both include this.
 *
 *   IR1 ACCEPT A READING (reading_accept_ok): a sensor's reading is taken
 *       when it lies inside the sensor's range lo..hi and moved no more
 *       than the step limit since the sensor's last accepted reading. Eve
 *       holds each field to its own range, so the rule comes as four
 *       margins, each 0..ISOBAR_MARGIN_MAX: above = reading - lo, below =
 *       hi - reading, rise = step - (reading - last) and fall = step -
 *       (last - reading); a first reading has no last and both step
 *       margins are the step itself (isobar_margins). A sensor's range is
 *       at most ISOBAR_MARGIN_MAX wide and its step at most
 *       ISOBAR_STEP_MAX, so a reading the rule takes never has a margin
 *       past ISOBAR_MARGIN_MAX; a margin past it is held (Eve refuses,
 *       KRON reads any margin at or above 0 as inside).
 *   IR2 A SHORT OUTLOOK (pressure_outlook) from the 3-hour pressure
 *       tendency in tenths of a hPa and the pressure now in tenths of a
 *       hPa, by a rule table after the barometric tendency terms
 *       (steady or slowly up to 1.5 hPa in 3 hours, very rapidly past
 *       6.0 hPa):
 *         1 storm likely        tendency at or below -60
 *         2 rain likely         -59 .. -16
 *         3 unsettled           -15 .. 15 and pressure below 10000
 *         4 no change           -15 .. 15 and pressure 10000 .. 10199
 *         5 fair                -15 .. 15 and pressure at or above 10200
 *         6 improving           16 .. 59
 *         7 clearing, windy     at or above 60
 *       tendency -500 .. 500 and pressure 8700 .. 10850 (870.0 to 1085.0
 *       hPa, the observed extremes), else refused (Trebago refuses, KRON
 *       answers: held). Advisory only: official warnings take precedence.
 *   IR3 THE STATION GRID (station_hour_high): row i's cell of the grid of
 *       stations by hours (hours since 1970), the highest reading of
 *       every row with its station and its hour, itself included. A
 *       reading at or below ISOBAR_GRID_FLOOR is refused (KRON's empty
 *       cell is that floor).
 *   IR4 AN OBSERVATION (wx_obs_record_ok) fits ^ZXISOBAR(station, stamp,
 *       kind) = reading ^ state: station at most 4 digits, stamp
 *       (seconds since 1970) 11, kind 1, reading 7 and state 1, sign
 *       aside.
 *   IR5 A SITE NODE (wx_site_node_ok) fits ^ZXISOSITE(parent, site) =
 *       kind ^ elev: parent and site at most 4 digits, kind 1 and elev
 *       (metres) 5, sign aside.
 *
 * Anything else is refused (ISOBAR_REFUSED), never rounded or wrapped. */
#ifndef ISOBAR_RULES_H
#define ISOBAR_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define ISOBAR_REFUSED    INT64_MIN
#define ISOBAR_MARGIN_MAX INT64_C(999999) /* IR1: Eve's reading_gap */
#define ISOBAR_STEP_MAX   INT64_C(499999) /* IR1: a step limit, half the widest range */
#define ISOBAR_POINT_MAX  INT64_C(999999) /* a sensor's lo, hi and its last reading */
#define ISOBAR_TEND_MAX   INT64_C(500)    /* IR2: 50.0 hPa in 3 hours */
#define ISOBAR_PRESS_MIN  INT64_C(8700)   /* IR2: 870.0 hPa */
#define ISOBAR_PRESS_MAX  INT64_C(10850)  /* IR2: 1085.0 hPa */
#define ISOBAR_GRID_FLOOR INT64_C(-999999999999999999) /* IR3: KRON's empty cell */
#define ISOBAR_ROWS_MAX   4096u

/* IR2's answers */
enum {
    ISOBAR_OUT_STORM = 1,
    ISOBAR_OUT_RAIN = 2,
    ISOBAR_OUT_UNSETTLED = 3,
    ISOBAR_OUT_NO_CHANGE = 4,
    ISOBAR_OUT_FAIR = 5,
    ISOBAR_OUT_IMPROVING = 6,
    ISOBAR_OUT_CLEARING = 7,
};

/* IR1 */
static inline bool isobar_margin(int64_t m)
{
    return m >= 0 && m <= ISOBAR_MARGIN_MAX;
}

static inline bool isobar_reading_accept_ok(int64_t above, int64_t below, int64_t rise,
                                            int64_t fall)
{
    return isobar_margin(above) && isobar_margin(below) && isobar_margin(rise) &&
           isobar_margin(fall);
}

/* IR1: the four margins of a reading; every input inside
 * -ISOBAR_POINT_MAX..ISOBAR_POINT_MAX (step 0..ISOBAR_STEP_MAX), else
 * false and m untouched. has_last 0 for a sensor's first reading. */
static inline bool isobar_margins(int64_t reading, int64_t lo, int64_t hi, int64_t last,
                                  int64_t step, bool has_last, int64_t m[4])
{
    if (!m || reading < -ISOBAR_POINT_MAX || reading > ISOBAR_POINT_MAX || lo < -ISOBAR_POINT_MAX ||
        lo > ISOBAR_POINT_MAX || hi < -ISOBAR_POINT_MAX || hi > ISOBAR_POINT_MAX ||
        last < -ISOBAR_POINT_MAX || last > ISOBAR_POINT_MAX || step < 0 || step > ISOBAR_STEP_MAX)
        return false;
    m[0] = reading - lo;
    m[1] = hi - reading;
    m[2] = has_last ? step - (reading - last) : step;
    m[3] = has_last ? step - (last - reading) : step;
    return true;
}

/* IR2 */
static inline int64_t isobar_pressure_outlook(int64_t tendency, int64_t pressure)
{
    if (tendency < -ISOBAR_TEND_MAX || tendency > ISOBAR_TEND_MAX || pressure < ISOBAR_PRESS_MIN ||
        pressure > ISOBAR_PRESS_MAX)
        return ISOBAR_REFUSED;
    if (tendency <= -60) return ISOBAR_OUT_STORM;
    if (tendency <= -16) return ISOBAR_OUT_RAIN;
    if (tendency >= 60) return ISOBAR_OUT_CLEARING;
    if (tendency >= 16) return ISOBAR_OUT_IMPROVING;
    if (pressure < 10000) return ISOBAR_OUT_UNSETTLED;
    if (pressure < 10200) return ISOBAR_OUT_NO_CHANGE;
    return ISOBAR_OUT_FAIR;
}

/* IR3: row i's cell, or ISOBAR_REFUSED */
static inline int64_t isobar_station_hour_high(const int64_t *station, const int64_t *hour,
                                               const int64_t *reading, uint32_t n, uint32_t i)
{
    if (!station || !hour || !reading || i >= n || n > ISOBAR_ROWS_MAX) return ISOBAR_REFUSED;
    int64_t high = ISOBAR_GRID_FLOOR;
    for (uint32_t j = 0; j < n; j++) {
        if (reading[j] <= ISOBAR_GRID_FLOOR) return ISOBAR_REFUSED;
        if (station[j] == station[i] && hour[j] == hour[i] && reading[j] > high) high = reading[j];
    }
    return high;
}

/* v fits in `digits` decimal digits, sign aside (MYRA's iN) */
static inline bool isobar_digits(int64_t v, uint32_t digits)
{
    int64_t lim = 1;
    if (v == INT64_MIN) return false;
    for (uint32_t i = 0; i < digits && lim <= INT64_MAX / 10; i++) lim *= 10;
    return (v < 0 ? -v : v) < lim;
}

/* IR4 */
static inline bool isobar_obs_record_ok(int64_t station, int64_t stamp, int64_t kind,
                                        int64_t reading, int64_t state)
{
    return isobar_digits(station, 4) && isobar_digits(stamp, 11) && isobar_digits(kind, 1) &&
           isobar_digits(reading, 7) && isobar_digits(state, 1);
}

/* IR5 */
static inline bool isobar_site_node_ok(int64_t parent, int64_t site, int64_t kind, int64_t elev)
{
    return isobar_digits(parent, 4) && isobar_digits(site, 4) && isobar_digits(kind, 1) &&
           isobar_digits(elev, 5);
}

#endif
