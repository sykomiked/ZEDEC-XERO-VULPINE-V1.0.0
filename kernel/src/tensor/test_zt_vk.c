/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zt_vk.c — the Vulkan backend (zt_vk.c) against zt_kern.h's C
 * reference, bit for bit, on any Vulkan GPU, or on lavapipe (Mesa's Vulkan on
 * the CPU, so it runs with no GPU at all). With ZT_VK_FORCE_I32=1 it checks
 * the kernels without 64-bit integers (pairs of 32-bit words, as GPUs
 * without shaderInt64 run them) on any device:
 *
 *   src/tensor/gen_zt_vk_spv.sh /tmp/zt_vk_spv.h
 *   gcc -std=c11 -O2 -Wall -Wextra -Werror -DTEST_HOST -Isrc/tensor \
 *       -DZT_VK_SPV_H='"/tmp/zt_vk_spv.h"' src/tensor/test_zt_vk.c src/tensor/zt_vk.c \
 *       src/tensor/zt_gpu.c src/tensor/zt_simd.c src/tensor/zt_simd_x86.c \
 *       src/tensor/zt_simd_neon.c src/tensor/zt_model.c src/tensor/zt_tok.c \
 *       src/tensor/zt_rope.c src/tensor/zt_gguf.c src/tensor/zt.c -ldl -lpthread -o /tmp/test_zt_vk
 *
 * The same checks as test_zt_metal.c and test_zt_cuda.c:
 * 1. random products: Q4_K, Q6_K and Q8_0 rows (every f16 class, all-ones and
 *    all-zero quants, activation shifts 0..255, and model-like values), row
 *    counts that leave the last workgroup partial, 1 token (decode) and token
 *    counts that leave the last tile partial, padded activation rows, and
 *    three products in one call sharing their input;
 * 2. products it must decline (weights outside every mapped region, zt_q8_t
 *    rows): nonzero return, nothing else;
 * 3. Qwen2.5-1.5B's shapes (1536 x 1536, 8960 x 1536, 1536 x 8960) at model
 *    scale, 1 and 37 tokens;
 * 4. whole forward passes of the fixture models with the GPU on: all logits
 *    identical to the C reference;
 * 5. the whole pass resident on the GPU (zt_kern_vk_resident, zt_gpu.h):
 *    every layer and the logits in one command buffer, the state shared:
 *    logits, cache, hidden state and saturation count identical to the C
 *    reference, also with gains that saturate; with the state not mapped,
 *    every pass is declined and the products path gives the same.
 * With no Vulkan loader, no Vulkan device, or a build without the SPIR-V it
 * prints SKIP and passes, unless ZT_VK_REQUIRE=1. ZT_VK_DEVICE=<n>
 * picks the device. */
#define _POSIX_C_SOURCE 200112L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_model_fixture.h"
#include "zt_gguf.h"
#include "zt_vk.h"
#include "zt_model.h"
#include "zt_simd.h"
#include "test_zt_fixture_run.h"
#include "test_zt_metal_gen.h"

static int failures = 0, checks = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) failures++;                                                                      \
        printf((c) ? "[PASS] " : "[FAIL] ");                                                       \
        printf(__VA_ARGS__);                                                                       \
        printf("\n");                                                                              \
    } while (0)

#define ARENA ((uint64_t) 96 << 20) /* weights live here: shared in place, so later writes show */
static uint8_t *arena;
static uint64_t arena_used;

static uint8_t *take(uint64_t n)
{
    uint8_t *p = arena + arena_used;
    arena_used += (n + 255) & ~(uint64_t) 255;
    return arena_used <= ARENA ? p : NULL;
}

static uint64_t row_bytes(uint32_t qt, uint32_t cols)
{
    return qt == ZT_GGML_Q4_K   ? (uint64_t) cols / 256 * ZT_Q4K_BYTES
           : qt == ZT_GGML_Q6_K ? (uint64_t) cols / 256 * ZT_Q6K_BYTES
                                : (uint64_t) cols / 32 * 34;
}

