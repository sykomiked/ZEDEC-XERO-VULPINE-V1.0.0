/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zt_metal_math.c — the Metal kernels' arithmetic (zt_metal_math.h),
 * compiled as C on any host, against zt_kern.h's C reference bit for bit.
 *
 * The GPU sums a row's sub-block terms in 32 lanes (lane l takes sub-blocks
 * l, l + 32, ...) and then adds the lanes; this test sums them in that order
 * too, and in a few others, on Q4_K, Q6_K and Q8_0 rows with random, all-ones
 * and all-zero quants, every f16 class (zero, subnormal, inf/NaN, largest)
 * and activation shifts 0..255 (term52's exact, flooring and saturating
 * paths). Then a C stand-in for the GPU's whole-matrix hook (zt_kern_t
 * mm_full, as zt_metal.m implements it: Q8_0/Q4_K/Q6_K only, all or nothing
 * per call) runs the fixture models' forward passes, which must give the C
 * reference's logits: that checks zt_model.c's use of the hook (grouped
 * q/k/v and gate/up products, bias and saturation count after it, fallback
 * when it declines). test_zt_metal.c then checks the GPU itself, on a Mac. */
#define _POSIX_C_SOURCE 200112L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_model_fixture.h"
#include "zt_gguf.h"
#include "zt_kern.h"
#include "zt_model.h"
#include "test_zt_fixture_run.h"
#include "test_zt_metal_gen.h"

#include "zt_metal_c.h"

static int failures = 0, checks = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) failures++;                                                                      \
        printf((c) ? "[PASS] " : "[FAIL] ");                                                       \
        printf(__VA_ARGS__);                                                                       \
        printf("\n");                                                                              \
    } while (0)

/* The GPU's order: 32 lanes, lane l takes sub-blocks l, l + 32, ...; then the
 * lanes are added pairwise (16, 8, 4, 2, 1 apart), as simd shuffles do. */
static zt_fx gpu_order(uint32_t qt, const uint8_t *w, const int16_t *a, const uint8_t *ash,
                       const int32_t *asum, uint32_t nb, uint32_t lanes)
{
    int64_t acc[64] = {0};
    for (uint32_t l = 0; l < lanes; l++)
        for (uint32_t b = l; b < nb; b += lanes)
            acc[l] += zm_sub(qt, w, b, a + b * 32, ash[b], asum[b]);
    for (uint32_t off = lanes / 2; off; off /= 2)
        for (uint32_t l = 0; l < off; l++) acc[l] += acc[l + off];
    return zm_round(acc[0]);
}

static zt_fx c_ref(uint32_t qt, const uint8_t *w, const int16_t *a, const uint8_t *ash,
                   const int32_t *asum, uint32_t nb)
{
    return qt == ZT_GGML_Q4_K   ? zt_dot_q4k_c(w, a, ash, asum, nb)
           : qt == ZT_GGML_Q6_K ? zt_dot_q6k_c(w, a, ash, asum, nb)
                                : zt_dot_raw_c(w, a, ash, nb);
}

/* The stand-in accelerator: refuses zt_q8_t rows (as the GPU does) and, when
 * fctx is non-null, every third call, so both paths are taken. */
static uint32_t emu_calls, emu_jobs;

static int emu_mm_full(void *fctx, const zt_mmf_t *jobs, uint32_t n)
{
    emu_calls++;
    if (fctx && emu_calls % 3 == 0) return 1;
    for (uint32_t i = 0; i < n; i++)
        if (!jobs[i].raw) return 1;
    for (uint32_t i = 0; i < n; i++) {
        const zt_mmf_t *j = &jobs[i];
        uint32_t nb = j->cols / 32, rb = zm_row_bytes(j->qtype, nb);
        for (uint32_t t = 0; t < j->B; t++)
            for (uint32_t r = 0; r < j->rows; r++)
                j->y[(uint64_t) t * j->ystride + r] = gpu_order(
                    j->qtype, j->raw + (uint64_t) r * rb, j->a + (uint64_t) t * j->astride,
                    j->ash + (uint64_t) t * (j->astride / 32),
                    j->asum + (uint64_t) t * (j->astride / 32), nb, 32);
        emu_jobs++;
    }
    return 0;
}

