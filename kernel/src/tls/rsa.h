/* rsa.h — RSA signature verification: PKCS #1 v1.5 and PSS (RFC 8017)
 *
 * Both are needed. Certificate chains are overwhelmingly signed with
 * PKCS #1 v1.5 (sha256WithRSAEncryption: Let's Encrypt R10-R14, ISRG Root
 * X1, GTS R1, DigiCert G2). TLS 1.3 CertificateVerify forbids v1.5 and uses
 * rsa_pss_rsae_sha256/384/512 instead.
 *
 * Policy, fixed here rather than left to callers:
 *  - modulus 2048 to 4096 bits; smaller keys are refused as too weak;
 *  - public exponent odd, at least 3, at most 32 bits;
 *  - the signature is exactly as long as the modulus and below it;
 *  - v1.5: the whole encoded block is rebuilt and compared, never parsed, so
 *    there is no DigestInfo parser to fool (the Bleichenbacher 2006 class);
 *    the DigestInfo must carry the NULL parameters;
 *  - PSS: MGF1 with the same hash and a salt as long as the digest, which is
 *    what TLS 1.3 and the CA/Browser Forum require.
 *
 * Verification only; works on public data. Freestanding: integer only, no
 * libc, no allocation. Uses about 6 KB of stack.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_TLS_RSA_H
#define ZXV_TLS_RSA_H

#include <stdint.h>
#include <stdbool.h>
#include "digest.h"

#define RSA_MIN_BITS 2048u
#define RSA_MAX_BITS 4096u

/* Whether (n, e) is a key this module will verify with. */
bool rsa_key_acceptable(const uint8_t *n_be, uint32_t n_len, const uint8_t *e_be, uint32_t e_len);

bool rsa_verify_pkcs1(const uint8_t *n_be, uint32_t n_len, const uint8_t *e_be, uint32_t e_len,
                      tls_hash_t hash, const uint8_t *digest, const uint8_t *sig, uint32_t sig_len);

bool rsa_verify_pss(const uint8_t *n_be, uint32_t n_len, const uint8_t *e_be, uint32_t e_len,
                    tls_hash_t hash, const uint8_t *digest, const uint8_t *sig, uint32_t sig_len);

#endif /* ZXV_TLS_RSA_H */
