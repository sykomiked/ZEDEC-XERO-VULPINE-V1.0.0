/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_simd_rvv.c — RISC-V vector (RVV 1.0) versions of zt_kern.h's kernels,
 * VLEN agnostic (every VLEN gives the same bits). HOSTED ONLY: never linked
 * into a kernel image (those keep the C reference).
 *
 * Build: the kernels are compiled only when this file is built with the V
 * extension (-march=rv64gcv for this file; GCC 13 has no per-function RISC-V
 * target attribute), and zt_simd_rvv() hands them out only when the kernel
 * reports V (Linux AT_HWCAP bit 'V' - 'A'), so the rest of a program can be
 * built for rv64gc and still run on a CPU without V. Built without V, or on
 * another ISA, the file compiles to zt_simd_rvv() returning null.
 *
 * A lane per block (dot products) or per token (batched rows), so no sum is
 * ever split across lanes: strided loads gather element t of every block (or
 * value 2p, 2p + 1 of every even or odd token), VWMACC widens 16 x 16-bit
 * products into the block's exact 32-bit sum, VWMUL forms the exact 64-bit
 * s * scale, and term52 runs per lane with the C reference's integer steps
 * (term52_rvv). The 64-bit terms are added by reductions in another order
 * than the C loop, which cannot change the result: integer addition is
 * associative (and modular in the vector unit). Vector lengths are set with
 * vsetvl from the work left, capped at 32 lanes so the per-lane parameters
 * fit fixed stack arrays; every LMUL is chosen so SEW/LMUL = 32 (e8 mf4,
 * e16 mf2, e32 m1, e64 m2) and one vl serves all of them. */
#include "zt_simd.h"
#include "zt_gguf.h"

#if defined(__riscv) && __riscv_xlen == 64 && defined(__linux__) &&                                \
    defined(__riscv_v_intrinsic) && __riscv_v_intrinsic >= 11000
#    include <riscv_vector.h>
#    include <string.h>
#    include <sys/auxv.h>

#    define VMAX 32u /* lanes per pass at most */

static inline uint32_t ld16(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8);
}

static inline size_t setvl(uint64_t left)
{
    return __riscv_vsetvl_e32m1(left < VMAX ? (size_t) left : VMAX);
}

/* zt_term52(v, k) per lane: with u = v >> clamp(-k, 0, 63) (floor) and
 * t = 2^52 >> clamp(k, 0, 63), the result is 2^52 when u > t, -2^52 when
 * u < -t, else u << clamp(k, 0, 63), which cannot overflow since |u| <= t.
 * For k <= 0 that is the floor shift and the final clamp (t = 2^52); for
 * k > 0 it is the reference's saturation test (t = 0 for k > 52); for
 * k < -62 the shift by 63 gives the sign, as the reference does. The
 * clamps also keep every count below 64, where RVV shifts use only the low
 * six bits. */
static inline vint64m2_t term52_rvv(vint64m2_t v, vint64m2_t k, size_t vl)
{
    vuint64m2_t kp = __riscv_vreinterpret_v_i64m2_u64m2(
        __riscv_vmin_vx_i64m2(__riscv_vmax_vx_i64m2(k, 0, vl), 63, vl));
    vuint64m2_t kn = __riscv_vreinterpret_v_i64m2_u64m2(
        __riscv_vmin_vx_i64m2(__riscv_vmax_vx_i64m2(__riscv_vneg_v_i64m2(k, vl), 0, vl), 63, vl));
    vint64m2_t u = __riscv_vsra_vv_i64m2(v, kn, vl);
    vint64m2_t t = __riscv_vsra_vv_i64m2(__riscv_vmv_v_x_i64m2(ZT_KERN_LIM52, vl), kp, vl);
    vint64m2_t r = __riscv_vsll_vv_i64m2(u, kp, vl);
    r = __riscv_vmerge_vxm_i64m2(r, ZT_KERN_LIM52, __riscv_vmsgt_vv_i64m2_b32(u, t, vl), vl);
    return __riscv_vmerge_vxm_i64m2(
        r, -ZT_KERN_LIM52, __riscv_vmslt_vv_i64m2_b32(u, __riscv_vneg_v_i64m2(t, vl), vl), vl);
}

