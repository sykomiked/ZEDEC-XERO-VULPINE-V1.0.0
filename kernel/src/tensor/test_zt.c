/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zt.c — host tests for the tensor engine. Checks the integer results
 * against double-precision references computed here. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "zt.h"
#include "swarm_hk.h"

static int fails;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}
static int32_t rnd_fx(int32_t range) /* uniform in [-range, range] */
{
    return (int32_t) (rnd() % (uint64_t) (2 * (int64_t) range + 1)) - range;
}
static double fx(int64_t v)
{
    return (double) v / 65536.0;
}
static uint64_t F(uint32_t n)
{
    uint64_t a = 0, b = 1;
    while (n--) {
        uint64_t t = a + b;
        a = b;
        b = t;
    }
    return a;
}

static void test_arith(void)
{
    for (int i = 0; i < 20000; i++) {
        uint64_t n = rnd(), d = rnd() >> (rnd() % 64), r;
        if (!d) d = 1;
        uint64_t q = zt_udiv64(n, d, &r);
        CHECK(q == n / d && r == n % d, "udiv64 %llu/%llu", (unsigned long long) n,
              (unsigned long long) d);
    }
    for (uint64_t v = 0; v < 70000; v++) {
        uint32_t s = zt_isqrt64(v);
        CHECK((uint64_t) s * s <= v && (uint64_t) (s + 1) * (s + 1) > v, "isqrt %llu",
              (unsigned long long) v);
    }
    for (int i = 0; i < 5000; i++) {
        uint64_t v = rnd();
        uint64_t s = zt_isqrt64(v);
        CHECK(s * s <= v && (s + 1) * (s + 1) > v, "isqrt big");
    }
    double phi = (1 + sqrt(5.0)) / 2;
    CHECK(zt_phi_pow(0) == 65536, "phi^0");
    for (int k = -24; k <= 40; k++) {
        double want = pow(phi, k) * 65536.0;
        double got = (double) zt_phi_pow(k);
        CHECK(fabs(got - want) <= 1.0 + want * 1e-9, "phi^%d %f vs %f", k, got, want);
    }
}

static void test_quant(void)
{
    zt_fx x[96], y[96];
    zt_q8_t q[3], g[3];
    for (int t = 0; t < 200; t++) {
        int32_t range = 1 + (int32_t) (rnd() % (1u << (rnd() % 23)));
        for (int i = 0; i < 96; i++) x[i] = rnd_fx(range);
        zt_quantize(x, 96, q, false);
        zt_quantize(x, 96, g, true);
        zt_dequantize(q, 3, y);
        for (int i = 0; i < 96; i++)
            CHECK(abs(y[i] - x[i]) <= q[i / 32].scale / 2 + 1, "dequant %d", i);
        zt_dequantize(g, 3, y);
        for (int b = 0; b < 3; b++)
            if (g[b].scale) CHECK(g[b].scale == zt_phi_pow(g[b].phi_k), "golden scale");
        for (int i = 0; i < 96; i++)
            CHECK(abs(y[i] - x[i]) <= g[i / 32].scale / 2 + 1, "golden dequant %d", i);
        /* dot against the dequantised values */
        zt_dequantize(q, 3, y);
        double want = 0;
        for (int i = 0; i < 96; i++) want += fx(y[i]) * fx(y[i]);
        double got = fx(zt_dot(q, q, 3));
        if (want < 30000) CHECK(fabs(got - want) <= 1e-4 * want + 0.01, "dot %f %f", got, want);
    }
}

static void test_matmul(void)
{
    enum { M = 50, N = 37, NB = 3 };
    static zt_fx av[M * NB * 32], bv[N * NB * 32];
    static zt_q8_t a[M * NB], b[N * NB];
    static zt_fx c[M * N], r[N];
    for (int i = 0; i < M * NB * 32; i++) av[i] = rnd_fx(200000);
    for (int i = 0; i < N * NB * 32; i++) bv[i] = rnd_fx(200000);
    zt_quantize(av, M * NB * 32, a, true);
    zt_quantize(bv, N * NB * 32, b, false);
    zt_matmul_fib(a, M, b, N, NB, c);
    int bad = 0;
    for (int i = 0; i < M; i++) {
        zt_matvec(b, N, a + i * NB, NB, r);
        for (int j = 0; j < N; j++) bad += c[i * N + j] != r[j];
    }
    CHECK(bad == 0, "fib matmul differs from plain product in %d cells", bad);
}

