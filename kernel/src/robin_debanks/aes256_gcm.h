/* aes256_gcm.h — Freestanding AES-256-GCM (NIST SP 800-38D / FIPS 197)
 *
 * Self-contained, dependency-free AES-256 block cipher and GCM
 * authenticated-encryption mode. No libc dependency beyond stdint.h.
 * Symmetric-only: per NIST SP 800-208 guidance, AES-256 retains ~128-bit
 * security against Grover's algorithm and is considered quantum-resistant
 * at the symmetric layer. This is NOT a substitute for post-quantum
 * asymmetric primitives (ML-KEM/ML-DSA) where public-key operations are
 * required -- see robin_debanks.h for the hybrid PQ architecture notes.
 *
 * Validated against NIST GCM Test Case 13 (all-zero 256-bit key, all-zero
 * 96-bit IV, empty plaintext/AAD -> tag 530f8afbc74536b9a963b4f1c4cb738b).
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef AES256_GCM_H
#define AES256_GCM_H

#include <stdint.h>
#include <stddef.h>

#define AES256_KEY_LEN   32
#define AES256_BLOCK_LEN 16
#define GCM_IV_LEN       12   /* 96-bit IV, standard GCM case */
#define GCM_TAG_LEN      16

/* Encrypt `len` bytes of plaintext with AES-256-GCM.
 * key:    32-byte key
 * iv:     12-byte nonce (MUST be unique per key; caller-managed counter)
 * aad:    optional additional authenticated data (may be NULL if aad_len==0)
 * out:    ciphertext buffer, same length as plaintext
 * tag:    16-byte authentication tag (output)
 */
void aes256_gcm_encrypt(const uint8_t key[AES256_KEY_LEN],
                         const uint8_t iv[GCM_IV_LEN],
                         const uint8_t *aad, size_t aad_len,
                         const uint8_t *plaintext, size_t len,
                         uint8_t *out,
                         uint8_t tag[GCM_TAG_LEN]);

/* Decrypt and verify. Returns 1 if the tag is valid (and `out` holds the
 * recovered plaintext), 0 if authentication fails (tampered/wrong key). */
int aes256_gcm_decrypt(const uint8_t key[AES256_KEY_LEN],
                        const uint8_t iv[GCM_IV_LEN],
                        const uint8_t *aad, size_t aad_len,
                        const uint8_t *ciphertext, size_t len,
                        const uint8_t tag[GCM_TAG_LEN],
                        uint8_t *out);

#endif /* AES256_GCM_H */
