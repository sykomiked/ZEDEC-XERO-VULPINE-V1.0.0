/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_spatial.c — host tests for the spatial engine (S.1, S.2, S.4 plan).
 *
 * Covers: wide arithmetic against native/__int128 references, exact
 * geometry edge cases (collinear, touching, huge coordinates, overflow and
 * range refusal), point in polygon IN/OUT/ON, the fusion truth table,
 * Hungarian and allocation against brute force (fixed seeds), deconfliction
 * (head-on, crossing, parallel, time-separated, exact separation boundary),
 * seasonal protected-zone refusal, energy refusal and every error path.
 * Doubles appear only as independent references.
 *
 * HONEST LIMITS: tests check the arithmetic and the rules, not the real
 * world (no sensor, wind or autopilot models).
 *
 *   cd kernel && gcc -std=c11 -O1 -g -Wall -Werror -Wextra -Wdouble-promotion \
 *     -Wfloat-conversion -fsanitize=address,undefined -fno-sanitize-recover=all \
 *     -DTEST_HOST -Iinclude src/spatial/test_spatial.c src/spatial/sp_*.c \
 *     -o /tmp/test_spatial -lm && /tmp/test_spatial
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "sp_geom.h"
#include "sp_fusion.h"
#include "sp_fleet.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (c)                                                                                     \
            g_pass++;                                                                              \
        else {                                                                                     \
            g_fail++;                                                                              \
            printf("[FAIL] %s:%d %s\n", __FILE__, __LINE__, m);                                    \
        }                                                                                          \
    } while (0)

static uint64_t g_rng;
static uint64_t rnd(void)
{
    uint64_t x = g_rng;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    g_rng = x;
    return x;
}
static int64_t rr(int64_t lo, int64_t hi) /* inclusive */
{
    return lo + (int64_t) (rnd() % (uint64_t) (hi - lo + 1));
}

static sp_vec2 V2(int64_t x, int64_t y)
{
    sp_vec2 v;
    v.x = x;
    v.y = y;
    return v;
}
static sp_vec3 V3(int64_t x, int64_t y, int64_t z)
{
    sp_vec3 v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}
static void rect(sp_poly *p, int64_t x0, int64_t y0, int64_t x1, int64_t y1)
{
    p->n = 4;
    p->v[0] = V2(x0, y0);
    p->v[1] = V2(x1, y0);
    p->v[2] = V2(x1, y1);
    p->v[3] = V2(x0, y1);
}

#define M ((int64_t) SP_COORD_MAX)

/* ===================================================================== */
static void test_wide(void)
{
    sp_u128 a, b, r;
    int64_t x;
    uint64_t q, rem;
    int i;

    sp_u128_mul64(UINT64_MAX, UINT64_MAX, &r);
    CHECK(r.hi == UINT64_MAX - 1 && r.lo == 1, "u128 max*max");
    sp_u128_mul64(0x100000000ull, 0x100000000ull, &r);
    CHECK(r.hi == 1 && r.lo == 0, "u128 2^32*2^32");
    a.hi = UINT64_MAX;
    a.lo = UINT64_MAX;
    b.hi = 0;
    b.lo = 1;
    CHECK(sp_u128_add(&a, &b, &r) == SP_ERR_OVERFLOW, "u128 add overflow via carry");
    a.hi = 0;
    a.lo = UINT64_MAX;
    CHECK(sp_u128_add(&a, &b, &r) == SP_OK && r.hi == 1 && r.lo == 0, "u128 add carry");
    a.hi = UINT64_MAX;
    a.lo = 0;
    b.hi = 1;
    b.lo = 0;
    CHECK(sp_u128_add(&a, &b, &r) == SP_ERR_OVERFLOW, "u128 add overflow hi");
    a.hi = 1;
    a.lo = 0;
    b.hi = 0;
    b.lo = UINT64_MAX;
    CHECK(sp_u128_cmp(&a, &b) == 1 && sp_u128_cmp(&b, &a) == -1 && sp_u128_cmp(&a, &a) == 0,
          "u128 cmp");
    b.hi = 1;
    b.lo = 1;
    CHECK(sp_u128_cmp(&a, &b) == -1, "u128 cmp low word");

    CHECK(sp_mul_i64(INT64_MIN, 1, &x) == SP_OK && x == INT64_MIN, "mul min*1");
    CHECK(sp_mul_i64(INT64_MIN, -1, &x) == SP_ERR_OVERFLOW, "mul min*-1 overflow");
    CHECK(sp_mul_i64((int64_t) 1 << 32, (int64_t) 1 << 31, &x) == SP_ERR_OVERFLOW,
          "mul 2^63 overflow");
    CHECK(sp_mul_i64(-((int64_t) 1 << 32), (int64_t) 1 << 31, &x) == SP_OK && x == INT64_MIN,
          "mul -2^63 ok");
    CHECK(sp_mul_i64(-((int64_t) 1 << 32), -((int64_t) 1 << 31), &x) == SP_ERR_OVERFLOW,
          "mul (-)(-) 2^63 overflow");
    CHECK(sp_mul_i64(3, -4, &x) == SP_OK && x == -12, "mul 3*-4");
    CHECK(sp_mul_i64(0, INT64_MIN, &x) == SP_OK && x == 0, "mul 0");
    CHECK(sp_mul_i64((int64_t) 1 << 33, (int64_t) 1 << 33, &x) == SP_ERR_OVERFLOW,
          "mul hi word overflow");
    CHECK(sp_mul_i64(1, 1, (int64_t *) 0) == SP_ERR_NULL, "mul null");
    CHECK(sp_add_i64(INT64_MAX, 1, &x) == SP_ERR_OVERFLOW, "add overflow");
    CHECK(sp_add_i64(5, -7, &x) == SP_OK && x == -2, "add");
    CHECK(sp_add_i64(1, 1, (int64_t *) 0) == SP_ERR_NULL, "add null");
    CHECK(sp_sub_i64(INT64_MIN, 1, &x) == SP_ERR_OVERFLOW, "sub overflow");
    CHECK(sp_sub_i64(5, 7, &x) == SP_OK && x == -2, "sub");
    CHECK(sp_sub_i64(1, 1, (int64_t *) 0) == SP_ERR_NULL, "sub null");
    g_rng = 0x1234567u;
    for (i = 0; i < 2000; i++) {
        int64_t p = (int64_t) rnd(), s = (int64_t) (rnd() >> (rnd() % 64));
        int ok = 1;
#ifdef __SIZEOF_INT128__
        __int128 ref = (__int128) p * s;
        int fits = ref >= INT64_MIN && ref <= INT64_MAX;
        int st = sp_mul_i64(p, s, &x);
        ok = fits ? (st == SP_OK && x == (int64_t) ref) : st == SP_ERR_OVERFLOW;
#endif
        if (!ok) {
            CHECK(0, "mul random vs int128");
            break;
        }
    }
    CHECK(1, "mul random done");

    CHECK(sp_udiv64(10, 0, &q, &rem) == SP_ERR_PARAM, "udiv by zero");
    CHECK(sp_udiv64(10, 3, (uint64_t *) 0, &rem) == SP_ERR_NULL, "udiv null");
    CHECK(sp_udiv64(UINT64_MAX, (1ull << 63) + 1, &q, &rem) == SP_OK && q == 1 &&
              rem == (1ull << 63) - 2,
          "udiv big divisor");
    for (i = 0; i < 3000; i++) {
        uint64_t n = rnd(), d = rnd() >> (rnd() % 64);
        if (d == 0) d = 1;
        if (sp_udiv64(n, d, &q, &rem) != SP_OK || q != n / d || rem != n % d) {
            CHECK(0, "udiv random");
            break;
        }
    }
    CHECK(sp_ceil_div_i64(7, 2, &x) == SP_OK && x == 4, "ceil 7/2");
    CHECK(sp_ceil_div_i64(8, 2, &x) == SP_OK && x == 4, "ceil 8/2");
    CHECK(sp_ceil_div_i64(0, 5, &x) == SP_OK && x == 0, "ceil 0");
    CHECK(sp_ceil_div_i64(-1, 5, &x) == SP_ERR_RANGE, "ceil negative n");
    CHECK(sp_ceil_div_i64(1, 0, &x) == SP_ERR_RANGE, "ceil d=0");
    CHECK(sp_ceil_div_i64(1, 1, (int64_t *) 0) == SP_ERR_NULL, "ceil null");
    CHECK(sp_floor_div_i64(-3, 2, &x) == SP_OK && x == -2, "floor -3/2");
    CHECK(sp_floor_div_i64(-4, 2, &x) == SP_OK && x == -2, "floor -4/2");
    CHECK(sp_floor_div_i64(3, 2, &x) == SP_OK && x == 1, "floor 3/2");
    CHECK(sp_floor_div_i64(INT64_MIN, 1, &x) == SP_OK && x == INT64_MIN, "floor min/1");
    CHECK(sp_floor_div_i64(INT64_MIN, 3, &x) == SP_OK && x == -3074457345618258603LL,
          "floor min/3");
    CHECK(sp_floor_div_i64(1, -1, &x) == SP_ERR_RANGE, "floor d<0");
    CHECK(sp_floor_div_i64(1, 1, (int64_t *) 0) == SP_ERR_NULL, "floor null");
    for (i = 0; i < 2000; i++) {
        int64_t n = (int64_t) rnd() >> 2, d = (int64_t) (rnd() >> (1 + rnd() % 63));
        int64_t ref;
        if (d == 0) d = 1;
        ref = n / d;
        if ((n % d) != 0 && n < 0) ref--;
        if (sp_floor_div_i64(n, d, &x) != SP_OK || x != ref) {
            CHECK(0, "floor random");
            break;
        }
    }
    CHECK(sp_isqrt_floor(0) == 0 && sp_isqrt_floor(1) == 1 && sp_isqrt_floor(3) == 1 &&
              sp_isqrt_floor(4) == 2 && sp_isqrt_floor(UINT64_MAX) == 0xFFFFFFFFu,
          "isqrt floor edges");
    CHECK(sp_isqrt_ceil(0) == 0 && sp_isqrt_ceil(2) == 2 && sp_isqrt_ceil(4) == 2 &&
              sp_isqrt_ceil(5) == 3 && sp_isqrt_ceil(UINT64_MAX) == 0x100000000ull,
          "isqrt ceil edges");
    for (i = 0; i < 3000; i++) {
        uint64_t v = rnd() >> (rnd() % 64), s = sp_isqrt_floor(v);
        sp_u128 lo, hi, vv;
        sp_u128_mul64(s, s, &lo);
        sp_u128_mul64(s + 1, s + 1, &hi);
        vv.hi = 0;
        vv.lo = v;
        if (sp_u128_cmp(&lo, &vv) > 0 || sp_u128_cmp(&hi, &vv) <= 0) {
            CHECK(0, "isqrt random");
            break;
        }
    }
}

static void test_big(void)
{
    sp_big a, b, r, t;
    int i;
    sp_big_from_i64(&a, 0);
    sp_big_from_i64(&b, -5);
    CHECK(sp_big_sign(&a) == 0 && sp_big_sign(&b) == -1, "big sign");
    sp_big_from_i64(&a, INT64_MIN);
    sp_big_from_i64(&b, INT64_MIN);
    CHECK(sp_big_mul(&r, &a, &b) == SP_OK && sp_big_sign(&r) == 1 && r.l[3] == 0x40000000u &&
              r.l[0] == 0 && r.l[2] == 0,
          "big min*min = 2^126");
    CHECK(sp_big_sub(&r, &a, &a) == SP_OK && sp_big_sign(&r) == 0 && r.neg == 0, "big x-x = +0");
    sp_big_from_i64(&a, 7);
    sp_big_from_i64(&b, -7);
    CHECK(sp_big_add(&r, &a, &b) == SP_OK && sp_big_sign(&r) == 0 && r.neg == 0, "big 7+-7");
    CHECK(sp_big_cmp(&a, &b) == 1 && sp_big_cmp(&b, &a) == -1 && sp_big_cmp(&a, &a) == 0,
          "big cmp signs");
    sp_big_from_i64(&a, -3);
    sp_big_from_i64(&b, -9);
    CHECK(sp_big_cmp(&a, &b) == 1, "big cmp negatives");
    sp_big_from_i64(&a, 0);
    sp_big_from_i64(&b, 0);
    b.neg = 1; /* a non-canonical -0 still compares equal to 0 */
    CHECK(sp_big_cmp(&a, &b) == 0, "big -0 == 0");
    CHECK(sp_big_add((sp_big *) 0, &a, &b) == SP_ERR_NULL && sp_big_sub(&r, 0, &b) == SP_ERR_NULL &&
              sp_big_mul(&r, &a, 0) == SP_ERR_NULL,
          "big null");
    /* overflow: 2^504 * 2^8 = 2^512 does not fit in 512 bits */
    sp_big_from_i64(&a, (int64_t) 1 << 62);
    sp_big_copy(&r, &a);
    for (i = 0; i < 7; i++) sp_big_mul(&r, &r, &a); /* 2^496 */
    sp_big_from_i64(&t, 256);
    CHECK(sp_big_mul(&b, &r, &t) == SP_OK, "big 2^504 fits");
    CHECK(sp_big_mul(&r, &b, &t) == SP_ERR_OVERFLOW, "big mul overflow 2^512");
    /* add overflow: (2^512 - 1) + 1 */
    for (i = 0; i < SP_BIG_LIMBS; i++) r.l[i] = 0xFFFFFFFFu;
    r.neg = 0;
    sp_big_from_i64(&t, 1);
    CHECK(sp_big_add(&b, &r, &t) == SP_ERR_OVERFLOW, "big add overflow");
    CHECK(sp_big_sub(&b, &r, &t) == SP_OK && b.l[0] == 0xFFFFFFFEu, "big sub near max");
    r.neg = 1;
    CHECK(sp_big_sub(&b, &r, &t) == SP_ERR_OVERFLOW, "big negative overflow");
#ifdef __SIZEOF_INT128__
    g_rng = 0xBADC0DEu;
    for (i = 0; i < 3000; i++) {
        int64_t x = (int64_t) rnd() >> (rnd() % 63), y = (int64_t) rnd() >> (rnd() % 63);
        __int128 p = (__int128) x * y, s = (__int128) x + y, d = (__int128) x - y;
        sp_big bx, by, bp, bs, bd;
        unsigned __int128 mp;
        int ok;
        sp_big_from_i64(&bx, x);
        sp_big_from_i64(&by, y);
        ok = sp_big_mul(&bp, &bx, &by) == SP_OK && sp_big_add(&bs, &bx, &by) == SP_OK &&
             sp_big_sub(&bd, &bx, &by) == SP_OK;
        mp = p < 0 ? (unsigned __int128) 0 - (unsigned __int128) p : (unsigned __int128) p;
        ok = ok && bp.l[0] == (uint32_t) mp && bp.l[1] == (uint32_t) (mp >> 32) &&
             bp.l[2] == (uint32_t) (mp >> 64) && bp.l[3] == (uint32_t) (mp >> 96) && bp.l[4] == 0 &&
             sp_big_sign(&bp) == (p > 0) - (p < 0);
        ok = ok && sp_big_sign(&bs) == (s > 0) - (s < 0) && sp_big_sign(&bd) == (d > 0) - (d < 0);
        ok = ok && sp_big_cmp(&bx, &by) == (x > y) - (x < y);
        if (!ok) {
            CHECK(0, "big random vs int128");
            break;
        }
    }
    CHECK(1, "big random done");
#endif
}

