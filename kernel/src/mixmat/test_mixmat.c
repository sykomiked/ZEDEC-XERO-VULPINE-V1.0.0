/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_mixmat.c — host test for mixmat.c (exact rational mixing matrices).
 *
 * Every expected value below is an exact rational computed independently
 * (Python fractions.Fraction, or by hand where noted), never read back from
 * the module. Covers: construction and dimension bounds, set_row_counts
 * (exact weights, every refusal, overflow), the stochastic predicate (exact
 * row sum, negatives, invalid entries, entries outside the live order), a
 * non-commutative 3x3 product, powers as Fibonacci numbers up to the int64
 * edge (F92 representable, F93 refused), apply/compound with the 2^level
 * scale and its 62 guard, the stationary solve (known answers, a case that
 * needs a row swap, singular refusals), the permutation test, and selfcheck. */
#include <stdio.h>
#include <stdint.h>
#include "mixmat.h"

static int pass, fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                                   \
        }                                                                                          \
    } while (0)

/* exact: same normalised numerator and denominator, and valid */
static int req(rat_t a, int64_t num, int64_t den)
{
    return a.valid && a.num == num && a.den == den;
}

static mixmat_t from_rows(uint32_t n, const int64_t *num, const int64_t *den)
{
    mixmat_t m = mixmat_zero(n);
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t j = 0; j < n; j++) m.a[i][j] = rat_make(num[i * n + j], den[i * n + j]);
    return m;
}

static mixvec_t vec(uint32_t n, const int64_t *v)
{
    mixvec_t r;
    r.n = n;
    r.valid = true;
    for (uint32_t i = 0; i < MIXMAT_MAX; i++) r.v[i] = rat_zero();
    for (uint32_t i = 0; i < n; i++) r.v[i] = rat_from_int(v[i]);
    return r;
}

static void test_construct(void)
{
    mixmat_t z = mixmat_zero(0);
    CHECK(!z.valid && z.n == 0, "zero(0) refused");
    z = mixmat_zero(MIXMAT_MAX + 1);
    CHECK(!z.valid && z.n == 0, "zero(14) refused");
    z = mixmat_zero(MIXMAT_MAX);
    CHECK(z.valid && z.n == 13, "zero(13) accepted");
    z = mixmat_zero(1);
    CHECK(z.valid && z.n == 1, "zero(1) accepted");
    int allzero = 1;
    for (uint32_t i = 0; i < MIXMAT_MAX; i++)
        for (uint32_t j = 0; j < MIXMAT_MAX; j++)
            if (!req(z.a[i][j], 0, 1)) allzero = 0;
    CHECK(allzero, "zero() clears every cell, in and out of range");

    mixmat_t id = mixmat_identity(3);
    CHECK(id.valid && id.n == 3, "identity(3) valid");
    int ok = 1;
    for (uint32_t i = 0; i < MIXMAT_MAX; i++)
        for (uint32_t j = 0; j < MIXMAT_MAX; j++) {
            int one = (i == j && i < 3);
            if (!req(id.a[i][j], one ? 1 : 0, 1)) ok = 0;
        }
    CHECK(ok, "identity(3): ones on the live diagonal only");
    id = mixmat_identity(MIXMAT_MAX);
    CHECK(id.valid && req(id.a[12][12], 1, 1) && req(id.a[12][11], 0, 1), "identity(13) corner");
    id = mixmat_identity(0);
    CHECK(!id.valid && id.n == 0, "identity(0) refused");
    id = mixmat_identity(14);
    CHECK(!id.valid, "identity(14) refused");
    CHECK(req(id.a[0][0], 0, 1), "refused identity writes no diagonal");
}

static void test_set_row(void)
{
    mixmat_t m = mixmat_zero(4);
    const int64_t thc[4] = {30, 21, 0, 2};
    CHECK(mixmat_set_row_counts(&m, 1, thc, 4), "thc row accepted");
    CHECK(req(m.a[1][0], 30, 53) && req(m.a[1][1], 21, 53) && req(m.a[1][2], 0, 1) &&
              req(m.a[1][3], 2, 53),
          "C21H30O2 weights exactly 30/53 21/53 0 2/53");
    CHECK(req(m.a[0][0], 0, 1) && req(m.a[2][1], 0, 1), "other rows untouched");

    const int64_t lead0[4] = {0, 0, 6, 2}; /* leading zero is not negative */
    CHECK(mixmat_set_row_counts(&m, 3, lead0, 4), "leading zero counts accepted");
    CHECK(req(m.a[3][0], 0, 1) && req(m.a[3][2], 3, 4) && req(m.a[3][3], 1, 4), "6/8 -> 3/4");

    const int64_t one[4] = {0, 0, 0, 7};
    CHECK(mixmat_set_row_counts(&m, 0, one, 4) && req(m.a[0][3], 1, 1), "single part -> 1");

    mixmat_t keep = m;
    const int64_t neg[4] = {1, -1, 1, 1};
    CHECK(!mixmat_set_row_counts(&m, 2, neg, 4), "negative count refused");
    const int64_t negl[4] = {1, 1, 1, -1};
    CHECK(!mixmat_set_row_counts(&m, 2, negl, 4), "negative last count refused");
    const int64_t none[4] = {0, 0, 0, 0};
    CHECK(!mixmat_set_row_counts(&m, 2, none, 4), "all-zero row refused");
    const int64_t ovf[4] = {INT64_MAX, 1, 0, 0};
    CHECK(!mixmat_set_row_counts(&m, 2, ovf, 4), "count sum overflow refused");
    const int64_t big[4] = {INT64_MAX - 1, 1, 0, 0};
    CHECK(mixmat_set_row_counts(&m, 2, big, 4), "count sum exactly INT64_MAX accepted");
    CHECK(req(m.a[2][1], 1, INT64_MAX), "1/INT64_MAX weight");
    m.a[2][0] = keep.a[2][0];
    m.a[2][1] = keep.a[2][1];

    CHECK(!mixmat_set_row_counts(&m, 4, thc, 4), "row == n refused");
    CHECK(mixmat_set_row_counts(&m, 3, thc, 4), "row == n-1 accepted");
    CHECK(!mixmat_set_row_counts(&m, 0, thc, 3), "count length != n refused");
    CHECK(!mixmat_set_row_counts(NULL, 0, thc, 4), "null matrix refused");
    CHECK(!mixmat_set_row_counts(&m, 0, NULL, 4), "null counts refused");
    mixmat_t bad = mixmat_zero(0);
    CHECK(!mixmat_set_row_counts(&bad, 0, thc, 0), "invalid matrix refused");
    mixmat_t inv = mixmat_zero(4);
    inv.valid = false;
    CHECK(!mixmat_set_row_counts(&inv, 0, thc, 4), "valid=false matrix refused");
}

