/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_xrisk.c — host tests for the exact loss-distribution module.
 *
 * WHAT IT CHECKS
 *   Big-integer primitives against unsigned __int128; closed forms (single
 *   exposure, binomial); brute-force enumeration of all 2^n default patterns
 *   (n <= 16, random rationals) for the pmf, the shared denominator, the
 *   expected loss, VaR and expected shortfall (ES checked against the
 *   quantile-integral definition); finite mixtures against brute force;
 *   paraconsistent fusion and LOW/HIGH bracketing; overflow refusal at the
 *   exact XRISK_BITS boundary; exact decimal printing; every error path.
 *
 * HONEST LIMITS
 *   The brute-force references use unsigned __int128, so they need a 64-bit
 *   host compiler and small denominators. Host-only (TEST_HOST).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xrisk.h"

typedef unsigned __int128 u128;

static int g_pass, g_fail;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (c)                                                                                     \
            g_pass++;                                                                              \
        else {                                                                                     \
            g_fail++;                                                                              \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                    \
        }                                                                                          \
    } while (0)

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void)
{
    uint64_t z = (g_rng += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
static uint64_t rndn(uint64_t n)
{
    return rnd() % n;
}
/* random value shifted right by a random amount < n (sequenced) */
static uint64_t rnds(uint64_t n)
{
    uint64_t v = rnd();
    return v >> rndn(n);
}

static xrisk_model_t g_m, g_m2;
static xrisk_dist_t *D1, *D2, *D3;

/* ---------- helpers ---------- */

static int big_to_u128(const xrisk_big_t *a, u128 *out)
{
    uint32_t i;
    u128 v = 0;
    for (i = 4; i < XRISK_LIMBS; i++)
        if (a->l[i]) return 0;
    for (i = 4; i-- > 0;) v = (v << 32) | a->l[i];
    *out = v;
    return 1;
}

static void u128_to_big(xrisk_big_t *a, u128 v)
{
    uint32_t i;
    xrisk_big_zero(a);
    for (i = 0; i < 4; i++) {
        a->l[i] = (uint32_t) v;
        v >>= 32;
    }
}

static int big_eq_u128(const xrisk_big_t *a, u128 v)
{
    u128 x;
    return big_to_u128(a, &x) && x == v;
}

static uint64_t gcd64(uint64_t a, uint64_t b)
{
    while (b) {
        uint64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

static u128 gcd128(u128 a, u128 b)
{
    while (b) {
        u128 t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/* rational r == a/b (u128) exactly, by cross multiplication via the module's
 * double-width comparison (itself tested against u128 below). */
static int rat_eq_u128(const xrisk_rat_t *r, u128 a, u128 b)
{
    xrisk_rat_t x;
    int c = 9;
    u128_to_big(&x.num, a);
    u128_to_big(&x.den, b);
    if (xrisk_rat_cmp(r, &x, &c)) return 0;
    return c == 0;
}

static int rat_le(const xrisk_rat_t *a, const xrisk_rat_t *b)
{
    int c = 9;
    if (xrisk_rat_cmp(a, b, &c)) return 0;
    return c <= 0;
}

/* ---------- big primitives ---------- */

static void test_big(void)
{
    xrisk_big_t a, b, c, q, r;
    u128 x, y, v;
    uint64_t u;
    int i;

    xrisk_big_from_u64(&a, 0);
    CHECK(xrisk_big_is_zero(&a));
    CHECK(xrisk_big_bits(&a) == 0);
    xrisk_big_from_u64(&a, 1);
    CHECK(!xrisk_big_is_zero(&a));
    CHECK(xrisk_big_bits(&a) == 1);
    xrisk_big_from_u64(&a, 0x8000000000000000ull);
    CHECK(xrisk_big_bits(&a) == 64);
    CHECK(xrisk_big_to_u64(&a, &u) == XRISK_OK && u == 0x8000000000000000ull);
    CHECK(xrisk_big_mul_u64(&a, &a, 2) == XRISK_OK);
    CHECK(xrisk_big_bits(&a) == 65);
    CHECK(xrisk_big_to_u64(&a, &u) == XRISK_E_OVERFLOW);
    CHECK(xrisk_big_to_u64(NULL, &u) == XRISK_E_NULL);
    CHECK(xrisk_big_bits(NULL) == 0);
    CHECK(xrisk_big_is_zero(NULL));

    for (i = 0; i < 3000; i++) {
        uint64_t a0 = rnd(), a1 = rnds(64), b0 = rnds(64);
        uint64_t m = rnds(64);
        int cmp;
        x = ((u128) (a1 >> 2) << 64) | a0; /* < 2^126 */
        y = b0;
        u128_to_big(&a, x);
        u128_to_big(&b, y);
        cmp = xrisk_big_cmp(&a, &b);
        CHECK(cmp == (x > y ? 1 : x < y ? -1 : 0));
        CHECK(xrisk_big_add(&c, &a, &b) == XRISK_OK && big_eq_u128(&c, x + y));
        if (x >= y)
            CHECK(xrisk_big_sub(&c, &a, &b) == XRISK_OK && big_eq_u128(&c, x - y));
        else
            CHECK(xrisk_big_sub(&c, &a, &b) == XRISK_E_RANGE);
        /* mul by m: use 62-bit operand so the product fits u128 */
        u128_to_big(&a, x >> 64 ? (x >> 64) : x);
        v = x >> 64 ? (x >> 64) : x;
        if ((v >> 64) == 0 && (m >> 62) == 0) {
            CHECK(xrisk_big_mul_u64(&c, &a, m) == XRISK_OK && big_eq_u128(&c, v * m));
        }
        if (y != 0) {
            u128_to_big(&a, x);
            CHECK(xrisk_big_divmod(&q, &r, &a, &b) == XRISK_OK);
            CHECK(big_eq_u128(&q, x / y));
            CHECK(big_eq_u128(&r, x % y));
        }
    }
    /* full 64-bit multiplier (general path) */
    for (i = 0; i < 500; i++) {
        uint64_t s = rnd(), m = rnd();
        xrisk_big_from_u64(&a, s);
        CHECK(xrisk_big_mul_u64(&c, &a, m) == XRISK_OK);
        CHECK(big_eq_u128(&c, (u128) s * m));
    }
    /* multi-limb carry chains */
    xrisk_big_zero(&a);
    for (i = 0; i < XRISK_LIMBS - 1; i++) a.l[i] = 0xFFFFFFFFu;
    xrisk_big_from_u64(&b, 1);
    CHECK(xrisk_big_add(&c, &a, &b) == XRISK_OK);
    CHECK(c.l[XRISK_LIMBS - 1] == 1 && c.l[0] == 0 &&
          xrisk_big_bits(&c) == 32 * (XRISK_LIMBS - 1) + 1);
    CHECK(xrisk_big_sub(&c, &c, &b) == XRISK_OK && xrisk_big_cmp(&c, &a) == 0);
    /* overflow at the full width */
    for (i = 0; i < XRISK_LIMBS; i++) a.l[i] = 0xFFFFFFFFu;
    CHECK(xrisk_big_add(&c, &a, &b) == XRISK_E_OVERFLOW);
    CHECK(xrisk_big_mul_u64(&c, &a, 2) == XRISK_E_OVERFLOW);
    CHECK(xrisk_big_mul_u64(&c, &a, 1) == XRISK_OK);
    CHECK(xrisk_big_mul_u64(&c, &a, 0x100000001ull) == XRISK_E_OVERFLOW);
    CHECK(xrisk_big_mul_u64(&c, &b, 0) == XRISK_OK && xrisk_big_is_zero(&c));
    /* divmod of a huge value by itself and by 1 */
    CHECK(xrisk_big_divmod(&q, &r, &a, &a) == XRISK_OK);
    CHECK(big_eq_u128(&q, 1) && xrisk_big_is_zero(&r));
    CHECK(xrisk_big_divmod(&q, &r, &a, &b) == XRISK_OK && xrisk_big_cmp(&q, &a) == 0);
    xrisk_big_from_u64(&b, 3);
    CHECK(xrisk_big_divmod(&q, NULL, &a, &b) == XRISK_OK);
    CHECK(xrisk_big_mul_u64(&c, &q, 3) == XRISK_OK &&
          xrisk_big_cmp(&c, &a) == 0); /* 2^W-1 = 0 mod 3 */
    xrisk_big_zero(&b);
    CHECK(xrisk_big_divmod(&q, &r, &a, &b) == XRISK_E_DIVZERO);
    CHECK(xrisk_big_divmod(&q, &r, NULL, &a) == XRISK_E_NULL);
    CHECK(xrisk_big_divmod(&q, &r, &a, NULL) == XRISK_E_NULL);
    CHECK(xrisk_big_add(NULL, &a, &b) == XRISK_E_NULL);
    CHECK(xrisk_big_add(&c, NULL, &b) == XRISK_E_NULL);
    CHECK(xrisk_big_add(&c, &a, NULL) == XRISK_E_NULL);
    CHECK(xrisk_big_sub(NULL, &a, &b) == XRISK_E_NULL);
    CHECK(xrisk_big_sub(&c, &a, NULL) == XRISK_E_NULL);
    CHECK(xrisk_big_mul_u64(NULL, &a, 2) == XRISK_E_NULL);
    CHECK(xrisk_big_mul_u64(&c, NULL, 2) == XRISK_E_NULL);
    CHECK(xrisk_big_cmp(NULL, &a) == 0);
    /* copy */
    xrisk_big_from_u64(&a, 12345);
    xrisk_big_copy(&b, &a);
    CHECK(xrisk_big_cmp(&a, &b) == 0);
}

/* ---------- rationals ---------- */

static void set_rat(xrisk_rat_t *r, u128 n, u128 d)
{
    u128_to_big(&r->num, n);
    u128_to_big(&r->den, d);
}

static void test_rat(void)
{
    xrisk_rat_t r, s;
    char buf[256];
    int c, i;

    set_rat(&r, 6, 8);
    CHECK(xrisk_rat_reduce(&r) == XRISK_OK && big_eq_u128(&r.num, 3) && big_eq_u128(&r.den, 4));
    set_rat(&r, 0, 8);
    CHECK(xrisk_rat_reduce(&r) == XRISK_OK && big_eq_u128(&r.num, 0) && big_eq_u128(&r.den, 1));
    set_rat(&r, 7, 13);
    CHECK(xrisk_rat_reduce(&r) == XRISK_OK && big_eq_u128(&r.num, 7) && big_eq_u128(&r.den, 13));
    set_rat(&r, 12, 0);
    CHECK(xrisk_rat_reduce(&r) == XRISK_E_DIVZERO);
    CHECK(xrisk_rat_reduce(NULL) == XRISK_E_NULL);
    for (i = 0; i < 400; i++) {
        u128 a = (u128) (rnd() >> 1), b;
        a = a * rnds(60) + 1;
        b = (u128) (rnd() >> 3) + 1;
        u128 k = (u128) (rndn(1000) + 1);
        k <<= rndn(20);
        u128 g = gcd128(a, b);
        if ((a >> 100) || (b * k >> 120)) continue;
        set_rat(&r, a * k, b * k);
        CHECK(xrisk_rat_reduce(&r) == XRISK_OK);
        CHECK(big_eq_u128(&r.num, a / g) && big_eq_u128(&r.den, b / g));
    }
    /* comparisons vs u128 cross products */
    for (i = 0; i < 1000; i++) {
        u128 a = rnd() >> 2, b = (rnd() >> 2) + 1, x = rnd() >> 2, y = (rnd() >> 2) + 1;
        u128 l = a * y, rr = x * b;
        set_rat(&r, a, b);
        set_rat(&s, x, y);
        CHECK(xrisk_rat_cmp(&r, &s, &c) == XRISK_OK && c == (l > rr ? 1 : l < rr ? -1 : 0));
    }
    set_rat(&r, 1, 3);
    set_rat(&s, 2, 6);
    CHECK(xrisk_rat_cmp(&r, &s, &c) == XRISK_OK && c == 0);
    set_rat(&s, 1, 0);
    CHECK(xrisk_rat_cmp(&r, &s, &c) == XRISK_E_DIVZERO);
    CHECK(xrisk_rat_cmp(&s, &r, &c) == XRISK_E_DIVZERO);
    CHECK(xrisk_rat_cmp(NULL, &r, &c) == XRISK_E_NULL);
    CHECK(xrisk_rat_cmp(&r, NULL, &c) == XRISK_E_NULL);
    CHECK(xrisk_rat_cmp(&r, &r, NULL) == XRISK_E_NULL);
    /* huge-vs-huge needs the double width */
    {
        xrisk_rat_t h1, h2;
        int k;
        for (k = 0; k < XRISK_LIMBS; k++) {
            h1.num.l[k] = 0xFFFFFFFFu;
            h1.den.l[k] = 0xFFFFFFFFu;
            h2.num.l[k] = 0xFFFFFFFFu;
            h2.den.l[k] = 0xFFFFFFFFu;
        }
        h2.den.l[0] = 0xFFFFFFFEu; /* h2 slightly larger than 1 */
        CHECK(xrisk_rat_cmp(&h1, &h2, &c) == XRISK_OK && c == -1);
        CHECK(xrisk_rat_cmp(&h2, &h1, &c) == XRISK_OK && c == 1);
    }

    /* decimal printing (truncated) */
    set_rat(&r, 1, 3);
    CHECK(xrisk_rat_to_dec(&r, 5, buf, sizeof buf) == XRISK_OK && !strcmp(buf, "0.33333"));
    set_rat(&r, 2, 3);
    CHECK(xrisk_rat_to_dec(&r, 5, buf, sizeof buf) == XRISK_OK && !strcmp(buf, "0.66666"));
    set_rat(&r, 1, 8);
    CHECK(xrisk_rat_to_dec(&r, 3, buf, sizeof buf) == XRISK_OK && !strcmp(buf, "0.125"));
    CHECK(xrisk_rat_to_dec(&r, 5, buf, sizeof buf) == XRISK_OK && !strcmp(buf, "0.12500"));
    CHECK(xrisk_rat_to_dec(&r, 2, buf, sizeof buf) == XRISK_OK && !strcmp(buf, "0.12"));
    set_rat(&r, 7, 2);
    CHECK(xrisk_rat_to_dec(&r, 0, buf, sizeof buf) == XRISK_OK && !strcmp(buf, "3"));
    CHECK(xrisk_rat_to_dec(&r, 1, buf, sizeof buf) == XRISK_OK && !strcmp(buf, "3.5"));
    set_rat(&r, 0, 2);
    CHECK(xrisk_rat_to_dec(&r, 2, buf, sizeof buf) == XRISK_OK && !strcmp(buf, "0.00"));
    set_rat(&r, 999, 1000);
    CHECK(xrisk_rat_to_dec(&r, 2, buf, sizeof buf) == XRISK_OK && !strcmp(buf, "0.99"));
    set_rat(&r, (u128) 1000000000000000000ull * 1000000000000ull + 7, 1);
    CHECK(xrisk_rat_to_dec(&r, 0, buf, sizeof buf) == XRISK_OK &&
          !strcmp(buf, "1000000000000000000000000000007"));
    set_rat(&r, 22, 7);
    CHECK(xrisk_rat_to_dec(&r, 10, buf, sizeof buf) == XRISK_OK && !strcmp(buf, "3.1428571428"));
    /* buffer sizes: "3.14" needs 5 bytes */
    CHECK(xrisk_rat_to_dec(&r, 2, buf, 5) == XRISK_OK && !strcmp(buf, "3.14"));
    CHECK(xrisk_rat_to_dec(&r, 2, buf, 4) == XRISK_E_BUF && buf[0] == '\0');
    CHECK(xrisk_rat_to_dec(&r, 0, buf, 2) == XRISK_OK && !strcmp(buf, "3"));
    CHECK(xrisk_rat_to_dec(&r, 0, buf, 1) == XRISK_E_BUF);
    CHECK(xrisk_rat_to_dec(&r, 0, buf, 0) == XRISK_E_BUF);
    CHECK(xrisk_rat_to_dec(NULL, 0, buf, 8) == XRISK_E_NULL);
    CHECK(xrisk_rat_to_dec(&r, 0, NULL, 8) == XRISK_E_NULL);
    set_rat(&r, 1, 0);
    CHECK(xrisk_rat_to_dec(&r, 2, buf, sizeof buf) == XRISK_E_DIVZERO);
}

/* ---------- fusion ---------- */

static xrisk_q_t Q(int64_t n, int64_t d)
{
    xrisk_q_t q;
    q.num = n;
    q.den = d;
    return q;
}

static void test_fuse(void)
{
    xrisk_q_t src[XRISK_MAX_SOURCES + 1], lo, hi;
    xrisk_state_t st;

    src[0] = Q(2, 100);
    src[1] = Q(25, 1000);
    src[2] = Q(3, 200);
    /* spread = 0.025 - 0.015 = 0.01 */
    CHECK(xrisk_fuse(src, 3, Q(1, 100), &lo, &hi, &st) == XRISK_OK);
    CHECK(st == XRISK_TRUE && lo.num == 1 && lo.den == 40 && hi.num == 1 && hi.den == 40);
    CHECK(xrisk_fuse(src, 3, Q(99, 10000), &lo, &hi, &st) == XRISK_OK);
    CHECK(st == XRISK_BOTH && lo.num == 3 && lo.den == 200 && hi.num == 1 && hi.den == 40);
    CHECK(xrisk_fuse(src, 3, Q(101, 10000), &lo, &hi, &st) == XRISK_OK && st == XRISK_TRUE);
    CHECK(xrisk_fuse(src, 1, Q(0, 1), &lo, &hi, &st) == XRISK_OK && st == XRISK_TRUE);
    CHECK(lo.num == 1 && lo.den == 50 && hi.num == 1 && hi.den == 50);
    src[3] = Q(4, 200); /* equal to src[0] */
    CHECK(xrisk_fuse(src, 1, Q(0, 1), &lo, &hi, &st) == XRISK_OK);
    {
        xrisk_q_t e[2];
        e[0] = Q(2, 100);
        e[1] = Q(4, 200);
        CHECK(xrisk_fuse(e, 2, Q(0, 7), &lo, &hi, &st) == XRISK_OK && st == XRISK_TRUE);
        e[1] = Q(0, 5);
        CHECK(xrisk_fuse(e, 2, Q(0, 7), &lo, &hi, &st) == XRISK_OK && st == XRISK_BOTH);
        CHECK(lo.num == 0 && lo.den == 1 && hi.num == 1 && hi.den == 50);
        /* order independence: max first */
        e[0] = Q(9, 10);
        e[1] = Q(1, 10);
        CHECK(xrisk_fuse(e, 2, Q(1, 2), &lo, &hi, &st) == XRISK_OK && st == XRISK_BOTH);
        CHECK(lo.num == 1 && lo.den == 10 && hi.num == 9 && hi.den == 10);
        CHECK(xrisk_fuse(e, 2, Q(8, 10), &lo, &hi, &st) == XRISK_OK && st == XRISK_TRUE);
        CHECK(lo.num == 9 && hi.num == 9);
        /* large denominators near 2^62 */
        e[0] = Q(1, 4611686018427387847LL);
        e[1] = Q(1, 4611686018427387903LL);
        CHECK(xrisk_fuse(e, 2, Q(0, 1), &lo, &hi, &st) == XRISK_OK && st == XRISK_BOTH);
        CHECK(hi.den == 4611686018427387847LL && lo.den == 4611686018427387903LL);
    }
    /* NEITHER and errors */
    st = XRISK_TRUE;
    CHECK(xrisk_fuse(src, 0, Q(0, 1), &lo, &hi, &st) == XRISK_E_NEITHER && st == XRISK_NEITHER);
    CHECK(xrisk_fuse(NULL, 0, Q(0, 1), &lo, &hi, &st) == XRISK_E_NEITHER);
    CHECK(xrisk_fuse(NULL, 2, Q(0, 1), &lo, &hi, &st) == XRISK_E_NULL);
    CHECK(xrisk_fuse(src, 2, Q(0, 1), NULL, &hi, &st) == XRISK_E_NULL);
    CHECK(xrisk_fuse(src, 2, Q(0, 1), &lo, NULL, &st) == XRISK_E_NULL);
    CHECK(xrisk_fuse(src, 2, Q(0, 1), &lo, &hi, NULL) == XRISK_E_NULL);
    CHECK(xrisk_fuse(src, XRISK_MAX_SOURCES + 1, Q(0, 1), &lo, &hi, &st) == XRISK_E_RANGE);
    CHECK(xrisk_fuse(src, 2, Q(0, 0), &lo, &hi, &st) == XRISK_E_PROB);
    CHECK(xrisk_fuse(src, 2, Q(-1, 1), &lo, &hi, &st) == XRISK_E_PROB);
    src[1] = Q(3, 2);
    CHECK(xrisk_fuse(src, 2, Q(0, 1), &lo, &hi, &st) == XRISK_E_PROB);
    src[1] = Q(-1, 2);
    CHECK(xrisk_fuse(src, 2, Q(0, 1), &lo, &hi, &st) == XRISK_E_PROB);
    src[1] = Q(1, 0);
    CHECK(xrisk_fuse(src, 2, Q(0, 1), &lo, &hi, &st) == XRISK_E_PROB);
    src[1] = Q(1, -2);
    CHECK(xrisk_fuse(src, 2, Q(0, 1), &lo, &hi, &st) == XRISK_E_PROB);
    src[1] = Q(1, 1);
    CHECK(xrisk_fuse(src, 2, Q(0, 1), &lo, &hi, &st) == XRISK_OK && st == XRISK_BOTH);
    CHECK(XRISK_NEITHER == 0 && XRISK_TRUE == 1 && XRISK_FALSE == 2 && XRISK_BOTH == 3);
    /* exactly XRISK_MAX_SOURCES sources is allowed; entries beyond n are ignored */
    {
        xrisk_q_t e[XRISK_MAX_SOURCES + 1];
        int k;
        for (k = 0; k < XRISK_MAX_SOURCES; k++) e[k] = Q(k + 1, 100);
        e[XRISK_MAX_SOURCES] = Q(5, 0); /* invalid, but outside n */
        CHECK(xrisk_fuse(e, XRISK_MAX_SOURCES, Q(1, 1), &lo, &hi, &st) == XRISK_OK);
        CHECK(st == XRISK_TRUE && lo.num * 100 == XRISK_MAX_SOURCES * lo.den);
        CHECK(xrisk_fuse(e, XRISK_MAX_SOURCES, Q(0, 1), &lo, &hi, &st) == XRISK_OK);
        CHECK(st == XRISK_BOTH && lo.num == 1 && lo.den == 100);
    }
}

/* ---------- closed forms ---------- */

static u128 binom(int n, int k)
{
    u128 r = 1;
    int i;
    for (i = 1; i <= k; i++) r = r * (u128) (n - k + i) / (u128) i;
    return r;
}

static void test_closed(void)
{
    xrisk_rat_t r;
    uint32_t v;
    int k, n;

    /* single exposure p = 3/10, loss 5 */
    CHECK(xrisk_model_init(&g_m, 1, 1) == XRISK_OK);
    CHECK(xrisk_model_set_loss(&g_m, 0, 5) == XRISK_OK);
    CHECK(xrisk_model_set_p(&g_m, 0, 0, 3, 10) == XRISK_OK);
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_OK);
    CHECK(D1->max_loss == 5 && big_eq_u128(&D1->den, 10));
    CHECK(xrisk_pmf(D1, 0, &r) == XRISK_OK && big_eq_u128(&r.num, 7) && big_eq_u128(&r.den, 10));
    for (k = 1; k < 5; k++)
        CHECK(xrisk_pmf(D1, (uint32_t) k, &r) == XRISK_OK && big_eq_u128(&r.num, 0));
    CHECK(xrisk_pmf(D1, 5, &r) == XRISK_OK && big_eq_u128(&r.num, 3));
    CHECK(xrisk_pmf(D1, 6, &r) == XRISK_OK && big_eq_u128(&r.num, 0)); /* beyond support */
    CHECK(xrisk_tail(D1, 0, &r) == XRISK_OK && big_eq_u128(&r.num, 10));
    CHECK(xrisk_tail(D1, 1, &r) == XRISK_OK && big_eq_u128(&r.num, 3));
    CHECK(xrisk_tail(D1, 5, &r) == XRISK_OK && big_eq_u128(&r.num, 3));
    CHECK(xrisk_tail(D1, 6, &r) == XRISK_OK && big_eq_u128(&r.num, 0));
    CHECK(xrisk_cdf(D1, 4, &r) == XRISK_OK && big_eq_u128(&r.num, 7));
    CHECK(xrisk_cdf(D1, 5, &r) == XRISK_OK && big_eq_u128(&r.num, 10));
    CHECK(xrisk_cdf(D1, 999, &r) == XRISK_OK && big_eq_u128(&r.num, 10));
    CHECK(xrisk_expected_loss(D1, &r) == XRISK_OK && rat_eq_u128(&r, 3, 2));
    /* VaR boundaries: P(L <= 0) = 7/10 exactly */
    CHECK(xrisk_var(D1, 7, 10, &v) == XRISK_OK && v == 0);
    CHECK(xrisk_var(D1, 70, 100, &v) == XRISK_OK && v == 0);
    CHECK(xrisk_var(D1, 7000001, 10000000, &v) == XRISK_OK && v == 5);
    CHECK(xrisk_var(D1, 6999999, 10000000, &v) == XRISK_OK && v == 0);
    CHECK(xrisk_var(D1, 1, 1000000, &v) == XRISK_OK && v == 0);
    CHECK(xrisk_var(D1, 999999, 1000000, &v) == XRISK_OK && v == 5);
    /* ES: alpha = 7/10 -> v=0, ES = E[L 1{L>0}]/(3/10) = 5 */
    CHECK(xrisk_es(D1, 7, 10, &r) == XRISK_OK && rat_eq_u128(&r, 5, 1));
    /* alpha = 1/2: v=0, ES = (3/2 + 0*(7/10-1/2)) / (1/2) = 3 */
    CHECK(xrisk_es(D1, 1, 2, &r) == XRISK_OK && rat_eq_u128(&r, 3, 1));
    /* alpha = 9/10: v=5, ES = (0 + 5*(1 - 9/10)) / (1/10) = 5 */
    CHECK(xrisk_es(D1, 9, 10, &r) == XRISK_OK && rat_eq_u128(&r, 5, 1));
    CHECK(xrisk_rat_reduce(&r) == XRISK_OK && big_eq_u128(&r.num, 5) && big_eq_u128(&r.den, 1));

    /* p = 1/10 loss 1: ES_{1/2} = 1/5 */
    CHECK(xrisk_model_set_loss(&g_m, 0, 1) == XRISK_OK);
    CHECK(xrisk_model_set_p(&g_m, 0, 0, 1, 10) == XRISK_OK);
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_OK);
    CHECK(xrisk_es(D1, 1, 2, &r) == XRISK_OK && rat_eq_u128(&r, 1, 5));
    CHECK(xrisk_es(D1, 95, 100, &r) == XRISK_OK && rat_eq_u128(&r, 1, 1));

    /* binomial: n identical exposures p = 3/10, unit loss */
    for (n = 1; n <= 30; n += 29) {
        u128 den = 1, tot = 0;
        for (k = 0; k < n; k++) den *= 10;
        CHECK(xrisk_model_init(&g_m, (uint32_t) n, 1) == XRISK_OK);
        for (k = 0; k < n; k++) {
            CHECK(xrisk_model_set_loss(&g_m, (uint32_t) k, 1) == XRISK_OK);
            CHECK(xrisk_model_set_p(&g_m, 0, (uint32_t) k, 3, 10) == XRISK_OK);
        }
        CHECK(xrisk_compute(&g_m, XRISK_SIDE_HIGH, D1) == XRISK_OK);
        CHECK(big_eq_u128(&D1->den, den));
        for (k = 0; k <= n; k++) {
            u128 e = binom(n, k);
            int j;
            for (j = 0; j < k; j++) e *= 3;
            for (j = k; j < n; j++) e *= 7;
            CHECK(xrisk_pmf(D1, (uint32_t) k, &r) == XRISK_OK && big_eq_u128(&r.num, e));
            tot += e;
        }
        CHECK(tot == den);
        CHECK(xrisk_expected_loss(D1, &r) == XRISK_OK && rat_eq_u128(&r, (u128) 3 * n, 10));
    }
    /* p = 0 and p = 1 exposures, and zero-loss exposure */
    CHECK(xrisk_model_init(&g_m, 4, 1) == XRISK_OK);
    CHECK(xrisk_model_set_loss(&g_m, 0, 3) == XRISK_OK);
    CHECK(xrisk_model_set_p(&g_m, 0, 0, 0, 7) == XRISK_OK);
    CHECK(xrisk_model_set_loss(&g_m, 1, 2) == XRISK_OK);
    CHECK(xrisk_model_set_p(&g_m, 0, 1, 5, 5) == XRISK_OK);
    CHECK(xrisk_model_set_loss(&g_m, 2, 0) == XRISK_OK);
    CHECK(xrisk_model_set_p(&g_m, 0, 2, 1, 3) == XRISK_OK);
    CHECK(xrisk_model_set_loss(&g_m, 3, 1) == XRISK_OK);
    CHECK(xrisk_model_set_p(&g_m, 0, 3, 1, 4) == XRISK_OK);
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_OK);
    CHECK(D1->max_loss == 6 && big_eq_u128(&D1->den, 12)); /* 1*1*3*4 */
    CHECK(xrisk_pmf(D1, 2, &r) == XRISK_OK && rat_eq_u128(&r, 3, 4));
    CHECK(xrisk_pmf(D1, 3, &r) == XRISK_OK && rat_eq_u128(&r, 1, 4));
    CHECK(xrisk_pmf(D1, 0, &r) == XRISK_OK && big_eq_u128(&r.num, 0));
    CHECK(xrisk_var(D1, 1, 1000, &v) == XRISK_OK && v == 2);
    CHECK(xrisk_var(D1, 3, 4, &v) == XRISK_OK && v == 2);
    CHECK(xrisk_var(D1, 751, 1000, &v) == XRISK_OK && v == 3);
    CHECK(xrisk_expected_loss(D1, &r) == XRISK_OK && rat_eq_u128(&r, 9, 4));
}

/* ---------- brute force ---------- */

typedef struct {
    int n, K;
    uint32_t loss[16];
    int64_t pn[XRISK_MAX_SCEN][16], pd[XRISK_MAX_SCEN][16];
    int64_t wn[XRISK_MAX_SCEN], wd[XRISK_MAX_SCEN];
} bf_t;

/* exact reference: masses over den = B * prod m_i, all in u128 */
static void brute(const bf_t *b, u128 *mass, u128 *den, uint32_t *total)
{
    uint64_t mi[16], B = 1;
    int i, s;
    uint32_t pat, tot = 0;
    for (i = 0; i < b->n; i++) tot += b->loss[i];
    *total = tot;
    for (s = 0; s < b->K; s++) {
        uint64_t g = gcd64((uint64_t) b->wn[s], (uint64_t) b->wd[s]);
        uint64_t d = b->wn[s] ? (uint64_t) b->wd[s] / g : 1;
        B = B / gcd64(B, d) * d;
    }
    for (i = 0; i < b->n; i++) {
        mi[i] = 1;
        for (s = 0; s < b->K; s++) {
            uint64_t g = gcd64((uint64_t) b->pn[s][i], (uint64_t) b->pd[s][i]);
            uint64_t d = b->pn[s][i] ? (uint64_t) b->pd[s][i] / g : 1;
            mi[i] = mi[i] / gcd64(mi[i], d) * d;
        }
    }
    *den = B;
    for (i = 0; i < b->n; i++) *den *= mi[i];
    for (pat = 0; pat <= tot; pat++) mass[pat] = 0;
    for (s = 0; s < b->K; s++) {
        uint64_t wg =
            b->wn[s] ? gcd64((uint64_t) b->wn[s], (uint64_t) b->wd[s]) : (uint64_t) b->wd[s];
        u128 ws = (u128) ((uint64_t) b->wn[s] / wg) * (B / ((uint64_t) b->wd[s] / wg));
        for (pat = 0; pat < (1u << b->n); pat++) {
            u128 pr = ws;
            uint32_t L = 0;
            for (i = 0; i < b->n; i++) {
                uint64_t pg = b->pn[s][i] ? gcd64((uint64_t) b->pn[s][i], (uint64_t) b->pd[s][i])
                                          : (uint64_t) b->pd[s][i];
                u128 pn =
                    (u128) ((uint64_t) b->pn[s][i] / pg) * (mi[i] / ((uint64_t) b->pd[s][i] / pg));
                if (pat >> i & 1u) {
                    pr *= pn;
                    L += b->loss[i];
                } else
                    pr *= (u128) mi[i] - pn;
            }
            mass[L] += pr;
        }
    }
}

static int load_model(xrisk_model_t *m, const bf_t *b)
{
    int i, s;
    if (xrisk_model_init(m, (uint32_t) b->n, (uint32_t) b->K)) return 0;
    for (i = 0; i < b->n; i++)
        if (xrisk_model_set_loss(m, (uint32_t) i, b->loss[i])) return 0;
    for (s = 0; s < b->K; s++) {
        if (xrisk_model_set_weight(m, (uint32_t) s, b->wn[s], b->wd[s])) return 0;
        for (i = 0; i < b->n; i++)
            if (xrisk_model_set_p(m, (uint32_t) s, (uint32_t) i, b->pn[s][i], b->pd[s][i]))
                return 0;
    }
    return 1;
}

/* checks dist d against the brute-force masses: pmf, sum, EL, VaR, ES */
static void check_against(const xrisk_dist_t *d, const u128 *mass, u128 den, uint32_t total)
{
    xrisk_rat_t r;
    u128 sum = 0, el = 0;
    uint32_t k, v;
    int ok = 1, t;
    static const int64_t al[][2] = {{1, 2}, {9, 10}, {99, 100}, {999, 1000}, {1, 3}, {2, 7}};
    CHECK(d->max_loss == total);
    CHECK(big_eq_u128(&d->den, den));
    for (k = 0; k <= total; k++) {
        if (!big_eq_u128(&d->mass[k], mass[k])) ok = 0;
        sum += mass[k];
        el += (u128) k * mass[k];
    }
    CHECK(ok);
    CHECK(sum == den);
    CHECK(xrisk_expected_loss(d, &r) == XRISK_OK && rat_eq_u128(&r, el, den));
    CHECK(xrisk_tail(d, 0, &r) == XRISK_OK && big_eq_u128(&r.num, den));
    for (t = 0; t < 6; t++) {
        u128 an = (u128) al[t][0], ad = (u128) al[t][1], F = 0, Fprev = 0, esn = 0;
        uint32_t ev = 0;
        int found = 0;
        /* VaR: smallest k with F(k) >= alpha; also boundary alpha = F(k) itself */
        for (k = 0; k <= total; k++) {
            F += mass[k];
            if (!found && F * ad >= an * den) {
                ev = k;
                found = 1;
            }
        }
        CHECK(xrisk_var(d, al[t][0], al[t][1], &v) == XRISK_OK && v == ev);
        /* ES*(1-alpha) = sum_k k * max(0, F(k) - max(F(k-1), alpha)), units of 1/(den*ad) */
        F = 0;
        for (k = 0; k <= total; k++) {
            u128 lo;
            Fprev = F;
            F += mass[k];
            lo = Fprev * ad > an * den ? Fprev * ad : an * den;
            if (F * ad > lo) esn += (u128) k * (F * ad - lo);
        }
        CHECK(xrisk_es(d, al[t][0], al[t][1], &r) == XRISK_OK &&
              rat_eq_u128(&r, esn, den * (ad - an)));
    }
    /* VaR exactly at each attained cdf level, and just below */
    {
        u128 F = 0;
        for (k = 0; k < total && k < 64; k++) {
            F += mass[k];
            if (F == 0 || F == den || mass[k] == 0) continue;
            if ((F >> 62) == 0 && (den >> 62) == 0) {
                u128 g = gcd128(F, den);
                CHECK(xrisk_var(d, (int64_t) (F / g), (int64_t) (den / g), &v) == XRISK_OK &&
                      v == k);
            }
        }
    }
}

static void test_brute(void)
{
    static bf_t b;
    static u128 mass[XRISK_MAX_UNITS + 1];
    u128 den;
    uint32_t total;
    int trial, i, s;
    static const int64_t dens[] = {2, 3, 4, 5, 7, 10, 12, 20, 25, 50, 64, 100};

    for (trial = 0; trial < 40; trial++) {
        memset(&b, 0, sizeof b);
        b.n = 1 + (int) rndn(16);
        b.K = trial < 25 ? 1 : 2 + (int) rndn(2);
        for (i = 0; i < b.n; i++) b.loss[i] = (uint32_t) rndn(6);
        if (b.K == 1) {
            b.wn[0] = 1;
            b.wd[0] = 1;
        } else if (b.K == 2) {
            b.wn[0] = 3;
            b.wd[0] = 4;
            b.wn[1] = 2;
            b.wd[1] = 8;
        } else {
            b.wn[0] = 1;
            b.wd[0] = 2;
            b.wn[1] = 1;
            b.wd[1] = 3;
            b.wn[2] = 1;
            b.wd[2] = 6;
        }
        for (s = 0; s < b.K; s++)
            for (i = 0; i < b.n; i++) {
                /* keep den small enough for u128: power-of-ten-ish dens in mixtures */
                int64_t d =
                    b.K == 1 ? dens[rndn(12)] : (int64_t[]){2, 4, 5, 10, 20, 25, 50, 100}[rndn(8)];
                b.pd[s][i] = d;
                b.pn[s][i] = (int64_t) rndn((uint64_t) d + 1);
            }
        CHECK(load_model(&g_m, &b));
        brute(&b, mass, &den, &total);
        CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_OK);
        check_against(D1, mass, den, total);
    }
    /* large denominators (general multiply path): n = 2 dens near 2^62 */
    for (trial = 0; trial < 20; trial++) {
        memset(&b, 0, sizeof b);
        b.n = 2;
        b.K = 1;
        b.wn[0] = b.wd[0] = 1;
        for (i = 0; i < 2; i++) {
            b.loss[i] = 1 + (uint32_t) rndn(3);
            b.pd[0][i] = (int64_t) ((rnd() >> 8) | 1); /* < 2^56: brute ES fits u128 */
            b.pn[0][i] = (int64_t) rndn((uint64_t) b.pd[0][i]);
        }
        CHECK(load_model(&g_m, &b));
        brute(&b, mass, &den, &total);
        CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_OK);
        check_against(D1, mass, den, total);
    }
    /* mixture with large weight denominators (general mixing path) */
    for (trial = 0; trial < 10; trial++) {
        int64_t wd = (int64_t) ((rnd() >> 4) | 1);
        int64_t w0 = (int64_t) rndn((uint64_t) wd) + 1;
        memset(&b, 0, sizeof b);
        b.n = 3;
        b.K = 2;
        b.wn[0] = w0;
        b.wd[0] = wd;
        b.wn[1] = wd - w0;
        b.wd[1] = wd;
        for (s = 0; s < 2; s++)
            for (i = 0; i < 3; i++) {
                b.loss[i] = 1 + (uint32_t) i;
                b.pd[s][i] = 10;
                b.pn[s][i] = (int64_t) rndn(11);
            }
        CHECK(load_model(&g_m, &b));
        brute(&b, mass, &den, &total);
        CHECK(xrisk_compute(&g_m, XRISK_SIDE_HIGH, D1) == XRISK_OK);
        check_against(D1, mass, den, total);
    }
}

/* ---------- conflict -> bounds bracketing ---------- */

static void test_bounds(void)
{
    xrisk_rat_t a, b, c;
    xrisk_q_t src[3];
    xrisk_state_t st;
    uint32_t i, k, vl, vh, vm;
    const int64_t al[][2] = {{9, 10}, {99, 100}, {999, 1000}};
    int t;

    CHECK(xrisk_model_init(&g_m, 40, 2) == XRISK_OK);
    CHECK(xrisk_model_init(&g_m2, 40, 2) == XRISK_OK);
    CHECK(xrisk_model_set_weight(&g_m, 0, 4, 5) == XRISK_OK);
    CHECK(xrisk_model_set_weight(&g_m, 1, 1, 5) == XRISK_OK);
    CHECK(xrisk_model_set_weight(&g_m2, 0, 4, 5) == XRISK_OK);
    CHECK(xrisk_model_set_weight(&g_m2, 1, 1, 5) == XRISK_OK);
    for (i = 0; i < 40; i++) {
        uint32_t L = 1 + (uint32_t) rndn(20);
        int s;
        CHECK(xrisk_model_set_loss(&g_m, i, L) == XRISK_OK);
        CHECK(xrisk_model_set_loss(&g_m2, i, L) == XRISK_OK);
        for (s = 0; s < 2; s++) {
            int64_t base = (int64_t) rndn(300) + 1 + (s ? 300 : 0); /* stressed scenario higher */
            src[0] = Q(base, 10000);
            src[1] = Q(base + (int64_t) rndn(5), 10000);
            src[2] = Q(base + (i % 4 == 0 ? 200 : 0), 10000); /* every 4th: conflicting source */
            CHECK(xrisk_model_set_sources(&g_m, (uint32_t) s, i, src, 3, Q(10, 10000), &st) ==
                  XRISK_OK);
            CHECK(st == (i % 4 == 0 ? XRISK_BOTH : XRISK_TRUE));
            /* middle model: a value inside the conflicting range */
            {
                int64_t mid =
                    i % 4 == 0 ? base + 100 : g_m.p_hi[s][i].num * (10000 / g_m.p_hi[s][i].den);
                CHECK(xrisk_model_set_p(&g_m2, (uint32_t) s, i, mid, 10000) == XRISK_OK);
            }
        }
    }
    CHECK(xrisk_model_conflicts(&g_m) == 10);
    CHECK(xrisk_model_conflicts(&g_m2) == 0);
    CHECK(xrisk_model_conflicts(NULL) == 0);
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_OK);
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_HIGH, D2) == XRISK_OK);
    CHECK(xrisk_compute(&g_m2, XRISK_SIDE_LOW, D3) == XRISK_OK);
    {
        int ok = 1, strict = 0;
        for (k = 0; k <= D1->max_loss; k++) {
            CHECK(xrisk_tail(D1, k, &a) == XRISK_OK);
            CHECK(xrisk_tail(D3, k, &c) == XRISK_OK);
            CHECK(xrisk_tail(D2, k, &b) == XRISK_OK);
            if (!rat_le(&a, &c) || !rat_le(&c, &b)) ok = 0;
            if (!rat_le(&b, &a)) strict = 1;
        }
        CHECK(ok);
        CHECK(strict); /* the bounds really differ */
    }
    CHECK(xrisk_expected_loss(D1, &a) == XRISK_OK && xrisk_expected_loss(D3, &c) == XRISK_OK &&
          xrisk_expected_loss(D2, &b) == XRISK_OK);
    CHECK(rat_le(&a, &c) && rat_le(&c, &b) && !rat_le(&b, &a));
    for (t = 0; t < 3; t++) {
        CHECK(xrisk_var(D1, al[t][0], al[t][1], &vl) == XRISK_OK);
        CHECK(xrisk_var(D3, al[t][0], al[t][1], &vm) == XRISK_OK);
        CHECK(xrisk_var(D2, al[t][0], al[t][1], &vh) == XRISK_OK);
        CHECK(vl <= vm && vm <= vh);
        CHECK(xrisk_es(D1, al[t][0], al[t][1], &a) == XRISK_OK);
        CHECK(xrisk_es(D3, al[t][0], al[t][1], &c) == XRISK_OK);
        CHECK(xrisk_es(D2, al[t][0], al[t][1], &b) == XRISK_OK);
        CHECK(rat_le(&a, &c) && rat_le(&c, &b));
    }
    /* NEITHER leaves the exposure unset: compute refuses */
    CHECK(xrisk_model_init(&g_m, 2, 1) == XRISK_OK);
    CHECK(xrisk_model_set_loss(&g_m, 0, 1) == XRISK_OK);
    CHECK(xrisk_model_set_loss(&g_m, 1, 1) == XRISK_OK);
    CHECK(xrisk_model_set_p(&g_m, 0, 0, 1, 2) == XRISK_OK);
    CHECK(xrisk_model_set_sources(&g_m, 0, 1, src, 0, Q(0, 1), &st) == XRISK_E_NEITHER);
    CHECK(st == XRISK_NEITHER);
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_E_UNSET);
    CHECK(xrisk_model_set_sources(NULL, 0, 1, src, 1, Q(0, 1), &st) == XRISK_E_NULL);
    CHECK(xrisk_model_set_sources(&g_m, 0, 1, src, 1, Q(0, 1), NULL) == XRISK_E_NULL);
    CHECK(xrisk_model_set_sources(&g_m, 1, 1, src, 1, Q(0, 1), &st) == XRISK_E_RANGE);
    CHECK(xrisk_model_set_sources(&g_m, 0, 2, src, 1, Q(0, 1), &st) == XRISK_E_RANGE);
    CHECK(xrisk_model_set_sources(&g_m, 0, 1, src, 1, Q(0, 1), &st) == XRISK_OK &&
          st == XRISK_TRUE);
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_OK);
}