/* ===================================================================== */
static void test_geometry(void)
{
    sp_vec2 a, b, c, d;
    int o;
    sp_isect_t is;

    a = V2(0, 0);
    b = V2(10, 0);
    c = V2(5, 5);
    CHECK(sp_orient2d(&a, &b, &c, &o) == SP_OK && o == 1, "orient ccw");
    c = V2(5, -5);
    CHECK(sp_orient2d(&a, &b, &c, &o) == SP_OK && o == -1, "orient cw");
    c = V2(20, 0);
    CHECK(sp_orient2d(&a, &b, &c, &o) == SP_OK && o == 0, "orient collinear");
    a = V2(-M, -M);
    b = V2(M, M);
    c = V2(M - 1, M);
    CHECK(sp_orient2d(&a, &b, &c, &o) == SP_OK && o == 1, "orient huge ccw");
    c = V2(M, M - 1);
    CHECK(sp_orient2d(&a, &b, &c, &o) == SP_OK && o == -1, "orient huge cw");
    c = V2(0, 0);
    CHECK(sp_orient2d(&a, &b, &c, &o) == SP_OK && o == 0, "orient huge collinear");
    c = V2(M + 1, 0);
    CHECK(sp_orient2d(&a, &b, &c, &o) == SP_ERR_RANGE, "orient out of range");
    c = V2(0, -M - 1);
    CHECK(sp_orient2d(&a, &b, &c, &o) == SP_ERR_RANGE, "orient out of range neg");
    CHECK(sp_orient2d(&a, &b, (sp_vec2 *) 0, &o) == SP_ERR_NULL, "orient null");
    CHECK(sp_vec2_check((sp_vec2 *) 0) == SP_ERR_NULL &&
              sp_vec3_check((sp_vec3 *) 0) == SP_ERR_NULL,
          "vec check null");
    c = V2(M, -M);
    CHECK(sp_vec2_check(&c) == SP_OK, "vec2 at limit ok");
    {
        sp_vec3 v = V3(0, 0, M + 1);
        CHECK(sp_vec3_check(&v) == SP_ERR_RANGE, "vec3 z out of range");
    }

    /* intersections */
    a = V2(0, 0);
    b = V2(10, 10);
    c = V2(0, 10);
    d = V2(10, 0);
    CHECK(sp_seg_intersect(&a, &b, &c, &d, &is) == SP_OK && is == SP_ISECT_PROPER, "isect X");
    c = V2(5, 5);
    d = V2(10, 0);
    CHECK(sp_seg_intersect(&a, &b, &c, &d, &is) == SP_OK && is == SP_ISECT_TOUCH, "isect T");
    c = V2(5, 5);
    d = V2(20, 20);
    CHECK(sp_seg_intersect(&a, &b, &c, &d, &is) == SP_OK && is == SP_ISECT_TOUCH,
          "isect collinear overlap");
    c = V2(11, 11);
    d = V2(20, 20);
    CHECK(sp_seg_intersect(&a, &b, &c, &d, &is) == SP_OK && is == SP_ISECT_NONE,
          "isect collinear disjoint");
    c = V2(10, 10);
    d = V2(20, 20);
    CHECK(sp_seg_intersect(&a, &b, &c, &d, &is) == SP_OK && is == SP_ISECT_TOUCH,
          "isect collinear endpoint");
    c = V2(0, 1);
    d = V2(10, 11);
    CHECK(sp_seg_intersect(&a, &b, &c, &d, &is) == SP_OK && is == SP_ISECT_NONE, "isect parallel");
    c = V2(3, 3);
    d = V2(3, 3);
    CHECK(sp_seg_intersect(&a, &b, &c, &d, &is) == SP_OK && is == SP_ISECT_TOUCH,
          "isect point on segment");
    c = V2(3, 4);
    CHECK(sp_seg_intersect(&a, &b, &c, &c, &is) == SP_OK && is == SP_ISECT_NONE,
          "isect point off segment");
    c = V2(6, 4);
    d = V2(100, 4);
    CHECK(sp_seg_intersect(&a, &b, &c, &d, &is) == SP_OK && is == SP_ISECT_NONE,
          "isect near miss (line hits outside)");
    a = V2(-M, -M);
    b = V2(M, M);
    c = V2(-M, M);
    d = V2(M, -M);
    CHECK(sp_seg_intersect(&a, &b, &c, &d, &is) == SP_OK && is == SP_ISECT_PROPER, "isect huge X");
    c = V2(-M, M);
    d = V2(M - 1, -M + 1); /* still crosses diagonal at an interior point */
    CHECK(sp_seg_intersect(&a, &b, &c, &d, &is) == SP_OK && is == SP_ISECT_PROPER,
          "isect huge X off-by-one");
    c = V2(-M, -M + 1);
    d = V2(M - 1, M);
    CHECK(sp_seg_intersect(&a, &b, &c, &d, &is) == SP_OK && is == SP_ISECT_NONE,
          "isect huge parallel 1mm apart");
    d = V2(M + 1, M);
    CHECK(sp_seg_intersect(&a, &b, &c, &d, &is) == SP_ERR_RANGE, "isect out of range");
    CHECK(sp_seg_intersect(&a, &b, &c, &d, (sp_isect_t *) 0) == SP_ERR_NULL, "isect null");
}

static void test_polygon(void)
{
    sp_poly p;
    sp_vec2 q;
    sp_loc_t l;
    int i;

    rect(&p, 0, 0, 10, 10);
    CHECK(sp_poly_validate(&p) == SP_OK, "square valid");
    q = V2(5, 5);
    CHECK(sp_point_in_poly(&p, &q, &l) == SP_OK && l == SP_LOC_IN, "square in");
    q = V2(15, 5);
    CHECK(sp_point_in_poly(&p, &q, &l) == SP_OK && l == SP_LOC_OUT, "square out");
    q = V2(10, 5);
    CHECK(sp_point_in_poly(&p, &q, &l) == SP_OK && l == SP_LOC_ON, "square on edge");
    q = V2(0, 0);
    CHECK(sp_point_in_poly(&p, &q, &l) == SP_OK && l == SP_LOC_ON, "square on vertex");
    q = V2(5, 10);
    CHECK(sp_point_in_poly(&p, &q, &l) == SP_OK && l == SP_LOC_ON, "square on top edge");
    q = V2(-1, 10);
    CHECK(sp_point_in_poly(&p, &q, &l) == SP_OK && l == SP_LOC_OUT, "square out level w/ top");
    q = V2(-1, 0);
    CHECK(sp_point_in_poly(&p, &q, &l) == SP_OK && l == SP_LOC_OUT, "square out level w/ bottom");
    q = V2(11, 0);
    CHECK(sp_point_in_poly(&p, &q, &l) == SP_OK && l == SP_LOC_OUT, "square out right of vertex");
    /* concave U: notch between x 3..7 above y 3 */
    p.n = 8;
    p.v[0] = V2(0, 0);
    p.v[1] = V2(10, 0);
    p.v[2] = V2(10, 10);
    p.v[3] = V2(7, 10);
    p.v[4] = V2(7, 3);
    p.v[5] = V2(3, 3);
    p.v[6] = V2(3, 10);
    p.v[7] = V2(0, 10);
    CHECK(sp_poly_validate(&p) == SP_OK, "U valid");
    q = V2(5, 5);
    CHECK(sp_point_in_poly(&p, &q, &l) == SP_OK && l == SP_LOC_OUT, "U notch out");
    q = V2(1, 5);
    CHECK(sp_point_in_poly(&p, &q, &l) == SP_OK && l == SP_LOC_IN, "U left arm in");
    q = V2(8, 9);
    CHECK(sp_point_in_poly(&p, &q, &l) == SP_OK && l == SP_LOC_IN, "U right arm in");
    q = V2(5, 3);
    CHECK(sp_point_in_poly(&p, &q, &l) == SP_OK && l == SP_LOC_ON, "U notch floor on");
    q = V2(5, 2);
    CHECK(sp_point_in_poly(&p, &q, &l) == SP_OK && l == SP_LOC_IN, "U base in");
    q = V2(-5, 3); /* ray level with notch vertices */
    CHECK(sp_point_in_poly(&p, &q, &l) == SP_OK && l == SP_LOC_OUT, "U level w/ vertices out");
    q = V2(1, 3);
    CHECK(sp_point_in_poly(&p, &q, &l) == SP_OK && l == SP_LOC_IN, "U level w/ vertices in");
    q = V2(5, 10);
    CHECK(sp_point_in_poly(&p, &q, &l) == SP_OK && l == SP_LOC_OUT, "U top of notch out");
    /* clockwise winding gives the same answers */
    {
        sp_poly r;
        r.n = 4;
        r.v[0] = V2(0, 0);
        r.v[1] = V2(0, 10);
        r.v[2] = V2(10, 10);
        r.v[3] = V2(10, 0);
        q = V2(5, 5);
        CHECK(sp_poly_validate(&r) == SP_OK && sp_point_in_poly(&r, &q, &l) == SP_OK &&
                  l == SP_LOC_IN,
              "cw square in");
    }
    /* huge square at the coordinate limit */
    rect(&p, -M, -M, M, M);
    CHECK(sp_poly_validate(&p) == SP_OK, "huge square valid");
    q = V2(M, 0);
    CHECK(sp_point_in_poly(&p, &q, &l) == SP_OK && l == SP_LOC_ON, "huge on edge");
    q = V2(M - 1, M - 1);
    CHECK(sp_point_in_poly(&p, &q, &l) == SP_OK && l == SP_LOC_IN, "huge in corner");
    q = V2(M + 1, 0);
    CHECK(sp_point_in_poly(&p, &q, &l) == SP_ERR_RANGE, "pip point out of range");
    /* diagonal triangle with huge coordinates */
    p.n = 3;
    p.v[0] = V2(-M, -M);
    p.v[1] = V2(M, -M);
    p.v[2] = V2(M, M);
    q = V2(1, 0);
    CHECK(sp_point_in_poly(&p, &q, &l) == SP_OK && l == SP_LOC_IN, "huge tri below diag in");
    q = V2(0, 0);
    CHECK(sp_point_in_poly(&p, &q, &l) == SP_OK && l == SP_LOC_ON, "huge tri on diag");
    q = V2(0, 1);
    CHECK(sp_point_in_poly(&p, &q, &l) == SP_OK && l == SP_LOC_OUT, "huge tri above diag out");
    /* random property vs axis-aligned reference */
    g_rng = 0x5151u;
    rect(&p, -50, -30, 40, 60);
    for (i = 0; i < 4000; i++) {
        sp_loc_t ref;
        q = V2(rr(-60, 50), rr(-40, 70));
        if (q.x > -50 && q.x < 40 && q.y > -30 && q.y < 60)
            ref = SP_LOC_IN;
        else if (q.x >= -50 && q.x <= 40 && q.y >= -30 && q.y <= 60)
            ref = SP_LOC_ON;
        else
            ref = SP_LOC_OUT;
        if (sp_point_in_poly(&p, &q, &l) != SP_OK || l != ref) {
            CHECK(0, "pip random rect");
            break;
        }
    }
    CHECK(1, "pip random done");

    /* validation errors */
    rect(&p, 0, 0, 10, 10);
    p.n = 2;
    CHECK(sp_poly_validate(&p) == SP_ERR_DEGENERATE, "poly n<3");
    p.n = SP_POLY_MAX + 1;
    CHECK(sp_poly_validate(&p) == SP_ERR_DEGENERATE, "poly n>max");
    CHECK(sp_point_in_poly(&p, &q, &l) == SP_ERR_DEGENERATE, "pip n>max");
    rect(&p, 0, 0, 10, 10);
    p.v[1] = p.v[0];
    CHECK(sp_poly_validate(&p) == SP_ERR_DEGENERATE, "poly zero edge");
    p.n = 4; /* bow tie */
    p.v[0] = V2(0, 0);
    p.v[1] = V2(10, 10);
    p.v[2] = V2(10, 0);
    p.v[3] = V2(0, 10);
    CHECK(sp_poly_validate(&p) == SP_ERR_DEGENERATE, "poly bow tie");
    p.n = 3; /* collinear triangle folds back */
    p.v[0] = V2(0, 0);
    p.v[1] = V2(5, 0);
    p.v[2] = V2(10, 0);
    CHECK(sp_poly_validate(&p) == SP_ERR_DEGENERATE, "poly collinear");
    p.n = 4; /* spike folding back along an edge */
    p.v[0] = V2(0, 0);
    p.v[1] = V2(10, 0);
    p.v[2] = V2(5, 0);
    p.v[3] = V2(5, 5);
    CHECK(sp_poly_validate(&p) == SP_ERR_DEGENERATE, "poly fold back");
    p.n = 5; /* vertex touching a non-adjacent edge */
    p.v[0] = V2(0, 0);
    p.v[1] = V2(10, 0);
    p.v[2] = V2(10, 10);
    p.v[3] = V2(5, 0);
    p.v[4] = V2(0, 10);
    CHECK(sp_poly_validate(&p) == SP_ERR_DEGENERATE, "poly vertex on edge");
    p.n = 4; /* straight-through redundant vertex is fine */
    p.v[0] = V2(0, 0);
    p.v[1] = V2(5, 0);
    p.v[2] = V2(10, 0);
    p.v[3] = V2(5, 5);
    CHECK(sp_poly_validate(&p) == SP_OK, "poly redundant collinear vertex ok");
    rect(&p, 0, 0, M + 1, 10);
    CHECK(sp_poly_validate(&p) == SP_ERR_RANGE, "poly vertex out of range");
    CHECK(sp_poly_validate((sp_poly *) 0) == SP_ERR_NULL, "poly null");
    CHECK(sp_point_in_poly(&p, (sp_vec2 *) 0, &l) == SP_ERR_NULL, "pip null");
}