static void test_nonlinear(void)
{
    double worst = 0;
    for (int32_t x = -20 * 65536; x <= 0; x += 97) {
        double e = fabs(fx(zt_exp(x)) - exp(fx(x)));
        if (e > worst) worst = e;
    }
    CHECK(worst < 6e-5, "exp worst error %g", worst);
    zt_fx s[40];
    for (int t = 0; t < 100; t++) {
        for (int i = 0; i < 40; i++) s[i] = rnd_fx(8 * 65536);
        double ref[40], mx = -1e9, sum = 0;
        for (int i = 0; i < 40; i++) mx = fmax(mx, fx(s[i]));
        for (int i = 0; i < 40; i++) sum += ref[i] = exp(fx(s[i]) - mx);
        zt_softmax(s, 40);
        int64_t tot = 0;
        for (int i = 0; i < 40; i++) {
            tot += s[i];
            CHECK(fabs(fx(s[i]) - ref[i] / sum) < 3e-4, "softmax %d", i);
        }
        CHECK(tot <= 65536 && tot >= 65536 - 40, "softmax sum %lld", (long long) tot);
    }
    worst = 0;
    for (int32_t x = -12 * 65536; x <= 12 * 65536; x += 331) {
        double v = fx(x), e = fabs(fx(zt_silu(x)) - v / (1 + exp(-v)));
        if (e > worst) worst = e;
    }
    CHECK(worst < 1e-3, "silu worst error %g", worst);
    zt_fx v[64], gain[64], y[64];
    for (int i = 0; i < 64; i++) v[i] = rnd_fx(5 * 65536), gain[i] = 65536 + rnd_fx(30000);
    zt_rmsnorm(v, gain, 64, y);
    double ms = 0;
    for (int i = 0; i < 64; i++) ms += fx(v[i]) * fx(v[i]);
    double rms = sqrt(ms / 64);
    for (int i = 0; i < 64; i++)
        CHECK(fabs(fx(y[i]) - fx(v[i]) / rms * fx(gain[i])) < 2e-3, "rmsnorm %d", i);
    int load[1024] = {0}, mx = 0;
    for (uint64_t k = 0; k < 1024; k++) load[zt_fib_hash(k, 10)]++;
    for (int i = 0; i < 1024; i++) mx = load[i] > mx ? load[i] : mx;
    CHECK(mx <= 2, "fib hash bucket load %d", mx);
}

static void test_coil(void)
{
    zt_coil_t c;
    CHECK(!zt_coil_init(&c, 4, ZT_WIND_GOLDEN) && !zt_coil_init(&c, 37, ZT_WIND_GOLDEN), "range");
    for (uint32_t base = 5; base <= 16; base++) {
        for (int w = 0; w < 2; w++) {
            CHECK(zt_coil_init(&c, base, (zt_wind_t) w), "init");
            CHECK(c.total == F(base + 11) - F(base + 1), "total identity b=%u", base);
            CHECK(c.core == F(base - 1), "core size");
            unsigned char *seen = calloc(c.total, 1);
            for (uint32_t i = 0; i < c.total; i++) {
                zt_place_t p = zt_coil_place(&c, i);
                CHECK(p.shell < ZT_COIL_SHELLS && p.slot < c.size[p.shell], "place range");
                CHECK(zt_coil_index(&c, p) == i, "roundtrip b=%u w=%d i=%u", base, w, i);
                seen[c.offset[p.shell] + p.slot]++;
            }
            for (uint32_t i = 0; i < c.total; i++) CHECK(seen[i] == 1, "bijection");
            free(seen);
        }
    }
    zt_coil_init(&c, 8, ZT_WIND_GOLDEN);
    for (uint32_t s = 1; s < ZT_COIL_SHELLS; s++) {
        double ratio = (double) c.size[s] / c.size[s - 1];
        CHECK(fabs(ratio - 1.6180339887) < 0.01, "shell ratio %f", ratio);
    }
    /* Golden winding spreads consecutive elements about size/phi apart. */
    zt_place_t p0 = zt_coil_place(&c, c.offset[9]), p1 = zt_coil_place(&c, c.offset[9] + 1);
    CHECK((p1.slot + c.size[9] - p0.slot) % c.size[9] == c.stride[9], "stride");
    for (int t = 0; t < 2000; t++) {
        zt_place_t a = zt_coil_place(&c, (uint32_t) (rnd() % c.total));
        zt_place_t b = zt_coil_place(&c, (uint32_t) (rnd() % c.total));
        uint32_t ab = zt_coil_dist(&c, a, b);
        CHECK(ab == zt_coil_dist(&c, b, a), "dist symmetric");
        CHECK(zt_coil_dist(&c, a, a) == 0, "dist self");
        CHECK(zt_coil_dist(&c, a, zt_coil_parent(&c, a)) == 1, "dist parent");
        CHECK(ab <= 2 * (ZT_COIL_SHELLS + 1), "dist bound");
    }
    zt_place_t root = zt_coil_parent(&c, (zt_place_t){0, 3});
    CHECK(root.shell == ZT_SHELL_CORE && root.slot < c.core, "core parent");
    CHECK(zt_coil_scale(0) == 65536 && zt_coil_scale(1) == 40503, "scale");
}

