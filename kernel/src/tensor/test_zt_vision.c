/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zt_vision.c — T23 encoder blocks (zt_nn) and the T24 image encoder
 * (zt_vision), against exact math and against a float64 numpy reference of
 * llama.cpp's semantics on a tiny random SigLIP + Idefics3 projector
 * (test_vision_fixture.h, from gen_vision_fixture.py). Built with
 * -DZT_TEST_SIMD and zt_simd*.c it also checks that every kernel set this CPU
 * runs, with and without threads, gives the C reference's bits. Hosted test
 * (libc, libm for the reference values). */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zt_nn.h"
#include "zt_vision.h"
#include "test_vision_fixture.h"
#ifdef ZT_TEST_SIMD
#    include "zt_simd.h"
#endif

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

static uint64_t rs = 0x5EED2026101201ull;
static double urand(void) /* uniform in [-1, 1) */
{
    rs = rs * 6364136223846793005ull + 1442695040888963407ull;
    return (double) (int64_t) (rs >> 11) / 4503599627370496.0 - 1.0;
}

static double q16d(zt_fx v)
{
    return v / 65536.0;
}

static zt_fx dq16(double v)
{
    return (zt_fx) lround(v * 65536.0);
}

static void test_activations(void)
{
    double e_erf = 0, e_tanh = 0, e_silu = 0, e_sig = 0;
    for (int64_t x = -20 * 65536; x <= 20 * 65536; x += 37) {
        double xd = x / 65536.0;
        double g_erf = 0.5 * xd * (1 + erf(xd / sqrt(2.0)));
        double g_tanh =
            0.5 * xd *
            (1 + tanh(sqrt(2 / 3.14159265358979323846) * (xd + 0.044715 * xd * xd * xd)));
        double sig = 1 / (1 + exp(-xd));
        double d;
        d = fabs(q16d(zt_nn_act1((zt_fx) x, ZT_NN_GELU_ERF)) - g_erf);
        if (d > e_erf) e_erf = d;
        d = fabs(q16d(zt_nn_act1((zt_fx) x, ZT_NN_GELU_TANH)) - g_tanh);
        if (d > e_tanh) e_tanh = d;
        d = fabs(q16d(zt_nn_act1((zt_fx) x, ZT_NN_SILU)) - xd * sig);
        if (d > e_silu) e_silu = d;
        d = fabs(q16d(zt_nn_act1((zt_fx) x, ZT_NN_SIGMOID)) - sig);
        if (d > e_sig) e_sig = d;
    }
    /* a gate error below 5e-7 times |x| <= 20, plus Q16 rounding (2^-17) */
    CHECK(e_erf < 2.2e-5 && e_tanh < 2.2e-5,
          "GELU erf and tanh forms on [-20, 20]: max abs err %.2e, %.2e (< 2.2e-5)", e_erf, e_tanh);
    CHECK(e_silu < 2.2e-5 && e_sig < 1.6e-5, "SiLU and sigmoid: max abs err %.2e, %.2e", e_silu,
          e_sig);
    CHECK(zt_nn_act1(0, ZT_NN_GELU_ERF) == 0 &&
              zt_nn_act1(INT32_MAX, ZT_NN_GELU_ERF) == INT32_MAX &&
              zt_nn_act1(INT32_MIN, ZT_NN_GELU_TANH) == 0 &&
              zt_nn_act1(INT32_MIN, ZT_NN_SIGMOID) == 0 &&
              zt_nn_act1(INT32_MAX, ZT_NN_SIGMOID) == 65536,
          "activation end values: GELU(0) = 0, GELU(max) = max, GELU(min) = 0, sigmoid 0 and 1");
}