/* acc[0] + the sum of v's vl lanes */
static inline vint64m1_t rsum_rvv(vint64m2_t v, vint64m1_t acc, size_t vl)
{
    return __riscv_vredsum_vs_i64m2_i64m1(v, acc, vl);
}

/* s[i] += sum over t in [t0, t1) of w[i * wstride + t] x[i * 32 + t]: the
 * exact sums of vl blocks (weights wstride bytes apart, activations 32 values
 * apart), one block per lane */
static inline vint32m1_t bsum_rvv(vint32m1_t s, const int8_t *w, ptrdiff_t wstride,
                                  const int16_t *x, uint32_t t0, uint32_t t1, size_t vl)
{
    for (uint32_t t = t0; t < t1; t++) {
        vint16mf2_t wv = __riscv_vsext_vf2_i16mf2(__riscv_vlse8_v_i8mf4(w + t, wstride, vl), vl);
        vint16mf2_t xv = __riscv_vlse16_v_i16mf2(x + t, 64, vl);
        s = __riscv_vwmacc_vv_i32m1(s, wv, xv, vl);
    }
    return s;
}

static zt_fx dot_raw_rvv(const uint8_t *row, const int16_t *a, const uint8_t *ash, uint32_t nb)
{
    int32_t mv[VMAX];
    int64_t kv[VMAX];
    vint64m1_t acc = __riscv_vmv_s_x_i64m1(0, 1);
    for (uint32_t b = 0; b < nb;) {
        size_t vl = setvl(nb - b);
        const uint8_t *r = row + (uint64_t) b * 34;
        for (size_t i = 0; i < vl; i++) {
            int32_t mm, e;
            zt_kern_f16(ld16(r + 34 * i), &mm, &e);
            mv[i] = mm;
            kv[i] = e + 16 + ash[b + i];
        }
        vint32m1_t s = bsum_rvv(__riscv_vmv_v_x_i32m1(0, vl), (const int8_t *) r + 2, 34,
                                a + (uint64_t) b * 32, 0, 32, vl);
        vint64m2_t v = __riscv_vwmul_vv_i64m2(s, __riscv_vle32_v_i32m1(mv, vl), vl);
        acc = rsum_rvv(term52_rvv(v, __riscv_vle64_v_i64m2(kv, vl), vl), acc, vl);
        b += (uint32_t) vl;
    }
    return zt_kern_round(__riscv_vmv_x_s_i64m1_i64(acc));
}

static zt_fx dot_q8_rvv(const zt_q8_t *w, const int16_t *a, const uint8_t *ash, uint32_t nb)
{
    int32_t mv[VMAX];
    int64_t kv[VMAX];
    vint64m1_t acc = __riscv_vmv_s_x_i64m1(0, 1);
    for (uint32_t b = 0; b < nb;) {
        size_t vl = setvl(nb - b);
        for (size_t i = 0; i < vl; i++) {
            mv[i] = w[b + i].scale;
            kv[i] = (int32_t) ash[b + i] - (int32_t) w[b + i].shift;
        }
        vint32m1_t s = bsum_rvv(__riscv_vmv_v_x_i32m1(0, vl), w[b].q, sizeof(zt_q8_t),
                                a + (uint64_t) b * 32, 0, 32, vl);
        vint64m2_t v = __riscv_vwmul_vv_i64m2(s, __riscv_vle32_v_i32m1(mv, vl), vl);
        acc = rsum_rvv(term52_rvv(v, __riscv_vle64_v_i64m2(kv, vl), vl), acc, vl);
        b += (uint32_t) vl;
    }
    return zt_kern_round(__riscv_vmv_x_s_i64m1_i64(acc));
}

