/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_pq.h — the post-quantum primitives Vinea calls, nothing more.
 *
 * ML-DSA-65 (FIPS 204) is implemented and NIST-vector checked in
 * src/pqsec/pq_mldsa65.c and declared in src/pqsec/pq_security.h. That header
 * also pulls in include/m5_types.h (<complex.h>), which a bare-metal target
 * does not have, so the three ML-DSA-65 entry points Vinea uses are
 * re-declared here with EXACTLY the same prototypes. test_vinea.c includes
 * both headers in one translation unit, so any drift between the two
 * declarations is a compile error, not a silent ABI mismatch.
 *
 * ML-KEM-768 (FIPS 203) comes straight from src/mlkem/mlkem768.h, which is
 * freestanding already.
 */
#ifndef VNA_PQ_H
#define VNA_PQ_H

#include <stdint.h>
#include <stdbool.h>
#include "../mlkem/mlkem768.h"

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

#define VNA_PK_LEN  PQ_MLDSA65_PK_BYTES
#define VNA_SK_LEN  PQ_MLDSA65_SK_BYTES
#define VNA_SIG_LEN PQ_MLDSA65_SIG_BYTES

/* ML-DSA-65 context strings: one per signed object type, so a signature made
 * for one purpose can never be replayed as another. */
#define VNA_CTX_MSG     "vinea/v2/msg"
#define VNA_CTX_RECORD  "vinea/v2/dht-record"
#define VNA_CTX_HS_RESP "vinea/v2/hs-responder"
#define VNA_CTX_HS_INIT "vinea/v2/hs-initiator"
#define VNA_CTX_RECEIPT "vinea/v2/trade-receipt"
#define VNA_CTX_LEDGER  "vinea/v2/ledger-head"
#define VNA_CTX_NODEREC "vinea/v2/node-record"
#define VNA_CTX_SPOOL   "vinea/v2/spool-item"

/* Sign / verify with a NUL-terminated context string. */
void vna_sign(const uint8_t sk[VNA_SK_LEN], const char *ctx, const uint8_t *msg, uint32_t len,
              const uint8_t rnd[32], uint8_t sig[VNA_SIG_LEN]);
bool vna_verify(const uint8_t pk[VNA_PK_LEN], const char *ctx, const uint8_t *msg, uint32_t len,
                const uint8_t sig[VNA_SIG_LEN]);

#endif /* VNA_PQ_H */
