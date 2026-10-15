/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_vec.c — the vector renderer and ZXVG reader (zui_vec.h V1..V8). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zui_vec.h"
#include "zui_progress.h"
#include "zui_tokens.h"
#include "test_zui_vec_fixture.inc" /* ZXVG_FIXTURE[], from tools/zxvg/test/fixture.svg */

static int fails, checks;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL (%s:%d) ", __FILE__, __LINE__);                                         \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

#define W    256
#define H    256
#define U(x) ((int32_t) ((x) * 16)) /* user units -> 1/16 */

/* a little double maths so the test needs no libm */
#define PI 3.14159265358979323846
static double fabs_(double x)
{
    return x < 0 ? -x : x;
}
static double floor_(double x)
{
    double t = (double) (long long) x;
    return t > x ? t - 1 : t;
}
static double sin_(double x)
{
    x -= 2 * PI * floor_(x / (2 * PI) + 0.5); /* -pi..pi */
    double t = x, s = x;
    for (int k = 1; k < 14; k++) {
        t *= -x * x / ((2 * k) * (2 * k + 1));
        s += t;
    }
    return s;
}
static double cos_(double x)
{
    return sin_(x + PI / 2);
}
#define fabs  fabs_
#define floor floor_
#define sin   sin_
#define cos   cos_

static uint32_t PX[W * H], PX2[W * H];
static uint32_t LAYER[W * H * 2];
static zui_vec_work_t WK;
static zui_surface_t S;
static zui_vec_t V;

static void clear(uint32_t rgb)
{
    for (int i = 0; i < W * H; i++) PX[i] = rgb;
    zui_surface_init(&S, PX, W, H, W);
    zui_vec_init(&V, &S, &WK);
}
static uint32_t at(int32_t x, int32_t y)
{
    return PX[y * W + x] & 0xFFFFFFu;
}
static double area(void) /* white-on-black coverage in px */
{
    double a = 0;
    for (int i = 0; i < W * H; i++) a += (double) (PX[i] & 0xFF) / 255.0;
    return a;
}
static int count_eq(uint32_t v)
{
    int n = 0;
    for (int i = 0; i < W * H; i++) n += (PX[i] & 0xFFFFFFu) == v;
    return n;
}
static uint32_t fnv(const uint32_t *p, int n)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; i++)
        for (int k = 0; k < 32; k += 8) h = (h ^ ((p[i] >> k) & 0xFFu)) * 16777619u;
    return h;
}

static const zui_vec_paint_t *white(void)
{
    static zui_vec_paint_t p;
    p = zui_vec_solid(0xFFFFFFFFu);
    return &p;
}
static zui_vec_stroke_t pen(double w, int join, int cap)
{
    zui_vec_stroke_t s;
    memset(&s, 0, sizeof s);
    s.width = U(w);
    s.join = (uint8_t) join;
    s.cap = (uint8_t) cap;
    s.miter_q8 = 4 * 256;
    return s;
}

static void star(double cx, double cy, double r) /* {5/2} pentagram, one path */
{
    for (int i = 0; i < 5; i++) {
        double a = -PI / 2 + i * 4 * PI / 5;
        int32_t x = U(cx + r * cos(a)), y = U(cy + r * sin(a));
        if (i == 0)
            zui_vec_move_to(&V, x, y);
        else
            zui_vec_line_to(&V, x, y);
    }
    zui_vec_close(&V);
}

/* ---- fills: exact coverage ----------------------------------------------- */
static void test_fill_exact(void)
{
    clear(0);
    zui_vec_rect(&V, U(10), U(10), U(40), U(40));
    CHECK(zui_vec_fill(&V, white(), ZUI_VEC_NONZERO) == 0, "fill ok");
    CHECK(count_eq(0xFFFFFFu) == 1600 && count_eq(0) == W * H - 1600,
          "V3: an integer square covers exactly its pixels (%d)", count_eq(0xFFFFFFu));
    CHECK(at(10, 10) == 0xFFFFFFu && at(49, 49) == 0xFFFFFFu && at(50, 49) == 0 && at(9, 10) == 0,
          "and nothing beyond its edges");

    clear(0);
    zui_vec_rect(&V, U(10.5), U(10.5), U(10), U(10));
    zui_vec_fill(&V, white(), ZUI_VEC_NONZERO);
    CHECK(at(15, 15) == 0xFFFFFFu, "inside a half-pixel square: full");
    CHECK(at(10, 15) == 0x808080u && at(20, 15) == 0x808080u && at(15, 10) == 0x808080u &&
              at(15, 20) == 0x808080u,
          "V3: half-covered edge pixels are exactly half (%06x %06x)", at(10, 15), at(15, 10));
    CHECK(at(10, 10) == 0x404040u && at(20, 20) == 0x404040u,
          "a quarter-covered corner is a quarter (%06x)", at(10, 10));
    CHECK(fabs(area() - 100.0) < 0.1, "area 100 (%.3f)", area());

    /* the path is kept: fill again elsewhere is the same path */
    clear(0);
    zui_vec_circle(&V, U(128), U(128), U(80));
    zui_vec_fill(&V, white(), ZUI_VEC_NONZERO);
    double a = area(), want = PI * 80 * 80;
    CHECK(fabs(a - want) / want < 0.002, "V4: a circle's area is pi r^2 (%.1f vs %.1f)", a, want);
    CHECK(at(128, 128) == 0xFFFFFFu && at(128, 46) == 0 && at(128, 49) == 0xFFFFFFu,
          "and it is round: top edge at r");
    int asym = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W / 2; x++) {
            int d = (int) (at(x, y) & 0xFF) - (int) (at(255 - x, y) & 0xFF);
            if (d > 3 || d < -3) asym++;
        }
    CHECK(asym == 0, "mirror symmetric within 3/255 (%d)", asym);
    clear(0);
    zui_vec_circle(&V, U(30), U(30), U(3));
    zui_vec_fill(&V, white(), ZUI_VEC_NONZERO);
    CHECK(fabs(area() - PI * 9) < 0.5, "a small circle too (%.2f)", area());
}