static double dref_pt_seg(double px, double py, double pz, double ax, double ay, double az,
                          double bx, double by, double bz)
{
    double ux = bx - ax, uy = by - ay, uz = bz - az;
    double den = ux * ux + uy * uy + uz * uz, t = 0.0;
    double dx, dy, dz;
    if (den > 0.0) t = ((px - ax) * ux + (py - ay) * uy + (pz - az) * uz) / den;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    dx = px - (ax + t * ux);
    dy = py - (ay + t * uy);
    dz = pz - (az + t * uz);
    return sqrt(dx * dx + dy * dy + dz * dz);
}

static void test_distance(void)
{
    sp_vec2 p, a, b, c, d;
    sp_vec3 p3, a3, b3;
    int o, i, bad = 0, tested = 0;
    int64_t len;

    a = V2(0, 0);
    b = V2(10, 0);
    p = V2(5, 3);
    CHECK(sp_dist_cmp_pt_seg2(&p, &a, &b, 3, &o) == SP_OK && o == 0, "pt-seg exactly m");
    CHECK(sp_dist_cmp_pt_seg2(&p, &a, &b, 4, &o) == SP_OK && o == -1, "pt-seg closer");
    CHECK(sp_dist_cmp_pt_seg2(&p, &a, &b, 2, &o) == SP_OK && o == 1, "pt-seg farther");
    p = V2(13, 4);
    CHECK(sp_dist_cmp_pt_seg2(&p, &a, &b, 5, &o) == SP_OK && o == 0, "pt-seg endpoint region b");
    p = V2(-3, -4);
    CHECK(sp_dist_cmp_pt_seg2(&p, &a, &b, 5, &o) == SP_OK && o == 0, "pt-seg endpoint region a");
    CHECK(sp_dist_cmp_pt_seg2(&p, &a, &a, 5, &o) == SP_OK && o == 0, "pt-seg degenerate");
    CHECK(sp_dist_cmp_pt_seg2(&p, &a, &a, 6, &o) == SP_OK && o == -1, "pt-seg degenerate closer");
    a = V2(0, 0);
    b = V2(6, 8);
    p = V2(7, 1); /* interior foot, distance exactly 5 */
    CHECK(sp_dist_cmp_pt_seg2(&p, &a, &b, 5, &o) == SP_OK && o == 0, "pt-seg slanted exact");
    CHECK(sp_dist_cmp_pt_seg2(&p, &a, &b, 6, &o) == SP_OK && o == -1, "pt-seg slanted closer");
    CHECK(sp_dist_cmp_pt_seg2(&p, &a, &b, 4, &o) == SP_OK && o == 1, "pt-seg slanted farther");
    p = V2(3, 4); /* on the segment */
    CHECK(sp_dist_cmp_pt_seg2(&p, &a, &b, 0, &o) == SP_OK && o == 0, "pt on seg, m=0");
    /* huge: distance from (M,-M) to the main diagonal = M*sqrt(2) = 759250124.99.. */
    a = V2(-M, -M);
    b = V2(M, M);
    p = V2(M, -M);
    CHECK(sp_dist_cmp_pt_seg2(&p, &a, &b, 759250124, &o) == SP_OK && o == 1, "huge farther");
    CHECK(sp_dist_cmp_pt_seg2(&p, &a, &b, 759250125, &o) == SP_OK && o == -1, "huge closer");
    CHECK(sp_dist_cmp_pt_seg2(&p, &a, &b, 2 * M, &o) == SP_OK && o == -1, "huge max margin");
    CHECK(sp_dist_cmp_pt_seg2(&p, &a, &b, 2 * M + 1, &o) == SP_ERR_RANGE, "margin too big");
    CHECK(sp_dist_cmp_pt_seg2(&p, &a, &b, -1, &o) == SP_ERR_RANGE, "margin negative");
    p = V2(M + 1, 0);
    CHECK(sp_dist_cmp_pt_seg2(&p, &a, &b, 1, &o) == SP_ERR_RANGE, "pt-seg out of range");
    CHECK(sp_dist_cmp_pt_seg2(&p, &a, &b, 1, (int *) 0) == SP_ERR_NULL, "pt-seg null");
    CHECK(sp_dist_cmp_pt_seg3(&p3, &a3, (sp_vec3 *) 0, 1, &o) == SP_ERR_NULL, "pt-seg3 null");
    /* 3D exact: (1,2,2) at distance 3 from z... */
    a3 = V3(0, 0, 0);
    b3 = V3(0, 0, 100);
    p3 = V3(2, 1, 50); /* sqrt(5) */
    CHECK(sp_dist_cmp_pt_seg3(&p3, &a3, &b3, 2, &o) == SP_OK && o == 1, "3d sqrt5 > 2");
    CHECK(sp_dist_cmp_pt_seg3(&p3, &a3, &b3, 3, &o) == SP_OK && o == -1, "3d sqrt5 < 3");
    p3 = V3(2, 2, 51);
    b3 = V3(0, 0, 50);
    CHECK(sp_dist_cmp_pt_seg3(&p3, &a3, &b3, 3, &o) == SP_OK && o == 0, "3d endpoint (2,2,1)=3");
    /* 3D huge corner-to-corner */
    a3 = V3(-M, -M, -M);
    b3 = V3(M, M, M);
    p3 = V3(M, -M, M); /* distance 2M*sqrt(2/3) = 876706902.6.. */
    CHECK(sp_dist_cmp_pt_seg3(&p3, &a3, &b3, 2 * M, &o) == SP_OK && o == -1,
          "3d huge closer than 2^30");
    a3 = V3(-M, -M, -M);
    b3 = V3(-M, -M, M);
    p3 = V3(M, M, 0); /* distance 2M*sqrt2 > 2^30 */
    CHECK(sp_dist_cmp_pt_seg3(&p3, &a3, &b3, 2 * M, &o) == SP_OK && o == 1, "3d huge farther");
    g_rng = 0xD15Cu;
    for (i = 0; i < 3000; i++) {
        int64_t r = i < 1500 ? 1000 : M;
        sp_vec3 P = V3(rr(-r, r), rr(-r, r), rr(-r, r)), A = V3(rr(-r, r), rr(-r, r), rr(-r, r));
        sp_vec3 B = V3(rr(-r, r), rr(-r, r), rr(-r, r));
        double dd =
            dref_pt_seg((double) P.x, (double) P.y, (double) P.z, (double) A.x, (double) A.y,
                        (double) A.z, (double) B.x, (double) B.y, (double) B.z);
        int64_t m = (int64_t) dd + rr(-2, 2);
        int expect;
        if (m < 0) m = 0;
        if (m > 2 * M) m = 2 * M;
        if (fabs(dd - (double) m) < 1e-3 * (1.0 + dd / 1e6)) continue;
        expect = dd < (double) m ? -1 : 1;
        tested++;
        if (sp_dist_cmp_pt_seg3(&P, &A, &B, m, &o) != SP_OK || o != expect) bad++;
    }
    CHECK(bad == 0 && tested > 2000, "pt-seg3 random vs double");

    /* segment-segment */
    a = V2(0, 0);
    b = V2(10, 0);
    c = V2(0, 5);
    d = V2(10, 5);
    CHECK(sp_dist_cmp_seg_seg2(&a, &b, &c, &d, 5, &o) == SP_OK && o == 0, "seg-seg parallel =m");
    CHECK(sp_dist_cmp_seg_seg2(&a, &b, &c, &d, 6, &o) == SP_OK && o == -1, "seg-seg parallel <m");
    CHECK(sp_dist_cmp_seg_seg2(&a, &b, &c, &d, 4, &o) == SP_OK && o == 1, "seg-seg parallel >m");
    c = V2(5, -5);
    d = V2(5, 5);
    CHECK(sp_dist_cmp_seg_seg2(&a, &b, &c, &d, 1, &o) == SP_OK && o == -1, "seg-seg crossing");
    CHECK(sp_dist_cmp_seg_seg2(&a, &b, &c, &d, 0, &o) == SP_OK && o == 0, "seg-seg crossing m=0");
    c = V2(13, 4);
    d = V2(20, 4);
    CHECK(sp_dist_cmp_seg_seg2(&a, &b, &c, &d, 5, &o) == SP_OK && o == 0,
          "seg-seg endpoint-endpoint");
    c = V2(-4, 3);
    d = V2(-3, 30);
    CHECK(sp_dist_cmp_seg_seg2(&c, &d, &a, &b, 5, &o) == SP_OK && o == 0,
          "seg-seg first endpoint of first seg");
    CHECK(sp_dist_cmp_seg_seg2(&a, &b, &c, &d, -1, &o) == SP_ERR_RANGE, "seg-seg bad m");
    CHECK(sp_dist_cmp_seg_seg2(&a, &b, &c, (sp_vec2 *) 0, 1, &o) == SP_ERR_NULL, "seg-seg null");
    d = V2(M + 1, 0);
    CHECK(sp_dist_cmp_seg_seg2(&a, &b, &c, &d, 1, &o) == SP_ERR_RANGE, "seg-seg range");

    a3 = V3(0, 0, 0);
    b3 = V3(3, 4, 12);
    CHECK(sp_len3_ceil(&a3, &b3, &len) == SP_OK && len == 13, "len exact 13");
    b3 = V3(1, 1, 0);
    CHECK(sp_len3_ceil(&a3, &b3, &len) == SP_OK && len == 2, "len ceil sqrt2");
    a3 = V3(-M, -M, -M);
    b3 = V3(M, M, M); /* 2M*sqrt3 = 1859775393.8 */
    CHECK(sp_len3_ceil(&a3, &b3, &len) == SP_OK && len == 1859775394, "len huge ceil");
    b3 = V3(M + 1, 0, 0);
    CHECK(sp_len3_ceil(&a3, &b3, &len) == SP_ERR_RANGE, "len range");
    CHECK(sp_len3_ceil(&a3, &b3, (int64_t *) 0) == SP_ERR_NULL, "len null");
}

static void test_alt_geo_pose(void)
{
    sp_alt_band band;
    sp_geo g;
    sp_frame f;
    sp_vec3 out;
    sp_heading h;
    sp_hvec2 hv;
    sp_vec2 t, p;
    int in, i, bad = 0;

    band.z_min = 10000;
    band.z_max = 120000;
    CHECK(sp_alt_band_contains(&band, 10000, &in) == SP_OK && in == 1, "band low edge in");
    CHECK(sp_alt_band_contains(&band, 120000, &in) == SP_OK && in == 1, "band high edge in");
    CHECK(sp_alt_band_contains(&band, 9999, &in) == SP_OK && in == 0, "band below");
    CHECK(sp_alt_band_contains(&band, 120001, &in) == SP_OK && in == 0, "band above");
    CHECK(sp_alt_band_contains(&band, M + 1, &in) == SP_ERR_RANGE, "band z range");
    CHECK(sp_alt_band_contains(&band, 0, (int *) 0) == SP_ERR_NULL, "band null");
    band.z_min = 5;
    band.z_max = 4;
    CHECK(sp_alt_band_validate(&band) == SP_ERR_PARAM, "band inverted");
    band.z_min = -M - 1;
    CHECK(sp_alt_band_validate(&band) == SP_ERR_RANGE, "band range");
    CHECK(sp_alt_band_contains(&band, 0, &in) == SP_ERR_RANGE, "band contains invalid band");
    CHECK(sp_alt_band_validate((sp_alt_band *) 0) == SP_ERR_NULL, "band null validate");

    g.lat_udeg = 90000001;
    g.lon_udeg = 0;
    g.alt_mm = 0;
    CHECK(sp_geo_validate(&g) == SP_ERR_RANGE, "geo lat range");
    g.lat_udeg = 0;
    g.lon_udeg = -180000001;
    CHECK(sp_geo_validate(&g) == SP_ERR_RANGE, "geo lon range");
    g.lon_udeg = 0;
    g.alt_mm = M + 1;
    CHECK(sp_geo_validate(&g) == SP_ERR_RANGE, "geo alt range");
    CHECK(sp_geo_validate((sp_geo *) 0) == SP_ERR_NULL, "geo null");
    f.origin.lat_udeg = 45000000;
    f.origin.lon_udeg = 179900000;
    f.origin.alt_mm = 1000;
    f.lat_mm_num = 1113195; /* 111.3195 mm per micro-degree */
    f.lat_mm_den = 10000;
    f.lon_mm_num = 3;
    f.lon_mm_den = 2;
    g.lat_udeg = 45000010;
    g.lon_udeg = -179900000; /* across the antimeridian: dlon = +200000 */
    g.alt_mm = 51000;
    CHECK(sp_geo_to_local(&f, &g, &out) == SP_OK && out.x == 300000 && out.y == 1113 &&
              out.z == 50000,
          "geo to local wrap east");
    g.lon_udeg = 179899999; /* dlon = -1 -> floor(-1.5) = -2 */
    g.lat_udeg = 44999990;  /* dlat = -10 -> floor(-1113.195) = -1114 */
    CHECK(sp_geo_to_local(&f, &g, &out) == SP_OK && out.x == -2 && out.y == -1114,
          "geo floor negative");
    f.origin.lon_udeg = -179900000;
    g.lon_udeg = 179900000; /* dlon = -200000 after wrap */
    CHECK(sp_geo_to_local(&f, &g, &out) == SP_OK && out.x == -300000, "geo wrap west");
    g.lon_udeg = 0;   /* dlon = 179.9e6 udeg * 4 mm/udeg > SP_COORD_MAX */
    f.lon_mm_num = 8; /* 4 mm per micro-degree */
    CHECK(sp_geo_to_local(&f, &g, &out) == SP_ERR_RANGE, "geo projected out of range");
    f.lon_mm_den = 0;
    CHECK(sp_geo_to_local(&f, &g, &out) == SP_ERR_PARAM, "geo bad den");
    f.lon_mm_den = 2;
    f.lat_mm_num = 0;
    CHECK(sp_geo_to_local(&f, &g, &out) == SP_ERR_PARAM, "geo bad lat scale");
    f.lat_mm_num = 1;
    f.origin.lat_udeg = 91000000;
    CHECK(sp_geo_to_local(&f, &g, &out) == SP_ERR_RANGE, "geo bad origin");
    f.origin.lat_udeg = 0;
    f.lon_mm_num = INT64_MAX;
    f.lon_mm_den = 1;
    g.lon_udeg = 100;
    f.origin.lon_udeg = 0;
    CHECK(sp_geo_to_local(&f, &g, &out) == SP_ERR_OVERFLOW, "geo overflow");
    CHECK(sp_geo_to_local(&f, (sp_geo *) 0, &out) == SP_ERR_NULL, "geo null");

    h.a = 3;
    h.b = 4;
    h.c = 5;
    t = V2(0, 0);
    p = V2(1, 0);
    CHECK(sp_pose2_apply(&h, &t, &p, &hv) == SP_OK && hv.X == 3 && hv.Y == 4 && hv.W == 5,
          "pose rotate 3-4-5");
    t = V2(10, -2);
    p = V2(0, 5);
    CHECK(sp_pose2_apply(&h, &t, &p, &hv) == SP_OK && hv.X == -20 + 50 && hv.Y == 15 - 10,
          "pose rotate+translate");
    g_rng = 0x7070u;
    h.a = 20;
    h.b = -21;
    h.c = 29;
    t = V2(0, 0);
    for (i = 0; i < 500; i++) {
        p = V2(rr(-M, M), rr(-M, M));
        if (sp_pose2_apply(&h, &t, &p, &hv) != SP_OK) {
            bad++;
            continue;
        }
#ifdef __SIZEOF_INT128__
        if ((__int128) hv.X * hv.X + (__int128) hv.Y * hv.Y !=
            (__int128) 841 * ((__int128) p.x * p.x + (__int128) p.y * p.y))
            bad++;
#endif
    }
    CHECK(bad == 0, "pose preserves norm exactly");
    h.c = 30;
    CHECK(sp_heading_validate(&h) == SP_ERR_PARAM, "heading not pythagorean");
    h.c = 0;
    CHECK(sp_heading_validate(&h) == SP_ERR_RANGE, "heading c=0");
    h.a = SP_HEADING_MAX + 1;
    h.c = 5;
    CHECK(sp_heading_validate(&h) == SP_ERR_RANGE, "heading too big");
    CHECK(sp_pose2_apply(&h, &t, &p, &hv) == SP_ERR_RANGE, "pose bad heading");
    h.a = 3;
    h.b = 4;
    h.c = 5;
    p = V2(M + 1, 0);
    CHECK(sp_pose2_apply(&h, &t, &p, &hv) == SP_ERR_RANGE, "pose range");
    CHECK(sp_pose2_apply(&h, &t, &p, (sp_hvec2 *) 0) == SP_ERR_NULL, "pose null");
    CHECK(sp_heading_validate((sp_heading *) 0) == SP_ERR_NULL, "heading null");
}

