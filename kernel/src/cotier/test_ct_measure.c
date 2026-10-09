/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_ct_measure.c — what does the tier-1 surplus gate cost in accuracy?
 *
 * Measured on the three tiny random models of src/tensor/test_model_fixture.h
 * (qwen2, qwen3, llama; 2 layers, 64 wide), which this test rebuilds byte for
 * byte (read-only use of the tensor fixture). The tensor code is not changed
 * and zt_model is not called: this file runs its own float64 forward pass of
 * llama.cpp's semantics (the same as gen_model_fixture.py's numpy one) from
 * the same weights, first checked against the fixture's reference logits,
 * and masks attention edges with ct_gate_attn exactly as an attention block
 * would. Unpruned and pruned runs use the same harness, so the differences
 * are the gate's alone.
 *
 * Reported per model and policy: the fraction of causal attention edges
 * pruned (of all edges, and of the edges outside the always-kept window),
 * the max and RMS logit error against unpruned (absolute, and relative to
 * the logits' standard deviation), top-1 agreement over every position,
 * greedy-chain agreement, and the gate's multiply-adds against attention's.
 *
 *   gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc/tensor -Isrc/cotier src/cotier/test_ct_measure.c \
 *       src/cotier/ct_gate.c src/tensor/zt_gguf.c src/tensor/zt.c src/tensor/zt_isf.c -lm \
 *       -o /tmp/test_ct_measure && /tmp/test_ct_measure
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ct_gate.h"
#include "zt_gguf.h"
#include "test_model_fixture.h"

static int failures = 0, checks = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        int ok_ = (c) ? 1 : 0;                                                                     \
        if (!ok_) failures++;                                                                      \
        printf(ok_ ? "[PASS] " : "[FAIL] ");                                                       \
        printf(__VA_ARGS__);                                                                       \
        printf("\n");                                                                              \
    } while (0)

/* ---- rebuild a fixture file (as test_zt_model.c does) ---- */