static void test_rules(void)
{
    clear(0);
    star(128, 128, 100);
    zui_vec_fill(&V, white(), ZUI_VEC_NONZERO);
    double nz = area();
    uint32_t c_nz = at(128, 128);
    clear(0);
    star(128, 128, 100);
    zui_vec_fill(&V, white(), ZUI_VEC_EVENODD);
    double eo = area();
    uint32_t c_eo = at(128, 128);
    CHECK(c_nz == 0xFFFFFFu && c_eo == 0, "V3: the star's centre: nonzero fills, even-odd not");
    /* the inner pentagon: circumradius r * sin(18)/sin(126) */
    double ri = 100 * sin(PI / 10) / sin(7 * PI / 10);
    double pent = 2.5 * ri * ri * sin(2 * PI / 5);
    CHECK(fabs((nz - eo) - pent) < 3.0, "the difference is the pentagon (%.1f vs %.1f)", nz - eo,
          pent);
    double outer = 10 * 0.5 * 100 * ri * sin(PI / 5); /* 10 triangles centre-tip-notch */
    CHECK(fabs(nz - outer) < 3.0, "nonzero area is the whole star (%.1f vs %.1f)", nz, outer);
    /* two overlapping squares wound the same way, even-odd leaves the overlap */
    clear(0);
    zui_vec_rect(&V, U(10), U(10), U(40), U(40));
    zui_vec_rect(&V, U(30), U(30), U(40), U(40));
    zui_vec_fill(&V, white(), ZUI_VEC_EVENODD);
    CHECK(fabs(area() - (1600 * 2 - 2 * 400)) < 0.01 && at(40, 40) == 0,
          "even-odd overlap is empty (%.1f)", area());
}

/* ---- strokes ------------------------------------------------------------- */
static double stroke_line(int cap)
{
    clear(0);
    zui_vec_move_to(&V, U(20), U(50));
    zui_vec_line_to(&V, U(120), U(50));
    zui_vec_stroke_t p = pen(10, ZUI_VEC_JOIN_MITER, cap);
    zui_vec_stroke(&V, white(), &p);
    return area();
}
static double stroke_square(int join)
{
    clear(0);
    zui_vec_rect(&V, U(20), U(20), U(40), U(40));
    zui_vec_stroke_t p = pen(8, join, ZUI_VEC_CAP_BUTT);
    zui_vec_stroke(&V, white(), &p);
    return area();
}

static void test_strokes(void)
{
    double a = stroke_line(ZUI_VEC_CAP_BUTT);
    CHECK(fabs(a - 1000) < 0.01 && at(20, 45) == 0xFFFFFFu && at(119, 54) == 0xFFFFFFu &&
              at(120, 50) == 0 && at(19, 50) == 0 && at(50, 44) == 0 && at(50, 55) == 0,
          "V5: a butt-capped line is exactly its rectangle (%.2f)", a);
    a = stroke_line(ZUI_VEC_CAP_SQUARE);
    CHECK(fabs(a - 1100) < 0.01, "square caps add half a width each end (%.2f)", a);
    a = stroke_line(ZUI_VEC_CAP_ROUND);
    CHECK(fabs(a - (1000 + PI * 25)) < 1.0, "round caps add a disc (%.2f)", a);

    a = stroke_square(ZUI_VEC_JOIN_MITER);
    CHECK(fabs(a - 1280) < 0.01 && at(16, 16) == 0xFFFFFFu && at(24, 24) == 0,
          "miter joins: a square ring 48^2 - 32^2 (%.2f)", a);
    a = stroke_square(ZUI_VEC_JOIN_BEVEL);
    CHECK(fabs(a - 1248) < 0.6 && at(16, 16) == 0, "bevel joins cut the corners (%.2f)", a);
    a = stroke_square(ZUI_VEC_JOIN_ROUND);
    CHECK(fabs(a - (1280 - 4 * (16 - PI * 4))) < 1.5, "round joins (%.2f)", a);

    /* a miter limit turns a sharp miter into a bevel */
    clear(0);
    zui_vec_move_to(&V, U(20), U(100));
    zui_vec_line_to(&V, U(120), U(110));
    zui_vec_line_to(&V, U(20), U(120));
    zui_vec_stroke_t p = pen(6, ZUI_VEC_JOIN_MITER, ZUI_VEC_CAP_BUTT);
    zui_vec_stroke(&V, white(), &p);
    double sharp = area();
    p.miter_q8 = 256 * 9;
    clear(0);
    zui_vec_move_to(&V, U(20), U(100));
    zui_vec_line_to(&V, U(120), U(110));
    zui_vec_line_to(&V, U(20), U(120));
    zui_vec_stroke(&V, white(), &p);
    double cut = area();
    p.miter_q8 = 256 * 12;
    clear(0);
    zui_vec_move_to(&V, U(20), U(100));
    zui_vec_line_to(&V, U(120), U(110));
    zui_vec_line_to(&V, U(20), U(120));
    zui_vec_stroke(&V, white(), &p);
    double tip = area();
    CHECK(fabs(sharp - cut) < 0.01 && tip > cut + 20,
          "miter limit: beveled at 4 and 9, tipped at "
          "12 (%.1f %.1f %.1f)",
          sharp, cut, tip);

    /* a translucent stroke crossing itself is not darker where it crosses */
    clear(0);
    zui_vec_move_to(&V, U(20), U(20));
    zui_vec_line_to(&V, U(100), U(100));
    zui_vec_line_to(&V, U(100), U(20));
    zui_vec_line_to(&V, U(20), U(100));
    zui_vec_stroke_t q = pen(8, ZUI_VEC_JOIN_MITER, ZUI_VEC_CAP_BUTT);
    zui_vec_paint_t half = zui_vec_solid(0x80FFFFFFu);
    zui_vec_stroke(&V, &half, &q);
    CHECK(at(60, 60) == 0x808080u && at(40, 40) == 0x808080u,
          "V5: overlaps never double-darken (%06x %06x)", at(60, 60), at(40, 40));

    /* dashes */
    clear(0);
    zui_vec_move_to(&V, U(20), U(50));
    zui_vec_line_to(&V, U(120), U(50));
    zui_vec_stroke_t d = pen(10, ZUI_VEC_JOIN_MITER, ZUI_VEC_CAP_BUTT);
    d.ndash = 2;
    d.dash[0] = U(10);
    d.dash[1] = U(10);
    zui_vec_stroke(&V, white(), &d);
    CHECK(fabs(area() - 500) < 0.01 && at(25, 50) == 0xFFFFFFu && at(35, 50) == 0 &&
              at(45, 50) == 0xFFFFFFu,
          "dashes: half on, in the right places (%.2f)", area());
    clear(0);
    zui_vec_move_to(&V, U(20), U(50));
    zui_vec_line_to(&V, U(120), U(50));
    d.dash_off = U(5);
    zui_vec_stroke(&V, white(), &d);
    CHECK(fabs(area() - 500) < 0.01 && at(22, 50) == 0xFFFFFFu && at(27, 50) == 0 &&
              at(37, 50) == 0xFFFFFFu,
          "dash offset shifts the pattern (%.2f)", area());
    clear(0);
    zui_vec_rect(&V, U(20), U(20), U(100), U(100));
    d.dash_off = 0;
    d.cap = ZUI_VEC_CAP_ROUND;
    d.dash[0] = 0;
    d.dash[1] = U(20);
    zui_vec_stroke(&V, white(), &d);
    double dots = area();
    CHECK(fabs(dots - 20 * PI * 25) < 20, "zero-length round dashes are dots (%.1f)", dots);

    /* a zero-length subpath with round caps is a dot; a lone move is nothing */
    clear(0);
    zui_vec_move_to(&V, U(50), U(50));
    zui_vec_close(&V);
    zui_vec_move_to(&V, U(150), U(150));
    zui_vec_stroke_t r = pen(10, ZUI_VEC_JOIN_ROUND, ZUI_VEC_CAP_ROUND);
    zui_vec_stroke(&V, white(), &r);
    CHECK(fabs(area() - PI * 25) < 1.0 && at(150, 150) == 0, "M Z dots, a lone M does not");
}

