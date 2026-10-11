/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_crypto.c — ABI 1.1: ML-KEM-1024 and ML-DSA-87 (kernel/src/pqsec,
 * vendored pq-crystals reference code) and Ed25519 (kernel/third_party/
 * ed25519). Every secret this file copies (seeds, randomness, expanded
 * keys) is wiped before it returns; calls are serialized by one mutex
 * because the vendored ML-DSA keygen hands its seed through a static. */
#define _POSIX_C_SOURCE 200809L
#include "zxv_internal.h"

#ifndef ZXV_NO_PQC

#include <pthread.h>
#include <string.h>

#include "ed25519.h"
#include "pq_matrix_algs.h"

_Static_assert(ZXV_MLKEM1024_EK_BYTES == PQM_MLKEM1024_EK_BYTES, "ek size");
_Static_assert(ZXV_MLKEM1024_DK_BYTES == PQM_MLKEM1024_DK_BYTES, "dk size");
_Static_assert(ZXV_MLKEM1024_CT_BYTES == PQM_MLKEM1024_CT_BYTES, "ct size");
_Static_assert(ZXV_MLDSA87_PK_BYTES == PQM_MLDSA87_PK_BYTES, "pk size");
_Static_assert(ZXV_MLDSA87_SK_BYTES == PQM_MLDSA87_SK_BYTES, "sk size");
_Static_assert(ZXV_MLDSA87_SIG_BYTES == PQM_MLDSA87_SIG_BYTES, "sig size");

static pthread_mutex_t g_pq = PTHREAD_MUTEX_INITIALIZER;

#define GATE(bit, name)                                         \
    int32_t rc = zxvi_cap_gate(cap, (bit), (name), NULL);       \
    if (rc) return rc

int32_t zxv_mlkem1024_keygen_derand(const zxv_cap *cap, const uint8_t d[32], const uint8_t z[32],
                                    uint8_t ek[1568], uint8_t dk[3168])
{
    GATE(ZXV_CAP_CRYPTO_KEM, "mlkem1024_keygen");
    if (!d || !z || !ek || !dk) return zxvi_fail(ZXV_E_ARG, "NULL argument");
    pthread_mutex_lock(&g_pq);
    pqm_mlkem1024_keygen(d, z, ek, dk);
    pthread_mutex_unlock(&g_pq);
    return zxvi_ok();
}

int32_t zxv_mlkem1024_keygen(const zxv_cap *cap, uint8_t ek[1568], uint8_t dk[3168])
{
    uint8_t dz[64];
    int32_t rc = zxv_random(dz, sizeof dz);
    if (rc == ZXV_OK) rc = zxv_mlkem1024_keygen_derand(cap, dz, dz + 32, ek, dk);
    zxvi_wipe(dz, sizeof dz);
    return rc;
}

int32_t zxv_mlkem1024_encaps_derand(const zxv_cap *cap, const uint8_t ek[1568], const uint8_t m[32],
                                    uint8_t ct[1568], uint8_t ss[32])
{
    GATE(ZXV_CAP_CRYPTO_KEM, "mlkem1024_encaps");
    if (!ek || !m || !ct || !ss) return zxvi_fail(ZXV_E_ARG, "NULL argument");
    pthread_mutex_lock(&g_pq);
    bool okay = pqm_mlkem1024_encaps(ek, m, ct, ss);
    pthread_mutex_unlock(&g_pq);
    if (!okay) {
        zxvi_wipe(ss, 32);
        return zxvi_fail(ZXV_E_FORMAT, "encapsulation key fails the FIPS 203 modulus check");
    }
    return zxvi_ok();
}

int32_t zxv_mlkem1024_encaps(const zxv_cap *cap, const uint8_t ek[1568], uint8_t ct[1568],
                             uint8_t ss[32])
{
    uint8_t m[32];
    int32_t rc = zxv_random(m, sizeof m);
    if (rc == ZXV_OK) rc = zxv_mlkem1024_encaps_derand(cap, ek, m, ct, ss);
    zxvi_wipe(m, sizeof m);
    return rc;
}

int32_t zxv_mlkem1024_decaps(const zxv_cap *cap, const uint8_t dk[3168], const uint8_t ct[1568],
                             uint8_t ss[32])
{
    GATE(ZXV_CAP_CRYPTO_KEM, "mlkem1024_decaps");
    if (!dk || !ct || !ss) return zxvi_fail(ZXV_E_ARG, "NULL argument");
    pthread_mutex_lock(&g_pq);
    bool okay = pqm_mlkem1024_check_dk(dk);
    if (okay) pqm_mlkem1024_decaps(dk, ct, ss);
    pthread_mutex_unlock(&g_pq);
    if (!okay) return zxvi_fail(ZXV_E_FORMAT, "decapsulation key fails the FIPS 203 hash check");
    return zxvi_ok();
}

