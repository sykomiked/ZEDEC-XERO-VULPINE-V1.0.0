/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* test_zt_rope.c — RoPE against a long double reference.
 *
 *   gcc -std=c11 -Wall -Werror -Wextra -Isrc/tensor src/tensor/test_zt_rope.c \
 *       src/tensor/zt_rope.c src/tensor/zt.c -lm -o /tmp/test_zt_rope && /tmp/test_zt_rope
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "zt_rope.h"

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

static uint32_t f32_bits(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}

static uint64_t rs = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 7;
    rs ^= rs << 17;
    return rs;
}

static const long double PI_L = 3.141592653589793238462643383279502884L;

/* reference rotation of one head */
static void ref_rope(const zt_fx *in, long double *out, uint32_t hd, uint32_t n_rot, bool neox,
                     long double base, const long double *factor, uint64_t pos)
{
    for (uint32_t k = 0; k < hd; k++) out[k] = in[k];
    for (uint32_t i = 0; i < n_rot / 2; i++) {
        long double th =
            (long double) pos * powl(base, -2.0L * i / n_rot) / (factor ? factor[i] : 1.0L);
        uint32_t a = neox ? i : 2 * i, b = neox ? i + n_rot / 2 : 2 * i + 1;
        long double x0 = in[a], x1 = in[b];
        out[a] = x0 * cosl(th) - x1 * sinl(th);
        out[b] = x0 * sinl(th) + x1 * cosl(th);
    }
}