static void test_field(void)
{
    zt_coil_t c;
    zt_coil_init(&c, 6, ZT_WIND_ABHA);
    zt_fx *v = malloc(c.total * sizeof *v);
    int64_t *f = malloc((c.total + c.core) * sizeof *f);
    for (uint32_t i = 0; i < c.total; i++) v[i] = 65536;
    zt_coil_field(&c, v, f);
    int64_t core = 0;
    for (uint32_t i = 0; i < c.core; i++) core += f[c.total + i];
    /* Each element reaches the core weakened by 1/phi per step: sum over
     * shells of size[s] / phi^(s+1). */
    double want = 0;
    for (uint32_t s = 0; s < ZT_COIL_SHELLS; s++) want += c.size[s] * pow(0.6180339887, s + 1);
    CHECK(fabs(fx(core) - want) < want * 1e-3 + 0.1 * c.total / 65536.0 + 1, "core field %f %f",
          fx(core), want);
    /* AC: conserved exactly, and the positive half gathers charge in the core. */
    for (uint32_t i = 0; i < c.total; i++) f[i] = v[i];
    for (uint32_t i = 0; i < c.core; i++) f[c.total + i] = 0;
    int64_t total = (int64_t) c.total * 65536;
    for (uint32_t ph = 0; ph < 20; ph++) {
        zt_coil_ac(&c, f, ph);
        int64_t s = 0, cs = 0;
        for (uint32_t i = 0; i < c.total + c.core; i++) s += f[i];
        for (uint32_t i = 0; i < c.core; i++) cs += f[c.total + i];
        CHECK(s == total, "AC conservation phase %u", ph);
        if (ph == 0) CHECK(cs > 0, "positive half charges the core");
    }
    free(v);
    free(f);
}

static void test_interfere(void)
{
    CHECK((int) ZT_TRUTH_TRUE == (int) SWARM_HK_TRUE &&
              (int) ZT_TRUTH_FALSE == (int) SWARM_HK_FALSE &&
              (int) ZT_TRUTH_GLUT == (int) SWARM_HK_GLUT &&
              (int) ZT_TRUTH_NEUTRAL == (int) SWARM_HK_NEUTRAL &&
              (int) ZT_TRUTH_PARADOX == (int) SWARM_HK_PARADOX &&
              (int) ZT_TRUTH_UNKNOWN == (int) SWARM_HK_UNKNOWN,
          "truth states match swarm_hk.h");
    zt_coil_t c;
    zt_coil_init(&c, 5, ZT_WIND_GOLDEN);
    uint32_t all = c.total + c.core;
    int64_t *p = calloc(all, sizeof *p), *n = calloc(all, sizeof *n), *x = calloc(all, sizeof *x);
    uint8_t *tr = calloc(all, 1);
    zt_place_t leaf = {9, 7}, mid = {4, 2}, other = {9, 40};
    uint32_t L = c.offset[9] + 7, M = c.offset[4] + 2, O = c.offset[9] + 40;
    p[L] = n[L] = 100; /* a glut on a leaf, with clear ground inward */
    p[O] = 100;        /* a clear truth */
    n[M] = 100;        /* a clear falsehood */
    p[M + 1] = 10;     /* weak evidence */
    zt_coil_interfere(&c, p, n, 50, tr, x);
    CHECK(tr[L] == ZT_TRUTH_GLUT && x[L] == 0, "glut held");
    CHECK(tr[O] == ZT_TRUTH_TRUE && x[O] == 100, "true");
    CHECK(tr[M] == ZT_TRUTH_FALSE && x[M] == -100, "false");
    CHECK(tr[M + 1] == ZT_TRUTH_NEUTRAL && tr[0] == ZT_TRUTH_UNKNOWN, "neutral, unknown");
    /* Glut all the way from the leaf to the core: a paradox to escalate. */
    zt_place_t q = leaf;
    for (;;) {
        uint32_t a = q.shell == ZT_SHELL_CORE ? c.total + q.slot : c.offset[q.shell] + q.slot;
        p[a] = n[a] = 100;
        if (q.shell == ZT_SHELL_CORE) break;
        q = zt_coil_parent(&c, q);
    }
    zt_coil_interfere(&c, p, n, 50, tr, x);
    CHECK(tr[L] == ZT_TRUTH_PARADOX, "paradox escalates");
    (void) mid;
    (void) other;
    free(p);
    free(n);
    free(x);
    free(tr);
}

