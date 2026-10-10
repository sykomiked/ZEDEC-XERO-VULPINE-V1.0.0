/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mk_pq.c — see mk_pq.h. */
#include "mk_pq.h"

/* The two FIPS 204 entry points of kernel/src/pqsec/pq_mldsa65.c, declared
 * here with the exact signatures of pq_security.h: that header pulls in
 * <complex.h> through m5_types.h and cannot be included in a bare-metal
 * build. test_market.c includes pq_security.h and asserts the sizes match. */
void pq_mldsa65_sign(const uint8_t sk[MK_PQ_SK_BYTES], const uint8_t *msg, uint32_t msg_len,
                     const uint8_t *ctx, uint32_t ctx_len, const uint8_t rnd[32],
                     uint8_t sig[MK_PQ_SIG_BYTES]);
bool pq_mldsa65_verify(const uint8_t pk[MK_PQ_PK_BYTES], const uint8_t *msg, uint32_t msg_len,
                       const uint8_t *ctx, uint32_t ctx_len, const uint8_t sig[MK_PQ_SIG_BYTES]);

void mk_pq_fingerprint(const uint8_t pk[MK_PQ_PK_BYTES], mk_id_t *out)
{
    pay_sha3_256(pk, MK_PQ_PK_BYTES, out->b);
}

bool mk_pq_add(mk_pq_keyring_t *k, const uint8_t pk[MK_PQ_PK_BYTES], mk_id_t *id)
{
    if (!k || k->n >= MK_PQ_KEYS) return false;
    mk_pq_fingerprint(pk, &k->id[k->n]);
    pay_memcpy(k->pk[k->n], pk, MK_PQ_PK_BYTES);
    if (id) pay_memcpy(id->b, k->id[k->n].b, 32);
    k->n++;
    return true;
}

bool mk_pq_verify(const mk_pq_keyring_t *k, const mk_id_t *signer, const uint8_t *msg, uint32_t len,
                  const uint8_t *sig, uint32_t sig_len)
{
    if (!k || !signer || !msg || !sig || sig_len != MK_PQ_SIG_BYTES) return false;
    for (uint32_t i = 0; i < k->n; i++)
        if (pay_memeq(k->id[i].b, signer->b, 32))
            return pq_mldsa65_verify(k->pk[i], msg, len, (const uint8_t *) MK_PQ_CTX,
                                     sizeof MK_PQ_CTX - 1, sig);
    return false;
}

void mk_pq_sign(const uint8_t sk[MK_PQ_SK_BYTES], const uint8_t digest[32], const uint8_t rnd[32],
                uint8_t sig[MK_PQ_SIG_BYTES])
{
    pq_mldsa65_sign(sk, digest, 32, (const uint8_t *) MK_PQ_CTX, sizeof MK_PQ_CTX - 1, rnd, sig);
}
