/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_adapters.h — the four sub-adapters of the adapter house (A1-A4 in
 * zt_adapter_core.h hold for all of them).
 *
 * TENSOR (zt_adapter_tensor.c)
 *   T1  FP32, BF16, FP16 -> Q16.16 by bit manipulation only: the exponent
 *       picks a shift of the 24- (or 11-) bit significand; rounding is
 *       half away from zero; |x| >= 32768 saturates to INT32_MAX / INT32_MIN;
 *       subnormals become 0; NaN becomes 0 and is counted. Error <= 2^-17
 *       inside the range. This is quantisation: it is not invertible.
 *   T2  INT8 x block scale: value = q * scale (Q16.16, saturating), scale per
 *       block of `block` elements. With snap_phi the scale is first replaced
 *       by the nearest phi^n (zt_phi.h), which moves it by up to ~24%.
 *   T3  TILING.  A rows x cols matrix is split recursively: while a part is
 *       larger than 21 x 21, its longer side L is cut at round(L / phi)
 *       (34 -> 21 + 13). Elements travel tile by tile, row-major inside a tile;
 *       drain_out writes them back to row-major positions.
 *   T4  FRAMES.  Three Q16.16 values per frame on S+ (W0, W2, W4); S- W1 =
 *       the first value's position in the tile walk, W3 = -(v0 + v1 + v2)
 *       mod 2^32, so each frame sums to zero and a damaged frame is caught.
 *       drain_out writes an int32_t Q16.16 array (row-major).
 * GRAPHICS (zt_adapter_graphics.c)
 *   G1  S+ = RGBA pixels (bytes R, G, B, A); S- = the pixel XOR the previous
 *       image's pixel (an exact residual: undo is one XOR); two pixels per
 *       frame, W4 = the first pixel's index.
 *   G2  FALSE drains the previous image exactly. GLUT/PARADOX drains a
 *       checkerboard: pixel (x, y) takes the new value when x + y is even and
 *       the previous value when odd, and the optional S0 depth plane is set
 *       to 0 (the screen surface) there. Values are never averaged.
 * AUDIO (zt_adapter_audio.c)
 *   U1  S+ = interleaved stereo int16 (3 sample pairs per frame, L low half);
 *       S- = the side signal (L - R) / 2 of those samples (anti-phase part).
 *   U2  FALSE drains silence; GLUT/PARADOX drains only the side channel
 *       (L = side, R = -side).
 *   U3  evaluate_interference on two data frames detects cancellation:
 *       E(a + b) * 8 < E(a) + E(b) with both active -> GLUT (two signals
 *       that would cancel are held, not summed to silence).
 *   U4  zt_audio_mix sums two streams; on cancellation it keeps them apart
 *       (a's mono on L, b's mono on R) instead of summing. Overflow is folded
 *       back (reflected at full scale, the excess scaled by 1/phi) instead of
 *       clipped: a wavefolder. It adds harmonics, which on the trunk bus land
 *       on other trunks (zt_trunk_bank.h B1).
 * TEXT (zt_adapter_text.c)
 *   X1  An alphabet of n <= 24 symbols; digit 0 is the end mark, so the base
 *       is N = n + 1 <= 25. 168 bits / 36 symbols = 4.67 bits a symbol:
 *       25^36 < 2^168 < 26^36, so 36 symbols fit a raw 21-octet block only
 *       when N <= 25 (zt_text_pack_raw168; byte-identical to swarm_en_pack
 *       for the Enochian alphabet: 23 letters + space + end = 25). A tagged
 *       frame keeps octet 0 for the tag and has 160 payload bits:
 *       21^36 < 2^160 < 22^36, so 36 symbols per tagged frame need N <= 21;
 *       for 22 <= N <= 25 a tagged frame carries 34 (25^34 < 2^160).
 *   X2  Dialectic pairs: claims for A and for not-A (A4). zt_text_answer
 *       writes an answer that states both premises when the state is held.
 */
#ifndef ZT_ADAPTERS_H
#define ZT_ADAPTERS_H

#include "zt_adapter_core.h"

/* ===== tensor ===== */
typedef enum {
    ZT_DTYPE_FP32 = 0,
    ZT_DTYPE_BF16 = 1,
    ZT_DTYPE_FP16 = 2,
    ZT_DTYPE_INT8 = 3
} zt_tensor_dtype_t;

