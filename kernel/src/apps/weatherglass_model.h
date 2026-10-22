/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* weatherglass_model.h — Weatherglass, local conditions and forecast for
 * where the machine is, as a model (zxm.h).
 *
 * ADVISORY ONLY. Frost, heat and wind warnings are this app's own reading
 * of a forecast, never an official warning; official warnings always take
 * precedence, and the notice says so every time it shows one.
 *
 * Where: zxvd's place.ask, under the owner's answer for this app (P2); the
 * point is checked as waymark.place_ok checks it. What: MET Norway
 * Locationforecast (compact JSON, CC BY 4.0) and US NWS active alerts
 * (GeoJSON, public domain), the live endpoints of the core pack
 * weatherglass-core.tar (tools/apps/datapacks/SOURCES.tsv). zxvd has no
 * method that fetches over HTTPS, so the model composes the two addresses
 * (forecast_url, alerts_url) and reads the answers a bridge hands it, and
 * only after the owner approved the network (warren.approval_ok). The last
 * forecast is cached in the Hoard (files.read, files.write) so it shows
 * offline; alerts are never cached. zxvd has no notification method
 * either, so the notice is text for the view. No drawing, no allocation,
 * integers only: a decimal in the JSON is read as text into tenths.
 *
 *   WG1 UNITS. A temperature in tenths of a degree Celsius is
 *       celsius * 9 / 5 + 320 tenths of a degree Fahrenheit; rain in tenths
 *       of a millimetre is rain * 50 / 127 hundredths of an inch (25.4 mm
 *       to the inch); wind in tenths of a metre a second is wind * 18 / 5
 *       tenths of a kilometre an hour. Each is an exact reduced rational;
 *       a value is shown floored toward minus infinity. The domains are
 *       celsius -1000 .. 1000, rain 0 .. 99999 and wind 0 .. 2000; anything
 *       else is refused.
 *   WG2 WARNINGS. Flags to add: WEATHERGLASS_FROST (temp at or below 0),
 *       WEATHERGLASS_HEAT (at or above 350, 35.0 degrees) and
 *       WEATHERGLASS_GALE (wind at or above 172, 17.2 m/s), for temp
 *       -1000 .. 1000 and wind 0 .. 2000, else refused. Calm is no flag:
 *       temp 1 .. 349 and wind 0 .. 171.
 *   WG3 THE WEEK. Every reading of the seven days from today (almanac.days
 *       of now): the lowest, the highest and the mean floored toward minus
 *       infinity, every reading inside -1000 .. 1000.
 *   WG4 A READING. stamp (seconds since 1970), temp, wind and rain (of the
 *       next period of the forecast: an hour, or six hours where it steps
 *       by six) fit ^ZXWEATHER: 11, 4, 4 and 5 digits, a sign aside. A
 *       reading the forecast gives outside the WG1 domains is not kept.
 *   WG5 THE CACHE. WEATHERGLASS_CACHE, one UTF-8 text file: "weatherglass
 *       1", "at <lat> <lon>", "updated <seconds>", then one
 *       "<stamp> <temp> <wind> <rain>" line per reading, in order, each
 *       line ending in "\n". Written whole after a forecast is read, and
 *       only then kept: a write zxvd refuses leaves the model as it was. */
#ifndef WEATHERGLASS_MODEL_H
#define WEATHERGLASS_MODEL_H

#include "zxm.h"

#define WEATHERGLASS_APP        "weatherglass"       /* its name for place.ask */
#define WEATHERGLASS_CACHE      "Weatherglass.cache" /* in the Hoard's home */
#define WEATHERGLASS_READINGS   96u                  /* MET's compact answer has about 90 */
#define WEATHERGLASS_ALERTS     8u
#define WEATHERGLASS_EVENT_MAX  63u
#define WEATHERGLASS_LEVEL_MAX  15u
#define WEATHERGLASS_HEAD_MAX   255u
#define WEATHERGLASS_URL_MAX    159u
#define WEATHERGLASS_NOTICE_MAX 1535u
#define WEATHERGLASS_JSON_MAX   (256u * 1024u) /* one answer handed in */
#define WEATHERGLASS_TOKENS     16384u
#define WEATHERGLASS_TEXT_MAX   8192u /* the cache, every line full */
#define WEATHERGLASS_REFUSED    (-1)
#define WEATHERGLASS_E_FILES    (-32014) /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */

#define WEATHERGLASS_TEMP_MIN (-1000) /* tenths of a degree Celsius */
#define WEATHERGLASS_TEMP_MAX 1000
#define WEATHERGLASS_RAIN_MAX 99999 /* tenths of a millimetre */
#define WEATHERGLASS_WIND_MAX 2000  /* tenths of a metre a second */
#define WEATHERGLASS_FROST_AT 0     /* at or below */
#define WEATHERGLASS_HEAT_AT  350   /* at or above */
#define WEATHERGLASS_GALE_AT  172   /* at or above */

enum { WEATHERGLASS_FROST = 1, WEATHERGLASS_HEAT = 2, WEATHERGLASS_GALE = 4 };

/* The addresses of the core pack's live endpoints (SOURCES.tsv), {lat} and
 * {lon} in decimal degrees to four places. */
#define WEATHERGLASS_FORECAST_AT "https://api.met.no/weatherapi/locationforecast/2.0/compact"
#define WEATHERGLASS_ALERTS_AT   "https://api.weather.gov/alerts/active"