static uint64_t mix(uint64_t seed, uint64_t i)
{
    uint64_t z = seed + (i + 1) * 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
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
                memcpy(p + 4 * i, &f, 4);
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

/* ---- float64 weights ---- */

static double h2d(uint16_t h)
{
    int e = (h >> 10) & 31, m = h & 1023;
    double v = e == 0 ? ldexp(m, -24) : e == 31 ? INFINITY : ldexp(1024 + m, e - 25);
    return (h >> 15) ? -v : v;
}

static double *tensor(const zt_gguf_t *g, const char *name, uint64_t *rows, uint64_t *cols)
{
    zt_gguf_tensor_t t;
    if (zt_gguf_find_tensor(g, name, &t)) return 0;
    double *w = malloc(t.n_elems * sizeof *w);
    for (uint64_t i = 0; i < t.n_elems; i++) {
        if (t.type == ZT_GGML_F32) {
            float f;
            memcpy(&f, t.data + 4 * i, 4);
            w[i] = f;
        } else if (t.type == ZT_GGML_F16) {
            w[i] = h2d((uint16_t) (t.data[2 * i] | t.data[2 * i + 1] << 8));
        } else if (t.type == ZT_GGML_Q8_0) {
            const uint8_t *blk = t.data + 34 * (i / 32);
            w[i] = h2d((uint16_t) (blk[0] | blk[1] << 8)) * (int8_t) blk[2 + i % 32];
        } else {
            free(w);
            return 0;
        }
    }
    if (cols) *cols = t.dims[0];
    if (rows) *rows = t.n_elems / t.dims[0];
    return w;
}

typedef struct {
    double *an, *wq, *wk, *wv, *bq, *bk, *bv, *qn, *kn, *wo, *fn, *wg, *wu, *wd;
} layer_t;

typedef struct {
    uint32_t ne, nf, nh, nkv, hd, nl, V;
    bool neox;
    double eps, inv[256];
    double *embd, *out, *onorm;
    layer_t L[4];
} model_t;

static float f32_key(const zt_gguf_t *g, const char *key)
{
    zt_gguf_val_t v;
    if (zt_gguf_find(g, key, &v)) return 0;
    uint32_t bits = (uint32_t) v.u;
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

static bool load(model_t *m, const zt_gguf_t *g)
{
    zt_gguf_val_t a;
    char arch[16] = {0}, key[96];
    if (zt_gguf_find(g, "general.architecture", &a) || a.str.len >= sizeof arch) return false;
    memcpy(arch, a.str.p, a.str.len);
#define KI(name) (snprintf(key, sizeof key, "%s." name, arch), zt_gguf_get_int(g, key, 0))
    m->nl = (uint32_t) KI("block_count");
    m->ne = (uint32_t) KI("embedding_length");
    m->nf = (uint32_t) KI("feed_forward_length");
    m->nh = (uint32_t) KI("attention.head_count");
    m->nkv = (uint32_t) KI("attention.head_count_kv");
    m->hd = (uint32_t) KI("attention.key_length");
    if (!m->hd) m->hd = m->ne / m->nh;
    snprintf(key, sizeof key, "%s.attention.layer_norm_rms_epsilon", arch);
    m->eps = f32_key(g, key);
    snprintf(key, sizeof key, "%s.rope.freq_base", arch);
    double base = f32_key(g, key);
    m->neox = strcmp(arch, "llama") != 0;
    uint64_t rows, cols;
    m->embd = tensor(g, "token_embd.weight", &rows, &cols);
    m->V = (uint32_t) rows;
    m->out = tensor(g, "output.weight", 0, 0);
    if (!m->out) m->out = m->embd;
    m->onorm = tensor(g, "output_norm.weight", 0, 0);
    double *ff = tensor(g, "rope_freqs.weight", 0, 0);
    for (uint32_t i = 0; i < m->hd / 2; i++)
        m->inv[i] = pow(base, -2.0 * i / m->hd) / (ff ? ff[i] : 1.0);
    free(ff);
    for (uint32_t l = 0; l < m->nl; l++) {
        layer_t *L = &m->L[l];
#define T(f, nm) (snprintf(key, sizeof key, "blk.%u." nm, l), L->f = tensor(g, key, 0, 0))
        T(an, "attn_norm.weight");
        T(wq, "attn_q.weight");
        T(wk, "attn_k.weight");
        T(wv, "attn_v.weight");
        T(bq, "attn_q.bias");
        T(bk, "attn_k.bias");
        T(bv, "attn_v.bias");
        T(qn, "attn_q_norm.weight");
        T(kn, "attn_k_norm.weight");
        T(wo, "attn_output.weight");
        T(fn, "ffn_norm.weight");
        T(wg, "ffn_gate.weight");
        T(wu, "ffn_up.weight");
        T(wd, "ffn_down.weight");
    }
    return m->embd && m->onorm && m->L[0].wq;
}

static void unload(model_t *m)
{
    if (m->out != m->embd) free(m->out);
    free(m->embd);
    free(m->onorm);
    for (uint32_t l = 0; l < m->nl; l++) {
        layer_t *L = &m->L[l];
        double *all[] = {L->an, L->wq, L->wk, L->wv, L->bq, L->bk, L->bv,
                         L->qn, L->kn, L->wo, L->fn, L->wg, L->wu, L->wd};
        for (uint32_t i = 0; i < sizeof all / sizeof *all; i++) free(all[i]);
    }
}

static void matvec(const double *w, uint32_t rows, uint32_t cols, const double *x, double *y)
{
    for (uint32_t r = 0; r < rows; r++) {
        double s = 0;
        for (uint32_t c = 0; c < cols; c++) s += w[(uint64_t) r * cols + c] * x[c];
        y[r] = s;
    }
}

static void norm(const double *x, const double *g, uint32_t n, double eps, double *y)
{
    double s = 0;
    for (uint32_t i = 0; i < n; i++) s += x[i] * x[i];
    double r = 1.0 / sqrt(s / n + eps);
    for (uint32_t i = 0; i < n; i++) y[i] = x[i] * r * g[i];
}

static void rope(const model_t *m, double *x, uint32_t pos)
{
    uint32_t half = m->hd / 2;
    for (uint32_t i = 0; i < half; i++) {
        uint32_t ia = m->neox ? i : 2 * i, ib = m->neox ? i + half : 2 * i + 1;
        double th = pos * m->inv[i], c = cos(th), s = sin(th), a = x[ia], b = x[ib];
        x[ia] = a * c - b * s;
        x[ib] = a * s + b * c;
    }
}

/* ---- pruning policies ---- */

enum { P_OFF, P_SURPLUS, P_ALIGN, P_RANDOM };

typedef struct {
    uint32_t kind;
    double param; /* SURPLUS: floor / ln N; ALIGN: cosine floor; RANDOM: drop probability */
    uint32_t window, clusters, iters;
    /* totals */
    uint64_t edges, kept, eligible, gate_macs, full_macs;
} policy_t;

static zt_fx q16(double v)
{
    double r = floor(v * 65536.0 + 0.5);
    return r > INT32_MAX ? INT32_MAX : r < INT32_MIN ? INT32_MIN : (zt_fx) r;
}

/* Forward pass over tokens[0..T); logits[T][V] (all positions, or only the
 * last row when last_only). */
static void forward(const model_t *m, const int32_t *tok, uint32_t T, policy_t *p, double *logits,
                    bool last_only)
{
    uint32_t ne = m->ne, hd = m->hd, qd = m->nh * hd, kvd = m->nkv * hd, grp = m->nh / m->nkv;
    double *x = calloc((size_t) T * ne, 8), *h = malloc(ne * 8), *q = malloc((size_t) T * qd * 8),
           *k = malloc((size_t) T * kvd * 8), *v = malloc((size_t) T * kvd * 8),
           *att = malloc(qd * 8), *tmp = malloc(ne * 8), *g = malloc(m->nf * 8),
           *u = malloc(m->nf * 8), *sc = malloc(T * 8);
    zt_fx *qb = malloc((size_t) T * hd * 4), *kb = malloc((size_t) T * hd * 4);
    uint8_t *keep = malloc((size_t) T * T), *qa = malloc(T), *ka = malloc(T);
    for (uint32_t t = 0; t < T; t++)
        memcpy(x + (size_t) t * ne, m->embd + (size_t) tok[t] * ne, ne * 8);
    ct_gate_cfg_t gc = {0};
    gc.mode = p->kind == P_ALIGN ? CT_GATE_ALIGN : CT_GATE_SURPLUS;
    gc.n_clusters = p->clusters;
    gc.iters = p->iters;
    gc.N = hd;
    gc.window = p->window;
    gc.floor = p->kind == P_ALIGN ? q16(p->param) : q16(p->param * log((double) hd));
    ct_gate_ctx_t ctx;
    ct_gate_ctx_init(&ctx, &gc, qa, ka, T);
    for (uint32_t l = 0; l < m->nl; l++) {
        const layer_t *L = &m->L[l];
        for (uint32_t t = 0; t < T; t++) {
            norm(x + (size_t) t * ne, L->an, ne, m->eps, h);
            double *qt = q + (size_t) t * qd, *kt = k + (size_t) t * kvd,
                   *vt = v + (size_t) t * kvd;
            matvec(L->wq, qd, ne, h, qt);
            matvec(L->wk, kvd, ne, h, kt);
            matvec(L->wv, kvd, ne, h, vt);
            if (L->bq)
                for (uint32_t i = 0; i < qd; i++) qt[i] += L->bq[i];
            if (L->bk)
                for (uint32_t i = 0; i < kvd; i++) kt[i] += L->bk[i];
            if (L->bv)
                for (uint32_t i = 0; i < kvd; i++) vt[i] += L->bv[i];
            for (uint32_t hh = 0; hh < m->nh; hh++) {
                if (L->qn) norm(qt + hh * hd, L->qn, hd, m->eps, qt + hh * hd);
                rope(m, qt + hh * hd, t);
            }
            for (uint32_t hh = 0; hh < m->nkv; hh++) {
                if (L->kn) norm(kt + hh * hd, L->kn, hd, m->eps, kt + hh * hd);
                rope(m, kt + hh * hd, t);
            }
        }
        double *ao = malloc((size_t) T * qd * 8);
        for (uint32_t hh = 0; hh < m->nh; hh++) {
            uint32_t kv = hh / grp;
            if (p->kind == P_SURPLUS || p->kind == P_ALIGN) {
                for (uint32_t t = 0; t < T; t++)
                    for (uint32_t i = 0; i < hd; i++) {
                        qb[t * hd + i] = q16(q[(size_t) t * qd + hh * hd + i]);
                        kb[t * hd + i] = q16(k[(size_t) t * kvd + kv * hd + i]);
                    }
                int32_t r = ct_gate_attn(&ctx, l, hh, qb, T, hd, 0, kb, T, hd, 0, hd, keep);
                if (r < 0) memset(keep, 1, (size_t) T * T);
            } else {
                for (uint32_t t = 0; t < T; t++)
                    for (uint32_t j = 0; j < T; j++) {
                        bool in = j <= t && (t - j < p->window || j == t);
                        bool rnd = (mix(0xC07135ull + l * 64 + hh, (uint64_t) t * T + j) >> 11) *
                                       (1.0 / 9007199254740992.0) >=
                                   p->param;
                        keep[(size_t) t * T + j] = j <= t && (p->kind == P_OFF || in || rnd);
                    }
            }
            for (uint32_t t = 0; t < T; t++) {
                const double *qt = q + (size_t) t * qd + hh * hd;
                double mx = -INFINITY, sum = 0;
                for (uint32_t j = 0; j <= t; j++) {
                    p->edges++;
                    if (t - j >= p->window) p->eligible++;
                    if (!keep[(size_t) t * T + j]) {
                        sc[j] = -INFINITY;
                        continue;
                    }
                    p->kept++;
                    double s = 0;
                    for (uint32_t i = 0; i < hd; i++)
                        s += qt[i] * k[(size_t) j * kvd + kv * hd + i];
                    sc[j] = s / sqrt((double) hd);
                    if (sc[j] > mx) mx = sc[j];
                }
                for (uint32_t j = 0; j <= t; j++) {
                    sc[j] = sc[j] == -INFINITY ? 0 : exp(sc[j] - mx);
                    sum += sc[j];
                }
                double *o = ao + (size_t) t * qd + hh * hd;
                for (uint32_t i = 0; i < hd; i++) {
                    double s = 0;
                    for (uint32_t j = 0; j <= t; j++)
                        s += sc[j] * v[(size_t) j * kvd + (size_t) kv * hd + i];
                    o[i] = s / sum;
                }
            }
        }
        for (uint32_t t = 0; t < T; t++) {
            double *xt = x + (size_t) t * ne;
            matvec(L->wo, ne, qd, ao + (size_t) t * qd, tmp);
            for (uint32_t i = 0; i < ne; i++) xt[i] += tmp[i];
            norm(xt, L->fn, ne, m->eps, h);
            matvec(L->wg, m->nf, ne, h, g);
            matvec(L->wu, m->nf, ne, h, u);
            for (uint32_t i = 0; i < m->nf; i++) g[i] = g[i] / (1 + exp(-g[i])) * u[i];
            matvec(L->wd, ne, m->nf, g, tmp);
            for (uint32_t i = 0; i < ne; i++) xt[i] += tmp[i];
        }
        free(ao);
        (void) att;
    }
    for (uint32_t t = last_only ? T - 1 : 0; t < T; t++) {
        norm(x + (size_t) t * ne, m->onorm, ne, m->eps, h);
        matvec(m->out, m->V, ne, h, logits + (size_t) (last_only ? 0 : t) * m->V);
    }
    p->gate_macs += ctx.gate_macs;
    p->full_macs += ctx.full_macs;
    free(x), free(h), free(q), free(k), free(v), free(att), free(tmp), free(g), free(u), free(sc);
    free(qb), free(kb), free(keep), free(qa), free(ka);
}

static uint32_t argmax(const double *l, uint32_t n)
{
    uint32_t b = 0;
    for (uint32_t i = 1; i < n; i++)
        if (l[i] > l[b]) b = i;
    return b;
}

/* ---- the experiment ---- */

#define NSEQ   4u
#define SEQLEN 96u
#define PROMPT 16u
#define NGEN   32u

typedef struct {
    double maxerr, rms, std, top1, chain_prefix, chain_match, prune_all, prune_elig, cost;
} result_t;

static void greedy(const model_t *m, const int32_t *prompt, policy_t *p, int32_t *out)
{
    int32_t seq[PROMPT + NGEN];
    double *lg = malloc(m->V * 8);
    memcpy(seq, prompt, PROMPT * 4);
    for (uint32_t i = 0; i < NGEN; i++) {
        forward(m, seq, PROMPT + i, p, lg, true);
        seq[PROMPT + i] = out[i] = (int32_t) argmax(lg, m->V);
    }
    free(lg);
}

static void run(const model_t *m, int32_t (*seqs)[SEQLEN], double **ref, const int32_t *chain_ref,
                policy_t *p, result_t *r)
{
    double *lg = malloc((size_t) SEQLEN * m->V * 8);
    double se = 0, sv = 0, mean = 0;
    uint64_t n = 0, agree = 0, pos = 0;
    r->maxerr = 0;
    p->edges = p->kept = p->eligible = p->gate_macs = p->full_macs = 0;
    for (uint32_t s = 0; s < NSEQ; s++) {
        forward(m, seqs[s], SEQLEN, p, lg, false);
        for (uint32_t t = 0; t < SEQLEN; t++) {
            const double *a = ref[s] + (size_t) t * m->V, *b = lg + (size_t) t * m->V;
            for (uint32_t i = 0; i < m->V; i++) {
                double e = fabs(a[i] - b[i]);
                if (e > r->maxerr) r->maxerr = e;
                se += e * e;
                sv += a[i] * a[i];
                mean += a[i];
                n++;
            }
            agree += argmax(a, m->V) == argmax(b, m->V);
            pos++;
        }
    }
    uint64_t pruned = p->edges - p->kept;
    r->prune_all = (double) pruned / p->edges;
    r->prune_elig = p->eligible ? (double) pruned / p->eligible : 0;
    r->cost = p->full_macs ? (double) p->gate_macs / p->full_macs : 0;
    mean /= n;
    r->std = sqrt(sv / n - mean * mean);
    r->rms = sqrt(se / n);
    r->top1 = (double) agree / pos;
    int32_t chain[NGEN];
    policy_t pc = *p;
    greedy(m, seqs[0], &pc, chain);
    uint32_t prefix = 0, match = 0;
    while (prefix < NGEN && chain[prefix] == chain_ref[prefix]) prefix++;
    for (uint32_t i = 0; i < NGEN; i++) match += chain[i] == chain_ref[i];
    r->chain_prefix = prefix;
    r->chain_match = (double) match / NGEN;
    free(lg);
}

static void print_row(const char *name, double param, const result_t *r)
{
    printf("       %-8s %6.3f | pruned %5.1f%% (%5.1f%% of eligible) | max err %7.4f (%5.3f sd) "
           "rms %7.4f (%5.3f sd) | top-1 %5.1f%% | greedy prefix %2.0f/%u, match %5.1f%% | "
           "gate/attn MACs %.2f\n",
           name, param, 100 * r->prune_all, 100 * r->prune_elig, r->maxerr, r->maxerr / r->std,
           r->rms, r->rms / r->std, 100 * r->top1, r->chain_prefix, NGEN, 100 * r->chain_match,
           r->cost);
}

int main(void)
{
    static const double surplus_floors[] = {0.50, 0.80, 0.90, 0.95, 0.98, 0.99};
    static const double align_floors[] = {-0.25, 0.0, 0.25, 0.5};
    const uint32_t nsf = sizeof surplus_floors / sizeof *surplus_floors;
    const uint32_t naf = sizeof align_floors / sizeof *align_floors;
    for (uint32_t vi = 0; vi < MF_N_VARIANTS; vi++) {
        const mf_variant_t *v = &MF_VARIANTS[vi];
        uint8_t *file = rebuild(v);
        CHECK(fnv(file, v->file_len) == v->fnv, "%s: fixture rebuilt byte for byte", v->name);
        zt_gguf_t g;
        model_t m;
        memset(&m, 0, sizeof m);
        CHECK(zt_gguf_open(&g, file, v->file_len) == 0 && load(&m, &g), "%s: weights loaded",
              v->name);
        /* the harness against the fixture's float64 reference */
        double maxe = 0;
        uint32_t off = 0, am = 0, amn = 0;
        policy_t off_p = {P_OFF, 0, 0, 1, 0, 0, 0, 0, 0, 0};
        for (uint32_t s = 0; s < v->n_seq; s++) {
            uint32_t n = v->seq_len[s];
            double *lg = malloc((size_t) n * m.V * 8);
            forward(&m, v->seq + off, n, &off_p, lg, false);
            for (uint32_t p = 0; p < n; p++) {
                for (uint32_t i = 0; i < MF_HEAD; i++) {
                    double e = fabs(lg[(size_t) p * m.V + i] -
                                    v->head[(size_t) (off + p) * MF_HEAD + i] / 65536.0);
                    if (e > maxe) maxe = e;
                }
                am += (int32_t) argmax(lg + (size_t) p * m.V, m.V) == v->argmax[off + p];
                amn++;
            }
            for (uint32_t i = 0; i < m.V; i++) {
                double e =
                    fabs(lg[(size_t) (n - 1) * m.V + i] - v->last[(size_t) s * m.V + i] / 65536.0);
                if (e > maxe) maxe = e;
            }
            free(lg);
            off += n;
        }
        CHECK(maxe < 2e-5 && am == amn,
              "%s: harness matches the fixture's float64 reference (max err %.2g, argmax %u/%u)",
              v->name, maxe, am, amn);
        /* evaluation sequences and the unpruned reference */
        int32_t seqs[NSEQ][SEQLEN];
        double *ref[NSEQ];
        for (uint32_t s = 0; s < NSEQ; s++) {
            for (uint32_t t = 0; t < SEQLEN; t++)
                seqs[s][t] = (int32_t) (mix(0x5EEDull + vi * 1000 + s, t) % m.V);
            ref[s] = malloc((size_t) SEQLEN * m.V * 8);
            forward(&m, seqs[s], SEQLEN, &off_p, ref[s], false);
        }
        int32_t chain_ref[NGEN];
        greedy(&m, seqs[0], &off_p, chain_ref);
        printf("       --- %s: %u sequences of %u random tokens, window 8, 8 clusters, 2 passes, "
               "N = head_dim = %u; greedy %u tokens after a %u-token prompt ---\n",
               v->name, NSEQ, SEQLEN, m.hd, NGEN, PROMPT);
        result_t r;
        double surplus_prune[8];
        for (uint32_t f = 0; f < nsf; f++) {
            policy_t p = {P_SURPLUS, surplus_floors[f], 8, 8, 2, 0, 0, 0, 0, 0};
            run(&m, seqs, ref, chain_ref, &p, &r);
            surplus_prune[f] = r.prune_elig;
            print_row("surplus", surplus_floors[f], &r);
            CHECK(r.prune_all < 1.0 && p.kept >= p.edges - p.eligible,
                  "%s surplus %.2f: window and diagonal always kept", v->name, surplus_floors[f]);
        }
        for (uint32_t f = 0; f < naf; f++) {
            policy_t p = {P_ALIGN, align_floors[f], 8, 8, 2, 0, 0, 0, 0, 0};
            run(&m, seqs, ref, chain_ref, &p, &r);
            print_row("align", align_floors[f], &r);
        }
        for (uint32_t f = 0; f < nsf; f++) {
            if (surplus_prune[f] == 0 || (f && surplus_prune[f] == surplus_prune[f - 1])) continue;
            policy_t p = {P_RANDOM, surplus_prune[f], 8, 8, 2, 0, 0, 0, 0, 0};
            run(&m, seqs, ref, chain_ref, &p, &r);
            print_row("random", surplus_prune[f], &r);
        }
        {
            policy_t p = {P_SURPLUS, 0.0, 8, 8, 2, 0, 0, 0, 0, 0};
            run(&m, seqs, ref, chain_ref, &p, &r);
            CHECK(r.prune_all == 0 && r.maxerr == 0 && r.top1 == 1.0,
                  "%s: floor 0 prunes nothing and changes nothing", v->name);
        }
        for (uint32_t s = 0; s < NSEQ; s++) free(ref[s]);
        unload(&m);
        free(file);
    }
    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
