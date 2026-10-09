/* pq_slhdsa.c — SLH-DSA-SHAKE-128s (FIPS 205) for the pqOS security layer.
 *
 * Thin glue over slhdsa-c (Apache-2.0 / MIT / ISC, see slhdsa/LICENSE and
 * slhdsa/README.zxv). SHAKE rather than SHA-2, matching the Keccak the rest
 * of the kernel already uses. Pure SLH-DSA (no pre-hash), Algorithms 21-24.
 * Checked against the NIST ACVP vectors in test_pq_kat.c.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 */
#include "pq_security.h"
#include "slhdsa/slh_dsa.h"

#define SLH_N 16u /* security parameter n for the 128s set */

void pq_slh128s_keygen(const uint8_t seed[48], uint8_t pk[PQ_SLH128S_PK_BYTES],
                       uint8_t sk[PQ_SLH128S_SK_BYTES])
{
    if (!seed || !pk || !sk) return;
    /* seed = SK.seed || SK.prf || PK.seed, n bytes each (FIPS 205 Alg 21). */
    slh_keygen_internal(sk, pk, seed, seed + SLH_N, seed + 2 * SLH_N, &slh_dsa_shake_128s);
}

void pq_slh128s_sign_ctx(const uint8_t sk[PQ_SLH128S_SK_BYTES], const uint8_t *msg,
                         uint32_t msg_len, const uint8_t *ctx, uint32_t ctx_len,
                         const uint8_t *opt_rnd, uint8_t sig[PQ_SLH128S_SIG_BYTES])
{
    if (!sig) return;
    for (uint32_t i = 0; i < PQ_SLH128S_SIG_BYTES; i++) sig[i] = 0;
    if (!sk || (!msg && msg_len) || (!ctx && ctx_len) || ctx_len > PQ_SLH128S_CTX_BYTES) return;
    static const uint8_t none[1] = {0};
    /* opt_rnd = NULL is the deterministic variant (addrnd = PK.seed). */
    slh_sign(sig, msg ? msg : none, msg_len, ctx ? ctx : none, ctx_len, sk, opt_rnd,
             &slh_dsa_shake_128s);
}

bool pq_slh128s_verify_ctx(const uint8_t pk[PQ_SLH128S_PK_BYTES], const uint8_t *msg,
                           uint32_t msg_len, const uint8_t *ctx, uint32_t ctx_len,
                           const uint8_t sig[PQ_SLH128S_SIG_BYTES])
{
    if (!pk || !sig || (!msg && msg_len) || (!ctx && ctx_len) || ctx_len > PQ_SLH128S_CTX_BYTES)
        return false;
    static const uint8_t none[1] = {0};
    return slh_verify(msg ? msg : none, msg_len, sig, PQ_SLH128S_SIG_BYTES, ctx ? ctx : none,
                      ctx_len, pk, &slh_dsa_shake_128s) == 1;
}

void pq_slh128s_sign(const uint8_t sk[PQ_SLH128S_SK_BYTES], const uint8_t *msg, uint32_t msg_len,
                     const uint8_t *opt_rnd, uint8_t sig[PQ_SLH128S_SIG_BYTES])
{
    pq_slh128s_sign_ctx(sk, msg, msg_len, 0, 0, opt_rnd, sig);
}

bool pq_slh128s_verify(const uint8_t pk[PQ_SLH128S_PK_BYTES], const uint8_t *msg, uint32_t msg_len,
                       const uint8_t sig[PQ_SLH128S_SIG_BYTES])
{
    return pq_slh128s_verify_ctx(pk, msg, msg_len, 0, 0, sig);
}
