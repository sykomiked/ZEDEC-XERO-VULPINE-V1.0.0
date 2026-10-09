/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
#include "quest_sign_mldsa.h"
#include "pq_security.h"
#include "keccak.h"

void qst_mldsa_key_id(const uint8_t pk[QST_MLDSA_PK_LEN], uint8_t key_id[32])
{
    sha3_256(pk, QST_MLDSA_PK_LEN, key_id);
}

void qst_mldsa_keypair(const uint8_t seed[32], uint8_t pk[QST_MLDSA_PK_LEN],
                       uint8_t sk[QST_MLDSA_SK_LEN], uint8_t key_id[32])
{
    pq_mldsa65_keygen(seed, pk, sk);
    qst_mldsa_key_id(pk, key_id);
}

bool qst_mldsa_sign(void *ctx, const uint8_t digest[32], uint8_t *sig, uint32_t cap, uint32_t *len)
{
    const qst_mldsa_key_t *k = (const qst_mldsa_key_t *) ctx;
    if (!k || !k->sk || !digest || !sig || !len || cap < QST_MLDSA_SIG_LEN) return false;
    pq_mldsa65_sign(k->sk, digest, 32, (const uint8_t *) QST_SIGN_CTX,
                    (uint32_t) sizeof QST_SIGN_CTX - 1, k->rnd, sig);
    *len = QST_MLDSA_SIG_LEN;
    return true;
}

bool qst_mldsa_verify(void *ctx, const uint8_t issuer_key[32], const uint8_t digest[32],
                      const uint8_t *sig, uint32_t len)
{
    const qst_mldsa_dir_t *d = (const qst_mldsa_dir_t *) ctx;
    if (!d || !issuer_key || !digest || !sig || len != QST_MLDSA_SIG_LEN) return false;
    for (uint32_t i = 0; i < d->n && i < QST_MLDSA_DIR_MAX; i++) {
        uint8_t id[32], diff = 0;
        qst_mldsa_key_id(d->pk[i], id);
        for (uint32_t j = 0; j < 32; j++) diff |= (uint8_t) (id[j] ^ issuer_key[j]);
        if (diff) continue;
        return pq_mldsa65_verify(d->pk[i], digest, 32, (const uint8_t *) QST_SIGN_CTX,
                                 (uint32_t) sizeof QST_SIGN_CTX - 1, sig);
    }
    return false;
}
