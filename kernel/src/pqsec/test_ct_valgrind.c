/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_ct_valgrind.c -- secret-dependent branch/index check ("ctgrind").
 *
 * Run under valgrind memcheck. The secret inputs are marked UNDEFINED with
 * VALGRIND_MAKE_MEM_UNDEFINED; memcheck then reports every conditional jump
 * and every memory address that depends on them ("Conditional jump or move
 * depends on uninitialised value", "Use of uninitialised value of size N").
 * Arithmetic on secrets is not reported, only branches and table indices.
 *
 * What this shows and what it does not:
 *   - It checks the code paths these inputs take, on this CPU, with this
 *     compiler and flags. It is not a proof of constant-time behaviour.
 *   - It cannot see timing of instructions whose latency depends on their
 *     operands (e.g. division on some CPUs) or microarchitectural leaks.
 *   - Only the secret parts of the keys are marked. Public values carried in
 *     the same buffers (rho, tr, the encapsulation key inside dk) stay
 *     defined, so public rejection sampling of the matrix A is not flagged.
 *
 * Modes (one per run, so each report names the operation):
 *   mlkem-decaps   ML-KEM-768 decapsulation; secret: s (dk[0..1151]) and z,
 *                  and the ciphertext is a valid one AND a tampered one
 *                  (implicit rejection must not branch on the comparison)
 *   mlkem-encaps   ML-KEM-768 encapsulation; secret: the 32-byte message m
 *   mldsa-sign     ML-DSA-65 signing; secret: K and s1, s2, t0 in sk.
 *                  Reported, not gated. Measured 2026-10 (gcc 11 and 13, -O2): every
 *                  report is in the vendored pq-crystals reference -- the norm
 *                  checks and rejection gotos, make_hint, hint packing and
 *                  challenge sampling from c~ -- i.e. on candidates that are
 *                  rejected or on values that are published in the signature.
 *                  That is how the reference is designed; this check does not
 *                  show that those leaks are harmless.
 *   selftest       a deliberate branch on a secret byte; the script requires
 *                  memcheck to report it, which shows the check is live.
 * ML-DSA verify has no secret input, so there is nothing to mark.
 *
 * Without valgrind the client requests are no-ops and the program just runs.
 */
#include <stdio.h>
#include <string.h>
#include <valgrind/memcheck.h>
#include "../mlkem/mlkem768.h"
#include "pq_security.h"

#define SECRET(p, n) VALGRIND_MAKE_MEM_UNDEFINED((p), (n))
#define PUBLIC(p, n) VALGRIND_MAKE_MEM_DEFINED((p), (n))

static void fill(uint8_t *p, size_t n, uint8_t seed)
{
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t) (seed + 37 * i + (i >> 3));
}

static int mlkem_decaps(void)
{
    static uint8_t ek[MLKEM768_EK_BYTES], dk[MLKEM768_DK_BYTES], c[MLKEM768_CT_BYTES];
    uint8_t d[32], z[32], m[32], ss_a[32], ss_b[32], ss_bad[32];
    fill(d, 32, 1);
    fill(z, 32, 2);
    fill(m, 32, 3);
    mlkem768_keygen(d, z, ek, dk);
    mlkem768_encaps(ek, m, c, ss_a);

    /* dk = s (1152) || ek (1184) || H(ek) (32) || z (32). */
    SECRET(dk, 1152);
    SECRET(dk + MLKEM768_DK_BYTES - 32, 32);
    mlkem768_decaps(dk, c, ss_b);
    c[5] ^= 1; /* tampered ciphertext: implicit rejection path */
    mlkem768_decaps(dk, c, ss_bad);
    PUBLIC(dk, MLKEM768_DK_BYTES);
    PUBLIC(ss_b, 32);
    PUBLIC(ss_bad, 32);
    int ok = memcmp(ss_a, ss_b, 32) == 0 && memcmp(ss_a, ss_bad, 32) != 0;
    printf("[%s] ML-KEM-768 decaps (valid and tampered ciphertext) ran with secret s, z\n",
           ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

static int mlkem_encaps(void)
{
    static uint8_t ek[MLKEM768_EK_BYTES], dk[MLKEM768_DK_BYTES], c[MLKEM768_CT_BYTES];
    uint8_t d[32], z[32], m[32], ss[32], ss2[32];
    fill(d, 32, 4);
    fill(z, 32, 5);
    fill(m, 32, 6);
    mlkem768_keygen(d, z, ek, dk);
    SECRET(m, 32);
    mlkem768_encaps(ek, m, c, ss);
    PUBLIC(c, sizeof c); /* the ciphertext is sent in the clear */
    PUBLIC(ss, 32);
    mlkem768_decaps(dk, c, ss2);
    int ok = memcmp(ss, ss2, 32) == 0;
    printf("[%s] ML-KEM-768 encaps ran with secret m\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

static int mldsa_sign(void)
{
    static uint8_t pk[PQ_MLDSA65_PK_BYTES], sk[PQ_MLDSA65_SK_BYTES], sig[PQ_MLDSA65_SIG_BYTES];
    uint8_t seed[32], rnd[32];
    const uint8_t msg[] = "constant-time check";
    fill(seed, 32, 7);
    fill(rnd, 32, 8);
    pq_mldsa65_keygen(seed, pk, sk);
    /* sk = rho (32) || K (32) || tr (64) || s1 || s2 || t0. */
    SECRET(sk + 32, 32);
    SECRET(sk + 128, PQ_MLDSA65_SK_BYTES - 128);
    SECRET(rnd, 32);
    pq_mldsa65_sign(sk, msg, sizeof msg - 1, NULL, 0, rnd, sig);
    PUBLIC(sig, sizeof sig);
    PUBLIC(sk, sizeof sk);
    int ok = pq_mldsa65_verify(pk, msg, sizeof msg - 1, NULL, 0, sig);
    printf("[%s] ML-DSA-65 sign ran with secret K, s1, s2, t0, rnd\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

static int selftest(void)
{
    volatile uint8_t key[1] = {0x5a};
    int r = 0;
    SECRET((void *) key, 1);
    if (key[0] & 1) r = 1; /* secret-dependent branch: must be reported */
    PUBLIC((void *) key, 1);
    printf("[PASS] selftest branch taken=%d\n", r);
    return 0;
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "";
    if (!strcmp(mode, "mlkem-decaps")) return mlkem_decaps();
    if (!strcmp(mode, "mlkem-encaps")) return mlkem_encaps();
    if (!strcmp(mode, "mldsa-sign")) return mldsa_sign();
    if (!strcmp(mode, "selftest")) return selftest();
    fprintf(stderr, "usage: %s mlkem-decaps|mlkem-encaps|mldsa-sign|selftest\n", argv[0]);
    return 2;
}
