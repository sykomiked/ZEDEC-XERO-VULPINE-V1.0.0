/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zt_lattice.h — lattice vector quantisers for the tensor engine: E8 for
 * weights, the Leech lattice for research. All integer.
 *
 *   T17 LATTICE QUANTISERS.  Weights can be rounded eight at a time to the
 *       nearest point of the E8 lattice instead of one at a time to a grid.
 *       E8 is the densest packing in 8 dimensions, so at the same density of
 *       points its rounding error is lower than a grid's: normalised second
 *       moment 0.0717 against 0.0833 for the cube, a granular gain of about
 *       0.65 dB. The nearest point is found exactly (Conway and Sloane: best
 *       point of the all-even and of the all-odd coset, each with one
 *       coordinate re-rounded to fix the sum, then the closer of the two).
 *       The codebook is the 26641 points of norm at most 8 (shells of 1, 240,
 *       2160, 6720 and 17520 points), so eight weights cost one 15-bit index,
 *       1.875 bits per weight, plus a scale shared by the block. It is built
 *       at run time in a fixed order (shell, then lexicographic), so the first
 *       1, 241, 2401 and 9121 entries are smaller nested codebooks. A point
 *       that falls outside the ball is pulled in by shrinking the input toward
 *       the origin until its nearest point is inside, then improved by root
 *       steps that stay inside. The block scale is 1.05 x the block's RMS,
 *       tuned on Gaussian data. The Leech lattice (24 dimensions, the densest
 *       there) is decoded exactly by trying all 8192 cosets of the Golay code:
 *       correct, but about 400 times dearer than E8 per weight (about 20 us
 *       per 24 values against 0.05 us per 8 on a desktop core), so it is for
 *       small tensors and experiments.
 *       HONEST LIMITS.  The gain over a well-tuned scalar quantiser at a
 *       comparable rate is real but modest: on Gaussian data with one scale
 *       per 32 weights, test_zt_lattice measures 9.89 dB SNR at 1.875 bits
 *       per weight against 9.59 dB for the best uniform 2-bit scalar
 *       quantiser at 2 bits (the Shannon bound at 1.875 bits is 11.29 dB).
 *       Indices are fixed length, not entropy coded. The pull-in is a
 *       heuristic: it always lands in the codebook, and matched an exhaustive
 *       codebook search on every Gaussian block tested, but it is not proved
 *       to find the nearest codebook point. Inputs saturate (E8 at 62 scales,
 *       Leech at 7500) and scales are capped (ZT_E8_SCALE_MAX,
 *       ZT_LEECH_SCALE_MAX) so every distance is exact in int64. The Leech
 *       decoder is brute force, not a hexacode decoder.
 *       ONE E8.  The roots come from src/e8/e8_lattice.h, the same tables
 *       src/e8 (E8 built from the icosians) reads. Its Gram matrix and the
 *       images of the icosian basis in these doubled coordinates live there,
 *       so the two modules describe one lattice: test_zt_e8 proves the map
 *       from icosian coefficients to doubled coordinates is an isometry that
 *       carries e8_roots() onto these 240 roots (e8_to_coords2 and
 *       e8_from_coords2 in e8.h convert).
 * Freestanding: no libc, no floating point, no 64-bit division. Every buffer
 * is the caller's.
 */
#ifndef ZT_LATTICE_H
#define ZT_LATTICE_H

#include <stdint.h>
#include <stdbool.h>
#include "zt.h"

/* E8, in doubled coordinates: v2 = 2 * point, so
 * 2E8 = { v in Z^8 : all v_i even or all odd, sum(v) = 0 mod 4 }.
 * A point v2 at block scale s stands for the values v2[i] * s / 2. */
#define ZT_E8_DIM       8u
#define ZT_E8_CODEBOOK  26641u    /* points of norm 0, 2, 4, 6, 8 */
#define ZT_E8_SCALE_MAX (1 << 29) /* Q16 8192.0: larger scales are capped */
#define ZT_E8_RMS_GAIN  68813     /* Q16 1.05: scale = 1.05 * block RMS (tuned, see test) */

/* End of each shell in the codebook: entries [end[k-1], end[k]) have true
 * norm 2k (doubled norm 8k). */
#define ZT_E8_SHELL0 1u
#define ZT_E8_SHELL1 241u
#define ZT_E8_SHELL2 2401u
#define ZT_E8_SHELL3 9121u
#define ZT_E8_SHELL4 26641u

/* Nearest point of E8 (scaled by s) to x. Ties go to the even coset, then to
 * the lower coordinate. Inputs beyond +-62 s saturate. Returns false (and the
 * origin) when scale <= 0. */
