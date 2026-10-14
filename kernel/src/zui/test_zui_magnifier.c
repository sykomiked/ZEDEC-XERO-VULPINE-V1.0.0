/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_magnifier.c — host tests for screen zoom: quarter-step zoom,
 * following focus, caret and pointer, point mapping both ways, nearest and
 * bilinear sampling, the lens, colour filters and the keys. */
#include <stdio.h>
#include <string.h>

#include "zui_magnifier.h"
#include "zui_tokens.h"

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

#define SW 400
#define SH 200

static uint32_t src_px[SW * SH];
static uint32_t dst_px[SW * SH];
static zui_surface_t SRC, DST;

/* every source pixel distinct: x in the low bits, y above */
static uint32_t pat(int32_t x, int32_t y)
{
    return (uint32_t) (x & 0x1FF) | ((uint32_t) (y & 0xFF) << 12) | 0x100000u;
}

static void fill_pattern(void)
{
    zui_surface_init(&SRC, src_px, SW, SH, SW);
    zui_surface_init(&DST, dst_px, SW, SH, SW);
    for (int32_t y = 0; y < SH; y++)
        for (int32_t x = 0; x < SW; x++) src_px[y * SW + x] = pat(x, y);
    memset(dst_px, 0, sizeof dst_px);
}

static zui_rect_t R(int32_t x, int32_t y, int32_t w, int32_t h)
{
    zui_rect_t r = {x, y, w, h};
    return r;
}

static zui_event_t key(uint8_t type, uint16_t k, uint8_t mods)
{
    zui_event_t e;
    memset(&e, 0, sizeof e);
    e.type = type;
    e.key = k;
    e.mods = mods;
    return e;
}

/* ---- M2: zoom -------------------------------------------------------------- */

static void test_zoom(void)
{
    zui_magnifier_t m;
    zui_mag_init(&m, SW, SH);
    CHECK(m.mode == ZUI_MAG_FULL && m.zoom_q8 == 512 && m.sample == ZUI_MAG_NEAREST &&
              m.filter == ZUI_FILTER_NONE,
          "defaults: full, 2x, nearest, no filter");
    CHECK(m.follow == (ZUI_MAG_FOLLOW_FOCUS | ZUI_MAG_FOLLOW_CARET | ZUI_MAG_FOLLOW_POINTER),
          "follows everything by default");
    CHECK(m.lens_w == SW / 3 && m.lens_h == SH / 3, "a third of the screen for the lens");
    CHECK(zui_mag_set_zoom(&m, 300) == 320, "300 snaps to 1.25x (320)");
    CHECK(zui_mag_set_zoom(&m, 287) == 256, "287 snaps down to 1x (%d)", m.zoom_q8);
    CHECK(zui_mag_set_zoom(&m, 288) == 320, "288 rounds up to 1.25x");
    CHECK(zui_mag_set_zoom(&m, 1) == ZUI_MAG_MIN_Q8, "below 1x clamps");
    CHECK(zui_mag_set_zoom(&m, -500) == ZUI_MAG_MIN_Q8, "negative clamps");
    CHECK(zui_mag_set_zoom(&m, 100000) == ZUI_MAG_MAX_Q8, "above 16x clamps");
    CHECK(zui_mag_set_zoom(&m, 4096) == 4096, "16x exactly");
    CHECK(zui_mag_zoom_step(&m, 1) == 4096, "no step past 16x");
    CHECK(zui_mag_zoom_step(&m, -1) == 4032, "one quarter down");
    zui_mag_set_zoom(&m, 512);
    CHECK(zui_mag_zoom_step(&m, 1) == 576, "2x + a quarter");
    CHECK(zui_mag_zoom_step(&m, -100) == 256, "many steps down stop at 1x");
    CHECK(zui_mag_zoom_step(&m, 1000) == 4096, "many steps up stop at 16x");
    /* every quarter step from 1x to 16x is reachable */
    zui_mag_set_zoom(&m, 256);
    int n = 0;
    while (m.zoom_q8 < 4096 && n < 100) {
        zui_mag_zoom_step(&m, 1);
        n++;
    }
    CHECK(n == 60, "60 quarter steps from 1x to 16x (%d)", n);
    /* the centre is kept */
    zui_mag_set_zoom(&m, 256);
    zui_rect_t v = zui_mag_view(&m);
    CHECK(v.x == 0 && v.y == 0 && v.w == SW && v.h == SH, "1x shows everything");
    zui_mag_set_zoom(&m, 512);
    v = zui_mag_view(&m);
    CHECK(v.x == 100 && v.y == 50 && v.w == 200 && v.h == 100, "2x keeps the middle (%d,%d %dx%d)",
          v.x, v.y, v.w, v.h);
    zui_mag_set_zoom(&m, 1024);
    v = zui_mag_view(&m);
    CHECK(v.x == 150 && v.y == 75 && v.w == 100 && v.h == 50, "4x still the middle");
    zui_mag_center(&m, 0, 0);
    zui_mag_set_zoom(&m, 2048);
    v = zui_mag_view(&m);
    CHECK(v.x == 25 && v.y == 12 && v.w == 50 && v.h == 25, "8x keeps the 4x view's centre (%d,%d)",
          v.x, v.y);
    zui_mag_set_zoom(&m, 320);
    v = zui_mag_view(&m);
    CHECK(v.w == 320 && v.h == 160, "1.25x: 320 x 160 source pixels (%dx%d)", v.w, v.h);
    /* resize clamps the view */
    zui_mag_set_zoom(&m, 512);
    zui_mag_center(&m, SW - 1, SH - 1);
    zui_mag_resize(&m, 100, 50);
    v = zui_mag_view(&m);
    CHECK(v.x == 50 && v.y == 25 && v.w == 50 && v.h == 25, "resize keeps the view on screen");
    zui_mag_resize(&m, -5, -5);
    CHECK(m.screen_w == 0 && m.screen_h == 0 && m.lens_w == 1, "a negative size is empty");
}

