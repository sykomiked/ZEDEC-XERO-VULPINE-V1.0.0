/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zt_gpu.c — the whole-pass GPU op list (zt_gpu.h) run by its C backend,
 * on any host, against zt_model.c bit for bit.
 *
 * 1. Each layer kernel of zt_metal_math.h against the zt_model.c function it
 *    ports (this file includes zt_model.c to reach them): random rows and the
 *    edges (all zero, INT32_MIN/MAX, single spikes, every activation shift,
 *    SiLU over the whole Q16 range, attention with queries that need
 *    scaling and scores that underflow to zero).
 * 2. The fixture models (Qwen2 with q/k/v bias, Qwen3 with q/k norms, Llama)
 *    through zt_model_eval with zt_kern_t.fwd_full = zg_sim_fwd: all logits
 *    of a 4-token batch, last-token logits, then greedy decode steps, both KV
 *    types (Q8 is declined and runs on the CPU), compared with the C
 *    reference: logits, the cache, the hidden state and the saturation count.
 * test_zt_metal.c runs the same op list on a Mac's GPU. */
#define _POSIX_C_SOURCE 200112L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zt_model.c"

#include "test_model_fixture.h"
#include "test_zt_fixture_run.h"
#include "zt_gpu.h"
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

static uint64_t rs = 0x2545F4914F6CDD1Dull;
static uint64_t rnd(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 7;
    rs ^= rs << 17;
    return rs;
}

/* a value of class c: random wide, random small, zero, extreme */
static int32_t val(uint32_t c)
{
    switch (c % 6) {
    case 0:
        return (int32_t) rnd();
    case 1:
        return (int32_t) (rnd() % 131072) - 65536;
    case 2:
        return 0;
    case 3:
        return rnd() & 1 ? INT32_MAX : INT32_MIN;
    case 4:
        return (int32_t) (rnd() % 2001) - 1000;
    default:
        return (int32_t) (rnd() >> (rnd() % 40)) * (rnd() & 1 ? 1 : -1);
    }
}

static zg_p P(uint32_t n)
{
    zg_p p;
    memset(&p, 0, sizeof p);
    p.u[0] = n;
    return p;
}

static void test_rms(void)
{
    uint32_t bad = 0, cases = 0;
    static int32_t x[4096], g[4096], ref[4096], got[4096];
    uint64_t inv;
    for (uint32_t t = 0; t < 3000; t++) {
        uint32_t n = 32 * (1 + rnd() % 64), cls = t % 7;
        for (uint32_t i = 0; i < n; i++) {
            x[i] = cls == 6 ? (i == rnd() % n ? val(0) : 0) : val(cls + (rnd() % 3 == 0));
            g[i] = (int32_t) (rnd() % 400000) - 100000;
        }
        if (t % 50 == 0)
            for (uint32_t i = 0; i < n; i++) x[i] = 0;
        zt_gain_t gn = {g, (uint32_t) (rnd() % 31)};
        uint64_t eps = t % 3 == 0 ? 0 : rnd() >> (rnd() % 63);
        rmsnorm(x, n, &gn, eps, ref);
        zg_p p = P(1);
        p.u[1] = n, p.u[2] = n, p.u[3] = (uint32_t) eps, p.u[4] = (uint32_t) (eps >> 32);
        zk_rms_inv(0, p, (zb *) x, (zb *) &inv, 0, 0, 0, 0, 0);
        p.u[3] = gn.frac, p.u[4] = n;
        for (uint32_t i = 0; i < n; i++)
            zk_rms_apply(i, p, (zb *) x, (zb *) &inv, (zb *) g, (zb *) got, 0, 0, 0);
        cases++;
        bad += memcmp(ref, got, n * 4) != 0;
    }
    CHECK(!bad, "RMS norm: %u rows (random, zero, extreme, spikes, eps 0..2^64) == rmsnorm", cases);
}

static void test_act(void)
{
    uint32_t bad = 0;
    static int32_t x[1024], qs[32], s2[32];
    static int16_t q[1024], q2[1024];
    static uint8_t sh[32], h2[32];
    for (uint32_t t = 0; t < 5000; t++) {
        for (uint32_t i = 0; i < 1024; i++) x[i] = val(t + (i == 0));
        act_quant(x, 1024, q, sh, qs);
        zg_p p = P(32);
        p.u[1] = 1024, p.u[2] = 1024;
        for (uint32_t i = 0; i < 32; i++) zk_act(i, p, (zb *) x, (zb *) q2, h2, (zb *) s2, 0, 0, 0);
        bad += memcmp(q, q2, sizeof q) || memcmp(sh, h2, sizeof sh) || memcmp(qs, s2, sizeof qs);
    }
    CHECK(!bad, "activation quantiser: 5000 rows of 32 blocks (every shift 0..17) == act_quant");
}

