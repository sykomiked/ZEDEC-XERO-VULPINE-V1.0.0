/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* pq_slh256s.c — SLH-DSA-SHAKE-256s (FIPS 205, category 5) for pq_matrix.
 *
 * Thin glue over the slhdsa-c copy vendored in slhdsa/ (Apache-2.0 / MIT /
 * ISC), the same one pq_slhdsa.c uses for SLH-DSA-SHAKE-128s; this file
 * only selects the 256s parameter set. Pure SLH-DSA, no pre-hash.
 * Checked against NIST ACVP vectors in test_pq_matrix.c.
 */
#include "pq_matrix_algs.h"
#include "slhdsa/slh_dsa.h"

void pqm_slh256s_keygen(const uint8_t seed[3 * PQM_SLH256S_N], uint8_t pk[PQM_SLH256S_PK_BYTES],
                        uint8_t sk[PQM_SLH256S_SK_BYTES])
{
    slh_keygen_internal(sk, pk, seed, seed + PQM_SLH256S_N, seed + 2 * PQM_SLH256S_N,
                        &slh_dsa_shake_256s);
}

void pqm_slh256s_sign(const uint8_t sk[PQM_SLH256S_SK_BYTES], const uint8_t *msg, size_t msg_len,
                      const uint8_t *ctx, size_t ctx_len, const uint8_t *addrnd,
                      uint8_t sig[PQM_SLH256S_SIG_BYTES])
{
    for (unsigned i = 0; i < PQM_SLH256S_SIG_BYTES; i++) sig[i] = 0;
    if ((!msg && msg_len) || (!ctx && ctx_len) || ctx_len > 255) return;
    static const uint8_t none[1] = {0};
    /* addrnd = NULL is the deterministic variant (addrnd = PK.seed). */
    slh_sign(sig, msg ? msg : none, msg_len, ctx ? ctx : none, ctx_len, sk, addrnd,
             &slh_dsa_shake_256s);
}

bool pqm_slh256s_verify(const uint8_t pk[PQM_SLH256S_PK_BYTES], const uint8_t *msg, size_t msg_len,
                        const uint8_t *ctx, size_t ctx_len,
                        const uint8_t sig[PQM_SLH256S_SIG_BYTES])
{
    if ((!msg && msg_len) || (!ctx && ctx_len) || ctx_len > 255) return false;
    static const uint8_t none[1] = {0};
    return slh_verify(msg ? msg : none, msg_len, sig, PQM_SLH256S_SIG_BYTES, ctx ? ctx : none,
                      ctx_len, pk, &slh_dsa_shake_256s) == 1;
}