static void test_center(void)
{
    zui_magnifier_t m;
    zui_mag_init(&m, SW, SH);
    zui_mag_center(&m, 200, 100);
    zui_rect_t v = zui_mag_view(&m);
    CHECK(v.x == 100 && v.y == 50, "centred on 200,100 (%d,%d)", v.x, v.y);
    zui_mag_center(&m, -50, -50);
    v = zui_mag_view(&m);
    CHECK(v.x == 0 && v.y == 0, "clamped at the top left");
    zui_mag_center(&m, 10000, 10000);
    v = zui_mag_view(&m);
    CHECK(v.x == 200 && v.y == 100 && v.x + v.w == SW, "clamped at the bottom right");
    m.mode = ZUI_MAG_LENS;
    zui_mag_center(&m, 30, 40);
    CHECK(m.lens_x == 30 && m.lens_y == 40, "lens: its point");
    zui_mag_center(&m, -3, 9999);
    CHECK(m.lens_x == 0 && m.lens_y == SH - 1, "lens point stays on screen");
}

/* ---- M3: following ----------------------------------------------------------- */

static void test_follow(void)
{
    zui_magnifier_t m;
    zui_mag_init(&m, SW, SH);
    zui_mag_center(&m, 0, 0); /* view 0,0 200x100 */
    CHECK(!zui_mag_follow_focus(&m, R(10, 10, 50, 20)), "focus in view: still");
    zui_rect_t v = zui_mag_view(&m);
    CHECK(v.x == 0 && v.y == 0, "did not move");
    CHECK(zui_mag_follow_focus(&m, R(250, 10, 40, 20)), "focus off to the right: moves");
    v = zui_mag_view(&m);
    CHECK(v.x == 170 && v.y == 0, "centred across, not down (%d,%d)", v.x, v.y);
    CHECK(zui_mag_follow_focus(&m, R(250, 150, 40, 20)), "focus below: moves");
    v = zui_mag_view(&m);
    CHECK(v.x == 170 && v.y == 100, "only down, and clamped (%d,%d)", v.x, v.y);
    /* partly visible counts as out of view */
    zui_mag_center(&m, 0, 0);
    CHECK(zui_mag_follow_focus(&m, R(190, 10, 20, 20)), "half out: centred");
    v = zui_mag_view(&m);
    CHECK(v.x == 100, "centred on 200 (%d)", v.x);
    /* larger than the view: its top left */
    zui_mag_center(&m, 399, 199);
    CHECK(zui_mag_follow_focus(&m, R(20, 30, 300, 150)), "a big pane");
    v = zui_mag_view(&m);
    CHECK(v.x == 20 && v.y == 30, "its top left (%d,%d)", v.x, v.y);
    /* empty rects still work */
    CHECK(zui_mag_follow_focus(&m, R(390, 190, 0, 0)), "a zero-size rect is a point");
    /* caret */
    zui_mag_center(&m, 0, 0);
    CHECK(!zui_mag_follow_caret(&m, R(100, 40, 2, 16)), "caret visible");
    CHECK(zui_mag_follow_caret(&m, R(250, 40, 2, 16)), "caret off screen");
    v = zui_mag_view(&m);
    CHECK(v.x == 151 && v.y == 0, "caret centred (%d,%d)", v.x, v.y);
    CHECK(zui_mag_follow_caret(&m, R(395, 40, 2, 16)), "caret at the far edge");
    v = zui_mag_view(&m);
    CHECK(v.x == 200, "centring stops at the screen edge (%d)", v.x);
    /* follow bits */
    m.follow = ZUI_MAG_FOLLOW_POINTER;
    zui_mag_center(&m, 0, 0);
    CHECK(!zui_mag_follow_focus(&m, R(390, 190, 5, 5)), "focus not followed");
    CHECK(!zui_mag_follow_caret(&m, R(390, 190, 2, 5)), "caret not followed");
    m.follow = 0;
    CHECK(!zui_mag_follow_pointer(&m, 399, 199), "pointer not followed");
    m.follow = ZUI_MAG_FOLLOW_POINTER;
    /* pointer: pushes only at an edge */
    CHECK(!zui_mag_follow_pointer(&m, 100, 50), "pointer inside: still");
    CHECK(!zui_mag_follow_pointer(&m, 199, 99), "on the last pixel: still");
    CHECK(zui_mag_follow_pointer(&m, 209, 99), "ten past the edge");
    v = zui_mag_view(&m);
    CHECK(v.x == 10 && v.y == 0, "pushed by ten (%d,%d)", v.x, v.y);
    CHECK(zui_mag_follow_pointer(&m, 5, 120), "left and down");
    v = zui_mag_view(&m);
    CHECK(v.x == 5 && v.y == 21, "pushed both ways (%d,%d)", v.x, v.y);
    CHECK(zui_mag_follow_pointer(&m, -100, -100), "off screen");
    v = zui_mag_view(&m);
    CHECK(v.x == 0 && v.y == 0, "clamped");
    /* lens follows the point itself */
    m.mode = ZUI_MAG_LENS;
    m.follow = ZUI_MAG_FOLLOW_FOCUS | ZUI_MAG_FOLLOW_POINTER;
    CHECK(zui_mag_follow_pointer(&m, 77, 66) && m.lens_x == 77 && m.lens_y == 66,
          "lens on pointer");
    CHECK(!zui_mag_follow_pointer(&m, 77, 66), "same point: no move");
    CHECK(zui_mag_follow_focus(&m, R(100, 100, 40, 20)) && m.lens_x == 120 && m.lens_y == 110,
          "lens on the focus centre");
}

