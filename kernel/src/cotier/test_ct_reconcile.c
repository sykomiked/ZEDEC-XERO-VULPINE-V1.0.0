/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_ct_reconcile.c — tier-3 reconciliation (ct_reconcile.h): float-bit
 * ingestion against a float64 reference (all FP16 and BF16 patterns, FP32
 * by exponent sweep and random patterns; NaN, Inf, subnormals), the
 * two-source decomposition against float64 and against surplus.h's
 * Theorem 4.1 (TEST_HOST build of surplus.c), and the paraconsistent drift
 * verdicts. Pass "full" to check all 2^32 FP32 patterns.
 *
 *   gcc -std=c11 -O2 -Wall -Wextra -Werror -DTEST_HOST -Isrc/tensor -Isrc/surplus -Isrc/cotier \
 *       src/cotier/test_ct_reconcile.c src/cotier/ct_reconcile.c src/surplus/surplus.c \
 *       src/tensor/zt.c src/tensor/zt_isf.c -lm -o /tmp/test_ct_reconcile
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ct_reconcile.h"
#include "surplus.h"

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

static uint64_t rng = 0xC0FFEE;
static uint64_t next(void)
{
    uint64_t z = (rng += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
static double unif(void)
{
    return (next() >> 11) * (1.0 / 9007199254740992.0) * 2 - 1;
}

/* ---- float64 references ---- */

static double h2d(uint16_t h)
{
    int e = (h >> 10) & 31, m = h & 1023;
    double v = e == 0 ? ldexp(m, -24) : e == 31 ? (m ? NAN : INFINITY) : ldexp(1024 + m, e - 25);
    return (h >> 15) ? -v : v;
}
static double bf2d(uint16_t h)
{
    uint32_t b = (uint32_t) h << 16;
    float f;
    memcpy(&f, &b, 4);
    return f;
}
static double f2d(uint32_t b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

/* expected Q16 and flags for a value v (sub = the input was subnormal) */
static zt_fx expect(double v, bool sub, uint32_t *fl)
{
    *fl = sub ? CT_ING_SUB : 0;
    if (isnan(v)) {
        *fl = CT_ING_NAN;
        return 0;
    }
    if (isinf(v)) {
        *fl = CT_ING_INF;
        return v > 0 ? INT32_MAX : INT32_MIN;
    }
    double x = v * 65536.0, r = nearbyint(x);
    if (r > INT32_MAX) {
        *fl |= CT_ING_SAT;
        return INT32_MAX;
    }
    if (r < INT32_MIN) {
        *fl |= CT_ING_SAT;
        return INT32_MIN;
    }
    if (r != x) *fl |= CT_ING_INEXACT;
    if (v != 0 && r == 0) *fl |= CT_ING_FLUSH;
    return (zt_fx) r;
}

static uint64_t check_f32(uint32_t b)
{
    uint32_t fl, efl;
    zt_fx got = ct_f32_to_q16(b, &fl);
    zt_fx want = expect(f2d(b), ((b >> 23) & 0xFF) == 0 && (b & 0x7FFFFF), &efl);
    return got != want || fl != efl;
}

/* double -> FP16 bits, nearest even (the simulated accelerator's output) */
static uint16_t d2h(double v)
{
    uint16_t s = signbit(v) ? 0x8000 : 0;
    double a = fabs(v);
    if (isnan(v)) return 0x7E00;
    if (a >= 65520.0) return s | 0x7C00;
    if (a < ldexp(1, -14)) return s | (uint16_t) nearbyint(a * ldexp(1, 24));
    int ex;
    frexp(a, &ex);
    int E = ex - 1;
    double m = nearbyint(ldexp(a, 10 - E));
    if (m >= 2048) {
        m = 1024;
        E++;
    }
    if (E > 15) return s | 0x7C00;
    return s | (uint16_t) (((E + 15) << 10) | ((int) m - 1024));
}

/* ---- decomposition reference ---- */

static void ref_decomp(const zt_fx *r, const zt_fx *a, uint32_t n, const uint8_t *cat,
                       uint32_t ncat, double *uc, double *ud)
{
    double nr = 0, na = 0, dot = 0, er[64] = {0}, ea[64] = {0};
    for (uint32_t i = 0; i < n; i++) {
        nr += (double) r[i] * r[i];
        na += (double) a[i] * a[i];
        dot += (double) r[i] * a[i];
        er[cat[i]] += (double) r[i] * r[i];
        ea[cat[i]] += (double) a[i] * a[i];
    }
    double al = 0;
    for (uint32_t c = 0; c < ncat; c++) al += sqrt(er[c] * ea[c]);
    al /= sqrt(nr * na);
    double cs = dot / sqrt(nr * na);
    *uc = 1 - al * al;
    *ud = al * al - cs * cs;
}

#define NV 64u

int main(int argc, char **argv)
{
    bool full = argc > 1 && !strcmp(argv[1], "full");
    uint32_t fl;

    /* ---- R1 ingest ---- */
    uint64_t bad = 0;
    for (uint32_t b = 0; b < 65536; b++) {
        uint32_t efl, e = (b >> 10) & 31;
        zt_fx got = ct_f16_to_q16((uint16_t) b, &fl);
        zt_fx want = expect(h2d((uint16_t) b), e == 0 && (b & 1023), &efl);
        bad += got != want || fl != efl;
    }
    CHECK(bad == 0, "FP16: all 65536 patterns match float64 (value and flags), %llu bad",
          (unsigned long long) bad);
    bad = 0;
    for (uint32_t b = 0; b < 65536; b++) {
        uint32_t efl, e = (b >> 7) & 255;
        zt_fx got = ct_bf16_to_q16((uint16_t) b, &fl);
        zt_fx want = expect(bf2d((uint16_t) b), e == 0 && (b & 127), &efl);
        bad += got != want || fl != efl;
    }
    CHECK(bad == 0, "BF16: all 65536 patterns match float64, %llu bad", (unsigned long long) bad);
    bad = 0;
    uint64_t tried = 0;
    static const uint32_t mant_edge[] = {0, 1, 2, 0x3FFFFF, 0x400000, 0x400001, 0x7FFFFE, 0x7FFFFF};
    for (uint32_t s = 0; s < 2; s++)
        for (uint32_t e = 0; e < 256; e++) {
            for (uint32_t j = 0; j < 8; j++, tried++)
                bad += check_f32(s << 31 | e << 23 | mant_edge[j]);
            for (uint32_t j = 0; j < 4096; j++, tried++)
                bad += check_f32(s << 31 | e << 23 | (uint32_t) (next() & 0x7FFFFF));
        }
    for (uint32_t j = 0; j < 4000000; j++, tried++) bad += check_f32((uint32_t) next());
    /* the Q16.16 boundaries: around 32768, -32768 and half a unit */
    static const float edges[] = {32767.99998f,
                                  32768.0f,
                                  -32768.0f,
                                  -32768.004f,
                                  32767.998f,
                                  7.62939453125e-06f,
                                  7.62939453125e-06f * 1.0000001f,
                                  -7.62939453125e-06f,
                                  2.2888183593750e-05f,
                                  1e-30f,
                                  -0.0f};
    for (uint32_t j = 0; j < sizeof edges / sizeof *edges; j++, tried++) {
        uint32_t b;
        memcpy(&b, &edges[j], 4);
        bad += check_f32(b);
    }
    if (full) {
        uint32_t b = 0;
        do {
            bad += check_f32(b);
            tried++;
        } while (++b != 0);
    }
    CHECK(bad == 0, "FP32: %llu patterns (every exponent, edges, random%s) match float64",
          (unsigned long long) tried, full ? ", all 2^32" : "");
    CHECK(ct_f32_to_q16(0x7FC00000u, &fl) == 0 && fl == CT_ING_NAN, "FP32 quiet NaN -> 0, flagged");
    CHECK(ct_f32_to_q16(0xFF800000u, &fl) == INT32_MIN && fl == CT_ING_INF, "FP32 -Inf saturates");
    CHECK(ct_f16_to_q16(0x0001, &fl) == 0 && fl == (CT_ING_SUB | CT_ING_FLUSH | CT_ING_INEXACT),
          "FP16 smallest subnormal flushes to 0 and says so");
    CHECK(ct_f16_to_q16(0x0200, &fl) == 2 && fl == CT_ING_SUB,
          "FP16 subnormal 2^-15 is exactly 2 Q16 units");

    {
        static uint16_t all[65536];
        static zt_fx out[65536];
        static uint8_t fla[65536];
        ct_ingest_stats_t st;
        memset(&st, 0, sizeof st);
        for (uint32_t b = 0; b < 65536; b++) all[b] = (uint16_t) b;
        int64_t faults = ct_ingest(CT_FMT_F16, all, 0, 65536, out, fla, &st);
        CHECK(faults == 2048 && st.nan == 2046 && st.pos_inf == 1 && st.neg_inf == 1 &&
                  st.subnormal == 2046 && st.n == 65536,
              "ingest stats over all FP16: %lld faults, %llu NaN, %llu+%llu Inf, %llu subnormal, "
              "%llu saturated, %llu flushed",
              (long long) faults, (unsigned long long) st.nan, (unsigned long long) st.pos_inf,
              (unsigned long long) st.neg_inf, (unsigned long long) st.subnormal,
              (unsigned long long) st.sat, (unsigned long long) st.flushed);
        CHECK(ct_ingest(7, all, 0, 4, out, 0, 0) == -1 &&
                  ct_ingest(CT_FMT_F32, all, 0, 4, out, 0, 0) == -1,
              "ingest: bad format or missing source -> -1");
    }

    /* ---- R2 decomposition ---- */
    static zt_fx r[NV], a[NV];
    static uint8_t cat[NV];
    double worst = 0;
    uint64_t incons = 0;
    for (uint32_t it = 0; it < 20000; it++) {
        uint32_t ncat = 2 + (uint32_t) (next() % 7);
        double scale = ldexp(1.0, (int) (next() % 30) - 8);
        for (uint32_t i = 0; i < NV; i++) {
            cat[i] = (uint8_t) (next() % ncat);
            r[i] = (zt_fx) (unif() * scale * 65536 / 8);
            double mixw = (it % 3) * 0.4;
            a[i] = (zt_fx) (((1 - mixw) * r[i] + mixw * unif() * scale * 65536 / 8));
        }
        ct_decomp_t d;
        if (ct_decompose(r, a, NV, cat, ncat, ncat, &d) != 0) continue;
        double uc, ud;
        ref_decomp(r, a, NV, cat, ncat, &uc, &ud);
        double e1 = fabs(d.u_cross / 65536.0 - uc), e2 = fabs(d.u_div / 65536.0 - ud);
        if (e1 > worst) worst = e1;
        if (e2 > worst) worst = e2;
        incons += !d.consistent;
    }
    CHECK(worst < 2e-4, "decomposition vs float64 over 20000 random pairs: worst error %.2g",
          worst);
    CHECK(incons == 0, "Theorem 4.1 identity and f bounds hold in integers (%llu inconsistent)",
          (unsigned long long) incons);

    /* pure block x: matches surplus_decompose (Paper A, Theorem 4.1) */
    {
        double wf = 0;
        for (uint32_t it = 0; it < 500; it++) {
            uint32_t ncat = 4, N = 4;
            for (uint32_t i = 0; i < NV; i++) {
                cat[i] = (uint8_t) (i % ncat);
                r[i] = cat[i] == 0 ? (zt_fx) (unif() * 65536 * 4) : 0;
                a[i] = (zt_fx) (unif() * 65536 * 4);
            }
            double ny = 0, npar = 0, nx = 0, dp = 0;
            for (uint32_t i = 0; i < NV; i++) {
                ny += (double) a[i] * a[i];
                if (!cat[i]) {
                    npar += (double) a[i] * a[i];
                    nx += (double) r[i] * r[i];
                    dp += (double) r[i] * a[i];
                }
            }
            double alpha = sqrt(npar / ny), beta = sqrt(1 - npar / ny);
            double cg = dp / sqrt(nx * npar), sg = sqrt(fmax(0, 1 - cg * cg));
            surplus_decomp_t sd;
            surplus_decompose(&sd, alpha, beta, sg, N);
            ct_decomp_t d;
            ct_decompose(r, a, NV, cat, ncat, N, &d);
            double e[3] = {fabs(d.f_cross / 65536.0 - sd.f_cross),
                           fabs(d.f_div / 65536.0 - sd.f_div),
                           fabs(d.f_total / 65536.0 - sd.f_total)};
            for (int j = 0; j < 3; j++)
                if (e[j] > wf) wf = e[j];
        }
        CHECK(wf < 1e-3,
              "pure block vector: f_cross, f_div, f(u) match surplus_decompose (worst %.2g)", wf);
    }

    /* the two sources separate */
    {
        for (uint32_t i = 0; i < NV; i++) {
            cat[i] = (uint8_t) (i / 16);
            r[i] = (zt_fx) ((1 + cat[i]) * 65536 * (1 + 0.3 * unif()));
        }
        ct_decomp_t d;
        memcpy(a, r, sizeof a);
        for (uint32_t i = 0; i < 16; i++) { /* reverse inside category 0: profile kept */
            a[i] = r[15 - i];
        }
        ct_decompose(r, a, NV, cat, 4, 4, &d);
        CHECK(d.u_cross <= 2 && d.u_div > 50, "within-category shuffle: u_cross %d, u_div %d (Q16)",
              d.u_cross, d.u_div);
        for (uint32_t i = 0; i < NV; i++) a[i] = cat[i] == 0 ? r[i] * 3 : r[i];
        ct_decompose(r, a, NV, cat, 4, 4, &d);
        CHECK(d.u_cross > 50 && d.u_div <= 4, "category re-weighting: u_cross %d, u_div %d (Q16)",
              d.u_cross, d.u_div);
        CHECK(ct_decompose(r, a, NV, cat, 0, 4, &d) == -1 &&
                  ct_decompose(r, a, NV, cat, 4, 1, &d) == -1,
              "decompose: n_cat 0 or N < 2 refused");
        cat[3] = 9;
        CHECK(ct_decompose(r, a, NV, cat, 4, 4, &d) == -1,
              "decompose: category id out of range refused");
        cat[3] = 0;
        static zt_fx z[NV];
        CHECK(ct_decompose(r, z, NV, cat, 4, 4, &d) == 1, "decompose: zero vector -> no evidence");
    }

    /* norm ratio */
    {
        double we = 0;
        for (uint32_t it = 0; it < 5000; it++) {
            double sr = ldexp(1, (int) (next() % 24) - 8), sa = sr * (0.01 + 4 * fabs(unif()));
            double nr = 0, na = 0;
            for (uint32_t i = 0; i < NV; i++) {
                r[i] = (zt_fx) (unif() * sr * 65536 / 8);
                a[i] = (zt_fx) (unif() * sa * 65536 / 8);
                nr += (double) r[i] * r[i];
                na += (double) a[i] * a[i];
            }
            if (!nr || !na) continue;
            double want = sqrt(na / nr), got = ct_norm_ratio(r, a, NV) / 65536.0;
            if (want < 30000) {
                double e = fabs(got - want) / want;
                if (e > we) we = e;
            }
        }
        CHECK(we < 1e-3, "norm ratio vs float64: worst relative error %.2g", we);
    }

    /* ---- R3, R4 verdicts on a simulated accelerator ---- */
    {
        ct_drift_cfg_t cfg = {8, 655, 655, 655}; /* L = 7; tau 0.01; tol 1% */
        ct_drift_stats_t st;
        memset(&st, 0, sizeof st);
        static uint16_t h[NV];
        static uint8_t fla[NV];
        for (uint32_t i = 0; i < NV; i++) cat[i] = (uint8_t) (i / 8);
        uint32_t want_true = 0;
        for (uint32_t row = 0; row < 1000; row++) {
            for (uint32_t i = 0; i < NV; i++) {
                double v = unif() * 4 * (1 + cat[i]);
                r[i] = (zt_fx) nearbyint(v * 65536);
                h[i] = d2h(v);
            }
            int64_t f = ct_ingest(CT_FMT_F16, h, 0, NV, a, fla, 0);
            want_true += ct_drift_check(&cfg, r, a, NV, cat, 8, (uint64_t) f, &st, 0) == CT_TRUE;
        }
        CHECK(want_true == 1000, "FP16 accelerator output of the same values: 1000/1000 TRUE (%u)",
              want_true);
        uint32_t v;
        ct_decomp_t d;
        for (uint32_t i = 0; i < NV; i++) {
            r[i] = (zt_fx) ((unif() + 2) * 65536 * (1 + cat[i]));
            a[i] = r[i] + r[i] / 10;
        }
        v = ct_drift_check(&cfg, r, a, NV, cat, 8, 0, &st, &d);
        CHECK(v == CT_GLUT, "10%% scale error, same direction: GLUT (got %u)", v);
        for (uint32_t i = 0; i < NV; i++) a[i] = r[(i + 8) % NV]; /* categories rotated */
        v = ct_drift_check(&cfg, r, a, NV, cat, 8, 0, &st, &d);
        CHECK(v == CT_GLUT,
              "categories swapped, same norm: GLUT, cross drift L*u_cross %.3f "
              "(got %u)",
              7 * d.u_cross / 65536.0, v);
        for (uint32_t i = 0; i < NV; i++) a[i] = r[(i & ~7u) + 7 - (i & 7u)];
        v = ct_drift_check(&cfg, r, a, NV, cat, 8, 0, &st, &d);
        CHECK(v == CT_GLUT && d.u_cross <= 2 && 7 * d.u_div > 655,
              "reversed inside each category: GLUT by divergence only (L*u_div %.3f)",
              7 * d.u_div / 65536.0);
        for (uint32_t i = 0; i < NV; i++) a[i] = 2 * r[(i + 8) % NV];
        v = ct_drift_check(&cfg, r, a, NV, cat, 8, 0, &st, &d);
        CHECK(v == CT_FALSE, "swapped and doubled: FALSE (got %u)", v);
        memcpy(a, r, sizeof a);
        a[5] = 0; /* where a NaN was ingested */
        v = ct_drift_check(&cfg, r, a, NV, cat, 8, 1, &st, &d);
        CHECK(v == CT_PARADOX, "a NaN in an otherwise agreeing row: PARADOX, escalate (got %u)", v);
        for (uint32_t i = 0; i < NV; i++) a[i] = 2 * r[(i + 8) % NV];
        v = ct_drift_check(&cfg, r, a, NV, cat, 8, 1, &st, &d);
        CHECK(v == CT_FALSE, "a NaN in a disagreeing row: FALSE (got %u)", v);
        memset(a, 0, sizeof a);
        v = ct_drift_check(&cfg, r, a, NV, cat, 8, 0, &st, &d);
        CHECK(v == CT_UNKNOWN, "all-zero output, no faults: UNKNOWN (got %u)", v);
        v = ct_drift_check(&cfg, r, a, NV, cat, 8, NV, &st, &d);
        CHECK(v == CT_FALSE, "all-NaN output: FALSE (got %u)", v);
        v = ct_drift_check(0, r, a, NV, cat, 8, 0, &st, &d);
        CHECK(v == CT_PARADOX, "no configuration: PARADOX, not a crash");
        printf("       verdict counts: TRUE %llu FALSE %llu GLUT %llu PARADOX %llu UNKNOWN %llu; "
               "cross %llu div %llu mag %llu faulted %llu\n",
               (unsigned long long) st.truth[CT_TRUE], (unsigned long long) st.truth[CT_FALSE],
               (unsigned long long) st.truth[CT_GLUT], (unsigned long long) st.truth[CT_PARADOX],
               (unsigned long long) st.truth[CT_UNKNOWN], (unsigned long long) st.cross_drift,
               (unsigned long long) st.div_drift, (unsigned long long) st.mag_drift,
               (unsigned long long) st.faulted);
        CHECK(st.samples == 1009 && st.truth[CT_TRUE] == 1000 && st.truth[CT_GLUT] == 3 &&
                  st.truth[CT_FALSE] == 3 && st.truth[CT_PARADOX] == 2 && st.truth[CT_UNKNOWN] == 1,
              "verdict counts add up");
    }

    /* fuzz: garbage bits through ingest and the verdict, never a crash */
    {
        ct_drift_stats_t st;
        memset(&st, 0, sizeof st);
        static uint32_t raw[NV];
        static uint16_t raw16[NV];
        ct_ingest_stats_t ist;
        memset(&ist, 0, sizeof ist);
        uint64_t out_of_range = 0;
        for (uint32_t it = 0; it < 200000; it++) {
            uint32_t fmt = (uint32_t) (next() % 3), n = 1 + (uint32_t) (next() % NV);
            for (uint32_t i = 0; i < n; i++) {
                uint64_t z = next();
                raw[i] = (uint32_t) z;
                raw16[i] = (uint16_t) (z >> 40);
                if ((z >> 60) == 0) raw[i] = 0x7F800000u | (raw[i] & 0x807FFFFFu); /* Inf/NaN */
                if ((z >> 60) == 1) raw16[i] = (uint16_t) (0x7C00u | (raw16[i] & 0x83FFu));
                r[i] = (zt_fx) (next() >> (next() % 64));
                cat[i] = (uint8_t) (next() % 9);
            }
            int64_t f = ct_ingest(fmt, raw16, raw, n, a, 0, &ist);
            ct_drift_cfg_t cfg = {(uint32_t) (next() % 40), (zt_fx) next(), (zt_fx) next(),
                                  (zt_fx) next()};
            uint32_t v = ct_drift_check(&cfg, r, a, n, cat, 1 + (uint32_t) (next() % 8),
                                        (uint64_t) f, &st, 0);
            out_of_range += v >= CT_N_TRUTH;
        }
        uint64_t sum = 0;
        for (uint32_t t = 0; t < CT_N_TRUTH; t++) sum += st.truth[t];
        CHECK(out_of_range == 0 && sum == st.samples && st.samples == 200000,
              "fuzz: 200000 garbage rows (%llu NaN, %llu Inf, %llu saturated, %llu subnormal "
              "ingested), every verdict in range: T %llu F %llu G %llu P %llu U %llu",
              (unsigned long long) ist.nan, (unsigned long long) (ist.pos_inf + ist.neg_inf),
              (unsigned long long) ist.sat, (unsigned long long) ist.subnormal,
              (unsigned long long) st.truth[CT_TRUE], (unsigned long long) st.truth[CT_FALSE],
              (unsigned long long) st.truth[CT_GLUT], (unsigned long long) st.truth[CT_PARADOX],
              (unsigned long long) st.truth[CT_UNKNOWN]);
    }

    /* fuzz with valid arguments: extreme Q16 values never break the theorem checks */
    {
        ct_drift_stats_t st;
        memset(&st, 0, sizeof st);
        for (uint32_t it = 0; it < 100000; it++) {
            uint32_t n = 1 + (uint32_t) (next() % NV), ncat = 1 + (uint32_t) (next() % 16);
            for (uint32_t i = 0; i < n; i++) {
                uint64_t z = next();
                r[i] = (z & 7) == 0 ? INT32_MIN : (zt_fx) (z >> (z % 40));
                a[i] = (z & 56) == 0 ? INT32_MAX : (zt_fx) (next() >> (next() % 40));
                cat[i] = (uint8_t) (next() % ncat);
            }
            ct_drift_cfg_t cfg = {2 + (uint32_t) (next() % 64), (zt_fx) (next() & 0xFFFFF),
                                  (zt_fx) (next() & 0xFFFFF), (zt_fx) (next() & 0xFFFFF)};
            ct_drift_check(&cfg, r, a, n, cat, ncat, 0, &st, 0);
        }
        CHECK(st.inconsistent == 0 && st.truth[CT_PARADOX] == 0,
              "fuzz: 100000 extreme rows with valid arguments, no theorem violation "
              "(T %llu F %llu G %llu U %llu)",
              (unsigned long long) st.truth[CT_TRUE], (unsigned long long) st.truth[CT_FALSE],
              (unsigned long long) st.truth[CT_GLUT], (unsigned long long) st.truth[CT_UNKNOWN]);
    }

    /* sampling */
    {
        uint32_t hit = 0, none = 0, all = 0;
        for (uint32_t i = 0; i < 100000; i++) {
            hit += ct_sample(42, i, 16384);
            none += ct_sample(42, i, 0);
            all += ct_sample(42, i, 65536);
        }
        CHECK(hit > 24000 && hit < 26000 && none == 0 && all == 100000,
              "sampling: rate 1/4 picks %u of 100000; 0 none; 1 all", hit);
    }

    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
