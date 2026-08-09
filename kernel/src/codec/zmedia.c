/* zmedia.c — Tri-Space media codec. See zmedia.h.
 *
 * Freestanding: integer only, no libc, no allocation, no float.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV tri-space media slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "zmedia.h"
#include "../invproof/invproof.h"

/* C(u)*cos((2x+1)u*pi/16) in Q12. Hardcoded because the kernel is freestanding
 * and has no float; generated once and checked by the round-trip tests. */
static const int32_t COS[8][8] = {
    {   2896,   2896,   2896,   2896,   2896,   2896,   2896,   2896 },
    {   4017,   3406,   2276,    799,   -799,  -2276,  -3406,  -4017 },
    {   3784,   1567,  -1567,  -3784,  -3784,  -1567,   1567,   3784 },
    {   3406,   -799,  -4017,  -2276,   2276,   4017,    799,  -3406 },
    {   2896,  -2896,  -2896,   2896,   2896,  -2896,  -2896,   2896 },
    {   2276,  -4017,    799,   3406,  -3406,   -799,   4017,  -2276 },
    {   1567,  -3784,   3784,  -1567,  -1567,   3784,  -3784,   1567 },
    {    799,  -2276,   3406,  -4017,   4017,  -3406,   2276,   -799 },};

/* Baseline luminance quantisation table (the classic JPEG Annex K values):
 * coarse for high frequencies, which is where the eye notices least. */
static const uint8_t QBASE[64] = {
    16,11,10,16,24,40,51,61,   12,12,14,19,26,58,60,55,
    14,13,16,24,40,57,69,56,   14,17,22,29,51,87,80,62,
    18,22,37,56,68,109,103,77, 24,35,55,64,81,104,113,92,
    49,64,78,87,103,121,120,101, 72,92,95,98,112,100,103,99
};

static const uint8_t ZIGZAG[64] = {
     0, 1, 8,16, 9, 2, 3,10, 17,24,32,25,18,11, 4, 5,
    12,19,26,33,40,48,41,34, 27,20,13, 6, 7,14,21,28,
    35,42,49,56,57,50,43,36, 29,22,15,23,30,37,44,51,
    58,59,52,45,38,31,39,46, 53,60,61,54,47,55,62,63
};

/* quality 1..100 -> per-coefficient step, same curve JPEG encoders use */
static int32_t qstep(uint32_t quality, uint32_t i) {
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;
    uint32_t scale = (quality < 50) ? (5000u / quality) : (200u - quality * 2u);
    int32_t q = (int32_t)((QBASE[i] * scale + 50u) / 100u);
    if (q < 1) q = 1;
    if (q > 255) q = 255;
    return q;
}

/* forward DCT-II on one 8x8 block; input 0..255, output coefficients */
static void fdct(const uint8_t *src, uint32_t stride, int32_t out[64]) {
    int32_t tmp[64];
    for (int y = 0; y < 8; y++)          /* rows */
        for (int u = 0; u < 8; u++) {
            int64_t s = 0;
            for (int x = 0; x < 8; x++)
                s += (int64_t)((int32_t)src[y * stride + x] - 128) * COS[u][x];
            tmp[y * 8 + u] = (int32_t)(s >> 12);
        }
    for (int u = 0; u < 8; u++)          /* columns */
        for (int v = 0; v < 8; v++) {
            int64_t s = 0;
            for (int y = 0; y < 8; y++)
                s += (int64_t)tmp[y * 8 + u] * COS[v][y];
            out[v * 8 + u] = (int32_t)(s >> 14);   /* >>12 then /4 overall */
        }
}

/* inverse DCT; writes 0..255 clamped */
static void idct(const int32_t in[64], uint8_t *dst, uint32_t stride) {
    int32_t tmp[64];
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            int64_t s = 0;
            for (int u = 0; u < 8; u++)
                s += (int64_t)in[y * 8 + u] * COS[u][x];
            tmp[y * 8 + x] = (int32_t)(s >> 12);
        }
    for (int x = 0; x < 8; x++)
        for (int y = 0; y < 8; y++) {
            int64_t s = 0;
            for (int v = 0; v < 8; v++)
                s += (int64_t)tmp[v * 8 + x] * COS[v][y];
            /* >>12 removes the Q12 COS scale; the extra >>2 is the 1/2 * 1/2
             * of the 2-D inverse DCT. Shifting >>10 here (as an earlier draft
             * did) multiplies by 4 instead of dividing, which saturates the
             * clamp and makes every pixel differ. */
            int32_t p = (int32_t)(s >> 14) + 128;
            if (p < 0) p = 0;
            if (p > 255) p = 255;
            dst[y * stride + x] = (uint8_t)p;
        }
}

static void wr32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}

#define ZM_HDR 16u   /* magic4 w4 h4 quality4 */

