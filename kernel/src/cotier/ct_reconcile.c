/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* ct_reconcile.c — tier-3 reconciliation (see ct_reconcile.h). */
#include "ct_reconcile.h"

/* ---- R1: float bits to Q16.16 ---- */

/* sign, biased exponent field, mantissa field of a binary float with ebits
 * exponent and mbits mantissa bits. */
static zt_fx decode(uint32_t sign, uint32_t ef, uint32_t mf, uint32_t ebits, uint32_t mbits,
                    uint32_t *flags)
{
    uint32_t fl = 0;
    uint32_t emax = (1u << ebits) - 1u;
    int32_t bias = (int32_t) ((1u << (ebits - 1u)) - 1u);
    zt_fx r;
    if (ef == emax) {
        if (mf) {
            fl = CT_ING_NAN;
            r = 0;
        } else {
            fl = CT_ING_INF;
            r = sign ? INT32_MIN : INT32_MAX;
        }
        if (flags) *flags = fl;
        return r;
    }
    if (ef == 0 && mf == 0) {
        if (flags) *flags = 0;
        return 0;
    }
    uint64_t m;
    int32_t e; /* value = m * 2^e */
    if (ef == 0) {
        fl |= CT_ING_SUB;
        m = mf;
        e = 1 - bias - (int32_t) mbits;
    } else {
        m = mf | ((uint64_t) 1 << mbits);
        e = (int32_t) ef - bias - (int32_t) mbits;
    }
    int32_t s = e + 16; /* Q16 raw = m * 2^s */
    uint64_t v;
    if (s >= 0) {
        if (s >= 40) {
            v = (uint64_t) 1 << 40; /* m >= 1: certainly out of range */
        } else {
            v = m << s; /* m < 2^24, s < 40 */
        }
    } else {
        uint32_t sh = (uint32_t) -s;
        if (sh > 40) {
            v = 0;
            fl |= CT_ING_INEXACT;
        } else {
            uint64_t rem = m & (((uint64_t) 1 << sh) - 1u), half = (uint64_t) 1 << (sh - 1u);
            v = m >> sh;
            if (rem) fl |= CT_ING_INEXACT;
            if (rem > half || (rem == half && (v & 1u))) v++; /* nearest, ties to even */
        }
        if (v == 0) fl |= CT_ING_FLUSH;
    }
    if (sign) {
        if (v > (uint64_t) 1 << 31) {
            fl |= CT_ING_SAT;
            r = INT32_MIN;
        } else {
            r = (zt_fx) (0 - (int64_t) v);
        }
    } else {
        if (v > (uint64_t) INT32_MAX) {
            fl |= CT_ING_SAT;
            r = INT32_MAX;
        } else {
            r = (zt_fx) v;
        }
    }
    if (fl & CT_ING_SAT) fl &= ~(uint32_t) CT_ING_INEXACT;
    if (flags) *flags = fl;
    return r;
}

zt_fx ct_f16_to_q16(uint16_t b, uint32_t *flags)
{
    return decode(b >> 15, (b >> 10) & 0x1Fu, b & 0x3FFu, 5, 10, flags);
}

zt_fx ct_bf16_to_q16(uint16_t b, uint32_t *flags)
{
    return decode(b >> 15, (b >> 7) & 0xFFu, b & 0x7Fu, 8, 7, flags);
}

zt_fx ct_f32_to_q16(uint32_t b, uint32_t *flags)
{
    return decode(b >> 31, (b >> 23) & 0xFFu, b & 0x7FFFFFu, 8, 23, flags);
}

int64_t ct_ingest(uint32_t fmt, const uint16_t *src16, const uint32_t *src32, uint64_t n,
                  zt_fx *out, uint8_t *flags, ct_ingest_stats_t *st)
{
    if (fmt > CT_FMT_F32 || (n && !out)) return -1;
    if (fmt == CT_FMT_F32 ? (n && !src32) : (n && !src16)) return -1;
    int64_t faults = 0;
    for (uint64_t i = 0; i < n; i++) {
        uint32_t fl;
        uint16_t h = fmt == CT_FMT_F32 ? 0 : src16[i];
        if (fmt == CT_FMT_F16)
            out[i] = ct_f16_to_q16(h, &fl);
        else if (fmt == CT_FMT_BF16)
            out[i] = ct_bf16_to_q16(h, &fl);
        else
            out[i] = ct_f32_to_q16(src32[i], &fl);
        if (flags) flags[i] = (uint8_t) fl;
        if (fl & CT_ING_FAULT) faults++;
        if (st) {
            st->n++;
            st->nan += (fl & CT_ING_NAN) != 0;
            st->pos_inf += (fl & CT_ING_INF) && out[i] > 0;
            st->neg_inf += (fl & CT_ING_INF) && out[i] < 0;
            st->sat += (fl & CT_ING_SAT) != 0;
            st->subnormal += (fl & CT_ING_SUB) != 0;
            st->flushed += (fl & CT_ING_FLUSH) != 0;
            st->inexact += (fl & CT_ING_INEXACT) != 0;
        }
    }
    return faults;
}

