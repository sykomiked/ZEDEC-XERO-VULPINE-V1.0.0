/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_simd_sve.c — AArch64 SVE versions of zt_kern.h's kernels, vector-length
 * agnostic (any SVE length, 128 to 2048 bits, gives the same bits).
 * HOSTED ONLY: never linked into a kernel image (those keep the C reference).
 *
 * Each function is compiled for SVE with a target attribute, so the file
 * builds with the default AArch64 flags; zt_simd_sve() hands the set out only
 * when the kernel reports SVE (Linux AT_HWCAP & HWCAP_SVE). Elsewhere (other
 * ISAs, other systems, compilers without the ACLE target attribute) the file
 * compiles to zt_simd_sve() returning null. Base SVE only: every step below
 * has an exact base-SVE instruction (SDOT 16 x 16 -> 64, SXTH, shifts by a
 * vector), so SVE2 would add nothing and would exclude SVE-only CPUs.
 *
 * Block sums (dot products): SDOT multiplies 16-bit weights (sign-extended by
 * the load) by the 16-bit activations and adds groups of 4 products into
 * 64-bit lanes (exact). Eight such vectors cover 32 * VL/64 values, that is
 * one 32-value block per 64-bit lane; three levels of UZP1/UZP2 + ADD
 * (pairwise sums over the concatenated vectors) turn them into the blocks'
 * exact sums, block b in lane b, at every vector length and with no
 * horizontal add per block. term52(s * scale, k) is then evaluated per lane
 * with the C reference's integer steps (see term52_sve), and the 64-bit lane
 * sums are added in another order than the C loop, which cannot change the
 * result: integer addition is associative (and modular in the vector unit).
 *
 * Batched rows (mm_batch): a lane per token, as the NEON and x86 tiles; the
 * activations' pair layout gives each token's two values as one 32-bit word,
 * split with SXTH and an arithmetic shift, so MLA by a broadcast weight forms
 * every token's exact block sum. A vector covers min(VL/32, 16) tokens of a
 * 16-token group (the groups are not contiguous in the packed layout). */
#include "zt_simd.h"
#include "zt_gguf.h"

#if defined(__aarch64__) && defined(__linux__) &&                                                  \
    (defined(__ARM_FEATURE_SVE) || (defined(__clang__) && __clang_major__ >= 18) ||                \
     (!defined(__clang__) && defined(__GNUC__) && __GNUC__ >= 13))
#    include <arm_sve.h>
#    include <string.h>
#    include <sys/auxv.h>

#    ifndef HWCAP_SVE
#        define HWCAP_SVE (1ul << 22)
#    endif

#    if defined(__clang__)
#        define SVE __attribute__((target("sve")))
#    else
#        define SVE __attribute__((target("+sve")))
#    endif

/* 64-bit lanes in the longest SVE vector (2048 bits) */
#    define VMAX 32u

static inline uint32_t ld16(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8);
}

/* zt_term52(v, k) per lane: with u = v >> clamp(-k, 0, 63) (floor) and
 * t = 2^52 >> clamp(k, 0, 63), the result is 2^52 when u > t, -2^52 when
 * u < -t, else u << clamp(k, 0, 63), which cannot overflow since |u| <= t.
 * For k <= 0 that is the floor shift and the final clamp (t = 2^52); for
 * k > 0 it is the reference's saturation test (t = 0 for k > 52); for
 * k < -62 the shift by 63 gives the sign, as the reference does. */
static inline SVE svint64_t term52_sve(svbool_t pg, svint64_t v, svint64_t k)
{
    const svint64_t lim = svdup_n_s64(ZT_KERN_LIM52);
    svuint64_t kp = svreinterpret_u64_s64(svmin_n_s64_x(pg, svmax_n_s64_x(pg, k, 0), 63));
    svuint64_t kn =
        svreinterpret_u64_s64(svmin_n_s64_x(pg, svmax_n_s64_x(pg, svneg_s64_x(pg, k), 0), 63));
    svint64_t u = svasr_s64_x(pg, v, kn);
    svint64_t t = svasr_s64_x(pg, lim, kp);
    svint64_t r = svlsl_s64_x(pg, u, kp);
    r = svsel_s64(svcmpgt_s64(pg, u, t), lim, r);
    return svsel_s64(svcmplt_s64(pg, u, svneg_s64_x(pg, t)), svneg_s64_x(pg, lim), r);
}

