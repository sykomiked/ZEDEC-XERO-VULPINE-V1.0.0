/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zphi.c — property tests for exact Z[phi] arithmetic (zphi.c).
 *
 * Every operation is checked against the same operation done in 128-bit
 * integers from its definition (phi^2 = phi + 1, conj(a + b phi) = (a + b) -
 * b phi, N = a^2 + ab - b^2, Tr = 2a + b), over random operands at every
 * magnitude from small to the int64 edges. The rule under test is the exact-
 * arithmetic one: a result marked valid is exactly right, a result that does
 * not fit int64 is never marked valid, and small operands never overflow. */
#include <stdio.h>
#include <math.h>
#include "zphi.h"

static int failures = 0;
static unsigned checks = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(cond)) {                                                                             \
            if (failures < 40) printf("  FAIL: %s (line %d)\n", msg, __LINE__);                    \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

typedef __int128 i128;
static uint64_t rs = 0x2545F4914F6CDD1DULL;
static uint64_t r64(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 7;
    rs ^= rs << 17;
    return rs;
}

/* operands spread over magnitudes: tiny, 16-bit, 31/32-bit (products near
 * the edge), 62/63-bit, and the exact edges */
static int64_t operand(void)
{
    switch (r64() % 8) {
    case 0:
        return (int64_t) (r64() % 13) - 6;
    case 1:
        return (int64_t) (r64() % 65536) - 32768;
    case 2:
        return (int64_t) (r64() >> 32) - (int64_t) 2147483648LL;
    case 3:
        return (int64_t) (r64() >> 31) - (int64_t) 4294967296LL;
    case 4:
        return (int64_t) (r64() >> 1) - (int64_t) (r64() >> 1);
    case 5:
        return (int64_t) r64();
    case 6:
        return (r64() & 1) ? INT64_MAX - (int64_t) (r64() % 3) : INT64_MIN + (int64_t) (r64() % 3);
    default:
        return (int64_t) (r64() % 2000001) - 1000000;
    }
}

/* a + b, saturating far outside int64 when 128 bits overflow (products of
 * two int64 are below 2^126, so only sums of products can) */
static i128 sat(i128 a, i128 b)
{
    i128 r;
    if (__builtin_add_overflow(a, b, &r)) return a > 0 ? (i128) 1 << 120 : -((i128) 1 << 120);
    return r;
}

static int fits(i128 v)
{
    return v >= (i128) INT64_MIN && v <= (i128) INT64_MAX;
}

/* valid => exact; does not fit => invalid */
static void expect(zphi_t got, i128 a, i128 b, const char *what)
{
    if (got.valid)
        CHECK(fits(a) && fits(b) && (i128) got.a == a && (i128) got.b == b, what);
    else
        CHECK(got.a == 0 && got.b == 0, "a refused result is the invalid value");
    if (!fits(a) || !fits(b)) CHECK(!got.valid, "a result outside int64 is never valid");
}

static int small(int64_t v)
{
    return v > -(1LL << 20) && v < (1LL << 20);
}

static void test_random(void)
{
    for (int round = 0; round < 400000; round++) {
        int64_t a = operand(), b = operand(), c = operand(), d = operand(), k = operand();
        zphi_t x = zphi_make(a, b), y = zphi_make(c, d);
        int sm = small(a) && small(b) && small(c) && small(d) && small(k);

        zphi_t s = zphi_add(x, y);
        expect(s, (i128) a + c, (i128) b + d, "add exact");
        CHECK(s.valid == (fits((i128) a + c) && fits((i128) b + d)), "add valid iff it fits");

        zphi_t t = zphi_sub(x, y);
        expect(t, (i128) a - c, (i128) b - d, "sub exact");
        CHECK(t.valid == (fits((i128) a - c) && fits((i128) b - d)), "sub valid iff it fits");

        zphi_t n = zphi_neg(x);
        expect(n, -(i128) a, -(i128) b, "neg exact");
        CHECK(n.valid == (a != INT64_MIN && b != INT64_MIN), "neg valid iff no INT64_MIN");

        zphi_t sc = zphi_scale(x, k);
        expect(sc, (i128) a * k, (i128) b * k, "scale exact");
        CHECK(sc.valid == (fits((i128) a * k) && fits((i128) b * k)), "scale valid iff it fits");

        /* (a + b phi)(c + d phi) = (ac + bd) + (ad + bc + bd) phi */
        i128 ma = sat((i128) a * c, (i128) b * d);
        i128 mb = sat(sat((i128) a * d, (i128) b * c), (i128) b * d);
        zphi_t m = zphi_mul(x, y);
        expect(m, ma, mb, "mul exact");
        if (sm) CHECK(m.valid, "small operands never overflow mul");
        zphi_t m2 = zphi_mul(y, x);
        CHECK(m.valid == m2.valid && (!m.valid || zphi_eq(m, m2)), "mul commutes");

        zphi_t cj = zphi_conj(x);
        expect(cj, (i128) a + b, -(i128) b, "conj exact");
        CHECK(cj.valid == (fits((i128) a + b) && b != INT64_MIN), "conj valid iff it fits");

        bool ok = true;
        int64_t nv = zphi_norm(x, &ok);
        i128 nexact = sat(sat((i128) a * a, (i128) a * b), -((i128) b * b));
        if (ok)
            CHECK((i128) nv == nexact, "norm exact");
        else
            CHECK(nv == 0, "a refused norm returns 0");
        if (!fits(nexact)) CHECK(!ok, "a norm outside int64 is refused");
        if (sm) CHECK(ok, "small operands never overflow norm");

        int64_t tv = zphi_trace(x, &ok);
        i128 texact = (i128) 2 * a + b;
        if (ok)
            CHECK((i128) tv == texact, "trace exact");
        else
            CHECK(tv == 0, "a refused trace returns 0");
        CHECK(ok == (fits((i128) 2 * a) && fits(texact)), "trace valid iff it fits");

        if (sm && small(a * 64) && small(b * 64) && small(c * 64) && small(d * 64)) {
            /* algebra (operands below 2^14, so N(xy) stays below 2^62): x conj(x) = N(x); x +
             * conj(x) = Tr(x); N multiplicative */
            zphi_t xc = zphi_mul(x, cj);
            CHECK(xc.valid && xc.b == 0 && xc.a == nv, "x conj(x) = N(x)");
            zphi_t xp = zphi_add(x, cj);
            CHECK(xp.valid && xp.b == 0 && xp.a == tv, "x + conj(x) = Tr(x)");
            bool o1, o2, o3;
            int64_t n1 = zphi_norm(x, &o1), n2 = zphi_norm(y, &o2), n12 = zphi_norm(m, &o3);
            CHECK(o1 && o2 && o3 && (i128) n12 == (i128) n1 * n2, "N(xy) = N(x) N(y)");
            CHECK(zphi_eq(zphi_conj(cj), x), "conj is an involution");
            CHECK(zphi_eq(zphi_sub(s, y), x), "(x + y) - y = x");
        }

        CHECK(zphi_eq(x, zphi_make(a, b)), "eq: same parts");
        CHECK(zphi_eq(x, y) == (a == c && b == d), "eq iff both parts agree");
        CHECK(zphi_is_zero(x) == (a == 0 && b == 0), "is_zero");
        CHECK(zphi_is_int(x) == (b == 0), "is_int");
    }
}

static void test_edges(void)
{
    const zphi_t I = zphi_invalid(), P = zphi_golden(), one = zphi_one();
    CHECK(!I.valid && I.a == 0 && I.b == 0, "invalid is (0, 0, false)");
    CHECK(zphi_eq(zphi_zero(), zphi_make(0, 0)), "zero");
    CHECK(zphi_eq(one, zphi_make(1, 0)), "one");
    CHECK(zphi_eq(P, zphi_make(0, 1)), "golden = phi");
    CHECK(zphi_eq(zphi_inv_golden(), zphi_make(-1, 1)), "1/phi = phi - 1");
    CHECK(zphi_eq(zphi_int(-7), zphi_make(-7, 0)), "int");
    CHECK(zphi_eq(zphi_mul(P, P), zphi_add(P, one)), "phi^2 = phi + 1");
    CHECK(zphi_eq(zphi_mul(P, zphi_inv_golden()), one), "phi * (1/phi) = 1");

    /* invalid poisons every operation, and is never equal to anything */
    zphi_t x = zphi_make(3, 4);
    CHECK(!zphi_add(I, x).valid && !zphi_add(x, I).valid, "add poisons");
    CHECK(!zphi_sub(I, x).valid && !zphi_sub(x, I).valid, "sub poisons");
    CHECK(!zphi_mul(I, x).valid && !zphi_mul(x, I).valid, "mul poisons");
    CHECK(!zphi_neg(I).valid && !zphi_scale(I, 2).valid && !zphi_conj(I).valid, "unary poisons");
    bool ok = true;
    CHECK(zphi_norm(I, &ok) == 0 && !ok, "norm of invalid refused");
    ok = true;
    CHECK(zphi_trace(I, &ok) == 0 && !ok, "trace of invalid refused");
    CHECK(zphi_norm(x, 0) == 9 + 12 - 16 && zphi_trace(x, 0) == 10, "ok may be NULL");
    CHECK(!zphi_eq(I, I) && !zphi_eq(I, zphi_zero()) && !zphi_eq(zphi_zero(), I), "eq refuses");
    CHECK(!zphi_is_zero(I) && !zphi_is_int(I), "invalid is neither zero nor an integer");
    zphi_t bad = I;
    bad.a = 5;
    bad.b = 0;
    CHECK(!zphi_is_int(bad) && !zphi_is_zero(zphi_invalid()),
          "validity is checked, not just parts");

    /* every individual overflow guard, at the edge it guards */
    const int64_t M = INT64_MAX, m = INT64_MIN;
    CHECK(!zphi_add(zphi_make(M, 0), zphi_make(1, 0)).valid, "add a overflows");
    CHECK(!zphi_add(zphi_make(0, M), zphi_make(0, 1)).valid, "add b overflows");
    CHECK(zphi_add(zphi_make(M, M), zphi_make(0, 0)).valid, "add at the edge fits");
    CHECK(!zphi_sub(zphi_make(m, 0), zphi_make(1, 0)).valid, "sub a overflows");
    CHECK(!zphi_sub(zphi_make(0, m), zphi_make(0, 1)).valid, "sub b overflows");
    CHECK(!zphi_neg(zphi_make(m, 0)).valid && !zphi_neg(zphi_make(0, m)).valid, "neg INT64_MIN");
    CHECK(zphi_eq(zphi_neg(zphi_make(M, -M)), zphi_make(-M, M)), "neg at the edge");
    CHECK(!zphi_scale(zphi_make(M, 0), 2).valid && !zphi_scale(zphi_make(0, M), 2).valid,
          "scale overflows");
    CHECK(zphi_eq(zphi_scale(zphi_make(M, m), 1), zphi_make(M, m)), "scale by 1 at the edge");
    CHECK(!zphi_conj(zphi_make(M, 1)).valid, "conj a + b overflows");
    CHECK(!zphi_conj(zphi_make(0, m)).valid, "conj -b overflows");
    CHECK(zphi_eq(zphi_conj(zphi_make(M - 1, 1)), zphi_make(M, -1)), "conj at the edge");
    const int64_t H = 3037000500LL; /* H^2 > INT64_MAX */
    CHECK(!zphi_mul(zphi_make(H, 0), zphi_make(H, 0)).valid, "mul ac overflows");
    CHECK(!zphi_mul(zphi_make(0, H), zphi_make(0, H)).valid, "mul bd overflows");
    CHECK(!zphi_mul(zphi_make(H, 0), zphi_make(0, H)).valid, "mul ad overflows");
    CHECK(!zphi_mul(zphi_make(0, H), zphi_make(H, 0)).valid, "mul bc overflows");
    CHECK(!zphi_mul(zphi_make(M, 1), zphi_make(1, 1)).valid, "mul ac + bd overflows");
    CHECK(zphi_eq(zphi_mul(zphi_make(M, 0), zphi_make(1, 1)), zphi_make(M, M)),
          "mul (M,0)(1,1) fits");
    CHECK(!zphi_mul(zphi_make(0, M / 2 + 1), zphi_make(1, 1)).valid,
          "mul (ad + bc) + bd overflows");
    CHECK(!zphi_mul(zphi_make(1, M / 2 + 1), zphi_make(1, 1)).valid, "mul ad + bc + bd overflows");
    CHECK(zphi_eq(zphi_mul(zphi_make(M, 0), one), zphi_make(M, 0)), "mul by 1 at the edge");
    ok = true;
    CHECK(zphi_norm(zphi_make(H, 0), &ok) == 0 && !ok, "norm a^2 overflows");
    ok = true;
    CHECK(zphi_norm(zphi_make(0, H), &ok) == 0 && !ok, "norm b^2 overflows");
    ok = true;
    CHECK(zphi_norm(zphi_make(M, -1), &ok) == 0 && !ok, "norm ab overflows");
    ok = true;
    CHECK(zphi_norm(zphi_make(3037000499LL, 3037000499LL), &ok) == 0 && !ok,
          "norm a^2 + ab overflows");
    ok = false;
    CHECK(zphi_norm(zphi_make(3037000499LL, 0), &ok) == 3037000499LL * 3037000499LL && ok,
          "norm fits");
    ok = true;
    CHECK(zphi_trace(zphi_make(M / 2 + 1, 0), &ok) == 0 && !ok, "trace 2a overflows");
    ok = true;
    CHECK(zphi_trace(zphi_make(M / 2, 2), &ok) == 0 && !ok, "trace 2a + b overflows");
    ok = false;
    CHECK(zphi_trace(zphi_make(M / 2, 1), &ok) == M && ok, "trace at the edge");

    /* phi^k = F(k-1) + F(k) phi, k up to 90 (F(91) still fits int64) */
    zphi_t pk = one;
    int64_t f0 = 0, f1 = 1;
    for (int k = 1; k <= 90; k++) {
        pk = zphi_mul(pk, P);
        CHECK(pk.valid && pk.a == f0 && pk.b == f1, "phi^k = F(k-1) + F(k) phi");
        int64_t f2 = f0 + f1;
        f0 = f1;
        f1 = f2;
    }

#if __STDC_HOSTED__
    CHECK(fabs(zphi_to_double(zphi_make(3, 5)) - (3.0 + 5.0 * 1.6180339887498949)) < 1e-9,
          "to_double");
    CHECK(fabs(zphi_to_double(zphi_make(-2, 0)) + 2.0) < 1e-12, "to_double of an integer");
    CHECK(zphi_to_double(I) == 0.0, "to_double of invalid");
    /* the type is public: an invalid value may carry leftover fields, and the
     * flag alone decides */
    const zphi_t junk = {7, 3, false};
    CHECK(zphi_to_double(junk) == 0.0 && !zphi_eq(junk, zphi_make(7, 3)) && !zphi_is_zero(junk) &&
              !zphi_is_int(junk) && !zphi_add(junk, one).valid,
          "an invalid value with leftover fields is still invalid");
#endif
    CHECK(zphi_selfcheck() == 0, "selfcheck passes");
}

int main(void)
{
    printf("test_zphi: exact Z[phi] arithmetic vs 128-bit integers\n");
    test_edges();
    test_random();
    printf("%u checks, %d failures\n", checks, failures);
    if (failures) {
        printf("[FAIL] test_zphi\n");
        return 1;
    }
    printf("[PASS] test_zphi\n");
    return 0;
}
