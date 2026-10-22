/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* weatherglass_model.c — Weatherglass, local conditions and forecast (see
 * weatherglass_model.h). Advisory only: official warnings take precedence. */
#include <string.h>

#include "weatherglass_model.h"

const char WEATHERGLASS_ATTRIBUTION[] =
    "Alerts from the U.S. National Weather Service (public domain); official warnings always "
    "take precedence\n"
    "Weather data from the Norwegian Meteorological Institute (MET Norway), CC BY 4.0\n";

/* ---- the rules ---------------------------------------------------------- */

static int64_t gcd(int64_t a, int64_t b)
{
    if (a < 0) a = -a;
    while (b) {
        int64_t t = a % b;
        a = b;
        b = t;
    }
    return a ? a : 1;
}

/* num/den = x * mul / div + add, reduced (every product is small: the
 * domains keep x * mul under 10^7). */
static void exact(int64_t x, int64_t mul, int64_t div, int64_t add, int64_t *num, int64_t *den)
{
    int64_t n = x * mul + add * div, g = gcd(n, div);
    *num = n / g;
    *den = div / g;
}

int weatherglass_fahrenheit(int64_t celsius, int64_t *num, int64_t *den)
{
    if (!num || !den || celsius < WEATHERGLASS_TEMP_MIN || celsius > WEATHERGLASS_TEMP_MAX)
        return WEATHERGLASS_REFUSED;
    exact(celsius, 9, 5, 320, num, den);
    return 0;
}

int weatherglass_rain_inch(int64_t rain, int64_t *num, int64_t *den)
{
    if (!num || !den || rain < 0 || rain > WEATHERGLASS_RAIN_MAX) return WEATHERGLASS_REFUSED;
    exact(rain, 50, 127, 0, num, den);
    return 0;
}

int weatherglass_wind_kmh(int64_t wind, int64_t *num, int64_t *den)
{
    if (!num || !den || wind < 0 || wind > WEATHERGLASS_WIND_MAX) return WEATHERGLASS_REFUSED;
    exact(wind, 18, 5, 0, num, den);
    return 0;
}

int64_t weatherglass_floor(int64_t num, int64_t den)
{
    int64_t q = num / den;
    if (num % den != 0 && num < 0) q -= 1;
    return q;
}

int32_t weatherglass_warnings(int64_t temp, int64_t wind)
{
    if (temp < WEATHERGLASS_TEMP_MIN || temp > WEATHERGLASS_TEMP_MAX || wind < 0 ||
        wind > WEATHERGLASS_WIND_MAX)
        return WEATHERGLASS_REFUSED;
    int32_t f = 0;
    if (temp <= WEATHERGLASS_FROST_AT) f |= WEATHERGLASS_FROST;
    if (temp >= WEATHERGLASS_HEAT_AT) f |= WEATHERGLASS_HEAT;
    if (wind >= WEATHERGLASS_GALE_AT) f |= WEATHERGLASS_GALE;
    return f;
}

bool weatherglass_calm(int64_t temp, int64_t wind)
{
    return temp > WEATHERGLASS_FROST_AT && temp < WEATHERGLASS_HEAT_AT && wind >= 0 &&
           wind < WEATHERGLASS_GALE_AT;
}

int weatherglass_week(const int64_t *temp, uint32_t n, int64_t out[3])
{
    if (!temp || !n || !out) return WEATHERGLASS_REFUSED;
    int64_t lo = WEATHERGLASS_TEMP_MAX, hi = WEATHERGLASS_TEMP_MIN, s = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (temp[i] < WEATHERGLASS_TEMP_MIN || temp[i] > WEATHERGLASS_TEMP_MAX)
            return WEATHERGLASS_REFUSED;
        lo = temp[i] < lo ? temp[i] : lo;
        hi = temp[i] > hi ? temp[i] : hi;
        s += temp[i]; /* n * 1000 at most: n is a count of readings */
    }
    out[0] = lo;
    out[1] = hi;
    out[2] = weatherglass_floor(s, (int64_t) n);
    return 0;
}

