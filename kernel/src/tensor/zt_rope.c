/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* zt_rope.c — rotary position encoding (T20). See zt_rope.h. */
#include "zt_rope.h"
#include "zt_rope_tables.h"

/* high 64 bits of a 64 x 64 product, from 32-bit limbs */
static uint64_t mulhi(uint64_t a, uint64_t b)
{
    uint64_t a0 = (uint32_t) a, a1 = a >> 32, b0 = (uint32_t) b, b1 = b >> 32;
    uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    uint64_t mid = (p00 >> 32) + (uint32_t) p01 + (uint32_t) p10;
    return p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

/* x * x for x in [1, 2) as Q62; the result is in [1, 4), Q62 */
static uint64_t sq_q62(uint64_t x)
{
    uint64_t hi = mulhi(x, x), lo = x * x;
    return (hi << 2) | (lo >> 62);
}

/* log2 of an f32 > 1, Q56 */
static bool log2_f32(uint32_t bits, uint64_t *out)
{
    uint32_t e = (bits >> 23) & 0xFF, m = bits & 0x7FFFFF;
    if (bits >> 31 || e == 0 || e == 0xFF) return false; /* negative, subnormal, inf, nan */
    int32_t ex = (int32_t) e - 127;
    if (ex < 0 || (ex == 0 && m == 0)) return false; /* base must exceed 1 */
    uint64_t x = (uint64_t) (m | 0x800000u) << 39;   /* Q62 in [1, 2) */
    uint64_t frac = 0;
    for (int k = 1; k <= 56; k++) {
        x = sq_q62(x);
        if (x >= (1ull << 63)) {
            x >>= 1;
            frac |= 1ull << (56 - k);
        }
    }
    *out = ((uint64_t) ex << 56) | frac;
    return true;
}

/* 2^(-t) for t >= 0 in Q56, as Q64 (saturating just below 1.0) */
static uint64_t exp2neg_q56(uint64_t t)
{
    uint64_t n = t >> 56, res = ~0ull;
    if (n >= 64) return 0;
    for (int k = 1; k <= 56; k++)
        if (t & (1ull << (56 - k))) res = mulhi(res, ZT_ROPE_EXP2N[k]);
    return res >> n;
}

bool zt_rope_init(zt_rope_t *r, uint32_t n_rot, uint32_t base_f32, bool neox)
{
    uint64_t L;
    if (!r || n_rot < 2 || (n_rot & 1) || n_rot > ZT_ROPE_MAX_ROT || !log2_f32(base_f32, &L))
        return false;
    r->n_rot = n_rot;
    r->neox = neox;
    /* exponent of pair i: 2 i L / n_rot = i q + floor(i rem / n_rot), exact */
    uint64_t rem, q = zt_udiv64(2 * L, n_rot, &rem);
    for (uint32_t i = 0; i < n_rot / 2; i++) {
        uint64_t t = i * q + zt_udiv64(i * rem, n_rot, 0);
        r->freq[i] = mulhi(ZT_ROPE_INV_2PI_Q64, exp2neg_q56(t));
    }
    return true;
}

/* f / v for an f32 v = mant * 2^sh, as floor(f * 2^-sh / mant) */
static bool div_f32(uint64_t f, uint32_t bits, uint64_t *out)
{
    uint32_t e = (bits >> 23) & 0xFF, m = bits & 0x7FFFFF;
    if (bits >> 31 || e == 0 || e == 0xFF) return false;
    int32_t sh = (int32_t) e - 150; /* v = (m | 2^23) * 2^sh */
    if (sh < -43) return false;     /* v < 2^-20 */
    uint64_t mant = m | 0x800000u, rem;
    if (sh >= 0) {
        *out = sh >= 64 ? 0 : zt_udiv64(f >> sh, mant, 0);
        return true;
    }
    uint64_t qv = zt_udiv64(f, mant, &rem);
    for (int32_t k = 0; k < -sh; k++) {
        if (qv >> 63) return false; /* the frequency would pass a full turn per position */
        rem <<= 1;
        qv = (qv << 1) | (rem >= mant);
        if (rem >= mant) rem -= mant;
    }
    *out = qv;
    return true;
}

bool zt_rope_divide(zt_rope_t *r, const uint32_t *factors_f32, uint32_t n)
{
    if (!r || !factors_f32 || (n != 1 && n != r->n_rot / 2)) return false;
    uint64_t tmp[ZT_ROPE_MAX_ROT / 2];
    for (uint32_t i = 0; i < r->n_rot / 2; i++)
        if (!div_f32(r->freq[i], factors_f32[n == 1 ? 0 : i], &tmp[i])) return false;
    for (uint32_t i = 0; i < r->n_rot / 2; i++) r->freq[i] = tmp[i];
    return true;
}

static int64_t rshift_round(int64_t v, uint32_t s)
{
    return (v + ((int64_t) 1 << (s - 1))) >> s;
}

void zt_rope_sincos(uint64_t phase, int32_t *sin_q30, int32_t *cos_q30)
{
    uint32_t j = (uint32_t) (phase >> 54);
    uint64_t rho = phase & ((1ull << 54) - 1);
    uint64_t d = mulhi(rho << 2, ZT_ROPE_HALF_PI_Q62); /* residual angle, radians, Q62 (< 0.0062) */
    uint64_t d2 = mulhi(d, d) << 2;                    /* Q62 */
    uint64_t d3 = mulhi(d2, d) << 2, d4 = mulhi(d2, d2) << 2;
    int64_t sd = (int64_t) (d - zt_udiv64(d3, 6, 0));
    int64_t cd = (int64_t) ((1ull << 62) - (d2 >> 1) + zt_udiv64(d4, 24, 0));
    int64_t s30 = rshift_round(sd, 32), c30 = rshift_round(cd, 32);
    int64_t sa = ZT_ROPE_SIN[j], ca = ZT_ROPE_SIN[(j + 256) & 1023];
    *sin_q30 = (int32_t) rshift_round(sa * c30 + ca * s30, 30);
    *cos_q30 = (int32_t) rshift_round(ca * c30 - sa * s30, 30);
}

static zt_fx sat32(int64_t v)
{
    return (zt_fx) (v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : v);
}

static void rotate(const zt_rope_t *r, zt_fx *x, const int32_t *s, const int32_t *c)
{
    uint32_t half = r->n_rot / 2;
    for (uint32_t i = 0; i < half; i++) {
        uint32_t a = r->neox ? i : 2 * i, b = r->neox ? i + half : 2 * i + 1;
        int64_t x0 = x[a], x1 = x[b];
        x[a] = sat32(rshift_round(x0 * c[i] - x1 * s[i], 30));
        x[b] = sat32(rshift_round(x0 * s[i] + x1 * c[i], 30));
    }
}

void zt_rope_apply(const zt_rope_t *r, zt_fx *x, uint32_t head_dim, uint64_t pos)
{
    zt_rope_apply_heads(r, x, 1, head_dim, pos);
}

/* The angles depend only on the position, so they are computed once and
 * shared by every head. */
void zt_rope_apply_heads(const zt_rope_t *r, zt_fx *x, uint32_t n_heads, uint32_t head_dim,
                         uint64_t pos)
{
    int32_t s[ZT_ROPE_MAX_ROT / 2], c[ZT_ROPE_MAX_ROT / 2];
    if (head_dim < r->n_rot) return;
    for (uint32_t i = 0; i < r->n_rot / 2; i++) zt_rope_sincos(pos * r->freq[i], &s[i], &c[i]);
    for (uint32_t h = 0; h < n_heads; h++) rotate(r, x + (uint64_t) h * head_dim, s, c);
}
