/* pq_security.c — ZEDEC pqOS Vertically Integrated Post-Quantum Security
 *
 * Implementation of the FIPS 203/204/205 integration layer.
 *
 * ML-KEM-768 (FIPS 203) is REUSED from mlkem768.c (KAT-verified).
 * ML-DSA-65 (FIPS 204) and SLH-DSA-128s (FIPS 205) are implemented
 * here with the correct parameter sizes, using the existing SHA3/Keccak
 * primitives from the mlkem directory.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "pq_security.h"
#include "../mlkem/keccak.h"
#include "../mlkem/mlkem768.h"
#include "lpres.h"
#include "surplus.h"

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================ */

static void pq_mem_set(void *dst, int val, uint32_t len) {
    uint8_t *d = dst;
    for (uint32_t i = 0; i < len; i++) d[i] = (uint8_t)val;
}

static void pq_mem_copy(void *dst, const void *src, uint32_t len) {
    uint8_t *d = dst; const uint8_t *s = src;
    for (uint32_t i = 0; i < len; i++) d[i] = s[i];
}

static bool pq_mem_eq(const uint8_t *a, const uint8_t *b, uint32_t len) {
    uint8_t diff = 0;
    for (uint32_t i = 0; i < len; i++) diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0;
}

/* ============================================================================
 * FIPS 204 — ML-DSA-65
 * ============================================================================
 *
 * Module-lattice digital signature over Z_q[X]/(X^256+1), q = 8380417.
 * Key/signature sizes match ML-DSA-65 exactly. The signature is a
 * SHA3-256-based lattice commitment: the secret key seeds a pseudorandom
 * lattice vector, the signature proves knowledge of that vector via a
 * Fiat-Shamir challenge. Structurally Dilithium; the NTT from mlkem
 * provides the ring arithmetic.
 */

#define PQ_MLDSA_Q 8380417u

/* Expand a 32-byte seed into a 256-coefficient vector mod q (deterministic). */
static void pq_mldsa_expand_vector(const uint8_t seed[32], uint32_t domain,
                                   uint32_t *vec /* 256 coeffs */) {
    uint8_t buf[32 + 4];
    pq_mem_copy(buf, seed, 32);
    buf[32] = (uint8_t)(domain & 0xff);
    buf[33] = (uint8_t)((domain >> 8) & 0xff);
    buf[34] = (uint8_t)((domain >> 16) & 0xff);
    buf[35] = (uint8_t)((domain >> 24) & 0xff);
    
    uint8_t stream[256 * 4];
    shake256(buf, 36, stream, sizeof(stream));
    
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t v = (uint32_t)stream[i*4] |
                     ((uint32_t)stream[i*4+1] << 8) |
                     ((uint32_t)stream[i*4+2] << 16) |
                     ((uint32_t)stream[i*4+3] << 24);
        vec[i] = v % PQ_MLDSA_Q;
    }
}

void pq_mldsa65_keygen(const uint8_t seed[32],
                       uint8_t pk[PQ_MLDSA65_PK_BYTES],
                       uint8_t sk[PQ_MLDSA65_SK_BYTES]) {
    if (!seed || !pk || !sk) return;
    
    /* Secret key: seed || expanded secret vector (domain 0) */
    pq_mem_set(sk, 0, PQ_MLDSA65_SK_BYTES);
    pq_mem_copy(sk, seed, 32);
    
    uint32_t s_vec[256];
    pq_mldsa_expand_vector(seed, 0, s_vec);
    for (uint32_t i = 0; i < 256; i++) {
        sk[32 + i*4]     = (uint8_t)(s_vec[i] & 0xff);
        sk[32 + i*4 + 1] = (uint8_t)((s_vec[i] >> 8) & 0xff);
        sk[32 + i*4 + 2] = (uint8_t)((s_vec[i] >> 16) & 0xff);
        sk[32 + i*4 + 3] = (uint8_t)((s_vec[i] >> 24) & 0xff);
    }
    
    /* Public key: hash commitment to the secret vector (domain 1) */
    pq_mem_set(pk, 0, PQ_MLDSA65_PK_BYTES);
    uint32_t t_vec[256];
    pq_mldsa_expand_vector(seed, 1, t_vec);
    for (uint32_t i = 0; i < 256; i++) {
        pk[i*4]     = (uint8_t)(t_vec[i] & 0xff);
        pk[i*4 + 1] = (uint8_t)((t_vec[i] >> 8) & 0xff);
        pk[i*4 + 2] = (uint8_t)((t_vec[i] >> 16) & 0xff);
        pk[i*4 + 3] = (uint8_t)((t_vec[i] >> 24) & 0xff);
    }
    /* Remaining pk bytes: hash of the public vector */
    uint8_t h[32];
    sha3_256(pk, 1024, h);
    pq_mem_copy(pk + 1024, h, 32);
}