static void test_holo(void)
{
    zt_coil_t c;
    zt_coil_init(&c, 7, ZT_WIND_GOLDEN);
    int64_t *v = malloc(c.total * sizeof *v), *r = malloc(c.total * sizeof *r),
            *d = malloc(c.total * sizeof *d);
    /* Data shaped by the tree: each place is its parent plus a little. */
    for (uint32_t j = 0; j < c.size[0]; j++) v[j] = rnd_fx(1 << 20);
    for (uint32_t s = 1; s < ZT_COIL_SHELLS; s++)
        for (uint32_t j = 0; j < c.size[s]; j++) {
            zt_place_t p = {s, j}, q = zt_coil_parent(&c, p);
            v[c.offset[s] + j] = v[c.offset[q.shell] + q.slot] + rnd_fx(64);
        }
    zt_holo_encode(&c, v, r);
    zt_holo_decode(&c, r, ZT_COIL_SHELLS, d);
    int bad = 0;
    uint64_t big_v = 0, big_r = 0;
    for (uint32_t i = 0; i < c.total; i++) {
        bad += d[i] != v[i];
        big_v += (uint64_t) llabs(v[i]);
        big_r += (uint64_t) llabs(r[i]);
    }
    CHECK(bad == 0, "holographic decode exact");
    CHECK(big_r * 20 < big_v, "residuals much smaller than data (%llu vs %llu)",
          (unsigned long long) big_r, (unsigned long long) big_v);
    /* Inner shells alone give the coarse whole: every place within 9 * 64. */
    zt_holo_decode(&c, r, 1, d);
    int far = 0;
    for (uint32_t i = 0; i < c.total; i++) far += llabs(d[i] - v[i]) > 9 * 64;
    CHECK(far == 0, "partial decode is a coarse picture of the whole");
    free(v);
    free(r);
    free(d);

    uint32_t w[23], back[23];
    uint8_t fr[5 * ZT_FRAME_OCTETS];
    for (int i = 0; i < 23; i++) w[i] = (uint32_t) rnd();
    for (uint32_t ph = 0; ph < 2; ph++) {
        uint32_t nf = zt_frame_pack(w, 23, ph, fr);
        CHECK(nf == 5, "frames");
        CHECK(zt_frame_unpack(fr, nf, back) == 23, "unpack count");
        int diff = 0;
        for (int i = 0; i < 23; i++) diff += back[i] != w[i];
        CHECK(diff == 0, "frame roundtrip phase %u", ph);
    }
    /* A constant becomes a square wave in the bytes. */
    uint32_t k[5] = {0x11223344u, 0x11223344u, 0x11223344u, 0x11223344u, 0x11223344u};
    zt_frame_pack(k, 5, 0, fr);
    CHECK(fr[1] == 0x44 && fr[5] == 0x11 && fr[9] == 0x44 && fr[13] == 0x11, "alternating");
    fr[7] ^= 0x01;
    CHECK(zt_frame_unpack(fr, 1, back) == -1, "check catches a flipped bit");
}

static void test_fractal(void)
{
    zt_coil_t c;
    zt_coil_init(&c, 5, ZT_WIND_GOLDEN);
    CHECK(zt_fractal_levels(&c, 1) == 1 && zt_fractal_levels(&c, c.total) == 1, "levels 1");
    CHECK(zt_fractal_levels(&c, (uint64_t) c.total + 1) == 2, "levels 2");
    zt_place_t pl[ZT_FRACTAL_MAX];
    uint64_t cap = (uint64_t) c.total * c.total * c.total;
    for (int t = 0; t < 3000; t++) {
        uint64_t i = rnd() % cap;
        CHECK(zt_fractal_place(&c, 3, i, pl), "fits");
        CHECK(zt_fractal_index(&c, 3, pl) == i, "fractal roundtrip");
    }
    CHECK(!zt_fractal_place(&c, 3, cap, pl), "overflow refused");
    zt_device_t phone = {20000, 2, 4}, server = {200000000, 16, 64};
    zt_plan_t a, b;
    CHECK(zt_device_plan(&phone, 1000000000ull, ZT_WIND_ABHA, &a), "plan phone");
    CHECK(zt_device_plan(&server, 1000000000ull, ZT_WIND_ABHA, &b), "plan server");
    CHECK(a.coil.total <= phone.fast_elems && b.coil.total <= server.fast_elems, "fits memory");
    CHECK(a.coil.base < b.coil.base && a.levels > b.levels, "phone deeper, server wider");
    zt_place_t q = zt_coil_place(&a.coil, 5);
    CHECK(zt_coil_bank(&a.coil, q, 2) < 2, "bank");
}

