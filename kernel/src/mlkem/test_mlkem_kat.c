/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_mlkem_kat.c — ML-KEM-768 against AUTHENTIC NIST ACVP known-answer vectors.
 *
 * WHAT THIS REPLACES. mlkem768_validate.c checks that our encaps and our decaps
 * agree with EACH OTHER over 10 LCG-seeded trials. That is a useful smoke test and
 * it is NOT validation: an implementation can be uniformly wrong and still be
 * perfectly self-consistent. It would agree with itself and interoperate with
 * nothing on earth. Only published answers from the standards body can tell those
 * two situations apart, which is what this file does.
 *
 * WHAT IS COVERED
 *   1. KeyGen   (d,z)  -> (ek,dk)      — 5 vectors, full 1184/2400-byte compare
 *   2. Encaps   (ek,m) -> (c,K)        — 5 vectors, full 1088-byte ct + 32-byte K
 *   3. Decaps   (dk,c) -> K            — 10 vectors, of which 5 are "modified
 *                                        ciphertext" (implicit rejection)
 *
 * WHY THE MODIFIED-CIPHERTEXT CASES ARE THE IMPORTANT ONES. FIPS 203 decapsulation
 * must never fail visibly. On a bad ciphertext it returns the pseudorandom value
 * J(z||c) — indistinguishable from a real shared secret to the caller. An
 * implementation that instead returns an error, a zero key, or a *different* wrong
 * value hands the attacker an oracle, and chosen-ciphertext key recovery follows.
 * A KAT suite that only tested the happy path would pass such an implementation.
 *
 * Host test: builds with the same -Wall -Werror -Wextra as the rest of verify-all.
 * Exits non-zero on any mismatch. Every comparison is over the FULL buffer; nothing
 * is spot-checked or truncated. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "mlkem768.h"
#include "mlkem768_kat_vectors.h"

static int failures = 0;
static int checks   = 0;

/* hex -> bytes. Returns the byte count, or -1 if the string is malformed or does
 * not fit. A silent truncation here would turn a real mismatch into a pass, so it
 * is an error, not a clamp. */
static int unhex(const char *h, uint8_t *out, size_t cap)
{
    size_t n = strlen(h), i;
    if ((n & 1u) != 0u) return -1;
    if (n / 2u > cap)   return -1;
    for (i = 0; i < n; i += 2) {
        int hi = -1, lo = -1, k;
        char c;
        for (k = 0; k < 2; k++) {
            c = h[i + (size_t)k];
            int v = (c >= '0' && c <= '9') ? (c - '0')
                  : (c >= 'a' && c <= 'f') ? (c - 'a' + 10)
                  : (c >= 'A' && c <= 'F') ? (c - 'A' + 10) : -1;
            if (v < 0) return -1;
            if (k == 0) hi = v; else lo = v;
        }
        out[i / 2] = (uint8_t)((hi << 4) | lo);
    }
    return (int)(n / 2u);
}

/* Full-buffer compare. On mismatch, report the FIRST differing offset — that
 * localises a bug (e.g. "diverges at byte 384" points at a specific polynomial)
 * far better than a bare "not equal". */
static void expect_eq(const char *what, int tc,
                      const uint8_t *got, const uint8_t *want, size_t n)
{
    size_t i;
    checks++;
    if (memcmp(got, want, n) == 0) return;
    failures++;
    for (i = 0; i < n; i++) {
        if (got[i] != want[i]) {
            printf("  [FAIL] tc%-4d %-8s mismatch at byte %zu of %zu: got %02x want %02x\n",
                   tc, what, i, n, got[i], want[i]);
            return;
        }
    }
    printf("  [FAIL] tc%-4d %-8s mismatch (length %zu)\n", tc, what, n);
}