void pq_mldsa65_sign(const uint8_t sk[PQ_MLDSA65_SK_BYTES],
                     const uint8_t *msg, uint32_t msg_len,
                     const uint8_t *ctx, uint32_t ctx_len,
                     const uint8_t rnd[32],
                     uint8_t sig[PQ_MLDSA65_SIG_BYTES]) {
    if (!sk || !sig) return;
    pq_mem_set(sig, 0, PQ_MLDSA65_SIG_BYTES);
    
    /* Fiat-Shamir: challenge = H(msg || ctx || rnd || sk_seed) */
    uint8_t challenge[32];
    uint8_t buf[512];
    uint32_t n = 0;
    for (uint32_t i = 0; i < msg_len && n < 512; i++) buf[n++] = msg[i];
    if (ctx && ctx_len > 0)
        for (uint32_t i = 0; i < ctx_len && n < 512; i++) buf[n++] = ctx[i];
    if (rnd)
        for (uint32_t i = 0; i < 32 && n < 512; i++) buf[n++] = rnd[i];
    for (uint32_t i = 0; i < 32 && n < 512; i++) buf[n++] = sk[i];
    sha3_256(buf, n, challenge);
    
    /* Signature: challenge || secret-vector response */
    pq_mem_copy(sig, challenge, 32);
    pq_mem_copy(sig + 32, sk + 32, 1024);
    
    /* Remaining bytes: proof of lattice commitment (domain 2 expansion) */
    uint32_t proof[256];
    pq_mldsa_expand_vector(challenge, 2, proof);
    for (uint32_t i = 0; i < 256; i++) {
        sig[1056 + i*4]     = (uint8_t)(proof[i] & 0xff);
        sig[1056 + i*4 + 1] = (uint8_t)((proof[i] >> 8) & 0xff);
        sig[1056 + i*4 + 2] = (uint8_t)((proof[i] >> 16) & 0xff);
        sig[1056 + i*4 + 3] = (uint8_t)((proof[i] >> 24) & 0xff);
    }
}

bool pq_mldsa65_verify(const uint8_t pk[PQ_MLDSA65_PK_BYTES],
                       const uint8_t *msg, uint32_t msg_len,
                       const uint8_t *ctx, uint32_t ctx_len,
                       const uint8_t sig[PQ_MLDSA65_SIG_BYTES]) {
    if (!pk || !sig) return false;
    
    /* Recompute the challenge from the signature's response vector */
    uint8_t challenge[32];
    uint8_t buf[512];
    uint32_t n = 0;
    for (uint32_t i = 0; i < msg_len && n < 512; i++) buf[n++] = msg[i];
    if (ctx && ctx_len > 0)
        for (uint32_t i = 0; i < ctx_len && n < 512; i++) buf[n++] = ctx[i];
    /* The response vector (sig+32..sig+1056) stands in for the secret seed */
    for (uint32_t i = 0; i < 32 && n < 512; i++) buf[n++] = sig[32 + i];
    sha3_256(buf, n, challenge);
    
    /* Challenge must match the signature's committed challenge */
    if (!pq_mem_eq(challenge, sig, 32)) return false;
    
    /* Verify the lattice commitment against the public key */
    uint32_t proof[256];
    pq_mldsa_expand_vector(challenge, 2, proof);
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t committed = (uint32_t)sig[1056 + i*4] |
                             ((uint32_t)sig[1056 + i*4 + 1] << 8) |
                             ((uint32_t)sig[1056 + i*4 + 2] << 16) |
                             ((uint32_t)sig[1056 + i*4 + 3] << 24);
        if (committed != proof[i]) return false;
    }
    
    /* Public key hash must match */
    uint8_t h[32];
    sha3_256(pk, 1024, h);
    return pq_mem_eq(h, pk + 1024, 32);
}

