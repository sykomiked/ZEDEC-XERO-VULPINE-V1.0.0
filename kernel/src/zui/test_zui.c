/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui.c — host tests for the zui renderer core: size classes, shapes,
 * text plumbing, and the depth effects' contracts (flat stays flat, held sits
 * at the glass, S+ and S- mirror, anaglyph of agreeing eyes is the picture). */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "zui_layout.h"
#include "zui_canvas.h"
#include "zui_depth.h"
#include "zui_logic.h"
#include "zui_space.h"
#include "zui_chiglet.h"
#include "zui_emote.h"
#include "zui_progress.h"
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

static void test_logic(void) {
    CHECK(zui_contrast_x100(0xFFFFFF, 0x000000) == 2100, "white on black is 21:1");
    CHECK(zui_contrast_x100(0x777777, 0x777777) == 100, "same colour is 1:1");
    int held_ok = 1, forb_ok = 1, text_ok = 1, mirror_ok = 1, split_ok = 1;
    for (uint32_t t = 0; t < 64; t++) {
        lpx_t tag = (lpx_t)t;
        zui_look_t l = zui_look(tag);
        if (lpx_forbidden(tag)) {
            if (!l.forbidden || l.z_pt || l.shimmer || l.split ||
                zui_look_color(&l, ZUI_GOLD, ZUI_VOID, 17) != ZUI_GOLD) forb_ok = 0;
            continue;
        }
        if (lpx_base(tag) == LPX_B_NEITHER) {
            for (uint32_t k = 0; k < 64; k++)
                if (l.z_pt || zui_look_color(&l, ZUI_GOLD, ZUI_VOID, k) != ZUI_GOLD) held_ok = 0;
        }
        if ((lpx_base(tag) == LPX_B_BOTH) != l.split) split_ok = 0;
        /* negation mirrors depth exactly (TRUE <-> FALSE, same polarity/phase) */
        zui_look_t n = zui_look(lpx_not(tag));
        if (n.z_pt != -l.z_pt) mirror_ok = 0;
        static const uint32_t fg[] = { ZUI_TEXT, ZUI_TEXT_2, ZUI_MUTED, ZUI_GOLD };
        static const uint32_t bgs[] = { ZUI_VOID, ZUI_PANEL, ZUI_RAISED };
        for (int i = 0; i < 4; i++) for (int j = 0; j < 3; j++) {
            uint32_t before = zui_contrast_x100(fg[i], bgs[j]);
            uint32_t after = zui_contrast_x100(zui_look_text(&l, fg[i], bgs[j]), bgs[j]);
            if (before >= 450 && after < 450) text_ok = 0;
        }
    }
    CHECK(held_ok, "NEITHER: at the glass, colour untouched, every polarity and phase");
    CHECK(forb_ok, "FORBIDDEN: flat, no shimmer, no split, colour untouched");
    CHECK(split_ok, "BOTH and only BOTH draws the colour/anti-colour pair");
    CHECK(mirror_ok, "negation mirrors depth");
    CHECK(text_ok, "text keeps 4.5:1 whenever it had it, for all 64 tags");

    zui_look_t tm = zui_look(lpx_make(LPX_B_TRUE, LPX_P_NEGENTROPIC, LPX_PH_MANIFEST));
    zui_look_t te = zui_look(lpx_make(LPX_B_TRUE, LPX_P_NEGENTROPIC, LPX_PH_EMERGING));
    zui_look_t tp = zui_look(lpx_make(LPX_B_TRUE, LPX_P_NEGENTROPIC, LPX_PH_POTENTIAL));
    zui_look_t td = zui_look(lpx_make(LPX_B_TRUE, LPX_P_NEGENTROPIC, LPX_PH_DISSOLVING));
    CHECK(tm.z_pt == 24 && tm.tone > 0, "TRUE manifest comes fully forward, tinted");
    CHECK(tm.z_pt > te.z_pt && te.z_pt > tp.z_pt && tp.z_pt > 0, "phase ramps the travel");
    CHECK(td.z_pt == te.z_pt, "dissolving retraces emerging");
    zui_look_t tn = zui_look(lpx_make(LPX_B_TRUE, LPX_P_NEUTRAL, LPX_PH_MANIFEST));
    zui_look_t tx = zui_look(lpx_make(LPX_B_TRUE, LPX_P_ENTROPIC, LPX_PH_MANIFEST));
    CHECK(tm.z_pt > tn.z_pt && tn.z_pt > tx.z_pt, "firmer evidence sits further forward");
    CHECK(tm.contrast > tn.contrast && tn.contrast > tx.contrast, "firmer evidence has more contrast");
    CHECK(tx.shimmer > 0 && tm.shimmer == 0, "only entropic shimmers");
    uint32_t a = zui_look_color(&tx, ZUI_GOLD, ZUI_VOID, 0);
    uint32_t b = zui_look_color(&tx, ZUI_GOLD, ZUI_VOID, 31);
    CHECK(a != b, "entropic shimmer moves with the phase tick");
    zui_look_t fm = zui_look(lpx_make(LPX_B_FALSE, LPX_P_NEGENTROPIC, LPX_PH_MANIFEST));
    uint32_t sh = zui_look_color(&fm, ZUI_TEXT, ZUI_VOID, 0);
    CHECK(zui_contrast_x100(sh, ZUI_VOID) < zui_contrast_x100(ZUI_TEXT, ZUI_VOID), "FALSE is shaded toward the void");
    uint32_t ti = zui_look_color(&tm, ZUI_GOLD, ZUI_VOID, 0);
    CHECK(zui_contrast_x100(ti, ZUI_VOID) > zui_contrast_x100(ZUI_GOLD, ZUI_VOID), "TRUE is tinted toward light");
    CHECK(zui_anti(0xD9A92E) == 0x2656D1, "anti-colour of gold is its complement");
}

