/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_metal_math.h — the integer arithmetic of the Metal kernels (zt_metal.m),
 * written once in the subset that is both C11 and Metal Shading Language.
 *
 * Every function here is a port of zt_kern.h's C reference, step for step:
 * the same exact 32-bit block sums, the same term52 of the same 64-bit
 * values, so a sum of them in any order is what zt_dot_*_c sums. One source
 * serves two builds:
 *   - zt_metal.m stringifies it (ZT_METAL_SRC(...) -> "...") into the shader
 *     it compiles at run time, after a prelude that defines the types below
 *     and ZM_DEV as Metal's `device` address space;
 *   - test_zt_metal_math.c compiles it as C (ZT_METAL_SRC(...) -> ...), and
 *     checks it against zt_kern.h on any host, with or without a GPU.
 * Rules for the body: no preprocessor lines and no `#` (it is stringified),
 * pointers only as ZM_DEV const, types zl (int64) zi (int32) zu (uint32)
 * zs (int16) zb (uint8) zc (int8). Not a header to include anywhere else. */

ZT_METAL_SRC(

static inline zl zm_term52(zl v, zi e)
{
    const zl lim = ((zl) 1) << 52;
    if (v == 0) return 0;
    if (e <= 0) {
        if (e < -62) return v < 0 ? -1 : 0;
        v >>= -e;
    } else {
        if (e > 52 || v > (lim >> e) || v < -(lim >> e)) return v < 0 ? -lim : lim;
        v *= ((zl) 1) << e;
    }
    return v > lim ? lim : v < -lim ? -lim : v;
}

static inline zi zm_round(zl acc)
{
    zl v = (acc + 32768) >> 16;
    return (zi) (v > 2147483647 ? 2147483647 : v < -2147483647 - 1 ? -2147483647 - 1 : v);
}

static inline zi zm_f16_m(zu d)
{
    zu ex = (d >> 10) & 31u;
    zi m = (zi) (d & 0x3FFu);
    if (ex == 31u) return 0;
    if (ex) m |= 0x400;
    return (d & 0x8000u) ? -m : m;
}

static inline zi zm_f16_e(zu d)
{
    zu ex = (d >> 10) & 31u;
    if (ex == 31u) return 0;
    return ex ? (zi) ex - 25 : -24;
}

static inline zu zm_u16(ZM_DEV const zb *p)
{
    return (zu) p[0] | ((zu) p[1] << 8);
}

static inline zi zm_dot32_q4(ZM_DEV const zb *q, zu hi, ZM_DEV const zs *x)
{
    zi s = 0;
    for (zu i = 0; i < 32u; i++) s += (zi) ((q[i] >> hi) & 15u) * (zi) x[i];
    return s;
}

static inline zl zm_q4k_sub(ZM_DEV const zb *blk, zu j, ZM_DEV const zs *x, zi sh, zi asum)
{
    zu d = zm_u16(blk), dmin = zm_u16(blk + 2);
    ZM_DEV const zb *p = blk + 4;
    zu sc, mn;
    if (j < 4u) {
        sc = p[j] & 63u;
        mn = p[j + 4u] & 63u;
    } else {
        sc = (p[j + 4u] & 15u) | ((p[j - 4u] >> 6) << 4);
        mn = (p[j + 4u] >> 4) | ((p[j] >> 6) << 4);
    }
    zi s = zm_dot32_q4(blk + 16u + (j / 2u) * 32u, (j & 1u) * 4u, x);
    return zm_term52((zl) s * (zl) sc * (zl) zm_f16_m(d), zm_f16_e(d) + 16 + sh) -
           zm_term52((zl) asum * (zl) mn * (zl) zm_f16_m(dmin), zm_f16_e(dmin) + 16 + sh);
}

static inline zl zm_q6k_sub(ZM_DEV const zb *blk, zu j, ZM_DEV const zs *x, zi sh)
{
    zu h = j / 4u, quarter = j % 4u;
    ZM_DEV const zb *ql = blk + h * 64u + (quarter & 1u) * 32u;
    ZM_DEV const zb *qh = blk + 128u + h * 32u;
    zu s4 = (quarter & 2u) ? 4u : 0u, s2 = 2u * quarter;
    zi s0 = 0, s1 = 0;
    for (zu l = 0; l < 16u; l++) {
        s0 += ((zi) (((ql[l] >> s4) & 15u) | (((qh[l] >> s2) & 3u) << 4)) - 32) * (zi) x[l];
        s1 += ((zi) (((ql[l + 16u] >> s4) & 15u) | (((qh[l + 16u] >> s2) & 3u) << 4)) - 32) *
              (zi) x[l + 16u];
    }
    zu d = zm_u16(blk + 208);
    zl v = (zl) (zc) blk[192u + 2u * j] * (zl) s0 + (zl) (zc) blk[193u + 2u * j] * (zl) s1;
    return zm_term52(v * (zl) zm_f16_m(d), zm_f16_e(d) + 16 + sh);
}

static inline zl zm_q80_sub(ZM_DEV const zb *blk, ZM_DEV const zs *x, zi sh)
{
    zi s = 0;
    for (zu i = 0; i < 32u; i++) s += (zi) (zc) blk[2u + i] * (zi) x[i];
    zu d = zm_u16(blk);
    return zm_term52((zl) s * (zl) zm_f16_m(d), zm_f16_e(d) + 16 + sh);
}

/* Activation sub-block b (32 values) of row r's weights, for the weight type
 * qt (12 Q4_K, 14 Q6_K, 8 Q8_0): the term it adds to the row's Q32 sum. */
static inline zl zm_sub(zu qt, ZM_DEV const zb *row, zu b, ZM_DEV const zs *x, zi sh, zi asum)
{
    if (qt == 12u) return zm_q4k_sub(row + (b / 8u) * 144u, b % 8u, x, sh, asum);
    if (qt == 14u) return zm_q6k_sub(row + (b / 8u) * 210u, b % 8u, x, sh);
    return zm_q80_sub(row + b * 34u, x, sh);
}

static inline zu zm_row_bytes(zu qt, zu nb)
{
    return qt == 12u ? (nb / 8u) * 144u : qt == 14u ? (nb / 8u) * 210u : nb * 34u;
}

)
