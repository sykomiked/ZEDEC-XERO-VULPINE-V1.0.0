/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_art_studio.c — known-answer geometry for Miss Potimus's Art Studio.
 *
 * Like the truetype test, every anchor is a number that must come out exactly:
 * an inked pixel count, a byte-for-byte rounded blend, a SHA-256 CID recomputed
 * independently, and the theme's real accent RGB. Truncation, an unrounded
 * divide, an off-by-one clip, or a fabricated hash all make a fixture disagree.
 */
#include <stdio.h>
#include "art_studio.h"
#include "ipfs.h"

static int failures = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("[FAIL] %s\n", msg);                                                            \
            failures++;                                                                            \
        } else {                                                                                   \
            printf("[PASS] %s\n", msg);                                                            \
        }                                                                                          \
    } while (0)

/* Independent recomputation of the studio's rounded blend — deliberately a
 * second implementation so the test does not merely echo the module. */
static uint8_t ref_blend(uint8_t src, uint8_t dst, uint8_t a)
{
    return (uint8_t) (((unsigned) src * a + (unsigned) dst * (255u - a) + 127u) / 255u);
}

/* ---- ANCHOR 1: fill_rect inks EXACTLY the right pixels, margins stay zero ---- */
static void test_fill_rect_exact(void)
{
    art_canvas_t c;
    art_canvas_init(&c, 16, 16);
    /* A 5x6 rectangle at (3,4): 30 pixels, all == 200; everything else == 0. */
    art_fill_rect(&c, 3, 4, 5, 6, 200);

    uint32_t inked = 0, wrong_value = 0, leaked = 0;
    for (uint32_t y = 0; y < c.h; y++) {
        for (uint32_t x = 0; x < c.w; x++) {
            uint8_t v = c.px[y * c.w + x];
            int inside = (x >= 3 && x < 8 && y >= 4 && y < 10);
            if (v) inked++;
            if (inside && v != 200) wrong_value++;
            if (!inside && v != 0) leaked++; /* margin must stay pristine */
        }
    }
    CHECK(inked == 30, "fill_rect inks exactly 30 pixels (5x6)");
    CHECK(wrong_value == 0, "every inked pixel holds the fill value 200");
    CHECK(leaked == 0, "the margin around the rect is exactly zero");
}

/* ---- ANCHOR 2: blend_pixel matches the rounded formula for several triples ---- */
static void test_blend_rounding(void)
{
    struct {
        uint8_t src, dst, a;
    } cases[] = {
        {255, 0, 128},  {0, 255, 128},   {200, 50, 77}, {123, 231, 1},
        {17, 240, 254}, {255, 255, 255}, {100, 100, 0},
    };
    int ok = 1;
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        art_canvas_t c;
        art_canvas_init(&c, 4, 4);
        art_fill_rect(&c, 1, 1, 1, 1, cases[i].dst); /* seed dst at (1,1) */
        art_blend_pixel(&c, 1, 1, cases[i].src, cases[i].a);
        uint8_t got = c.px[1 * c.w + 1];
        uint8_t exp = ref_blend(cases[i].src, cases[i].dst, cases[i].a);
        if (got != exp) {
            ok = 0;
            printf("  blend(%u,%u,%u) got %u want %u\n", cases[i].src, cases[i].dst, cases[i].a,
                   got, exp);
        }
    }
    CHECK(ok, "blend_pixel == (src*a + dst*(255-a) + 127)/255 for all triples");

    /* A concrete hand-checked value: 255 over 0 at a=128 -> (255*128+127)/255 = 128. */
    art_canvas_t c;
    art_canvas_init(&c, 2, 2);
    art_blend_pixel(&c, 0, 0, 255, 128);
    CHECK(c.px[0] == 128, "blend 255 over 0 at alpha 128 rounds to 128");
}

/* ---- ANCHOR 3: off-frame writes are no-ops (1x1 canvas under ASan) ---- */
static void test_bounds_noop(void)
{
    art_canvas_t c;
    art_canvas_init(&c, 1, 1);
    /* Everything below reaches outside the single pixel; nothing may overrun. */
    art_fill_rect(&c, 5, 5, 10, 10, 255);
    art_fill_rect(&c, -20, -20, 10, 10, 255);
    art_hline(&c, -4, 0, 100, 255); /* crosses the pixel from the left */
    art_vline(&c, 0, -4, 100, 255); /* crosses the pixel from the top  */
    art_blend_pixel(&c, 9, 9, 255, 255);
    art_blend_pixel(&c, -1, 0, 255, 255);
    art_point_t path[] = {{-10, -10}, {50, 50}, {-3, 7}};
    art_stroke_path(&c, path, 3, 255); /* a line THROUGH (0,0) is allowed */
    /* The hline/vline/stroke each legitimately cross (0,0), so it may be inked;
     * what matters for ASan is that no write landed off the 1-byte buffer. The
     * pixel is a valid index either way. */
    CHECK(c.w == 1 && c.h == 1, "1x1 canvas survives off-frame ops (no overrun)");

    /* A stricter no-op: a fill entirely off-frame leaves the pixel untouched. */
    art_canvas_init(&c, 1, 1);
    art_fill_rect(&c, 100, 100, 4, 4, 255);
    CHECK(c.px[0] == 0, "a fully off-frame fill inks nothing");

    /* Extreme int32 coordinates must not overflow the loop offset arithmetic
     * (UBSan) and must not hang; all are clipped to a no-op on this 1x1 canvas. */
    art_canvas_init(&c, 1, 1);
    art_fill_rect(&c, 2147483640, 2147483640, 100, 100, 255); /* near INT32_MAX */
    art_fill_rect(&c, -2147483640, -2147483640, 100, 100, 255);
    art_hline(&c, 2147483640, 0, 100, 255);
    art_vline(&c, 0, 2147483640, 100, 255);
    art_point_t ext[] = {{-2147483000, -2147483000}, {2147483000, 2147483000}};
    art_stroke_path(&c, ext, 2, 255); /* opposite extremes: guarded no-op */
    CHECK(c.px[0] == 0, "extreme int32 coordinates: no overflow, no hang, no ink");
}

