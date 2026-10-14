/* ecdsa.h — ECDSA signature verification over NIST P-256 and P-384
 *
 * These two curves carry almost every elliptic-curve certificate on the
 * public web (Let's Encrypt E5-E9, Google WE1/WE2, Sectigo E36, the ISRG X2,
 * GTS R3/R4 and DigiCert G3 roots) and the TLS 1.3 schemes
 * ecdsa_secp256r1_sha256 and ecdsa_secp384r1_sha384.
 *
 * Construction
 *   Field and scalar arithmetic: bn.h Montgomery contexts for p and n.
 *   Points: projective (X:Y:Z) with the complete addition formula of Renes,
 *   Costello and Batina, "Complete addition formulas for prime order elliptic
 *   curves" (EUROCRYPT 2016), Algorithm 4 (a = -3). One formula adds,
 *   doubles and handles the point at infinity, so no exceptional case can be
 *   mishandled.
 *   u1*G + u2*Q by Shamir's trick (verification data is public).
 *
 * Strict by design: the signature must be DER (SEQUENCE of two minimal
 * positive INTEGERs, nothing trailing), r and s must lie in [1, n-1], and the
 * public key must be an uncompressed point on the curve. Anything else is
 * rejected, never repaired. Checked against the Wycheproof vectors.
 *
 * Freestanding: integer only, no libc, no allocation. Uses about 4 KB of
 * stack.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_TLS_ECDSA_H
#define ZXV_TLS_ECDSA_H

#include <stdint.h>
#include <stdbool.h>

typedef enum { ECDSA_CURVE_NONE = 0, ECDSA_P256 = 1, ECDSA_P384 = 2 } ecdsa_curve_t;

/* Field size in bytes: 32 or 48 (0 for an unknown curve). */
uint32_t ecdsa_field_len(ecdsa_curve_t curve);

/* Whether `pub` is 0x04 || X || Y with X, Y < p and the point on the curve. */
bool ecdsa_pubkey_valid(ecdsa_curve_t curve, const uint8_t *pub, uint32_t pub_len);

/* Verify a DER signature over a message digest. A digest longer than the
 * group order is truncated to its leftmost bits, as FIPS 186-4 specifies. */
bool ecdsa_verify_digest(ecdsa_curve_t curve, const uint8_t *pub, uint32_t pub_len,
                         const uint8_t *digest, uint32_t digest_len, const uint8_t *sig_der,
                         uint32_t sig_len);

#endif /* ZXV_TLS_ECDSA_H */
