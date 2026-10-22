/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Isobar's languages, witnessed, row by row against isobar_rules.h, the C
 * the model runs; the parts it composes (weatherglass, homestead, furrow,
 * trellis, wellspring) resolved and called; and the BOTH of two near
 * stations that disagree about the same air. Slots:
 * @logic.isobar_reading_accept_ok, @num.isobar_pressure_outlook,
 * @num.isobar_station_hour_high, @logic.isobar_wx_obs_record_ok,
 * @logic.isobar_wx_site_node_ok; and, used, @num.weatherglass_warn_flags,
 * @num.weatherglass_temp_f_tenths, @num.weatherglass_rain_in_x100,
 * @num.weatherglass_wind_kmh_x10, @num.weatherglass_week_temps,
 * @logic.homestead_home_device_ok, @num.furrow_range_total,
 * @num.trellis_peers_in_state, @num.wellspring_band. */
#include "furrow_rules.h"
#include "homestead_rules.h"
#include "isobar_rules.h"
#include "lpres.h"
#include "trellis_rules.h"

APP_TEST(isobar, 50, 239)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];

    /* compose: five parts, every part entry it exports resolves to its part */
    static const char *const used[9][2] = {{"weatherglass.temp_f_tenths", "weatherglass"},
                                           {"weatherglass.rain_in_x100", "weatherglass"},
                                           {"weatherglass.wind_kmh_x10", "weatherglass"},
                                           {"weatherglass.warn_flags", "weatherglass"},
                                           {"weatherglass.week_temps", "weatherglass"},
                                           {"homestead.home_device_ok", "homestead"},
                                           {"furrow.range_total", "furrow"},
                                           {"trellis.peers_in_state", "trellis"},
                                           {"wellspring.band", "wellspring"}};
    CHECK(app->resolved && app->nuse == 5 && app->composed == 7 && app->n == 50,
          "Isobar: composed of 5 parts, 7 own lines and 43 of theirs (%u, %u)", app->composed,
          app->n);
    for (uint32_t i = 0; i < 9u; i++) {
        const zxapp_logic_t *L = zxapp_find(app, used[i][0]);
        CHECK(L && L->origin && !strcmp(L->origin->id, used[i][1]) && L->loaded &&
                  zxapp_contract(app, used[i][0]),
              "Isobar: %s resolves to %s and loads", used[i][0], used[i][1]);
    }
    zxapp_origin_t org[8];
    CHECK(zxapp_provenance(app, org, 8) == 6 && !org[0].from, "Isobar: itself and five parts");

    /* IR1: reading_accept_ok, Eve = KRON = C, every edge of each margin */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 5u ? wrand(0, ISOBAR_MARGIN_MAX) : wrand(-3, 3);
        b[i] = i % 6u ? wrand(0, ISOBAR_MARGIN_MAX) : wrand(-3, 3);
        c[i] = i % 7u ? wrand(0, ISOBAR_MARGIN_MAX) : wrand(-3, 3);
        d[i] = i % 4u ? wrand(0, ISOBAR_MARGIN_MAX) : wrand(-3, 3);
        if (i == 0) a[i] = b[i] = c[i] = d[i] = 0;                 /* zero: on the edges */
        if (i == 1) a[i] = b[i] = c[i] = d[i] = ISOBAR_MARGIN_MAX; /* max */
        if (i == 2) a[i] = -1, b[i] = c[i] = d[i] = 5;             /* below the range */
        if (i == 3) b[i] = -1, a[i] = c[i] = d[i] = 5;             /* above it */
        if (i == 4) c[i] = -1, a[i] = b[i] = d[i] = 5;             /* rose too far */
        if (i == 5) d[i] = -1, a[i] = b[i] = c[i] = 5;             /* fell too far */
        want[i] = T_(isobar_reading_accept_ok(a[i], b[i], c[i], d[i]));
    }
    zxapp_arg_t ra[4] = {{"above", a, WAVE_ROWS},
                         {"below", b, WAVE_ROWS},
                         {"rise", c, WAVE_ROWS},
                         {"fall", d, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[5] == ZXAPP_FALSE &&
              wave_same(app, "reading_accept_ok", ra, 4, want, WAVE_ROWS, true),
          "Isobar: reading_accept_ok, Eve = KRON = C");
    for (uint32_t k = 0; k < 4u; k++) {
        /* a margin past 999999 (max + 1): Eve refuses it, KRON reads it as inside: BOTH */
        int64_t m[4][2] = {{5, 5}, {5, 5}, {5, 5}, {5, 5}};
        m[k][1] = ISOBAR_MARGIN_MAX + 1;
        zxapp_arg_t xa[4] = {
            {"above", m[0], 2}, {"below", m[1], 2}, {"rise", m[2], 2}, {"fall", m[3], 2}};
        CHECK(!isobar_reading_accept_ok(m[0][1], m[1][1], m[2][1], m[3][1]) &&
                  wave_lp(app, "reading_accept_ok", xa, 4) == ZXAPP_BOTH,
              "Isobar: reading_accept_ok, margin %u past 999999: BOTH", k);
    }
    {
        /* the margins of a reading against its sensor (IR1) */
        int64_t m[4];
        CHECK(isobar_margins(21, -400, 500, 18, 50, true, m) && m[0] == 421 && m[1] == 479 &&
                  m[2] == 47 && m[3] == 53 && isobar_reading_accept_ok(m[0], m[1], m[2], m[3]),
              "Isobar: 2.1 C after 1.8 C inside -40.0..50.0, step 5.0: accepted");
        CHECK(isobar_margins(90, -400, 500, 18, 50, true, m) && m[2] == -22 &&
                  !isobar_reading_accept_ok(m[0], m[1], m[2], m[3]) &&
                  isobar_margins(90, -400, 500, 0, 50, false, m) && m[2] == 50 && m[3] == 50 &&
                  isobar_reading_accept_ok(m[0], m[1], m[2], m[3]) &&
                  !isobar_margins(1, 0, 2, 0, ISOBAR_STEP_MAX + 1, false, m),
              "Isobar: a jump past the step refused; a first reading has no last");
    }

    /* IR2: pressure_outlook, Trebago = KRON = C, every row of the table */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 3u ? wrand(-80, 80) : wrand(-ISOBAR_TEND_MAX, ISOBAR_TEND_MAX);
        b[i] = i % 2u ? wrand(9950, 10250) : wrand(ISOBAR_PRESS_MIN, ISOBAR_PRESS_MAX);
        if (i == 0) a[i] = 0, b[i] = ISOBAR_PRESS_MIN;               /* the least pressure */
        if (i == 1) a[i] = ISOBAR_TEND_MAX, b[i] = ISOBAR_PRESS_MAX; /* max */
        if (i == 2) a[i] = -ISOBAR_TEND_MAX, b[i] = 10132;           /* the steepest fall */
        if (i == 3) a[i] = -60, b[i] = 10132;                        /* storm's edge */
        if (i == 4) a[i] = -59, b[i] = 10132;                        /* rain's edge */
        if (i == 5) a[i] = -16, b[i] = 10132;
        if (i == 6) a[i] = -15, b[i] = 9999; /* steady, low */
        if (i == 7) a[i] = 15, b[i] = 10000; /* steady, no change */
        if (i == 8) a[i] = 0, b[i] = 10199;
        if (i == 9) a[i] = 0, b[i] = 10200;   /* steady, high */
        if (i == 10) a[i] = 16, b[i] = 10132; /* improving's edge */
        if (i == 11) a[i] = 59, b[i] = 10132;
        if (i == 12) a[i] = 60, b[i] = 10132; /* clearing's edge */
        want[i] = isobar_pressure_outlook(a[i], b[i]);
        CHECK(want[i] != ISOBAR_REFUSED, "Isobar: IR2 row %u answers", i);
    }
    zxapp_arg_t pa[2] = {{"tendency", a, WAVE_ROWS}, {"pressure", b, WAVE_ROWS}};
    CHECK(want[3] == ISOBAR_OUT_STORM && want[4] == ISOBAR_OUT_RAIN &&
              want[6] == ISOBAR_OUT_UNSETTLED && want[7] == ISOBAR_OUT_NO_CHANGE &&
              want[9] == ISOBAR_OUT_FAIR && want[10] == ISOBAR_OUT_IMPROVING &&
              want[12] == ISOBAR_OUT_CLEARING &&
              wave_same(app, "pressure_outlook", pa, 2, want, WAVE_ROWS, false),
          "Isobar: pressure_outlook, Trebago = KRON = C");
    {
        /* outside the table (max + 1, and below the least): Trebago refuses, KRON answers: BOTH */
        static const int64_t bad[4][2] = {{ISOBAR_TEND_MAX + 1, 10132},
                                          {-ISOBAR_TEND_MAX - 1, 10132},
                                          {0, ISOBAR_PRESS_MIN - 1},
                                          {0, ISOBAR_PRESS_MAX + 1}};
        for (uint32_t i = 0; i < 4u; i++) {
            int64_t te[2] = {0, bad[i][0]}, pr[2] = {10132, bad[i][1]};
            zxapp_arg_t xa[2] = {{"tendency", te, 2}, {"pressure", pr, 2}};
            CHECK(isobar_pressure_outlook(bad[i][0], bad[i][1]) == ISOBAR_REFUSED &&
                      wave_lp(app, "pressure_outlook", xa, 2) == ZXAPP_BOTH,
                  "Isobar: pressure_outlook outside its table (%u): BOTH", i);
        }
    }

    /* IR3: station_hour_high, KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 4);
        b[i] = wrand(499000, 499005);
        c[i] = i % 9u ? wrand(-1000, 1000) : wrand(-9999999, 9999999);
    }
    a[0] = 1, b[0] = 499000, c[0] = 0;
    a[1] = 1, b[1] = 499000, c[1] = -1; /* a cell's high is its highest, 0 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = isobar_station_hour_high(a, b, c, WAVE_ROWS, i);
    zxapp_arg_t ga[3] = {
        {"station", a, WAVE_ROWS}, {"hour", b, WAVE_ROWS}, {"reading", c, WAVE_ROWS}};
    CHECK(want[1] >= 0 && want[1] == want[0] &&
              wave_same(app, "station_hour_high", ga, 3, want, WAVE_ROWS, false),
          "Isobar: station_hour_high, KRON = C");

    /* IR4: wx_obs_record_ok, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-12000, 12000);
        b[i] = i % 3u ? wrand(0, 2000000000) : wrand(-120000000000, 120000000000);
        c[i] = wrand(-12, 12);
        d[i] = wrand(-12000000, 12000000);
        e[i] = wrand(-12, 12);
        if (i == 0) a[i] = 9999, b[i] = 99999999999, c[i] = 9, d[i] = 9999999, e[i] = 9; /* max */
        if (i == 1) a[i] = 10000, b[i] = 1, c[i] = 1, d[i] = 1, e[i] = 0;         /* max + 1 */
        if (i == 2) a[i] = 0, b[i] = 0, c[i] = 0, d[i] = 0, e[i] = 0;             /* zero */
        if (i == 3) a[i] = -9999, b[i] = 1, c[i] = -5, d[i] = -9999999, e[i] = 1; /* negative */
        want[i] = T_(isobar_obs_record_ok(a[i], b[i], c[i], d[i], e[i]));
    }
    zxapp_arg_t oa[5] = {{"station", a, WAVE_ROWS},
                         {"stamp", b, WAVE_ROWS},
                         {"kind", c, WAVE_ROWS},
                         {"reading", d, WAVE_ROWS},
                         {"state", e, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[3] == ZXAPP_TRUE &&
              wave_same(app, "wx_obs_record_ok", oa, 5, want, WAVE_ROWS, true),
          "Isobar: wx_obs_record_ok, MYRA = C");

    /* IR5: wx_site_node_ok, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-12000, 12000);
        b[i] = wrand(-12000, 12000);
        c[i] = wrand(-12, 12);
        d[i] = wrand(-120000, 120000);
        if (i == 0) a[i] = 9999, b[i] = 9999, c[i] = 9, d[i] = 99999; /* max */
        if (i == 1) a[i] = 1, b[i] = 10000, c[i] = 2, d[i] = 0;       /* max + 1 */
        if (i == 2) a[i] = 0, b[i] = 0, c[i] = 0, d[i] = 0;           /* zero */
        if (i == 3) a[i] = 1, b[i] = 2, c[i] = 2, d[i] = -430;        /* below the sea */
        want[i] = T_(isobar_site_node_ok(a[i], b[i], c[i], d[i]));
    }
    zxapp_arg_t sa[4] = {{"parent", a, WAVE_ROWS},
                         {"site", b, WAVE_ROWS},
                         {"kind", c, WAVE_ROWS},
                         {"elev", d, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[3] == ZXAPP_TRUE &&
              wave_same(app, "wx_site_node_ok", sa, 4, want, WAVE_ROWS, true),
          "Isobar: wx_site_node_ok, MYRA = C");

    /* the parts, called through Isobar as it exports them */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1000, 1000);
        b[i] = wrand(0, 2000);
        want[i] = (a[i] <= 0) + 2 * (a[i] >= 350) + 4 * (b[i] >= 172);
    }
    zxapp_arg_t wa[2] = {{"temp", a, WAVE_ROWS}, {"wind", b, WAVE_ROWS}};
    CHECK(wave_same(app, "weatherglass.warn_flags", wa, 2, want, WAVE_ROWS, false),
          "Isobar: weatherglass.warn_flags, a station's warnings");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = 5 * wrand(-200, 200); /* a multiple of 5: a whole number of tenths F */
        want[i] = a[i] * 9 / 5 + 320;
    }
    zxapp_arg_t fa[1] = {{"celsius", a, WAVE_ROWS}};
    CHECK(wave_same(app, "weatherglass.temp_f_tenths", fa, 1, want, WAVE_ROWS, false),
          "Isobar: weatherglass.temp_f_tenths, a reading in Fahrenheit");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = 127 * wrand(0, 787);
        want[i] = a[i] * 50 / 127;
    }
    zxapp_arg_t ia[1] = {{"rain", a, WAVE_ROWS}};
    CHECK(wave_same(app, "weatherglass.rain_in_x100", ia, 1, want, WAVE_ROWS, false),
          "Isobar: weatherglass.rain_in_x100, rain in inches");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = 5 * wrand(0, 400);
        want[i] = a[i] * 18 / 5;
    }
    zxapp_arg_t ka[1] = {{"wind", a, WAVE_ROWS}};
    CHECK(wave_same(app, "weatherglass.wind_kmh_x10", ka, 1, want, WAVE_ROWS, false),
          "Isobar: weatherglass.wind_kmh_x10, wind in km/h");
    {
        static const int64_t week[7] = {-50, 10, 25, 120, -3, 60, 77}, stats[3] = {-50, 120, 34};
        zxapp_arg_t xa[1] = {{"temp", week, 7}};
        CHECK(wave_same(app, "weatherglass.week_temps", xa, 1, stats, 3, false),
              "Isobar: weatherglass.week_temps, a station's week");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        /* MYRA's digits and Eve's kinds agree for devices 1.. and kinds 1..6 */
        a[i] = wrand(1, 10005);
        b[i] = wrand(HOMESTEAD_LIGHT, HOMESTEAD_OVEN);
        c[i] = wrand(-100005, 100005);
        d[i] = wrand(-100005, 100005);
        e[i] = wrand(-100005, 100005);
        if (i == 0) a[i] = 7, b[i] = HOMESTEAD_HEATER, c[i] = -400, d[i] = 400, e[i] = 50;
        want[i] = T_(homestead_device_ok(a[i], b[i], c[i], d[i], e[i]));
    }
    {
        zxapp_arg_t ha[5] = {{"device", a, WAVE_ROWS},
                             {"kind", b, WAVE_ROWS},
                             {"lo", c, WAVE_ROWS},
                             {"hi", d, WAVE_ROWS},
                             {"setpoint", e, WAVE_ROWS}};
        CHECK(want[0] == ZXAPP_TRUE &&
                  wave_same(app, "homestead.home_device_ok", ha, 5, want, WAVE_ROWS, true),
              "Isobar: homestead.home_device_ok, a station's heater record");
    }
    {
        int64_t s = 0;
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = 499000 + (int64_t) i; /* the hours */
            b[i] = 499010;
            c[i] = 499033;
            d[i] = wrand(0, 2000); /* tenths of a mm */
        }
        CHECK(furrow_range_total(a, b, c, d, WAVE_ROWS, &s) && s > 0,
              "Isobar: a day's rain answers");
        zxapp_arg_t ta[4] = {{"pos", a, WAVE_ROWS},
                             {"lo", b, WAVE_ROWS},
                             {"hi", c, WAVE_ROWS},
                             {"cell", d, WAVE_ROWS}};
        CHECK(wave_same(app, "furrow.range_total", ta, 4, &s, 1, false),
              "Isobar: furrow.range_total, the rain of a range of hours");
    }
    {
        int64_t n = 0;
        for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(1, 3), b[i] = TRELLIS_ACTIVE;
        n = trellis_peers_in_state(a, b, WAVE_ROWS);
        zxapp_arg_t na[2] = {{"state", a, WAVE_ROWS}, {"want", b, WAVE_ROWS}};
        CHECK(n != TRELLIS_REFUSED && wave_same(app, "trellis.peers_in_state", na, 2, &n, 1, false),
              "Isobar: trellis.peers_in_state, the station nodes active");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        b[i] = wrand(-1000, 1000);
        c[i] = b[i] + wrand(0, 1000);
        a[i] = wrand(-2000, 2000);
        want[i] = a[i] < b[i] ? -1 : a[i] > c[i] ? 1 : 0;
    }
    zxapp_arg_t ba[3] = {{"reading", a, WAVE_ROWS}, {"lo", b, WAVE_ROWS}, {"hi", c, WAVE_ROWS}};
    CHECK(wave_same(app, "wellspring.band", ba, 3, want, WAVE_ROWS, false),
          "Isobar: wellspring.band, where a refused reading fell");

    /* IM9, the BOTH test: two stations 400 m apart read 2.1 C and 7.4 C,
     * both within range; each is a source about "the site is at frost
     * risk" (at or below 3.0 C). The sources joined (lpres.h: knowledge
     * join across sources, then conjoin across subjects) give BOTH, and
     * both readings are kept as they are, never averaged */
    {
        int64_t near[2] = {21, 74}, m[4];
        CHECK(isobar_margins(near[0], -400, 500, 20, 50, true, m) &&
                  isobar_reading_accept_ok(m[0], m[1], m[2], m[3]) &&
                  isobar_margins(near[1], -400, 500, 70, 50, true, m) &&
                  isobar_reading_accept_ok(m[0], m[1], m[2], m[3]),
              "Isobar: 2.1 C and 7.4 C are both accepted");
        lpres_state_t ea = near[0] <= 30 ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
        lpres_state_t eb = near[1] <= 30 ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
        lpres_state_t site =
            lpres_conjoin(lpres_kjoin(ea, eb), lpres_conjoin(LPRES_STATE_TRUE, LPRES_STATE_TRUE));
        CHECK(site == LPRES_STATE_BOTH && site == 3 && near[0] == 21 && near[1] == 74,
              "Isobar: a microclimate: BOTH, and both readings kept");
        CHECK(lpres_disjoin(site, LPRES_STATE_FALSE) == LPRES_STATE_BOTH &&
                  lpres_conjoin(site, LPRES_STATE_NEITHER) == LPRES_STATE_NEITHER &&
                  lpres_kjoin(ea, ea) == LPRES_STATE_TRUE,
              "Isobar: BOTH is never TRUE; one source alone is not a contradiction");
    }
}
