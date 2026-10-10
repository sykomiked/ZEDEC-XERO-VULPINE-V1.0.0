/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxv_fixed.h -- integer fixed-point helpers for freestanding kernel code.
 *
 * Kernel images are built with -mgeneral-regs-only (arm64, arm32, x86_64) or
 * an integer -march/-mabi (riscv), so float, double and _Complex are a build
 * error there. Code that used them now uses these helpers instead:
 *
 *   Q16.16  value * 65536 in a signed integer (int32_t or int64_t)
 *   turn    an angle as a fraction of a full circle, 2^32 = 360 degrees
 *
 * Everything is header-only, static inline, and uses no division by a
 * variable 64-bit divisor (fx_udiv64 is a shift-subtract loop), no __int128
 * and no libgcc helper, so it is safe on rv32/arm32 and in host tests.
 */
#ifndef ZXV_FIXED_H
#define ZXV_FIXED_H

#include <stdint.h>

#define Q16_SHIFT 16
#define Q16_ONE   ((int32_t) 1 << Q16_SHIFT)
/* num/den in Q16.16. Meant for compile-time constants (num, den literal). */
#define Q16_CONST(num, den) ((int32_t) (((int64_t) (num) * 65536 + ((den) / 2)) / (den)))

/* 64/64 unsigned divide by shift-subtract. d == 0 returns 0 (rem = n). */
static inline uint64_t fx_udiv64(uint64_t n, uint64_t d, uint64_t *rem)
{
    if (d == 0) {
        if (rem) *rem = n;
        return 0;
    }
    if (n < d) {
        if (rem) *rem = n;
        return 0;
    }
    if ((n >> 32) == 0 && (d >> 32) == 0) {
        uint32_t n32 = (uint32_t) n, d32 = (uint32_t) d;
        if (rem) *rem = n32 % d32;
        return n32 / d32;
    }
    uint64_t q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) {
            r -= d;
            q |= (uint64_t) 1 << i;
        }
    }
    if (rem) *rem = r;
    return q;
}

/* Signed 64/64 divide, truncating toward zero (C semantics). d == 0 -> 0. */
static inline int64_t fx_sdiv64(int64_t n, int64_t d)
{
    if (d == 0) return 0;
    int neg = (n < 0) != (d < 0);
    uint64_t un = n < 0 ? (uint64_t) 0 - (uint64_t) n : (uint64_t) n;
    uint64_t ud = d < 0 ? (uint64_t) 0 - (uint64_t) d : (uint64_t) d;
    uint64_t q = fx_udiv64(un, ud, 0);
    return neg ? (int64_t) ((uint64_t) 0 - q) : (int64_t) q;
}

/* Full 64x64 -> 128-bit unsigned product as (hi, lo), from 32-bit halves. */
static inline void fx_umul64_wide(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo)
{
    uint64_t al = (uint32_t) a, ah = a >> 32, bl = (uint32_t) b, bh = b >> 32;
    uint64_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    uint64_t mid = (ll >> 32) + (uint32_t) lh + (uint32_t) hl;
    *lo = (mid << 32) | (uint32_t) ll;
    *hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
}

/* Compare a*b with c*d exactly (unsigned): -1, 0 or 1. */
static inline int fx_cmp_umul(uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
    uint64_t h1, l1, h2, l2;
    fx_umul64_wide(a, b, &h1, &l1);
    fx_umul64_wide(c, d, &h2, &l2);
    if (h1 != h2) return h1 < h2 ? -1 : 1;
    if (l1 != l2) return l1 < l2 ? -1 : 1;
    return 0;
}

