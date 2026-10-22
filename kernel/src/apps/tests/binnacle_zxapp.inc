/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Binnacle's languages, witnessed, row by row against binnacle_rules.h,
 * the C the model runs; the parts it composes (waymark, weatherglass,
 * orrery, sundial, gnomon) resolved and called; a fix from two sights
 * through the languages equal to the C's exact fraction; and the BOTH of
 * a star fix and a clock fix 1000 m apart with a 500 m tolerance, plotted
 * as the box holding both. Slots: @num.binnacle_lop_fix_offset,
 * @num.binnacle_wpt_bearing_cdeg, @num.binnacle_wpt_range_m,
 * @num.binnacle_hazard_margin, @logic.binnacle_heading_safe,
 * @num.binnacle_leg_run_m, @logic.binnacle_waypoint_node_ok; and, used,
 * @logic.waymark_place_ok, @num.weatherglass_wind_kmh_x10,
 * @num.weatherglass_warn_flags, @num.orrery_civil_day,
 * @num.orrery_planet_longitude, @logic.orrery_sky_above_horizon,
 * @num.sundial_zone_minute, @num.gnomon_triangle_area2. */
#include "binnacle_rules.h"

/* the lop_fix_offset rows as the languages answer them (exact fractions),
 * each against the C's; lp TRUE when Trebago and KRON agree */
static bool bn_fix_rows(zxapp_t *a, const zxapp_arg_t *args, const rat_t *want, uint32_t rows)
{
    zxapp_num_t o[WAVE_ROWS];
    uint32_t k = 0;
    int32_t lp = -1;
    int rc = zxapp_call_witnessed(a, &H, "lop_fix_offset", args, 7, o, WAVE_ROWS, &k, &lp);
    if (rc != ZXAPP_OK || lp != ZXAPP_TRUE || k != rows) {
        printf("    lop_fix_offset: rc %d lp %d n %u (%s)\n", rc, (int) lp, k, a->err);
        return false;
    }
    for (uint32_t i = 0; i < rows; i++)
        if (!want[i].valid || o[i].num != want[i].num || o[i].den != want[i].den) {
            printf("    lop_fix_offset row %u: %lld/%lld, want %lld/%lld\n", i,
                   (long long) o[i].num, (long long) o[i].den, (long long) want[i].num,
                   (long long) want[i].den);
            return false;
        }
    return true;
}