/* 64-bit lanes holding sums of 4 consecutive products w[i] x[i] */
static inline SVE svint64_t dot4_sve(svbool_t pg, const int8_t *w, const int16_t *x)
{
    return svdot_s64(svdup_n_s64(0), svld1sb_s16(pg, w), svld1_s16(pg, x));
}

/* lane i = a||b lane 2i + lane 2i + 1 */
static inline SVE svint64_t padd_sve(svint64_t a, svint64_t b)
{
    return svadd_s64_x(svptrue_b64(), svuzp1_s64(a, b), svuzp2_s64(a, b));
}

/* Where a pass's values come from: value e (< ne) of the pass is weight
 * w[(e / 32) * stride + e % 32] times activation x[e]. A vector of halfwords
 * must not cross a block in w: true when stride is 32 (contiguous) or when
 * the vector divides a block (see bsrc_init). */
typedef struct {
    const int8_t *w;
    const int16_t *x;
    uint64_t stride, ne;
} bsrc_t;

/* The pass over m blocks of 32 int8 weights stride bytes apart. When a
 * vector of halfwords does not divide a block (VL above 512 bits, or not a
 * power of two), the weights are first copied together into buf. */
static inline SVE bsrc_t bsrc_init(const int8_t *w, uint64_t stride, const int16_t *x, uint32_t m,
                                   int8_t *buf)
{
    bsrc_t s = {w, x, stride, (uint64_t) m * 32};
    if (stride != 32 && 32 % svcnth() != 0) {
        for (uint32_t i = 0; i < m; i++) memcpy(buf + 32 * i, w + i * stride, 32);
        s.w = buf;
        s.stride = 32;
    }
    return s;
}

/* vector j of the pass, as 64-bit lanes of 4-product sums */
static inline SVE svint64_t dj_sve(const bsrc_t *s, uint64_t j)
{
    uint64_t e = j * svcnth();
    if (e >= s->ne) return svdup_n_s64(0);
    return dot4_sve(svwhilelt_b16_u64(e, s->ne), s->w + (e / 32) * s->stride + e % 32, s->x + e);
}

/* lane i = the sum of 16-value group i of vectors j0 .. j0 + 3 */
static inline SVE svint64_t g16_sve(const bsrc_t *s, uint64_t j0)
{
    return padd_sve(padd_sve(dj_sve(s, j0), dj_sve(s, j0 + 1)),
                    padd_sve(dj_sve(s, j0 + 2), dj_sve(s, j0 + 3)));
}

/* lane b = the exact sum of block b of the pass (32 values) */
static inline SVE svint64_t bsum32_sve(const bsrc_t *s)
{
    return padd_sve(g16_sve(s, 0), g16_sve(s, 4));
}

static SVE zt_fx dot_raw_sve(const uint8_t *row, const int16_t *a, const uint8_t *ash, uint32_t nb)
{
    uint32_t n = (uint32_t) svcntd();
    int8_t buf[VMAX * 32];
    int64_t mv[VMAX], kv[VMAX];
    svint64_t acc = svdup_n_s64(0);
    for (uint32_t b = 0; b < nb; b += n) {
        uint32_t m = nb - b < n ? nb - b : n;
        const uint8_t *r = row + (uint64_t) b * 34;
        for (uint32_t i = 0; i < m; i++) {
            int32_t mm, e;
            zt_kern_f16(ld16(r + 34 * i), &mm, &e);
            mv[i] = mm;
            kv[i] = e + 16 + ash[b + i];
        }
        svbool_t pg = svwhilelt_b64_u32(0, m);
        bsrc_t src = bsrc_init((const int8_t *) r + 2, 34, a + (uint64_t) b * 32, m, buf);
        svint64_t s = bsum32_sve(&src);
        svint64_t v = svmul_s64_x(pg, s, svld1_s64(pg, mv));
        acc = svadd_s64_m(pg, acc, term52_sve(pg, v, svld1_s64(pg, kv)));
    }
    return zt_kern_round(svaddv_s64(svptrue_b64(), acc));
}