/* ---- K-quant rows in place (zt_kern.h zt_dot_q4k_c / zt_dot_q6k_c) ----
 * Up to KG blocks of 256 weights are unpacked to int8 (sub-block j of block g
 * at 256 g + 32 j), then their sub-blocks' sums are formed a lane each. */
#    define KG (VMAX / 8)

/* Q4_K nibbles (128 bytes) to 256 weights 0..15 */
static inline void unpack_q4k_rvv(const uint8_t *q, int8_t *out)
{
    uint8_t *o = (uint8_t *) out;
    for (uint32_t p = 0; p < 4; p++)
        for (size_t i = 0, vl; i < 32; i += vl) {
            vl = __riscv_vsetvl_e8m1(32 - i);
            vuint8m1_t b = __riscv_vle8_v_u8m1(q + 32 * p + i, vl);
            __riscv_vse8_v_u8m1(o + 64 * p + i, __riscv_vand_vx_u8m1(b, 15, vl), vl);
            __riscv_vse8_v_u8m1(o + 64 * p + 32 + i, __riscv_vsrl_vx_u8m1(b, 4, vl), vl);
        }
}

/* Q6_K block to 256 weights -32..31, in the order of zt_kern_q6k_q */
static inline void unpack_q6k_rvv(const uint8_t *blk, int8_t *out)
{
    uint8_t *o = (uint8_t *) out;
    for (uint32_t h = 0; h < 2; h++)
        for (uint32_t qq = 0; qq < 4; qq++)
            for (size_t i = 0, vl; i < 32; i += vl) {
                vl = __riscv_vsetvl_e8m1(32 - i);
                vuint8m1_t lo = __riscv_vle8_v_u8m1(blk + h * 64 + (qq & 1) * 32 + i, vl);
                lo = qq & 2 ? __riscv_vsrl_vx_u8m1(lo, 4, vl) : __riscv_vand_vx_u8m1(lo, 15, vl);
                vuint8m1_t hi = __riscv_vle8_v_u8m1(blk + 128 + h * 32 + i, vl);
                hi = __riscv_vand_vx_u8m1(__riscv_vsrl_vx_u8m1(hi, 2 * qq, vl), 3, vl);
                vuint8m1_t q = __riscv_vor_vv_u8m1(lo, __riscv_vsll_vx_u8m1(hi, 4, vl), vl);
                __riscv_vse8_v_u8m1(o + h * 128 + qq * 32 + i, __riscv_vsub_vx_u8m1(q, 32, vl), vl);
            }
}

static zt_fx dot_q4k_rvv(const uint8_t *row, const int16_t *a, const uint8_t *ash,
                         const int32_t *asum, uint32_t nb)
{
    uint32_t nk = nb / 8;
    int8_t buf[KG * 256];
    int32_t m1[VMAX], m2[VMAX], u[VMAX];
    int64_t k1[VMAX], k2[VMAX];
    int32_t dm[KG], de[KG], mm[KG], me[KG];
    uint8_t sm[KG][16];
    vint64m1_t acc = __riscv_vmv_s_x_i64m1(0, 1);
    for (uint32_t k = 0; k < nk; k += KG) {
        uint32_t g = nk - k < KG ? nk - k : KG;
        for (uint32_t gi = 0; gi < g; gi++) {
            const uint8_t *blk = row + (uint64_t) (k + gi) * ZT_Q4K_BYTES;
            zt_kern_f16(ld16(blk), &dm[gi], &de[gi]);
            zt_kern_f16(ld16(blk + 2), &mm[gi], &me[gi]);
            zt_kern_q4k_scales(blk + 4, sm[gi]);
            unpack_q4k_rvv(blk + 16, buf + 256 * gi);
        }
        for (uint32_t t0 = 0; t0 < 8 * g;) {
            size_t vl = setvl(8 * g - t0);
            for (size_t i = 0; i < vl; i++) {
                uint32_t bi = t0 + (uint32_t) i, gi = bi / 8, j = bi % 8, b = k * 8 + bi;
                m1[i] = sm[gi][j] * dm[gi];
                k1[i] = de[gi] + 16 + ash[b];
                m2[i] = sm[gi][8 + j] * mm[gi];
                k2[i] = me[gi] + 16 + ash[b];
                u[i] = asum[b];
            }
            vint32m1_t s = bsum_rvv(__riscv_vmv_v_x_i32m1(0, vl), buf + 32 * t0, 32,
                                    a + ((uint64_t) k * 8 + t0) * 32, 0, 32, vl);
            vint64m2_t v1 = __riscv_vwmul_vv_i64m2(s, __riscv_vle32_v_i32m1(m1, vl), vl);
            vint64m2_t v2 = __riscv_vwmul_vv_i64m2(__riscv_vle32_v_i32m1(u, vl),
                                                   __riscv_vle32_v_i32m1(m2, vl), vl);
            vint64m2_t t =
                __riscv_vsub_vv_i64m2(term52_rvv(v1, __riscv_vle64_v_i64m2(k1, vl), vl),
                                      term52_rvv(v2, __riscv_vle64_v_i64m2(k2, vl), vl), vl);
            acc = rsum_rvv(t, acc, vl);
            t0 += (uint32_t) vl;
        }
    }
    return zt_kern_round(__riscv_vmv_x_s_i64m1_i64(acc));
}

