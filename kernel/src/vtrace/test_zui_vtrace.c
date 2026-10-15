/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_vtrace.c — the vectoriser (zui_vtrace.h) and the vector movie
 * (zui_vtrace_video.h): exactness, fidelity, seams, holes, scaling, the
 * depth filter on traced output, provenance, and fail-closed readers.
 * Fixtures: tools/vtrace/make_fixtures.py (test/fx_vtrace.inc). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sha256.h"
#include "zui_depth.h"
#include "zx_image.h"
#include "zui_logic.h"
#include "zui_vec.h"
#include "zui_video.h"
#include "zui_vtrace.h"
#include "zui_vtrace_video.h"

#include "test/fx_vtrace.inc"

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
#define PASS(...)                                                                                  \
    do {                                                                                           \
        printf("[PASS] ");                                                                         \
        printf(__VA_ARGS__);                                                                       \
        printf("\n");                                                                              \
    } while (0)

static zui_vec_work_t VW;

/* ---- helpers ------------------------------------------------------------ */
typedef struct {
    uint32_t *px;
    uint32_t w, h;
} pic_t;

static pic_t decode(const uint8_t *d, uint32_t n)
{
    pic_t p = {0, 0, 0};
    zx_image_info_t in;
    if (zx_image_probe(d, n, &in) != 0) return p;
    void *work = malloc(in.work + 16);
    p.px = malloc(in.px_bytes);
    if (zx_image_decode(d, n, work, in.work + 16, p.px, in.w * in.h, &in) != 0) {
        free(p.px);
        p.px = 0;
    }
    free(work);
    p.w = in.w, p.h = in.h;
    return p;
}

typedef struct {
    uint8_t *z;
    int32_t len;
    zvt_result_t r;
} trace_t;

static trace_t trace_be(const uint32_t *px, uint32_t w, uint32_t h, const uint8_t *src,
                        uint32_t srclen, const zvt_params_t *p, const zvt_backend_t *be)
{
    trace_t t;
    memset(&t, 0, sizeof t);
    uint64_t ws = zvt_work_size(w, h), os = zvt_out_max(w, h);
    void *work = malloc(ws);
    for (int tries = 0; tries < 5; tries++, os *= 2) { /* ZVT_E_SPACE: a bigger output */
        free(t.z);
        t.z = malloc(os);
        t.len = zvt_trace(px, w, h, w, src, srclen, p, be, 0, work, ws, t.z, (uint32_t) os, &t.r);
        if (t.len != ZVT_E_SPACE) break;
    }
    free(work);
    return t;
}
static trace_t trace(const uint32_t *px, uint32_t w, uint32_t h, const uint8_t *src,
                     uint32_t srclen, const zvt_params_t *p)
{
    return trace_be(px, w, h, src, srclen, p, 0);
}

/* draw z into a fresh surface of sw x sh filled with bg */
static uint32_t *draw(const uint8_t *z, uint32_t n, int32_t sw, int32_t sh, uint32_t bg)
{
    uint32_t *px = malloc((size_t) sw * sh * 4);
    for (int32_t i = 0; i < sw * sh; i++) px[i] = bg;
    zui_surface_t s;
    zui_surface_init(&s, px, sw, sh, sw);
    zui_vec_draw_zxvg(&s, &VW, z, n, (zui_rect_t){0, 0, sw, sh}, 0);
    return px;
}

static uint32_t over(uint32_t q, uint32_t bg)
{
    uint32_t a = q >> 24, o = 0;
    for (int sh = 0; sh < 24; sh += 8) {
        uint32_t s = (q >> sh) & 255u, d = (bg >> sh) & 255u;
        o |= ((s * a + d * (255u - a) + 127u) / 255u) << sh;
    }
    return o;
}

static void dump_ppm(const char *name, const uint32_t *px, uint32_t w, uint32_t h)
{
    const char *dir = getenv("ZVT_DUMP");
    if (!dir) return;
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (uint32_t i = 0; i < w * h; i++) {
        uint8_t c[3] = {(uint8_t) (px[i] >> 16), (uint8_t) (px[i] >> 8), (uint8_t) px[i]};
        fwrite(c, 1, 3, f);
    }
    fclose(f);
}

/* ---- pixel-exact ---------------------------------------------------------- */
static void synth_flat(uint32_t *px, uint32_t w, uint32_t h)
{
    /* rings with holes, a diagonal staircase, a checker patch, a 1 px line */
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            int32_t dx = (int32_t) x - 20, dy = (int32_t) y - 20, d2 = dx * dx + dy * dy;
            uint32_t c = 0xFFF0F0E8u;
            if (d2 < 15 * 15) c = 0xFFC02030u;
            if (d2 < 8 * 8) c = 0xFFF0F0E8u;
            if (d2 < 3 * 3) c = 0xFF2040A0u;
            if (x > 40 && y > 4 && y < 30 && x - 40 > y - 4) c = 0xFF30A050u;
            if (x >= 8 && x < 16 && y >= 44 && y < 52 && ((x ^ y) & 1)) c = 0xFF101010u;
            if (y == 40 && x > 20) c = 0xFF101010u;
            if (x >= 30 && x < 60 && y >= 46 && y < 58 && !(x > 40 && x < 50 && y > 50 && y < 54))
                c = 0xFFE0B020u;
            px[y * w + x] = c;
        }
}