/* ---- M6: mapping ----------------------------------------------------------------- */

static void test_mapping(void)
{
    zui_magnifier_t m;
    zui_mag_init(&m, SW, SH);
    zui_mag_center(&m, 200, 100); /* 2x, view at 100,50 */
    int32_t x = 0, y = 0;
    CHECK(zui_mag_to_source(&m, 0, 0, &x, &y) && x == 100 && y == 50, "screen 0,0 is 100,50");
    CHECK(zui_mag_to_source(&m, 1, 1, &x, &y) && x == 100 && y == 50, "a 2x2 block");
    CHECK(zui_mag_to_source(&m, 2, 3, &x, &y) && x == 101 && y == 51, "the next pixel");
    CHECK(zui_mag_to_source(&m, SW - 1, SH - 1, &x, &y) && x == 299 && y == 149, "far corner");
    CHECK(!zui_mag_to_source(&m, SW, 0, &x, &y), "past the screen");
    CHECK(!zui_mag_to_source(&m, -1, 0, &x, &y), "before the screen");
    CHECK(zui_mag_to_screen(&m, 100, 50, &x, &y) && x == 1 && y == 1, "100,50 at 1,1 (%d,%d)", x,
          y);
    CHECK(!zui_mag_to_screen(&m, 99, 50, &x, &y), "left of the view is not shown");
    CHECK(!zui_mag_to_screen(&m, 300, 100, &x, &y), "right of the view is not shown");
    /* round trip at every quarter step */
    bool ok = true;
    for (int32_t z = 256; z <= 4096 && ok; z += 64) {
        zui_mag_set_zoom(&m, z);
        zui_mag_center(&m, 123, 77);
        zui_rect_t v = zui_mag_view(&m);
        for (int32_t py = v.y; py < v.y + v.h && ok; py += 3)
            for (int32_t px = v.x; px < v.x + v.w && ok; px += 3) {
                int32_t sx, sy, bx, by;
                if (!zui_mag_to_screen(&m, px, py, &sx, &sy)) continue; /* a cut edge pixel */
                if (!zui_mag_to_source(&m, sx, sy, &bx, &by) || bx != px || by != py) {
                    ok = false;
                    printf("    zoom %d: %d,%d -> %d,%d -> %d,%d\n", z, px, py, sx, sy, bx, by);
                }
            }
    }
    CHECK(ok, "source -> screen -> source at every zoom");
    /* every screen pixel maps into the view */
    zui_mag_set_zoom(&m, 448); /* 1.75x */
    zui_mag_center(&m, 50, 50);
    zui_rect_t v = zui_mag_view(&m);
    ok = true;
    for (int32_t sy = 0; sy < SH; sy += 7)
        for (int32_t sx = 0; sx < SW; sx += 7) {
            int32_t bx, by;
            if (!zui_mag_to_source(&m, sx, sy, &bx, &by) || bx < v.x || by < v.y ||
                bx >= v.x + v.w || by >= v.y + v.h)
                ok = false;
        }
    CHECK(ok, "screen -> source lands in the view");
    /* lens */
    m.mode = ZUI_MAG_LENS;
    zui_mag_set_zoom(&m, 512);
    zui_mag_center(&m, 200, 100); /* lens 133x66 centred on 200,100 */
    CHECK(zui_mag_to_source(&m, 5, 5, &x, &y) && x == 5 && y == 5, "outside the lens: 1x");
    CHECK(zui_mag_to_source(&m, 200, 100, &x, &y) && x == 200 && y == 100,
          "the lens centre is its point");
    CHECK(zui_mag_to_source(&m, 210, 100, &x, &y) && x == 205, "10 px right in the lens is 5 (%d)",
          x);
    CHECK(zui_mag_to_source(&m, 190, 90, &x, &y) && x == 195 && y == 95, "and left / up");
    CHECK(zui_mag_to_screen(&m, 205, 100, &x, &y) && x == 210 && y == 100, "back again (%d,%d)", x,
          y);
    CHECK(zui_mag_to_screen(&m, 5, 5, &x, &y) && x == 5 && y == 5, "far away: at 1x");
    CHECK(!zui_mag_to_screen(&m, 140, 100, &x, &y), "covered by the lens: not shown");
    CHECK(!zui_mag_to_screen(&m, -1, 5, &x, &y), "off screen");
    /* lens at a corner stays whole */
    zui_mag_center(&m, 0, 0);
    CHECK(zui_mag_to_source(&m, 0, 0, &x, &y) && x == 0 && y == 0, "corner lens");
}

