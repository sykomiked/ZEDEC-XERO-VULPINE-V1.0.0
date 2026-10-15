/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* xrisk.c — exact loss distributions by convolution over loss units.
 *
 * WHAT IT DOES
 *   See xrisk.h. One shared denominator den = B * prod m_i is built first
 *   (exact, overflow refused); then a dynamic program convolves the exposures
 *   one at a time over loss units, keeping every probability mass as a big
 *   integer numerator over the running product of denominators. Scenario
 *   distributions are mixed with integer weights over B.
 *
 * HONEST LIMITS
 *   Fixed-width big integers (XRISK_BITS); anything larger is refused. The DP
 *   is O(scenarios * exposures * units * limbs). Discrete models only.
 *   No floating point, no allocation, no libc.
 */
#include "xrisk.h"

#define XR_W XRISK_LIMBS

/* ------------------------------------------------------------------ */
/* small integer helpers                                               */
/* ------------------------------------------------------------------ */

/* 64-bit unsigned divide; d != 0. Native on 64-bit targets, shift-subtract
 * elsewhere (so i386 needs no libgcc __udivdi3). -DXRISK_SOFT_DIV forces the
 * shift-subtract path on 64-bit hosts so the tests exercise it. */
static uint64_t xr_udiv64(uint64_t n, uint64_t d, uint64_t *rem)
{
#if UINTPTR_MAX > 0xFFFFFFFFu && !defined(XRISK_SOFT_DIV)
    if (rem) *rem = n % d;
    return n / d;
#else
    uint64_t q = 0, r = 0;
    int i;
    for (i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) {
            r -= d;
            q |= (uint64_t) 1 << i;
        }
    }
    if (rem) *rem = r;
    return q;
#endif
}

static uint64_t xr_gcd64(uint64_t a, uint64_t b)
{
    while (b != 0) {
        uint64_t t;
        (void) xr_udiv64(a, b, &t);
        a = b;
        b = t;
    }
    return a;
}

/* lcm of two positive int64 values; 0 on overflow. */
static int64_t xr_lcm64(int64_t a, int64_t b)
{
    uint64_t g = xr_gcd64((uint64_t) a, (uint64_t) b);
    int64_t r;
    if (__builtin_mul_overflow((int64_t) xr_udiv64((uint64_t) a, g, 0), b, &r)) return 0;
    return r;
}

static int xr_q_valid(xrisk_q_t q)
{
    return q.den > 0 && q.num >= 0 && q.num <= q.den;
}

static xrisk_q_t xr_q_reduce(xrisk_q_t q)
{
    xrisk_q_t r;
    if (q.num == 0) {
        r.num = 0;
        r.den = 1;
        return r;
    }
    uint64_t g = xr_gcd64((uint64_t) q.num, (uint64_t) q.den);
    r.num = (int64_t) xr_udiv64((uint64_t) q.num, g, 0);
    r.den = (int64_t) xr_udiv64((uint64_t) q.den, g, 0);
    return r;
}

/* ------------------------------------------------------------------ */
/* limb kernels (length-bounded)                                       */
/* ------------------------------------------------------------------ */

static void xr_add128(uint64_t *lo, uint64_t *hi, uint64_t x)
{
    *lo += x;
    if (*lo < x) (*hi)++;
}

/* dst = a*q + b*p over n limbs. a may alias dst; b must not alias dst.
 * a or b may be NULL (treated as zero). Returns 0, or 1 if the result does
 * not fit in n limbs. */
static int xr_lin2(uint32_t *dst, const uint32_t *a, uint64_t q, const uint32_t *b, uint64_t p,
                   uint32_t n)
{
    uint32_t j;
    if (q <= 0xFFFFFFFFu && p <= 0xFFFFFFFFu && q + p <= 0xFFFFFFFFu) {
        uint64_t carry = 0;
        for (j = 0; j < n; j++) {
            uint64_t t = carry;
            if (a) t += (uint64_t) a[j] * q;
            if (b) t += (uint64_t) b[j] * p;
            dst[j] = (uint32_t) t;
            carry = t >> 32;
        }
        return carry != 0;
    }
    {
        uint64_t qlo = q & 0xFFFFFFFFu, qhi = q >> 32;
        uint64_t plo = p & 0xFFFFFFFFu, phi = p >> 32;
        uint64_t lo = 0, hi = 0;
        uint32_t aprev = 0, bprev = 0;
        for (j = 0; j < n; j++) {
            uint32_t aj = a ? a[j] : 0;
            uint32_t bj = b ? b[j] : 0;
            xr_add128(&lo, &hi, (uint64_t) aj * qlo);
            xr_add128(&lo, &hi, (uint64_t) aprev * qhi);
            xr_add128(&lo, &hi, (uint64_t) bj * plo);
            xr_add128(&lo, &hi, (uint64_t) bprev * phi);
            dst[j] = (uint32_t) lo;
            lo = (lo >> 32) | (hi << 32);
            hi >>= 32;
            aprev = aj;
            bprev = bj;
        }
        xr_add128(&lo, &hi, (uint64_t) aprev * qhi);
        xr_add128(&lo, &hi, (uint64_t) bprev * phi);
        return (lo | hi) != 0;
    }
}

