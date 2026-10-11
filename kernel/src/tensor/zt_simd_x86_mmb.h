/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_simd_x86_mmb.h — the AVX-512 batched-row tile (zt_kern.h zt_mmb_t),
 * included by zt_simd_x86.c once per instruction choice. Before including,
 * define MMB_NAME (the function), MMB_TGT (its target attribute) and
 * MMB_VNNI (1: VPDPWSSD; 0: VPMADDWD + VPADDD). Both form the same exact
 * 32-bit block sums (no lane can overflow: |sum| < 2^28).
 *
 * Rows [r0, r1) are taken TR at a time and the tokens two groups (32) at a
 * time. For each 256-weight block every row's weights are unpacked once to
 * int16; then for each 32-weight sub-block the TR x 2 tile of 16-lane sums is
 * formed with each activation vector loaded once and each weight pair
 * broadcast once, and the scale terms are added to 64-bit accumulators in
 * slot order (zt_kern.h ZT_MMB_SLOT): the even lanes' products first
 * (VPMULDQ on the sums), then the odd lanes' (VPMULDQ on the sums shifted
 * down 32 bits). */

/* d[2i + g] = row i's pairs [p0, p1) against group g's tokens */
#if MMB_VNNI
#    define MMB_MAC(acc, w, x) _mm512_dpwssd_epi32(acc, w, x)
#else
#    define MMB_MAC(acc, w, x) _mm512_add_epi32(acc, _mm512_madd_epi16(w, x))
#endif
#define MMB_DOT(d, p0, p1)                                                                         \
    do {                                                                                           \
        __m512i d0 = _mm512_setzero_si512(), d1 = d0, d2 = d0, d3 = d0, d4 = d0, d5 = d0, d6 = d0, \
                d7 = d0;                                                                           \
        _Pragma("GCC unroll 16") for (uint32_t p = (p0); p < (p1); p++)                            \
        {                                                                                          \
            __m512i x0 = _mm512_load_si512((const void *) (xa + 32 * p));                          \
            __m512i x1 = _mm512_load_si512((const void *) (xb + 32 * p));                          \
            __m512i w0 = _mm512_set1_epi32(ld32(ws + 2 * p));                                      \
            __m512i w1 = _mm512_set1_epi32(ld32(ws + 256 + 2 * p));                                \
            __m512i w2 = _mm512_set1_epi32(ld32(ws + 512 + 2 * p));                                \
            __m512i w3 = _mm512_set1_epi32(ld32(ws + 768 + 2 * p));                                \
            d0 = MMB_MAC(d0, w0, x0);                                                              \
            d1 = MMB_MAC(d1, w0, x1);                                                              \
            d2 = MMB_MAC(d2, w1, x0);                                                              \
            d3 = MMB_MAC(d3, w1, x1);                                                              \
            d4 = MMB_MAC(d4, w2, x0);                                                              \
            d5 = MMB_MAC(d5, w2, x1);                                                              \
            d6 = MMB_MAC(d6, w3, x0);                                                              \
            d7 = MMB_MAC(d7, w3, x1);                                                              \
        }                                                                                          \
        d[0] = d0, d[1] = d1, d[2] = d2, d[3] = d3, d[4] = d4, d[5] = d5, d[6] = d6, d[7] = d7;    \
    } while (0)

#define MMB_CAT_(a, b) a##b
#define MMB_CAT(a, b)  MMB_CAT_(a, b)
#define MMB_BODY       MMB_CAT(MMB_NAME, _body)

