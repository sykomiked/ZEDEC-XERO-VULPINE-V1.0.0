/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cpk.h — the trusted-signer keyring for compliance manifests (ML-DSA-87).
 *
 * A thin binding of cp_common.h's verify hook to ML-DSA-87 (FIPS 204,
 * kernel/src/pqsec/pq_mldsa87.c, pure ML-DSA with a context string). Nothing
 * cryptographic is implemented here.
 *
 *   K1 KEY ID. A signer is named by SHA-256("zxv/cpk-keyid/v1" || pk); the
 *      manifest header carries that id (M1) and the keyring maps it back to
 *      the public key.
 *   K2 SCOPE. Each trusted key lists the manifest kinds it may sign (a bit
 *      mask of 1 << CP_KIND_*) and one country ("**" = any). A key trusted to
 *      sign Germany's tax table cannot sign France's capital-control profile.
 *   K3 CONTEXT. Every signature uses the ML-DSA context string CPK_CTX, so a
 *      compliance signature can never be replayed as some other ZXV
 *      signature that signs the same bytes under another context.
 *   K4 FULL. The keyring holds CPK_MAX keys; adding past that, adding an
 *      unscoped key, or adding the same key twice is refused.
 *
 * HONEST LIMITS. Who goes in the keyring is a policy decision made outside
 * this code (the operator, per jurisdiction). There is no key rotation or
 * revocation list for signers yet: remove a key by rebuilding the keyring.
 */
#ifndef ZXV_CPK_H
#define ZXV_CPK_H

#include "cp_common.h"
#include "../pqsec/pq_matrix_algs.h"

#define CPK_MAX     8u
#define CPK_CTX     "ZXV-COMPLIANCE"
#define CPK_CTX_LEN 14u
#define CPK_PK_LEN  PQM_MLDSA87_PK_BYTES
#define CPK_SK_LEN  PQM_MLDSA87_SK_BYTES
#define CPK_SIG_LEN PQM_MLDSA87_SIG_BYTES

typedef struct {
    uint8_t key_id[CP_KEYID_LEN];
    uint8_t pk[CPK_PK_LEN];
    uint32_t kinds; /* 1u << CP_KIND_* */
    char country[2];
} cpk_key_t;

typedef struct {
    cpk_key_t key[CPK_MAX];
    uint32_t n;
} cpk_ring_t;

void cpk_key_id(const uint8_t pk[CPK_PK_LEN], uint8_t out[CP_KEYID_LEN]);
void cpk_init(cpk_ring_t *r);
bool cpk_add(cpk_ring_t *r, const uint8_t pk[CPK_PK_LEN], uint32_t kinds, const char country[2]);
/* cp_verify_fn: ctx is a cpk_ring_t. */
bool cpk_verify(void *ctx, const cp_hdr_t *hdr, const uint8_t *msg, size_t msg_len,
                const uint8_t *sig, size_t sig_len);
/* Verify against a raw public key (kycwrap holder keys). */
bool cpk_verify_pk(const uint8_t *pk, size_t pk_len, const uint8_t *msg, size_t msg_len,
                   const uint8_t *sig, size_t sig_len);
/* Sign with CPK_CTX. rnd: 32 fresh bytes (hedged) or NULL (deterministic). */
void cpk_sign(const uint8_t sk[CPK_SK_LEN], const uint8_t *msg, size_t msg_len, const uint8_t *rnd,
              uint8_t sig[CPK_SIG_LEN]);
/* Key pair from a 32-byte seed. */
void cpk_keygen(const uint8_t seed[32], uint8_t pk[CPK_PK_LEN], uint8_t sk[CPK_SK_LEN]);

#endif /* ZXV_CPK_H */
