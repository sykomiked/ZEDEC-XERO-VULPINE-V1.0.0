/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* alx_z.h — booklet compression (docs/spec/MATRIX.md section 5).
 *
 * The codec writes the LZ4 block format, so any LZ4 block decoder reads
 * it, but the encoder is this file's own: a hash-chain search with one step
 * of lazy matching, integers only, byte-at-a-time reads, no allocation. The
 * same bytes in give the same bytes out on every platform and word size,
 * which a CID over compressed bytes needs. (A different LZ4 encoder may
 * choose other matches; its output still decodes here, but it would be a
 * different block with a different CID.)
 *
 * A compressed booklet is stored as an ALXZ block, big-endian:
 *   header, 64 bytes: "ALXZ", 1, codec (1: LZ4 block), u16 zero,
 *     u32 plain length, u32 payload length, plain CID (36: CIDv1 raw
 *     sha2-256 of the plain booklet), 12 zero
 *   payload
 * The block's own CID is over these stored bytes; the plain CID inside
 * names the content. A booklet that would not shrink is stored bare (its
 * ALXB bytes, plain CID = stored CID), so no stored block is ever larger
 * than its booklet.
 *
 * Freestanding C11: integers only, no allocation. */
#ifndef ZXV_ALX_Z_H
#define ZXV_ALX_Z_H

#include "alx.h"

#define ALX_Z_HEAD     64u
#define ALX_Z_LZ4      1u
#define ALX_Z_MAXPLAIN (1u << 20)
#define ALX_Z_WORK     (65536u + 65536u) /* int32 scratch for the encoder */

/* The worst case of alx_z_lz4 for n input bytes. */
uint32_t alx_z_bound(uint32_t n);
/* LZ4 block encode of in[0..n) into out (cap >= alx_z_bound(n)). work:
 * ALX_Z_WORK int32s. Returns the encoded length, or 0 when n > 1 MiB or
 * cap is too small. n == 0 encodes to one zero byte. */
uint32_t alx_z_lz4(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap, int32_t *work);
/* LZ4 block decode; every read and write is bounds-checked. Returns the
 * decoded length, or -1 for a malformed block or one that does not fit. */
int32_t alx_z_unlz4(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap);

/* Store a booklet: an ALXZ block in out when it is smaller than the
 * booklet, else a copy of the booklet. cap >= len + ALX_Z_HEAD. Returns the
 * stored length (0 on a bad argument). */
uint32_t alx_z_store(const uint8_t *b, uint32_t len, uint8_t *out, uint32_t cap, int32_t *work);
/* Whether a stored block is an ALXZ block. */
bool alx_z_is(const uint8_t *b, uint32_t len);
/* Open an ALXZ block: its header, its payload decoded into out (cap at
 * least the plain length), and the decoded bytes against the plain CID.
 * Returns the plain length, ALX_E_FORM for a malformed block, ALX_E_HASH
 * when the bytes are not the ones the plain CID names. */
int32_t alx_z_open(const uint8_t *b, uint32_t len, uint8_t *out, uint32_t cap);
/* the plain length an ALXZ block declares (0 if it is not one) */
uint32_t alx_z_plain_len(const uint8_t *b, uint32_t len);

#endif