static SVE zt_fx dot_q8_sve(const zt_q8_t *w, const int16_t *a, const uint8_t *ash, uint32_t nb)
{
    uint32_t n = (uint32_t) svcntd();
    int8_t buf[VMAX * 32];
    int64_t mv[VMAX], kv[VMAX];
    svint64_t acc = svdup_n_s64(0);
    for (uint32_t b = 0; b < nb; b += n) {
        uint32_t m = nb - b < n ? nb - b : n;
        for (uint32_t i = 0; i < m; i++) {
            mv[i] = w[b + i].scale;
            kv[i] = (int32_t) ash[b + i] - (int32_t) w[b + i].shift;
        }
        svbool_t pg = svwhilelt_b64_u32(0, m);
        bsrc_t src = bsrc_init(w[b].q, sizeof(zt_q8_t), a + (uint64_t) b * 32, m, buf);
        svint64_t s = bsum32_sve(&src);
        svint64_t v = svmul_s64_x(pg, s, svld1_s64(pg, mv));
        acc = svadd_s64_m(pg, acc, term52_sve(pg, v, svld1_s64(pg, kv)));
    }
    return zt_kern_round(svaddv_s64(svptrue_b64(), acc));
}

/* ---- K-quant rows in place (zt_kern.h zt_dot_q4k_c / zt_dot_q6k_c) ----
 * A block's 256 weights are unpacked to int8 (sub-block j at 32 j), then the
 * block sums are formed as above. */

/* Q4_K nibbles (128 bytes) to 256 weights 0..15 */
static inline SVE void unpack_q4k_sve(const uint8_t *q, int8_t *out)
{
    uint64_t n = svcntb();
    for (uint32_t p = 0; p < 4; p++)
        for (uint64_t i = 0; i < 32; i += n) {
            svbool_t pg = svwhilelt_b8_u64(i, 32);
            svuint8_t b = svld1_u8(pg, q + 32 * p + i);
            svst1_s8(pg, out + 64 * p + i, svreinterpret_s8_u8(svand_n_u8_x(pg, b, 15)));
            svst1_s8(pg, out + 64 * p + 32 + i, svreinterpret_s8_u8(svlsr_n_u8_x(pg, b, 4)));
        }
}

/* Q6_K block (low nibbles, high bit pairs) to 256 weights -32..31, in the
 * order of zt_kern_q6k_q */
static inline SVE void unpack_q6k_sve(const uint8_t *blk, int8_t *out)
{
    uint64_t n = svcntb();
    for (uint32_t h = 0; h < 2; h++)
        for (uint32_t qq = 0; qq < 4; qq++)
            for (uint64_t i = 0; i < 32; i += n) {
                svbool_t pg = svwhilelt_b8_u64(i, 32);
                svuint8_t lo = svld1_u8(pg, blk + h * 64 + (qq & 1) * 32 + i);
                lo = qq & 2 ? svlsr_n_u8_x(pg, lo, 4) : svand_n_u8_x(pg, lo, 15);
                svuint8_t hi = svld1_u8(pg, blk + 128 + h * 32 + i);
                hi = svand_n_u8_x(pg, svlsr_u8_x(pg, hi, svdup_n_u8((uint8_t) (2 * qq))), 3);
                svuint8_t q = svsub_n_u8_x(pg, svorr_u8_x(pg, lo, svlsl_n_u8_x(pg, hi, 4)), 32);
                svst1_s8(pg, out + h * 128 + qq * 32 + i, svreinterpret_s8_u8(q));
            }
}

/* K-quant blocks per pass: enough for one vector of block sums (VL/64 lanes
 * at most VMAX = 4 blocks of 8) */
#    define KG_MAX (VMAX / 8)