/* ------------------------------------------------------------------ */
/* big integer (full width)                                            */
/* ------------------------------------------------------------------ */

void xrisk_big_zero(xrisk_big_t *a)
{
    uint32_t i;
    if (!a) return;
    for (i = 0; i < XR_W; i++) a->l[i] = 0;
}

void xrisk_big_from_u64(xrisk_big_t *a, uint64_t v)
{
    if (!a) return;
    xrisk_big_zero(a);
    a->l[0] = (uint32_t) v;
    a->l[1] = (uint32_t) (v >> 32);
}

void xrisk_big_copy(xrisk_big_t *dst, const xrisk_big_t *src)
{
    uint32_t i;
    if (!dst || !src) return;
    for (i = 0; i < XR_W; i++) dst->l[i] = src->l[i];
}

int xrisk_big_is_zero(const xrisk_big_t *a)
{
    uint32_t i;
    if (!a) return 1;
    for (i = 0; i < XR_W; i++)
        if (a->l[i]) return 0;
    return 1;
}

int xrisk_big_cmp(const xrisk_big_t *a, const xrisk_big_t *b)
{
    uint32_t i = XR_W;
    if (!a || !b) return 0;
    while (i-- > 0) {
        if (a->l[i] != b->l[i]) return a->l[i] > b->l[i] ? 1 : -1;
    }
    return 0;
}

int xrisk_big_add(xrisk_big_t *dst, const xrisk_big_t *a, const xrisk_big_t *b)
{
    uint64_t c = 0;
    uint32_t i;
    if (!dst || !a || !b) return XRISK_E_NULL;
    for (i = 0; i < XR_W; i++) {
        uint64_t t = (uint64_t) a->l[i] + b->l[i] + c;
        dst->l[i] = (uint32_t) t;
        c = t >> 32;
    }
    return c ? XRISK_E_OVERFLOW : XRISK_OK;
}

/* wrapping subtract; returns borrow */
static uint32_t xr_sub_raw(uint32_t *d, const uint32_t *a, const uint32_t *b, uint32_t n)
{
    uint32_t i, br = 0;
    for (i = 0; i < n; i++) {
        uint64_t t = (uint64_t) a[i] - b[i] - br;
        d[i] = (uint32_t) t;
        br = (uint32_t) ((t >> 32) & 1u);
    }
    return br;
}

int xrisk_big_sub(xrisk_big_t *dst, const xrisk_big_t *a, const xrisk_big_t *b)
{
    if (!dst || !a || !b) return XRISK_E_NULL;
    if (xrisk_big_cmp(a, b) < 0) return XRISK_E_RANGE;
    (void) xr_sub_raw(dst->l, a->l, b->l, XR_W);
    return XRISK_OK;
}

int xrisk_big_mul_u64(xrisk_big_t *dst, const xrisk_big_t *a, uint64_t m)
{
    if (!dst || !a) return XRISK_E_NULL;
    if (dst != a) xrisk_big_copy(dst, a);
    return xr_lin2(dst->l, dst->l, m, (const uint32_t *) 0, 0, XR_W) ? XRISK_E_OVERFLOW : XRISK_OK;
}

uint32_t xrisk_big_bits(const xrisk_big_t *a)
{
    uint32_t i = XR_W;
    if (!a) return 0;
    while (i-- > 0) {
        if (a->l[i]) {
            uint32_t v = a->l[i], b = 0;
            while (v) {
                b++;
                v >>= 1;
            }
            return i * 32u + b;
        }
    }
    return 0;
}