static void test_silu(void)
{
    uint32_t bad = 0, n = 0;
    const uint32_t *E = zt_exp2_table();
    for (int64_t g = INT32_MIN; g <= INT32_MAX; g += 4093 + (rnd() % 7)) {
        int32_t gv = (int32_t) g, u = val((uint32_t) n), a = gv, b = u;
        zg_p p = P(1);
        zk_silu(0, p, (zb *) &a, (zb *) &b, (zb *) E, 0, 0, 0, 0);
        bad += a != silu_mul(gv, u);
        n++;
    }
    for (int32_t gv = -65536 * 40; gv <= 65536 * 40; gv += 97) {
        int32_t u = 65536, a = gv, b = u;
        zk_silu(0, P(1), (zb *) &a, (zb *) &b, (zb *) E, 0, 0, 0, 0);
        bad += a != silu_mul(gv, u);
        n++;
    }
    CHECK(!bad, "SiLU * up: %u gates over the whole Q16 range == silu_mul", n);
}

static void test_misc(void)
{
    /* zm_exp == zt_exp, zm_mul_shr == mul_shr, zm_round_div == round_div, zm_udiv */
    uint32_t bad = 0;
    const uint32_t *E = zt_exp2_table();
    for (int64_t x = INT32_MIN; x <= 65536; x += 1009)
        bad += zm_exp((zi) x, E) != zt_exp((zt_fx) x);
    for (uint32_t t = 0; t < 200000; t++) {
        int64_t a = (int64_t) (rnd() >> (rnd() % 64)) * (rnd() & 1 ? 1 : -1);
        if (a == INT64_MIN) a++;
        uint32_t b = (uint32_t) rnd() >> (rnd() % 32), sh = (uint32_t) (rnd() % 80);
        bad += zm_mul_shr(a, b, sh) != mul_shr(a, b, sh);
        uint64_t d = (rnd() >> (rnd() % 64)) | 1;
        int64_t n = (int64_t) (rnd() >> 2) * (rnd() & 1 ? 1 : -1);
        bad += zm_round_div(n, d) != round_div(n, d);
        uint64_t nn = rnd(), dd = rnd() >> (rnd() % 64);
        bad += dd && zm_udiv(nn, dd) != nn / dd;
        uint64_t v = rnd() >> (rnd() % 64);
        bad += zm_isqrt(v) != zt_isqrt64(v);
    }
    CHECK(!bad, "exp, mul_shr, round_div, 64-bit divide and isqrt == the C reference");
}

