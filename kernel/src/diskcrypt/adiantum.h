/* adiantum.h — Adiantum length-preserving encryption for disk sectors
 *
 * WHY THIS CONSTRUCTION
 * ---------------------
 * A disk sector has no room for a nonce or a tag: 512 bytes in, 512 bytes
 * out. The usual answer is AES-XTS, but ZXV's AES is table-driven, and a
 * table-driven AES running on every sector read leaks key bits through the
 * cache to anything sharing the core. XTS also needs the AES S-box sixty-four
 * times per sector, so a constant-time software AES would be slow.
 *
 * Adiantum (Crowley and Biggers, https://eprint.iacr.org/2018/720) was
 * designed for exactly this case: CPUs with no AES instructions. Each sector
 * is encrypted with
 *   - the XChaCha12 stream cipher over all but the last 16 bytes,
 *   - two passes of an NH + Poly1305 hash, and
 *   - one AES-256 call on a single 16-byte block.
 * It is a wide-block cipher: changing any bit of a sector changes the whole
 * ciphertext sector, which XTS does not give. ChaCha, NH and Poly1305 are
 * add-rotate-xor and multiplication only. The one AES call here computes its
 * S-box arithmetically (an inverse in GF(2^8), then the affine map) instead of
 * looking it up, so nothing in this file indexes memory with secret data.
 *
 * The parameters match the Linux kernel's "adiantum(xchacha12,aes)" with a
 * 32-byte tweak, so a volume could be read by other Adiantum implementations
 * given the same key, and the published test vectors apply unchanged.
 *
 * WHAT IT DOES NOT GIVE
 * ---------------------
 * It is length-preserving, so it cannot detect tampering: a changed
 * ciphertext sector decrypts to random-looking plaintext, not an error. And
 * it is deterministic per (key, tweak): writing the same data to the same
 * sector twice gives the same ciphertext, so an attacker who sees the disk at
 * two times learns which sectors changed. Both are true of every
 * length-preserving disk cipher, XTS included.
 *
 * Freestanding: integer only, no libc, no allocation.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_ADIANTUM_H
#define ZXV_ADIANTUM_H

#include <stdint.h>
#include <stdbool.h>

#define ADIANTUM_KEY_LEN   32u
#define ADIANTUM_TWEAK_LEN 32u
#define ADIANTUM_MIN_LEN   16u

/* ---- AES-256 on one block, constant time (no table lookups) */
typedef struct {
    uint8_t rk[240]; /* 15 round keys */
} aes256_ct_t;

void aes256_ct_init(aes256_ct_t *a, const uint8_t key[32]);
void aes256_ct_encrypt(const aes256_ct_t *a, const uint8_t in[16], uint8_t out[16]);
void aes256_ct_decrypt(const aes256_ct_t *a, const uint8_t in[16], uint8_t out[16]);

/* ---- XChaCha12 (24-byte nonce, 64-bit block counter) */
void xchacha12_xor(const uint8_t key[32], const uint8_t nonce[24], uint64_t counter,
                   const uint8_t *in, uint8_t *out, uint32_t len);

/* ---- NHPoly1305, the Adiantum message hash (not a MAC; never use it as one) */
#define NH_KEY_WORDS       268u /* 1024-byte NH message + 3 extra passes of 16 bytes */
#define NHPOLY1305_KEY_LEN (16u + NH_KEY_WORDS * 4u)

typedef struct {
    uint32_t r[5]; /* clamped Poly1305 r, 26-bit limbs */
} poly1305_key_t;

typedef struct {
    poly1305_key_t poly;
    uint32_t nh[NH_KEY_WORDS];
} nhpoly1305_key_t;

void nhpoly1305_setkey(nhpoly1305_key_t *k, const uint8_t key[NHPOLY1305_KEY_LEN]);
/* The 16-byte hash of `len` bytes (zero-padded to a multiple of 16). */
void nhpoly1305(const nhpoly1305_key_t *k, const uint8_t *msg, uint32_t len, uint8_t out[16]);

/* ---- Adiantum */
typedef struct {
    uint8_t ks[32];        /* K_S, the XChaCha12 key */
    aes256_ct_t aes;       /* K_E */
    poly1305_key_t ktweak; /* K_T */
    nhpoly1305_key_t kmsg; /* K_M and K_N */
} adiantum_t;

void adiantum_init(adiantum_t *c, const uint8_t key[ADIANTUM_KEY_LEN]);
/* Encrypt or decrypt `len` (>= 16) bytes. `out` may equal `in`. Returns
 * false only for a length below 16. */
bool adiantum_encrypt(const adiantum_t *c, const uint8_t tweak[ADIANTUM_TWEAK_LEN],
                      const uint8_t *in, uint8_t *out, uint32_t len);
bool adiantum_decrypt(const adiantum_t *c, const uint8_t tweak[ADIANTUM_TWEAK_LEN],
                      const uint8_t *in, uint8_t *out, uint32_t len);
/* Overwrite every key byte with zero. */
void adiantum_wipe(adiantum_t *c);

#endif /* ZXV_ADIANTUM_H */