/* ---- M4: rendering ------------------------------------------------------------- */

static bool same_as_src(void)
{
    for (int32_t i = 0; i < SW * SH; i++)
        if (dst_px[i] != src_px[i]) return false;
    return true;
}

static void test_render_full(void)
{
    zui_magnifier_t m;
    fill_pattern();
    zui_mag_init(&m, SW, SH);
    zui_mag_set_zoom(&m, 256);
    CHECK(zui_mag_render(&m, &SRC, &DST) && same_as_src(), "1x nearest is an exact copy");
    m.sample = ZUI_MAG_BILINEAR;
    memset(dst_px, 0, sizeof dst_px);
    CHECK(zui_mag_render(&m, &SRC, &DST) && same_as_src(), "1x bilinear is an exact copy");
    /* 2x nearest: each source pixel is a 2x2 block */
    m.sample = ZUI_MAG_NEAREST;
    zui_mag_set_zoom(&m, 512); /* view 100,50 */
    zui_mag_render(&m, &SRC, &DST);
    bool ok = true;
    for (int32_t y = 0; y < SH; y++)
        for (int32_t x = 0; x < SW; x++)
            if (dst_px[y * SW + x] != pat(100 + x / 2, 50 + y / 2)) ok = false;
    CHECK(ok, "2x nearest: blocks of the viewport");
    /* 4x at the corner */
    zui_mag_set_zoom(&m, 1024);
    zui_mag_center(&m, 0, 0);
    zui_mag_render(&m, &SRC, &DST);
    ok = true;
    for (int32_t y = 0; y < SH; y++)
        for (int32_t x = 0; x < SW; x++)
            if (dst_px[y * SW + x] != pat(x / 4, y / 4)) ok = false;
    CHECK(ok, "4x nearest from the corner");
    /* 1.5x: each source pixel is 1 or 2 wide, never 0 or 3 */
    zui_mag_set_zoom(&m, 384);
    zui_mag_center(&m, 0, 0);
    zui_mag_render(&m, &SRC, &DST);
    int run = 1, worst_lo = 99, worst_hi = 0;
    for (int32_t x = 1; x < SW; x++) {
        if (dst_px[x] == dst_px[x - 1]) {
            run++;
        } else {
            if (run < worst_lo) worst_lo = run;
            if (run > worst_hi) worst_hi = run;
            CHECK((dst_px[x] & 0x1FF) == (dst_px[x - 1] & 0x1FF) + 1, "1.5x skips no pixel at %d",
                  x);
            run = 1;
        }
    }
    CHECK(worst_lo >= 1 && worst_hi <= 2, "1.5x runs of 1 or 2 (%d..%d)", worst_lo, worst_hi);

    /* bilinear between a black and a white column */
    for (int32_t y = 0; y < SH; y++)
        for (int32_t x = 0; x < SW; x++) src_px[y * SW + x] = x == 0 ? 0 : 0xFFFFFFu;
    m.sample = ZUI_MAG_BILINEAR;
    zui_mag_set_zoom(&m, 512);
    zui_mag_center(&m, 0, 0);
    zui_mag_render(&m, &SRC, &DST);
    CHECK(dst_px[0] == 0, "edge clamps to black (%06X)", dst_px[0]);
    CHECK(dst_px[1] == 0x404040u, "a quarter of the way: 64 (%06X)", dst_px[1]);
    CHECK(dst_px[2] == 0xBFBFBFu, "three quarters: 191 (%06X)", dst_px[2]);
    CHECK(dst_px[3] == 0xFFFFFFu, "then white");
    m.sample = ZUI_MAG_NEAREST;
    zui_mag_render(&m, &SRC, &DST);
    CHECK(dst_px[1] == 0 && dst_px[2] == 0xFFFFFFu, "nearest keeps the edge hard");
    /* a flat colour stays flat under bilinear at any zoom */
    for (int32_t i = 0; i < SW * SH; i++) src_px[i] = ZUI_VOID;
    m.sample = ZUI_MAG_BILINEAR;
    ok = true;
    for (int32_t z = 256; z <= 4096; z += 320) {
        zui_mag_set_zoom(&m, z);
        zui_mag_center(&m, 77, 33);
        zui_mag_render(&m, &SRC, &DST);
        for (int32_t i = 0; i < SW * SH; i += 97)
            if (dst_px[i] != ZUI_VOID) ok = false;
    }
    CHECK(ok, "flat stays flat");
    /* the X byte never leaks */
    for (int32_t i = 0; i < SW * SH; i++) src_px[i] = 0xAB123456u;
    m.sample = ZUI_MAG_NEAREST;
    zui_mag_render(&m, &SRC, &DST);
    CHECK(dst_px[5] == 0x123456u, "nearest clears the X byte");
    m.sample = ZUI_MAG_BILINEAR;
    zui_mag_render(&m, &SRC, &DST);
    CHECK(dst_px[5] == 0x123456u, "bilinear too");
}