static void test_stochastic(void)
{
    mixmat_t m = mixmat_zero(2);
    const int64_t r0[2] = {1, 2}, r1[2] = {3, 1};
    mixmat_set_row_counts(&m, 0, r0, 2);
    mixmat_set_row_counts(&m, 1, r1, 2);
    CHECK(mixmat_is_stochastic(&m), "count rows are stochastic");
    m.a[5][5] = rat_make(1, 0); /* invalid cell outside the live order: ignored */
    m.a[0][2] = rat_from_int(9);
    m.a[2][0] = rat_from_int(9);
    CHECK(mixmat_is_stochastic(&m), "cells outside n are ignored");

    mixmat_t s = m;
    s.a[1][0] = rat_make(3, 2);
    s.a[1][1] = rat_make(-1, 2); /* sums to 1, but negative */
    CHECK(!mixmat_is_stochastic(&s), "negative entry refused even though row sums to 1");
    s = m;
    s.a[1][1] = rat_make(1, 5); /* row 1 sums to 3/4 + 1/5 */
    CHECK(!mixmat_is_stochastic(&s), "row summing below 1 refused");
    s = m;
    s.a[1][1] = rat_make(1, 3); /* 3/4 + 1/3 > 1 */
    CHECK(!mixmat_is_stochastic(&s), "row summing above 1 refused");
    s = m;
    s.a[0][1] = rat_make(1, 0);
    CHECK(!mixmat_is_stochastic(&s), "invalid entry refused");
    s = m;
    s.a[0][0] = rat_make(1, 4294967291LL); /* sum denominator would pass int64 */
    s.a[0][1] = rat_make(1, 4294967279LL);
    CHECK(!mixmat_is_stochastic(&s), "overflowing row sum refused");
    s = m;
    s.a[1][0] = rat_zero();
    s.a[1][1] = rat_from_int(1);
    CHECK(mixmat_is_stochastic(&s), "0,1 row is stochastic (zero is non-negative)");

    s = m;
    s.valid = false;
    CHECK(!mixmat_is_stochastic(&s), "invalid matrix refused");
    s = m;
    s.n = 0;
    CHECK(!mixmat_is_stochastic(&s), "order 0 refused");
    s = m;
    s.n = MIXMAT_MAX + 1;
    CHECK(!mixmat_is_stochastic(&s), "order 14 refused");
    CHECK(!mixmat_is_stochastic(NULL), "null refused");
    mixmat_t z = mixmat_zero(3);
    CHECK(!mixmat_is_stochastic(&z), "all-zero matrix is not stochastic");
    mixmat_t id = mixmat_identity(MIXMAT_MAX);
    CHECK(mixmat_is_stochastic(&id), "identity(13) is stochastic");
    id.a[12][12] = rat_make(1, 2);
    CHECK(!mixmat_is_stochastic(&id), "last row of 13 is checked");
}