bool weatherglass_reading_ok(int64_t stamp, int64_t temp, int64_t wind, int64_t rain)
{
    return stamp >= -99999999999 && stamp <= 99999999999 && temp >= -9999 && temp <= 9999 &&
           wind >= -9999 && wind <= 9999 && rain >= -99999 && rain <= 99999;
}

bool weatherglass_place_ok(int64_t lat, int64_t lon)
{
    return lat >= -90000000 && lat <= 90000000 && lon >= -180000000 && lon <= 180000000;
}

bool weatherglass_approval_ok(int64_t owner, int64_t approved)
{
    return owner == 1 && approved == 1;
}

int64_t weatherglass_day(int64_t seconds)
{
    return weatherglass_floor(seconds, 86400);
}

/* ---- text ------------------------------------------------------------------ */

/* Append s to out at *at, bounded by cap (room for a NUL is kept). */
static bool put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    uint32_t n = (uint32_t) strlen(s);
    if (*at + n + 1u > cap) return false;
    memcpy(out + *at, s, n);
    *at += n;
    out[*at] = 0;
    return true;
}

static bool put_num(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    char t[24];
    int k = 23;
    bool neg = v < 0;
    uint64_t u = neg ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    t[k] = 0;
    do {
        t[--k] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u && k > 1);
    if (neg) t[--k] = '-';
    return put(out, cap, at, t + k);
}

/* v / scale with `places` digits after the point ("-0.5", "54.1", "0.05"). */
static bool put_fixed(char *out, uint32_t cap, uint32_t *at, int64_t v, int64_t scale,
                      uint32_t places)
{
    char frac[8];
    uint64_t u = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    uint64_t f = u % (uint64_t) scale;
    for (uint32_t i = places; i > 0; i--) {
        frac[i - 1u] = (char) ('0' + (int) (f % 10u));
        f /= 10u;
    }
    frac[places] = 0;
    return (v >= 0 || put(out, cap, at, "-")) &&
           put_num(out, cap, at, (int64_t) (u / (uint64_t) scale)) && put(out, cap, at, ".") &&
           put(out, cap, at, frac);
}

/* A JSON number as tenths, floored toward minus infinity: -?digits(.digits)?,
 * at most 9 whole digits; an exponent is refused (no binary approximations). */
static bool tenths_of(const char *s, uint32_t n, int64_t *v)
{
    uint32_t i = 0, digits = 0;
    bool neg = false, rest = false;
    int64_t x = 0;
    if (i < n && s[i] == '-') {
        neg = true;
        i++;
    }
    while (i < n && s[i] >= '0' && s[i] <= '9') {
        if (++digits > 9u) return false;
        x = x * 10 + (s[i] - '0');
        i++;
    }
    if (!digits) return false;
    x *= 10;
    if (i < n && s[i] == '.') {
        i++;
        if (i >= n || s[i] < '0' || s[i] > '9') return false;
        x += s[i] - '0';
        i++;
        while (i < n && s[i] >= '0' && s[i] <= '9') {
            rest = rest || s[i] != '0';
            i++;
        }
    }
    if (i != n) return false;
    *v = neg ? -x - (rest ? 1 : 0) : x;
    return true;
}

/* Days from 1970-01-01 to a civil date (proleptic Gregorian). */
static int64_t days_from_civil(int64_t y, int64_t m, int64_t d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static bool digits_at(const char *s, uint32_t at, uint32_t n, int64_t *v)
{
    int64_t x = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (s[at + i] < '0' || s[at + i] > '9') return false;
        x = x * 10 + (s[at + i] - '0');
    }
    *v = x;
    return true;
}

/* "YYYY-MM-DDTHH:MM:SSZ" (RFC 3339 in UTC, as MET writes it) as seconds. */
static bool stamp_of(const char *s, uint32_t n, int64_t *t)
{
    static const int8_t mdays[12] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int64_t y, mo, d, h, mi, se;
    if (n != 20u || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' || s[16] != ':' ||
        s[19] != 'Z' || !digits_at(s, 0, 4, &y) || !digits_at(s, 5, 2, &mo) ||
        !digits_at(s, 8, 2, &d) || !digits_at(s, 11, 2, &h) || !digits_at(s, 14, 2, &mi) ||
        !digits_at(s, 17, 2, &se) || mo < 1 || mo > 12 || d < 1 || d > mdays[mo - 1] || h > 23 ||
        mi > 59 || se > 59)
        return false;
    *t = days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + se;
    return true;
}