/* ---- R2: two-source decomposition ---- */

static uint32_t uabs32(int32_t v)
{
    return v < 0 ? (uint32_t) 0 - (uint32_t) v : (uint32_t) v;
}

static uint32_t top_bit(uint32_t m)
{
    uint32_t t = 0;
    while (m >> t > 1u) t++;
    return t;
}

/* v scaled by 2^(14 - top): within (-2^15, 2^15). */
static int64_t sc14(int32_t v, uint32_t top)
{
    uint32_t m = uabs32(v);
    m = top >= 14 ? m >> (top - 14) : m << (14 - top);
    return v < 0 ? -(int64_t) m : (int64_t) m;
}

static uint64_t udiv(uint64_t n, uint64_t d)
{
    return d ? zt_udiv64(n, d, 0) : 0;
}

int32_t ct_decompose(const zt_fx *ref, const zt_fx *acc, uint32_t n, const uint8_t *cat_of,
                     uint32_t n_cat, uint32_t N, ct_decomp_t *d)
{
    if (!d) return -1;
    d->category = 0;
    d->alpha = 0;
    d->u_cross = d->u_div = d->u_total = d->f_cross = d->f_div = d->f_total = 0;
    d->consistent = true;
    if (!ref || !acc || !cat_of || !n_cat || n_cat > CT_MAX_CATEGORIES || n > (1u << 24) || N < 2)
        return -1;
    uint32_t mr = 0, ma = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (cat_of[i] >= n_cat) return -1;
        if (uabs32(ref[i]) > mr) mr = uabs32(ref[i]);
        if (uabs32(acc[i]) > ma) ma = uabs32(acc[i]);
    }
    if (!mr || !ma) return 1;
    uint32_t tr = top_bit(mr), ta = top_bit(ma);
    /* Per-category sums of the scaled vectors: each value is within 2^15,
     * so every sum is below n * 2^30 <= 2^54. */
    int64_t ex[CT_MAX_CATEGORIES], ea[CT_MAX_CATEGORIES], dot = 0;
    for (uint32_t c = 0; c < n_cat; c++) ex[c] = ea[c] = 0;
    for (uint32_t i = 0; i < n; i++) {
        int64_t x = sc14(ref[i], tr), a = sc14(acc[i], ta);
        ex[cat_of[i]] += x * x;
        ea[cat_of[i]] += a * a;
        dot += x * a;
    }
    int64_t nx = 0, na = 0;
    for (uint32_t c = 0; c < n_cat; c++) {
        nx += ex[c];
        na += ea[c];
        if (ex[c] > ex[d->category]) d->category = c;
    }
    /* Scale the sums below 2^30 (dot by the square root of the factor) so
     * every product below fits 64 bits. Ratios are unchanged. */
    uint32_t sx = 0, sa = 0;
    while ((nx >> (2u * sx)) >= ((int64_t) 1 << 30)) sx++;
    while ((na >> (2u * sa)) >= ((int64_t) 1 << 30)) sa++;
    nx = na = 0;
    for (uint32_t c = 0; c < n_cat; c++) {
        ex[c] >>= 2u * sx;
        ea[c] >>= 2u * sa;
        nx += ex[c];
        na += ea[c];
    }
    for (uint32_t k = 0; k < sx + sa; k++) dot /= 2;
    if (nx <= 0 || na <= 0) return 1;
    uint64_t ad = (uint64_t) (dot < 0 ? -dot : dot);
    uint64_t den = ((uint64_t) nx * (uint64_t) na) >> 16; /* >= 2^40 */
    uint64_t cos2 = udiv(ad * ad, den);                   /* (x.y)^2, Q16 */
    if (cos2 > ZT_ONE) cos2 = ZT_ONE;
    uint64_t sp = 0;
    for (uint32_t c = 0; c < n_cat; c++)
        if (ex[c] && ea[c]) sp += zt_isqrt64((uint64_t) ex[c] * (uint64_t) ea[c]);
    uint64_t root = zt_isqrt64((uint64_t) nx * (uint64_t) na);
    uint64_t alpha = udiv(sp << 16, root); /* p.q, Q16 */
    if (alpha > ZT_ONE) alpha = ZT_ONE;
    uint64_t alpha2 = (alpha * alpha) >> 16;
    int64_t ud = (int64_t) alpha2 - (int64_t) cos2;
    d->alpha = (zt_fx) alpha;
    d->u_cross = (zt_fx) (ZT_ONE - alpha2);
    d->u_div = (zt_fx) (ud < 0 ? 0 : ud);
    d->u_total = (zt_fx) (ZT_ONE - cos2);
    d->f_cross = zt_surplus_f(d->u_cross, N);
    d->f_div = zt_surplus_f(d->u_div, N);
    d->f_total = zt_surplus_f(d->u_total, N);
    /* Cauchy-Schwarz per category (u_div >= 0) and the f bounds, with room
     * for rounding: each u is within 4 Q16 units of exact, which f (slope
     * at most N - 1) turns into (N - 1) * 4, plus a few units of zt_ln. */
    int64_t lo = d->f_cross > d->f_div ? d->f_cross : d->f_div;
    int64_t slack = 8 + 4 * (int64_t) (N - 1u);
    d->consistent = ud >= -8 && d->f_total >= lo - slack &&
                    (int64_t) d->f_total <= (int64_t) d->f_cross + d->f_div + slack;
    return 0;
}

