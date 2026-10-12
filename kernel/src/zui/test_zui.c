/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui.c — host tests for the zui renderer core: size classes, shapes,
 * text plumbing, and the depth effects' contracts (flat stays flat, held sits
 * at the glass, S+ and S- mirror, anaglyph of agreeing eyes is the picture). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zui_layout.h"
#include "zui_canvas.h"
#include "zui_depth.h"
#include "../font/ttf_fixture.inc"   /* MINI_TTF[] */
#include "zui_ttf_fixture.inc"       /* ZUI_TTF[]: metrics + a composite */

static int fails, checks;
#define CHECK(c, m) do { checks++; if (!(c)) { fails++; \
    printf("  FAIL %s (%s:%d)\n", m, __FILE__, __LINE__); } } while (0)

static int iabs(int v) { return v < 0 ? -v : v; }

static void test_layout(void) {
    zui_screen_t phone = { 1170, 2532, 768, ZUI_INPUT_TOUCH };   /* 390 pt @3x */
    zui_screen_t tablet = { 1640, 2360, 512, ZUI_INPUT_TOUCH };  /* 820 pt @2x */
    zui_screen_t laptop = { 2880, 1800, 512, ZUI_INPUT_POINTER };/* 1440 pt   */
    zui_screen_t big = { 3840, 2160, 256, ZUI_INPUT_POINTER };   /* 3840 pt   */
    CHECK(zui_class_for(&phone) == ZUI_COMPACT, "390 pt phone is compact");
    CHECK(zui_class_for(&tablet) == ZUI_MEDIUM, "820 pt tablet is medium");
    CHECK(zui_class_for(&laptop) == ZUI_EXPANDED, "1440 pt laptop is expanded");
    CHECK(zui_class_for(&big) == ZUI_LARGE, "4K at 1x is large");

    zui_screen_t edge = { 599, 800, 256, ZUI_INPUT_POINTER };
    CHECK(zui_class_for(&edge) == ZUI_COMPACT, "599 pt is still compact");
    edge.px_w = 600;
    CHECK(zui_class_for(&edge) == ZUI_MEDIUM, "600 pt is medium");
    edge.px_w = 1600;
    CHECK(zui_class_for(&edge) == ZUI_LARGE, "1600 pt is large");

    zui_plan_t p;
    zui_plan(&phone, &p);
    CHECK(p.bottom.w == 1170 && p.rail.w == 0 && p.list.w == 0, "phone: bottom bar, no rail");
    CHECK(p.main.y + p.main.h == p.bottom.y, "phone: main ends at bottom bar");
    CHECK(p.target_px == 44 * 3, "touch target 44 pt at 3x");
    zui_plan(&laptop, &p);
    CHECK(p.rail.w > 0 && p.list.w > 0 && p.inspector.w == 0, "laptop: rail + list, no inspector");
    CHECK(p.main.x + p.main.w == 2880, "laptop: main reaches the right edge");
    CHECK(p.target_px == 28 * 2, "pointer target 28 pt at 2x");
    zui_plan(&big, &p);
    CHECK(p.inspector.w > 0 && p.inspector.x + p.inspector.w == 3840, "large: inspector at right edge");
    CHECK(p.main.w > 0 && p.list.x + p.list.w == p.main.x, "large: panes abut");

    zui_screen_t zero = { 100, 100, 0, ZUI_INPUT_POINTER };
    CHECK(zui_class_for(&zero) == ZUI_COMPACT, "zero scale does not divide by zero");
}

static uint32_t buf[64 * 64], buf2[64 * 64], buf3[64 * 64];