int32_t zxv_mldsa87_keygen_seed(const zxv_cap *cap, const uint8_t seed[32], uint8_t pk[2592],
                                uint8_t sk[4896])
{
    GATE(ZXV_CAP_CRYPTO_SIGN, "mldsa87_keygen");
    if (!seed || !pk || !sk) return zxvi_fail(ZXV_E_ARG, "NULL argument");
    pthread_mutex_lock(&g_pq);
    pqm_mldsa87_keygen(seed, pk, sk);
    pthread_mutex_unlock(&g_pq);
    return zxvi_ok();
}

int32_t zxv_mldsa87_keygen(const zxv_cap *cap, uint8_t pk[2592], uint8_t sk[4896])
{
    uint8_t seed[32];
    int32_t rc = zxv_random(seed, sizeof seed);
    if (rc == ZXV_OK) rc = zxv_mldsa87_keygen_seed(cap, seed, pk, sk);
    zxvi_wipe(seed, sizeof seed);
    return rc;
}

static int32_t mldsa_sign(const zxv_cap *cap, const uint8_t *sk, const uint8_t *msg,
                          size_t msg_len, const uint8_t *ctx, size_t ctx_len, const uint8_t *rnd,
                          uint8_t *sig)
{
    GATE(ZXV_CAP_CRYPTO_SIGN, "mldsa87_sign");
    if (!sk || !sig || (!msg && msg_len) || (!ctx && ctx_len))
        return zxvi_fail(ZXV_E_ARG, "NULL argument");
    if (ctx_len > ZXV_MLDSA_CTX_MAX)
        return zxvi_fail(ZXV_E_TOO_LONG, "context is %zu bytes; maximum is 255", ctx_len);
    pthread_mutex_lock(&g_pq);
    pqm_mldsa87_sign(sk, msg ? msg : (const uint8_t *) "", msg_len, ctx, ctx_len, rnd, sig);
    pthread_mutex_unlock(&g_pq);
    return zxvi_ok();
}

int32_t zxv_mldsa87_sign(const zxv_cap *cap, const uint8_t sk[4896], const uint8_t *msg,
                         size_t msg_len, const uint8_t *ctx, size_t ctx_len, uint8_t sig[4627])
{
    uint8_t rnd[32];
    int32_t rc = zxv_random(rnd, sizeof rnd);
    if (rc == ZXV_OK) rc = mldsa_sign(cap, sk, msg, msg_len, ctx, ctx_len, rnd, sig);
    zxvi_wipe(rnd, sizeof rnd);
    return rc;
}

int32_t zxv_mldsa87_sign_deterministic(const zxv_cap *cap, const uint8_t sk[4896],
                                       const uint8_t *msg, size_t msg_len, const uint8_t *ctx,
                                       size_t ctx_len, uint8_t sig[4627])
{
    return mldsa_sign(cap, sk, msg, msg_len, ctx, ctx_len, NULL, sig);
}

int32_t zxv_mldsa87_verify(const zxv_cap *cap, const uint8_t pk[2592], const uint8_t *msg,
                           size_t msg_len, const uint8_t *ctx, size_t ctx_len,
                           const uint8_t sig[4627])
{
    GATE(ZXV_CAP_CRYPTO_SIGN, "mldsa87_verify");
    if (!pk || !sig || (!msg && msg_len) || (!ctx && ctx_len))
        return zxvi_fail(ZXV_E_ARG, "NULL argument");
    if (ctx_len > ZXV_MLDSA_CTX_MAX)
        return zxvi_fail(ZXV_E_TOO_LONG, "context is %zu bytes; maximum is 255", ctx_len);
    pthread_mutex_lock(&g_pq);
    bool okay = pqm_mldsa87_verify(pk, msg ? msg : (const uint8_t *) "", msg_len, ctx, ctx_len, sig);
    pthread_mutex_unlock(&g_pq);
    return okay ? zxvi_ok() : zxvi_fail(ZXV_E_VERIFY, "ML-DSA-87 signature does not verify");
}

/* Ed25519: the RFC 8032 private key is the 32-byte seed; the library's
 * 64-byte expanded key exists only on this stack and is wiped. */
int32_t zxv_ed25519_public(const zxv_cap *cap, const uint8_t sk[32], uint8_t pk[32])
{
    GATE(ZXV_CAP_CRYPTO_SIGN, "ed25519_public");
    if (!sk || !pk) return zxvi_fail(ZXV_E_ARG, "NULL argument");
    uint8_t ex[64];
    ed25519_create_keypair(pk, ex, sk);
    zxvi_wipe(ex, sizeof ex);
    return zxvi_ok();
}

int32_t zxv_ed25519_keygen(const zxv_cap *cap, uint8_t pk[32], uint8_t sk[32])
{
    if (!pk || !sk) return zxvi_fail(ZXV_E_ARG, "NULL argument");
    int32_t rc = zxvi_cap_gate(cap, ZXV_CAP_CRYPTO_SIGN, "ed25519_keygen", NULL);
    if (rc) return rc;
    if ((rc = zxv_random(sk, 32))) return rc;
    return zxv_ed25519_public(cap, sk, pk);
}