static void test_render_lens(void)
{
    zui_magnifier_t m;
    fill_pattern();
    zui_mag_init(&m, SW, SH);
    m.mode = ZUI_MAG_LENS;
    m.lens_w = 40;
    m.lens_h = 20;
    zui_mag_set_zoom(&m, 512);
    zui_mag_center(&m, 100, 100); /* lens 80,90 .. 120,110 */
    CHECK(zui_mag_render(&m, &SRC, &DST), "lens renders");
    bool outside = true;
    for (int32_t y = 0; y < SH; y++)
        for (int32_t x = 0; x < SW; x++) {
            bool in = x >= 80 && x < 120 && y >= 90 && y < 110;
            if (!in && dst_px[y * SW + x] != pat(x, y)) outside = false;
        }
    CHECK(outside, "outside the lens: the 1x screen");
    CHECK(dst_px[90 * SW + 80] == ZUI_GOLD && dst_px[91 * SW + 81] == ZUI_GOLD &&
              dst_px[109 * SW + 119] == ZUI_GOLD,
          "a gold ring two pixels wide");
    CHECK(dst_px[92 * SW + 82] != ZUI_GOLD, "inside the ring is the view");
    CHECK(dst_px[100 * SW + 100] == pat(100, 100), "the centre shows the point");
    CHECK(dst_px[100 * SW + 99] == pat(100, 100), "the point's pixel is two wide");
    CHECK(dst_px[100 * SW + 101] == pat(101, 100) && dst_px[100 * SW + 102] == pat(101, 100),
          "2x around it");
    CHECK(dst_px[100 * SW + 103] == pat(102, 100), "then the next");
    CHECK(dst_px[100 * SW + 98] == pat(99, 100), "and to the left");
    /* no ring */
    m.ring_px = 0;
    zui_mag_render(&m, &SRC, &DST);
    CHECK(dst_px[90 * SW + 80] == pat(90, 95), "ring off: the view reaches the edge (%05X)",
          dst_px[90 * SW + 80]);
    /* at 1x the lens is invisible apart from its ring */
    zui_mag_set_zoom(&m, 256);
    zui_mag_render(&m, &SRC, &DST);
    CHECK(same_as_src(), "1x lens without a ring changes nothing");
    /* the filter applies inside the lens only */
    m.filter = ZUI_FILTER_INVERT;
    zui_mag_render(&m, &SRC, &DST);
    CHECK(dst_px[100 * SW + 100] == (pat(100, 100) ^ 0xFFFFFFu), "inverted inside");
    CHECK(dst_px[10 * SW + 10] == pat(10, 10), "not outside");
    /* a lens at the corner is pushed on screen */
    m.filter = ZUI_FILTER_NONE;
    m.ring_px = 1;
    zui_mag_center(&m, 0, 0);
    zui_mag_render(&m, &SRC, &DST);
    CHECK(dst_px[0] == ZUI_GOLD && dst_px[19 * SW + 39] == ZUI_GOLD &&
              dst_px[20 * SW + 40] == pat(40, 20),
          "the lens stays whole at the corner");
}

