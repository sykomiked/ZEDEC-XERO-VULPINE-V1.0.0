/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* smoke.c - links against libzxv-pqc.a and zxv_pqc.h only.
 *
 *   cc -Ibuild/libzxv-pqc/include smoke.c build/libzxv-pqc/libzxv-pqc.a
 *
 * Known answers: SHA3-256("abc") (FIPS 202 example) and ML-KEM-768 keygen
 * for NIST ACVP ML-KEM-keyGen-FIPS203 tcId 1 (digests of ek and dk).
 * Round trips with a tamper check for ML-KEM-768/1024, ML-DSA-65/87 and
 * SLH-DSA-SHAKE-128s. (SLH-DSA-256s is in the library but slow to sign,
 * so it is only exercised by the kernel tree's test_pq_matrix.)
 * The "random" bytes are fixed so the run is reproducible; real callers
 * must pass fresh randomness. */
#include <stdio.h>
#include <string.h>
#include "zxv_pqc.h"

static int fails, checks;
#define CHECK(c, what)                                                                             \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("FAIL %s\n", what);                                                             \
        } else {                                                                                   \
            printf("ok   %s\n", what);                                                             \
        }                                                                                          \
    } while (0)

static void unhex(const char *h, uint8_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        sscanf(h + 2 * i, "%2x", &v);
        out[i] = (uint8_t) v;
    }
}

static int digest_is(const uint8_t *data, size_t len, const char *hex)
{
    uint8_t d[32], want[32];
    sha3_256(data, len, d);
    unhex(hex, want, 32);
    return !memcmp(d, want, 32);
}

static uint8_t sig_slh[PQ_SLH128S_SIG_BYTES];
static uint8_t sig87[PQM_MLDSA87_SIG_BYTES];

int main(void)
{
    static const uint8_t msg[] = "libzxv-pqc smoke message";
    uint8_t seed[96];
    for (int i = 0; i < 96; i++) seed[i] = (uint8_t) (i * 7 + 1);

    CHECK(digest_is((const uint8_t *) "abc", 3,
                    "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532"),
          "SHA3-256(\"abc\") known answer");

    /* ML-KEM-768: ACVP keyGen tcId 1 */
    {
        static uint8_t ek[MLKEM768_EK_BYTES], dk[MLKEM768_DK_BYTES], c[MLKEM768_CT_BYTES];
        uint8_t d[32], z[32], ss1[32], ss2[32];
        unhex("e582b7d75e6c80b05ae392a1fc9f7153b12390fd99930368cc67a768baebc8a0", d, 32);
        unhex("1cdacb8740c0b87c4a379575f187b367cbfa3b300bf591b109f79816e9cbe8f0", z, 32);
        mlkem768_keygen(d, z, ek, dk);
        CHECK(digest_is(ek, sizeof ek,
                        "81e66ef5a7a221619f6a64039cc369843e10df5c859f6959cc3fd8e5272330fd") &&
                  digest_is(dk, sizeof dk,
                            "be81068c104cd6cf8efd800b294f4a15bb8a8050993fd54a2cc428841ef6ca44"),
              "ML-KEM-768 keygen matches NIST ACVP tcId 1");
        mlkem768_encaps(ek, seed, c, ss1);
        mlkem768_decaps(dk, c, ss2);
        CHECK(!memcmp(ss1, ss2, 32), "ML-KEM-768 encaps/decaps agree");
        c[5] ^= 1;
        mlkem768_decaps(dk, c, ss2);
        CHECK(memcmp(ss1, ss2, 32) != 0, "ML-KEM-768 tampered ciphertext: implicit rejection");
    }
    /* ML-KEM-1024 */
    {
        static uint8_t ek[PQM_MLKEM1024_EK_BYTES], dk[PQM_MLKEM1024_DK_BYTES],
            c[PQM_MLKEM1024_CT_BYTES];
        uint8_t ss1[32], ss2[32];
        pqm_mlkem1024_keygen(seed, seed + 32, ek, dk);
        CHECK(pqm_mlkem1024_check_ek(ek) && pqm_mlkem1024_check_dk(dk), "ML-KEM-1024 key checks");
        CHECK(pqm_mlkem1024_encaps(ek, seed + 64, c, ss1), "ML-KEM-1024 encaps");
        pqm_mlkem1024_decaps(dk, c, ss2);
        CHECK(!memcmp(ss1, ss2, 32), "ML-KEM-1024 encaps/decaps agree");
    }
    /* ML-DSA-65 */
    {
        static uint8_t pk[PQ_MLDSA65_PK_BYTES], sk[PQ_MLDSA65_SK_BYTES], sig[PQ_MLDSA65_SIG_BYTES];
        pq_mldsa65_keygen(seed, pk, sk);
        pq_mldsa65_sign(sk, msg, sizeof msg, (const uint8_t *) "ctx", 3, NULL, sig);
        CHECK(pq_mldsa65_verify(pk, msg, sizeof msg, (const uint8_t *) "ctx", 3, sig),
              "ML-DSA-65 sign/verify");
        CHECK(!pq_mldsa65_verify(pk, msg, sizeof msg, (const uint8_t *) "ctY", 3, sig),
              "ML-DSA-65 wrong context rejected");
        sig[100] ^= 0x10;
        CHECK(!pq_mldsa65_verify(pk, msg, sizeof msg, (const uint8_t *) "ctx", 3, sig),
              "ML-DSA-65 tampered signature rejected");
    }
    /* ML-DSA-87 */
    {
        static uint8_t pk[PQM_MLDSA87_PK_BYTES], sk[PQM_MLDSA87_SK_BYTES];
        pqm_mldsa87_keygen(seed + 1, pk, sk);
        pqm_mldsa87_sign(sk, msg, sizeof msg, NULL, 0, seed + 40, sig87);
        CHECK(pqm_mldsa87_verify(pk, msg, sizeof msg, NULL, 0, sig87), "ML-DSA-87 sign/verify");
        CHECK(!pqm_mldsa87_verify(pk, msg, sizeof msg - 1, NULL, 0, sig87),
              "ML-DSA-87 other message rejected");
    }
    /* SLH-DSA-SHAKE-128s */
    {
        uint8_t pk[PQ_SLH128S_PK_BYTES], sk[PQ_SLH128S_SK_BYTES];
        pq_slh128s_keygen(seed, pk, sk);
        pq_slh128s_sign(sk, msg, sizeof msg, NULL, sig_slh);
        CHECK(pq_slh128s_verify(pk, msg, sizeof msg, sig_slh), "SLH-DSA-128s sign/verify");
        sig_slh[PQ_SLH128S_SIG_BYTES - 1] ^= 1;
        CHECK(!pq_slh128s_verify(pk, msg, sizeof msg, sig_slh),
              "SLH-DSA-128s tampered signature rejected");
    }

    printf("%d/%d checks\n", checks - fails, checks);
    return fails ? 1 : 0;
}
