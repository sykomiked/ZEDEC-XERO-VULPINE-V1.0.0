/* zmedia.h — Tri-Space media codec: lossy compression that is not lossy
 *
 * THE IDEA
 * --------
 * Every lossy codec works by DESTROYING information: it transforms, quantises,
 * and throws the remainder away. The remainder is not noise — it is the exact
 * difference between what you had and what you can get back — and conventional
 * formats simply discard it, which is why "lossy" is a one-way door.
 *
 * Tri-Space gives that remainder somewhere to live:
 *
 *     S+  .zxvc   the quantised transform      -- what you DO get back
 *     S-  .cedez  the discarded remainder      -- the inverse / undo
 *     S0  .cedec  parameters not yet resolved  -- the unresolved remainder
 *
 * Decode S+ alone and you get the ordinary lossy image, small and fast. Apply
 * S- and you get the ORIGINAL BACK, byte for byte. So the same artifact is
 * lossy or lossless depending on which members of the triad you carry — you
 * choose at transmission time rather than at encode time, and the choice is
 * reversible because nothing was destroyed.
 *
 * S- IS A ZXI INVERSE WITNESS
 * ---------------------------
 * The remainder is not stored as an ad-hoc residual format. It is exactly the
 * inverse witness from invproof.h: a sparse XOR delta plus SHA-256 of both
 * endpoints. That means the restoration is not merely performed, it is
 * VERIFIED — zxi_verify re-derives the digests and refuses if applying the undo
 * does not land byte-exactly on the original. A codec that says "lossless"
 * usually asks you to trust it; this one carries a proof, and the proof is the
 * same machinery the triad rules already use for TRI_INV_EXACT.
 *
 * WHAT IS AND IS NOT IMPLEMENTED (do not overstate this)
 * ------------------------------------------------------
 * IMPLEMENTED: an 8x8 integer DCT-II / IDCT, a quality-scaled quantiser,
 * zig-zag scan and run-length coding of the zero runs. That is a real lossy
 * image pipeline of the same family as baseline JPEG's core.
 *
 * NOT IMPLEMENTED: Huffman/arithmetic entropy coding, chroma subsampling,
 * colour transforms, progressive scan, and any interoperability with JPEG,
 * PNG, AV1 or any other standard. This decodes ITS OWN format only. It is not
 * a JPEG decoder and must not be described as one. Reading foreign media still
 * needs real decoders for those formats; that gap is unchanged by this file.
 *
 * Single-component (greyscale/luma) 8-bit. Freestanding: integer only, no
 * libc, no allocation, no float.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV tri-space media slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_ZMEDIA_H
#define ZXV_ZMEDIA_H

#include <stdint.h>
#include <stdbool.h>

#define ZM_BLK        8
#define ZM_MAGIC      0x5A4D4431u   /* 'ZMD1' */
#define ZM_MAX_W      512
#define ZM_MAX_H      512
#define ZM_MAX_PIXELS (ZM_MAX_W * ZM_MAX_H)

typedef enum {
    ZM_OK = 0,
    ZM_ERR_ARGS      = -1,
    ZM_ERR_SIZE      = -2,   /* dimensions unsupported or not 8-aligned    */
    ZM_ERR_SPACE     = -3,   /* output buffer too small                    */
    ZM_ERR_FORMAT    = -4,   /* not a ZMD1 stream / truncated              */
    ZM_ERR_NOT_EXACT = -5    /* S- did not restore the original            */
} zm_result_t;

/* Encode to S+ : the quantised transform. `quality` 1..100 (higher = closer to
 * the original and larger). Returns bytes written, or <0. */
int zm_encode_positive(const uint8_t *pixels, uint32_t w, uint32_t h,
                       uint32_t quality, uint8_t *out, uint32_t max);

/* Decode S+ : the ordinary lossy reconstruction. Returns 0, or <0. */
int zm_decode_positive(const uint8_t *sp, uint32_t splen,
                       uint8_t *pixels, uint32_t w, uint32_t h);

/* Build S- : the discarded remainder, as a ZXI inverse witness that turns the
 * lossy reconstruction back into the original. Returns bytes written, or <0. */
int zm_build_negative(const uint8_t *original, const uint8_t *reconstructed,
                      uint32_t npixels, uint8_t *out, uint32_t max);

/* Build S0 : the parameters this encode did NOT resolve (colour space, gamma,
 * intended display). Declared unresolved rather than silently assumed — an S0
 * carries no capability and cannot act. Returns bytes written, or <0. */
int zm_build_neutral(uint32_t w, uint32_t h, uint32_t quality,
                     uint8_t *out, uint32_t max);

/* Apply S- to a lossy reconstruction and VERIFY the result is byte-exact.
 * Returns ZM_OK only if the restoration is proven, ZM_ERR_NOT_EXACT if the
 * witness does not restore the original. `scratch` needs npixels bytes. */
zm_result_t zm_restore_exact(uint8_t *reconstructed, uint32_t npixels,
                             const uint8_t *sm, uint32_t smlen,
                             uint8_t *scratch, uint32_t scratch_len);

#endif /* ZXV_ZMEDIA_H */