static void test_norms(zt_nn_t *nn)
{
    enum { N = 384, T = 3 };
    static zt_fx x[T * N], y[T * N], b[N];
    static int32_t gv[N];
    for (uint32_t i = 0; i < N; i++) {
        gv[i] = (int32_t) lround((1.0 + 0.3 * urand()) * (1 << 20));
        b[i] = dq16(0.2 * urand());
    }
    double scale[T] = {1.0, 300.0, 0.002};
    for (uint32_t t = 0; t < T; t++)
        for (uint32_t i = 0; i < N; i++) x[t * N + i] = dq16(scale[t] * (urand() + 0.5));
    zt_nn_norm_t p = {{gv, 20}, b};
    uint32_t eps = 0x358637BDu; /* 1e-6f */
    zt_nn_layernorm(nn, x, N, N, T, &p, eps, y, N);
    double emax = 0;
    for (uint32_t t = 0; t < T; t++) {
        double mu = 0, var = 0;
        for (uint32_t i = 0; i < N; i++) mu += q16d(x[t * N + i]);
        mu /= N;
        for (uint32_t i = 0; i < N; i++) var += pow(q16d(x[t * N + i]) - mu, 2);
        var /= N;
        for (uint32_t i = 0; i < N; i++) {
            double ref =
                (q16d(x[t * N + i]) - mu) / sqrt(var + 1e-6) * (gv[i] / 1048576.0) + q16d(b[i]);
            double d = fabs(q16d(y[t * N + i]) - ref);
            if (t < 2 && d > emax) emax = d; /* row 2 is mostly rounding noise */
        }
    }
    CHECK(emax < 2e-4, "LayerNorm (gain, bias) of rows at scale 1 and 300: max abs err %.2e", emax);
    /* GroupNorm over [T][C]: 4 groups */
    enum { GT = 7, GC = 32, GG = 4 };
    static zt_fx gx[GT * GC], g0[GT * GC];
    for (uint32_t i = 0; i < GT * GC; i++) g0[i] = gx[i] = dq16(3 * urand() + (i % GC) * 0.1);
    zt_nn_norm_t gp = {{gv, 20}, b};
    zt_nn_groupnorm(gx, GC, GT, GC, GG, &gp, 0x3727C5ACu /* 1e-5f */);
    emax = 0;
    for (uint32_t g = 0; g < GG; g++) {
        double mu = 0, var = 0;
        uint32_t cg = GC / GG;
        for (uint32_t t = 0; t < GT; t++)
            for (uint32_t c = g * cg; c < (g + 1) * cg; c++) mu += q16d(g0[t * GC + c]);
        mu /= GT * cg;
        for (uint32_t t = 0; t < GT; t++)
            for (uint32_t c = g * cg; c < (g + 1) * cg; c++)
                var += pow(q16d(g0[t * GC + c]) - mu, 2);
        var /= GT * cg;
        for (uint32_t t = 0; t < GT; t++)
            for (uint32_t c = g * cg; c < (g + 1) * cg; c++) {
                double ref = (q16d(g0[t * GC + c]) - mu) / sqrt(var + 1e-5) * (gv[c] / 1048576.0) +
                             q16d(b[c]);
                double d = fabs(q16d(gx[t * GC + c]) - ref);
                if (d > emax) emax = d;
            }
    }
    CHECK(emax < 1e-4, "GroupNorm (4 groups over 7 steps): max abs err %.2e", emax);
}

static void test_attention(zt_nn_t *nn)
{
    enum { NQ = 9, NK = 300, NH = 3, HD = 40, W = NH * HD };
    static zt_fx q[NQ * W], k[NK * W], v[NK * W], o[NQ * W];
    for (uint32_t i = 0; i < NQ * W; i++) q[i] = dq16(2.5 * urand());
    for (uint32_t i = 0; i < NK * W; i++) k[i] = dq16(2.5 * urand()), v[i] = dq16(4 * urand());
    for (int causal = 0; causal < 2; causal++) {
        zt_nn_attention(nn, q, W, k, W, v, W, NQ, NK, NH, HD, causal, o, W);
        double emax = 0;
        static double s[NK];
        for (uint32_t i = 0; i < NQ; i++)
            for (uint32_t h = 0; h < NH; h++) {
                uint32_t nk = causal ? NK - NQ + i + 1 : NK;
                double mx = -1e300, sum = 0;
                for (uint32_t j = 0; j < nk; j++) {
                    double d = 0;
                    for (uint32_t c = 0; c < HD; c++)
                        d += q16d(q[i * W + h * HD + c]) * q16d(k[j * W + h * HD + c]);
                    s[j] = d / sqrt(HD);
                    if (s[j] > mx) mx = s[j];
                }
                for (uint32_t j = 0; j < nk; j++) sum += s[j] = exp(s[j] - mx);
                for (uint32_t c = 0; c < HD; c++) {
                    double r = 0;
                    for (uint32_t j = 0; j < nk; j++) r += s[j] * q16d(v[j * W + h * HD + c]);
                    double d = fabs(q16d(o[i * W + h * HD + c]) - r / sum);
                    if (d > emax) emax = d;
                }
            }
        CHECK(emax < 3e-3, "attention, %s, 9 queries x 300 keys, 3 heads of 40: max abs err %.2e",
              causal ? "causal" : "non-causal", emax);
    }
}