zt_fx ct_norm_ratio(const zt_fx *ref, const zt_fx *acc, uint32_t n)
{
    uint32_t mr = 0, ma = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (uabs32(ref[i]) > mr) mr = uabs32(ref[i]);
        if (uabs32(acc[i]) > ma) ma = uabs32(acc[i]);
    }
    if (!mr) return 0;
    if (!ma) return 0;
    uint32_t tr = top_bit(mr), ta = top_bit(ma);
    uint64_t sr = 0, sa = 0;
    for (uint32_t i = 0; i < n; i++) {
        int64_t r = sc14(ref[i], tr), a = sc14(acc[i], ta);
        sr += (uint64_t) (r * r);
        sa += (uint64_t) (a * a);
    }
    int32_t k = (int32_t) ta - (int32_t) tr;
    while (sr >= ((uint64_t) 1 << 30)) {
        sr >>= 2;
        k--;
    }
    while (sa >= ((uint64_t) 1 << 30)) {
        sa >>= 2;
        k++;
    }
    uint64_t q = udiv(sa << 32, sr); /* sa / sr in Q32; sr >= 2^26 */
    uint64_t r = zt_isqrt64(q);      /* sqrt(sa / sr) in Q16 */
    if (k >= 0) {
        if (k >= 32 || r > ((uint64_t) INT32_MAX >> k)) return INT32_MAX;
        return (zt_fx) (r << k);
    }
    if (k <= -40) return 0;
    return (zt_fx) (r >> (uint32_t) -k);
}

/* ---- R3, R4: drift and verdict ---- */

static zt_fx sat_mul(uint32_t L, zt_fx u)
{
    int64_t v = (int64_t) L * u;
    return v > INT32_MAX ? INT32_MAX : (zt_fx) v;
}

uint32_t ct_drift_check(const ct_drift_cfg_t *cfg, const zt_fx *ref, const zt_fx *acc, uint32_t n,
                        const uint8_t *cat_of, uint32_t n_cat, uint64_t faults,
                        ct_drift_stats_t *st, ct_decomp_t *dout)
{
    ct_decomp_t dd, *d = dout ? dout : &dd;
    uint32_t verdict;
    bool cross_bad = false, div_bad = false, mag_bad = false, incons = false;
    zt_fx lc = 0, ld = 0;
    int32_t r = cfg ? ct_decompose(ref, acc, n, cat_of, n_cat, cfg->N, d) : -1;
    if (r < 0) {
        verdict = CT_PARADOX;
        incons = true;
    } else if (r == 1) {
        verdict = faults ? CT_FALSE : CT_UNKNOWN; /* nothing valid came back */
    } else {
        uint32_t L = cfg->N - 1u;
        lc = sat_mul(L, d->u_cross);
        ld = sat_mul(L, d->u_div);
        cross_bad = lc > cfg->tau_cross;
        div_bad = ld > cfg->tau_div;
        int64_t ratio = ct_norm_ratio(ref, acc, n);
        int64_t dm = ratio - ZT_ONE;
        if (dm < 0) dm = -dm;
        mag_bad = dm > cfg->tol_mag;
        bool dir_ok = !cross_bad && !div_bad, mag_ok = !mag_bad;
        incons = !d->consistent;
        if (incons)
            verdict = CT_PARADOX;
        else if (faults)
            verdict = dir_ok && mag_ok ? CT_PARADOX : CT_FALSE;
        else if (dir_ok && mag_ok)
            verdict = CT_TRUE;
        else if (!dir_ok && !mag_ok)
            verdict = CT_FALSE;
        else
            verdict = CT_GLUT;
    }
    if (st) {
        st->samples++;
        st->truth[verdict]++;
        st->cross_drift += cross_bad;
        st->div_drift += div_bad;
        st->mag_drift += mag_bad;
        st->faulted += faults != 0;
        st->inconsistent += incons;
        if (lc > st->worst_cross) st->worst_cross = lc;
        if (ld > st->worst_div) st->worst_div = ld;
    }
    return verdict;
}

bool ct_sample(uint64_t seed, uint64_t index, uint32_t rate_q16)
{
    uint64_t z = seed + (index + 1u) * 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return (uint32_t) (z & 0xFFFFu) < rate_q16;
}
