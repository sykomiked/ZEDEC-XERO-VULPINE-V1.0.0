/* mlkem_ntt.h — ML-KEM-768 (FIPS 203) NTT / polynomial ring arithmetic
 *
 * Z_3329[X]/(X^256+1), the ring ML-KEM operates over. The NTT converts
 * a polynomial into a representation where multiplication is pointwise
 * (over 128 degree-1 pairs), via the number-theoretic analogue of the
 * FFT: 17 is a primitive 256th root of unity mod 3329, and each NTT
 * "layer" applies a discrete phase rotation (multiplication by a power
 * of 17) to a butterfly pair -- the same structural role IPHASE's
 * complex phase rotation plays, generalized to a finite field.
 *
 * Deliberately uses plain 32-bit modular arithmetic with explicit
 * range correction rather than Montgomery/Barrett reduction tricks:
 * this is a low-throughput key-exchange operation (not a hot network
 * path), so auditability is worth far more here than the constant
 * factor Montgomery reduction would save -- consistent with the same
 * choice already made for GHASH in aes256_gcm.c.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef MLKEM_NTT_H
#define MLKEM_NTT_H

#include <stdint.h>

#define MLKEM_N 256
#define MLKEM_Q 3329

typedef struct {
    int16_t coeffs[MLKEM_N];  /* always kept reduced to [0, MLKEM_Q) */
} poly_t;

/* In-place forward NTT (FIPS 203 Algorithm 9): normal -> NTT domain. */
void poly_ntt(poly_t *p);

/* In-place inverse NTT (FIPS 203 Algorithm 10): NTT -> normal domain,
 * including the final scaling by 128^-1 mod 3329. */
void poly_invntt(poly_t *p);

/* Pointwise multiply two NTT-domain polynomials (FIPS 203 Algorithm 12
 * MultiplyNTTs, built from Algorithm 11 BaseCaseMultiply per pair). */
void poly_basemul(const poly_t *a, const poly_t *b, poly_t *out);

/* Coefficient-wise addition / subtraction, mod 3329, result in [0,Q). */
void poly_add(const poly_t *a, const poly_t *b, poly_t *out);
void poly_sub(const poly_t *a, const poly_t *b, poly_t *out);

/* Reduce an arbitrary int32_t into [0, MLKEM_Q). Exposed for testing. */
int16_t mlkem_mod_reduce(int32_t a);

#endif /* MLKEM_NTT_H */