#define ZT_TILE_MAX 21u

typedef struct {
    uint16_t r0, c0, nr, nc;
} zt_tile_t;

typedef struct {
    zt_tensor_dtype_t dtype;
    uint32_t rows, cols;            /* rows * cols elements, each <= 65535 */
    const int32_t *block_scale_q16; /* INT8: one scale per block */
    uint32_t block;                 /* INT8: elements per scale */
    bool snap_phi;                  /* T2 */
    uint32_t nan_count, sat_count;  /* T1 statistics of the last pump */
} zt_tensor_adapter_state_t;

void zt_adapter_tensor_init(zt_adapter_t *a, zt_tensor_adapter_state_t *st, uint8_t trunk);
#define ZT_Q16_NAN 1u
#define ZT_Q16_SAT 2u
int32_t zt_fp32_bits_to_q16(uint32_t bits, uint32_t *flags);
int32_t zt_bf16_bits_to_q16(uint16_t bits, uint32_t *flags);
int32_t zt_fp16_bits_to_q16(uint16_t bits, uint32_t *flags);
/* T3: write up to cap tiles; returns the total count (may exceed cap). */
uint32_t zt_tensor_tile_plan(uint32_t rows, uint32_t cols, zt_tile_t *out, uint32_t cap);
size_t zt_tensor_frames_for(uint32_t elements);

/* ===== graphics ===== */
typedef struct {
    uint32_t width, height;
    const uint8_t *prev; /* previous image, RGBA bytes; NULL = all zero */
    uint8_t *depth;      /* optional S0 plane, one byte per pixel */
} zt_graphics_adapter_state_t;

void zt_adapter_graphics_init(zt_adapter_t *a, zt_graphics_adapter_state_t *st, uint8_t trunk);
/* Fill a rectangle of a w-wide RGBA image whose two semantic layers claim
 * color_a and color_b: TRUE -> a, FALSE -> b, GLUT/PARADOX -> checkerboard. */
void zt_gfx_render_conflict(uint8_t *rgba, uint32_t w, uint32_t x0, uint32_t y0, uint32_t x1,
                            uint32_t y1, uint32_t color_a, uint32_t color_b,
                            zt_truth_state_t truth);

/* ===== audio ===== */
typedef struct {
    uint32_t folds; /* samples folded by zt_audio_mix */
} zt_audio_adapter_state_t;

void zt_adapter_audio_init(zt_adapter_t *a, zt_audio_adapter_state_t *st, uint8_t trunk);
int16_t zt_audio_soft_fold(int32_t x, uint32_t *folds);
/* n stereo pairs (2n int16 each). Returns TRUE (summed) or GLUT (held). */
zt_truth_state_t zt_audio_mix(const int16_t *a, const int16_t *b, size_t n, int16_t *out,
                              uint32_t *folds);

/* ===== text ===== */
#define ZT_TEXT_MAX_ALPHABET 24u

typedef struct {
    char alphabet[ZT_TEXT_MAX_ALPHABET]; /* symbol k is digit k + 1 */
    uint32_t letters;                    /* n */
    uint32_t base;                       /* N = n + 1 */
    uint32_t per_frame;                  /* symbols per tagged frame (X1) */
} zt_text_adapter_state_t;

/* False if the alphabet is empty, too long or repeats a character. */
bool zt_adapter_text_init(zt_adapter_t *a, zt_text_adapter_state_t *st, uint8_t trunk,
                          const char *alphabet, uint32_t letters);
/* Largest k with base^k < 2^bits. */
uint32_t zt_text_symbols_fit(uint32_t base, uint32_t bits);
/* X1 raw 168-bit blocks (no tag): 36 symbols per 21 octets, base <= 25.
 * Returns bytes written / symbols read, or a negative error. */
int zt_text_pack_raw168(const zt_text_adapter_state_t *st, const char *text, size_t len,
                        uint8_t *out, size_t cap);
int zt_text_unpack_raw168(const zt_text_adapter_state_t *st, const uint8_t *in, size_t len,
                          char *out, size_t cap);
/* X2: NUL-terminated answer; returns its length or a negative error. */
int zt_text_answer(const char *premise_a, const char *premise_not_a, zt_truth_state_t truth,
                   char *out, size_t cap);

#endif /* ZT_ADAPTERS_H */