/* ---- ANCHOR 4: export CID equals ipfs_cid_from_bytes over the exact bytes ---- */
static void test_export_cid(void)
{
    art_canvas_t c;
    art_canvas_init(&c, 12, 9);
    art_fill_rect(&c, 2, 2, 6, 4, 111);
    art_hline(&c, 0, 8, 12, 42);
    art_blend_pixel(&c, 5, 5, 200, 90);

    uint8_t got[32], want[32];
    int32_t r = art_export_cid(&c, got);
    int32_t r2 = ipfs_cid_from_bytes(c.px, c.w * c.h, want); /* the exact live span */

    int same = 1;
    for (int i = 0; i < 32; i++)
        if (got[i] != want[i]) same = 0;
    CHECK(r == 0 && r2 == 0, "art_export_cid and ipfs_cid_from_bytes both succeed");
    CHECK(same, "export CID == SHA-256(px[0..w*h)) exactly (content-addressed)");

    /* Determinism + sensitivity: same art -> same CID; one flipped pixel differs. */
    uint8_t again[32];
    art_export_cid(&c, again);
    int stable = 1;
    for (int i = 0; i < 32; i++)
        if (again[i] != got[i]) stable = 0;
    CHECK(stable, "identical canvases export an identical CID (deterministic)");

    c.px[0] ^= 1u;
    uint8_t moved[32];
    art_export_cid(&c, moved);
    int differs = 0;
    for (int i = 0; i < 32; i++)
        if (moved[i] != got[i]) differs = 1;
    CHECK(differs, "one changed pixel changes the CID (self-certifying)");
}

/* ---- ANCHOR 5: palette resolves the theme's ACCENT token to its real RGB ---- */
static void test_palette(void)
{
    theme_t t;
    theme_init(&t);
    art_palette_t p;
    art_palette_from_theme(&p, &t);
    CHECK(art_palette_color(&p, THEME_ACCENT) == 0xFF6600u,
          "palette resolves THEME_ACCENT to 0xFF6600 (fire orange)");
    CHECK(art_palette_color(&p, THEME_GOLD) == 0xFFD700u,
          "palette resolves THEME_GOLD to 0xFFD700 (the hoard)");
    /* An override flows straight through the palette rebuild. */
    theme_set(&t, THEME_ACCENT, 0x123456u);
    art_palette_from_theme(&p, &t);
    CHECK(art_palette_color(&p, THEME_ACCENT) == 0x123456u,
          "a theme override recolours the palette");
    CHECK(art_palette_color(&p, THEME__COUNT) == 0u,
          "an out-of-range slot returns 0, not an overrun");
}

/* ---- sigil composer: a real card rasterises within the frame ---- */
static void test_compose_sigil(void)
{
    /* A tiny 3-node triangle circuit on a 2-unit lattice. */
    sigil_t s;
    sig_init(&s, 777);
    s.fab_n = 12;
    s.fab_k = 5;
    sig_add_node(&s, 0, 0);
    sig_add_node(&s, 3, 0);
    sig_add_node(&s, 0, 3);
    sig_add_edge(&s, 0, 1);
    sig_add_edge(&s, 1, 2);
    sig_add_edge(&s, 2, 0);

    art_canvas_t c;
    art_canvas_init(&c, 64, 64);
    int32_t drawn = art_compose_sigil(&c, &s, 8, 8, 12, 255, 128);
    CHECK(drawn == 3, "compose_sigil draws all 3 on-frame nodes");

    uint32_t node_ink = 0, edge_ink = 0;
    for (uint32_t i = 0; i < c.w * c.h; i++) {
        if (c.px[i] == 255)
            node_ink++;
        else if (c.px[i] == 128)
            edge_ink++;
    }
    CHECK(node_ink > 0 && edge_ink > 0, "sigil inks both nodes (255) and edges (128)");

    /* NULL-safety of the composer. */
    CHECK(art_compose_sigil(NULL, &s, 0, 0, 1, 1, 1) == -1, "compose_sigil rejects NULL canvas");
    CHECK(art_compose_sigil(&c, NULL, 0, 0, 1, 1, 1) == -1, "compose_sigil rejects NULL sigil");
}

int main(void)
{
    printf("=== Miss Potimus's Art Studio — known-answer geometry ===\n");
    test_fill_rect_exact();
    test_blend_rounding();
    test_bounds_noop();
    test_export_cid();
    test_palette();
    test_compose_sigil();
    if (failures) {
        printf("\n%d FAILED\n", failures);
        return 1;
    }
    printf("\nALL PASSED — the canvas never bled past its frame.\n");
    return 0;
}
