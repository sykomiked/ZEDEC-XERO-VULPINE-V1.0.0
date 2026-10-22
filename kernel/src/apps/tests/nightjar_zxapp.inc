/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Nightjar's languages against the C its model runs (nightjar_rules.h). */
#include "nightjar_rules.h"

APP_TEST(nightjar, 5, 133)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], want[WAVE_ROWS];

    /* speech_cuts_in (NR1): Eve = KRON = C over the kinds and above them */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = i % 4 ? wrand(1, 3) : wrand(1, 1000000);
    a[0] = 1, a[1] = 2, a[2] = 3, a[3] = 4; /* alert, focus, page, max + 1 */
    a[4] = INT64_MAX;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(nightjar_cuts_in(a[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE,
          "Nightjar: the C rule's own edges (NR1)");
    zxapp_arg_t ka = {"kind", a, WAVE_ROWS};
    CHECK(wave_same(app, "speech_cuts_in", &ka, 1, want, WAVE_ROWS, true),
          "Nightjar: interrupt now, Eve = KRON = C");
    {
        static const int64_t zero[2] = {1, 0}, neg[2] = {2, -1}, low[2] = {1, INT64_MIN};
        zxapp_arg_t za = {"kind", zero, 2}, na = {"kind", neg, 2}, la = {"kind", low, 2};
        CHECK(wave_lp(app, "speech_cuts_in", &za, 1) == ZXAPP_BOTH &&
                  wave_lp(app, "speech_cuts_in", &na, 1) == ZXAPP_BOTH &&
                  wave_lp(app, "speech_cuts_in", &la, 1) == ZXAPP_BOTH,
              "Nightjar: a kind that is no kind: Eve refuses, KRON cuts in: BOTH, held");
    }

    /* speech_voice_ok (NR2): Eve = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 600);
        b[i] = wrand(0, 300);
    }
    a[0] = 80, b[0] = 50;   /* both lowest */
    a[1] = 450, b[1] = 200; /* both highest */
    a[2] = 79, b[2] = 100;
    a[3] = 451, b[3] = 100;
    a[4] = 180, b[4] = 49;
    a[5] = 180, b[5] = 201;
    a[6] = 0, b[6] = 0;
    a[7] = -180, b[7] = -100;
    a[8] = INT64_MAX, b[8] = 100;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(nightjar_voice_ok(a[i], b[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE && want[8] == ZXAPP_FALSE,
          "Nightjar: the C rule's own edges (NR2)");
    zxapp_arg_t va[2] = {{"wpm", a, WAVE_ROWS}, {"pitch", b, WAVE_ROWS}};
    CHECK(wave_same(app, "speech_voice_ok", va, 2, want, WAVE_ROWS, true),
          "Nightjar: speech limits, Eve = C");

    /* a11y_element_ok (NR3): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1000, 1000);
        b[i] = wrand(-10, 10);
        c[i] = wrand(-100, 100);
        d[i] = wrand(-10000, 10000);
        e[i] = wrand(-10000, 10000);
        f[i] = wrand(-10, 10);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0, f[0] = 0;
    a[1] = 999, b[1] = 9, c[1] = 99, d[1] = 9999, e[1] = 9999, f[1] = 9;
    a[2] = 1000, b[2] = 9, c[2] = 99, d[2] = 9999, e[2] = 9999, f[2] = 9;
    a[3] = 999, b[3] = 10, c[3] = 99, d[3] = 9999, e[3] = 9999, f[3] = 9;
    a[4] = 999, b[4] = 9, c[4] = 100, d[4] = 9999, e[4] = 9999, f[4] = 9;
    a[5] = 999, b[5] = 9, c[5] = 99, d[5] = 10000, e[5] = 9999, f[5] = 9;
    a[6] = 999, b[6] = 9, c[6] = 99, d[6] = 9999, e[6] = 10000, f[6] = 9;
    a[7] = 999, b[7] = 9, c[7] = 99, d[7] = 9999, e[7] = 9999, f[7] = 10;
    a[8] = -999, b[8] = -9, c[8] = -99, d[8] = -9999, e[8] = -9999, f[8] = -9;
    a[9] = 12, b[9] = 2, c[9] = 4, d[9] = 3, e[9] = 40, f[9] = 4; /* "Save", a button, checked */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(nightjar_element_fits(a[i], b[i], c[i], d[i], e[i], f[i]));
    CHECK(want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE && want[3] == ZXAPP_FALSE &&
              want[7] == ZXAPP_FALSE && want[8] == ZXAPP_TRUE && want[9] == ZXAPP_TRUE,
          "Nightjar: the C rule's own edges (NR3)");
    zxapp_arg_t ea[6] = {{"element", a, WAVE_ROWS}, {"role", b, WAVE_ROWS},
                         {"namelen", c, WAVE_ROWS}, {"row", d, WAVE_ROWS},
                         {"column", e, WAVE_ROWS},  {"state", f, WAVE_ROWS}};
    CHECK(wave_same(app, "a11y_element_ok", ea, 6, want, WAVE_ROWS, true),
          "Nightjar: an element makes a ^ZXNIGHTJAR record, MYRA = C");

    /* reading_place (NR4): KRON = C, rows and columns on a small grid so
     * many share a row, and some a spot */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 7);
        b[i] = i % 3 ? wrand(0, 7) : wrand(0, 999999);
    }
    a[0] = 0, b[0] = 0;           /* the first spot */
    a[1] = 999999, b[1] = 999999; /* the last spot */
    a[2] = 0, b[2] = 999999;      /* the end of the first row, before the second */
    a[3] = 1, b[3] = 0;
    a[4] = 3, b[4] = 3, a[5] = 3, b[5] = 3; /* two at one spot share a place */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = nightjar_reading_place(a, b, WAVE_ROWS, i);
    CHECK(want[0] == 1 && want[1] == WAVE_ROWS && want[4] == want[5] && want[2] < want[3],
          "Nightjar: the C rule's own edges (NR4)");
    zxapp_arg_t ra[2] = {{"row", a, WAVE_ROWS}, {"column", b, WAVE_ROWS}};
    CHECK(wave_same(app, "reading_place", ra, 2, want, WAVE_ROWS, false),
          "Nightjar: reading order, KRON = C");
}