/* ---------- capacity boundaries ---------- */

static void test_capacity(void)
{
    xrisk_q_t src[2];
    xrisk_state_t st;
    xrisk_rat_t r;
    uint32_t i, s;
    /* full obligor capacity: conflict only on the last exposure, scenario 1 only */
    CHECK(xrisk_model_init(&g_m, XRISK_MAX_OBLIGORS, 2) == XRISK_OK);
    CHECK(xrisk_model_set_weight(&g_m, 0, 1, 2) == XRISK_OK);
    CHECK(xrisk_model_set_weight(&g_m, 1, 1, 2) == XRISK_OK);
    for (i = 0; i < XRISK_MAX_OBLIGORS; i++) {
        CHECK(xrisk_model_set_loss(&g_m, i, 1) == XRISK_OK);
        for (s = 0; s < 2; s++) CHECK(xrisk_model_set_p(&g_m, s, i, 1, 10) == XRISK_OK);
    }
    CHECK(xrisk_model_conflicts(&g_m) == 0);
    src[0] = Q(1, 10);
    src[1] = Q(3, 10);
    CHECK(xrisk_model_set_sources(&g_m, 1, XRISK_MAX_OBLIGORS - 1, src, 2, Q(0, 1), &st) ==
          XRISK_OK);
    CHECK(st == XRISK_BOTH);
    CHECK(xrisk_model_conflicts(&g_m) == 1);
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_OK);
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_HIGH, D2) == XRISK_OK);
    /* EL low = 256/10, high = 255/10 + (1/2)(1/10 + 3/10) = 257/10 */
    CHECK(xrisk_expected_loss(D1, &r) == XRISK_OK && rat_eq_u128(&r, 256, 10));
    CHECK(xrisk_expected_loss(D2, &r) == XRISK_OK && rat_eq_u128(&r, 257, 10));
    /* stale data beyond n_obl is not counted */
    CHECK(xrisk_model_init(&g_m, 2, 1) == XRISK_OK);
    g_m.p_lo[0][2].num = 1;
    g_m.p_lo[0][2].den = 2;
    CHECK(xrisk_model_conflicts(&g_m) == 0);
    /* full scenario capacity, equal weights 1/XRISK_MAX_SCEN */
    CHECK(xrisk_model_init(&g_m, 3, XRISK_MAX_SCEN) == XRISK_OK);
    for (i = 0; i < 3; i++) CHECK(xrisk_model_set_loss(&g_m, i, i + 1) == XRISK_OK);
    for (s = 0; s < XRISK_MAX_SCEN; s++) {
        CHECK(xrisk_model_set_weight(&g_m, s, 1, XRISK_MAX_SCEN) == XRISK_OK);
        for (i = 0; i < 3; i++) CHECK(xrisk_model_set_p(&g_m, s, i, s, XRISK_MAX_SCEN) == XRISK_OK);
    }
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_OK);
    /* P(L = 6) = mean over s of (s/16)^3 = sum s^3 / 16^4 = 14400/65536 */
    CHECK(xrisk_pmf(D1, 6, &r) == XRISK_OK && rat_eq_u128(&r, 14400, 65536));
    CHECK(xrisk_model_conflicts(&g_m) == 0);
    g_m.p_hi[XRISK_MAX_SCEN - 1][2].num = 1;
    CHECK(xrisk_model_conflicts(&g_m) == 1);
    /* stale scenario beyond n_scen is not counted */
    g_m.n_scen = 2;
    g_m.p_hi[2][0].num = 3;
    CHECK(xrisk_model_conflicts(&g_m) == 0);
    g_m.p_hi[1][0].num = 3;
    CHECK(xrisk_model_conflicts(&g_m) == 1);
}

