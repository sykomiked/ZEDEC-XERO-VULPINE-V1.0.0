/* digest.h — the hash functions certificate and handshake signatures name
 *
 * One switch over SHA-256 / SHA-384 / SHA-512 so the RSA, ECDSA and X.509
 * code can name a hash by value. SHA-1 and MD5 are deliberately absent: a
 * signature that needs them is refused, not verified.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_TLS_DIGEST_H
#define ZXV_TLS_DIGEST_H

#include <stdint.h>

typedef enum {
    TLS_HASH_NONE = 0,
    TLS_HASH_SHA256 = 1,
    TLS_HASH_SHA384 = 2,
    TLS_HASH_SHA512 = 3
} tls_hash_t;

#define TLS_DIGEST_MAX 64u

/* 32, 48 or 64; 0 for TLS_HASH_NONE or an unknown value. */
uint32_t tls_digest_len(tls_hash_t h);
/* Hash `len` bytes into `out` (tls_digest_len(h) bytes). Returns the length
 * written, 0 for an unknown hash. */
uint32_t tls_digest(tls_hash_t h, const uint8_t *data, uint32_t len, uint8_t *out);
/* Hash the concatenation a || b || c (any of them may be empty). */
uint32_t tls_digest3(tls_hash_t h, const uint8_t *a, uint32_t alen, const uint8_t *b, uint32_t blen,
                     const uint8_t *c, uint32_t clen, uint8_t *out);

#endif /* ZXV_TLS_DIGEST_H */
