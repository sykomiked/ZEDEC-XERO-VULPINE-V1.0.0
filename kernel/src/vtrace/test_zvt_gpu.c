/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zvt_gpu.c — every GPU backend present (zvt_gpu.h: CUDA, Vulkan) gives
 * the CPU reference's bits: each stage on awkward sizes and random pixels,
 * then whole traces of every fixture under every preset and a whole film,
 * byte for byte, with no fallback. A backend that is not present is [SKIP]
 * (ZVT_GPU_REQUIRE=cuda,vulkan makes that a failure).
 *
 *   test_zvt_gpu            run
 *   test_zvt_gpu --source   print the CUDA source NVRTC compiles (for the
 *                           offline PTX build and test_zvt_cuda_emu.cpp) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zx_image.h"
#include "zui_vtrace.h"
#include "zui_vtrace_video.h"
#include "zvt_gpu.h"

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

static uint64_t rng = 0x7674726163650001ull;
static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (uint32_t) rng;
}

/* a picture with flat patches, gradients, noise and some transparency */
static void picture(uint32_t *px, uint32_t w, uint32_t h, int kind)
{
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            uint32_t v;
            if (kind == 0)
                v = rnd();
            else if (kind == 1)
                v = 0xFF000000u | ((x * 7u) & 255u) << 16 | ((y * 5u) & 255u) << 8 |
                    ((x + y) & 255u);
            else
                v = ((x / 5u + y / 3u) & 1u) ? 0xFF2050A0u : 0xFFE0C040u + (rnd() & 0x0F0F0Fu);
            if (kind != 1 && (rnd() & 15u) == 0) v &= 0x7FFFFFFFu; /* partial / no alpha */
            px[y * w + x] = v;
        }
}

static void stages(const zvt_backend_t *be, const char *name)
{
    static const uint32_t sz[][2] = {{1, 1},    {2, 1},    {7, 5},    {64, 48},
                                     {333, 17}, {17, 333}, {1023, 3}, {300, 301}};
    uint32_t ok = 0, total = 0;
    for (unsigned s = 0; s < sizeof sz / sizeof sz[0]; s++)
        for (int kind = 0; kind < 3; kind++) {
            uint32_t w = sz[s][0], h = sz[s][1], n = w * h;
            uint32_t *a = malloc(n * 4), *b = malloc(n * 4), *c1 = malloc(n * 4),
                     *c2 = malloc(n * 4);
            uint8_t *i1 = malloc(n), *i2 = malloc(n), *e1 = malloc(n), *e2 = malloc(n);
            picture(a, w, h, kind);
            picture(b, w, h, (kind + 1) % 3);
            /* smooth */
            for (uint32_t thr = 0; thr <= 255; thr += 85) {
                zvt_cpu_smooth(a, c1, w, h, thr);
                memset(c2, 0xA5, n * 4);
                total++;
                ok += be->smooth(be->ctx, a, c2, w, h, thr) == 0 && !memcmp(c1, c2, n * 4);
            }
            /* quant, with 1, 7 and 256 entries */
            uint32_t pal[256];
            for (int k = 0; k < 256; k++) pal[k] = 0xFF000000u | rnd();
            for (uint32_t np = 1; np <= 256; np = np == 1 ? 7 : np == 7 ? 256 : 999) {
                zvt_cpu_quant(a, n, pal, np, i1);
                memset(i2, 0x5A, n);
                total++;
                ok += be->quant(be->ctx, a, n, pal, np, i2) == 0 && !memcmp(i1, i2, n);
            }
            /* edges of that index map */
            zvt_cpu_quant(a, n, pal, 7, i1);
            zvt_cpu_edges(i1, w, h, e1);
            memset(e2, 0x77, n);
            total++;
            ok += be->edges(be->ctx, i1, w, h, e2) == 0 && !memcmp(e1, e2, n);
            /* metric with the fidelity geometry */
            uint32_t wx = w < 8 ? w : 8, wy = h < 8 ? h : 8, st = 4;
            uint32_t nw = ((w - wx) / st + 1) * ((h - wy) / st + 1);
            uint32_t *r1 = malloc(h * 4), *r2 = malloc(h * 4), *w1 = malloc(nw * 20),
                     *w2 = malloc(nw * 20);
            zvt_cpu_metric(a, b, w, h, wx, wy, st, r1, w1);
            memset(r2, 1, h * 4), memset(w2, 2, nw * 20);
            total++;
            ok += be->metric(be->ctx, a, b, w, h, wx, wy, st, r2, w2) == 0 &&
                  !memcmp(r1, r2, h * 4) && !memcmp(w1, w2, nw * 20);
            /* diff */
            for (uint32_t thr = 0; thr <= 64; thr += 32) {
                zvt_cpu_diff(a, b, w, h, thr, i1, r1);
                memset(i2, 3, n), memset(r2, 3, h * 4);
                total++;
                ok += be->diff(be->ctx, a, b, w, h, thr, i2, r2) == 0 && !memcmp(i1, i2, n) &&
                      !memcmp(r1, r2, h * 4);
            }
            free(r1), free(r2), free(w1), free(w2);
            free(a), free(b), free(c1), free(c2), free(i1), free(i2), free(e1), free(e2);
        }
    CHECK(ok == total, "%s: %u of %u stage runs identical", name, ok, total);
    printf("%s %s stages: %u/%u runs (smooth, quant, edges, metric, diff on 8 sizes x 3 pictures) "
           "bit-identical to the CPU reference\n",
           ok == total ? "[PASS]" : "[FAIL]", name, ok, total);
}