static void test_mul(void)
{
    /* A=[[1,2,3],[-4,5/2,6],[7,0,1/3]], B=[[1/2,0,2],[3,-1,0],[1,4,1/5]] */
    const int64_t an[9] = {1, 2, 3, -4, 5, 6, 7, 0, 1}, ad[9] = {1, 1, 1, 1, 2, 1, 1, 1, 3};
    const int64_t bn[9] = {1, 0, 2, 3, -1, 0, 1, 4, 1}, bd[9] = {2, 1, 1, 1, 1, 1, 1, 1, 5};
    mixmat_t A = from_rows(3, an, ad), B = from_rows(3, bn, bd);
    mixmat_t ab = mixmat_mul(&A, &B), ba = mixmat_mul(&B, &A);
    /* AB = [[19/2,10,13/5],[23/2,43/2,-34/5],[23/6,4/3,211/15]] */
    const int64_t abn[9] = {19, 10, 13, 23, 43, -34, 23, 4, 211},
                  abd[9] = {2, 1, 5, 2, 2, 5, 6, 3, 15};
    /* BA = [[29/2,1,13/6],[7,7/2,3],[-68/5,12,406/15]] */
    const int64_t ban[9] = {29, 1, 13, 7, 7, 3, -68, 12, 406},
                  bad[9] = {2, 1, 6, 1, 2, 1, 5, 1, 15};
    CHECK(ab.valid && ab.n == 3 && ba.valid && ba.n == 3, "3x3 products valid");
    int ok1 = 1, ok2 = 1;
    for (int i = 0; i < 9; i++) {
        if (!req(ab.a[i / 3][i % 3], abn[i], abd[i])) ok1 = 0;
        if (!req(ba.a[i / 3][i % 3], ban[i], bad[i])) ok2 = 0;
    }
    CHECK(ok1, "AB known answer");
    CHECK(ok2, "BA known answer (non-commutative)");
    CHECK(req(ab.a[3][3], 0, 1) && req(ab.a[0][3], 0, 1), "product leaves cells outside n zero");
    /* cells outside the live order are ignored, even when they hold junk */
    mixmat_t Aj = A, Bj = B;
    for (uint32_t i = 0; i < MIXMAT_MAX; i++) {
        Aj.a[i][3] = Aj.a[3][i] = rat_from_int(5);
        Bj.a[i][3] = Bj.a[3][i] = rat_from_int(7);
    }
    mixmat_t abj = mixmat_mul(&Aj, &Bj);
    int okj = abj.valid;
    for (int i = 0; i < 9; i++)
        if (!req(abj.a[i / 3][i % 3], abn[i], abd[i])) okj = 0;
    CHECK(okj, "junk outside n does not leak into the product");
    CHECK(req(abj.a[3][0], 0, 1) && req(abj.a[0][3], 0, 1), "product row/col n stays zero");

    /* stochastic closure, by hand: [[1/2,1/2],[1/3,2/3]]·[[1,0],[1/4,3/4]] */
    const int64_t xn[4] = {1, 1, 1, 2}, xd[4] = {2, 2, 3, 3};
    const int64_t yn[4] = {1, 0, 1, 3}, yd[4] = {1, 1, 4, 4};
    mixmat_t X = from_rows(2, xn, xd), Y = from_rows(2, yn, yd);
    mixmat_t xy = mixmat_mul(&X, &Y);
    CHECK(req(xy.a[0][0], 5, 8) && req(xy.a[0][1], 3, 8) && req(xy.a[1][0], 1, 2) &&
              req(xy.a[1][1], 1, 2),
          "XY = [[5/8,3/8],[1/2,1/2]]");
    CHECK(mixmat_is_stochastic(&xy), "product of stochastic is stochastic");

    mixmat_t two = mixmat_identity(2);
    mixmat_t r = mixmat_mul(&A, &two);
    CHECK(!r.valid, "order mismatch refused");
    CHECK(r.n == 3, "refused product keeps the left order");
    r = mixmat_mul(NULL, &A);
    CHECK(r.n == 1, "refused product with null left has order 1");
    CHECK(!r.valid, "null left refused");
    r = mixmat_mul(&A, NULL);
    CHECK(!r.valid, "null right refused");
    mixmat_t Ai = A;
    Ai.valid = false;
    r = mixmat_mul(&Ai, &B);
    CHECK(!r.valid, "invalid left refused");
    r = mixmat_mul(&B, &Ai);
    CHECK(!r.valid, "invalid right refused");

    /* 1/p1 · 1/p2 has a denominator past int64: reported, never wrong */
    const int64_t on[1] = {1}, p1[1] = {4294967291LL}, p2[1] = {4294967279LL};
    mixmat_t P1 = from_rows(1, on, p1), P2 = from_rows(1, on, p2);
    r = mixmat_mul(&P1, &P2);
    CHECK(!r.valid, "overflowing product refused");
    r = mixmat_mul(&P1, &P1);
    CHECK(!r.valid, "overflowing square refused");
}

static void test_pow(void)
{
    /* Q = [[1,1],[1,0]], Q^k = [[F(k+1),F(k)],[F(k),F(k-1)]] */
    const int64_t qn[4] = {1, 1, 1, 0}, qd[4] = {1, 1, 1, 1};
    mixmat_t Q = from_rows(2, qn, qd);
    int64_t fib[94];
    fib[0] = 0;
    fib[1] = 1;
    for (int i = 2; i <= 92; i++) fib[i] = fib[i - 1] + fib[i - 2];

    mixmat_t p0 = mixmat_pow(&Q, 0);
    CHECK(p0.valid && p0.n == 2 && req(p0.a[0][0], 1, 1) && req(p0.a[0][1], 0, 1) &&
              req(p0.a[1][0], 0, 1) && req(p0.a[1][1], 1, 1),
          "Q^0 = I");
    const uint32_t ks[] = {1, 2, 3, 5, 6, 7, 10, 64, 90, 91};
    for (unsigned t = 0; t < sizeof ks / sizeof ks[0]; t++) {
        uint32_t k = ks[t];
        mixmat_t p = mixmat_pow(&Q, k);
        int ok = p.valid && req(p.a[0][0], fib[k + 1], 1) && req(p.a[0][1], fib[k], 1) &&
                 req(p.a[1][0], fib[k], 1) && req(p.a[1][1], fib[k - 1], 1);
        if (!ok) printf("  k=%u\n", k);
        CHECK(ok, "Q^k is the Fibonacci matrix");
    }
    CHECK(fib[92] == 7540113804746346429LL, "F92 sanity");
    mixmat_t p92 = mixmat_pow(&Q, 92); /* contains F93 > INT64_MAX */
    CHECK(!p92.valid, "Q^92 overflows and is refused");
    mixmat_t p200 = mixmat_pow(&Q, 200);
    CHECK(!p200.valid, "Q^200 refused");

    /* non-commutativity cannot hide in pow, but a stochastic power can be
     * checked exactly: [[1/2,1/2],[1/3,2/3]]^2 = [[5/12,7/12],[7/18,11/18]] */
    const int64_t xn[4] = {1, 1, 1, 2}, xd[4] = {2, 2, 3, 3};
    mixmat_t X = from_rows(2, xn, xd);
    mixmat_t x2 = mixmat_pow(&X, 2);
    CHECK(req(x2.a[0][0], 5, 12) && req(x2.a[0][1], 7, 12) && req(x2.a[1][0], 7, 18) &&
              req(x2.a[1][1], 11, 18),
          "X^2 known answer");
    mixmat_t x3 = mixmat_pow(&X, 3), x3m = mixmat_mul(&x2, &X);
    int same = x3.valid && x3m.valid;
    for (int i = 0; i < 4; i++)
        if (!rat_eq(x3.a[i / 2][i % 2], x3m.a[i / 2][i % 2])) same = 0;
    CHECK(same, "X^3 == X^2 * X");
    CHECK(req(x3.a[0][0], 29, 72), "X^3[0][0] = 29/72");

    mixmat_t r = mixmat_pow(NULL, 2);
    CHECK(!r.valid && r.n == 1, "pow(null) refused, order 1");
    mixmat_t inv = X;
    inv.valid = false;
    r = mixmat_pow(&inv, 0);
    CHECK(!r.valid, "pow(invalid, 0) refused");

    /* acc overflow (k odd, base fits): P^3 with 1/p denominators */
    const int64_t on[1] = {1}, pd[1] = {2097200};
    mixmat_t P = from_rows(1, on, pd); /* 2097200^3 > 2^63 */
    r = mixmat_pow(&P, 3);
    CHECK(!r.valid, "pow overflow in the accumulator refused");
    r = mixmat_pow(&P, 2);
    CHECK(req(r.a[0][0], 1, 2097200LL * 2097200LL), "P^2 exact");
}