static zt_fx c_ref(uint32_t qt, const uint8_t *w, const int16_t *a, const uint8_t *ash,
                   const int32_t *asum, uint32_t nb)
{
    return qt == ZT_GGML_Q4_K   ? zt_dot_q4k_c(w, a, ash, asum, nb)
           : qt == ZT_GGML_Q6_K ? zt_dot_q6k_c(w, a, ash, asum, nb)
                                : zt_dot_raw_c(w, a, ash, nb);
}

typedef struct {
    uint64_t products, outs, mism;
} stat_t;

/* n products (rows[i] x cols) of type qt on one B-token input, compared */
static void mm_case(zt_vk_t *mt, uint32_t qt, uint32_t n, const uint32_t *rows, uint32_t cols,
                    uint32_t B, uint32_t pad, uint32_t qmode, uint32_t xmode, int shift_all,
                    stat_t *st)
{
    uint32_t astride = cols + 32 * pad, nb = cols / 32, sb = astride / 32;
    int16_t *a = calloc((size_t) B * astride, 2);
    uint8_t *ash = calloc((size_t) B * sb, 1);
    int32_t *asum = calloc((size_t) B * sb, 4);
    for (uint32_t t = 0; t < B; t++) {
        afill(a + (size_t) t * astride, ash + (size_t) t * sb, asum + (size_t) t * sb, nb, xmode);
        if (shift_all >= 0) memset(ash + (size_t) t * sb, shift_all, nb);
    }
    zt_mmf_t jobs[3];
    zt_fx *y[3];
    uint64_t rb = row_bytes(qt, cols);
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *w = take(rb * rows[i]);
        if (!w) {
            CHECK(0, "test arena large enough");
            return;
        }
        for (uint32_t r = 0; r < rows[i]; r++) wfill(qt, w + r * rb, cols / 256, qmode);
        y[i] = malloc((size_t) B * (rows[i] + 3) * 4);
        for (uint64_t k = 0; k < (uint64_t) B * (rows[i] + 3); k++) y[i][k] = 0x5A5A5A5A;
        zt_mmf_t j = {w, 0, qt, rows[i], cols, a, ash, asum, astride, B, y[i], rows[i] + 3};
        jobs[i] = j;
    }
    int rc = zt_vk_mm(mt, jobs, n);
    for (uint32_t i = 0; i < n; i++) {
        st->products++;
        for (uint32_t t = 0; t < B; t++)
            for (uint32_t r = 0; r < rows[i] + 3; r++) {
                zt_fx got = y[i][(size_t) t * (rows[i] + 3) + r];
                zt_fx want = r < rows[i] ? c_ref(qt, jobs[i].raw + r * rb, a + (size_t) t * astride,
                                                 ash + (size_t) t * sb, asum + (size_t) t * sb, nb)
                                         : 0x5A5A5A5A; /* the stride gap is not written */
                st->outs++;
                if ((rc || got != want) && st->mism++ < 6)
                    printf("  mismatch qt=%u rows=%u cols=%u B=%u row %u token %u: C %d, GPU "
                           "%d%s\n",
                           qt, rows[i], cols, B, r, t, want, got, rc ? " (declined)" : "");
            }
        free(y[i]);
    }
    free(a), free(ash), free(asum);
}

static void test_random(zt_vk_t *mt)
{
    static const uint32_t qts[] = {ZT_GGML_Q4_K, ZT_GGML_Q6_K, ZT_GGML_Q8_0};
    static const char *names[] = {"Q4_K", "Q6_K", "Q8_0"};
    for (uint32_t qi = 0; qi < 3; qi++) {
        stat_t st = {0, 0, 0};
        for (uint32_t c = 0; c < 400; c++) {
            arena_used = 0;
            model = c >= 256 && c % 2; /* first 256: every shift 0..255, all f16 classes */
            uint32_t n = c % 7 == 0 ? 3 : 1;
            uint32_t rows[3] = {1 + below(70), 1 + below(9), 1 + below(130)};
            uint32_t cols = 256 * (c % 13 == 0 ? 35 : 1 + below(6));
            uint32_t B = c % 3 == 0 ? 1 : 2 + below(19);
            int shift_all = c < 256 ? (int) c : -1;
            mm_case(mt, qts[qi], n, rows, cols, B, below(3), below(3), below(4), shift_all, &st);
        }
        CHECK(st.mism == 0,
              "%s: %llu GPU products (%llu outputs: 1..20 tokens, partial tiles and threadgroups, "
              "grouped calls, shifts 0..255) identical to the C reference (%llu mismatches)",
              names[qi], (unsigned long long) st.products, (unsigned long long) st.outs,
              (unsigned long long) st.mism);
    }
}