static void test_render_guards(void)
{
    zui_magnifier_t m;
    fill_pattern();
    zui_mag_init(&m, SW, SH);
    CHECK(!zui_mag_render(&m, &SRC, &SRC), "the same surface is refused");
    zui_surface_t half;
    zui_surface_init(&half, src_px + SW * 50, SW, 100, SW);
    CHECK(!zui_mag_render(&m, &SRC, &half), "overlapping pixels are refused");
    CHECK(!zui_mag_render(&m, 0, &DST) && !zui_mag_render(&m, &SRC, 0) &&
              !zui_mag_render(0, &SRC, &DST),
          "NULLs are refused");
    zui_surface_t empty;
    zui_surface_init(&empty, dst_px, 0, 0, 0);
    CHECK(!zui_mag_render(&m, &SRC, &empty), "an empty surface is refused");
    /* the destination clip is respected */
    for (int32_t i = 0; i < SW * SH; i++) dst_px[i] = 0x777777u;
    DST.clip = R(10, 10, 20, 20);
    zui_mag_set_zoom(&m, 256);
    CHECK(zui_mag_render(&m, &SRC, &DST), "clipped render");
    CHECK(dst_px[0] == 0x777777u && dst_px[9 * SW + 15] == 0x777777u &&
              dst_px[30 * SW + 15] == 0x777777u,
          "outside the clip untouched");
    CHECK(dst_px[10 * SW + 10] == pat(10, 10) && dst_px[29 * SW + 29] == pat(29, 29),
          "inside the clip drawn");
    DST.clip = R(-50, -50, 10000, 10000);
    CHECK(zui_mag_render(&m, &SRC, &DST) && same_as_src(), "a huge clip is cut to the surface");
    /* a smaller destination (a window) */
    zui_surface_t small;
    static uint32_t small_px[50 * 30];
    zui_surface_init(&small, small_px, 50, 30, 50);
    zui_mag_set_zoom(&m, 512);
    zui_mag_center(&m, 0, 0);
    CHECK(zui_mag_render(&m, &SRC, &small) && small_px[0] == pat(0, 0) &&
              small_px[29 * 50 + 49] == pat(24, 14),
          "renders into a smaller surface");
    /* a stride wider than the width */
    static uint32_t wide_px[64 * 8];
    zui_surface_t wide;
    zui_surface_init(&wide, wide_px, 60, 8, 64);
    for (int32_t i = 0; i < 64 * 8; i++) wide_px[i] = 0xDEADu;
    zui_mag_set_zoom(&m, 256);
    zui_mag_render(&m, &SRC, &wide);
    CHECK(wide_px[60] == 0xDEADu && wide_px[63] == 0xDEADu && wide_px[64] == pat(0, 1),
          "stride padding untouched");
}

/* ---- M5: filters -------------------------------------------------------------- */

static int dist(uint32_t a, uint32_t b)
{
    int d = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        int x = (int) ((a >> sh) & 255u) - (int) ((b >> sh) & 255u);
        d += x < 0 ? -x : x;
    }
    return d;
}