static void test_conv(void)
{
    enum { T = 11, C = 5, K = 3 };
    zt_fx x[T * C], cols[T * C * K], y[T * C];
    for (uint32_t i = 0; i < T * C; i++) x[i] = (zt_fx) (i * 65536);
    uint32_t to = zt_nn_im2col1d(x, C, T, C, K, 2, 1, cols, C * K);
    bool ok = to == (T + 2 - K) / 2 + 1;
    for (uint32_t t = 0; t < to; t++)
        for (uint32_t c = 0; c < C; c++)
            for (uint32_t j = 0; j < K; j++) {
                int32_t s = (int32_t) (t * 2 + j) - 1;
                zt_fx want = s < 0 || s >= T ? 0 : x[s * C + c];
                ok &= cols[t * C * K + c * K + j] == want;
            }
    CHECK(ok, "im2col1d: stride 2, pad 1, column c*K + j, zeros outside (%u steps)", to);
    int32_t wv[C * K];
    for (uint32_t i = 0; i < C * K; i++) wv[i] = ((int32_t) (i % 7) - 3) * (1 << 22); /* frac 24 */
    zt_gain_t w = {wv, 24};
    zt_fx b[C] = {65536, 0, -65536, 3, 0};
    zt_nn_dwconv1d(x, C, T, C, K, 1, &w, b, y, C);
    ok = true;
    for (uint32_t t = 0; t < T; t++)
        for (uint32_t c = 0; c < C; c++) {
            double r = q16d(b[c]);
            for (uint32_t j = 0; j < K; j++) {
                int32_t s = (int32_t) t + (int32_t) j - 1;
                if (s >= 0 && s < T) r += (wv[c * K + j] / 16777216.0) * q16d(x[s * C + c]);
            }
            ok &= fabs(q16d(y[t * C + c]) - r) < 1e-4;
        }
    CHECK(ok, "depthwise conv1d with bias equals the exact sum");
}

typedef struct {
    zt_gguf_t g;
    zt_vision_t v;
    void *arena, *work;
    uint64_t wbytes;
} vis_t;

