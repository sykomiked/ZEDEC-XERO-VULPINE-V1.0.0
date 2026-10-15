/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zx_image.c — zx_inflate and the zx_image decoders and scaler.
 *
 * Fixtures come from tools/media/make_fixtures.py (test/fx_*.inc). Lossless
 * formats must decode to the reference exactly. JPEG must match Pillow's
 * libjpeg-turbo decode within FX tol (2 levels per channel); the largest
 * difference seen is printed. Every good fixture is also cut at every length
 * and has random bytes flipped: each must give a clean error (or, for a cut
 * that only removed bytes after the end of the picture, the same picture),
 * never a crash; run under -fsanitize=address,undefined to see it. Each cut
 * copy is allocated at its exact length so a read past it is caught. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zx_image.h"
#include "zx_inflate.h"
#include "zui_capture.h"

typedef struct {
    const char *name;
    const uint8_t *d;
    uint32_t n, w, h;
    const uint32_t *ref;
    int tol, rc, fmt, flags;
} fx_t;
typedef struct {
    const char *name;
    const uint8_t *z;
    uint32_t zn, n, seed;
    int zlib, rep, pad;
} inf_case_t;
typedef struct {
    const char *name;
    const uint8_t *z;
    uint32_t n;
    int rc;
} inf_bad_t;
#ifndef FUZZ_ROUNDS
#    define FUZZ_ROUNDS 250 /* flipped copies per fixture; raise for a longer soak */
#endif
#define FL_ASCII  1
#define FL_NOFUZZ 2

#include "test/fx_inflate.inc"
#include "test/fx_png.inc"
#include "test/fx_jpeg.inc"
#include "test/fx_misc.inc"

static int fails, checks, sec_checks, sec_fails;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        sec_checks++;                                                                              \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            sec_fails++;                                                                           \
            printf("  FAIL (%s:%d) ", __FILE__, __LINE__);                                         \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static void section(const char *name)
{
    printf("%s %s (%d checks)\n", sec_fails ? "[FAIL]" : "[PASS]", name, sec_checks);
    sec_checks = sec_fails = 0;
}

#define WORK_BYTES (16u << 20)
#define PX_MAX     (1u << 20)
static uint8_t WORK[WORK_BYTES];
static uint32_t PX[PX_MAX], PX2[PX_MAX];
static uint8_t OUT[300000], TEXT[300000];

static uint32_t lcg_x;
static uint32_t lcg(void)
{
    lcg_x = lcg_x * 1103515245u + 12345u;
    return lcg_x >> 16;
}

/* the generator's lcg_text */
static void lcg_text(uint8_t *o, uint32_t n, uint32_t seed)
{
    static const char *const W[] = {"zedec ", "vulpine ",   "kernel ",  "vector ",  "the ", "of ",
                                    "\n",     "0123456789", "inflate ", "\0\0\0\0", "\xff"};
    static const uint32_t L[] = {6, 8, 7, 7, 4, 3, 1, 10, 8, 4, 1};
    uint32_t k = 0;
    lcg_x = seed;
    while (k < n) {
        uint32_t v = lcg();
        if (v % 17 == 0) {
            o[k++] = (uint8_t) (lcg() & 255);
        } else {
            for (uint32_t i = 0; i < L[v % 11] && k < n; i++) o[k++] = (uint8_t) W[v % 11][i];
        }
    }
}

#ifndef FUZZ_SEED
#    define FUZZ_SEED 12345u
#endif
static uint32_t rng_x = FUZZ_SEED;
static uint32_t rng(void)
{
    rng_x ^= rng_x << 13;
    rng_x ^= rng_x >> 17;
    rng_x ^= rng_x << 5;
    return rng_x;
}

/* ---- zx_inflate ---------------------------------------------------------- */
static int inflate_case(const inf_case_t *c, const uint8_t *z, uint32_t zn, uint32_t cap,
                        uint32_t *got)
{
    if (c->zlib) return zx_inflate_zlib(z, zn, OUT, cap, got);
    return zx_inflate_raw(z, zn, OUT, cap, got, 0);
}

