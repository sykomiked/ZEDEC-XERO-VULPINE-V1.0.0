/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cn_pq.h — the two ML-DSA-65 entry points cardnet uses, declared here
 * exactly as in kernel/src/pqsec/pq_security.h. That header pulls in
 * surplus.h (and <math.h>) through lpres.h, which freestanding module code
 * must not; test_cardnet.c includes BOTH headers in one translation unit, so
 * any drift between these declarations and the real ones is a compile error.
 * Implementations: kernel/src/pqsec/pq_mldsa65.c + vendored mldsa/.
 */
#ifndef ZXV_CN_PQ_H
#define ZXV_CN_PQ_H

#include <stdint.h>
#include <stdbool.h>

void pq_mldsa65_sign(const uint8_t sk[4032], const uint8_t *msg, uint32_t msg_len,
                     const uint8_t *ctx, uint32_t ctx_len, const uint8_t rnd[32],
                     uint8_t sig[3309]);
bool pq_mldsa65_verify(const uint8_t pk[1952], const uint8_t *msg, uint32_t msg_len,
                       const uint8_t *ctx, uint32_t ctx_len, const uint8_t sig[3309]);

#endif /* ZXV_CN_PQ_H */
