/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sp_geom.c — exact integer geometry (roadmap S.1). See sp_geom.h.
 *
 * Freestanding: integer-only, no allocation, no libc and no libgcc
 * helpers (64-bit division and 64x64 multiplication are written out here
 * so the object links on i386 as well).
 *
 * HONEST LIMITS: see sp_geom.h. Exact arithmetic, approximate world: the
 * local flat frame and the caller's projection scales are approximations.
 */
#include "sp_geom.h"

/* ================= 128-bit helper ================= */

void sp_u128_mul64(uint64_t a, uint64_t b, sp_u128 *r)
{
    uint64_t a0 = a & 0xFFFFFFFFu, a1 = a >> 32;
    uint64_t b0 = b & 0xFFFFFFFFu, b1 = b >> 32;
    uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    uint64_t mid = (p00 >> 32) + (p01 & 0xFFFFFFFFu) + (p10 & 0xFFFFFFFFu);
    r->lo = (mid << 32) | (p00 & 0xFFFFFFFFu);
    r->hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

int sp_u128_add(const sp_u128 *a, const sp_u128 *b, sp_u128 *r)
{
    uint64_t lo = a->lo + b->lo;
    uint64_t carry = lo < a->lo ? 1u : 0u;
    uint64_t hi;
    if (__builtin_add_overflow(a->hi, b->hi, &hi)) return SP_ERR_OVERFLOW;
    if (__builtin_add_overflow(hi, carry, &hi)) return SP_ERR_OVERFLOW;
    r->hi = hi;
    r->lo = lo;
    return SP_OK;
}

int sp_u128_cmp(const sp_u128 *a, const sp_u128 *b)
{
    if (a->hi != b->hi) return a->hi < b->hi ? -1 : 1;
    if (a->lo != b->lo) return a->lo < b->lo ? -1 : 1;
    return 0;
}

/* ================= checked int64 ================= */

int sp_add_i64(int64_t a, int64_t b, int64_t *r)
{
    if (!r) return SP_ERR_NULL;
    if (__builtin_add_overflow(a, b, r)) return SP_ERR_OVERFLOW;
    return SP_OK;
}

int sp_sub_i64(int64_t a, int64_t b, int64_t *r)
{
    if (!r) return SP_ERR_NULL;
    if (__builtin_sub_overflow(a, b, r)) return SP_ERR_OVERFLOW;
    return SP_OK;
}

static uint64_t sp_mag(int64_t a)
{
    return a < 0 ? (uint64_t) 0 - (uint64_t) a : (uint64_t) a;
}

int sp_mul_i64(int64_t a, int64_t b, int64_t *r)
{
    sp_u128 p;
    int neg = (a < 0) != (b < 0);
    if (!r) return SP_ERR_NULL;
    sp_u128_mul64(sp_mag(a), sp_mag(b), &p);
    if (p.hi != 0) return SP_ERR_OVERFLOW;
    if (neg) {
        if (p.lo > ((uint64_t) 1 << 63)) return SP_ERR_OVERFLOW;
        if (p.lo == 0)
            *r = 0;
        else
            *r = -(int64_t) (p.lo - 1u) - 1;
    } else {
        if (p.lo > (uint64_t) INT64_MAX) return SP_ERR_OVERFLOW;
        *r = (int64_t) p.lo;
    }
    return SP_OK;
}

int sp_udiv64(uint64_t n, uint64_t d, uint64_t *q, uint64_t *rem)
{
    uint64_t qq = 0, r = 0;
    int i;
    if (!q || !rem) return SP_ERR_NULL;
    if (d == 0) return SP_ERR_PARAM;
    for (i = 63; i >= 0; i--) {
        uint64_t top = r >> 63;
        r = (r << 1) | ((n >> i) & 1u);
        if (top || r >= d) {
            r -= d;
            qq |= (uint64_t) 1 << i;
        }
    }
    *q = qq;
    *rem = r;
    return SP_OK;
}

int sp_ceil_div_i64(int64_t n, int64_t d, int64_t *q)
{
    uint64_t qq, rr;
    int st;
    if (!q) return SP_ERR_NULL;
    if (n < 0 || d <= 0) return SP_ERR_RANGE;
    st = sp_udiv64((uint64_t) n, (uint64_t) d, &qq, &rr);
    if (st) return st;
    if (rr) qq++;
    *q = (int64_t) qq;
    return SP_OK;
}

int sp_floor_div_i64(int64_t n, int64_t d, int64_t *q)
{
    uint64_t qq, rr;
    int st;
    if (!q) return SP_ERR_NULL;
    if (d <= 0) return SP_ERR_RANGE;
    st = sp_udiv64(sp_mag(n), (uint64_t) d, &qq, &rr);
    if (st) return st;
    if (n >= 0) {
        *q = (int64_t) qq; /* qq <= INT64_MAX since n >= 0 and d >= 1 */
    } else {
        if (rr) qq++;
        /* qq <= 2^63 here; 2^63 only when n = INT64_MIN, d = 1 */
        *q = qq == ((uint64_t) 1 << 63) ? INT64_MIN : -(int64_t) qq;
    }
    return SP_OK;
}

uint64_t sp_isqrt_floor(uint64_t v)
{
    uint64_t res = 0, bit = (uint64_t) 1 << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= res + bit) {
            v -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}

uint64_t sp_isqrt_ceil(uint64_t v)
{
    uint64_t r = sp_isqrt_floor(v);
    return r * r < v ? r + 1u : r;
}

/* ================= 512-bit signed integer ================= */

static void sp_mag_zero(uint32_t *l)
{
    int i;
    for (i = 0; i < SP_BIG_LIMBS; i++) l[i] = 0;
}

static int sp_mag_is_zero(const uint32_t *l)
{
    int i;
    for (i = 0; i < SP_BIG_LIMBS; i++)
        if (l[i]) return 0;
    return 1;
}

static int sp_mag_cmp(const uint32_t *a, const uint32_t *b)
{
    int i;
    for (i = SP_BIG_LIMBS - 1; i >= 0; i--)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

/* r = a + b; returns overflow flag */
static int sp_mag_add(uint32_t *r, const uint32_t *a, const uint32_t *b)
{
    uint64_t c = 0;
    int i;
    for (i = 0; i < SP_BIG_LIMBS; i++) {
        c += (uint64_t) a[i] + b[i];
        r[i] = (uint32_t) c;
        c >>= 32;
    }
    return c != 0;
}

/* r = a - b, requires a >= b */
static void sp_mag_sub(uint32_t *r, const uint32_t *a, const uint32_t *b)
{
    uint64_t borrow = 0;
    int i;
    for (i = 0; i < SP_BIG_LIMBS; i++) {
        uint64_t s = (uint64_t) b[i] + borrow;
        if ((uint64_t) a[i] >= s) {
            r[i] = (uint32_t) ((uint64_t) a[i] - s);
            borrow = 0;
        } else {
            r[i] = (uint32_t) (((uint64_t) 1 << 32) + a[i] - s);
            borrow = 1;
        }
    }
}

void sp_big_from_i64(sp_big *r, int64_t v)
{
    uint64_t m = sp_mag(v);
    sp_mag_zero(r->l);
    r->l[0] = (uint32_t) m;
    r->l[1] = (uint32_t) (m >> 32);
    r->neg = v < 0;
}

void sp_big_copy(sp_big *r, const sp_big *a)
{
    int i;
    for (i = 0; i < SP_BIG_LIMBS; i++) r->l[i] = a->l[i];
    r->neg = a->neg;
}

int sp_big_sign(const sp_big *a)
{
    if (sp_mag_is_zero(a->l)) return 0;
    return a->neg ? -1 : 1;
}

int sp_big_cmp(const sp_big *a, const sp_big *b)
{
    int sa = sp_big_sign(a), sb = sp_big_sign(b), c;
    if (sa != sb) return sa < sb ? -1 : 1;
    if (sa == 0) return 0;
    c = sp_mag_cmp(a->l, b->l);
    return sa > 0 ? c : -c;
}

/* r = a + (bneg ? -|b| : |b|) with b's magnitude bl */
static int sp_big_addsigned(sp_big *r, const sp_big *a, const uint32_t *bl, int bneg)
{
    uint32_t t[SP_BIG_LIMBS];
    int neg, i;
    if (a->neg == bneg) {
        if (sp_mag_add(t, a->l, bl)) return SP_ERR_OVERFLOW;
        neg = a->neg;
    } else if (sp_mag_cmp(a->l, bl) >= 0) {
        sp_mag_sub(t, a->l, bl);
        neg = a->neg;
    } else {
        sp_mag_sub(t, bl, a->l);
        neg = bneg;
    }
    for (i = 0; i < SP_BIG_LIMBS; i++) r->l[i] = t[i];
    r->neg = sp_mag_is_zero(t) ? 0 : neg;
    return SP_OK;
}

int sp_big_add(sp_big *r, const sp_big *a, const sp_big *b)
{
    if (!r || !a || !b) return SP_ERR_NULL;
    return sp_big_addsigned(r, a, b->l, b->neg);
}

int sp_big_sub(sp_big *r, const sp_big *a, const sp_big *b)
{
    if (!r || !a || !b) return SP_ERR_NULL;
    return sp_big_addsigned(r, a, b->l, !b->neg);
}

int sp_big_mul(sp_big *r, const sp_big *a, const sp_big *b)
{
    uint32_t t[2 * SP_BIG_LIMBS];
    int i, j, neg;
    if (!r || !a || !b) return SP_ERR_NULL;
    for (i = 0; i < 2 * SP_BIG_LIMBS; i++) t[i] = 0;
    for (i = 0; i < SP_BIG_LIMBS; i++) {
        uint64_t c = 0;
        if (a->l[i] == 0) continue;
        for (j = 0; j < SP_BIG_LIMBS; j++) {
            c += (uint64_t) a->l[i] * b->l[j] + t[i + j];
            t[i + j] = (uint32_t) c;
            c >>= 32;
        }
        t[i + SP_BIG_LIMBS] = (uint32_t) c;
    }
    for (i = SP_BIG_LIMBS; i < 2 * SP_BIG_LIMBS; i++)
        if (t[i]) return SP_ERR_OVERFLOW;
    neg = a->neg != b->neg;
    for (i = 0; i < SP_BIG_LIMBS; i++) r->l[i] = t[i];
    r->neg = sp_mag_is_zero(r->l) ? 0 : neg;
    return SP_OK;
}

/* ================= vectors and predicates ================= */

static int sp_in_range(int64_t v)
{
    return v >= -SP_COORD_MAX && v <= SP_COORD_MAX;
}

int sp_vec2_check(const sp_vec2 *v)
{
    if (!v) return SP_ERR_NULL;
    return sp_in_range(v->x) && sp_in_range(v->y) ? SP_OK : SP_ERR_RANGE;
}

int sp_vec3_check(const sp_vec3 *v)
{
    if (!v) return SP_ERR_NULL;
    return sp_in_range(v->x) && sp_in_range(v->y) && sp_in_range(v->z) ? SP_OK : SP_ERR_RANGE;
}

static int sp_sgn(int64_t v)
{
    return v > 0 ? 1 : (v < 0 ? -1 : 0);
}

/* cross of (b-a) and (c-a), inputs already range-checked */
static int sp_cross2(const sp_vec2 *a, const sp_vec2 *b, const sp_vec2 *c, int64_t *out)
{
    int64_t p, q;
    int st = sp_mul_i64(b->x - a->x, c->y - a->y, &p);
    if (st) return st;
    st = sp_mul_i64(b->y - a->y, c->x - a->x, &q);
    if (st) return st;
    return sp_sub_i64(p, q, out);
}

int sp_orient2d(const sp_vec2 *a, const sp_vec2 *b, const sp_vec2 *c, int *out)
{
    int64_t cr;
    int st;
    if (!a || !b || !c || !out) return SP_ERR_NULL;
    if (sp_vec2_check(a) || sp_vec2_check(b) || sp_vec2_check(c)) return SP_ERR_RANGE;
    st = sp_cross2(a, b, c, &cr);
    if (st) return st;
    *out = sp_sgn(cr);
    return SP_OK;
}

static int64_t sp_min64(int64_t a, int64_t b)
{
    return a < b ? a : b;
}
static int64_t sp_max64(int64_t a, int64_t b)
{
    return a > b ? a : b;
}

/* p collinear with segment ab: is it within the closed bounding box? */
static int sp_on_box(const sp_vec2 *a, const sp_vec2 *b, const sp_vec2 *p)
{
    return p->x >= sp_min64(a->x, b->x) && p->x <= sp_max64(a->x, b->x) &&
           p->y >= sp_min64(a->y, b->y) && p->y <= sp_max64(a->y, b->y);
}

int sp_seg_intersect(const sp_vec2 *a, const sp_vec2 *b, const sp_vec2 *c, const sp_vec2 *d,
                     sp_isect_t *out)
{
    int d1, d2, d3, d4, st;
    if (!a || !b || !c || !d || !out) return SP_ERR_NULL;
    if ((st = sp_orient2d(c, d, a, &d1)) != SP_OK) return st;
    if ((st = sp_orient2d(c, d, b, &d2)) != SP_OK) return st;
    if ((st = sp_orient2d(a, b, c, &d3)) != SP_OK) return st;
    if ((st = sp_orient2d(a, b, d, &d4)) != SP_OK) return st;
    if (d1 * d2 < 0 && d3 * d4 < 0) {
        *out = SP_ISECT_PROPER;
        return SP_OK;
    }
    if ((d1 == 0 && sp_on_box(c, d, a)) || (d2 == 0 && sp_on_box(c, d, b)) ||
        (d3 == 0 && sp_on_box(a, b, c)) || (d4 == 0 && sp_on_box(a, b, d))) {
        *out = SP_ISECT_TOUCH;
        return SP_OK;
    }
    *out = SP_ISECT_NONE;
    return SP_OK;
}

static uint64_t sp_sq(int64_t v)
{
    uint64_t m = sp_mag(v);
    return m * m; /* callers guarantee |v| <= 2^31 */
}

int sp_dist_cmp_pt_seg3(const sp_vec3 *p, const sp_vec3 *a, const sp_vec3 *b, int64_t m, int *out)
{
    int64_t abx, aby, abz, apx, apy, apz, t, den, cx, cy, cz, u, w;
    uint64_t m2;
    int st;
    if (!p || !a || !b || !out) return SP_ERR_NULL;
    if (sp_vec3_check(p) || sp_vec3_check(a) || sp_vec3_check(b)) return SP_ERR_RANGE;
    if (m < 0 || m > 2 * SP_COORD_MAX) return SP_ERR_RANGE;
    m2 = (uint64_t) m * (uint64_t) m; /* <= 2^60 */
    abx = b->x - a->x;
    aby = b->y - a->y;
    abz = b->z - a->z;
    apx = p->x - a->x;
    apy = p->y - a->y;
    apz = p->z - a->z;
    /* every |difference| <= 2^30, so each product <= 2^60 and sums of
     * three <= 3 * 2^60 < 2^62: plain int64 cannot overflow here */
    t = apx * abx + apy * aby + apz * abz;
    den = abx * abx + aby * aby + abz * abz;
    if (den == 0 || t <= 0 || t >= den) {
        uint64_t d2;
        if (den != 0 && t >= den)
            d2 = sp_sq(p->x - b->x) + sp_sq(p->y - b->y) + sp_sq(p->z - b->z);
        else
            d2 = sp_sq(apx) + sp_sq(apy) + sp_sq(apz);
        *out = d2 < m2 ? -1 : (d2 > m2 ? 1 : 0);
        return SP_OK;
    }
    /* interior foot: dist^2 = |ap x ab|^2 / den, compare |ap x ab|^2 with
     * m^2 * den in 128 bits. Each cross component <= 2^61. */
    if ((st = sp_mul_i64(apy, abz, &u)) || (st = sp_mul_i64(apz, aby, &w)) ||
        (st = sp_sub_i64(u, w, &cx)))
        return st;
    if ((st = sp_mul_i64(apz, abx, &u)) || (st = sp_mul_i64(apx, abz, &w)) ||
        (st = sp_sub_i64(u, w, &cy)))
        return st;
    if ((st = sp_mul_i64(apx, aby, &u)) || (st = sp_mul_i64(apy, abx, &w)) ||
        (st = sp_sub_i64(u, w, &cz)))
        return st;
    {
        sp_u128 s, q, rhs;
        sp_u128_mul64(sp_mag(cx), sp_mag(cx), &s);
        sp_u128_mul64(sp_mag(cy), sp_mag(cy), &q);
        if ((st = sp_u128_add(&s, &q, &s)) != SP_OK) return st;
        sp_u128_mul64(sp_mag(cz), sp_mag(cz), &q);
        if ((st = sp_u128_add(&s, &q, &s)) != SP_OK) return st;
        sp_u128_mul64(m2, (uint64_t) den, &rhs);
        *out = sp_u128_cmp(&s, &rhs);
    }
    return SP_OK;
}

int sp_dist_cmp_pt_seg2(const sp_vec2 *p, const sp_vec2 *a, const sp_vec2 *b, int64_t m, int *out)
{
    sp_vec3 p3, a3, b3;
    if (!p || !a || !b || !out) return SP_ERR_NULL;
    p3.x = p->x;
    p3.y = p->y;
    p3.z = 0;
    a3.x = a->x;
    a3.y = a->y;
    a3.z = 0;
    b3.x = b->x;
    b3.y = b->y;
    b3.z = 0;
    return sp_dist_cmp_pt_seg3(&p3, &a3, &b3, m, out);
}

int sp_dist_cmp_seg_seg2(const sp_vec2 *a, const sp_vec2 *b, const sp_vec2 *c, const sp_vec2 *d,
                         int64_t m, int *out)
{
    sp_isect_t is;
    int st, r, best;
    if (!a || !b || !c || !d || !out) return SP_ERR_NULL;
    if (m < 0 || m > 2 * SP_COORD_MAX) return SP_ERR_RANGE;
    if ((st = sp_seg_intersect(a, b, c, d, &is)) != SP_OK) return st;
    if (is != SP_ISECT_NONE) {
        *out = m == 0 ? 0 : -1;
        return SP_OK;
    }
    /* disjoint segments: the minimum distance is reached at an endpoint */
    if ((st = sp_dist_cmp_pt_seg2(a, c, d, m, &best)) != SP_OK) return st;
    if ((st = sp_dist_cmp_pt_seg2(b, c, d, m, &r)) != SP_OK) return st;
    if (r < best) best = r;
    if ((st = sp_dist_cmp_pt_seg2(c, a, b, m, &r)) != SP_OK) return st;
    if (r < best) best = r;
    if ((st = sp_dist_cmp_pt_seg2(d, a, b, m, &r)) != SP_OK) return st;
    if (r < best) best = r;
    *out = best;
    return SP_OK;
}

int sp_len3_ceil(const sp_vec3 *a, const sp_vec3 *b, int64_t *out)
{
    uint64_t s;
    if (!a || !b || !out) return SP_ERR_NULL;
    if (sp_vec3_check(a) || sp_vec3_check(b)) return SP_ERR_RANGE;
    s = sp_sq(b->x - a->x) + sp_sq(b->y - a->y) + sp_sq(b->z - a->z); /* < 2^62 */
    *out = (int64_t) sp_isqrt_ceil(s);
    return SP_OK;
}

/* ================= polygons ================= */

int sp_poly_validate(const sp_poly *pg)
{
    uint32_t i, j, n;
    sp_big area, term, x, y;
    int st;
    if (!pg) return SP_ERR_NULL;
    n = pg->n;
    if (n < 3 || n > SP_POLY_MAX) return SP_ERR_DEGENERATE;
    for (i = 0; i < n; i++)
        if (sp_vec2_check(&pg->v[i])) return SP_ERR_RANGE;
    for (i = 0; i < n; i++) {
        const sp_vec2 *a = &pg->v[i], *b = &pg->v[(i + 1) % n], *c = &pg->v[(i + 2) % n];
        int o;
        if (a->x == b->x && a->y == b->y) return SP_ERR_DEGENERATE;
        /* adjacent edges ab, bc must not fold back over each other */
        if ((st = sp_orient2d(a, b, c, &o)) != SP_OK) return st;
        if (o == 0 && (b->x - a->x) * (c->x - b->x) + (b->y - a->y) * (c->y - b->y) < 0)
            return SP_ERR_DEGENERATE;
    }
    for (i = 0; i < n; i++) {
        for (j = i + 1; j < n; j++) {
            sp_isect_t is;
            if (j == i + 1 || (i == 0 && j == n - 1)) continue; /* adjacent */
            if ((st = sp_seg_intersect(&pg->v[i], &pg->v[(i + 1) % n], &pg->v[j],
                                       &pg->v[(j + 1) % n], &is)) != SP_OK)
                return st;
            if (is != SP_ISECT_NONE) return SP_ERR_DEGENERATE;
        }
    }
    /* twice the signed area, relative to v[0], in wide arithmetic */
    sp_big_from_i64(&area, 0);
    for (i = 1; i + 1 < n; i++) {
        sp_big_from_i64(&x, pg->v[i].x - pg->v[0].x);
        sp_big_from_i64(&y, pg->v[i + 1].y - pg->v[0].y);
        if ((st = sp_big_mul(&term, &x, &y)) != SP_OK) return st;
        if ((st = sp_big_add(&area, &area, &term)) != SP_OK) return st;
        sp_big_from_i64(&x, pg->v[i + 1].x - pg->v[0].x);
        sp_big_from_i64(&y, pg->v[i].y - pg->v[0].y);
        if ((st = sp_big_mul(&term, &x, &y)) != SP_OK) return st;
        if ((st = sp_big_sub(&area, &area, &term)) != SP_OK) return st;
    }
    if (sp_big_sign(&area) == 0) return SP_ERR_DEGENERATE;
    return SP_OK;
}

int sp_point_in_poly(const sp_poly *pg, const sp_vec2 *p, sp_loc_t *out)
{
    uint32_t i, n;
    int inside = 0, st;
    if (!pg || !p || !out) return SP_ERR_NULL;
    n = pg->n;
    if (n < 3 || n > SP_POLY_MAX) return SP_ERR_DEGENERATE;
    if (sp_vec2_check(p)) return SP_ERR_RANGE;
    for (i = 0; i < n; i++) {
        const sp_vec2 *a = &pg->v[i], *b = &pg->v[(i + 1) % n];
        int o;
        if ((st = sp_orient2d(a, b, p, &o)) != SP_OK) return st;
        if (o == 0 && sp_on_box(a, b, p)) {
            *out = SP_LOC_ON;
            return SP_OK;
        }
        /* half-open crossing rule on the upward ray test, exact by sign */
        if (a->y <= p->y && b->y > p->y && o > 0) inside = !inside;
        if (b->y <= p->y && a->y > p->y && o < 0) inside = !inside;
    }
    *out = inside ? SP_LOC_IN : SP_LOC_OUT;
    return SP_OK;
}

/* ================= altitude bands ================= */

int sp_alt_band_validate(const sp_alt_band *b)
{
    if (!b) return SP_ERR_NULL;
    if (!sp_in_range(b->z_min) || !sp_in_range(b->z_max)) return SP_ERR_RANGE;
    if (b->z_min > b->z_max) return SP_ERR_PARAM;
    return SP_OK;
}

int sp_alt_band_contains(const sp_alt_band *b, int64_t z, int *out)
{
    int st;
    if (!b || !out) return SP_ERR_NULL;
    if ((st = sp_alt_band_validate(b)) != SP_OK) return st;
    if (!sp_in_range(z)) return SP_ERR_RANGE;
    *out = z >= b->z_min && z <= b->z_max;
    return SP_OK;
}

/* ================= geodetic ================= */

#define SP_UDEG_90  ((int64_t) 90000000)
#define SP_UDEG_180 ((int64_t) 180000000)
#define SP_UDEG_360 ((int64_t) 360000000)

int sp_geo_validate(const sp_geo *g)
{
    if (!g) return SP_ERR_NULL;
    if (g->lat_udeg < -SP_UDEG_90 || g->lat_udeg > SP_UDEG_90) return SP_ERR_RANGE;
    if (g->lon_udeg < -SP_UDEG_180 || g->lon_udeg > SP_UDEG_180) return SP_ERR_RANGE;
    if (!sp_in_range(g->alt_mm)) return SP_ERR_RANGE;
    return SP_OK;
}

int sp_geo_to_local(const sp_frame *f, const sp_geo *g, sp_vec3 *out)
{
    int64_t dlat, dlon, t, x, y, z;
    int st;
    if (!f || !g || !out) return SP_ERR_NULL;
    if ((st = sp_geo_validate(&f->origin)) != SP_OK) return st;
    if ((st = sp_geo_validate(g)) != SP_OK) return st;
    if (f->lat_mm_num <= 0 || f->lat_mm_den <= 0 || f->lon_mm_num < 0 || f->lon_mm_den <= 0)
        return SP_ERR_PARAM;
    dlat = g->lat_udeg - f->origin.lat_udeg;
    dlon = g->lon_udeg - f->origin.lon_udeg;
    if (dlon > SP_UDEG_180) dlon -= SP_UDEG_360;
    if (dlon <= -SP_UDEG_180) dlon += SP_UDEG_360;
    if ((st = sp_mul_i64(dlon, f->lon_mm_num, &t)) != SP_OK) return st;
    if ((st = sp_floor_div_i64(t, f->lon_mm_den, &x)) != SP_OK) return st;
    if ((st = sp_mul_i64(dlat, f->lat_mm_num, &t)) != SP_OK) return st;
    if ((st = sp_floor_div_i64(t, f->lat_mm_den, &y)) != SP_OK) return st;
    z = g->alt_mm - f->origin.alt_mm;
    if (!sp_in_range(x) || !sp_in_range(y) || !sp_in_range(z)) return SP_ERR_RANGE;
    out->x = x;
    out->y = y;
    out->z = z;
    return SP_OK;
}

/* ================= Pythagorean headings ================= */

int sp_heading_validate(const sp_heading *h)
{
    if (!h) return SP_ERR_NULL;
    if (h->c <= 0 || h->c > SP_HEADING_MAX || h->a < -SP_HEADING_MAX || h->a > SP_HEADING_MAX ||
        h->b < -SP_HEADING_MAX || h->b > SP_HEADING_MAX)
        return SP_ERR_RANGE;
    if (h->a * h->a + h->b * h->b != h->c * h->c) return SP_ERR_PARAM;
    return SP_OK;
}

int sp_pose2_apply(const sp_heading *h, const sp_vec2 *t, const sp_vec2 *p, sp_hvec2 *out)
{
    int64_t u, w, s;
    int st;
    if (!h || !t || !p || !out) return SP_ERR_NULL;
    if ((st = sp_heading_validate(h)) != SP_OK) return st;
    if (sp_vec2_check(t) || sp_vec2_check(p)) return SP_ERR_RANGE;
    /* |products| <= 2^20 * 2^29 = 2^49: still checked for uniformity */
    if ((st = sp_mul_i64(h->a, p->x, &u)) || (st = sp_mul_i64(h->b, p->y, &w)) ||
        (st = sp_sub_i64(u, w, &s)) || (st = sp_mul_i64(h->c, t->x, &u)) ||
        (st = sp_add_i64(s, u, &out->X)))
        return st;
    if ((st = sp_mul_i64(h->b, p->x, &u)) || (st = sp_mul_i64(h->a, p->y, &w)) ||
        (st = sp_add_i64(u, w, &s)) || (st = sp_mul_i64(h->c, t->y, &u)) ||
        (st = sp_add_i64(s, u, &out->Y)))
        return st;
    out->W = h->c;
    return SP_OK;
}
