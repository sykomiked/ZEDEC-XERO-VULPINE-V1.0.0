/* keccak.h — Freestanding Keccak-f[1600] / SHA-3 / SHAKE (FIPS 202)
 *
 * Self-contained, dependency-free. Required by ML-KEM (FIPS 203) for
 * hashing (H = SHA3-256, G = SHA3-512, J = SHAKE256) and pseudorandom
 * byte generation (XOF = SHAKE128, used for matrix A and CBD noise).
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * License: SEL-3.3 (kernel component)
 */
#ifndef KECCAK_H
#define KECCAK_H

#include <stdint.h>
#include <stddef.h>

#define SHA3_256_DIGEST_LEN 32
#define SHA3_512_DIGEST_LEN 64

/* One-shot fixed-output hashes */
void sha3_256(const uint8_t *data, size_t len, uint8_t out[SHA3_256_DIGEST_LEN]);
void sha3_512(const uint8_t *data, size_t len, uint8_t out[SHA3_512_DIGEST_LEN]);

/* Extendable-output functions: absorb one input, squeeze `out_len` bytes. */
void shake128(const uint8_t *data, size_t len, uint8_t *out, size_t out_len);
void shake256(const uint8_t *data, size_t len, uint8_t *out, size_t out_len);

/* Incremental SHAKE128 context, used by ML-KEM's matrix-A generation,
 * which absorbs a short fixed prefix once and squeezes many independent
 * streams (one per matrix entry) without re-absorbing the prefix. */
typedef struct {
    uint64_t state[25];
    uint8_t  buf[168];   /* SHAKE128 rate = 168 bytes */
    size_t   buf_len;
    int      squeezing;
} shake128_ctx_t;

void shake128_init(shake128_ctx_t *ctx);
void shake128_absorb(shake128_ctx_t *ctx, const uint8_t *data, size_t len);
void shake128_squeeze(shake128_ctx_t *ctx, uint8_t *out, size_t out_len);

#endif /* KECCAK_H */
