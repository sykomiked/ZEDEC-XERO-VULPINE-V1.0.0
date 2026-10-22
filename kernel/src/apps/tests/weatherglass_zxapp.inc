/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Weatherglass's languages, witnessed, row by row against the rules of
 * weatherglass_model.c (written out here, since this file cannot include
 * the model's header: it reaches zxm.h). Slots: @num.weatherglass_temp_f_tenths,
 * @num.weatherglass_rain_in_x100, @num.weatherglass_wind_kmh_x10,
 * @num.weatherglass_warn_flags, @logic.weatherglass_weather_calm,
 * @num.weatherglass_week_temps, @logic.weatherglass_forecast_ok. */

static int64_t wg_gcd(int64_t a, int64_t b)
{
    if (a < 0) a = -a;
    while (b) {
        int64_t t = a % b;
        a = b;
        b = t;
    }
    return a ? a : 1;
}

/* weatherglass_warnings: 1 frost, 2 heat, 4 wind; -1 outside the domain */
static int64_t wg_warnings(int64_t temp, int64_t wind)
{
    if (temp < -1000 || temp > 1000 || wind < 0 || wind > 2000) return -1;
    return (temp <= 0 ? 1 : 0) + (temp >= 350 ? 2 : 0) + (wind >= 172 ? 4 : 0);
}

static bool wg_calm(int64_t temp, int64_t wind) /* weatherglass_calm */
{
    return temp >= 1 && temp <= 349 && wind >= 0 && wind <= 171;
}

static bool wg_reading_ok(int64_t stamp, int64_t temp, int64_t wind, int64_t rain)
{
    /* weatherglass_reading_ok: the digits of ^ZXWEATHER, a sign aside */
    return stamp >= -99999999999 && stamp <= 99999999999 && temp >= -9999 && temp <= 9999 &&
           wind >= -9999 && wind <= 9999 && rain >= -99999 && rain <= 99999;
}

/* One witnessed call of a conversion: every row the reduced num * mul / div
 * (+ add), as an exact rational, and the two languages agree (TRUE). */
static bool wg_exact(zxapp_t *a, const char *entry, const char *arg, const int64_t *x,
                     uint32_t rows, int64_t mul, int64_t div, int64_t add)
{
    zxapp_arg_t xa = {arg, x, rows};
    zxapp_num_t o[WAVE_ROWS];
    uint32_t k = 0;
    int32_t lp = -1;
    int rc = zxapp_call_witnessed(a, &H, entry, &xa, 1, o, WAVE_ROWS, &k, &lp);
    if (rc != ZXAPP_OK || lp != ZXAPP_TRUE || k != rows) {
        printf("    %s: rc %d lp %d n %u (%s)\n", entry, rc, (int) lp, k, a->err);
        return false;
    }
    for (uint32_t i = 0; i < rows; i++) {
        int64_t n = x[i] * mul + add * div, g = wg_gcd(n, div);
        if (o[i].num != n / g || o[i].den != div / g) {
            printf("    %s row %u: %lld/%lld, want %lld/%lld\n", entry, i, (long long) o[i].num,
                   (long long) o[i].den, (long long) (n / g), (long long) (div / g));
            return false;
        }
    }
    return true;
}

APP_TEST(weatherglass, 25, 88)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], want[WAVE_ROWS];

    /* temp_f_tenths: celsius * 9 / 5 + 320, exact, Trebago = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(-1000, 1000);
    a[0] = -1000, a[1] = 1000, a[2] = 0, a[3] = 1, a[4] = -1, a[5] = -400;
    CHECK(wg_exact(app, "temp_f_tenths", "celsius", a, WAVE_ROWS, 9, 5, 320),
          "Weatherglass: temp_f_tenths, Trebago = KRON = C");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = 5 * wrand(-200, 200); /* whole tenths of a degree F */
        want[i] = a[i] * 9 / 5 + 320;
    }
    zxapp_arg_t ca = {"celsius", a, WAVE_ROWS};
    CHECK(wave_same(app, "temp_f_tenths", &ca, 1, want, WAVE_ROWS, false),
          "Weatherglass: temp_f_tenths in whole tenths");
    {
        /* max + 1 and min - 1: Trebago refuses, KRON answers */
        static const int64_t up[2] = {0, 1001}, down[2] = {0, -1001};
        zxapp_arg_t ua = {"celsius", up, 2}, da = {"celsius", down, 2};
        CHECK(wave_lp(app, "temp_f_tenths", &ua, 1) == ZXAPP_BOTH,
              "Weatherglass: 100.1 degrees: Trebago refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "temp_f_tenths", &da, 1) == ZXAPP_BOTH,
              "Weatherglass: -100.1 degrees: BOTH");
    }

    /* rain_in_x100: rain * 50 / 127, exact, Trebago = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(0, 99999);
    a[0] = 0, a[1] = 99999, a[2] = 1, a[3] = 127, a[4] = 254;
    CHECK(wg_exact(app, "rain_in_x100", "rain", a, WAVE_ROWS, 50, 127, 0),
          "Weatherglass: rain_in_x100, Trebago = KRON = C");
    {
        static const int64_t neg[2] = {0, -1}, big[2] = {0, 100000};
        zxapp_arg_t na = {"rain", neg, 2}, ba = {"rain", big, 2};
        CHECK(wave_lp(app, "rain_in_x100", &na, 1) == ZXAPP_BOTH,
              "Weatherglass: negative rain: Trebago refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "rain_in_x100", &ba, 1) == ZXAPP_BOTH,
              "Weatherglass: rain past 9999.9 mm: BOTH");
    }

    /* wind_kmh_x10: wind * 18 / 5, exact, Trebago = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(0, 2000);
    a[0] = 0, a[1] = 2000, a[2] = 1, a[3] = 172, a[4] = 171;
    CHECK(wg_exact(app, "wind_kmh_x10", "wind", a, WAVE_ROWS, 18, 5, 0),
          "Weatherglass: wind_kmh_x10, Trebago = KRON = C");
    {
        static const int64_t neg[2] = {0, -1}, big[2] = {0, 2001};
        zxapp_arg_t na = {"wind", neg, 2}, ba = {"wind", big, 2};
        CHECK(wave_lp(app, "wind_kmh_x10", &na, 1) == ZXAPP_BOTH,
              "Weatherglass: negative wind: Trebago refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "wind_kmh_x10", &ba, 1) == ZXAPP_BOTH,
              "Weatherglass: wind past 200 m/s: BOTH");
    }

    /* warn_flags: the flags, Trebago = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1000, 1000);
        b[i] = wrand(0, 2000);
    }
    a[0] = 0, b[0] = 0;        /* frost at 0.0 */
    a[1] = 1, b[1] = 0;        /* one tenth above: none */
    a[2] = 349, b[2] = 171;    /* both just below */
    a[3] = 350, b[3] = 172;    /* heat and a gale */
    a[4] = -1000, b[4] = 2000; /* the domain's corners */
    a[5] = 1000, b[5] = 0;
    a[6] = -5, b[6] = 300; /* frost and a gale */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = wg_warnings(a[i], b[i]);
    zxapp_arg_t wa[2] = {{"temp", a, WAVE_ROWS}, {"wind", b, WAVE_ROWS}};
    CHECK(wave_same(app, "warn_flags", wa, 2, want, WAVE_ROWS, false),
          "Weatherglass: warn_flags, Trebago = KRON = C");
    {
        static const int64_t t[2] = {0, 0}, w[2] = {0, -1}, hot[2] = {0, 1001}, z[2] = {0, 0};
        zxapp_arg_t na[2] = {{"temp", t, 2}, {"wind", w, 2}};
        zxapp_arg_t ha[2] = {{"temp", hot, 2}, {"wind", z, 2}};
        CHECK(wave_lp(app, "warn_flags", na, 2) == ZXAPP_BOTH,
              "Weatherglass: a negative wind: Trebago refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "warn_flags", ha, 2) == ZXAPP_BOTH, "Weatherglass: 100.1 degrees: BOTH");
    }

    /* weather_calm: no warning, Eve = KRON = C, and TRUE exactly when the
     * flags are 0 (the same rule as a truth) */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1000, 1000);
        b[i] = wrand(0, 400);
    }
    a[0] = 1, b[0] = 0;        /* every low end */
    a[1] = 349, b[1] = 171;    /* every high end */
    a[2] = 0, b[2] = 0;        /* frost */
    a[3] = 350, b[3] = 0;      /* heat */
    a[4] = 200, b[4] = 172;    /* a gale */
    a[5] = -1000, b[5] = 2000; /* everything */
    a[6] = 200, b[6] = 50;
    uint32_t same = 0;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        want[i] = T_(wg_calm(a[i], b[i]));
        same += wg_calm(a[i], b[i]) == (wg_warnings(a[i], b[i]) == 0);
    }
    CHECK(same == WAVE_ROWS, "Weatherglass: calm is exactly no flag (%u of %u)", same,
          (unsigned) WAVE_ROWS);
    zxapp_arg_t ma[2] = {{"temp", a, WAVE_ROWS}, {"wind", b, WAVE_ROWS}};
    CHECK(wave_same(app, "weather_calm", ma, 2, want, WAVE_ROWS, true),
          "Weatherglass: weather_calm, Eve = KRON = C");
    {
        /* a negative wind is below a gale for KRON, outside 0 .. 171 for Eve */
        static const int64_t t[2] = {200, 200}, w[2] = {0, -1};
        zxapp_arg_t na[2] = {{"temp", t, 2}, {"wind", w, 2}};
        CHECK(wave_lp(app, "weather_calm", na, 2) == ZXAPP_BOTH,
              "Weatherglass: a negative wind: Eve FALSE, KRON TRUE: BOTH");
    }

    /* week_temps: 64 weeks, lowest, highest and floored mean (Trebago alone) */
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t t[7], s = 0, lo = 1000, hi = -1000, want3[3];
        uint32_t n = r < 8 ? r % 7 + 1 : 7;
        for (uint32_t i = 0; i < n; i++) {
            t[i] = r == 0 ? -1000 : r == 1 ? 1000 : wrand(-1000, 1000);
            s += t[i];
            lo = t[i] < lo ? t[i] : lo;
            hi = t[i] > hi ? t[i] : hi;
        }
        int64_t q = s / (int64_t) n;
        if (s % (int64_t) n != 0 && s < 0) q -= 1; /* toward minus infinity */
        want3[0] = lo, want3[1] = hi, want3[2] = q;
        zxapp_arg_t ta = {"temp", t, n};
        CHECK(wave_same(app, "week_temps", &ta, 1, want3, 3, false),
              "Weatherglass: week %u (%u readings), Trebago = C", r, n);
    }
    {
        /* a reading past the domain is refused, never answered */
        static const int64_t over[3] = {10, 1001, 20};
        zxapp_arg_t oa = {"temp", over, 3};
        CHECK(wave_lp(app, "week_temps", &oa, 1) == ZXAPP_NEITHER,
              "Weatherglass: a reading of 100.1 degrees: refused: NEITHER");
    }

    /* forecast_ok: a cached reading fits ^ZXWEATHER, MYRA = C */
    static int64_t s[WAVE_ROWS], w[WAVE_ROWS], rn[WAVE_ROWS];
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        s[i] = wrand(0, 99999999999);
        a[i] = wrand(-1000, 1000);
        w[i] = wrand(0, 2000);
        rn[i] = wrand(0, 99999);
    }
    s[0] = 1792627200, a[0] = -35, w[0] = 41, rn[0] = 2;         /* a reading */
    s[1] = 99999999999, a[1] = 9999, w[1] = 9999, rn[1] = 99999; /* every max */
    s[2] = 100000000000, a[2] = 0, w[2] = 0, rn[2] = 0;          /* stamp max + 1 */
    s[3] = 0, a[3] = 10000, w[3] = 0, rn[3] = 0;                 /* temp max + 1 */
    s[4] = 0, a[4] = 0, w[4] = 10000, rn[4] = 0;                 /* wind max + 1 */
    s[5] = 0, a[5] = 0, w[5] = 0, rn[5] = 100000;                /* rain max + 1 */
    s[6] = 0, a[6] = -9999, w[6] = 0, rn[6] = 0;                 /* negative, a sign aside */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(wg_reading_ok(s[i], a[i], w[i], rn[i]));
    zxapp_arg_t ra[4] = {{"stamp", s, WAVE_ROWS},
                         {"temp", a, WAVE_ROWS},
                         {"wind", w, WAVE_ROWS},
                         {"rain", rn, WAVE_ROWS}};
    CHECK(wave_same(app, "forecast_ok", ra, 4, want, WAVE_ROWS, true),
          "Weatherglass: forecast_ok, MYRA = C");
}