/* ---------- overflow refusal ---------- */

static void test_overflow(void)
{
    xrisk_rat_t r;
    uint32_t i, v;
    char buf[64];
    /* 256 exposures with den 2^16: den = 2^4096 has 4097 bits > XRISK_BITS */
    CHECK(xrisk_model_init(&g_m, 256, 1) == XRISK_OK);
    for (i = 0; i < 256; i++) {
        CHECK(xrisk_model_set_loss(&g_m, i, 1) == XRISK_OK);
        CHECK(xrisk_model_set_p(&g_m, 0, i, 1, 65536) == XRISK_OK);
    }
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_E_OVERFLOW);
    CHECK(xrisk_big_is_zero(&D1->den));
    /* metrics refuse a refused dist */
    CHECK(xrisk_pmf(D1, 0, &r) == XRISK_E_UNSET);
    CHECK(xrisk_cdf(D1, 0, &r) == XRISK_E_UNSET);
    CHECK(xrisk_tail(D1, 0, &r) == XRISK_E_UNSET);
    CHECK(xrisk_expected_loss(D1, &r) == XRISK_E_UNSET);
    CHECK(xrisk_var(D1, 1, 2, &v) == XRISK_E_UNSET);
    CHECK(xrisk_es(D1, 1, 2, &r) == XRISK_E_UNSET);
    /* one exposure at 2^15 instead: den = 2^4095, exactly 4096 bits -> accepted */
    CHECK(xrisk_model_set_p(&g_m, 0, 255, 1, 32768) == XRISK_OK);
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_OK);
    CHECK(xrisk_big_bits(&D1->den) == XRISK_BITS);
    CHECK(xrisk_tail(D1, 0, &r) == XRISK_OK && xrisk_big_cmp(&r.num, &D1->den) == 0);
    CHECK(xrisk_pmf(D1, 256, &r) == XRISK_OK && xrisk_rat_reduce(&r) == XRISK_OK);
    CHECK(big_eq_u128(&r.num, 1) && xrisk_big_bits(&r.den) == XRISK_BITS); /* 2^-4095 */
    CHECK(xrisk_expected_loss(D1, &r) == XRISK_OK && xrisk_rat_reduce(&r) == XRISK_OK);
    /* EL = 255/65536 + 1/32768 = 257/65536 */
    CHECK(big_eq_u128(&r.num, 257) && big_eq_u128(&r.den, 65536));
    CHECK(xrisk_rat_to_dec(&r, 8, buf, sizeof buf) == XRISK_OK && !strcmp(buf, "0.00392150"));
    CHECK(xrisk_es(D1, 999, 1000, &r) == XRISK_OK);
    /* lcm overflow across scenarios */
    CHECK(xrisk_model_init(&g_m, 1, 2) == XRISK_OK);
    CHECK(xrisk_model_set_loss(&g_m, 0, 1) == XRISK_OK);
    CHECK(xrisk_model_set_weight(&g_m, 0, 1, 2) == XRISK_OK);
    CHECK(xrisk_model_set_weight(&g_m, 1, 1, 2) == XRISK_OK);
    CHECK(xrisk_model_set_p(&g_m, 0, 0, 1, 4611686018427387847LL) == XRISK_OK);
    CHECK(xrisk_model_set_p(&g_m, 1, 0, 1, 4611686018427387903LL) == XRISK_OK);
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_E_OVERFLOW);
    CHECK(xrisk_model_set_p(&g_m, 1, 0, 2, 4611686018427387847LL) == XRISK_OK);
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_OK);
    CHECK(xrisk_pmf(D1, 1, &r) == XRISK_OK &&
          rat_eq_u128(&r, 3, (u128) 2 * 4611686018427387847ull));
    /* weight lcm overflow */
    CHECK(xrisk_model_set_weight(&g_m, 0, 1, 4611686018427387847LL) == XRISK_OK);
    CHECK(xrisk_model_set_weight(&g_m, 1, 1, 4611686018427387903LL) == XRISK_OK);
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_E_OVERFLOW);
    /* many co-prime large dens: refused, never rounded */
    CHECK(xrisk_model_init(&g_m, 80, 1) == XRISK_OK);
    for (i = 0; i < 80; i++) {
        CHECK(xrisk_model_set_loss(&g_m, i, 1) == XRISK_OK);
        CHECK(xrisk_model_set_p(&g_m, 0, i, 1, 4611686018427387847LL) == XRISK_OK);
    }
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_E_OVERFLOW);
}