/* ---- paint ----------------------------------------------------------------- */
static void test_paint(void)
{
    clear(0);
    zui_vec_paint_t g;
    memset(&g, 0, sizeof g);
    g.kind = ZUI_VEC_LINEAR;
    g.nstop = 2;
    g.stop[0] = (zui_vec_stop_t){0, 0xFF000000u};
    g.stop[1] = (zui_vec_stop_t){255, 0xFFFFFFFFu};
    g.g = (zui_vec_xf_t){256 << 16, 0, 0, 256 << 16, 0, 0};
    zui_vec_rect(&V, 0, 0, U(256), U(256));
    zui_vec_fill(&V, &g, ZUI_VEC_NONZERO);
    int bad = 0;
    for (int x = 0; x < W; x++) {
        int want = (int) floor(255.0 * (x + 0.5) / 256.0 + 0.5), got = (int) (at(x, 100) & 0xFF);
        if (abs(want - got) > 1 || at(x, 100) != at(x, 7)) bad++;
    }
    CHECK(bad == 0, "linear gradient: exact ramp (%d off)", bad);

    g.spread = ZUI_VEC_REPEAT;
    g.g = (zui_vec_xf_t){64 << 16, 0, 0, 64 << 16, 0, 0};
    zui_vec_fill(&V, &g, ZUI_VEC_NONZERO);
    CHECK(at(10, 5) == at(74, 5) && at(10, 5) == at(138, 5), "repeat repeats");
    g.spread = ZUI_VEC_REFLECT;
    zui_vec_fill(&V, &g, ZUI_VEC_NONZERO);
    int d = (int) (at(10, 5) & 0xFF) - (int) (at(117, 5) & 0xFF);
    CHECK(abs(d) <= 1 && (at(70, 5) & 0xFF) > 200, "reflect mirrors (%d)", d);
    g.spread = ZUI_VEC_PAD;
    zui_vec_fill(&V, &g, ZUI_VEC_NONZERO);
    CHECK(at(200, 5) == 0xFFFFFFu && at(0, 5) == 0x020202u, "pad holds the ends (%06x)", at(0, 5));

    /* rotated linear: gradient along y via the matrix */
    g.g = (zui_vec_xf_t){0, 256 << 16, -(256 << 16), 0, 0, 0};
    zui_vec_fill(&V, &g, ZUI_VEC_NONZERO);
    CHECK(at(3, 100) == at(250, 100) && (at(3, 10) & 0xFF) < (at(3, 200) & 0xFF),
          "a rotated gradient runs down");

    clear(0);
    zui_vec_rect(&V, 0, 0, U(256), U(256));
    g.kind = ZUI_VEC_RADIAL;
    g.g = (zui_vec_xf_t){100 << 16, 0, 0, 100 << 16, 128 << 16, 128 << 16};
    zui_vec_fill(&V, &g, ZUI_VEC_NONZERO);
    int mid = (int) (at(178, 128) & 0xFF);
    CHECK((at(128, 128) & 0xFF) <= 2 && abs(mid - 129) <= 2 && at(240, 128) == 0xFFFFFFu &&
              at(128, 240) == 0xFFFFFFu,
          "radial gradient: centre, half radius (%d), outside", mid);
    CHECK(at(178, 128) == at(78, 127) ||
              abs((int) (at(178, 128) & 0xFF) - (int) (at(77, 128) & 0xFF)) <= 1,
          "radial is symmetric");

    /* degenerate gradient: the last stop */
    g.g = (zui_vec_xf_t){0, 0, 0, 0, 0, 0};
    zui_vec_fill(&V, &g, ZUI_VEC_NONZERO);
    CHECK(at(5, 5) == 0xFFFFFFu, "a zero-size gradient paints its last stop");

    /* alpha and opacity */
    clear(0);
    zui_vec_rect(&V, U(10), U(10), U(10), U(10));
    zui_vec_paint_t a = zui_vec_solid(0x80FFFFFFu);
    zui_vec_fill(&V, &a, ZUI_VEC_NONZERO);
    CHECK(at(15, 15) == 0x808080u, "paint alpha (%06x)", at(15, 15));
    clear(0);
    V.opacity = 128;
    zui_vec_rect(&V, U(10), U(10), U(10), U(10));
    zui_vec_fill(&V, white(), ZUI_VEC_NONZERO);
    CHECK(at(15, 15) == 0x808080u, "opacity (%06x)", at(15, 15));
    /* clip */
    clear(0);
    V.clip = (zui_rect_t){50, 50, 10, 10};
    zui_vec_rect(&V, 0, 0, U(256), U(256));
    zui_vec_fill(&V, white(), ZUI_VEC_NONZERO);
    CHECK(count_eq(0xFFFFFFu) == 100 && at(50, 50) == 0xFFFFFFu && at(60, 50) == 0,
          "clip to a rect (%d)", count_eq(0xFFFFFFu));
    clear(0);
    S.clip = (zui_rect_t){0, 0, 20, 20};
    V.clip = (zui_rect_t){10, 10, 100, 100};
    zui_vec_rect(&V, 0, 0, U(256), U(256));
    zui_vec_fill(&V, white(), ZUI_VEC_NONZERO);
    CHECK(count_eq(0xFFFFFFu) == 100, "and to the surface's clip");
}

