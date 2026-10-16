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
#include "zui_login.h"
#include "zui_pxl.h"
#include "zui_para.h"
#include "../font/ttf_fixture.inc" /* MINI_TTF[] */
#include "zui_ttf_fixture.inc"     /* ZUI_TTF[]: metrics + a composite */

static int fails, checks;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL %s (%s:%d)\n", m, __FILE__, __LINE__);                                  \
        }                                                                                          \
    } while (0)

static int iabs(int v)
{
    return v < 0 ? -v : v;
}

static void test_layout(void)
{
    zui_screen_t phone = {1170, 2532, 768, ZUI_INPUT_TOUCH};    /* 390 pt @3x */
    zui_screen_t tablet = {1640, 2360, 512, ZUI_INPUT_TOUCH};   /* 820 pt @2x */
    zui_screen_t laptop = {2880, 1800, 512, ZUI_INPUT_POINTER}; /* 1440 pt   */
    zui_screen_t big = {3840, 2160, 256, ZUI_INPUT_POINTER};    /* 3840 pt   */
    CHECK(zui_class_for(&phone) == ZUI_COMPACT, "390 pt phone is compact");
    CHECK(zui_class_for(&tablet) == ZUI_MEDIUM, "820 pt tablet is medium");
    CHECK(zui_class_for(&laptop) == ZUI_EXPANDED, "1440 pt laptop is expanded");
    CHECK(zui_class_for(&big) == ZUI_LARGE, "4K at 1x is large");

    zui_screen_t edge = {599, 800, 256, ZUI_INPUT_POINTER};
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
    CHECK(p.inspector.w > 0 && p.inspector.x + p.inspector.w == 3840,
          "large: inspector at right edge");
    CHECK(p.main.w > 0 && p.list.x + p.list.w == p.main.x, "large: panes abut");

    zui_screen_t zero = {100, 100, 0, ZUI_INPUT_POINTER};
    CHECK(zui_class_for(&zero) == ZUI_COMPACT, "zero scale does not divide by zero");
}

static uint32_t buf[64 * 64], buf2[64 * 64], buf3[64 * 64];

static void test_shapes(void)
{
    zui_surface_t s;
    zui_surface_init(&s, buf, 64, 64, 64);
    zui_fill(&s, (zui_rect_t){0, 0, 64, 64}, ZUI_VOID);
    zui_fill_rrect(&s, (zui_rect_t){8, 8, 48, 48}, 10, ZUI_GOLD, 255);
    CHECK(buf[32 * 64 + 32] == ZUI_GOLD, "rrect centre is solid");
    CHECK(buf[8 * 64 + 8] == ZUI_VOID, "rrect corner pixel is outside the arc");
    int partial = 0;
    for (int y = 8; y < 18; y++)
        for (int x = 8; x < 18; x++)
            if (buf[y * 64 + x] != ZUI_VOID && buf[y * 64 + x] != ZUI_GOLD) partial++;
    CHECK(partial >= 8, "corner arc is anti-aliased");
    CHECK(buf[8 * 64 + 32] == ZUI_GOLD && buf[7 * 64 + 32] == ZUI_VOID, "straight edges are crisp");

    zui_fill(&s, (zui_rect_t){0, 0, 64, 64}, ZUI_VOID);
    zui_stroke_rrect(&s, (zui_rect_t){4, 4, 56, 56}, 8, 2, ZUI_EDGE, 255);
    CHECK(buf[32 * 64 + 4] == ZUI_EDGE && buf[32 * 64 + 5] == ZUI_EDGE, "stroke is 2 px");
    CHECK(buf[32 * 64 + 6] == ZUI_VOID, "stroke interior is untouched");

    zui_fill(&s, (zui_rect_t){0, 0, 64, 64}, ZUI_VOID);
    zui_clip(&s, (zui_rect_t){0, 0, 32, 64});
    zui_fill(&s, (zui_rect_t){0, 0, 64, 64}, ZUI_TEXT);
    CHECK(buf[10 * 64 + 31] == ZUI_TEXT && buf[10 * 64 + 32] == ZUI_VOID, "clip holds");
    zui_unclip(&s);
    zui_fill(&s, (zui_rect_t){-10, -10, 1000, 1000}, ZUI_PANEL);
    CHECK(buf[63 * 64 + 63] == ZUI_PANEL, "oversized fill is clamped, not overrun");

    zui_fill(&s, (zui_rect_t){0, 0, 64, 64}, ZUI_VOID);
    zui_dash_rrect(&s, (zui_rect_t){2, 2, 60, 40}, 6, 1, 4, ZUI_GOLD);
    int on = 0, off = 0;
    for (int x = 10; x < 54; x++) {
        if (buf[2 * 64 + x] == ZUI_GOLD)
            on++;
        else
            off++;
    }
    CHECK(on > 10 && off > 10, "dashed edge alternates");

    CHECK(zui_mix(0x000000, 0xFFFFFF, 0) == 0x000000, "mix t=0");
    CHECK(zui_mix(0x000000, 0xFFFFFF, 255) == 0xFFFFFF, "mix t=255");
    CHECK(zui_mix(0x000000, 0xFFFFFF, 1000) == 0xFFFFFF, "mix clamps t");
}

static void test_text_plumbing(void)
{
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
                if (simple[y * 40 + x] != comp[y * 40 + x + 10]) {
                    same = 0;
                    break;
                }
        CHECK(same, "composite = component at its x offset");
        static uint8_t self[64];
        ttf_font_t bad = m;
        bad.num_glyphs = 2; /* component gid out of range */
        CHECK(!ttf_render_at(&bad, 2, 20, self, 8, 8, 0, 8), "out-of-range glyph declined");
    }
    zui_glyph_cache_init(&cache);
    {
        zui_text_t mt = {&m, &cache, 20, false};
        CHECK(zui_text_width(&mt, "AA") == 24, "text width sums advances (600/1000 x 20 px x 2)");
        CHECK(zui_text_ascent(&mt) == 16 && zui_text_descent(&mt) == -4, "ascent/descent in px");
    }
    zui_text_t t = {&f, &cache, 16, false};
    CHECK(zui_text_width(&t, "") == 0, "empty string has no width");
    zui_surface_t s;
    zui_surface_init(&s, buf, 64, 64, 64);
    zui_fill(&s, (zui_rect_t){0, 0, 64, 64}, ZUI_VOID);
    (void) zui_text(&s, &t, 2, 40, ZUI_TEXT, "\xFF\xC3 bad utf8 \xE2\x82");
    CHECK(1, "malformed UTF-8 does not crash");
    CHECK(zui_text(&s, 0, 5, 5, 0, "x") == 5, "null text is a no-op");
}

static int32_t paint_calls;
static int32_t test_painter(zui_surface_t *s, uint32_t cp, int32_t x, int32_t base, uint32_t size,
                            uint32_t rgb, uint32_t a, void *ctx)
{
    (void) a;
    (void) ctx;
    if (cp != 0x1F604u) return -1;
    if (s) {
        paint_calls++;
        zui_fill(s, (zui_rect_t){x, base - (int32_t) size, (int32_t) size, (int32_t) size}, rgb);
    }
    return (int32_t) size + 2;
}

static int ink_count(const uint32_t *p, int n, uint32_t ground)
{
    int k = 0;
    for (int i = 0; i < n; i++) k += p[i] != ground;
    return k;
}