static void test_vision(void)
{
    vis_t V;
    zt_model_err_t err;
    memset(&err, 0, sizeof err);
    int32_t r = zt_gguf_open(&V.g, VF_GGUF, sizeof VF_GGUF);
    uint64_t ab = r ? 0 : zt_vision_arena_bytes(&V.g);
    V.arena = ab ? malloc(ab) : 0;
    r = V.arena ? zt_vision_load(&V.v, &V.g, V.arena, ab, &err) : -1;
    CHECK(r == 0, "fixture projector loads (%d %s %s)", r, r ? err.what : "", r ? err.name : "");
    if (r) return;
    const zt_vision_cfg_t *c = &V.v.cfg;
    CHECK(c->image_size == 16 && c->patch == 4 && c->n_patch == 16 && c->n_out == VF_NOUT &&
              c->proj_dim == VF_PD && V.v.patch_w.q8 && V.v.patch_w.cols == 64 &&
              V.v.layers[0].wq.raw && V.v.layers[1].w2.q8 && V.v.layers[0].w1.rows == 128,
          "config: 16x16 image, 4x4 patches, 4 outputs of 48; F32 patch rows of 48 padded to "
          "64 and converted; Q8_0 in place; F16 converted; fc1 found under ffn_down");
    V.wbytes = zt_vision_work_bytes(&V.v);
    V.work = malloc(V.wbytes);
    zt_fx out[VF_NOUT * VF_PD], out2[VF_NOUT * VF_PD];
    uint32_t nsat = 99;
    r = zt_vision_encode(&V.v, VF_IMG, out, V.work, V.wbytes, &nsat);
    double emax = 0, se = 0, sr = 0;
    for (uint32_t i = 0; i < VF_NOUT * VF_PD; i++) {
        double d = q16d(out[i]) - VF_EXPECT[i] / 65536.0;
        if (fabs(d) > emax) emax = fabs(d);
        se += d * d;
        sr += pow(VF_EXPECT[i] / 65536.0, 2);
    }
    /* the F32 patch matrix and the F16 fc2 are rounded to 8 bits on load
     * (about 0.4% of each block's largest weight); the rest is Q16 rounding */
    CHECK(r == 0 && nsat == 0 && emax < 0.002 && sqrt(se / sr) < 0.001,
          "encoder output vs float64: max abs err %.4f (< 0.002), relative RMS %.5f (< 0.001), "
          "no saturation",
          emax, sqrt(se / sr));
    zt_vision_encode(&V.v, VF_IMG, out2, V.work, V.wbytes, 0);
    CHECK(!memcmp(out, out2, sizeof out), "a second encode gives the same bits");
    CHECK(zt_vision_encode(&V.v, VF_IMG, out2, V.work, V.wbytes - 4096, 0) == ZT_NN_ESPACE &&
              zt_vision_load(&V.v, &V.g, V.arena, ab - 64, &err) == ZT_NN_ESPACE,
          "too little work memory or arena is refused");
    zt_vision_load(&V.v, &V.g, V.arena, ab, &err);
#ifdef ZT_TEST_SIMD
    const zt_kern_t *ks[8];
    uint32_t nk = zt_simd_list(ks, 8);
    zt_pool_t *pool = zt_pool_new(3);
    for (uint32_t i = 0; i < nk; i++)
        for (int th = 0; th < 2; th++) {
            zt_kern_t kt;
            zt_kern_threaded(&kt, ks[i], th ? pool : 0);
            V.v.kern = &kt;
            memset(out2, 0, sizeof out2);
            zt_vision_encode(&V.v, VF_IMG, out2, V.work, V.wbytes, 0);
            CHECK(!memcmp(out, out2, sizeof out), "kernel set %s%s: the C reference's bits",
                  ks[i]->name, th ? ", 3 threads" : "");
        }
    V.v.kern = 0;
    zt_pool_free(pool);
#endif
    /* a corrupted header field is refused, not run */
    uint8_t *bad = malloc(sizeof VF_GGUF);
    memcpy(bad, VF_GGUF, sizeof VF_GGUF);
    for (uint64_t i = 0; i + 9 < sizeof VF_GGUF; i++)
        if (!memcmp(bad + i, "idefics3", 8)) {
            bad[i] = 'X';
            break;
        }
    zt_gguf_t gb;
    zt_vision_cfg_t cb;
    CHECK(zt_gguf_open(&gb, bad, sizeof VF_GGUF) == 0 &&
              zt_vision_config(&gb, &cb, &err) == ZT_NN_ECONFIG && zt_vision_arena_bytes(&gb) == 0,
          "another projector type is refused (%s)", err.what);
    free(bad);
    free(V.work);
    free(V.arena);
}

static void test_resize(void)
{
    uint8_t dst[VF_S * VF_S * 3];
    zt_vision_resize(VF_RSRC, VF_RW, VF_RH, VF_RW * 3, dst, VF_S);
    CHECK(!memcmp(dst, VF_RDST, sizeof dst), "resize 37x23 -> 16x16: Python's integer area "
                                             "average, byte for byte");
    uint8_t one[3] = {10, 200, 255}, big[8 * 8 * 3];
    zt_vision_resize(one, 1, 1, 3, big, 8);
    bool ok = true;
    for (int i = 0; i < 64; i++)
        ok &= big[3 * i] == 10 && big[3 * i + 1] == 200 && big[3 * i + 2] == 255;
    CHECK(ok, "resize 1x1 -> 8x8 repeats the pixel");
}

int main(void)
{
    printf("=== T23 encoder blocks, T24 image encoder ===\n");
    static uint8_t mem[8 << 20];
    zt_nn_t nn;
    uint64_t need = zt_nn_bytes(64, 512, 512, 64, 4);
    CHECK(need <= sizeof mem && zt_nn_init(&nn, 64, 512, 512, 64, 4, mem, sizeof mem) == 0,
          "zt_nn context fits (%llu bytes)", (unsigned long long) need);
    test_activations();
    test_norms(&nn);
    test_attention(&nn);
    test_conv();
    test_vision();
    test_resize();
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
