/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Dyer's languages against the C its model runs (dyer_rules.h).
 * Slots: @num.dyer_rgb_to_hsl, @num.dyer_hsl_to_rgb,
 * @num.dyer_rel_luminance, @num.dyer_contrast_ratio_x100,
 * @num.dyer_contrast_grade, @logic.dyer_swatch_ok. */
#include "dyer_rules.h"

APP_TEST(dyer, 12, 116)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], want[WAVE_ROWS];

    /* rgb_to_hsl (DY1): KRON = Trebago = C, every branch and edge */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, DYER_CHANNEL_MAX);
        b[i] = wrand(0, DYER_CHANNEL_MAX);
        c[i] = wrand(0, DYER_CHANNEL_MAX);
        d[i] = wrand(DYER_HUE, DYER_LIGHTNESS);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = DYER_LIGHTNESS;            /* black: 0 */
    a[1] = 255, b[1] = 255, c[1] = 255, d[1] = DYER_LIGHTNESS;      /* white: 1000 */
    a[2] = 255, b[2] = 255, c[2] = 255, d[2] = DYER_SATURATION;     /* a grey has none */
    a[3] = 128, b[3] = 128, c[3] = 128, d[3] = DYER_HUE;            /* nor any hue */
    a[4] = 255, b[4] = 0, c[4] = 0, d[4] = DYER_HUE;                /* red: 0 */
    a[5] = 0, b[5] = 255, c[5] = 0, d[5] = DYER_HUE;                /* green: 120 */
    a[6] = 0, b[6] = 0, c[6] = 255, d[6] = DYER_HUE;                /* blue: 240 */
    a[7] = 255, b[7] = 0, c[7] = 1, d[7] = DYER_HUE;                /* just under 360: 0 */
    a[8] = 255, b[8] = 255, c[8] = 0, d[8] = DYER_HUE;              /* red and green tie: 60 */
    a[9] = 0, b[9] = 255, c[9] = 255, d[9] = DYER_HUE;              /* green and blue tie: 180 */
    a[10] = 255, b[10] = 0, c[10] = 0, d[10] = DYER_SATURATION;     /* full: 1000 */
    a[11] = 1, b[11] = 0, c[11] = 0, d[11] = DYER_SATURATION;       /* darkest red: 1000 */
    a[12] = 255, b[12] = 254, c[12] = 254, d[12] = DYER_SATURATION; /* lightest tint */
    a[13] = 10, b[13] = 20, c[13] = 30, d[13] = DYER_HUE;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = dyer_hsl_part(a[i], b[i], c[i], d[i]);
    CHECK(want[0] == 0 && want[1] == 1000 && want[2] == 0 && want[3] == 0 && want[4] == 0 &&
              want[5] == 120 && want[6] == 240 && want[7] == 0 && want[8] == 60 && want[9] == 180 &&
              want[10] == 1000 && want[11] == 1000 && want[12] == 1000 && want[13] == 210,
          "Dyer: rgb_to_hsl's C at its edges");
    zxapp_arg_t ha[4] = {
        {"r", a, WAVE_ROWS}, {"g", b, WAVE_ROWS}, {"b", c, WAVE_ROWS}, {"part", d, WAVE_ROWS}};
    CHECK(wave_same(app, "rgb_to_hsl", ha, 4, want, WAVE_ROWS, false),
          "Dyer: rgb_to_hsl, KRON = Trebago = C");
    {
        static const int64_t r[2] = {10, 256}, g[2] = {20, 0}, bl[2] = {30, 0}, p[2] = {1, 1};
        zxapp_arg_t xa[4] = {{"r", r, 2}, {"g", g, 2}, {"b", bl, 2}, {"part", p, 2}};
        CHECK(wave_lp(app, "rgb_to_hsl", xa, 4) == ZXAPP_BOTH,
              "Dyer: a channel of 256: Trebago refuses, KRON answers: BOTH");
        static const int64_t n[2] = {-1, 10}, p2[2] = {1, 0};
        zxapp_arg_t ya[4] = {{"r", n, 2}, {"g", g, 2}, {"b", bl, 2}, {"part", p, 2}};
        zxapp_arg_t za[4] = {{"r", r, 2}, {"g", g, 2}, {"b", bl, 2}, {"part", p2, 2}};
        CHECK(wave_lp(app, "rgb_to_hsl", ya, 4) == ZXAPP_BOTH &&
                  wave_lp(app, "rgb_to_hsl", za, 4) == ZXAPP_BOTH,
              "Dyer: a negative channel, a part of 0: held");
        CHECK(dyer_hsl_part(256, 0, 0, 1) == DYER_REFUSED &&
                  dyer_hsl_part(0, -1, 0, 1) == DYER_REFUSED &&
                  dyer_hsl_part(0, 0, 0, 4) == DYER_REFUSED,
              "Dyer: the C refuses them");
    }

    /* hsl_to_rgb (DY2): KRON = Trebago = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, DYER_HUE_MAX);
        b[i] = wrand(0, DYER_UNIT);
        c[i] = wrand(0, DYER_UNIT);
        d[i] = wrand(DYER_RED, DYER_BLUE);
    }
    a[0] = 0, b[0] = 1000, c[0] = 500, d[0] = DYER_RED;      /* pure red: 255 */
    a[1] = 0, b[1] = 1000, c[1] = 500, d[1] = DYER_GREEN;    /* none */
    a[2] = 120, b[2] = 1000, c[2] = 500, d[2] = DYER_GREEN;  /* pure green */
    a[3] = 240, b[3] = 1000, c[3] = 500, d[3] = DYER_BLUE;   /* pure blue */
    a[4] = 359, b[4] = 1000, c[4] = 500, d[4] = DYER_BLUE;   /* the last hue */
    a[5] = 0, b[5] = 0, c[5] = 0, d[5] = DYER_RED;           /* black */
    a[6] = 0, b[6] = 0, c[6] = 1000, d[6] = DYER_BLUE;       /* white */
    a[7] = 359, b[7] = 1000, c[7] = 1000, d[7] = DYER_GREEN; /* white at any hue */
    a[8] = 210, b[8] = 500, c[8] = 500, d[8] = DYER_BLUE;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = dyer_rgb_part(a[i], b[i], c[i], d[i]);
    CHECK(want[0] == 255 && want[1] == 0 && want[2] == 255 && want[3] == 255 && want[4] == 4 &&
              want[5] == 0 && want[6] == 255 && want[7] == 255 && want[8] == 191,
          "Dyer: hsl_to_rgb's C at its edges");
    zxapp_arg_t ra[4] = {
        {"h", a, WAVE_ROWS}, {"s", b, WAVE_ROWS}, {"l", c, WAVE_ROWS}, {"part", d, WAVE_ROWS}};
    CHECK(wave_same(app, "hsl_to_rgb", ra, 4, want, WAVE_ROWS, false),
          "Dyer: hsl_to_rgb, KRON = Trebago = C");
    {
        static const int64_t h[2] = {0, 360}, s[2] = {500, 500}, l[2] = {500, 500}, p[2] = {1, 1},
                             s2[2] = {500, 1001}, h2[2] = {0, -1};
        zxapp_arg_t xa[4] = {{"h", h, 2}, {"s", s, 2}, {"l", l, 2}, {"part", p, 2}};
        zxapp_arg_t ya[4] = {{"h", h2, 2}, {"s", s2, 2}, {"l", l, 2}, {"part", p, 2}};
        CHECK(wave_lp(app, "hsl_to_rgb", xa, 4) == ZXAPP_BOTH &&
                  wave_lp(app, "hsl_to_rgb", ya, 4) == ZXAPP_BOTH,
              "Dyer: a hue of 360, a saturation of 1001: Trebago refuses, KRON answers: BOTH");
        CHECK(dyer_rgb_part(360, 0, 0, 1) == DYER_REFUSED &&
                  dyer_rgb_part(0, 1001, 0, 1) == DYER_REFUSED &&
                  dyer_rgb_part(0, 0, -1, 1) == DYER_REFUSED &&
                  dyer_rgb_part(0, 0, 0, 0) == DYER_REFUSED,
              "Dyer: the C refuses them");
    }

    /* rel_luminance (DY3): KRON = Trebago = C, over one table */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, DYER_CHANNEL_MAX);
        b[i] = wrand(0, DYER_CHANNEL_MAX);
        c[i] = wrand(0, DYER_CHANNEL_MAX);
    }
    a[0] = 0, b[0] = 0, c[0] = 0;       /* black: 0 */
    a[1] = 255, b[1] = 255, c[1] = 255; /* white: 1000000 */
    a[2] = 10, b[2] = 10, c[2] = 10;    /* the last value below the knee */
    a[3] = 11, b[3] = 11, c[3] = 11;    /* the first above it */
    a[4] = 255, b[4] = 0, c[4] = 0;
    a[5] = 0, b[5] = 255, c[5] = 0;
    a[6] = 0, b[6] = 0, c[6] = 255;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = dyer_luminance(a[i], b[i], c[i]);
    CHECK(want[0] == 0 && want[1] == DYER_LUM_MAX && want[2] == 3035 && want[3] == 3347 &&
              want[4] == 212600 && want[5] == 715200 && want[6] == 72200,
          "Dyer: rel_luminance's C: %lld %lld", (long long) want[2], (long long) want[3]);
    zxapp_arg_t la[3] = {{"r", a, WAVE_ROWS}, {"g", b, WAVE_ROWS}, {"b", c, WAVE_ROWS}};
    CHECK(wave_same(app, "rel_luminance", la, 3, want, WAVE_ROWS, false),
          "Dyer: rel_luminance, KRON = Trebago = C");
    {
        /* both refuse a channel past the table: no answer, not held */
        static const int64_t r[2] = {10, 256}, g[2] = {0, 0}, bl[2] = {0, 0};
        zxapp_arg_t xa[3] = {{"r", r, 2}, {"g", g, 2}, {"b", bl, 2}};
        CHECK(wave_lp(app, "rel_luminance", xa, 3) == ZXAPP_NEITHER &&
                  dyer_luminance(256, 0, 0) == DYER_REFUSED &&
                  dyer_luminance(0, 0, -1) == DYER_REFUSED,
              "Dyer: a channel of 256: KRON and Trebago both refuse");
    }

    /* contrast_ratio_x100 (DY4): KRON = Trebago = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, DYER_LUM_MAX);
        b[i] = wrand(0, DYER_LUM_MAX);
    }
    a[0] = DYER_LUM_MAX, b[0] = 0; /* white on black: 2100 */
    a[1] = 0, b[1] = DYER_LUM_MAX; /* either order */
    a[2] = 0, b[2] = 0;            /* one colour: 100 */
    a[3] = DYER_LUM_MAX, b[3] = DYER_LUM_MAX;
    a[4] = DYER_LUM_MAX, b[4] = 181164; /* #767676 on white: 454 */
    a[5] = DYER_LUM_MAX, b[5] = 184475; /* #777777 on white: 447 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = dyer_contrast_x100(a[i], b[i]);
    CHECK(want[0] == 2100 && want[1] == 2100 && want[2] == 100 && want[3] == 100 &&
              want[4] == 454 && want[5] == 447,
          "Dyer: contrast_ratio_x100's C at its edges");
    zxapp_arg_t ca[2] = {{"fore", a, WAVE_ROWS}, {"back", b, WAVE_ROWS}};
    CHECK(wave_same(app, "contrast_ratio_x100", ca, 2, want, WAVE_ROWS, false),
          "Dyer: contrast_ratio_x100, KRON = Trebago = C");
    {
        static const int64_t f[2] = {5, 1000001}, k[2] = {6, 0}, f2[2] = {5, -1};
        zxapp_arg_t xa[2] = {{"fore", f, 2}, {"back", k, 2}};
        zxapp_arg_t ya[2] = {{"fore", f2, 2}, {"back", k, 2}};
        CHECK(wave_lp(app, "contrast_ratio_x100", xa, 2) == ZXAPP_BOTH &&
                  wave_lp(app, "contrast_ratio_x100", ya, 2) == ZXAPP_BOTH &&
                  dyer_contrast_x100(1000001, 0) == DYER_REFUSED &&
                  dyer_contrast_x100(0, -1) == DYER_REFUSED,
              "Dyer: a luminance past white or below black: Trebago refuses, KRON answers: BOTH");
    }

    /* contrast_grade (DY5): KRON = Trebago = C, both marks */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(DYER_RATIO_MIN, DYER_RATIO_MAX);
    a[0] = 100, a[1] = 299, a[2] = 300, a[3] = 449, a[4] = 450, a[5] = 2100;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = dyer_contrast_grade(a[i]);
    CHECK(want[0] == 0 && want[1] == 0 && want[2] == 1 && want[3] == 1 && want[4] == 2 &&
              want[5] == 2,
          "Dyer: contrast_grade's C at the marks");
    zxapp_arg_t ga[1] = {{"ratio", a, WAVE_ROWS}};
    CHECK(wave_same(app, "contrast_grade", ga, 1, want, WAVE_ROWS, false),
          "Dyer: contrast_grade, KRON = Trebago = C");
    {
        static const int64_t lo[2] = {450, 99}, hi[2] = {450, 2101};
        zxapp_arg_t xa[1] = {{"ratio", lo, 2}}, ya[1] = {{"ratio", hi, 2}};
        CHECK(wave_lp(app, "contrast_grade", xa, 1) == ZXAPP_BOTH &&
                  wave_lp(app, "contrast_grade", ya, 1) == ZXAPP_BOTH &&
                  dyer_contrast_grade(99) == DYER_REFUSED &&
                  dyer_contrast_grade(2101) == DYER_REFUSED,
              "Dyer: a ratio no two colours make: Trebago refuses, KRON answers: BOTH");
    }

    /* swatch_ok (DY6): Eve = MYRA = C inside the record's digits */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, DYER_PLACES - 1);
        b[i] = wrand(0, DYER_COLOR_MAX);
    }
    a[0] = 0, b[0] = 0;                            /* the first place, black */
    a[1] = DYER_PLACES - 1, b[1] = DYER_COLOR_MAX; /* the last place, white */
    a[2] = 100, b[2] = 0;                          /* past both: false */
    a[3] = 0, b[3] = 100000000;                    /* past both */
    a[4] = -100, b[4] = 5;                         /* past both, negative */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(dyer_swatch_ok(a[i], b[i]));
    zxapp_arg_t sa[2] = {{"place", a, WAVE_ROWS}, {"color", b, WAVE_ROWS}};
    CHECK(wave_same(app, "swatch_ok", sa, 2, want, WAVE_ROWS, true),
          "Dyer: swatch_ok, Eve = MYRA = C");
    {
        /* past a range but inside the record's digits: MYRA true, Eve false */
        static const int64_t p[2] = {1, 64}, k[2] = {0, 0}, p1[2] = {1, 1}, k2[2] = {0, 16777216},
                             pn[2] = {1, -1};
        zxapp_arg_t xa[2] = {{"place", p, 2}, {"color", k, 2}};
        zxapp_arg_t ya[2] = {{"place", p1, 2}, {"color", k2, 2}};
        zxapp_arg_t za[2] = {{"place", pn, 2}, {"color", k, 2}};
        CHECK(wave_lp(app, "swatch_ok", xa, 2) == ZXAPP_BOTH &&
                  wave_lp(app, "swatch_ok", ya, 2) == ZXAPP_BOTH &&
                  wave_lp(app, "swatch_ok", za, 2) == ZXAPP_BOTH,
              "Dyer: place 64, colour 0x1000000, place -1: MYRA fits, Eve refuses: BOTH");
        CHECK(!dyer_swatch_ok(64, 0) && dyer_record_fits(64, 0) && !dyer_swatch_ok(1, 16777216) &&
                  dyer_record_fits(1, 16777216) && !dyer_record_fits(100, 0) &&
                  !dyer_record_fits(0, 100000000),
              "Dyer: the ranges sit inside the record's digits");
    }
}