static void test_shapes(void) {
    zui_surface_t s;
    zui_surface_init(&s, buf, 64, 64, 64);
    zui_fill(&s, (zui_rect_t){ 0, 0, 64, 64 }, ZUI_VOID);
    zui_fill_rrect(&s, (zui_rect_t){ 8, 8, 48, 48 }, 10, ZUI_GOLD, 255);
    CHECK(buf[32 * 64 + 32] == ZUI_GOLD, "rrect centre is solid");
    CHECK(buf[8 * 64 + 8] == ZUI_VOID, "rrect corner pixel is outside the arc");
    int partial = 0;
    for (int y = 8; y < 18; y++)
        for (int x = 8; x < 18; x++)
            if (buf[y * 64 + x] != ZUI_VOID && buf[y * 64 + x] != ZUI_GOLD) partial++;
    CHECK(partial >= 8, "corner arc is anti-aliased");
    CHECK(buf[8 * 64 + 32] == ZUI_GOLD && buf[7 * 64 + 32] == ZUI_VOID, "straight edges are crisp");

    zui_fill(&s, (zui_rect_t){ 0, 0, 64, 64 }, ZUI_VOID);
    zui_stroke_rrect(&s, (zui_rect_t){ 4, 4, 56, 56 }, 8, 2, ZUI_EDGE, 255);
    CHECK(buf[32 * 64 + 4] == ZUI_EDGE && buf[32 * 64 + 5] == ZUI_EDGE, "stroke is 2 px");
    CHECK(buf[32 * 64 + 6] == ZUI_VOID, "stroke interior is untouched");

    zui_fill(&s, (zui_rect_t){ 0, 0, 64, 64 }, ZUI_VOID);
    zui_clip(&s, (zui_rect_t){ 0, 0, 32, 64 });
    zui_fill(&s, (zui_rect_t){ 0, 0, 64, 64 }, ZUI_TEXT);
    CHECK(buf[10 * 64 + 31] == ZUI_TEXT && buf[10 * 64 + 32] == ZUI_VOID, "clip holds");
    zui_unclip(&s);
    zui_fill(&s, (zui_rect_t){ -10, -10, 1000, 1000 }, ZUI_PANEL);
    CHECK(buf[63 * 64 + 63] == ZUI_PANEL, "oversized fill is clamped, not overrun");

    zui_fill(&s, (zui_rect_t){ 0, 0, 64, 64 }, ZUI_VOID);
    zui_dash_rrect(&s, (zui_rect_t){ 2, 2, 60, 40 }, 6, 1, 4, ZUI_GOLD);
    int on = 0, off = 0;
    for (int x = 10; x < 54; x++) { if (buf[2 * 64 + x] == ZUI_GOLD) on++; else off++; }
    CHECK(on > 10 && off > 10, "dashed edge alternates");

    CHECK(zui_mix(0x000000, 0xFFFFFF, 0) == 0x000000, "mix t=0");
    CHECK(zui_mix(0x000000, 0xFFFFFF, 255) == 0xFFFFFF, "mix t=255");
    CHECK(zui_mix(0x000000, 0xFFFFFF, 1000) == 0xFFFFFF, "mix clamps t");
}

static void test_text_plumbing(void) {
    static zui_glyph_cache_t cache;
    ttf_font_t f;
    CHECK(ttf_parse(&f, MINI_TTF, sizeof MINI_TTF), "fixture parses");
    CHECK(ttf_advance(&f, 1) == 0, "no hmtx -> advance 0, not garbage");
    uint8_t a[24 * 24], b[24 * 24];
    bool ra = ttf_render(&f, 1, 20, a, 24, 24);
    bool rb = ttf_render_at(&f, 1, 20, b, 24, 24, 0, 24);
    CHECK(ra == rb && memcmp(a, b, sizeof a) == 0, "ttf_render == ttf_render_at(origin 0, h)");
    if (ra) {
        uint8_t c[24 * 24];
        ttf_render_at(&f, 1, 20, c, 24, 24, 4 * 256, 24);
        int moved = memcmp(a, c, sizeof a) != 0;
        CHECK(moved, "pen origin moves the glyph");
    }
    /* metrics + composite glyphs */
    ttf_font_t m;
    CHECK(ttf_parse(&m, ZUI_TTF, sizeof ZUI_TTF), "metrics fixture parses");
    CHECK(ttf_advance(&m, 1) == 600 && ttf_advance(&m, 2) == 600, "hmtx advance, shared tail");
    CHECK(m.ascender == 800 && m.descender == -200, "hhea ascender/descender");
    {
        static uint8_t simple[40 * 24], comp[40 * 24];
        bool rs = ttf_render_at(&m, 1, 20, simple, 40, 24, 0, 20);
        bool rc = ttf_render_at(&m, 2, 20, comp, 40, 24, 0, 20);
        CHECK(rs && rc, "composite glyph renders");
        /* the composite is the simple glyph moved right by 500/1000 em = 10 px */
        int same = 1;
        for (int y = 0; y < 24 && same; y++)
            for (int x = 0; x < 30; x++)
                if (simple[y * 40 + x] != comp[y * 40 + x + 10]) { same = 0; break; }
        CHECK(same, "composite = component at its x offset");
        static uint8_t self[64];
        ttf_font_t bad = m;
        bad.num_glyphs = 2;                         /* component gid out of range */
        CHECK(!ttf_render_at(&bad, 2, 20, self, 8, 8, 0, 8), "out-of-range glyph declined");
    }
    zui_glyph_cache_init(&cache);
    {
        zui_text_t mt = { &m, &cache, 20, false };
        CHECK(zui_text_width(&mt, "AA") == 24, "text width sums advances (600/1000 x 20 px x 2)");
        CHECK(zui_text_ascent(&mt) == 16 && zui_text_descent(&mt) == -4, "ascent/descent in px");
    }
    zui_text_t t = { &f, &cache, 16, false };
    CHECK(zui_text_width(&t, "") == 0, "empty string has no width");
    zui_surface_t s;
    zui_surface_init(&s, buf, 64, 64, 64);
    zui_fill(&s, (zui_rect_t){ 0, 0, 64, 64 }, ZUI_VOID);
    (void)zui_text(&s, &t, 2, 40, ZUI_TEXT, "\xFF\xC3 bad utf8 \xE2\x82");
    CHECK(1, "malformed UTF-8 does not crash");
    CHECK(zui_text(&s, 0, 5, 5, 0, "x") == 5, "null text is a no-op");
}