static void test_pixel_exact(void)
{
    zvt_params_t p;
    zvt_preset(&p, ZVT_PRESET_PIXEL);
    /* the sprite fixture: transparency included */
    pic_t s = decode(fx_pixel_png, sizeof fx_pixel_png);
    CHECK(s.px && s.w == 32 && s.h == 32, "decode sprite");
    trace_t t = trace(s.px, s.w, s.h, fx_pixel_png, sizeof fx_pixel_png, &p);
    CHECK(t.len > 0, "trace sprite: %d", t.len);
    CHECK(t.r.exact && t.r.exact_possible && t.r.psnr_cdb == ZVT_PSNR_INF && t.r.ssim == 10000,
          "sprite exact (%d %d %d)", t.r.exact, t.r.psnr_cdb, t.r.ssim);
    CHECK(zui_zxvg_check(t.z, (uint32_t) t.len) == 0, "sprite ZXVG valid");
    /* independent check on two backgrounds: transparent stays transparent */
    uint32_t bgs[2] = {0x000000u, 0xFF00FFu};
    int diff = 0;
    for (int b = 0; b < 2; b++) {
        uint32_t *r = draw(t.z, (uint32_t) t.len, 32, 32, bgs[b]);
        for (uint32_t i = 0; i < 32 * 32; i++) diff += r[i] != over(s.px[i], bgs[b]);
        free(r);
    }
    CHECK(diff == 0, "sprite re-render differs in %d pixels", diff);
    PASS("pixel-exact: 32x32 sprite with transparency -> %u paths, %d bytes, identical on two "
         "backgrounds",
         t.r.paths, t.len);
    free(t.z);
    free(s.px);

    /* synthetic flat picture: rings with holes, staircase, checker, 1 px line */
    uint32_t W = 64, H = 64, *px = malloc(W * H * 4);
    synth_flat(px, W, H);
    t = trace(px, W, H, 0, 0, &p);
    CHECK(t.len > 0 && t.r.exact && t.r.cid_of_pixels, "flat exact %d %d", t.len, t.r.exact);
    uint32_t *r = draw(t.z, (uint32_t) t.len, (int32_t) W, (int32_t) H, 0x123456u);
    diff = 0;
    for (uint32_t i = 0; i < W * H; i++) diff += r[i] != (px[i] & 0xFFFFFFu);
    CHECK(diff == 0, "flat re-render differs in %d pixels", diff);
    PASS("pixel-exact: 64x64 flat picture (holes, checker, staircase) identical, %u paths, %d "
         "bytes for %u raw bytes",
         t.r.paths, t.len, W * H * 3);
    free(r);
    free(t.z);
    /* too many colours: honestly not exact */
    for (uint32_t i = 0; i < W * H; i++) px[i] = 0xFF000000u | (i * 2654435761u >> 8);
    t = trace(px, W, H, 0, 0, &p);
    CHECK(t.len > 0 && !t.r.exact && !t.r.exact_possible, "noise must not be exact (%d)", t.len);
    PASS("pixel-exact: 4096 distinct colours -> APPROXIMATE (psnr %d.%02d dB), never claimed "
         "exact",
         t.r.psnr_cdb / 100, t.r.psnr_cdb % 100);
    free(t.z);
    free(px);
    /* two colours, but a boundary far past the renderer's path caps: the
     * over-long chains are refitted (relaxed), the result says APPROXIMATE */
    W = 320, H = 320, px = malloc(W * H * 4);
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++) {
            /* one dark region: a spine and 2 px staircase bands off it */
            uint32_t k = (x + 6u * H - y) % 6u;
            bool in = x < 2u || (k < 2u && x < W - 4u && y < H - 4u);
            px[y * W + x] = in ? 0xFF203040u : 0xFFE0D0C0u;
        }
    t = trace(px, W, H, 0, 0, &p);
    CHECK(t.len > 0 && t.r.refits > 0 && t.r.exact_possible && !t.r.exact &&
              zui_zxvg_check(t.z, (uint32_t) t.len) == 0,
          "pixel caps %d refits %u exact %d", t.len, t.r.refits, t.r.exact);
    PASS("pixel-exact past the caps: 2 colours, 320x320, one region with a 2 px staircase comb -> "
         "%u "
         "chains refitted to fit the "
         "renderer, APPROXIMATE (psnr %d.%02d dB, ssim 0.%04d), never refused",
         t.r.refits, t.r.psnr_cdb / 100, t.r.psnr_cdb % 100, t.r.ssim);
    free(t.z);
    free(px);
}