static zt_fx dot_q6k_rvv(const uint8_t *row, const int16_t *a, const uint8_t *ash,
                         const int32_t *asum, uint32_t nb)
{
    (void) asum;
    uint32_t nk = nb / 8;
    int8_t buf[KG * 256];
    int32_t m0[VMAX], m1[VMAX];
    int64_t kv[VMAX];
    int32_t dm[KG], de[KG];
    vint64m1_t acc = __riscv_vmv_s_x_i64m1(0, 1);
    for (uint32_t k = 0; k < nk; k += KG) {
        uint32_t g = nk - k < KG ? nk - k : KG;
        for (uint32_t gi = 0; gi < g; gi++) {
            const uint8_t *blk = row + (uint64_t) (k + gi) * ZT_Q6K_BYTES;
            zt_kern_f16(ld16(blk + 208), &dm[gi], &de[gi]);
            unpack_q6k_rvv(blk, buf + 256 * gi);
        }
        for (uint32_t t0 = 0; t0 < 8 * g;) {
            size_t vl = setvl(8 * g - t0);
            for (size_t i = 0; i < vl; i++) {
                uint32_t bi = t0 + (uint32_t) i, gi = bi / 8, j = bi % 8;
                const int8_t *sc =
                    (const int8_t *) (row + (uint64_t) (k + gi) * ZT_Q6K_BYTES + 192);
                m0[i] = sc[2 * j] * dm[gi];
                m1[i] = sc[2 * j + 1] * dm[gi];
                kv[i] = de[gi] + 16 + ash[k * 8 + bi];
            }
            const int8_t *wb = buf + 32 * t0;
            const int16_t *xb = a + ((uint64_t) k * 8 + t0) * 32;
            vint32m1_t s0 = bsum_rvv(__riscv_vmv_v_x_i32m1(0, vl), wb, 32, xb, 0, 16, vl);
            vint32m1_t s1 = bsum_rvv(__riscv_vmv_v_x_i32m1(0, vl), wb, 32, xb, 16, 32, vl);
            vint64m2_t v = __riscv_vwmul_vv_i64m2(s0, __riscv_vle32_v_i32m1(m0, vl), vl);
            v = __riscv_vwmacc_vv_i64m2(v, s1, __riscv_vle32_v_i32m1(m1, vl), vl);
            acc = rsum_rvv(term52_rvv(v, __riscv_vle64_v_i64m2(kv, vl), vl), acc, vl);
            t0 += (uint32_t) vl;
        }
    }
    return zt_kern_round(__riscv_vmv_x_s_i64m1_i64(acc));
}

