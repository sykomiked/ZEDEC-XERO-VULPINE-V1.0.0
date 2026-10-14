/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zt_lora.c — T23 LoRA adapters on one shared base (zt_model.h): the
 * three tiny models of test_model_fixture.h with the adapters of
 * test_lora_fixture.h (gen_lora_fixture.py, written by gguf-py as llama.cpp's
 * convert_lora_to_gguf.py writes them), against a float64 reference of each
 * base with its adapter merged. Also: no adapter and an all-zero adapter give
 * the base model's bits; batching, every SIMD kernel set and thread counts
 * give the same bits; several sequences share one base, each with its own
 * adapter or none, without disturbing each other; bad adapters are refused.
 *
 *   gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc/tensor src/tensor/test_zt_lora.c \
 *       src/tensor/zt_simd.c src/tensor/zt_simd_x86.c src/tensor/zt_simd_neon.c \
 *       src/tensor/zt_model.c src/tensor/zt_tok.c src/tensor/zt_rope.c src/tensor/zt_gguf.c \
 *       src/tensor/zt.c -lpthread -o /tmp/test_zt_lora && /tmp/test_zt_lora
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zt_model.h"
#include "zt_simd.h"
#include "test_model_fixture.h"
#include "test_lora_fixture.h"

static int failures = 0, checks = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            failures++;                                                                            \
            printf("[FAIL] ");                                                                     \
        } else                                                                                     \
            printf("[PASS] ");                                                                     \
        printf(__VA_ARGS__);                                                                       \
        printf("\n");                                                                              \
    } while (0)

/* ---- rebuilding the fixture files (gen_model_fixture.py / gen_lora_fixture.py) ---- */

static uint64_t mix(uint64_t seed, uint64_t i)
{
    uint64_t z = seed + (i + 1) * 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static void put_f32(uint8_t *p, float v)
{
    memcpy(p, &v, 4);
}

static float pow2neg(uint32_t k)
{
    float f = 1.0f;
    while (k--) f *= 0.5f;
    return f;
}

static uint8_t *rebuild(const char *hdr, uint32_t hdr_len, uint64_t file_len, const mf_gen_t *gen,
                        uint32_t n_gen)
{
    uint8_t *b = calloc(1, file_len);
    memcpy(b, hdr, hdr_len);
    for (uint32_t t = 0; t < n_gen; t++) {
        const mf_gen_t *g = &gen[t];
        uint8_t *p = b + hdr_len + g->off;
        if (g->kind == 1) {
            for (uint64_t k = 0; k < g->n / 32; k++, p += 34) {
                uint16_t d = (uint16_t) ((g->param << 10) | (mix(g->seed, k * 33) & 0x3FF));
                p[0] = (uint8_t) d;
                p[1] = (uint8_t) (d >> 8);
                for (int i = 0; i < 32; i++)
                    p[2 + i] =
                        (uint8_t) (int8_t) ((int) (mix(g->seed, k * 33 + 1 + i) % 255) - 127);
            }
        } else if (g->kind == 2) {
            for (uint64_t i = 0; i < g->n; i++) {
                uint64_t z = mix(g->seed, i);
                uint16_t h = (uint16_t) ((((z >> 20) & 1) << 15) |
                                         ((g->param - ((z >> 16) & 1)) << 10) | (z & 0x3FF));
                p[2 * i] = (uint8_t) h;
                p[2 * i + 1] = (uint8_t) (h >> 8);
            }
        } else if (g->kind == 6 || g->kind == 7) {
            for (uint64_t i = 0; i < g->n; i++) {
                uint64_t z = mix(g->seed, i);
                if (g->kind == 6) {
                    put_f32(p + 4 * i, (float) ((int) (z % 2049) - 1024) * pow2neg(g->param));
                } else {
                    float f = (float) ((int) (z % 257) - 128) * pow2neg(g->param);
                    uint32_t u;
                    memcpy(&u, &f, 4);
                    p[2 * i] = (uint8_t) (u >> 16);
                    p[2 * i + 1] = (uint8_t) (u >> 24);
                }
            }
        } else {
            for (uint64_t i = 0; i < g->n; i++) {
                uint64_t z = mix(g->seed, i);
                float f = g->kind == 3   ? 1.0f + (float) ((int) (z % 1025) - 512) / 2048.0f
                          : g->kind == 4 ? (float) ((int) (z % 2049) - 1024) / 4096.0f
                                         : 1.0f + (float) i / 8.0f;
                put_f32(p + 4 * i, f);
            }
        }
    }
    return b;
}

static uint64_t fnv(const uint8_t *b, uint64_t n)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint64_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x100000001b3ull;
    return h;
}

