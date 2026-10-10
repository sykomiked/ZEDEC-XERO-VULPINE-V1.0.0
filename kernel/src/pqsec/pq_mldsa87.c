/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* pq_mldsa87.c — ML-DSA-87 (FIPS 204, category 5) for pq_matrix.
 *
 * The same vendored pq-crystals dilithium reference that pq_mldsa65.c
 * uses (mldsa/, CC0 / Apache-2.0), compiled a second time here in mode 5
 * as one translation unit. mldsa/config.h fixes DILITHIUM_MODE to 3 for
 * the mode-3 build, so this unit supplies its own config in front of it
 * (CONFIG_H is the vendored file's include guard) and the mode-3 build
 * is untouched. The reference namespaces every mode-dependent symbol
 * (pqcrystals_dilithium5_ref_*); its Keccak (pqcrystals_dilithium_fips202_
 * ref_*) is mode-independent and comes from the mode-3 build's
 * mldsa/fips202.c, so link this with PQSIG_SRCS.
 *
 * Checked against NIST ACVP ML-DSA-87 vectors in test_pq_matrix.c.
 */
#define CONFIG_H
#define DILITHIUM_MODE 5
#define DILITHIUM_RANDOMIZED_SIGNING
#define CRYPTO_ALGNAME         "Dilithium5"
#define DILITHIUM_NAMESPACETOP pqcrystals_dilithium5_ref
#define DILITHIUM_NAMESPACE(s) pqcrystals_dilithium5_ref_##s

/* keypair() draws its seed from randombytes(). The mode-3 glue owns the
 * zxv_mldsa_randombytes symbol, so this unit renames its own. */
#include "mldsa/randombytes.h"
#undef randombytes
#define randombytes zxv_mldsa87_randombytes
void randombytes(uint8_t *out, size_t outlen);

#include "mldsa/ntt.c"
#include "mldsa/packing.c"
#include "mldsa/poly.c"
#include "mldsa/polyvec.c"
#include "mldsa/reduce.c"
#include "mldsa/rounding.c"
#include "mldsa/sign.c"
#include "mldsa/symmetric-shake.c"

#include "pq_matrix_algs.h"

#if CRYPTO_PUBLICKEYBYTES != PQM_MLDSA87_PK_BYTES ||                                               \
    CRYPTO_SECRETKEYBYTES != PQM_MLDSA87_SK_BYTES || CRYPTO_BYTES != PQM_MLDSA87_SIG_BYTES
#    error "vendored ML-DSA parameters do not match ML-DSA-87"
#endif

/* Hands keygen's caller seed to keypair() for the one call. Single-
 * threaded by design, like pq_mldsa65.c. */
static const uint8_t *g_seed87;

void randombytes(uint8_t *out, size_t outlen)
{
    for (size_t i = 0; i < outlen; i++) out[i] = g_seed87 ? g_seed87[i % SEEDBYTES] : 0;
}

void pqm_mldsa87_keygen(const uint8_t seed[32], uint8_t pk[PQM_MLDSA87_PK_BYTES],
                        uint8_t sk[PQM_MLDSA87_SK_BYTES])
{
    g_seed87 = seed;
    crypto_sign_keypair(pk, sk);
    g_seed87 = 0;
}

void pqm_mldsa87_sign(const uint8_t sk[PQM_MLDSA87_SK_BYTES], const uint8_t *msg, size_t msg_len,
                      const uint8_t *ctx, size_t ctx_len, const uint8_t *rnd,
                      uint8_t sig[PQM_MLDSA87_SIG_BYTES])
{
    for (unsigned i = 0; i < PQM_MLDSA87_SIG_BYTES; i++) sig[i] = 0;
    if ((!msg && msg_len) || (!ctx && ctx_len) || ctx_len > PQM_MLDSA_CTX_BYTES) return;
    /* FIPS 204 Algorithm 2: M' = 0 || |ctx| || ctx || M. */
    uint8_t pre[2 + PQM_MLDSA_CTX_BYTES];
    uint8_t zero[RNDBYTES] = {0};
    static const uint8_t none[1] = {0};
    pre[0] = 0;
    pre[1] = (uint8_t) ctx_len;
    for (size_t i = 0; i < ctx_len; i++) pre[2 + i] = ctx[i];
    size_t siglen = 0;
    crypto_sign_signature_internal(sig, &siglen, msg ? msg : none, msg_len, pre, 2 + ctx_len,
                                   rnd ? rnd : zero, sk);
}

bool pqm_mldsa87_verify(const uint8_t pk[PQM_MLDSA87_PK_BYTES], const uint8_t *msg, size_t msg_len,
                        const uint8_t *ctx, size_t ctx_len,
                        const uint8_t sig[PQM_MLDSA87_SIG_BYTES])
{
    if ((!msg && msg_len) || (!ctx && ctx_len) || ctx_len > PQM_MLDSA_CTX_BYTES) return false;
    static const uint8_t none[1] = {0};
    return crypto_sign_verify(sig, PQM_MLDSA87_SIG_BYTES, msg ? msg : none, msg_len,
                              ctx ? ctx : none, ctx_len, pk) == 0;
}