static void test_decline(zt_vk_t *mt)
{
    arena_used = 0;
    model = 1;
    int16_t a[256] = {0};
    uint8_t ash[8] = {0};
    int32_t asum[8] = {0};
    zt_fx y[4] = {7, 7, 7, 7};
    uint8_t *outside = malloc(4 * ZT_Q4K_BYTES);
    wfill(ZT_GGML_Q4_K, outside, 4, 0);
    zt_mmf_t j = {outside, 0, ZT_GGML_Q4_K, 4, 256, a, ash, asum, 256, 1, y, 4};
    int r1 = zt_vk_mm(mt, &j, 1);
    zt_q8_t q8[4];
    memset(q8, 0, sizeof q8);
    zt_mmf_t j2 = {0, q8, 0, 4, 32, a, ash, asum, 256, 1, y, 4};
    int r2 = zt_vk_mm(mt, &j2, 1);
    uint8_t *in = take(4 * ZT_Q4K_BYTES);
    wfill(ZT_GGML_Q4_K, in, 4, 0);
    zt_mmf_t pair[2] = {{in, 0, ZT_GGML_Q4_K, 4, 256, a, ash, asum, 256, 1, y, 4}, j};
    int r3 = zt_vk_mm(mt, pair, 2);
    CHECK(r1 && r2 && r3 && y[0] == 7 && y[3] == 7,
          "declines weights outside every mapped region, zt_q8_t rows, and a call holding one "
          "of them, writing nothing");
    free(outside);
}

static void test_shapes(zt_vk_t *mt)
{
    static const uint32_t shp[][2] = {{1536, 1536}, {8960, 1536}, {1536, 8960}, {256, 1536}};
    static const uint32_t qts[] = {ZT_GGML_Q4_K, ZT_GGML_Q6_K};
    stat_t st = {0, 0, 0};
    model = 1;
    for (uint32_t s = 0; s < 4; s++)
        for (uint32_t qi = 0; qi < 2; qi++)
            for (uint32_t B = 1; B <= 37; B += 36) {
                arena_used = 0;
                mm_case(mt, qts[qi], 1, &shp[s][0], shp[s][1], B, 0, 0, 3, -1, &st);
            }
    CHECK(st.mism == 0,
          "Qwen2.5-1.5B shapes (1536x1536, 8960x1536, 1536x8960, 256x1536; Q4_K and Q6_K; 1 and "
          "37 tokens): %llu outputs identical to the C reference",
          (unsigned long long) st.outs);
}