/* ===================================================================== */
static sp_fuse_policy pol(void)
{
    sp_fuse_policy p;
    p.cruise_speed_mm_s = 10000;
    p.slow_speed_mm_s = 3000;
    p.conflict_speed_mm_s = 1000;
    p.hold_dist_mm = 2000;
    p.slow_dist_mm = 10000;
    p.base_margin_mm = 500;
    p.conflict_extra_mm = 1500;
    p.min_healthy = 1;
    p.hold_on_conflict = 0;
    return p;
}
static sp_reading RD(int64_t v, int64_t t, sp_health_t h)
{
    sp_reading r;
    r.value_mm = v;
    r.tol_mm = t;
    r.health = h;
    return r;
}

static void test_fusion(void)
{
    sp_fuse_policy p = pol();
    sp_reading r[SP_FUSE_MAX + 1];
    memset(r, 0, sizeof r);
    sp_advice a;
    sp_fused f;
    int i, bad = 0;

    CHECK(sp_fuse_range(r, 0, &p, &a) == SP_OK && a.state == SP_STATE_NEITHER &&
              a.action == SP_ACT_HOLD && a.speed_cap_mm_s == 0 && a.margin_mm == 2000 &&
              a.advisory == 1 && a.requires_human_approval == 1,
          "fusion: no sources -> NEITHER HOLD");
    r[0] = RD(50000, 100, SP_HEALTH_FAULT);
    r[1] = RD(50000, 100, SP_HEALTH_FAULT);
    CHECK(sp_fuse_range(r, 2, &p, &a) == SP_OK && a.state == SP_STATE_NEITHER &&
              a.action == SP_ACT_HOLD && a.fused.n_fault == 2,
          "fusion: all faulted -> NEITHER HOLD");
    r[0] = RD(50000, 100, SP_HEALTH_OK);
    CHECK(sp_fuse_range(r, 1, &p, &a) == SP_OK && a.state == SP_STATE_TRUE &&
              a.action == SP_ACT_PROCEED && a.speed_cap_mm_s == 10000 && a.fused.lo == 49900 &&
              a.fused.hi == 50100 && a.margin_mm == 500,
          "fusion: single ok -> TRUE PROCEED");
    r[1] = RD(50150, 100, SP_HEALTH_OK);
    CHECK(sp_fuse_range(r, 2, &p, &a) == SP_OK && a.state == SP_STATE_TRUE && a.fused.lo == 50050 &&
              a.fused.hi == 50100 && a.distance_used_mm == 50050 && a.action == SP_ACT_PROCEED,
          "fusion: overlap -> intersection");
    r[1] = RD(50200, 100, SP_HEALTH_OK); /* touches exactly at 50100 */
    CHECK(sp_fuse_range(r, 2, &p, &a) == SP_OK && a.state == SP_STATE_TRUE && a.fused.lo == 50100 &&
              a.fused.hi == 50100,
          "fusion: touching intervals agree");
    r[1] = RD(50201, 100, SP_HEALTH_OK); /* 1 mm gap */
    CHECK(sp_fuse_range(r, 2, &p, &a) == SP_OK && a.state == SP_STATE_BOTH &&
              a.action == SP_ACT_SLOW && a.speed_cap_mm_s == 1000 && a.margin_mm == 2000 &&
              a.distance_used_mm == 49900 && a.fused.cons_lo == 49900 && a.fused.cons_hi == 50301,
          "fusion: 1 mm gap -> BOTH SLOW cautious");
    /* LiDAR in smoke: sees a wall at 1.5 m, radar sees 40 m */
    r[0] = RD(1500, 50, SP_HEALTH_OK);
    r[1] = RD(40000, 200, SP_HEALTH_OK);
    CHECK(sp_fuse_range(r, 2, &p, &a) == SP_OK && a.state == SP_STATE_BOTH &&
              a.action == SP_ACT_HOLD && a.distance_used_mm == 1450,
          "fusion: conflict near -> BOTH HOLD");
    r[0] = RD(3500, 0, SP_HEALTH_OK); /* hold + extra = 3500 -> HOLD */
    CHECK(sp_fuse_range(r, 2, &p, &a) == SP_OK && a.state == SP_STATE_BOTH &&
              a.action == SP_ACT_HOLD,
          "fusion: conflict at hold+extra boundary -> HOLD");
    r[0] = RD(3501, 0, SP_HEALTH_OK);
    CHECK(sp_fuse_range(r, 2, &p, &a) == SP_OK && a.state == SP_STATE_BOTH &&
              a.action == SP_ACT_SLOW,
          "fusion: conflict just beyond -> SLOW");
    p.hold_on_conflict = 1;
    CHECK(sp_fuse_range(r, 2, &p, &a) == SP_OK && a.action == SP_ACT_HOLD && a.speed_cap_mm_s == 0,
          "fusion: hold_on_conflict");
    p = pol();
    r[0] = RD(30000, 100, SP_HEALTH_OK);
    r[1] = RD(30050, 100, SP_HEALTH_OK);
    r[2] = RD(60000, 100, SP_HEALTH_OK);
    CHECK(sp_fuse_range(r, 3, &p, &a) == SP_OK && a.state == SP_STATE_BOTH &&
              a.distance_used_mm == 29900,
          "fusion: 2 of 3 agree is still BOTH (no voting)");
    r[2] = RD(60000, 100, SP_HEALTH_FAULT);
    CHECK(sp_fuse_range(r, 3, &p, &a) == SP_OK && a.state == SP_STATE_TRUE &&
              a.action == SP_ACT_SLOW && a.speed_cap_mm_s == 3000 && a.fused.n_fault == 1 &&
              a.fused.n_ok == 2,
          "fusion: agree with one faulted -> TRUE but SLOW");
    r[0] = RD(30000, 100, SP_HEALTH_DEGRADED);
    CHECK(sp_fuse_range(r, 1, &p, &a) == SP_OK && a.state == SP_STATE_TRUE &&
              a.action == SP_ACT_SLOW && a.fused.n_degraded == 1,
          "fusion: degraded only -> SLOW");
    r[0] = RD(2100, 100, SP_HEALTH_OK); /* lo = 2000 = hold */
    CHECK(sp_fuse_range(r, 1, &p, &a) == SP_OK && a.action == SP_ACT_HOLD, "TRUE at hold -> HOLD");
    r[0] = RD(2101, 100, SP_HEALTH_OK);
    CHECK(sp_fuse_range(r, 1, &p, &a) == SP_OK && a.action == SP_ACT_SLOW, "TRUE hold+1 -> SLOW");
    r[0] = RD(10100, 100, SP_HEALTH_OK);
    CHECK(sp_fuse_range(r, 1, &p, &a) == SP_OK && a.action == SP_ACT_SLOW,
          "TRUE at slow_dist -> SLOW");
    r[0] = RD(10101, 100, SP_HEALTH_OK);
    CHECK(sp_fuse_range(r, 1, &p, &a) == SP_OK && a.action == SP_ACT_PROCEED,
          "TRUE slow_dist+1 -> PROCEED");
    p.min_healthy = 2;
    r[1] = RD(10101, 100, SP_HEALTH_FAULT);
    CHECK(sp_fuse_range(r, 2, &p, &a) == SP_OK && a.state == SP_STATE_NEITHER &&
              a.action == SP_ACT_HOLD && a.fused.cons_lo == 10001,
          "fusion: below min_healthy -> NEITHER HOLD");
    p = pol();

    /* property: matches a reference, never FALSE, PROCEED only when safe */
    g_rng = 0xF00Du;
    for (i = 0; i < 3000; i++) {
        uint32_t n = (uint32_t) rr(0, 5), k, nh = 0;
        int64_t mlo = 0, mhi = 0, clo = 0;
        int nfd = 0;
        sp_state_t ref;
        for (k = 0; k < n; k++) {
            r[k] = RD(rr(0, 20000), rr(0, 3000), (sp_health_t) rr(0, 2));
            if (r[k].health == SP_HEALTH_FAULT || r[k].health == SP_HEALTH_DEGRADED) nfd = 1;
            if (r[k].health == SP_HEALTH_FAULT) continue;
            if (nh == 0 || r[k].value_mm - r[k].tol_mm > mlo) mlo = r[k].value_mm - r[k].tol_mm;
            if (nh == 0 || r[k].value_mm + r[k].tol_mm < mhi) mhi = r[k].value_mm + r[k].tol_mm;
            if (nh == 0 || r[k].value_mm - r[k].tol_mm < clo) clo = r[k].value_mm - r[k].tol_mm;
            nh++;
        }
        ref = nh == 0 ? SP_STATE_NEITHER : (mlo <= mhi ? SP_STATE_TRUE : SP_STATE_BOTH);
        if (sp_fuse_range(r, n, &p, &a) != SP_OK || a.state != ref || a.state == SP_STATE_FALSE ||
            a.advisory != 1 || a.requires_human_approval != 1 ||
            (a.action == SP_ACT_PROCEED && (ref != SP_STATE_TRUE || nfd)) ||
            (ref == SP_STATE_TRUE && (a.fused.lo != mlo || a.fused.hi != mhi)) ||
            (ref == SP_STATE_BOTH && a.distance_used_mm != clo) ||
            (a.action == SP_ACT_HOLD) != (a.speed_cap_mm_s == 0))
            bad++;
    }
    CHECK(bad == 0, "fusion random property");

    /* errors */
    CHECK(sp_fuse_range(r, SP_FUSE_MAX + 1, &p, &a) == SP_ERR_CAPACITY, "fusion capacity");
    CHECK(sp_fuse_range((sp_reading *) 0, 1, &p, &a) == SP_ERR_NULL, "fusion null readings");
    CHECK(sp_fuse_range(r, 1, &p, (sp_advice *) 0) == SP_ERR_NULL, "fusion null out");
    CHECK(sp_fuse_range(r, 1, (sp_fuse_policy *) 0, &a) == SP_ERR_NULL, "fusion null policy");
    r[0] = RD(M + 1, 0, SP_HEALTH_OK);
    CHECK(sp_fuse_range(r, 1, &p, &a) == SP_ERR_RANGE, "fusion value range");
    r[0] = RD(0, -1, SP_HEALTH_OK);
    CHECK(sp_fuse_range(r, 1, &p, &a) == SP_ERR_RANGE, "fusion tol negative");
    r[0] = RD(0, M + 1, SP_HEALTH_OK);
    CHECK(sp_fuse_range(r, 1, &p, &a) == SP_ERR_RANGE, "fusion tol too big");
    r[0] = RD(0, 0, (sp_health_t) 3);
    CHECK(sp_fuse_range(r, 1, &p, &a) == SP_ERR_RANGE, "fusion bad health");
    CHECK(sp_fuse_interval(r, 0, 0, &f) == SP_ERR_RANGE, "interval min_healthy 0");
    CHECK(sp_fuse_interval(r, 0, SP_FUSE_MAX + 1, &f) == SP_ERR_RANGE, "interval min_healthy big");
    p.slow_speed_mm_s = 20000;
    CHECK(sp_fuse_policy_validate(&p) == SP_ERR_PARAM, "policy slow > cruise");
    p = pol();
    p.conflict_speed_mm_s = 4000;
    CHECK(sp_fuse_policy_validate(&p) == SP_ERR_PARAM, "policy conflict > slow");
    p = pol();
    p.hold_dist_mm = 20000;
    CHECK(sp_fuse_policy_validate(&p) == SP_ERR_PARAM, "policy hold > slow_dist");
    p = pol();
    p.min_healthy = 0;
    CHECK(sp_fuse_policy_validate(&p) == SP_ERR_RANGE, "policy min_healthy 0");
    p = pol();
    p.hold_on_conflict = 2;
    CHECK(sp_fuse_policy_validate(&p) == SP_ERR_PARAM, "policy hold flag");
    p = pol();
    p.base_margin_mm = -1;
    CHECK(sp_fuse_policy_validate(&p) == SP_ERR_RANGE, "policy margin negative");
    p = pol();
    p.cruise_speed_mm_s = -1;
    CHECK(sp_fuse_policy_validate(&p) == SP_ERR_RANGE, "policy speed negative");
    p = pol();
    p.conflict_extra_mm = M + 1;
    CHECK(sp_fuse_policy_validate(&p) == SP_ERR_RANGE, "policy extra too big");
    CHECK(sp_fuse_policy_validate((sp_fuse_policy *) 0) == SP_ERR_NULL, "policy null");
    p.conflict_extra_mm = 0;
    p = pol();
    r[0] = RD(5, 0, SP_HEALTH_OK);
    CHECK(sp_fuse_range(r, 1, &(sp_fuse_policy){0, 0, 0, 0, 0, 0, 0, 0, 0}, &a) == SP_ERR_RANGE,
          "fusion invalid policy refused");

    /* position fusion */
    {
        sp_pos_reading pr[3];
        sp_pos_fused pf;
        pr[0].pos = V3(1000, 2000, 3000);
        pr[0].tol_mm = 50;
        pr[0].health = SP_HEALTH_OK;
        pr[1].pos = V3(1060, 1990, 3000);
        pr[1].tol_mm = 20;
        pr[1].health = SP_HEALTH_OK;
        CHECK(sp_fuse_position(pr, 2, 1, &pf) == SP_OK && pf.state == SP_STATE_TRUE &&
                  pf.lo.x == 1040 && pf.hi.x == 1050 && pf.lo.y == 1970 && pf.hi.y == 2010 &&
                  pf.lo.z == 2980 && pf.hi.z == 3020,
              "position agree box");
        pr[1].pos.z = 3100; /* z conflict */
        CHECK(sp_fuse_position(pr, 2, 1, &pf) == SP_OK && pf.state == SP_STATE_BOTH &&
                  pf.lo.x == 0 && pf.hi.z == 0,
              "position one-axis conflict -> BOTH");
        pr[0].health = SP_HEALTH_FAULT;
        pr[1].health = SP_HEALTH_FAULT;
        CHECK(sp_fuse_position(pr, 2, 1, &pf) == SP_OK && pf.state == SP_STATE_NEITHER,
              "position all faulted -> NEITHER");
        CHECK(sp_fuse_position(pr, SP_FUSE_MAX + 1, 1, &pf) == SP_ERR_CAPACITY,
              "position capacity");
        CHECK(sp_fuse_position((sp_pos_reading *) 0, 1, 1, &pf) == SP_ERR_NULL, "position null");
        pr[0].pos.y = M + 1;
        CHECK(sp_fuse_position(pr, 1, 1, &pf) == SP_ERR_RANGE, "position range");
    }
}

