/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxv_pqc.h - public header of libzxv-pqc.
 *
 * Post-quantum primitives from the ZXV kernel, as one static library:
 *   FIPS 203 ML-KEM-768   mlkem768_keygen / _encaps / _decaps
 *   FIPS 203 ML-KEM-1024  pqm_mlkem1024_*
 *   FIPS 204 ML-DSA-65    pq_mldsa65_*         ML-DSA-87  pqm_mldsa87_*
 *   FIPS 205 SLH-DSA-SHAKE-128s  pq_slh128s_*  SLH-DSA-SHAKE-256s  pqm_slh256s_*
 *   FIPS 202 SHA3-256/512, SHAKE128/256 (keccak.h)
 *
 * Every function takes its randomness as an argument (there is no RNG in
 * the library: the caller supplies fresh bytes from its own source), and
 * none allocates memory. They are not re-entrant across threads: the
 * ML-DSA keygen hands its seed to the reference code through a static
 * buffer, so serialise calls or use one thread.
 *
 * The algorithms are tested against NIST ACVP vectors in the ZXV tree
 * (test_mlkem_kat, test_pq_kat, test_pq_matrix). They are NOT formally
 * verified, NOT FIPS 140-3 validated, NOT certified, and have had no
 * side-channel review beyond what the vendored reference code provides.
 */
#ifndef ZXV_PQC_H
#define ZXV_PQC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "zxv/mlkem/keccak.h"
#include "zxv/mlkem/mlkem768.h"
#include "zxv/pqsec/pq_matrix_algs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- ML-DSA-65 (FIPS 204). Same declarations as kernel pq_security.h. ---- */
#define PQ_MLDSA65_PK_BYTES  1952
#define PQ_MLDSA65_SK_BYTES  4032
#define PQ_MLDSA65_SIG_BYTES 3309
#define PQ_MLDSA65_CTX_BYTES 255

/* seed: 32 bytes of fresh randomness. */
void pq_mldsa65_keygen(const uint8_t seed[32], uint8_t pk[PQ_MLDSA65_PK_BYTES],
                       uint8_t sk[PQ_MLDSA65_SK_BYTES]);
/* ctx may be NULL/0 (at most 255 bytes); rnd: 32 fresh bytes (hedged) or
 * NULL (deterministic). On bad arguments sig is all zero (never verifies). */
void pq_mldsa65_sign(const uint8_t sk[PQ_MLDSA65_SK_BYTES], const uint8_t *msg, uint32_t msg_len,
                     const uint8_t *ctx, uint32_t ctx_len, const uint8_t rnd[32],
                     uint8_t sig[PQ_MLDSA65_SIG_BYTES]);
bool pq_mldsa65_verify(const uint8_t pk[PQ_MLDSA65_PK_BYTES], const uint8_t *msg, uint32_t msg_len,
                       const uint8_t *ctx, uint32_t ctx_len,
                       const uint8_t sig[PQ_MLDSA65_SIG_BYTES]);

/* ---- SLH-DSA-SHAKE-128s (FIPS 205) ---- */
#define PQ_SLH128S_PK_BYTES  32
#define PQ_SLH128S_SK_BYTES  64
#define PQ_SLH128S_SIG_BYTES 7856
#define PQ_SLH128S_CTX_BYTES 255

/* seed: 48 fresh bytes, read as SK.seed || SK.prf || PK.seed. */
void pq_slh128s_keygen(const uint8_t seed[48], uint8_t pk[PQ_SLH128S_PK_BYTES],
                       uint8_t sk[PQ_SLH128S_SK_BYTES]);
/* Empty context. opt_rnd: 16 fresh bytes (hedged) or NULL (deterministic). */
void pq_slh128s_sign(const uint8_t sk[PQ_SLH128S_SK_BYTES], const uint8_t *msg, uint32_t msg_len,
                     const uint8_t *opt_rnd, uint8_t sig[PQ_SLH128S_SIG_BYTES]);
bool pq_slh128s_verify(const uint8_t pk[PQ_SLH128S_PK_BYTES], const uint8_t *msg, uint32_t msg_len,
                       const uint8_t sig[PQ_SLH128S_SIG_BYTES]);
/* With a context string (FIPS 205 Algorithms 22/24), ctx_len <= 255. */
void pq_slh128s_sign_ctx(const uint8_t sk[PQ_SLH128S_SK_BYTES], const uint8_t *msg,
                         uint32_t msg_len, const uint8_t *ctx, uint32_t ctx_len,
                         const uint8_t *opt_rnd, uint8_t sig[PQ_SLH128S_SIG_BYTES]);
bool pq_slh128s_verify_ctx(const uint8_t pk[PQ_SLH128S_PK_BYTES], const uint8_t *msg,
                           uint32_t msg_len, const uint8_t *ctx, uint32_t ctx_len,
                           const uint8_t sig[PQ_SLH128S_SIG_BYTES]);

#ifdef __cplusplus
}
#endif

#endif /* ZXV_PQC_H */