/* ---- the answers handed in ------------------------------------------------- */

static w4_jtok_t TOK[WEATHERGLASS_TOKENS];
static int32_t NTOK;
static const char *JS;

static int32_t member(int32_t obj, const char *key, uint8_t type)
{
    if (obj < 0 || TOK[obj].type != W4_J_OBJ) return -1;
    int32_t v = w4_json_get(JS, TOK, NTOK, obj, key);
    return v >= 0 && TOK[v].type == type ? v : -1;
}

static bool tenths_at(int32_t obj, const char *key, int64_t *v)
{
    int32_t t = member(obj, key, W4_J_NUM);
    return t >= 0 && tenths_of(JS + TOK[t].start, TOK[t].end - TOK[t].start, v);
}

static bool stamp_at(int32_t obj, const char *key, int64_t *v)
{
    int32_t t = member(obj, key, W4_J_STR);
    return t >= 0 && stamp_of(JS + TOK[t].start, TOK[t].end - TOK[t].start, v);
}

/* A string member, cut to cap - 1 bytes on a character boundary; "" when
 * it is absent or null. */
static bool text_at(int32_t obj, const char *key, char *out, uint32_t cap)
{
    static char big[4096];
    out[0] = 0;
    int32_t v = obj >= 0 ? w4_json_get(JS, TOK, NTOK, obj, key) : -1;
    if (v < 0 || TOK[v].type == W4_J_NULL) return true;
    if (TOK[v].type != W4_J_STR) return false;
    int32_t n = w4_json_str(JS, &TOK[v], big, sizeof big);
    if (n < 0) return false;
    uint32_t k = zxm_utf8_cut(big, (uint32_t) n < cap - 1u ? (uint32_t) n : cap - 1u);
    memcpy(out, big, k);
    out[k] = 0;
    return true;
}

static bool tokenize(const char *json, uint32_t len)
{
    JS = json;
    NTOK = w4_json_parse(json, len, TOK, WEATHERGLASS_TOKENS, W4_JSON_MAX_DEPTH);
    return NTOK > 0 && TOK[0].type == W4_J_OBJ;
}

/* The rain of the next period: next_1_hours, else next_6_hours, else 0
 * (the last steps of a forecast carry neither). */
static bool rain_at(int32_t data, int64_t *rain)
{
    static const char *const NEXT[2] = {"next_1_hours", "next_6_hours"};
    for (uint32_t k = 0; k < 2u; k++) {
        int32_t p = member(data, NEXT[k], W4_J_OBJ);
        if (p < 0) continue;
        int32_t d = member(p, "details", W4_J_OBJ);
        if (d >= 0 && member(d, "precipitation_amount", W4_J_NUM) >= 0)
            return tenths_at(d, "precipitation_amount", rain);
    }
    *rain = 0;
    return true;
}

/* MET Locationforecast 2.0 compact (GeoJSON): properties.meta.updated_at,
 * and properties.timeseries[] of {time, data: {instant: {details:
 * {air_temperature, wind_speed}}, next_1_hours | next_6_hours: {details:
 * {precipitation_amount}}}}, in time order. */