static void test_fallback(void)
{
    printf("--- text: fallback chain, drawn symbols, painter ---\n");
    static zui_glyph_cache_t cache;
    static uint8_t bfont[sizeof ZUI_TTF];
    memcpy(bfont, ZUI_TTF, sizeof ZUI_TTF);
    bfont[0x97] = 0x42; /* the cmap's one segment moves from 'A' to 'B' */
    bfont[0x9d] = 0x42;
    bfont[0xa1] = 0xbf;
    ttf_font_t a, b;
    CHECK(ttf_parse(&a, ZUI_TTF, sizeof ZUI_TTF) && ttf_parse(&b, bfont, sizeof bfont),
          "two fixture faces parse");
    CHECK(ttf_glyph_index(&a, 'B') == 0 && ttf_glyph_index(&b, 'B') == 1, "only the second has B");
    zui_glyph_cache_init(&cache);
    zui_text_t t = {&a, &cache, 20, false};
    int32_t box = zui_text_width_q8(&t, "B");
    const ttf_font_t *chain[2] = {&b, 0};
    zui_text_fallback(&cache, chain, 2);
    CHECK(cache.n_fallback == 1, "NULL entries are dropped from the chain");
    CHECK(zui_text_width(&t, "B") == 12, "B comes from the fallback face (600/1000 x 20)");
    CHECK(zui_text_width_q8(&t, "A") == 12 * 256, "A still comes from the text's own face");
    (void) box;

    static uint32_t p1[96 * 48], p2[96 * 48];
    zui_surface_t s1, s2;
    zui_surface_init(&s1, p1, 96, 48, 96);
    zui_surface_init(&s2, p2, 96, 48, 96);
    zui_fill(&s1, (zui_rect_t){0, 0, 96, 48}, ZUI_VOID);
    zui_fill(&s2, (zui_rect_t){0, 0, 96, 48}, ZUI_VOID);
    zui_text(&s1, &t, 4, 30, ZUI_TEXT, "B");
    CHECK(ink_count(p1, 96 * 48, ZUI_VOID) > 20, "the fallback glyph has ink");

    /* drawn keyboard symbols */
    const uint32_t keys[] = {0x2318u, 0x2325u, 0x21E7u, 0x2303u, 0x23CEu, 0x21B5u,
                             0x232Bu, 0x238Bu, 0x21E5u, 0x2713u, 0x2715u};
    int all = 1, inked = 1;
    for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        all &= zui_symbol_has(keys[i]) && zui_symbol(0, keys[i], 0, 0, 20, 0, 0) == 18;
        zui_fill(&s2, (zui_rect_t){0, 0, 96, 48}, ZUI_VOID);
        zui_symbol(&s2, keys[i], 10, 34, 24, ZUI_GOLD, 255);
        inked &= ink_count(p2, 96 * 48, ZUI_VOID) > 30;
    }
    CHECK(all, "every keyboard symbol is known, 0.9 em wide");
    CHECK(inked, "and every one draws");
    CHECK(!zui_symbol_has('A') && zui_symbol(0, 'A', 0, 0, 20, 0, 0) < 0,
          "letters are not symbols");
    zui_fill(&s1, (zui_rect_t){0, 0, 96, 48}, ZUI_VOID);
    zui_fill(&s2, (zui_rect_t){0, 0, 96, 48}, ZUI_VOID);
    int32_t end = zui_text(&s1, &t, 4, 30, ZUI_TEXT,
                           "\xE2\x8C\x98"
                           "B");
    zui_symbol(&s2, 0x2318u, 4, 30, 20, ZUI_TEXT, 255);
    zui_text(&s2, &t, 4 + 18, 30, ZUI_TEXT, "B");
    CHECK(memcmp(p1, p2, sizeof p1) == 0 && end == 4 + 18 + 12,
          "text draws the command key, then carries on after it");
    CHECK(zui_text_width(&t, "\xE2\x8C\x98\xE2\x87\xA7"
                             "B") == 18 + 18 + 12,
          "widths agree");

    /* a painter for what nothing else has */
    int32_t before = zui_text_width_q8(&t, "\xF0\x9F\x98\x84");
    zui_text_painter(&cache, test_painter, 0);
    CHECK(zui_text_width(&t, "\xF0\x9F\x98\x84") == 22, "the painter sets the emoji's width");
    paint_calls = 0;
    zui_fill(&s1, (zui_rect_t){0, 0, 96, 48}, ZUI_VOID);
    zui_text(&s1, &t, 4, 30, ZUI_GOLD, "\xF0\x9F\x98\x84");
    CHECK(paint_calls == 1 && p1[20 * 96 + 10] == ZUI_GOLD, "and draws it");
    zui_text_painter(&cache, 0, 0);
    CHECK(zui_text_width_q8(&t, "\xF0\x9F\x98\x84") == before,
          "without one, the face's own box stays");

    /* the swarm's own faces as emoji */
    zui_text_painter(&cache, zui_emote_glyph, 0);
    CHECK(zui_text_width(&t, "\xF0\x9F\x98\x84\xF0\x9F\xA4\x94") == 40,
          "joy and curious emoji are one em each");
    zui_fill(&s1, (zui_rect_t){0, 0, 96, 48}, ZUI_VOID);
    zui_text(&s1, &t, 4, 30, ZUI_TEXT, "\xF0\x9F\x98\x84");
    CHECK(ink_count(p1, 96 * 48, ZUI_VOID) > 60, "and draw as faces");
    CHECK(zui_emote_glyph(0, 0x1F680u, 0, 0, 20, 0, 0, 0) < 0, "a rocket is not a feeling");
    zui_text_painter(&cache, 0, 0);

    /* a full cache starts over but keeps its chain */
    cache.used = ZUI_GLYPH_POOL;
    zui_fill(&s1, (zui_rect_t){0, 0, 96, 48}, ZUI_VOID);
    zui_text(&s1, &t, 4, 30, ZUI_TEXT, "B");
    CHECK(cache.n_fallback == 1 && ink_count(p1, 96 * 48, ZUI_VOID) > 20,
          "a cache reset keeps the fallback chain");
    zui_glyph_cache_init(&cache);
    CHECK(cache.n_fallback == 0 && !cache.sym, "init clears it");

    /* the line primitive */
    zui_fill(&s1, (zui_rect_t){0, 0, 96, 48}, ZUI_VOID);
    zui_line_q8(&s1, 10 * 256, 10 * 256, 80 * 256, 10 * 256, 2 * 256, ZUI_GOLD, 255);
    CHECK(p1[10 * 96 + 40] == ZUI_GOLD && p1[20 * 96 + 40] == ZUI_VOID,
          "a line covers its path only");
    zui_line_q8(&s1, -5000, -5000, 50000, 50000, 512, ZUI_GOLD, 255);
    zui_line_q8(0, 0, 0, 10, 10, 256, ZUI_GOLD, 255);
    CHECK(1, "off-surface and NULL lines are safe");
}

static void test_para(void)
{
    printf("--- text: shaping and bidi before drawing ---\n");
    static zui_para_t P;
    /* Hebrew alef bet gimel, logical -> visual reversed */
    CHECK(!strcmp(zui_para_layout(&P, "\xD7\x90\xD7\x91\xD7\x92", 0, ZUI_BIDI_AUTO),
                  "\xD7\x92\xD7\x91\xD7\x90") &&
              P.rtl,
          "Hebrew runs right to left");
    CHECK(!strcmp(zui_para_layout(&P, "ZXV 1.0", 0, ZUI_BIDI_AUTO), "ZXV 1.0") && !P.rtl,
          "Latin is untouched");
    /* beh beh: initial + final forms, then reversed for display */
    CHECK(!strcmp(zui_para_layout(&P, "\xD8\xA8\xD8\xA8", 0, ZUI_BIDI_AUTO),
                  "\xEF\xBA\x90\xEF\xBA\x91"),
          "Arabic letters join, then reverse");
    /* ka + vowel sign i: the sign is drawn before its consonant */
    CHECK(!strcmp(zui_para_layout(&P, "\xE0\xA4\x95\xE0\xA4\xBF", 0, ZUI_BIDI_AUTO),
                  "\xE0\xA4\xBF\xE0\xA4\x95"),
          "Devanagari pre-base vowel moves before the consonant");
    /* an English sentence holding Hebrew in brackets keeps its order */
    CHECK(!strcmp(zui_para_layout(&P, "a (\xD7\x90\xD7\x91) b", 0, ZUI_BIDI_LTR),
                  "a (\xD7\x91\xD7\x90) b"),
          "an embedded right-to-left word reverses in place");
    CHECK(!strcmp(zui_para_layout(&P, 0, 0, ZUI_BIDI_AUTO), "") &&
              !strcmp(zui_para_layout(0, "x", 0, ZUI_BIDI_AUTO), ""),
          "NULL text or workspace gives an empty string");
    static zui_glyph_cache_t cache;
    zui_glyph_cache_init(&cache);
    ttf_font_t m;
    ttf_parse(&m, ZUI_TTF, sizeof ZUI_TTF);
    zui_text_t t = {&m, &cache, 20, false};
    CHECK(zui_text_para_width(&t, &P, "AA", ZUI_BIDI_AUTO) == 24, "width after layout");
    static uint32_t px[64 * 32];
    zui_surface_t s;
    zui_surface_init(&s, px, 64, 32, 64);
    CHECK(zui_text_para(&s, &t, &P, 2, 24, ZUI_TEXT, "AA", ZUI_BIDI_AUTO) == 26, "and drawing");
}

static void test_depth(void)
{
    zui_depth_t d;
    zui_depth_init(&d, ZUI_DEPTH_ANAGLYPH_AMBER_BLUE, 40);
    CHECK(d.view_dist_px == 2400 && d.ipd_px == 252, "defaults: 60 cm, 63 mm at 40 px/cm");

    /* held is at the glass for every magnitude and every eye */
    for (int m = 0; m < 500; m += 7) {
        CHECK(zui_role_z(ZUI_ROLE_HELD, m) == 0, "held z is 0");
        int32_t dx, dy;
        d.head_x = 333;
        d.head_y = -91;
        zui_layer_offset(&d, zui_role_z(ZUI_ROLE_HELD, m), -1, &dx, &dy);
        if (dx || dy) {
            CHECK(0, "held shifts by exactly 0");
            break;
        }
    }
    d.head_x = 0;
    d.head_y = 0;
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
    d.mode = ZUI_DEPTH_FLAT;
    d.head_x = 200;
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
            if (iabs((int) ((ab >> k) & 0xFF) - (int) gv) > 2) ok = 0;
            if (iabs((int) ((rc >> k) & 0xFF) - (int) gv) > 2) ok = 0;
        }
        if (!ok) {
            CHECK(0, "grey survives both anaglyph matrices");
            break;
        }
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
    CHECK(zui_anaglyph_px(ZUI_DEPTH_FLAT, 0x123456, 0xFFFFFF) == 0x123456,
          "non-anaglyph mode passes left through");

    zui_surface_t L, R, O;
    zui_surface_init(&L, buf, 64, 64, 64);
    zui_surface_init(&R, buf2, 64, 64, 64);
    zui_surface_init(&O, buf3, 64, 64, 64);
    zui_fill(&L, (zui_rect_t){0, 0, 64, 64}, ZUI_GOLD);
    zui_fill(&R, (zui_rect_t){0, 0, 64, 64}, ZUI_GOLD);
    zui_depth_init(&d, ZUI_DEPTH_FLAT, 40);
    CHECK(!zui_anaglyph(&d, &O, &L, &R), "anaglyph refuses a non-anaglyph mode");
    d.mode = ZUI_DEPTH_ANAGLYPH_AMBER_BLUE;
    CHECK(zui_anaglyph(&d, &O, &L, &R), "anaglyph composes");
    CHECK(buf3[0] == buf3[64 * 64 - 1], "uniform input gives uniform output");
    zui_surface_t small;
    zui_surface_init(&small, buf3, 32, 32, 64);
    CHECK(!zui_anaglyph(&d, &small, &L, &R), "size mismatch refused");
}

