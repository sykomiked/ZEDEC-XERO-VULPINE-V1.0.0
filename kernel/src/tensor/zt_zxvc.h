/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zt_zxvc.h — the .zxvc model package: exact integer weights, every part
 * pinned by SHA-256, one signed manifest digest (roadmap 6.6c, M.1 and M.4).
 *
 *   EXACT WEIGHTS.  A float tensor (F32, F16 or BF16) is stored as blocks of
 *       32 integers sharing one binary exponent: value = m * 2^e, with m a
 *       signed 8, 16, 24, 32 or 64-bit integer chosen per block. Every finite
 *       F16, BF16 and F32 value is m * 2^e for some odd m, so the block
 *       exponent is the smallest e in the block and every value is exact as
 *       long as the block's spread fits in 63 bits. A block that does not fit
 *       (or holds a NaN or an infinity) is refused, never rounded. Negative
 *       zeros are kept in a 32-bit mask, so decoding gives back the source
 *       bits, not just the source values. The bit-exact check is exactly
 *       that: decode every block to the source type and compare bytes.
 *       Typical F16 weights need 16-bit integers (the same size as the
 *       source); BF16 weights usually fit 16 bits as well.
 *   NATIVE WEIGHTS.  GGUF quantised tensors (Q8_0, Q4_K, Q6_K, Q4_0, Q5_0,
 *       Q5_1 and the rest) are kept byte for byte: the engine already uses
 *       Q8_0, Q4_K and Q6_K in place in exact integer arithmetic, and
 *       re-quantising them would add a second rounding.
 *   LAYOUT (all little-endian):
 *       header      80 bytes: "ZXVC", version, part count, tensor count,
 *                   flags, table offsets, data offset, file size, signature
 *                   offset and length
 *       parts       ZXVC_PART_BYTES each: name, offset, length, SHA-256
 *       tensors     ZXVC_TENSOR_BYTES each: name, encoding, source type,
 *                   shape, payload offset/length/SHA-256, and the tensor's
 *                   offset/length/SHA-256 in the source file
 *       payloads    parts and tensors, 32-byte aligned
 *       signature   optional: SHA-256 of the signer's ML-DSA-87 public key,
 *                   then the signature over the manifest digest
 *   MANIFEST DIGEST.  SHA-256 of "zxv/zxvc/v1" || header (signature fields
 *       zeroed) || parts table || tensor table. The tables carry the hash of
 *       every payload, so one signature covers every byte of the package.
 *
 * HONEST LIMITS.  Exactness is per block: a block whose values span more
 * than 2^63 (for example 1e-30 next to 1.0 in F32 or BF16) cannot be stored
 * exactly and the conversion stops with ZXVC_ENOTEXACT. F64 and integer
 * tensors are not supported. Decoding an exact tensor is sequential (blocks
 * have variable size). Signing and checking the signature are the caller's
 * job (zxvc_digest gives the message); this file only parses and hashes.
 * Freestanding C11: no libc, no allocation, no floating point.
 */
#ifndef ZT_ZXVC_H
#define ZT_ZXVC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "zt.h"

#define ZXVC_MAGIC         0x4356585Au /* "ZXVC" */
#define ZXVC_VERSION       1u
#define ZXVC_HEADER_BYTES  80u
#define ZXVC_PART_NAME     32u
#define ZXVC_PART_BYTES    80u /* name 32, off 8, len 8, sha 32 */
#define ZXVC_TENSOR_NAME   96u
#define ZXVC_MAX_DIMS      4u
#define ZXVC_TENSOR_BYTES  248u
#define ZXVC_ALIGN         32u
#define ZXVC_BLK           32u
#define ZXVC_BLK_MAX_BYTES (3u + 4u + ZXVC_BLK * 8u)
#define ZXVC_HASH          32u
#define ZXVC_SIG_CTX       "zxv/zxvc/v1"

enum { ZXVC_ENC_NATIVE = 1, ZXVC_ENC_EXACT = 2 };

/* Package flags. */
#define ZXVC_FLAG_FROM_GGUF        1u /* a "gguf-prefix" part rebuilds the source file */
#define ZXVC_FLAG_FROM_SAFETENSORS 2u
#define ZXVC_FLAG_LICENCE_OVERRIDE 4u /* licence not on the allow-list; owner overrode */

enum {
    ZXVC_OK = 0,
    ZXVC_EARG = -1,
    ZXVC_EMAGIC = -2,
    ZXVC_EVERSION = -3,
    ZXVC_ETRUNC = -4,    /* a table or payload runs past the buffer */
    ZXVC_ETYPE = -5,     /* source type not supported for exact encoding */
    ZXVC_ENOTEXACT = -6, /* NaN/inf, or a block spread beyond 63 bits */
    ZXVC_EHASH = -7,     /* a payload does not match its SHA-256 */
    ZXVC_ECORRUPT = -8,  /* a block does not decode to a value of the source type */
    ZXVC_ENOTFOUND = -9,
    ZXVC_ESPACE = -10 /* a caller buffer is too small */
};