/* ---- seams and holes -------------------------------------------------------- */
static void test_seams(void)
{
    /* every colour has green 200; the surface is pure magenta (green 0): any
     * background showing through lowers green */
    uint32_t W = 80, H = 60, *px = malloc(W * H * 4);
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++) {
            int32_t dx = (int32_t) x - 30, dy = (int32_t) y - 30;
            uint32_t c = 0xFF78C878u;
            if (dx * dx + dy * dy < 400) c = 0xFFE0C890u;
            if (dx * dx + 4 * dy * dy < 120) c = 0xFF28C8F0u;
            if (x > 50 && 2 * y + x > 120) c = 0xFFF0C878u;
            if (x > 60 && y < 25) c = 0xFF10C810u;
            px[y * W + x] = c;
        }
    zvt_params_t p;
    zvt_preset(&p, ZVT_PRESET_ICON);
    trace_t t = trace(px, W, H, 0, 0, &p);
    CHECK(t.len > 0, "seam trace %d", t.len);
    const int32_t sz[4][2] = {{80, 60}, {320, 240}, {188, 141}, {56, 42}}; /* all exactly 4:3 */
    int leaks = 0;
    for (int k = 0; k < 4; k++) {
        uint32_t *r = draw(t.z, (uint32_t) t.len, sz[k][0], sz[k][1], 0xFF00FFu);
        for (int32_t i = 0; i < sz[k][0] * sz[k][1]; i++) leaks += ((r[i] >> 8) & 255u) < 199u;
        if (k == 1) dump_ppm("seams_4x", r, 320, 240);
        free(r);
    }
    CHECK(leaks == 0, "background leaked into %d pixels", leaks);
    PASS("no gaps: 5 regions with curved shared borders, drawn at 1x, 4x, 2.35x and 0.7x over "
         "magenta: 0 pixels show the background");

    /* a parent/child border (a tree edge) blends only the two colours */
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++)
            px[y * W + x] = 3 * x + 2 * y < 200 ? 0xFFC82828u : 0xFF2828C8u;
    free(t.z);
    t = trace(px, W, H, 0, 0, &p);
    uint32_t *r = draw(t.z, (uint32_t) t.len, 264, 198, 0x00FF00u);
    int off = 0;
    for (int32_t i = 0; i < 264 * 198; i++) {
        uint32_t c = r[i], R = (c >> 16) & 255u, G = (c >> 8) & 255u, B = c & 255u;
        bool o = G != 40u || R + B < 238u || R + B > 242u;
        if (o && off < 5) printf("  off at %d,%d: %06x\n", i % 264, i / 264, c & 0xFFFFFFu);
        off += o;
    }
    dump_ppm("seam2", r, 264, 198);
    CHECK(off == 0, "seam pixels off the A-B line: %d", off);
    PASS("seam: a diagonal two-colour border at 3.3x: every pixel is a mix of the two colours "
         "only");
    free(r);
    free(t.z);

    /* holes: a ring around a hole with a dot inside */
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++) {
            int32_t dx = (int32_t) x - 40, dy = (int32_t) y - 30, d2 = dx * dx + dy * dy;
            px[y * W + x] = d2 < 25 * 25 && d2 >= 14 * 14 ? 0xFF101010u
                            : d2 < 5 * 5                  ? 0xFFD02020u
                                                          : 0xFFFFFFFFu;
        }
    t = trace(px, W, H, 0, 0, &p);
    r = draw(t.z, (uint32_t) t.len, (int32_t) W, (int32_t) H, 0x00FF00u);
    CHECK((r[30 * W + 40] & 0xFFFFFFu) == 0xD02020u, "dot");
    CHECK((r[30 * W + 50] & 0xFFFFFFu) == 0xFFFFFFu, "hole %06x", r[30 * W + 50] & 0xFFFFFFu);
    CHECK((r[30 * W + 60] & 0xFFFFFFu) == 0x101010u, "ring");
    CHECK((r[30 * W + 78] & 0xFFFFFFu) == 0xFFFFFFu, "outside");
    PASS("holes: ring hole and the dot inside it survive (%u paths)", t.r.paths);
    free(r);
    free(t.z);
    free(px);
}

/* ---- fidelity on the fixtures ---------------------------------------------- */
static int32_t logo_psnr, logo_ssim, photo_psnr, photo_ssim;

