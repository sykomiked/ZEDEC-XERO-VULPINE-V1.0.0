/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_boot.h — the tensor engine's boot self-check (one half of the [AI_OK]
 * line; swarm_boot.h holds the other half and the combined check).
 *
 * THE FORWARD STEP.  A fixed, tiny slice of one transformer layer, all in
 * the engine's own integer arithmetic (zt.h, zt_rope.h):
 *   x  64 Q16 values x[i] = qx[i] * 512, qx in -127..127 with |qx| = 127 in
 *      each block of 32, so zt_quantize recovers it EXACTLY (scale 512);
 *   W  8 rows of 64 values w[r][i] = qw[r][i] * 256, also exact (scale 256);
 *   y  = zt_matvec(W, x): each block term is sum(qw qx) * 512 * 256 / 2^16,
 *      so y[r] = 2 * sum_i qw[r][i] qx[i] exactly. The self-check recomputes
 *      that sum with a plain integer loop and requires equality;
 *   h  = zt_rmsnorm(y, gain), gain[i] = 1 + i/16 (Q16);
 *   q  = zt_rope_apply(h) at position 5, n_rot 8, base 10000, interleaved;
 *   p  = zt_softmax(q) (required: every p >= 0, sum within 8/65536 of 1);
 *   s  = zt_silu(q[i]); e = zt_exp(-1) (and zt_exp(0) must be exactly 1).
 * ZT_BOOT_HASH is the FNV-1a (zt_boot_fnv) of y, h, q, p, s, e in that
 * order. It is a regression constant, not an accuracy claim: it pins that
 * the bare-metal build computes bit for bit what the host build computes
 * (test_ai_selfcheck asserts the same value on the host). This is a
 * known-answer self-test of the arithmetic, not a trained model running.
 *
 * THE HASH.  FNV-1a, 32-bit, each value fed as the 4 little-endian bytes of
 * its low 32 bits. Defined arithmetically, so it is the same on every byte
 * order.
 *
 * Freestanding: integer only, no libc, no allocation; every buffer is a
 * static of a few KiB (the RoPE table is the largest, about 2 KiB). Depends
 * on zt.c and zt_rope.c only.
 */
#ifndef ZT_BOOT_H
#define ZT_BOOT_H

#include <stdint.h>

#define ZT_BOOT_DIM  64u
#define ZT_BOOT_ROWS 8u
#define ZT_BOOT_POS  5u
#define ZT_BOOT_HASH 0xfbc3385au

#define ZT_BOOT_FNV_INIT 0x811C9DC5u
/* One FNV-1a 32-bit step over the 4 little-endian bytes of v. */
uint32_t zt_boot_fnv(uint32_t h, uint32_t v);

/* Returns 0 on success, or the 1-based number of the first failed check.
 * *hash (may be NULL) receives the hash of what was computed. */
int zt_boot_selfcheck(uint32_t *hash);

#endif /* ZT_BOOT_H */