int xrisk_big_to_u64(const xrisk_big_t *a, uint64_t *out)
{
    if (!a || !out) return XRISK_E_NULL;
    if (xrisk_big_bits(a) > 64) return XRISK_E_OVERFLOW;
    *out = ((uint64_t) a->l[1] << 32) | a->l[0];
    return XRISK_OK;
}

static uint32_t xr_shl1(uint32_t *a, uint32_t n)
{
    uint32_t i, c = 0;
    for (i = 0; i < n; i++) {
        uint32_t nc = a[i] >> 31;
        a[i] = (a[i] << 1) | c;
        c = nc;
    }
    return c;
}

static void xr_shr1(uint32_t *a, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        a[i] >>= 1;
        if (i + 1 < n) a[i] |= a[i + 1] << 31;
    }
}

static int xr_cmp_n(const uint32_t *a, const uint32_t *b, uint32_t n)
{
    uint32_t i = n;
    while (i-- > 0) {
        if (a[i] != b[i]) return a[i] > b[i] ? 1 : -1;
    }
    return 0;
}

int xrisk_big_divmod(xrisk_big_t *q, xrisk_big_t *r, const xrisk_big_t *a, const xrisk_big_t *d)
{
    xrisk_big_t qq, rr;
    uint32_t nb, i;
    if (!a || !d) return XRISK_E_NULL;
    if (xrisk_big_is_zero(d)) return XRISK_E_DIVZERO;
    xrisk_big_zero(&qq);
    xrisk_big_zero(&rr);
    nb = xrisk_big_bits(a);
    i = nb;
    while (i-- > 0) {
        uint32_t c = xr_shl1(rr.l, XR_W);
        rr.l[0] |= (a->l[i / 32] >> (i % 32)) & 1u;
        if (c || xr_cmp_n(rr.l, d->l, XR_W) >= 0) {
            (void) xr_sub_raw(rr.l, rr.l, d->l, XR_W);
            qq.l[i / 32] |= 1u << (i % 32);
        }
    }
    if (q) xrisk_big_copy(q, &qq);
    if (r) xrisk_big_copy(r, &rr);
    return XRISK_OK;
}

/* a = a / v, returns remainder; v != 0, v < 2^32 */
static uint32_t xr_div_small(uint32_t *a, uint32_t n, uint32_t v)
{
    uint64_t rem = 0;
    uint32_t i = n;
    while (i-- > 0) {
        uint64_t cur = (rem << 32) | a[i];
        a[i] = (uint32_t) xr_udiv64(cur, v, &rem);
    }
    return (uint32_t) rem;
}

/* ------------------------------------------------------------------ */
/* rationals                                                           */
/* ------------------------------------------------------------------ */

static void xr_gcd_big(xrisk_big_t *g, const xrisk_big_t *a, const xrisk_big_t *b)
{
    xrisk_big_t u, v;
    uint32_t shift = 0;
    xrisk_big_copy(&u, a);
    xrisk_big_copy(&v, b);
    if (xrisk_big_is_zero(&u)) {
        xrisk_big_copy(g, &v);
        return;
    }
    if (xrisk_big_is_zero(&v)) {
        xrisk_big_copy(g, &u);
        return;
    }
    while (((u.l[0] | v.l[0]) & 1u) == 0) {
        xr_shr1(u.l, XR_W);
        xr_shr1(v.l, XR_W);
        shift++;
    }
    while ((u.l[0] & 1u) == 0) xr_shr1(u.l, XR_W);
    for (;;) {
        while ((v.l[0] & 1u) == 0) xr_shr1(v.l, XR_W);
        if (xrisk_big_cmp(&u, &v) > 0) {
            xrisk_big_t t;
            xrisk_big_copy(&t, &u);
            xrisk_big_copy(&u, &v);
            xrisk_big_copy(&v, &t);
        }
        (void) xr_sub_raw(v.l, v.l, u.l, XR_W);
        if (xrisk_big_is_zero(&v)) break;
    }
    while (shift-- > 0) (void) xr_shl1(u.l, XR_W);
    xrisk_big_copy(g, &u);
}

