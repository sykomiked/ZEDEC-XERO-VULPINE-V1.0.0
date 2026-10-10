/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* pa_pq.h — the ML-DSA-65 entry points peer_audit calls, nothing more.
 *
 * They are implemented in src/pqsec/pq_mldsa65.c and declared in
 * src/pqsec/pq_security.h, which also pulls in headers a bare-metal target
 * does not have. The prototypes are re-declared here EXACTLY as there;
 * test_peer_audit.c includes both headers in one translation unit, so any
 * drift is a compile error (the same pattern as vinea/vna_pq.h).
 */
#ifndef ZXV_PA_PQ_H
#define ZXV_PA_PQ_H

#include <stdint.h>
#include <stdbool.h>

#ifndef PQ_MLDSA65_PK_BYTES
#    define PQ_MLDSA65_PK_BYTES  1952
#    define PQ_MLDSA65_SK_BYTES  4032
#    define PQ_MLDSA65_SIG_BYTES 3309
#    define PQ_MLDSA65_CTX_BYTES 255
#endif

void pq_mldsa65_keygen(const uint8_t seed[32], uint8_t pk[PQ_MLDSA65_PK_BYTES],
                       uint8_t sk[PQ_MLDSA65_SK_BYTES]);
void pq_mldsa65_sign(const uint8_t sk[PQ_MLDSA65_SK_BYTES], const uint8_t *msg, uint32_t msg_len,
                     const uint8_t *ctx, uint32_t ctx_len, const uint8_t rnd[32],
                     uint8_t sig[PQ_MLDSA65_SIG_BYTES]);
bool pq_mldsa65_verify(const uint8_t pk[PQ_MLDSA65_PK_BYTES], const uint8_t *msg, uint32_t msg_len,
                       const uint8_t *ctx, uint32_t ctx_len,
                       const uint8_t sig[PQ_MLDSA65_SIG_BYTES]);

#endif /* ZXV_PA_PQ_H */