static void test_apply_compound(void)
{
    const int64_t mn[4] = {1, 2, 3, 4}, md[4] = {1, 1, 1, 1};
    mixmat_t M = from_rows(2, mn, md);
    const int64_t v56[2] = {5, 6};
    mixvec_t v = vec(2, v56);
    mixvec_t c;
    mixvec_t r = mixmat_apply(&M, &v);
    CHECK(r.valid && r.n == 2 && req(r.v[0], 17, 1) && req(r.v[1], 39, 1), "M v = [17,39]");
    CHECK(req(r.v[2], 0, 1) && req(r.v[12], 0, 1), "apply clears tail");
    mixmat_t Mj = M;
    mixvec_t vj = v;
    for (uint32_t i = 0; i < MIXMAT_MAX; i++) Mj.a[i][2] = Mj.a[2][i] = rat_from_int(9);
    vj.v[2] = rat_from_int(11);
    r = mixmat_apply(&Mj, &vj);
    CHECK(r.valid && req(r.v[0], 17, 1) && req(r.v[1], 39, 1) && req(r.v[2], 0, 1),
          "junk outside n is ignored by apply");
    c = mixmat_compound(&Mj, &vj, 2);
    CHECK(c.valid && req(c.v[0], 68, 1) && req(c.v[1], 156, 1) && req(c.v[2], 0, 1),
          "junk outside n is ignored by compound");
    /* full order 13: I13 · (1..13) at level 2 = 4·(1..13) */
    mixmat_t I13 = mixmat_identity(MIXMAT_MAX);
    int64_t seq[MIXMAT_MAX];
    for (int i = 0; i < (int) MIXMAT_MAX; i++) seq[i] = i + 1;
    mixvec_t v13 = vec(MIXMAT_MAX, seq);
    c = mixmat_compound(&I13, &v13, 2);
    int ok13 = c.valid && c.n == 13;
    for (int i = 0; i < (int) MIXMAT_MAX; i++)
        if (!req(c.v[i], 4 * (i + 1), 1)) ok13 = 0;
    CHECK(ok13, "order-13 compound");

    const int64_t xn[4] = {1, 1, 1, 2}, xd[4] = {2, 2, 3, 3};
    mixmat_t X = from_rows(2, xn, xd);
    const int64_t v36[2] = {3, 6};
    mixvec_t w = vec(2, v36);
    r = mixmat_apply(&X, &w);
    CHECK(req(r.v[0], 9, 2) && req(r.v[1], 5, 1), "weighted average: [9/2, 5]");

    for (uint32_t lv = 0; lv <= 3; lv++) {
        mixvec_t c = mixmat_compound(&X, &w, lv);
        CHECK(c.valid && c.n == 2 && rat_eq(c.v[0], rat_make(9LL << lv, 2)) &&
                  req(c.v[1], 5LL << lv, 1),
              "compound scales by exactly 2^level");
    }
    c = mixmat_compound(&M, &v, 6);
    CHECK(req(c.v[0], 17 * 64, 1) && req(c.v[1], 39 * 64, 1), "level 6 = x64");

    mixmat_t I1 = mixmat_identity(1);
    const int64_t three[1] = {3}, four[1] = {4};
    mixvec_t v3 = vec(1, three), v4 = vec(1, four);
    c = mixmat_compound(&I1, &v3, 61);
    CHECK(req(c.v[0], 3LL << 61, 1), "level 61 representable");
    c = mixmat_compound(&I1, &v4, 61);
    CHECK(!c.valid, "4 * 2^61 overflows and is refused");
    c = mixmat_compound(&I1, &v3, 62);
    CHECK(!c.valid, "level 62 refused");
    const int64_t zero1[1] = {0};
    mixvec_t v0 = vec(1, zero1);
    c = mixmat_compound(&I1, &v0, 62);
    CHECK(!c.valid, "level 62 refused even for a zero vector");
    c = mixmat_compound(&I1, &v0, 100);
    CHECK(!c.valid, "level 100 refused");
    c = mixmat_compound(&M, &v3, 1);
    CHECK(!c.valid, "compound propagates apply refusal");

    r = mixmat_apply(&M, &v3);
    CHECK(!r.valid && r.n == 0, "order mismatch refused");
    int zt = 1;
    for (uint32_t i = 0; i < MIXMAT_MAX; i++)
        if (!req(r.v[i], 0, 1)) zt = 0;
    CHECK(zt, "refused apply returns an all-zero vector");
    r = mixmat_apply(NULL, &v);
    CHECK(!r.valid, "null matrix refused");
    r = mixmat_apply(&M, NULL);
    CHECK(!r.valid, "null vector refused");
    mixvec_t vi = v;
    vi.valid = false;
    r = mixmat_apply(&M, &vi);
    CHECK(!r.valid, "invalid vector refused");
    mixmat_t Mi = M;
    Mi.valid = false;
    r = mixmat_apply(&Mi, &v);
    CHECK(!r.valid, "invalid matrix refused");

    const int64_t on[1] = {1}, pd[1] = {4294967291LL};
    mixmat_t P = from_rows(1, on, pd);
    mixvec_t pv;
    pv.n = 1;
    pv.valid = true;
    for (uint32_t i = 0; i < MIXMAT_MAX; i++) pv.v[i] = rat_zero();
    pv.v[0] = rat_make(1, 4294967279LL);
    r = mixmat_apply(&P, &pv);
    CHECK(!r.valid, "overflowing apply refused");
    /* overflow in the SUM, not the product: 1/p1 + 1/p2 */
    mixmat_t S = mixmat_zero(2);
    S.a[0][0] = rat_make(1, 4294967291LL);
    S.a[0][1] = rat_make(1, 4294967279LL);
    const int64_t ones[2] = {1, 1};
    mixvec_t o = vec(2, ones);
    r = mixmat_apply(&S, &o);
    CHECK(!r.valid, "overflowing accumulation refused");
}