int xrisk_rat_reduce(xrisk_rat_t *r)
{
    xrisk_big_t g, one;
    if (!r) return XRISK_E_NULL;
    if (xrisk_big_is_zero(&r->den)) return XRISK_E_DIVZERO;
    if (xrisk_big_is_zero(&r->num)) {
        xrisk_big_from_u64(&r->den, 1);
        return XRISK_OK;
    }
    xr_gcd_big(&g, &r->num, &r->den);
    xrisk_big_from_u64(&one, 1);
    if (xrisk_big_cmp(&g, &one) == 0) return XRISK_OK;
    (void) xrisk_big_divmod(&r->num, (xrisk_big_t *) 0, &r->num, &g);
    (void) xrisk_big_divmod(&r->den, (xrisk_big_t *) 0, &r->den, &g);
    return XRISK_OK;
}

static uint32_t xr_used(const xrisk_big_t *a)
{
    return (xrisk_big_bits(a) + 31u) / 32u;
}

/* out (2*XR_W limbs) = a * b */
static void xr_mul_full(uint32_t *out, const xrisk_big_t *a, const xrisk_big_t *b)
{
    uint32_t na = xr_used(a), nb = xr_used(b), i, j;
    for (i = 0; i < 2u * XR_W; i++) out[i] = 0;
    for (i = 0; i < na; i++) {
        uint64_t c = 0;
        for (j = 0; j < nb; j++) {
            uint64_t t = (uint64_t) a->l[i] * b->l[j] + out[i + j] + c;
            out[i + j] = (uint32_t) t;
            c = t >> 32;
        }
        out[i + nb] = (uint32_t) c;
    }
}

int xrisk_rat_cmp(const xrisk_rat_t *a, const xrisk_rat_t *b, int *out)
{
    uint32_t x[2 * XR_W], y[2 * XR_W];
    if (!a || !b || !out) return XRISK_E_NULL;
    if (xrisk_big_is_zero(&a->den) || xrisk_big_is_zero(&b->den)) return XRISK_E_DIVZERO;
    xr_mul_full(x, &a->num, &b->den);
    xr_mul_full(y, &b->num, &a->den);
    *out = xr_cmp_n(x, y, 2 * XR_W);
    return XRISK_OK;
}

int xrisk_rat_to_dec(const xrisk_rat_t *r, uint32_t places, char *buf, size_t cap)
{
    xrisk_big_t ip, rem;
    char tmp[XR_W * 10 + 2];
    size_t pos = 0, nt = 0;
    uint32_t i;
    int rc;
    if (!r || !buf) return XRISK_E_NULL;
    if (cap == 0) return XRISK_E_BUF;
    buf[0] = '\0';
    rc = xrisk_big_divmod(&ip, &rem, &r->num, &r->den);
    if (rc) return rc;
    /* integer part, least significant digit first */
    do {
        tmp[nt++] = (char) ('0' + xr_div_small(ip.l, XR_W, 10));
    } while (!xrisk_big_is_zero(&ip));
    if (nt + 1 > cap) return XRISK_E_BUF;
    while (nt > 0) buf[pos++] = tmp[--nt];
    if (places > 0) {
        if (pos + 1 + places + 1 > cap) {
            buf[0] = '\0';
            return XRISK_E_BUF;
        }
        buf[pos++] = '.';
        for (i = 0; i < places; i++) {
            char dgt = '0';
            /* rem < den <= 2^XRISK_BITS, so rem*10 fits in the headroom */
            if (xrisk_big_mul_u64(&rem, &rem, 10)) {
                buf[0] = '\0';
                return XRISK_E_OVERFLOW;
            }
            while (xrisk_big_cmp(&rem, &r->den) >= 0) {
                (void) xr_sub_raw(rem.l, rem.l, r->den.l, XR_W);
                dgt++;
            }
            buf[pos++] = dgt;
        }
    }
    buf[pos] = '\0';
    return XRISK_OK;
}

/* ------------------------------------------------------------------ */
/* fusion                                                              */
/* ------------------------------------------------------------------ */

/* sign(a - b) for non-negative fractions with positive dens */
static int xr_q_cmp(xrisk_q_t a, xrisk_q_t b)
{
    xrisk_big_t x, y;
    xrisk_big_from_u64(&x, (uint64_t) a.num);
    xrisk_big_from_u64(&y, (uint64_t) b.num);
    (void) xrisk_big_mul_u64(&x, &x, (uint64_t) b.den);
    (void) xrisk_big_mul_u64(&y, &y, (uint64_t) a.den);
    return xrisk_big_cmp(&x, &y);
}

