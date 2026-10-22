/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Gesso's languages against the C its model runs (gesso_rules.h).
 * Slots: @num.gesso_pixel_bright, @num.gesso_pixel_contrast,
 * @num.gesso_pixel_blend, @num.gesso_pixel_range_count,
 * @logic.gesso_brush_limits_ok, @logic.gesso_canvas_limits_ok,
 * @logic.gesso_paint_layer_ok, and, used as they are, Hollow's
 * @num.hollow_shown and Witness's @num.witness_kind_count. */
#include "gesso_rules.h"

APP_TEST(gesso, 29, 53)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];

    /* pixel_bright (GR1): edges, then swept, KRON = C++ = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, GESSO_PX_MAX);
        b[i] = wrand(-GESSO_PX_MAX, GESSO_PX_MAX);
    }
    a[0] = 0, b[0] = 0;                        /* black stays */
    a[1] = GESSO_PX_MAX, b[1] = 0;             /* white stays */
    a[2] = 0, b[2] = -GESSO_PX_MAX;            /* held at 0 */
    a[3] = GESSO_PX_MAX, b[3] = GESSO_PX_MAX;  /* held at 255 */
    a[4] = 0, b[4] = GESSO_PX_MAX;             /* black to white */
    a[5] = GESSO_PX_MAX, b[5] = -GESSO_PX_MAX; /* white to black */
    a[6] = 100, b[6] = 50;
    a[7] = 100, b[7] = -150;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = gesso_bright_px(a[i], b[i]);
    CHECK(want[0] == 0 && want[1] == 255 && want[2] == 0 && want[3] == 255 && want[4] == 255 &&
              want[5] == 0 && want[6] == 150 && want[7] == 0 &&
              gesso_bright_px(256, 0) == GESSO_REFUSED && gesso_bright_px(-1, 0) == GESSO_REFUSED &&
              gesso_bright_px(0, 256) == GESSO_REFUSED && gesso_bright_px(0, -256) == GESSO_REFUSED,
          "Gesso: the C brightness's own edges (GR1)");
    zxapp_arg_t ba[2] = {{"px", a, WAVE_ROWS}, {"delta", b, WAVE_ROWS}};
    CHECK(wave_same(app, "pixel_bright", ba, 2, want, WAVE_ROWS, false),
          "Gesso: pixel_bright, KRON = C++ = C");
    {
        static const int64_t over[2] = {10, 256}, neg[2] = {10, -1}, z2[2] = {0, 0},
                             tens[2] = {10, 10}, far[2] = {0, -256};
        zxapp_arg_t xa[2] = {{"px", over, 2}, {"delta", z2, 2}};
        zxapp_arg_t ya[2] = {{"px", neg, 2}, {"delta", z2, 2}};
        zxapp_arg_t za[2] = {{"px", tens, 2}, {"delta", far, 2}};
        CHECK(wave_lp(app, "pixel_bright", xa, 2) == ZXAPP_BOTH,
              "Gesso: a value of 256: C++ refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "pixel_bright", ya, 2) == ZXAPP_BOTH,
              "Gesso: a value of -1: C++ refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "pixel_bright", za, 2) == ZXAPP_BOTH,
              "Gesso: a delta of -256: C++ refuses, KRON answers: BOTH");
    }

    /* pixel_contrast (GR2): edges, then swept, KRON = C++ = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, GESSO_PX_MAX);
        b[i] = wrand(0, GESSO_GAIN_MAX);
    }
    a[0] = 128, b[0] = GESSO_UNITY; /* grey stays */
    a[1] = 0, b[1] = GESSO_UNITY;   /* unity: as it was */
    a[2] = 255, b[2] = GESSO_UNITY;
    a[3] = 100, b[3] = 20000;          /* twice: below grey goes down */
    a[4] = 200, b[4] = 20000;          /* held at 255 */
    a[5] = 0, b[5] = 0;                /* no contrast: grey */
    a[6] = 0, b[6] = GESSO_GAIN_MAX;   /* held at 0 */
    a[7] = 127, b[7] = 5000;           /* -0.5 floors to -1 */
    a[8] = 129, b[8] = 5000;           /* 0.5 floors to 0 */
    a[9] = 255, b[9] = GESSO_GAIN_MAX; /* max, held */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = gesso_contrast_px(a[i], b[i]);
    CHECK(want[0] == 128 && want[1] == 0 && want[2] == 255 && want[3] == 72 && want[4] == 255 &&
              want[5] == 128 && want[6] == 0 && want[7] == 127 && want[8] == 128 &&
              want[9] == 255 && gesso_contrast_px(256, 1) == GESSO_REFUSED &&
              gesso_contrast_px(1, -1) == GESSO_REFUSED &&
              gesso_contrast_px(1, GESSO_GAIN_MAX + 1) == GESSO_REFUSED,
          "Gesso: the C contrast's own edges (GR2)");
    zxapp_arg_t ca[2] = {{"px", a, WAVE_ROWS}, {"gain", b, WAVE_ROWS}};
    CHECK(wave_same(app, "pixel_contrast", ca, 2, want, WAVE_ROWS, false),
          "Gesso: pixel_contrast, KRON = C++ = C");
    {
        static const int64_t p2[2] = {10, 10}, g2[2] = {GESSO_UNITY, GESSO_GAIN_MAX + 1},
                             pn[2] = {10, -1}, gu[2] = {GESSO_UNITY, GESSO_UNITY};
        zxapp_arg_t xa[2] = {{"px", p2, 2}, {"gain", g2, 2}};
        zxapp_arg_t ya[2] = {{"px", pn, 2}, {"gain", gu, 2}};
        CHECK(wave_lp(app, "pixel_contrast", xa, 2) == ZXAPP_BOTH,
              "Gesso: a gain past 4 times: C++ refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "pixel_contrast", ya, 2) == ZXAPP_BOTH,
              "Gesso: a value of -1: C++ refuses, KRON answers: BOTH");
    }

    /* pixel_blend (GR3): edges, then swept, KRON = C++ = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, GESSO_PX_MAX);
        b[i] = wrand(0, GESSO_PX_MAX);
        c[i] = wrand(0, GESSO_PX_MAX);
    }
    a[0] = 0, b[0] = 255, c[0] = 0;     /* all base */
    a[1] = 0, b[1] = 255, c[1] = 255;   /* all top */
    a[2] = 255, b[2] = 255, c[2] = 128; /* white over white */
    a[3] = 100, b[3] = 200, c[3] = 128; /* 150.19 floored */
    a[4] = 10, b[4] = 20, c[4] = 1;     /* 10.03 floored */
    a[5] = 255, b[5] = 0, c[5] = 255;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = gesso_blend_px(a[i], b[i], c[i]);
    CHECK(want[0] == 0 && want[1] == 255 && want[2] == 255 && want[3] == 150 && want[4] == 10 &&
              want[5] == 0 && gesso_blend_px(0, 0, 256) == GESSO_REFUSED &&
              gesso_blend_px(-1, 0, 0) == GESSO_REFUSED &&
              gesso_blend_px(0, 256, 0) == GESSO_REFUSED,
          "Gesso: the C blend's own edges (GR3)");
    zxapp_arg_t la[3] = {{"base", a, WAVE_ROWS}, {"top", b, WAVE_ROWS}, {"alpha", c, WAVE_ROWS}};
    CHECK(wave_same(app, "pixel_blend", la, 3, want, WAVE_ROWS, false),
          "Gesso: pixel_blend, KRON = C++ = C");
    {
        static const int64_t one[2] = {1, 1}, al[2] = {1, 256};
        zxapp_arg_t xa[3] = {{"base", one, 2}, {"top", one, 2}, {"alpha", al, 2}};
        CHECK(wave_lp(app, "pixel_blend", xa, 3) == ZXAPP_BOTH,
              "Gesso: an alpha of 256: C++ refuses, KRON answers: BOTH");
    }

    /* pixel_range_count (GR4): rows of pixels, one lo and one hi each, KRON = Trebago = C */
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t row[64], lo[1], hi[1], count;
        uint32_t n = 1u + r % 64u;
        for (uint32_t i = 0; i < n; i++) row[i] = wrand(0, GESSO_PX_MAX);
        lo[0] = wrand(0, GESSO_PX_MAX);
        hi[0] = r % 4u ? wrand(lo[0], GESSO_PX_MAX) : wrand(0, GESSO_PX_MAX);
        if (r == 0) lo[0] = 0, hi[0] = GESSO_PX_MAX; /* the whole range: every pixel */
        if (r == 1) lo[0] = 9, hi[0] = 3;            /* an empty range */
        count = gesso_range_count(row, n, lo[0], hi[0]);
        CHECK(r != 0 || count == (int64_t) n, "Gesso: the whole range counts every pixel");
        CHECK(r != 1 || count == 0, "Gesso: an empty range counts none");
        zxapp_arg_t ra[3] = {{"px", row, n}, {"lo", lo, 1}, {"hi", hi, 1}};
        CHECK(wave_same(app, "pixel_range_count", ra, 3, &count, 1, false),
              "Gesso: row %u, %u pixels in %lld..%lld: KRON = Trebago = C", r, n, (long long) lo[0],
              (long long) hi[0]);
    }
    {
        static const int64_t px[2] = {1, 2}, two[2] = {1, 1}, hi2[2] = {2, 2};
        zxapp_arg_t xa[3] = {{"px", px, 2}, {"lo", two, 2}, {"hi", hi2, 2}};
        CHECK(wave_lp(app, "pixel_range_count", xa, 3) == ZXAPP_BOTH,
              "Gesso: two lo and hi: Trebago refuses, KRON sums them: BOTH");
    }

    /* brush_limits_ok (GR5): Eve = Sutra = C, every edge, then swept inside Sutra's 9(4) */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, GESSO_BRUSH_MAX + 2);
        b[i] = wrand(0, GESSO_PX_MAX + 2);
        c[i] = wrand(0, GESSO_HARD_MAX + 2);
    }
    a[0] = 1, b[0] = 0, c[0] = 0;                                       /* least */
    a[1] = GESSO_BRUSH_MAX, b[1] = GESSO_PX_MAX, c[1] = GESSO_HARD_MAX; /* most */
    a[2] = 0, b[2] = 10, c[2] = 50;                                     /* no brush */
    a[3] = GESSO_BRUSH_MAX + 1, b[3] = 10, c[3] = 50;                   /* size + 1 */
    a[4] = 10, b[4] = GESSO_PX_MAX + 1, c[4] = 50;                      /* opacity + 1 */
    a[5] = 10, b[5] = 10, c[5] = GESSO_HARD_MAX + 1;                    /* hardness + 1 */
    a[6] = 9999, b[6] = 9999, c[6] = 9999;                              /* Sutra's most */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(gesso_brush_ok(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE,
          "Gesso: the C brush limits' own edges (GR5)");
    zxapp_arg_t wa[3] = {
        {"size", a, WAVE_ROWS}, {"opacity", b, WAVE_ROWS}, {"hardness", c, WAVE_ROWS}};
    CHECK(wave_same(app, "brush_limits_ok", wa, 3, want, WAVE_ROWS, true),
          "Gesso: brush_limits_ok, Eve = Sutra = C");
    {
        static const int64_t sz[2] = {10, -1}, big[2] = {10, 10000}, ten[2] = {10, 10};
        zxapp_arg_t xa[3] = {{"size", sz, 2}, {"opacity", ten, 2}, {"hardness", ten, 2}};
        zxapp_arg_t ya[3] = {{"size", ten, 2}, {"opacity", big, 2}, {"hardness", ten, 2}};
        CHECK(wave_lp(app, "brush_limits_ok", xa, 3) == ZXAPP_BOTH,
              "Gesso: a size of -1: Sutra refuses, Eve answers false: BOTH");
        CHECK(wave_lp(app, "brush_limits_ok", ya, 3) == ZXAPP_BOTH,
              "Gesso: an opacity past 9(4): Sutra refuses, Eve answers false: BOTH");
    }

    /* canvas_limits_ok (GR6): Eve = Sutra = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, GESSO_SIDE_MAX + 2);
        b[i] = wrand(0, GESSO_SIDE_MAX + 2);
    }
    a[0] = 1, b[0] = 1;
    a[1] = GESSO_SIDE_MAX, b[1] = GESSO_SIDE_MAX;
    a[2] = 0, b[2] = 100;
    a[3] = GESSO_SIDE_MAX + 1, b[3] = 100;
    a[4] = 100, b[4] = 0;
    a[5] = 100, b[5] = GESSO_SIDE_MAX + 1;
    a[6] = 99999, b[6] = 99999; /* Sutra's most */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(gesso_canvas_ok(a[i], b[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE,
          "Gesso: the C canvas limits' own edges (GR6)");
    zxapp_arg_t va[2] = {{"width", a, WAVE_ROWS}, {"height", b, WAVE_ROWS}};
    CHECK(wave_same(app, "canvas_limits_ok", va, 2, want, WAVE_ROWS, true),
          "Gesso: canvas_limits_ok, Eve = Sutra = C");
    {
        static const int64_t w2[2] = {100, -1}, h2[2] = {100, 100}, wb[2] = {100, 100000};
        zxapp_arg_t xa[2] = {{"width", w2, 2}, {"height", h2, 2}};
        zxapp_arg_t ya[2] = {{"width", wb, 2}, {"height", h2, 2}};
        CHECK(wave_lp(app, "canvas_limits_ok", xa, 2) == ZXAPP_BOTH,
              "Gesso: a width of -1: Sutra refuses, Eve answers false: BOTH");
        CHECK(wave_lp(app, "canvas_limits_ok", ya, 2) == ZXAPP_BOTH,
              "Gesso: a width past 9(5): Sutra refuses, Eve answers false: BOTH");
    }

    /* paint_layer_ok (GR7): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-12, 12);
        b[i] = wrand(-12, 12);
        c[i] = wrand(-120000, 120000);
        d[i] = wrand(-1200, 1200);
        e[i] = wrand(-12, 12);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0;
    a[1] = 9, b[1] = 9, c[1] = GESSO_AMOUNT_MAX, d[1] = 999, e[1] = 9;      /* every max */
    a[2] = 10, b[2] = 1, c[2] = 1, d[2] = 1, e[2] = 1;                      /* layer + 1 */
    a[3] = 1, b[3] = 10, c[3] = 1, d[3] = 1, e[3] = 1;                      /* kind + 1 */
    a[4] = 1, b[4] = 1, c[4] = GESSO_AMOUNT_MAX + 1, d[4] = 1, e[4] = 1;    /* amount + 1 */
    a[5] = 1, b[5] = 1, c[5] = 1, d[5] = 1000, e[5] = 1;                    /* opacity + 1 */
    a[6] = 1, b[6] = 1, c[6] = 1, d[6] = 1, e[6] = 10;                      /* visible + 1 */
    a[7] = -9, b[7] = -9, c[7] = -GESSO_AMOUNT_MAX, d[7] = -999, e[7] = -9; /* sign aside */
    a[8] = 2, b[8] = GESSO_LAYER_CONTRAST, c[8] = 15000, d[8] = 200, e[8] = 1;
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(gesso_record_fits(a[i], b[i], c[i], d[i], e[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_TRUE && want[8] == ZXAPP_TRUE,
          "Gesso: the layer record's own edges (GR7)");
    zxapp_arg_t ya[5] = {{"layer", a, WAVE_ROWS},
                         {"kind", b, WAVE_ROWS},
                         {"amount", c, WAVE_ROWS},
                         {"opacity", d, WAVE_ROWS},
                         {"visible", e, WAVE_ROWS}};
    CHECK(wave_same(app, "paint_layer_ok", ya, 5, want, WAVE_ROWS, true),
          "Gesso: a layer makes a ^ZXGESSO record, MYRA = C");

    /* hollow.shown: a brush dab's span clipped to the canvas, the model's C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        b[i] = wrand(1, GESSO_BRUSH_MAX);
        c[i] = wrand(1, GESSO_SIDE_MAX);
        a[i] = wrand(-GESSO_BRUSH_MAX, GESSO_SIDE_MAX + GESSO_BRUSH_MAX);
    }
    a[0] = -4, b[0] = 9, c[0] = 1024;                          /* off the left: 5 shown */
    a[1] = 1020, b[1] = 9, c[1] = 1024;                        /* off the right: 4 */
    a[2] = 0, b[2] = 9, c[2] = 1024;                           /* inside */
    a[3] = 2000, b[3] = 9, c[3] = 1024;                        /* past it: none */
    a[4] = -GESSO_BRUSH_MAX, b[4] = GESSO_BRUSH_MAX, c[4] = 1; /* ends at 0: none */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = hollow_shown(a[i], b[i], c[i]);
    CHECK(want[0] == 5 && want[1] == 4 && want[2] == 9 && want[3] == 0 && want[4] == 0,
          "Gesso: a dab's span on the canvas, its own edges");
    zxapp_arg_t sa[3] = {{"pos", a, WAVE_ROWS}, {"size", b, WAVE_ROWS}, {"span", c, WAVE_ROWS}};
    CHECK(wave_same(app, "hollow.shown", sa, 3, want, WAVE_ROWS, false),
          "Gesso: a dab clipped through hollow.shown, KRON = Sutra = C");
    {
        static const int64_t p[2] = {0, 0}, s[2] = {9, -1}, w[2] = {1024, 1024};
        zxapp_arg_t xa[3] = {{"pos", p, 2}, {"size", s, 2}, {"span", w, 2}};
        CHECK(wave_lp(app, "hollow.shown", xa, 3) == ZXAPP_BOTH,
              "Gesso: a negative brush: Sutra refuses, KRON answers: BOTH");
    }

    /* witness.kind_count: the values an edit held at 0 or 255, the model's C */
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t row[64], one[1], count;
        uint32_t n = 1u + r % 64u;
        int64_t delta = wrand(-GESSO_PX_MAX, GESSO_PX_MAX);
        for (uint32_t i = 0; i < n; i++) row[i] = gesso_bright_px(wrand(0, GESSO_PX_MAX), delta);
        one[0] = r % 2u ? GESSO_PX_MAX : 0;
        count = witness_kind_count(row, n, one[0]);
        zxapp_arg_t ka[2] = {{"kind", row, n}, {"want", one, 1}};
        CHECK(wave_same(app, "witness.kind_count", ka, 2, &count, 1, false),
              "Gesso: row %u brightened by %lld, %lld held at %lld through witness.kind_count", r,
              (long long) delta, (long long) count, (long long) one[0]);
    }
    {
        static const int64_t k2[2] = {1, 2}, w2[2] = {1, 1};
        zxapp_arg_t ka[2] = {{"kind", k2, 2}, {"want", w2, 2}};
        CHECK(wave_lp(app, "witness.kind_count", ka, 2) == ZXAPP_BOTH,
              "Gesso: two levels at once: Trebago refuses, KRON sums them: BOTH");
    }
}
