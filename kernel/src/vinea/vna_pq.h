/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_pq.h — the post-quantum primitives Vinea calls, nothing more.
 *
 * ML-DSA-87 (FIPS 204) and ML-KEM-1024 (FIPS 203), both category 5, from the
 * pqsec layered hybrid (src/pqsec/pq_mldsa87.c, src/pqsec/pq_mlkem1024.c,
 * NIST ACVP checked in test_pq_matrix.c). pq_matrix_algs.h is freestanding
 * (stdint/stdbool/stddef only). Bootlegger uses the same two primitives, so
 * the system has one post-quantum strength for sessions and identities.
 */
#ifndef VNA_PQ_H
#define VNA_PQ_H

#include <stdint.h>
#include <stdbool.h>
#include "../mlkem/keccak.h"
#include "../pqsec/pq_matrix_algs.h"

#define VNA_KEM_EK_LEN PQM_MLKEM1024_EK_BYTES
#define VNA_KEM_DK_LEN PQM_MLKEM1024_DK_BYTES
#define VNA_KEM_CT_LEN PQM_MLKEM1024_CT_BYTES

#define VNA_PK_LEN  PQM_MLDSA87_PK_BYTES
#define VNA_SK_LEN  PQM_MLDSA87_SK_BYTES
#define VNA_SIG_LEN PQM_MLDSA87_SIG_BYTES

/* ML-DSA-87 context strings: one per signed object type, so a signature made
 * for one purpose can never be replayed as another. */
#define VNA_CTX_MSG     "vinea/v3/msg"
#define VNA_CTX_RECORD  "vinea/v3/dht-record"
#define VNA_CTX_HS_RESP "vinea/v3/hs-responder"
#define VNA_CTX_HS_INIT "vinea/v3/hs-initiator"
#define VNA_CTX_RECEIPT "vinea/v3/trade-receipt"
#define VNA_CTX_LEDGER  "vinea/v3/ledger-head"
#define VNA_CTX_NODEREC "vinea/v3/node-record"
#define VNA_CTX_SPOOL   "vinea/v3/spool-item"

/* Sign / verify with a NUL-terminated context string. */
void vna_sign(const uint8_t sk[VNA_SK_LEN], const char *ctx, const uint8_t *msg, uint32_t len,
              const uint8_t rnd[32], uint8_t sig[VNA_SIG_LEN]);
bool vna_verify(const uint8_t pk[VNA_PK_LEN], const char *ctx, const uint8_t *msg, uint32_t len,
                const uint8_t sig[VNA_SIG_LEN]);

#endif /* VNA_PQ_H */