int xrisk_fuse(const xrisk_q_t *src, uint32_t n, xrisk_q_t tol, xrisk_q_t *lo, xrisk_q_t *hi,
               xrisk_state_t *state)
{
    uint32_t i;
    xrisk_q_t mn, mx;
    xrisk_big_t lhs, t, rhs;
    if (!lo || !hi || !state) return XRISK_E_NULL;
    *state = XRISK_NEITHER;
    if (n == 0) return XRISK_E_NEITHER;
    if (!src) return XRISK_E_NULL;
    if (n > XRISK_MAX_SOURCES) return XRISK_E_RANGE;
    if (tol.den <= 0 || tol.num < 0) return XRISK_E_PROB;
    for (i = 0; i < n; i++)
        if (!xr_q_valid(src[i])) return XRISK_E_PROB;
    mn = xr_q_reduce(src[0]);
    mx = mn;
    for (i = 1; i < n; i++) {
        xrisk_q_t c = xr_q_reduce(src[i]);
        if (xr_q_cmp(c, mn) < 0) mn = c;
        if (xr_q_cmp(c, mx) > 0) mx = c;
    }
    /* (mx - mn) <= tol  <=>  (mx.n*mn.d - mn.n*mx.d) * tol.d <= tol.n * mx.d * mn.d */
    xrisk_big_from_u64(&lhs, (uint64_t) mx.num);
    (void) xrisk_big_mul_u64(&lhs, &lhs, (uint64_t) mn.den);
    xrisk_big_from_u64(&t, (uint64_t) mn.num);
    (void) xrisk_big_mul_u64(&t, &t, (uint64_t) mx.den);
    (void) xrisk_big_sub(&lhs, &lhs, &t);
    (void) xrisk_big_mul_u64(&lhs, &lhs, (uint64_t) tol.den);
    xrisk_big_from_u64(&rhs, (uint64_t) tol.num);
    (void) xrisk_big_mul_u64(&rhs, &rhs, (uint64_t) mx.den);
    (void) xrisk_big_mul_u64(&rhs, &rhs, (uint64_t) mn.den);
    *hi = mx;
    if (xrisk_big_cmp(&lhs, &rhs) <= 0) {
        *lo = mx; /* agree: most conservative value on both sides */
        *state = XRISK_TRUE;
    } else {
        *lo = mn;
        *state = XRISK_BOTH;
    }
    return XRISK_OK;
}

/* ------------------------------------------------------------------ */
/* model                                                               */
/* ------------------------------------------------------------------ */

int xrisk_model_init(xrisk_model_t *m, uint32_t n_obl, uint32_t n_scen)
{
    uint32_t s, i;
    if (!m) return XRISK_E_NULL;
    if (n_obl == 0 || n_obl > XRISK_MAX_OBLIGORS) return XRISK_E_RANGE;
    if (n_scen == 0 || n_scen > XRISK_MAX_SCEN) return XRISK_E_RANGE;
    m->n_obl = n_obl;
    m->n_scen = n_scen;
    for (i = 0; i < XRISK_MAX_OBLIGORS; i++) {
        m->loss[i] = 0;
        m->loss_set[i] = 0;
    }
    for (s = 0; s < XRISK_MAX_SCEN; s++) {
        m->w[s].num = 0;
        m->w[s].den = 0;
        for (i = 0; i < XRISK_MAX_OBLIGORS; i++) {
            m->p_lo[s][i].num = 0;
            m->p_lo[s][i].den = 0;
            m->p_hi[s][i].num = 0;
            m->p_hi[s][i].den = 0;
        }
    }
    if (n_scen == 1) {
        m->w[0].num = 1;
        m->w[0].den = 1;
    }
    return XRISK_OK;
}

int xrisk_model_set_loss(xrisk_model_t *m, uint32_t i, uint32_t units)
{
    if (!m) return XRISK_E_NULL;
    if (i >= m->n_obl || units > XRISK_MAX_UNITS) return XRISK_E_RANGE;
    m->loss[i] = units;
    m->loss_set[i] = 1;
    return XRISK_OK;
}

int xrisk_model_set_weight(xrisk_model_t *m, uint32_t s, int64_t num, int64_t den)
{
    xrisk_q_t q;
    if (!m) return XRISK_E_NULL;
    if (s >= m->n_scen) return XRISK_E_RANGE;
    q.num = num;
    q.den = den;
    if (!xr_q_valid(q)) return XRISK_E_WEIGHT;
    m->w[s] = xr_q_reduce(q);
    return XRISK_OK;
}