static SVE zt_fx dot_q4k_sve(const uint8_t *row, const int16_t *a, const uint8_t *ash,
                             const int32_t *asum, uint32_t nb)
{
    uint32_t n = (uint32_t) svcntd(), nk = nb / 8, G = n >= 8 ? n / 8 : 1;
    int8_t buf[KG_MAX * 256];
    int64_t m1[VMAX], k1[VMAX], m2[VMAX], k2[VMAX], u[VMAX];
    int32_t dm[KG_MAX], de[KG_MAX], mm[KG_MAX], me[KG_MAX];
    uint8_t sm[KG_MAX][16];
    svint64_t acc = svdup_n_s64(0);
    for (uint32_t k = 0; k < nk; k += G) {
        uint32_t g = nk - k < G ? nk - k : G;
        for (uint32_t gi = 0; gi < g; gi++) {
            const uint8_t *blk = row + (uint64_t) (k + gi) * ZT_Q4K_BYTES;
            zt_kern_f16(ld16(blk), &dm[gi], &de[gi]);
            zt_kern_f16(ld16(blk + 2), &mm[gi], &me[gi]);
            zt_kern_q4k_scales(blk + 4, sm[gi]);
            unpack_q4k_sve(blk + 16, buf + 256 * gi);
        }
        for (uint32_t t0 = 0; t0 < 8 * g; t0 += n) {
            uint32_t m = 8 * g - t0 < n ? 8 * g - t0 : n;
            for (uint32_t i = 0; i < m; i++) {
                uint32_t bi = t0 + i, gi = bi / 8, j = bi % 8, b = k * 8 + bi;
                m1[i] = (int64_t) sm[gi][j] * dm[gi];
                k1[i] = de[gi] + 16 + ash[b];
                m2[i] = (int64_t) sm[gi][8 + j] * mm[gi];
                k2[i] = me[gi] + 16 + ash[b];
                u[i] = asum[b];
            }
            svbool_t pg = svwhilelt_b64_u32(0, m);
            bsrc_t src = bsrc_init(buf + 32 * t0, 32, a + ((uint64_t) k * 8 + t0) * 32, m, 0);
            svint64_t v1 = svmul_s64_x(pg, bsum32_sve(&src), svld1_s64(pg, m1));
            svint64_t v2 = svmul_s64_x(pg, svld1_s64(pg, u), svld1_s64(pg, m2));
            acc = svadd_s64_m(pg, acc, term52_sve(pg, v1, svld1_s64(pg, k1)));
            acc = svsub_s64_m(pg, acc, term52_sve(pg, v2, svld1_s64(pg, k2)));
        }
    }
    return zt_kern_round(svaddv_s64(svptrue_b64(), acc));
}

static SVE zt_fx dot_q6k_sve(const uint8_t *row, const int16_t *a, const uint8_t *ash,
                             const int32_t *asum, uint32_t nb)
{
    (void) asum;
    uint32_t n = (uint32_t) svcntd(), nk = nb / 8, G = n >= 8 ? n / 8 : 1;
    int8_t buf[KG_MAX * 256];
    int64_t m0[VMAX], m1[VMAX], kv[VMAX];
    int32_t dm[KG_MAX], de[KG_MAX];
    svint64_t acc = svdup_n_s64(0);
    for (uint32_t k = 0; k < nk; k += G) {
        uint32_t g = nk - k < G ? nk - k : G;
        for (uint32_t gi = 0; gi < g; gi++) {
            const uint8_t *blk = row + (uint64_t) (k + gi) * ZT_Q6K_BYTES;
            zt_kern_f16(ld16(blk + 208), &dm[gi], &de[gi]);
            unpack_q6k_sve(blk, buf + 256 * gi);
        }
        for (uint32_t t0 = 0; t0 < 8 * g; t0 += n) {
            uint32_t m = 8 * g - t0 < n ? 8 * g - t0 : n;
            for (uint32_t i = 0; i < m; i++) {
                uint32_t bi = t0 + i, gi = bi / 8, j = bi % 8;
                const int8_t *sc =
                    (const int8_t *) (row + (uint64_t) (k + gi) * ZT_Q6K_BYTES + 192);
                m0[i] = (int64_t) sc[2 * j] * dm[gi];
                m1[i] = (int64_t) sc[2 * j + 1] * dm[gi];
                kv[i] = de[gi] + 16 + ash[k * 8 + bi];
            }
            svbool_t pg = svwhilelt_b64_u32(0, m);
            bsrc_t src = bsrc_init(buf + 32 * t0, 32, a + ((uint64_t) k * 8 + t0) * 32, m, 0);
            /* 16-value sums: q0 holds groups 0..VL/64-1, q1 the next; even
             * groups are each block's first 16 weights (sc[2j]), odd the rest */
            svint64_t q0 = g16_sve(&src, 0), q1 = g16_sve(&src, 4);
            svint64_t v = svmul_s64_x(pg, svuzp1_s64(q0, q1), svld1_s64(pg, m0));
            v = svmla_s64_x(pg, v, svuzp2_s64(q0, q1), svld1_s64(pg, m1));
            acc = svadd_s64_m(pg, acc, term52_sve(pg, v, svld1_s64(pg, kv)));
        }
    }
    return zt_kern_round(svaddv_s64(svptrue_b64(), acc));
}