typedef struct {
    char name[ZXVC_PART_NAME + 1];
    uint64_t off, len;
    uint8_t sha[ZXVC_HASH];
    const uint8_t *data;
} zxvc_part_t;

typedef struct {
    char name[ZXVC_TENSOR_NAME + 1];
    uint32_t enc;      /* ZXVC_ENC_* */
    uint32_t src_type; /* ZT_GGML_* type of the source */
    uint32_t n_dims;
    uint32_t src_file;            /* which source file (sharded safetensors), 0 for GGUF */
    uint64_t dims[ZXVC_MAX_DIMS]; /* dims[0] is the row length (GGUF order) */
    uint64_t n_elems;
    uint64_t off, len; /* payload in the package */
    uint64_t src_off, src_len;
    uint8_t sha[ZXVC_HASH];
    uint8_t src_sha[ZXVC_HASH];
    const uint8_t *data;
} zxvc_tensor_t;

typedef struct {
    const uint8_t *buf;
    uint64_t size;
    uint32_t version, n_parts, n_tensors, flags;
    uint64_t parts_off, tensors_off, data_off, file_size;
    uint64_t sig_off, sig_len;
} zxvc_t;

/* ---- exact blocks ---- */

/* Bytes per value of an exact-encodable source type (F32 4, F16/BF16 2), or
 * 0 if the type is not one. */
uint32_t zxvc_src_size(uint32_t type);
/* Encode n (1..32) source values (little-endian bytes of `type`) into one
 * block at out (ZXVC_BLK_MAX_BYTES of room). Returns the block's size, or
 * ZXVC_ETYPE / ZXVC_ENOTEXACT / ZXVC_EARG. */
int32_t zxvc_block_encode(uint32_t type, const uint8_t *src, uint32_t n, uint8_t *out);
/* Decode one block of n values back to the source type's bytes. Returns the
 * bytes consumed, or ZXVC_ETRUNC / ZXVC_ECORRUPT / ZXVC_ETYPE. */
int32_t zxvc_block_decode(uint32_t type, const uint8_t *in, uint64_t avail, uint32_t n,
                          uint8_t *src_out);
/* Decode one block of n values to Q16, rounded as zt_f32_to_q16 /
 * zt_f16_to_q16 round the same source values (half away from zero,
 * saturating). Returns the bytes consumed or a negative error. */
int32_t zxvc_block_q16(const uint8_t *in, uint64_t avail, uint32_t n, zt_fx *out);

/* ---- the package ---- */

int32_t zxvc_open(zxvc_t *z, const uint8_t *buf, uint64_t size);
int32_t zxvc_part(const zxvc_t *z, uint32_t i, zxvc_part_t *p);
int32_t zxvc_find_part(const zxvc_t *z, const char *name, zxvc_part_t *p);
int32_t zxvc_tensor(const zxvc_t *z, uint32_t i, zxvc_tensor_t *t);
int32_t zxvc_find_tensor(const zxvc_t *z, const char *name, zxvc_tensor_t *t);
/* The manifest digest (see above): the message a signer signs. */
void zxvc_digest(const zxvc_t *z, uint8_t out[ZXVC_HASH]);
/* Re-hash every part and tensor payload. ZXVC_OK, or ZXVC_EHASH with *bad
 * set to the index (parts first, then tensors: n_parts + tensor index). */
int32_t zxvc_verify_hashes(const zxvc_t *z, uint32_t *bad);
/* Decode a whole tensor back to its source bytes (src_len bytes at out).
 * For a native tensor this is a copy. Check the result against src_sha. */
int32_t zxvc_tensor_source(const zxvc_tensor_t *t, uint8_t *out, uint64_t out_len);
/* Decode a whole exact tensor to Q16 (n_elems values). Native tensors go
 * through zt_gguf_dequant instead. */
int32_t zxvc_tensor_q16(const zxvc_tensor_t *t, zt_fx *out, uint64_t n);

/* ---- writing (used by the host tool; still freestanding) ---- */

/* Fill the header bytes. */
void zxvc_put_header(uint8_t h[ZXVC_HEADER_BYTES], const zxvc_t *z);
void zxvc_put_part(uint8_t e[ZXVC_PART_BYTES], const zxvc_part_t *p);
void zxvc_put_tensor(uint8_t e[ZXVC_TENSOR_BYTES], const zxvc_tensor_t *t);

const char *zxvc_err_name(int32_t code);

#endif /* ZT_ZXVC_H */
