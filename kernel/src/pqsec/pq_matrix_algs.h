/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* pq_matrix_algs.h — the single-algorithm layers under pq_matrix.h.
 *
 * Each function here is a thin, derandomised wrapper over one vendored
 * reference implementation: every random input is a caller-supplied
 * buffer, so the functions reproduce the published known-answer tests
 * byte for byte and the library never draws randomness of its own.
 *
 *   ML-KEM-1024          FIPS 203, category 5    mlkem1024/ (pq-crystals kyber ref)
 *   HQC-5                HQC v5.0.0, category 5  hqc5/      (official HQC ref)
 *   ML-DSA-87            FIPS 204, category 5    mldsa/     (pq-crystals dilithium ref, mode 5)
 *   SLH-DSA-SHAKE-256s   FIPS 205, category 5    slhdsa/    (slhdsa-c)
 *
 * ML-KEM-768, ML-DSA-65 and X25519 come from the existing kernel modules
 * (src/mlkem, pq_mldsa65.c, src/tls/x25519.c).
 *
 * Most callers want pq_matrix.h, not this header.
 */
#ifndef PQ_MATRIX_ALGS_H
#define PQ_MATRIX_ALGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- ML-KEM-1024 (FIPS 203) ------------------------------------------- */
#define PQM_MLKEM1024_EK_BYTES 1568u
#define PQM_MLKEM1024_DK_BYTES 3168u
#define PQM_MLKEM1024_CT_BYTES 1568u
#define PQM_SS_BYTES           32u

/* FIPS 203 Algorithm 16 (ML-KEM.KeyGen_internal): d, z are 32 fresh bytes. */
void pqm_mlkem1024_keygen(const uint8_t d[32], const uint8_t z[32],
                          uint8_t ek[PQM_MLKEM1024_EK_BYTES], uint8_t dk[PQM_MLKEM1024_DK_BYTES]);
/* FIPS 203 Algorithm 17 with the section 7.2 input check. Returns false,
 * and writes nothing usable, if ek fails the modulus check. */
bool pqm_mlkem1024_encaps(const uint8_t ek[PQM_MLKEM1024_EK_BYTES], const uint8_t m[32],
                          uint8_t ct[PQM_MLKEM1024_CT_BYTES], uint8_t ss[PQM_SS_BYTES]);
/* FIPS 203 Algorithm 18: always produces a secret (implicit rejection). */
void pqm_mlkem1024_decaps(const uint8_t dk[PQM_MLKEM1024_DK_BYTES],
                          const uint8_t ct[PQM_MLKEM1024_CT_BYTES], uint8_t ss[PQM_SS_BYTES]);
/* FIPS 203 section 7.2 / 7.3 input checks. */
bool pqm_mlkem1024_check_ek(const uint8_t ek[PQM_MLKEM1024_EK_BYTES]);
bool pqm_mlkem1024_check_dk(const uint8_t dk[PQM_MLKEM1024_DK_BYTES]);

/* ---- HQC-5 (HQC specification v5.0.0, 2025-08-22) --------------------- */
#define PQM_HQC5_PK_BYTES   7237u
#define PQM_HQC5_SK_BYTES   7333u
#define PQM_HQC5_CT_BYTES   14421u
#define PQM_HQC5_SALT_BYTES 16u

/* seed_kem: 32 fresh bytes (the first PRNG draw of HQC KeyGen). */
void pqm_hqc5_keygen(const uint8_t seed_kem[32], uint8_t pk[PQM_HQC5_PK_BYTES],
                     uint8_t sk[PQM_HQC5_SK_BYTES]);
/* m: 32 fresh bytes, salt: 16 fresh bytes (HQC Encaps draws them in that
 * order). */
void pqm_hqc5_encaps(const uint8_t pk[PQM_HQC5_PK_BYTES], const uint8_t m[32],
                     const uint8_t salt[PQM_HQC5_SALT_BYTES], uint8_t ct[PQM_HQC5_CT_BYTES],
                     uint8_t ss[PQM_SS_BYTES]);
/* Always produces a secret (implicit rejection with sigma). */
void pqm_hqc5_decaps(const uint8_t sk[PQM_HQC5_SK_BYTES], const uint8_t ct[PQM_HQC5_CT_BYTES],
                     uint8_t ss[PQM_SS_BYTES]);

/* ---- ML-DSA-87 (FIPS 204) --------------------------------------------- */
#define PQM_MLDSA87_PK_BYTES  2592u
#define PQM_MLDSA87_SK_BYTES  4896u
#define PQM_MLDSA87_SIG_BYTES 4627u
#define PQM_MLDSA_CTX_BYTES   255u

/* FIPS 204 Algorithm 6 (ML-DSA.KeyGen_internal) from a 32-byte seed. */
void pqm_mldsa87_keygen(const uint8_t seed[32], uint8_t pk[PQM_MLDSA87_PK_BYTES],
                        uint8_t sk[PQM_MLDSA87_SK_BYTES]);
/* FIPS 204 Algorithm 2 (pure ML-DSA.Sign). rnd = NULL is the deterministic
 * variant; otherwise 32 fresh bytes (hedged). */
void pqm_mldsa87_sign(const uint8_t sk[PQM_MLDSA87_SK_BYTES], const uint8_t *msg, size_t msg_len,
                      const uint8_t *ctx, size_t ctx_len, const uint8_t *rnd,
                      uint8_t sig[PQM_MLDSA87_SIG_BYTES]);
bool pqm_mldsa87_verify(const uint8_t pk[PQM_MLDSA87_PK_BYTES], const uint8_t *msg, size_t msg_len,
                        const uint8_t *ctx, size_t ctx_len,
                        const uint8_t sig[PQM_MLDSA87_SIG_BYTES]);

/* ---- SLH-DSA-SHAKE-256s (FIPS 205) ------------------------------------ */
#define PQM_SLH256S_PK_BYTES  64u
#define PQM_SLH256S_SK_BYTES  128u
#define PQM_SLH256S_SIG_BYTES 29792u
#define PQM_SLH256S_N         32u

/* seed = SK.seed || SK.prf || PK.seed, 32 bytes each (FIPS 205 Alg 18). */
void pqm_slh256s_keygen(const uint8_t seed[3 * PQM_SLH256S_N], uint8_t pk[PQM_SLH256S_PK_BYTES],
                        uint8_t sk[PQM_SLH256S_SK_BYTES]);
/* Pure SLH-DSA.Sign (Alg 22). addrnd = NULL is the deterministic variant;
 * otherwise 32 fresh bytes. */
void pqm_slh256s_sign(const uint8_t sk[PQM_SLH256S_SK_BYTES], const uint8_t *msg, size_t msg_len,
                      const uint8_t *ctx, size_t ctx_len, const uint8_t *addrnd,
                      uint8_t sig[PQM_SLH256S_SIG_BYTES]);
bool pqm_slh256s_verify(const uint8_t pk[PQM_SLH256S_PK_BYTES], const uint8_t *msg, size_t msg_len,
                        const uint8_t *ctx, size_t ctx_len,
                        const uint8_t sig[PQM_SLH256S_SIG_BYTES]);

#endif /* PQ_MATRIX_ALGS_H */
