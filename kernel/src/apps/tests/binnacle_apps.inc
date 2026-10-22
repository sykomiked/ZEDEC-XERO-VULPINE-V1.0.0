/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Binnacle's model against zxvd's real dispatcher (LINK): the clock from
 * sys.info, the device's place from place.here, waypoints put on the map
 * only on the owner's word (place.add) and read back (place.list); a star
 * fix from two sights (made up for the test), a clock fix by dead
 * reckoning 1000 m from it held BOTH with a 500 m tolerance and plotted
 * as the box holding both; bearings, distances and a safe heading only
 * from a TRUE position; the leg's notice. */
#include "binnacle_model.h"

static binnacle_t BIN;

/* the waypoint called `name` on the chart, or -1 */
static int32_t bin_wpt(const binnacle_t *b, const char *name)
{
    for (uint32_t i = 0; i < b->nwpt; i++)
        if (!strcmp(b->wpt[i].name, name)) return (int32_t) i;
    return -1;
}

APPS_TEST(binnacle)
{
    static char text[BINNACLE_NOTICE_MAX];
    static int64_t bearing[BINNACLE_WPTS], range[BINNACLE_WPTS];
    bool safe = true;
    uint32_t n = 0;

    /* the rules the model runs */
    {
        rat_t e = binnacle_lop_fix(0, 10000, 500000000, 10000, 0, 300000000, 0);
        rat_t q = binnacle_lop_fix(5000, 8660, 10000000, -5000, 8660, 20000000, 1);
        CHECK(e.valid && e.num == 30000 && e.den == 1 && q.valid && q.num == 750000 &&
                  q.den == 433 && !binnacle_lop_fix(0, 10000, 1, 0, 10000, 2, 0).valid &&
                  binnacle_bearing(100, 100) == 4500 && binnacle_range(3, 4) == 5 &&
                  binnacle_hazard_margin(100, 35000, 1000) == -900 &&
                  binnacle_heading_safe(9000, 1, 0) && !binnacle_heading_safe(9000, 0, 0) &&
                  binnacle_leg_run(1852, 3600) == 1852 &&
                  binnacle_leg_run(999999, 999999) == BINNACLE_REFUSED &&
                  binnacle_node_ok(1, 1, 50000000, -4000000) && !binnacle_node_ok(1000, 1, 0, 0),
              "binnacle: the fix, the bearing and distance, the margin, the run, the tree");
    }

    /* refused before anything is sent, nothing kept */
    binnacle_init(&BIN, &LINK);
    binnacle_sight_t sight[2] = {{0, 40, 0, 30, 4000}, {3, 40, 0, 300, -2000}};
    int before = sent;
    CHECK(binnacle_add_waypoint(&BIN, "", 0, 0, NULL, true) == ZXM_E_ARG &&
              binnacle_add_waypoint(&BIN, "Binnacle X", BINNACLE_LAT_MAX + 1, 0, NULL, true) ==
                  ZXM_E_ARG &&
              binnacle_star_fix(&BIN, 50000000, -4000000, sight) == ZXM_E_STATE &&
              binnacle_mark_hazard(&BIN, 0, BINNACLE_TURN) == ZXM_E_ARG &&
              binnacle_route_add(&BIN, 1, 0) == ZXM_E_ARG &&
              binnacle_position(&BIN, 500) == ZXM_E_STATE &&
              binnacle_to_waypoints(&BIN, bearing, range, BINNACLE_WPTS, &n) == ZXM_E_STATE &&
              binnacle_leg_notice(&BIN, 10, 100, 0, text, sizeof text) == ZXM_E_STATE &&
              sent == before && BIN.nwpt == 0 && !BIN.star_known,
          "binnacle: refused before anything is sent");

    /* BM1: the clock */
    CHECK(binnacle_clock(&BIN) == ZXM_OK && BIN.clock_known && BIN.now_s > 1700000000,
          "binnacle: the clock from sys.info: %s", LINK.err);

    /* BM4: a star fix at an assumed position of 50 N, 4 W; a sight of a body
     * below the horizon there is refused */
    {
        binnacle_sight_t low[2] = {{0, -60, 0, 30, 4000}, {0, 40, 0, 300, -2000}};
        binnacle_sight_t par[2] = {{0, 40, 0, 30, 4000}, {0, 40, 0, 210, -2000}};
        CHECK(binnacle_star_fix(&BIN, 50000000, -4000000, low) == ZXM_E_ARG &&
                  binnacle_star_fix(&BIN, 50000000, -4000000, par) == ZXM_E_STATE &&
                  !BIN.star_known,
              "binnacle: a body below the horizon, or parallel lines: no fix");
        rat_t e = binnacle_lop_fix(5000, 8660, 40000000, -8660, 5000, -20000000, 0);
        rat_t q = binnacle_lop_fix(5000, 8660, 40000000, -8660, 5000, -20000000, 1);
        CHECK(binnacle_star_fix(&BIN, 50000000, -4000000, sight) == ZXM_OK && BIN.star_known &&
                  rat_eq(BIN.star_east, e) && rat_eq(BIN.star_north, q) &&
                  BIN.star_lat == 50002464 && BIN.star_lon == -3994194 &&
                  BIN.planet_mdeg[0] == ORRERY_REFUSED &&
                  BIN.planet_mdeg[1] ==
                      orrery_planet_longitude(3, binnacle_floor_div(BIN.now_s, 86400)),
              "binnacle: the star fix %lld,%lld from two lines of position, exact",
              (long long) BIN.star_lat, (long long) BIN.star_lon);
    }

    /* BM5, BM6, the BOTH test: a clock fix an hour on at 1000 m an hour due
     * north of the star fix; 500 m tolerance: BOTH, the box holding both */
    CHECK(binnacle_clock_fix(&BIN, BIN.star_lat, BIN.star_lon, BIN.now_s - 3600, 0, 1000) ==
                  ZXM_OK &&
              BIN.dr_lat == BIN.star_lat + 8983 && BIN.dr_lon == BIN.star_lon &&
              binnacle_clock_fix(&BIN, 0, 0, BIN.now_s + 1, 0, 1000) == ZXM_E_STATE,
          "binnacle: the clock fix by dead reckoning");
    CHECK(binnacle_position(&BIN, 500) == ZXM_OK && BIN.pos.said == LPRES_STATE_BOTH &&
              BIN.pos.south == BIN.star_lat && BIN.pos.north == BIN.dr_lat &&
              BIN.pos.west == BIN.star_lon && BIN.pos.east == BIN.star_lon,
          "binnacle: star and clock fixes 999 m apart, 500 m tolerance: BOTH");
    CHECK(binnacle_position_text(&BIN, text, sizeof text) > 0 &&
              strstr(text, "somewhere in the box 50.002464,-3.994194 to 50.011447,-3.994194") &&
              strstr(text, BINNACLE_ADVISORY),
          "binnacle: plotted as the box, never one point: %s", text);
    CHECK(binnacle_to_waypoints(&BIN, bearing, range, BINNACLE_WPTS, &n) == ZXM_E_STATE &&
              binnacle_heading_check(&BIN, 9000, 0, 1, 1000, &safe) == ZXM_E_ARG,
          "binnacle: no bearing from a BOTH position");

    /* BM2: the device's place, waypoints on the map only on the owner's word */
    zxm_begin(&LINK, "place.set_here");
    zxm_p_i64(&LINK, "lat", 50002464);
    zxm_p_i64(&LINK, "lon", -3994194);
    CHECK(zxm_call(&LINK) == ZXM_OK && binnacle_here(&BIN) == ZXM_OK && BIN.here_known &&
              BIN.here_lat == 50002464 && BIN.here_lon == -3994194,
          "binnacle: here, from place.here: %s", LINK.err);
    before = sent;
    CHECK(binnacle_add_waypoint(&BIN, "Binnacle fairway buoy", 50002464, -4004194, NULL, false) ==
                  ZXM_E_STATE &&
              sent == before && BIN.nwpt == 0,
          "binnacle: no waypoint without the owner's word, nothing sent");
    CHECK(binnacle_add_waypoint(&BIN, "Binnacle fairway buoy", 50002464, -4004194, "start", true) ==
                  ZXM_OK &&
              binnacle_add_waypoint(&BIN, "Binnacle harbour mouth", 50102464, -4004194, NULL,
                                    true) == ZXM_OK &&
              binnacle_add_waypoint(&BIN, "Binnacle east mark", 50002464, -3944194, NULL, true) ==
                  ZXM_OK &&
              BIN.nwpt == 3,
          "binnacle: three waypoints on the map: %s", LINK.err);
    CHECK(binnacle_places(&BIN) == ZXM_OK && BIN.nwpt >= 3u, "binnacle: the chart from place.list");
    int32_t buoy = bin_wpt(&BIN, "Binnacle fairway buoy"),
            mouth = bin_wpt(&BIN, "Binnacle harbour mouth"),
            east = bin_wpt(&BIN, "Binnacle east mark");
    CHECK(buoy >= 0 && mouth >= 0 && east >= 0 && BIN.wpt[mouth].lat == 50102464,
          "binnacle: every waypoint read back exact");

    /* BM3: a route, each waypoint fitting the tree */
    CHECK(binnacle_route_add(&BIN, 1, (uint32_t) buoy) == ZXM_OK &&
              binnacle_route_add(&BIN, 1, (uint32_t) mouth) == ZXM_OK &&
              binnacle_route_add(&BIN, BINNACLE_ROUTES + 1u, (uint32_t) buoy) == ZXM_E_ARG &&
              BIN.nroute[0] == 2 && BIN.route[0][1] == (uint32_t) mouth,
          "binnacle: route 1, the buoy then the harbour mouth");

    /* BM6, BM7: within 1500 m the fixes agree: TRUE, bearings and distances */
    CHECK(binnacle_position(&BIN, 1500) == ZXM_OK && BIN.pos.said == LPRES_STATE_TRUE &&
              binnacle_to_waypoints(&BIN, bearing, range, BINNACLE_WPTS, &n) == ZXM_OK &&
              n == BIN.nwpt && bearing[buoy] == 27000 && range[buoy] == 716 &&
              bearing[mouth] == 35600 && bearing[east] == 9000 && range[east] == 3577,
          "binnacle: TRUE; the buoy %lld cdeg %lld m, the mouth %lld cdeg %lld m, the mark %lld "
          "cdeg %lld m",
          (long long) bearing[buoy], (long long) range[buoy], (long long) bearing[mouth],
          (long long) range[mouth], (long long) bearing[east], (long long) range[east]);
    CHECK(binnacle_position_text(&BIN, text, sizeof text) > 0 &&
              strstr(text, "Position 50.002464,-3.994194 (the star fix and the clock fix agree)"),
          "binnacle: one point when TRUE: %s", text);

    /* BM7: a safe heading: hazard 40..50 degrees; the leg buoy to mouth runs
     * north 715 m west of the fix */
    CHECK(binnacle_mark_hazard(&BIN, 4000, 5000) == ZXM_OK, "binnacle: a hazard sector marked");
    CHECK(binnacle_heading_check(&BIN, 9000, (uint32_t) buoy, (uint32_t) mouth, 1000, &safe) ==
                  ZXM_OK &&
              safe &&
              binnacle_heading_check(&BIN, 4500, (uint32_t) buoy, (uint32_t) mouth, 1000, &safe) ==
                  ZXM_OK &&
              !safe &&
              binnacle_heading_check(&BIN, 9000, (uint32_t) buoy, (uint32_t) mouth, 500, &safe) ==
                  ZXM_OK &&
              !safe &&
              binnacle_heading_check(&BIN, 9000, (uint32_t) buoy, (uint32_t) east, 0, &safe) ==
                  ZXM_OK &&
              safe,
          "binnacle: clear of the hazard and within the cross-track limit, safe; into the hazard "
          "or 715 m off a 500 m limit, not");

    /* BM8: the leg's notice */
    {
        int32_t len = binnacle_leg_notice(&BIN, 180, 100, 60, text, sizeof text);
        CHECK(len > 0 && strstr(text, "Leg: wind 64.8 km/h, gale warning; local time ") &&
                  strstr(text, BINNACLE_ADVISORY) &&
                  binnacle_leg_notice(&BIN, 2001, 100, 60, text, sizeof text) == ZXM_E_ARG &&
                  binnacle_leg_notice(&BIN, 10, 100, 841, text, sizeof text) == ZXM_E_ARG,
              "binnacle: the leg's wind, warning and local time: %s", text);
    }
}