static uint8_t *trace(const uint8_t *d, uint32_t n, zvt_preset_t pr, const zvt_backend_t *be,
                      int32_t *len, zvt_result_t *r)
{
    zx_image_info_t in;
    *len = -100;
    if (zx_image_probe(d, n, &in) != 0) return 0;
    void *work = malloc(in.work + 16);
    uint32_t *px = malloc(in.px_bytes);
    if (zx_image_decode(d, n, work, in.work + 16, px, in.w * in.h, &in) != 0) {
        free(work), free(px);
        return 0;
    }
    free(work);
    zvt_params_t p;
    zvt_preset(&p, pr);
    uint64_t ws = zvt_work_size(in.w, in.h), os = zvt_out_max(in.w, in.h);
    work = malloc(ws);
    uint8_t *out = malloc(os);
    *len = zvt_trace(px, in.w, in.h, in.w, d, n, &p, be, 0, work, ws, out, (uint32_t) os, r);
    free(work), free(px);
    return out;
}

static void whole(const zvt_backend_t *be, const char *name)
{
    static const struct {
        const uint8_t *d;
        uint32_t n;
    } fx[] = {{fx_pixel_png, sizeof fx_pixel_png},
              {fx_logo_png, sizeof fx_logo_png},
              {fx_photo_jpg, sizeof fx_photo_jpg}};
    uint32_t same = 0, total = 0, fb = 0;
    for (unsigned f = 0; f < 3; f++)
        for (int pr = 0; pr < ZVT_PRESET_COUNT; pr++) {
            int32_t l1, l2;
            zvt_result_t r1, r2;
            uint8_t *a = trace(fx[f].d, fx[f].n, (zvt_preset_t) pr, 0, &l1, &r1);
            uint8_t *b = trace(fx[f].d, fx[f].n, (zvt_preset_t) pr, be, &l2, &r2);
            total++;
            same += l1 > 0 && l1 == l2 && !memcmp(a, b, (size_t) l1) &&
                    r1.psnr_cdb == r2.psnr_cdb && r1.ssim == r2.ssim;
            fb += r2.fallbacks;
            free(a), free(b);
        }
    CHECK(same == total && fb == 0, "%s: %u of %u traces identical, %u fallbacks", name, same,
          total, fb);
    /* a film: the scene-change diff runs on the backend too */
    zvt_params_t p;
    zvt_preset(&p, ZVT_PRESET_ILLUSTRATION);
    uint64_t ws = zvm_trace_video_work(fx_clip_y4m, sizeof fx_clip_y4m, 0);
    void *work = malloc(ws);
    uint8_t *o1 = malloc(1u << 22), *o2 = malloc(1u << 22);
    zvm_result_t m1, m2;
    int32_t a =
        zvm_trace_video(fx_clip_y4m, sizeof fx_clip_y4m, &p, 0, 0, work, ws, o1, 1u << 22, &m1);
    int32_t b =
        zvm_trace_video(fx_clip_y4m, sizeof fx_clip_y4m, &p, 0, be, work, ws, o2, 1u << 22, &m2);
    CHECK(a > 0 && a == b && !memcmp(o1, o2, (size_t) a) && m2.fallbacks == 0, "%s film %d %d",
          name, a, b);
    free(o1), free(o2), free(work);
    printf("%s %s whole traces: %u/%u (3 fixtures x 4 presets) byte-identical ZXVG and scores, 0 "
           "fallbacks; the 14-frame film byte-identical ZXVM\n",
           same == total && fb == 0 && a == b ? "[PASS]" : "[FAIL]", name, same, total);
}

static int required(const char *name)
{
    const char *r = getenv("ZVT_GPU_REQUIRE");
    return r && strstr(r, name) != 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--source")) {
        char *s = zvt_cuda_source();
        if (!s) return 1;
        fputs(s, stdout);
        free(s);
        return 0;
    }
    char err[512];
    zvt_cuda_t *cu = zvt_cuda_new(0, err, sizeof err);
    if (cu) {
        zvt_backend_t be;
        zvt_cuda_backend(cu, &be);
        printf("[INFO] CUDA: %s\n", zvt_cuda_name(cu));
        stages(&be, "cuda");
        whole(&be, "cuda");
        zvt_gpu_stats_t st;
        zvt_cuda_stats(cu, &st);
        printf("[INFO] cuda: %llu calls, %llu refused, %.1f ms\n", (unsigned long long) st.calls,
               (unsigned long long) st.fallbacks, (double) st.ns / 1e6);
        zvt_cuda_free(cu);
    } else {
        printf("[SKIP] cuda: %s\n", err);
        CHECK(!required("cuda"), "CUDA required: %s", err);
    }
    zvt_vk_t *vk = zvt_vk_new(-1, err, sizeof err);
    if (vk) {
        zvt_backend_t be;
        zvt_vk_backend(vk, &be);
        printf("[INFO] Vulkan: %s\n", zvt_vk_name(vk));
        stages(&be, "vulkan");
        whole(&be, "vulkan");
        zvt_gpu_stats_t st;
        zvt_vk_stats(vk, &st);
        printf("[INFO] vulkan: %llu calls, %llu refused, %.1f ms\n", (unsigned long long) st.calls,
               (unsigned long long) st.fallbacks, (double) st.ns / 1e6);
        zvt_vk_free(vk);
    } else {
        printf("[SKIP] vulkan: %s\n", err);
        CHECK(!required("vulkan"), "Vulkan required: %s", err);
    }
    printf("%s test_zvt_gpu: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails, checks);
    return fails != 0;
}