static void test_spaces(void) {
    zsp_place_t home = { ZSP_HOME, ZSP_OWNER }, cloud = { ZSP_CLOUDTOP, ZSP_OWNER };
    zsp_place_t team = { ZSP_TEAM, ZSP_MEMBER }, office_guest = { ZSP_OFFICE, ZSP_GUEST };
    zsp_place_t park = { ZSP_PARK, ZSP_PUBLIC }, hall = { ZSP_HALL, ZSP_OWNER };
    zsp_place_t cloud_visitor = { ZSP_CLOUDTOP, ZSP_PUBLIC }, home_other = { ZSP_HOME, ZSP_MEMBER };
    lpx_t t = zsp_cross(home, cloud);
    CHECK(lpx_base(t) == LPX_B_NEITHER, "home -> cloud top is held in the hall");
    CHECK(lpx_base(zsp_confirm(t)) == LPX_B_TRUE, "confirming a held crossing lets it through");
    CHECK(lpx_base(zsp_cross(cloud, home)) == LPX_B_TRUE, "toward more private proceeds");
    CHECK(lpx_base(zsp_cross(team, team)) == LPX_B_TRUE, "within a space proceeds");
    CHECK(lpx_base(zsp_cross(team, park)) == LPX_B_NEITHER, "team -> park is held");
    CHECK(lpx_base(zsp_cross(home, office_guest)) == LPX_B_FALSE, "a guest cannot put things in an office");
    CHECK(lpx_base(zsp_cross(cloud_visitor, home)) == LPX_B_TRUE, "a visitor may take a public thing home");
    CHECK(lpx_base(zsp_cross(home_other, home)) == LPX_B_FALSE, "nobody else can see into your home");
    CHECK(lpx_base(zsp_cross(home, hall)) == LPX_B_FALSE, "nothing is kept in the hall");
    CHECK(lpx_base(zsp_confirm(zsp_cross(home, office_guest))) == LPX_B_FALSE, "a refusal stays refused");
    CHECK(zsp_can_view(ZSP_PARK, ZSP_PUBLIC) && zsp_can_write(ZSP_PARK, ZSP_PUBLIC), "the park is open to all");
    CHECK(zsp_can_view(ZSP_CLOUDTOP, ZSP_PUBLIC) && !zsp_can_write(ZSP_CLOUDTOP, ZSP_PUBLIC),
          "anyone may look at a cloud top, only the owner arranges it");
    CHECK(!zsp_can_view((zsp_kind_t)99, ZSP_OWNER) && !zsp_can_view(ZSP_HOME, (zsp_role_t)9),
          "unknown kinds and roles have no rights");
    int mono = 1;
    for (int a = 0; a < 5; a++) for (int b = 0; b < 5; b++) {
        lpx_t x = zsp_cross((zsp_place_t){ (zsp_kind_t)a, ZSP_OWNER }, (zsp_place_t){ (zsp_kind_t)b, ZSP_OWNER });
        if ((b > a) != (lpx_base(x) == LPX_B_NEITHER)) mono = 0;
    }
    CHECK(mono, "for an owner, exactly the crossings toward more openness are held");

    zsp_rules_t vert = { ZSP_GOV_VERTICAL, 0 }, flat = { ZSP_GOV_HORIZONTAL, 60 };
    CHECK(lpx_base(zsp_decide(&vert, ZSP_OWNER, 5, 0, 0)) == LPX_B_TRUE, "vertical: the owner decides");
    CHECK(lpx_base(zsp_decide(&vert, ZSP_MEMBER, 5, 5, 0)) == LPX_B_NEITHER, "vertical: a member's proposal waits for the owner");
    CHECK(lpx_base(zsp_decide(&vert, ZSP_GUEST, 5, 0, 0)) == LPX_B_FALSE, "guests cannot propose");
    CHECK(lpx_base(zsp_decide(&flat, ZSP_MEMBER, 5, 3, 0)) == LPX_B_TRUE, "flat: 3 of 5 meets 60% with no objection");
    CHECK(lpx_base(zsp_decide(&flat, ZSP_MEMBER, 5, 2, 0)) == LPX_B_NEITHER, "flat: below quorum keeps gathering");
    CHECK(lpx_base(zsp_decide(&flat, ZSP_MEMBER, 5, 3, 1)) == LPX_B_BOTH, "flat: quorum with an objection is a dispute");
    CHECK(lpx_action(zsp_decide(&flat, ZSP_MEMBER, 5, 3, 1)) == LPX_ACT_ESCALATE, "a dispute goes to people");
    CHECK(lpx_base(zsp_decide(&flat, ZSP_MEMBER, 5, 1, 3)) == LPX_B_FALSE, "flat: fails once quorum is out of reach");
    CHECK(lpx_base(zsp_decide(&flat, ZSP_OWNER, 5, 0, 0)) == LPX_B_NEITHER, "flat: even the owner needs the members");
    CHECK(lpx_base(zsp_decide(&flat, ZSP_MEMBER, 0, 0, 0)) == LPX_B_FALSE, "an empty space decides nothing");
    zsp_rules_t maj = { ZSP_GOV_HORIZONTAL, 0 };
    CHECK(lpx_base(zsp_decide(&maj, ZSP_MEMBER, 4, 2, 0)) == LPX_B_NEITHER &&
          lpx_base(zsp_decide(&maj, ZSP_MEMBER, 4, 3, 0)) == LPX_B_TRUE, "default quorum is a simple majority");
    CHECK(lpx_base(zsp_decide(&maj, ZSP_MEMBER, 3, 99, 99)) == LPX_B_TRUE, "votes are clamped to the member count");
}

