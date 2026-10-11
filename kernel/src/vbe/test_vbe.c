/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_vbe.c — host test for the VBE linear-framebuffer driver (vbe.c).
 *
 * The "framebuffer" is a host array with guard words on both sides, so any
 * write outside width*height is caught even in a build without ASan. Covers:
 *   - vbe_init / vbe_init_fb: every state and mode field, pitch and size for
 *     32/24/16 bpp, and the 0xE0000000 default base;
 *   - set/get pixel: row-major indexing and all four clipping edges;
 *   - fill_rect / clear_screen: exact coverage against a predicate, for
 *     interior, clipped, empty and negative rectangles;
 *   - draw_line: Bresenham runs stepped by hand (shallow, steep, diagonal,
 *     reversed, degenerate and off-screen);
 *   - draw_char / draw_string / draw_text_ex: the ASCII-32 font index, glyph
 *     bits for 'A', the out-of-range fallback to the blank glyph, a..z
 *     folding, integer scaling, transparent backgrounds and NULL guards;
 *   - the whole 128-glyph font table, pinned by a golden hash of its
 *     rendering, so a single flipped bit in any glyph fails.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "vbe.h"

static int failures = 0;
static int checks = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s (line %d)\n", m, __LINE__);                                          \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

/* ===== fixture: a W x H framebuffer with guard words on both sides ===== */
#define W      40
#define H      24
#define GUARD  64
#define POISON 0xDEADBEEFu
#define BGC    0x00112233u
#define FGC    0x00FFEEDDu
#define MARK   0x00ABCDEFu

static uint32_t g_mem[GUARD + W * H + GUARD];
static uint32_t *const g_fb = g_mem + GUARD;
static vbe_state_t g_v;

static void fx_reset(uint32_t fill)
{
    for (uint32_t i = 0; i < GUARD; i++) {
        g_mem[i] = POISON;
        g_mem[GUARD + W * H + i] = POISON;
    }
    for (uint32_t i = 0; i < W * H; i++) g_fb[i] = fill;
    memset(&g_v, 0, sizeof g_v);
    vbe_init_fb(&g_v, W, H, 32, (uintptr_t) g_fb);
}

static bool guards_intact(void)
{
    for (uint32_t i = 0; i < GUARD; i++)
        if (g_mem[i] != POISON || g_mem[GUARD + W * H + i] != POISON) return false;
    return true;
}

static uint32_t px(int x, int y)
{
    return g_fb[y * W + x];
}

static int count_color(uint32_t c)
{
    int n = 0;
    for (int i = 0; i < W * H; i++) n += g_fb[i] == c;
    return n;
}

/* ===== init ===== */
static void test_init(void)
{
    vbe_state_t v;
    memset(&v, 0x5A, sizeof v);
    vbe_init(&v, 1024, 768, 32);
    CHECK(v.width == 1024 && v.height == 768 && v.bpp == 32, "init: geometry stored");
    CHECK(v.pitch == 4096u, "init: pitch = 1024*4");
    CHECK(v.fb_size == 3145728u, "init: fb_size = 1024*768*4");
    CHECK((uintptr_t) v.fb == (uintptr_t) 0xE0000000u, "init: default base 0xE0000000");
    CHECK(v.mode.framebuffer_addr == (uintptr_t) 0xE0000000u, "init: mode.framebuffer_addr");
    CHECK(v.mode.width == 1024 && v.mode.height == 768 && v.mode.bpp == 32, "init: mode geometry");
    CHECK(v.mode.pitch == 4096u, "init: mode.pitch mirrors pitch");
    CHECK(v.mode.valid == true, "init: mode valid");

    vbe_init_fb(&v, 800, 600, 24, (uintptr_t) 0x12345678u);
    CHECK(v.pitch == 2400u && v.mode.pitch == 2400u, "init_fb: 24bpp pitch = 800*3");
    CHECK(v.fb_size == 1440000u, "init_fb: 24bpp size = 800*600*3");
    CHECK((uintptr_t) v.fb == (uintptr_t) 0x12345678u, "init_fb: fb = given address");
    CHECK(v.mode.framebuffer_addr == (uintptr_t) 0x12345678u, "init_fb: mode address");
    CHECK(v.bpp == 24 && v.mode.bpp == 24, "init_fb: bpp 24 stored twice");

    vbe_init_fb(&v, 640, 480, 16, (uintptr_t) 0x1000u);
    CHECK(v.pitch == 1280u && v.fb_size == 614400u, "init_fb: 16bpp pitch/size");
    CHECK(v.mode.width == 640 && v.mode.height == 480, "init_fb: mode geometry 640x480");

    /* bytes per pixel is bpp/8 exactly: 8 bpp -> 1, 64 bpp -> 8 */
    vbe_init_fb(&v, 10, 2, 8, (uintptr_t) 0x3000u);
    CHECK(v.pitch == 10u && v.fb_size == 20u, "init_fb: 8bpp pitch 10, size 20");
    vbe_init_fb(&v, 10, 2, 64, (uintptr_t) 0x3000u);
    CHECK(v.pitch == 80u && v.mode.pitch == 80u && v.fb_size == 160u,
          "init_fb: 64bpp pitch 80, size 160");

    /* the size product is formed in 32 bits from 16-bit dims: 4096*4096*4 = 64 MiB */
    vbe_init_fb(&v, 4096, 4096, 32, (uintptr_t) 0x2000u);
    CHECK(v.fb_size == 67108864u, "init_fb: 4096x4096x4 = 64 MiB, no 16-bit truncation");
    CHECK(v.pitch == 16384u, "init_fb: pitch 16384");
}