static void test_models(zt_vk_t *mt_unused)
{
    (void) mt_unused;
    zt_pool_t *pool = zt_pool_new(3);
    for (uint32_t vi = 0; vi < MF_N_VARIANTS; vi++) {
        const mf_variant_t *v = &MF_VARIANTS[vi];
        uint8_t *heap = fx_rebuild(v);
        /* the same bytes page aligned too */
        uint8_t *aligned = NULL;
        if (posix_memalign((void **) &aligned, 65536, (v->file_len + 65535) / 65536 * 65536))
            continue;
        memcpy(aligned, heap, v->file_len);
        for (uint32_t al = 0; al < 2; al++) {
            uint8_t *file = al ? aligned : heap;
            zt_vk_t *mt = zt_vk_new(-1, NULL, 0);
            zt_gguf_t g;
            zt_model_t m;
            zt_model_err_t err;
            if (!mt || zt_vk_map(mt, file, v->file_len) || zt_gguf_open(&g, file, v->file_len)) {
                CHECK(0, "%s: fixture opens and maps", v->name);
                zt_vk_free(mt);
                continue;
            }
            uint64_t ab = zt_model_arena_bytes(&g);
            void *marena = malloc(ab);
            if (zt_model_load(&m, &g, marena, ab, &err)) {
                CHECK(0, "%s: fixture loads", v->name);
                continue;
            }
            for (uint32_t kv = 0; kv < 2; kv++) {
                uint64_t n = 0, n2 = 0;
                m.kern = 0;
                zt_fx *ref = fx_run(&m, v, kv, &n);
                zt_kern_t cpu, k;
                zt_kern_threaded(&cpu, zt_simd_best(), pool);
                zt_kern_vk(&k, &cpu, mt);
                m.kern = &k;
                zt_fx *got = fx_run(&m, v, kv, &n2);
                zt_vk_stats_t ms;
                zt_vk_stats(mt, &ms);
                CHECK(n2 == n && !memcmp(ref, got, n * 4) && ms.products > 0,
                      "%s, KV %s, file %s: all %llu logits with the GPU identical to the C "
                      "reference (%llu products on the GPU, %llu calls declined)",
                      v->name, kv ? "Q8" : "Q16", al ? "page aligned" : "on the heap",
                      (unsigned long long) n, (unsigned long long) ms.products,
                      (unsigned long long) ms.declined);
                free(ref);
                free(got);
            }
            free(marena);
            zt_vk_free(mt);
        }
        free(heap);
        free(aligned);
    }
    zt_pool_free(pool);
}

/* the fixture sequence (4-token batches, all logits; then 8 greedy steps)
 * with the state in page-aligned memory, mapped for the GPU when map */
static zt_fx *run_resident(zt_vk_t *mt, const zt_model_t *m, const mf_variant_t *v, bool map,
                           uint64_t *n_out, int32_t **kv_out, uint32_t *nsat)
{
    uint32_t V = v->n_vocab, n0 = v->seq_len[0], steps = 8;
    uint64_t need = (zt_model_state_bytes(m, 64, 4, ZT_KV_Q16) + 65535) / 65536 * 65536;
    void *mem = NULL;
    if (posix_memalign(&mem, 65536, need)) return NULL;
    memset(mem, 0, need);
    if (map && zt_vk_map_shared(mt, mem, need)) return NULL;
    zt_model_state_t s;
    zt_fx *out = malloc((size_t) V * (n0 + steps) * 4);
    zt_model_state_init(&s, m, 64, 4, ZT_KV_Q16, mem, need);
    zt_model_eval(m, &s, v->seq, n0, out, ZT_EVAL_ALL_LOGITS);
    zt_fx *lg = out + (uint64_t) (n0 - 1) * V;
    for (uint32_t i = 0; i < steps; i++) {
        int32_t id = zt_argmax(lg, V);
        lg += V;
        zt_model_decode(m, &s, id, lg);
    }
    uint64_t kvn = (uint64_t) m->cfg.n_layer * 64 * m->kv_dim;
    *kv_out = malloc(kvn * 8);
    memcpy(*kv_out, s.k16, kvn * 4);
    memcpy(*kv_out + kvn, s.v16, kvn * 4);
    *nsat = s.n_sat;
    *n_out = (uint64_t) V * (n0 + steps);
    /* the memory stays mapped in mt until it is freed: not freed here */
    return out;
}

