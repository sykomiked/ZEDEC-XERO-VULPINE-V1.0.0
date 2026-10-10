/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* social_sign_mldsa.c — ML-DSA-65 behind the social signer interface. */
#include "social_sign_mldsa.h"
#include "../pqsec/pq_security.h"

_Static_assert(SP_PK_LEN == PQ_MLDSA65_PK_BYTES, "social pk size is not ML-DSA-65");
_Static_assert(SP_SIG_LEN == PQ_MLDSA65_SIG_BYTES, "social sig size is not ML-DSA-65");
_Static_assert(SP_MLDSA_SK_LEN == PQ_MLDSA65_SK_BYTES, "social sk size is not ML-DSA-65");

static const uint8_t CTX[] = SP_SIGN_CTX;
#define CTX_LEN ((uint32_t) sizeof CTX - 1u)

void sp_mldsa_keypair(const uint8_t seed[32], uint8_t pk[SP_PK_LEN], uint8_t sk[SP_MLDSA_SK_LEN],
                      uint8_t node_id[SP_NODEID_LEN])
{
    pq_mldsa65_keygen(seed, pk, sk);
    if (node_id) sp_node_id(pk, node_id);
}

bool sp_mldsa_sign(void *ctx, const uint8_t id[SP_ID_LEN], uint8_t sig[SP_SIG_LEN])
{
    const sp_mldsa_key_t *k = (const sp_mldsa_key_t *) ctx;
    if (!k || !k->sk) return false;
    pq_mldsa65_sign(k->sk, id, SP_ID_LEN, CTX, CTX_LEN, k->rnd, sig);
    return true; /* an all-zero sig (bad arguments) is caught by sp_sign */
}

bool sp_mldsa_verify(void *ctx, const uint8_t pk[SP_PK_LEN], const uint8_t id[SP_ID_LEN],
                     const uint8_t sig[SP_SIG_LEN])
{
    (void) ctx;
    return pq_mldsa65_verify(pk, id, SP_ID_LEN, CTX, CTX_LEN, sig);
}
