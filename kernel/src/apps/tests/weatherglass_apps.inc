/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Weatherglass's model against zxvd's real dispatcher (LINK): where the
 * machine is from place.ask, the forecast cached with files.write and read
 * back with files.read. zxvd has no HTTPS method, so the answers of MET
 * Norway's Locationforecast and the NWS alerts are small hand-written
 * samples in the endpoints' documented shapes, handed in as a bridge would. */
#include "weatherglass_model.h"

static weatherglass_t WG, WG2;

/* 2026-10-22T12:00:00Z */
#define WG_NOW 1792670400

/* Locationforecast 2.0 compact: five steps, the last outside the domain */
static const char WG_MET[] =
    "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[10.7522,59.9139,"
    "23]},\"properties\":{\"meta\":{\"updated_at\":\"2026-10-22T10:30:00Z\",\"units\":{"
    "\"air_temperature\":\"celsius\",\"precipitation_amount\":\"mm\",\"wind_speed\":\"m/s\"}},"
    "\"timeseries\":["
    "{\"time\":\"2026-10-22T11:00:00Z\",\"data\":{\"instant\":{\"details\":{"
    "\"air_pressure_at_sea_level\":1012.3,\"air_temperature\":12.3,\"wind_from_direction\":"
    "200.1,\"wind_speed\":4.1}},\"next_1_hours\":{\"summary\":{\"symbol_code\":\"lightrain\"},"
    "\"details\":{\"precipitation_amount\":0.2}}}},"
    "{\"time\":\"2026-10-22T12:00:00Z\",\"data\":{\"instant\":{\"details\":{\"air_temperature\":"
    "-0.4,\"wind_speed\":18.0}},\"next_1_hours\":{\"summary\":{\"symbol_code\":\"cloudy\"},"
    "\"details\":{\"precipitation_amount\":0.0}}}},"
    "{\"time\":\"2026-10-23T12:00:00Z\",\"data\":{\"instant\":{\"details\":{\"air_temperature\":"
    "36.15,\"wind_speed\":2}},\"next_6_hours\":{\"summary\":{\"symbol_code\":\"rain\"},"
    "\"details\":{\"precipitation_amount\":12.7}}}},"
    "{\"time\":\"2026-10-30T00:00:00Z\",\"data\":{\"instant\":{\"details\":{\"air_temperature\":"
    "5.0,\"wind_speed\":1.0}}}},"
    "{\"time\":\"2026-10-30T06:00:00Z\",\"data\":{\"instant\":{\"details\":{\"air_temperature\":"
    "150.0,\"wind_speed\":1.0}}}}"
    "]}}";

/* NWS /alerts/active?point=: two features, one headline null */
static const char WG_NWS[] =
    "{\"type\":\"FeatureCollection\",\"features\":["
    "{\"type\":\"Feature\",\"properties\":{\"event\":\"Frost Advisory\",\"severity\":\"Minor\","
    "\"headline\":\"Frost Advisory issued October 22 at 3:00AM\"}},"
    "{\"type\":\"Feature\",\"properties\":{\"event\":\"Wind Advisory\",\"severity\":\"Moderate\","
    "\"headline\":null}}],\"title\":\"Current watches, warnings, and advisories\"}";

static const char WG_CACHED[] = "weatherglass 1\nat 59913900 10752200\nupdated 1792665000\n"
                                "1792666800 123 41 2\n1792670400 -4 180 0\n"
                                "1792756800 361 20 127\n1793318400 50 10 0\n";

static bool wg_file_is(const char *want)
{
    static uint8_t buf[WEATHERGLASS_TEXT_MAX];
    uint32_t got = 0;
    uint64_t size = 0;
    if (zxf_read(WEATHERGLASS_CACHE, 0, buf, sizeof buf - 1u, true, &got, &size) != 0) return false;
    buf[got] = 0;
    return size == got && !strcmp((const char *) buf, want);
}