/* ---- batched rows (zt_kern.h zt_mmb_t): a lane per token ----
 * A tile is TRN rows by MMB_GC groups of 16 tokens; each row's 256 weights of
 * a K-quant block (or 8 Q8_0 blocks) are unpacked to int8 once and used for
 * every token. Per pair p, a vector of 32-bit words holds the tokens' values
 * 2p (low half) and 2p + 1 (high half); MLA by the two broadcast weights
 * gives each token's exact block sum in its 32-bit lane. Seen as 64-bit
 * lanes, that vector holds token pairs (2i, 2i + 1): the low halves are the
 * even tokens (slots i, zt_kern.h ZT_MMB_SLOT) and the high halves the odd
 * ones (slots 8 + i), which is the order of the packed exponents and sums. */
#    define TRN    4u
#    define MMB_GC 8u

typedef struct {
    int32_t m0[8], m1[8], mt[8], ek[8], e, em;
} tparam_t;

static inline SVE void unpack_row_sve(const uint8_t *blk, uint32_t qt, int8_t *w, tparam_t *t)
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
        unpack_q4k_sve(blk + 16, w);
    } else if (qt == ZT_GGML_Q6_K) {
        int32_t dm;
        zt_kern_f16(ld16(blk + 208), &dm, &t->e);
        t->e += 16;
        const int8_t *sc = (const int8_t *) (blk + 192);
        for (uint32_t i = 0; i < 8; i++) {
            t->m0[i] = sc[2 * i] * dm;
            t->m1[i] = sc[2 * i + 1] * dm;
        }
        unpack_q6k_sve(blk, w);
    } else {
        for (uint32_t i = 0; i < 8; i++, blk += 34) {
            zt_kern_f16(ld16(blk), &t->m0[i], &t->ek[i]);
            t->ek[i] += 16;
            memcpy(w + 32 * i, blk + 2, 32);
        }
    }
}

/* the TRN = 4 rows' sums for pair p (words xw, weights wp of row 0, rows 256
 * apart): a += low half * w[2p] + high half * w[2p + 1] */
static inline SVE void mac4_sve(svbool_t pg, svint32_t xw, const int8_t *wp, svint32_t *a0,
                                svint32_t *a1, svint32_t *a2, svint32_t *a3)
{
    svint32_t lo = svexth_s32_x(pg, xw), hi = svasr_n_s32_x(pg, xw, 16);
    *a0 = svmla_n_s32_x(pg, svmla_n_s32_x(pg, *a0, lo, wp[0]), hi, wp[1]);
    *a1 = svmla_n_s32_x(pg, svmla_n_s32_x(pg, *a1, lo, wp[256]), hi, wp[257]);
    *a2 = svmla_n_s32_x(pg, svmla_n_s32_x(pg, *a2, lo, wp[512]), hi, wp[513]);
    *a3 = svmla_n_s32_x(pg, svmla_n_s32_x(pg, *a3, lo, wp[768]), hi, wp[769]);
}