static int is_fixed(const mixmat_t *m, const mixvec_t *pi)
{
    for (uint32_t j = 0; j < m->n; j++) {
        rat_t acc = rat_zero();
        for (uint32_t i = 0; i < m->n; i++) acc = rat_add(acc, rat_mul(pi->v[i], m->a[i][j]));
        if (!acc.valid || !rat_eq(acc, pi->v[j])) return 0;
    }
    return 1;
}

static void test_stationary(void)
{
    mixvec_t pi;
    /* [[2,1,1],[1,1,1],[1,1,2]] by counts -> pi = (4/11, 3/11, 4/11) */
    mixmat_t p = mixmat_zero(3);
    const int64_t r0[3] = {2, 1, 1}, r1[3] = {1, 1, 1}, r2[3] = {1, 1, 2};
    mixmat_set_row_counts(&p, 0, r0, 3);
    mixmat_set_row_counts(&p, 1, r1, 3);
    mixmat_set_row_counts(&p, 2, r2, 3);
    CHECK(mixmat_stationary(&p, &pi), "primitive 3x3 solved");
    CHECK(pi.valid && pi.n == 3 && req(pi.v[0], 4, 11) && req(pi.v[1], 3, 11) &&
              req(pi.v[2], 4, 11),
          "pi = (4/11, 3/11, 4/11)");
    CHECK(req(pi.v[3], 0, 1) && req(pi.v[12], 0, 1), "pi tail cleared");

    /* two-state: [[2/3,1/3],[1/4,3/4]] -> (b/(a+b), a/(a+b)) = (3/7, 4/7) */
    mixmat_t t = mixmat_zero(2);
    const int64_t a0[2] = {2, 1}, a1[2] = {1, 3};
    mixmat_set_row_counts(&t, 0, a0, 2);
    mixmat_set_row_counts(&t, 1, a1, 2);
    CHECK(mixmat_stationary(&t, &pi) && req(pi.v[0], 3, 7) && req(pi.v[1], 4, 7),
          "two-state pi = (3/7, 4/7)");

    /* the four-compound matrix from the paper (H2O, THC, GABA, serotonin) */
    mixmat_t h = mixmat_zero(4);
    const int64_t h2o[4] = {2, 0, 0, 1}, thc[4] = {30, 21, 0, 2}, gaba[4] = {9, 4, 1, 2},
                  ser[4] = {12, 10, 2, 1};
    mixmat_set_row_counts(&h, 0, h2o, 4);
    mixmat_set_row_counts(&h, 1, thc, 4);
    mixmat_set_row_counts(&h, 2, gaba, 4);
    mixmat_set_row_counts(&h, 3, ser, 4);
    CHECK(mixmat_stationary(&h, &pi) && req(pi.v[0], 1278, 2101) && req(pi.v[1], 4187, 27313) &&
              req(pi.v[2], 512, 27313) && req(pi.v[3], 6000, 27313),
          "compound matrix pi known answer");
    CHECK(is_fixed(&h, &pi), "pi M == pi");

    /* absorbing state 0: needs a row swap at column 0. pi = (1, 0) */
    mixmat_t ab = mixmat_zero(2);
    const int64_t b0[2] = {1, 0}, b1[2] = {1, 1};
    mixmat_set_row_counts(&ab, 0, b0, 2);
    mixmat_set_row_counts(&ab, 1, b1, 2);
    CHECK(mixmat_stationary(&ab, &pi) && req(pi.v[0], 1, 1) && req(pi.v[1], 0, 1),
          "absorbing chain pi = (1, 0)");
    /* absorbing state in the middle of 3 */
    mixmat_t ab3 = mixmat_zero(3);
    const int64_t c0[3] = {1, 1, 0}, c1[3] = {0, 1, 0}, c2[3] = {0, 1, 1};
    mixmat_set_row_counts(&ab3, 0, c0, 3);
    mixmat_set_row_counts(&ab3, 1, c1, 3);
    mixmat_set_row_counts(&ab3, 2, c2, 3);
    CHECK(mixmat_stationary(&ab3, &pi) && req(pi.v[0], 0, 1) && req(pi.v[1], 1, 1) &&
              req(pi.v[2], 0, 1),
          "absorbing middle pi = (0, 1, 0)");

    mixmat_t one = mixmat_identity(1);
    CHECK(mixmat_stationary(&one, &pi) && pi.n == 1 && req(pi.v[0], 1, 1), "order 1: pi = (1)");

    /* a 3-cycle has the unique stationary vector (1/3,1/3,1/3) */
    mixmat_t cyc = mixmat_zero(3);
    const int64_t e0[3] = {0, 1, 0}, e1[3] = {0, 0, 1}, e2[3] = {1, 0, 0};
    mixmat_set_row_counts(&cyc, 0, e0, 3);
    mixmat_set_row_counts(&cyc, 1, e1, 3);
    mixmat_set_row_counts(&cyc, 2, e2, 3);
    CHECK(mixmat_stationary(&cyc, &pi) && req(pi.v[0], 1, 3) && req(pi.v[1], 1, 3) &&
              req(pi.v[2], 1, 3),
          "3-cycle pi uniform");

    /* junk outside the live order is ignored: same answer as t */
    mixmat_t tj = t;
    for (uint32_t i = 0; i < MIXMAT_MAX; i++) tj.a[i][2] = tj.a[2][i] = rat_make(1, 0);
    CHECK(mixmat_stationary(&tj, &pi) && req(pi.v[0], 3, 7) && req(pi.v[1], 4, 7),
          "junk outside n ignored by the solver");

    /* order 13, doubly stochastic ring (stay or step): pi uniform 1/13 */
    mixmat_t ring = mixmat_zero(MIXMAT_MAX);
    for (uint32_t i = 0; i < MIXMAT_MAX; i++) {
        int64_t cnt[MIXMAT_MAX] = {0};
        cnt[i] = 1;
        cnt[(i + 1) % MIXMAT_MAX] = 2;
        mixmat_set_row_counts(&ring, i, cnt, MIXMAT_MAX);
    }
    int okr = mixmat_stationary(&ring, &pi) && pi.n == 13;
    for (uint32_t i = 0; i < MIXMAT_MAX; i++)
        if (!req(pi.v[i], 1, 13)) okr = 0;
    CHECK(okr, "order-13 ring: pi uniform 1/13");

    /* exact arithmetic that runs out of int64 is REFUSED, never wrong.
     * Found by search; the first fails in the elimination step, the second
     * in the pivot division. */
    mixmat_t ov3 = mixmat_zero(3);
    const int64_t o0[3] = {997011, 101737, 206466}, o1[3] = {4927, 214385, 423843},
                  o2[3] = {28684, 342871, 657058};
    mixmat_set_row_counts(&ov3, 0, o0, 3);
    mixmat_set_row_counts(&ov3, 1, o1, 3);
    mixmat_set_row_counts(&ov3, 2, o2, 3);
    CHECK(mixmat_is_stochastic(&ov3), "overflow case 1 is stochastic");
    CHECK(!mixmat_stationary(&ov3, &pi), "overflow in elimination refused");
    mixmat_t ov4 = mixmat_zero(4);
    const int64_t q0[4] = {1172921604, 307, 202, 244050605}, q1[4] = {910, 3, 641840579, 260},
                  q2[4] = {1306697071, 302326301, 97782, 13759}, q3[4] = {1, 1690577418, 2, 896};
    mixmat_set_row_counts(&ov4, 0, q0, 4);
    mixmat_set_row_counts(&ov4, 1, q1, 4);
    mixmat_set_row_counts(&ov4, 2, q2, 4);
    mixmat_set_row_counts(&ov4, 3, q3, 4);
    CHECK(mixmat_is_stochastic(&ov4), "overflow case 2 is stochastic");
    CHECK(!mixmat_stationary(&ov4, &pi), "overflow in pivot division refused");

    mixvec_t untouched;
    untouched.n = 77;
    untouched.valid = false;
    mixmat_t id = mixmat_identity(3);
    CHECK(!mixmat_stationary(&id, &untouched), "identity: no unique pi");
    CHECK(untouched.n == 77, "refusal leaves out alone");
    /* two closed classes {0,1} and {2} */
    mixmat_t two = mixmat_zero(3);
    const int64_t d0[3] = {1, 1, 0}, d1[3] = {1, 1, 0}, d2[3] = {0, 0, 1};
    mixmat_set_row_counts(&two, 0, d0, 3);
    mixmat_set_row_counts(&two, 1, d1, 3);
    mixmat_set_row_counts(&two, 2, d2, 3);
    CHECK(!mixmat_stationary(&two, &pi), "two closed classes refused");
    mixmat_t sw = mixmat_zero(2);
    const int64_t s0[2] = {0, 1}, s1[2] = {1, 0};
    mixmat_set_row_counts(&sw, 0, s0, 2);
    mixmat_set_row_counts(&sw, 1, s1, 2);
    CHECK(mixmat_stationary(&sw, &pi) && req(pi.v[0], 1, 2) && req(pi.v[1], 1, 2),
          "swap pi = (1/2, 1/2)");

    mixmat_t ns = mixmat_zero(2);
    CHECK(!mixmat_stationary(&ns, &pi), "non-stochastic refused");
    CHECK(!mixmat_stationary(NULL, &pi), "null matrix refused");
    CHECK(!mixmat_stationary(&t, NULL), "null out refused");
    mixmat_t ti = t;
    ti.valid = false;
    CHECK(!mixmat_stationary(&ti, &pi), "invalid matrix refused");
}