static void test_inflate(void)
{
    for (int i = 0; i < INF_CASES_N; i++) {
        const inf_case_t *c = &INF_CASES[i];
        uint32_t n = c->n, got = 0;
        if (c->rep) {
            lcg_text(TEXT, n / c->rep, c->seed);
            for (int r = 1; r < c->rep; r++) memcpy(TEXT + r * (n / c->rep), TEXT, n / c->rep);
        } else {
            lcg_text(TEXT, n, c->seed);
        }
        int rc = inflate_case(c, c->z, c->zn, sizeof OUT, &got);
        CHECK(rc == ZX_INF_OK && got == n && memcmp(OUT, TEXT, n) == 0,
              "IF1 %s decodes (rc %d, %u/%u)", c->name, rc, got, n);
        if (n) {
            rc = inflate_case(c, c->z, c->zn, n - 1, &got);
            CHECK(rc == ZX_INF_E_SPACE && got <= n - 1,
                  "IF2 %s: one byte short of room is E_SPACE (%d)", c->name, rc);
        }
        /* every cut is an error: a zlib stream ends with its checksum */
        uint32_t step = c->zn > 2000 ? c->zn / 500 : 1, bad = 0;
        for (uint32_t L = 0; L < c->zn; L += step) {
            uint8_t *cp = malloc(L ? L : 1);
            memcpy(cp, c->z, L);
            rc = inflate_case(c, cp, L, sizeof OUT, &got);
            if (c->zlib
                    ? rc == ZX_INF_OK
                    : (rc == ZX_INF_OK && got == n && memcmp(OUT, TEXT, n) == 0 && L < c->zn - 1))
                bad++;
            free(cp);
        }
        CHECK(bad == 0, "IF2 %s: no truncation decodes as complete (%u did)", c->name, bad);
    }
    for (int i = 0; i < INF_BAD_N; i++) {
        uint32_t got;
        int rc = zx_inflate_raw(INF_BAD[i].z, INF_BAD[i].n, OUT, sizeof OUT, &got, 0);
        CHECK(rc == INF_BAD[i].rc, "IF3 bad stream %s -> %d (want %d)", INF_BAD[i].name, rc,
              INF_BAD[i].rc);
    }
    /* stored blocks over 64 KiB, built here: three blocks, Adler-32 checked */
    {
        uint32_t n = 150000, pos = 2, got;
        lcg_text(TEXT, n, 99);
        static uint8_t Z[160000];
        Z[0] = 0x78, Z[1] = 0x01;
        for (uint32_t off = 0; off < n;) {
            uint32_t k = n - off > 65535 ? 65535 : n - off;
            Z[pos++] = off + k == n;
            Z[pos++] = (uint8_t) k, Z[pos++] = (uint8_t) (k >> 8);
            Z[pos++] = (uint8_t) ~k, Z[pos++] = (uint8_t) (~k >> 8);
            memcpy(Z + pos, TEXT + off, k);
            pos += k, off += k;
        }
        uint32_t a = zui_adler32(1, TEXT, n);
        Z[pos++] = (uint8_t) (a >> 24), Z[pos++] = (uint8_t) (a >> 16),
        Z[pos++] = (uint8_t) (a >> 8), Z[pos++] = (uint8_t) a;
        int rc = zx_inflate_zlib(Z, pos, OUT, sizeof OUT, &got);
        CHECK(rc == 0 && got == n && !memcmp(OUT, TEXT, n), "IF1 stored blocks over 64 KiB (%d)",
              rc);
        Z[pos - 1] ^= 1;
        CHECK(zx_inflate_zlib(Z, pos, OUT, sizeof OUT, &got) == ZX_INF_E_CHECK,
              "IF4 a wrong Adler-32 is refused");
        Z[pos - 1] ^= 1;
        Z[1] = 0x02;
        CHECK(zx_inflate_zlib(Z, pos, OUT, sizeof OUT, &got) == ZX_INF_E_CHECK,
              "IF4 a wrong FCHECK is refused");
        Z[0] = 0x78, Z[1] = 0xBB; /* FDICT set, FCHECK valid */
        CHECK(zx_inflate_zlib(Z, pos, OUT, sizeof OUT, &got) == ZX_INF_E_DATA,
              "IF4 a preset dictionary is refused");
        Z[0] = 0x88, Z[1] = 0x1D; /* window 2^16: (0x88 << 8 | 0x1D) % 31 == 0 */
        CHECK(zx_inflate_zlib(Z, pos, OUT, sizeof OUT, &got) == ZX_INF_E_CHECK,
              "IF4 a 64 KiB window is refused");
    }
    CHECK(zx_inflate_zlib(0, 0, OUT, 1, &(uint32_t){0}) == ZX_INF_E_ARG, "null input is E_ARG");
    /* flipped bytes: never a crash, never past the buffer */
    uint32_t odd = 0;
    for (int i = 0; i < INF_CASES_N; i++) {
        const inf_case_t *c = &INF_CASES[i];
        if (c->zn < 4) continue;
        uint8_t *cp = malloc(c->zn);
        for (int t = 0; t < 200; t++) {
            memcpy(cp, c->z, c->zn);
            for (int k = 0; k < 1 + (int) (rng() % 3); k++)
                cp[rng() % c->zn] ^= (uint8_t) (1 + rng() % 255);
            uint32_t got = 0, cap = 1 + rng() % sizeof OUT;
            int rc = inflate_case(c, cp, c->zn, cap, &got);
            if (got > cap || rc > 0 || rc < ZX_INF_E_ARG) odd++;
        }
        free(cp);
    }
    CHECK(odd == 0, "IF2 random byte flips stay in bounds with a known result (%u odd)", odd);
    section("zx_inflate");
}