static void test_fidelity(void)
{
    zvt_params_t p;
    pic_t s = decode(fx_logo_png, sizeof fx_logo_png);
    CHECK(s.px != 0, "decode logo");
    zvt_preset(&p, ZVT_PRESET_ICON);
    trace_t t = trace(s.px, s.w, s.h, fx_logo_png, sizeof fx_logo_png, &p);
    CHECK(t.len > 0 && !t.r.exact, "logo %d", t.len);
    logo_psnr = t.r.psnr_cdb, logo_ssim = t.r.ssim;
    CHECK(t.r.psnr_cdb >= 3200 && t.r.ssim >= 9800, "logo psnr %d ssim %d", t.r.psnr_cdb, t.r.ssim);
    {
        uint32_t *r = draw(t.z, (uint32_t) t.len, 512, 512, 0);
        dump_ppm("logo_4x", r, 512, 512);
        free(r);
    }
    PASS("logo 128x128 (anti-aliased, 5 colours), icon preset: PSNR %d.%02d dB, SSIM 0.%04d, %u "
         "paths, %d bytes (PNG %u) - APPROXIMATE",
         t.r.psnr_cdb / 100, t.r.psnr_cdb % 100, t.r.ssim, t.r.paths, t.len,
         (unsigned) sizeof fx_logo_png);
    /* scaling: at 4x the curved edges have intermediate coverage (no staircase) */
    uint32_t *r4 = draw(t.z, (uint32_t) t.len, 512, 512, 0);
    uint32_t edge = 0, mid = 0;
    for (uint32_t y = 40; y < 280; y++) /* the ring's area */
        for (uint32_t x = 40; x < 280; x++) {
            uint32_t c = r4[y * 512 + x], a = r4[y * 512 + x + 1];
            if (c == a) continue;
            edge++;
            uint32_t R = (c >> 16) & 255u, G = (c >> 8) & 255u;
            bool pure = (c & 0xFFFFFFu) == 0xFFFFFFu || (R == 200 && G == 30) ||
                        (c & 0xFFFFFFu) == 0x1E3CA0u;
            mid += !pure;
        }
    CHECK(edge > 0 && mid * 100 >= edge * 40, "4x edges: %u of %u intermediate", mid, edge);
    PASS("scale: logo at 4x: %u%% of edge transitions are intermediate coverage (smooth, not "
         "staircase)",
         edge ? mid * 100 / edge : 0);
    free(r4);
    free(t.z);
    free(s.px);

    s = decode(fx_photo_jpg, sizeof fx_photo_jpg);
    CHECK(s.px != 0, "decode photo");
    zvt_preset(&p, ZVT_PRESET_PHOTO);
    t = trace(s.px, s.w, s.h, fx_photo_jpg, sizeof fx_photo_jpg, &p);
    CHECK(t.len > 0 && !t.r.exact, "photo %d", t.len);
    photo_psnr = t.r.psnr_cdb, photo_ssim = t.r.ssim;
    CHECK(t.r.psnr_cdb >= 2200 && t.r.ssim >= 6000, "photo psnr %d ssim %d", t.r.psnr_cdb,
          t.r.ssim);
    {
        uint32_t *r = draw(t.z, (uint32_t) t.len, 96, 96, 0);
        dump_ppm("photo_1x", r, 96, 96);
        dump_ppm("photo_src", s.px, 96, 96);
        free(r);
    }
    PASS("photo 96x96 JPEG, photo preset: APPROXIMATE, PSNR %d.%02d dB, SSIM 0.%04d, %u paths, "
         "%u gradients, %d bytes (JPEG %u)",
         t.r.psnr_cdb / 100, t.r.psnr_cdb % 100, t.r.ssim, t.r.paths, t.r.gradients, t.len,
         (unsigned) sizeof fx_photo_jpg);
    /* every preset on every fixture gives a valid file */
    const uint8_t *fx[3] = {fx_pixel_png, fx_logo_png, fx_photo_jpg};
    uint32_t fn[3] = {sizeof fx_pixel_png, sizeof fx_logo_png, sizeof fx_photo_jpg};
    int ok = 0;
    for (int f = 0; f < 3; f++) {
        pic_t q = decode(fx[f], fn[f]);
        for (int pr = 0; pr < ZVT_PRESET_COUNT; pr++) {
            zvt_preset(&p, (zvt_preset_t) pr);
            trace_t u = trace(q.px, q.w, q.h, fx[f], fn[f], &p);
            ok += u.len > 0 && zui_zxvg_check(u.z, (uint32_t) u.len) == 0;
            free(u.z);
        }
        free(q.px);
    }
    CHECK(ok == 12, "valid %d/12", ok);
    PASS("every preset x every fixture: 12/12 outputs pass zui_zxvg_check");
    free(t.z);
    free(s.px);
}

/* ---- the depth filter on traced output --------------------------------------- */
static void filter(uint32_t *px, uint32_t n, const zui_look_t *l, const zui_depth_t *d)
{
    for (uint32_t i = 0; i < n; i++)
        px[i] = zui_chroma_px(d, zui_look_color(l, px[i] & 0xFFFFFFu, 0x101418u, 7u), 24);
}

static void test_depth(void)
{
    zvt_params_t p;
    zvt_preset(&p, ZVT_PRESET_PIXEL);
    uint32_t W = 64, H = 64, *src = malloc(W * H * 4);
    synth_flat(src, W, H);
    trace_t t = trace(src, W, H, 0, 0, &p);
    uint32_t *vec = draw(t.z, (uint32_t) t.len, (int32_t) W, (int32_t) H, 0);
    for (uint32_t i = 0; i < W * H; i++) src[i] &= 0xFFFFFFu;
    zui_depth_t d;
    zui_depth_init(&d, ZUI_DEPTH_CHROMA, 40);
    int diff = 0, changed = 0;
    for (lpx_t tag = 0; tag < 64; tag += 9) {
        zui_look_t l = zui_look(tag);
        uint32_t *a = malloc(W * H * 4), *b = malloc(W * H * 4);
        memcpy(a, src, W * H * 4), memcpy(b, vec, W * H * 4);
        filter(a, W * H, &l, &d), filter(b, W * H, &l, &d);
        diff += memcmp(a, b, W * H * 4) != 0;
        changed += memcmp(a, src, W * H * 4) != 0;
        free(a), free(b);
    }
    CHECK(diff == 0 && changed > 0, "depth filter: %d tags differ, %d changed", diff, changed);
    /* anaglyph of a traced, re-rendered pair equals the per-pixel transform */
    zui_depth_init(&d, ZUI_DEPTH_ANAGLYPH_AMBER_BLUE, 40);
    uint32_t *o = malloc(W * H * 4);
    zui_surface_t so, sl, sr;
    zui_surface_init(&so, o, (int32_t) W, (int32_t) H, (int32_t) W);
    zui_surface_init(&sl, vec, (int32_t) W, (int32_t) H, (int32_t) W);
    zui_surface_init(&sr, src, (int32_t) W, (int32_t) H, (int32_t) W);
    bool ok = zui_anaglyph(&d, &so, &sl, &sr);
    int bad = 0;
    for (uint32_t i = 0; i < W * H; i++)
        bad += o[i] != zui_anaglyph_px(ZUI_DEPTH_ANAGLYPH_AMBER_BLUE, vec[i], src[i]);
    CHECK(ok && bad == 0, "anaglyph %d %d", ok, bad);
    PASS("depth filter: zui_look + chromostereopsis on the traced re-render equals the raster's "
         "for 8 tags; anaglyph of traced surfaces is the per-pixel transform");
    free(o);
    free(vec);
    free(t.z);
    free(src);
}