static void test_filters(void)
{
    CHECK(zui_filter_px(ZUI_FILTER_NONE, 0x123456u) == 0x123456u, "none");
    CHECK(zui_filter_px(ZUI_FILTER_NONE, 0xFF123456u) == 0x123456u, "none clears the X byte");
    CHECK(zui_filter_px(ZUI_FILTER_INVERT, ZUI_VOID) == 0xFFD6EEu, "invert the ground");
    CHECK(zui_filter_px(ZUI_FILTER_INVERT, zui_filter_px(ZUI_FILTER_INVERT, ZUI_GOLD)) == ZUI_GOLD,
          "invert twice");
    CHECK(zui_filter_px(ZUI_FILTER_INVERT, 0xFF000000u) == 0xFFFFFFu, "invert ignores X");
    CHECK(zui_filter_px(ZUI_FILTER_GRAYSCALE, 0xFFFFFFu) == 0xFFFFFFu, "white stays white");
    CHECK(zui_filter_px(ZUI_FILTER_GRAYSCALE, 0) == 0, "black stays black");
    CHECK(zui_filter_px(ZUI_FILTER_GRAYSCALE, 0x00FF00u) == 0xB6B6B6u, "green is a light grey");
    CHECK(zui_filter_px(ZUI_FILTER_GRAYSCALE, 0x0000FFu) == 0x131313u, "blue is a dark grey");
    uint32_t g = zui_filter_px(ZUI_FILTER_GRAYSCALE, ZUI_GOLD);
    CHECK(((g >> 16) & 255u) == (g & 255u) && ((g >> 8) & 255u) == (g & 255u), "gold goes grey");
    bool grey_ok[ZUI_FILTER_COUNT] = {true, true, true, true, true, true};
    for (int f = ZUI_FILTER_GRAYSCALE; f < ZUI_FILTER_COUNT; f++)
        for (uint32_t v = 0; v < 256; v++)
            if (zui_filter_px((zui_filter_t) f, v * 0x010101u) != v * 0x010101u) grey_ok[f] = false;
    CHECK(grey_ok[ZUI_FILTER_GRAYSCALE], "grayscale keeps greys");
    CHECK(grey_ok[ZUI_FILTER_PROTAN], "protan keeps all 256 greys (rows sum to 1)");
    CHECK(grey_ok[ZUI_FILTER_DEUTAN], "deutan keeps all 256 greys");
    CHECK(grey_ok[ZUI_FILTER_TRITAN], "tritan keeps all 256 greys");
    /* red and green run together for protan and deutan */
    uint32_t red = 0xE03030u, green = 0x30A030u;
    int d0 = dist(red, green);
    int dp = dist(zui_filter_px(ZUI_FILTER_PROTAN, red), zui_filter_px(ZUI_FILTER_PROTAN, green));
    int dd = dist(zui_filter_px(ZUI_FILTER_DEUTAN, red), zui_filter_px(ZUI_FILTER_DEUTAN, green));
    int dt = dist(zui_filter_px(ZUI_FILTER_TRITAN, red), zui_filter_px(ZUI_FILTER_TRITAN, green));
    CHECK(dp * 2 < d0, "protan: red and green close up (%d -> %d)", d0, dp);
    CHECK(dd * 2 < d0, "deutan: red and green close up (%d -> %d)", d0, dd);
    CHECK(dt * 3 > d0 * 2, "tritan: red and green stay apart (%d -> %d)", d0, dt);
    /* protan darkens red; tritan gives yellow some blue and blue some green */
    uint32_t pr = zui_filter_px(ZUI_FILTER_PROTAN, 0xFF0000u);
    uint32_t dr = zui_filter_px(ZUI_FILTER_DEUTAN, 0xFF0000u);
    CHECK(((pr >> 16) & 255u) < ((dr >> 16) & 255u), "protan red is darker than deutan red");
    uint32_t ty = zui_filter_px(ZUI_FILTER_TRITAN, 0xFFFF00u);
    CHECK((ty & 255u) > 128u, "tritan: yellow takes on blue (%06X)", ty);
    uint32_t tb = zui_filter_px(ZUI_FILTER_TRITAN, 0x0000FFu);
    CHECK(((tb >> 8) & 255u) > 64u, "tritan: blue takes on green (%06X)", tb);
    for (int f = 0; f < ZUI_FILTER_COUNT; f++)
        CHECK(zui_filter_px((zui_filter_t) f, 0xFFFFFFu) ==
                  (f == ZUI_FILTER_INVERT ? 0u : 0xFFFFFFu),
              "white under %s", zui_filter_name((zui_filter_t) f));
    CHECK(zui_filter_px((zui_filter_t) 42, 0x123456u) == 0x123456u, "an unknown filter is none");
    /* deterministic: same input, same output */
    CHECK(zui_filter_px(ZUI_FILTER_DEUTAN, ZUI_GOLD) == zui_filter_px(ZUI_FILTER_DEUTAN, ZUI_GOLD),
          "deterministic");
    CHECK(strcmp(zui_filter_name(ZUI_FILTER_PROTAN), "protan") == 0 &&
              strcmp(zui_filter_name(ZUI_FILTER_TRITAN), "tritan") == 0 &&
              strcmp(zui_filter_name((zui_filter_t) 9), "?") == 0,
          "names");
    /* in place on a rectangle, clipped */
    fill_pattern();
    for (int32_t i = 0; i < SW * SH; i++) src_px[i] = 0x102030u;
    zui_filter_rect(&SRC, R(-5, -5, 10, 10), ZUI_FILTER_INVERT);
    CHECK(src_px[0] == 0xEFDFCFu && src_px[4 * SW + 4] == 0xEFDFCFu, "inside inverted");
    CHECK(src_px[5] == 0x102030u && src_px[5 * SW] == 0x102030u, "outside not");
    SRC.clip = R(100, 100, 2, 2);
    zui_filter_rect(&SRC, R(0, 0, SW, SH), ZUI_FILTER_GRAYSCALE);
    CHECK(src_px[100 * SW + 100] != 0x102030u && src_px[99 * SW + 100] == 0x102030u &&
              src_px[102 * SW + 101] == 0x102030u,
          "the surface clip limits it");
    zui_filter_rect(&SRC, R(0, 0, SW, SH), ZUI_FILTER_NONE);
    zui_filter_rect(0, R(0, 0, 1, 1), ZUI_FILTER_INVERT);
    CHECK(true, "none and NULL are no-ops");
    /* full-screen render with each filter equals the per-pixel filter */
    fill_pattern();
    zui_magnifier_t m;
    zui_mag_init(&m, SW, SH);
    zui_mag_set_zoom(&m, 256);
    bool ok = true;
    for (int f = 0; f < ZUI_FILTER_COUNT; f++) {
        m.filter = (zui_filter_t) f;
        zui_mag_render(&m, &SRC, &DST);
        for (int32_t i = 0; i < SW * SH; i += 31)
            if (dst_px[i] != zui_filter_px((zui_filter_t) f, src_px[i])) ok = false;
    }
    CHECK(ok, "render applies the filter to every pixel");
}