/* inlined once per weight type (qt a constant), so the type tests vanish */
/* the same for one group (xa only): d[2i] */
#define MMB_DOT1(d, p0, p1)                                                                        \
    do {                                                                                           \
        __m512i d0 = _mm512_setzero_si512(), d2 = d0, d4 = d0, d6 = d0;                            \
        _Pragma("GCC unroll 16") for (uint32_t p = (p0); p < (p1); p++)                            \
        {                                                                                          \
            __m512i x0 = _mm512_load_si512((const void *) (xa + 32 * p));                          \
            d0 = MMB_MAC(d0, _mm512_set1_epi32(ld32(ws + 2 * p)), x0);                             \
            d2 = MMB_MAC(d2, _mm512_set1_epi32(ld32(ws + 256 + 2 * p)), x0);                       \
            d4 = MMB_MAC(d4, _mm512_set1_epi32(ld32(ws + 512 + 2 * p)), x0);                       \
            d6 = MMB_MAC(d6, _mm512_set1_epi32(ld32(ws + 768 + 2 * p)), x0);                       \
        }                                                                                          \
        d[0] = d0, d[2] = d2, d[4] = d4, d[6] = d6;                                                \
    } while (0)

static inline __attribute__((always_inline)) MMB_TGT void MMB_BODY(const zt_mmb_t *j, uint32_t r0,
                                                                   uint32_t r1, const uint32_t qt)
{
    uint32_t nb = j->cols / 32, nk = nb / 8, ng = mmb_groups(j);
    uint64_t kbytes = qt == ZT_GGML_Q4_K   ? ZT_Q4K_BYTES
                      : qt == ZT_GGML_Q6_K ? ZT_Q6K_BYTES
                                           : 8 * 34;
    int32_t safe = qt == ZT_GGML_Q4_K   ? ZT_MMB_SAFE_Q4K
                   : qt == ZT_GGML_Q6_K ? ZT_MMB_SAFE_Q6K
                                        : ZT_MMB_SAFE_Q8;
    int16_t w[TR * 256] __attribute__((aligned(64)));
    int64_t acc[TR][MMB_GC][ZT_MMB_G] __attribute__((aligned(64)));
    tparam_t tp[TR];
    for (uint32_t r = r0; r < r1; r += TR) {
        uint32_t nr = r1 - r < TR ? r1 - r : TR;
        if (nr < TR) __builtin_memset(w, 0, sizeof w); /* missing rows: zero weights */
        for (uint32_t g0 = 0; g0 < ng; g0 += MMB_GC) {
            uint32_t gn = ng - g0 < MMB_GC ? ng - g0 : MMB_GC;
            __builtin_memset(acc, 0, sizeof acc);
            for (uint32_t k = 0; k < nk; k++) {
                for (uint32_t i = 0; i < nr; i++) {
                    const uint8_t *blk = j->raw + ((uint64_t) (r + i) * nk + k) * kbytes;
                    if (qt == ZT_GGML_Q4_K)
                        unpack_q4k_512(blk, w + 256 * i, &tp[i]);
                    else if (qt == ZT_GGML_Q6_K)
                        unpack_q6k_512(blk, w + 256 * i, &tp[i]);
                    else
                        unpack_q8_0_512(blk, w + 256 * i, &tp[i]);
                }
                for (uint32_t gi = 0; gi < gn; gi += 2) {
                    uint32_t g2 = gn - gi < 2 ? gn - gi : 2;
                    for (uint32_t sb = 0; sb < 8; sb++) {
                        uint64_t gb = (uint64_t) (g0 + gi) * nb + k * 8 + sb;
                        const int16_t *xa = j->aT + gb * 32 * ZT_MMB_G;
                        const int16_t *xb = xa + (uint64_t) nb * 32 * ZT_MMB_G; /* g2 == 2 */
                        const int16_t *ws = w + 32 * sb;
                        /* s: pairs 0..15 (Q6_K: 0..7), s2: Q6_K pairs 8..15 */
                        __m512i s[2 * TR], s2[2 * TR];
                        if (g2 == 1 && qt == ZT_GGML_Q6_K) {
                            MMB_DOT1(s, 0, 8);
                            MMB_DOT1(s2, 8, 16);
                        } else if (g2 == 1) {
                            MMB_DOT1(s, 0, 16);
                        } else if (qt == ZT_GGML_Q6_K) {
                            MMB_DOT(s, 0, 8);
                            MMB_DOT(s2, 8, 16);
                        } else {
                            MMB_DOT(s, 0, 16);
                        }
                        for (uint32_t g = 0; g < g2; g++) {
                            uint64_t gbg = gb + (uint64_t) g * nb;
                            const int64_t *hp = j->ashT + gbg * ZT_MMB_G;
                            const int64_t *up = j->asumT + gbg * ZT_MMB_G;
                            __m512i h0 = _mm512_load_si512((const void *) hp);
                            __m512i h1 = _mm512_load_si512((const void *) (hp + 8));
                            __m512i u0 = _mm512_load_si512((const void *) up);
                            __m512i u1 = _mm512_load_si512((const void *) (up + 8));
                            int32_t hx = j->ashx[gbg];
                            for (uint32_t i = 0; i < nr; i++) {
                                int64_t *a = acc[i][gi + g];
                                const tparam_t *t = &tp[i];
                                __m512i A0 = _mm512_load_si512((const void *) a);
                                __m512i A1 = _mm512_load_si512((const void *) (a + 8));
                                __m512i sv = s[2 * i + g], so = _mm512_srli_epi64(sv, 32);
                                __m512i M0 = _mm512_set1_epi64(t->m0[sb]);
                                __m512i ve = _mm512_mul_epi32(sv, M0),
                                        vo = _mm512_mul_epi32(so, M0);
                                if (qt == ZT_GGML_Q6_K) {
                                    __m512i M1 = _mm512_set1_epi64(t->m1[sb]);
                                    __m512i s2v = s2[2 * i + g];
                                    ve = _mm512_add_epi64(ve, _mm512_mul_epi32(s2v, M1));
                                    vo = _mm512_add_epi64(
                                        vo, _mm512_mul_epi32(_mm512_srli_epi64(s2v, 32), M1));
                                    term16_512(&ve, &vo, h0, h1, t->e, hx, safe);
                                } else if (qt == ZT_GGML_Q4_K) {
                                    __m512i MT = _mm512_set1_epi64(t->mt[sb]);
                                    __m512i ue = _mm512_mul_epi32(u0, MT);
                                    __m512i uo = _mm512_mul_epi32(u1, MT);
                                    term16_512(&ve, &vo, h0, h1, t->e, hx, safe);
                                    term16_512(&ue, &uo, h0, h1, t->em, hx, safe);
                                    ve = _mm512_sub_epi64(ve, ue);
                                    vo = _mm512_sub_epi64(vo, uo);
                                } else {
                                    term16_512(&ve, &vo, h0, h1, t->ek[sb], hx, safe);
                                }
                                A0 = _mm512_add_epi64(A0, ve);
                                A1 = _mm512_add_epi64(A1, vo);
                                _mm512_store_si512((void *) a, A0);
                                _mm512_store_si512((void *) (a + 8), A1);
                            }
                        }
                    }
                }
            }
            for (uint32_t i = 0; i < nr; i++)
                mmb_store(j, r + i, g0, gn, (const int64_t(*)[ZT_MMB_G]) acc[i]);
        }
    }
}

static MMB_TGT void MMB_NAME(const zt_mmb_t *j, uint32_t r0, uint32_t r1)
{
    if (j->qtype == ZT_GGML_Q4_K)
        MMB_BODY(j, r0, r1, ZT_GGML_Q4_K);
    else if (j->qtype == ZT_GGML_Q6_K)
        MMB_BODY(j, r0, r1, ZT_GGML_Q6_K);
    else
        MMB_BODY(j, r0, r1, ZT_GGML_Q8_0);
}

#undef MMB_BODY
#undef MMB_MAC
#undef MMB_DOT
#undef MMB_DOT1
#undef MMB_NAME
#undef MMB_TGT
#undef MMB_VNNI