static void two_squares(void)
{
    zui_vec_rect(&V, U(10), U(10), U(20), U(20));
    zui_vec_fill(&V, white(), ZUI_VEC_NONZERO);
    zui_vec_begin(&V);
    zui_vec_rect(&V, U(20), U(10), U(20), U(20));
    zui_vec_fill(&V, white(), ZUI_VEC_NONZERO);
}

static void test_groups(void)
{
    clear(0);
    V.layer = LAYER;
    V.layer_cap = W * H;
    zui_vec_push(&V, 0, 128);
    two_squares();
    zui_vec_pop(&V);
    CHECK(at(25, 20) == 0x808080u && at(15, 20) == 0x808080u && at(35, 20) == 0x808080u,
          "V6: group opacity with a layer is exact (%06x %06x)", at(25, 20), at(15, 20));
    clear(0);
    zui_vec_push(&V, 0, 128);
    two_squares();
    zui_vec_pop(&V);
    CHECK(at(15, 20) == 0x808080u && at(25, 20) == 0xC0C0C0u,
          "without a layer it multiplies into each child (%06x)", at(25, 20));
    /* nested layers and a too-small layer buffer fall back */
    clear(0);
    V.layer = LAYER;
    V.layer_cap = W * H * 2;
    zui_vec_push(&V, 0, 128);
    zui_vec_push(&V, 0, 200);
    two_squares();
    zui_vec_pop(&V);
    zui_vec_pop(&V);
    CHECK(at(25, 20) == at(15, 20) && (at(15, 20) & 0xFF) > 90 && (at(15, 20) & 0xFF) < 110,
          "nested layers (%06x %06x)", at(25, 20), at(15, 20));
    clear(0);
    V.layer = LAYER;
    V.layer_cap = 10;
    zui_vec_push(&V, 0, 128);
    two_squares();
    zui_vec_pop(&V);
    CHECK(at(25, 20) == 0xC0C0C0u, "a layer that does not fit falls back");

    /* transforms */
    clear(0);
    zui_vec_xf_t m = zui_vec_mul(zui_vec_translate(128 << 16, 128 << 16), zui_vec_rotate(8192));
    zui_vec_push(&V, &m, 256);
    zui_vec_rect(&V, U(-40), U(-40), U(80), U(80));
    zui_vec_fill(&V, white(), ZUI_VEC_NONZERO);
    zui_vec_pop(&V);
    CHECK(fabs(area() - 6400) < 3 && at(128, 128 - 55) == 0xFFFFFFu && at(128 - 45, 128 - 45) == 0,
          "a rotated square keeps its area (%.1f)", area());
    clear(0);
    zui_vec_xf_t sc = zui_vec_scale(3 << 15, 1 << 16); /* 1.5 x 1 */
    zui_vec_push(&V, &sc, 256);
    zui_vec_rect(&V, U(10), U(10), U(20), U(20));
    zui_vec_fill(&V, white(), ZUI_VEC_NONZERO);
    zui_vec_pop(&V);
    CHECK(fabs(area() - 600) < 0.01, "non-uniform scale (%.2f)", area());
    CHECK(V.depth == 0 && V.xf.a == 65536, "pop restores the transform");
    for (int i = 0; i < ZUI_VEC_MAX_DEPTH; i++) zui_vec_push(&V, 0, 256);
    CHECK(zui_vec_push(&V, 0, 256) == -1, "nesting is capped");
    for (int i = 0; i < ZUI_VEC_MAX_DEPTH; i++) zui_vec_pop(&V);
    zui_vec_pop(&V); /* extra pop is harmless */
    CHECK(V.depth == 0, "and unwinds");
}

static void test_caps(void)
{
    clear(0);
    uint32_t before = fnv(PX, W * H);
    zui_vec_move_to(&V, 0, 0);
    for (int i = 0; i < ZUI_VEC_MAX_PTS + 10; i++)
        zui_vec_line_to(&V, U(i % 200), U((i * 7) % 200));
    CHECK(zui_vec_fill(&V, white(), ZUI_VEC_NONZERO) == -1 && (V.err & ZUI_VEC_EFULL),
          "V7: a path over the point cap is refused");
    CHECK(fnv(PX, W * H) == before, "and nothing is drawn");
    zui_vec_begin(&V);
    zui_vec_rect(&V, U(1), U(1), U(2), U(2));
    CHECK(zui_vec_fill(&V, white(), ZUI_VEC_NONZERO) == 0, "begin recovers");
    /* edge cap: many small circles in one path */
    clear(0);
    V.err = 0;
    before = fnv(PX, W * H);
    for (int i = 0; i < 400; i++)
        zui_vec_circle(&V, U(10 + i % 20 * 12), U(10 + i / 20 * 12), U(5));
    int rc = zui_vec_fill(&V, white(), ZUI_VEC_NONZERO);
    CHECK((rc == 0) == !(V.err & ZUI_VEC_EFULL), "fill reports what it did");
    if (rc < 0) CHECK(fnv(PX, W * H) == before, "a refused shape draws nothing");
}

/* ---- ZXVG ---------------------------------------------------------------- */
typedef struct {
    uint8_t b[8192];
    uint32_t n;
} buf_t;
static void u8(buf_t *b, uint32_t v)
{
    b->b[b->n++] = (uint8_t) v;
}
static void uv(buf_t *b, uint32_t v)
{
    while (v >= 0x80u) {
        u8(b, (v & 0x7Fu) | 0x80u);
        v >>= 7;
    }
    u8(b, v);
}
static void zz(buf_t *b, int32_t v)
{
    uv(b, v >= 0 ? (uint32_t) v << 1 : ((uint32_t) (-(v + 1)) << 1) | 1u);
}
static void head(buf_t *b, int32_t vw, int32_t vh)
{
    b->n = 0;
    u8(b, 'Z'), u8(b, 'X'), u8(b, 'V'), u8(b, 'G'), u8(b, 1), u8(b, 0);
    zz(b, 0), zz(b, 0), uv(b, (uint32_t) U(vw)), uv(b, (uint32_t) U(vh));
}
static void pal(buf_t *b, uint32_t n, const uint32_t *argb)
{
    uv(b, n);
    for (uint32_t i = 0; i < n; i++)
        u8(b, (argb[i] >> 16) & 0xFF), u8(b, (argb[i] >> 8) & 0xFF), u8(b, argb[i] & 0xFF),
            u8(b, argb[i] >> 24);
}
static void ids(buf_t *b, uint32_t n, const char *const *s)
{
    uv(b, n);
    for (uint32_t i = 0; i < n; i++) {
        u8(b, (uint32_t) strlen(s[i]));
        for (const char *p = s[i]; *p; p++) u8(b, (uint8_t) *p);
    }
}
static int32_t CPX, CPY;
static void mv(buf_t *b, int op, double x, double y)
{
    u8(b, (uint32_t) op);
    zz(b, U(x) - CPX), zz(b, U(y) - CPY);
    CPX = U(x), CPY = U(y);
}
static void rect_cmd(buf_t *b, double x, double y, double w, double h)
{
    double sx = x, sy = y;
    mv(b, 1, x, y);
    mv(b, 2, x + w, y);
    mv(b, 2, x + w, y + h);
    mv(b, 2, x, y + h);
    u8(b, 6);
    CPX = U(sx), CPY = U(sy);
}
static void fill_cmd(buf_t *b, uint32_t rule, uint32_t ref, uint32_t alpha)
{
    u8(b, 0x10), u8(b, rule), uv(b, ref), u8(b, alpha);
}

