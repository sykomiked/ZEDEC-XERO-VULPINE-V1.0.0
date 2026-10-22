/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Orrery's model against zxvd's real dispatcher (LINK): the time is
 * sys.info's time_ms and the place is place.here, zxvd's own
 * (zxv_sysinfo.c, zxv_place.c), both read for real (OM1). The sky is then
 * checked at a fixed time set on the model after that read, so every
 * answer below is known: 2026-10-22 06:59:30 UTC (1792652370 s since
 * 1970, day 20748), seen from London (51.5074 N, 0.1278 W). */
#include "orrery_model.h"

#define ORR_T0 INT64_C(1792652370)

static int orr_rpc(const char *method, const char *params)
{
    zxm_begin(&LINK, method);
    zxm_p_raw(&LINK, params, (uint32_t) strlen(params));
    return zxm_call(&LINK);
}

APPS_TEST(orrery)
{
    static orrery_t O;
    int64_t v = -1, id = 0;
    bool up[ORRERY_STARS];
    uint32_t n = 99;

    orrery_init(&O, &LINK);
    CHECK(!O.have && O.nobs == 0 && O.next_id == 1, "orrery: nothing read to begin with");
    CHECK(orrery_planet_now(&O, 1, &v) == ZXM_E_STATE &&
              orrery_sidereal_now(&O, &v) == ZXM_E_STATE &&
              orrery_observe(&O, 1, 0, 0, &id) == ZXM_E_STATE,
          "orrery: no sky before a read");

    /* OM1: zxvd's clock for real, and no place set yet */
    CHECK(zpl_open(NULL) == 0, "orrery: places in memory");
    CHECK(orr_rpc("place.forget_here", "{}") == ZXM_OK, "orrery: no place: %s", LINK.err);
    CHECK(orrery_refresh(&O) == ZXM_OK && O.have && !O.place && O.now_s >= ORR_T0 - 31536000 &&
              O.day == O.now_s / 86400,
          "orrery: sys.info's time_ms and place.here read: %s", LINK.err);
    CHECK(orrery_planet_now(&O, 3, &v) == ZXM_OK && v >= 0 && v < ORRERY_TURN_MDEG &&
              v == orrery_planet_longitude(3, O.day),
          "orrery: a planet's longitude today without a place");
    CHECK(orrery_sidereal_now(&O, &v) == ZXM_E_STATE && orrery_stars_up(&O, &n, up) == ZXM_E_STATE,
          "orrery: no stars without a place");

    CHECK(orr_rpc("place.set_here", "{\"lat\":51507400,\"lon\":-127800,\"accuracy_m\":25}") ==
              ZXM_OK,
          "orrery: a place set: %s", LINK.err);
    CHECK(orrery_refresh(&O) == ZXM_OK && O.place && O.lat_u6 == 51507400 && O.lon_u6 == -127800,
          "orrery: place.here read: %s", LINK.err);
    CHECK(orrery_stars_up(&O, &n, up) == ZXM_OK && n <= ORRERY_STARS && up[7] && !up[1],
          "orrery: from London, Polaris is always up and Canopus never");

    /* OM2-OM4 at a known time */
    O.now_s = ORR_T0;
    O.day = ORR_T0 / 86400;
    CHECK(O.day == 20748 && orrery_civil_day(2026, 10, 22) == O.day,
          "orrery: the clock's day is the date's day number");
    static const int64_t LON[ORRERY_PLANETS] = {358295, 27632, 30069, 85992,
                                                127859, 17642, 68090, 3436};
    bool planets = true;
    for (int64_t p = 1; p <= ORRERY_PLANETS; p++)
        planets = planets && orrery_planet_now(&O, p, &v) == ZXM_OK && v == LON[p - 1];
    CHECK(planets, "orrery: the eight mean longitudes on 2026-10-22");
    CHECK(orrery_planet_now(&O, 0, &v) == ZXM_E_ARG && orrery_planet_now(&O, 9, &v) == ZXM_E_ARG,
          "orrery: no ninth planet");
    CHECK(orrery_sidereal_now(&O, &v) == ZXM_OK && v == 135471,
          "orrery: local sidereal time 135.471 degrees (%lld)", (long long) v);
    CHECK(orrery_stars_up(&O, &n, up) == ZXM_OK && n == 7 && up[0] && !up[1] && up[2] && up[3] &&
              up[4] && up[5] && up[6] && up[7],
          "orrery: seven of the eight bright stars are up, Canopus is not (%u)", n);
    CHECK(orrery_star_up(&O, ORRERY_STARS, &up[0]) == ZXM_E_ARG, "orrery: no star past the table");
    CHECK(orrery_sidereal(0, 180000001) == ORRERY_REFUSED &&
              orrery_sidereal(-1, 0) == ORRERY_REFUSED && orrery_sidereal(946728000, 0) == 280460 &&
              orrery_sidereal(946728000 + 86400, 0) == 281446,
          "orrery: sidereal time at 2000-01-01 12:00 and a day later");

    /* OM5: observations, checked before they are kept */
    CHECK(orrery_observation_valid(1, 0, 0, 0) && orrery_observation_valid(108, 0, 90000, 359999) &&
              !orrery_observation_valid(0, 0, 0, 0) && !orrery_observation_valid(9, 0, 0, 0) &&
              !orrery_observation_valid(109, 0, 0, 0) && !orrery_observation_valid(1, -1, 0, 0) &&
              !orrery_observation_valid(1, 0, 90001, 0) &&
              !orrery_observation_valid(1, 0, 0, 360000),
          "orrery: what an observation may hold");
    CHECK(orrery_observe(&O, 4, 23000, 121000, &id) == ZXM_OK && id == 1 && O.nobs == 1 &&
              orrery_find(&O, 1) && orrery_find(&O, 1)->time == ORR_T0 &&
              orrery_find(&O, 1)->body == 4,
          "orrery: Mars seen now");
    CHECK(orrery_observe(&O, 9, 0, 0, &id) == ZXM_E_ARG &&
              orrery_observe(&O, 4, -90001, 0, &id) == ZXM_E_ARG && O.nobs == 1,
          "orrery: a bad observation changes nothing");
    bool fill = true;
    for (uint32_t i = 1; i < ORRERY_OBS_MAX; i++)
        fill = fill &&
               orrery_observe(&O, 101 + (int64_t) (i % ORRERY_STARS), 1000, 2000, &id) == ZXM_OK;
    CHECK(fill && O.nobs == ORRERY_OBS_MAX && orrery_observe(&O, 1, 0, 0, &id) == ZXM_E_STATE &&
              O.nobs == ORRERY_OBS_MAX,
          "orrery: a full log refuses, never overwrites");
    CHECK(orrery_forget(&O, 1) == ZXM_OK && !orrery_find(&O, 1) && O.nobs == ORRERY_OBS_MAX - 1 &&
              orrery_forget(&O, 1) == ZXM_E_ARG && orrery_observe(&O, 2, 0, 0, &id) == ZXM_OK &&
              id == ORRERY_OBS_MAX + 1,
          "orrery: forgetting one makes room, and numbers never repeat");

    /* OM1: an answer out of range is refused whole, and what was read is kept */
    static orrery_t Q;
    zxm_link_init(&FAKE, canned, &FAKE);
    orrery_init(&Q, &FAKE);
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"time_ms\":-5}}";
    CHECK(orrery_refresh(&Q) == ZXM_E_PROTOCOL && !Q.have, "orrery: a time before 1970 is refused");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"time_ms\":1000,\"known\":true,"
             "\"lat\":90000001,\"lon\":0}}";
    CHECK(orrery_refresh(&Q) == ZXM_E_PROTOCOL && !Q.have,
          "orrery: a latitude past the pole is refused");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"time_ms\":86400000,\"known\":false}}";
    CHECK(orrery_refresh(&Q) == ZXM_OK && Q.have && Q.day == 1 && !Q.place,
          "orrery: an unknown place is kept as unknown");
    CANNED = 0;
    CHECK(orr_rpc("place.forget_here", "{}") == ZXM_OK, "orrery: the place forgotten again");
}
