/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_image.c — PNG pictures read, cropped, fitted and drawn: every
 * format pixel for pixel, the fit's averaging, the drawing's strength and
 * gold, and damaged files refused without harm. */
#include <stdio.h>
#include <string.h>

#include "zui_image.h"
#include "zui_png_fixture.inc"

static int pass, fail;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("[FAIL] %s:%d ", __FILE__, __LINE__);                                           \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static uint8_t work[1 << 20];
static uint32_t px[256 * 256];
static uint8_t file[1 << 16];

/* a PNG built here: RGBA 8, filter 0, one stored deflate block (CRCs and the
 * Adler sum are not checked by the reader, so they are left zero) */
static uint32_t make_png(uint8_t *out, uint32_t w, uint32_t h, const uint32_t *rgba)
{
    uint32_t n = 0, raw = h * (1 + 4 * w);
#define B(v)   out[n++] = (uint8_t) (v)
#define U32(v) B((v) >> 24), B((v) >> 16), B((v) >> 8), B(v)
    static const uint8_t SIG[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    for (int i = 0; i < 8; i++) B(SIG[i]);
    U32(13u);
    B('I'), B('H'), B('D'), B('R');
    U32(w), U32(h);
    B(8), B(6), B(0), B(0), B(0);
    U32(0u);
    U32(raw + 11u);
    B('I'), B('D'), B('A'), B('T');
    B(0x78), B(0x01), B(0x01);
    B(raw), B(raw >> 8), B(~raw), B((~raw) >> 8);
    for (uint32_t y = 0; y < h; y++) {
        B(0);
        for (uint32_t x = 0; x < w; x++) {
            uint32_t p = rgba[y * w + x];
            B(p >> 24), B(p >> 16), B(p >> 8), B(p);
        }
    }
    U32(0u); /* adler */
    U32(0u); /* crc */
    U32(0u);
    B('I'), B('E'), B('N'), B('D');
    U32(0u);
#undef B
#undef U32
    return n;
}

static void test_formats(void)
{
    for (uint32_t c = 0; c < sizeof PNG_CASES / sizeof PNG_CASES[0]; c++) {
        const png_case_t *k = &PNG_CASES[c];
        zui_image_t img;
        uint32_t need = zui_image_work_size(k->png, k->n, 64);
        CHECK(need > 0 && need < sizeof work, "%s: scratch size known", k->name);
        int r = zui_image_load(&img, px, 64, 64, k->png, k->n,
                               k->keyed ? ZUI_IMG_KEY_GROUND : ZUI_IMG_NO_KEY, work, need);
        CHECK(r == ZUI_IMG_OK, "%s: reads (%d)", k->name, r);
        CHECK(img.w == k->w && img.h == k->h, "%s: cropped to %dx%d, got %dx%d", k->name, k->w,
              k->h, img.w, img.h);
        uint32_t bad = 0;
        for (int32_t i = 0; i < k->w * k->h && r == ZUI_IMG_OK; i++)
            if (px[i] != k->want[i]) {
                if (!bad)
                    printf("  %s px %d: %08x want %08x\n", k->name, i, (unsigned) px[i],
                           (unsigned) k->want[i]);
                bad++;
            }
        CHECK(!bad, "%s: every pixel as it should be (%u wrong)", k->name, bad);
        CHECK(zui_image_load(&img, px, 64, 64, k->png, k->n, ZUI_IMG_NO_KEY, work, need - 1) ==
                  ZUI_IMG_ESPACE,
              "%s: too little scratch is refused", k->name);
    }
    zui_image_t img;
    CHECK(zui_image_load(&img, px, 64, 64, PNG_INTERLACED, sizeof PNG_INTERLACED, ZUI_IMG_NO_KEY,
                         work, sizeof work) == ZUI_IMG_EUNSUPPORTED,
          "an interlaced file is refused");
    /* the icon's ground is clear only when keyed */
    const png_case_t *ic = &PNG_CASES[1];
    zui_image_load(&img, px, 64, 64, ic->png, ic->n, ZUI_IMG_NO_KEY, work, sizeof work);
    CHECK(img.w == 16 && img.h == 12 && px[0] == 0xFF002911u, "unkeyed, the green stays");
}

static void test_fit(void)
{
    static uint32_t rgba[64 * 32];
    /* left half red, right half blue, a clear border two pixels wide */
    for (uint32_t y = 0; y < 32; y++)
        for (uint32_t x = 0; x < 64; x++)
            rgba[y * 64 + x] = (x < 2 || x >= 62 || y < 2 || y >= 30) ? 0u
                               : x < 32                               ? 0xFF0000FFu
                                                                      : 0x0000FFFFu;
    uint32_t n = make_png(file, 64, 32, rgba);
    zui_image_t img;
    CHECK(zui_image_load(&img, px, 15, 15, file, n, ZUI_IMG_NO_KEY, work, sizeof work) ==
              ZUI_IMG_OK,
          "a built file reads");
    CHECK(img.w == 15 && img.h == 7, "60x28 drawn part fits 15 wide: %dx%d", img.w, img.h);
    CHECK(px[0] == 0xFFFF0000u && px[14] == 0xFF0000FFu, "red on the left, blue on the right");
    /* a fine checker averages to grey */
    for (uint32_t y = 0; y < 32; y++)
        for (uint32_t x = 0; x < 64; x++)
            rgba[y * 64 + x] = (x + y) & 1u ? 0xFFFFFFFFu : 0x000000FFu;
    n = make_png(file, 64, 32, rgba);
    zui_image_load(&img, px, 16, 16, file, n, ZUI_IMG_NO_KEY, work, sizeof work);
    CHECK(img.w == 16 && img.h == 8, "64x32 into 16: 16x8");
    uint32_t g = (px[3 * 16 + 5] >> 8) & 255u;
    CHECK(g == 127 || g == 128, "each pixel the average of the sixteen it covers (%u)", g);
    /* never enlarged */
    zui_image_load(&img, px, 200, 200, file, n, ZUI_IMG_NO_KEY, work, sizeof work);
    CHECK(img.w == 64 && img.h == 32, "a small picture keeps its size");
    /* nothing drawn */
    for (uint32_t i = 0; i < 64 * 32; i++) rgba[i] = 0x12345600u;
    n = make_png(file, 64, 32, rgba);
    CHECK(zui_image_load(&img, px, 16, 16, file, n, ZUI_IMG_NO_KEY, work, sizeof work) ==
              ZUI_IMG_EEMPTY,
          "an all-clear picture is empty");
}

static void test_damage(void)
{
    zui_image_t img;
    const png_case_t *k = &PNG_CASES[0];
    CHECK(zui_image_load(&img, px, 64, 64, (const uint8_t *) "not a png at all, no", 20,
                         ZUI_IMG_NO_KEY, work, sizeof work) == ZUI_IMG_EFORMAT,
          "not a PNG");
    CHECK(zui_image_load(&img, px, 64, 64, k->png, 40, ZUI_IMG_NO_KEY, work, sizeof work) < 0,
          "cut short");
    CHECK(zui_image_load(0, px, 64, 64, k->png, k->n, ZUI_IMG_NO_KEY, work, sizeof work) < 0 &&
              zui_image_load(&img, px, 0, 64, k->png, k->n, ZUI_IMG_NO_KEY, work, sizeof work) < 0,
          "bad arguments refused");
    CHECK(zui_image_work_size(k->png, 7, 64) == 0, "no size for a non-PNG");
    /* every single byte damaged in turn, and every length cut: never a crash,
     * never a write past the box */
    static uint8_t bad[sizeof PNG_RGBA8];
    uint32_t ok = 0, refused = 0;
    for (uint32_t i = 8; i < k->n; i++)
        for (uint32_t v = 1; v < 256; v += 85) {
            memcpy(bad, k->png, k->n);
            bad[i] ^= (uint8_t) v;
            px[64 * 64] = 0xDEADBEEFu;
            int r = zui_image_load(&img, px, 64, 64, bad, k->n, ZUI_IMG_NO_KEY, work, sizeof work);
            if (r == ZUI_IMG_OK)
                ok += img.w <= 64 && img.h <= 64;
            else
                refused++;
            if (px[64 * 64] != 0xDEADBEEFu) ok = 0x80000000u;
        }
    for (uint32_t n = 0; n < k->n; n++) {
        int r = zui_image_load(&img, px, 64, 64, k->png, n, ZUI_IMG_NO_KEY, work, sizeof work);
        refused += r != ZUI_IMG_OK;
    }
    CHECK(!(ok & 0x80000000u) && refused > 0, "damaged files: %u read in bounds, %u refused", ok,
          refused);
}

static void test_draw(void)
{
    static uint32_t fb[40 * 40];
    zui_surface_t s;
    zui_surface_init(&s, fb, 40, 40, 40);
    for (int i = 0; i < 40 * 40; i++) fb[i] = 0x002911u;
    /* a 4x2 picture: opaque white, half-clear white */
    uint32_t pic[8] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0x80808080u, 0x80808080u,
                       0xFFFFFFFFu, 0xFFFFFFFFu, 0x80808080u, 0x80808080u};
    zui_image_t img = {pic, 4, 2};
    zui_rect_t box = {10, 10, 4, 2};
    zui_image_draw(&s, &img, box, 0, ZUI_IMG_NO_KEY);
    CHECK(fb[10 * 40 + 10] == 0x002911u, "strength 0 draws nothing");
    zui_image_draw(&s, &img, box, 256, ZUI_IMG_NO_KEY);
    CHECK(fb[10 * 40 + 10] == 0xFFFFFFu, "opaque white over green is white");
    uint32_t h = fb[10 * 40 + 12];
    CHECK(((h >> 16) & 255u) == 0x80 && ((h >> 8) & 255u) == 0x80 + 0x29 * 127 / 255,
          "half-clear white lets half the green through (%06x)", (unsigned) h);
    for (int i = 0; i < 40 * 40; i++) fb[i] = 0x002911u;
    zui_image_draw(&s, &img, box, 128, ZUI_IMG_NO_KEY);
    uint32_t q = fb[10 * 40 + 10];
    CHECK(((q >> 16) & 255u) >= 126 && ((q >> 16) & 255u) <= 128, "half strength, half way (%06x)",
          (unsigned) q);
    /* held to gold: white comes out the palette's gold */
    for (int i = 0; i < 40 * 40; i++) fb[i] = 0x002911u;
    zui_image_draw(&s, &img, box, 256, 0xF2C230u);
    CHECK(fb[10 * 40 + 10] == 0xF2C230u, "a system app's white is gold (%06x)",
          (unsigned) fb[10 * 40 + 10]);
    /* shrunk to a smaller box, centred, nothing outside it */
    for (int i = 0; i < 40 * 40; i++) fb[i] = 0x002911u;
    zui_rect_t small = {20, 20, 2, 2};
    zui_image_draw(&s, &img, small, 256, ZUI_IMG_NO_KEY);
    uint32_t inside = 0, outside = 0;
    for (int y = 0; y < 40; y++)
        for (int x = 0; x < 40; x++) {
            int in = x >= 20 && x < 22 && y >= 20 && y < 22;
            if (fb[y * 40 + x] != 0x002911u) *(in ? &inside : &outside) += 1;
        }
    CHECK(inside == 2 && outside == 0, "2x1 inside a 2x2 box, nothing outside (%u, %u)", inside,
          outside);
    /* clipped */
    for (int i = 0; i < 40 * 40; i++) fb[i] = 0x002911u;
    zui_rect_t clip = {0, 0, 11, 40};
    zui_clip(&s, clip);
    zui_image_draw(&s, &img, box, 256, ZUI_IMG_NO_KEY);
    CHECK(fb[10 * 40 + 10] == 0xFFFFFFu && fb[10 * 40 + 11] == 0x002911u, "the clip is kept");
}

int main(void)
{
    test_formats();
    test_fit();
    test_damage();
    test_draw();
    printf("test_zui_image: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
