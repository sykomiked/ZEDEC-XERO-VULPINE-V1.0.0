/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* alx_carto_mldsa.h — ML-DSA-65 for cartographers' overlays (alx_carto.h).
 * The seed for a key must come from a real entropy source. With rnd NULL
 * signing is deterministic; fresh rnd per signature resists fault attacks
 * better. Secret keys are the caller's memory and nothing here wipes them. */
#ifndef ZXV_ALX_CARTO_MLDSA_H
#define ZXV_ALX_CARTO_MLDSA_H

#include "alx_carto.h"

#define ALX_MLDSA_SK 4032u

/* alx_verify_fn; ctx unused */
bool alx_mldsa_verify(void *ctx, const uint8_t pk[ALX_IDX_PK], const uint8_t *msg, uint32_t len,
                      const uint8_t sig[ALX_IDX_SIG]);
void alx_mldsa_sign(const uint8_t sk[ALX_MLDSA_SK], const uint8_t *msg, uint32_t len,
                    const uint8_t rnd[32], uint8_t sig[ALX_IDX_SIG]);
void alx_mldsa_keypair(const uint8_t seed[32], uint8_t pk[ALX_IDX_PK], uint8_t sk[ALX_MLDSA_SK]);

#endif