/* ---- batched rows (zt_kern.h zt_mmb_t): a lane per token ----
 * A tile is TRN rows by MMB_GC groups of 16 tokens; each row's 256 weights of
 * a K-quant block (or 8 Q8_0 blocks) are unpacked to int8 once and used for
 * every token. Lanes hold the even tokens (slots 0..7, zt_kern.h
 * ZT_MMB_SLOT) or the odd ones (slots 8..15): strided loads (every second
 * token) fetch their values 2p and 2p + 1, so the sums come out in the order
 * of the packed exponents and sums. */
#    define TRN    4u
#    define MMB_GC 8u

typedef struct {
    int32_t m0[8], m1[8], mt[8], ek[8], e, em;
} tparam_t;

static inline void unpack_row_rvv(const uint8_t *blk, uint32_t qt, int8_t *w, tparam_t *t)
{
    if (qt == ZT_GGML_Q4_K) {
        int32_t dm, mm;
        zt_kern_f16(ld16(blk), &dm, &t->e);
        zt_kern_f16(ld16(blk + 2), &mm, &t->em);
        t->e += 16;
        t->em += 16;
        uint8_t sm[16];
        zt_kern_q4k_scales(blk + 4, sm);
        for (uint32_t i = 0; i < 8; i++) {
            t->m0[i] = sm[i] * dm;
            t->mt[i] = sm[8 + i] * mm;
        }
        unpack_q4k_rvv(blk + 16, w);
    } else if (qt == ZT_GGML_Q6_K) {
        int32_t dm;
        zt_kern_f16(ld16(blk + 208), &dm, &t->e);
        t->e += 16;
        const int8_t *sc = (const int8_t *) (blk + 192);
        for (uint32_t i = 0; i < 8; i++) {
            t->m0[i] = sc[2 * i] * dm;
            t->m1[i] = sc[2 * i + 1] * dm;
        }
        unpack_q6k_rvv(blk, w);
    } else {
        for (uint32_t i = 0; i < 8; i++, blk += 34) {
            zt_kern_f16(ld16(blk), &t->m0[i], &t->ek[i]);
            t->ek[i] += 16;
            memcpy(w + 32 * i, blk + 2, 32);
        }
    }
}

/* the TRN = 4 rows' sums for pair p (values lo, hi of the lanes' tokens;
 * weights wp of row 0, rows 256 apart): a += lo * w[2p] + hi * w[2p + 1] */
static inline void mac4_rvv(vint16mf2_t lo, vint16mf2_t hi, const int8_t *wp, vint32m1_t *a0,
                            vint32m1_t *a1, vint32m1_t *a2, vint32m1_t *a3, size_t vl)
{
    *a0 = __riscv_vwmacc_vx_i32m1(__riscv_vwmacc_vx_i32m1(*a0, wp[0], lo, vl), wp[1], hi, vl);
    *a1 = __riscv_vwmacc_vx_i32m1(__riscv_vwmacc_vx_i32m1(*a1, wp[256], lo, vl), wp[257], hi, vl);
    *a2 = __riscv_vwmacc_vx_i32m1(__riscv_vwmacc_vx_i32m1(*a2, wp[512], lo, vl), wp[513], hi, vl);
    *a3 = __riscv_vwmacc_vx_i32m1(__riscv_vwmacc_vx_i32m1(*a3, wp[768], lo, vl), wp[769], hi, vl);
}