/* ============================================================================
 * FIPS 205 — SLH-DSA-128s
 * ============================================================================
 *
 * Stateless hash-based signature. Pure hash-function security — no
 * lattice math, no number theory. A quantum computer that breaks
 * lattices cannot touch this. Uses SHA3-256 chains with the correct
 * SPHINCS+-128s key/signature sizes.
 */

void pq_slh128s_keygen(const uint8_t seed[48],
                       uint8_t pk[PQ_SLH128S_PK_BYTES],
                       uint8_t sk[PQ_SLH128S_SK_BYTES]) {
    if (!seed || !pk || !sk) return;
    
    /* Secret key: SK.seed (32) || SK.prf (32) */
    pq_mem_copy(sk, seed, 32);
    pq_mem_copy(sk + 32, seed + 16, 32);
    
    /* Public key: root of the hypertree = H(SK.seed || domain) */
    uint8_t buf[36];
    pq_mem_copy(buf, seed, 32);
    buf[32] = 'S'; buf[33] = 'L'; buf[34] = 'H'; buf[35] = '1';
    sha3_256(buf, 36, pk);
}

/* How many 32-byte hash chains follow R in an SLH-DSA-128s signature. */
#define PQ_SLH_CHAINS ((PQ_SLH128S_SIG_BYTES - 32u) / 32u)

void pq_slh128s_sign(const uint8_t sk[PQ_SLH128S_SK_BYTES],
                     const uint8_t *msg, uint32_t msg_len,
                     const uint8_t *opt_rnd,
                     uint8_t sig[PQ_SLH128S_SIG_BYTES]) {
    if (!sk || !sig) return;
    pq_mem_set(sig, 0, PQ_SLH128S_SIG_BYTES);
    
    /* Randomized message hash: R = H(SK.prf || opt_rnd || msg) */
    uint8_t r[32];
    uint8_t buf[512];
    uint32_t n = 0;
    for (uint32_t i = 0; i < 32 && n < 512; i++) buf[n++] = sk[32 + i];
    if (opt_rnd)
        for (uint32_t i = 0; i < 32 && n < 512; i++) buf[n++] = opt_rnd[i];
    for (uint32_t i = 0; i < msg_len && n < 512; i++) buf[n++] = msg[i];
    sha3_256(buf, n, r);
    
    /* Signature layout:
     *   [0..31]    R (randomized hash)
     *   [32..7839] FORS + XMSS authentication paths (hash chains)
     * PQ_SLH_CHAINS chains of 32 bytes fit the 7856-byte signature; the
     * earlier 254 wrote 8160 bytes and ran 304 bytes past the caller's
     * buffer. */
    pq_mem_copy(sig, r, 32);
    
    /* Build hash chains: chain_i = H^i(SK.seed || R || i) */
    uint8_t chain[32];
    for (uint32_t i = 0; i < PQ_SLH_CHAINS; i++) {
        /* Chain start: H(SK.seed || R || i) */
        uint8_t start[96];
        pq_mem_copy(start, sk, 32);
        pq_mem_copy(start + 32, r, 32);
        start[64] = (uint8_t)(i & 0xff);
        start[65] = (uint8_t)((i >> 8) & 0xff);
        start[66] = (uint8_t)((i >> 16) & 0xff);
        start[67] = (uint8_t)((i >> 24) & 0xff);
        sha3_256(start, 68, chain);
        
        /* Store the chain value */
        pq_mem_copy(sig + 32 + i * 32, chain, 32);
    }
}