/* ---- helpers ---- */

typedef struct {
    zt_gguf_t g;
    zt_model_t m;
    uint8_t *file, *arena;
} base_t;

typedef struct {
    zt_gguf_t g;
    zt_lora_t lo;
    uint8_t *arena;
} adapter_t;

static int32_t load_lora(adapter_t *A, const zt_model_t *m, const uint8_t *buf, uint64_t n,
                         uint32_t scale, zt_model_err_t *err)
{
    memset(A, 0, sizeof *A);
    int32_t r = zt_gguf_open(&A->g, buf, n);
    if (r) return r;
    uint64_t need = zt_lora_arena_bytes(m, &A->g);
    if (!need) return zt_lora_load(&A->lo, m, &A->g, scale, 0, 0, err); /* says why */
    A->arena = malloc(need);
    r = zt_lora_load(&A->lo, m, &A->g, scale, A->arena, need, err);
    return r;
}

typedef struct {
    zt_model_state_t s;
    uint8_t *mem;
} state_t;

static void mkstate(state_t *S, const zt_model_t *m, uint32_t batch, const zt_lora_t *lo)
{
    uint64_t need = zt_model_state_bytes(m, 64, batch, ZT_KV_Q16);
    S->mem = malloc(need);
    zt_model_state_init(&S->s, m, 64, batch, ZT_KV_Q16, S->mem, need);
    zt_model_state_set_lora(&S->s, m, lo);
}

/* all logits of seq (n tokens) with batch size `batch`, decoding one at a
 * time when batch is 0 */
static zt_fx *run(const zt_model_t *m, const zt_lora_t *lo, const int32_t *seq, uint32_t n,
                  uint32_t batch)
{
    uint32_t V = m->cfg.n_vocab;
    zt_fx *lg = malloc((size_t) n * V * 4);
    state_t S;
    mkstate(&S, m, batch ? batch : 1, lo);
    if (batch) {
        zt_model_eval(m, &S.s, seq, n, lg, ZT_EVAL_ALL_LOGITS);
    } else {
        for (uint32_t i = 0; i < n; i++) zt_model_decode(m, &S.s, seq[i], lg + (size_t) i * V);
    }
    free(S.mem);
    return lg;
}

/* a tensor's data inside a rebuilt file, for patching */
static uint8_t *tensor_data(uint8_t *file, uint64_t n, const char *name, uint64_t *bytes)
{
    zt_gguf_t g;
    zt_gguf_tensor_t t;
    if (zt_gguf_open(&g, file, n) || zt_gguf_find_tensor(&g, name, &t)) return 0;
    *bytes = t.n_bytes;
    return file + (t.data - file);
}

/* first occurrence of needle in the header */
static uint8_t *find(uint8_t *hay, uint64_t n, const char *needle)
{
    uint64_t k = strlen(needle);
    for (uint64_t i = 0; i + k <= n; i++)
        if (!memcmp(hay + i, needle, k)) return hay + i;
    return 0;
}

static int32_t try_lora(const zt_model_t *m, const uint8_t *buf, uint64_t n, uint32_t scale,
                        zt_model_err_t *e)
{
    adapter_t A;
    int32_t r = load_lora(&A, m, buf, n, scale, e);
    free(A.arena);
    return r;
}

typedef struct {
    double maxerr, sumsq;
    uint64_t n;
    uint32_t top1, top1_n, top1_clear, top1_clear_n;
} acc_t;