static inline __attribute__((always_inline)) void mmb_body_rvv(const zt_mmb_t *j, uint32_t r0,
                                                               uint32_t r1, const uint32_t qt)
{
    uint32_t nb = j->cols / 32, nk = nb / 8, ng = (j->B + ZT_MMB_G - 1) / ZT_MMB_G;
    uint64_t kbytes = qt == ZT_GGML_Q4_K   ? ZT_Q4K_BYTES
                      : qt == ZT_GGML_Q6_K ? ZT_Q6K_BYTES
                                           : 8 * 34;
    int8_t w[TRN * 256];
    int64_t acc[TRN][MMB_GC][ZT_MMB_G];
    tparam_t tp[TRN];
    for (uint32_t r = r0; r < r1; r += TRN) {
        uint32_t nr = r1 - r < TRN ? r1 - r : TRN;
        if (nr < TRN) memset(w, 0, sizeof w);
        for (uint32_t g0 = 0; g0 < ng; g0 += MMB_GC) {
            uint32_t gn = ng - g0 < MMB_GC ? ng - g0 : MMB_GC;
            memset(acc, 0, sizeof acc);
            for (uint32_t k = 0; k < nk; k++) {
                for (uint32_t i = 0; i < nr; i++)
                    unpack_row_rvv(j->raw + ((uint64_t) (r + i) * nk + k) * kbytes, qt, w + 256 * i,
                                   &tp[i]);
                for (uint32_t gi = 0; gi < gn; gi++)
                    for (uint32_t sb = 0; sb < 8; sb++) {
                        uint64_t gb = (uint64_t) (g0 + gi) * nb + k * 8 + sb;
                        const int16_t *xb = j->aT + gb * 32 * ZT_MMB_G;
                        const int64_t *hp = j->ashT + gb * ZT_MMB_G;
                        const int64_t *up = j->asumT + gb * ZT_MMB_G;
                        /* slot c0 + i: token 2 (c0 % 8 + i) + c0 / 8 */
                        for (uint32_t c0 = 0; c0 < ZT_MMB_G;) {
                            size_t vl = __riscv_vsetvl_e32m1(8 - c0 % 8);
                            const int16_t *xt = xb + 2 * (2 * (c0 % 8) + c0 / 8);
                            vint32m1_t s0 = __riscv_vmv_v_x_i32m1(0, vl), s1 = s0, s2 = s0, s3 = s0,
                                       u0 = s0, u1 = s0, u2 = s0, u3 = s0;
                            for (uint32_t p = 0; p < 16; p++) {
                                const int16_t *xp = xt + 2 * ZT_MMB_G * p;
                                vint16mf2_t lo = __riscv_vlse16_v_i16mf2(xp, 8, vl);
                                vint16mf2_t hi = __riscv_vlse16_v_i16mf2(xp + 1, 8, vl);
                                const int8_t *wp = w + 32 * sb + 2 * p;
                                if (qt == ZT_GGML_Q6_K && p >= 8)
                                    mac4_rvv(lo, hi, wp, &u0, &u1, &u2, &u3, vl);
                                else
                                    mac4_rvv(lo, hi, wp, &s0, &s1, &s2, &s3, vl);
                            }
                            vint64m2_t hv = __riscv_vle64_v_i64m2(hp + c0, vl);
                            for (uint32_t i = 0; i < nr; i++) {
                                const tparam_t *t = &tp[i];
                                vint32m1_t si = i == 0 ? s0 : i == 1 ? s1 : i == 2 ? s2 : s3;
                                vint32m1_t ui = i == 0 ? u0 : i == 1 ? u1 : i == 2 ? u2 : u3;
                                vint64m2_t v = __riscv_vwmul_vx_i64m2(si, t->m0[sb], vl);
                                if (qt == ZT_GGML_Q6_K) {
                                    v = __riscv_vwmacc_vx_i64m2(v, t->m1[sb], ui, vl);
                                    v = term52_rvv(v, __riscv_vadd_vx_i64m2(hv, t->e, vl), vl);
                                } else if (qt == ZT_GGML_Q4_K) {
                                    vint64m2_t uv = __riscv_vmul_vx_i64m2(
                                        __riscv_vle64_v_i64m2(up + c0, vl), t->mt[sb], vl);
                                    v = __riscv_vsub_vv_i64m2(
                                        term52_rvv(v, __riscv_vadd_vx_i64m2(hv, t->e, vl), vl),
                                        term52_rvv(uv, __riscv_vadd_vx_i64m2(hv, t->em, vl), vl),
                                        vl);
                                } else {
                                    v = term52_rvv(v, __riscv_vadd_vx_i64m2(hv, t->ek[sb], vl), vl);
                                }
                                int64_t *ap = acc[i][gi] + c0;
                                __riscv_vse64_v_i64m2(
                                    ap, __riscv_vadd_vv_i64m2(__riscv_vle64_v_i64m2(ap, vl), v, vl),
                                    vl);
                            }
                            c0 += (uint32_t) vl;
                        }
                    }
            }
            for (uint32_t i = 0; i < nr; i++)
                for (uint32_t gi = 0; gi < gn; gi++)
                    for (uint32_t t = 0; t < ZT_MMB_G; t++) {
                        uint32_t tok = (g0 + gi) * ZT_MMB_G + t;
                        if (tok < j->B)
                            j->y[(uint64_t) tok * j->ystride + r + i] =
                                zt_kern_round(acc[i][gi][ZT_MMB_SLOT(t)]);
                    }
        }
    }
}