APPS_TEST(weatherglass)
{
    /* the rules (WG1-WG4) */
    int64_t num = 0, den = 0, wk[3] = {0, 0, 0};
    CHECK(weatherglass_fahrenheit(0, &num, &den) == 0 && num == 320 && den == 1 &&
              weatherglass_fahrenheit(1, &num, &den) == 0 && num == 1609 && den == 5 &&
              weatherglass_fahrenheit(-1000, &num, &den) == 0 && num == -1480 && den == 1 &&
              weatherglass_fahrenheit(-400, &num, &den) == 0 && num == -400 &&
              weatherglass_fahrenheit(1001, &num, &den) == WEATHERGLASS_REFUSED &&
              weatherglass_fahrenheit(-1001, &num, &den) == WEATHERGLASS_REFUSED && num == -400 &&
              weatherglass_fahrenheit(0, NULL, &den) == WEATHERGLASS_REFUSED,
          "fahrenheit: exact, refused outside -100..100 degrees");
    CHECK(weatherglass_rain_inch(127, &num, &den) == 0 && num == 50 && den == 1 &&
              weatherglass_rain_inch(1, &num, &den) == 0 && num == 50 && den == 127 &&
              weatherglass_rain_inch(99999, &num, &den) == 0 && num == 4999950 && den == 127 &&
              weatherglass_rain_inch(-1, &num, &den) == WEATHERGLASS_REFUSED &&
              weatherglass_rain_inch(100000, &num, &den) == WEATHERGLASS_REFUSED,
          "rain: hundredths of an inch, exact");
    CHECK(weatherglass_wind_kmh(5, &num, &den) == 0 && num == 18 && den == 1 &&
              weatherglass_wind_kmh(172, &num, &den) == 0 && num == 3096 && den == 5 &&
              weatherglass_wind_kmh(2000, &num, &den) == 0 && num == 7200 &&
              weatherglass_wind_kmh(-1, &num, &den) == WEATHERGLASS_REFUSED &&
              weatherglass_wind_kmh(2001, &num, &den) == WEATHERGLASS_REFUSED,
          "wind: tenths of a km/h, exact");
    CHECK(weatherglass_floor(1564, 5) == 312 && weatherglass_floor(-1, 5) == -1 &&
              weatherglass_floor(-5, 5) == -1 && weatherglass_floor(0, 7) == 0,
          "floor toward minus infinity");
    CHECK(weatherglass_warnings(0, 0) == WEATHERGLASS_FROST && weatherglass_warnings(1, 171) == 0 &&
              weatherglass_warnings(349, 0) == 0 &&
              weatherglass_warnings(350, 0) == WEATHERGLASS_HEAT &&
              weatherglass_warnings(200, 172) == WEATHERGLASS_GALE &&
              weatherglass_warnings(-4, 180) == (WEATHERGLASS_FROST | WEATHERGLASS_GALE) &&
              weatherglass_warnings(1001, 0) == WEATHERGLASS_REFUSED &&
              weatherglass_warnings(0, -1) == WEATHERGLASS_REFUSED &&
              weatherglass_warnings(0, 2001) == WEATHERGLASS_REFUSED,
          "warnings: the thresholds are inside, the domain refused past");
    CHECK(weatherglass_calm(1, 0) && weatherglass_calm(349, 171) && !weatherglass_calm(0, 0) &&
              !weatherglass_calm(350, 0) && !weatherglass_calm(200, 172) &&
              !weatherglass_calm(200, -1),
          "calm: no flag");
    int64_t t7[7] = {-50, 10, 25, 120, -3, 60, 77}, t2[2] = {-1, -2}, bad[2] = {0, 1001};
    CHECK(weatherglass_week(t7, 7, wk) == 0 && wk[0] == -50 && wk[1] == 120 && wk[2] == 34 &&
              weatherglass_week(t2, 2, wk) == 0 && wk[0] == -2 && wk[1] == -1 && wk[2] == -2 &&
              weatherglass_week(bad, 2, wk) == WEATHERGLASS_REFUSED &&
              weatherglass_week(t7, 0, wk) == WEATHERGLASS_REFUSED,
          "week: lowest, highest, floored mean");
    CHECK(weatherglass_reading_ok(1792627200, -35, 41, 2) &&
              weatherglass_reading_ok(99999999999, 9999, 9999, 99999) &&
              !weatherglass_reading_ok(100000000000, 0, 0, 0) &&
              !weatherglass_reading_ok(0, 10000, 0, 0) && !weatherglass_reading_ok(0, 0, 0, 100000),
          "a reading's record");
    CHECK(weatherglass_place_ok(90000000, -180000000) && !weatherglass_place_ok(90000001, 0) &&
              weatherglass_approval_ok(1, 1) && !weatherglass_approval_ok(1, 2) &&
              !weatherglass_approval_ok(0, 1) && weatherglass_day(1792670400) == 20748 &&
              weatherglass_day(-1) == -1,
          "the used rules: place_ok, approval_ok, days");
    CHECK(!strncmp(WEATHERGLASS_ATTRIBUTION, "Alerts from the U.S. National Weather Service", 45) &&
              strstr(WEATHERGLASS_ATTRIBUTION,
                     "\nWeather data from the Norwegian Meteorological Institute (MET Norway), "
                     "CC BY 4.0\n"),
          "the attribution, as SOURCES.tsv gives it, sorted");

    /* nothing is read from the network before a location and the owner's word */
    CHECK(zxf_use_space(NULL) == 0, "home");
    CHECK(zpl_open(NULL) == 0, "places in memory");
    weatherglass_init(&WG, &LINK);
    int before = sent;
    CHECK(weatherglass_take_forecast(&WG, WG_MET, sizeof WG_MET - 1u, WG_NOW) == ZXM_E_STATE &&
              weatherglass_take_alerts(&WG, WG_NWS, sizeof WG_NWS - 1u, WG_NOW) == ZXM_E_STATE &&
              sent == before && WG.n == 0,
          "not located: nothing taken, nothing sent");

    /* no cache yet: an empty forecast */
    weatherglass_init(&WG2, &LINK);
    CHECK(weatherglass_load(&WG2, WG_NOW) == ZXM_OK && WG2.n == 0 && !WG2.now.known,
          "no cache yet: %s", LINK.err);

    /* where: place.ask, under the owner's answer */
    CHECK(weatherglass_locate(&WG) == ZXM_E_STATE && !strcmp(WG.answer, "pending") && !WG.located &&
              strstr(WG.notice, "waiting for the owner"),
          "no answer yet: pending (%s)", WG.notice);
    CHECK(zpl_permit(WEATHERGLASS_APP, ZPL_NEVER) == 0 && weatherglass_locate(&WG) == ZXM_E_STATE &&
              !strcmp(WG.answer, "never"),
          "the owner said never");
    zpl_forget_here();
    CHECK(zpl_permit(WEATHERGLASS_APP, ZPL_ALWAYS) == 0 &&
              weatherglass_locate(&WG) == ZXM_E_STATE && !strcmp(WG.answer, "unknown") &&
              strstr(WG.notice, "No location"),
          "allowed, but no location set");
    CHECK(zpl_set_here((zpl_point_t){59913900, 10752200}, 50, "owner") == 0 &&
              weatherglass_locate(&WG) == ZXM_OK && WG.located && WG.lat == 59913900 &&
              WG.lon == 10752200,
          "located: %s", LINK.err);
    CHECK(!strcmp(WG.forecast_url, "https://api.met.no/weatherapi/locationforecast/2.0/"
                                   "compact?lat=59.9139&lon=10.7522") &&
              !strcmp(WG.alerts_url, "https://api.weather.gov/alerts/active?point=59.9139,10.7522"),
          "the addresses: %s %s", WG.forecast_url, WG.alerts_url);

    /* the owner's word for the network (warren.approval_ok) */
    before = sent;
    CHECK(weatherglass_approve(&WG, 1, 2) == ZXM_E_STATE && !WG.approved &&
              weatherglass_approve(&WG, 0, 1) == ZXM_E_STATE && !WG.approved &&
              weatherglass_take_forecast(&WG, WG_MET, sizeof WG_MET - 1u, WG_NOW) == ZXM_E_STATE &&
              sent == before,
          "not approved: nothing taken");
    CHECK(weatherglass_approve(&WG, 1, 1) == ZXM_OK && WG.approved, "approved");

    /* the forecast: parsed into tenths, cached, shown */
    CHECK(weatherglass_take_forecast(&WG, WG_MET, sizeof WG_MET - 1u, WG_NOW) == ZXM_OK &&
              WG.n == 4 && WG.updated == 1792665000 && !WG.from_cache,
          "forecast taken: %d readings (%s)", (int) WG.n, LINK.err);
    CHECK(WG.r[0].stamp == 1792666800 && WG.r[0].temp == 123 && WG.r[0].wind == 41 &&
              WG.r[0].rain == 2 && WG.r[1].temp == -4 && WG.r[1].wind == 180 &&
              WG.r[2].temp == 361 && WG.r[2].wind == 20 && WG.r[2].rain == 127 &&
              WG.r[3].rain == 0 && WG.r[3].stamp == 1793318400,
          "decimals read as tenths, floored; 150.0 degrees left out");
    CHECK(wg_file_is(WG_CACHED), "the cache in the Hoard (WG5)");
    CHECK(WG.now.known && WG.now.at.stamp == WG_NOW && WG.now.temp_f == 312 &&
              WG.now.wind_kmh == 648 && WG.now.rain_in == 0 &&
              WG.now.flags == (WEATHERGLASS_FROST | WEATHERGLASS_GALE) && !WG.now.calm &&
              WG.now.week_n == 3 && WG.now.week[0] == -4 && WG.now.week[1] == 361 &&
              WG.now.week[2] == 160,
          "now: -0.4 C and a gale, the week of three readings");
    CHECK(strstr(WG.notice, "Now: -0.4 C (31.2 F), wind 18.0 m/s (64.8 km/h), rain 0.0 mm "
                            "(0.00 in). This week: low -0.4 C, high 36.1 C, mean 16.0 C.") &&
              strstr(WG.notice, "Advisory: frost (at or below 0.0 C); a gale (wind at or above "
                                "17.2 m/s). This is Weatherglass's own reading") &&
              strstr(WG.notice, "official warnings always take precedence"),
          "the notice: %s", WG.notice);

    /* alerts: shown, never cached */
    CHECK(weatherglass_take_alerts(&WG, WG_NWS, sizeof WG_NWS - 1u, WG_NOW) == ZXM_OK &&
              WG.nalerts == 2 && WG.alerts_total == 2 &&
              !strcmp(WG.alert[0].event, "Frost Advisory") &&
              !strcmp(WG.alert[1].severity, "Moderate") && !WG.alert[1].headline[0] &&
              strstr(WG.notice, "2 alerts from the National Weather Service: Frost Advisory "
                                "(Minor); Wind Advisory (Moderate). Follow the official warnings."),
          "alerts: %s", WG.notice);
    CHECK(wg_file_is(WG_CACHED), "alerts are not cached");

    /* an answer that is not the documented shape changes nothing */
    static const char nothing[] = "{\"properties\":{\"meta\":{\"updated_at\":"
                                  "\"2026-10-22T10:30:00Z\"},\"timeseries\":[]}}";
    static const char expo[] =
        "{\"properties\":{\"meta\":{\"updated_at\":\"2026-10-22T10:30:00Z\"},"
        "\"timeseries\":[{\"time\":\"2026-10-22T11:00:00Z\",\"data\":{"
        "\"instant\":{\"details\":{\"air_temperature\":1.2e1,"
        "\"wind_speed\":1}}}}]}}";
    static const char late[] =
        "{\"properties\":{\"meta\":{\"updated_at\":\"2026-13-22T10:30:00Z\"},"
        "\"timeseries\":[]}}";
    before = sent;
    CHECK(weatherglass_take_forecast(&WG, nothing, sizeof nothing - 1u, WG_NOW) == ZXM_E_PROTOCOL &&
              weatherglass_take_forecast(&WG, expo, sizeof expo - 1u, WG_NOW) == ZXM_E_PROTOCOL &&
              weatherglass_take_forecast(&WG, late, sizeof late - 1u, WG_NOW) == ZXM_E_PROTOCOL &&
              weatherglass_take_forecast(&WG, "[1]", 3, WG_NOW) == ZXM_E_PROTOCOL &&
              weatherglass_take_alerts(&WG, "{\"features\":[{}]}", 17, WG_NOW) == ZXM_E_PROTOCOL &&
              weatherglass_take_forecast(&WG, WG_MET, 0, WG_NOW) == ZXM_E_ARG && WG.n == 4 &&
              WG.nalerts == 2 && sent == before && wg_file_is(WG_CACHED),
          "not a forecast: refused, nothing written");

    /* offline: the cache, read back by another instance */
    weatherglass_init(&WG2, &LINK);
    CHECK(weatherglass_load(&WG2, WG_NOW) == ZXM_OK && WG2.from_cache && WG2.n == 4 &&
              !memcmp(WG2.r, WG.r, 4 * sizeof WG.r[0]) && WG2.updated == 1792665000 &&
              WG2.now.flags == WG.now.flags && WG2.nalerts == 0 &&
              strstr(WG2.notice, "Offline, the forecast last read: -0.4 C (31.2 F)") == WG2.notice,
          "offline from the cache: %s", WG2.notice);
    /* a week later only the last reading is in the week, and it is shown */
    weatherglass_show(&WG2, 1793318400);
    CHECK(WG2.now.at.stamp == 1793318400 && WG2.now.calm && WG2.now.flags == 0 &&
              WG2.now.week_n == 1 && WG2.now.week[2] == 50 && !strstr(WG2.notice, "Advisory"),
          "a calm day: no advisory: %s", WG2.notice);

    /* south and west: the signs in the addresses */
    CHECK(zpl_set_here((zpl_point_t){-33868800, -151209300}, 50, "owner") == 0 &&
              weatherglass_locate(&WG2) == ZXM_OK &&
              strstr(WG2.forecast_url, "?lat=-33.8688&lon=-151.2093") &&
              strstr(WG2.alerts_url, "?point=-33.8688,-151.2093"),
          "signs: %s", WG2.forecast_url);
    zpl_close();
}
