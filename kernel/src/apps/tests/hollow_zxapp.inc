/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Hollow's languages, witnessed, row by row against the C its model runs
 * (hollow_rules.h HR1-HR6). Slots: @num.hollow_tile_cols,
 * @num.hollow_tile_rows, @num.hollow_tile_cell, @num.hollow_shown,
 * @logic.hollow_window_ok, @logic.hollow_dock_ok. */
#include "hollow_rules.h"

APP_TEST(hollow, 11, 2)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], want[WAVE_ROWS];

    /* tile_cols and tile_rows (HR1, HR2): every n the grid takes, KRON = C++ = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = i < 64u ? (int64_t) i + 1 : 0;
    a[63] = 0; /* no windows: no grid */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = hollow_tile_cols(a[i]);
    CHECK(want[0] == 1 && want[1] == 2 && want[3] == 2 && want[4] == 3 && want[8] == 3 &&
              want[9] == 4 && want[62] == 8 && want[63] == 0,
          "Hollow: the C columns' own edges (HR1)");
    zxapp_arg_t na = {"windows", a, WAVE_ROWS};
    CHECK(wave_same(app, "tile_cols", &na, 1, want, WAVE_ROWS, false),
          "Hollow: tile_cols, KRON = C++ = C");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = hollow_tile_rows(a[i]);
    CHECK(want[0] == 1 && want[1] == 1 && want[2] == 2 && want[4] == 2 && want[6] == 3 &&
              want[9] == 3 && want[62] == 8 && want[63] == 0,
          "Hollow: the C rows' own edges (HR2)");
    CHECK(wave_same(app, "tile_rows", &na, 1, want, WAVE_ROWS, false),
          "Hollow: tile_rows, KRON = C++ = C");
    {
        /* max + 1 and a negative count: C++ refuses, KRON answers */
        static const int64_t over[2] = {4, 65}, neg[2] = {4, -1};
        zxapp_arg_t oa = {"windows", over, 2}, ga = {"windows", neg, 2};
        CHECK(wave_lp(app, "tile_cols", &oa, 1) == ZXAPP_BOTH,
              "Hollow: 65 windows: C++ refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "tile_cols", &ga, 1) == ZXAPP_BOTH,
              "Hollow: -1 windows: C++ refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "tile_rows", &oa, 1) == ZXAPP_BOTH,
              "Hollow: rows of 65 windows: C++ refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "tile_rows", &ga, 1) == ZXAPP_BOTH,
              "Hollow: rows of -1 windows: C++ refuses, KRON answers: BOTH");
    }

    /* tile_cell (HR3): a side in parts, KRON = C++ = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, HOLLOW_PX_MAX);
        b[i] = wrand(1, HOLLOW_TILE_MAX);
    }
    a[0] = 0, b[0] = 1;                           /* nothing to split */
    a[1] = HOLLOW_PX_MAX, b[1] = 1;               /* max, whole */
    a[2] = HOLLOW_PX_MAX, b[2] = HOLLOW_TILE_MAX; /* max in the most parts */
    a[3] = 1, b[3] = HOLLOW_TILE_MAX;             /* less than a pixel each: 0 */
    a[4] = 1920, b[4] = 7;                        /* 274.28.. floored */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = hollow_tile_cell(a[i], b[i]);
    CHECK(want[1] == HOLLOW_PX_MAX && want[2] == 256 && want[3] == 0 && want[4] == 274,
          "Hollow: the C cell's own edges (HR3)");
    zxapp_arg_t ca[2] = {{"span", a, WAVE_ROWS}, {"parts", b, WAVE_ROWS}};
    CHECK(wave_same(app, "tile_cell", ca, 2, want, WAVE_ROWS, false),
          "Hollow: tile_cell, KRON = C++ = C");
    {
        static const int64_t sp[2] = {1920, HOLLOW_PX_MAX + 1}, pt[2] = {2, 2};
        zxapp_arg_t xa[2] = {{"span", sp, 2}, {"parts", pt, 2}};
        CHECK(wave_lp(app, "tile_cell", xa, 2) == ZXAPP_BOTH,
              "Hollow: a side past 16384: C++ refuses, KRON answers: BOTH");
        static const int64_t many[2] = {2, HOLLOW_TILE_MAX + 1};
        zxapp_arg_t ma[2] = {{"span", sp, 2}, {"parts", many, 2}};
        CHECK(wave_lp(app, "tile_cell", ma, 2) == ZXAPP_BOTH,
              "Hollow: 65 parts: C++ refuses, KRON answers: BOTH");
        static const int64_t zero[2] = {0, 0};
        zxapp_arg_t za[2] = {{"span", sp, 2}, {"parts", zero, 2}};
        CHECK(wave_lp(app, "tile_cell", za, 2) == ZXAPP_NEITHER,
              "Hollow: no parts: both refuse: NEITHER");
    }

    /* shown (HR4): a side against the screen, KRON = Sutra = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        c[i] = wrand(0, 8000);
        a[i] = wrand(-9000, 9000);
        b[i] = wrand(0, 9000);
    }
    a[0] = 0, b[0] = 800, c[0] = 1920;                                    /* inside */
    a[1] = -100, b[1] = 300, c[1] = 1920;                                 /* off the left */
    a[2] = 1900, b[2] = 800, c[2] = 1920;                                 /* off the right */
    a[3] = 1920, b[3] = 100, c[3] = 1920;                                 /* just past it */
    a[4] = -300, b[4] = 300, c[4] = 1920;                                 /* ends at 0 */
    a[5] = 100, b[5] = 0, c[5] = 1920;                                    /* no size */
    a[6] = 0, b[6] = 0, c[6] = 0;                                         /* nothing */
    a[7] = -HOLLOW_PIC_MAX, b[7] = HOLLOW_PIC_MAX, c[7] = HOLLOW_PIC_MAX; /* every max */
    a[8] = HOLLOW_PIC_MAX, b[8] = HOLLOW_PIC_MAX, c[8] = HOLLOW_PIC_MAX;
    a[9] = -50, b[9] = 5000, c[9] = 1920; /* wider than the screen */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = hollow_shown(a[i], b[i], c[i]);
    CHECK(want[0] == 800 && want[1] == 200 && want[2] == 20 && want[3] == 0 && want[4] == 0 &&
              want[5] == 0 && want[7] == 0 && want[8] == 0 && want[9] == 1920,
          "Hollow: the C shown's own edges (HR4)");
    zxapp_arg_t sa[3] = {{"pos", a, WAVE_ROWS}, {"size", b, WAVE_ROWS}, {"span", c, WAVE_ROWS}};
    CHECK(wave_same(app, "shown", sa, 3, want, WAVE_ROWS, false),
          "Hollow: shown, KRON = Sutra = C");
    {
        static const int64_t p[2] = {0, 0}, s[2] = {800, -1}, w[2] = {1920, 1920};
        zxapp_arg_t xa[3] = {{"pos", p, 2}, {"size", s, 2}, {"span", w, 2}};
        CHECK(wave_lp(app, "shown", xa, 3) == ZXAPP_BOTH,
              "Hollow: a negative size: Sutra refuses, KRON answers: BOTH");
        static const int64_t far[2] = {0, HOLLOW_PIC_MAX + 1}, ten[2] = {10, 10};
        zxapp_arg_t fa[3] = {{"pos", far, 2}, {"size", ten, 2}, {"span", w, 2}};
        CHECK(wave_lp(app, "shown", fa, 3) == ZXAPP_BOTH,
              "Hollow: pos past s9(5): Sutra refuses, KRON answers: BOTH");
    }

    /* window_ok (HR5): the limits, Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(100, 17000);
        b[i] = wrand(50, 17000);
        c[i] = wrand(0, 17000);
        d[i] = wrand(0, 17000);
    }
    a[0] = HOLLOW_MIN_W, b[0] = HOLLOW_MIN_H, c[0] = 32, d[0] = 32;   /* every least */
    a[1] = HOLLOW_PX_MAX, b[1] = HOLLOW_PX_MAX, c[1] = HOLLOW_PX_MAX, /* every max */
        d[1] = HOLLOW_PX_MAX;
    a[2] = HOLLOW_MIN_W - 1, b[2] = 600, c[2] = 800, d[2] = 600;  /* too narrow */
    a[3] = 800, b[3] = HOLLOW_MIN_H - 1, c[3] = 800, d[3] = 600;  /* too short */
    a[4] = HOLLOW_PX_MAX + 1, b[4] = 600, c[4] = 800, d[4] = 600; /* max + 1 */
    a[5] = 800, b[5] = 600, c[5] = 31, d[5] = 600;                /* 31 px across */
    a[6] = 800, b[6] = 600, c[6] = 800, d[6] = 31;                /* 31 px down */
    a[7] = 0, b[7] = 0, c[7] = 0, d[7] = 0;                       /* nothing */
    a[8] = -800, b[8] = 600, c[8] = 800, d[8] = 600;              /* negative */
    a[9] = 800, b[9] = 600, c[9] = 800, d[9] = 600;               /* a usual window */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(hollow_window_fits(a[i], b[i], c[i], d[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[5] == ZXAPP_FALSE && want[8] == ZXAPP_FALSE && want[9] == ZXAPP_TRUE,
          "Hollow: the C limits' own edges (HR5)");
    zxapp_arg_t wa[4] = {{"width", a, WAVE_ROWS},
                         {"height", b, WAVE_ROWS},
                         {"across", c, WAVE_ROWS},
                         {"down", d, WAVE_ROWS}};
    CHECK(wave_same(app, "window_ok", wa, 4, want, WAVE_ROWS, true),
          "Hollow: window_ok, Eve = KRON = C");
    {
        /* one width for two windows: Eve shares it (EV1), KRON does not stretch it */
        static const int64_t w1[1] = {800}, h2[2] = {600, 600}, sw2[2] = {800, 800},
                             sh2[2] = {600, 600};
        zxapp_arg_t xa[4] = {
            {"width", w1, 1}, {"height", h2, 2}, {"across", sw2, 2}, {"down", sh2, 2}};
        CHECK(wave_lp(app, "window_ok", xa, 4) == ZXAPP_BOTH,
              "Hollow: one width for two windows: Eve answers, KRON refuses: BOTH");
    }

    /* dock_ok (HR6): a dock place's record, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-120, 120);
        b[i] = wrand(-1200, 1200);
        c[i] = wrand(-12, 12);
        d[i] = wrand(0, 2000000000000);
    }
    a[0] = 99, b[0] = 999, c[0] = 9, d[0] = 999999999999;     /* every max */
    a[1] = 100, b[1] = 1, c[1] = 1, d[1] = 0;                 /* place max + 1 */
    a[2] = 0, b[2] = 1000, c[2] = 1, d[2] = 0;                /* app max + 1 */
    a[3] = 0, b[3] = 1, c[3] = 10, d[3] = 0;                  /* pinned max + 1 */
    a[4] = 0, b[4] = 1, c[4] = 1, d[4] = 1000000000000;       /* added max + 1 */
    a[5] = -99, b[5] = -999, c[5] = -9, d[5] = -999999999999; /* sign aside */
    a[6] = 0, b[6] = 0, c[6] = 0, d[6] = 0;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(hollow_dock_fits(a[i], b[i], c[i], d[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE &&
              want[5] == ZXAPP_TRUE,
          "Hollow: the C record's own edges (HR6)");
    zxapp_arg_t da[4] = {{"place", a, WAVE_ROWS},
                         {"app", b, WAVE_ROWS},
                         {"pinned", c, WAVE_ROWS},
                         {"added", d, WAVE_ROWS}};
    CHECK(wave_same(app, "dock_ok", da, 4, want, WAVE_ROWS, true), "Hollow: dock_ok, MYRA = C");
}
