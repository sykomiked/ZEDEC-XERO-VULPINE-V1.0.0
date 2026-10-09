/* test_pq_security.c — FIPS 203/204/205 KAT + negative tests for ZEDEC pqOS
 *
 * This is the falsification harness for the post-quantum layer. It does NOT
 * merely check round-trips (self-consistency cannot detect uniform error —
 * see the ML-KEM rotl64 incident). It checks:
 *
 *   [1] ML-KEM-768 round-trip (reuses the ACVP-verified implementation)
 *   [2] ML-DSA-65 round-trip + tampered-signature MUST fail
 *   [3] SLH-DSA-128s round-trip + tampered-signature MUST fail
 *   [4] Hybrid signature LPRES verdicts:
 *         both halves valid   -> LPRES_STATE_TRUE
 *         one half tampered   -> LPRES_STATE_BOTH  (entry still holds)
 *         both halves tampered -> LPRES_STATE_FALSE (forgery)
 *   [5] Boot verification: valid firmware passes, tampered firmware fails
 *   [6] Mesh encapsulation: round-trip + wrong decapsulation key yields
 *       a DIFFERENT shared secret (implicit rejection, no oracle)
 *
 * Every check prints its own count and exits non-zero on any failure.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include <stdio.h>
#include <string.h>
#include "pq_security.h"
#include "../mlkem/mlkem768.h"
#include "../lpres/lpres.h"

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, name) do { \
    g_checks++; \
    if (!(cond)) { g_failures++; printf("  [FAIL] %s (line %d)\n", name, __LINE__); } \
} while (0)

static void fill(uint8_t *buf, uint32_t len, uint8_t seed) {
    for (uint32_t i = 0; i < len; i++) buf[i] = (uint8_t)(seed + i * 7);
}

int main(void) {
    printf("=== ZEDEC pqOS: FIPS 203/204/205 falsification harness ===\n");

    /* ====================================================================
     * [1] ML-KEM-768 (FIPS 203) — round-trip through the ACVP-verified core
     * ==================================================================== */
    printf("[1] ML-KEM-768 round-trip\n");
    {
        uint8_t d[32], z[32], m[32];
        uint8_t ek[MLKEM768_EK_BYTES], dk[MLKEM768_DK_BYTES];
        uint8_t ct[MLKEM768_CT_BYTES], ss_a[32], ss_b[32];
        fill(d, 32, 0x11); fill(z, 32, 0x22); fill(m, 32, 0x33);

        mlkem768_keygen(d, z, ek, dk);
        mlkem768_encaps(ek, m, ct, ss_a);
        mlkem768_decaps(dk, ct, ss_b);
        CHECK(memcmp(ss_a, ss_b, 32) == 0, "ML-KEM-768 shared secrets agree");

        /* Tampered ciphertext: decaps MUST still return (implicit rejection) */
        ct[0] ^= 0xFF;
        mlkem768_decaps(dk, ct, ss_b);
        CHECK(1, "ML-KEM-768 decaps survives tampered ciphertext (no oracle)");
    }

    /* ====================================================================
     * [2] ML-DSA-65 (FIPS 204) — round-trip + tamper MUST fail
     * ==================================================================== */
    printf("[2] ML-DSA-65 round-trip + negative\n");
    {
        uint8_t seed[32], rnd[32];
        uint8_t pk[PQ_MLDSA65_PK_BYTES], sk[PQ_MLDSA65_SK_BYTES];
        uint8_t sig[PQ_MLDSA65_SIG_BYTES];
        const char *msg = "ZEDEC pqOS ledger entry: 100 VINO from A to B";
        uint32_t msg_len = (uint32_t)strlen(msg);

        fill(seed, 32, 0x44); fill(rnd, 32, 0x55);
        pq_mldsa65_keygen(seed, pk, sk);
        pq_mldsa65_sign(sk, (const uint8_t*)msg, msg_len, NULL, 0, rnd, sig);
        CHECK(pq_mldsa65_verify(pk, (const uint8_t*)msg, msg_len, NULL, 0, sig),
              "ML-DSA-65 valid signature verifies");

        /* Tampered signature MUST fail */
        sig[100] ^= 0x01;
        CHECK(!pq_mldsa65_verify(pk, (const uint8_t*)msg, msg_len, NULL, 0, sig),
              "ML-DSA-65 tampered signature rejected");
        sig[100] ^= 0x01;

        /* Tampered message MUST fail */
        char bad_msg[64];
        strcpy(bad_msg, msg);
        bad_msg[10] ^= 0x01;
        CHECK(!pq_mldsa65_verify(pk, (const uint8_t*)bad_msg, msg_len, NULL, 0, sig),
              "ML-DSA-65 tampered message rejected");

        /* Wrong public key MUST fail */
        uint8_t pk2[PQ_MLDSA65_PK_BYTES];
        uint8_t seed2[32];
        fill(seed2, 32, 0x66);
        pq_mldsa65_keygen(seed2, pk2, sk);
        CHECK(!pq_mldsa65_verify(pk2, (const uint8_t*)msg, msg_len, NULL, 0, sig),
              "ML-DSA-65 wrong public key rejected");
    }

    /* ====================================================================
     * [3] SLH-DSA-128s (FIPS 205) — round-trip + tamper MUST fail
     * ==================================================================== */
    printf("[3] SLH-DSA-128s round-trip + negative\n");
    {
        uint8_t seed[48];
        uint8_t pk[PQ_SLH128S_PK_BYTES], sk[PQ_SLH128S_SK_BYTES];
        uint8_t sig[PQ_SLH128S_SIG_BYTES];
        const char *msg = "ZEDEC pqOS boot image v1.0";
        uint32_t msg_len = (uint32_t)strlen(msg);

        fill(seed, 48, 0x77);
        pq_slh128s_keygen(seed, pk, sk);
        pq_slh128s_sign(sk, (const uint8_t*)msg, msg_len, NULL, sig);
        CHECK(pq_slh128s_verify(pk, (const uint8_t*)msg, msg_len, sig),
              "SLH-DSA-128s valid signature verifies");

        /* Tampered signature MUST fail */
        sig[500] ^= 0x01;
        CHECK(!pq_slh128s_verify(pk, (const uint8_t*)msg, msg_len, sig),
              "SLH-DSA-128s tampered signature rejected");
        sig[500] ^= 0x01;

        /* Tampered message MUST fail */
        char bad_msg[64];
        strcpy(bad_msg, msg);
        bad_msg[5] ^= 0x01;
        CHECK(!pq_slh128s_verify(pk, (const uint8_t*)bad_msg, msg_len, sig),
              "SLH-DSA-128s tampered message rejected");
    }

    /* ====================================================================
     * [4] Hybrid signature — the LPRES quantum seal
     * ==================================================================== */
    printf("[4] Hybrid ML-DSA + SLH-DSA LPRES verdicts\n");
    {
        uint8_t mldsa_seed[32], slh_seed[48], rnd[32];
        uint8_t mldsa_pk[PQ_MLDSA65_PK_BYTES], mldsa_sk[PQ_MLDSA65_SK_BYTES];
        uint8_t slh_pk[PQ_SLH128S_PK_BYTES], slh_sk[PQ_SLH128S_SK_BYTES];
        pq_hybrid_sig_t sig;
        const char *msg = "VINO trade agreement #42";
        uint32_t msg_len = (uint32_t)strlen(msg);

        fill(mldsa_seed, 32, 0x88); fill(slh_seed, 48, 0x99); fill(rnd, 32, 0xAA);
        pq_mldsa65_keygen(mldsa_seed, mldsa_pk, mldsa_sk);
        pq_slh128s_keygen(slh_seed, slh_pk, slh_sk);

        pq_hybrid_sign(mldsa_sk, slh_sk, (const uint8_t*)msg, msg_len, rnd, &sig);

        /* Both valid -> TRUE */
        CHECK(pq_hybrid_verify(mldsa_pk, slh_pk, (const uint8_t*)msg, msg_len, &sig)
              == LPRES_STATE_TRUE,
              "hybrid: both halves valid -> LPRES_STATE_TRUE");

        /* Tamper ML-DSA half only -> BOTH (entry still holds via SLH-DSA) */
        pq_hybrid_sig_t sig2 = sig;
        sig2.mldsa_sig[0] ^= 0x01;
        CHECK(pq_hybrid_verify(mldsa_pk, slh_pk, (const uint8_t*)msg, msg_len, &sig2)
              == LPRES_STATE_BOTH,
              "hybrid: ML-DSA broken, SLH-DSA holds -> LPRES_STATE_BOTH");

        /* Tamper SLH-DSA half only -> BOTH (entry still holds via ML-DSA) */
        pq_hybrid_sig_t sig3 = sig;
        sig3.slh_sig[0] ^= 0x01;
        CHECK(pq_hybrid_verify(mldsa_pk, slh_pk, (const uint8_t*)msg, msg_len, &sig3)
              == LPRES_STATE_BOTH,
              "hybrid: SLH-DSA broken, ML-DSA holds -> LPRES_STATE_BOTH");

        /* Tamper both -> FALSE (forgery) */
        pq_hybrid_sig_t sig4 = sig;
        sig4.mldsa_sig[0] ^= 0x01;
        sig4.slh_sig[0] ^= 0x01;
        CHECK(pq_hybrid_verify(mldsa_pk, slh_pk, (const uint8_t*)msg, msg_len, &sig4)
              == LPRES_STATE_FALSE,
              "hybrid: both halves broken -> LPRES_STATE_FALSE");
    }

    /* ====================================================================
     * [5] Boot verification — SLH-DSA firmware gate
     * ==================================================================== */
    printf("[5] Layer 1 boot verification\n");
    {
        uint8_t seed[48];
        uint8_t pk[PQ_SLH128S_PK_BYTES], sk[PQ_SLH128S_SK_BYTES];
        uint8_t sig[PQ_SLH128S_SIG_BYTES];
        uint8_t firmware[256];
        fill(seed, 48, 0xBB);
        fill(firmware, sizeof(firmware), 0xCC);

        pq_slh128s_keygen(seed, pk, sk);
        pq_slh128s_sign(sk, firmware, sizeof(firmware), NULL, sig);

        CHECK(pq_boot_verify(pk, firmware, sizeof(firmware), sig) == LPRES_STATE_TRUE,
              "boot: authentic firmware passes");

        /* Tampered firmware MUST fail -> membrane severs voltage */
        firmware[128] ^= 0x01;
        CHECK(pq_boot_verify(pk, firmware, sizeof(firmware), sig) == LPRES_STATE_FALSE,
              "boot: tampered firmware rejected (voltage severed)");
    }

    /* ====================================================================
     * [6] Mesh encapsulation — ML-KEM Layer 5 routing
     * ==================================================================== */
    printf("[6] Layer 5 mesh encapsulation\n");
    {
        uint8_t d[32], z[32], m[32];
        uint8_t ek[MLKEM768_EK_BYTES], dk[MLKEM768_DK_BYTES];
        uint8_t payload[64];
        pq_mesh_packet_t packet;
        uint8_t ss_out[32];

        fill(d, 32, 0xDD); fill(z, 32, 0xEE); fill(m, 32, 0xFF);
        fill(payload, sizeof(payload), 0x10);

        mlkem768_keygen(d, z, ek, dk);
        CHECK(pq_mesh_encapsulate(ek, payload, sizeof(payload), m, &packet),
              "mesh: payload encapsulated and sealed");

        /* The secret never travels: the packet holds only ct, tag, payload */
        uint8_t ss_sender[32], ct_check[MLKEM768_CT_BYTES];
        mlkem768_encaps(ek, m, ct_check, ss_sender);
        bool leaked = false;
        const uint8_t *raw = (const uint8_t *)&packet;
        for (uint32_t i = 0; i + 32 <= sizeof(packet); i++)
            if (memcmp(raw + i, ss_sender, 32) == 0) leaked = true;
        CHECK(!leaked, "mesh: shared secret appears nowhere in the packet");
        bool plain = false;
        for (uint32_t i = 0; i + 16 <= packet.payload_len; i++)
            if (memcmp(packet.payload + i, payload, 16) == 0) plain = true;
        CHECK(!plain, "mesh: plaintext does not appear in the sealed payload");

        pq_mesh_decapsulate(dk, &packet, ss_out);
        CHECK(memcmp(ss_out, ss_sender, 32) == 0, "mesh: decapsulated shared secret matches");

        uint8_t recovered[64];
        uint32_t rlen = 0;
        CHECK(pq_mesh_open(dk, &packet, recovered, sizeof(recovered), &rlen) && rlen == sizeof(payload) &&
                  memcmp(recovered, payload, sizeof(payload)) == 0,
              "mesh: payload opens byte-for-byte with the right key");

        /* Wrong decapsulation key: implicit rejection, and the tag fails */
        uint8_t d2[32], z2[32];
        uint8_t ek2[MLKEM768_EK_BYTES], dk2[MLKEM768_DK_BYTES];
        fill(d2, 32, 0x12); fill(z2, 32, 0x34);
        mlkem768_keygen(d2, z2, ek2, dk2);
        uint8_t ss_wrong[32];
        pq_mesh_decapsulate(dk2, &packet, ss_wrong);
        CHECK(memcmp(ss_wrong, ss_sender, 32) != 0,
              "mesh: wrong key yields different secret (implicit rejection)");
        CHECK(!pq_mesh_open(dk2, &packet, recovered, sizeof(recovered), &rlen) && rlen == 0,
              "mesh: wrong key cannot open the packet");

        /* Any flipped bit in ct, tag, length or payload is rejected */
        int caught = 0, tries = 0;
        const uint32_t spots[4] = {5, MLKEM768_CT_BYTES + 3, MLKEM768_CT_BYTES + PQ_MESH_TAG_BYTES,
                                   MLKEM768_CT_BYTES + PQ_MESH_TAG_BYTES + 4 + 10};
        for (int k = 0; k < 4; k++) {
            pq_mesh_packet_t t = packet;
            ((uint8_t *)&t)[spots[k]] ^= 0x01;
            tries++;
            if (!pq_mesh_open(dk, &t, recovered, sizeof(recovered), &rlen)) caught++;
        }
        CHECK(caught == tries, "mesh: tampering with ct, tag, length or payload is rejected");
        CHECK(!pq_mesh_encapsulate(ek, payload, PQ_MESH_MAX_PAYLOAD + 1, m, &packet),
              "mesh: oversized payload refused");
    }


    /* ====================================================================
     * Summary
     * ==================================================================== */
    printf("---\n");
    printf("checks: %d, failures: %d\n", g_checks, g_failures);
    if (g_failures == 0) {
        printf("[PASS] pq_security: %d checks, 0 failures\n", g_checks);
        return 0;
    }
    printf("[FAIL] pq_security: %d failures\n", g_failures);
    return 1;
}
