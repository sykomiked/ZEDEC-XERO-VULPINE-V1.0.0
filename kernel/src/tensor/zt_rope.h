/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* zt_rope.h — rotary position encoding (RoPE), integer only.
 *
 *   T20 ROTARY POSITIONS.  Pair i of a head is turned by the angle
 *       pos * base^(-2i/n_rot). Each frequency is held as turns per
 *       position in Q64 (a full turn is 2^64), so the angle at any
 *       position is one wrapping 64-bit multiply: the reduction modulo 2 pi
 *       is free and exact, and precision does not decay at long context.
 *       The frequencies come from base (the GGUF f32 rope.freq_base, bit
 *       for bit) through integer log2 and exp2 to about 2^-55 relative
 *       error. Sine and cosine come from a 1024-step table plus a fourth-
 *       order correction, to within 2 units of Q30. Both pairings exist:
 *       NEOX (i with i + n_rot/2; Qwen2, Qwen3) and interleaved (2i with
 *       2i+1; Llama). Per-frequency divisors (the GGUF rope_freqs tensor,
 *       as Llama 3 uses; or one linear factor for every pair) are applied
 *       exactly as divisions. Dimensions past n_rot are left as they are.
 *       Not covered: YaRN and LongRoPE, which need their own corrections.
 */
#ifndef ZT_ROPE_H
#define ZT_ROPE_H

#include <stdint.h>
#include <stdbool.h>
#include "zt.h"

#define ZT_ROPE_MAX_ROT 512u

typedef struct {
    uint32_t n_rot; /* rotated dimensions (even, <= ZT_ROPE_MAX_ROT) */
    bool neox;
    uint64_t freq[ZT_ROPE_MAX_ROT / 2]; /* turns per position, Q64 */
} zt_rope_t;

/* base_f32: the IEEE single bits of the base (e.g. 0x461C4000 = 10000.0);
 * it must be finite and greater than 1. Returns false on bad arguments. */
bool zt_rope_init(zt_rope_t *r, uint32_t n_rot, uint32_t base_f32, bool neox);
/* Divide frequency i by factors[i] (f32 bits, finite and >= 1/2^20), or every
 * frequency by one factor when n == 1. */
bool zt_rope_divide(zt_rope_t *r, const uint32_t *factors_f32, uint32_t n);

/* sin and cos of phase (turns, Q64) in Q30. */
void zt_rope_sincos(uint64_t phase, int32_t *sin_q30, int32_t *cos_q30);

/* Rotate one head (head_dim >= n_rot values, Q16) in place for position pos. */
void zt_rope_apply(const zt_rope_t *r, zt_fx *x, uint32_t head_dim, uint64_t pos);
/* n_heads heads laid out one after another. */
void zt_rope_apply_heads(const zt_rope_t *r, zt_fx *x, uint32_t n_heads, uint32_t head_dim,
                         uint64_t pos);

#endif /* ZT_ROPE_H */