/* attention for one token: zk_att_* against attend() on a Q16 cache */
static void test_attention(void)
{
    uint32_t bad = 0, cases = 0;
    for (uint32_t t = 0; t < 300; t++) {
        uint32_t nkv = 1 + rnd() % 3, grp = 1 + rnd() % 3, nh = nkv * grp;
        uint32_t hd = 8 * (1 + rnd() % 16), kvd = nkv * hd, n_ctx = 1 + rnd() % 300;
        uint32_t p = (uint32_t) (rnd() % n_ctx), wide = t % 4;
        zt_model_t m;
        memset(&m, 0, sizeof m);
        m.cfg.n_head = nh, m.cfg.n_head_kv = nkv, m.cfg.head_dim = hd, m.kv_dim = kvd;
        m.q_dim = nh * hd;
        uint32_t rt = zt_isqrt64((uint64_t) hd << 40);
        m.inv_sqrt_q30 = (uint32_t) zt_udiv64(1ull << 50, rt, 0);
        zt_model_state_t s;
        memset(&s, 0, sizeof s);
        s.kv_type = ZT_KV_Q16, s.n_ctx = n_ctx;
        s.k16 = malloc((size_t) n_ctx * kvd * 4), s.v16 = malloc((size_t) n_ctx * kvd * 4);
        int32_t *q = malloc((size_t) nh * hd * 4), *ref = malloc((size_t) nh * hd * 4);
        for (uint32_t i = 0; i < n_ctx * kvd; i++) {
            s.k16[i] = wide ? val(i) : (int32_t) (rnd() % 400000) - 200000;
            s.v16[i] = wide ? val(i + 1) : (int32_t) (rnd() % 400000) - 200000;
        }
        for (uint32_t i = 0; i < nh * hd; i++)
            q[i] = wide ? val(i) : (int32_t) (rnd() % 400000) - 200000;
        zt_fx *scores = malloc((size_t) n_ctx * 4);
        int64_t *acc = malloc((size_t) hd * 8);
        int32_t *qs = malloc((size_t) hd * 4);
        for (uint32_t h = 0; h < nh; h++) attend(&m, &s, 0, q, p, ref, h, scores, acc, qs);
        /* GPU kernels: one token at position p, P = p + 1 slots */
        uint32_t PP = p + 1;
        int32_t *gq = malloc((size_t) nh * hd * 4), *sq = malloc(nh * 4),
                *sc = malloc((size_t) nh * PP * 4);
        int32_t *mx = malloc(nh * 4), *out = malloc((size_t) nh * hd * 4);
        uint64_t *sum = malloc(nh * 8);
        zg_p pp = P(0);
        uint32_t u[9] = {0, nh, hd, grp, kvd, PP, p, 0, m.inv_sqrt_q30};
        memcpy(pp.u, u, sizeof u);
        const uint32_t *E = zt_exp2_table();
        for (uint32_t i = 0; i < nh; i++)
            zk_att_q(i, pp, (zb *) q, (zb *) gq, (zb *) sq, 0, 0, 0, 0);
        for (uint32_t i = 0; i < nh * PP; i++)
            zk_att_score(i, pp, (zb *) gq, (zb *) sq, (zb *) sc, (zb *) s.k16, 0, 0, 0);
        for (uint32_t i = 0; i < nh; i++) zk_att_max(i, pp, (zb *) sc, (zb *) mx, 0, 0, 0, 0, 0);
        for (uint32_t i = 0; i < nh * PP; i++)
            zk_att_exp(i, pp, (zb *) sc, (zb *) mx, (zb *) E, 0, 0, 0, 0);
        for (uint32_t i = 0; i < nh; i++) zk_att_sum(i, pp, (zb *) sc, (zb *) sum, 0, 0, 0, 0, 0);
        for (uint32_t i = 0; i < nh * hd; i++)
            zk_att_wv(i, pp, (zb *) sc, (zb *) sum, (zb *) out, (zb *) s.v16, 0, 0, 0);
        bad += memcmp(ref, out, (size_t) nh * hd * 4) != 0;
        cases++;
        free(s.k16), free(s.v16), free(q), free(ref), free(scores), free(acc), free(qs);
        free(gq), free(sq), free(sc), free(mx), free(out), free(sum);
    }
    CHECK(!bad,
          "attention: %u heads sets (GQA 1..3, head_dim 8..128, positions 0..299, wide "
          "and extreme caches) == attend()",
          cases);
}

static void test_rope(void)
{
    uint32_t bad = 0;
    for (uint32_t t = 0; t < 400; t++) {
        uint32_t hd = 2 * (2 + rnd() % 63), nrot = hd - 2 * (rnd() % (hd / 4 + 1)),
                 nh = 1 + rnd() % 4;
        if (nrot < 2) nrot = 2;
        zt_rope_t r;
        if (!zt_rope_init(&r, nrot, 0x461C4000u, t & 1)) continue;
        uint64_t pos = rnd() % 100000;
        int32_t x[4 * 128], y[4 * 128], sc[64 * 2];
        for (uint32_t i = 0; i < nh * hd; i++) x[i] = y[i] = val(i + t);
        zt_rope_apply_heads(&r, x, nh, hd, pos);
        for (uint32_t i = 0; i < nrot / 2; i++)
            zt_rope_sincos(pos * r.freq[i], &sc[2 * i], &sc[2 * i + 1]);
        zg_p p = P(nh * nrot / 2);
        p.u[1] = nh, p.u[2] = hd, p.u[3] = nrot / 2, p.u[4] = r.neox;
        for (uint32_t i = 0; i < nh * nrot / 2; i++)
            zk_rope(i, p, (zb *) y, (zb *) sc, 0, 0, 0, 0, 0);
        bad += memcmp(x, y, nh * hd * 4) != 0;
    }
    CHECK(!bad,
          "RoPE: 400 head sets (NEOX and interleaved, partial rotation) == zt_rope_apply_heads");
}

