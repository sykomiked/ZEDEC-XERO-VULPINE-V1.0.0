/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* test_zt_model.c — the integer forward pass (T21) against a float64 numpy
 * reference of llama.cpp's semantics (test_model_fixture.h, from
 * gen_model_fixture.py), on three tiny random models written by gguf-py:
 * qwen2 (GQA, QKV biases), qwen3 (Q/K norm, tied output) and llama
 * (interleaved RoPE, rope_freqs, F16 matrices converted into the arena).
 * Also: batch prefill == one-at-a-time decode, determinism, arena sizing,
 * rejection of bad files, sampling statistics, the generate loop with the
 * tokenizer fixture, a header fuzzer, and a speed figure.
 *
 *   gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc/tensor src/tensor/test_zt_model.c \
 *       src/tensor/zt_model.c src/tensor/zt_tok.c src/tensor/zt_rope.c src/tensor/zt_gguf.c \
 *       src/tensor/zt.c -o /tmp/test_zt_model && /tmp/test_zt_model
 * Add -fsanitize=address,undefined and pass "fuzz" for the long fuzz run.
 */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "zt_model.h"
#include "test_model_fixture.h"
#include "test_tok_fixture.h"

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

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* ---- rebuilding a fixture file ---- */

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

static uint8_t *rebuild(const mf_variant_t *v)
{
    uint8_t *b = calloc(1, v->file_len);
    memcpy(b, v->hdr, v->hdr_len);
    for (uint32_t t = 0; t < v->n_gen; t++) {
        const mf_gen_t *g = &v->gen[t];
        uint8_t *p = b + v->hdr_len + g->off;
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

typedef struct {
    zt_gguf_t g;
    zt_model_t m;
    uint8_t *arena;
    uint64_t arena_bytes;
} loaded_t;

static int32_t load(loaded_t *L, const uint8_t *buf, uint64_t n, zt_model_err_t *err)
{
    int32_t r = zt_gguf_open(&L->g, buf, n);
    if (r) {
        err->code = r;
        err->what = "gguf";
        err->name[0] = 0;
        return r;
    }
    L->arena_bytes = zt_model_arena_bytes(&L->g);
    if (!L->arena_bytes) return zt_model_load(&L->m, &L->g, 0, 0, err); /* says why */
    L->arena = malloc(L->arena_bytes);
    r = zt_model_load(&L->m, &L->g, L->arena, L->arena_bytes, err);
    if (r) {
        free(L->arena);
        L->arena = 0;
    }
    return r;
}

typedef struct {
    zt_model_state_t s;
    uint8_t *mem;
} state_t;

static int32_t mkstate(state_t *S, const zt_model_t *m, uint32_t ctx, uint32_t batch, uint32_t kv)
{
    uint64_t need = zt_model_state_bytes(m, ctx, batch, kv);
    S->mem = malloc(need);
    return zt_model_state_init(&S->s, m, ctx, batch, kv, S->mem, need);
}

static bool same_mat(const zt_mat_t *a, const zt_mat_t *b)
{
    if (a->raw || b->raw) return a->raw == b->raw;
    for (uint64_t i = 0; i < (uint64_t) a->rows * a->cols / 32; i++)
        if (a->q8[i].scale != b->q8[i].scale || a->q8[i].shift != b->q8[i].shift ||
            memcmp(a->q8[i].q, b->q8[i].q, 32))
            return false;
    return true;
}

/* ---- accuracy against the reference ---- */

typedef struct {
    double maxerr, sumsq;
    uint64_t n;
    uint32_t top1, top1_n, top1_clear, top1_clear_n;
} acc_t;

static void accuracy(const mf_variant_t *v, loaded_t *L, uint32_t kv, uint32_t batch, acc_t *a)
{
    memset(a, 0, sizeof *a);
    uint32_t V = v->n_vocab, off = 0, poff = 0;
    for (uint32_t s = 0; s < v->n_seq; s++) {
        uint32_t n = v->seq_len[s];
        zt_fx *lg = malloc((size_t) n * V * 4);
        state_t S;
        mkstate(&S, &L->m, 64, batch, kv);
        int32_t r = zt_model_eval(&L->m, &S.s, v->seq + off, n, lg, ZT_EVAL_ALL_LOGITS);
        if (r) printf("       eval error %d\n", r);
        for (uint32_t p = 0; p < n; p++) {
            const zt_fx *row = lg + (size_t) p * V;
            for (uint32_t i = 0; i < MF_HEAD; i++) {
                double e = (row[i] - v->head[(size_t) (poff + p) * MF_HEAD + i]) / 65536.0;
                if (e < 0) e = -e;
                if (e > a->maxerr) a->maxerr = e;
                a->sumsq += e * e;
                a->n++;
            }
            int32_t am = zt_argmax(row, V);
            a->top1_n++;
            a->top1 += am == v->argmax[poff + p];
            if (v->gap[poff + p] > 0.05 * 65536) {
                a->top1_clear_n++;
                a->top1_clear += am == v->argmax[poff + p];
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
        free(S.mem);
        off += n;
        poff += n;
    }
}

/* ---- header surgery for the bad-file tests ---- */

static uint8_t *find(uint8_t *hay, uint64_t n, const char *needle, uint64_t len)
{
    for (uint64_t i = 0; i + len <= n; i++)
        if (!memcmp(hay + i, needle, len)) return hay + i;
    return 0;
}

/* pointer to the type field of a tensor info in the header */
static uint8_t *tinfo_type(uint8_t *b, uint64_t hdr, const char *name)
{
    uint64_t l = strlen(name);
    uint8_t *p = 0;
    for (uint64_t i = 8; i + l <= hdr; i++) {
        uint64_t sl;
        memcpy(&sl, b + i - 8, 8);
        if (sl == l && !memcmp(b + i, name, l)) {
            p = b + i + l;
            break;
        }
    }
    if (!p) return 0;
    uint32_t nd;
    memcpy(&nd, p, 4);
    return p + 4 + 8 * nd;
}

static uint8_t *tinfo_dims(uint8_t *b, uint64_t hdr, const char *name)
{
    uint8_t *t = tinfo_type(b, hdr, name);
    if (!t) return 0;
    uint32_t nd;
    for (nd = 1; nd <= 4; nd++) {
        uint32_t x;
        memcpy(&x, t - 4 - 8 * nd, 4);
        if (x == nd) return t - 8 * nd;
    }
    return 0;
}

/* a metadata u32 value by key */
static uint8_t *kv_u32(uint8_t *b, uint64_t hdr, const char *key)
{
    uint8_t *p = find(b, hdr, key, strlen(key));
    return p ? p + strlen(key) + 4 : 0;
}

static int32_t try_load(const uint8_t *b, uint64_t n, zt_model_err_t *err)
{
    loaded_t L;
    memset(err, 0, sizeof *err);
    int32_t r = load(&L, b, n, err);
    if (!r) free(L.arena);
    return r;
}

/* ---- a small GGUF writer for the synthetic real-size layer ---- */

typedef struct {
    uint8_t *b;
    size_t n, cap;
} wbuf_t;

static void wput(wbuf_t *w, const void *p, size_t n)
{
    if (w->n + n > w->cap) {
        w->cap = (w->n + n) * 2;
        w->b = realloc(w->b, w->cap);
    }
    memcpy(w->b + w->n, p, n);
    w->n += n;
}
static void w32(wbuf_t *w, uint32_t v)
{
    wput(w, &v, 4);
}
static void w64(wbuf_t *w, uint64_t v)
{
    wput(w, &v, 8);
}
static void wstr(wbuf_t *w, const char *s)
{
    w64(w, strlen(s));
    wput(w, s, strlen(s));
}
static void kvu32(wbuf_t *w, const char *k, uint32_t v)
{
    wstr(w, k);
    w32(w, ZT_GGUF_U32);
    w32(w, v);
}
static void kvf32(wbuf_t *w, const char *k, float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    wstr(w, k);
    w32(w, ZT_GGUF_F32);
    w32(w, v);
}

typedef struct {
    char name[48];
    uint64_t d0, d1;
    uint32_t type;
} wt_t;

static uint8_t *synth_qwen2(uint32_t ne, uint32_t nh, uint32_t nkv, uint32_t nf, uint32_t nl,
                            uint32_t V, uint64_t *size)
{
    wt_t t[16 * 64];
    uint32_t nt = 0, hd = ne / nh;
#define ADD(nm, a, b, ty)                                                                          \
    do {                                                                                           \
        snprintf(t[nt].name, sizeof t[nt].name, "%s", nm);                                         \
        t[nt].d0 = a;                                                                              \
        t[nt].d1 = b;                                                                              \
        t[nt].type = ty;                                                                           \
        nt++;                                                                                      \
    } while (0)
    ADD("token_embd.weight", ne, V, ZT_GGML_Q8_0);
    for (uint32_t l = 0; l < nl; l++) {
        char nm[48];
#define ADDL(sfx, a, b, ty)                                                                        \
    do {                                                                                           \
        snprintf(nm, sizeof nm, "blk.%u.%s", l, sfx);                                              \
        ADD(nm, a, b, ty);                                                                         \
    } while (0)
        ADDL("attn_norm.weight", ne, 0, ZT_GGML_F32);
        ADDL("attn_q.weight", ne, nh * hd, ZT_GGML_Q8_0);
        ADDL("attn_k.weight", ne, nkv * hd, ZT_GGML_Q8_0);
        ADDL("attn_v.weight", ne, nkv * hd, ZT_GGML_Q8_0);
        ADDL("attn_q.bias", nh * hd, 0, ZT_GGML_F32);
        ADDL("attn_k.bias", ((uint64_t)nkv) * hd, 0, ZT_GGML_F32);
        ADDL("attn_v.bias", nkv * hd, 0, ZT_GGML_F32);
        ADDL("attn_output.weight", ((uint64_t)nh) * hd, ne, ZT_GGML_Q8_0);
        ADDL("ffn_norm.weight", ne, 0, ZT_GGML_F32);
        ADDL("ffn_gate.weight", ne, nf, ZT_GGML_Q8_0);
        ADDL("ffn_up.weight", ne, nf, ZT_GGML_Q8_0);
        ADDL("ffn_down.weight", nf, ne, ZT_GGML_Q8_0);
    }
    ADD("output_norm.weight", ne, 0, ZT_GGML_F32);
    wbuf_t w = {0, 0, 0};
    w32(&w, 0x46554747u);
    w32(&w, 3);
    w64(&w, nt);
    w64(&w, 10);
    wstr(&w, "general.architecture");
    w32(&w, ZT_GGUF_STRING);
    wstr(&w, "qwen2");
    kvu32(&w, "qwen2.block_count", nl);
    kvu32(&w, "qwen2.embedding_length", ne);
    kvu32(&w, "qwen2.feed_forward_length", nf);
    kvu32(&w, "qwen2.attention.head_count", nh);
    kvu32(&w, "qwen2.attention.head_count_kv", nkv);
    kvu32(&w, "qwen2.context_length", 32768);
    kvf32(&w, "qwen2.attention.layer_norm_rms_epsilon", 1e-6f);
    kvf32(&w, "qwen2.rope.freq_base", 1e6f);
    kvu32(&w, "general.alignment", 32);
    uint64_t off = 0, offs[16 * 64];
    for (uint32_t i = 0; i < nt; i++) {
        wstr(&w, t[i].name);
        w32(&w, t[i].d1 ? 2 : 1);
        w64(&w, t[i].d0);
        if (t[i].d1) w64(&w, t[i].d1);
        w32(&w, t[i].type);
        w64(&w, off);
        offs[i] = off;
        off += (zt_ggml_bytes(t[i].type, t[i].d0 * (t[i].d1 ? t[i].d1 : 1)) + 31) & ~31ull;
    }
    while (w.n % 32) wput(&w, "", 1);
    size_t data = w.n;
    *size = data + off;
    uint8_t *b = calloc(1, *size);
    memcpy(b, w.b, data);
    free(w.b);
    uint64_t z = 12345;
    for (uint32_t i = 0; i < nt; i++) {
        uint8_t *p = b + data + offs[i];
        uint64_t n = t[i].d0 * (t[i].d1 ? t[i].d1 : 1);
        if (t[i].type == ZT_GGML_F32) {
            for (uint64_t k = 0; k < n; k++)
                put_f32(p + 4 * k, strstr(t[i].name, "bias") ? 0.01f : 1.0f);
        } else {
            for (uint64_t k = 0; k < n / 32; k++, p += 34) {
                z = mix(z, k);
                p[0] = (uint8_t) (z & 0xFF);
                p[1] = (uint8_t) (0x14 | ((z >> 8) & 3)); /* d ~ 2^-10 */
                for (int j = 0; j < 32; j++) p[2 + j] = (uint8_t) (mix(z, j) % 255 - 127);
            }
        }
    }
    return b;
}

/* ---- generate callbacks ---- */

typedef struct {
    int32_t ids[64];
    uint32_t n, stop_after;
    uint8_t text[4096];
    uint32_t len;
} cb_t;

static bool on_tok(void *ctx, int32_t id, const uint8_t *piece, uint32_t len)
{
    cb_t *c = ctx;
    if (c->n < 64) c->ids[c->n] = id;
    c->n++;
    if (c->len + len <= sizeof c->text) memcpy(c->text + c->len, piece, len);
    c->len += len;
    return !c->stop_after || c->n < c->stop_after;
}

/* ---- fuzz ---- */

static uint64_t frs = 0x2026100912345ull;
static uint64_t frand(void)
{
    frs ^= frs << 13;
    frs ^= frs >> 7;
    frs ^= frs << 17;
    return frs;
}

static void fuzz(const mf_variant_t *v, const uint8_t *orig, uint32_t iters, uint32_t *n_ok,
                 uint32_t *n_ran)
{
    uint8_t *b = malloc(v->file_len);
    for (uint32_t it = 0; it < iters; it++) {
        memcpy(b, orig, v->file_len);
        uint32_t flips = 1 + frand() % 4;
        for (uint32_t f = 0; f < flips; f++) {
            uint64_t pos = frand() % (it % 8 == 7 ? v->file_len : v->hdr_len);
            uint32_t mode = frand() % 4;
            if (mode == 0)
                b[pos] ^= (uint8_t) (1u << (frand() % 8));
            else if (mode == 1)
                b[pos] = (uint8_t) frand();
            else if (mode == 2)
                b[pos] = 0xFF;
            else
                b[pos] = 0;
        }
        uint64_t len = v->file_len;
        if (it % 16 == 3) len = frand() % v->file_len;
        zt_gguf_t g;
        if (zt_gguf_open(&g, b, len)) continue;
        zt_model_cfg_t c;
        zt_model_err_t err;
        if (zt_model_config(&g, &c, &err)) continue;
        uint64_t ab = zt_model_arena_bytes(&g);
        if (!ab || ab > (256u << 20)) continue;
        uint8_t *arena = malloc(ab);
        zt_model_t m;
        if (!zt_model_load(&m, &g, arena, ab, &err)) {
            (*n_ok)++;
            uint64_t sb = zt_model_state_bytes(&m, 4, 2, it & 1);
            if (sb && sb < (256u << 20)) {
                uint8_t *sm = malloc(sb);
                zt_model_state_t s;
                zt_fx *lg = malloc((size_t) m.cfg.n_vocab * 4);
                int32_t toks[3] = {0, (int32_t) (m.cfg.n_vocab - 1), 1 % (int32_t) m.cfg.n_vocab};
                if (!zt_model_state_init(&s, &m, 4, 2, it & 1, sm, sb) &&
                    !zt_model_eval(&m, &s, toks, 3, lg, 0))
                    (*n_ran)++;
                free(lg);
                free(sm);
            }
        }
        free(arena);
    }
    free(b);
}

/* ---- main ---- */

int main(int argc, char **argv)
{
    bool long_fuzz = argc > 1 && !strcmp(argv[1], "fuzz");
    printf("=== T21 integer forward pass ===\n");
    uint8_t *files[MF_N_VARIANTS];
    loaded_t L[MF_N_VARIANTS];
    for (uint32_t vi = 0; vi < MF_N_VARIANTS; vi++) {
        const mf_variant_t *v = &MF_VARIANTS[vi];
        files[vi] = rebuild(v);
        CHECK(fnv(files[vi], v->file_len) == v->fnv,
              "%s: rebuilt file is byte-identical to gguf-py's (%llu bytes, FNV-1a)", v->name,
              (unsigned long long) v->file_len);
        zt_model_err_t err;
        int32_t r = load(&L[vi], files[vi], v->file_len, &err);
        CHECK(r == 0, "%s: loads (%d %s %s)", v->name, r, r ? err.what : "", r ? err.name : "");
        if (r) return 1;
    }
    {
        const zt_model_cfg_t *c = &L[0].m.cfg;
        CHECK(c->arch == ZT_ARCH_QWEN2 && c->n_layer == 2 && c->n_embd == 64 && c->n_ff == 128 &&
                  c->n_head == 4 && c->n_head_kv == 2 && c->head_dim == 16 && c->n_vocab == 1200 &&
                  c->rope_neox && c->has_qkv_bias && !c->has_qk_norm && !c->tied_output &&
                  c->rms_eps_f32 == 0x358637BDu && c->rope_base_f32 == 0x49742400u,
              "qwen2 config read: 2 layers, 64/128, 4 heads, 2 KV heads, biases, NEOX");
        c = &L[1].m.cfg;
        CHECK(c->arch == ZT_ARCH_QWEN3 && c->head_dim == 32 && L[1].m.q_dim == 128 &&
                  c->has_qk_norm && c->tied_output && !c->has_qkv_bias && L[1].m.out.raw,
              "qwen3 config read: key_length 32 != 64/4, Q/K norm, output tied to token_embd "
              "(Q8_0, in place)");
        c = &L[2].m.cfg;
        CHECK(c->arch == ZT_ARCH_LLAMA && !c->rope_neox && c->has_rope_freqs && c->n_head_kv == 4 &&
                  L[2].m.layers[0].wg.q8 && L[2].m.layers[0].wq.raw && L[2].m.out.q8,
              "llama config read: interleaved RoPE, rope_freqs, F16 matrices converted, Q8_0 "
              "in place");
    }

    /* accuracy */
    /* Q16 KV: Q8_0 weights are used exactly, so only activation rounding is
     * left; llama's F16 matrices take a second 8-bit rounding (a numpy
     * simulation of that rounding alone moves the logits by 0.13). Q8 KV: the
     * same simulation of int8 K and V gives 0.08-0.15 on these models. */
    double tol[MF_N_VARIANTS] = {0.005, 0.005, 0.2}, tol8[MF_N_VARIANTS] = {0.2, 0.25, 0.3};
    for (uint32_t vi = 0; vi < MF_N_VARIANTS; vi++) {
        const mf_variant_t *v = &MF_VARIANTS[vi];
        for (uint32_t kv = 0; kv < 2; kv++) {
            acc_t a;
            accuracy(v, &L[vi], kv, 8, &a);
            double t = kv ? tol8[vi] : tol[vi];
            CHECK(a.maxerr < t,
                  "%s, KV %s: logits within %.3f of float64 (max abs err %.5f, rms %.5f, %llu "
                  "values)",
                  v->name, kv ? "Q8" : "Q16", t, a.maxerr, a.n ? __builtin_sqrt(a.sumsq / a.n) : 0,
                  (unsigned long long) a.n);
            CHECK(a.top1_clear == a.top1_clear_n,
                  "%s, KV %s: top-1 agrees at every position with a top-2 gap > 0.05 (%u/%u; all "
                  "positions %u/%u)",
                  v->name, kv ? "Q8" : "Q16", a.top1_clear, a.top1_clear_n, a.top1, a.top1_n);
        }
    }

    /* greedy chains, incremental decode vs batch, determinism */
    for (uint32_t vi = 0; vi < MF_N_VARIANTS; vi++) {
        const mf_variant_t *v = &MF_VARIANTS[vi];
        zt_model_t *m = &L[vi].m;
        uint32_t V = v->n_vocab, n0 = v->seq_len[0];
        zt_fx *lg = malloc((size_t) V * 4);
        state_t S;
        mkstate(&S, m, 64, 4, ZT_KV_Q16);
        zt_model_eval(m, &S.s, v->seq, n0, lg, 0);
        uint32_t same = 0;
        int32_t chain[64];
        for (uint32_t i = 0; i < v->n_greedy; i++) {
            chain[i] = zt_argmax(lg, V);
            same += chain[i] == v->greedy[i];
            zt_model_decode(m, &S.s, chain[i], lg);
        }
        CHECK(same == v->n_greedy,
              "%s: greedy generation matches the reference argmax chain (%u/%u)", v->name, same,
              v->n_greedy);
        /* full recompute of prompt + chain, every way of batching it */
        uint32_t n = n0 + v->n_greedy;
        int32_t *all = malloc(n * 4);
        memcpy(all, v->seq, n0 * 4);
        memcpy(all + n0, chain, v->n_greedy * 4);
        for (uint32_t kv = 0; kv < 2; kv++) {
            zt_fx *ref = malloc((size_t) n * V * 4), *got = malloc((size_t) n * V * 4);
            state_t A;
            mkstate(&A, m, 64, 1, kv);
            for (uint32_t i = 0; i < n; i++) zt_model_decode(m, &A.s, all[i], ref + (size_t) i * V);
            bool ok = true;
            uint32_t batches[4] = {2, 5, 8, 64};
            for (int bi = 0; bi < 4; bi++) {
                state_t B;
                mkstate(&B, m, 64, batches[bi], kv);
                zt_model_eval(m, &B.s, all, n, got, ZT_EVAL_ALL_LOGITS);
                ok = ok && !memcmp(ref, got, (size_t) n * V * 4);
                /* split: prefill a prefix, then decode the rest */
                zt_model_state_seek(&B.s, 0);
                zt_model_eval(m, &B.s, all, 7, 0, 0);
                for (uint32_t i = 7; i < n; i++)
                    zt_model_decode(m, &B.s, all[i], got + (size_t) i * V);
                ok = ok && !memcmp(ref + 7 * V, got + 7 * V, (size_t) (n - 7) * V * 4);
                free(B.mem);
            }
            CHECK(ok,
                  "%s, KV %s: incremental decode == full recompute bit for bit (batches 1, 2, 5, "
                  "8, 64; prefill + decode; %u tokens)",
                  v->name, kv ? "Q8" : "Q16", n);
            /* determinism: a fresh load and state gives the same bits */
            loaded_t L2;
            zt_model_err_t err;
            load(&L2, files[vi], v->file_len, &err);
            state_t C;
            mkstate(&C, &L2.m, 64, 3, kv);
            zt_model_eval(&L2.m, &C.s, all, n, got, ZT_EVAL_ALL_LOGITS);
            const zt_mat_t *w0 = &L[vi].m.layers[1].wd, *w1 = &L2.m.layers[1].wd;
            CHECK(!memcmp(ref, got, (size_t) n * V * 4) && same_mat(w0, w1),
                  "%s, KV %s: deterministic (fresh load: same arena, same logits)", v->name,
                  kv ? "Q8" : "Q16");
            free(L2.arena);
            free(C.mem);
            free(A.mem);
            free(ref);
            free(got);
        }
        free(all);
        free(lg);
        free(S.mem);
    }

    /* arena and state sizing */
    for (uint32_t vi = 0; vi < MF_N_VARIANTS; vi++) {
        const mf_variant_t *v = &MF_VARIANTS[vi];
        uint64_t ab = L[vi].arena_bytes;
        uint8_t *ar = malloc(ab + 256);
        memset(ar, 0xA5, ab + 256);
        zt_model_t m;
        zt_model_err_t err;
        int32_t r1 = zt_model_load(&m, &L[vi].g, ar, ab, &err);
        bool canary = true;
        for (uint32_t i = 0; i < 256; i++) canary = canary && ar[ab + i] == 0xA5;
        int32_t r2 = zt_model_load(&m, &L[vi].g, ar, 100, &err);
        int32_t r3 = zt_model_load(&m, &L[vi].g, ar + 1, ab - 64, &err);
        CHECK(r1 == 0 && canary && r2 == ZT_MODEL_ESPACE && r3 == ZT_MODEL_ESPACE,
              "%s: arena %llu bytes is enough and never overrun; a short arena is refused", v->name,
              (unsigned long long) ab);
        uint64_t sb = zt_model_state_bytes(&L[vi].m, 32, 4, ZT_KV_Q8);
        uint8_t *sm = malloc(sb + 256);
        memset(sm, 0x5A, sb + 256);
        zt_model_state_t s;
        int32_t s1 = zt_model_state_init(&s, &L[vi].m, 32, 4, ZT_KV_Q8, sm, sb);
        int32_t toks[32];
        for (int i = 0; i < 32; i++) toks[i] = (i * 37) % (int) v->n_vocab;
        zt_fx *lg = malloc((size_t) 32 * v->n_vocab * 4);
        int32_t e1 = zt_model_eval(&L[vi].m, &s, toks, 32, lg, ZT_EVAL_ALL_LOGITS);
        int32_t e2 = zt_model_decode(&L[vi].m, &s, 1, lg);
        canary = true;
        for (uint32_t i = 0; i < 256; i++) canary = canary && sm[sb + i] == 0x5A;
        int32_t s2 = zt_model_state_init(&s, &L[vi].m, 32, 4, ZT_KV_Q8, sm, sb - 65);
        CHECK(s1 == 0 && e1 == 0 && e2 == ZT_MODEL_ECTX && canary && s2 == ZT_MODEL_ESPACE,
              "%s: state %llu bytes (32 ctx, Q8 KV) filled to the last slot without overrun; "
              "full cache says ECTX",
              v->name, (unsigned long long) sb);
        free(lg);
        free(sm);
        free(ar);
    }
    {
        uint64_t q16 = zt_model_state_bytes(&L[0].m, 1024, 1, ZT_KV_Q16);
        uint64_t q8 = zt_model_state_bytes(&L[0].m, 1024, 1, ZT_KV_Q8);
        printf("       KV cache at 1024 tokens (qwen2 tiny): Q16 %llu bytes, Q8 %llu bytes\n",
               (unsigned long long) q16, (unsigned long long) q8);
        CHECK(q8 * 5 < q16 * 2,
              "Q8 KV cache is under 0.4 of Q16's (head_dim 16: 16 bytes + 5 of scale per head)");
        CHECK(L[2].arena_bytes > L[0].arena_bytes && L[0].arena_bytes < 20000,
              "Q8_0 weights need no arena copy (qwen2 arena %llu bytes; llama with F16 "
              "converted %llu)",
              (unsigned long long) L[0].arena_bytes, (unsigned long long) L[2].arena_bytes);
    }

    /* bad and unsupported files */
    {
        const mf_variant_t *v = &MF_VARIANTS[0];
        uint8_t *b = malloc(v->file_len);
        zt_model_err_t err;
        int32_t r;
        memcpy(b, files[0], v->file_len);
        memcpy(find(b, v->hdr_len, "qwen2", 5), "gemma", 5);
        r = try_load(b, v->file_len, &err);
        CHECK(r == ZT_MODEL_EARCH && !strcmp(err.name, "gemma"),
              "unsupported architecture refused: %d \"%s: %s\"", r, err.what, err.name);
        memcpy(b, files[0], v->file_len);
        memcpy(tinfo_type(b, v->hdr_len, "blk.1.ffn_up.weight"), "\x06\0\0\0", 4); /* Q5_0 */
        r = try_load(b, v->file_len, &err);
        CHECK(r == ZT_GGUF_EUNSUPPORTED && !strcmp(err.name, "blk.1.ffn_up.weight"),
              "unsupported tensor type refused: %d \"%s: %s\"", r, err.what, err.name);
        memcpy(b, files[0], v->file_len);
        memcpy(find(b, v->hdr_len, "blk.1.ffn_norm", 14), "blk.1.ffn_nurm", 14);
        r = try_load(b, v->file_len, &err);
        CHECK(r == ZT_MODEL_EMISSING && !strcmp(err.name, "blk.1.ffn_norm.weight"),
              "missing tensor refused: %d \"%s: %s\"", r, err.what, err.name);
        memcpy(b, files[0], v->file_len);
        {
            uint8_t *d = tinfo_dims(b, v->hdr_len, "blk.0.attn_k.weight");
            uint64_t x = 64;
            memcpy(d + 8, &x, 8); /* 32 rows -> 64 rows */
        }
        r = try_load(b, v->hdr_len, &err);
        int32_t r2 = try_load(b, v->file_len, &err);
        CHECK(r != 0 && r2 == ZT_MODEL_ESHAPE && !strcmp(err.name, "blk.0.attn_k.weight"),
              "wrong tensor shape refused: %d \"%s: %s\"", r2, err.what, err.name);
        memcpy(b, files[0], v->file_len);
        memcpy(kv_u32(b, v->hdr_len, "qwen2.attention.head_count_kv"), "\x03\0\0\0", 4);
        r = try_load(b, v->file_len, &err);
        CHECK(r == ZT_MODEL_ECONFIG, "4 heads over 3 KV heads refused: %d \"%s: %s\"", r, err.what,
              err.name);
        memcpy(b, files[0], v->file_len);
        memcpy(find(b, v->hdr_len, "qwen2.block_count", 17), "qwen2.block_cuont", 17);
        r = try_load(b, v->file_len, &err);
        CHECK(r == ZT_MODEL_EMISSING, "missing block_count refused: %d \"%s: %s\"", r, err.what,
              err.name);
        memcpy(b, files[0], v->file_len);
        r = try_load(b, v->file_len / 2, &err);
        CHECK(r == ZT_GGUF_ETRUNC, "truncated file refused: %d", r);
        r = try_load((const uint8_t *) TOK_GGUF, TOK_GGUF_LEN, &err);
        CHECK(r == ZT_MODEL_EMISSING, "a tokenizer-only GGUF is not a model: %d \"%s: %s\"", r,
              err.what, err.name);
        state_t S;
        mkstate(&S, &L[0].m, 8, 2, 0);
        int32_t badtok[2] = {1, 1200};
        CHECK(zt_model_eval(&L[0].m, &S.s, badtok, 2, 0, 0) == ZT_MODEL_EARG && S.s.pos == 0,
              "token id >= n_vocab refused before anything runs");
        free(S.mem);
        free(b);
    }

    /* sampling */
    {
        uint32_t V = 1000;
        zt_fx *lg = malloc(V * 4);
        for (uint32_t i = 0; i < V; i++) lg[i] = (zt_fx) ((mix(77, i) % 600000) - 300000);
        uint8_t *work = malloc(zt_sample_work_bytes(V));
        uint32_t order[1000];
        for (uint32_t i = 0; i < V; i++) order[i] = i;
        for (uint32_t i = 0; i < V; i++) /* reference order: logit desc, id asc */
            for (uint32_t j = i + 1; j < V; j++)
                if (lg[order[j]] > lg[order[i]] ||
                    (lg[order[j]] == lg[order[i]] && order[j] < order[i])) {
                    uint32_t t = order[i];
                    order[i] = order[j];
                    order[j] = t;
                }
        zt_sampler_params_t p = {ZT_ONE, 5, 0, 0, 0, 1};
        zt_sampler_t sp;
        uint32_t outside = 0, hist[1000] = {0};
        for (uint32_t k = 0; k < 20000; k++) {
            p.seed = k;
            zt_sampler_init(&sp, &p);
            int32_t t = zt_sample(&sp, lg, V, 0, 0, work, zt_sample_work_bytes(V));
            bool in = false;
            for (int i = 0; i < 5; i++) in = in || (uint32_t) t == order[i];
            outside += !in;
            hist[t]++;
        }
        CHECK(outside == 0, "top-k 5: never outside the 5 best in 20000 draws");
        double z = 0, ps[5];
        for (int i = 0; i < 5; i++) {
            ps[i] = __builtin_exp((lg[order[i]] - lg[order[0]]) / 65536.0);
            z += ps[i];
        }
        double chi = 0;
        for (int i = 0; i < 5; i++) {
            double e = 20000 * ps[i] / z;
            chi += (hist[order[i]] - e) * (hist[order[i]] - e) / e;
        }
        CHECK(chi < 20.0, "top-k 5: frequencies follow softmax (chi^2 %.2f with 4 dof)", chi);
        /* top-p */
        p = (zt_sampler_params_t){ZT_ONE, 0, 58982 /* 0.9 */, 0, 0, 0};
        double cum = 0, tot = 0;
        for (uint32_t i = 0; i < V; i++) tot += __builtin_exp((lg[i] - lg[order[0]]) / 65536.0);
        uint32_t nucleus = 0;
        while (cum < 0.9 * tot)
            cum += __builtin_exp((lg[order[nucleus++]] - lg[order[0]]) / 65536.0);
        outside = 0;
        for (uint32_t k = 0; k < 5000; k++) {
            p.seed = k * 7 + 1;
            zt_sampler_init(&sp, &p);
            int32_t t = zt_sample(&sp, lg, V, 0, 0, work, zt_sample_work_bytes(V));
            bool in = false;
            for (uint32_t i = 0; i < nucleus + 1; i++) in = in || (uint32_t) t == order[i];
            outside += !in;
        }
        CHECK(outside == 0, "top-p 0.9: never outside the nucleus (%u tokens) in 5000 draws",
              nucleus);
        /* same seed, same stream */
        p = (zt_sampler_params_t){52429 /* 0.8 */, 40, 62259, 72090 /* 1.1 */, 64, 424242};
        int32_t s1[64], s2[64], s3[64];
        int32_t histv[3] = {(int32_t) order[0], (int32_t) order[1], 3};
        zt_sampler_t a, b2, c;
        zt_sampler_init(&a, &p);
        zt_sampler_init(&b2, &p);
        p.seed++;
        zt_sampler_init(&c, &p);
        uint32_t diff = 0;
        for (int i = 0; i < 64; i++) {
            s1[i] = zt_sample(&a, lg, V, histv, 3, work, zt_sample_work_bytes(V));
            s2[i] = zt_sample(&b2, lg, V, histv, 3, work, zt_sample_work_bytes(V));
            s3[i] = zt_sample(&c, lg, V, histv, 3, work, zt_sample_work_bytes(V));
            diff += s1[i] != s3[i];
        }
        CHECK(!memcmp(s1, s2, sizeof s1) && diff > 0,
              "same seed gives the same 64 tokens; another seed differs (%u/64)", diff);
        /* temperature 0 = greedy; repetition penalty moves the argmax */
        p = (zt_sampler_params_t){0, 0, 0, 0, 0, 0};
        zt_sampler_init(&sp, &p);
        int32_t g0 = zt_sample(&sp, lg, V, 0, 0, work, zt_sample_work_bytes(V));
        p.repeat_penalty = 2 * ZT_ONE;
        p.repeat_last_n = 8;
        zt_sampler_init(&sp, &p);
        int32_t hh[2] = {(int32_t) order[0], (int32_t) order[0]};
        int32_t g1 = zt_sample(&sp, lg, V, hh, 2, work, zt_sample_work_bytes(V));
        CHECK(g0 == (int32_t) order[0] && g0 == zt_argmax(lg, V) && g1 == (int32_t) order[1],
              "temperature 0 is the argmax; penalty 2.0 on it hands the pick to the runner-up");
        /* low temperature concentrates, high temperature spreads */
        uint32_t top_lo = 0, top_hi = 0;
        for (uint32_t k = 0; k < 2000; k++) {
            p = (zt_sampler_params_t){6554 /* 0.1 */, 0, 0, 0, 0, k};
            zt_sampler_init(&sp, &p);
            top_lo +=
                zt_sample(&sp, lg, V, 0, 0, work, zt_sample_work_bytes(V)) == (int32_t) order[0];
            p.temperature = 4 * ZT_ONE;
            zt_sampler_init(&sp, &p);
            top_hi +=
                zt_sample(&sp, lg, V, 0, 0, work, zt_sample_work_bytes(V)) == (int32_t) order[0];
        }
        CHECK(top_lo > top_hi,
              "temperature 0.1 picks the top token more often than 4.0 (%u vs %u /2000)", top_lo,
              top_hi);
        CHECK(zt_sample(&sp, lg, V, 0, 0, work, 8) == ZT_MODEL_ESPACE,
              "short sampler work refused");
        free(work);
        free(lg);
    }

    /* generate, with the tokenizer fixture */
    {
        const mf_variant_t *v = &MF_VARIANTS[0];
        zt_gguf_t tg;
        zt_tok_t tok;
        zt_gguf_open(&tg, (const uint8_t *) TOK_GGUF, TOK_GGUF_LEN);
        uint64_t tb = zt_tok_arena_bytes(&tg);
        uint8_t *tar = malloc(tb);
        CHECK(zt_tok_load(&tok, &tg, tar, tb) == 0 && tok.n_vocab == v->n_vocab,
              "tokenizer fixture loads; its vocabulary matches the qwen2 model's");
        const uint8_t *prompt = (const uint8_t *) TOK_TEXTS[12].s;
        uint32_t plen = TOK_TEXTS[12].len;
        uint64_t wb = zt_model_gen_work_bytes(&L[0].m, &tok, plen, 32);
        uint8_t *work = malloc(wb);
        state_t S;
        mkstate(&S, &L[0].m, 128, 8, ZT_KV_Q16);
        cb_t cb;
        memset(&cb, 0, sizeof cb);
        zt_gen_params_t gp = {0};
        gp.max_tokens = v->n_greedy;
        gp.parse_special = true;
        gp.on_token = on_tok;
        gp.ctx = &cb;
        zt_gen_result_t res;
        int32_t r = zt_model_generate(&L[0].m, &S.s, &tok, prompt, plen, &gp, work, wb, &res);
        bool same = r == 0 && cb.n == v->n_greedy && res.n_gen == v->n_greedy;
        for (uint32_t i = 0; same && i < v->n_greedy; i++) same = cb.ids[i] == v->greedy[i];
        CHECK(same && res.n_prompt == v->seq_len[0] && res.stop == ZT_GEN_MAX,
              "generate: \"<|im_start|>user\\nHi there<|im_end|>...\" tokenizes to the reference "
              "prompt (%u ids), and greedy output matches the reference chain (%u tokens)",
              res.n_prompt, cb.n);
        uint8_t dec[4096];
        uint64_t dn = 0;
        zt_tok_decode(&tok, cb.ids, cb.n, dec, sizeof dec, &dn);
        CHECK(dn == cb.len && !memcmp(dec, cb.text, dn),
              "generate: callback pieces concatenate to decode(ids) (%u bytes)", cb.len);
        CHECK(S.s.pos == res.n_prompt + res.n_gen - 1,
              "generate: the cache holds the prompt and every fed token (%u)", S.s.pos);
        /* stop string spanning the end of token 4's text */
        uint8_t piece[256];
        uint64_t pl = 0;
        zt_tok_decode(&tok, &v->greedy[3], 1, piece, sizeof piece, &pl);
        zt_stop_str_t ss = {piece + (pl > 2 ? pl - 2 : 0), (uint32_t) (pl > 2 ? 2 : pl)};
        uint32_t first = 0; /* the first token whose text ends with the stop string */
        {
            uint8_t acc[4096];
            uint64_t an = 0;
            for (uint32_t i = 0; i < v->n_greedy && !first; i++) {
                uint64_t n1 = 0;
                zt_tok_decode(&tok, &v->greedy[i], 1, acc + an, sizeof acc - an, &n1);
                an += n1;
                if (an >= ss.len && !memcmp(acc + an - ss.len, ss.s, ss.len)) first = i + 1;
            }
        }
        zt_model_state_seek(&S.s, 0);
        memset(&cb, 0, sizeof cb);
        gp.stop_strs = &ss;
        gp.n_stop_strs = 1;
        r = zt_model_generate(&L[0].m, &S.s, &tok, prompt, plen, &gp, work, wb, &res);
        CHECK(r == 0 && res.stop == ZT_GEN_STOP_STR && res.n_gen == first &&
                  res.stop_trim == ss.len,
              "generate: stops on a stop string after token %u, trim %u bytes", res.n_gen,
              res.stop_trim);
        gp.n_stop_strs = 0;
        zt_model_state_seek(&S.s, 0);
        memset(&cb, 0, sizeof cb);
        cb.stop_after = 3;
        r = zt_model_generate(&L[0].m, &S.s, &tok, prompt, plen, &gp, work, wb, &res);
        CHECK(r == 0 && res.stop == ZT_GEN_CALLBACK && res.n_gen == 3,
              "generate: the callback can stop it (3 tokens)");
        cb.stop_after = 0;
        zt_model_state_seek(&S.s, 0);
        int32_t stop_id = v->greedy[5];
        gp.stop_ids = &stop_id;
        gp.n_stop_ids = 1;
        uint32_t first_id = 0;
        while (v->greedy[first_id] != stop_id) first_id++;
        r = zt_model_generate(&L[0].m, &S.s, &tok, prompt, plen, &gp, work, wb, &res);
        CHECK(r == 0 && res.stop == ZT_GEN_EOS && res.n_gen == first_id,
              "generate: a caller stop id ends it before that token (%u tokens)", res.n_gen);
        gp.n_stop_ids = 0;
        state_t T;
        mkstate(&T, &L[0].m, v->seq_len[0] + 4, 8, ZT_KV_Q8);
        r = zt_model_generate(&L[0].m, &T.s, &tok, prompt, plen, &gp, work, wb, &res);
        CHECK(r == 0 && res.stop == ZT_GEN_CTX && res.n_gen == 5 && T.s.pos == T.s.n_ctx,
              "generate: stops when the KV cache is full (%u generated)", res.n_gen);
        /* a second turn continues the same cache */
        zt_model_state_seek(&S.s, 0);
        gp.max_tokens = 4;
        r = zt_model_generate(&L[0].m, &S.s, &tok, prompt, plen, &gp, work, wb, &res);
        uint32_t p1 = S.s.pos;
        r |= zt_model_generate(&L[0].m, &S.s, &tok, (const uint8_t *) "<|im_end|>\n", 11, &gp, work,
                               wb, &res);
        CHECK(r == 0 && S.s.pos > p1 && res.n_gen == 4,
              "generate: a second turn appends (%u -> %u)", p1, S.s.pos);
        zt_sampler_params_t sp = {45875 /* 0.7 */, 20, 62259 /* 0.95 */, 72090, 64, 99};
        zt_sampler_t s1, s2;
        zt_sampler_init(&s1, &sp);
        zt_sampler_init(&s2, &sp);
        cb_t c1, c2;
        memset(&c1, 0, sizeof c1);
        memset(&c2, 0, sizeof c2);
        gp.max_tokens = 12;
        gp.sampler = &s1;
        gp.ctx = &c1;
        zt_model_state_seek(&S.s, 0);
        zt_model_generate(&L[0].m, &S.s, &tok, prompt, plen, &gp, work, wb, &res);
        gp.sampler = &s2;
        gp.ctx = &c2;
        zt_model_state_seek(&S.s, 0);
        zt_model_generate(&L[0].m, &S.s, &tok, prompt, plen, &gp, work, wb, &res);
        CHECK(c1.n == c2.n && c1.n > 0 && !memcmp(c1.ids, c2.ids, c1.n * 4),
              "generate with sampling: same seed, same %u tokens", c1.n);
        uint64_t need = zt_model_gen_work_bytes(&L[0].m, &tok, plen, gp.max_tokens);
        CHECK(zt_model_generate(&L[0].m, &S.s, &tok, prompt, plen, &gp, work, need - 1, &res) ==
                  ZT_MODEL_ESPACE,
              "generate: short work area refused");
        free(T.mem);
        free(S.mem);
        free(work);
        free(tar);
    }

    /* fuzz the loader with mutated headers */
    {
        uint32_t iters = long_fuzz ? 20000 : 1500, ok = 0, ran = 0;
        double t0 = now();
        for (uint32_t vi = 0; vi < MF_N_VARIANTS; vi++)
            fuzz(&MF_VARIANTS[vi], files[vi], iters, &ok, &ran);
        CHECK(true, "fuzz: %u mutated files per model, %u still loaded, %u ran 3 tokens (%.1f s)",
              iters, ok, ran, now() - t0);
    }

    /* speed */
    {
        state_t S;
        const zt_model_t *m = &L[0].m;
        mkstate(&S, m, 256, 32, ZT_KV_Q16);
        zt_fx *lg = malloc(m->cfg.n_vocab * 4);
        int32_t toks[128];
        for (int i = 0; i < 128; i++) toks[i] = (i * 7) % 1200;
        double t0 = now();
        uint32_t reps = 0;
        do {
            zt_model_state_seek(&S.s, 0);
            for (int i = 0; i < 128; i++) zt_model_decode(m, &S.s, toks[i], lg);
            reps++;
        } while (now() - t0 < 0.5);
        double tiny = reps * 128 / (now() - t0);
        printf("       tiny qwen2 (2 layers, 64 wide, vocab 1200): %.0f tokens/s decode (one "
               "thread)\n",
               tiny);
        free(lg);
        free(S.mem);
        /* one real-size Qwen2.5-0.5B layer */
        uint64_t sz;
        uint8_t *b = synth_qwen2(896, 14, 2, 4864, 1, 32, &sz);
        loaded_t R;
        zt_model_err_t err;
        int32_t r = load(&R, b, sz, &err);
        CHECK(r == 0,
              "synthetic real-size layer loads (hidden 896, 14 heads, 2 KV, ff 4864: %.1f MB)",
              sz / 1e6);
        if (!r) {
            mkstate(&S, &R.m, 512, 32, ZT_KV_Q16);
            lg = malloc(32 * 4);
            int32_t tk[64];
            for (int i = 0; i < 64; i++) tk[i] = i % 32;
            zt_model_eval(&R.m, &S.s, tk, 8, lg, 0);
            t0 = now();
            uint32_t n = 0;
            while (now() - t0 < 1.0) {
                zt_model_decode(&R.m, &S.s, tk[n % 64], lg);
                n++;
                if (S.s.pos > 480) zt_model_state_seek(&S.s, 8);
            }
            double t_layer = (now() - t0) / n;
            zt_model_state_seek(&S.s, 0);
            t0 = now();
            uint32_t np = 0;
            while (now() - t0 < 1.0) {
                zt_model_state_seek(&S.s, 0);
                zt_model_eval(&R.m, &S.s, tk, 64, lg, 0);
                np += 64;
            }
            double t_layer_pf = (now() - t0) / np;
            double macs = 896.0 * (896 + 128 + 128 + 896) + 3.0 * 896 * 4864, head = 151936.0 * 896;
            double est = 24 * t_layer + t_layer * head / macs;
            double est_pf = 24 * t_layer_pf + t_layer_pf * head / macs;
            printf("       one 0.5B-size layer: %.2f ms per token decode, %.2f ms per token in "
                   "batched prefill (%.2f GMAC/s)\n",
                   t_layer * 1e3, t_layer_pf * 1e3, macs / t_layer / 1e9);
            printf("       ESTIMATE for Qwen2.5-0.5B Q8_0 (24 layers + 151936 x 896 head, short "
                   "context, one thread): %.2f tokens/s decode, %.1f tokens/s prefill\n",
                   1 / est, 1 / est_pf);
            free(lg);
            free(S.mem);
            free(R.arena);
        }
        free(b);
    }

    for (uint32_t vi = 0; vi < MF_N_VARIANTS; vi++) {
        free(files[vi]);
        free(L[vi].arena);
    }
    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