/* a document using every command */
static void rich(buf_t *b)
{
    static const uint32_t P[4] = {0xFFFFFFFFu, 0xFFF2C230u, 0x80002911u, 0xFF6FB8FFu};
    static const char *const I[3] = {"chassis", "core-artifact", "kinetic-element"};
    CPX = CPY = 0;
    head(b, 100, 100);
    pal(b, 4, P);
    uv(b, 2);
    u8(b, 1), u8(b, 0); /* linear */
    zz(b, 100 << 16), zz(b, 0), zz(b, 0), zz(b, 100 << 16), zz(b, 0), zz(b, 0);
    u8(b, 2), u8(b, 0), uv(b, 1), u8(b, 255), uv(b, 3);
    u8(b, 2), u8(b, 1); /* radial */
    zz(b, 30 << 16), zz(b, 0), zz(b, 0), zz(b, 30 << 16), zz(b, 50 << 16), zz(b, 50 << 16);
    u8(b, 3), u8(b, 0), uv(b, 0), u8(b, 128), uv(b, 2), u8(b, 255), uv(b, 1);
    ids(b, 3, I);
    u8(b, 0x20), uv(b, 1), u8(b, 255), u8(b, 1);
    zz(b, 1 << 16), zz(b, 0), zz(b, 0), zz(b, 1 << 16), zz(b, 5 << 16), zz(b, 5 << 16);
    rect_cmd(b, 10, 10, 20, 20);
    fill_cmd(b, 0, 0 << 1 | 1, 255); /* gradient 0 */
    u8(b, 0x11), uv(b, 0), u8(b, 255), uv(b, (uint32_t) U(2)), u8(b, 0), u8(b, 0), uv(b, 1024);
    uv(b, 0), zz(b, 0);
    u8(b, 0x21);
    u8(b, 0x20), uv(b, 2), u8(b, 200), u8(b, 0);
    u8(b, 7);
    mv(b, 1, 50, 20);
    u8(b, 3), zz(b, U(10)), zz(b, U(-10)), zz(b, U(20)), zz(b, 0);
    CPX += U(20);
    u8(b, 4), zz(b, U(5)), zz(b, U(10)), zz(b, U(-15)), zz(b, U(20)), zz(b, U(-20)), zz(b, U(15));
    CPX += U(-20), CPY += U(15);
    u8(b, 5), uv(b, (uint32_t) U(8)), uv(b, (uint32_t) U(6)), uv(b, 4096), u8(b, 3), zz(b, U(10)),
        zz(b, U(-5));
    CPX += U(10), CPY += U(-5);
    u8(b, 6);
    CPX = U(50), CPY = U(20);
    fill_cmd(b, 1, 1 << 1 | 1, 200); /* gradient 1 */
    u8(b, 0x11), uv(b, 3 << 1), u8(b, 255), uv(b, (uint32_t) U(1.5)), u8(b, 1), u8(b, 1),
        uv(b, 1024);
    uv(b, 2), uv(b, (uint32_t) U(3)), uv(b, (uint32_t) U(2)), zz(b, U(1));
    u8(b, 0x20), uv(b, 3), u8(b, 255), u8(b, 0);
    rect_cmd(b, 60, 60, 20, 20);
    fill_cmd(b, 0, 2 << 1, 255);
    u8(b, 0x21);
    u8(b, 0x21);
    u8(b, 0x00);
}