/* ---------- error paths ---------- */

static void test_errors(void)
{
    xrisk_rat_t r;
    uint32_t v;
    CHECK(xrisk_model_init(NULL, 1, 1) == XRISK_E_NULL);
    CHECK(xrisk_model_init(&g_m, 0, 1) == XRISK_E_RANGE);
    CHECK(xrisk_model_init(&g_m, XRISK_MAX_OBLIGORS + 1, 1) == XRISK_E_RANGE);
    CHECK(xrisk_model_init(&g_m, XRISK_MAX_OBLIGORS, 1) == XRISK_OK);
    CHECK(xrisk_model_init(&g_m, 1, 0) == XRISK_E_RANGE);
    CHECK(xrisk_model_init(&g_m, 1, XRISK_MAX_SCEN + 1) == XRISK_E_RANGE);
    CHECK(xrisk_model_init(&g_m, 1, XRISK_MAX_SCEN) == XRISK_OK);
    CHECK(xrisk_model_init(&g_m, 2, 1) == XRISK_OK);
    CHECK(xrisk_model_set_loss(NULL, 0, 1) == XRISK_E_NULL);
    CHECK(xrisk_model_set_loss(&g_m, 2, 1) == XRISK_E_RANGE);
    CHECK(xrisk_model_set_loss(&g_m, 0, XRISK_MAX_UNITS + 1) == XRISK_E_RANGE);
    CHECK(xrisk_model_set_loss(&g_m, 0, XRISK_MAX_UNITS) == XRISK_OK);
    CHECK(xrisk_model_set_p(NULL, 0, 0, 1, 2) == XRISK_E_NULL);
    CHECK(xrisk_model_set_p(&g_m, 1, 0, 1, 2) == XRISK_E_RANGE);
    CHECK(xrisk_model_set_p(&g_m, 0, 2, 1, 2) == XRISK_E_RANGE);
    CHECK(xrisk_model_set_p(&g_m, 0, 0, 1, 0) == XRISK_E_PROB);
    CHECK(xrisk_model_set_p(&g_m, 0, 0, 0, 0) == XRISK_E_PROB);
    CHECK(xrisk_model_set_weight(&g_m, 0, 0, 0) == XRISK_E_WEIGHT);
    CHECK(xrisk_model_set_p(&g_m, 0, 0, 1, -2) == XRISK_E_PROB);
    CHECK(xrisk_model_set_p(&g_m, 0, 0, -1, 2) == XRISK_E_PROB);
    CHECK(xrisk_model_set_p(&g_m, 0, 0, 3, 2) == XRISK_E_PROB);
    CHECK(xrisk_model_set_p(&g_m, 0, 0, 2, 2) == XRISK_OK);
    CHECK(xrisk_model_set_p(&g_m, 0, 0, 0, 2) == XRISK_OK);
    CHECK(xrisk_model_set_weight(NULL, 0, 1, 1) == XRISK_E_NULL);
    CHECK(xrisk_model_set_weight(&g_m, 1, 1, 1) == XRISK_E_RANGE);
    CHECK(xrisk_model_set_weight(&g_m, 0, 2, 1) == XRISK_E_WEIGHT);
    CHECK(xrisk_model_set_weight(&g_m, 0, -1, 1) == XRISK_E_WEIGHT);
    CHECK(xrisk_model_set_weight(&g_m, 0, 1, 0) == XRISK_E_WEIGHT);
    CHECK(xrisk_model_set_weight(&g_m, 0, 3, 3) == XRISK_OK);
    /* compute errors */
    CHECK(xrisk_compute(NULL, XRISK_SIDE_LOW, D1) == XRISK_E_NULL);
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, NULL) == XRISK_E_NULL);
    CHECK(xrisk_compute(&g_m, (xrisk_side_t) 2, D1) == XRISK_E_RANGE);
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_E_UNSET); /* loss[1] unset */
    CHECK(xrisk_model_set_loss(&g_m, 1, 1) == XRISK_OK);
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_E_RANGE); /* 4097 units */
    CHECK(xrisk_model_set_loss(&g_m, 0, XRISK_MAX_UNITS - 1) == XRISK_OK);
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_E_UNSET); /* p[1] unset */
    CHECK(xrisk_model_set_p(&g_m, 0, 1, 1, 2) == XRISK_OK);
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_OK);
    CHECK(D1->max_loss == XRISK_MAX_UNITS);
    CHECK(xrisk_pmf(D1, XRISK_MAX_UNITS, &r) == XRISK_OK && rat_eq_u128(&r, 0, 1));
    CHECK(xrisk_pmf(D1, 1, &r) == XRISK_OK && rat_eq_u128(&r, 1, 2));
    /* corrupt model fields */
    g_m.n_obl = 0;
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_E_RANGE);
    g_m.n_obl = XRISK_MAX_OBLIGORS + 1;
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_E_RANGE);
    g_m.n_obl = 2;
    g_m.n_scen = 0;
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_E_RANGE);
    g_m.n_scen = XRISK_MAX_SCEN + 1;
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_E_RANGE);
    g_m.n_scen = 1;
    g_m.loss[0] = XRISK_MAX_UNITS + 1;
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_E_RANGE);
    g_m.loss[0] = 1;
    g_m.p_lo[0][0].num = 5; /* 5/2 after direct poke */
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_E_PROB);
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_HIGH, D1) == XRISK_OK); /* hi side untouched */
    g_m.p_lo[0][0].num = 1;
    g_m.w[0].num = 2;
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_E_WEIGHT);
    g_m.w[0].num = 1;
    /* weights must sum to exactly 1 */
    CHECK(xrisk_model_init(&g_m, 1, 3) == XRISK_OK);
    CHECK(xrisk_model_set_loss(&g_m, 0, 1) == XRISK_OK);
    CHECK(xrisk_model_set_p(&g_m, 0, 0, 1, 2) == XRISK_OK);
    CHECK(xrisk_model_set_p(&g_m, 1, 0, 1, 2) == XRISK_OK);
    CHECK(xrisk_model_set_p(&g_m, 2, 0, 1, 2) == XRISK_OK);
    CHECK(xrisk_model_set_weight(&g_m, 0, 1, 3) == XRISK_OK);
    CHECK(xrisk_model_set_weight(&g_m, 1, 1, 3) == XRISK_OK);
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_E_UNSET);
    CHECK(xrisk_model_set_weight(&g_m, 2, 1, 4) == XRISK_OK);
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_E_WEIGHT);
    CHECK(xrisk_model_set_weight(&g_m, 2, 1, 2) == XRISK_OK);
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_E_WEIGHT);
    CHECK(xrisk_model_set_weight(&g_m, 2, 2, 6) == XRISK_OK);
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_OK);
    CHECK(big_eq_u128(&D1->den, 6)); /* B = 3, m = 2 */
    CHECK(xrisk_pmf(D1, 1, &r) == XRISK_OK && rat_eq_u128(&r, 1, 2));
    /* weight 0 scenario is skipped but still validated */
    CHECK(xrisk_model_set_weight(&g_m, 0, 0, 3) == XRISK_OK);
    CHECK(xrisk_model_set_weight(&g_m, 1, 2, 3) == XRISK_OK);
    CHECK(xrisk_model_set_p(&g_m, 0, 0, 1, 1) == XRISK_OK);
    CHECK(xrisk_compute(&g_m, XRISK_SIDE_LOW, D1) == XRISK_OK);
    CHECK(xrisk_pmf(D1, 1, &r) == XRISK_OK && rat_eq_u128(&r, 1, 2));
    /* metric argument errors */
    CHECK(xrisk_pmf(NULL, 0, &r) == XRISK_E_NULL);
    CHECK(xrisk_pmf(D1, 0, NULL) == XRISK_E_NULL);
    CHECK(xrisk_cdf(D1, 0, NULL) == XRISK_E_NULL);
    CHECK(xrisk_tail(D1, 0, NULL) == XRISK_E_NULL);
    CHECK(xrisk_expected_loss(D1, NULL) == XRISK_E_NULL);
    CHECK(xrisk_es(D1, 1, 2, NULL) == XRISK_E_NULL);
    CHECK(xrisk_var(D1, 1, 2, NULL) == XRISK_E_NULL);
    CHECK(xrisk_var(NULL, 1, 2, &v) == XRISK_E_NULL);
    CHECK(xrisk_var(D1, 0, 2, &v) == XRISK_E_ALPHA);
    CHECK(xrisk_var(D1, 2, 2, &v) == XRISK_E_ALPHA);
    CHECK(xrisk_var(D1, 3, 2, &v) == XRISK_E_ALPHA);
    CHECK(xrisk_var(D1, -1, 2, &v) == XRISK_E_ALPHA);
    CHECK(xrisk_var(D1, 1, 0, &v) == XRISK_E_ALPHA);
    CHECK(xrisk_var(D1, -1, -2, &v) == XRISK_E_ALPHA);
    CHECK(xrisk_var(D1, 1, 2, &v) == XRISK_OK && v == 0);
    CHECK(xrisk_es(D1, 0, 2, &r) == XRISK_E_ALPHA);
    CHECK(xrisk_es(D1, 2, 2, &r) == XRISK_E_ALPHA);
    CHECK(xrisk_es(D1, 1, 2, &r) == XRISK_OK && rat_eq_u128(&r, 1, 1));
    /* distribution whose masses do not sum to den is rejected by VaR */
    xrisk_big_zero(&D1->mass[1]);
    CHECK(xrisk_var(D1, 3, 4, &v) == XRISK_E_UNSET);
    CHECK(xrisk_es(D1, 3, 4, &r) == XRISK_E_UNSET);
    D1->max_loss = XRISK_MAX_UNITS + 1;
    CHECK(xrisk_pmf(D1, 0, &r) == XRISK_E_UNSET);
}

int main(void)
{
    D1 = malloc(sizeof *D1);
    D2 = malloc(sizeof *D2);
    D3 = malloc(sizeof *D3);
    if (!D1 || !D2 || !D3) return 2;
    memset(D1, 0, sizeof *D1);
    test_big();
    test_rat();
    test_fuse();
    test_closed();
    test_brute();
    test_bounds();
    test_capacity();
    test_overflow();
    test_errors();
    free(D1);
    free(D2);
    free(D3);
    printf("%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