int xrisk_model_set_p(xrisk_model_t *m, uint32_t s, uint32_t i, int64_t num, int64_t den)
{
    xrisk_q_t q;
    if (!m) return XRISK_E_NULL;
    if (s >= m->n_scen || i >= m->n_obl) return XRISK_E_RANGE;
    q.num = num;
    q.den = den;
    if (!xr_q_valid(q)) return XRISK_E_PROB;
    q = xr_q_reduce(q);
    m->p_lo[s][i] = q;
    m->p_hi[s][i] = q;
    return XRISK_OK;
}

int xrisk_model_set_sources(xrisk_model_t *m, uint32_t s, uint32_t i, const xrisk_q_t *src,
                            uint32_t n, xrisk_q_t tol, xrisk_state_t *state)
{
    xrisk_q_t lo, hi;
    int rc;
    if (!m || !state) return XRISK_E_NULL;
    if (s >= m->n_scen || i >= m->n_obl) return XRISK_E_RANGE;
    rc = xrisk_fuse(src, n, tol, &lo, &hi, state);
    if (rc) return rc;
    m->p_lo[s][i] = lo;
    m->p_hi[s][i] = hi;
    return XRISK_OK;
}

uint32_t xrisk_model_conflicts(const xrisk_model_t *m)
{
    uint32_t i, s, c = 0;
    if (!m || m->n_obl > XRISK_MAX_OBLIGORS || m->n_scen > XRISK_MAX_SCEN) return 0;
    for (i = 0; i < m->n_obl; i++) {
        for (s = 0; s < m->n_scen; s++) {
            if (m->p_lo[s][i].num != m->p_hi[s][i].num || m->p_lo[s][i].den != m->p_hi[s][i].den) {
                c++;
                break;
            }
        }
    }
    return c;
}

/* ------------------------------------------------------------------ */
/* exact distribution                                                  */
/* ------------------------------------------------------------------ */

/* Convolve all exposures of scenario s into arr[0..total]; numerators over
 * prod m_i. */
static int xr_scenario_dp(const xrisk_model_t *m, xrisk_side_t side, uint32_t s, const int64_t *mi,
                          xrisk_big_t *arr, uint32_t total, uint32_t len)
{
    uint32_t i, k, cur = 0;
    for (k = 0; k <= total; k++) xrisk_big_zero(&arr[k]);
    arr[0].l[0] = 1;
    for (i = 0; i < m->n_obl; i++) {
        xrisk_q_t p = side == XRISK_SIDE_HIGH ? m->p_hi[s][i] : m->p_lo[s][i];
        uint64_t M = (uint64_t) mi[i];
        uint64_t pn = (uint64_t) p.num * xr_udiv64(M, (uint64_t) p.den, 0); /* <= M, exact */
        uint64_t qn = M - pn;
        uint32_t L = m->loss[i];
        if (L == 0 || pn == 0) {
            if (M != 1)
                for (k = 0; k <= cur; k++)
                    if (xr_lin2(arr[k].l, arr[k].l, M, (const uint32_t *) 0, 0, len))
                        return XRISK_E_OVERFLOW;
            continue;
        }
        k = cur + L + 1;
        while (k-- > 0) {
            const uint32_t *a = k <= cur ? arr[k].l : (const uint32_t *) 0;
            const uint32_t *b = (k >= L && k - L <= cur) ? arr[k - L].l : (const uint32_t *) 0;
            if (xr_lin2(arr[k].l, a, qn, b, pn, len)) return XRISK_E_OVERFLOW;
        }
        cur += L;
    }
    return XRISK_OK;
}

