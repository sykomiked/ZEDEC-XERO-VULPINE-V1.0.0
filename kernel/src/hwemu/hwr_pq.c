/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hwr_pq.c — the PQ binding of hwr_verify_fn (hosted / pqsec builds). Kept
 * apart from hwr.c so the update policy stays crypto-free. */
#include "hwr_pq.h"
#include "../pqsec/pq_matrix_algs.h"

#define CTX     ((const uint8_t *) HWR_SIG_CONTEXT)
#define CTX_LEN (sizeof HWR_SIG_CONTEXT - 1u)

bool hwr_pq_verify(void *ctx, uint16_t alg, const uint8_t *pk, uint32_t pk_len, const uint8_t *msg,
                   uint32_t msg_len, const uint8_t *sig, uint32_t sig_len)
{
    (void) ctx;
    if (!pk || !msg || !sig) return false;
    if (alg == HWR_ALG_MLDSA87) {
        if (pk_len != PQM_MLDSA87_PK_BYTES || sig_len != PQM_MLDSA87_SIG_BYTES) return false;
        return pqm_mldsa87_verify(pk, msg, msg_len, CTX, CTX_LEN, sig);
    }
    if (alg == HWR_ALG_DUAL) {
        if (pk_len != HWR_DUAL_PK || sig_len != HWR_DUAL_SIG) return false;
        /* Both, always: no early exit that would make one scheme optional. */
        bool a = pqm_mldsa87_verify(pk, msg, msg_len, CTX, CTX_LEN, sig);
        bool b = pqm_slh256s_verify(pk + HWR_MLDSA87_PK, msg, msg_len, CTX, CTX_LEN,
                                    sig + HWR_MLDSA87_SIG);
        return a && b;
    }
    return false;
}

void hwr_mldsa87_sign(const uint8_t *sk, const uint8_t header[HWR_HEADER], const uint8_t *rnd,
                      uint8_t *sig)
{
    pqm_mldsa87_sign(sk, header, HWR_HEADER, CTX, CTX_LEN, rnd, sig);
}

void hwr_dual_sign(const uint8_t *mldsa_sk, const uint8_t *slh_sk, const uint8_t header[HWR_HEADER],
                   const uint8_t *rnd, uint8_t *sig)
{
    pqm_mldsa87_sign(mldsa_sk, header, HWR_HEADER, CTX, CTX_LEN, rnd, sig);
    pqm_slh256s_sign(slh_sk, header, HWR_HEADER, CTX, CTX_LEN, rnd, sig + HWR_MLDSA87_SIG);
}