static bool parse_forecast(weatherglass_reading_t *r, uint32_t *nr, int64_t *updated)
{
    int32_t props = member(0, "properties", W4_J_OBJ);
    int32_t meta = member(props, "meta", W4_J_OBJ);
    int32_t series = member(props, "timeseries", W4_J_ARR);
    if (series < 0 || !stamp_at(meta, "updated_at", updated)) return false;
    uint32_t n = 0;
    for (uint32_t i = 0; i < TOK[series].size && n < WEATHERGLASS_READINGS; i++) {
        int32_t e = w4_json_at(TOK, NTOK, series, i);
        int32_t data = member(e, "data", W4_J_OBJ);
        int32_t det = member(member(data, "instant", W4_J_OBJ), "details", W4_J_OBJ);
        int64_t t, temp, wind, rain;
        if (!stamp_at(e, "time", &t) || !tenths_at(det, "air_temperature", &temp) ||
            !tenths_at(det, "wind_speed", &wind) || !rain_at(data, &rain) ||
            (n && t <= r[n - 1u].stamp))
            return false;
        /* WG4: a reading outside the WG1 domains is not kept */
        if (weatherglass_warnings(temp, wind) == WEATHERGLASS_REFUSED || rain < 0 ||
            rain > WEATHERGLASS_RAIN_MAX || !weatherglass_reading_ok(t, temp, wind, rain))
            continue;
        r[n++] = (weatherglass_reading_t){t, (int32_t) temp, (int32_t) wind, (int32_t) rain};
    }
    *nr = n;
    return n > 0;
}

/* NWS active alerts (GeoJSON): features[].properties {event, severity,
 * headline}. */
static bool parse_alerts(weatherglass_alert_t *a, uint32_t *na, uint32_t *total)
{
    int32_t feats = member(0, "features", W4_J_ARR);
    if (feats < 0) return false;
    uint32_t n = 0;
    for (uint32_t i = 0; i < TOK[feats].size; i++) {
        int32_t p = member(w4_json_at(TOK, NTOK, feats, i), "properties", W4_J_OBJ);
        static weatherglass_alert_t x;
        if (p < 0 || !text_at(p, "event", x.event, sizeof x.event) || !x.event[0] ||
            !text_at(p, "severity", x.severity, sizeof x.severity) ||
            !text_at(p, "headline", x.headline, sizeof x.headline))
            return false;
        if (!x.severity[0]) memcpy(x.severity, "Unknown", 8);
        if (n < WEATHERGLASS_ALERTS) a[n++] = x;
    }
    *na = n;
    *total = TOK[feats].size;
    return true;
}

/* ---- zxvd ---------------------------------------------------------------- */

/* Decimal degrees to four places from millionths, toward zero (MET asks
 * for at most four decimals). */
static bool put_degrees(char *out, uint32_t cap, uint32_t *at, int32_t millionths)
{
    return put_fixed(out, cap, at, (int64_t) millionths / 100, 10000, 4);
}

static bool compose_urls(weatherglass_t *w)
{
    uint32_t a = 0, b = 0;
    char *f = w->forecast_url, *al = w->alerts_url;
    uint32_t cap = WEATHERGLASS_URL_MAX + 1u;
    return put(f, cap, &a, WEATHERGLASS_FORECAST_AT "?lat=") && put_degrees(f, cap, &a, w->lat) &&
           put(f, cap, &a, "&lon=") && put_degrees(f, cap, &a, w->lon) &&
           put(al, cap, &b, WEATHERGLASS_ALERTS_AT "?point=") && put_degrees(al, cap, &b, w->lat) &&
           put(al, cap, &b, ",") && put_degrees(al, cap, &b, w->lon);
}

void weatherglass_init(weatherglass_t *w, zxm_link_t *link)
{
    memset(w, 0, sizeof *w);
    w->link = link;
}

static void say(weatherglass_t *w, const char *s)
{
    uint32_t at = 0;
    w->notice[0] = 0;
    (void) put(w->notice, sizeof w->notice, &at, s);
}

