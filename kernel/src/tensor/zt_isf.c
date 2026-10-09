/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zt_isf.c — the Interaction Surplus Framework inside the tensor core, in
 * Q16 integers. See zt.h T9 and src/surplus/surplus.h for the theory. */
#include "zt.h"

int64_t zt__sdiv64(int64_t n, int64_t d);

#define LN2_Q16 45426 /* ln 2 * 2^16 */

/* log2 by repeated squaring of the mantissa: one exact bit per square. */
zt_fx zt_log2(uint64_t x)
{
    if (x == 0) return INT32_MIN;
    int32_t p = 63;
    while (!((x >> p) & 1u)) p--;
    uint64_t m = p > 30 ? x >> (p - 30) : x << (30 - p); /* [2^30, 2^31) */
    int32_t frac = 0;
    for (int i = 1; i <= 16; i++) {
        m = (m * m) >> 30;
        if (m >= ((uint64_t) 1 << 31)) {
            m >>= 1;
            frac |= 1 << (16 - i);
        }
    }
    return (p - 16) * 65536 + frac;
}

zt_fx zt_ln(uint64_t x)
{
    return (zt_fx) (((int64_t) zt_log2(x) * LN2_Q16) >> 16);
}

zt_fx zt_surplus_u(const zt_fx *a, const zt_fx *b, uint32_t n)
{
    int64_t dot = 0, na = 0, nb = 0;
    for (uint32_t i = 0; i < n; i++) {
        dot += ((int64_t) a[i] * b[i]) >> 16;
        na += ((int64_t) a[i] * a[i]) >> 16;
        nb += ((int64_t) b[i] * b[i]) >> 16;
    }
    if (na <= 0 || nb <= 0) return 0; /* a zero vector adds nothing */
    /* cos^2 = dot^2 / (na nb) is unchanged when all three halve together. */
    while (na > ((int64_t) 1 << 40) || nb > ((int64_t) 1 << 40)) {
        dot /= 2;
        na >>= 1;
        nb >>= 1;
    }
    int64_t ra = zt_isqrt64((uint64_t) na << 16), rb = zt_isqrt64((uint64_t) nb << 16);
    int64_t den = (ra * rb) >> 16;
    if (den <= 0) return 0;
    int64_t cs = zt__sdiv64(dot * 65536, den);
    if (cs > ZT_ONE) cs = ZT_ONE;
    if (cs < -ZT_ONE) cs = -ZT_ONE;
    int64_t u = ZT_ONE - ((cs * cs) >> 16);
    return (zt_fx) (u < 0 ? 0 : u);
}

zt_fx zt_surplus_f(zt_fx u, uint32_t N)
{
    if (N < 2 || u <= 0) return 0;
    if (u > ZT_ONE) u = ZT_ONE;
    return zt_ln(((uint64_t) 1 << 16) + (uint64_t) (N - 1) * (uint32_t) u);
}

uint32_t zt_surplus_gate(const zt_fx *v, uint32_t count, uint32_t n, uint32_t N, zt_fx threshold,
                         bool *keep, zt_fx *total_surplus)
{
    uint32_t kept = 0;
    int64_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        zt_fx least = INT32_MAX;
        for (uint32_t j = 0; j < i; j++) {
            if (!keep[j]) continue;
            zt_fx f = zt_surplus_f(zt_surplus_u(v + (uint64_t) i * n, v + (uint64_t) j * n, n), N);
            if (f < least) least = f;
        }
        keep[i] = kept == 0 || least >= threshold;
        if (keep[i]) {
            if (kept) total += least;
            kept++;
        }
    }
    if (total_surplus) *total_surplus = (zt_fx) (total > INT32_MAX ? INT32_MAX : total);
    return kept;
}

void zt_isf_init(zt_isf_t *s, int64_t Q0, zt_fx delta, zt_fx eta, uint32_t N)
{
    s->Q = Q0 < 0 ? 0 : Q0;
    s->delta = delta;
    s->eta = eta;
    s->N = N;
}

bool zt_isf_can_grow(const zt_isf_t *s, zt_fx S, zt_fx C)
{
    return (((int64_t) s->eta * S) >> 16) > ((s->Q * s->delta) >> 16) + C;
}

int64_t zt_isf_step(zt_isf_t *s, zt_fx S, zt_fx C)
{
    int64_t q = s->Q - ((s->Q * s->delta) >> 16) + (((int64_t) s->eta * S) >> 16) - C;
    s->Q = q < 0 ? 0 : q;
    return s->Q;
}

int64_t zt_isf_ceiling(const zt_isf_t *s)
{
    if (s->delta <= 0) return INT64_MAX;
    int64_t lnN = s->N < 2 ? 0 : zt_ln((uint64_t) s->N << 16);
    int64_t c = zt__sdiv64((((int64_t) s->eta * lnN) >> 16) * 65536, s->delta);
    return c > s->Q ? c : s->Q;
}