/* ---- provenance ------------------------------------------------------------- */
static void test_record(void)
{
    zvt_params_t p;
    zvt_preset(&p, ZVT_PRESET_ICON);
    pic_t s = decode(fx_logo_png, sizeof fx_logo_png);
    trace_t t = trace(s.px, s.w, s.h, fx_logo_png, sizeof fx_logo_png, &p);
    uint8_t want[36] = {0x01, 0x55, 0x12, 0x20};
    sha256(fx_logo_png, sizeof fx_logo_png, want + 4);
    CHECK(memcmp(t.r.cid, want, 36) == 0, "CID");
    uint8_t rec[ZVT_REC_LEN];
    zvt_record_write(&t.r, t.z, (uint32_t) t.len, rec);
    zvt_record_t o;
    CHECK(zvt_record_read(rec, sizeof rec, t.z, (uint32_t) t.len, &o) == 0, "read");
    CHECK(!o.exact && o.psnr_cdb == t.r.psnr_cdb && o.ssim == t.r.ssim &&
              o.preset == ZVT_PRESET_ICON && o.w == 128 && memcmp(o.cid, want, 36) == 0 &&
              o.zxvg_bytes == (uint32_t) t.len,
          "fields");
    int bad = 0;
    for (uint32_t n = 0; n < sizeof rec; n++) bad += zvt_record_read(rec, n, 0, 0, &o) == 0;
    for (uint32_t i = 0; i < sizeof rec * 8; i++) {
        rec[i / 8] ^= (uint8_t) (1u << (i % 8));
        bad += zvt_record_read(rec, sizeof rec, 0, 0, &o) == 0;
        rec[i / 8] ^= (uint8_t) (1u << (i % 8));
    }
    t.z[t.len / 2] ^= 1u;
    bad += zvt_record_read(rec, sizeof rec, t.z, (uint32_t) t.len, &o) == 0;
    CHECK(bad == 0, "%d malformed records accepted", bad);
    PASS("provenance: ZXVT record round-trips, CID = CIDv1(sha256(PNG bytes)); %u truncations, "
         "%u bit flips and a changed ZXVG all refused",
         (unsigned) sizeof rec, (unsigned) sizeof rec * 8);
    free(t.z);
    free(s.px);
}

/* ---- backends, determinism, edge cases -------------------------------------- */
static int calls;
static int fail_q(void *c, const uint32_t *px, uint32_t n, const uint32_t *pal, uint32_t np,
                  uint8_t *idx)
{
    (void) c, (void) px, (void) n, (void) pal, (void) np, (void) idx;
    calls++;
    return -1;
}