static void accuracy(const lf_variant_t *v, const zt_model_t *m, const zt_lora_t *lo, acc_t *a)
{
    memset(a, 0, sizeof *a);
    uint32_t V = m->cfg.n_vocab, off = 0;
    for (uint32_t s = 0; s < v->n_seq; s++) {
        uint32_t n = v->seq_len[s];
        zt_fx *lg = run(m, lo, v->seq + off, n, 8);
        for (uint32_t p = 0; p < n; p++) {
            const zt_fx *row = lg + (size_t) p * V;
            for (uint32_t i = 0; i < LF_HEAD; i++) {
                double e = (row[i] - v->head[(size_t) (off + p) * LF_HEAD + i]) / 65536.0;
                if (e < 0) e = -e;
                if (e > a->maxerr) a->maxerr = e;
                a->sumsq += e * e;
                a->n++;
            }
            int32_t am = zt_argmax(row, V);
            a->top1_n++;
            a->top1 += am == v->argmax[off + p];
            if (v->gap[off + p] > 0.05 * 65536) {
                a->top1_clear_n++;
                a->top1_clear += am == v->argmax[off + p];
            }
        }
        const zt_fx *row = lg + (size_t) (n - 1) * V;
        for (uint32_t i = 0; i < V; i++) {
            double e = (row[i] - v->last[(size_t) s * V + i]) / 65536.0;
            if (e < 0) e = -e;
            if (e > a->maxerr) a->maxerr = e;
            a->sumsq += e * e;
            a->n++;
        }
        free(lg);
        off += n;
    }
}

static bool same(const zt_fx *a, const zt_fx *b, uint64_t n)
{
    return !memcmp(a, b, n * 4);
}