static void test_zxvg(void)
{
    buf_t b;
    rich(&b);
    CHECK(zui_zxvg_check(b.b, b.n) == 0, "V8: a well-formed file checks");
    zui_rect_t vb;
    CHECK(zui_zxvg_viewbox(b.b, b.n, &vb) == 0 && vb.w == U(100) && vb.h == U(100), "viewBox");
    char id[32];
    CHECK(zui_zxvg_id_count(b.b, b.n) == 3 && zui_zxvg_id(b.b, b.n, 2, id) == 0 &&
              strcmp(id, "kinetic-element") == 0 && zui_zxvg_id(b.b, b.n, 3, id) == -1,
          "ids are kept and listed");
    clear(0);
    CHECK(zui_vec_draw_zxvg(&S, &WK, b.b, b.n, (zui_rect_t){0, 0, 100, 100}, 0) == 0, "draws");
    CHECK(at(25, 25) != 0 && at(70, 70) != 0 && at(95, 5) == 0, "where it should (%06x %06x)",
          at(25, 25), at(70, 70));
    zui_vec_group_info_t gi;
    CHECK(zui_zxvg_group(b.b, b.n, "chassis", &WK, &gi) == 0 && gi.x0 == U(9) && gi.y0 == U(9) &&
              gi.x1 == U(31) && gi.y1 == U(31) && gi.px == U(20) && gi.py == U(20) && gi.index == 0,
          "group box in its own units, stroke included (%d %d %d %d)", gi.x0, gi.y0, gi.x1, gi.y1);
    CHECK(zui_zxvg_group(b.b, b.n, "kinetic-element", &WK, &gi) == 0 && gi.x0 == U(60) &&
              gi.x1 == U(80) && gi.index == 2,
          "a nested group's box");
    CHECK(zui_zxvg_group(b.b, b.n, "core-artifact", &WK, &gi) == 0 && gi.x0 < U(50) &&
              gi.x1 > U(60) && gi.y0 < U(20) && gi.y1 > U(30),
          "core-artifact includes its children (%d..%d)", gi.x0, gi.x1);
    CHECK(zui_zxvg_group(b.b, b.n, "nope", &WK, &gi) == -1 &&
              zui_zxvg_group(b.b, b.n, "chassi", &WK, &gi) == -1,
          "an unknown id is absent");

    /* overrides */
    zui_vec_override_t ov;
    zui_vec_override_init(&ov, "kinetic-element");
    clear(0);
    zui_vec_draw_zxvg(&S, &WK, b.b, b.n, (zui_rect_t){0, 0, 100, 100}, 0);
    memcpy(PX2, PX, sizeof PX);
    clear(0);
    zui_vec_opts_t o;
    memset(&o, 0, sizeof o);
    o.ov = &ov;
    o.nov = 1;
    zui_vec_draw_zxvg(&S, &WK, b.b, b.n, (zui_rect_t){0, 0, 100, 100}, &o);
    CHECK(memcmp(PX, PX2, sizeof PX) == 0, "an identity override changes nothing");
    ov.m[4] = -(40 << 16);
    clear(0);
    zui_vec_draw_zxvg(&S, &WK, b.b, b.n, (zui_rect_t){0, 0, 100, 100}, &o);
    CHECK(at(70, 70) == 0 && at(30, 70) != 0, "an override moves the group");
    zui_vec_override_init(&ov, "kinetic-element");
    ov.opacity = 0;
    clear(0);
    zui_vec_draw_zxvg(&S, &WK, b.b, b.n, (zui_rect_t){0, 0, 100, 100}, &o);
    CHECK(at(70, 70) == 0 && at(25, 25) == (PX2[25 * W + 25] & 0xFFFFFFu),
          "opacity 0 hides only that group");
    zui_vec_override_init(&ov, "chassis");
    ov.tint = 0xFF0000u;
    ov.tint_amt = 256;
    clear(0);
    zui_vec_draw_zxvg(&S, &WK, b.b, b.n, (zui_rect_t){0, 0, 100, 100}, &o);
    CHECK(at(25, 25) == 0xFF0000u, "a full tint recolours the group (%06x)", at(25, 25));
    zui_vec_override_init(&ov, "chassis");
    ov.grad_shift = 1 << 15;
    clear(0);
    zui_vec_draw_zxvg(&S, &WK, b.b, b.n, (zui_rect_t){0, 0, 100, 100}, &o);
    CHECK(at(16, 16) != (PX2[16 * W + 16] & 0xFFFFFFu), "grad_shift moves the gradient");
    zui_vec_override_init(&ov, "kinetic-element");
    ov.m[0] = 0, ov.m[1] = 65536, ov.m[2] = -65536, ov.m[3] = 0; /* 90 degrees */
    ov.px = U(70), ov.py = U(70);
    clear(0);
    zui_vec_draw_zxvg(&S, &WK, b.b, b.n, (zui_rect_t){0, 0, 100, 100}, &o);
    int same = 0;
    for (int y = 55; y < 85; y++)
        for (int x = 55; x < 85; x++) same += at(x, y) == (PX2[y * W + x] & 0xFFFFFFu);
    CHECK(same >= 900 - 4, "a quarter turn about the centre maps a square onto itself (%d)", same);
}

static void test_simple(void)
{
    buf_t b;
    rich(&b);
    zui_rect_t box = {0, 0, 100, 100};
    clear(0);
    CHECK(zui_vec_draw_simple(&S, &WK, b.b, b.n, box, 256, 0xFFFF0000u) == 0, "simple draws");
    CHECK(at(25, 25) == 0xFF0000u && at(15, 50) == 0,
          "a tint holds every group to one colour "
          "(%06x)",
          at(25, 25));
    CHECK(at(70, 70) >= 0x630000u && at(70, 70) <= 0x650000u &&
              !(at(70, 70) & 0xFFFF), /* 128 x 200/255 */
          "keeping each paint's alpha (%06x)", at(70, 70));
    zui_vec_opts_t o;
    memset(&o, 0, sizeof o);
    o.opacity = 128;
    clear(0);
    zui_vec_draw_zxvg(&S, &WK, b.b, b.n, box, &o);
    memcpy(PX2, PX, sizeof PX);
    clear(0);
    zui_vec_draw_simple(&S, &WK, b.b, b.n, box, 128, 0);
    CHECK(memcmp(PX, PX2, sizeof PX) == 0, "strength is the opacity; tint 0 keeps the colours");
    clear(0);
    CHECK(zui_vec_draw_simple(&S, &WK, b.b, b.n, box, 0, 0xFFFF0000u) == 0 && count_eq(0) == W * H,
          "strength 0 draws nothing");
    b.b[b.n - 1] = 0x42;
    CHECK(zui_vec_draw_simple(&S, &WK, b.b, b.n, box, 256, 0) == -1 && count_eq(0) == W * H,
          "a malformed blob is refused and draws nothing");
}

static void test_scale(void)
{
    buf_t b;
    static const uint32_t P[1] = {0xFFFFFFFFu};
    CPX = CPY = 0;
    head(&b, 100, 100);
    pal(&b, 1, P);
    uv(&b, 0);
    uv(&b, 0);
    for (int i = 0; i < 5; i++) {
        double a = -PI / 2 + i * 4 * PI / 5;
        mv(&b, i ? 2 : 1, 50 + 45 * cos(a), 50 + 45 * sin(a));
    }
    u8(&b, 6);
    fill_cmd(&b, 1, 0, 255);
    u8(&b, 0);
    CHECK(zui_zxvg_check(b.b, b.n) == 0, "star file checks");
    clear(0);
    zui_vec_draw_zxvg(&S, &WK, b.b, b.n, (zui_rect_t){0, 0, 60, 60}, 0);
    double a1 = area();
    clear(0);
    zui_vec_draw_zxvg(&S, &WK, b.b, b.n, (zui_rect_t){0, 0, 120, 120}, 0);
    double a2 = area();
    CHECK(fabs(a2 / a1 - 4.0) < 0.04, "scale invariance: 2x covers 4x (%.3f)", a2 / a1);
    clear(0);
    zui_vec_draw_zxvg(&S, &WK, b.b, b.n, (zui_rect_t){0, 0, 240, 240}, 0);
    double a4 = area();
    CHECK(fabs(a4 / a1 - 16.0) < 0.16, "4x covers 16x (%.3f)", a4 / a1);
    /* fit keeps the aspect and centres */
    clear(0);
    zui_vec_draw_zxvg(&S, &WK, b.b, b.n, (zui_rect_t){0, 0, 240, 120}, 0);
    CHECK(fabs(area() - a2) < 0.01, "a wide box draws the same size (%.1f)", area());
    int minx = W, maxx = -1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (at(x, y)) minx = x < minx ? x : minx, maxx = x > maxx ? x : maxx;
    CHECK(abs((minx + maxx) - 239) <= 1, "centred in it (%d..%d)", minx, maxx);
    CHECK(zui_vec_draw_zxvg(&S, &WK, b.b, b.n, (zui_rect_t){0, 0, 0, 10}, 0) == 0,
          "an empty box draws nothing");
}