bool zt_e8_nearest(const zt_fx x[8], zt_fx scale, int8_t out2[8]);
bool zt_e8_is_point(const int8_t v2[8]);
int32_t zt_e8_norm2(const int8_t v2[8]); /* sum v2^2 = 4 * true norm */
/* out[i] = v2[i] * scale / 2, rounded to nearest (halves away from zero). */
void zt_e8_dequantize(const int8_t v2[8], zt_fx scale, zt_fx out[8]);

/* Fill table (ZT_E8_CODEBOOK * 8 bytes) with the codebook, sorted by shell
 * and then lexicographically. Returns the number of points written. */
uint32_t zt_e8_codebook_build(int8_t *table);
/* Index of v2 in the codebook, or -1 if it is not a codebook point. Binary
 * search inside the point's shell. */
int32_t zt_e8_encode(const int8_t *table, const int8_t v2[8]);
bool zt_e8_decode(const int8_t *table, uint32_t index, int8_t v2[8]);
/* Quantise 8 values to a codebook index. If the nearest lattice point is
 * outside the ball, the input is multiplied by alpha in [0, 1), found by a
 * 16-step bisection on the largest alpha whose nearest point is inside; of
 * the inside points met on the way the one closest to the original x is
 * kept. Then a local search steps by whichever of the 240 roots most lowers
 * the distance to x while staying in the ball, until none does. The result
 * is always a codebook point; it is usually, not always, the nearest one
 * (the test measures how often). */
uint16_t zt_e8_quantize(const int8_t *table, const zt_fx x[8], zt_fx scale);
/* RMS of n values (n <= 65536), Q16; and the block scale ZT_E8_RMS_GAIN * RMS
 * (0 for an all-zero block, else at least 1 and at most ZT_E8_SCALE_MAX). */
zt_fx zt_e8_rms(const zt_fx *x, uint32_t n);
zt_fx zt_e8_scale(const zt_fx *x, uint32_t n);

/* The extended binary Golay code [24, 12, 8]: the cyclic code of length 23
 * generated by g(x) = x^11 + x^10 + x^6 + x^5 + x^4 + x^2 + 1 (rows x^i g(x),
 * i = 0..11), with an overall parity bit at bit 23. Codeword i (i < 4096) is
 * the XOR of the rows named by the bits of i. */
uint32_t zt_golay_codeword(uint32_t i);
bool zt_golay_is_codeword(uint32_t w);

/* The Leech lattice in its sqrt(8)-scaled integer form: x in Z^24, all x_i of
 * one parity m; for m = 0 the places with x_i = 2 (mod 4) form a Golay
 * codeword and sum(x) = 0 (mod 8); for m = 1 the places with x_i = 3 (mod 4)
 * form a codeword and sum(x) = 4 (mod 8). Minimal vectors have norm 32.
 * A point x at scale s stands for x[i] * s / 4, so the minimal vectors have
 * norm 2 s^2, the same as E8's roots at the same scale. */
#define ZT_LEECH_DIM       24u
#define ZT_LEECH_SCALE_MAX (1 << 28) /* Q16 4096.0 */

/* Workspace for the decoder (about 42 KB), so nothing large sits on a kernel
 * stack. zt_leech_init fills the codeword list once; the rest is per call. */
typedef struct {
    uint32_t golay[4096];
    int64_t cost[3][2][256]; /* per byte of the codeword and parity m */
    int64_t fix[3][2][256];  /* cheapest +-4 move in that byte */
    uint8_t sum8[3][2][256]; /* sum of the rounded coordinates mod 8 */
    int32_t near[24][4];     /* nearest value of each residue class mod 4 */
    int64_t res[24][4];      /* its residual, in units of scale / 4 / 65536 */
    int64_t delta[24][4];    /* extra cost of the next value of that class */
} zt_leech_work_t;

void zt_leech_init(zt_leech_work_t *w);
/* Exact nearest point of the Leech lattice (scaled by s/4) to x, by brute
 * force over the 8192 cosets: about 8192 * 10 integer operations after a
 * table set-up of a few thousand. Inputs beyond +-7500 s saturate. Returns false
 * (and the origin) when scale <= 0. */
bool zt_leech_nearest(zt_leech_work_t *w, const zt_fx x[24], zt_fx scale, int16_t out[24]);
bool zt_leech_is_point(const int16_t v[24]);
/* out[i] = v[i] * scale / 4, rounded to nearest. */
void zt_leech_dequantize(const int16_t v[24], zt_fx scale, zt_fx out[24]);

#endif /* ZT_LATTICE_H */