int weatherglass_locate(weatherglass_t *w)
{
    zxm_link_t *l = w->link;
    zxm_begin(l, "place.ask");
    zxm_p_str(l, "app", WEATHERGLASS_APP);
    int rc = zxm_call(l);
    if (rc) return rc;
    char answer[16];
    if (zxm_get_str(l, l->result, "answer", answer, sizeof answer)) return ZXM_E_PROTOCOL;
    memcpy(w->answer, answer, sizeof answer);
    if (strcmp(answer, "allowed")) {
        w->located = false;
        w->forecast_url[0] = w->alerts_url[0] = 0;
        say(w, !strcmp(answer, "pending")
                   ? "Weatherglass is waiting for the owner to allow it to know the location "
                     "(Waymark)."
               : !strcmp(answer, "never")
                   ? "The owner said Weatherglass may never know the location."
                   : "No location is set. Set one in Waymark.");
        return ZXM_E_STATE;
    }
    int64_t lat, lon;
    if (zxm_get_i64(l, l->result, "lat", &lat) || zxm_get_i64(l, l->result, "lon", &lon) ||
        !weatherglass_place_ok(lat, lon))
        return ZXM_E_PROTOCOL;
    w->lat = (int32_t) lat;
    w->lon = (int32_t) lon;
    w->forecast_url[0] = w->alerts_url[0] = 0;
    if (!compose_urls(w)) return ZXM_E_TOOBIG;
    w->located = true;
    return ZXM_OK;
}

int weatherglass_approve(weatherglass_t *w, int64_t owner, int64_t approved)
{
    w->approved = weatherglass_approval_ok(owner, approved);
    if (w->approved) return ZXM_OK;
    say(w, "The network is not read: the owner has not approved it.");
    return ZXM_E_STATE;
}

/* The cache text for readings r (WG5); its length, or -1 when it does not fit. */
static int32_t compose_cache(const weatherglass_t *w, const weatherglass_reading_t *r, uint32_t n,
                             int64_t updated, char *out, uint32_t cap)
{
    uint32_t at = 0;
    if (!put(out, cap, &at, "weatherglass 1\nat ") || !put_num(out, cap, &at, w->lat) ||
        !put(out, cap, &at, " ") || !put_num(out, cap, &at, w->lon) ||
        !put(out, cap, &at, "\nupdated ") || !put_num(out, cap, &at, updated) ||
        !put(out, cap, &at, "\n"))
        return -1;
    for (uint32_t i = 0; i < n; i++)
        if (!put_num(out, cap, &at, r[i].stamp) || !put(out, cap, &at, " ") ||
            !put_num(out, cap, &at, r[i].temp) || !put(out, cap, &at, " ") ||
            !put_num(out, cap, &at, r[i].wind) || !put(out, cap, &at, " ") ||
            !put_num(out, cap, &at, r[i].rain) || !put(out, cap, &at, "\n"))
            return -1;
    return (int32_t) at;
}

int weatherglass_take_forecast(weatherglass_t *w, const char *json, uint32_t len, int64_t now)
{
    static weatherglass_reading_t r[WEATHERGLASS_READINGS];
    static char text[WEATHERGLASS_TEXT_MAX];
    if (!w->located || !w->approved) return ZXM_E_STATE;
    if (!json || !len || len > WEATHERGLASS_JSON_MAX) return ZXM_E_ARG;
    uint32_t n = 0;
    int64_t updated = 0;
    if (!tokenize(json, len) || !parse_forecast(r, &n, &updated)) return ZXM_E_PROTOCOL;
    int32_t tl = compose_cache(w, r, n, updated, text, sizeof text);
    if (tl < 0) return ZXM_E_TOOBIG;
    zxm_begin(w->link, "files.write");
    zxm_p_str(w->link, "path", WEATHERGLASS_CACHE);
    zxm_p_strn(w->link, "text", text, (uint32_t) tl);
    int rc = zxm_call(w->link);
    if (rc) return rc; /* WG5: nothing kept */
    memcpy(w->r, r, (size_t) n * sizeof r[0]);
    w->n = n;
    w->updated = updated;
    w->from_cache = false;
    weatherglass_show(w, now);
    return ZXM_OK;
}

int weatherglass_take_alerts(weatherglass_t *w, const char *json, uint32_t len, int64_t now)
{
    static weatherglass_alert_t a[WEATHERGLASS_ALERTS];
    if (!w->located || !w->approved) return ZXM_E_STATE;
    if (!json || !len || len > WEATHERGLASS_JSON_MAX) return ZXM_E_ARG;
    uint32_t n = 0, total = 0;
    if (!tokenize(json, len) || !parse_alerts(a, &n, &total)) return ZXM_E_PROTOCOL;
    memcpy(w->alert, a, (size_t) n * sizeof a[0]);
    w->nalerts = n;
    w->alerts_total = total;
    weatherglass_show(w, now);
    return ZXM_OK;
}

