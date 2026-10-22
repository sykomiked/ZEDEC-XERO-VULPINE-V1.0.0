/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Orrery's languages against the C its model runs (orrery_rules.h).
 * Slots: @num.orrery_civil_day, @num.orrery_planet_longitude,
 * @logic.orrery_sky_above_horizon, @logic.orrery_sky_observation_ok, and
 * through the parts it uses, @time.almanac_days and
 * @num.solstice_month_days. */
#include "orrery_rules.h"

/* One Lingua call per row (zxapp_lingua.c L1): KRON, FORTRAN 77 and Pascal
 * on one date, which must agree with the C. */
static bool orr_day_same(zxapp_t *app, int64_t y, int64_t m, int64_t d)
{
    int64_t yv[1] = {y}, mv[1] = {m}, dv[1] = {d}, w[1] = {orrery_civil_day(y, m, d)};
    zxapp_arg_t a[3] = {{"year", yv, 1}, {"month", mv, 1}, {"day", dv, 1}};
    if (w[0] != ORRERY_NO_DAY && wave_same(app, "civil_day", a, 3, w, 1, false)) return true;
    printf("    civil_day %lld-%lld-%lld\n", (long long) y, (long long) m, (long long) d);
    return false;
}

static int32_t orr_day_lp(zxapp_t *app, int64_t y, int64_t m, int64_t d)
{
    int64_t yv[1] = {y}, mv[1] = {m}, dv[1] = {d};
    zxapp_arg_t a[3] = {{"year", yv, 1}, {"month", mv, 1}, {"day", dv, 1}};
    return wave_lp(app, "civil_day", a, 3);
}

static bool orr_lon_same(zxapp_t *app, int64_t planet, int64_t day)
{
    int64_t pv[1] = {planet}, dv[1] = {day}, w[1] = {orrery_planet_longitude(planet, day)};
    zxapp_arg_t a[2] = {{"planet", pv, 1}, {"day", dv, 1}};
    if (w[0] >= 0 && wave_same(app, "planet_longitude", a, 2, w, 1, false)) return true;
    printf("    planet_longitude %lld day %lld\n", (long long) planet, (long long) day);
    return false;
}

static int32_t orr_lon_lp(zxapp_t *app, int64_t planet, int64_t day)
{
    int64_t pv[1] = {planet}, dv[1] = {day};
    zxapp_arg_t a[2] = {{"planet", pv, 1}, {"day", dv, 1}};
    return wave_lp(app, "planet_longitude", a, 2);
}