/* ===================================================================== */
static int64_t g_cm[SP_HUNG_MAX][SP_HUNG_MAX];
static int64_t bf_best;
static void bf_perm(uint32_t n, uint32_t row, uint32_t used, int64_t acc)
{
    uint32_t j;
    if (row == n) {
        if (acc < bf_best) bf_best = acc;
        return;
    }
    for (j = 0; j < n; j++)
        if (!(used & (1u << j))) bf_perm(n, row + 1, used | (1u << j), acc + g_cm[row][j]);
}

static void test_hungarian(void)
{
    int32_t r2c[SP_HUNG_MAX];
    int64_t tot;
    int i, bad = 0;
    g_rng = 0xA55A5AA5u;
    for (i = 0; i < 400; i++) {
        uint32_t n = (uint32_t) rr(1, 7), x, y, seen = 0;
        int64_t sum = 0, hi = i % 3 == 0 ? 5 : 1000;
        for (x = 0; x < n; x++)
            for (y = 0; y < n; y++) g_cm[x][y] = rr(0, hi);
        bf_best = INT64_MAX;
        bf_perm(n, 0, 0, 0);
        if (sp_hungarian(n, (const int64_t(*)[SP_HUNG_MAX]) g_cm, r2c, &tot) != SP_OK) {
            bad++;
            continue;
        }
        for (x = 0; x < n; x++) {
            seen |= 1u << r2c[x];
            sum += g_cm[x][r2c[x]];
        }
        if (tot != bf_best || sum != tot || seen != (1u << n) - 1u) bad++;
    }
    CHECK(bad == 0, "hungarian vs brute force (400 instances)");
    /* 16x16 with large entries: still a permutation, and no better swap */
    {
        uint32_t x, y;
        int improved = 0;
        for (x = 0; x < 16; x++)
            for (y = 0; y < 16; y++) g_cm[x][y] = rr(0, SP_HUNG_COST_MAX);
        CHECK(sp_hungarian(16, (const int64_t(*)[SP_HUNG_MAX]) g_cm, r2c, &tot) == SP_OK,
              "hungarian 16x16 big entries");
        for (x = 0; x < 16; x++)
            for (y = x + 1; y < 16; y++)
                if (g_cm[x][r2c[y]] + g_cm[y][r2c[x]] < g_cm[x][r2c[x]] + g_cm[y][r2c[y]])
                    improved = 1;
        CHECK(!improved, "hungarian 16x16 2-opt optimal");
    }
    g_cm[0][0] = -1;
    CHECK(sp_hungarian(2, (const int64_t(*)[SP_HUNG_MAX]) g_cm, r2c, &tot) == SP_ERR_RANGE,
          "hungarian negative entry");
    g_cm[0][0] = SP_HUNG_COST_MAX + 1;
    CHECK(sp_hungarian(2, (const int64_t(*)[SP_HUNG_MAX]) g_cm, r2c, &tot) == SP_ERR_RANGE,
          "hungarian entry too big");
    CHECK(sp_hungarian(0, (const int64_t(*)[SP_HUNG_MAX]) g_cm, r2c, &tot) == SP_ERR_CAPACITY,
          "hungarian n=0");
    CHECK(sp_hungarian(17, (const int64_t(*)[SP_HUNG_MAX]) g_cm, r2c, &tot) == SP_ERR_CAPACITY,
          "hungarian n=17");
    CHECK(sp_hungarian(2, (const int64_t(*)[SP_HUNG_MAX]) 0, r2c, &tot) == SP_ERR_NULL,
          "hungarian null");
}

static sp_vehicle VEH(uint32_t id, int64_t x, int64_t y, uint32_t mask)
{
    sp_vehicle v;
    memset(&v, 0, sizeof v);
    v.id = id;
    v.vclass = SP_VEH_MULTIROTOR;
    v.purpose_mask = mask;
    v.home = V3(x, y, 50000);
    v.payload_capacity_g = 2000;
    v.energy_budget_mwh = 100000000;
    v.rate_dist_mwh_per_km = 20000;
    v.rate_climb_mwh_per_m = 100;
    v.rate_time_mwh_per_h = 36000;
    v.cruise_speed_mm_s = 10000;
    v.noise_db = 60;
    v.priority = 0;
    return v;
}
static sp_task TASK(uint32_t id, sp_purpose_t pu, int64_t x0, int64_t y0, int64_t x1, int64_t y1,
                    int64_t g)
{
    sp_task t;
    t.id = id;
    t.purpose = pu;
    t.pickup = V3(x0, y0, 50000);
    t.dropoff = V3(x1, y1, 50000);
    t.payload_g = g;
    return t;
}

static int64_t abf_cost[SP_MAX_VEHICLES][SP_MAX_TASKS];
static int abf_ok[SP_MAX_VEHICLES][SP_MAX_TASKS];
static int abf_bestn;
static int64_t abf_bestc;
static void abf(uint32_t nv, uint32_t nt, uint32_t vi, uint32_t used, int cnt, int64_t c)
{
    uint32_t j;
    if (vi == nv) {
        if (cnt > abf_bestn || (cnt == abf_bestn && c < abf_bestc)) {
            abf_bestn = cnt;
            abf_bestc = c;
        }
        return;
    }
    abf(nv, nt, vi + 1, used, cnt, c);
    for (j = 0; j < nt; j++)
        if (abf_ok[vi][j] && !(used & (1u << j)))
            abf(nv, nt, vi + 1, used | (1u << j), cnt + 1, c + abf_cost[vi][j]);
}

static sp_mission g_m;
static void base_mission(void)
{
    memset(&g_m, 0, sizeof g_m);
    g_m.purpose = SP_PURPOSE_DELIVERY;
    g_m.epoch_ms = 1700000000000LL;
    g_m.has_keep_in = 1;
    rect(&g_m.keep_in, -1000000, -1000000, 1000000, 1000000);
    g_m.band.z_min = 10000;
    g_m.band.z_max = 120000;
    g_m.clearance_mm = 5000;
    g_m.energy_budget_mwh = 1000000000;
    g_m.n_zones = 0;
}

static void test_allocation(void)
{
    sp_vehicle v[SP_MAX_VEHICLES + 1];
    sp_task t[SP_MAX_TASKS + 1];
    sp_alloc_cfg cfg;
    sp_alloc_result res;
    sp_path route;
    int i, bad = 0;
    uint32_t mask = SP_PURPOSE_BIT(SP_PURPOSE_DELIVERY) | SP_PURPOSE_BIT(SP_PURPOSE_SURVEY);

    cfg.w_dist_per_m = 1;
    cfg.w_energy_per_mwh = 1;
    cfg.mission = (const sp_mission *) 0;
    cfg.depart_ms = 0;

    /* route build exact times: 300,400 -> 500 m at 10 m/s = 50 s */
    v[0] = VEH(1, 0, 0, mask);
    t[0] = TASK(1, SP_PURPOSE_DELIVERY, 300000, 400000, 300000, 400000, 100);
    CHECK(sp_route_build(&v[0], &t[0], 1000, &route) == SP_OK && route.n == 4 &&
              route.wp[0].t_ms == 1000 && route.wp[1].t_ms == 51000 && route.wp[2].t_ms == 51001 &&
              route.wp[3].t_ms == 101001,
          "route build times (zero leg -> 1 ms)");
    {
        int64_t e;
        sp_unassigned_t s;
        int64_t c;
        /* energy: len 1,000,000 mm * 20000 / 1e6 = 20000; time 100001 ms *
         * 36000 / 3.6e6 = 1000.01 -> 1001; climb 0 */
        CHECK(sp_path_energy(&v[0], &route, &e) == SP_OK && e == 21001, "route energy exact");
        CHECK(sp_alloc_pair(&v[0], &t[0], &cfg, &s, &c) == SP_OK && s == SP_UA_ASSIGNED &&
                  c == 1000 + 21001,
              "pair cost = metres + mWh");
        v[0].energy_budget_mwh = 21000;
        CHECK(sp_alloc_pair(&v[0], &t[0], &cfg, &s, &c) == SP_OK && s == SP_UA_ENERGY,
              "pair energy refused one mWh short");
        v[0].energy_budget_mwh = 21001;
        CHECK(sp_alloc_pair(&v[0], &t[0], &cfg, &s, &c) == SP_OK && s == SP_UA_ASSIGNED,
              "pair energy exact budget ok");
        t[0].payload_g = 2001;
        CHECK(sp_alloc_pair(&v[0], &t[0], &cfg, &s, &c) == SP_OK && s == SP_UA_PAYLOAD,
              "pair payload refused");
        t[0].payload_g = 100;
        t[0].purpose = SP_PURPOSE_AGRICULTURE;
        CHECK(sp_alloc_pair(&v[0], &t[0], &cfg, &s, &c) == SP_OK && s == SP_UA_PURPOSE,
              "pair purpose refused");
        t[0].purpose = (sp_purpose_t) 9;
        CHECK(sp_alloc_pair(&v[0], &t[0], &cfg, &s, &c) == SP_ERR_PURPOSE, "task purpose invalid");
        t[0].purpose = SP_PURPOSE_DELIVERY;
        cfg.w_dist_per_m = -1;
        CHECK(sp_alloc_pair(&v[0], &t[0], &cfg, &s, &c) == SP_ERR_RANGE, "pair weight range");
        cfg.w_dist_per_m = 1;
        v[0].rate_dist_mwh_per_km = SP_RATE_MAX;
        v[0].energy_budget_mwh = SP_ENERGY_MAX;
        cfg.w_energy_per_mwh = (int64_t) 1 << 20;
        CHECK(sp_alloc_pair(&v[0], &t[0], &cfg, &s, &c) == SP_ERR_OVERFLOW,
              "pair cost overflow reported");
        cfg.w_energy_per_mwh = 1;
        CHECK(sp_route_build(&v[0], &t[0], SP_TIME_MAX, &route) == SP_ERR_RANGE,
              "route past time range");
        CHECK(sp_route_build(&v[0], &t[0], -1, &route) == SP_ERR_RANGE, "route negative depart");
        CHECK(sp_route_build(&v[0], (sp_task *) 0, 0, &route) == SP_ERR_NULL, "route null");
        CHECK(sp_alloc_pair(&v[0], &t[0], &cfg, &s, (int64_t *) 0) == SP_ERR_NULL, "pair null");
    }

    /* scenario: 2 vehicles, 4 tasks with one of each failure reason */
    v[0] = VEH(1, 0, 0, mask);
    v[1] = VEH(2, 100000, 0, SP_PURPOSE_BIT(SP_PURPOSE_DELIVERY));
    v[1].payload_capacity_g = 5000;
    t[0] = TASK(10, SP_PURPOSE_DELIVERY, 10000, 0, 20000, 0, 100);    /* near v0 */
    t[1] = TASK(11, SP_PURPOSE_DELIVERY, 110000, 0, 120000, 0, 4000); /* heavy: v1 only */
    t[2] = TASK(12, SP_PURPOSE_INSPECTION, 0, 0, 1000, 0, 0);         /* nobody */
    t[3] = TASK(13, SP_PURPOSE_DELIVERY, 0, 0, 5000, 0, 9000);        /* too heavy */
    CHECK(sp_allocate(v, 2, t, 4, &cfg, &res) == SP_OK && res.n_assigned == 2 &&
              res.task_vehicle[0] == 0 && res.task_vehicle[1] == 1 &&
              res.task_status[2] == SP_UA_PURPOSE && res.task_status[3] == SP_UA_PAYLOAD &&
              res.vehicle_task[0] == 0 && res.vehicle_task[1] == 1 && res.advisory == 1 &&
              res.requires_human_approval == 1 &&
              res.total_cost == res.task_cost[0] + res.task_cost[1],
          "alloc scenario with reasons");
    t[2] = TASK(12, SP_PURPOSE_DELIVERY, 900000, 0, 900000, 900000, 0); /* far */
    v[0].energy_budget_mwh = 50000;
    v[1].energy_budget_mwh = 50000;
    CHECK(sp_allocate(v, 2, t, 3, &cfg, &res) == SP_OK && res.task_status[2] == SP_UA_ENERGY &&
              res.task_vehicle[2] == -1,
          "alloc energy reason");
    v[0].energy_budget_mwh = 100000000;
    t[1] = TASK(11, SP_PURPOSE_DELIVERY, 30000, 0, 20000, 0, 100);
    v[1] = VEH(2, 900000, 900000, SP_PURPOSE_BIT(SP_PURPOSE_SURVEY));
    CHECK(sp_allocate(v, 2, t, 2, &cfg, &res) == SP_OK && res.n_assigned == 1 &&
              (res.task_status[0] == SP_UA_OUTNUMBERED || res.task_status[1] == SP_UA_OUTNUMBERED),
          "alloc outnumbered reason");
    /* mission route check: protected zone (always active) on the way */
    base_mission();
    g_m.n_zones = 1;
    g_m.zones[0].kind = SP_ZONE_PROTECTED_HABITAT;
    rect(&g_m.zones[0].area, 40000, -500000, 60000, 500000);
    g_m.zones[0].always_active = 1;
    g_m.zones[0].noise_buffer_mm = 0;
    g_m.zones[0].noise_ceiling_db = 100;
    cfg.mission = &g_m;
    v[1] = VEH(2, 0, 200000, mask);
    t[0] = TASK(10, SP_PURPOSE_DELIVERY, 100000, 0, 100000, 0, 100); /* crosses zone */
    t[1] = TASK(11, SP_PURPOSE_DELIVERY, 0, 100000, 0, 150000, 100);
    CHECK(sp_allocate(v, 2, t, 2, &cfg, &res) == SP_OK && res.task_status[0] == SP_UA_ROUTE &&
              res.task_vehicle[1] >= 0,
          "alloc route refused through protected zone");
    t[1].purpose = SP_PURPOSE_SURVEY; /* mission is delivery */
    CHECK(sp_allocate(v, 2, t, 2, &cfg, &res) == SP_OK && res.task_status[1] == SP_UA_PURPOSE,
          "alloc task purpose differs from mission");
    g_m.purpose = (sp_purpose_t) 0;
    CHECK(sp_allocate(v, 2, t, 2, &cfg, &res) == SP_ERR_PURPOSE, "alloc invalid mission");
    cfg.mission = (const sp_mission *) 0;

    /* random instances vs brute force */
    g_rng = 0xC0FFEEu;
    for (i = 0; i < 150; i++) {
        uint32_t nv = (uint32_t) rr(1, 5), nt = (uint32_t) rr(1, 5), x, y;
        int stx = SP_OK;
        for (x = 0; x < nv; x++) {
            v[x] = VEH(x, rr(-100000, 100000), rr(-100000, 100000), (uint32_t) rr(1, 31) << 1);
            v[x].payload_capacity_g = rr(0, 3000);
            v[x].energy_budget_mwh = rr(1000, 20000);
        }
        for (y = 0; y < nt; y++)
            t[y] = TASK(y, (sp_purpose_t) rr(1, 5), rr(-100000, 100000), rr(-100000, 100000),
                        rr(-100000, 100000), rr(-100000, 100000), rr(0, 3000));
        cfg.w_dist_per_m = rr(0, 3);
        cfg.w_energy_per_mwh = rr(0, 3);
        for (x = 0; x < nv && stx == SP_OK; x++)
            for (y = 0; y < nt && stx == SP_OK; y++) {
                sp_unassigned_t s;
                stx = sp_alloc_pair(&v[x], &t[y], &cfg, &s, &abf_cost[x][y]);
                abf_ok[x][y] = s == SP_UA_ASSIGNED;
            }
        abf_bestn = -1;
        abf_bestc = 0;
        abf(nv, nt, 0, 0, 0, 0);
        if (stx != SP_OK || sp_allocate(v, nv, t, nt, &cfg, &res) != SP_OK ||
            (int) res.n_assigned != abf_bestn || res.total_cost != abf_bestc)
            bad++;
        for (y = 0; y < nt; y++) {
            int vi = res.task_vehicle[y];
            if (vi >= 0 && (!abf_ok[vi][y] || res.vehicle_task[vi] != (int32_t) y)) bad++;
            if (vi < 0 && res.task_status[y] == SP_UA_ASSIGNED) bad++;
        }
    }
    CHECK(bad == 0, "allocation vs brute force (150 instances)");

    cfg.w_dist_per_m = 1;
    cfg.w_energy_per_mwh = 1;
    CHECK(sp_allocate(v, 0, t, 1, &cfg, &res) == SP_ERR_CAPACITY, "alloc nv=0");
    CHECK(sp_allocate(v, 1, t, SP_MAX_TASKS + 1, &cfg, &res) == SP_ERR_CAPACITY, "alloc nt>max");
    CHECK(sp_allocate(v, SP_MAX_VEHICLES + 1, t, 1, &cfg, &res) == SP_ERR_CAPACITY, "alloc nv>max");
    CHECK(sp_allocate(v, 1, t, 1, &cfg, (sp_alloc_result *) 0) == SP_ERR_NULL, "alloc null");
    v[0].cruise_speed_mm_s = 0;
    CHECK(sp_allocate(v, 1, t, 1, &cfg, &res) == SP_ERR_RANGE, "alloc invalid vehicle");
}

