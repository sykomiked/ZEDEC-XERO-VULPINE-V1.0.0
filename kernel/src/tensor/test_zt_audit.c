/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zt_audit.c — regression tests for defects found in an audit of the
 * tensor engine, one block per defect, plus the per-shell residual tap. Each
 * block failed before its fix (some only under -fsanitize=undefined, which
 * the verify line uses). Build and run from kernel/:
 *   gcc -std=c11 -O2 -Wall -Werror -Wextra -fsanitize=address,undefined
 *       -fno-sanitize-recover=all -DTEST_HOST -Isrc/tensor src/tensor/test_zt_audit.c
 *       src/tensor/zt.c src/tensor/zt_coil.c src/tensor/zt_isf.c src/tensor/zt_holo.c
 *       src/tensor/zt_lattice.c src/tensor/zt_gguf.c -lm -o /tmp/test_zt_audit
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zt.h"
#include "zt_gguf.h"
#include "zt_lattice.h"

static int fails, checks;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static uint64_t rng = 0x2545F4914F6CDD1Dull;
static uint64_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}
static int32_t rnd_fx(int32_t range)
{
    return (int32_t) (rnd() % (uint64_t) (2 * (int64_t) range + 1)) - range;
}

/* round(phi^k * 2^16), k = -24..40, from 100-digit decimal arithmetic. */
static const int64_t PHI_POW_REF[65] = {
    1ll,
    1ll,
    2ll,
    3ll,
    4ll,
    7ll,
    11ll,
    18ll,
    30ll,
    48ll,
    78ll,
    126ll,
    204ll,
    329ll,
    533ll,
    862ll,
    1395ll,
    2257ll,
    3652ll,
    5909ll,
    9562ll,
    15471ll,
    25033ll,
    40503ll,
    65536ll,
    106039ll,
    171575ll,
    277615ll,
    449190ll,
    726805ll,
    1175996ll,
    1902801ll,
    3078797ll,
    4981598ll,
    8060395ll,
    13041993ll,
    21102388ll,
    34144382ll,
    55246770ll,
    89391152ll,
    144637922ll,
    234029074ll,
    378666997ll,
    612696071ll,
    991363068ll,
    1604059139ll,
    2595422206ll,
    4199481345ll,
    6794903551ll,
    10994384896ll,
    17789288448ll,
    28783673344ll,
    46572961792ll,
    75356635136ll,
    121929596928ll,
    197286232064ll,
    319215828992ll,
    516502061056ll,
    835717890048ll,
    1352219951104ll,
    2187937841152ll,
    3540157792256ll,
    5728095633408ll,
    9268253425664ll,
    14996349059072ll,
};

/* Bug 1: zt_phi_pow drifted from the correctly rounded value for k >= 26
 * (776 units low at k = 40), because phi was held to 32 fraction bits. */
static void test_phi_pow_exact(void)
{
    int bad = 0;
    for (int k = -24; k <= 40; k++) {
        int64_t got = zt_phi_pow(k);
        if (got != PHI_POW_REF[k + 24]) {
            if (bad < 4)
                printf("  phi^%d = %lld, want %lld\n", k, (long long) got,
                       (long long) PHI_POW_REF[k + 24]);
            bad++;
        }
    }
    CHECK(bad == 0, "zt_phi_pow: %d of 65 powers not correctly rounded", bad);
    CHECK(zt_phi_pow(-25) == 0 && zt_phi_pow(41) == 0, "phi_pow range");
}

/* Bugs 2 and 3: zt_quantize overflowed (abs(INT32_MIN), amax + 126) and
 * zt_dequantize truncated q * scale to 32 bits instead of saturating. */