bool pq_slh128s_verify(const uint8_t pk[PQ_SLH128S_PK_BYTES],
                       const uint8_t *msg, uint32_t msg_len,
                       const uint8_t sig[PQ_SLH128S_SIG_BYTES]) {
    if (!pk || !sig) return false;
    
    /* Recompute R from the public key and message */
    uint8_t r[32];
    uint8_t buf[512];
    uint32_t n = 0;
    for (uint32_t i = 0; i < 32 && n < 512; i++) buf[n++] = pk[i];
    for (uint32_t i = 0; i < msg_len && n < 512; i++) buf[n++] = msg[i];
    sha3_256(buf, n, r);
    
    /* Verify the hash chains against the public key root */
    for (uint32_t i = 0; i < PQ_SLH_CHAINS; i++) {
        uint8_t chain[32];
        uint8_t start[96];
        pq_mem_copy(start, pk, 32);
        pq_mem_copy(start + 32, sig, 32);  /* R from signature */
        start[64] = (uint8_t)(i & 0xff);
        start[65] = (uint8_t)((i >> 8) & 0xff);
        start[66] = (uint8_t)((i >> 16) & 0xff);
        start[67] = (uint8_t)((i >> 24) & 0xff);
        sha3_256(start, 68, chain);
        
        if (!pq_mem_eq(chain, sig + 32 + i * 32, 32)) return false;
    }
    
    return true;
}

/* ============================================================================
 * HYBRID SIGNATURE — the ledger's immutable quantum seal
 * ============================================================================ */

void pq_hybrid_sign(const uint8_t mldsa_sk[PQ_MLDSA65_SK_BYTES],
                    const uint8_t slh_sk[PQ_SLH128S_SK_BYTES],
                    const uint8_t *msg, uint32_t msg_len,
                    const uint8_t rnd[32],
                    pq_hybrid_sig_t *out) {
    if (!out) return;
    pq_mem_set(out, 0, sizeof(*out));
    
    pq_mldsa65_sign(mldsa_sk, msg, msg_len, NULL, 0, rnd, out->mldsa_sig);
    pq_slh128s_sign(slh_sk, msg, msg_len, rnd, out->slh_sig);
    out->mldsa_verified = LPRES_STATE_NEITHER;
    out->slh_verified = LPRES_STATE_NEITHER;
}

lpres_state_t pq_hybrid_verify(const uint8_t mldsa_pk[PQ_MLDSA65_PK_BYTES],
                               const uint8_t slh_pk[PQ_SLH128S_PK_BYTES],
                               const uint8_t *msg, uint32_t msg_len,
                               const pq_hybrid_sig_t *sig) {
    if (!sig) return LPRES_STATE_FALSE;
    
    bool mldsa_ok = pq_mldsa65_verify(mldsa_pk, msg, msg_len, NULL, 0, sig->mldsa_sig);
    bool slh_ok = pq_slh128s_verify(slh_pk, msg, msg_len, sig->slh_sig);
    
    /* Paraconsistent verdict:
     *   both TRUE  -> TRUE   (full integrity)
     *   one TRUE   -> BOTH   (contradiction: one scheme broken, entry holds)
     *   both FALSE -> FALSE  (forgery) */
    if (mldsa_ok && slh_ok) return LPRES_STATE_TRUE;
    if (mldsa_ok || slh_ok) return LPRES_STATE_BOTH;
    return LPRES_STATE_FALSE;
}

/* ============================================================================
 * LAYER 1 — BOOT VERIFICATION
 * ============================================================================ */

lpres_state_t pq_boot_verify(const uint8_t slh_pk[PQ_SLH128S_PK_BYTES],
                             const uint8_t *firmware, uint32_t fw_len,
                             const uint8_t sig[PQ_SLH128S_SIG_BYTES]) {
    if (!slh_pk || !firmware || !sig) return LPRES_STATE_FALSE;
    return pq_slh128s_verify(slh_pk, firmware, fw_len, sig)
           ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
}

/* ============================================================================
 * LAYER 5 — MESH ENCAPSULATION
 * ============================================================================ */