static void test_malformed(void)
{
    buf_t b;
    rich(&b);
    buf_t m;
#define MUT(stmt, what)                                                                            \
    do {                                                                                           \
        m = b;                                                                                     \
        stmt;                                                                                      \
        CHECK(zui_zxvg_check(m.b, m.n) == -1, "V8: refuses %s", what);                             \
    } while (0)
    MUT(m.b[0] = 'P', "a bad magic");
    MUT(m.b[4] = 2, "another version");
    MUT(m.b[5] = 1, "unknown flags");
    MUT(m.n--, "a missing END");
    MUT(m.b[m.n++] = 0, "trailing bytes");
    MUT(m.b[m.n - 2] = 0x00, "unbalanced groups");
    MUT(m.b[m.n - 1] = 0x99, "an unknown command");
    MUT(m.n = 3, "a stub");
    CHECK(zui_zxvg_check(0, 0) == -1 && zui_zxvg_check(b.b, 0) == -1, "null and empty");
    /* hand-built bad documents */
    static const uint32_t P[1] = {0xFFFFFFFFu};
    head(&m, 10, 10);
    pal(&m, 1, P);
    uv(&m, 0), uv(&m, 0);
    rect_cmd(&m, 1, 1, 2, 2);
    fill_cmd(&m, 0, 1 << 1, 255); /* palette index 1 of 1 */
    u8(&m, 0);
    CHECK(zui_zxvg_check(m.b, m.n) == -1, "a palette index out of range");
    head(&m, 10, 10);
    pal(&m, 1, P);
    uv(&m, 0), uv(&m, 0);
    for (int i = 0; i < ZUI_VEC_MAX_DEPTH + 1; i++) u8(&m, 0x20), uv(&m, 0), u8(&m, 255), u8(&m, 0);
    for (int i = 0; i < ZUI_VEC_MAX_DEPTH + 1; i++) u8(&m, 0x21);
    u8(&m, 0);
    CHECK(zui_zxvg_check(m.b, m.n) == -1, "groups nested too deep");
    head(&m, 10, 10);
    pal(&m, 0, P);
    uv(&m, 0), uv(&m, 0);
    u8(&m, 1), u8(&m, 0xFF), u8(&m, 0xFF), u8(&m, 0xFF), u8(&m, 0xFF), u8(&m, 0x7F), u8(&m, 0);
    u8(&m, 0);
    CHECK(zui_zxvg_check(m.b, m.n) == -1, "an overlong varint");
    head(&m, 10, 10);
    pal(&m, 0, P);
    uv(&m, 0), uv(&m, 0);
    u8(&m, 1), zz(&m, (1 << 23)), zz(&m, 0), u8(&m, 0);
    CHECK(zui_zxvg_check(m.b, m.n) == -1, "a coordinate out of range");
    head(&m, 10, 10);
    pal(&m, 0, P);
    uv(&m, 0);
    static const char *const D[2] = {"a", "a"};
    ids(&m, 2, D);
    u8(&m, 0);
    CHECK(zui_zxvg_check(m.b, m.n) == -1, "duplicate ids");
    static const char *const B[1] = {"a b"};
    head(&m, 10, 10);
    pal(&m, 0, P);
    uv(&m, 0);
    ids(&m, 1, B);
    u8(&m, 0);
    CHECK(zui_zxvg_check(m.b, m.n) == -1, "an id with a space");
    head(&m, 0, 10);
    pal(&m, 0, P);
    uv(&m, 0), uv(&m, 0), u8(&m, 0);
    CHECK(zui_zxvg_check(m.b, m.n) == -1, "an empty viewBox");
    head(&m, 10, 10);
    pal(&m, 1, P);
    uv(&m, 0), uv(&m, 0);
    rect_cmd(&m, 1, 1, 2, 2);
    u8(&m, 0x11), uv(&m, 0), u8(&m, 255), uv(&m, 16), u8(&m, 0), u8(&m, 0), uv(&m, 1024);
    uv(&m, 1), uv(&m, 16), zz(&m, 0), u8(&m, 0);
    CHECK(zui_zxvg_check(m.b, m.n) == -1, "an odd dash count");
}

/* fuzz: mutate the rich document, every result either checks and draws or is
 * refused and draws nothing (run under ASan + UBSan in CI). */
static uint32_t rng = 0x2545F491u;
static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}
#define FW 96
#define FH 80
static uint32_t FPX[FW * FH];
static zui_surface_t FS;
static void test_fuzz(void)
{
    zui_surface_init(&FS, FPX, FW, FH, FW);
    buf_t b, m;
    rich(&b);
    int ok = 0, refused = 0, bad = 0;
    zui_vec_group_info_t gi;
    for (int it = 0; it < 20000; it++) {
        m = b;
        uint32_t kind = rnd() % 6;
        uint32_t k = 1 + rnd() % 4;
        for (uint32_t j = 0; j < k; j++) {
            if (m.n == 0) break;
            uint32_t p = rnd() % m.n;
            if (kind == 0) m.b[p] ^= (uint8_t) (1u << (rnd() % 8));
            if (kind == 1) m.b[p] = (uint8_t) rnd();
            if (kind == 2 && m.n < sizeof m.b - 1) { /* insert */
                memmove(m.b + p + 1, m.b + p, m.n - p);
                m.b[p] = (uint8_t) rnd();
                m.n++;
            }
            if (kind == 3 && m.n > 1) { /* delete */
                memmove(m.b + p, m.b + p + 1, m.n - p - 1);
                m.n--;
            }
            if (kind == 4) m.n = p; /* truncate */
            if (kind == 5) m.b[p] = (uint8_t) (rnd() & 1 ? 0xFF : 0x80);
        }
        int rc = zui_zxvg_check(m.b, m.n);
        for (int i = 0; i < FW * FH; i++) FPX[i] = 0x123456u;
        int dr = zui_vec_draw_zxvg(&FS, &WK, m.b, m.n, (zui_rect_t){3, 5, 90, 70}, 0);
        if (rc == 0) {
            ok++;
            if (dr != 0) bad++;
            zui_zxvg_group(m.b, m.n, "chassis", &WK, &gi);
        } else {
            refused++;
            int same = 0;
            for (int i = 0; i < FW * FH; i++) same += FPX[i] == 0x123456u;
            if (dr != -1 || same != FW * FH) bad++;
        }
        if (kind == 4 && rc == 0) bad++; /* no prefix of a file is a file */
    }
    CHECK(bad == 0, "V8: fuzz: %d mutated files, %d accepted, %d refused, %d inconsistent",
          ok + refused, ok, refused, bad);
    /* random bytes behind a valid header */
    int acc = 0;
    for (int it = 0; it < 20000; it++) {
        head(&m, 50, 50);
        pal(&m, 2, (const uint32_t[]){0xFFFFFFFFu, 0x80FF0000u});
        uv(&m, 0), uv(&m, 0);
        uint32_t n = rnd() % 48;
        for (uint32_t j = 0; j < n; j++) u8(&m, rnd() % 3 ? rnd() % 0x22 : rnd());
        u8(&m, 0);
        if (zui_zxvg_check(m.b, m.n) == 0) {
            acc++;
            zui_vec_draw_zxvg(&FS, &WK, m.b, m.n, (zui_rect_t){0, 0, 64, 64}, 0);
        }
    }
    printf("  fuzz: %d mutated files (%d accepted, %d refused); %d of 20000 random streams drawn\n",
           ok + refused, ok, refused, acc);
    CHECK(acc > 0 && ok > 0 && refused > 0, "the fuzz reached both outcomes");
}