/* A signed decimal of 1 to 12 digits at s[*at], ended by `end`. */
static bool get_num(const char *s, uint32_t n, uint32_t *at, char end, int64_t *v)
{
    uint32_t i = *at, digits = 0;
    bool neg = i < n && s[i] == '-';
    int64_t x = 0;
    if (neg) i++;
    while (i < n && s[i] >= '0' && s[i] <= '9' && digits < 13u) {
        x = x * 10 + (s[i] - '0');
        i++;
        digits++;
    }
    if (!digits || digits > 12u || i >= n || s[i] != end) return false;
    *v = neg ? -x : x;
    *at = i + 1u;
    return true;
}

/* Parse the cache (WG5) into w's readings; false when it is not one. */
static bool parse_cache(weatherglass_t *w, const char *s, uint32_t n)
{
    static weatherglass_reading_t r[WEATHERGLASS_READINGS];
    static const char HEAD[] = "weatherglass 1\nat ";
    uint32_t at = (uint32_t) strlen(HEAD), k = 0;
    int64_t lat, lon, updated;
    if (n < at || memcmp(s, HEAD, at) || !get_num(s, n, &at, ' ', &lat) ||
        !get_num(s, n, &at, '\n', &lon) || !weatherglass_place_ok(lat, lon) || n - at < 8u ||
        memcmp(s + at, "updated ", 8))
        return false;
    at += 8u;
    if (!get_num(s, n, &at, '\n', &updated)) return false;
    while (at < n) {
        int64_t t, temp, wind, rain;
        if (k >= WEATHERGLASS_READINGS || !get_num(s, n, &at, ' ', &t) ||
            !get_num(s, n, &at, ' ', &temp) || !get_num(s, n, &at, ' ', &wind) ||
            !get_num(s, n, &at, '\n', &rain) || !weatherglass_reading_ok(t, temp, wind, rain) ||
            weatherglass_warnings(temp, wind) == WEATHERGLASS_REFUSED || rain < 0 ||
            (k && t <= r[k - 1u].stamp))
            return false;
        r[k++] = (weatherglass_reading_t){t, (int32_t) temp, (int32_t) wind, (int32_t) rain};
    }
    if (!k) return false;
    memcpy(w->r, r, (size_t) k * sizeof r[0]);
    w->n = k;
    w->updated = updated;
    w->from_cache = true;
    return true;
}

int weatherglass_load(weatherglass_t *w, int64_t now)
{
    static char text[WEATHERGLASS_TEXT_MAX];
    zxm_link_t *l = w->link;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", WEATHERGLASS_CACHE);
    int rc = zxm_call(l);
    if (rc == ZXM_E_REMOTE && l->err_code == WEATHERGLASS_E_FILES && strstr(l->err, "no such file"))
        return ZXM_OK; /* no forecast read yet */
    if (rc) return rc;
    bool eof;
    int32_t t = zxm_get(l, l->result, "text");
    if (zxm_get_bool(l, l->result, "eof", &eof) || t < 0) return ZXM_E_PROTOCOL;
    if (!eof) return ZXM_E_TOOBIG;
    int32_t n = w4_json_str(l->resp, &l->tok[t], text, sizeof text);
    if (n < 0) return ZXM_E_TOOBIG;
    if (!parse_cache(w, text, (uint32_t) n)) return ZXM_E_PROTOCOL;
    weatherglass_show(w, now);
    return ZXM_OK;
}

/* ---- what is shown ---------------------------------------------------------- */

static const char *const FLAG_TEXT[3] = {
    "frost (at or below 0.0 C)",
    "heat (at or above 35.0 C)",
    "a gale (wind at or above 17.2 m/s)",
};