static void test_resident(void)
{
    zt_pool_t *pool = zt_pool_new(3);
    for (uint32_t vi = 0; vi < MF_N_VARIANTS; vi++) {
        const mf_variant_t *v = &MF_VARIANTS[vi];
        uint8_t *heap = fx_rebuild(v), *file = NULL;
        if (posix_memalign((void **) &file, 65536, (v->file_len + 65535) / 65536 * 65536)) continue;
        memcpy(file, heap, v->file_len);
        for (uint32_t hot = 0; hot < 2; hot++)
            for (uint32_t map = 0; map < 2; map++) {
                zt_vk_t *mt = zt_vk_new(-1, NULL, 0);
                zt_gguf_t g;
                zt_model_t m;
                zt_model_err_t err;
                if (!mt || zt_vk_map(mt, file, v->file_len) ||
                    zt_gguf_open(&g, file, v->file_len)) {
                    CHECK(0, "%s: fixture opens and maps", v->name);
                    zt_vk_free(mt);
                    continue;
                }
                uint64_t ab = zt_model_arena_bytes(&g);
                void *marena = malloc(ab);
                if (zt_model_load(&m, &g, marena, ab, &err)) {
                    CHECK(0, "%s: fixture loads", v->name);
                    continue;
                }
                if (hot) /* gains near 2^30: products and residuals saturate */
                    for (uint32_t l = 0; l < m.cfg.n_layer; l++)
                        for (uint32_t i = 0; i < m.cfg.n_embd; i++) {
                            int32_t gv = (i % 3 ? 1 : -1) * (int32_t) (0x3FFFFFFF - i * 977);
                            ((int32_t *) m.layers[l].attn_norm.g)[i] = gv;
                            ((int32_t *) m.layers[l].ffn_norm.g)[i] = gv;
                        }
                bool want = m.layers[0].wg.raw != 0 && map; /* Llama's FFN is zt_q8_t rows */
                uint64_t n = 0, n2 = 0;
                int32_t *kv_ref = NULL, *kv_got = NULL;
                uint32_t sat_ref = 0, sat_got = 0;
                m.kern = 0;
                zt_fx *ref = run_resident(mt, &m, v, false, &n, &kv_ref, &sat_ref);
                zt_kern_t cpu, k;
                zt_kern_threaded(&cpu, zt_simd_best(), pool);
                zt_kern_vk_resident(&k, &cpu, mt);
                m.kern = &k;
                zt_fx *got = run_resident(mt, &m, v, map, &n2, &kv_got, &sat_got);
                zt_vk_stats_t ms;
                zt_vk_stats(mt, &ms);
                uint64_t kvn = (uint64_t) m.cfg.n_layer * 64 * m.kv_dim;
                CHECK(ref && got && n2 == n && !memcmp(ref, got, n * 4) &&
                          !memcmp(kv_ref, kv_got, kvn * 8) && sat_ref == sat_got &&
                          (want ? ms.passes > 0 && ms.passes_declined == 0 : ms.passes == 0),
                      "%s%s, state %s: whole pass resident on the GPU: %llu logits, the cache "
                      "and the saturation count (%u) identical to the C reference (%llu passes, "
                      "%llu ops, %llu declined)",
                      v->name, hot ? ", gains near 2^30" : "", map ? "mapped" : "not mapped",
                      (unsigned long long) n, sat_got, (unsigned long long) ms.passes,
                      (unsigned long long) ms.pass_ops, (unsigned long long) ms.passes_declined);
                free(ref), free(got), free(kv_ref), free(kv_got);
                free(marena);
                zt_vk_free(mt);
            }
        free(heap);
        free(file);
    }
    zt_pool_free(pool);
}

int main(void)
{
    printf("=== zt_vk: GPU matrix products vs the C reference, bit for bit ===\n");
    char err[256] = "";
    zt_vk_t *mt = zt_vk_new(-1, err, sizeof err);
    if (!mt) {
        const char *req = getenv("ZT_VK_REQUIRE");
        int must = req && req[0] == '1';
        printf("[%s] no usable Vulkan GPU: %s\n", must ? "FAIL" : "SKIP", err);
        return must ? 1 : 0;
    }
    printf("[INFO] GPU: %s, kernels: %s\n", zt_vk_name(mt),
           zt_vk_i32(mt) ? "32-bit words only (no shaderInt64 used)" : "64-bit integers");
    if (posix_memalign((void **) &arena, 65536, ARENA) || zt_vk_map_shared(mt, arena, ARENA)) {
        CHECK(0, "the test arena maps");
        return 1;
    }
    zt_vk_stats_t ms;
    zt_vk_stats(mt, &ms);
    printf("[INFO] the test arena is %s\n",
           ms.nocopy ? "imported in place" : "mirrored (no host import on this driver)");
    test_random(mt);
    test_decline(mt);
    test_shapes(mt);
    test_models(mt);
    test_resident();
    zt_vk_free(mt);
    free(arena);
    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