/* ===== pixels ===== */
static void test_pixels(void)
{
    fx_reset(0);
    vbe_set_pixel(&g_v, 3, 2, FGC);
    CHECK(g_fb[2 * W + 3] == FGC, "set_pixel: (3,2) is fb[2*W+3]");
    CHECK(count_color(FGC) == 1, "set_pixel: exactly one pixel written");
    CHECK(vbe_get_pixel(&g_v, 3, 2) == FGC, "get_pixel: reads back (3,2)");
    CHECK(vbe_get_pixel(&g_v, 2, 3) == 0, "get_pixel: (2,3) is a different pixel");

    vbe_set_pixel(&g_v, 0, 0, 1u);
    vbe_set_pixel(&g_v, W - 1, 0, 2u);
    vbe_set_pixel(&g_v, 0, H - 1, 3u);
    vbe_set_pixel(&g_v, W - 1, H - 1, 4u);
    CHECK(g_fb[0] == 1u && g_fb[W - 1] == 2u, "set_pixel: top corners");
    CHECK(g_fb[(H - 1) * W] == 3u && g_fb[W * H - 1] == 4u, "set_pixel: bottom corners");
    CHECK(vbe_get_pixel(&g_v, W - 1, H - 1) == 4u && vbe_get_pixel(&g_v, 0, 0) == 1u,
          "get_pixel: corners read back");

    fx_reset(0);
    vbe_set_pixel(&g_v, -1, 0, FGC);
    vbe_set_pixel(&g_v, W, 0, FGC);
    vbe_set_pixel(&g_v, 0, -1, FGC);
    vbe_set_pixel(&g_v, 0, H, FGC);
    vbe_set_pixel(&g_v, W, H, FGC);
    vbe_set_pixel(&g_v, -1, -1, FGC);
    vbe_set_pixel(&g_v, 100000, 5, FGC);
    vbe_set_pixel(&g_v, 5, -100000, FGC);
    CHECK(count_color(FGC) == 0, "set_pixel: every out-of-bounds write dropped");
    CHECK(guards_intact(), "set_pixel: guards intact");

    for (int i = 0; i < W * H; i++) g_fb[i] = MARK;
    CHECK(vbe_get_pixel(&g_v, -1, 0) == 0 && vbe_get_pixel(&g_v, W, 0) == 0,
          "get_pixel: x out of range reads 0");
    CHECK(vbe_get_pixel(&g_v, 0, -1) == 0 && vbe_get_pixel(&g_v, 0, H) == 0,
          "get_pixel: y out of range reads 0");
    CHECK(vbe_get_pixel(&g_v, W - 1, H - 1) == MARK, "get_pixel: last in-range pixel reads");
}