int32_t zxv_ed25519_sign(const zxv_cap *cap, const uint8_t sk[32], const uint8_t *msg,
                         size_t msg_len, uint8_t sig[64])
{
    GATE(ZXV_CAP_CRYPTO_SIGN, "ed25519_sign");
    if (!sk || !sig || (!msg && msg_len)) return zxvi_fail(ZXV_E_ARG, "NULL argument");
    uint8_t pk[32], ex[64];
    ed25519_create_keypair(pk, ex, sk);
    ed25519_sign(sig, msg ? msg : (const uint8_t *) "", msg_len, pk, ex);
    zxvi_wipe(ex, sizeof ex);
    return zxvi_ok();
}

int32_t zxv_ed25519_verify(const zxv_cap *cap, const uint8_t pk[32], const uint8_t *msg,
                           size_t msg_len, const uint8_t sig[64])
{
    GATE(ZXV_CAP_CRYPTO_SIGN, "ed25519_verify");
    if (!pk || !sig || (!msg && msg_len)) return zxvi_fail(ZXV_E_ARG, "NULL argument");
    return ed25519_verify(sig, msg ? msg : (const uint8_t *) "", msg_len, pk)
               ? zxvi_ok()
               : zxvi_fail(ZXV_E_VERIFY, "Ed25519 signature does not verify");
}

#else /* ZXV_NO_PQC: a build without the crypto sources (e.g. a minimal Wasm). */

#define NI return zxvi_fail(ZXV_E_NOTIMPL, "built without post-quantum crypto (ZXV_NO_PQC)")
int32_t zxv_mlkem1024_keygen(const zxv_cap *c, uint8_t e[1568], uint8_t d[3168]) { (void) c; (void) e; (void) d; NI; }
int32_t zxv_mlkem1024_keygen_derand(const zxv_cap *c, const uint8_t d[32], const uint8_t z[32], uint8_t e[1568], uint8_t k[3168]) { (void) c; (void) d; (void) z; (void) e; (void) k; NI; }
int32_t zxv_mlkem1024_encaps(const zxv_cap *c, const uint8_t e[1568], uint8_t t[1568], uint8_t s[32]) { (void) c; (void) e; (void) t; (void) s; NI; }
int32_t zxv_mlkem1024_encaps_derand(const zxv_cap *c, const uint8_t e[1568], const uint8_t m[32], uint8_t t[1568], uint8_t s[32]) { (void) c; (void) e; (void) m; (void) t; (void) s; NI; }
int32_t zxv_mlkem1024_decaps(const zxv_cap *c, const uint8_t d[3168], const uint8_t t[1568], uint8_t s[32]) { (void) c; (void) d; (void) t; (void) s; NI; }
int32_t zxv_mldsa87_keygen(const zxv_cap *c, uint8_t p[2592], uint8_t s[4896]) { (void) c; (void) p; (void) s; NI; }
int32_t zxv_mldsa87_keygen_seed(const zxv_cap *c, const uint8_t d[32], uint8_t p[2592], uint8_t s[4896]) { (void) c; (void) d; (void) p; (void) s; NI; }
int32_t zxv_mldsa87_sign(const zxv_cap *c, const uint8_t s[4896], const uint8_t *m, size_t n, const uint8_t *x, size_t xn, uint8_t g[4627]) { (void) c; (void) s; (void) m; (void) n; (void) x; (void) xn; (void) g; NI; }
int32_t zxv_mldsa87_sign_deterministic(const zxv_cap *c, const uint8_t s[4896], const uint8_t *m, size_t n, const uint8_t *x, size_t xn, uint8_t g[4627]) { (void) c; (void) s; (void) m; (void) n; (void) x; (void) xn; (void) g; NI; }
int32_t zxv_mldsa87_verify(const zxv_cap *c, const uint8_t p[2592], const uint8_t *m, size_t n, const uint8_t *x, size_t xn, const uint8_t g[4627]) { (void) c; (void) p; (void) m; (void) n; (void) x; (void) xn; (void) g; NI; }
int32_t zxv_ed25519_keygen(const zxv_cap *c, uint8_t p[32], uint8_t s[32]) { (void) c; (void) p; (void) s; NI; }
int32_t zxv_ed25519_public(const zxv_cap *c, const uint8_t s[32], uint8_t p[32]) { (void) c; (void) p; (void) s; NI; }
int32_t zxv_ed25519_sign(const zxv_cap *c, const uint8_t s[32], const uint8_t *m, size_t n, uint8_t g[64]) { (void) c; (void) s; (void) m; (void) n; (void) g; NI; }
int32_t zxv_ed25519_verify(const zxv_cap *c, const uint8_t p[32], const uint8_t *m, size_t n, const uint8_t g[64]) { (void) c; (void) p; (void) m; (void) n; (void) g; NI; }

#endif