static void test_backend(void)
{
    zvt_params_t p;
    zvt_preset(&p, ZVT_PRESET_PHOTO);
    pic_t s = decode(fx_photo_jpg, sizeof fx_photo_jpg);
    trace_t a = trace(s.px, s.w, s.h, 0, 0, &p), b = trace(s.px, s.w, s.h, 0, 0, &p);
    CHECK(a.len == b.len && memcmp(a.z, b.z, (size_t) a.len) == 0, "determinism");
    zvt_backend_t be = zvt_cpu_backend;
    be.name = "failing";
    be.quant = fail_q;
    trace_t c = trace_be(s.px, s.w, s.h, 0, 0, &p, &be);
    CHECK(c.len == a.len && memcmp(c.z, a.z, (size_t) a.len) == 0 && c.r.fallbacks > 0 && calls,
          "fallback");
    PASS("backends: same bytes twice; a backend that fails falls back to the CPU (%u fallbacks) "
         "with identical output",
         c.r.fallbacks);
    free(a.z), free(b.z), free(c.z), free(s.px);

    /* edge cases */
    uint32_t one = 0xFF336699u, two[2] = {0xFF000000u, 0xFFFFFFFFu}, clear[12] = {0};
    int ok = 0;
    for (int pr = 0; pr < ZVT_PRESET_COUNT; pr++) {
        zvt_preset(&p, (zvt_preset_t) pr);
        trace_t u = trace(&one, 1, 1, 0, 0, &p);
        ok += u.len > 0 && zui_zxvg_check(u.z, (uint32_t) u.len) == 0;
        free(u.z);
        u = trace(two, 2, 1, 0, 0, &p);
        ok += u.len > 0 && zui_zxvg_check(u.z, (uint32_t) u.len) == 0;
        free(u.z);
        u = trace(clear, 4, 3, 0, 0, &p);
        ok += u.len > 0 && zui_zxvg_check(u.z, (uint32_t) u.len) == 0 && u.r.paths == 0;
        free(u.z);
    }
    CHECK(ok == 12, "edge cases %d/12", ok);
    zvt_preset(&p, ZVT_PRESET_ICON);
    zvt_result_t r;
    uint8_t small[64], work[256];
    CHECK(zvt_trace(&one, 0, 1, 1, 0, 0, &p, 0, 0, work, sizeof work, small, 64, &r) == ZVT_E_SIZE,
          "0 width");
    CHECK(zvt_trace(&one, ZVT_MAX_DIM + 1, 1, ZVT_MAX_DIM + 1, 0, 0, &p, 0, 0, work, sizeof work,
                    small, 64, &r) == ZVT_E_SIZE,
          "too wide");
    CHECK(zvt_trace(&one, 1, 1, 1, 0, 0, &p, 0, 0, work, sizeof work, small, 64, &r) == ZVT_E_SPACE,
          "small work");
    uint64_t ws = zvt_work_size(64, 64);
    void *wk = malloc(ws);
    uint32_t *px = malloc(64 * 64 * 4);
    synth_flat(px, 64, 64);
    CHECK(zvt_trace(px, 64, 64, 64, 0, 0, &p, 0, 0, wk, ws, small, sizeof small, &r) == ZVT_E_SPACE,
          "small out");
    free(wk), free(px);
    PASS("edge cases: 1x1, 2x1, all-transparent under every preset; size, work and output "
         "limits refused cleanly");
}

void test_video(void); /* below */