/* ===== fill_rect against a predicate ===== */
static bool rect_matches(int x, int y, int w, int h, uint32_t c, uint32_t bg)
{
    for (int yy = 0; yy < H; yy++)
        for (int xx = 0; xx < W; xx++) {
            bool in = xx >= x && xx < x + w && yy >= y && yy < y + h;
            if (px(xx, yy) != (in ? c : bg)) return false;
        }
    return guards_intact();
}

static void test_fill_rect(void)
{
    static const int cases[][4] = {
        {2, 3, 5, 4},               /* interior */
        {0, 0, W, H},               /* exactly full */
        {-3, -2, 6, 5},             /* clipped top-left */
        {W - 4, H - 3, 9, 8},       /* clipped bottom-right */
        {-10, -10, W + 20, H + 20}, /* larger than screen */
        {0, 5, W, 1},               /* one full row */
        {7, 0, 1, H},               /* one full column */
        {W - 1, H - 1, 1, 1},       /* last pixel */
        {0, 0, 1, 1},               /* first pixel */
    };
    char msg[96];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        fx_reset(BGC);
        vbe_fill_rect(&g_v, cases[i][0], cases[i][1], cases[i][2], cases[i][3], FGC);
        snprintf(msg, sizeof msg, "fill_rect: case %u (%d,%d %dx%d) exact coverage", i, cases[i][0],
                 cases[i][1], cases[i][2], cases[i][3]);
        CHECK(rect_matches(cases[i][0], cases[i][1], cases[i][2], cases[i][3], FGC, BGC), msg);
    }
    fx_reset(BGC);
    vbe_fill_rect(&g_v, 2, 3, 5, 4, FGC);
    CHECK(count_color(FGC) == 20, "fill_rect: 5x4 interior writes 20 pixels");

    /* empty / degenerate rectangles write nothing */
    static const int empty[][4] = {
        {3, 3, 0, 5}, {3, 3, 5, 0},  {3, 3, -2, 5}, {3, 3, 5, -2},        {W, 0, 5, 5},
        {0, H, 5, 5}, {-5, 0, 5, 5}, {0, -5, 5, 5}, {W + 3, H + 3, 4, 4},
    };
    for (unsigned i = 0; i < sizeof empty / sizeof empty[0]; i++) {
        fx_reset(BGC);
        vbe_fill_rect(&g_v, empty[i][0], empty[i][1], empty[i][2], empty[i][3], FGC);
        snprintf(msg, sizeof msg, "fill_rect: empty case %u writes nothing", i);
        CHECK(count_color(BGC) == W * H && guards_intact(), msg);
    }

    fx_reset(BGC);
    vbe_clear_screen(&g_v, MARK);
    CHECK(count_color(MARK) == W * H, "clear_screen: every pixel");
    CHECK(guards_intact(), "clear_screen: nothing beyond the screen");
}

/* ===== Bresenham, stepped by hand ===== */
typedef struct {
    int x, y;
} pt_t;

static bool line_is(const pt_t *pts, int n)
{
    int lit = 0;
    for (int i = 0; i < n; i++)
        if (px(pts[i].x, pts[i].y) != FGC) return false;
    lit = count_color(FGC);
    return lit == n && guards_intact();
}