int zm_encode_positive(const uint8_t *pixels, uint32_t w, uint32_t h,
                       uint32_t quality, uint8_t *out, uint32_t max) {
    if (!pixels || !out) return ZM_ERR_ARGS;
    if (w == 0 || h == 0 || (w & 7u) || (h & 7u)) return ZM_ERR_SIZE;
    if (w > ZM_MAX_W || h > ZM_MAX_H) return ZM_ERR_SIZE;
    if (max < ZM_HDR) return ZM_ERR_SPACE;

    wr32(out, ZM_MAGIC); wr32(out+4, w); wr32(out+8, h); wr32(out+12, quality);
    uint32_t o = ZM_HDR;

    for (uint32_t by = 0; by < h; by += 8)
        for (uint32_t bx = 0; bx < w; bx += 8) {
            int32_t c[64];
            fdct(pixels + by * w + bx, w, c);
            /* quantise, then zig-zag + run-length the zeros: the whole reason
             * high-frequency detail is cheap to drop is that it quantises to
             * long runs of zero. */
            int16_t q[64];
            for (uint32_t i = 0; i < 64; i++) {
                int32_t s = qstep(quality, i);
                int32_t v = c[ZIGZAG[i]];
                v = (v >= 0) ? (v + s / 2) / s : -((-v + s / 2) / s);
                if (v > 32767) v = 32767;
                if (v < -32768) v = -32768;
                q[i] = (int16_t)v;
            }
            uint32_t i = 0;
            while (i < 64) {
                if (q[i] == 0) {
                    uint32_t run = 0;
                    while (i < 64 && q[i] == 0 && run < 255) { i++; run++; }
                    if (o + 2 > max) return ZM_ERR_SPACE;
                    out[o++] = 0; out[o++] = (uint8_t)run;
                } else {
                    if (o + 3 > max) return ZM_ERR_SPACE;
                    out[o++] = 1;
                    out[o++] = (uint8_t)(q[i] & 0xFF);
                    out[o++] = (uint8_t)((uint16_t)q[i] >> 8);
                    i++;
                }
            }
        }
    return (int)o;
}

int zm_decode_positive(const uint8_t *sp, uint32_t splen,
                       uint8_t *pixels, uint32_t w, uint32_t h) {
    if (!sp || !pixels || splen < ZM_HDR) return ZM_ERR_FORMAT;
    if (rd32(sp) != ZM_MAGIC) return ZM_ERR_FORMAT;
    if (rd32(sp+4) != w || rd32(sp+8) != h) return ZM_ERR_FORMAT;
    uint32_t quality = rd32(sp+12);
    uint32_t o = ZM_HDR;

    for (uint32_t by = 0; by < h; by += 8)
        for (uint32_t bx = 0; bx < w; bx += 8) {
            int16_t q[64];
            uint32_t i = 0;
            while (i < 64) {
                if (o >= splen) return ZM_ERR_FORMAT;
                uint8_t tag = sp[o++];
                if (tag == 0) {
                    if (o >= splen) return ZM_ERR_FORMAT;
                    uint32_t run = sp[o++];
                    if (run == 0 || i + run > 64) return ZM_ERR_FORMAT;
                    while (run--) q[i++] = 0;
                } else {
                    if (o + 2 > splen) return ZM_ERR_FORMAT;
                    uint16_t lo = sp[o++], hi = sp[o++];
                    q[i++] = (int16_t)(lo | (uint16_t)(hi << 8));
                }
            }
            int32_t c[64];
            for (uint32_t k = 0; k < 64; k++) c[k] = 0;
            for (uint32_t k = 0; k < 64; k++)
                c[ZIGZAG[k]] = (int32_t)q[k] * qstep(quality, k);
            idct(c, pixels + by * w + bx, w);
        }
    return ZM_OK;
}

int zm_build_negative(const uint8_t *original, const uint8_t *reconstructed,
                      uint32_t npixels, uint8_t *out, uint32_t max) {
    if (!original || !reconstructed || !out) return ZM_ERR_ARGS;
    /* The remainder IS an inverse witness: it takes the lossy reconstruction
     * back to the original, and carries the digests that let a verifier prove
     * it rather than take our word for it. */
    int n = zxi_build(original, reconstructed, npixels, out, max);
    return (n < 0) ? ZM_ERR_SPACE : n;
}

int zm_build_neutral(uint32_t w, uint32_t h, uint32_t quality,
                     uint8_t *out, uint32_t max) {
    if (!out) return ZM_ERR_ARGS;
    /* S0 records what this encode did NOT decide. Colour space and gamma are
     * genuinely unresolved for a raw single-component buffer, and guessing
     * would be inventing provenance; a policy resolves them later. Plain text,
     * no capability, cannot act. */
    static const char *T =
        "ZXV-TRISPACE-NEUTRAL-1\n"
        "unresolved: colour-space\n"
        "unresolved: transfer-function\n"
        "unresolved: intended-display\n"
        "resolved-by: signed-policy\n";
    uint32_t o = 0;
    for (const char *p = T; *p && o + 1 < max; p++) out[o++] = (uint8_t)*p;
    /* geometry is measured, not unresolved, so it is recorded */
    if (o + 12 > max) return ZM_ERR_SPACE;
    wr32(out+o, w); wr32(out+o+4, h); wr32(out+o+8, quality);
    o += 12;
    return (int)o;
}

zm_result_t zm_restore_exact(uint8_t *reconstructed, uint32_t npixels,
                             const uint8_t *sm, uint32_t smlen,
                             uint8_t *scratch, uint32_t scratch_len) {
    if (!reconstructed || !sm || !scratch) return ZM_ERR_ARGS;
    zxi_result_t r = zxi_verify(sm, smlen, reconstructed, npixels,
                                scratch, scratch_len);
    if (r != ZXI_OK) return ZM_ERR_NOT_EXACT;
    for (uint32_t i = 0; i < npixels; i++) reconstructed[i] = scratch[i];
    return ZM_OK;
}
