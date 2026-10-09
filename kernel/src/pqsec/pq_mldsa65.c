/* pq_mldsa65.c — ML-DSA-65 (FIPS 204) for the pqOS security layer.
 *
 * Thin glue over the pq-crystals reference implementation vendored in
 * mldsa/ (CC0 / Apache-2.0, see mldsa/LICENSE and mldsa/README.zxv),
 * built in mode 3, which is ML-DSA-65. Checked against the NIST ACVP
 * vectors in test_pq_kat.c.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 */
#include "pq_security.h"
#include "mldsa/params.h"
#include "mldsa/sign.h"
#include "mldsa/randombytes.h"

#if CRYPTO_PUBLICKEYBYTES != PQ_MLDSA65_PK_BYTES ||                                                \
    CRYPTO_SECRETKEYBYTES != PQ_MLDSA65_SK_BYTES || CRYPTO_BYTES != PQ_MLDSA65_SIG_BYTES
#    error "vendored ML-DSA parameters do not match ML-DSA-65"
#endif

/* The reference keypair() draws its 32-byte seed from randombytes(). The
 * kernel has no system RNG at this layer, so keygen hands its caller's seed
 * over through this slot for the one call. Single-threaded by design, like
 * the rest of the security layer. */
static const uint8_t *g_seed;

void randombytes(uint8_t *out, size_t outlen)
{
    for (size_t i = 0; i < outlen; i++) out[i] = g_seed ? g_seed[i % SEEDBYTES] : 0;
}

void pq_mldsa65_keygen(const uint8_t seed[32], uint8_t pk[PQ_MLDSA65_PK_BYTES],
                       uint8_t sk[PQ_MLDSA65_SK_BYTES])
{
    if (!seed || !pk || !sk) return;
    g_seed = seed;
    crypto_sign_keypair(pk, sk);
    g_seed = 0;
}

void pq_mldsa65_sign(const uint8_t sk[PQ_MLDSA65_SK_BYTES], const uint8_t *msg, uint32_t msg_len,
                     const uint8_t *ctx, uint32_t ctx_len, const uint8_t rnd[32],
                     uint8_t sig[PQ_MLDSA65_SIG_BYTES])
{
    if (!sig) return;
    for (uint32_t i = 0; i < PQ_MLDSA65_SIG_BYTES; i++) sig[i] = 0;
    if (!sk || (!msg && msg_len) || (!ctx && ctx_len) || ctx_len > PQ_MLDSA65_CTX_BYTES) return;

    /* FIPS 204 Algorithm 2: M' = 0 || |ctx| || ctx || M. rnd = 0^32 is the
     * deterministic variant; otherwise the hedged one. */
    uint8_t pre[2 + PQ_MLDSA65_CTX_BYTES];
    uint8_t zero[RNDBYTES] = {0};
    pre[0] = 0;
    pre[1] = (uint8_t) ctx_len;
    for (uint32_t i = 0; i < ctx_len; i++) pre[2 + i] = ctx[i];
    size_t siglen = 0;
    crypto_sign_signature_internal(sig, &siglen, msg, msg_len, pre, 2 + ctx_len, rnd ? rnd : zero,
                                   sk);
}

bool pq_mldsa65_verify(const uint8_t pk[PQ_MLDSA65_PK_BYTES], const uint8_t *msg, uint32_t msg_len,
                       const uint8_t *ctx, uint32_t ctx_len,
                       const uint8_t sig[PQ_MLDSA65_SIG_BYTES])
{
    if (!pk || !sig || (!msg && msg_len) || (!ctx && ctx_len) || ctx_len > PQ_MLDSA65_CTX_BYTES)
        return false;
    static const uint8_t none[1] = {0};
    return crypto_sign_verify(sig, PQ_MLDSA65_SIG_BYTES, msg ? msg : none, msg_len,
                              ctx ? ctx : none, ctx_len, pk) == 0;
}
