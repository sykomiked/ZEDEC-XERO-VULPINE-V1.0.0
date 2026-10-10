/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_mlkem_reject.c — ML-KEM implicit rejection (FIPS 203 Algorithm 18).
 *
 *   1. ML-KEM-768 (src/mlkem) and ML-KEM-1024 (pqsec/pq_mlkem1024.c): a valid
 *      ciphertext decapsulates to the encapsulated secret; a tampered one
 *      (a bit flipped at several positions, the first and last byte included)
 *      decapsulates to exactly K_bar = J(z || c) = SHAKE256(z || c, 32), the
 *      FIPS 203 implicit-rejection key, computed here independently.
 *   2. Built with -DZXV_CTGRIND and run under valgrind memcheck (the
 *      "ctgrind" technique): the secret parts of the decapsulation key (the
 *      K-PKE secret vector and z) are marked undefined, so any branch or
 *      memory index that depends on them, or on anything derived from them
 *      (m', the re-encryption, the compare result, the selected key), is
 *      reported as "Conditional jump or move depends on uninitialised
 *      value(s)". The verify-all recipe runs it with --error-exitcode, so a
 *      secret-dependent branch in decapsulation fails the build. This is
 *      evidence for the code as compiled on this host at this -O level, not
 *      a proof for every compiler and target.
 */
#include <stdio.h>
#include <string.h>
#include "keccak.h"
#include "mlkem768.h"
#include "../pqsec/pq_matrix_algs.h"

#ifdef ZXV_CTGRIND
#    include <valgrind/memcheck.h>
#    define CT_SECRET(p, n) VALGRIND_MAKE_MEM_UNDEFINED((p), (n))
#    define CT_PUBLIC(p, n) VALGRIND_MAKE_MEM_DEFINED((p), (n))
#else
#    define CT_SECRET(p, n) ((void) (p), (void) (n))
#    define CT_PUBLIC(p, n) ((void) (p), (void) (n))
#endif

static int failures = 0, checks = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

static void fill(uint8_t *p, uint32_t n, uint8_t seed)
{
    uint32_t x = 0x9E3779B9u ^ seed;
    for (uint32_t i = 0; i < n; i++) {
        x = x * 1664525u + 1013904223u;
        p[i] = (uint8_t) (x >> 24);
    }
}

/* J(z || c) = SHAKE256(z || c, 32), FIPS 203 section 4.1 / Algorithm 18 line 7. */
static void j_reject(const uint8_t z[32], const uint8_t *c, uint32_t clen, uint8_t out[32])
{
    static uint8_t buf[32 + 1568];
    memcpy(buf, z, 32);
    memcpy(buf + 32, c, clen);
    shake256(buf, 32 + clen, out, 32);
}

/* Positions to tamper with: first byte, last byte, and a few in between. */
static const uint32_t POS_FRAC[] = {0, 1, 7, 333, 0xFFFFFFFFu};

static void test_768(void)
{
    static uint8_t ek[MLKEM768_EK_BYTES], dk[MLKEM768_DK_BYTES], c[MLKEM768_CT_BYTES],
        bad[MLKEM768_CT_BYTES];
    uint8_t d[32], z[32], m[32], ss_enc[32], ss[32], want[32];
    fill(d, 32, 1);
    fill(z, 32, 2);
    fill(m, 32, 3);
    mlkem768_keygen(d, z, ek, dk);
    mlkem768_encaps(ek, m, c, ss_enc);

    /* secret parts of dk: dk_pke (s) at the start, z at the end */
    CT_SECRET(dk, KPE_DK_BYTES);
    CT_SECRET(dk + MLKEM768_DK_BYTES - 32, 32);
    mlkem768_decaps(dk, c, ss);
    CT_PUBLIC(ss, 32);
    CHECK(memcmp(ss, ss_enc, 32) == 0, "ML-KEM-768: valid ciphertext gives the encapsulated key");

    int all_j = 1, none_real = 1;
    for (uint32_t k = 0; k < sizeof POS_FRAC / sizeof POS_FRAC[0]; k++) {
        uint32_t pos = POS_FRAC[k] == 0xFFFFFFFFu ? MLKEM768_CT_BYTES - 1 : POS_FRAC[k];
        memcpy(bad, c, sizeof bad);
        bad[pos] ^= (uint8_t) (1u << (k & 7));
        mlkem768_decaps(dk, bad, ss);
        CT_PUBLIC(ss, 32);
        j_reject(z, bad, MLKEM768_CT_BYTES, want);
        if (memcmp(ss, want, 32) != 0) all_j = 0;
        if (memcmp(ss, ss_enc, 32) == 0) none_real = 0;
    }
    CT_PUBLIC(dk, MLKEM768_DK_BYTES);
    CHECK(all_j, "ML-KEM-768: every tampered ciphertext gives K_bar = SHAKE256(z || c)");
    CHECK(none_real, "ML-KEM-768: no tampered ciphertext gives the real key");
}

static void test_1024(void)
{
    static uint8_t ek[PQM_MLKEM1024_EK_BYTES], dk[PQM_MLKEM1024_DK_BYTES],
        c[PQM_MLKEM1024_CT_BYTES], bad[PQM_MLKEM1024_CT_BYTES];
    uint8_t d[32], z[32], m[32], ss_enc[32], ss[32], want[32];
    fill(d, 32, 11);
    fill(z, 32, 12);
    fill(m, 32, 13);
    pqm_mlkem1024_keygen(d, z, ek, dk);
    CHECK(pqm_mlkem1024_encaps(ek, m, c, ss_enc), "ML-KEM-1024: encapsulation");

    /* dk = s (1536) || ek (1568) || H(ek) (32) || z (32) */
    CT_SECRET(dk, 1536);
    CT_SECRET(dk + PQM_MLKEM1024_DK_BYTES - 32, 32);
    pqm_mlkem1024_decaps(dk, c, ss);
    CT_PUBLIC(ss, 32);
    CHECK(memcmp(ss, ss_enc, 32) == 0, "ML-KEM-1024: valid ciphertext gives the encapsulated key");

    int all_j = 1, none_real = 1;
    for (uint32_t k = 0; k < sizeof POS_FRAC / sizeof POS_FRAC[0]; k++) {
        uint32_t pos = POS_FRAC[k] == 0xFFFFFFFFu ? PQM_MLKEM1024_CT_BYTES - 1 : POS_FRAC[k];
        memcpy(bad, c, sizeof bad);
        bad[pos] ^= (uint8_t) (1u << (k & 7));
        pqm_mlkem1024_decaps(dk, bad, ss);
        CT_PUBLIC(ss, 32);
        j_reject(z, bad, PQM_MLKEM1024_CT_BYTES, want);
        if (memcmp(ss, want, 32) != 0) all_j = 0;
        if (memcmp(ss, ss_enc, 32) == 0) none_real = 0;
    }
    CT_PUBLIC(dk, PQM_MLKEM1024_DK_BYTES);
    CHECK(all_j, "ML-KEM-1024: every tampered ciphertext gives K_bar = SHAKE256(z || c)");
    CHECK(none_real, "ML-KEM-1024: no tampered ciphertext gives the real key");
}

int main(void)
{
    test_768();
    test_1024();
    printf("\n%s: %d of %d checks failed\n", failures ? "FAILED" : "ALL PASS", failures, checks);
    return failures ? 1 : 0;
}