static inline SVE __attribute__((always_inline)) void mmb_body_sve(const zt_mmb_t *j, uint32_t r0,
                                                                   uint32_t r1, const uint32_t qt)
{
    uint32_t nb = j->cols / 32, nk = nb / 8, ng = (j->B + ZT_MMB_G - 1) / ZT_MMB_G;
    uint64_t kbytes = qt == ZT_GGML_Q4_K   ? ZT_Q4K_BYTES
                      : qt == ZT_GGML_Q6_K ? ZT_Q6K_BYTES
                                           : 8 * 34;
    uint32_t nt = (uint32_t) svcntw() < ZT_MMB_G ? (uint32_t) svcntw() : ZT_MMB_G;
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
                    unpack_row_sve(j->raw + ((uint64_t) (r + i) * nk + k) * kbytes, qt, w + 256 * i,
                                   &tp[i]);
                for (uint32_t gi = 0; gi < gn; gi++)
                    for (uint32_t sb = 0; sb < 8; sb++) {
                        uint64_t gb = (uint64_t) (g0 + gi) * nb + k * 8 + sb;
                        const int16_t *xb = j->aT + gb * 32 * ZT_MMB_G;
                        const int64_t *hp = j->ashT + gb * ZT_MMB_G;
                        const int64_t *up = j->asumT + gb * ZT_MMB_G;
                        for (uint32_t t0 = 0; t0 < ZT_MMB_G; t0 += nt) {
                            uint32_t c = ZT_MMB_G - t0 < nt ? ZT_MMB_G - t0 : nt, h0 = t0 / 2;
                            svbool_t p16 = svwhilelt_b16_u32(0, 2 * c);
                            svbool_t p32 = svwhilelt_b32_u32(0, c);
                            svbool_t p64 = svwhilelt_b64_u32(0, c / 2);
                            svint32_t s0 = svdup_n_s32(0), s1 = s0, s2 = s0, s3 = s0;
                            svint32_t u0 = s0, u1 = s0, u2 = s0, u3 = s0;
                            for (uint32_t p = 0; p < 16; p++) {
                                svint32_t xw = svreinterpret_s32_s16(
                                    svld1_s16(p16, xb + ((uint64_t) p * ZT_MMB_G + t0) * 2));
                                const int8_t *wp = w + 32 * sb + 2 * p;
                                if (qt == ZT_GGML_Q6_K && p >= 8)
                                    mac4_sve(p32, xw, wp, &u0, &u1, &u2, &u3);
                                else
                                    mac4_sve(p32, xw, wp, &s0, &s1, &s2, &s3);
                            }
                            svint64_t he = svld1_s64(p64, hp + h0),
                                      ho = svld1_s64(p64, hp + 8 + h0);
                            for (uint32_t i = 0; i < nr; i++) {
                                const tparam_t *t = &tp[i];
                                svint64_t sw = svreinterpret_s64_s32(i == 0   ? s0
                                                                     : i == 1 ? s1
                                                                     : i == 2 ? s2
                                                                              : s3);
                                svint64_t ve = svmul_n_s64_x(p64, svextw_s64_x(p64, sw), t->m0[sb]);
                                svint64_t vo =
                                    svmul_n_s64_x(p64, svasr_n_s64_x(p64, sw, 32), t->m0[sb]);
                                if (qt == ZT_GGML_Q6_K) {
                                    svint64_t sw2 = svreinterpret_s64_s32(i == 0   ? u0
                                                                          : i == 1 ? u1
                                                                          : i == 2 ? u2
                                                                                   : u3);
                                    ve = svmla_n_s64_x(p64, ve, svextw_s64_x(p64, sw2), t->m1[sb]);
                                    vo = svmla_n_s64_x(p64, vo, svasr_n_s64_x(p64, sw2, 32),
                                                       t->m1[sb]);
                                    ve = term52_sve(p64, ve, svadd_n_s64_x(p64, he, t->e));
                                    vo = term52_sve(p64, vo, svadd_n_s64_x(p64, ho, t->e));
                                } else if (qt == ZT_GGML_Q4_K) {
                                    svint64_t ue =
                                        svmul_n_s64_x(p64, svld1_s64(p64, up + h0), t->mt[sb]);
                                    svint64_t uo =
                                        svmul_n_s64_x(p64, svld1_s64(p64, up + 8 + h0), t->mt[sb]);
                                    ve = svsub_s64_x(
                                        p64, term52_sve(p64, ve, svadd_n_s64_x(p64, he, t->e)),
                                        term52_sve(p64, ue, svadd_n_s64_x(p64, he, t->em)));
                                    vo = svsub_s64_x(
                                        p64, term52_sve(p64, vo, svadd_n_s64_x(p64, ho, t->e)),
                                        term52_sve(p64, uo, svadd_n_s64_x(p64, ho, t->em)));
                                } else {
                                    ve = term52_sve(p64, ve, svadd_n_s64_x(p64, he, t->ek[sb]));
                                    vo = term52_sve(p64, vo, svadd_n_s64_x(p64, ho, t->ek[sb]));
                                }
                                int64_t *ap = acc[i][gi];
                                svst1_s64(p64, ap + h0,
                                          svadd_s64_x(p64, svld1_s64(p64, ap + h0), ve));
                                svst1_s64(p64, ap + 8 + h0,
                                          svadd_s64_x(p64, svld1_s64(p64, ap + 8 + h0), vo));
                            }
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

static SVE int mmb_sve(const zt_mmb_t *j, uint32_t r0, uint32_t r1)
{
    if (!j->raw) return 1;
    if (j->qtype == ZT_GGML_Q4_K)
        mmb_body_sve(j, r0, r1, ZT_GGML_Q4_K);
    else if (j->qtype == ZT_GGML_Q6_K)
        mmb_body_sve(j, r0, r1, ZT_GGML_Q6_K);
    else if (j->qtype == ZT_GGML_Q8_0 && j->cols % 256 == 0)
        mmb_body_sve(j, r0, r1, ZT_GGML_Q8_0);
    else
        return 1;
    return 0;
}

/* ---- attention over a Q16 KV cache (zt_kern.h): 32-bit values loaded
 * sign-extended to 64-bit lanes, MLA (exact 64-bit products) ---- */

static SVE void att_qk_sve(const int32_t *q, const int32_t *k, uint64_t kstride, uint32_t n,
                           uint32_t hd, int64_t *S)
{
    uint64_t vl = svcntd();
    for (uint32_t i = 0; i < n; i++, k += kstride) {
        svint64_t a = svdup_n_s64(0);
        for (uint64_t j = 0; j < hd; j += vl) {
            svbool_t pg = svwhilelt_b64_u64(j, hd);
            a = svmla_s64_m(pg, a, svld1sw_s64(pg, q + j), svld1sw_s64(pg, k + j));
        }
        S[i] = svaddv_s64(svptrue_b64(), a);
    }
}

static SVE void att_wv_sve(const int32_t *w, const int32_t *v, uint64_t vstride, uint32_t n,
                           uint32_t hd, int64_t *acc)
{
    uint64_t vl = svcntd();
    for (uint64_t j0 = 0; j0 < hd; j0 += vl) {
        svbool_t pg = svwhilelt_b64_u64(j0, hd);
        svint64_t a0 = svld1_s64(pg, acc + j0);
        const int32_t *vi = v + j0;
        for (uint32_t i = 0; i < n; i++, vi += vstride)
            if (w[i]) a0 = svmla_n_s64_m(pg, a0, svld1sw_s64(pg, vi), w[i]);
        svst1_s64(pg, acc + j0, a0);
    }
}

static const zt_kern_t K_SVE = {"sve",       dot_raw_sve, dot_q8_sve, 0,          0,
                                dot_q4k_sve, dot_q6k_sve, mmb_sve,    att_qk_sve, att_wv_sve,
                                0,           0,           0};

const zt_kern_t *zt_simd_sve(void)
{
    return (getauxval(AT_HWCAP) & HWCAP_SVE) ? &K_SVE : 0;
}

#else

const zt_kern_t *zt_simd_sve(void)
{
    return 0;
}

#endif
