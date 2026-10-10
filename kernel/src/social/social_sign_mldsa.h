/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* social_sign_mldsa.h — binds the social record signer (social_post.h P5)
 * to ML-DSA-65 (FIPS 204) in src/pqsec/pq_security.h.
 *
 * Signature: pq_mldsa65_sign(sk, id, 32, "zxv.social.v1", 13, rnd, sig).
 * The context string separates social signatures from every other use of
 * the same key (identity challenges, ledger seals).
 *
 * This is the only social file that includes pq_security.h. That header
 * pulls in m5_types.h, which includes <complex.h>, so on a bare-metal build
 * it needs -Iinclude/freestanding_stubs (the kernel Makefile's stub). The
 * rest of src/social builds freestanding without it.
 *
 * RULE: freestanding C11, no libc, no malloc, no floating point. Keys are
 * the caller's memory; this file keeps no state.
 *
 * HONEST LIMITS
 * -------------
 * pq_mldsa65_keygen takes a 32-byte seed and the whole key is derived from
 * it: the seed must come from a real entropy source, which the kernel does
 * not have at this layer. With rnd == NULL signing is deterministic (FIPS
 * 204 permits it, but hedged signing with fresh rnd per signature resists
 * fault attacks better). The secret key is 4032 bytes in caller memory and
 * nothing here wipes it.
 */
#ifndef ZXV_SOCIAL_SIGN_MLDSA_H
#define ZXV_SOCIAL_SIGN_MLDSA_H

#include "social_post.h"

#define SP_MLDSA_SK_LEN 4032u
#define SP_SIGN_CTX     "zxv.social.v1"

typedef struct {
    const uint8_t *sk;  /* SP_MLDSA_SK_LEN bytes */
    const uint8_t *rnd; /* 32 fresh bytes per signature, or NULL (deterministic) */
} sp_mldsa_key_t;

/* Derive a keypair from a 32-byte seed and the NodeID it gives. */
void sp_mldsa_keypair(const uint8_t seed[32], uint8_t pk[SP_PK_LEN], uint8_t sk[SP_MLDSA_SK_LEN],
                      uint8_t node_id[SP_NODEID_LEN]);

/* sp_signer_t.sign with ctx = sp_mldsa_key_t*. */
bool sp_mldsa_sign(void *ctx, const uint8_t id[SP_ID_LEN], uint8_t sig[SP_SIG_LEN]);

/* sp_verifier_t.verify; ctx unused. */
bool sp_mldsa_verify(void *ctx, const uint8_t pk[SP_PK_LEN], const uint8_t id[SP_ID_LEN],
                     const uint8_t sig[SP_SIG_LEN]);

#endif /* ZXV_SOCIAL_SIGN_MLDSA_H */