int main(void)
{
    printf("=== T20 rotary position encoding ===\n");

    /* sin/cos accuracy over the whole circle */
    {
        long double worst = 0;
        for (int k = 0; k < 200000; k++) {
            uint64_t ph = k < 1024 ? (uint64_t) k << 54 : rnd();
            int32_t s, c;
            zt_rope_sincos(ph, &s, &c);
            long double a = 2 * PI_L * ((long double) ph / 18446744073709551616.0L);
            long double es = fabsl(s - sinl(a) * 1073741824.0L),
                        ec = fabsl(c - cosl(a) * 1073741824.0L);
            if (es > worst) worst = es;
            if (ec > worst) worst = ec;
        }
        printf("       worst sin/cos error: %.3Lf units of Q30\n", worst);
        CHECK(worst <= 2.0L, "sin and cos within 2 units of Q30 at 200000 phases");
        int32_t s, c;
        zt_rope_sincos(0, &s, &c);
        CHECK(s == 0 && c == (1 << 30), "angle 0 is exactly (0, 1)");
        zt_rope_sincos(1ull << 62, &s, &c);
        CHECK(s == (1 << 30) && c == 0, "a quarter turn is exactly (1, 0)");
    }

    /* frequencies for the bases real models use */
    {
        const float bases[] = {10000.0f, 500000.0f, 1000000.0f, 5000000.0f, 1.5f, 75000.25f};
        long double worst = 0;
        for (int b = 0; b < 6; b++) {
            const uint32_t dims[] = {64, 80, 128, 256};
            for (int d = 0; d < 4; d++) {
                zt_rope_t r;
                if (!zt_rope_init(&r, dims[d], f32_bits(bases[b]), true)) {
                    worst = 1;
                    continue;
                }
                for (uint32_t i = 0; i < dims[d] / 2; i++) {
                    long double f = powl((long double) bases[b], -2.0L * i / dims[d]) / (2 * PI_L);
                    /* relative error, but a frequency is only held to one
                     * unit of 2^-64 turns, so small ones are judged by that */
                    long double ulps = fabsl((long double) r.freq[i] - f * 18446744073709551616.0L);
                    long double rel = ulps <= 2.0L ? 0 : ulps / (f * 18446744073709551616.0L);
                    if (rel > worst) worst = rel;
                }
            }
        }
        printf("       worst frequency relative error: %.3Le\n", worst);
        CHECK(worst < 1e-15L,
              "frequencies match base^(-2i/d)/(2 pi) to 1e-15, or within 2 units of 2^-64");
    }

    /* rotated heads against the reference, both pairings, long positions */
    {
        const struct {
            float base;
            uint32_t hd, n_rot;
            bool neox;
        } cases[] = {{1000000.0f, 128, 128, true},
                     {10000.0f, 64, 64, false},
                     {500000.0f, 128, 128, false},
                     {10000.0f, 128, 64, true}};
        const uint64_t poss[] = {0, 1, 7, 4095, 32767, 131071, 1000000, 4000000};
        for (int k = 0; k < 4; k++) {
            zt_rope_t r;
            bool ok = zt_rope_init(&r, cases[k].n_rot, f32_bits(cases[k].base), cases[k].neox);
            long double worst = 0;
            bool untouched = true;
            for (int p = 0; p < 8; p++)
                for (int rep = 0; rep < 20; rep++) {
                    zt_fx x[256], y[256];
                    long double ref[256];
                    for (uint32_t i = 0; i < cases[k].hd; i++)
                        x[i] = (zt_fx) ((int64_t) (rnd() % 2000001) - 1000000);
                    memcpy(y, x, cases[k].hd * sizeof x[0]);
                    zt_rope_apply(&r, y, cases[k].hd, poss[p]);
                    ref_rope(x, ref, cases[k].hd, cases[k].n_rot, cases[k].neox, cases[k].base, 0,
                             poss[p]);
                    for (uint32_t i = 0; i < cases[k].hd; i++) {
                        long double e = fabsl(y[i] - ref[i]);
                        if (e > worst) worst = e;
                    }
                    for (uint32_t i = cases[k].n_rot; i < cases[k].hd; i++)
                        untouched &= y[i] == x[i];
                }
            char msg[160];
            snprintf(msg, sizeof msg,
                     "%s, base %.0f, n_rot %u/%u: within 1 Q16 unit of the reference up to pos 4M "
                     "(worst %.3Lf)",
                     cases[k].neox ? "NEOX" : "interleaved", (double) cases[k].base, cases[k].n_rot,
                     cases[k].hd, worst);
            CHECK(ok && worst <= 1.0L, msg);
            if (cases[k].n_rot < cases[k].hd)
                CHECK(untouched, "dimensions past n_rot are left as they are");
        }
    }

    /* Llama 3 style per-frequency divisors, and one linear factor */
    {
        zt_rope_t r;
        uint32_t fb[64];
        long double fl[64];
        for (int i = 0; i < 64; i++) {
            float f = i < 20 ? 1.0f : i < 40 ? 1.0f + (float) (i - 20) * 0.35f : 8.0f;
            fb[i] = f32_bits(f);
            fl[i] = f;
        }
        bool ok = zt_rope_init(&r, 128, f32_bits(500000.0f), false) && zt_rope_divide(&r, fb, 64);
        long double worst = 0;
        for (int rep = 0; rep < 50; rep++) {
            zt_fx x[128], y[128];
            long double ref[128];
            uint64_t pos = rnd() % 131072;
            for (int i = 0; i < 128; i++) x[i] = (zt_fx) ((int64_t) (rnd() % 2000001) - 1000000);
            memcpy(y, x, sizeof x);
            zt_rope_apply(&r, y, 128, pos);
            ref_rope(x, ref, 128, 128, false, 500000.0L, fl, pos);
            for (int i = 0; i < 128; i++)
                if (fabsl(y[i] - ref[i]) > worst) worst = fabsl(y[i] - ref[i]);
        }
        CHECK(ok && worst <= 1.0L, "per-frequency divisors (rope_freqs) match the reference");
        zt_rope_t a, b;
        uint32_t four = f32_bits(4.0f);
        zt_rope_init(&a, 64, f32_bits(10000.0f), true);
        zt_rope_init(&b, 64, f32_bits(10000.0f), true);
        zt_rope_divide(&b, &four, 1);
        bool same = true;
        for (int i = 0; i < 32; i++) same &= b.freq[i] == a.freq[i] >> 2;
        CHECK(same, "a linear factor of 4 divides every frequency by 4");
    }

    /* properties: rotation keeps length, shares angles across heads, is relative */
    {
        zt_rope_t r;
        zt_rope_init(&r, 64, f32_bits(10000.0f), true);
        zt_fx h[4 * 64], one[64];
        for (int i = 0; i < 256; i++) h[i] = (zt_fx) ((int64_t) (rnd() % 400001) - 200000);
        memcpy(one, h + 128, sizeof one);
        zt_rope_apply_heads(&r, h, 4, 64, 999);
        zt_rope_apply(&r, one, 64, 999);
        CHECK(memcmp(one, h + 128, sizeof one) == 0,
              "multi-head application equals per-head application");

        /* <R(p)q, R(p+k)v> depends only on k */
        zt_fx q[64], v[64];
        for (int i = 0; i < 64; i++)
            q[i] = (zt_fx) ((int64_t) (rnd() % 200001) - 100000),
            v[i] = (zt_fx) ((int64_t) (rnd() % 200001) - 100000);
        long double dots[3];
        const uint64_t ps[3] = {10, 5000, 777777};
        for (int t = 0; t < 3; t++) {
            zt_fx a[64], b[64];
            memcpy(a, q, sizeof a);
            memcpy(b, v, sizeof b);
            zt_rope_apply(&r, a, 64, ps[t]);
            zt_rope_apply(&r, b, 64, ps[t] + 17);
            long double d = 0;
            for (int i = 0; i < 64; i++) d += (long double) a[i] * b[i];
            dots[t] = d / 65536.0L / 65536.0L;
        }
        long double spread = fabsl(dots[0] - dots[1]) + fabsl(dots[0] - dots[2]);
        CHECK(spread < 1e-3L * (fabsl(dots[0]) + 1),
              "attention scores depend only on relative position");
    }

    /* argument checks */
    {
        zt_rope_t r;
        CHECK(!zt_rope_init(&r, 63, f32_bits(10000.0f), true), "odd n_rot is refused");
        CHECK(!zt_rope_init(&r, 1024, f32_bits(10000.0f), true),
              "n_rot above the maximum is refused");
        CHECK(!zt_rope_init(&r, 64, f32_bits(1.0f), true) &&
                  !zt_rope_init(&r, 64, f32_bits(0.5f), true),
              "a base of 1 or below is refused");
        CHECK(!zt_rope_init(&r, 64, 0x7F800000u, true) && !zt_rope_init(&r, 64, 0x7FC00000u, true),
              "infinite and NaN bases are refused");
        zt_rope_init(&r, 64, f32_bits(10000.0f), true);
        uint32_t neg = f32_bits(-2.0f), tiny = f32_bits(1e-9f);
        CHECK(!zt_rope_divide(&r, &neg, 1) && !zt_rope_divide(&r, &tiny, 1),
              "negative and tiny divisors are refused");
        CHECK(!zt_rope_divide(&r, &neg, 5), "a divisor count other than 1 or n_rot/2 is refused");
    }

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