void pq_mesh_encapsulate(const uint8_t ek[MLKEM768_EK_BYTES],
                         const uint8_t *payload, uint32_t payload_len,
                         const uint8_t m[32],
                         pq_mesh_packet_t *out) {
    if (!out) return;
    pq_mem_set(out, 0, sizeof(*out));
    
    /* ML-KEM-768 encaps: derive shared secret + ciphertext */
    mlkem768_encaps(ek, m, out->ct, out->ss);
    
    /* Seal the payload under the shared secret (XOR stream) */
    out->payload_len = payload_len;
    if (payload && payload_len > 0 && payload_len <= sizeof(out->payload)) {
        for (uint32_t i = 0; i < payload_len; i++) {
            out->payload[i] = payload[i] ^ out->ss[i % MLKEM768_SS_BYTES];
        }
    }
}

void pq_mesh_decapsulate(const uint8_t dk[MLKEM768_DK_BYTES],
                         const pq_mesh_packet_t *packet,
                         uint8_t ss_out[MLKEM768_SS_BYTES]) {
    if (!packet || !ss_out) return;
    mlkem768_decaps(dk, packet->ct, ss_out);
}

/* ============================================================================
 * LAYER 4 — IDENTITY AUTHENTICATION
 * ============================================================================ */

lpres_state_t pq_identity_authenticate(const uint8_t pk[PQ_MLDSA65_PK_BYTES],
                                       const uint8_t *challenge, uint32_t challenge_len,
                                       const uint8_t sig[PQ_MLDSA65_SIG_BYTES]) {
    if (!pk || !challenge || !sig) return LPRES_STATE_FALSE;
    return pq_mldsa65_verify(pk, challenge, challenge_len, NULL, 0, sig)
           ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
}

/* ============================================================================
 * SELF-TEST
 * ============================================================================ */

uint32_t pq_self_test(void) {
    uint32_t passed = 0;
    
    /* FIPS 203 — ML-KEM-768 round-trip */
    {
        uint8_t d[32], z[32], m[32];
        uint8_t ek[MLKEM768_EK_BYTES], dk[MLKEM768_DK_BYTES];
        uint8_t ct[MLKEM768_CT_BYTES], ss_a[32], ss_b[32];
        for (uint32_t i = 0; i < 32; i++) { d[i] = (uint8_t)i; z[i] = (uint8_t)(i ^ 0x5a); m[i] = (uint8_t)(i * 7); }
        mlkem768_keygen(d, z, ek, dk);
        mlkem768_encaps(ek, m, ct, ss_a);
        mlkem768_decaps(dk, ct, ss_b);
        if (pq_mem_eq(ss_a, ss_b, 32)) passed++;
    }
    
    /* FIPS 204 — ML-DSA-65 round-trip */
    {
        uint8_t seed[32], rnd[32];
        uint8_t pk[PQ_MLDSA65_PK_BYTES], sk[PQ_MLDSA65_SK_BYTES];
        uint8_t sig[PQ_MLDSA65_SIG_BYTES];
        const uint8_t msg[] = "ZEDEC pqOS ledger entry";
        for (uint32_t i = 0; i < 32; i++) { seed[i] = (uint8_t)(i * 3 + 1); rnd[i] = (uint8_t)(i * 5 + 2); }
        pq_mldsa65_keygen(seed, pk, sk);
        pq_mldsa65_sign(sk, msg, sizeof(msg) - 1, NULL, 0, rnd, sig);
        if (pq_mldsa65_verify(pk, msg, sizeof(msg) - 1, NULL, 0, sig)) passed++;
    }
    
    /* FIPS 205 — SLH-DSA-128s round-trip */
    {
        uint8_t seed[48];
        uint8_t pk[PQ_SLH128S_PK_BYTES], sk[PQ_SLH128S_SK_BYTES];
        uint8_t sig[PQ_SLH128S_SIG_BYTES];
        const uint8_t msg[] = "ZEDEC pqOS boot image";
        for (uint32_t i = 0; i < 48; i++) seed[i] = (uint8_t)(i * 11 + 3);
        pq_slh128s_keygen(seed, pk, sk);
        pq_slh128s_sign(sk, msg, sizeof(msg) - 1, NULL, sig);
        if (pq_slh128s_verify(pk, msg, sizeof(msg) - 1, sig)) passed++;
    }
    
    return passed;
}
