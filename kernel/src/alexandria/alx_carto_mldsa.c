/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* alx_carto_mldsa.c — binds cartographers' overlay signatures to ML-DSA-65
 * (FIPS 204, src/pqsec/pq_security.h) with the context
 * "zxv.alexandria.index.v1", which separates them from every other use of
 * the same key. The only Alexandria file that includes pq_security.h. */
#include "alx_carto_mldsa.h"
#include "../pqsec/pq_security.h"

bool alx_mldsa_verify(void *ctx, const uint8_t pk[ALX_IDX_PK], const uint8_t *msg, uint32_t len,
                      const uint8_t sig[ALX_IDX_SIG])
{
    (void) ctx;
    return pq_mldsa65_verify(pk, msg, len, (const uint8_t *) ALX_IDX_CTX, sizeof ALX_IDX_CTX - 1u,
                             sig);
}

void alx_mldsa_sign(const uint8_t sk[ALX_MLDSA_SK], const uint8_t *msg, uint32_t len,
                    const uint8_t rnd[32], uint8_t sig[ALX_IDX_SIG])
{
    pq_mldsa65_sign(sk, msg, len, (const uint8_t *) ALX_IDX_CTX, sizeof ALX_IDX_CTX - 1u, rnd, sig);
}

void alx_mldsa_keypair(const uint8_t seed[32], uint8_t pk[ALX_IDX_PK], uint8_t sk[ALX_MLDSA_SK])
{
    pq_mldsa65_keygen(seed, pk, sk);
}