static void test_permutation(void)
{
    mixmat_t id = mixmat_identity(3);
    CHECK(mixmat_is_permutation(&id), "identity is a permutation");
    mixmat_t cyc = mixmat_zero(3);
    const int64_t e0[3] = {0, 1, 0}, e1[3] = {0, 0, 1}, e2[3] = {1, 0, 0};
    mixmat_set_row_counts(&cyc, 0, e0, 3);
    mixmat_set_row_counts(&cyc, 1, e1, 3);
    mixmat_set_row_counts(&cyc, 2, e2, 3);
    CHECK(mixmat_is_permutation(&cyc), "3-cycle is a permutation");
    mixmat_t col = cyc;
    col.a[2][0] = rat_zero();
    col.a[2][1] = rat_from_int(1); /* rows fine, column 1 has two ones */
    CHECK(mixmat_is_stochastic(&col), "collapsing map is stochastic");
    CHECK(!mixmat_is_permutation(&col), "collapsing map is not a permutation");
    mixmat_t mix = mixmat_zero(2);
    const int64_t m0[2] = {1, 1}, m1[2] = {0, 1};
    mixmat_set_row_counts(&mix, 0, m0, 2);
    mixmat_set_row_counts(&mix, 1, m1, 2);
    CHECK(!mixmat_is_permutation(&mix), "mixing row is not a permutation");
    mixmat_t mixl = mixmat_zero(2);
    mixmat_set_row_counts(&mixl, 0, m1, 2);
    mixmat_set_row_counts(&mixl, 1, m0, 2);
    CHECK(!mixmat_is_permutation(&mixl), "mixing last row is not a permutation");
    mixmat_t z = mixmat_zero(3);
    CHECK(!mixmat_is_permutation(&z), "zero matrix is not a permutation");
    CHECK(!mixmat_is_permutation(NULL), "null is not a permutation");
    mixmat_t big = mixmat_identity(MIXMAT_MAX);
    CHECK(mixmat_is_permutation(&big), "identity(13) is a permutation");
    mixmat_t idj = mixmat_identity(3);
    for (uint32_t i = 0; i < MIXMAT_MAX; i++) idj.a[i][3] = idj.a[3][i] = rat_from_int(1);
    CHECK(mixmat_is_permutation(&idj), "ones outside n do not count");
    big.a[12][12] = rat_zero();
    big.a[12][0] = rat_from_int(1);
    CHECK(!mixmat_is_permutation(&big), "last column checked");
}