static void test_models(void)
{
    for (uint32_t vi = 0; vi < MF_N_VARIANTS; vi++) {
        const mf_variant_t *v = &MF_VARIANTS[vi];
        uint8_t *file = fx_rebuild(v);
        zt_gguf_t g;
        zt_model_t m;
        zt_model_err_t err;
        if (zt_gguf_open(&g, file, v->file_len)) {
            CHECK(0, "%s: fixture opens", v->name);
            continue;
        }
        uint64_t ab = zt_model_arena_bytes(&g);
        void *arena = malloc(ab);
        if (zt_model_load(&m, &g, arena, ab, &err)) {
            CHECK(0, "%s: fixture loads", v->name);
            continue;
        }
        for (uint32_t kv = 0; kv < 2; kv++) {
            uint64_t n = 0;
            m.kern = 0;
            zt_fx *ref = fx_run(&m, v, kv, &n);
            for (uint32_t refuse = 0; refuse < 2; refuse++) {
                zt_kern_t k = {"emu", 0, 0, 0, 0, 0, 0, 0, 0, 0, emu_mm_full, refuse ? &k : 0, 0};
                m.kern = &k;
                emu_calls = emu_jobs = 0;
                uint64_t n2 = 0;
                zt_fx *got = fx_run(&m, v, kv, &n2);
                CHECK(n2 == n && !memcmp(ref, got, n * 4) && emu_jobs > 0,
                      "%s, KV %s, whole-matrix hook%s: all %llu logits identical to the C "
                      "reference (%u calls, %u products on the stand-in)",
                      v->name, kv ? "Q8" : "Q16", refuse ? " declining every third call" : "",
                      (unsigned long long) n, emu_calls, emu_jobs);
                free(got);
            }
            free(ref);
        }
        free(arena);
        free(file);
    }
}

int main(void)
{
    printf("=== zt_metal_math: the Metal kernels' integer arithmetic vs zt_kern.h, as C ===\n");
    static const uint32_t qts[] = {ZT_GGML_Q4_K, ZT_GGML_Q6_K, ZT_GGML_Q8_0};
    static const char *names[] = {"Q4_K", "Q6_K", "Q8_0"};
    static const uint32_t lens[] = {1, 2, 3, 6, 35}; /* 256 .. 8960 values */
    for (uint32_t qi = 0; qi < 3; qi++) {
        uint32_t qt = qts[qi];
        uint64_t rows = 0, mism = 0, nonzero = 0, sat = 0;
        for (uint32_t t = 0; t < 6000; t++) {
            uint32_t nkb = t < 256 * 3 ? 2 : (t % 5 == 0 ? lens[below(5)] : 1 + below(8));
            uint32_t nb = nkb * 8;
            uint8_t *w = malloc((size_t) nb * 34 + nkb * ZT_Q6K_BYTES);
            int16_t *a = malloc((size_t) nb * 64);
            uint8_t *ash = malloc(nb);
            int32_t *asum = malloc((size_t) nb * 4);
            uint32_t qmode = below(3), xmode = below(4);
            model = t >= 256 * 3 && t % 2;
            wfill(qt, w, nkb, qmode);
            afill(a, ash, asum, nb, xmode);
            if (t < 256 * 3) /* first 768 rows: every shift 0..255, three times */
                memset(ash, (int) (t % 256), nb);
            zt_fx c = c_ref(qt, w, a, ash, asum, nb);
            zt_fx g32 = gpu_order(qt, w, a, ash, asum, nb, 32);
            zt_fx g16 = gpu_order(qt, w, a, ash, asum, nb, 16);
            zt_fx g1 = gpu_order(qt, w, a, ash, asum, nb, 1);
            rows++;
            nonzero += c != 0;
            sat += c == INT32_MAX || c == INT32_MIN;
            if ((c != g32 || c != g16 || c != g1) && mism++ < 4)
                printf("  %s mismatch nb=%u: C %d, lanes32 %d, lanes16 %d, serial %d\n", names[qi],
                       nb, c, g32, g16, g1);
            free(w), free(a), free(ash), free(asum);
        }
        CHECK(mism == 0,
              "%s: %llu rows identical to the C reference in 32-lane, 16-lane and serial order "
              "(%llu nonzero, %llu saturated; %llu mismatches)",
              names[qi], (unsigned long long) rows, (unsigned long long) nonzero,
              (unsigned long long) sat, (unsigned long long) mism);
    }
    test_models();
    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