/* ---- whole passes on the fixtures ---- */

typedef struct {
    zt_fx *logits;
    uint64_t n;
    int32_t *kv;
    uint64_t kvn;
    zt_fx hid[4096];
    uint32_t nsat, ne;
} run_t;

/* a 4-token batch (all logits), the same again with last-token logits only,
 * then 8 greedy steps; the state at the end */
static void run(const zt_model_t *m, const mf_variant_t *v, uint32_t kv, run_t *o)
{
    uint32_t V = v->n_vocab, n0 = v->seq_len[0], steps = 8, ne = m->cfg.n_embd;
    uint64_t need = zt_model_state_bytes(m, 64, 4, kv);
    void *mem = calloc(1, need);
    zt_model_state_t s;
    zt_model_state_init(&s, m, 64, 4, kv, mem, need);
    o->n = (uint64_t) V * (2 * n0 + steps);
    o->logits = calloc(o->n, 4);
    zt_model_eval(m, &s, v->seq, n0, o->logits, ZT_EVAL_ALL_LOGITS);
    zt_fx *lg = o->logits + (uint64_t) n0 * V;
    zt_model_eval(m, &s, v->seq, n0, lg, 0); /* first = B - 1 */
    for (uint32_t i = 0; i < steps; i++) {
        int32_t id = zt_argmax(lg, V);
        lg += V;
        zt_model_decode(m, &s, id, lg);
    }
    o->kvn = kv == ZT_KV_Q16 ? (uint64_t) m->cfg.n_layer * 64 * m->kv_dim : 0;
    o->kv = calloc(o->kvn * 2 + 1, 4);
    if (o->kvn) {
        memcpy(o->kv, s.k16, o->kvn * 4);
        memcpy(o->kv + o->kvn, s.v16, o->kvn * 4);
    }
    o->ne = ne;
    memcpy(o->hid, s.hid, ne * 4);
    o->nsat = s.n_sat;
    free(mem);
}

static uint32_t sim_taken;

static int sim_counted(void *fctx, const void *m, void *s, uint32_t B, zt_fx *logits,
                       uint32_t first, uint32_t *n_sat)
{
    int r = zg_sim_fwd(fctx, m, s, B, logits, first, n_sat);
    sim_taken += r == 0;
    return r;
}

/* bias and residual: the counts and clamps of zt_model.c mm_rows (at or past
 * the 32-bit limits) and tok_rows TS_RESID (past them) */
static void test_counts(void)
{
    uint32_t bad = 0, n = 0;
    for (uint32_t t = 0; t < 20000; t++) {
        int32_t y = val(t), bv = val(t + 3), x = val(t + 1), o = val(t + 5);
        int64_t v = (int64_t) y + bv, w = (int64_t) x + o;
        uint32_t want_b = v >= INT32_MAX || v <= INT32_MIN, want_r = w > INT32_MAX || w < INT32_MIN;
        zm_ctr cb = 0, cr = 0;
        zg_p p = P(1);
        p.u[1] = 1, p.u[2] = 1;
        int32_t yy = y, xx = x;
        zk_bias(0, p, (zb *) &yy, (zb *) &bv, 0, 0, 0, 0, &cb);
        zk_resid(0, P(1), (zb *) &xx, (zb *) &o, 0, 0, 0, 0, &cr);
        bad += cb != want_b || yy != sat32(v) || cr != want_r || xx != sat32(w);
        n += want_b + want_r;
    }
    CHECK(!bad, "bias and residual: 20000 pairs clamp and count as zt_model.c (%u at the limits)",
          n);
}

