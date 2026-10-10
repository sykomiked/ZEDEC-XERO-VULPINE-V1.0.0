/* test_pq_kat.c — ML-DSA-65 and SLH-DSA-SHAKE-128s against NIST ACVP.
 *
 * A signature scheme that only round-trips against itself proves nothing:
 * the stand-in this replaced round-tripped (once its overflow was fixed)
 * while being forgeable. These vectors come from NIST's ACVP server, so
 * each pass is agreement with an independent implementation:
 *   ML-DSA-65:  keyGen from seed, deterministic + hedged sigGen, sigVer
 *               including signatures modified in z / hint / commitment;
 *   SLH-DSA:    keyGen from (SK.seed, SK.prf, PK.seed), deterministic +
 *               hedged sigGen, sigVer including modified R / SIGFORS.
 * Vectors: pq_kat_vectors.h, regenerated with gen_pq_kat.py.
 */
#include <stdio.h>
#include <string.h>
#include "pq_security.h"
#include "../mlkem/keccak.h"
#include "pq_kat_vectors.h"

static int failures = 0, checks = 0;
static void check(int ok, const char *what, uint32_t tc, const char *why)
{
    checks++;
    if (!ok) failures++;
    if (tc)
        printf("  [%s] %s (ACVP tcId %u)%s%s\n", ok ? "PASS" : "FAIL", what, tc, *why ? ": " : "",
               why);
    else
        printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
}

static int digest_is(const uint8_t *data, uint32_t len, const uint8_t want[32])
{
    uint8_t h[32];
    sha3_256(data, len, h);
    return memcmp(h, want, 32) == 0;
}

int main(void)
{
    static uint8_t pk[PQ_MLDSA65_PK_BYTES], sk[PQ_MLDSA65_SK_BYTES], sig[PQ_MLDSA65_SIG_BYTES];
    static uint8_t spk[PQ_SLH128S_PK_BYTES], ssk[PQ_SLH128S_SK_BYTES], ssig[PQ_SLH128S_SIG_BYTES];
    printf("=== FIPS 204 / 205 known-answer tests (NIST ACVP) ===\n");

    for (uint32_t i = 0; i < KAT_COUNT; i++) {
        const kat_t *k = &KATS[i];
        switch (k->kind) {
        case MLDSA_KEYGEN:
            pq_mldsa65_keygen(k->a, pk, sk);
            check(digest_is(pk, sizeof(pk), k->b) && digest_is(sk, sizeof(sk), k->c),
                  "ML-DSA-65 keyGen pk and sk match", k->tc, k->why);
            break;
        case MLDSA_SIGN:
            pq_mldsa65_sign(k->a, k->b, k->nb, k->c, k->nc, k->nd ? k->d : NULL, sig);
            check(digest_is(sig, sizeof(sig), k->e),
                  k->nd ? "ML-DSA-65 hedged sigGen matches"
                        : "ML-DSA-65 deterministic sigGen matches",
                  k->tc, k->why);
            break;
        case MLDSA_VERIFY:
            check((int) pq_mldsa65_verify(k->a, k->b, k->nb, k->c, k->nc, k->d) == k->expect,
                  k->expect ? "ML-DSA-65 sigVer accepts" : "ML-DSA-65 sigVer rejects", k->tc,
                  k->why);
            break;
        case SLH_KEYGEN:
            pq_slh128s_keygen(k->a, spk, ssk);
            check(memcmp(spk, k->b, sizeof(spk)) == 0 && memcmp(ssk, k->c, sizeof(ssk)) == 0,
                  "SLH-DSA-SHAKE-128s keyGen pk and sk match", k->tc, k->why);
            break;
        case SLH_SIGN:
            pq_slh128s_sign_ctx(k->a, k->b, k->nb, k->c, k->nc, k->nd ? k->d : NULL, ssig);
            check(digest_is(ssig, sizeof(ssig), k->e),
                  k->nd ? "SLH-DSA-SHAKE-128s hedged sigGen matches"
                        : "SLH-DSA-SHAKE-128s deterministic sigGen matches",
                  k->tc, k->why);
            break;
        case SLH_VERIFY:
            check((int) pq_slh128s_verify_ctx(k->a, k->b, k->nb, k->c, k->nc, k->d) == k->expect,
                  k->expect ? "SLH-DSA-SHAKE-128s sigVer accepts"
                            : "SLH-DSA-SHAKE-128s sigVer rejects",
                  k->tc, k->why);
            break;
        }
    }

    /* the context string is bound: the same signature under another ctx fails */
    {
        static const uint8_t seed[32] = {1, 2, 3};
        static const uint8_t msg[] = "ledger entry";
        pq_mldsa65_keygen(seed, pk, sk);
        pq_mldsa65_sign(sk, msg, sizeof(msg) - 1, (const uint8_t *) "A", 1, NULL, sig);
        check(pq_mldsa65_verify(pk, msg, sizeof(msg) - 1, (const uint8_t *) "A", 1, sig),
              "ML-DSA-65 ctx A verifies", 0, "");
        check(!pq_mldsa65_verify(pk, msg, sizeof(msg) - 1, (const uint8_t *) "B", 1, sig),
              "ML-DSA-65 the same signature under ctx B is rejected", 0, "");
        pq_mldsa65_sign(sk, msg, sizeof(msg) - 1, NULL, 256, NULL, sig);
        check(!pq_mldsa65_verify(pk, msg, sizeof(msg) - 1, NULL, 0, sig),
              "ML-DSA-65 bad arguments leave a signature that never verifies", 0, "");
    }

    printf("\nchecks: %d, failures: %d\n", checks, failures);
    if (failures) {
        printf("[FAIL] pq_kat: %d failures\n", failures);
        return 1;
    }
    return 0;
}