static int mmb_rvv(const zt_mmb_t *j, uint32_t r0, uint32_t r1)
{
    if (!j->raw) return 1;
    if (j->qtype == ZT_GGML_Q4_K)
        mmb_body_rvv(j, r0, r1, ZT_GGML_Q4_K);
    else if (j->qtype == ZT_GGML_Q6_K)
        mmb_body_rvv(j, r0, r1, ZT_GGML_Q6_K);
    else if (j->qtype == ZT_GGML_Q8_0 && j->cols % 256 == 0)
        mmb_body_rvv(j, r0, r1, ZT_GGML_Q8_0);
    else
        return 1;
    return 0;
}

/* ---- attention over a Q16 KV cache (zt_kern.h): VWMUL/VWMACC 32 x 32 -> 64 ---- */

static void att_qk_rvv(const int32_t *q, const int32_t *k, uint64_t kstride, uint32_t n,
                       uint32_t hd, int64_t *S)
{
    for (uint32_t i = 0; i < n; i++, k += kstride) {
        vint64m1_t acc = __riscv_vmv_s_x_i64m1(0, 1);
        for (size_t j = 0, vl; j < hd; j += vl) {
            vl = __riscv_vsetvl_e32m1(hd - j);
            vint64m2_t p = __riscv_vwmul_vv_i64m2(__riscv_vle32_v_i32m1(q + j, vl),
                                                  __riscv_vle32_v_i32m1(k + j, vl), vl);
            acc = rsum_rvv(p, acc, vl);
        }
        S[i] = __riscv_vmv_x_s_i64m1_i64(acc);
    }
}

static void att_wv_rvv(const int32_t *w, const int32_t *v, uint64_t vstride, uint32_t n,
                       uint32_t hd, int64_t *acc)
{
    for (size_t j0 = 0, vl; j0 < hd; j0 += vl) {
        vl = __riscv_vsetvl_e64m4(hd - j0);
        vint64m4_t a = __riscv_vle64_v_i64m4(acc + j0, vl);
        const int32_t *vi = v + j0;
        for (uint32_t i = 0; i < n; i++, vi += vstride)
            if (w[i]) a = __riscv_vwmacc_vx_i64m4(a, w[i], __riscv_vle32_v_i32m2(vi, vl), vl);
        __riscv_vse64_v_i64m4(acc + j0, a, vl);
    }
}

static const zt_kern_t K_RVV = {"rvv",       dot_raw_rvv, dot_q8_rvv, 0,          0,
                                dot_q4k_rvv, dot_q6k_rvv, mmb_rvv,    att_qk_rvv, att_wv_rvv,
                                0,           0,           0};

const zt_kern_t *zt_simd_rvv(void)
{
    return (getauxval(AT_HWCAP) & (1ul << ('V' - 'A'))) ? &K_RVV : 0;
}

#else

const zt_kern_t *zt_simd_rvv(void)
{
    return 0;
}

#endif