static uint64_t fnv(const uint32_t *p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

static uint64_t draw_chg(const zui_chiglet_t *c, uint32_t tick) {
    zui_surface_t s;
    zui_surface_init(&s, buf, 64, 64, 64);
    zui_fill(&s, (zui_rect_t){ 0, 0, 64, 64 }, ZUI_VOID);
    zui_chiglet_draw(&s, c, 32, 34, 14, tick);
    return fnv(buf, 64 * 64);
}

static void test_chiglet(void) {
    zui_chiglet_t a = { { 1, 2, 3, 4, 5, 6, 7, 8 }, 1, ZUI_CHG_DECIDED };
    zui_chiglet_t b = a;
    CHECK(draw_chg(&a, 3) == draw_chg(&b, 3), "same genes, same companion");
    uint64_t base = draw_chg(&a, 3);
    int all_genes_show = 1;
    for (int gi = 0; gi < 8; gi++) {
        zui_chiglet_t m = a;
        m.genes[gi] = (uint8_t)(m.genes[gi] + 1);
        if (draw_chg(&m, 3) == base) all_genes_show = 0;
    }
    CHECK(all_genes_show, "every gene changes the drawn form");
    b.generation = 3;
    CHECK(draw_chg(&b, 3) != base, "evolving adds halo rings");
    b = a; b.mood = ZUI_CHG_UNCERTAIN;
    CHECK(draw_chg(&b, 3) != base, "uncertain shows on its face");
    b = a; b.mood = ZUI_CHG_SPEAKING;
    CHECK(draw_chg(&b, 3) != base, "speaking opens its mouth");
    CHECK(draw_chg(&a, 4) != base, "motes move with the tick");
    zui_surface_t s;
    zui_surface_init(&s, buf, 64, 64, 64);
    zui_fill(&s, (zui_rect_t){ 0, 0, 64, 64 }, ZUI_VOID);
    uint64_t empty = fnv(buf, 64 * 64);
    zui_chiglet_draw(&s, &a, 32, 32, 4, 0);
    CHECK(fnv(buf, 64 * 64) == empty, "too small to draw draws nothing");
    zui_chiglet_draw(&s, &a, -500, 9000, 30, 0);
    CHECK(1, "drawing off-surface is clipped, not overrun");
    CHECK(zui_chiglet_accent(0) == ZUI_GOLD, "null companion falls back to gold");
}

static uint64_t draw_face(zui_face_t f, zui_health_t h, uint32_t tick) {
    zui_surface_t s;
    zui_surface_init(&s, buf, 64, 64, 64);
    zui_fill(&s, (zui_rect_t){ 0, 0, 64, 64 }, ZUI_VOID);
    zui_face_draw(&s, &f, h, 32, 32, 22, tick);
    return fnv(buf, 64 * 64);
}

static int face_eq(zui_face_t a, zui_face_t b) {
    return a.mouth_curve == b.mouth_curve && a.mouth_open == b.mouth_open && a.eye_open == b.eye_open &&
           a.squint == b.squint && a.brow_tilt == b.brow_tilt && a.brow_raise == b.brow_raise &&
           a.blush == b.blush && a.tear == b.tear && a.color == b.color;
}

static void test_emote(void) {
    /* every emoji is its own face, and draws differently */
    int distinct = 1;
    uint64_t h[SWARM_EMO_COUNT];
    for (int e = 0; e < SWARM_EMO_COUNT; e++) {
        h[e] = draw_face(zui_face_of((swarm_emotion_t)e), ZUI_HEALTH_OK, 0);
        for (int k = 0; k < e; k++) if (h[k] == h[e]) distinct = 0;
    }
    CHECK(distinct, "all eight emoji draw as different faces");
    CHECK(draw_face(zui_face_of(SWARM_EMO_JOY), ZUI_HEALTH_OK, 0) == h[SWARM_EMO_JOY], "same face, same pixels");
    CHECK(face_eq(zui_face_of((swarm_emotion_t)99), zui_face_of(SWARM_EMO_NEUTRAL)), "unknown emotion falls back to neutral");

    /* intensity is the Fibonacci charge */
    zui_face_t n = zui_face_of(SWARM_EMO_NEUTRAL);
    int zero_is_neutral = 1;
    for (int e = 0; e < SWARM_EMO_COUNT; e++)
        if (!face_eq(zui_face_feeling((swarm_feeling_t){ (swarm_emotion_t)e, 0 }), n)) zero_is_neutral = 0;
    CHECK(zero_is_neutral, "intensity 0 shows the neutral face for every emotion");
    CHECK(face_eq(zui_face_feeling((swarm_feeling_t){ SWARM_EMO_SADNESS, 5 }), zui_face_of(SWARM_EMO_SADNESS)),
          "intensity 5 (charge 8) is the full expression");
    int mono = 1;
    int16_t prev = 0;
    for (uint8_t k = 0; k <= 5; k++) {
        int16_t c = zui_face_feeling((swarm_feeling_t){ SWARM_EMO_JOY, k }).mouth_curve;
        if (k && c <= prev) mono = 0;
        prev = c;
    }
    CHECK(mono, "the smile grows with every intensity step");
    CHECK(zui_face_feeling((swarm_feeling_t){ SWARM_EMO_JOY, 3 }).mouth_curve == 256 * 3 / 8,
          "intensity 3 moves the face exactly charge 3 / 8 of the way");

    /* morphing is a walk: exact ends, every frame in between real */
    zui_face_t a = zui_face_of(SWARM_EMO_CURIOUS), b = zui_face_of(SWARM_EMO_WORRY);
    CHECK(face_eq(zui_face_lerp(a, b, 0), a) && face_eq(zui_face_lerp(a, b, 256), b), "morph is exact at both ends");
    uint64_t mid = draw_face(zui_face_lerp(a, b, 128), ZUI_HEALTH_OK, 0);
    CHECK(mid != draw_face(a, ZUI_HEALTH_OK, 0) && mid != draw_face(b, ZUI_HEALTH_OK, 0), "the midpoint is a face of its own");
    CHECK(zui_emote_ease(0) == 0 && zui_emote_ease(256) == 256 && zui_emote_ease(128) == 128, "ease is exact and symmetric");
    int ease_mono = 1;
    for (uint32_t t = 1; t <= 256; t++) if (zui_emote_ease(t) < zui_emote_ease(t - 1)) ease_mono = 0;
    CHECK(ease_mono, "ease never goes backwards");

    zui_emote_t m;
    zui_emote_init(&m, (swarm_feeling_t){ SWARM_EMO_CALM, 3 }, 1000);
    zui_emote_set(&m, (swarm_feeling_t){ SWARM_EMO_WORRY, 4 }, 1000);
    zui_face_t w = zui_face_feeling((swarm_feeling_t){ SWARM_EMO_WORRY, 4 });
    zui_face_t c3 = zui_face_feeling((swarm_feeling_t){ SWARM_EMO_CALM, 3 });
    CHECK(face_eq(zui_emote_now(&m, 1000, 0), c3), "a morph starts from what was on screen");
    CHECK(face_eq(zui_emote_now(&m, 1000 + ZUI_EMOTE_MORPH_MS, 0), w), "and arrives on time");
    CHECK(face_eq(zui_emote_now(&m, 1000, 1), w), "reduce motion jumps straight to the new face");
    zui_face_t half = zui_emote_now(&m, 1000 + ZUI_EMOTE_MORPH_MS / 2, 0);
    zui_emote_set(&m, (swarm_feeling_t){ SWARM_EMO_JOY, 2 }, 1000 + ZUI_EMOTE_MORPH_MS / 2);
    CHECK(face_eq(zui_emote_now(&m, 1000 + ZUI_EMOTE_MORPH_MS / 2, 0), half),
          "a new feeling mid-morph turns from where the face is, no jump");
    zui_emote_init(&m, (swarm_feeling_t){ SWARM_EMO_CALM, 3 }, 0xFFFFFF00u);
    zui_emote_set(&m, (swarm_feeling_t){ SWARM_EMO_JOY, 5 }, 0xFFFFFF00u);
    CHECK(face_eq(zui_emote_now(&m, 0xFFFFFF00u + ZUI_EMOTE_MORPH_MS, 0), zui_face_of(SWARM_EMO_JOY)),
          "morph timing survives the millisecond clock wrapping");

    /* health is not feeling */
    uint64_t ok = draw_face(n, ZUI_HEALTH_OK, 0);
    uint64_t slow = draw_face(n, ZUI_HEALTH_SLOW, 0), fail = draw_face(n, ZUI_HEALTH_FAILING, 0),
             stop = draw_face(n, ZUI_HEALTH_STOPPED, 0);
    CHECK(ok != slow && ok != fail && ok != stop && slow != fail && fail != stop && slow != stop,
          "each health state looks different on the same face");
    CHECK(draw_face(n, ZUI_HEALTH_SLOW, 3) != slow, "the slow ring turns");
    CHECK(draw_face(n, ZUI_HEALTH_OK, 3) == ok, "a healthy face is still: no motion without meaning");

    /* the logic tag it shows with */
    lpx_t t = zui_emote_tag((swarm_feeling_t){ SWARM_EMO_JOY, 5 }, ZUI_HEALTH_OK);
    CHECK(lpx_base(t) == LPX_B_TRUE && lpx_pol(t) == LPX_P_NEGENTROPIC && lpx_phase(t) == LPX_PH_MANIFEST,
          "strong joy, healthy: TRUE, converging, manifest");
    t = zui_emote_tag((swarm_feeling_t){ SWARM_EMO_FRUSTRATION, 2 }, ZUI_HEALTH_FAILING);
    CHECK(lpx_base(t) == LPX_B_FALSE && lpx_pol(t) == LPX_P_ENTROPIC && lpx_phase(t) == LPX_PH_EMERGING,
          "mild frustration, failing: FALSE, diverging, emerging");
    t = zui_emote_tag((swarm_feeling_t){ SWARM_EMO_WORRY, 0 }, ZUI_HEALTH_STOPPED);
    CHECK(lpx_base(t) == LPX_B_NEITHER && lpx_phase(t) == LPX_PH_DISSOLVING && !lpx_forbidden(t),
          "no charge reads NEITHER; stopped dissolves; never forbidden");
    CHECK(strcmp(zui_emote_word(SWARM_EMO_CURIOUS), "curious") == 0 && strcmp(zui_emote_word((swarm_emotion_t)42), "neutral") == 0,
          "words for screen readers");

    /* off-surface and tiny draws are safe */
    zui_surface_t s;
    zui_surface_init(&s, buf, 64, 64, 64);
    zui_fill(&s, (zui_rect_t){ 0, 0, 64, 64 }, ZUI_VOID);
    uint64_t empty = fnv(buf, 64 * 64);
    zui_face_draw(&s, &n, ZUI_HEALTH_OK, 32, 32, 3, 0);
    CHECK(fnv(buf, 64 * 64) == empty, "a face too small to read draws nothing");
    zui_face_draw(&s, &n, ZUI_HEALTH_FAILING, -900, 5000, 40, 0);
    CHECK(1, "drawing off-surface is clipped, not overrun");
}

static uint32_t seen_lit[8];
static int seen_calls;
static void rec_part(zui_surface_t *s, zui_rect_t b, uint32_t lit, void *ctx) {
    (void)s; (void)b; seen_lit[(intptr_t)ctx] = lit; seen_calls++;
}

static void test_progress(void) {
    zui_surface_t s;
    zui_surface_init(&s, buf, 64, 64, 64);
    zui_fill(&s, (zui_rect_t){ 0, 0, 64, 64 }, ZUI_VOID);
    zui_rect_t tr = { 4, 30, 56, 3 };
    int32_t hl, hr, mid = 4 + 28;
    zui_bar_draw(&s, tr, ZUI_BAR_GATHER, 0, 0, &hl, &hr);
    CHECK(hl == 4 && hr == 60, "gathering starts with both heads at the ends");
    zui_bar_draw(&s, tr, ZUI_BAR_GATHER, 256, 0, &hl, &hr);
    CHECK(hl == mid && hr == mid, "and finishes with the heads met at the centre");
    zui_bar_draw(&s, tr, ZUI_BAR_GATHER, 128, 0, &hl, &hr);
    CHECK(hl - 4 == 60 - hr, "the two halves stay mirrored");
    zui_bar_draw(&s, tr, ZUI_BAR_RELEASE, 0, 0, &hl, &hr);
    CHECK(hl == mid && hr == mid, "releasing starts at the centre");
    zui_bar_draw(&s, tr, ZUI_BAR_RELEASE, 256, 0, &hl, &hr);
    CHECK(hl == 4 && hr == 60, "and reaches both edges");
    zui_bar_draw(&s, tr, ZUI_BAR_GATHER, 999, 0, &hl, &hr);
    CHECK(hl == mid && hr == mid, "progress past full is full, not past the centre");

    /* unknown progress breathes and never claims a position */
    zui_fill(&s, (zui_rect_t){ 0, 0, 64, 64 }, ZUI_VOID);
    zui_bar_draw(&s, tr, ZUI_BAR_GATHER, ZUI_BAR_UNKNOWN, 0, &hl, &hr);
    uint64_t a = fnv(buf, 64 * 64);
    int32_t w0 = hr - hl;
    zui_fill(&s, (zui_rect_t){ 0, 0, 64, 64 }, ZUI_VOID);
    zui_bar_draw(&s, tr, ZUI_BAR_GATHER, ZUI_BAR_UNKNOWN, 16, &hl, &hr);
    CHECK(fnv(buf, 64 * 64) != a && hr - hl > w0, "unknown progress breathes with the tick");
    CHECK(hl + hr == 2 * mid, "and stays centred");

    /* lighting: outer items first, the centre only when the heads meet */
    CHECK(zui_bar_lit(10, mid, 4, 60, 8) == 0, "a left item is dark before its head arrives");
    CHECK(zui_bar_lit(10, mid, 20, 44, 8) == 256, "and fully lit once it has passed");
    CHECK(zui_bar_lit(54, mid, 20, 44, 8) == 256, "the right side mirrors it");
    CHECK(zui_bar_lit(mid, mid, 20, 44, 8) == 0, "the centre waits while the halves are apart");
    CHECK(zui_bar_lit(mid, mid, mid, mid, 8) == 256, "and lights when they meet");
    uint32_t ramp = zui_bar_lit(10, mid, 10, 54, 8);
    CHECK(ramp > 0 && ramp < 256, "switching on is a short ramp, not a jump");

    /* the launch screen calls each part with its lit level */
    static uint32_t big[320 * 200];
    zui_surface_t L;
    zui_surface_init(&L, big, 320, 200, 320);
    zui_launch_t l = { rec_part, { rec_part, rec_part, rec_part, rec_part, rec_part },
                       { (void *)7, (void *)0, (void *)1, (void *)2, (void *)3, (void *)4 }, 5 };
    seen_calls = 0;
    zui_launch_draw(&L, (zui_rect_t){ 0, 0, 320, 200 }, 256, &l, (zui_launch_state_t){ 0, 0, 0 }, 0);
    CHECK(seen_calls == 6 && seen_lit[0] == 0 && seen_lit[2] == 0 && seen_lit[7] == 0,
          "at the start every part and the mark are dim");
    zui_launch_draw(&L, (zui_rect_t){ 0, 0, 320, 200 }, 256, &l, (zui_launch_state_t){ 150, 0, 0 }, 0);
    CHECK(seen_lit[0] == 256 && seen_lit[4] == 256 && seen_lit[2] == 0, "outer parts light first, mirrored");
    zui_launch_draw(&L, (zui_rect_t){ 0, 0, 320, 200 }, 256, &l, (zui_launch_state_t){ 256, 0, 0 }, 0);
    CHECK(seen_lit[2] == 256 && seen_lit[7] == 0, "the centre part lights last; the mark waits");
    zui_launch_draw(&L, (zui_rect_t){ 0, 0, 320, 200 }, 256, &l, (zui_launch_state_t){ 256, 256, 128 }, 0);
    CHECK(seen_lit[7] == 256, "then the mark lights");
    uint64_t h1 = fnv(big, 320 * 200);
    zui_launch_draw(&L, (zui_rect_t){ 0, 0, 320, 200 }, 256, &l, (zui_launch_state_t){ 256, 256, 128 }, 0);
    CHECK(fnv(big, 320 * 200) == h1, "same state, same pixels");
    zui_launch_draw(&L, (zui_rect_t){ 0, 0, 320, 200 }, 256, &l, (zui_launch_state_t){ 256, 256, 200 }, 0);
    CHECK(fnv(big, 320 * 200) != h1, "the top bar moves with the rest of the load");
    zui_launch_t none = { 0, { 0 }, { 0 }, 0 };
    zui_launch_draw(&L, (zui_rect_t){ 0, 0, 320, 200 }, 256, &none, (zui_launch_state_t){ 256, 256, 256 }, 0);
    CHECK(1, "a launch with no parts or painters still draws its bars");
}

int main(void) {
    printf("=== zui: layout, shapes, text plumbing, depth ===\n");
    test_layout();
    test_shapes();
    test_text_plumbing();
    test_depth();
    test_logic();
    test_spaces();
    test_chiglet();
    test_emote();
    test_progress();
    printf("%d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