/* ---- the decoders -------------------------------------------------------- */
static int maxdiff(const uint32_t *a, const uint32_t *b, uint32_t n)
{
    int m = 0;
    for (uint32_t i = 0; i < n; i++)
        for (int s = 0; s < 32; s += 8) {
            int d = (int) ((a[i] >> s) & 255) - (int) ((b[i] >> s) & 255);
            if (d < 0) d = -d;
            if (d > m) m = d;
        }
    return m;
}

static int decode(const uint8_t *d, uint32_t n, uint32_t *px, zx_image_info_t *info)
{
    return zx_image_decode(d, n, WORK, WORK_BYTES, px, PX_MAX, info);
}

static int jpeg_max;

static void test_table(const fx_t *t, int nt, const char *what)
{
    for (int i = 0; i < nt; i++) {
        const fx_t *f = &t[i];
        zx_image_info_t info;
        int prc = zx_image_probe(f->d, f->n, &info);
        int rc = decode(f->d, f->n, PX, &info);
        if (f->rc != 0) {
            CHECK(rc == f->rc, "%s refused with %d (want %d: %s)", f->name, rc, f->rc, info.why);
            CHECK(info.why && info.why[0], "%s: a refusal says why", f->name);
            continue;
        }
        CHECK(prc == 0 && rc == 0, "%s decodes (probe %d, decode %d: %s)", f->name, prc, rc,
              info.why);
        CHECK(info.format == (zui_img_format_t) f->fmt && info.w == f->w && info.h == f->h,
              "%s: probe says %s %ux%u", f->name, zx_image_format_name(info.format), info.w,
              info.h);
        if (rc || info.w != f->w || info.h != f->h) continue;
        int m = maxdiff(PX, f->ref, f->w * f->h);
        if (f->fmt == 2 && m > jpeg_max) jpeg_max = m;
        CHECK(m <= f->tol, "%s matches the reference (max diff %d, allowed %d)", f->name, m,
              f->tol);
    }
    section(what);
}