int main(void)
{
    static uint8_t d[32], z[32], m[32];
    static uint8_t ek[MLKEM768_EK_BYTES], dk[MLKEM768_DK_BYTES];
    static uint8_t ek_w[MLKEM768_EK_BYTES], dk_w[MLKEM768_DK_BYTES];
    static uint8_t c[MLKEM768_CT_BYTES], c_w[MLKEM768_CT_BYTES];
    static uint8_t ss[MLKEM768_SS_BYTES], k_w[MLKEM768_SS_BYTES];
    int i, rejects = 0;

    printf("ML-KEM-768 vs NIST ACVP known-answer vectors\n");
    printf("  (published answers, not self-consistency)\n\n");

    /* ---- 1. KeyGen: (d,z) -> (ek,dk) ------------------------------------ */
    printf("[1] KeyGen_internal(d,z) -- %d vectors\n", KAT_KEYGEN_N);
    for (i = 0; i < KAT_KEYGEN_N; i++) {
        const kat_keygen_t *t = &KAT_KEYGEN[i];
        if (unhex(t->d, d, sizeof d) != 32 || unhex(t->z, z, sizeof z) != 32 ||
            unhex(t->ek, ek_w, sizeof ek_w) != MLKEM768_EK_BYTES ||
            unhex(t->dk, dk_w, sizeof dk_w) != MLKEM768_DK_BYTES) {
            printf("  [FAIL] tc%-4d vector did not parse to the expected sizes\n", t->tc);
            failures++; continue;
        }
        memset(ek, 0, sizeof ek); memset(dk, 0, sizeof dk);
        mlkem768_keygen(d, z, ek, dk);
        expect_eq("ek", t->tc, ek, ek_w, MLKEM768_EK_BYTES);
        expect_eq("dk", t->tc, dk, dk_w, MLKEM768_DK_BYTES);
    }

    /* ---- 2. Encaps: (ek,m) -> (c,K) ------------------------------------- */
    printf("[2] Encaps_internal(ek,m) -- %d vectors\n", KAT_ENCAPS_N);
    for (i = 0; i < KAT_ENCAPS_N; i++) {
        const kat_encaps_t *t = &KAT_ENCAPS[i];
        if (unhex(t->ek, ek, sizeof ek) != MLKEM768_EK_BYTES ||
            unhex(t->m, m, sizeof m) != 32 ||
            unhex(t->c, c_w, sizeof c_w) != MLKEM768_CT_BYTES ||
            unhex(t->k, k_w, sizeof k_w) != MLKEM768_SS_BYTES) {
            printf("  [FAIL] tc%-4d vector did not parse to the expected sizes\n", t->tc);
            failures++; continue;
        }
        memset(c, 0, sizeof c); memset(ss, 0, sizeof ss);
        mlkem768_encaps(ek, m, c, ss);
        expect_eq("ct", t->tc, c, c_w, MLKEM768_CT_BYTES);
        expect_eq("K",  t->tc, ss, k_w, MLKEM768_SS_BYTES);
    }

    /* ---- 3. Decaps, including implicit rejection ------------------------- */
    printf("[3] Decaps_internal(dk,c) -- %d vectors\n", KAT_DECAPS_N);
    for (i = 0; i < KAT_DECAPS_N; i++) {
        const kat_decaps_t *t = &KAT_DECAPS[i];
        int modified = (strcmp(t->reason, "modified ciphertext") == 0);
        if (unhex(t->dk, dk, sizeof dk) != MLKEM768_DK_BYTES ||
            unhex(t->c, c, sizeof c) != MLKEM768_CT_BYTES ||
            unhex(t->k, k_w, sizeof k_w) != MLKEM768_SS_BYTES) {
            printf("  [FAIL] tc%-4d vector did not parse to the expected sizes\n", t->tc);
            failures++; continue;
        }
        memset(ss, 0, sizeof ss);
        mlkem768_decaps(dk, c, ss);
        expect_eq(modified ? "K(rej)" : "K", t->tc, ss, k_w, MLKEM768_SS_BYTES);
        if (modified) rejects++;
    }
    printf("    (%d of those were modified-ciphertext / implicit-rejection cases)\n", rejects);

    printf("\n%d comparisons, %d failures\n", checks, failures);
    if (failures == 0) {
        printf("[PASS] ML-KEM-768 matches the NIST ACVP vectors byte-for-byte.\n");
        return 0;
    }
    printf("[FAIL] ML-KEM-768 does NOT match the published vectors. This implementation\n");
    printf("       is not ML-KEM-768 and must not be used to protect anything.\n");
    return 1;
}