APP_TEST(binnacle, 50, 147)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], g[WAVE_ROWS], want[WAVE_ROWS];
    static rat_t rw[WAVE_ROWS];

    /* compose: five parts, every part entry it calls resolves to its part */
    static const char *const used[8][2] = {{"waymark.place_ok", "waymark"},
                                           {"weatherglass.wind_kmh_x10", "weatherglass"},
                                           {"weatherglass.warn_flags", "weatherglass"},
                                           {"orrery.civil_day", "orrery"},
                                           {"orrery.planet_longitude", "orrery"},
                                           {"orrery.sky_above_horizon", "orrery"},
                                           {"sundial.zone_minute", "sundial"},
                                           {"gnomon.triangle_area2", "gnomon"}};
    CHECK(app->resolved && app->nuse == 5 && app->composed == 11 && app->n == 50u &&
              app->n <= ZXAPP_LOGIC_MAX && app->nuse <= ZXAPP_USES,
          "Binnacle: composed of %u parts, %u own lines and %u of theirs, within %u", app->nuse,
          app->composed, app->n - app->composed, (unsigned) ZXAPP_LOGIC_MAX);
    for (uint32_t i = 0; i < 8u; i++) {
        const zxapp_logic_t *L = zxapp_find(app, used[i][0]);
        CHECK(L && L->origin && !strcmp(L->origin->id, used[i][1]) && L->loaded &&
                  zxapp_contract(app, used[i][0]),
              "Binnacle: %s resolves to %s and loads", used[i][0], used[i][1]);
    }

    /* BR1: lop_fix_offset, Trebago = KRON = C (rational) */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t z1 = wrand(0, 359), z2;
        a[i] = orrery_sin(z1);
        b[i] = orrery_cos(z1);
        do { /* two azimuths whose lines cross */
            z2 = wrand(0, 359);
            d[i] = orrery_sin(z2);
            e[i] = orrery_cos(z2);
        } while (a[i] * e[i] == d[i] * b[i]);
        c[i] = i % 3u ? wrand(-BINNACLE_P_MAX, BINNACLE_P_MAX)
                      : BINNACLE_UNIT * wrand(-999999, 999999);
        f[i] = wrand(-BINNACLE_P_MAX, BINNACLE_P_MAX);
        g[i] = wrand(0, 1);
        if (i == 0)
            a[i] = 0, b[i] = BINNACLE_UNIT, c[i] = 500000000, d[i] = BINNACLE_UNIT, e[i] = 0;
        if (i == 0) f[i] = 300000000, g[i] = 0;
        if (i == 1) a[i] = -BINNACLE_UNIT, b[i] = -BINNACLE_UNIT, c[i] = -BINNACLE_P_MAX;
        if (i == 1) d[i] = BINNACLE_UNIT, e[i] = -BINNACLE_UNIT, f[i] = BINNACLE_P_MAX, g[i] = 1;
        rw[i] = binnacle_lop_fix(a[i], b[i], c[i], d[i], e[i], f[i], g[i]);
    }
    zxapp_arg_t fa[7] = {{"sa", a, WAVE_ROWS},  {"ca", b, WAVE_ROWS}, {"pa", c, WAVE_ROWS},
                         {"sb", d, WAVE_ROWS},  {"cb", e, WAVE_ROWS}, {"pb", f, WAVE_ROWS},
                         {"axis", g, WAVE_ROWS}};
    CHECK(rw[0].valid && rw[0].num == 30000 && rw[0].den == 1 && rw[1].valid && rw[1].num == 0 &&
              rw[1].den == 1 && bn_fix_rows(app, fa, rw, WAVE_ROWS),
          "Binnacle: lop_fix_offset, Trebago = KRON = C, exact fractions");
    {
        static const int64_t s0[2] = {0, 0}, s1[2] = {0, BINNACLE_UNIT + 1},
                             cu[2] = {BINNACLE_UNIT, BINNACLE_UNIT}, p5[2] = {500000000, 500000000},
                             su[2] = {BINNACLE_UNIT, BINNACLE_UNIT}, p3[2] = {300000000, 300000000},
                             ax0[2] = {0, 0}, ax2[2] = {0, 2},
                             pbig[2] = {500000000, BINNACLE_P_MAX + 1};
        zxapp_arg_t b1[7] = {{"sa", s1, 2}, {"ca", cu, 2}, {"pa", p5, 2},   {"sb", su, 2},
                             {"cb", s0, 2}, {"pb", p3, 2}, {"axis", ax0, 2}};
        zxapp_arg_t b2[7] = {{"sa", s0, 2}, {"ca", cu, 2}, {"pa", p5, 2},   {"sb", su, 2},
                             {"cb", s0, 2}, {"pb", p3, 2}, {"axis", ax2, 2}};
        zxapp_arg_t b3[7] = {{"sa", s0, 2}, {"ca", cu, 2}, {"pa", pbig, 2}, {"sb", su, 2},
                             {"cb", s0, 2}, {"pb", p3, 2}, {"axis", ax0, 2}};
        CHECK(
            !binnacle_lop_fix(BINNACLE_UNIT + 1, BINNACLE_UNIT, 1, BINNACLE_UNIT, 0, 1, 0).valid &&
                !binnacle_lop_fix(0, BINNACLE_UNIT, 1, 0, BINNACLE_UNIT, 1, 0).valid &&
                wave_lp(app, "lop_fix_offset", b1, 7) == ZXAPP_BOTH &&
                wave_lp(app, "lop_fix_offset", b2, 7) == ZXAPP_BOTH &&
                wave_lp(app, "lop_fix_offset", b3, 7) == ZXAPP_BOTH,
            "Binnacle: a sine, an intercept or an axis outside: Trebago refuses, KRON answers: "
            "BOTH; parallel lines give no fix");
    }

    /* BR2, BR3: wpt_bearing_cdeg and wpt_range_m, KRON = C over the waypoint vectors */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2u ? wrand(-BINNACLE_D_MAX, BINNACLE_D_MAX) : wrand(-5000, 5000);
        b[i] = i % 3u ? wrand(-BINNACLE_D_MAX, BINNACLE_D_MAX) : wrand(-5000, 5000);
        if (i == 0) a[i] = BINNACLE_D_MAX, b[i] = -BINNACLE_D_MAX;
        if (i == 1) a[i] = 1000, b[i] = 9;
        if (i == 2) a[i] = 0, b[i] = -1;
        if (i == 3) a[i] = -7, b[i] = 0;
        if (a[i] == 0 && b[i] == 0) a[i] = 1;
        want[i] = binnacle_bearing(a[i], b[i]);
        c[i] = binnacle_range(a[i], b[i]);
    }
    zxapp_arg_t wa[2] = {{"dn", a, WAVE_ROWS}, {"de", b, WAVE_ROWS}};
    CHECK(want[0] == 31500 && want[1] == 100 && want[2] == 27000 && want[3] == 18000 &&
              wave_same(app, "wpt_bearing_cdeg", wa, 2, want, WAVE_ROWS, false),
          "Binnacle: wpt_bearing_cdeg, KRON = C");
    CHECK(c[0] == 28284271 && c[1] == 1000 && c[2] == 1 && c[3] == 7 &&
              wave_same(app, "wpt_range_m", wa, 2, c, WAVE_ROWS, false),
          "Binnacle: wpt_range_m, KRON = C, floored");
    {
        bool ok = true;
        for (int64_t deg = 0; deg < 360 && ok; deg++) { /* every whole degree round the card */
            int64_t dn = orrery_cos(deg) * 1000, de = orrery_sin(deg) * 1000;
            ok = binnacle_bearing(dn, de) == 100 * deg;
        }
        CHECK(ok && binnacle_bearing(0, 0) == BINNACLE_REFUSED &&
                  binnacle_bearing(BINNACLE_D_MAX + 1, 0) == BINNACLE_REFUSED &&
                  binnacle_range(0, -BINNACLE_D_MAX - 1) == BINNACLE_REFUSED &&
                  binnacle_root(0) == 0 && binnacle_root(24) == 4 && binnacle_root(25) == 5,
              "Binnacle: every whole degree round the card, and the edges");
    }

    /* BR4: hazard_margin, KRON = Trebago = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, BINNACLE_TURN - 1);
        b[i] = wrand(0, BINNACLE_TURN - 1);
        c[i] = i % 2u ? wrand(0, BINNACLE_TURN - 1) : (b[i] + wrand(0, 3000)) % BINNACLE_TURN;
        if (i == 0) a[i] = 100, b[i] = 35000, c[i] = 1000;
        if (i == 1) a[i] = 5000, b[i] = 35000, c[i] = 1000;
        if (i == 2) a[i] = BINNACLE_TURN - 1, b[i] = 0, c[i] = BINNACLE_TURN - 1;
        want[i] = binnacle_hazard_margin(a[i], b[i], c[i]);
    }
    zxapp_arg_t ha[3] = {{"heading", a, WAVE_ROWS}, {"lo", b, WAVE_ROWS}, {"hi", c, WAVE_ROWS}};
    CHECK(want[0] == -900 && want[1] == 4000 && want[2] == 0 &&
              wave_same(app, "hazard_margin", ha, 3, want, WAVE_ROWS, false),
          "Binnacle: hazard_margin, KRON = Trebago = C");
    {
        static const int64_t h2[2] = {100, BINNACLE_TURN}, l2[2] = {0, 0}, e2[2] = {50, 50},
                             lneg[2] = {0, -1};
        zxapp_arg_t m1[3] = {{"heading", h2, 2}, {"lo", l2, 2}, {"hi", e2, 2}};
        zxapp_arg_t m2[3] = {{"heading", e2, 2}, {"lo", lneg, 2}, {"hi", e2, 2}};
        CHECK(binnacle_hazard_margin(BINNACLE_TURN, 0, 50) == BINNACLE_REFUSED &&
                  wave_lp(app, "hazard_margin", m1, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "hazard_margin", m2, 3) == ZXAPP_BOTH,
              "Binnacle: a bearing past the card: Trebago refuses, KRON answers: BOTH");
    }

    /* BR5: heading_safe, Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-2, BINNACLE_TURN + 1);
        b[i] = i % 3u ? wrand(1, BINNACLE_TURN - 1) : wrand(-BINNACLE_TURN, BINNACLE_TURN);
        c[i] = i % 4u ? wrand(0, BINNACLE_ROOM_MAX) : wrand(-5, BINNACLE_ROOM_MAX + 5);
        if (i == 0) a[i] = 0, b[i] = 1, c[i] = 0;
        if (i == 1) a[i] = BINNACLE_TURN - 1, b[i] = BINNACLE_TURN - 1, c[i] = BINNACLE_ROOM_MAX;
        if (i == 2) a[i] = 9000, b[i] = 0, c[i] = 10;
        if (i == 3) a[i] = 9000, b[i] = 5, c[i] = -1;
        want[i] = T_(binnacle_heading_safe(a[i], b[i], c[i]));
    }
    zxapp_arg_t sa[3] = {
        {"heading", a, WAVE_ROWS}, {"margin", b, WAVE_ROWS}, {"xtkroom", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE &&
              wave_same(app, "heading_safe", sa, 3, want, WAVE_ROWS, true),
          "Binnacle: heading_safe, Eve = KRON = C");

    /* BR6: leg_run_m, Sutra = Trebago = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, BINNACLE_SPEED_MAX);
        b[i] = wrand(0, 359999);
        if (i == 0) a[i] = BINNACLE_KNOT_MH, b[i] = 3600;
        if (i == 1) a[i] = BINNACLE_SPEED_MAX, b[i] = 359999;
        if (i == 2) a[i] = 0, b[i] = 0;
        if (i == 3) a[i] = BINNACLE_SPEED_MAX, b[i] = 1;
        want[i] = binnacle_leg_run(a[i], b[i]);
    }
    zxapp_arg_t ra[2] = {{"speed", a, WAVE_ROWS}, {"secs", b, WAVE_ROWS}};
    CHECK(want[0] == 1852 && want[1] == 99999622 && want[2] == 0 && want[3] == 277 &&
              wave_same(app, "leg_run_m", ra, 2, want, WAVE_ROWS, false),
          "Binnacle: leg_run_m, Sutra = Trebago = C");
    {
        static const int64_t sp[2] = {BINNACLE_SPEED_MAX, BINNACLE_SPEED_MAX},
                             se[2] = {BINNACLE_SECS_MAX, 1}, s360[2] = {1852, 360001};
        zxapp_arg_t o1[2] = {{"speed", sp, 2}, {"secs", se, 2}};
        zxapp_arg_t o2[2] = {{"speed", sp, 2}, {"secs", s360, 2}};
        CHECK(binnacle_leg_run(BINNACLE_SPEED_MAX, BINNACLE_SECS_MAX) == BINNACLE_REFUSED &&
                  binnacle_leg_run(-1, 1) == BINNACLE_REFUSED &&
                  wave_lp(app, "leg_run_m", o1, 2) == ZXAPP_BOTH &&
                  wave_lp(app, "leg_run_m", o2, 2) == ZXAPP_BOTH,
              "Binnacle: a run past 99999999 m: Sutra refuses the overflow, Trebago answers: "
              "BOTH");
    }

    /* BR7: waypoint_node_ok, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 5u ? wrand(1, 999) : wrand(-1200, 1200);
        b[i] = i % 6u ? wrand(1, 999) : wrand(-1200, 1200);
        c[i] = i % 4u ? wrand(-BINNACLE_LAT_MAX, BINNACLE_LAT_MAX) : wrand(-120000000, 120000000);
        d[i] = i % 7u ? wrand(-BINNACLE_LON_MAX, BINNACLE_LON_MAX) : wrand(-1200000000, 1200000000);
        if (i == 0) a[i] = 1, b[i] = 1, c[i] = 59913900, d[i] = 10752200;
        if (i == 1) a[i] = 1000, b[i] = 1, c[i] = 0, d[i] = 0;
        want[i] = T_(binnacle_node_ok(a[i], b[i], c[i], d[i]));
    }
    zxapp_arg_t na[4] = {{"route", a, WAVE_ROWS},
                         {"wpt", b, WAVE_ROWS},
                         {"lat", c, WAVE_ROWS},
                         {"lon", d, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE &&
              wave_same(app, "waypoint_node_ok", na, 4, want, WAVE_ROWS, true),
          "Binnacle: waypoint_node_ok, MYRA = C");

    /* the parts, called through Binnacle as it exports them */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-INT64_C(200000000), INT64_C(200000000));
        b[i] = wrand(-INT64_C(200000000), INT64_C(200000000));
        if (i == 0) a[i] = BINNACLE_LAT_MAX, b[i] = -BINNACLE_LON_MAX;
        want[i] = T_(binnacle_place_ok(a[i], b[i]));
    }
    zxapp_arg_t pa[2] = {{"lat", a, WAVE_ROWS}, {"lon", b, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && wave_same(app, "waymark.place_ok", pa, 2, want, WAVE_ROWS, true),
          "Binnacle: waymark.place_ok, every fix and waypoint");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1000, 1000);
        b[i] = i % 2u ? 5 * wrand(0, 400) : wrand(0, 2000); /* tenths of a m/s */
        want[i] = (a[i] <= 0 ? 1 : 0) + (a[i] >= 350 ? 2 : 0) + (b[i] >= 172 ? 4 : 0);
        c[i] = b[i] * 18 / 5; /* whole when the wind is a multiple of 5 */
    }
    zxapp_arg_t ga[2] = {{"temp", a, WAVE_ROWS}, {"wind", b, WAVE_ROWS}};
    CHECK(wave_same(app, "weatherglass.warn_flags", ga, 2, want, WAVE_ROWS, false),
          "Binnacle: weatherglass.warn_flags, the leg's warnings");
    {
        zxapp_num_t o[WAVE_ROWS];
        uint32_t k = 0;
        int32_t lp = -1;
        bool ok = zxapp_call_witnessed(app, &H, "weatherglass.wind_kmh_x10", &ga[1], 1, o,
                                       WAVE_ROWS, &k, &lp) == ZXAPP_OK &&
                  lp == ZXAPP_TRUE && k == WAVE_ROWS;
        for (uint32_t i = 0; ok && i < WAVE_ROWS; i++) {
            int64_t n = b[i] * 18, q = 5;
            if (n % 5 == 0) n /= 5, q = 1;
            ok = o[i].num == n && o[i].den == q && (q != 1 || o[i].num == c[i]);
        }
        CHECK(ok, "Binnacle: weatherglass.wind_kmh_x10, the leg's wind in km/h, exact");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) { /* one row a call: Lingua's FORTRAN 77 reads one */
        int64_t y = wrand(1, 9999), m = wrand(1, 12), dd = wrand(1, 28);
        if (i == 0) y = 2026, m = 10, dd = 22;
        int64_t w = orrery_civil_day(y, m, dd);
        zxapp_arg_t ca[3] = {{"year", &y, 1}, {"month", &m, 1}, {"day", &dd, 1}};
        CHECK((i || w == 20748) && wave_same(app, "orrery.civil_day", ca, 3, &w, 1, false),
              "Binnacle: orrery.civil_day %lld-%lld-%lld = %lld, a sight's day number",
              (long long) y, (long long) m, (long long) dd, (long long) w);
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t pl = wrand(1, ORRERY_PLANETS), dy = wrand(ORRERY_DAY_MIN, ORRERY_DAY_MAX);
        if (i == 0) pl = 3, dy = ORRERY_DAY_2000;
        int64_t w = orrery_planet_longitude(pl, dy);
        zxapp_arg_t la[2] = {{"planet", &pl, 1}, {"day", &dy, 1}};
        CHECK((i || w == 99972) && wave_same(app, "orrery.planet_longitude", la, 2, &w, 1, false),
              "Binnacle: orrery.planet_longitude %lld on day %lld = %lld, a planet's mean "
              "longitude that day",
              (long long) pl, (long long) dy, (long long) w);
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-90, 90);
        b[i] = wrand(-90, 90);
        c[i] = wrand(0, 359);
        want[i] = T_(orrery_above_horizon(a[i], b[i], c[i]));
    }
    zxapp_arg_t za[3] = {{"lat", a, WAVE_ROWS}, {"dec", b, WAVE_ROWS}, {"hour", c, WAVE_ROWS}};
    CHECK(wave_same(app, "orrery.sky_above_horizon", za, 3, want, WAVE_ROWS, true),
          "Binnacle: orrery.sky_above_horizon, a body above the horizon for a sight");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1439);
        b[i] = wrand(SUNDIAL_OFFSET_MIN, SUNDIAL_OFFSET_MAX);
        want[i] = sundial_zone_minute(a[i], b[i]);
    }
    zxapp_arg_t ua[2] = {{"utc", a, WAVE_ROWS}, {"offset", b, WAVE_ROWS}};
    CHECK(wave_same(app, "sundial.zone_minute", ua, 2, want, WAVE_ROWS, false),
          "Binnacle: sundial.zone_minute, the zone's local time for the log");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = 0;
        b[i] = 0;
        c[i] = wrand(-GNOMON_COORD_MAX, GNOMON_COORD_MAX);
        d[i] = wrand(-GNOMON_COORD_MAX, GNOMON_COORD_MAX);
        e[i] = wrand(-GNOMON_COORD_MAX, GNOMON_COORD_MAX);
        f[i] = wrand(-GNOMON_COORD_MAX, GNOMON_COORD_MAX);
        (void) gnomon_triangle_area2(a[i], b[i], c[i], d[i], e[i], f[i], &want[i]);
    }
    zxapp_arg_t ta[6] = {{"ax", a, WAVE_ROWS}, {"ay", b, WAVE_ROWS}, {"bx", c, WAVE_ROWS},
                         {"by", d, WAVE_ROWS}, {"cx", e, WAVE_ROWS}, {"cy", f, WAVE_ROWS}};
    CHECK(wave_same(app, "gnomon.triangle_area2", ta, 6, want, WAVE_ROWS, false),
          "Binnacle: gnomon.triangle_area2, leg start, leg end and the fix");

    /* a fix through the languages as the model takes it (BM4): sights of
     * azimuth 30 and 300 degrees, intercepts 0.004 and -0.002 degree */
    {
        static int64_t s1[2], c1[2], p1[2], s2[2], c2[2], p2[2], ax[2] = {0, 1};
        rat_t fw[2];
        for (uint32_t i = 0; i < 2u; i++) {
            s1[i] = orrery_sin(30);
            c1[i] = orrery_cos(30);
            p1[i] = BINNACLE_UNIT * 4000;
            s2[i] = orrery_sin(300);
            c2[i] = orrery_cos(300);
            p2[i] = BINNACLE_UNIT * -2000;
            fw[i] = binnacle_lop_fix(s1[i], c1[i], p1[i], s2[i], c2[i], p2[i], ax[i]);
        }
        zxapp_arg_t xa[7] = {{"sa", s1, 2}, {"ca", c1, 2}, {"pa", p1, 2},  {"sb", s2, 2},
                             {"cb", c2, 2}, {"pb", p2, 2}, {"axis", ax, 2}};
        CHECK(fw[0].valid && fw[1].valid && bn_fix_rows(app, xa, fw, 2),
              "Binnacle: two sights meet at %lld/%lld east, %lld/%lld north, in every language",
              (long long) fw[0].num, (long long) fw[0].den, (long long) fw[1].num,
              (long long) fw[1].den);
    }

    /* BR8, the BOTH test: a star fix and a clock fix 1000 m apart with a
     * 500 m tolerance: BOTH, plotted as the box holding both */
    {
        binnacle_box_t box;
        int64_t dn = 0, de = 0;
        CHECK(binnacle_metres(50000000, -4000000, 50008984, -4000000, &dn, &de) && dn == 1000 &&
                  de == 0,
              "Binnacle: the two fixes are 1000 m apart (%lld, %lld)", (long long) dn,
              (long long) de);
        CHECK(binnacle_position_join(50000000, -4000000, 50008984, -4000000, 500, &box) &&
                  box.said == LPRES_STATE_BOTH && (int) box.said == 3 &&
                  lpres_conjoin(box.said, LPRES_STATE_TRUE) != LPRES_STATE_TRUE &&
                  box.south == 50000000 && box.north == 50008984 && box.west == -4000000 &&
                  box.east == -4000000,
              "Binnacle: 1000 m apart, 500 m tolerance: BOTH, the box holding both");
        CHECK(binnacle_position_join(50000000, -4000000, 50008984, -4000000, 1000, &box) &&
                  box.said == LPRES_STATE_TRUE &&
                  binnacle_position_join(10000000, 179999000, 10000000, -179999000, 500, &box) &&
                  box.said == LPRES_STATE_TRUE && box.west == 179999000 && box.east == -179999000,
              "Binnacle: within the tolerance TRUE; a box across 180 degrees runs west to east");
    }
}
