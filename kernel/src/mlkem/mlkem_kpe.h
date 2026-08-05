/* mlkem_kpe.h — ML-KEM-768 K-PKE layer (FIPS 203 Algorithms 13-15)
 *
 * The underlying public-key encryption scheme ML-KEM is built on.
 * k=3 module rank (ML-KEM-768 specifically).
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef MLKEM_KPE_H
#define MLKEM_KPE_H

#include "mlkem_ntt.h"
#include "mlkem_encode.h"
#include <stdint.h>

#define KPE_K 3
#define KPE_EK_BYTES (384 * KPE_K + 32)                   /* t (12-bit x 256 x k) + rho */
#define KPE_DK_BYTES (384 * KPE_K)                        /* s (12-bit x 256 x k) */
#define KPE_C1_BYTES (KPE_K * MLKEM768_DU * MLKEM_N / 8)  /* 3*10*256/8 = 960 */
#define KPE_C2_BYTES (MLKEM768_DV * MLKEM_N / 8)          /* 4*256/8 = 128 */
#define KPE_CT_BYTES (KPE_C1_BYTES + KPE_C2_BYTES)        /* 1088 */

typedef struct { poly_t v[KPE_K]; } poly_vec_t;

/* d: 32 bytes randomness. Outputs ek (KPE_EK_BYTES) and dk (KPE_DK_BYTES). */
void kpe_keygen(const uint8_t d[32], uint8_t ek[KPE_EK_BYTES], uint8_t dk[KPE_DK_BYTES]);

/* m: 32-byte message. rand: 32 bytes encryption randomness.
 * Output ciphertext c (KPE_CT_BYTES). */
void kpe_encrypt(const uint8_t ek[KPE_EK_BYTES], const uint8_t m[32],
                  const uint8_t rand[32], uint8_t c[KPE_CT_BYTES]);

/* Output recovered message m (32 bytes). */
void kpe_decrypt(const uint8_t dk[KPE_DK_BYTES], const uint8_t c[KPE_CT_BYTES],
                  uint8_t m[32]);

#endif /* MLKEM_KPE_H */
