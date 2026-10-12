/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hwr_pq.h — post-quantum signatures for reconfiguration images and
 * contract packages (hwr.h R1): ML-DSA-87 alone, or the default dual suite
 * ML-DSA-87 + SLH-DSA-SHAKE-256s. */
#ifndef HWR_PQ_H
#define HWR_PQ_H

#include "hwr.h"

/* An hwr_verify_fn for HWR_ALG_MLDSA87 and HWR_ALG_DUAL, with the
 * HWR_SIG_CONTEXT domain context. A dual key or signature is the ML-DSA
 * part followed by the SLH-DSA part, and both must verify. */
bool hwr_pq_verify(void *ctx, uint16_t alg, const uint8_t *pk, uint32_t pk_len, const uint8_t *msg,
                   uint32_t msg_len, const uint8_t *sig, uint32_t sig_len);
/* Sign a 96-byte header with ML-DSA-87 (sk 4896 bytes, sig 4627 bytes).
 * rnd: 32 fresh bytes, or NULL for deterministic signing. */
void hwr_mldsa87_sign(const uint8_t *sk, const uint8_t header[HWR_HEADER], const uint8_t *rnd,
                      uint8_t *sig);
/* Sign a header with the dual suite: mldsa_sk 4896 bytes, slh_sk 128
 * bytes, sig HWR_DUAL_SIG bytes. SLH-DSA-256s signing is slow (seconds);
 * it is a release-time step, not a runtime one. */
void hwr_dual_sign(const uint8_t *mldsa_sk, const uint8_t *slh_sk, const uint8_t header[HWR_HEADER],
                   const uint8_t *rnd, uint8_t *sig);

#endif /* HWR_PQ_H */