/* ATTRIBUTION.txt as the pack carries it: each source's line from
 * SOURCES.tsv, sorted, one per line. The view shows it whenever it shows
 * a forecast or an alert. */
extern const char WEATHERGLASS_ATTRIBUTION[];

typedef struct {
    int64_t stamp; /* seconds since 1970 */
    int32_t temp;  /* tenths of a degree Celsius */
    int32_t wind;  /* tenths of a metre a second */
    int32_t rain;  /* tenths of a millimetre, the next period */
} weatherglass_reading_t;

typedef struct {
    char event[WEATHERGLASS_EVENT_MAX + 1];    /* "Frost Advisory" */
    char severity[WEATHERGLASS_LEVEL_MAX + 1]; /* "Minor" .. "Extreme", "Unknown" */
    char headline[WEATHERGLASS_HEAD_MAX + 1];
} weatherglass_alert_t;

/* What is shown now (weatherglass_t.now): the reading for now, its units
 * floored for display, its flags and the week. */
typedef struct {
    bool known;
    weatherglass_reading_t at;
    int64_t temp_f;   /* tenths of a degree Fahrenheit */
    int64_t wind_kmh; /* tenths of a km/h */
    int64_t rain_in;  /* hundredths of an inch */
    int32_t flags;    /* WG2 */
    bool calm;
    uint32_t week_n; /* readings in the week */
    int64_t week[3]; /* lowest, highest, floored mean (WG3) */
} weatherglass_now_t;

typedef struct {
    zxm_link_t *link;
    bool located;     /* place.ask answered allowed with a sound point */
    int32_t lat, lon; /* millionths of a degree */
    char answer[16];  /* place.ask's last answer */
    bool approved;    /* the owner's word for the network (warren.approval_ok) */
    char forecast_url[WEATHERGLASS_URL_MAX + 1];
    char alerts_url[WEATHERGLASS_URL_MAX + 1];
    weatherglass_reading_t r[WEATHERGLASS_READINGS];
    uint32_t n;
    int64_t updated; /* the forecast's own time, seconds since 1970 */
    bool from_cache; /* read from WEATHERGLASS_CACHE, not handed in */
    weatherglass_alert_t alert[WEATHERGLASS_ALERTS];
    uint32_t nalerts, alerts_total; /* kept, and in the answer */
    weatherglass_now_t now;
    char notice[WEATHERGLASS_NOTICE_MAX + 1];
} weatherglass_t;

/* The rules, as the app's logic says them (weatherglass.zxapp). */

/* WG1: num/den reduced, den > 0; 0, or WEATHERGLASS_REFUSED outside the
 * domain (num and den are then left alone). */
int weatherglass_fahrenheit(int64_t celsius, int64_t *num, int64_t *den);
int weatherglass_rain_inch(int64_t rain, int64_t *num, int64_t *den);
int weatherglass_wind_kmh(int64_t wind, int64_t *num, int64_t *den);
/* floor(num / den) toward minus infinity, den > 0. */
int64_t weatherglass_floor(int64_t num, int64_t den);
/* WG2: the flags, or WEATHERGLASS_REFUSED. */
int32_t weatherglass_warnings(int64_t temp, int64_t wind);
bool weatherglass_calm(int64_t temp, int64_t wind);
/* WG3: out = lowest, highest, floored mean; 0, or WEATHERGLASS_REFUSED for
 * no reading or one outside the domain. */
int weatherglass_week(const int64_t *temp, uint32_t n, int64_t out[3]);
/* WG4 */
bool weatherglass_reading_ok(int64_t stamp, int64_t temp, int64_t wind, int64_t rain);
/* The used rules: waymark.place_ok, warren.approval_ok, almanac.days. */
bool weatherglass_place_ok(int64_t lat, int64_t lon);
bool weatherglass_approval_ok(int64_t owner, int64_t approved);
int64_t weatherglass_day(int64_t seconds);

/* The app. */
void weatherglass_init(weatherglass_t *w, zxm_link_t *link);
/* Ask zxvd where the machine is (place.ask): ZXM_OK and the addresses when
 * allowed; ZXM_E_STATE with the reason in the notice when the owner has
 * not answered, said never, or no location is set. */
int weatherglass_locate(weatherglass_t *w);
/* The owner's word for the network: owner 1 in the owner's own session,
 * approved 1 for yes. Anything else withdraws it (ZXM_E_STATE). */
int weatherglass_approve(weatherglass_t *w, int64_t owner, int64_t approved);
/* MET's answer from forecast_url, as handed in: parsed, cached (WG5), and
 * shown for now (seconds since 1970). ZXM_E_STATE before locate and
 * approve; ZXM_E_PROTOCOL for an answer that is not the documented shape. */
int weatherglass_take_forecast(weatherglass_t *w, const char *json, uint32_t len, int64_t now);
/* The NWS answer from alerts_url, as handed in (never cached). */
int weatherglass_take_alerts(weatherglass_t *w, const char *json, uint32_t len, int64_t now);
/* The cached forecast, offline (WG5); no cache yet is ZXM_OK and nothing. */
int weatherglass_load(weatherglass_t *w, int64_t now);
/* What is shown for now: the last reading at or before now (the first when
 * all are later), and the notice. */
void weatherglass_show(weatherglass_t *w, int64_t now);

#endif
