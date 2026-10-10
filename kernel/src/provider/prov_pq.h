/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* prov_pq.h — ML-DSA-65 (FIPS 204, kernel/src/pqsec) wired into the provider
 * layer's verify hook, plus signing helpers for providers and users. The
 * context string is "zxv-prov" for every signature, so a provider signature
 * can never be replayed as a signature from another ZXV module. */
#ifndef ZXV_PROV_PQ_H
#define ZXV_PROV_PQ_H

#include "prov.h"

#define PROV_SK_BYTES 4032u /* PQ_MLDSA65_SK_BYTES */

/* prov_verify_fn over pq_mldsa65_verify; ctx unused. */
bool prov_pq_verify(void *ctx, const uint8_t pk[PROV_PK_BYTES], const uint8_t *msg, uint32_t len,
                    const uint8_t sig[PROV_SIG_BYTES]);
/* Deterministic ML-DSA-65 signature over msg with the "zxv-prov" context. */
void prov_pq_sign(const uint8_t sk[PROV_SK_BYTES], const uint8_t *msg, uint32_t len,
                  uint8_t sig[PROV_SIG_BYTES]);
void prov_pq_keygen(const uint8_t seed[32], uint8_t pk[PROV_PK_BYTES], uint8_t sk[PROV_SK_BYTES]);
/* Sign a descriptor (for prov_register) and an ask (for prov_ask_post). */
void prov_pq_sign_desc(const prov_desc_t *d, const uint8_t pk[PROV_PK_BYTES],
                       const uint8_t sk[PROV_SK_BYTES], uint8_t sig[PROV_SIG_BYTES]);
void prov_pq_sign_ask(const prov_net_t *n, uint32_t provider, uint8_t offer, uint16_t asset,
                      uint64_t unit_price, uint64_t qty, uint64_t nonce,
                      const uint8_t sk[PROV_SK_BYTES], uint8_t sig[PROV_SIG_BYTES]);
/* Co-sign a receipt as the provider (as_provider) or as the user. */
void prov_pq_sign_receipt(prov_receipt_t *r, const uint8_t sk[PROV_SK_BYTES], bool as_provider);

#endif /* ZXV_PROV_PQ_H */