int main(void)
{
    test_pixel_exact();
    test_seams();
    test_fidelity();
    test_depth();
    test_record();
    test_backend();
    test_video();
    printf("%s test_zui_vtrace: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails != 0;
}

/* ---- the vector film (ZXVM) ------------------------------------------------- */
typedef struct {
    uint8_t *z;
    int32_t len;
    zvm_result_t r;
} film_t;

static film_t film(const uint8_t *d, uint32_t n, zvt_preset_t preset)
{
    film_t f;
    memset(&f, 0, sizeof f);
    zvt_params_t p;
    zvt_preset(&p, preset);
    uint64_t ws = zvm_trace_video_work(d, n, 0);
    void *work = malloc(ws);
    f.z = malloc(1u << 22);
    f.len = zvm_trace_video(d, n, &p, 0, 0, work, ws, f.z, 1u << 22, &f.r);
    free(work);
    return f;
}

static void fix_sha(uint8_t *d, uint32_t n)
{
    sha256(d, n - 32u, d + n - 32u);
}

static void test_video_clip(void)
{
    /* by hand: the writer, one frame at a time, keeping every frame's ZXVG */
    zvt_params_t p;
    zvt_preset(&p, ZVT_PRESET_ILLUSTRATION);
    zui_video_info_t vi;
    CHECK(zui_video_probe(fx_clip_y4m, sizeof fx_clip_y4m, 0, &vi) == 0 && vi.frames == 14,
          "clip probe");
    zui_video_t v;
    void *vwk = malloc(vi.work);
    CHECK(zui_video_open(&v, fx_clip_y4m, sizeof fx_clip_y4m, vwk, vi.work, 0) == 0, "clip open");
    uint64_t ww = zvm_writer_work_size(vi.w, vi.h, 14);
    void *work = malloc(ww);
    uint8_t *out = malloc(1u << 22);
    uint8_t cid[ZVT_CID_LEN];
    zvt_cid(fx_clip_y4m, sizeof fx_clip_y4m, cid);
    zvm_writer_t wr;
    CHECK(zvm_writer_begin(&wr, vi.w, vi.h, 14, vi.rate_num, vi.rate_den, cid, &p, 0, 0, work, ww,
                           out, 1u << 22) == 0,
          "writer begin");
    uint8_t *frames[14];
    uint32_t flen[14];
    uint64_t pts[14], dur[14];
    bool key[14];
    uint32_t nf = 0, key0_same = 0;
    zui_video_frame_t f;
    while (nf < 14 && zui_video_next_frame(&v, &f) == 1) {
        CHECK(zvm_writer_frame(&wr, f.px, f.pts_us, f.dur_us) == 0, "frame %u", nf);
        frames[nf] = malloc(wr.zxvg_len);
        memcpy(frames[nf], wr.zxvg, wr.zxvg_len);
        flen[nf] = wr.zxvg_len, pts[nf] = f.pts_us, dur[nf] = f.dur_us, key[nf] = wr.last_key;
        if (nf == 0) { /* a keyframe is exactly the picture's own trace */
            zvt_params_t q = p;
            q.gradients = false;
            trace_t t = trace(f.px, f.w, f.h, 0, 0, &q);
            key0_same = t.len == (int32_t) flen[0] && memcmp(t.z, frames[0], flen[0]) == 0;
            free(t.z);
        }
        nf++;
    }
    zvm_result_t r;
    int32_t len = zvm_writer_end(&wr, &r);
    CHECK(len > 0 && nf == 14 && r.frames == 14, "writer end %d", len);
    CHECK(key0_same, "keyframe 0 differs from its own trace");
    CHECK(key[0] && key[9] && r.keyframes == 2, "keyframes %u (0:%d 9:%d)", r.keyframes, key[0],
          key[9]);
    CHECK(r.bytes < r.zxvg_bytes / 2u, "deltas %u vs %llu", r.bytes,
          (unsigned long long) r.zxvg_bytes);
    CHECK(memcmp(r.cid, cid, ZVT_CID_LEN) == 0 && r.duration_us == 14u * 40000u, "cid / duration");
    PASS("film: 48x32 Y4M, 14 frames, cut at 9 -> keyframes at 0 and 9 only; %u bytes for frames "
         "that "
         "are %llu bytes as separate ZXVG (%u%%), %u shapes reused, %u new",
         r.bytes, (unsigned long long) r.zxvg_bytes, (unsigned) (r.bytes * 100ull / r.zxvg_bytes),
         r.shapes_ref, r.shapes_new);

    /* the one-call path writes the same bytes */
    film_t fm = film(fx_clip_y4m, sizeof fx_clip_y4m, ZVT_PRESET_ILLUSTRATION);
    CHECK(fm.len == len && memcmp(fm.z, out, (size_t) len) == 0, "zvm_trace_video differs");

    /* play it back: every frame the very ZXVG traced, with its time */
    zvm_info_t info;
    CHECK(zvm_probe(out, (uint32_t) len, &info) == 0 && info.frames == 14 && info.w == 48 &&
              info.h == 32,
          "probe");
    CHECK(memcmp(info.cid, cid, ZVT_CID_LEN) == 0 && !info.cid_of_pixels, "info cid");
    void *pw = malloc(info.work);
    zvm_player_t pl;
    CHECK(zvm_open(&pl, out, (uint32_t) len, pw, info.work) == 0, "player open");
    uint32_t same = 0, timed = 0, drawn = 0;
    int32_t psmin = ZVT_PSNR_INF;
    uint32_t *big = malloc(192u * 128u * 4u);
    for (int pass = 0; pass < 2; pass++) { /* twice: rewind works */
        zvm_frame_t fr;
        for (uint32_t i = 0; zvm_next_frame(&pl, 0, (zui_rect_t){0, 0, 0, 0}, &fr) == 1; i++) {
            same += i < nf && fr.zxvg_len == flen[i] && memcmp(fr.zxvg, frames[i], flen[i]) == 0;
            timed += i < nf && fr.pts_us == pts[i] && fr.dur_us == dur[i] && fr.key == key[i];
            if (fr.psnr_cdb < psmin) psmin = fr.psnr_cdb;
        }
        zvm_rewind(&pl);
    }
    /* drawn at 4x through the player = the frame's ZXVG drawn directly */
    for (uint32_t i = 0; i < nf; i++) {
        zvm_frame_t fr;
        zui_surface_t s;
        for (uint32_t k = 0; k < 192u * 128u; k++) big[k] = 0xFF00FFu;
        zui_surface_init(&s, big, 192, 128, 192);
        CHECK(zvm_next_frame(&pl, &s, (zui_rect_t){0, 0, 192, 128}, &fr) == 1, "draw %u", i);
        uint32_t *ref = draw(frames[i], flen[i], 192, 128, 0xFF00FFu);
        drawn += memcmp(ref, big, 192u * 128u * 4u) == 0;
        free(ref);
    }
    /* seeking into a GOP rebuilds from its keyframe */
    zvm_frame_t fr;
    CHECK(zvm_seek(&pl, 12) == 0 && zvm_next_frame(&pl, 0, (zui_rect_t){0, 0, 0, 0}, &fr) == 1 &&
              fr.index == 12 && fr.zxvg_len == flen[12] &&
              memcmp(fr.zxvg, frames[12], flen[12]) == 0,
          "seek 12");
    CHECK(zvm_seek(&pl, 5) == 0 && zvm_next_frame(&pl, 0, (zui_rect_t){0, 0, 0, 0}, &fr) == 1 &&
              fr.zxvg_len == flen[5] && memcmp(fr.zxvg, frames[5], flen[5]) == 0,
          "seek 5");
    CHECK(same == 2u * nf && timed == 2u * nf && drawn == nf, "same %u timed %u drawn %u", same,
          timed, drawn);
    PASS("film playback: %u/%u frames rebuilt byte-identical to their traces (twice, after a "
         "rewind), "
         "times and durations kept (25 fps -> 40000 us), drawn at 4x identical to the frame ZXVG, "
         "seek rebuilds from the keyframe; per-frame PSNR min %d.%02d dB avg %d.%02d dB, SSIM min "
         "0.%04d avg 0.%04d; CID = CIDv1(sha256(Y4M))",
         same / 2u, nf, psmin / 100, psmin % 100, r.psnr_avg / 100, r.psnr_avg % 100, r.ssim_min,
         r.ssim_avg);

    /* fuzz the reader: truncation, flips (refused by the trailer), and
     * structural damage with the trailer made good again */
    uint32_t n = (uint32_t) len, refused = 0, tried = 0, ok_after = 0;
    uint8_t *m = malloc(n);
    for (uint32_t cut = 0; cut < n; cut += (cut < 200 ? 1 : 7)) {
        zvm_player_t q;
        tried++;
        refused += zvm_open(&q, out, cut, pw, info.work) < 0;
    }
    for (uint32_t i = 0; i < n; i += 3) {
        memcpy(m, out, n);
        m[i] ^= (uint8_t) (1u << (i % 8));
        zvm_player_t q;
        tried++;
        refused += zvm_open(&q, m, n, pw, info.work) < 0;
    }
    uint32_t seed = 12345u, structural = 0, sref = 0, broken = 0;
    for (int it = 0; it < 3000; it++) {
        memcpy(m, out, n);
        int nm = 1 + (int) (seed % 4u);
        for (int k = 0; k < nm; k++) {
            seed = seed * 1103515245u + 12345u;
            uint32_t at = (seed >> 8) % (n - 32u);
            seed = seed * 1103515245u + 12345u;
            m[at] = (uint8_t) (seed >> 16);
        }
        fix_sha(m, n);
        zvm_player_t q;
        zvm_info_t qi;
        structural++;
        if (zvm_probe(m, n, &qi) < 0 || qi.work > info.work * 4u) {
            sref++;
            continue;
        }
        void *qw = malloc(qi.work);
        if (zvm_open(&q, m, n, qw, qi.work) < 0) {
            sref++;
        } else { /* whatever opened plays to the end with valid frames */
            zvm_frame_t qf;
            int rc;
            uint32_t good = 1;
            while ((rc = zvm_next_frame(&q, 0, (zui_rect_t){0, 0, 0, 0}, &qf)) == 1)
                good &= zui_zxvg_check(qf.zxvg, qf.zxvg_len) == 0;
            ok_after += good && rc == 0;
            broken += !(good && rc == 0); /* must not happen */
        }
        free(qw);
    }
    CHECK(refused == tried, "refused %u of %u", refused, tried);
    CHECK(broken == 0, "%u opened films failed while playing", broken);
    PASS("ZXVM reader fails closed: %u truncations and bit flips all refused; %u structural "
         "mutations "
         "with the trailer fixed: %u refused, %u still well formed and played to the end",
         tried, structural, sref, ok_after);
    free(m);
    free(big);
    free(pw);
    free(fm.z);
    for (uint32_t i = 0; i < nf; i++) free(frames[i]);
    free(out);
    free(work);
    free(vwk);
}

static void test_video_gif(void)
{
    film_t fm = film(fx_anim_gif, sizeof fx_anim_gif, ZVT_PRESET_PIXEL);
    CHECK(fm.len > 0, "gif film %d", fm.len);
    if (fm.len <= 0) return;
    zvm_info_t info;
    CHECK(zvm_probe(fm.z, (uint32_t) fm.len, &info) == 0 && info.frames == 6, "gif probe");
    void *pw = malloc(info.work);
    zvm_player_t pl;
    CHECK(zvm_open(&pl, fm.z, (uint32_t) fm.len, pw, info.work) == 0, "gif open");
    static const uint64_t want_pts[6] = {0, 100000, 200000, 400000, 500000, 600000};
    static const uint64_t want_dur[6] = {100000, 100000, 200000, 100000, 100000, 300000};
    /* each frame is pixel-exact under the pixel preset: compare with the GIF */
    zui_video_info_t vi;
    zui_video_probe(fx_anim_gif, sizeof fx_anim_gif, 0, &vi);
    zui_video_t v;
    void *vwk = malloc(vi.work);
    zui_video_open(&v, fx_anim_gif, sizeof fx_anim_gif, vwk, vi.work, 0);
    uint32_t ok = 0, exact = 0;
    zvm_frame_t fr;
    zui_video_frame_t gf;
    for (uint32_t i = 0; i < 6 && zvm_next_frame(&pl, 0, (zui_rect_t){0, 0, 0, 0}, &fr) == 1; i++) {
        ok += fr.pts_us == want_pts[i] && fr.dur_us == want_dur[i];
        if (zui_video_next_frame(&v, &gf) == 1) {
            uint32_t *r = draw(fr.zxvg, fr.zxvg_len, 24, 16, 0xFF123456u);
            uint32_t same = 1;
            for (uint32_t k = 0; k < 24u * 16u; k++)
                same &= (r[k] & 0xFFFFFFu) == (over(gf.px[k], 0x123456u) & 0xFFFFFFu);
            exact += same && fr.psnr_cdb == ZVT_PSNR_INF;
            free(r);
        }
    }
    CHECK(ok == 6 && info.duration_us == 900000u, "gif timing %u, duration %llu", ok,
          (unsigned long long) info.duration_us);
    CHECK(exact == 6, "gif exact %u", exact);
    PASS("film: animated GIF (6 frames, delays 100/100/200/100/100/300 ms) -> ZXVM of %d bytes, "
         "pixel preset: %u/6 frames pixel-identical, presentation times and the 900 ms total kept, "
         "%u keyframe(s)",
         fm.len, exact, info.keyframes);
    zui_video_close(&v);
    free(vwk);
    free(pw);
    free(fm.z);
}

void test_video(void)
{
    test_video_clip();
    test_video_gif();
}