int xrisk_compute(const xrisk_model_t *m, xrisk_side_t side, xrisk_dist_t *d)
{
    int64_t mi[XRISK_MAX_OBLIGORS];
    int64_t B = 1, wsum = 0;
    uint32_t i, s, k, total = 0, len;
    int rc;
    if (!m || !d) return XRISK_E_NULL;
    if (side != XRISK_SIDE_LOW && side != XRISK_SIDE_HIGH) return XRISK_E_RANGE;
    if (m->n_obl == 0 || m->n_obl > XRISK_MAX_OBLIGORS) return XRISK_E_RANGE;
    if (m->n_scen == 0 || m->n_scen > XRISK_MAX_SCEN) return XRISK_E_RANGE;
    xrisk_big_zero(&d->den);
    d->max_loss = 0;
    d->len = 0;
    for (i = 0; i < m->n_obl; i++) {
        if (!m->loss_set[i]) return XRISK_E_UNSET;
        if (m->loss[i] > XRISK_MAX_UNITS) return XRISK_E_RANGE;
        total += m->loss[i];
        if (total > XRISK_MAX_UNITS) return XRISK_E_RANGE;
    }
    /* weights: common denominator B, must sum to exactly 1 */
    for (s = 0; s < m->n_scen; s++) {
        if (m->w[s].den == 0) return XRISK_E_UNSET;
        if (!xr_q_valid(m->w[s])) return XRISK_E_WEIGHT;
        B = xr_lcm64(B, m->w[s].den);
        if (B == 0) return XRISK_E_OVERFLOW;
    }
    for (s = 0; s < m->n_scen; s++) {
        int64_t a = m->w[s].num * (int64_t) xr_udiv64((uint64_t) B, (uint64_t) m->w[s].den,
                                                      0); /* <= B, no overflow */
        if (__builtin_add_overflow(wsum, a, &wsum)) return XRISK_E_WEIGHT;
    }
    if (wsum != B) return XRISK_E_WEIGHT;
    /* per-exposure common denominators */
    for (i = 0; i < m->n_obl; i++) {
        mi[i] = 1;
        for (s = 0; s < m->n_scen; s++) {
            xrisk_q_t p = side == XRISK_SIDE_HIGH ? m->p_hi[s][i] : m->p_lo[s][i];
            if (p.den == 0) return XRISK_E_UNSET;
            if (!xr_q_valid(p)) return XRISK_E_PROB;
            mi[i] = xr_lcm64(mi[i], p.den);
            if (mi[i] == 0) return XRISK_E_OVERFLOW;
        }
    }
    /* den = B * prod mi, refused if it exceeds XRISK_BITS */
    xrisk_big_from_u64(&d->den, (uint64_t) B);
    for (i = 0; i < m->n_obl; i++) {
        if (xrisk_big_mul_u64(&d->den, &d->den, (uint64_t) mi[i]) ||
            xrisk_big_bits(&d->den) > XRISK_BITS) {
            xrisk_big_zero(&d->den);
            return XRISK_E_OVERFLOW;
        }
    }
    len = (xrisk_big_bits(&d->den) + 31u) / 32u + 1u;
    if (len > XR_W) len = XR_W;

    if (m->n_scen == 1) {
        rc = xr_scenario_dp(m, side, 0, mi, d->mass, total, len);
    } else {
        rc = XRISK_OK;
        for (k = 0; k <= total; k++) xrisk_big_zero(&d->mass[k]);
        for (s = 0; s < m->n_scen && rc == XRISK_OK; s++) {
            uint64_t a = (uint64_t) (m->w[s].num *
                                     (int64_t) xr_udiv64((uint64_t) B, (uint64_t) m->w[s].den, 0));
            if (a == 0) continue;
            rc = xr_scenario_dp(m, side, s, mi, d->work, total, len);
            for (k = 0; k <= total && rc == XRISK_OK; k++)
                if (xr_lin2(d->mass[k].l, d->mass[k].l, 1, d->work[k].l, a, len))
                    rc = XRISK_E_OVERFLOW;
        }
    }
    if (rc) {
        xrisk_big_zero(&d->den);
        return rc;
    }
    d->max_loss = total;
    d->len = len;
    return XRISK_OK;
}

static int xr_dist_ok(const xrisk_dist_t *d)
{
    if (!d) return XRISK_E_NULL;
    if (xrisk_big_is_zero(&d->den) || d->max_loss > XRISK_MAX_UNITS) return XRISK_E_UNSET;
    return XRISK_OK;
}

static int xr_out(const xrisk_dist_t *d, xrisk_rat_t *out)
{
    int rc = xr_dist_ok(d);
    if (rc) return rc;
    if (!out) return XRISK_E_NULL;
    xrisk_big_zero(&out->num);
    xrisk_big_copy(&out->den, &d->den);
    return XRISK_OK;
}

int xrisk_pmf(const xrisk_dist_t *d, uint32_t k, xrisk_rat_t *out)
{
    int rc = xr_out(d, out);
    if (rc) return rc;
    if (k <= d->max_loss) xrisk_big_copy(&out->num, &d->mass[k]);
    return XRISK_OK;
}

