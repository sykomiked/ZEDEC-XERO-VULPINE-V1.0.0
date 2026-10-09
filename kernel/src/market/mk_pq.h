/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mk_pq.h — ML-DSA-65 signatures for market objects (kernel/src/pqsec).
 *
 * A market identity (mk_id_t) is the SHA3-256 fingerprint of an ML-DSA-65
 * public key. A keyring maps fingerprints to keys; mk_pq_verify checks a
 * FIPS 204 signature over the 32-byte object digest with the context string
 * "zxv-market-v1". The operator's mk_hooks_t.verify forwards to it.
 *
 * HONEST LIMITS. The keyring is a small fixed table; discovering and
 * trusting keys (who a fingerprint belongs to) is the identity layer's job.
 * Long-lived institutional anchors should use the dual-signature hybrid of
 * pq_matrix.h; this adapter is single-algorithm ML-DSA-65. Freestanding.
 */
#ifndef ZXV_MK_PQ_H
#define ZXV_MK_PQ_H

#include "market.h"

/* FIPS 204 ML-DSA-65 sizes, equal to PQ_MLDSA65_* in pq_security.h (that
 * header pulls in <complex.h>, so it is not included here; test_market.c
 * asserts the sizes agree). */
#define MK_PQ_PK_BYTES  1952u
#define MK_PQ_SK_BYTES  4032u
#define MK_PQ_SIG_BYTES 3309u
#define MK_PQ_KEYS      16u
#define MK_PQ_CTX       "zxv-market-v1"

typedef struct {
    mk_id_t id[MK_PQ_KEYS];
    uint8_t pk[MK_PQ_KEYS][MK_PQ_PK_BYTES];
    uint32_t n;
} mk_pq_keyring_t;

/* Fingerprint of a public key. */
void mk_pq_fingerprint(const uint8_t pk[MK_PQ_PK_BYTES], mk_id_t *out);
/* Add a key; returns its fingerprint in *id. False when full. */
bool mk_pq_add(mk_pq_keyring_t *k, const uint8_t pk[MK_PQ_PK_BYTES], mk_id_t *id);
bool mk_pq_verify(const mk_pq_keyring_t *k, const mk_id_t *signer, const uint8_t *msg, uint32_t len,
                  const uint8_t *sig, uint32_t sig_len);
/* Sign a digest (the user's own device). */
void mk_pq_sign(const uint8_t sk[MK_PQ_SK_BYTES], const uint8_t digest[32], const uint8_t rnd[32],
                uint8_t sig[MK_PQ_SIG_BYTES]);

#endif /* ZXV_MK_PQ_H */