static void test_logic(void)
{
    CHECK(zui_contrast_x100(0xFFFFFF, 0x000000) == 2100, "white on black is 21:1");
    CHECK(zui_contrast_x100(0x777777, 0x777777) == 100, "same colour is 1:1");
    int held_ok = 1, forb_ok = 1, text_ok = 1, mirror_ok = 1, split_ok = 1;
    for (uint32_t t = 0; t < 64; t++) {
        lpx_t tag = (lpx_t) t;
        zui_look_t l = zui_look(tag);
        if (lpx_forbidden(tag)) {
            if (!l.forbidden || l.z_pt || l.shimmer || l.split ||
                zui_look_color(&l, ZUI_GOLD, ZUI_VOID, 17) != ZUI_GOLD)
                forb_ok = 0;
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
        static const uint32_t fg[] = {ZUI_TEXT, ZUI_TEXT_2, ZUI_MUTED, ZUI_GOLD};
        static const uint32_t bgs[] = {ZUI_VOID, ZUI_PANEL, ZUI_RAISED};
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 3; j++) {
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
    CHECK(tm.contrast > tn.contrast && tn.contrast > tx.contrast,
          "firmer evidence has more contrast");
    CHECK(tx.shimmer > 0 && tm.shimmer == 0, "only entropic shimmers");
    uint32_t a = zui_look_color(&tx, ZUI_GOLD, ZUI_VOID, 0);
    uint32_t b = zui_look_color(&tx, ZUI_GOLD, ZUI_VOID, 31);
    CHECK(a != b, "entropic shimmer moves with the phase tick");
    zui_look_t fm = zui_look(lpx_make(LPX_B_FALSE, LPX_P_NEGENTROPIC, LPX_PH_MANIFEST));
    uint32_t sh = zui_look_color(&fm, ZUI_TEXT, ZUI_VOID, 0);
    CHECK(zui_contrast_x100(sh, ZUI_VOID) < zui_contrast_x100(ZUI_TEXT, ZUI_VOID),
          "FALSE is shaded toward the void");
    uint32_t ti = zui_look_color(&tm, ZUI_GOLD, ZUI_VOID, 0);
    CHECK(zui_contrast_x100(ti, ZUI_VOID) > zui_contrast_x100(ZUI_GOLD, ZUI_VOID),
          "TRUE is tinted toward light");
    CHECK(zui_anti(0xD9A92E) == 0x2656D1, "anti-colour of gold is its complement");
}

static void test_spaces(void)
{
    zsp_place_t home = {ZSP_HOME, ZSP_OWNER}, cloud = {ZSP_CLOUDTOP, ZSP_OWNER};
    zsp_place_t team = {ZSP_TEAM, ZSP_MEMBER}, office_guest = {ZSP_OFFICE, ZSP_GUEST};
    zsp_place_t park = {ZSP_PARK, ZSP_PUBLIC}, hall = {ZSP_HALL, ZSP_OWNER};
    zsp_place_t cloud_visitor = {ZSP_CLOUDTOP, ZSP_PUBLIC}, home_other = {ZSP_HOME, ZSP_MEMBER};
    lpx_t t = zsp_cross(home, cloud);
    CHECK(lpx_base(t) == LPX_B_NEITHER, "home -> cloud top is held in the hall");
    CHECK(lpx_base(zsp_confirm(t)) == LPX_B_TRUE, "confirming a held crossing lets it through");
    CHECK(lpx_base(zsp_cross(cloud, home)) == LPX_B_TRUE, "toward more private proceeds");
    CHECK(lpx_base(zsp_cross(team, team)) == LPX_B_TRUE, "within a space proceeds");
    CHECK(lpx_base(zsp_cross(team, park)) == LPX_B_NEITHER, "team -> park is held");
    CHECK(lpx_base(zsp_cross(home, office_guest)) == LPX_B_FALSE,
          "a guest cannot put things in an office");
    CHECK(lpx_base(zsp_cross(cloud_visitor, home)) == LPX_B_TRUE,
          "a visitor may take a public thing home");
    CHECK(lpx_base(zsp_cross(home_other, home)) == LPX_B_FALSE,
          "nobody else can see into your home");
    CHECK(lpx_base(zsp_cross(home, hall)) == LPX_B_FALSE, "nothing is kept in the hall");
    CHECK(lpx_base(zsp_confirm(zsp_cross(home, office_guest))) == LPX_B_FALSE,
          "a refusal stays refused");
    CHECK(zsp_can_view(ZSP_PARK, ZSP_PUBLIC) && zsp_can_write(ZSP_PARK, ZSP_PUBLIC),
          "the park is open to all");
    CHECK(zsp_can_view(ZSP_CLOUDTOP, ZSP_PUBLIC) && !zsp_can_write(ZSP_CLOUDTOP, ZSP_PUBLIC),
          "anyone may look at a cloud top, only the owner arranges it");
    CHECK(!zsp_can_view((zsp_kind_t) 99, ZSP_OWNER) && !zsp_can_view(ZSP_HOME, (zsp_role_t) 9),
          "unknown kinds and roles have no rights");
    int mono = 1;
    for (int a = 0; a < 5; a++)
        for (int b = 0; b < 5; b++) {
            lpx_t x = zsp_cross((zsp_place_t){(zsp_kind_t) a, ZSP_OWNER},
                                (zsp_place_t){(zsp_kind_t) b, ZSP_OWNER});
            if ((b > a) != (lpx_base(x) == LPX_B_NEITHER)) mono = 0;
        }
    CHECK(mono, "for an owner, exactly the crossings toward more openness are held");

    zsp_rules_t vert = {ZSP_GOV_VERTICAL, 0}, flat = {ZSP_GOV_HORIZONTAL, 60};
    CHECK(lpx_base(zsp_decide(&vert, ZSP_OWNER, 5, 0, 0)) == LPX_B_TRUE,
          "vertical: the owner decides");
    CHECK(lpx_base(zsp_decide(&vert, ZSP_MEMBER, 5, 5, 0)) == LPX_B_NEITHER,
          "vertical: a member's proposal waits for the owner");
    CHECK(lpx_base(zsp_decide(&vert, ZSP_GUEST, 5, 0, 0)) == LPX_B_FALSE, "guests cannot propose");
    CHECK(lpx_base(zsp_decide(&flat, ZSP_MEMBER, 5, 3, 0)) == LPX_B_TRUE,
          "flat: 3 of 5 meets 60% with no objection");
    CHECK(lpx_base(zsp_decide(&flat, ZSP_MEMBER, 5, 2, 0)) == LPX_B_NEITHER,
          "flat: below quorum keeps gathering");
    CHECK(lpx_base(zsp_decide(&flat, ZSP_MEMBER, 5, 3, 1)) == LPX_B_BOTH,
          "flat: quorum with an objection is a dispute");
    CHECK(lpx_action(zsp_decide(&flat, ZSP_MEMBER, 5, 3, 1)) == LPX_ACT_ESCALATE,
          "a dispute goes to people");
    CHECK(lpx_base(zsp_decide(&flat, ZSP_MEMBER, 5, 1, 3)) == LPX_B_FALSE,
          "flat: fails once quorum is out of reach");
    CHECK(lpx_base(zsp_decide(&flat, ZSP_OWNER, 5, 0, 0)) == LPX_B_NEITHER,
          "flat: even the owner needs the members");
    CHECK(lpx_base(zsp_decide(&flat, ZSP_MEMBER, 0, 0, 0)) == LPX_B_FALSE,
          "an empty space decides nothing");
    zsp_rules_t maj = {ZSP_GOV_HORIZONTAL, 0};
    CHECK(lpx_base(zsp_decide(&maj, ZSP_MEMBER, 4, 2, 0)) == LPX_B_NEITHER &&
              lpx_base(zsp_decide(&maj, ZSP_MEMBER, 4, 3, 0)) == LPX_B_TRUE,
          "default quorum is a simple majority");
    CHECK(lpx_base(zsp_decide(&maj, ZSP_MEMBER, 3, 99, 99)) == LPX_B_TRUE,
          "votes are clamped to the member count");
}

static uint64_t fnv(const uint32_t *p, size_t n)
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

static uint64_t draw_chg(const zui_chiglet_t *c, uint32_t tick)
{
    zui_surface_t s;
    zui_surface_init(&s, buf, 64, 64, 64);
    zui_fill(&s, (zui_rect_t){0, 0, 64, 64}, ZUI_VOID);
    zui_chiglet_draw(&s, c, 32, 34, 14, tick);
    return fnv(buf, 64 * 64);
}

static void test_chiglet(void)
{
    zui_chiglet_t a = {{1, 2, 3, 4, 5, 6, 7, 8}, 1, ZUI_CHG_DECIDED, 0};
    zui_chiglet_t b = a;
    CHECK(draw_chg(&a, 3) == draw_chg(&b, 3), "same genes, same companion");
    uint64_t base = draw_chg(&a, 3);
    int all_genes_show = 1;
    for (int gi = 0; gi < 8; gi++) {
        zui_chiglet_t m = a;
        m.genes[gi] = (uint8_t) (m.genes[gi] + 1);
        if (draw_chg(&m, 3) == base) all_genes_show = 0;
    }
    CHECK(all_genes_show, "every gene changes the drawn form");
    b.generation = 3;
    CHECK(draw_chg(&b, 3) != base, "evolving adds halo rings");
    b = a;
    b.mood = ZUI_CHG_UNCERTAIN;
    CHECK(draw_chg(&b, 3) != base, "uncertain shows on its face");
    b = a;
    b.mood = ZUI_CHG_SPEAKING;
    CHECK(draw_chg(&b, 3) != base, "speaking opens its mouth");
    uint64_t speaking = draw_chg(&b, 3);
    b.mouth = 1;
    uint64_t shut = draw_chg(&b, 3);
    CHECK(shut != speaking, "mouth 1: shut while speaking");
    b.mouth = 255;
    uint64_t wide = draw_chg(&b, 3);
    CHECK(wide != shut && wide != speaking, "mouth 255: wide open");
    b.mouth = 128;
    CHECK(draw_chg(&b, 3) != wide && draw_chg(&b, 3) != shut, "mouth follows its level");
    b = a;
    b.mouth = 255;
    CHECK(draw_chg(&b, 3) != base, "the level opens it in any mood");
    CHECK(draw_chg(&a, 4) != base, "motes move with the tick");
    zui_surface_t s;
    zui_surface_init(&s, buf, 64, 64, 64);
    zui_fill(&s, (zui_rect_t){0, 0, 64, 64}, ZUI_VOID);
    uint64_t empty = fnv(buf, 64 * 64);
    zui_chiglet_draw(&s, &a, 32, 32, 4, 0);
    CHECK(fnv(buf, 64 * 64) == empty, "too small to draw draws nothing");
    zui_chiglet_draw(&s, &a, -500, 9000, 30, 0);
    CHECK(1, "drawing off-surface is clipped, not overrun");
    CHECK(zui_chiglet_accent(0) == ZUI_GOLD, "null companion falls back to gold");
}

static uint64_t draw_face(zui_face_t f, zui_health_t h, uint32_t tick)
{
    zui_surface_t s;
    zui_surface_init(&s, buf, 64, 64, 64);
    zui_fill(&s, (zui_rect_t){0, 0, 64, 64}, ZUI_VOID);
    zui_face_draw(&s, &f, h, 32, 32, 22, tick);
    return fnv(buf, 64 * 64);
}

static int face_eq(zui_face_t a, zui_face_t b)
{
    return a.mouth_curve == b.mouth_curve && a.mouth_open == b.mouth_open &&
           a.eye_open == b.eye_open && a.squint == b.squint && a.brow_tilt == b.brow_tilt &&
           a.brow_raise == b.brow_raise && a.blush == b.blush && a.tear == b.tear &&
           a.color == b.color;
}

static void test_emote(void)
{
    /* every emoji is its own face, and draws differently */
    int distinct = 1;
    uint64_t h[SWARM_EMO_COUNT];
    for (int e = 0; e < SWARM_EMO_COUNT; e++) {
        h[e] = draw_face(zui_face_of((swarm_emotion_t) e), ZUI_HEALTH_OK, 0);
        for (int k = 0; k < e; k++)
            if (h[k] == h[e]) distinct = 0;
    }
    CHECK(distinct, "all eight emoji draw as different faces");
    CHECK(draw_face(zui_face_of(SWARM_EMO_JOY), ZUI_HEALTH_OK, 0) == h[SWARM_EMO_JOY],
          "same face, same pixels");
    CHECK(face_eq(zui_face_of((swarm_emotion_t) 99), zui_face_of(SWARM_EMO_NEUTRAL)),
          "unknown emotion falls back to neutral");

    /* intensity is the Fibonacci charge */
    zui_face_t n = zui_face_of(SWARM_EMO_NEUTRAL);
    int zero_is_neutral = 1;
    for (int e = 0; e < SWARM_EMO_COUNT; e++)
        if (!face_eq(zui_face_feeling((swarm_feeling_t){(swarm_emotion_t) e, 0}), n))
            zero_is_neutral = 0;
    CHECK(zero_is_neutral, "intensity 0 shows the neutral face for every emotion");
    CHECK(face_eq(zui_face_feeling((swarm_feeling_t){SWARM_EMO_SADNESS, 5}),
                  zui_face_of(SWARM_EMO_SADNESS)),
          "intensity 5 (charge 8) is the full expression");
    int mono = 1;
    int16_t prev = 0;
    for (uint8_t k = 0; k <= 5; k++) {
        int16_t c = zui_face_feeling((swarm_feeling_t){SWARM_EMO_JOY, k}).mouth_curve;
        if (k && c <= prev) mono = 0;
        prev = c;
    }
    CHECK(mono, "the smile grows with every intensity step");
    CHECK(zui_face_feeling((swarm_feeling_t){SWARM_EMO_JOY, 3}).mouth_curve == 256 * 3 / 8,
          "intensity 3 moves the face exactly charge 3 / 8 of the way");

    /* morphing is a walk: exact ends, every frame in between real */
    zui_face_t a = zui_face_of(SWARM_EMO_CURIOUS), b = zui_face_of(SWARM_EMO_WORRY);
    CHECK(face_eq(zui_face_lerp(a, b, 0), a) && face_eq(zui_face_lerp(a, b, 256), b),
          "morph is exact at both ends");
    uint64_t mid = draw_face(zui_face_lerp(a, b, 128), ZUI_HEALTH_OK, 0);
    CHECK(mid != draw_face(a, ZUI_HEALTH_OK, 0) && mid != draw_face(b, ZUI_HEALTH_OK, 0),
          "the midpoint is a face of its own");
    CHECK(zui_emote_ease(0) == 0 && zui_emote_ease(256) == 256 && zui_emote_ease(128) == 128,
          "ease is exact and symmetric");
    int ease_mono = 1;
    for (uint32_t t = 1; t <= 256; t++)
        if (zui_emote_ease(t) < zui_emote_ease(t - 1)) ease_mono = 0;
    CHECK(ease_mono, "ease never goes backwards");

    zui_emote_t m;
    zui_emote_init(&m, (swarm_feeling_t){SWARM_EMO_CALM, 3}, 1000);
    zui_emote_set(&m, (swarm_feeling_t){SWARM_EMO_WORRY, 4}, 1000);
    zui_face_t w = zui_face_feeling((swarm_feeling_t){SWARM_EMO_WORRY, 4});
    zui_face_t c3 = zui_face_feeling((swarm_feeling_t){SWARM_EMO_CALM, 3});
    CHECK(face_eq(zui_emote_now(&m, 1000, 0), c3), "a morph starts from what was on screen");
    CHECK(face_eq(zui_emote_now(&m, 1000 + ZUI_EMOTE_MORPH_MS, 0), w), "and arrives on time");
    CHECK(face_eq(zui_emote_now(&m, 1000, 1), w), "reduce motion jumps straight to the new face");
    zui_face_t half = zui_emote_now(&m, 1000 + ZUI_EMOTE_MORPH_MS / 2, 0);
    zui_emote_set(&m, (swarm_feeling_t){SWARM_EMO_JOY, 2}, 1000 + ZUI_EMOTE_MORPH_MS / 2);
    CHECK(face_eq(zui_emote_now(&m, 1000 + ZUI_EMOTE_MORPH_MS / 2, 0), half),
          "a new feeling mid-morph turns from where the face is, no jump");
    zui_emote_init(&m, (swarm_feeling_t){SWARM_EMO_CALM, 3}, 0xFFFFFF00u);
    zui_emote_set(&m, (swarm_feeling_t){SWARM_EMO_JOY, 5}, 0xFFFFFF00u);
    CHECK(
        face_eq(zui_emote_now(&m, 0xFFFFFF00u + ZUI_EMOTE_MORPH_MS, 0), zui_face_of(SWARM_EMO_JOY)),
        "morph timing survives the millisecond clock wrapping");

    /* health is not feeling */
    uint64_t ok = draw_face(n, ZUI_HEALTH_OK, 0);
    uint64_t slow = draw_face(n, ZUI_HEALTH_SLOW, 0), fail = draw_face(n, ZUI_HEALTH_FAILING, 0),
             stop = draw_face(n, ZUI_HEALTH_STOPPED, 0);
    CHECK(ok != slow && ok != fail && ok != stop && slow != fail && fail != stop && slow != stop,
          "each health state looks different on the same face");
    CHECK(draw_face(n, ZUI_HEALTH_SLOW, 3) != slow, "the slow ring turns");
    CHECK(draw_face(n, ZUI_HEALTH_OK, 3) == ok,
          "a healthy face is still: no motion without meaning");

    /* the logic tag it shows with */
    lpx_t t = zui_emote_tag((swarm_feeling_t){SWARM_EMO_JOY, 5}, ZUI_HEALTH_OK);
    CHECK(lpx_base(t) == LPX_B_TRUE && lpx_pol(t) == LPX_P_NEGENTROPIC &&
              lpx_phase(t) == LPX_PH_MANIFEST,
          "strong joy, healthy: TRUE, converging, manifest");
    t = zui_emote_tag((swarm_feeling_t){SWARM_EMO_FRUSTRATION, 2}, ZUI_HEALTH_FAILING);
    CHECK(lpx_base(t) == LPX_B_FALSE && lpx_pol(t) == LPX_P_ENTROPIC &&
              lpx_phase(t) == LPX_PH_EMERGING,
          "mild frustration, failing: FALSE, diverging, emerging");
    t = zui_emote_tag((swarm_feeling_t){SWARM_EMO_WORRY, 0}, ZUI_HEALTH_STOPPED);
    CHECK(lpx_base(t) == LPX_B_NEITHER && lpx_phase(t) == LPX_PH_DISSOLVING && !lpx_forbidden(t),
          "no charge reads NEITHER; stopped dissolves; never forbidden");
    CHECK(strcmp(zui_emote_word(SWARM_EMO_CURIOUS), "curious") == 0 &&
              strcmp(zui_emote_word((swarm_emotion_t) 42), "neutral") == 0,
          "words for screen readers");

    /* off-surface and tiny draws are safe */
    zui_surface_t s;
    zui_surface_init(&s, buf, 64, 64, 64);
    zui_fill(&s, (zui_rect_t){0, 0, 64, 64}, ZUI_VOID);
    uint64_t empty = fnv(buf, 64 * 64);
    zui_face_draw(&s, &n, ZUI_HEALTH_OK, 32, 32, 3, 0);
    CHECK(fnv(buf, 64 * 64) == empty, "a face too small to read draws nothing");
    zui_face_draw(&s, &n, ZUI_HEALTH_FAILING, -900, 5000, 40, 0);
    CHECK(1, "drawing off-surface is clipped, not overrun");
}

static uint32_t seen_lit[8];
static int seen_calls;
static void rec_part(zui_surface_t *s, zui_rect_t b, uint32_t lit, void *ctx)
{
    (void) s;
    (void) b;
    seen_lit[(intptr_t) ctx] = lit;
    seen_calls++;
}

static void test_progress(void)
{
    zui_surface_t s;
    zui_surface_init(&s, buf, 64, 64, 64);
    zui_fill(&s, (zui_rect_t){0, 0, 64, 64}, ZUI_VOID);
    zui_rect_t tr = {4, 30, 56, 3};
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
    zui_fill(&s, (zui_rect_t){0, 0, 64, 64}, ZUI_VOID);
    zui_bar_draw(&s, tr, ZUI_BAR_GATHER, ZUI_BAR_UNKNOWN, 0, &hl, &hr);
    uint64_t a = fnv(buf, 64 * 64);
    int32_t w0 = hr - hl;
    zui_fill(&s, (zui_rect_t){0, 0, 64, 64}, ZUI_VOID);
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
    zui_launch_t l = {rec_part,
                      {rec_part, rec_part, rec_part, rec_part, rec_part},
                      {(void *) 7, (void *) 0, (void *) 1, (void *) 2, (void *) 3, (void *) 4},
                      5,
                      0};
    seen_calls = 0;
    zui_launch_draw(&L, (zui_rect_t){0, 0, 320, 200}, 256, &l, (zui_launch_state_t){0, 0, 0, 0}, 0);
    CHECK(seen_calls == 6 && seen_lit[0] == 0 && seen_lit[2] == 0 && seen_lit[7] == 0,
          "at the start every part and the mark are dim");
    zui_launch_draw(&L, (zui_rect_t){0, 0, 320, 200}, 256, &l, (zui_launch_state_t){150, 0, 0, 0},
                    0);
    CHECK(seen_lit[0] == 256 && seen_lit[4] == 256 && seen_lit[2] == 0,
          "outer parts light first, mirrored");
    zui_launch_draw(&L, (zui_rect_t){0, 0, 320, 200}, 256, &l, (zui_launch_state_t){256, 0, 0, 0},
                    0);
    CHECK(seen_lit[2] == 256 && seen_lit[7] == 0, "the centre part lights last; the mark waits");
    zui_launch_draw(&L, (zui_rect_t){0, 0, 320, 200}, 256, &l,
                    (zui_launch_state_t){256, 256, 128, 0}, 0);
    CHECK(seen_lit[7] == 256, "then the mark lights");
    uint64_t h1 = fnv(big, 320 * 200);
    zui_launch_draw(&L, (zui_rect_t){0, 0, 320, 200}, 256, &l,
                    (zui_launch_state_t){256, 256, 128, 0}, 0);
    CHECK(fnv(big, 320 * 200) == h1, "same state, same pixels");
    zui_launch_draw(&L, (zui_rect_t){0, 0, 320, 200}, 256, &l,
                    (zui_launch_state_t){256, 256, 200, 0}, 0);
    CHECK(fnv(big, 320 * 200) != h1, "the top bar moves with the rest of the load");
    zui_launch_t none = {0, {0}, {0}, 0, 0};
    zui_launch_draw(&L, (zui_rect_t){0, 0, 320, 200}, 256, &none,
                    (zui_launch_state_t){256, 256, 256, 0}, 0);
    CHECK(1, "a launch with no parts or painters still draws its bars");

    /* the vault: bolts lock as the top bar fills and throw back as it opens */
    zui_launch_draw(&L, (zui_rect_t){0, 0, 320, 200}, 256, &l,
                    (zui_launch_state_t){256, 256, 30, 0}, 0);
    uint64_t nb = fnv(big, 320 * 200);
    zui_launch_draw(&L, (zui_rect_t){0, 0, 320, 200}, 256, &l,
                    (zui_launch_state_t){256, 256, 120, 0}, 0);
    uint32_t seam = big[(200 / 10) * 320 + 160];
    CHECK(seam == ZUI_GOLD || seam == ZUI_GOLD_GLOW || seam == ZUI_PANEL,
          "the first bolt is locked across the seam");
    zui_launch_draw(&L, (zui_rect_t){0, 0, 320, 200}, 256, &l,
                    (zui_launch_state_t){256, 256, 30, 0}, 0);
    CHECK(fnv(big, 320 * 200) == nb, "no bolt before the bar reaches it");
    static uint32_t fpx[320 * 200], bpx[320 * 200], opx[320 * 200];
    zui_surface_t F, B, O;
    zui_surface_init(&F, fpx, 320, 200, 320);
    zui_surface_init(&B, bpx, 320, 200, 320);
    zui_surface_init(&O, opx, 320, 200, 320);
    zui_launch_draw(&F, (zui_rect_t){0, 0, 320, 200}, 256, &l,
                    (zui_launch_state_t){256, 256, 256, 32}, 0);
    uint64_t half_thrown = fnv(fpx, 320 * 200);
    zui_launch_draw(&F, (zui_rect_t){0, 0, 320, 200}, 256, &l,
                    (zui_launch_state_t){256, 256, 256, 64}, 0);
    CHECK(fnv(fpx, 320 * 200) != half_thrown, "the bolts move as they throw back");
    zui_fill(&B, (zui_rect_t){0, 0, 320, 200}, 0x123456u);
    zui_vault_draw(&O, (zui_rect_t){0, 0, 320, 200}, 256, &F, &B, 0);
    CHECK(fnv(opx, 320 * 200) == fnv(fpx, 320 * 200), "closed, the vault is the face itself");
    zui_vault_draw(&O, (zui_rect_t){0, 0, 320, 200}, 256, &F, &B, 64);
    CHECK(fnv(opx, 320 * 200) == fnv(fpx, 320 * 200), "the doors hold while the bolts throw");
    zui_vault_draw(&O, (zui_rect_t){0, 0, 320, 200}, 256, &F, &B, 160);
    CHECK((opx[100 * 320 + 160] & 0xFFFFFFu) != (fpx[100 * 320 + 160] & 0xFFFFFFu) &&
              opx[100 * 320 + 160] != ZUI_GOLD,
          "part-open, the centre shows what is behind");
    zui_vault_draw(&O, (zui_rect_t){0, 0, 320, 200}, 256, &F, &B, 256);
    int all = 1;
    for (int i = 0; i < 320 * 200; i++)
        if (opx[i] != 0x123456u) {
            all = 0;
            break;
        }
    CHECK(all, "fully open, only the loaded view remains");
    zui_vault_draw(&O, (zui_rect_t){0, 0, 320, 200}, 256, &F, 0, 256);
    CHECK(1, "a missing view is refused, not read");
}

static void test_foil(void)
{
    printf("--- foil: colour / anti-colour depth ---\n");
    zui_depth_t d;
    zui_depth_init(&d, ZUI_DEPTH_FOIL, 40);
    CHECK(d.foil_k > 0 && d.chroma_k > 24, "foil turns the cues up by default");
    CHECK(zui_foil_px(0x808080u, 0, 256) == 0x808080u &&
              zui_foil_px(0x808080u, 200, 256) == 0x808080u,
          "grey never shimmers");
    uint32_t g0 = zui_foil_px(ZUI_GOLD, 0, 256), g1 = zui_foil_px(ZUI_GOLD, 127, 256);
    CHECK(g0 != ZUI_GOLD && g1 != ZUI_GOLD && g0 != g1, "gold swings both ways across the cycle");
    CHECK((g1 & 0xFF) > (ZUI_GOLD & 0xFF), "toward its anti-colour (more blue) at the crest");
    uint32_t v1 = zui_foil_px(ZUI_VOID, 127, 256);
    int dv = (int) ((v1 >> 8) & 0xFF) - (int) ((ZUI_VOID >> 8) & 0xFF);
    int dg = (int) (g1 & 0xFF) - (int) (ZUI_GOLD & 0xFF);
    CHECK(dv < dg / 3, "the dark ground moves far less than gold");
    CHECK(zui_foil_px(ZUI_GOLD, 127, 0) == ZUI_GOLD, "strength 0 is identity");

    static uint32_t px[96 * 64], ref[96 * 64];
    zui_surface_t s;
    zui_surface_init(&s, px, 96, 64, 96);
    zui_fill(&s, (zui_rect_t){0, 0, 96, 64}, ZUI_VOID);
    zui_fill_rrect(&s, (zui_rect_t){16, 12, 64, 40}, 8, ZUI_GOLD, 255u);
    memcpy(ref, px, sizeof px);
    zui_foil_rect(&s, &d, (zui_rect_t){16, 12, 64, 40}, 8, 0);
    CHECK(memcmp(ref, px, sizeof px) == 0, "held at the glass (z 0): untouched");
    zui_depth_t flat;
    zui_depth_init(&flat, ZUI_DEPTH_FLAT, 40);
    zui_foil_rect(&s, &flat, (zui_rect_t){16, 12, 64, 40}, 8, 20);
    CHECK(memcmp(ref, px, sizeof px) == 0, "other modes: untouched");
    zui_foil_rect(&s, &d, (zui_rect_t){16, 12, 64, 40}, 8, 20);
    uint64_t a = fnv(px, 96 * 64);
    CHECK(memcmp(ref, px, sizeof px) != 0, "a raised thing shimmers");
    CHECK(px[0] == ZUI_VOID && px[63 * 96 + 95] == ZUI_VOID, "and nothing outside it changes");
    CHECK(((px[(12 + 20) * 96 + 14] >> 16) & 0xFF) > ((px[(12 + 20) * 96 + 14]) & 0xFF) + 8 &&
              (px[(12 + 20) * 96 + 14] & 0xFF) < (px[(12 + 20) * 96 + 81] & 0xFF),
          "warm fringe on the lit edge, cool anti-colour on the far edge");
    memcpy(px, ref, sizeof px);
    d.head_x = 120;
    zui_foil_rect(&s, &d, (zui_rect_t){16, 12, 64, 40}, 8, 20);
    CHECK(fnv(px, 96 * 64) != a, "tilting the view slides the sheen");
    memcpy(px, ref, sizeof px);
    d.head_x = 0;
    zui_foil_rect(&s, &d, (zui_rect_t){16, 12, 64, 40}, 8, 20);
    CHECK(fnv(px, 96 * 64) == a, "same view, same pixels");
    int32_t dx, dy;
    d.head_x = 200;
    zui_layer_offset(&d, 40, 0, &dx, &dy);
    CHECK(dx != 0, "foil moves layers with the head, like parallax");
    CHECK(zui_chroma_px(&d, 0x808080u, 30) != 0x808080u, "and tints near things warm");
}

static void test_login(void)
{
    printf("--- login after boot ---\n");
    char ini[3];
    zui_initials("Ana Maria", ini);
    CHECK(ini[0] == 'A' && ini[1] == 'M' && !ini[2], "initials from two words");
    zui_initials("michael", ini);
    CHECK(ini[0] == 'M' && !ini[1], "one word, upper-cased");
    zui_initials("  ", ini);
    CHECK(!ini[0], "nothing to take");
    zui_initials("j.r. tolkien", ini);
    CHECK(ini[0] == 'J' && ini[1] == 'R', "punctuation splits words");

    static zui_login_person_t ppl[100];
    static char names[100][12];
    for (int i = 0; i < 100; i++) {
        snprintf(names[i], sizeof names[i], "p%d", i);
        ppl[i] = (zui_login_person_t){names[i], 0, (uint8_t) (i % 3), i == 99};
    }
    static uint32_t px[480 * 320];
    zui_surface_t s;
    zui_surface_init(&s, px, 480, 320, 480);
    zui_rect_t a = {0, 0, 480, 320};
    zui_login_t l = {0};
    l.people = ppl;
    l.n = 100;
    l.chosen = 1;
    l.can_add = true; /* p1: passphrase */
    zui_login_draw(&s, a, 256, &l, 0, 0);
    uint64_t h0 = fnv(px, 480 * 320);
    zui_login_draw(&s, a, 256, &l, 0, 0);
    CHECK(fnv(px, 480 * 320) == h0,
          "a hundred people draw, and the same state gives the same pixels");
    l.typed = 5;
    zui_login_draw(&s, a, 256, &l, 0, 0);
    uint64_t h5 = fnv(px, 480 * 320);
    CHECK(h5 != h0, "typing shows dots");

    uint32_t idx = 999;
    /* the chosen person sits in the middle of the row; their neighbour is one step right */
    zui_login_hit_t h = ZUI_LOGIN_NOTHING;
    l.chosen = 50;
    for (int32_t y = 160; y < 320 && h != ZUI_LOGIN_PERSON; y++)
        h = zui_login_hit(a, 256, &l, 240, y, &idx);
    CHECK(h == ZUI_LOGIN_PERSON && idx == 50,
          "a long row scrolls to keep the chosen person in the middle");
    int32_t ry = 0;
    for (int32_t y = 160; y < 320; y++)
        if (zui_login_hit(a, 256, &l, 240, y, 0) == ZUI_LOGIN_PERSON) {
            ry = y;
            break;
        }
    CHECK(zui_login_hit(a, 256, &l, 240 + 112, ry + 4, &idx) == ZUI_LOGIN_PERSON && idx == 51,
          "one step right is the next person");
    l.chosen = 0;
    CHECK(zui_login_hit(a, 256, &l, 56, ry + 4, &idx) == ZUI_LOGIN_PERSON && idx == 0,
          "but it never scrolls past its first person");
    l.chosen = 1;
    l.chosen = 99;
    int32_t addx = -1, lastx = -1;
    for (int32_t x = 0; x < 480; x++) {
        zui_login_hit_t t = zui_login_hit(a, 256, &l, x, ry + 4, &idx);
        if (t == ZUI_LOGIN_ADD && addx < 0) addx = x;
        if (t == ZUI_LOGIN_PERSON && idx == 99) lastx = x;
    }
    CHECK(addx > lastx && lastx > 0 && addx < 480,
          "at the end of the row the last person, then Add person");
    l.can_add = false;
    int any_add = 0;
    for (int32_t x = 0; x < 480; x++)
        any_add |= zui_login_hit(a, 256, &l, x, ry + 4, 0) == ZUI_LOGIN_ADD;
    CHECK(!any_add, "no owner, no Add person");
    zui_login_person_t few[3] = {{"a", 0, 0, false}, {"b", 0, 0, false}, {"c", 0, 0, false}};
    zui_login_t lf = {0};
    lf.people = few;
    lf.n = 3;
    lf.chosen = 0;
    CHECK(zui_login_hit(a, 256, &lf, 240, ry + 4, &idx) == ZUI_LOGIN_PERSON && idx == 1,
          "a row that fits is centred as a whole");

    /* the lock decides the controls */
    int found_field = 0, found_enter = 0;
    l.chosen = 1;
    l.typed = 0;
    for (int32_t y = 0; y < 320; y += 2)
        for (int32_t x = 0; x < 480; x += 2) {
            zui_login_hit_t t = zui_login_hit(a, 256, &l, x, y, 0);
            found_field |= t == ZUI_LOGIN_FIELD;
            found_enter |= t == ZUI_LOGIN_ENTER;
        }
    CHECK(found_field && found_enter, "a passphrase seat has a field and an Enter arrow");
    l.chosen = 0;
    found_field = found_enter = 0;
    for (int32_t y = 0; y < 320; y += 2)
        for (int32_t x = 0; x < 480; x += 2) {
            zui_login_hit_t t = zui_login_hit(a, 256, &l, x, y, 0);
            found_field |= t == ZUI_LOGIN_FIELD;
            found_enter |= t == ZUI_LOGIN_ENTER;
        }
    CHECK(!found_field && found_enter, "an open seat has only Enter");
    l.chosen = 2;
    found_field = found_enter = 0;
    for (int32_t y = 0; y < 320; y += 2)
        for (int32_t x = 0; x < 480; x += 2) {
            zui_login_hit_t t = zui_login_hit(a, 256, &l, x, y, 0);
            found_field |= t == ZUI_LOGIN_FIELD;
            found_enter |= t == ZUI_LOGIN_ENTER;
        }
    CHECK(!found_field && !found_enter, "a passkey seat waits for the device's unlock");

    /* the chosen person's ring and name act on them: an open seat enters
     * (the same as its Enter button), a passphrase seat focuses the field,
     * and a passkey seat stays with the host's own unlock. The row keeps its
     * hits where the two overlap. At 480x320 the block lands at
     * x 214..266, y 130..238; the chosen row circle is centred (240, 234). */
    l.chosen = 0; /* ppl[0]: ZUI_LOCK_NONE */
    CHECK(zui_login_hit(a, 256, &l, 240, 156, 0) == ZUI_LOGIN_ENTER,
          "an open seat's ring enters, not just its Enter button");
    CHECK(zui_login_hit(a, 256, &l, 240, 186, 0) == ZUI_LOGIN_ENTER,
          "an open seat's block below the ring enters too");
    l.chosen = 1; /* ppl[1]: ZUI_LOCK_PASS */
    CHECK(zui_login_hit(a, 256, &l, 240, 156, 0) == ZUI_LOGIN_FIELD,
          "a passphrase seat's ring focuses the field");
    l.chosen = 2; /* ppl[2]: ZUI_LOCK_PASSKEY */
    CHECK(zui_login_hit(a, 256, &l, 240, 156, 0) == ZUI_LOGIN_NOTHING,
          "a passkey seat's ring stays inert");
    l.chosen = 0;
    CHECK(zui_login_hit(a, 256, &l, 264, 234, &idx) == ZUI_LOGIN_PERSON && idx == 2,
          "the row still wins where the block reaches it");

    l.chosen = 1;
    l.wrong = true;
    l.shake_q8 = 40;
    zui_login_draw(&s, a, 256, &l, 0, 0);
    uint64_t hs = fnv(px, 480 * 320);
    l.shake_q8 = 0;
    zui_login_draw(&s, a, 256, &l, 0, 0);
    CHECK(fnv(px, 480 * 320) != hs, "a wrong try shakes the field");
    l.n = 0;
    zui_login_draw(&s, a, 256, &l, 0, 0);
    CHECK(zui_login_hit(a, 256, &l, 240, 160, 0) == ZUI_LOGIN_NOTHING,
          "an empty device still draws");
    zui_login_draw(0, a, 256, &l, 0, 0);
    CHECK(1, "a null surface is ignored");
}

static int lum_of(uint32_t c)
{
    return (int) ((((c >> 16) & 0xFF) * 77 + ((c >> 8) & 0xFF) * 150 + (c & 0xFF) * 29) >> 8);
}

static int sat_of(uint32_t c)
{
    int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    int mx = r > g ? (r > b ? r : b) : (g > b ? g : b),
        mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    return mx - mn;
}

static void test_contrast(void)
{
    printf("--- tokens: WCAG contrast ---\n");
    const uint32_t grounds[3] = {ZUI_VOID, ZUI_PANEL, ZUI_RAISED};
    int text_ok = 1, muted_ok = 1, edge_ok = 1;
    for (int i = 0; i < 3; i++) {
        text_ok &= zui_contrast_x100(ZUI_TEXT, grounds[i]) >= 700;
        muted_ok &= zui_contrast_x100(ZUI_MUTED, grounds[i]) >= 450;
        edge_ok &= zui_contrast_x100(ZUI_EDGE, grounds[i]) >= 300;
    }
    CHECK(text_ok, "body text reaches 7:1 (AAA) on every ground");
    CHECK(muted_ok, "muted text reaches 4.5:1 on every ground");
    CHECK(zui_contrast_x100(ZUI_FAINT, ZUI_VOID) >= 450 &&
              zui_contrast_x100(ZUI_FAINT, ZUI_PANEL) >= 450,
          "faint text reaches 4.5:1 on VOID and PANEL");
    CHECK(zui_contrast_x100(ZUI_FAINT, ZUI_RAISED) >= 300, "and 3:1 on RAISED (large/disabled)");
    CHECK(edge_ok, "control outlines reach 3:1 on every ground");
    CHECK(zui_contrast_x100(ZUI_GOLD, ZUI_VOID) >= 700 &&
              zui_contrast_x100(ZUI_VOID, ZUI_GOLD) >= 700,
          "gold on the ground, and the ground on gold buttons, reach 7:1");
}

static void test_pxl(void)
{
    printf("--- pxl: colour/anti-colour x tint/shade per pixel ---\n");
    const uint32_t c = 0x3C8A5Au;
    CHECK(zui_pxl_px(c, 0, 0) == c, "a 0, b 0 is identity");
    CHECK(lum_of(zui_pxl_px(c, 0, 120)) > lum_of(c), "b + tints (lighter)");
    CHECK(lum_of(zui_pxl_px(c, 0, -120)) < lum_of(c), "b - shades (darker)");
    CHECK(sat_of(zui_pxl_px(c, 120, 0)) > sat_of(c), "a + leans into its own colour");
    uint32_t anti = zui_pxl_px(c, -200, 0);
    CHECK(sat_of(anti) < sat_of(c) && ((anti >> 16) & 0xFF) > ((c >> 16) & 0xFF),
          "a - leans toward the complement (green loses, red gains)");
    CHECK(zui_pxl_px(0x808080u, 200, 0) == 0x808080u, "grey has no colour to lean into");

    static zui_pxl_lut_t lut, flat;
    zui_pxl_build(&lut, 256);
    zui_pxl_build(&flat, 0);
    lpx_t T = lpx_make(LPX_B_TRUE, LPX_P_NEGENTROPIC, LPX_PH_MANIFEST);
    lpx_t F = lpx_make(LPX_B_FALSE, LPX_P_NEGENTROPIC, LPX_PH_MANIFEST);
    lpx_t N = lpx_make(LPX_B_NEITHER, LPX_P_NEUTRAL, LPX_PH_MANIFEST);
    lpx_t B = lpx_make(LPX_B_BOTH, LPX_P_NEGENTROPIC, LPX_PH_MANIFEST);
    lpx_t X = lpx_make(LPX_B_TRUE, LPX_P_FORBIDDEN, LPX_PH_MANIFEST);
    int zn = ZUI_PXL_ZBINS - 4, zf = 3, z0 = ZUI_PXL_ZBINS / 2;
    CHECK(lut.cell[T][zn].a > 0 && lut.cell[T][zn].b > 0, "TRUE near: colour and tint");
    CHECK(lut.cell[F][zf].a < 0 && lut.cell[F][zf].b < 0, "FALSE far: anti-colour and shade");
    CHECK(lut.cell[N][z0].a == 0 && lut.cell[N][z0].b == 0 && !lut.cell[N][z0].glut,
          "NEITHER is held at the glass");
    CHECK(lut.cell[B][z0].glut && lut.cell[B][z0].a > 0, "BOTH is a glut");
    int xz = 1;
    for (int z = 0; z < ZUI_PXL_ZBINS; z++)
        xz &= lut.cell[X][z].a == 0 && lut.cell[X][z].b == 0 && !lut.cell[X][z].glut;
    CHECK(xz, "FORBIDDEN stays flat at every depth");
    int fz = 1;
    for (int t = 0; t < 64; t++)
        for (int z = 0; z < ZUI_PXL_ZBINS; z++)
            fz &= flat.cell[t][z].a == 0 && flat.cell[t][z].b == 0;
    CHECK(fz, "strength 0 builds the identity table");

    enum { W = 64, H = 48 };
    static uint32_t px[W * H], ref[W * H];
    static uint8_t tag[W * H];
    static int8_t zb[W * H];
    for (int i = 0; i < W * H; i++) ref[i] = 0x2A6A47u;
    zui_surface_t s;
    zui_surface_init(&s, px, W, H, W);
    zui_plane_t pl;
    zui_plane_init(&pl, tag, zb, W, H, W);
    zui_plane_clear(&pl);
    memcpy(px, ref, sizeof px);
    zui_pxl_apply(&s, &pl, &lut, -181, -181, 0);
    CHECK(memcmp(px, ref, sizeof px) == 0, "a cleared plane leaves the frame untouched");

    zui_plane_mark(&pl, (zui_rect_t){16, 12, 32, 24}, 4, T, 96);
    CHECK(tag[0] == N && zb[0] == 0 && tag[24 * W + 32] == T && zb[24 * W + 32] == 96,
          "mark stamps inside only");
    CHECK(tag[12 * W + 16] == N, "rounded corner left alone");
    memcpy(px, ref, sizeof px);
    zui_pxl_apply(&s, &pl, &flat, 0, 0, 0);
    CHECK(memcmp(px, ref, sizeof px) == 0, "strength 0 with straight-on light is identity");
    memcpy(px, ref, sizeof px);
    zui_pxl_apply(&s, &pl, &lut, -181, -181, 0);
    CHECK(px[2 * W + 2] == ref[0], "flat ground away from the card untouched");
    CHECK(lum_of(px[24 * W + 32]) > lum_of(ref[0]), "the raised card comes forward (tinted)");
    int lit = lum_of(px[12 * W + 32]), shadow = lum_of(px[35 * W + 32]);
    CHECK(lit > lum_of(px[24 * W + 32]), "edge facing the light is lit");
    CHECK(shadow < lit, "far edge is darker than the lit edge");
    CHECK(lum_of(px[36 * W + 32]) < lum_of(ref[0]), "shadow falls on the ground beyond");

    zui_plane_clear(&pl);
    zui_plane_mark(&pl, (zui_rect_t){0, 0, W, H}, 0, B, 0);
    memcpy(px, ref, sizeof px);
    zui_pxl_apply(&s, &pl, &lut, 0, 0, 0);
    CHECK(px[10 * W + 10] != px[10 * W + 11] && px[10 * W + 10] == px[11 * W + 11],
          "BOTH interleaves colour and anti-colour by pixel");
    uint32_t a0 = px[10 * W + 10];
    memcpy(px, ref, sizeof px);
    zui_pxl_apply(&s, &pl, &lut, 0, 0, 32);
    CHECK(px[10 * W + 11] == a0, "the interleave swaps as the tick moves");
    memcpy(ref, px, sizeof px);
    for (int i = 0; i < W * H; i++) px[i] = 0x2A6A47u;
    zui_pxl_apply(&s, &pl, &lut, 0, 0, 32);
    CHECK(memcmp(px, ref, sizeof px) == 0, "deterministic");
    zui_plane_mark(&pl, (zui_rect_t){8, 6, 30, 20}, 3, T, 80);
    zui_plane_mark(&pl, (zui_rect_t){30, 20, 26, 22}, 0, F, -60);
    static uint32_t band[W * H];
    for (int i = 0; i < W * H; i++) px[i] = band[i] = 0x2A6A47u + (uint32_t) (i % 7) * 0x030201u;
    zui_pxl_apply(&s, &pl, &lut, 181, -181, 16);
    zui_surface_t sb;
    zui_surface_init(&sb, band, W, H, W);
    zui_pxl_apply_rows(&sb, &pl, &lut, 181, -181, 16, 30, H + 9);
    zui_pxl_apply_rows(&sb, &pl, &lut, 181, -181, 16, -4, 13);
    zui_pxl_apply_rows(&sb, &pl, &lut, 181, -181, 16, 13, 30);
    zui_pxl_apply_rows(&sb, &pl, &lut, 181, -181, 16, 20, 20);
    CHECK(memcmp(px, band, sizeof px) == 0, "bands in any order give exactly the full frame");
    zui_pxl_apply(NULL, &pl, &lut, 0, 0, 0);
    zui_pxl_apply(&s, NULL, &lut, 0, 0, 0);
    zui_plane_mark(&pl, (zui_rect_t){-50, -50, 400, 400}, 0, T, 500);
    CHECK(zb[0] == 127 && zb[W * H - 1] == 127, "out-of-range marks clip and clamp");

    /* axis C: round <-> angular */
    lpx_t RND = lpx_make(LPX_B_TRUE, LPX_P_NEGENTROPIC, LPX_PH_MANIFEST);
    lpx_t ANG = lpx_make(LPX_B_TRUE, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
    lpx_t ENT = lpx_make(LPX_B_TRUE, LPX_P_ENTROPIC, LPX_PH_MANIFEST);
    zui_pxl_style_t sr = zui_pxl_style(RND), sa = zui_pxl_style(ANG), sx = zui_pxl_style(X);
    CHECK(sr.round_q8 == 256 && sr.edge_px == 3, "settled, cooperative things are round");
    CHECK(sa.round_q8 < sr.round_q8 && sa.edge_px == 1, "unformed things are angular");
    CHECK(zui_pxl_style(ENT).edge_px == 1, "entropic things are angular");
    CHECK(sx.round_q8 == 0 && sx.hold_ms == 0, "FORBIDDEN is square and still");
    CHECK(zui_pxl_radius(RND, 16) == 16 && zui_pxl_radius(ANG, 16) < 16 &&
              zui_pxl_radius(X, 16) < zui_pxl_radius(ANG, 16) && zui_pxl_radius(RND, 0) == 0,
          "corner radius follows the tag");
    /* the bevel: a round card ramps over 3 px, an angular one is a single lip */
    int32_t ramp[2][5];
    for (int k = 0; k < 2; k++) {
        zui_plane_clear(&pl);
        zui_plane_mark(&pl, (zui_rect_t){16, 12, 32, 24}, 0, k ? ANG : RND, 64);
        for (int i = 0; i < W * H; i++) px[i] = 0x2A6A47u;
        zui_pxl_apply(&s, &pl, &lut, 0, -181, 0);
        uint32_t mid = px[24 * W + 32];
        for (int d = 0; d < 5; d++) ramp[k][d] = lum_of(px[(12 + d) * W + 32]) - lum_of(mid);
    }
    CHECK(ramp[1][0] > 0 && ramp[1][1] == 0, "angular: one crisp lit lip");
    CHECK(ramp[0][0] > ramp[0][1] && ramp[0][1] > ramp[0][2] && ramp[0][2] > 0 && ramp[0][3] == 0,
          "round: the lit lip ramps over 3 px");
    CHECK(ramp[0][0] == ramp[1][0], "both start from the same height");

    /* axis D: duration <-> briefness */
    lpx_t LAST = RND, BRIEF = lpx_make(LPX_B_TRUE, LPX_P_ENTROPIC, LPX_PH_DISSOLVING);
    zui_pxl_style_t sl = zui_pxl_style(LAST), sbr = zui_pxl_style(BRIEF);
    CHECK(!sl.brief && sbr.brief && sl.hold_ms > 4 * sbr.hold_ms,
          "manifest lasts, dissolving is brief");
    CHECK(zui_pxl_ease(LAST, 0) == 0 && zui_pxl_ease(BRIEF, 0) == 0, "every effect starts at 0");
    CHECK(zui_pxl_ease(LAST, 5000) == 256 && zui_pxl_ease(BRIEF, 5000) == 256, "and ends at 256");
    CHECK(zui_pxl_ease(X, 0) == 256 && zui_pxl_decay(X, 0) == 0, "FORBIDDEN never animates");
    int mono = 1;
    for (uint32_t t = 0, prev = 0; t <= 1300; t += 10) {
        uint32_t v = (uint32_t) zui_pxl_ease(LAST, t);
        mono &= v >= prev && v <= 256;
        prev = v;
    }
    CHECK(mono, "easing never runs backwards");
    uint32_t q = (uint32_t) sbr.hold_ms / 4, ql = (uint32_t) sl.hold_ms / 4;
    CHECK(zui_pxl_ease(BRIEF, q) > 128, "brief snaps: past halfway in the first quarter");
    CHECK(zui_pxl_ease(LAST, ql) < 64, "lasting settles in slowly");
    CHECK(zui_pxl_ease(LAST, (uint32_t) sl.hold_ms / 2) >= 120 &&
              zui_pxl_ease(LAST, (uint32_t) sl.hold_ms / 2) <= 136,
          "and is halfway at half time");
    CHECK(zui_pxl_decay(BRIEF, q) + zui_pxl_ease(BRIEF, q) == 256, "decay is what is left");
    static zui_pxl_lut_t l2;
    zui_pxl_build(&l2, 256);
    CHECK(l2.cell[BRIEF][16].tsh < l2.cell[LAST][16].tsh, "brief lustre moves faster");

    /* video: depth from the frame's own brightness */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) px[y * W + x] = x < W / 2 ? 0x101010u : 0xF0F0F0u;
    zui_plane_clear(&pl);
    zui_plane_mark_luma(&pl, &s, (zui_rect_t){8, 8, 48, 32}, LAST, 256);
    CHECK(zb[20 * W + 50] > 40 && zb[20 * W + 12] < -40 && tag[20 * W + 50] == LAST,
          "bright parts of a frame come forward, dark parts recede");
    CHECK(zb[2 * W + 2] == 0 && tag[2 * W + 2] == N, "outside the clip untouched");
    zui_plane_mark_luma(&pl, &s, (zui_rect_t){8, 8, 48, 32}, LAST, 0);
    CHECK(zb[20 * W + 50] == 0, "z_k 0 leaves the clip flat");
}

int main(void)
{
    printf("=== zui: layout, shapes, text plumbing, depth ===\n");
    test_layout();
    test_shapes();
    test_text_plumbing();
    test_fallback();
    test_para();
    test_depth();
    test_logic();
    test_spaces();
    test_chiglet();
    test_emote();
    test_progress();
    test_login();
    test_foil();
    test_pxl();
    test_contrast();
    printf("%d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