int xrisk_cdf(const xrisk_dist_t *d, uint32_t k, xrisk_rat_t *out)
{
    uint32_t j;
    int rc = xr_out(d, out);
    if (rc) return rc;
    for (j = 0; j <= k && j <= d->max_loss; j++) {
        rc = xrisk_big_add(&out->num, &out->num, &d->mass[j]);
        if (rc) return rc;
    }
    return XRISK_OK;
}

int xrisk_tail(const xrisk_dist_t *d, uint32_t k, xrisk_rat_t *out)
{
    uint32_t j;
    int rc = xr_out(d, out);
    if (rc) return rc;
    for (j = k; j <= d->max_loss; j++) {
        rc = xrisk_big_add(&out->num, &out->num, &d->mass[j]);
        if (rc) return rc;
    }
    return XRISK_OK;
}

int xrisk_expected_loss(const xrisk_dist_t *d, xrisk_rat_t *out)
{
    uint32_t k;
    xrisk_big_t t;
    int rc = xr_out(d, out);
    if (rc) return rc;
    for (k = 1; k <= d->max_loss; k++) {
        rc = xrisk_big_mul_u64(&t, &d->mass[k], k);
        if (!rc) rc = xrisk_big_add(&out->num, &out->num, &t);
        if (rc) return rc;
    }
    return XRISK_OK;
}

static int xr_alpha_ok(int64_t an, int64_t ad)
{
    return ad > 0 && an > 0 && an < ad;
}

/* VaR plus the cumulative numerator C = sum mass[0..v] */
static int xr_var_c(const xrisk_dist_t *d, int64_t an, int64_t ad, uint32_t *v, xrisk_big_t *c)
{
    xrisk_big_t rhs, lhs;
    uint32_t k;
    int rc;
    rc = xrisk_big_mul_u64(&rhs, &d->den, (uint64_t) an);
    if (rc) return rc;
    xrisk_big_zero(c);
    for (k = 0; k <= d->max_loss; k++) {
        rc = xrisk_big_add(c, c, &d->mass[k]);
        if (!rc) rc = xrisk_big_mul_u64(&lhs, c, (uint64_t) ad);
        if (rc) return rc;
        if (xrisk_big_cmp(&lhs, &rhs) >= 0) {
            *v = k;
            return XRISK_OK;
        }
    }
    return XRISK_E_UNSET; /* masses do not sum to den: not a valid dist */
}

int xrisk_var(const xrisk_dist_t *d, int64_t an, int64_t ad, uint32_t *k)
{
    xrisk_big_t c;
    int rc = xr_dist_ok(d);
    if (rc) return rc;
    if (!k) return XRISK_E_NULL;
    if (!xr_alpha_ok(an, ad)) return XRISK_E_ALPHA;
    return xr_var_c(d, an, ad, k, &c);
}

int xrisk_es(const xrisk_dist_t *d, int64_t an, int64_t ad, xrisk_rat_t *out)
{
    xrisk_big_t c, t, u;
    uint32_t v, k;
    int rc = xr_out(d, out);
    if (rc) return rc;
    if (!xr_alpha_ok(an, ad)) return XRISK_E_ALPHA;
    rc = xr_var_c(d, an, ad, &v, &c);
    if (rc) return rc;
    /* S = sum_{k>v} k mass[k]  (into out->num) */
    for (k = v + 1; k <= d->max_loss; k++) {
        rc = xrisk_big_mul_u64(&t, &d->mass[k], k);
        if (!rc) rc = xrisk_big_add(&out->num, &out->num, &t);
        if (rc) return rc;
    }
    rc = xrisk_big_mul_u64(&out->num, &out->num, (uint64_t) ad);
    /* + v * (C*ad - an*den), which is >= 0 by the VaR definition */
    if (!rc) rc = xrisk_big_mul_u64(&t, &c, (uint64_t) ad);
    if (!rc) rc = xrisk_big_mul_u64(&u, &d->den, (uint64_t) an);
    if (!rc) rc = xrisk_big_sub(&t, &t, &u);
    if (!rc) rc = xrisk_big_mul_u64(&t, &t, v);
    if (!rc) rc = xrisk_big_add(&out->num, &out->num, &t);
    if (!rc) rc = xrisk_big_mul_u64(&out->den, &d->den, (uint64_t) (ad - an));
    return rc;
}
