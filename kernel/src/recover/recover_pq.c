/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* recover_pq.c — the ML-DSA-87 binding of rcv_sign_fn / rcv_verify_fn. */
#include "recover_pq.h"
#include "../pqsec/pq_matrix_algs.h"

#define CTX     ((const uint8_t *) RCV_SIG_CONTEXT)
#define CTX_LEN (sizeof RCV_SIG_CONTEXT - 1u)

_Static_assert(RCV_MLDSA87_SIG == PQM_MLDSA87_SIG_BYTES, "ML-DSA-87 signature size");
_Static_assert(RCV_MLDSA87_PK == PQM_MLDSA87_PK_BYTES, "ML-DSA-87 public key size");
_Static_assert(RCV_MLDSA87_SK == PQM_MLDSA87_SK_BYTES, "ML-DSA-87 secret key size");
_Static_assert(RCV_SIG_MAX >= PQM_MLDSA87_SIG_BYTES, "proof has room for ML-DSA-87");

uint32_t rcv_mldsa87_sign(void *ctx, const uint8_t msg[PDX_HASH], uint8_t *sig, uint32_t cap)
{
    const rcv_mldsa87_key_t *k = (const rcv_mldsa87_key_t *) ctx;
    if (!k || !k->sk || !msg || !sig || cap < PQM_MLDSA87_SIG_BYTES) return 0;
    pqm_mldsa87_sign(k->sk, msg, PDX_HASH, CTX, CTX_LEN, k->rnd, sig);
    return PQM_MLDSA87_SIG_BYTES;
}

bool rcv_mldsa87_verify(void *ctx, const uint8_t *pk, uint32_t pk_len, const uint8_t msg[PDX_HASH],
                        const uint8_t *sig, uint32_t sig_len)
{
    (void) ctx;
    if (!pk || !msg || !sig || pk_len != PQM_MLDSA87_PK_BYTES || sig_len != PQM_MLDSA87_SIG_BYTES)
        return false;
    return pqm_mldsa87_verify(pk, msg, PDX_HASH, CTX, CTX_LEN, sig);
}