static void test_lines(void)
{
    /* (0,0)->(9,3): dx=9 dy=3 err=6; stepped by hand */
    static const pt_t shallow[] = {{0, 0}, {1, 0}, {2, 1}, {3, 1}, {4, 1},
                                   {5, 2}, {6, 2}, {7, 2}, {8, 3}, {9, 3}};
    fx_reset(0);
    vbe_draw_line(&g_v, 0, 0, 9, 3, FGC);
    CHECK(line_is(shallow, 10), "line: (0,0)->(9,3) exact 10-pixel run");
    fx_reset(0);
    vbe_draw_line(&g_v, 9, 3, 0, 0, FGC);
    CHECK(line_is(shallow, 10), "line: reversed (9,3)->(0,0) same run");

    /* (0,0)->(2,7): dx=2 dy=7 err=-5 */
    static const pt_t steep[] = {{0, 0}, {0, 1}, {1, 2}, {1, 3}, {1, 4}, {1, 5}, {2, 6}, {2, 7}};
    fx_reset(0);
    vbe_draw_line(&g_v, 0, 0, 2, 7, FGC);
    CHECK(line_is(steep, 8), "line: steep (0,0)->(2,7) exact 8-pixel run");

    /* mirrored in x: (12,0)->(10,7) */
    static const pt_t steep_m[] = {{12, 0}, {12, 1}, {11, 2}, {11, 3},
                                   {11, 4}, {11, 5}, {10, 6}, {10, 7}};
    fx_reset(0);
    vbe_draw_line(&g_v, 12, 0, 10, 7, FGC);
    CHECK(line_is(steep_m, 8), "line: steep mirrored (12,0)->(10,7)");

    /* ties: 2*err == -dy steps y only; 2*err == dx steps x only */
    static const pt_t tie_y[] = {{0, 0}, {0, 1}, {1, 2}};
    fx_reset(0);
    vbe_draw_line(&g_v, 0, 0, 1, 2, FGC);
    CHECK(line_is(tie_y, 3), "line: (0,0)->(1,2), e2 == -dy does not step x");
    static const pt_t tie_x[] = {{0, 0}, {1, 0}, {2, 1}};
    fx_reset(0);
    vbe_draw_line(&g_v, 0, 0, 2, 1, FGC);
    CHECK(line_is(tie_x, 3), "line: (0,0)->(2,1), e2 == dx does not step y");

    static const pt_t diag[] = {{3, 3}, {4, 4}, {5, 5}, {6, 6}, {7, 7}};
    fx_reset(0);
    vbe_draw_line(&g_v, 3, 3, 7, 7, FGC);
    CHECK(line_is(diag, 5), "line: 45-degree diagonal");
    static const pt_t anti[] = {{7, 3}, {6, 4}, {5, 5}, {4, 6}, {3, 7}};
    fx_reset(0);
    vbe_draw_line(&g_v, 3, 7, 7, 3, FGC);
    CHECK(line_is(anti, 5), "line: anti-diagonal upward");

    static const pt_t horiz[] = {{2, 5}, {3, 5}, {4, 5}, {5, 5}, {6, 5}};
    fx_reset(0);
    vbe_draw_line(&g_v, 6, 5, 2, 5, FGC);
    CHECK(line_is(horiz, 5), "line: horizontal right-to-left");
    static const pt_t vert[] = {{4, 1}, {4, 2}, {4, 3}, {4, 4}};
    fx_reset(0);
    vbe_draw_line(&g_v, 4, 4, 4, 1, FGC);
    CHECK(line_is(vert, 4), "line: vertical bottom-to-top");
    static const pt_t dot[] = {{8, 8}};
    fx_reset(0);
    vbe_draw_line(&g_v, 8, 8, 8, 8, FGC);
    CHECK(line_is(dot, 1), "line: degenerate single point");

    /* partly off-screen: only on-screen pixels, no stray writes */
    static const pt_t clip[] = {{0, 1}, {1, 1}, {2, 1}, {3, 1}};
    fx_reset(0);
    vbe_draw_line(&g_v, -5, 1, 3, 1, FGC);
    CHECK(line_is(clip, 4), "line: off-screen start clipped per pixel");
    fx_reset(0);
    vbe_draw_line(&g_v, W - 2, H - 2, W + 5, H + 5, FGC);
    CHECK(px(W - 2, H - 2) == FGC && px(W - 1, H - 1) == FGC && count_color(FGC) == 2 &&
              guards_intact(),
          "line: off-screen end clipped per pixel");
}

