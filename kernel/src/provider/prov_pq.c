/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* prov_pq.c — ML-DSA-65 for the provider layer (see prov_pq.h). */
#include "prov_pq.h"

/* The ML-DSA-65 entry points of kernel/src/pqsec/pq_mldsa65.c, declared here
 * exactly as in pq_security.h (that header pulls in <complex.h> through
 * m5_types.h and so cannot be included in a freestanding build). The host
 * test includes pq_security.h too, which checks these declarations agree. */
void pq_mldsa65_keygen(const uint8_t seed[32], uint8_t pk[1952], uint8_t sk[4032]);
void pq_mldsa65_sign(const uint8_t sk[4032], const uint8_t *msg, uint32_t msg_len,
                     const uint8_t *ctx, uint32_t ctx_len, const uint8_t rnd[32],
                     uint8_t sig[3309]);
bool pq_mldsa65_verify(const uint8_t pk[1952], const uint8_t *msg, uint32_t msg_len,
                       const uint8_t *ctx, uint32_t ctx_len, const uint8_t sig[3309]);

static const uint8_t CTX[8] = {'z', 'x', 'v', '-', 'p', 'r', 'o', 'v'};

bool prov_pq_verify(void *ctx, const uint8_t pk[PROV_PK_BYTES], const uint8_t *msg, uint32_t len,
                    const uint8_t sig[PROV_SIG_BYTES])
{
    (void) ctx;
    if (!pk || !msg || !sig) return false;
    return pq_mldsa65_verify(pk, msg, len, CTX, sizeof CTX, sig);
}

void prov_pq_sign(const uint8_t sk[PROV_SK_BYTES], const uint8_t *msg, uint32_t len,
                  uint8_t sig[PROV_SIG_BYTES])
{
    pq_mldsa65_sign(sk, msg, len, CTX, sizeof CTX, 0, sig);
}

void prov_pq_keygen(const uint8_t seed[32], uint8_t pk[PROV_PK_BYTES], uint8_t sk[PROV_SK_BYTES])
{
    pq_mldsa65_keygen(seed, pk, sk);
}

void prov_pq_sign_desc(const prov_desc_t *d, const uint8_t pk[PROV_PK_BYTES],
                       const uint8_t sk[PROV_SK_BYTES], uint8_t sig[PROV_SIG_BYTES])
{
    uint8_t dg[PROV_HASH_LEN];
    prov_desc_digest(d, pk, dg);
    prov_pq_sign(sk, dg, PROV_HASH_LEN, sig);
}

void prov_pq_sign_ask(const prov_net_t *n, uint32_t provider, uint8_t offer, uint16_t asset,
                      uint64_t unit_price, uint64_t qty, uint64_t nonce,
                      const uint8_t sk[PROV_SK_BYTES], uint8_t sig[PROV_SIG_BYTES])
{
    uint8_t dg[PROV_HASH_LEN];
    prov_ask_digest(n, provider, offer, asset, unit_price, qty, nonce, dg);
    prov_pq_sign(sk, dg, PROV_HASH_LEN, sig);
}

void prov_pq_sign_receipt(prov_receipt_t *r, const uint8_t sk[PROV_SK_BYTES], bool as_provider)
{
    uint8_t dg[PROV_HASH_LEN];
    prov_receipt_digest(r, dg);
    if (as_provider) {
        prov_pq_sign(sk, dg, PROV_HASH_LEN, r->sig_provider);
        r->has_sig_provider = true;
    } else {
        prov_pq_sign(sk, dg, PROV_HASH_LEN, r->sig_user);
        r->has_sig_user = true;
    }
}