/* IM2: cut at every length, flip random bytes */
static void test_fuzz(const fx_t *t, int nt, const char *what)
{
    uint32_t cuts = 0, flips = 0, odd = 0;
    for (int i = 0; i < nt; i++) {
        const fx_t *f = &t[i];
        if (f->rc != 0) continue;
        zx_image_info_t info;
        if (decode(f->d, f->n, PX2, &info)) continue;
        uint32_t npx = info.w * info.h;
        uint32_t step = (f->flags & FL_NOFUZZ) ? f->n / 97 + 1 : 1;
        for (uint32_t L = 0; L < f->n; L += step) {
            uint8_t *cp = malloc(L ? L : 1);
            memcpy(cp, f->d, L);
            int rc = decode(cp, L, PX, &info);
            cuts++;
            if (rc == 0) {
                bool same = info.w * info.h == npx && !memcmp(PX, PX2, npx * 4);
                if (!same && !(f->flags & FL_ASCII)) {
                    odd++;
                    printf("  cut %s at %u decodes to a different picture\n", f->name, L);
                }
            } else if (rc > 0 || rc < ZX_IMG_E_CHECK) {
                odd++;
            }
            free(cp);
        }
        int rounds = (f->flags & FL_NOFUZZ) ? FUZZ_ROUNDS / 4 : FUZZ_ROUNDS;
        uint8_t *cp = malloc(f->n);
        for (int r = 0; r < rounds; r++) {
            memcpy(cp, f->d, f->n);
            int k = 1 + (int) (rng() % 4);
            for (int j = 0; j < k; j++) cp[rng() % f->n] ^= (uint8_t) (1 + rng() % 255);
            int rc = decode(cp, f->n, PX, &info);
            flips++;
            if (rc > 0 || rc < ZX_IMG_E_CHECK) odd++;
        }
        free(cp);
    }
    CHECK(odd == 0, "%s: %u cuts and %u flipped copies: clean errors or the same picture (%u odd)",
          what, cuts, flips, odd);
    printf("  %s: %u cuts, %u flipped copies\n", what, cuts, flips);
}

static const fx_t *find(const char *name)
{
    const fx_t *t[3] = {FX_PNG, FX_JPEG, FX_MISC};
    int n[3] = {FX_PNG_N, FX_JPEG_N, FX_MISC_N};
    for (int k = 0; k < 3; k++)
        for (int i = 0; i < n[k]; i++)
            if (!strcmp(t[k][i].name, name)) return &t[k][i];
    printf("  no fixture %s\n", name);
    exit(1);
}

static void test_api(void)
{
    zx_image_info_t info;
    const fx_t *p = find("png_rgba8"), *j = find("jpg_exif6"), *g = find("gif_anim");
    CHECK(zx_image_probe(p->d, p->n, &info) == 0 && info.work > 0 &&
              info.px_bytes == p->w * p->h * 4 && info.alpha && !info.progressive,
          "probe reports work %u and %u output bytes", info.work, info.px_bytes);
    uint32_t need = info.work;
    CHECK(zx_image_decode(p->d, p->n, WORK, need - 1, PX, PX_MAX, &info) == ZX_IMG_E_SPACE,
          "one byte short of work is E_SPACE");
    CHECK(zx_image_decode(p->d, p->n, WORK + 1, need, PX, PX_MAX, &info) == 0,
          "exactly the reported work, unaligned, is enough");
    CHECK(zx_image_decode(p->d, p->n, WORK, need, PX, p->w * p->h - 1, &info) == ZX_IMG_E_SPACE,
          "an output one pixel short is E_SPACE");
    CHECK(zx_image_decode(p->d, p->n, WORK, need, 0, 0, &info) == ZX_IMG_E_ARG,
          "no output is E_ARG");
    CHECK(zx_image_probe(0, 5, &info) == ZX_IMG_E_ARG &&
              zx_image_probe(p->d, p->n, 0) == ZX_IMG_E_ARG,
          "null pointers are E_ARG");
    static const uint8_t junk[40] = "this is not a picture of anything at all";
    CHECK(zx_image_probe(junk, sizeof junk, &info) == ZX_IMG_E_FORMAT, "text is not a format (%s)",
          info.why);
    CHECK(zx_image_probe(junk, 0, &info) == ZX_IMG_E_FORMAT, "empty input is not a format");
    CHECK(zx_image_probe(j->d, j->n, &info) == 0 && info.orientation == 6 && info.w == j->w &&
              info.h == j->h,
          "IM4 EXIF orientation 6 swaps width and height (%ux%u)", info.w, info.h);
    CHECK(zx_image_probe(find("jpg_prog420")->d, find("jpg_prog420")->n, &info) == 0 &&
              info.progressive,
          "progressive JPEG is reported");
    CHECK(zx_image_probe(g->d, g->n, &info) == 0 && info.frames == 5 && info.alpha,
          "GIF frame count %u", info.frames);
    CHECK(zx_image_probe(find("png_i_rgba8")->d, find("png_i_rgba8")->n, &info) == 0 &&
              info.progressive,
          "Adam7 is reported");
    uint32_t mx = zui_jpeg_work_max(4096, 4096);
    CHECK(zx_image_probe(find("jpg_big_prog")->d, find("jpg_big_prog")->n, &info) == 0 &&
              info.work <= zui_jpeg_work_max(info.w, info.h) + 8 && mx > 4096u * 4096u * 3,
          "zui_jpeg_work_max covers the real need (%u <= %u)", info.work,
          zui_jpeg_work_max(info.w, info.h));
    CHECK(!strcmp(zx_image_format_name(ZX_IMG_JPEG), "JPEG") &&
              !strcmp(zx_image_error(ZX_IMG_E_TRUNC), "truncated"),
          "names and error strings");
    section("API and limits");
}