/* ---- fault injection into the rational layer ---------------------------
 * mixmat_selfcheck exists to CATCH a broken substrate, so it is tested the
 * same way: link with -Wl,--wrap=<fn> for the rational primitives mixmat.c
 * calls, break one primitive at a time, and require the self-check to report
 * EXACTLY the number of problems it reports for that fault today. A check
 * removed or weakened inside the self-check changes one of these counts. */
enum {
    F_NONE,
    F_EQ_FALSE,
    F_EQ_TRUE,
    F_CMP_NEG,
    F_ZERO_TRUE,
    F_ZERO_FALSE,
    F_ADD_BAD,
    F_MUL_BAD,
    F_MAKE_BAD,
    F_ADD_OFF,
    F_MUL_OFF,
    F_SUB_BAD,
    F_DIV_BAD,
    F_DIV_OFF,
    F_SUB_OFF,
    F_EQ_FLIP,
    F_ZERO_FLIP,
    F_CMP_FLIP,
    F_MAKE_OFF,
    F_COUNT
};
static int g_fault;
static uint32_t g_calls;
rat_t __real_rat_add(rat_t a, rat_t b);
rat_t __real_rat_sub(rat_t a, rat_t b);
rat_t __real_rat_mul(rat_t a, rat_t b);
rat_t __real_rat_div(rat_t a, rat_t b);
rat_t __real_rat_make(int64_t n, int64_t d);
bool __real_rat_eq(rat_t a, rat_t b);
int __real_rat_cmp(rat_t a, rat_t b);
bool __real_rat_is_zero(rat_t a);
rat_t __wrap_rat_add(rat_t a, rat_t b);
rat_t __wrap_rat_sub(rat_t a, rat_t b);
rat_t __wrap_rat_mul(rat_t a, rat_t b);
rat_t __wrap_rat_div(rat_t a, rat_t b);
rat_t __wrap_rat_make(int64_t n, int64_t d);
bool __wrap_rat_eq(rat_t a, rat_t b);
int __wrap_rat_cmp(rat_t a, rat_t b);
bool __wrap_rat_is_zero(rat_t a);