/* ===================================================================== */
static sp_path PATH2(int64_t x0, int64_t y0, int64_t z0, int64_t t0, int64_t x1, int64_t y1,
                     int64_t z1, int64_t t1)
{
    sp_path p;
    memset(&p, 0, sizeof p);
    p.n = 2;
    p.wp[0].p = V3(x0, y0, z0);
    p.wp[0].t_ms = t0;
    p.wp[1].p = V3(x1, y1, z1);
    p.wp[1].t_ms = t1;
    return p;
}

static void test_path_check(void)
{
    sp_vehicle v = VEH(1, 0, 0, SP_PURPOSE_BIT(SP_PURPOSE_DELIVERY));
    sp_path p;
    sp_check ck;
    int64_t e;
    const int64_t W0 = 10000000, W1 = 20000000; /* protected window, ms after epoch */

    base_mission();
    /* zone 0 keep-out (airfield), zone 1 protected habitat (nesting season) */
    g_m.n_zones = 2;
    g_m.zones[0].kind = SP_ZONE_KEEP_OUT;
    rect(&g_m.zones[0].area, 200000, -50000, 300000, 50000);
    g_m.zones[0].always_active = 1;
    g_m.zones[1].kind = SP_ZONE_PROTECTED_HABITAT;
    rect(&g_m.zones[1].area, -300000, -50000, -200000, 50000);
    g_m.zones[1].always_active = 0;
    g_m.zones[1].active_from_ms = g_m.epoch_ms + W0;
    g_m.zones[1].active_until_ms = g_m.epoch_ms + W1;
    g_m.zones[1].noise_buffer_mm = 50000;
    g_m.zones[1].noise_ceiling_db = 55;
    CHECK(sp_mission_validate(&g_m) == SP_OK, "mission valid");

    p = PATH2(0, 300000, 50000, 0, 300000, 700000, 50000, 50000);
    CHECK(sp_path_check(&g_m, &v, &p, 0, &ck) == SP_OK && ck.ok == 1 &&
              ck.reason == SP_REFUSE_NONE && ck.energy_mwh == 10500,
          "path ok, energy 10500 mWh");
    CHECK(sp_path_energy(&v, &p, &e) == SP_OK && e == 10500, "path energy");
    /* climb cost: 100 m climb * 100 mWh/m = 10000 */
    p = PATH2(0, 300000, 20000, 0, 0, 300000, 120000, 10000);
    CHECK(sp_path_energy(&v, &p, &e) == SP_OK && e == 2000 + 10000 + 100, "climb energy");
    /* altitude band */
    p = PATH2(0, 300000, 10000, 0, 300000, 700000, 120000, 50000);
    CHECK(sp_path_check(&g_m, &v, &p, 0, &ck) == SP_OK && ck.ok == 1, "band edges ok");
    p.wp[1].p.z = 120001;
    CHECK(sp_path_check(&g_m, &v, &p, 0, &ck) == SP_OK && ck.ok == 0 &&
              ck.reason == SP_REFUSE_ALTITUDE && ck.index == 1,
          "altitude refused");
    /* keep-in */
    p = PATH2(0, 300000, 50000, 0, 0, 1000001, 50000, 50000);
    CHECK(sp_path_check(&g_m, &v, &p, 0, &ck) == SP_OK && ck.reason == SP_REFUSE_KEEP_IN,
          "keep-in outside refused");
    p.wp[1].p.y = 1000000;
    CHECK(sp_path_check(&g_m, &v, &p, 0, &ck) == SP_OK && ck.reason == SP_REFUSE_KEEP_IN,
          "keep-in ON boundary refused (cautious)");
    p.wp[1].p.y = 995001;
    CHECK(sp_path_check(&g_m, &v, &p, 0, &ck) == SP_OK && ck.reason == SP_REFUSE_KEEP_IN,
          "keep-in within clearance refused");
    p.wp[1].p.y = 995000;
    CHECK(sp_path_check(&g_m, &v, &p, 0, &ck) == SP_OK && ck.ok == 1,
          "keep-in exactly at clearance ok");
    /* keep-out */
    p = PATH2(100000, 0, 50000, 0, 400000, 0, 50000, 30000);
    CHECK(sp_path_check(&g_m, &v, &p, 0, &ck) == SP_OK && ck.reason == SP_REFUSE_KEEP_OUT &&
              ck.zone == 0 && ck.index == 0,
          "keep-out crossing refused");
    p = PATH2(100000, 55000, 50000, 0, 400000, 55000, 50000, 30000);
    CHECK(sp_path_check(&g_m, &v, &p, 0, &ck) == SP_OK && ck.ok == 1,
          "keep-out passing exactly at clearance ok");
    p.wp[0].p.y = 54999;
    p.wp[1].p.y = 54999;
    CHECK(sp_path_check(&g_m, &v, &p, 0, &ck) == SP_OK && ck.reason == SP_REFUSE_KEEP_OUT,
          "keep-out 1 mm inside clearance refused");
    p = PATH2(250000, 0, 50000, 0, 250000, 0, 60000, 3000); /* vertical inside */
    CHECK(sp_path_check(&g_m, &v, &p, 0, &ck) == SP_OK && ck.reason == SP_REFUSE_KEEP_OUT,
          "keep-out vertical climb inside refused");
    /* protected habitat, seasonal */
    p = PATH2(-400000, 0, 50000, 0, -100000, 0, 50000, 30000);
    CHECK(sp_path_check(&g_m, &v, &p, 0, &ck) == SP_OK && ck.ok == 1,
          "protected zone outside season ok");
    CHECK(sp_path_check(&g_m, &v, &p, W0 - 30000, &ck) == SP_OK &&
              ck.reason == SP_REFUSE_PROTECTED && ck.zone == 1,
          "protected: segment ending exactly at season start refused");
    CHECK(sp_path_check(&g_m, &v, &p, W0 - 30001, &ck) == SP_OK && ck.ok == 1,
          "protected: segment ending 1 ms before season ok");
    CHECK(sp_path_check(&g_m, &v, &p, W1 - 1, &ck) == SP_OK && ck.reason == SP_REFUSE_PROTECTED,
          "protected: starting in last ms of season refused");
    CHECK(sp_path_check(&g_m, &v, &p, W1, &ck) == SP_OK && ck.ok == 1,
          "protected: starting exactly at season end ok");
    /* noise near an active protected zone */
    p = PATH2(-400000, 70000, 50000, 0, -100000, 70000, 50000, 30000); /* 20 m away */
    CHECK(sp_path_check(&g_m, &v, &p, W0, &ck) == SP_OK && ck.reason == SP_REFUSE_NOISE &&
              ck.zone == 1,
          "noise ceiling refused (60 > 55 dB)");
    v.noise_db = 55;
    CHECK(sp_path_check(&g_m, &v, &p, W0, &ck) == SP_OK && ck.ok == 1, "noise at ceiling ok");
    v.noise_db = 60;
    CHECK(sp_path_check(&g_m, &v, &p, 0, &ck) == SP_OK && ck.ok == 1,
          "noise ignored out of season");
    p = PATH2(-400000, 100000, 50000, 0, -100000, 100000, 50000, 30000); /* exactly 50 m */
    CHECK(sp_path_check(&g_m, &v, &p, W0, &ck) == SP_OK && ck.ok == 1,
          "noise buffer exactly at distance ok");
    /* energy budget, hard */
    p = PATH2(0, 300000, 50000, 0, 300000, 700000, 50000, 50000);
    g_m.energy_budget_mwh = 10499;
    CHECK(sp_path_check(&g_m, &v, &p, 0, &ck) == SP_OK && ck.ok == 0 &&
              ck.reason == SP_REFUSE_ENERGY && ck.energy_mwh == 10500,
          "mission energy budget refused");
    g_m.energy_budget_mwh = 10500;
    CHECK(sp_path_check(&g_m, &v, &p, 0, &ck) == SP_OK && ck.ok == 1, "energy exactly budget ok");
    v.energy_budget_mwh = 10499;
    CHECK(sp_path_check(&g_m, &v, &p, 0, &ck) == SP_OK && ck.reason == SP_REFUSE_ENERGY,
          "vehicle energy refused");
    v.energy_budget_mwh = 100000000;
    g_m.energy_budget_mwh = 1000000000;
    /* purpose */
    v.purpose_mask = SP_PURPOSE_BIT(SP_PURPOSE_SURVEY);
    CHECK(sp_path_check(&g_m, &v, &p, 0, &ck) == SP_OK && ck.reason == SP_REFUSE_PURPOSE,
          "vehicle purpose refused");
    v.purpose_mask = SP_PURPOSE_BIT(SP_PURPOSE_DELIVERY);
    /* no keep-in: anything inside the coordinate range is allowed */
    g_m.has_keep_in = 0;
    p = PATH2(0, 1500000, 50000, 0, 0, 2000000, 50000, 50000);
    CHECK(sp_path_check(&g_m, &v, &p, 0, &ck) == SP_OK && ck.ok == 1, "no keep-in ok");
    g_m.has_keep_in = 1;

    /* error paths */
    p = PATH2(0, 300000, 50000, 0, 300000, 700000, 50000, 50000);
    p.wp[1].t_ms = 0;
    CHECK(sp_path_check(&g_m, &v, &p, 0, &ck) == SP_ERR_ORDER, "path times equal");
    p.wp[1].t_ms = 50000;
    p.n = 1;
    CHECK(sp_path_check(&g_m, &v, &p, 0, &ck) == SP_ERR_DEGENERATE, "path n=1");
    p.n = SP_MAX_WP + 1;
    CHECK(sp_path_validate(&p, 0) == SP_ERR_DEGENERATE, "path n>max");
    p.n = 2;
    CHECK(sp_path_check(&g_m, &v, &p, SP_TIME_MAX - 49999, &ck) == SP_ERR_RANGE,
          "path delay past time range");
    CHECK(sp_path_check(&g_m, &v, &p, SP_TIME_MAX - 50000, &ck) == SP_OK, "path delay at limit");
    CHECK(sp_path_check(&g_m, &v, &p, -1, &ck) == SP_ERR_RANGE, "path negative delay");
    p.wp[0].t_ms = -1;
    CHECK(sp_path_validate(&p, 0) == SP_ERR_RANGE, "path negative time");
    p.wp[0].t_ms = 0;
    p.wp[0].p.x = M + 1;
    CHECK(sp_path_validate(&p, 0) == SP_ERR_RANGE, "path coord range");
    p.wp[0].p.x = 0;
    CHECK(sp_path_check(&g_m, &v, &p, 0, (sp_check *) 0) == SP_ERR_NULL, "check null");
    CHECK(sp_path_validate((sp_path *) 0, 0) == SP_ERR_NULL, "path null");
    CHECK(sp_path_energy(&v, (sp_path *) 0, &e) == SP_ERR_NULL, "energy null");
    g_m.purpose = (sp_purpose_t) 6;
    CHECK(sp_mission_validate(&g_m) == SP_ERR_PURPOSE, "mission purpose 6");
    g_m.purpose = (sp_purpose_t) 0;
    CHECK(sp_path_check(&g_m, &v, &p, 0, &ck) == SP_ERR_PURPOSE, "mission purpose 0");
    g_m.purpose = SP_PURPOSE_DELIVERY;
    g_m.n_zones = SP_MAX_ZONES + 1;
    CHECK(sp_mission_validate(&g_m) == SP_ERR_CAPACITY, "mission zones > max");
    g_m.n_zones = 2;
    g_m.zones[1].active_until_ms = g_m.zones[1].active_from_ms;
    CHECK(sp_mission_validate(&g_m) == SP_ERR_PARAM, "zone empty window");
    g_m.zones[1].active_until_ms = g_m.epoch_ms + W1;
    g_m.zones[1].kind = (sp_zone_kind_t) 7;
    CHECK(sp_mission_validate(&g_m) == SP_ERR_PARAM, "zone kind");
    g_m.zones[1].kind = SP_ZONE_PROTECTED_HABITAT;
    g_m.zones[1].noise_ceiling_db = 201;
    CHECK(sp_mission_validate(&g_m) == SP_ERR_RANGE, "zone noise ceiling");
    g_m.zones[1].noise_ceiling_db = 55;
    g_m.zones[1].noise_buffer_mm = -1;
    CHECK(sp_mission_validate(&g_m) == SP_ERR_RANGE, "zone noise buffer");
    g_m.zones[1].noise_buffer_mm = 50000;
    g_m.zones[1].always_active = 2;
    CHECK(sp_mission_validate(&g_m) == SP_ERR_PARAM, "zone always flag");
    g_m.zones[1].always_active = 0;
    g_m.zones[0].area.n = 2;
    CHECK(sp_mission_validate(&g_m) == SP_ERR_DEGENERATE, "zone polygon invalid");
    g_m.zones[0].area.n = 4;
    g_m.keep_in.v[1] = g_m.keep_in.v[0];
    CHECK(sp_mission_validate(&g_m) == SP_ERR_DEGENERATE, "keep-in polygon invalid");
    rect(&g_m.keep_in, -1000000, -1000000, 1000000, 1000000);
    g_m.has_keep_in = 3;
    CHECK(sp_mission_validate(&g_m) == SP_ERR_PARAM, "keep-in flag");
    g_m.has_keep_in = 1;
    g_m.band.z_min = 200000;
    CHECK(sp_mission_validate(&g_m) == SP_ERR_PARAM, "mission band inverted");
    g_m.band.z_min = 10000;
    g_m.clearance_mm = -1;
    CHECK(sp_mission_validate(&g_m) == SP_ERR_RANGE, "mission clearance");
    g_m.clearance_mm = 5000;
    g_m.energy_budget_mwh = -1;
    CHECK(sp_mission_validate(&g_m) == SP_ERR_RANGE, "mission energy");
    g_m.energy_budget_mwh = 1000;
    g_m.epoch_ms = -1;
    CHECK(sp_mission_validate(&g_m) == SP_ERR_RANGE, "mission epoch");
    g_m.epoch_ms = 0;
    CHECK(sp_mission_validate((sp_mission *) 0) == SP_ERR_NULL, "mission null");
    {
        sp_vehicle w = v;
        w.vclass = (sp_vehicle_class_t) 9;
        CHECK(sp_vehicle_validate(&w) == SP_ERR_PARAM, "vehicle class");
        w = v;
        w.purpose_mask = 1; /* bit 0 is not a purpose */
        CHECK(sp_vehicle_validate(&w) == SP_ERR_PURPOSE, "vehicle mask bit0");
        w.purpose_mask = 0;
        CHECK(sp_vehicle_validate(&w) == SP_ERR_PURPOSE, "vehicle mask empty");
        w.purpose_mask = 1u << 6;
        CHECK(sp_vehicle_validate(&w) == SP_ERR_PURPOSE, "vehicle mask unknown");
        w = v;
        w.noise_db = 201;
        CHECK(sp_vehicle_validate(&w) == SP_ERR_RANGE, "vehicle noise");
        w = v;
        w.cruise_speed_mm_s = SP_SPEED_MAX + 1;
        CHECK(sp_vehicle_validate(&w) == SP_ERR_RANGE, "vehicle speed");
        w = v;
        w.rate_time_mwh_per_h = -1;
        CHECK(sp_vehicle_validate(&w) == SP_ERR_RANGE, "vehicle rate");
        w = v;
        w.energy_budget_mwh = -1;
        CHECK(sp_vehicle_validate(&w) == SP_ERR_RANGE, "vehicle energy");
        w = v;
        w.payload_capacity_g = -1;
        CHECK(sp_vehicle_validate(&w) == SP_ERR_RANGE, "vehicle payload");
        w = v;
        w.home.z = M + 1;
        CHECK(sp_vehicle_validate(&w) == SP_ERR_RANGE, "vehicle home");
        CHECK(sp_vehicle_validate((sp_vehicle *) 0) == SP_ERR_NULL, "vehicle null");
        w = v;
        w.rate_dist_mwh_per_km = SP_RATE_MAX;
        p = PATH2(-M, -M, -M, 0, M, M, M, SP_TIME_MAX);
        CHECK(sp_path_energy(&w, &p, &e) == SP_OK && e > 0, "energy huge path computes");
    }
}