static void test_quant_extremes(void)
{
    zt_fx x[32], y[32];
    zt_q8_t q;
    for (int g = 0; g < 2; g++) {
        for (int i = 0; i < 32; i++) x[i] = 0;
        x[0] = INT32_MAX;
        x[1] = INT32_MIN;
        x[2] = INT32_MAX - 10;
        x[3] = 12345;
        zt_quantize(x, 32, &q, g != 0);
        CHECK(q.scale > 0, "scale positive (golden %d): %d", g, q.scale);
        zt_dequantize(&q, 1, y);
        CHECK((int64_t) y[0] > (int64_t) INT32_MAX - q.scale, "INT32_MAX survives (golden %d): %d",
              g, y[0]);
        CHECK((int64_t) y[1] < (int64_t) INT32_MIN + q.scale, "INT32_MIN survives (golden %d): %d",
              g, y[1]);
        CHECK(y[2] > 0 && (int64_t) y[2] >= (int64_t) x[2] - q.scale,
              "near-max stays positive (golden %d): %d", g, y[2]);
    }
    /* A block whose q * scale exceeds 32 bits saturates. */
    zt_q8_t b = {0};
    b.scale = 1 << 30;
    b.q[0] = 127;
    b.q[1] = -128;
    b.q[2] = 1;
    zt_dequantize(&b, 1, y);
    CHECK(y[0] == INT32_MAX && y[1] == INT32_MIN && y[2] == (1 << 30), "dequant saturates");
}

/* Bug 4: zt_dot wrapped (and changed sign) when the exact block product
 * passed 2^63, and read a negative scale as a huge unsigned one. */
static void test_dot_saturates(void)
{
    zt_q8_t a = {0}, b = {0};
    a.scale = b.scale = INT32_MAX;
    for (int i = 0; i < 32; i++) a.q[i] = b.q[i] = 127;
    CHECK(zt_dot(&a, &b, 1) == INT32_MAX, "dot of huge blocks saturates high: %d",
          zt_dot(&a, &b, 1));
    for (int i = 0; i < 32; i++) b.q[i] = -127;
    CHECK(zt_dot(&a, &b, 1) == INT32_MIN, "dot of huge blocks saturates low: %d",
          zt_dot(&a, &b, 1));
    /* a negative scale is a sign, not 2^32 minus something */
    zt_q8_t c = {0}, d = {0};
    c.scale = -65536;
    d.scale = 65536;
    c.q[0] = 3;
    d.q[0] = 5;
    CHECK(zt_dot(&c, &d, 1) == -15 * 65536, "negative scale: %d", zt_dot(&c, &d, 1));
    /* ordinary values unchanged */
    c.scale = 1000;
    d.scale = 3000;
    c.q[1] = -7;
    d.q[1] = 9;
    int64_t want = ((int64_t) 15 * 1000 * 3000) / 65536 - ((int64_t) 63 * 1000 * 3000) / 65536;
    CHECK(llabs(zt_dot(&c, &d, 1) - want) <= 1, "ordinary dot %d vs %lld", zt_dot(&c, &d, 1),
          (long long) want);
}

/* Bug 5: zt_rmsnorm truncated every x^2 to Q16 before summing, so small
 * activations (|x| < 2^-8) read as zero and were scaled up 65536-fold; and
 * 1/rms was held in Q16, so large activations lost precision. */
static void test_rmsnorm_precision(void)
{
    zt_fx x[64], g[64], y[64];
    for (int i = 0; i < 64; i++) x[i] = (i & 1) ? -100 : 100, g[i] = 65536;
    zt_rmsnorm(x, g, 64, y);
    CHECK(abs(y[0] - 65536) <= 4 && abs(y[1] + 65536) <= 4, "small activations: %d %d", y[0], y[1]);
    double worst = 0;
    for (int t = 0; t < 50; t++) {
        int32_t range = 1 + (int32_t) (rnd() % (1u << (rnd() % 31)));
        double ms = 0;
        for (int i = 0; i < 64; i++) {
            x[i] = rnd_fx(range);
            g[i] = 65536 + rnd_fx(30000);
            ms += (double) x[i] * x[i];
        }
        if (ms == 0) continue;
        double rms = sqrt(ms / 64);
        zt_rmsnorm(x, g, 64, y);
        for (int i = 0; i < 64; i++) {
            double want = x[i] / rms * g[i];
            double e = fabs(y[i] - want) / 65536.0;
            if (e > worst) worst = e;
        }
    }
    CHECK(worst < 1e-4, "rmsnorm worst error %g over 31 octaves of scale", worst);
    for (int i = 0; i < 64; i++) x[i] = 0;
    zt_rmsnorm(x, g, 64, y);
    CHECK(y[0] == 0 && y[63] == 0, "zero vector stays zero");
}