/* ---- M7: keys ----------------------------------------------------------------- */

static void test_keys(void)
{
    zui_magnifier_t m;
    zui_mag_init(&m, SW, SH);
    uint8_t ma = ZUI_MOD_META | ZUI_MOD_ALT;
    zui_event_t e = key(ZUI_EV_KEY_DOWN, '=', ma);
    CHECK(zui_mag_event(&m, &e) && m.zoom_q8 == 576, "Meta+Alt+= zooms in a quarter");
    e.key = '+';
    CHECK(zui_mag_event(&m, &e) && m.zoom_q8 == 640, "+ too");
    e.key = '-';
    CHECK(zui_mag_event(&m, &e) && m.zoom_q8 == 576, "- zooms out");
    e.key = '0';
    CHECK(zui_mag_event(&m, &e) && m.zoom_q8 == 256, "0 is 1x");
    e.key = 'm';
    CHECK(zui_mag_event(&m, &e) && m.mode == ZUI_MAG_LENS, "m: lens");
    CHECK(zui_mag_event(&m, &e) && m.mode == ZUI_MAG_FULL, "m: full again");
    e.key = 'i';
    CHECK(zui_mag_event(&m, &e) && m.filter == ZUI_FILTER_INVERT, "i: invert");
    CHECK(zui_mag_event(&m, &e) && m.filter == ZUI_FILTER_NONE, "i: off");
    e.key = 'f';
    int seen = 0;
    for (int i = 0; i < ZUI_FILTER_COUNT; i++) {
        zui_mag_event(&m, &e);
        seen |= 1 << m.filter;
    }
    CHECK(seen == (1 << ZUI_FILTER_COUNT) - 1 && m.filter == ZUI_FILTER_NONE,
          "f cycles every filter and back");
    e = key(ZUI_EV_KEY_UP, '=', ma);
    int32_t z = m.zoom_q8;
    CHECK(zui_mag_event(&m, &e) && m.zoom_q8 == z, "key up is swallowed, does nothing");
    e = key(ZUI_EV_KEY_DOWN, '=', ma | ZUI_MOD_CMD);
    CHECK(zui_mag_event(&m, &e), "the shortcut bit alongside is fine");
    e = key(ZUI_EV_KEY_DOWN, '=', ma | ZUI_MOD_CTRL);
    CHECK(!zui_mag_event(&m, &e), "with Ctrl it is not ours (the reader's)");
    e = key(ZUI_EV_KEY_DOWN, '=', ZUI_MOD_ALT);
    CHECK(!zui_mag_event(&m, &e), "Alt alone is not ours");
    e = key(ZUI_EV_KEY_DOWN, '=', 0);
    CHECK(!zui_mag_event(&m, &e), "plain = types");
    e = key(ZUI_EV_KEY_DOWN, 'x', ma);
    CHECK(!zui_mag_event(&m, &e), "an unbound key passes");
    e = key(ZUI_EV_PTR_MOVE, 0, ma);
    CHECK(!zui_mag_event(&m, &e), "not a key");
    CHECK(!zui_mag_event(&m, 0) && !zui_mag_event(0, &e), "NULLs");
}

int main(void)
{
    test_zoom();
    test_center();
    test_follow();
    test_mapping();
    test_render_full();
    test_render_lens();
    test_render_guards();
    test_filters();
    test_keys();
    printf("%s test_zui_magnifier: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}
