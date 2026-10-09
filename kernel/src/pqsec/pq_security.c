/* pq_security.c — ZEDEC pqOS Vertically Integrated Post-Quantum Security
 *
 * Implementation of the FIPS 203/204/205 integration layer.
 *
 * ML-KEM-768 (FIPS 203) is REUSED from mlkem768.c (KAT-verified).
 * ML-DSA-65 (FIPS 204) and SLH-DSA-SHAKE-128s (FIPS 205) are the
 * reference implementations vendored under mldsa/ and slhdsa/, wrapped by
 * pq_mldsa65.c and pq_slhdsa.c and checked against NIST ACVP vectors
 * (test_pq_kat.c). An earlier stand-in here was not a signature scheme:
 * valid signatures did not verify and the ML-DSA one carried the secret
 * key.
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

static bool pq_mem_eq(const uint8_t *a, const uint8_t *b, uint32_t len) {
    uint8_t diff = 0;
    for (uint32_t i = 0; i < len; i++) diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0;
}

/* ML-DSA-65 (FIPS 204) lives in pq_mldsa65.c and SLH-DSA-SHAKE-128s
 * (FIPS 205) in pq_slhdsa.c, both over vendored reference code. */

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