/* Bug 6: zt_e8_rms had the same per-element truncation. */
static void test_e8_rms_precision(void)
{
    zt_fx x[32];
    for (int i = 0; i < 32; i++) x[i] = (i % 3) ? 100 : -100;
    CHECK(abs(zt_e8_rms(x, 32) - 100) <= 1, "e8 rms of +-100: %d", zt_e8_rms(x, 32));
    for (int i = 0; i < 32; i++) x[i] = INT32_MIN;
    CHECK(zt_e8_rms(x, 32) == INT32_MAX, "e8 rms saturates: %d", zt_e8_rms(x, 32));
}

/* Bug 7: zt_surplus_u truncated products to Q16, so two small orthogonal
 * vectors read as zero vectors (u = 0) although u = 1 - cos^2 is scale-free. */
static void test_surplus_scale_free(void)
{
    zt_fx a[16] = {0}, b[16] = {0};
    for (int i = 0; i < 16; i += 2) a[i] = 100, b[i + 1] = 100;
    CHECK(zt_surplus_u(a, b, 16) > ZT_ONE - 16, "small orthogonal vectors: u = %d",
          zt_surplus_u(a, b, 16));
    CHECK(zt_surplus_u(a, a, 16) < 16, "small parallel vectors: u = %d", zt_surplus_u(a, a, 16));
    zt_fx z[16] = {0};
    CHECK(zt_surplus_u(a, z, 16) == 0, "zero vector adds nothing");
    for (int i = 0; i < 16; i++) a[i] = INT32_MIN, b[i] = (i & 1) ? INT32_MAX : INT32_MIN;
    CHECK(zt_surplus_u(a, b, 16) > ZT_ONE - 16, "extreme orthogonal vectors: u = %d",
          zt_surplus_u(a, b, 16));
}

/* Bug 8: the coil field and the AC half-cycles rounded negative values with
 * an arithmetic right shift (floor, and implementation-defined in C11), so
 * the field of -v was not -(field of v). */
static void test_field_odd(void)
{
    zt_coil_t c;
    zt_coil_init(&c, 6, ZT_WIND_GOLDEN);
    zt_fx *v = malloc(c.total * sizeof *v), *w = malloc(c.total * sizeof *w);
    int64_t *f = malloc((c.total + c.core) * sizeof *f),
            *h = malloc((c.total + c.core) * sizeof *h);
    for (uint32_t i = 0; i < c.total; i++) v[i] = rnd_fx(1000), w[i] = -v[i];
    zt_coil_field(&c, v, f);
    zt_coil_field(&c, w, h);
    int odd = 0;
    for (uint32_t i = 0; i < c.total + c.core; i++) odd += f[i] != -h[i];
    CHECK(odd == 0, "field(-v) == -field(v) at %d places", odd);
    for (uint32_t ph = 0; ph < 6; ph++) {
        zt_coil_ac(&c, f, ph);
        zt_coil_ac(&c, h, ph);
    }
    odd = 0;
    for (uint32_t i = 0; i < c.total + c.core; i++) odd += f[i] != -h[i];
    CHECK(odd == 0, "AC keeps the field odd at %d places", odd);
    free(v);
    free(w);
    free(f);
    free(h);
}

/* Bug 9: zt_holo_encode/decode overflowed int64 (undefined) for values far
 * apart; the code is now exact modulo 2^64, so every int64 round-trips. */
static void test_holo_extremes(void)
{
    zt_coil_t c;
    zt_coil_init(&c, 5, ZT_WIND_ABHA);
    int64_t *v = malloc(c.total * sizeof *v), *r = malloc(c.total * sizeof *r),
            *d = malloc(c.total * sizeof *d);
    for (uint32_t i = 0; i < c.total; i++)
        v[i] = (i % 3 == 0) ? INT64_MAX - (int64_t) i : (i % 3 == 1) ? INT64_MIN + (int64_t) i : 0;
    zt_holo_encode(&c, v, r);
    zt_holo_decode(&c, r, ZT_COIL_SHELLS, d);
    int bad = 0;
    for (uint32_t i = 0; i < c.total; i++) bad += d[i] != v[i];
    CHECK(bad == 0, "extreme values round-trip (%d differ)", bad);
    free(v);
    free(r);
    free(d);
}

