/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* quest_sign_mldsa.h — binds quest badges and revocations to ML-DSA-65
 * (FIPS 204) in src/pqsec/pq_security.h.
 *
 * Signature: pq_mldsa65_sign(sk, digest, 32, "zxv.quest.v1", 12, rnd, sig).
 * The context string keeps badge signatures apart from every other use of
 * the same key (social records, ledger seals). An issuer's key id is
 * SHA3-256 of its 1952-byte public key.
 *
 * This is the only quest file that includes pq_security.h. That header
 * pulls in <complex.h> through m5_types.h, so a bare-metal build needs
 * -Iinclude/freestanding_stubs, as src/social/social_sign_mldsa.c does.
 *
 * HONEST LIMITS. Key generation takes a 32-byte seed that must come from a
 * real entropy source; this layer has none. With rnd == NULL signing is
 * deterministic (FIPS 204 allows it; hedged signing resists fault attacks
 * better). Secret keys are caller memory and nothing here wipes them. The
 * directory is a small linear list, not a trust policy: deciding which
 * issuers a community accepts is governance, not this file.
 */
#ifndef ZXV_QUEST_SIGN_MLDSA_H
#define ZXV_QUEST_SIGN_MLDSA_H

#include <stdint.h>
#include <stdbool.h>

#define QST_MLDSA_PK_LEN  1952u
#define QST_MLDSA_SK_LEN  4032u
#define QST_MLDSA_SIG_LEN 3309u
#define QST_MLDSA_DIR_MAX 8u
#define QST_SIGN_CTX      "zxv.quest.v1"

typedef struct {
    const uint8_t *sk;  /* QST_MLDSA_SK_LEN bytes */
    const uint8_t *rnd; /* 32 fresh bytes, or NULL (deterministic) */
} qst_mldsa_key_t;

typedef struct {
    const uint8_t *pk[QST_MLDSA_DIR_MAX]; /* QST_MLDSA_PK_LEN bytes each */
    uint32_t n;
} qst_mldsa_dir_t;

void qst_mldsa_keypair(const uint8_t seed[32], uint8_t pk[QST_MLDSA_PK_LEN],
                       uint8_t sk[QST_MLDSA_SK_LEN], uint8_t key_id[32]);
void qst_mldsa_key_id(const uint8_t pk[QST_MLDSA_PK_LEN], uint8_t key_id[32]);

/* qst_sign_fn with ctx = qst_mldsa_key_t* */
bool qst_mldsa_sign(void *ctx, const uint8_t digest[32], uint8_t *sig, uint32_t cap, uint32_t *len);
/* qst_verify_fn with ctx = qst_mldsa_dir_t* */
bool qst_mldsa_verify(void *ctx, const uint8_t issuer_key[32], const uint8_t digest[32],
                      const uint8_t *sig, uint32_t len);

#endif /* ZXV_QUEST_SIGN_MLDSA_H */