APP_TEST(orrery, 22, 101)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];

    /* the C's own day count: every day of years 1..9999 back to its date */
    bool cal = true;
    for (int64_t day = 0; day <= SOLSTICE_DAY_MAX && cal; day += day < 40000 ? 1 : 89) {
        int64_t y, m, dd;
        cal = solstice_date_of(day, &y, &m, &dd) && orrery_civil_day(y, m, dd) == day;
        if (!cal) printf("    day %lld\n", (long long) day);
    }
    CHECK(cal, "Orrery: civil_day is the inverse of the date of a day, 1970..9999");
    CHECK(orrery_civil_day(1970, 1, 1) == 0 && orrery_civil_day(1969, 12, 31) == -1 &&
              orrery_civil_day(2000, 1, 1) == ORRERY_DAY_2000 &&
              orrery_civil_day(1, 1, 1) == ORRERY_DAY_MIN &&
              orrery_civil_day(9999, 12, 31) == ORRERY_DAY_MAX &&
              orrery_civil_day(10000, 1, 1) == ORRERY_NO_DAY &&
              orrery_civil_day(0, 1, 1) == ORRERY_NO_DAY &&
              orrery_civil_day(-1, 1, 1) == ORRERY_NO_DAY &&
              orrery_civil_day(2023, 2, 29) == ORRERY_NO_DAY &&
              orrery_civil_day(2023, 13, 1) == ORRERY_NO_DAY &&
              orrery_civil_day(2023, 4, 0) == ORRERY_NO_DAY,
          "Orrery: civil_day, the C's own edges");

    /* civil_day (OR1): KRON = FORTRAN 77 = Pascal = C, one row per call, 64 dates */
    static const int64_t CD[][3] = {{1, 1, 1},     {1, 2, 28},     {4, 2, 29},    {100, 3, 1},
                                    {400, 2, 29},  {1582, 10, 15}, {1900, 2, 28}, {1969, 12, 31},
                                    {1970, 1, 1},  {2000, 2, 29},  {2000, 3, 1},  {2026, 10, 22},
                                    {9999, 12, 31}};
    bool all = true;
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t y, m, dd;
        if (r < sizeof CD / sizeof CD[0]) {
            y = CD[r][0], m = CD[r][1], dd = CD[r][2];
        } else {
            y = wrand(1, r % 2 ? 2100 : 9999);
            m = wrand(1, 12);
            dd = wrand(1, solstice_month_days(y, m));
        }
        all = orr_day_same(app, y, m, dd) && all;
    }
    CHECK(all, "Orrery: civil_day, KRON = FORTRAN 77 = Pascal = C, 64 dates");
    CHECK(orrery_civil_day(10000, 1, 1) == ORRERY_NO_DAY &&
              orr_day_lp(app, 10000, 1, 1) == ZXAPP_BOTH,
          "Orrery: civil_day in year 10000: FORTRAN 77 and Pascal refuse, KRON answers: BOTH");
    CHECK(orr_day_lp(app, 2023, 2, 29) == ZXAPP_BOTH && orr_day_lp(app, 0, 1, 1) == ZXAPP_BOTH &&
              orr_day_lp(app, -400, 3, 1) == ZXAPP_BOTH,
          "Orrery: civil_day, no such date or year 0 or before: refused by two, counted by KRON: "
          "BOTH");

    /* planet_longitude (OR2): KRON = FORTRAN 77 = C, one row per call, 64 rows */
    CHECK(orrery_planet_longitude(1, ORRERY_DAY_2000) == 250204 &&
              orrery_planet_longitude(1, ORRERY_DAY_2000 - 1) == 246111 &&
              orrery_planet_longitude(3, ORRERY_DAY_2000) == 99972 &&
              orrery_planet_longitude(8, ORRERY_DAY_MIN) == 258035 &&
              orrery_planet_longitude(4, ORRERY_DAY_MAX) == 107644 &&
              orrery_planet_longitude(0, 0) == ORRERY_REFUSED &&
              orrery_planet_longitude(9, 0) == ORRERY_REFUSED &&
              orrery_planet_longitude(-1, 0) == ORRERY_REFUSED &&
              orrery_planet_longitude(1, ORRERY_DAY_MAX + 1) == ORRERY_REFUSED &&
              orrery_planet_longitude(1, ORRERY_DAY_MIN - 1) == ORRERY_REFUSED,
          "Orrery: planet_longitude, the C's own edges");
    all = true;
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t p = (int64_t) (r % ORRERY_PLANETS) + 1;
        int64_t day = r < 8    ? ORRERY_DAY_2000
                      : r < 16 ? ORRERY_DAY_MIN
                      : r < 24 ? ORRERY_DAY_MAX
                      : r < 32 ? ORRERY_DAY_2000 - 1
                               : wrand(ORRERY_DAY_MIN, ORRERY_DAY_MAX);
        int64_t v = orrery_planet_longitude(p, day);
        all = v >= 0 && v < ORRERY_TURN_MDEG && orr_lon_same(app, p, day) && all;
    }
    CHECK(all, "Orrery: planet_longitude, KRON = FORTRAN 77 = C, 64 planet-days");
    CHECK(orr_lon_lp(app, 1, ORRERY_DAY_MAX + 1) == ZXAPP_BOTH &&
              orr_lon_lp(app, 5, ORRERY_DAY_MIN - 1) == ZXAPP_BOTH,
          "Orrery: planet_longitude past year 9999 or before year 1: FORTRAN 77 refuses, KRON "
          "answers: BOTH");
    CHECK(orr_lon_lp(app, 0, ORRERY_DAY_2000) == ZXAPP_NEITHER &&
              orr_lon_lp(app, 9, ORRERY_DAY_2000) == ZXAPP_NEITHER,
          "Orrery: planet_longitude, no such planet: both refuse");

    /* sky_above_horizon (OR3): KRON's 360-entry table = the C's quarter table, swept */
    bool sym = true;
    for (int64_t k = 0; k < 360; k++)
        sym = sym && orrery_sin(k) == -orrery_sin(-k) && orrery_sin(k) == orrery_sin(180 - k) &&
              orrery_cos(k) == orrery_sin(90 - k);
    CHECK(sym && orrery_sin(30) == 5000 && orrery_sin(90) == 10000 && orrery_sin(270) == -10000,
          "Orrery: the sine table's symmetries");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-90, 90);
        b[i] = wrand(-90, 90);
        c[i] = wrand(0, 359);
    }
    a[0] = 51, b[0] = 89, c[0] = 180; /* a pole star from London at its lowest: up */
    a[1] = 51, b[1] = -60, c[1] = 0;  /* never rises there */
    a[2] = 51, b[2] = 0, c[2] = 90;   /* on the horizon: not above */
    a[3] = 90, b[3] = 1, c[3] = 0;    /* from the pole, north of the equator */
    a[4] = 90, b[4] = -1, c[4] = 0;   /* and south of it */
    a[5] = -33, b[5] = -60, c[5] = 0; /* the southern sky from the south */
    a[6] = 91;                        /* latitude max + 1 */
    b[7] = -91;                       /* declination min - 1 */
    c[8] = 360;                       /* hour max + 1 */
    c[9] = -1;                        /* hour min - 1 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(orrery_above_horizon(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_TRUE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_TRUE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE && want[8] == ZXAPP_FALSE &&
              want[9] == ZXAPP_FALSE,
          "Orrery: sky_above_horizon, the C's own edges");
    zxapp_arg_t ha[3] = {{"lat", a, WAVE_ROWS}, {"dec", b, WAVE_ROWS}, {"hour", c, WAVE_ROWS}};
    CHECK(wave_same(app, "sky_above_horizon", ha, 3, want, WAVE_ROWS, true),
          "Orrery: sky_above_horizon, KRON = C (one language: NEITHER)");
    {
        /* every whole degree of hour angle at one latitude and declination */
        static int64_t la[360], de[360], ho[360], w2[360];
        zxapp_num_t out[360];
        uint32_t n = 0;
        int32_t lp = -1;
        for (int64_t k = 0; k < 360; k++) {
            la[k] = 40, de[k] = k % 2 ? -20 : 20, ho[k] = k;
            w2[k] = T_(orrery_above_horizon(40, de[k], k));
        }
        zxapp_arg_t fa[3] = {{"lat", la, 360}, {"dec", de, 360}, {"hour", ho, 360}};
        bool same = zxapp_call_witnessed(app, &H, "sky_above_horizon", fa, 3, out, 360, &n, &lp) ==
                        ZXAPP_OK &&
                    n == 360;
        for (uint32_t k = 0; same && k < 360; k++) same = out[k].num == w2[k] && out[k].den == 0;
        CHECK(same, "Orrery: sky_above_horizon over every degree of the KRON table = C");
    }

    /* sky_observation_ok (OR4): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 999999999);
        b[i] = i % 2 ? wrand(1, 8) : wrand(101, 108);
        c[i] = wrand(0, SOLSTICE_SECONDS_MAX);
        d[i] = wrand(-90000, 90000);
        e[i] = wrand(0, 359999);
    }
    a[0] = 1, b[0] = 1, c[0] = 0, d[0] = 0, e[0] = 0; /* every least */
    a[1] = 999999999, b[1] = 9999, c[1] = 999999999999, d[1] = 999999, e[1] = 999999; /* most */
    a[2] = 1000000000;             /* obs max + 1 */
    b[3] = 10000;                  /* body max + 1 */
    c[4] = INT64_C(1000000000000); /* time max + 1 */
    d[5] = 1000000;                /* alt max + 1 */
    e[6] = 1000000;                /* az max + 1 */
    d[7] = -999999;                /* sign aside */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(orrery_record_fits(a[i], b[i], c[i], d[i], e[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_TRUE,
          "Orrery: sky_observation_ok, the C's own edges");
    zxapp_arg_t oa[5] = {{"obs", a, WAVE_ROWS},
                         {"body", b, WAVE_ROWS},
                         {"time", c, WAVE_ROWS},
                         {"alt", d, WAVE_ROWS},
                         {"az", e, WAVE_ROWS}};
    CHECK(wave_same(app, "sky_observation_ok", oa, 5, want, WAVE_ROWS, true),
          "Orrery: sky_observation_ok, MYRA = C");

    /* the day of zxvd's clock, through almanac.days: floor(seconds / 86400) */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, SOLSTICE_SECONDS_MAX);
        want[i] = a[i] / SOLSTICE_DAY_S;
    }
    a[0] = 0, want[0] = 0;
    a[1] = SOLSTICE_DAY_S - 1, want[1] = 0;
    a[2] = SOLSTICE_DAY_S, want[2] = 1;
    a[3] = SOLSTICE_SECONDS_MAX, want[3] = SOLSTICE_DAY_MAX;
    zxapp_arg_t da[1] = {{"seconds", a, WAVE_ROWS}};
    CHECK(wave_same(app, "almanac.days", da, 1, want, WAVE_ROWS, false),
          "Orrery: the clock's day, almanac.days = C");

    /* a date's day inside its month, through solstice.month_days */
    int64_t yv[1] = {2000}, mv[1] = {2}, w1[1] = {29};
    zxapp_arg_t ma[2] = {{"year", yv, 1}, {"month", mv, 1}};
    CHECK(wave_same(app, "solstice.month_days", ma, 2, w1, 1, false) &&
              solstice_month_days(2000, 2) == 29,
          "Orrery: February 2000 through solstice.month_days = C");
}