/* reference minimum distance of two linear motions over their overlap */
static double dref_seg_conflict(const sp_waypoint *a0, const sp_waypoint *a1, const sp_waypoint *b0,
                                const sp_waypoint *b1, int *overlap)
{
    double t0 = (double) (a0->t_ms > b0->t_ms ? a0->t_ms : b0->t_ms);
    double t1 = (double) (a1->t_ms < b1->t_ms ? a1->t_ms : b1->t_ms);
    double pa[3], pb[3], va[3], vb[3], r0[3], dv[3], aa = 0, bb = 0, s, best;
    double da = (double) (a1->t_ms - a0->t_ms), db = (double) (b1->t_ms - b0->t_ms);
    int k;
    *overlap = t0 <= t1;
    if (!*overlap) return 0.0;
    pa[0] = (double) a0->p.x;
    pa[1] = (double) a0->p.y;
    pa[2] = (double) a0->p.z;
    pb[0] = (double) b0->p.x;
    pb[1] = (double) b0->p.y;
    pb[2] = (double) b0->p.z;
    va[0] = (double) (a1->p.x - a0->p.x) / da;
    va[1] = (double) (a1->p.y - a0->p.y) / da;
    va[2] = (double) (a1->p.z - a0->p.z) / da;
    vb[0] = (double) (b1->p.x - b0->p.x) / db;
    vb[1] = (double) (b1->p.y - b0->p.y) / db;
    vb[2] = (double) (b1->p.z - b0->p.z) / db;
    for (k = 0; k < 3; k++) {
        r0[k] =
            (pa[k] + va[k] * (t0 - (double) a0->t_ms)) - (pb[k] + vb[k] * (t0 - (double) b0->t_ms));
        dv[k] = va[k] - vb[k];
        aa += dv[k] * dv[k];
        bb += r0[k] * dv[k];
    }
    s = aa > 0.0 ? -bb / aa : 0.0;
    if (s < 0.0) s = 0.0;
    if (s > t1 - t0) s = t1 - t0;
    best = 0.0;
    for (k = 0; k < 3; k++) best += (r0[k] + dv[k] * s) * (r0[k] + dv[k] * s);
    return sqrt(best);
}

static void test_conflict(void)
{
    sp_path a, b;
    sp_conflict c;
    int loss, i, bad = 0, tested = 0;
    int64_t f, u;

    /* head-on, same altitude */
    a = PATH2(-50000, 0, 50000, 0, 50000, 0, 50000, 10000);
    b = PATH2(50000, 0, 50000, 0, -50000, 0, 50000, 10000);
    CHECK(sp_paths_conflict(&a, 0, &b, 0, 10000, &loss, &c) == SP_OK && loss == 1 && c.seg_a == 0 &&
              c.seg_b == 0 && c.t_from == 0 && c.t_until == 10000,
          "head-on conflict");
    /* head-on with vertical separation exactly sep: no loss; sep+1: loss */
    b = PATH2(50000, 0, 60000, 0, -50000, 0, 60000, 10000);
    CHECK(sp_paths_conflict(&a, 0, &b, 0, 10000, &loss, &c) == SP_OK && loss == 0,
          "head-on vertical sep exactly 10 m -> no loss");
    CHECK(sp_paths_conflict(&a, 0, &b, 0, 10001, &loss, &c) == SP_OK && loss == 1,
          "head-on vertical sep 1 mm short -> loss");
    /* parallel, same speed, offset exactly sep (constant relative position) */
    b = PATH2(-50000, 500, 50000, 0, 50000, 500, 50000, 10000);
    CHECK(sp_paths_conflict(&a, 0, &b, 0, 500, &loss, &c) == SP_OK && loss == 0,
          "parallel at exactly sep -> no loss");
    CHECK(sp_paths_conflict(&a, 0, &b, 0, 501, &loss, &c) == SP_OK && loss == 1,
          "parallel at sep-1 -> loss");
    /* crossing at the same time */
    a = PATH2(-50000, 0, 50000, 0, 50000, 0, 50000, 10000);
    b = PATH2(0, -50000, 50000, 0, 0, 50000, 50000, 10000);
    CHECK(sp_paths_conflict(&a, 0, &b, 0, 1000, &loss, &c) == SP_OK && loss == 1, "crossing");
    /* crossing, B 5 s later: min distance 25000*sqrt2 = 35355.339 (interior) */
    CHECK(sp_paths_conflict(&a, 0, &b, 5000, 35355, &loss, &c) == SP_OK && loss == 0,
          "crossing delayed: 35355 < dmin -> no loss");
    CHECK(sp_paths_conflict(&a, 0, &b, 5000, 35356, &loss, &c) == SP_OK && loss == 1,
          "crossing delayed: 35356 > dmin -> loss");
    /* time-separated: same corridor, B after A */
    b = PATH2(-50000, 0, 50000, 0, 50000, 0, 50000, 10000);
    CHECK(sp_paths_conflict(&a, 0, &b, 10001, 1000000, &loss, &c) == SP_OK && loss == 0,
          "time-separated -> no loss");
    /* touching in time at a single instant at the same point */
    b = PATH2(50000, 0, 50000, 10000, 90000, 0, 50000, 20000);
    CHECK(sp_paths_conflict(&a, 0, &b, 0, 1, &loss, &c) == SP_OK && loss == 1 &&
              c.t_from == 10000 && c.t_until == 10000,
          "single shared instant -> loss");
    /* still closing at the end of the overlap: f(L) branch */
    {
        sp_waypoint a0, a1, b0, b1;
        a0.p = V3(0, 0, 0);
        a0.t_ms = 0;
        a1.p = V3(1000, 0, 0);
        a1.t_ms = 1000;
        b0.p = V3(100000, 0, 0);
        b0.t_ms = 0;
        b1.p = V3(99000, 0, 0);
        b1.t_ms = 1000;
        CHECK(sp_seg_conflict(&a0, &a1, 0, &b0, &b1, 0, 98000, &loss, &f, &u) == SP_OK && loss == 0,
              "closing, end distance exactly sep -> no loss");
        CHECK(sp_seg_conflict(&a0, &a1, 0, &b0, &b1, 0, 98001, &loss, &f, &u) == SP_OK && loss == 1,
              "closing, end distance < sep -> loss");
        /* moving apart: minimum at start */
        a1.p = V3(-1000, 0, 0);
        b0.p = V3(100, 0, 0);
        b1.p = V3(1100, 0, 0);
        CHECK(sp_seg_conflict(&a0, &a1, 0, &b0, &b1, 0, 100, &loss, &f, &u) == SP_OK && loss == 0,
              "separating, start exactly sep -> no loss");
        CHECK(sp_seg_conflict(&a0, &a1, 0, &b0, &b1, 0, 101, &loss, &f, &u) == SP_OK && loss == 1,
              "separating, start < sep -> loss");
        /* overlap window with delays */
        CHECK(sp_seg_conflict(&a0, &a1, 300, &b0, &b1, 700, 1, &loss, &f, &u) == SP_OK &&
                  f == 700 && u == 1300,
              "overlap window with delays");
        /* huge coordinates and the full time range */
        a0.p = V3(-M, -M, -M);
        a0.t_ms = 0;
        a1.p = V3(M, M, M);
        a1.t_ms = SP_TIME_MAX;
        b0.p = V3(M, M, -M);
        b0.t_ms = 1;
        b1.p = V3(-M, -M, M);
        b1.t_ms = SP_TIME_MAX - 1;
        CHECK(sp_seg_conflict(&a0, &a1, 0, &b0, &b1, 0, 1000, &loss, &f, &u) == SP_OK,
              "huge coordinates and times compute without overflow");
        {
            int ov;
            double dd = dref_seg_conflict(&a0, &a1, &b0, &b1, &ov);
            CHECK(loss == (dd < 1000.0), "huge case agrees with double reference");
        }
        /* errors */
        CHECK(sp_seg_conflict(&a0, &a1, 0, &b0, &b1, 0, 0, &loss, &f, &u) == SP_ERR_RANGE,
              "conflict sep 0");
        CHECK(sp_seg_conflict(&a0, &a1, 1, &b0, &b1, 0, 10, &loss, &f, &u) == SP_ERR_RANGE,
              "conflict delay past time range");
        CHECK(sp_seg_conflict(&a0, &a1, -1, &b0, &b1, 0, 10, &loss, &f, &u) == SP_ERR_RANGE,
              "conflict negative delay");
        CHECK(sp_seg_conflict(&a1, &a0, 0, &b0, &b1, 0, 10, &loss, &f, &u) == SP_ERR_ORDER,
              "conflict reversed times");
        a0.p.x = M + 1;
        CHECK(sp_seg_conflict(&a0, &a1, 0, &b0, &b1, 0, 10, &loss, &f, &u) == SP_ERR_RANGE,
              "conflict coord range");
        CHECK(sp_seg_conflict(&a0, &a1, 0, &b0, &b1, 0, 10, &loss, &f, (int64_t *) 0) ==
                  SP_ERR_NULL,
              "conflict null");
    }
    /* random vs double reference */
    g_rng = 0x5EEDu;
    for (i = 0; i < 4000; i++) {
        sp_waypoint a0, a1, b0, b1;
        int ov;
        double dd;
        int64_t sep;
        a0.p = V3(rr(-100000, 100000), rr(-100000, 100000), rr(0, 2000));
        a1.p = V3(rr(-100000, 100000), rr(-100000, 100000), rr(0, 2000));
        b0.p = V3(rr(-100000, 100000), rr(-100000, 100000), rr(0, 2000));
        b1.p = V3(rr(-100000, 100000), rr(-100000, 100000), rr(0, 2000));
        a0.t_ms = rr(0, 50000);
        a1.t_ms = a0.t_ms + rr(1, 50000);
        b0.t_ms = rr(0, 50000);
        b1.t_ms = b0.t_ms + rr(1, 50000);
        if (i % 4 == 0) b1.p = V3(b0.p.x + a1.p.x - a0.p.x, b0.p.y, b0.p.z); /* near-parallel */
        dd = dref_seg_conflict(&a0, &a1, &b0, &b1, &ov);
        sep = (int64_t) dd + rr(-2, 2);
        if (sep < 1) sep = 1;
        if (sp_seg_conflict(&a0, &a1, 0, &b0, &b1, 0, sep, &loss, &f, &u) != SP_OK) {
            bad++;
            continue;
        }
        if (!ov) {
            if (loss) bad++;
            continue;
        }
        if (fabs(dd - (double) sep) < 1e-3) continue;
        tested++;
        if (loss != (dd < (double) sep)) bad++;
    }
    CHECK(bad == 0 && tested > 1000, "segment conflict random vs double reference");
    CHECK(sp_paths_conflict(&a, 0, &b, 0, 10, (int *) 0, &c) == SP_ERR_NULL, "paths null");
    a.wp[1].t_ms = 0;
    CHECK(sp_paths_conflict(&a, 0, &b, 0, 10, &loss, &c) == SP_ERR_ORDER, "paths invalid");
}