int main(void)
{
    printf("=== T23 LoRA adapters on a shared base ===\n");
    base_t B[MF_N_VARIANTS];
    for (uint32_t vi = 0; vi < MF_N_VARIANTS; vi++) {
        const mf_variant_t *v = &MF_VARIANTS[vi];
        B[vi].file = rebuild(v->hdr, v->hdr_len, v->file_len, v->gen, v->n_gen);
        zt_gguf_open(&B[vi].g, B[vi].file, v->file_len);
        uint64_t ab = zt_model_arena_bytes(&B[vi].g);
        B[vi].arena = malloc(ab);
        zt_model_err_t e;
        int32_t r = zt_model_load(&B[vi].m, &B[vi].g, B[vi].arena, ab, &e);
        CHECK(fnv(B[vi].file, v->file_len) == v->fnv && r == 0,
              "%s base: rebuilt from test_model_fixture.h and loaded", v->name);
        if (r) return 1;
    }

    uint8_t *F[LF_N_VARIANTS];
    adapter_t A[LF_N_VARIANTS];
    for (uint32_t li = 0; li < LF_N_VARIANTS; li++) {
        const lf_variant_t *v = &LF_VARIANTS[li];
        const zt_model_t *m = &B[v->base].m;
        F[li] = rebuild(v->hdr, v->hdr_len, v->file_len, v->gen, v->n_gen);
        CHECK(fnv(F[li], v->file_len) == v->fnv,
              "%s adapter: rebuilt file is byte-identical to gguf-py's (%llu bytes)", v->name,
              (unsigned long long) v->file_len);
        zt_model_err_t e;
        memset(&e, 0, sizeof e);
        int32_t r = load_lora(&A[li], m, F[li], v->file_len, v->scale_q16, &e);
        CHECK(r == 0 && A[li].lo.n_mats == v->n_mats * m->cfg.n_layer &&
                  A[li].lo.max_rank == v->rank && A[li].lo.scale_q16 == v->scale_q16,
              "%s adapter: loads, %u matrices of rank %u (%d %s %s)", v->name, A[li].lo.n_mats,
              A[li].lo.max_rank, (int) r, r ? e.what : "", r ? e.name : "");
        if (r) return 1;
    }

    /* accuracy against the merged float64 reference. The base alone is
     * within 0.005 (Q8_0 weights) / 0.2 (llama's converted F16) of its own
     * reference (test_zt_model). The adapter's weights are stored as 8-bit
     * blocks: a numpy simulation of that rounding alone (scale * B and A
     * rounded per block of 32, merged, float64 forward) moves these logits
     * by 0.105 (qwen2), 0.038 (qwen3) and 0.106 (llama). The Q16 and
     * 16-bit-block rounding of the rank intermediate adds little on top. */
    double tol[LF_N_VARIANTS] = {0.15, 0.06, 0.3};
    for (uint32_t li = 0; li < LF_N_VARIANTS; li++) {
        const lf_variant_t *v = &LF_VARIANTS[li];
        acc_t a;
        accuracy(v, &B[v->base].m, &A[li].lo, &a);
        CHECK(a.maxerr < tol[li],
              "%s + LoRA: logits within %.2f of the merged float64 model (max abs err %.5f, rms "
              "%.5f, %llu values)",
              v->name, tol[li], a.maxerr, a.n ? __builtin_sqrt(a.sumsq / a.n) : 0,
              (unsigned long long) a.n);
        CHECK(
            a.top1_clear == a.top1_clear_n,
            "%s + LoRA: top-1 agrees at every position with a top-2 gap > 0.05 (%u/%u; all %u/%u)",
            v->name, a.top1_clear, a.top1_clear_n, a.top1, a.top1_n);
        /* the base without the adapter is far from that reference: the
         * adapter really changes the model */
        accuracy(v, &B[v->base].m, 0, &a);
        CHECK(a.maxerr > 1.0, "%s without its adapter: %.2f away from the merged reference",
              v->name, a.maxerr);
    }

    /* null adapter, and an adapter whose B is all zero, give the base's bits */
    for (uint32_t li = 0; li < LF_N_VARIANTS; li++) {
        const lf_variant_t *v = &LF_VARIANTS[li];
        const zt_model_t *m = &B[v->base].m;
        uint32_t n = v->seq_len[0], V = m->cfg.n_vocab;
        zt_fx *base = run(m, 0, v->seq, n, 8);
        uint8_t *z = malloc(v->file_len);
        memcpy(z, F[li], v->file_len);
        uint32_t zeroed = 0;
        for (uint32_t l = 0; l < m->cfg.n_layer; l++)
            for (uint32_t k = 0; k < ZT_LORA_MATS; k++) {
                static const char *const mn[7] = {"attn_q",   "attn_k", "attn_v",  "attn_output",
                                                  "ffn_gate", "ffn_up", "ffn_down"};
                char name[80];
                uint64_t nb;
                snprintf(name, sizeof name, "blk.%u.%s.weight.lora_b", l, mn[k]);
                uint8_t *p = tensor_data(z, v->file_len, name, &nb);
                if (p) {
                    memset(p, 0, nb);
                    zeroed++;
                }
            }
        adapter_t Z;
        zt_model_err_t e;
        int32_t r = load_lora(&Z, m, z, v->file_len, v->scale_q16, &e);
        zt_fx *zl = run(m, r ? 0 : &Z.lo, v->seq, n, 8);
        CHECK(r == 0 && zeroed == v->n_mats * m->cfg.n_layer && same(base, zl, (uint64_t) n * V),
              "%s: an adapter with B = 0 gives the base model's logits bit for bit", v->name);
        free(zl);
        free(Z.arena);
        free(z);
        state_t S;
        mkstate(&S, m, 8, &A[li].lo);
        zt_model_state_set_lora(&S.s, m, 0);
        zt_fx *nl = malloc((size_t) n * V * 4);
        zt_model_eval(m, &S.s, v->seq, n, nl, ZT_EVAL_ALL_LOGITS);
        CHECK(same(base, nl, (uint64_t) n * V),
              "%s: clearing a sequence's adapter gives the base model's logits bit for bit",
              v->name);
        free(nl);
        free(S.mem);
        free(base);
    }

    /* batching and kernel sets: the same bits */
    const zt_kern_t *ks[8];
    uint32_t nk = zt_simd_list(ks, 8);
    zt_pool_t *pool = zt_pool_new(4);
    uint64_t digest = 0xcbf29ce484222325ull;
    for (uint32_t li = 0; li < LF_N_VARIANTS; li++) {
        const lf_variant_t *v = &LF_VARIANTS[li];
        zt_model_t m = B[v->base].m;
        uint32_t n = v->seq_len[0], V = m.cfg.n_vocab;
        uint64_t nv = (uint64_t) n * V;
        zt_fx *ref = run(&m, &A[li].lo, v->seq, n, 0);
        for (uint64_t i = 0; i < nv * 4; i++)
            digest = (digest ^ ((uint8_t *) ref)[i]) * 0x100000001b3ull;
        uint32_t ok = 0, tried = 0;
        uint32_t batches[4] = {1, 3, 4, 8};
        for (uint32_t bi = 0; bi < 4; bi++) {
            zt_fx *x = run(&m, &A[li].lo, v->seq, n, batches[bi]);
            ok += same(ref, x, nv);
            tried++;
            free(x);
        }
        for (uint32_t ki = 0; ki < nk; ki++) {
            zt_kern_t th;
            zt_kern_threaded(&th, ks[ki], pool);
            const zt_kern_t *use[2] = {ks[ki], &th};
            for (uint32_t u = 0; u < 2; u++) {
                m.kern = use[u];
                for (uint32_t bi = 0; bi < 4; bi++) {
                    zt_fx *x = run(&m, &A[li].lo, v->seq, n, batches[bi]);
                    ok += same(ref, x, nv);
                    tried++;
                    free(x);
                }
            }
            m.kern = 0;
        }
        CHECK(ok == tried,
              "%s + LoRA: one-at-a-time decode == batches 1/3/4/8 on %u kernel sets, single and "
              "4 threads, bit for bit (%u/%u)",
              v->name, nk, ok, tried);
        free(ref);
    }
    printf("       kernel sets:");
    for (uint32_t ki = 0; ki < nk; ki++) printf(" %s", ks[ki]->name);
    printf("\n");
    /* the same on every machine: the Makefile compares this line between
     * the x86_64 run and the aarch64 run under qemu */
    printf("lora logits digest %016llx\n", (unsigned long long) digest);

    /* several agents on one base: each sequence keeps its own adapter */
    {
        const lf_variant_t *v = &LF_VARIANTS[0];
        const zt_model_t *m = &B[v->base].m;
        uint32_t V = m->cfg.n_vocab, n = v->seq_len[0];
        adapter_t half;
        zt_model_err_t e;
        int32_t r = load_lora(&half, m, F[0], v->file_len, v->scale_q16 / 2, &e);
        const zt_lora_t *los[3] = {&A[0].lo, 0, r ? 0 : &half.lo};
        zt_fx *solo[3];
        for (int i = 0; i < 3; i++) solo[i] = run(m, los[i], v->seq, n, 0);
        state_t S[3];
        for (int i = 0; i < 3; i++) mkstate(&S[i], m, 1, los[i]);
        zt_fx *mixd[3];
        for (int i = 0; i < 3; i++) mixd[i] = malloc((size_t) n * V * 4);
        for (uint32_t p = 0; p < n; p++)
            for (int i = 0; i < 3; i++)
                zt_model_decode(m, &S[i].s, v->seq[p], mixd[i] + (size_t) p * V);
        bool ok = r == 0;
        for (int i = 0; i < 3; i++) ok = ok && same(solo[i], mixd[i], (uint64_t) n * V);
        CHECK(ok && !same(solo[0], solo[1], (uint64_t) n * V) &&
                  !same(solo[0], solo[2], (uint64_t) n * V),
              "three agents on one qwen2 base (adapter x1, none, adapter x0.5), decoding in "
              "turns, each get their solo logits bit for bit");
        for (int i = 0; i < 3; i++) {
            free(solo[i]);
            free(mixd[i]);
            free(S[i].mem);
        }
        free(half.arena);
    }

    /* refusals */
    {
        const lf_variant_t *v = &LF_VARIANTS[0];
        const zt_model_t *m = &B[v->base].m;
        zt_model_err_t e;
        uint8_t *f = malloc(v->file_len);
        int32_t r = try_lora(m, F[0], v->file_len, 0, &e);
        CHECK(r == ZT_MODEL_EARG, "adapter scale 0 refused (%d)", (int) r);
        r = try_lora(&B[2].m, F[0], v->file_len, 65536, &e);
        CHECK(r == ZT_MODEL_EARCH, "a qwen2 adapter on a llama base refused (%d %s)", (int) r,
              e.what);
        r = try_lora(&B[1].m, F[0], v->file_len, 65536, &e);
        CHECK(r == ZT_MODEL_EARCH, "a qwen2 adapter on a qwen3 base refused (%d)", (int) r);

        memcpy(f, F[0], v->file_len);
        memcpy(find(f, v->hdr_len, "blk.0.attn_q.weight.lora_a"), "blk.0.attn_q.weight.lora_x", 26);
        r = try_lora(m, f, v->file_len, 65536, &e);
        CHECK(r == ZT_GGUF_EUNSUPPORTED && !strcmp(e.name, "blk.0.attn_q.weight.lora_x"),
              "an unknown adapter tensor is refused by name, not ignored (%d %s)", (int) r, e.name);

        memcpy(f, F[0], v->file_len);
        memcpy(find(f, v->hdr_len, "blk.1.ffn_up.weight.lora_b"), "blk.1.ffn_up.weight.lora_a", 26);
        r = try_lora(m, f, v->file_len, 65536, &e);
        CHECK(r != 0, "a duplicated lora_a / missing lora_b is refused (%d %s %s)", (int) r, e.what,
              e.name);

        memcpy(f, F[0], v->file_len);
        memcpy(find(f, v->hdr_len, "blk.0.attn_q.weight.lora_a"), "token_embd.weight.lora_a..", 26);
        r = try_lora(m, f, v->file_len, 65536, &e);
        CHECK(r == ZT_GGUF_EUNSUPPORTED, "an adapter on token_embd is refused (%d)", (int) r);

        memcpy(f, F[0], v->file_len);
        uint8_t *al = find(f, v->hdr_len, "adapter.lora.alpha");
        CHECK(al != 0, "fixture has adapter.lora.alpha");
        if (al) {
            al[18 + 4 + 3] |= 0x80; /* key, u32 type, then the f32's sign byte */
            r = try_lora(m, f, v->file_len, 65536, &e);
            CHECK(r == ZT_MODEL_ECONFIG, "a negative alpha is refused (%d)", (int) r);
        }

        memcpy(f, F[0], v->file_len);
        memcpy(find(f, v->hdr_len, "lora"), "dora", 4);
        r = try_lora(m, f, v->file_len, 65536, &e);
        CHECK(r == ZT_MODEL_EARCH, "adapter.type other than lora is refused (%d)", (int) r);

        memcpy(f, F[0], v->file_len);
        memcpy(find(f, v->hdr_len, "adapter"), "adaptor", 7);
        r = try_lora(m, f, v->file_len, 65536, &e);
        CHECK(r == ZT_MODEL_ECONFIG, "general.type other than adapter is refused (%d)", (int) r);

        adapter_t X;
        zt_gguf_open(&X.g, F[0], v->file_len);
        uint64_t need = zt_lora_arena_bytes(m, &X.g);
        X.arena = malloc(need);
        r = zt_lora_load(&X.lo, m, &X.g, 65536, X.arena, need - 65, &e);
        CHECK(r == ZT_MODEL_ESPACE, "a short arena is refused (%d)", (int) r);
        r = zt_lora_load(&X.lo, m, &X.g, 65536, X.arena, need, &e);
        state_t S;
        mkstate(&S, &B[1].m, 1, 0);
        CHECK(r == 0 && zt_model_state_set_lora(&S.s, &B[1].m, &X.lo) == ZT_MODEL_EARG &&
                  S.s.lora == 0 && zt_model_state_set_lora(&S.s, m, &A[1].lo) == ZT_MODEL_EARG,
              "a sequence refuses an adapter loaded for another base");
        free(S.mem);
        free(X.arena);
        free(f);
    }

    zt_pool_free(pool);
    for (uint32_t li = 0; li < LF_N_VARIANTS; li++) {
        free(A[li].arena);
        free(F[li]);
    }
    for (uint32_t vi = 0; vi < MF_N_VARIANTS; vi++) {
        free(B[vi].arena);
        free(B[vi].file);
    }
    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