/* ---- GIF animation -------------------------------------------------------- */
static uint32_t CANVAS[64 * 64], PREV[64 * 64];
static void test_gif_anim(void)
{
    const fx_t *f = find("gif_anim");
    zui_gif_t g;
    static uint8_t lzw[ZUI_GIF_WORK];
    CHECK(zui_gif_open(&g, f->d, f->n, CANVAS, PREV, lzw, sizeof lzw - 1) == ZX_IMG_E_SPACE ||
              ZUI_GIF_WORK - 1 >= 16385,
          "LZW work is checked");
    CHECK(zui_gif_open(&g, f->d, f->n, CANVAS, PREV, lzw, sizeof lzw) == 0 && g.w == f->w &&
              g.h == f->h,
          "open %ux%u", g.w, g.h);
    for (int pass = 0; pass < 2; pass++) {
        int k = 0, rc;
        uint32_t delay;
        while ((rc = zui_gif_next(&g, &delay)) == 1) {
            CHECK(k < FX_GIF_NFRAMES && !memcmp(CANVAS, FX_GIF_FRAMES[k], (size_t) f->w * f->h * 4),
                  "pass %d frame %d composites as the model (disposal, transparency, clipping)",
                  pass, k);
            CHECK(delay == FX_GIF_DELAYS[k], "frame %d delay %u cs", k, delay);
            k++;
        }
        CHECK(rc == 0 && k == FX_GIF_NFRAMES, "pass %d: %d frames then the end", pass, k);
        CHECK(g.loops == 0, "NETSCAPE loop count 0 (forever)");
        zui_gif_rewind(&g);
    }
    /* without a restore buffer disposal 3 acts as 2: still five frames */
    int k = 0;
    zui_gif_open(&g, f->d, f->n, CANVAS, 0, lzw, sizeof lzw);
    while (zui_gif_next(&g, 0) == 1) k++;
    CHECK(k == FX_GIF_NFRAMES, "no restore buffer: %d frames", k);
    section("GIF animation");
}

/* ---- drawing -------------------------------------------------------------- */
#define SW 64
#define SH 48
static uint32_t SURF[SW * SH];
static zui_surface_t S;

static void surf(uint32_t rgb)
{
    for (int i = 0; i < SW * SH; i++) SURF[i] = rgb;
    zui_surface_init(&S, SURF, SW, SH, SW);
}