/* ---- the launch screen mark ----------------------------------------------- */
static zui_rect_t mark_box;
static int mark_calls;
static void rec_mark(zui_surface_t *s, zui_rect_t box, uint32_t lit, void *ctx)
{
    mark_box = box;
    mark_calls++;
    zui_vec_mark(s, box, lit, ctx);
}

static void test_mark(void)
{
    zui_vec_mark_t mk = {ZXVG_FIXTURE, ZXVG_FIXTURE_len, &WK, 64, 0, 0, 0, 0};
    zui_launch_t none, l;
    memset(&none, 0, sizeof none);
    memset(&l, 0, sizeof l);
    l.mark = rec_mark;
    l.ctx[0] = &mk;
    zui_rect_t area = {0, 0, W, H};
    clear(0);
    zui_launch_draw(&S, area, 256, &none, (zui_launch_state_t){0, 0, 0}, 0);
    memcpy(PX2, PX, sizeof PX);
    clear(0);
    zui_launch_draw(&S, area, 256, &l, (zui_launch_state_t){0, 0, 0}, 0);
    int diff = 0, outside = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (PX[y * W + x] != PX2[y * W + x]) {
                diff++;
                if (x < mark_box.x || y < mark_box.y || x >= mark_box.x + mark_box.w ||
                    y >= mark_box.y + mark_box.h)
                    outside++;
            }
    CHECK(mark_calls == 1 && diff > mark_box.w * mark_box.h / 8 && outside == 0,
          "the ZXVG mark draws in the launch mark box only (%d px, %d outside)", diff, outside);
    CHECK(S.clip.x == 0 && S.clip.w == W && S.clip.h == H, "and leaves the clip alone");
    uint32_t dim = at(mark_box.x + mark_box.w / 2, mark_box.y + mark_box.h / 2);
    clear(0);
    zui_launch_draw(&S, area, 256, &l, (zui_launch_state_t){256, 256, 0}, 0);
    uint32_t lit = at(mark_box.x + mark_box.w / 2, mark_box.y + mark_box.h / 2);
    CHECK((lit & 0xFF00) > (dim & 0xFF00), "lit is brighter than dim (%06x > %06x)", lit, dim);
    /* the mark at any size: the boot screen at 1x and 2x */
    clear(0);
    zui_vec_mark(&S, (zui_rect_t){0, 0, 64, 64}, 256, &mk);
    uint32_t h1 = fnv(PX, W * H);
    clear(0);
    zui_vec_mark(&S, (zui_rect_t){0, 0, 64, 64}, 256, &mk);
    CHECK(h1 == fnv(PX, W * H), "V2: the same mark draws the same pixels");
    CHECK(zui_zxvg_check(ZXVG_FIXTURE, ZXVG_FIXTURE_len) == 0 &&
              zui_zxvg_id_count(ZXVG_FIXTURE, ZXVG_FIXTURE_len) == 3,
          "the svg2zxvg.py fixture checks and keeps its ids");
    zui_vec_group_info_t gi;
    CHECK(zui_zxvg_group(ZXVG_FIXTURE, ZXVG_FIXTURE_len, "chassis", &WK, &gi) == 0 &&
              gi.x0 < U(-100) && gi.x1 > U(100) && abs(gi.px) <= 2 && abs(gi.py) <= 2,
          "its chassis is centred in its own units (%d %d)", gi.px, gi.py);
    mk.dim = 0;
    clear(0);
    zui_vec_mark(&S, (zui_rect_t){0, 0, 64, 64}, 0, &mk);
    CHECK(count_eq(0) == W * H, "dim 0 at lit 0 draws nothing");
}

/* V2: one scene hashed; the constant is the same on every compiler and host */
static void test_determinism(void)
{
    uint32_t h[2];
    for (int k = 0; k < 2; k++) {
        clear(0x002911u);
        zui_vec_draw_zxvg(&S, &WK, ZXVG_FIXTURE, ZXVG_FIXTURE_len, (zui_rect_t){0, 0, W, H}, 0);
        V.layer = LAYER;
        V.layer_cap = W * H;
        star(60, 190, 50);
        zui_vec_paint_t g = zui_vec_solid(0xC0F2C230u);
        zui_vec_fill(&V, &g, ZUI_VEC_EVENODD);
        zui_vec_stroke_t p = pen(3.5, ZUI_VEC_JOIN_ROUND, ZUI_VEC_CAP_ROUND);
        zui_vec_stroke(&V, white(), &p);
        h[k] = fnv(PX, W * H);
    }
    CHECK(h[0] == h[1], "the same scene twice is the same");
    printf("  scene hash %08x\n", h[0]);
    CHECK(h[0] == 0x822c1da3u, "V2: the scene hash is the reference (%08x)", h[0]);
}

int main(void)
{
    test_fill_exact();
    test_rules();
    test_strokes();
    test_paint();
    test_groups();
    test_caps();
    test_zxvg();
    test_simple();
    test_scale();
    test_malformed();
    test_fuzz();
    test_mark();
    test_determinism();
    printf("%s test_zui_vec: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails, checks);
    return fails ? 1 : 0;
}