static void test_depth(void) {
    zui_depth_t d;
    zui_depth_init(&d, ZUI_DEPTH_ANAGLYPH_AMBER_BLUE, 40);
    CHECK(d.view_dist_px == 2400 && d.ipd_px == 252, "defaults: 60 cm, 63 mm at 40 px/cm");

    /* held is at the glass for every magnitude and every eye */
    for (int m = 0; m < 500; m += 7) {
        CHECK(zui_role_z(ZUI_ROLE_HELD, m) == 0, "held z is 0");
        int32_t dx, dy;
        d.head_x = 333; d.head_y = -91;
        zui_layer_offset(&d, zui_role_z(ZUI_ROLE_HELD, m), -1, &dx, &dy);
        if (dx || dy) { CHECK(0, "held shifts by exactly 0"); break; }
    }
    d.head_x = 0; d.head_y = 0;
    CHECK(zui_role_z(ZUI_ROLE_FORWARD, 40) == -zui_role_z(ZUI_ROLE_BACK, 40), "S+ and S- mirror");

    /* window geometry: behind moves with the eye, in front moves against it */
    CHECK(zui_eye_shift(&d, -100, 120) > 0, "behind the glass follows the eye");
    CHECK(zui_eye_shift(&d, 100, 120) < 0, "in front of the glass opposes the eye");
    CHECK(zui_eye_shift(&d, 100, 120) == -zui_eye_shift(&d, 100, -120), "left/right eyes mirror");
    CHECK(iabs(zui_eye_shift(&d, 600, 120)) > iabs(zui_eye_shift(&d, -600, 120)),
          "near objects shift more than equally far ones behind");
    CHECK(zui_eye_shift(&d, 1000000, 120) != 0, "z beyond the eye is clamped, no divide by zero");

    /* eyes see opposite disparity for a forward layer */
    int32_t lx, rx;
    zui_layer_offset(&d, 60, -1, &lx, 0);
    zui_layer_offset(&d, 60, +1, &rx, 0);
    CHECK(lx == -rx && lx > 0, "crossed disparity for a forward layer");
    d.mode = ZUI_DEPTH_FLAT; d.head_x = 200;
    zui_layer_offset(&d, 60, -1, &lx, 0);
    CHECK(lx == 0, "flat mode never shifts");
    d.mode = ZUI_DEPTH_PARALLAX;
    zui_layer_offset(&d, 60, 0, &lx, 0);
    CHECK(lx < 0, "parallax follows the tracked head");

    /* chroma: identity at z == 0, warm near, cool far, reversible */
    d.mode = ZUI_DEPTH_CHROMA;
    CHECK(zui_chroma_px(&d, 0x808080, 0) == 0x808080, "chroma identity at the glass");
    uint32_t n = zui_chroma_px(&d, 0x808080, 80), f = zui_chroma_px(&d, 0x808080, -80);
    CHECK(((n >> 16) & 0xFF) > 0x80 && (n & 0xFF) < 0x80, "near goes warm");
    CHECK(((f >> 16) & 0xFF) < 0x80 && (f & 0xFF) > 0x80, "far goes cool");
    d.reverse_chroma = true;
    CHECK(zui_chroma_px(&d, 0x808080, 80) == f, "reverse swaps warm and cool");
    d.mode = ZUI_DEPTH_FLAT;
    CHECK(zui_chroma_px(&d, 0x808080, 80) == 0x808080, "chroma off outside chroma mode");

    /* anaglyph: agreeing eyes reproduce greys (rows of L+R sum to 1) */
    for (uint32_t gv = 0; gv < 256; gv += 15) {
        uint32_t g = gv * 0x010101u;
        uint32_t ab = zui_anaglyph_px(ZUI_DEPTH_ANAGLYPH_AMBER_BLUE, g, g);
        uint32_t rc = zui_anaglyph_px(ZUI_DEPTH_ANAGLYPH_RED_CYAN, g, g);
        int ok = 1;
        for (int k = 0; k < 24; k += 8) {
            if (iabs((int)((ab >> k) & 0xFF) - (int)gv) > 2) ok = 0;
            if (iabs((int)((rc >> k) & 0xFF) - (int)gv) > 2) ok = 0;
        }
        if (!ok) { CHECK(0, "grey survives both anaglyph matrices"); break; }
    }
    /* each eye's picture lands in its own filter colour */
    uint32_t lw = zui_anaglyph_px(ZUI_DEPTH_ANAGLYPH_RED_CYAN, 0xFFFFFF, 0x000000);
    CHECK(((lw >> 16) & 0xFF) > 200 && ((lw >> 8) & 0xFF) < 10 && (lw & 0xFF) < 10,
          "red/cyan: left eye goes to red");
    uint32_t rw = zui_anaglyph_px(ZUI_DEPTH_ANAGLYPH_AMBER_BLUE, 0x000000, 0xFFFFFF);
    CHECK((rw & 0xFF) > 200 && ((rw >> 16) & 0xFF) < 60, "amber/blue: right eye goes to blue");
    uint32_t aw = zui_anaglyph_px(ZUI_DEPTH_ANAGLYPH_AMBER_BLUE, 0xFFFFFF, 0x000000);
    CHECK(((aw >> 16) & 0xFF) > 200 && ((aw >> 8) & 0xFF) > 200 && (aw & 0xFF) < 60,
          "amber/blue: left eye goes to amber");
    CHECK(zui_anaglyph_px(ZUI_DEPTH_FLAT, 0x123456, 0xFFFFFF) == 0x123456, "non-anaglyph mode passes left through");

    zui_surface_t L, R, O;
    zui_surface_init(&L, buf, 64, 64, 64);
    zui_surface_init(&R, buf2, 64, 64, 64);
    zui_surface_init(&O, buf3, 64, 64, 64);
    zui_fill(&L, (zui_rect_t){ 0, 0, 64, 64 }, ZUI_GOLD);
    zui_fill(&R, (zui_rect_t){ 0, 0, 64, 64 }, ZUI_GOLD);
    zui_depth_init(&d, ZUI_DEPTH_FLAT, 40);
    CHECK(!zui_anaglyph(&d, &O, &L, &R), "anaglyph refuses a non-anaglyph mode");
    d.mode = ZUI_DEPTH_ANAGLYPH_AMBER_BLUE;
    CHECK(zui_anaglyph(&d, &O, &L, &R), "anaglyph composes");
    CHECK(buf3[0] == buf3[64 * 64 - 1], "uniform input gives uniform output");
    zui_surface_t small;
    zui_surface_init(&small, buf3, 32, 32, 64);
    CHECK(!zui_anaglyph(&d, &small, &L, &R), "size mismatch refused");
}

int main(void) {
    printf("=== zui: layout, shapes, text plumbing, depth ===\n");
    test_layout();
    test_shapes();
    test_text_plumbing();
    test_depth();
    printf("%d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