void weatherglass_show(weatherglass_t *w, int64_t now)
{
    static int64_t week[WEATHERGLASS_READINGS];
    weatherglass_now_t *s = &w->now;
    memset(s, 0, sizeof *s);
    w->notice[0] = 0;
    char *o = w->notice;
    uint32_t cap = sizeof w->notice, at = 0;
    bool ok = true;
    if (w->n) {
        uint32_t pick = 0;
        for (uint32_t i = 0; i < w->n; i++)
            if (w->r[i].stamp <= now) pick = i;
        const weatherglass_reading_t *r = &w->r[pick];
        int64_t num, den;
        s->known = true;
        s->at = *r;
        /* every reading kept is inside the WG1 domains (WG4) */
        (void) weatherglass_fahrenheit(r->temp, &num, &den);
        s->temp_f = weatherglass_floor(num, den);
        (void) weatherglass_wind_kmh(r->wind, &num, &den);
        s->wind_kmh = weatherglass_floor(num, den);
        (void) weatherglass_rain_inch(r->rain, &num, &den);
        s->rain_in = weatherglass_floor(num, den);
        s->flags = weatherglass_warnings(r->temp, r->wind);
        s->calm = weatherglass_calm(r->temp, r->wind);
        int64_t today = weatherglass_day(now);
        for (uint32_t i = 0; i < w->n; i++) {
            int64_t d = weatherglass_day(w->r[i].stamp) - today;
            if (d >= 0 && d < 7) week[s->week_n++] = w->r[i].temp;
        }
        if (s->week_n && weatherglass_week(week, s->week_n, s->week)) s->week_n = 0;
        ok = put(o, cap, &at, w->from_cache ? "Offline, the forecast last read: " : "Now: ") &&
             put_fixed(o, cap, &at, r->temp, 10, 1) && put(o, cap, &at, " C (") &&
             put_fixed(o, cap, &at, s->temp_f, 10, 1) && put(o, cap, &at, " F), wind ") &&
             put_fixed(o, cap, &at, r->wind, 10, 1) && put(o, cap, &at, " m/s (") &&
             put_fixed(o, cap, &at, s->wind_kmh, 10, 1) && put(o, cap, &at, " km/h), rain ") &&
             put_fixed(o, cap, &at, r->rain, 10, 1) && put(o, cap, &at, " mm (") &&
             put_fixed(o, cap, &at, s->rain_in, 100, 2) && put(o, cap, &at, " in).");
        if (ok && s->week_n)
            ok = put(o, cap, &at, " This week: low ") &&
                 put_fixed(o, cap, &at, s->week[0], 10, 1) && put(o, cap, &at, " C, high ") &&
                 put_fixed(o, cap, &at, s->week[1], 10, 1) && put(o, cap, &at, " C, mean ") &&
                 put_fixed(o, cap, &at, s->week[2], 10, 1) && put(o, cap, &at, " C.");
        if (ok && s->flags > 0) {
            const char *sep = " Advisory: ";
            for (uint32_t k = 0; k < 3u && ok; k++)
                if (s->flags & (1 << k)) {
                    ok = put(o, cap, &at, sep) && put(o, cap, &at, FLAG_TEXT[k]);
                    sep = "; ";
                }
            ok = ok && put(o, cap, &at,
                           ". This is Weatherglass's own reading of the forecast, not an "
                           "official warning; official warnings always take precedence.");
        }
    }
    if (ok && w->alerts_total) {
        ok = put(o, cap, &at, at ? " " : "") && put_num(o, cap, &at, w->alerts_total) &&
             put(o, cap, &at, w->alerts_total == 1 ? " alert" : " alerts") &&
             put(o, cap, &at, " from the National Weather Service:");
        for (uint32_t i = 0; i < w->nalerts && ok; i++)
            ok = put(o, cap, &at, i ? "; " : " ") && put(o, cap, &at, w->alert[i].event) &&
                 put(o, cap, &at, " (") && put(o, cap, &at, w->alert[i].severity) &&
                 put(o, cap, &at, ")");
        ok = ok && put(o, cap, &at, ". Follow the official warnings.");
    }
    /* a part that does not fit is left out whole: put writes all of it or nothing */
    (void) ok;
}