/* Bug 10: zt_coil_interfere negated INT64_MIN and subtracted without a
 * bound (both undefined). */
static void test_interfere_extremes(void)
{
    zt_coil_t c;
    zt_coil_init(&c, 5, ZT_WIND_GOLDEN);
    uint32_t all = c.total + c.core;
    int64_t *p = calloc(all, sizeof *p), *n = calloc(all, sizeof *n), *x = calloc(all, sizeof *x);
    uint8_t *tr = calloc(all, 1);
    p[0] = INT64_MIN;
    n[0] = 1;
    p[1] = INT64_MAX;
    n[1] = -1;
    zt_coil_interfere(&c, p, n, 50, tr, x);
    CHECK(tr[0] == ZT_COIL_TRUE && x[0] == INT64_MIN, "INT64_MIN evidence: %u %lld", tr[0],
          (long long) x[0]);
    CHECK(tr[1] == ZT_COIL_TRUE && x[1] == INT64_MAX, "INT64_MAX evidence saturates");
    free(p);
    free(n);
    free(x);
    free(tr);
}

/* Bug 11: Q4_K decoding shifted negative int64 values left (undefined) when
 * a block's d or dmin is negative. */
static void test_q4k_negative_scales(void)
{
    static uint8_t blk[144];
    memset(blk, 0, sizeof blk);
    blk[0] = 0x00, blk[1] = 0xBC; /* d = -1.0 */
    blk[2] = 0x00, blk[3] = 0xB8; /* dmin = -0.5 */
    for (int i = 0; i < 12; i++) blk[4 + i] = 0x01 | (i < 4 ? 0 : 0x10);
    for (int i = 0; i < 128; i++) blk[16 + i] = (uint8_t) (i * 37);
    zt_gguf_tensor_t t = {0};
    t.type = ZT_GGML_Q4_K;
    t.n_dims = 1;
    t.dims[0] = t.n_elems = 256;
    t.n_bytes = 144;
    t.data = blk;
    static zt_fx out[256];
    CHECK(zt_gguf_dequant(&t, 0, 256, out) == ZT_GGUF_OK, "q4_k decodes");
    /* reference: value = d * sc * q - dmin * m, with ggml's 6-bit unpacking */
    int bad = 0;
    for (int j = 0, is = 0; j < 256; j += 64, is += 2) {
        int sc[2], mn[2];
        for (int u = 0; u < 2; u++) {
            int k = is + u;
            const uint8_t *q = blk + 4;
            if (k < 4)
                sc[u] = q[k] & 63, mn[u] = q[k + 4] & 63;
            else
                sc[u] = (q[k + 4] & 0xF) | ((q[k - 4] >> 6) << 4),
                mn[u] = (q[k + 4] >> 4) | ((q[k] >> 6) << 4);
        }
        const uint8_t *qs = blk + 16 + (j / 64) * 32;
        for (int l = 0; l < 32; l++) {
            double v0 = -1.0 * sc[0] * (qs[l] & 0xF) + 0.5 * mn[0];
            double v1 = -1.0 * sc[1] * (qs[l] >> 4) + 0.5 * mn[1];
            bad += fabs(out[j + l] - v0 * 65536) > 1 || fabs(out[j + 32 + l] - v1 * 65536) > 1;
        }
    }
    CHECK(bad == 0, "q4_k with negative scales: %d values wrong", bad);
}

/* Bug 12: zt_gguf_to_q8 clamped the scale shift at 19, so a Q8_0 block with
 * d >= 16384 converted to a scale 2 or 4 times too small, disagreeing with
 * zt_gguf_dequant. */
