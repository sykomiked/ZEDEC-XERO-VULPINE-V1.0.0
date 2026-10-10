/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fortran.c — see fortran.h. Integer-only record framing and HFP/IEEE. */
#include "fortran.h"
#include "legacy_util.h"

/* ===================== record markers ===================== */

static uint64_t read_marker(const uint8_t *p, uint8_t size, bool be)
{
    uint64_t v = 0;
    if (be) {
        for (uint32_t i = 0; i < size; i++) v = (v << 8) | p[i];
    } else {
        for (uint32_t i = 0; i < size; i++) v |= (uint64_t) p[i] << (8 * i);
    }
    return v;
}

static void write_marker(uint8_t *p, uint8_t size, bool be, uint64_t v)
{
    if (be) {
        for (uint32_t i = 0; i < size; i++) p[size - 1 - i] = (uint8_t) (v >> (8 * i));
    } else {
        for (uint32_t i = 0; i < size; i++) p[i] = (uint8_t) (v >> (8 * i));
    }
}

void fortran_rec_init(fortran_rec_iter *it, const uint8_t *buf, uint32_t len, uint8_t marker_size,
                      bool big_endian)
{
    it->buf = buf;
    it->len = len;
    it->pos = 0;
    it->marker_size = (marker_size == 8) ? 8 : 4;
    it->big_endian = big_endian;
}

const uint8_t *fortran_rec_next(fortran_rec_iter *it, uint32_t *out_len)
{
    uint8_t ms = it->marker_size;
    if (it->pos + ms > it->len) return 0;
    uint64_t lead = read_marker(it->buf + it->pos, ms, it->big_endian);
    uint32_t start = it->pos + ms;
    if (lead > it->len || start + lead + ms > it->len) return 0;
    uint64_t trail = read_marker(it->buf + start + lead, ms, it->big_endian);
    if (trail != lead) return 0;
    if (out_len) *out_len = (uint32_t) lead;
    it->pos = start + (uint32_t) lead + ms;
    return it->buf + start;
}

uint32_t fortran_rec_write(const uint8_t *data, uint32_t n, uint8_t marker_size, bool big_endian,
                           uint8_t *out, uint32_t cap)
{
    uint8_t ms = (marker_size == 8) ? 8 : 4;
    uint32_t total = (uint32_t) ms + n + ms;
    if (total > cap) return 0;
    write_marker(out, ms, big_endian, n);
    lg_copy(out + ms, data, n);
    write_marker(out + ms + n, ms, big_endian, n);
    return total;
}

/* ===================== IEEE width conversions ===================== */

uint64_t ieee32_to_ieee64(uint32_t x)
{
    uint64_t s = (x >> 31) & 1u;
    uint32_t e = (x >> 23) & 0xFF;
    uint32_t m = x & 0x7FFFFF;
    if (e == 0 && m == 0) return s << 63; /* signed zero */
    if (e == 0xFF) {                      /* Inf / NaN */
        return (s << 63) | ((uint64_t) 0x7FF << 52) | ((uint64_t) m << 29);
    }
    if (e == 0) {
        /* subnormal single: normalize into a double */
        int32_t exp = -126;
        while ((m & 0x800000) == 0) {
            m <<= 1;
            exp--;
        }
        m &= 0x7FFFFF;
        uint64_t e64 = (uint64_t) (exp + 1023);
        return (s << 63) | (e64 << 52) | ((uint64_t) m << 29);
    }
    uint64_t e64 = (uint64_t) ((int32_t) e - 127 + 1023);
    return (s << 63) | (e64 << 52) | ((uint64_t) m << 29);
}

uint32_t ieee64_to_ieee32(uint64_t x)
{
    uint32_t s = (uint32_t) ((x >> 63) & 1u);
    uint32_t e = (uint32_t) ((x >> 52) & 0x7FF);
    uint64_t m = x & 0xFFFFFFFFFFFFFULL;
    if (e == 0 && m == 0) return s << 31;
    if (e == 0x7FF) return (s << 31) | (0xFFu << 23) | (m ? 0x400000u : 0u);
    int32_t E = (int32_t) e - 1023;
    int32_t e32 = E + 127;
    if (e32 >= 255) return (s << 31) | (0xFFu << 23); /* overflow -> Inf */
    if (e32 <= 0) return s << 31;                     /* underflow -> flush to zero */
    uint32_t m32 = (uint32_t) (m >> 29);
    uint32_t rem = (uint32_t) (m & 0x1FFFFFFFu);
    const uint32_t half = 0x10000000u;
    if (rem > half || (rem == half && (m32 & 1u))) {
        m32++;
        if (m32 == 0x800000u) {
            m32 = 0;
            e32++;
            if (e32 >= 255) return (s << 31) | (0xFFu << 23);
        }
    }
    return (s << 31) | ((uint32_t) e32 << 23) | m32;
}

/* ===================== HFP64 <-> IEEE64 ===================== */