static void test_deconflict(void)
{
    sp_vehicle v[SP_MAX_VEHICLES + 1];
    sp_path p[SP_MAX_VEHICLES + 1];
    sp_deconf_cfg cfg;
    sp_deconf_plan plan;
    uint32_t i, j;
    int bad = 0;
    uint32_t mask = SP_PURPOSE_BIT(SP_PURPOSE_DELIVERY);

    base_mission();
    g_m.has_keep_in = 0;
    g_m.band.z_min = 0;
    g_m.band.z_max = 200000;
    cfg.sep_mm = 10000;
    cfg.step_ms = 1000;
    cfg.max_steps = 100;
    v[0] = VEH(1, 0, 0, mask);
    v[0].priority = 10;
    v[1] = VEH(2, 0, 0, mask);
    v[1].priority = 5;
    v[2] = VEH(3, 0, 0, mask);
    v[2].priority = 1;
    p[0] = PATH2(-50000, 0, 50000, 0, 50000, 0, 50000, 10000);
    p[1] = PATH2(50000, 0, 50000, 0, -50000, 0, 50000, 10000);
    p[2] = PATH2(-50000, 400000, 50000, 0, 50000, 400000, 50000, 10000);
    CHECK(sp_deconflict(&g_m, v, p, 3, &cfg, &plan) == SP_OK && plan.advisory == 1 &&
              plan.requires_human_approval == 1 && plan.n_conflicts_initial == 1 &&
              plan.conflicts[0].a == 0 && plan.conflicts[0].b == 1,
          "deconflict: initial conflict found");
    /* head-on meets for any delay <= 10000 (at 10000 they share one point) */
    CHECK(plan.status[0] == SP_DC_CLEARED && plan.delay_ms[0] == 0 &&
              plan.status[1] == SP_DC_CLEARED && plan.delay_ms[1] == 11000 &&
              plan.status[2] == SP_DC_CLEARED && plan.delay_ms[2] == 0 && plan.n_cleared == 3,
          "deconflict: lower priority delayed exactly 11 s");
    for (i = 0; i < 3; i++)
        for (j = i + 1; j < 3; j++) {
            int loss;
            if (sp_paths_conflict(&p[i], plan.delay_ms[i], &p[j], plan.delay_ms[j], cfg.sep_mm,
                                  &loss, (sp_conflict *) 0) != SP_OK ||
                loss)
                bad++;
        }
    CHECK(bad == 0, "deconflict: cleared plan is conflict-free");
    /* swap priorities: vehicle 0 is delayed instead */
    v[0].priority = 1;
    v[1].priority = 10;
    CHECK(sp_deconflict(&g_m, v, p, 3, &cfg, &plan) == SP_OK && plan.delay_ms[0] == 11000 &&
              plan.delay_ms[1] == 0,
          "deconflict: priority decides who waits");
    /* equal priorities: earlier index keeps its slot */
    v[0].priority = 3;
    v[1].priority = 3;
    CHECK(sp_deconflict(&g_m, v, p, 2, &cfg, &plan) == SP_OK && plan.delay_ms[0] == 0 &&
              plan.delay_ms[1] == 11000,
          "deconflict: stable order on ties");
    /* step 7000: 7000 conflicts, 14000 is clear */
    cfg.step_ms = 7000;
    CHECK(sp_deconflict(&g_m, v, p, 2, &cfg, &plan) == SP_OK && plan.delay_ms[1] == 14000,
          "deconflict: integer steps");
    cfg.step_ms = 1000;
    cfg.max_steps = 10;
    CHECK(sp_deconflict(&g_m, v, p, 2, &cfg, &plan) == SP_OK &&
              plan.status[1] == SP_DC_UNRESOLVED && plan.n_unresolved == 1,
          "deconflict: bounded steps -> unresolved");
    cfg.max_steps = 0;
    CHECK(sp_deconflict(&g_m, v, p, 2, &cfg, &plan) == SP_OK && plan.status[1] == SP_DC_UNRESOLVED,
          "deconflict: max_steps 0 -> unresolved");
    cfg.max_steps = 100;
    /* delays that would leave the time range stop the search */
    p[0] = PATH2(-50000, 0, 50000, SP_TIME_MAX - 10000, 50000, 0, 50000, SP_TIME_MAX);
    p[1] = PATH2(50000, 0, 50000, SP_TIME_MAX - 10000, -50000, 0, 50000, SP_TIME_MAX);
    CHECK(sp_deconflict(&g_m, v, p, 2, &cfg, &plan) == SP_OK && plan.status[1] == SP_DC_UNRESOLVED,
          "deconflict: time range end -> unresolved");
    /* seasonal: a delay would run the second vehicle into the nesting season */
    p[0] = PATH2(-50000, 0, 50000, 0, 50000, 0, 50000, 10000);
    p[1] = PATH2(50000, 0, 50000, 0, -50000, 0, 50000, 10000);
    g_m.n_zones = 1;
    g_m.zones[0].kind = SP_ZONE_PROTECTED_HABITAT;
    rect(&g_m.zones[0].area, -1000, 20000, 1000, 30000); /* 20 m off the corridor */
    g_m.zones[0].always_active = 0;
    g_m.zones[0].active_from_ms = g_m.epoch_ms + 20000;
    g_m.zones[0].active_until_ms = g_m.epoch_ms + 1000000000;
    g_m.zones[0].noise_buffer_mm = 30000;
    g_m.zones[0].noise_ceiling_db = 40; /* both vehicles are 60 dB */
    CHECK(sp_deconflict(&g_m, v, p, 2, &cfg, &plan) == SP_OK && plan.status[0] == SP_DC_CLEARED &&
              plan.status[1] == SP_DC_UNRESOLVED,
          "deconflict: delay into nesting season refused -> unresolved");
    g_m.zones[0].active_from_ms = g_m.epoch_ms + 25000;
    CHECK(sp_deconflict(&g_m, v, p, 2, &cfg, &plan) == SP_OK && plan.status[1] == SP_DC_CLEARED &&
              plan.delay_ms[1] == 11000,
          "deconflict: season starts later -> cleared");
    g_m.zones[0].active_from_ms = g_m.epoch_ms;
    CHECK(sp_deconflict(&g_m, v, p, 2, &cfg, &plan) == SP_OK && plan.status[0] == SP_DC_REFUSED &&
              plan.refuse[0] == SP_REFUSE_NOISE && plan.status[1] == SP_DC_REFUSED &&
              plan.n_refused == 2,
          "deconflict: in season both refused (noise)");
    g_m.n_zones = 0;
    /* a refused vehicle is not scheduled, so others need not avoid it */
    g_m.band.z_max = 40000;
    p[1] = PATH2(50000, 0, 50000, 0, -50000, 0, 30000, 10000); /* starts above band */
    p[0] = PATH2(-50000, 0, 30000, 0, 50000, 0, 30000, 10000);
    CHECK(sp_deconflict(&g_m, v, p, 2, &cfg, &plan) == SP_OK && plan.status[1] == SP_DC_REFUSED &&
              plan.refuse[1] == SP_REFUSE_ALTITUDE && plan.status[0] == SP_DC_CLEARED &&
              plan.delay_ms[0] == 0,
          "deconflict: refused vehicle excluded");
    g_m.band.z_max = 200000;
    /* many vehicles on one corridor: all cleared and pairwise conflict-free */
    for (i = 0; i < SP_MAX_VEHICLES; i++) {
        v[i] = VEH(i, 0, 0, mask);
        v[i].priority = (int32_t) (i % 3);
        p[i] = PATH2(-50000, (int64_t) (i % 2) * 5000, 50000, 0, 50000, (int64_t) (i % 2) * 5000,
                     50000, 10000);
    }
    cfg.max_steps = 1000;
    bad = 0;
    CHECK(sp_deconflict(&g_m, v, p, SP_MAX_VEHICLES, &cfg, &plan) == SP_OK &&
              plan.n_cleared == SP_MAX_VEHICLES && plan.n_conflicts_initial == 120 &&
              plan.n_conflicts_stored == SP_MAX_CONFLICTS,
          "deconflict: 16 vehicles, 120 initial conflicts, stored capped");
    for (i = 0; i < SP_MAX_VEHICLES; i++)
        for (j = i + 1; j < SP_MAX_VEHICLES; j++) {
            int loss;
            if (sp_paths_conflict(&p[i], plan.delay_ms[i], &p[j], plan.delay_ms[j], cfg.sep_mm,
                                  &loss, (sp_conflict *) 0) != SP_OK ||
                loss)
                bad++;
        }
    CHECK(bad == 0, "deconflict: 16-vehicle plan conflict-free");

    /* errors */
    cfg.sep_mm = 0;
    CHECK(sp_deconflict(&g_m, v, p, 2, &cfg, &plan) == SP_ERR_RANGE, "deconflict sep 0");
    cfg.sep_mm = 10000;
    cfg.step_ms = 0;
    CHECK(sp_deconflict(&g_m, v, p, 2, &cfg, &plan) == SP_ERR_RANGE, "deconflict step 0");
    cfg.step_ms = 1000;
    cfg.max_steps = SP_STEPS_MAX + 1;
    CHECK(sp_deconflict(&g_m, v, p, 2, &cfg, &plan) == SP_ERR_RANGE, "deconflict steps big");
    cfg.max_steps = 10;
    CHECK(sp_deconflict(&g_m, v, p, 0, &cfg, &plan) == SP_ERR_CAPACITY, "deconflict n=0");
    CHECK(sp_deconflict(&g_m, v, p, SP_MAX_VEHICLES + 1, &cfg, &plan) == SP_ERR_CAPACITY,
          "deconflict n>max");
    CHECK(sp_deconflict(&g_m, v, p, 2, &cfg, (sp_deconf_plan *) 0) == SP_ERR_NULL,
          "deconflict null");
    p[1].wp[1].t_ms = p[1].wp[0].t_ms;
    CHECK(sp_deconflict(&g_m, v, p, 2, &cfg, &plan) == SP_ERR_ORDER, "deconflict bad path");
    p[1].wp[1].t_ms = 10000;
    v[1].cruise_speed_mm_s = 0;
    CHECK(sp_deconflict(&g_m, v, p, 2, &cfg, &plan) == SP_ERR_RANGE, "deconflict bad vehicle");
    v[1].cruise_speed_mm_s = 10000;
    g_m.purpose = (sp_purpose_t) 0;
    CHECK(sp_deconflict(&g_m, v, p, 2, &cfg, &plan) == SP_ERR_PURPOSE, "deconflict bad mission");
}

int main(void)
{
    test_wide();
    test_big();
    test_geometry();
    test_polygon();
    test_distance();
    test_alt_geo_pose();
    test_fusion();
    test_hungarian();
    test_allocation();
    test_path_check();
    test_conflict();
    test_deconflict();
    if (g_fail) {
        printf("%d checks passed, %d FAILED\n", g_pass, g_fail);
        return 1;
    }
    printf("%d checks passed\n", g_pass);
    return 0;
}