static void test_models(void)
{
    zg_sim_t *g = zg_sim_new();
    for (uint32_t vi = 0; vi < MF_N_VARIANTS; vi++) {
        const mf_variant_t *v = &MF_VARIANTS[vi];
        uint8_t *file = fx_rebuild(v);
        zt_gguf_t gg;
        zt_model_t m;
        zt_model_err_t err;
        if (zt_gguf_open(&gg, file, v->file_len)) {
            CHECK(0, "%s: fixture opens", v->name);
            continue;
        }
        uint64_t ab = zt_model_arena_bytes(&gg);
        void *arena = malloc(ab);
        if (zt_model_load(&m, &gg, arena, ab, &err)) {
            CHECK(0, "%s: fixture loads", v->name);
            continue;
        }
        /* the Llama fixture's FFN is zt_q8_t rows (converted), which the GPU
         * declines; the others are GGUF Q8_0 in place */
        bool want = m.layers[0].wg.raw != 0;
        CHECK(zg_supported(&m) == want, "%s: the GPU pass %s it", v->name,
              want ? "supports" : "declines");
        for (uint32_t kv = 0; kv < 2; kv++) {
            run_t ref, got;
            m.kern = 0;
            run(&m, v, kv, &ref);
            zt_kern_t k;
            memset(&k, 0, sizeof k);
            k.name = "gpu-sim";
            k.fwd_full = sim_counted;
            k.fctx = g;
            m.kern = &k;
            sim_taken = 0;
            run(&m, v, kv, &got);
            uint32_t passes =
                2 * ((v->seq_len[0] + 3) / 4) + 8; /* batches of 4, twice, then 8 steps */
            bool taken_ok = (want && kv == ZT_KV_Q16) ? sim_taken == passes : sim_taken == 0;
            if (getenv("ZG_DEBUG"))
                printf("  logits %d kv %d hid %d nsat %u/%u\n",
                       !memcmp(ref.logits, got.logits, ref.n * 4),
                       !memcmp(ref.kv, got.kv, ref.kvn * 8), !memcmp(ref.hid, got.hid, ref.ne * 4),
                       ref.nsat, got.nsat);
            CHECK(!memcmp(ref.logits, got.logits, ref.n * 4) &&
                      !memcmp(ref.kv, got.kv, ref.kvn * 8) &&
                      !memcmp(ref.hid, got.hid, ref.ne * 4) && ref.nsat == got.nsat && taken_ok,
                  "%s, KV %s, %s: %llu logits, the cache, the hidden state and the saturation "
                  "count (%u) identical to zt_model.c (%u passes on the GPU list)",
                  v->name, kv ? "Q8" : "Q16",
                  want && !kv ? "whole pass on the C GPU backend" : "declined, run on the CPU",
                  (unsigned long long) ref.n, ref.nsat, sim_taken);
            free(ref.logits), free(got.logits), free(ref.kv), free(got.kv);
        }
        if (want) { /* gains near 2^30: products and residuals saturate */
            for (uint32_t l = 0; l < m.cfg.n_layer; l++) {
                int32_t *ga = (int32_t *) m.layers[l].attn_norm.g;
                int32_t *gf = (int32_t *) m.layers[l].ffn_norm.g;
                for (uint32_t i = 0; i < m.cfg.n_embd; i++)
                    ga[i] = gf[i] = (i % 3 ? 1 : -1) * (int32_t) (0x3FFFFFFF - i * 977);
                /* q/k/v biases at the limits (Qwen2): the bias kernel clamps
                 * and counts, and the values carry into attention */
                if (m.layers[l].bv)
                    for (uint32_t i = 0; i < m.kv_dim; i++)
                        ((int32_t *) m.layers[l].bv)[i] =
                            i % 2 ? INT32_MAX - (int32_t) i : INT32_MIN + (int32_t) i;
            }
            run_t ref, got;
            m.kern = 0;
            run(&m, v, ZT_KV_Q16, &ref);
            zt_kern_t k;
            memset(&k, 0, sizeof k);
            k.fwd_full = sim_counted;
            k.fctx = g;
            m.kern = &k;
            sim_taken = 0;
            run(&m, v, ZT_KV_Q16, &got);
            CHECK(!memcmp(ref.logits, got.logits, ref.n * 4) &&
                      !memcmp(ref.kv, got.kv, ref.kvn * 8) && ref.nsat == got.nsat &&
                      (ref.nsat > 0 || !m.layers[0].bv) &&
                      sim_taken == 2 * ((v->seq_len[0] + 3) / 4) + 8,
                  "%s, gains near 2^30: saturating passes on the C GPU backend == zt_model.c (%u "
                  "saturations counted alike)",
                  v->name, got.nsat);
            free(ref.logits), free(got.logits), free(ref.kv), free(got.kv);
        }
        free(arena);
        free(file);
    }
    zg_sim_free(g);
}

int main(void)
{
    (void) fx_run; /* test_zt_fixture_run.h: this test runs its own sequence */
    printf("=== zt_gpu: the whole forward pass as GPU kernels, run as C, vs zt_model.c ===\n");
    test_misc();
    test_rms();
    test_act();
    test_silu();
    test_rope();
    test_attention();
    test_counts();
    test_models();
    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