static rat_t bad_rat(void)
{
    rat_t r = {0, 1, false};
    return r;
}
rat_t __wrap_rat_add(rat_t a, rat_t b)
{
    rat_t r = __real_rat_add(a, b);
    if (g_fault == F_ADD_BAD) return bad_rat();
    if (g_fault == F_ADD_OFF && (++g_calls % 7u) == 0)
        return __real_rat_add(r, __real_rat_make(1, 97));
    return r;
}
rat_t __wrap_rat_sub(rat_t a, rat_t b)
{
    if (g_fault == F_SUB_BAD) return bad_rat();
    rat_t r = __real_rat_sub(a, b);
    if (g_fault == F_SUB_OFF && (++g_calls % 4u) == 0)
        return __real_rat_add(r, __real_rat_make(1, 83));
    return r;
}
rat_t __wrap_rat_mul(rat_t a, rat_t b)
{
    rat_t r = __real_rat_mul(a, b);
    if (g_fault == F_MUL_BAD) return bad_rat();
    if (g_fault == F_MUL_OFF && (++g_calls % 5u) == 0)
        return __real_rat_add(r, __real_rat_make(1, 89));
    return r;
}
rat_t __wrap_rat_div(rat_t a, rat_t b)
{
    if (g_fault == F_DIV_BAD) return bad_rat();
    rat_t r = __real_rat_div(a, b);
    if (g_fault == F_DIV_OFF && (++g_calls % 3u) == 0)
        return __real_rat_add(r, __real_rat_make(1, 101));
    return r;
}
static int g_record;
static int64_t g_mk[128][2];
static uint32_t g_nmk;
rat_t __wrap_rat_make(int64_t n, int64_t d)
{
    if (g_record) {
        if (g_nmk < 128) g_mk[g_nmk][0] = n, g_mk[g_nmk][1] = d;
        g_nmk++;
    }
    if (g_fault == F_MAKE_BAD) return bad_rat();
    if (g_fault == F_MAKE_OFF && (++g_calls % 3u) == 0) return __real_rat_make(n + 1, d);
    return __real_rat_make(n, d);
}
bool __wrap_rat_eq(rat_t a, rat_t b)
{
    if (g_fault == F_EQ_FALSE) return false;
    if (g_fault == F_EQ_TRUE) return true;
    if (g_fault == F_EQ_FLIP && (++g_calls % 3u) == 0) return !__real_rat_eq(a, b);
    return __real_rat_eq(a, b);
}
int __wrap_rat_cmp(rat_t a, rat_t b)
{
    if (g_fault == F_CMP_NEG) return -1;
    if (g_fault == F_CMP_FLIP && (++g_calls % 5u) == 0) return -__real_rat_cmp(a, b);
    return __real_rat_cmp(a, b);
}
bool __wrap_rat_is_zero(rat_t a)
{
    if (g_fault == F_ZERO_TRUE) return true;
    if (g_fault == F_ZERO_FALSE) return false;
    if (g_fault == F_ZERO_FLIP && (++g_calls % 3u) == 0) return !__real_rat_is_zero(a);
    return __real_rat_is_zero(a);
}

/* The self-check's data, read back through the only place it becomes
 * rationals (rat_make in set_row_counts, plus its two spot checks), against
 * the compounds and matrices its comments document. */
static void test_selfcheck_data(void)
{
    static const int64_t want[][2] = {
        {2, 3},   {0, 3},   {0, 3},  {1, 3},  /* H2O */
        {30, 53}, {21, 53}, {0, 53}, {2, 53}, /* C21H30O2 */
        {9, 16},  {4, 16},  {1, 16}, {2, 16}, /* C4H9NO2 */
        {12, 25}, {10, 25}, {2, 25}, {1, 25}, /* C10H12N2O */
        {2, 3},   {21, 53},                   /* spot checks */
        {1, 2},   {1, 2},   {0, 2},           /* closure: (1,1,0) */
        {0, 2},   {1, 2},   {1, 2},           /* (0,1,1) */
        {1, 2},   {0, 2},   {1, 2},           /* (1,0,1) */
        {2, 4},   {1, 4},   {1, 4},           /* fixed point: (2,1,1) */
        {1, 3},   {1, 3},   {1, 3},           /* (1,1,1) */
        {1, 4},   {1, 4},   {2, 4},           /* (1,1,2) */
        {0, 1},   {1, 1},   {0, 1},           /* 3-cycle */
        {0, 1},   {0, 1},   {1, 1},  {1, 1},  {0, 1}, {0, 1},
    };
    const uint32_t n = sizeof want / sizeof want[0];
    g_nmk = 0;
    g_record = 1;
    uint32_t bad = mixmat_selfcheck();
    g_record = 0;
    CHECK(bad == 0, "selfcheck healthy while recorded");
    CHECK(g_nmk == n, "selfcheck builds exactly its documented rows");
    int ok = 1;
    for (uint32_t i = 0; i < n && i < g_nmk; i++)
        if (g_mk[i][0] != want[i][0] || g_mk[i][1] != want[i][1]) {
            printf("  rat_make #%u = %lld/%lld\n", i, (long long) g_mk[i][0],
                   (long long) g_mk[i][1]);
            ok = 0;
        }
    CHECK(ok, "selfcheck weights are the documented counts");
}

static void test_selfcheck_faults(void)
{
    static const uint32_t want[F_COUNT] = {0,  65, 3, 23, 1, 2,  13, 8,  23, 34,
                                           34, 1,  1, 4,  1, 36, 3,  21, 25};
    for (int f = 0; f < F_COUNT; f++) {
        g_fault = f;
        g_calls = 0;
        uint32_t got = mixmat_selfcheck();
        g_fault = F_NONE;
        if (got != want[f]) printf("  fault %d -> %u\n", f, got);
        CHECK(got == want[f], "selfcheck reports the exact problem count for the fault");
    }
}

int main(void)
{
    test_construct();
    test_set_row();
    test_stochastic();
    test_mul();
    test_pow();
    test_apply_compound();
    test_stationary();
    test_permutation();
    CHECK(mixmat_selfcheck() == 0, "selfcheck healthy");
    test_selfcheck_faults();
    test_selfcheck_data();
    printf("test_mixmat: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