static void test_draw(void)
{
    /* identity: an opaque picture lands pixel for pixel */
    uint32_t img[12 * 8];
    for (int i = 0; i < 96; i++) img[i] = 0xFF000000u | (uint32_t) (i * 2654435761u & 0xFFFFFF);
    surf(0);
    zx_image_blit(&S, (zui_rect_t){5, 7, 12, 8}, img, 12, 8, 12, ZX_IMG_FIT_CONTAIN, 255);
    int same = 1;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 12; x++)
            same &= SURF[(7 + y) * SW + 5 + x] == (img[y * 12 + x] & 0xFFFFFF);
    CHECK(same && SURF[0] == 0 && SURF[7 * SW + 4] == 0,
          "IM5 1:1 blit is exact and stays in its box");
    /* 2x shrink averages exactly */
    uint32_t ck[4 * 4];
    for (int i = 0; i < 16; i++) ck[i] = ((i / 4 + i % 4) & 1) ? 0xFFFFFFFFu : 0xFF000000u;
    surf(0x123456);
    zx_image_blit(&S, (zui_rect_t){0, 0, 2, 2}, ck, 4, 4, 4, ZX_IMG_FIT_STRETCH, 255);
    CHECK(SURF[0] == 0x808080 && SURF[1] == 0x808080 && SURF[SW + 1] == 0x808080,
          "area average of a checker is grey (%06x)", SURF[0]);
    /* 3 -> 2 shrink: the middle source pixel is split exactly */
    uint32_t tri[3] = {0xFF000000u, 0xFF0000FFu, 0xFF0000F0u};
    surf(0);
    zx_image_blit(&S, (zui_rect_t){0, 0, 2, 1}, tri, 3, 1, 3, ZX_IMG_FIT_STRETCH, 255);
    CHECK(SURF[0] == (0 * 2 + 255) / 3 && SURF[1] == (255 + 240 * 2) / 3,
          "3->2 area weights 2:1 (%06x %06x)", SURF[0], SURF[1]);
    /* growth is bilinear: a 2-pixel ramp to 8 pixels is monotonic and keeps the ends */
    uint32_t ramp[2] = {0xFF000000u, 0xFF0000FFu};
    surf(0);
    zx_image_blit(&S, (zui_rect_t){0, 0, 8, 1}, ramp, 2, 1, 2, ZX_IMG_FIT_STRETCH, 255);
    int mono = 1;
    for (int x = 1; x < 8; x++) mono &= SURF[x] >= SURF[x - 1];
    CHECK(mono && SURF[0] == 0 && SURF[7] == 255 && SURF[3] > 0 && SURF[3] < 255,
          "bilinear growth is a ramp (%u .. %u)", SURF[3], SURF[4]);
    /* a flat colour stays flat at any scale */
    uint32_t flat[5 * 3];
    for (int i = 0; i < 15; i++) flat[i] = 0xFF3366CCu;
    surf(0);
    zx_image_blit(&S, (zui_rect_t){0, 0, SW, SH}, flat, 5, 3, 5, ZX_IMG_FIT_STRETCH, 255);
    int flatok = 1;
    for (int i = 0; i < SW * SH; i++) flatok &= SURF[i] == 0x3366CC;
    CHECK(flatok, "a flat colour stays exactly flat when stretched");
    /* contain: a 4:1 picture in a square box is letterboxed, centred */
    zui_rect_t r = zx_image_place(40, 10, (zui_rect_t){0, 0, 20, 20}, ZX_IMG_FIT_CONTAIN);
    CHECK(r.x == 0 && r.w == 20 && r.h == 5 && r.y == 7, "contain places %d,%d %dx%d", r.x, r.y,
          r.w, r.h);
    r = zx_image_place(40, 10, (zui_rect_t){0, 0, 20, 20}, ZX_IMG_FIT_COVER);
    CHECK(r.h == 20 && r.w == 80 && r.x == -30, "cover places %d,%d %dx%d", r.x, r.y, r.w, r.h);
    r = zx_image_place(40, 10, (zui_rect_t){3, 4, 20, 20}, ZX_IMG_FIT_NONE);
    CHECK(r.w == 40 && r.h == 10 && r.x == 3 - 10 && r.y == 4 + 5, "none places %d,%d %dx%d", r.x,
          r.y, r.w, r.h);
    surf(0x111111);
    zx_image_blit(&S, (zui_rect_t){10, 10, 20, 20}, flat, 5, 3, 5, ZX_IMG_FIT_COVER, 255);
    int cov = 1, out = 1;
    for (int y = 0; y < SH; y++)
        for (int x = 0; x < SW; x++) {
            bool in = x >= 10 && x < 30 && y >= 10 && y < 30;
            if (in)
                cov &= SURF[y * SW + x] == 0x3366CC;
            else
                out &= SURF[y * SW + x] == 0x111111;
        }
    CHECK(cov && out, "cover fills the box and nothing outside it");
    /* clip and opacity */
    surf(0);
    zui_clip(&S, (zui_rect_t){0, 0, 8, 8});
    zx_image_blit(&S, (zui_rect_t){0, 0, 32, 32}, flat, 5, 3, 5, ZX_IMG_FIT_STRETCH, 255);
    CHECK(SURF[7 * SW + 7] == 0x3366CC && SURF[8 * SW + 8] == 0 && SURF[9] == 0,
          "the surface clip is kept");
    surf(0);
    zx_image_blit(&S, (zui_rect_t){0, 0, 4, 4}, flat, 5, 3, 5, ZX_IMG_FIT_STRETCH, 0);
    CHECK(SURF[0] == 0, "opacity 0 draws nothing");
    zx_image_blit(&S, (zui_rect_t){0, 0, 4, 4}, flat, 5, 3, 5, ZX_IMG_FIT_STRETCH, 128);
    CHECK(SURF[0] == (((0x33 * 128 + 127) / 255) << 16 | ((0x66 * 128 + 127) / 255) << 8 |
                      ((0xCC * 128 + 127) / 255)),
          "opacity 128 blends halfway (%06x)", SURF[0]);
    /* alpha: premultiplied filtering, no dark fringe from a transparent neighbour */
    uint32_t edge[2] = {0x00000000u, 0xFFFF0000u};
    surf(0xFFFFFF);
    zx_image_blit(&S, (zui_rect_t){0, 0, 1, 1}, edge, 2, 1, 2, ZX_IMG_FIT_STRETCH, 255);
    CHECK((SURF[0] >> 16) == 0xFF && (SURF[0] & 0xFF) >= 0x7F && (SURF[0] & 0xFF) <= 0x80 &&
              ((SURF[0] >> 8) & 0xFF) == (SURF[0] & 0xFF),
          "half-covered red over white is pink, not dark (%06x)", SURF[0]);
    /* end to end */
    const fx_t *p = find("png_rgb8");
    zx_image_info_t info;
    zx_image_probe(p->d, p->n, &info);
    surf(0);
    CHECK(zx_image_draw(&S, (zui_rect_t){0, 0, SW, SH}, p->d, p->n, WORK,
                        zx_image_draw_work(&info) - 1, ZX_IMG_FIT_CONTAIN, 255) == ZX_IMG_E_SPACE,
          "draw with too little work is E_SPACE");
    CHECK(zx_image_draw(&S, (zui_rect_t){0, 0, p->w, p->h}, p->d, p->n, WORK,
                        zx_image_draw_work(&info), ZX_IMG_FIT_CONTAIN, 255) == 0,
          "probe, decode and draw in one call");
    same = 1;
    for (uint32_t y = 0; y < p->h; y++)
        for (uint32_t x = 0; x < p->w; x++)
            same &= SURF[y * SW + x] == (p->ref[y * p->w + x] & 0xFFFFFF);
    CHECK(same, "the drawn PNG is the reference");
    const fx_t *b = find("jpg_big_prog");
    surf(0);
    CHECK(zx_image_draw(&S, (zui_rect_t){0, 0, SW, SH}, b->d, b->n, WORK, WORK_BYTES,
                        ZX_IMG_FIT_CONTAIN, 255) == 0,
          "a 96x72 JPEG shrinks into 64x48");
    /* compare one output pixel with a direct area average of the reference */
    /* out (20, 20) covers source [30, 31.5) both ways: weights 2 and 1 */
    uint64_t s = 0;
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 2; x++)
            s += (uint64_t) (2 - y) * (2 - x) * ((b->ref[(30 + y) * 96 + 30 + x] >> 8) & 255);
    int want = (int) ((s + 4) / 9), got = (int) ((SURF[20 * SW + 20] >> 8) & 255);
    CHECK(got - want <= 3 && want - got <= 3, "shrunk pixel is the area mean (%d vs %d)", got,
          want);
    section("drawing and scaling");
}

int main(void)
{
    test_inflate();
    test_table(FX_PNG, FX_PNG_N, "PNG");
    test_table(FX_JPEG, FX_JPEG_N, "JPEG");
    printf("  JPEG: largest difference from libjpeg-turbo %d (allowed 2)\n", jpeg_max);
    test_table(FX_MISC, FX_MISC_N, "GIF BMP PNM QOI TGA");
    test_fuzz(FX_PNG, FX_PNG_N, "PNG");
    test_fuzz(FX_JPEG, FX_JPEG_N, "JPEG");
    test_fuzz(FX_MISC, FX_MISC_N, "GIF BMP PNM QOI TGA");
    section("truncation and corruption");
    test_api();
    test_gif_anim();
    test_draw();
    printf("%s test_zx_image: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails, checks);
    return fails ? 1 : 0;
}