uint64_t hfp64_to_ieee64(uint64_t hfp)
{
    uint64_t s = (hfp >> 63) & 1u;
    uint32_t hexexp = (uint32_t) ((hfp >> 56) & 0x7F);
    uint64_t frac = hfp & 0x00FFFFFFFFFFFFFFULL; /* 56 bits */
    if (frac == 0) return s << 63;               /* signed zero */

    /* find index of most significant set bit (0..55) */
    int top = 55;
    while (top >= 0 && ((frac >> top) & 1u) == 0) top--;

    /* value = frac * 2^(4*hexexp - 312); frac = 1.f * 2^top
     * => unbiased binary exponent E = top + 4*hexexp - 312 */
    int32_t E = top + 4 * (int32_t) hexexp - 312;
    int32_t ebias = E + 1023;

    /* mantissa: align bit `top` to bit 52 (the implicit one), keep 52 below */
    uint64_t m;
    int shift = 52 - top;
    if (shift >= 0)
        m = (frac << shift) & 0xFFFFFFFFFFFFFULL;
    else
        m = (frac >> (-shift)) & 0xFFFFFFFFFFFFFULL;

    if (ebias <= 0) return s << 63;                                  /* underflow -> zero */
    if (ebias >= 0x7FF) return (s << 63) | ((uint64_t) 0x7FF << 52); /* overflow -> Inf */
    return (s << 63) | ((uint64_t) ebias << 52) | m;
}

uint64_t ieee64_to_hfp64(uint64_t ieee)
{
    uint64_t s = (ieee >> 63) & 1u;
    uint32_t e = (uint32_t) ((ieee >> 52) & 0x7FF);
    uint64_t m = ieee & 0xFFFFFFFFFFFFFULL;
    if (e == 0 && m == 0) return s << 63; /* signed zero */
    if (e == 0x7FF) {
        /* Inf/NaN have no HFP form: use the largest HFP magnitude */
        return (s << 63) | ((uint64_t) 0x7F << 56) | 0x00FFFFFFFFFFFFFFULL;
    }

    uint64_t significand = (1ULL << 52) | m; /* 53-bit, bit52 set */
    int32_t E2 = (int32_t) e - 1023 - 52;    /* value = significand * 2^E2 */
    int32_t shift_total = E2 + 312;
    /* choose hexexp = floor(shift_total/4) so residual r in {0,1,2,3} and
     * frac = significand << r keeps the top hex digit nonzero (canonical). */
    int32_t h = shift_total >= 0 ? shift_total / 4 : -(((-shift_total) + 3) / 4);
    int32_t r = shift_total - 4 * h;
    /* r must be in 0..3 */
    uint64_t frac = significand << r;
    if (h < 0) h = 0;
    if (h > 0x7F) {
        return (s << 63) | ((uint64_t) 0x7F << 56) | 0x00FFFFFFFFFFFFFFULL;
    }
    return (s << 63) | ((uint64_t) (uint32_t) h << 56) | (frac & 0x00FFFFFFFFFFFFFFULL);
}

/* ===================== HFP32 <-> IEEE32 (via 64-bit) ===================== */

uint32_t hfp32_to_ieee32(uint32_t hfp)
{
    uint32_t s = (hfp >> 31) & 1u;
    uint32_t hexexp = (hfp >> 24) & 0x7F;
    uint32_t frac24 = hfp & 0x00FFFFFF;
    /* widen to an HFP64 with the same hexexp, fraction in the top 24 bits */
    uint64_t hfp64 = ((uint64_t) s << 63) | ((uint64_t) hexexp << 56) | ((uint64_t) frac24 << 32);
    uint64_t ieee64 = hfp64_to_ieee64(hfp64);
    return ieee64_to_ieee32(ieee64);
}

uint32_t ieee32_to_hfp32(uint32_t ieee)
{
    uint64_t ieee64 = ieee32_to_ieee64(ieee);
    uint64_t hfp64 = ieee64_to_hfp64(ieee64);
    uint32_t s = (uint32_t) ((hfp64 >> 63) & 1u);
    uint32_t hexexp = (uint32_t) ((hfp64 >> 56) & 0x7F);
    uint64_t frac56 = hfp64 & 0x00FFFFFFFFFFFFFFULL;
    /* narrow fraction to 24 bits, round to nearest-even on the dropped 32 bits */
    uint32_t frac24 = (uint32_t) (frac56 >> 32);
    uint32_t rem = (uint32_t) (frac56 & 0xFFFFFFFFu);
    if (rem > 0x80000000u || (rem == 0x80000000u && (frac24 & 1u))) {
        frac24++;
        if (frac24 > 0xFFFFFF) {
            /* carry out of the fraction: shift right one hex digit, bump exp */
            frac24 >>= 4;
            hexexp++;
            if (hexexp > 0x7F) {
                hexexp = 0x7F;
                frac24 = 0xFFFFFF;
            }
        }
    }
    if (frac24 == 0 && hexexp == 0) return s << 31; /* zero */
    return (s << 31) | (hexexp << 24) | (frac24 & 0x00FFFFFF);
}