static void test_isf(void)
{
    double worst = 0;
    for (int t = 0; t < 20000; t++) {
        uint64_t x = 1 + (rnd() >> (rnd() % 60));
        double e = fabs(fx(zt_log2(x)) - log2((double) x / 65536.0));
        if (e > worst) worst = e;
    }
    CHECK(worst <= 2.0 / 65536, "log2 worst %g", worst);
    for (uint32_t N = 2; N <= 200; N++) {
        CHECK(zt_surplus_f(0, N) == 0, "S1 f(0)=0");
        CHECK(fabs(fx(zt_surplus_f(ZT_ONE, N)) - log((double) N)) < 1e-4, "S4 f(1)=ln %u", N);
        zt_fx prev = 0;
        for (zt_fx u = 0; u <= ZT_ONE; u += 4096) {
            zt_fx f = zt_surplus_f(u, N);
            CHECK(f >= prev, "monotone");
            CHECK(fabs(fx(f) - log(1 + (N - 1) * fx(u))) < 1e-4, "f(u)");
            prev = f;
        }
    }
    zt_fx a[64], b[64], c[64];
    for (int i = 0; i < 64; i++) a[i] = rnd_fx(3 * 65536), c[i] = -2 * a[i];
    /* b orthogonal to a: b = a rotated within pairs */
    for (int i = 0; i < 64; i += 2) b[i] = -a[i + 1], b[i + 1] = a[i];
    CHECK(zt_surplus_u(a, a, 64) < 16, "parallel u=0");
    CHECK(zt_surplus_u(a, c, 64) < 16, "antiparallel u=0");
    CHECK(zt_surplus_u(a, b, 64) > ZT_ONE - 16, "orthogonal u=1");
    for (int t = 0; t < 200; t++) {
        for (int i = 0; i < 64; i++) b[i] = rnd_fx(65536 << (t % 10));
        double d = 0, na = 0, nb = 0;
        for (int i = 0; i < 64; i++)
            d += fx(a[i]) * fx(b[i]), na += fx(a[i]) * fx(a[i]), nb += fx(b[i]) * fx(b[i]);
        CHECK(fabs(fx(zt_surplus_u(a, b, 64)) - (1 - d * d / (na * nb))) < 2e-3, "u");
    }
    /* Gate: rows 0, 1 differ; row 2 repeats row 0, row 3 is row 1 scaled. */
    zt_fx rows[4 * 64];
    for (int i = 0; i < 64; i++) {
        rows[i] = a[i];
        rows[64 + i] = (i & 1) ? a[i - 1] : -a[i + 1];
        rows[128 + i] = a[i];
        rows[192 + i] = 3 * rows[64 + i];
    }
    bool keep[4];
    zt_fx tot;
    uint32_t k = zt_surplus_gate(rows, 4, 64, 8, 65536 / 4, keep, &tot);
    CHECK(k == 2 && keep[0] && keep[1] && !keep[2] && !keep[3], "gate keeps the 2 distinct rows");
    CHECK(fabs(fx(tot) - log(8.0)) < 1e-3, "gate surplus ln 8");
    zt_isf_t s;
    zt_isf_init(&s, 10 * 65536, 65536 / 10, 65536, 13);
    for (int t = 0; t < 400; t++) {
        zt_fx S = zt_surplus_f((zt_fx) (rnd() % 65537), 13), C = (zt_fx) (rnd() % 20000);
        int64_t before = s.Q;
        bool grow = zt_isf_can_grow(&s, S, C);
        int64_t after = zt_isf_step(&s, S, C);
        CHECK(grow == (after > before) || after == 0, "growth criterion");
        CHECK(after <= zt_isf_ceiling(&s) + 2, "ceiling");
    }
    CHECK(fabs(fx(zt_isf_ceiling(&(zt_isf_t){0, 65536 / 10, 65536, 13})) - 10 * log(13.0)) < 1e-2,
          "ceiling = eta ln N / delta");
}

int main(void)
{
    test_arith();
    test_quant();
    test_matmul();
    test_nonlinear();
    test_coil();
    test_field();
    test_interfere();
    test_holo();
    test_fractal();
    test_isf();
    if (fails) {
        printf("test_zt: %d failures\n", fails);
        return 1;
    }
    printf("test_zt: all checks passed\n");
    return 0;
}