/* floor(sqrt(v)), bit by bit, no division. */
static inline uint32_t fx_isqrt64(uint64_t v)
{
    uint64_t res = 0;
    uint64_t bit = (uint64_t) 1 << 62;
    while (bit > v) bit >>= 2;
    while (bit != 0) {
        if (v >= res + bit) {
            v -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t) res;
}

/* Q16.16 multiply of two values whose product fits in int64. Rounds toward
 * zero, like a C integer division by 65536. */
static inline int64_t fx_mul_q16(int64_t a, int64_t b)
{
    int64_t p = a * b;
    return p < 0 ? -(int64_t) ((uint64_t) (-p) >> Q16_SHIFT)
                 : (int64_t) ((uint64_t) p >> Q16_SHIFT);
}

/* Q16.16 ratio num/den (den > 0 for a meaningful result; den == 0 -> 0).
 * |num| must be below 2^47 so num << 16 fits. */
static inline int64_t fx_ratio_q16(int64_t num, int64_t den)
{
    return fx_sdiv64(num * 65536, den);
}

/* floor(a * b / d) for unsigned 64-bit a, b, d without __int128: the full
 * 128-bit product is divided by shift-subtract. Saturates to UINT64_MAX when
 * the quotient does not fit (or d == 0). */
static inline uint64_t fx_umuldiv64(uint64_t a, uint64_t b, uint64_t d)
{
    uint64_t hi, lo;
    if (d == 0) return UINT64_MAX;
    fx_umul64_wide(a, b, &hi, &lo);
    if (hi >= d) return UINT64_MAX;
    if (hi == 0) return fx_udiv64(lo, d, 0);
    uint64_t q = 0, r = hi;
    for (int i = 63; i >= 0; i--) {
        uint64_t top = r >> 63;
        r = (r << 1) | ((lo >> i) & 1u);
        if (top || r >= d) {
            r -= d;
            q |= (uint64_t) 1 << i;
        }
    }
    return q;
}

/* a / b as unsigned Q32.32 (floor), saturating at UINT64_MAX. b == 0 -> 0. */
static inline uint64_t fx_ratio_q32(uint64_t a, uint64_t b)
{
    if (b == 0) return 0;
    return fx_umuldiv64(a, (uint64_t) 1 << 32, b);
}

/* log2 of a Q16.16 value, as Q16.16 (floor of the 16-bit fraction). x == 0
 * returns INT32_MIN. Integer part from the top bit, fraction by repeated
 * squaring of the mantissa in Q30. */
static inline int32_t fx_log2_q16(uint64_t x_q16)
{
    if (x_q16 == 0) return INT32_MIN;
    int msb = 63;
    while (((x_q16 >> msb) & 1u) == 0) msb--;
    int32_t ip = (int32_t) msb - Q16_SHIFT;
    uint64_t m = msb >= 30 ? x_q16 >> (msb - 30) : x_q16 << (30 - msb); /* [1,2) in Q30 */
    int32_t frac = 0;
    for (int k = 15; k >= 0; k--) {
        m = (m * m) >> 30;
        if (m >= ((uint64_t) 2 << 30)) {
            m >>= 1;
            frac |= (int32_t) 1 << k;
        }
    }
    return ip * 65536 + frac;
}

/* 2^y for y in Q16.16, as an unsigned Q16.16 value. Saturates at
 * UINT64_MAX >> 1 for large y; underflows to 0. */
static inline uint64_t fx_exp2_q16(int64_t y_q16)
{
    /* 2^(2^-k) for k = 1..16, Q30 */
    static const uint32_t tab[16] = {
        1518500250u, 1276901417u, 1170923762u, 1121280436u, 1097253708u, 1085434106u,
        1079572136u, 1076653033u, 1075196443u, 1074468888u, 1074105294u, 1073923544u,
        1073832680u, 1073787251u, 1073764537u, 1073753181u,
    };
    int64_t ip = y_q16 >> 16; /* floor */
    uint32_t f = (uint32_t) (y_q16 & 0xFFFF);
    uint64_t v = (uint64_t) 1 << 30; /* Q30 */
    for (int k = 0; k < 16; k++)
        if (f & (0x8000u >> k)) v = (v * tab[k] + ((uint64_t) 1 << 29)) >> 30;
    /* v in [1,2) Q30; result Q16 = v * 2^ip >> 14 */
    int64_t sh = ip - 14;
    if (sh >= 0) {
        if (sh > 31) return UINT64_MAX >> 1;
        return v << sh;
    }
    if (sh <= -63) return 0;
    return v >> (-sh);
}

/* ---- trigonometry on a binary turn (2^32 = full circle) -------------------
 * sin and cos in Q16.16, from a Q30 Taylor series on [0, pi/2] (error below
 * 1e-9 before the final rounding, so the Q16.16 result is correctly rounded
 * to within one unit). */
#define FX_HALF_PI_Q30 1686629713LL /* pi/2 * 2^30 */

static inline int64_t fx_q30_mul(int64_t a, int64_t b)
{ /* |a|,|b| < 2^31 */
    int64_t p = a * b;
    return p < 0 ? -(int64_t) ((uint64_t) (-p) >> 30) : (int64_t) ((uint64_t) p >> 30);
}

/* sin(x) and cos(x) for x in Q30, 0 <= x <= pi/2. */
static inline void fx_sincos_q30(int64_t x, int64_t *s, int64_t *c)
{
    /* 1/(k(k+1)) in Q30 for the Horner steps (round(2^30 / d)). */
    static const int64_t inv[8] = {
        536870912LL, /* 1/2   */
        178956971LL, /* 1/6   */
        89478485LL,  /* 1/12  */
        53687091LL,  /* 1/20  */
        35791394LL,  /* 1/30  */
        25565282LL,  /* 1/42  */
        19173961LL,  /* 1/56  */
        14913081LL,  /* 1/72  */
    };
    static const int64_t inv2[4] = {
        11930465LL, /* 1/90  */
        9761289LL,  /* 1/110 */
        8134408LL,  /* 1/132 */
        6882960LL,  /* 1/156 */
    };
    const int64_t one = (int64_t) 1 << 30;
    int64_t x2 = fx_q30_mul(x, x);
    /* sin = x(1 - x^2/6(1 - x^2/20(1 - x^2/42(1 - x^2/72(1 - x^2/110(1 - x^2/156)))))) */
    int64_t t = one - fx_q30_mul(x2, inv2[3]);
    t = one - fx_q30_mul(fx_q30_mul(x2, inv2[1]), t);
    t = one - fx_q30_mul(fx_q30_mul(x2, inv[7]), t);
    t = one - fx_q30_mul(fx_q30_mul(x2, inv[5]), t);
    t = one - fx_q30_mul(fx_q30_mul(x2, inv[3]), t);
    t = one - fx_q30_mul(fx_q30_mul(x2, inv[1]), t);
    *s = fx_q30_mul(x, t);
    /* cos = 1 - x^2/2(1 - x^2/12(1 - x^2/30(1 - x^2/56(1 - x^2/90(1 - x^2/132))))) */
    t = one - fx_q30_mul(x2, inv2[2]);
    t = one - fx_q30_mul(fx_q30_mul(x2, inv2[0]), t);
    t = one - fx_q30_mul(fx_q30_mul(x2, inv[6]), t);
    t = one - fx_q30_mul(fx_q30_mul(x2, inv[4]), t);
    t = one - fx_q30_mul(fx_q30_mul(x2, inv[2]), t);
    *c = one - fx_q30_mul(fx_q30_mul(x2, inv[0]), t);
}

static inline int32_t fx_q30_to_q16(int64_t v)
{
    int64_t a = v < 0 ? -v : v;
    a = (a + (1 << 13)) >> 14;
    return (int32_t) (v < 0 ? -a : a);
}

/* sin and cos of a turn angle, Q16.16. */
static inline void fx_sincos_turn(uint32_t turn, int32_t *s_out, int32_t *c_out)
{
    uint32_t quad = turn >> 30;
    int64_t r = (int64_t) (turn & 0x3FFFFFFFu); /* fraction of a quarter, Q30 */
    int64_t x = (r * FX_HALF_PI_Q30) >> 30;     /* radians, Q30, 0..pi/2 */
    int64_t s, c;
    fx_sincos_q30(x, &s, &c);
    int64_t so, co;
    switch (quad) {
    case 0:
        so = s;
        co = c;
        break;
    case 1:
        so = c;
        co = -s;
        break;
    case 2:
        so = -s;
        co = -c;
        break;
    default:
        so = -c;
        co = s;
        break;
    }
    if (s_out) *s_out = fx_q30_to_q16(so);
    if (c_out) *c_out = fx_q30_to_q16(co);
}

static inline int32_t fx_sin_turn(uint32_t turn)
{
    int32_t s;
    fx_sincos_turn(turn, &s, 0);
    return s;
}

static inline int32_t fx_cos_turn(uint32_t turn)
{
    int32_t c;
    fx_sincos_turn(turn, 0, &c);
    return c;
}

/* k/n of a full turn as a binary turn (n > 0). */
static inline uint32_t fx_turn_frac(uint64_t k, uint64_t n)
{
    uint64_t rem;
    uint64_t whole = fx_udiv64(k, n, &rem);
    (void) whole; /* full turns wrap away */
    return (uint32_t) fx_udiv64(rem << 32, n, 0);
}

/* ---- complex numbers, Q16.16 parts held in int64 --------------------------- */
typedef struct zxv_cq16 {
    int64_t re, im;
} zxv_cq16_t;

static inline zxv_cq16_t cq16(int64_t re, int64_t im)
{
    zxv_cq16_t z;
    z.re = re;
    z.im = im;
    return z;
}

static inline zxv_cq16_t cq16_add(zxv_cq16_t a, zxv_cq16_t b)
{
    return cq16(a.re + b.re, a.im + b.im);
}

static inline int cq16_eq(zxv_cq16_t a, zxv_cq16_t b)
{
    return a.re == b.re && a.im == b.im;
}

static inline int cq16_is_zero(zxv_cq16_t a)
{
    return a.re == 0 && a.im == 0;
}

/* Product of two Q16.16 complex values; parts must stay below 2^31 in
 * magnitude for the partial products to fit. */
static inline zxv_cq16_t cq16_mul(zxv_cq16_t a, zxv_cq16_t b)
{
    return cq16(fx_mul_q16(a.re, b.re) - fx_mul_q16(a.im, b.im),
                fx_mul_q16(a.re, b.im) + fx_mul_q16(a.im, b.re));
}

/* e^(i * 2pi * turn/2^32) as a Q16.16 complex value. */
static inline zxv_cq16_t cq16_expi_turn(uint32_t turn)
{
    int32_t s, c;
    fx_sincos_turn(turn, &s, &c);
    return cq16(c, s);
}

/* |z| in Q16.16 (floor). Parts are scaled down until their squares fit. */
static inline uint64_t cq16_abs(zxv_cq16_t z)
{
    uint64_t a = z.re < 0 ? (uint64_t) 0 - (uint64_t) z.re : (uint64_t) z.re;
    uint64_t b = z.im < 0 ? (uint64_t) 0 - (uint64_t) z.im : (uint64_t) z.im;
    unsigned sh = 0;
    while (a >= ((uint64_t) 1 << 31) || b >= ((uint64_t) 1 << 31)) {
        a >>= 1;
        b >>= 1;
        sh++;
    }
    return (uint64_t) fx_isqrt64(a * a + b * b) << sh;
}

#endif /* ZXV_FIXED_H */