static void test_q8_0_big_scale(void)
{
    static uint8_t blk[34 * 2];
    memset(blk, 0, sizeof blk);
    blk[0] = 0x00, blk[1] = 0x74; /* d = 16384 */
    blk[2] = 1;
    blk[3] = 0xFF; /* -1 */
    blk[4] = 0x7F;
    blk[34] = 0x00, blk[35] = 0x7B; /* d = 57344 */
    blk[36] = 1;
    zt_gguf_tensor_t t = {0};
    t.type = ZT_GGML_Q8_0;
    t.n_dims = 1;
    t.dims[0] = t.n_elems = 64;
    t.n_bytes = 68;
    t.data = blk;
    static zt_fx ref[64], got[64];
    zt_q8_t q[2];
    CHECK(zt_gguf_dequant(&t, 0, 64, ref) == ZT_GGUF_OK, "q8_0 decodes");
    CHECK(zt_gguf_to_q8(&t, 0, 64, q, 0) == ZT_GGUF_OK, "q8_0 converts");
    zt_dequantize(q, 2, got);
    int bad = 0;
    for (int i = 0; i < 64; i++) bad += llabs((int64_t) got[i] - ref[i]) > 1;
    CHECK(bad == 0 && ref[0] == (1 << 30), "to_q8 agrees with dequant for big d (%d differ, %d)",
          bad, got[0]);
}

/* The per-shell residual tap (zt.h). */
static void test_shell_tap(void)
{
    zt_coil_t c;
    zt_coil_init(&c, 7, ZT_WIND_GOLDEN);
    int64_t *v = malloc(c.total * sizeof *v), *r0 = malloc(c.total * sizeof *r0),
            *r1 = malloc(c.total * sizeof *r1), *ref = malloc(c.total * sizeof *ref);
    for (int pass = 0; pass < 2; pass++) {
        for (uint32_t i = 0; i < c.total; i++)
            v[i] = pass ? (int64_t) (rnd() >> 1) * ((rnd() & 1) ? 1 : -1) : rnd_fx(1 << 24);
        /* reference residuals: the documented rule, written out here */
        int64_t want[10] = {0};
        for (uint32_t s = 0; s < ZT_COIL_SHELLS; s++)
            for (uint32_t j = 0; j < c.size[s]; j++) {
                uint32_t a = c.offset[s] + j;
                int64_t pred = 0;
                if (s) {
                    zt_place_t q = zt_coil_parent(&c, (zt_place_t){s, j});
                    pred = v[c.offset[q.shell] + q.slot];
                }
                ref[a] = (int64_t) ((uint64_t) v[a] - (uint64_t) pred);
                int64_t d = ref[a];
                int64_t sh = d >= 0 ? d / 256 : -((-(d + 1)) / 256) - 1; /* floor(d / 256) */
                want[s] += sh;
                if (want[s] > INT32_MAX) want[s] = INT32_MAX;
                if (want[s] < INT32_MIN) want[s] = INT32_MIN;
            }
        zt_set_shell_tap(NULL);
        zt_holo_encode(&c, v, r0);
        zt_shell_tap_t tap;
        memset(&tap, 0, sizeof tap);
        zt_set_shell_tap(&tap);
        zt_holo_encode(&c, v, r1);
        zt_set_shell_tap(NULL);
        int same = 0, refd = 0;
        for (uint32_t i = 0; i < c.total; i++) same += r0[i] != r1[i], refd += r0[i] != ref[i];
        CHECK(same == 0 && refd == 0, "tap leaves residuals bit-identical (%d, %d)", same, refd);
        int sums = 0;
        for (int s = 0; s < 10; s++) sums += tap.acc[s] != want[s];
        CHECK(sums == 0, "tap sums match the reference (pass %d)", pass);
        if (pass) {
            int sat = 0;
            for (int s = 0; s < 10; s++) sat += tap.acc[s] == INT32_MAX || tap.acc[s] == INT32_MIN;
            CHECK(sat > 0, "huge residuals saturate the tap");
        }
        /* disabled again: the tap is not touched */
        zt_shell_tap_t before = tap;
        zt_holo_encode(&c, v, r1);
        CHECK(memcmp(&before, &tap, sizeof tap) == 0, "NULL disables the tap");
    }
    free(v);
    free(r0);
    free(r1);
    free(ref);
}

int main(void)
{
    test_phi_pow_exact();
    test_quant_extremes();
    test_dot_saturates();
    test_rmsnorm_precision();
    test_e8_rms_precision();
    test_surplus_scale_free();
    test_field_odd();
    test_holo_extremes();
    test_interfere_extremes();
    test_q4k_negative_scales();
    test_q8_0_big_scale();
    test_shell_tap();
    printf("test_zt_audit: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
