/* bn.h — fixed-size modular arithmetic for signature verification
 *
 * One Montgomery multiplier (CIOS, 32-bit limbs, 32x32->64 products only)
 * serves every modulus the certificate verifier needs: the RSA modulus (2048
 * to 4096 bits) and the P-256 and P-384 field and group orders. Numbers are
 * little-endian arrays of uint32_t; a context fixes the limb count.
 *
 * Scope: this is for VERIFICATION, which works on public data only (keys,
 * signatures, digests). The exponentiation is not constant-time in the
 * exponent and must not be used with a secret exponent.
 *
 * Freestanding: integer only, no libc, no allocation.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_TLS_BN_H
#define ZXV_TLS_BN_H

#include <stdint.h>
#include <stdbool.h>

#define BN_LIMBS_MAX 128u /* 4096 bits */

typedef struct {
    uint32_t n;                 /* limbs in use */
    uint32_t m[BN_LIMBS_MAX];   /* the modulus, odd */
    uint32_t rr[BN_LIMBS_MAX];  /* R^2 mod m, R = 2^(32n) */
    uint32_t one[BN_LIMBS_MAX]; /* R mod m: 1 in Montgomery form */
    uint32_t n0;                /* -m^-1 mod 2^32 */
} bn_mont_t;

/* Set up a context for an odd modulus > 1 given big-endian. Leading zero
 * bytes are ignored. Fails for an even modulus or one wider than 4096 bits. */
bool bn_mont_init(bn_mont_t *ctx, const uint8_t *mod_be, uint32_t len);

/* Number of significant bits in a big-endian byte string (0 for zero). */
uint32_t bn_bits_be(const uint8_t *be, uint32_t len);

/* Load a big-endian value into n limbs. Fails if it does not fit. */
bool bn_from_be(uint32_t *r, uint32_t n, const uint8_t *be, uint32_t len);
/* Store n limbs as exactly `len` big-endian bytes (truncating high limbs or
 * zero-padding as needed; callers size `len` to hold the value). */
void bn_to_be(uint8_t *be, uint32_t len, const uint32_t *a, uint32_t n);

int bn_cmp(const uint32_t *a, const uint32_t *b, uint32_t n);
bool bn_is_zero(const uint32_t *a, uint32_t n);

/* All of the following take inputs already reduced below the modulus. */
void bn_mod_add(const bn_mont_t *ctx, uint32_t *r, const uint32_t *a, const uint32_t *b);
void bn_mod_sub(const bn_mont_t *ctx, uint32_t *r, const uint32_t *a, const uint32_t *b);
/* r = a * b / R mod m. r may alias a or b. */
void bn_mont_mul(const bn_mont_t *ctx, uint32_t *r, const uint32_t *a, const uint32_t *b);
void bn_to_mont(const bn_mont_t *ctx, uint32_t *r, const uint32_t *a);
void bn_from_mont(const bn_mont_t *ctx, uint32_t *r, const uint32_t *a);
/* r = a^e (a and r in Montgomery form; e big-endian, public). */
void bn_mont_pow(const bn_mont_t *ctx, uint32_t *r, const uint32_t *a, const uint8_t *e_be,
                 uint32_t e_len);
/* r = a^-1 for a prime modulus, by Fermat (a and r in Montgomery form). */
void bn_mont_inv_prime(const bn_mont_t *ctx, uint32_t *r, const uint32_t *a);
/* For a < 2m: r = a mod m. */
void bn_reduce_once(const bn_mont_t *ctx, uint32_t *r, const uint32_t *a);

#endif /* ZXV_TLS_BN_H */