/* ===== glyphs ===== */
static const uint8_t GLYPH_A[16] = {0x00, 0x00, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66,
                                    0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t GLYPH_BANG[16] = {0x00, 0x00, 0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00,
                                       0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/* the 8x16 cell at (x0,y0), magnified by `scale`, matches `g` (bg==MARK: untouched) */
static bool cell_is(int x0, int y0, const uint8_t *g, int scale, uint32_t fg, uint32_t bg)
{
    for (int r = 0; r < 16 * scale; r++)
        for (int c = 0; c < 8 * scale; c++) {
            bool on = (g[r / scale] >> (7 - c / scale)) & 1u;
            if (px(x0 + c, y0 + r) != (on ? fg : bg)) return false;
        }
    return true;
}

static const uint8_t BLANK[16] = {0};

static void test_char(void)
{
    fx_reset(MARK);
    vbe_draw_char(&g_v, 4, 2, 'A', FGC, BGC);
    CHECK(cell_is(4, 2, GLYPH_A, 1, FGC, BGC), "char: 'A' is font entry 33 (ASCII-32 index)");
    CHECK(count_color(FGC) + count_color(BGC) == 128, "char: exactly one 8x16 cell written");
    CHECK(px(3, 2) == MARK && px(12, 2) == MARK && px(4, 1) == MARK && px(4, 18) == MARK,
          "char: nothing outside the cell");
    CHECK(count_color(FGC) == 30, "char: 'A' has 30 lit pixels");

    fx_reset(MARK);
    vbe_draw_char(&g_v, 0, 0, '!', FGC, BGC);
    CHECK(cell_is(0, 0, GLYPH_BANG, 1, FGC, BGC), "char: '!' is font entry 1");
    fx_reset(MARK);
    vbe_draw_char(&g_v, 0, 0, ' ', FGC, BGC);
    CHECK(cell_is(0, 0, BLANK, 1, FGC, BGC), "char: space is blank");
    fx_reset(MARK);
    vbe_draw_char(&g_v, 0, 0, (char) 0x1F, FGC, BGC);
    CHECK(cell_is(0, 0, BLANK, 1, FGC, BGC), "char: control 0x1F falls back to blank");
    fx_reset(MARK);
    vbe_draw_char(&g_v, 0, 0, (char) 0xA0, FGC, BGC);
    CHECK(cell_is(0, 0, BLANK, 1, FGC, BGC), "char: 0xA0 (index 128) falls back to blank");
    fx_reset(MARK);
    vbe_draw_char(&g_v, 0, 0, (char) 0xFF, FGC, BGC);
    CHECK(cell_is(0, 0, BLANK, 1, FGC, BGC), "char: 0xFF falls back to blank");
    CHECK(guards_intact(), "char: guards intact");

    /* clipped at the bottom-right corner: in-range part drawn, no stray writes */
    fx_reset(MARK);
    vbe_draw_char(&g_v, W - 4, H - 6, 'A', FGC, BGC);
    CHECK(px(W - 2, H - 4) == FGC && px(W - 4, H - 4) == BGC, "char: clipped cell partly drawn");
    CHECK(count_color(FGC) + count_color(BGC) == 24 && guards_intact(),
          "char: clipped to 4x6 pixels");
    fx_reset(MARK);
    vbe_draw_char(&g_v, -4, -3, 'A', FGC, BGC);
    CHECK(count_color(FGC) + count_color(BGC) == 4 * 13 && px(0, 0) == BGC && px(1, 0) == FGC,
          "char: clipped at top-left");
}

static void test_string(void)
{
    fx_reset(MARK);
    vbe_draw_string(&g_v, 1, 2, "A!", FGC, BGC);
    CHECK(cell_is(1, 2, GLYPH_A, 1, FGC, BGC), "string: first glyph at x");
    CHECK(cell_is(9, 2, GLYPH_BANG, 1, FGC, BGC), "string: second glyph at x+8");
    CHECK(count_color(FGC) + count_color(BGC) == 256, "string: two cells, nothing else");

    fx_reset(MARK);
    vbe_draw_string(&g_v, 0, 0, "a", FGC, BGC);
    CHECK(!cell_is(0, 0, GLYPH_A, 1, FGC, BGC), "string: no a..z folding in draw_string");

    fx_reset(MARK);
    vbe_draw_string(&g_v, 0, 0, "", FGC, BGC);
    vbe_draw_string(&g_v, 0, 0, NULL, FGC, BGC);
    vbe_draw_string(NULL, 0, 0, "A", FGC, BGC);
    vbe_state_t nofb = g_v;
    nofb.fb = NULL;
    vbe_draw_string(&nofb, 0, 0, "A", FGC, BGC);
    CHECK(count_color(MARK) == W * H, "string: empty / NULL str / NULL vbe / NULL fb draw nothing");
}

static void test_text_ex(void)
{
    /* opaque, scale 1: identical to draw_char */
    fx_reset(MARK);
    vbe_draw_text_ex(&g_v, 2, 1, "A", FGC, BGC, 1, 1);
    CHECK(cell_is(2, 1, GLYPH_A, 1, FGC, BGC), "text_ex: opaque scale 1 'A'");
    CHECK(count_color(FGC) + count_color(BGC) == 128, "text_ex: one cell");

    /* lowercase folds to uppercase */
    fx_reset(MARK);
    vbe_draw_text_ex(&g_v, 2, 1, "a", FGC, BGC, 1, 1);
    CHECK(cell_is(2, 1, GLYPH_A, 1, FGC, BGC), "text_ex: 'a' folds to 'A'");
    fx_reset(MARK);
    vbe_draw_text_ex(&g_v, 0, 0, "z", FGC, BGC, 1, 1);
    uint32_t zfold[W * H];
    memcpy(zfold, g_fb, sizeof zfold);
    fx_reset(MARK);
    vbe_draw_char(&g_v, 0, 0, 'Z', FGC, BGC);
    CHECK(memcmp(zfold, g_fb, sizeof zfold) == 0, "text_ex: 'z' folds to 'Z'");
    /* '`' (just below 'a') and '{' (just above 'z') do not fold */
    fx_reset(MARK);
    vbe_draw_text_ex(&g_v, 0, 0, "`{", FGC, BGC, 1, 1);
    uint32_t a[W * H];
    memcpy(a, g_fb, sizeof a);
    fx_reset(MARK);
    vbe_draw_char(&g_v, 0, 0, '`', FGC, BGC);
    vbe_draw_char(&g_v, 8, 0, '{', FGC, BGC);
    CHECK(memcmp(a, g_fb, sizeof a) == 0, "text_ex: '`' and '{' are not folded");

    /* transparent: background pixels untouched */
    fx_reset(MARK);
    vbe_draw_text_ex(&g_v, 2, 1, "A", FGC, BGC, 1, 0);
    CHECK(cell_is(2, 1, GLYPH_A, 1, FGC, MARK), "text_ex: transparent keeps background");
    CHECK(count_color(BGC) == 0 && count_color(FGC) == 30, "text_ex: transparent writes only fg");

    /* scale 2: each bit a 2x2 block, 16x32 cell */
    fx_reset(MARK);
    vbe_draw_text_ex(&g_v, 3, -8, "A", FGC, BGC, 2, 1);
    /* cell rows -8..23 visible from 0: compare via a shifted window */
    bool ok = true;
    for (int r = 8; r < 32; r++)
        for (int c = 0; c < 16; c++) {
            bool on = (GLYPH_A[r / 2] >> (7 - c / 2)) & 1u;
            if (px(3 + c, r - 8) != (on ? FGC : BGC)) ok = false;
        }
    CHECK(ok, "text_ex: scale 2 magnifies each bit to a 2x2 block");
    CHECK(count_color(FGC) + count_color(BGC) == 16 * 24 && guards_intact(),
          "text_ex: scale 2 cell clipped at the top");

    /* scale 3, all 30 lit bits on screen * 9 */
    fx_reset(MARK);
    vbe_draw_text_ex(&g_v, 0, -6, "A", FGC, MARK, 3, 0);
    CHECK(count_color(FGC) == 30 * 9, "text_ex: scale 3 transparent = 30*9 fg pixels");

    /* scale < 1 is treated as 1, and advance is 8*scale */
    fx_reset(MARK);
    vbe_draw_text_ex(&g_v, 0, 0, "A!", FGC, BGC, 0, 1);
    CHECK(cell_is(0, 0, GLYPH_A, 1, FGC, BGC) && cell_is(8, 0, GLYPH_BANG, 1, FGC, BGC),
          "text_ex: scale 0 -> 1, advance 8");
    fx_reset(MARK);
    vbe_draw_text_ex(&g_v, 0, 0, "A!", FGC, BGC, -3, 1);
    CHECK(cell_is(0, 0, GLYPH_A, 1, FGC, BGC) && cell_is(8, 0, GLYPH_BANG, 1, FGC, BGC),
          "text_ex: negative scale -> 1");
    CHECK(count_color(FGC) + count_color(BGC) == 256, "text_ex: two 8x16 cells only");
    fx_reset(MARK);
    vbe_draw_text_ex(&g_v, 0, -16, "!A", FGC, BGC, 2, 1);
    ok = true;
    for (int r = 16; r < 32; r++)
        for (int c = 0; c < 16; c++) {
            bool on = (GLYPH_A[r / 2] >> (7 - c / 2)) & 1u;
            if (px(16 + c, r - 16) != (on ? FGC : BGC)) ok = false;
        }
    CHECK(ok, "text_ex: scale 2 advance is 16");

    /* out of range: blank glyph */
    fx_reset(MARK);
    vbe_draw_text_ex(&g_v, 0, 0, "\x1f\xa0", FGC, BGC, 1, 1);
    CHECK(count_color(BGC) == 256 && count_color(FGC) == 0, "text_ex: out-of-range -> blank");

    /* NULL guards */
    fx_reset(MARK);
    vbe_draw_text_ex(NULL, 0, 0, "A", FGC, BGC, 1, 1);
    vbe_draw_text_ex(&g_v, 0, 0, NULL, FGC, BGC, 1, 1);
    vbe_state_t nofb = g_v;
    nofb.fb = NULL;
    vbe_draw_text_ex(&nofb, 0, 0, "A", FGC, BGC, 1, 1);
    CHECK(count_color(MARK) == W * H, "text_ex: NULL vbe / str / fb draw nothing");
}

/* ===== the whole font, pinned =====
 * Render every glyph index 0..127 (chars 0x20..0x9F) and hash the on/off bits
 * row by row. The constant was recorded from this table; any edit to any glyph
 * changes it (FNV-1a steps are bijections, so one flipped pixel always
 * moves the hash). Index 0 (space) and the out-of-range fallback are blank. */
#define FONT_GOLDEN 0xE8F2B8D4u

static uint32_t font_hash(void)
{
    static uint32_t big[128 * 8 * 16];
    vbe_state_t v;
    vbe_init_fb(&v, 128 * 8, 16, 32, (uintptr_t) big);
    for (int i = 0; i < 128; i++) vbe_draw_char(&v, i * 8, 0, (char) (i + 32), 1u, 0u);
    uint32_t h = 2166136261u;
    for (int i = 0; i < 128 * 8 * 16; i++) {
        h ^= big[i];
        h *= 16777619u;
    }
    return h;
}

/* draw_text_ex (scale 1, opaque) renders every glyph exactly as draw_char,
 * except that a..z take the A..Z glyphs */
static void test_text_ex_matches_char(void)
{
    static uint32_t big_a[128 * 8 * 16], big_b[128 * 8 * 16];
    vbe_state_t va, vb;
    vbe_init_fb(&va, 128 * 8, 16, 32, (uintptr_t) big_a);
    vbe_init_fb(&vb, 128 * 8, 16, 32, (uintptr_t) big_b);
    char str[129];
    for (int i = 0; i < 128; i++) {
        int c = i + 32;
        str[i] = (char) c;
        vbe_draw_char(&vb, i * 8, 0, (char) (c >= 'a' && c <= 'z' ? c - 32 : c), 1u, 2u);
    }
    str[128] = 0;
    vbe_draw_text_ex(&va, 0, 0, str, 1u, 2u, 1, 1);
    CHECK(memcmp(big_a, big_b, sizeof big_a) == 0,
          "text_ex: all 128 glyphs match draw_char (a..z folded)");
}

static void test_font_table(void)
{
    uint32_t h = font_hash();
    printf("font hash 0x%08X\n", h);
    CHECK(h == FONT_GOLDEN, "font: 128-glyph table matches the golden hash");
    /* hand-checked anchors inside the table */
    static const uint8_t ZERO[16] = {0, 0, 0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C};
    static const uint8_t ZED[16] = {0, 0, 0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E};
    fx_reset(MARK);
    vbe_draw_char(&g_v, 0, 0, '0', FGC, BGC);
    vbe_draw_char(&g_v, 8, 0, 'Z', FGC, BGC);
    CHECK(cell_is(0, 0, ZERO, 1, FGC, BGC) && cell_is(8, 0, ZED, 1, FGC, BGC),
          "font: '0' and 'Z' bitmaps");
}

int main(void)
{
    test_init();
    test_pixels();
    test_fill_rect();
    test_lines();
    test_char();
    test_string();
    test_text_ex();
    test_text_ex_matches_char();
    test_font_table();
    printf("test_vbe: %d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
