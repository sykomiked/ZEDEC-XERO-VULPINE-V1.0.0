/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* test_pq_matrix.c — the post-quantum matrix against published vectors,
 * then the hybrid constructions, the wire format and the cost.
 *
 *   1. Known answers. ML-KEM-1024 keyGen / encapsulation / decapsulation /
 *      input checks, ML-DSA-87 keyGen / sigGen / sigVer and
 *      SLH-DSA-SHAKE-256s keyGen / sigGen / sigVer from NIST ACVP; HQC-5
 *      from the official v5.0.0 KAT file; X25519 from RFC 7748 including
 *      the 1- and 1000-iteration vectors. (pq_matrix_kat_vectors.h,
 *      regenerated with gen_pq_matrix_kat.py.)
 *   2. The combiner and the seed expansion recomputed from the component
 *      algorithms with the kernel's own Keccak (src/mlkem/keccak.c, not the
 *      slhdsa-c copy pq_matrix.c uses), at every level.
 *   3. Round trips at every level; implicit rejection of any tampered
 *      ciphertext component; dual signatures failing when either half is
 *      tampered, swapped between signatures, or lifted into a lower level.
 *   4. Serialisation round trips and a fuzzed parser (>= 20000 inputs).
 *   5. Benchmarks: time and size per level next to ChaCha20-Poly1305 bulk
 *      throughput, which is what the session keys feed.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "pq_matrix.h"
#include "pq_security.h"
#include "../mlkem/keccak.h"
#include "../tls/aead.h"
#include "../tls/x25519.h"
#include "pq_matrix_kat_vectors.h"

static int failures = 0, checks = 0;
static void check(int ok, const char *what, uint32_t tc, const char *why)
{
    checks++;
    if (!ok) failures++;
    if (tc)
        printf("  [%s] %s (tc %u)%s%s\n", ok ? "PASS" : "FAIL", what, tc, *why ? ": " : "", why);
    else
        printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
}
/* For loops that would print thousands of lines: count, print on failure. */
static void quiet(int ok, const char *what)
{
    checks++;
    if (!ok) {
        failures++;
        printf("  [FAIL] %s\n", what);
    }
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}
/* Decode `n` bytes of hex text into a scratch slot; returns the slot. */
static uint8_t g_slot[5][40000];
static const uint8_t *hx(int slot, const char *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        g_slot[slot][i] = (uint8_t) (hexval(s[2 * i]) << 4 | hexval(s[2 * i + 1]));
    return g_slot[slot];
}
static void hexto(const char *s, uint8_t *out, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        out[i] = (uint8_t) (hexval(s[2 * i]) << 4 | hexval(s[2 * i + 1]));
}
static int digest_is(const uint8_t *data, size_t len, const uint8_t want[32])
{
    uint8_t h[32];
    sha3_256(data, len, h);
    return memcmp(h, want, 32) == 0;
}

/* Test-only xorshift generator; the library itself never makes randomness. */
static uint64_t g_rng = 0x9e3779b97f4a7c15ull;
static uint64_t rnd64(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return g_rng;
}
static void rndfill(uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t) rnd64();
}

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double) t.tv_sec + (double) t.tv_nsec * 1e-9;
}

/* Big objects live in static storage. */
static pqm_kem_pk_t kpk, kpk2;
static pqm_kem_sk_t ksk, ksk2;
static pqm_kem_ct_t kct, kct2;
static pqm_sig_pk_t spk, spk2;
static pqm_sig_sk_t ssk, ssk2;
static pqm_sig_t sig, sig2, sig3;
static uint8_t wire[PQM_SIG_MAX_BYTES + 64], wire2[PQM_SIG_MAX_BYTES + 64];

/* ===================================================================== */
/* 1. known answers                                                       */
/* ===================================================================== */

static void test_kats(void)
{
    static uint8_t ek[PQM_MLKEM1024_EK_BYTES], dk[PQM_MLKEM1024_DK_BYTES], ct[PQM_HQC5_CT_BYTES],
        ss[32], ss2[32];
    static uint8_t pk[PQM_HQC5_PK_BYTES], sk[PQM_HQC5_SK_BYTES], s87[PQM_SLH256S_SIG_BYTES];
    printf("--- known-answer tests: NIST ACVP, HQC v5.0.0 KAT, RFC 7748 ---\n");
    for (uint32_t i = 0; i < PQM_KAT_COUNT; i++) {
        const pqm_kat_t *k = &PQM_KATS[i];
        const uint8_t *a = hx(0, k->a, k->na), *b = hx(1, k->b, k->nb), *c = hx(2, k->c, k->nc),
                      *d = hx(3, k->d, k->nd), *e = hx(4, k->e, k->ne);
        switch (k->kind) {
        case PQM_KAT_MLKEM_KEYGEN:
            pqm_mlkem1024_keygen(a, b, ek, dk);
            check(digest_is(ek, sizeof ek, c) && digest_is(dk, sizeof dk, d) &&
                      pqm_mlkem1024_check_dk(dk),
                  "ML-KEM-1024 keyGen ek and dk match", k->tc, k->why);
            break;
        case PQM_KAT_MLKEM_ENCAP:
            check(pqm_mlkem1024_encaps(a, b, ct, ss) && digest_is(ct, PQM_MLKEM1024_CT_BYTES, c) &&
                      memcmp(ss, d, 32) == 0,
                  "ML-KEM-1024 encapsulation c and K match", k->tc, k->why);
            break;
        case PQM_KAT_MLKEM_DECAP:
            pqm_mlkem1024_decaps(a, b, ss);
            check(memcmp(ss, c, 32) == 0, "ML-KEM-1024 decapsulation K matches", k->tc, k->why);
            break;
        case PQM_KAT_MLKEM_EKCHECK:
            check((int) pqm_mlkem1024_check_ek(a) == k->expect,
                  k->expect ? "ML-KEM-1024 ek check accepts" : "ML-KEM-1024 ek check rejects",
                  k->tc, k->why);
            if (!k->expect) {
                memcpy(ss, "not-overwritten-not-overwritten!", 32);
                check(!pqm_mlkem1024_encaps(a, a, ct, ss) && ss[0] == 0,
                      "ML-KEM-1024 encaps refuses that ek", k->tc, "");
            }
            break;
        case PQM_KAT_MLKEM_DKCHECK:
            check((int) pqm_mlkem1024_check_dk(a) == k->expect,
                  k->expect ? "ML-KEM-1024 dk check accepts" : "ML-KEM-1024 dk check rejects",
                  k->tc, k->why);
            break;
        case PQM_KAT_MLDSA_KEYGEN: {
            static uint8_t p[PQM_MLDSA87_PK_BYTES], s[PQM_MLDSA87_SK_BYTES];
            pqm_mldsa87_keygen(a, p, s);
            check(digest_is(p, sizeof p, b) && digest_is(s, sizeof s, c),
                  "ML-DSA-87 keyGen pk and sk match", k->tc, k->why);
            break;
        }
        case PQM_KAT_MLDSA_SIGN: {
            static uint8_t sg[PQM_MLDSA87_SIG_BYTES];
            pqm_mldsa87_sign(a, b, k->nb, c, k->nc, k->nd ? d : NULL, sg);
            check(digest_is(sg, sizeof sg, e),
                  k->nd ? "ML-DSA-87 hedged sigGen matches"
                        : "ML-DSA-87 deterministic sigGen matches",
                  k->tc, k->why);
            break;
        }
        case PQM_KAT_MLDSA_VERIFY:
            check((int) pqm_mldsa87_verify(a, b, k->nb, c, k->nc, d) == k->expect,
                  k->expect ? "ML-DSA-87 sigVer accepts" : "ML-DSA-87 sigVer rejects", k->tc,
                  k->why);
            break;
        case PQM_KAT_SLH_KEYGEN: {
            uint8_t p[PQM_SLH256S_PK_BYTES], s[PQM_SLH256S_SK_BYTES];
            pqm_slh256s_keygen(a, p, s);
            check(memcmp(p, b, sizeof p) == 0 && memcmp(s, c, sizeof s) == 0,
                  "SLH-DSA-SHAKE-256s keyGen pk and sk match", k->tc, k->why);
            break;
        }
        case PQM_KAT_SLH_SIGN:
            pqm_slh256s_sign(a, b, k->nb, c, k->nc, k->nd ? d : NULL, s87);
            check(digest_is(s87, sizeof s87, e),
                  k->nd ? "SLH-DSA-SHAKE-256s hedged sigGen matches"
                        : "SLH-DSA-SHAKE-256s deterministic sigGen matches",
                  k->tc, k->why);
            break;
        case PQM_KAT_SLH_VERIFY:
            check((int) pqm_slh256s_verify(a, b, k->nb, c, k->nc, d) == k->expect,
                  k->expect ? "SLH-DSA-SHAKE-256s sigVer accepts"
                            : "SLH-DSA-SHAKE-256s sigVer rejects",
                  k->tc, k->why);
            break;
        case PQM_KAT_HQC: {
            /* The KAT's 48-byte seed initialises HQC's SHAKE-256 PRNG
             * (SHAKE256(seed || 0x00)); KeyGen draws seed_kem, then Encaps
             * draws m and salt from the same stream. */
            uint8_t in[49], draw[80];
            memcpy(in, a, 48);
            in[48] = 0;
            shake256(in, sizeof in, draw, sizeof draw);
            pqm_hqc5_keygen(draw, pk, sk);
            pqm_hqc5_encaps(pk, draw + 32, draw + 64, ct, ss);
            pqm_hqc5_decaps(sk, ct, ss2);
            char what[80];
            snprintf(what, sizeof what, "HQC-5 pk, sk, ct and ss match the official KAT, count %u",
                     k->tc);
            check(digest_is(pk, sizeof pk, b) && digest_is(sk, sizeof sk, c) &&
                      digest_is(ct, PQM_HQC5_CT_BYTES, d) && memcmp(ss, e, 32) == 0 &&
                      memcmp(ss2, e, 32) == 0,
                  what, 0, "");
            break;
        }
        }
    }

    /* RFC 7748 section 5.2: one vector and the iterated test. */
    uint8_t kk[32], uu[32], r[32];
    hexto("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4", kk, 32);
    hexto("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c", uu, 32);
    x25519(r, kk, uu);
    hexto("c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552", uu, 32);
    check(memcmp(r, uu, 32) == 0, "X25519 RFC 7748 5.2 vector 1", 0, "");
    memset(kk, 0, 32);
    memset(uu, 0, 32);
    kk[0] = uu[0] = 9;
    uint8_t want1[32], want1000[32];
    hexto("422c8e7a6227d7bca1350b3e2bb7279f7897b87bb6854b783c60e80311ae3079", want1, 32);
    hexto("684cf59ba83309552800ef566f2f4d3c1c3887c49360e3875f2eb94d99532c51", want1000, 32);
    for (int i = 1; i <= 1000; i++) {
        x25519(r, kk, uu);
        memcpy(uu, kk, 32);
        memcpy(kk, r, 32);
        if (i == 1) check(memcmp(kk, want1, 32) == 0, "X25519 RFC 7748 iterated, 1 round", 0, "");
    }
    check(memcmp(kk, want1000, 32) == 0, "X25519 RFC 7748 iterated, 1000 rounds", 0, "");
}

/* ===================================================================== */
/* 2. combiner and seed expansion, recomputed independently               */
/* ===================================================================== */

static void xof(const char *label, unsigned level, const uint8_t *in, size_t n, uint8_t *out,
                size_t out_len)
{
    uint8_t buf[64 + 2 + 64];
    size_t l = strlen(label);
    memcpy(buf, label, l);
    buf[l] = 0;
    buf[l + 1] = (uint8_t) level;
    memcpy(buf + l + 2, in, n);
    shake256(buf, l + 2 + n, out, out_len);
}

static void test_combiner(pqm_level_t level)
{
    static uint8_t ek[PQM_MLKEM1024_EK_BYTES], dk[PQM_MLKEM1024_DK_BYTES],
        mct[PQM_MLKEM1024_CT_BYTES], hpk[PQM_HQC5_PK_BYTES], hsk[PQM_HQC5_SK_BYTES],
        hct[PQM_HQC5_CT_BYTES], buf[64 + PQM_MLKEM1024_CT_BYTES + PQM_HQC5_CT_BYTES + 128];
    uint8_t seed[32], coins[32], r[128], e[112], ssm[32], ssh[32], ssx[32], xpk[32], eph[32],
        want[32], got[32];
    char what[96];
    rndfill(seed, 32);
    rndfill(coins, 32);
    pqm_kem_keygen(level, seed, &kpk, &ksk);
    pqm_encaps(&kpk, coins, &kct, got);

    xof("ZXV-PQM-v1/kem-keygen", level, seed, 32, r, 128);
    xof("ZXV-PQM-v1/kem-encaps", level, coins, 32, e, 112);
    size_t ekl = level == PQM_LEVEL_STANDARD ? MLKEM768_EK_BYTES : PQM_MLKEM1024_EK_BYTES;
    size_t ctl = level == PQM_LEVEL_STANDARD ? MLKEM768_CT_BYTES : PQM_MLKEM1024_CT_BYTES;
    if (level == PQM_LEVEL_STANDARD) {
        mlkem768_keygen(r, r + 32, ek, dk);
        mlkem768_encaps(ek, e, mct, ssm);
    } else {
        pqm_mlkem1024_keygen(r, r + 32, ek, dk);
        pqm_mlkem1024_encaps(ek, e, mct, ssm);
    }
    int keys_ok = memcmp(ek, kpk.mlkem_ek, ekl) == 0 && memcmp(mct, kct.mlkem_ct, ctl) == 0;
    if (level == PQM_LEVEL_MATRIX) {
        pqm_hqc5_keygen(r + 64, hpk, hsk);
        pqm_hqc5_encaps(hpk, e + 32, e + 64, hct, ssh);
        keys_ok &=
            memcmp(hpk, kpk.hqc_pk, sizeof hpk) == 0 && memcmp(hct, kct.hqc_ct, sizeof hct) == 0;
    }
    x25519_public(xpk, r + 96);
    x25519_public(eph, e + 80);
    x25519(ssx, e + 80, xpk);
    keys_ok &= memcmp(xpk, kpk.x25519_pk, 32) == 0 && memcmp(eph, kct.x25519_eph, 32) == 0;

    size_t n = 0;
    memcpy(buf + n, ssm, 32), n += 32;
    if (level == PQM_LEVEL_MATRIX) memcpy(buf + n, ssh, 32), n += 32;
    memcpy(buf + n, ssx, 32), n += 32;
    memcpy(buf + n, mct, ctl), n += ctl;
    if (level == PQM_LEVEL_MATRIX) memcpy(buf + n, hct, sizeof hct), n += sizeof hct;
    memcpy(buf + n, eph, 32), n += 32;
    memcpy(buf + n, xpk, 32), n += 32;
    memcpy(buf + n, "ZXV-PQM-v1", 10), n += 10;
    buf[n++] = (uint8_t) level;
    sha3_256(buf, n, want);

    snprintf(what, sizeof what, "%s: seed expansion reproduced from the component algorithms",
             pqm_level_name(level));
    check(keys_ok, what, 0, "");
    snprintf(what, sizeof what, "%s: combined secret == SHA3-256 over the %zu-byte transcript",
             pqm_level_name(level), n);
    check(memcmp(want, got, 32) == 0, what, 0, "");
}

/* ===================================================================== */
/* 3. round trips, implicit rejection, dual-signature binding             */
/* ===================================================================== */

static void test_kem(pqm_level_t level)
{
    uint8_t seed[32], coins[32], s1[32], s2[32];
    char what[128];
    const char *L = pqm_level_name(level);
    rndfill(seed, 32);
    rndfill(coins, 32);
    snprintf(what, sizeof what, "%s: keygen", L);
    check(pqm_kem_keygen(level, seed, &kpk, &ksk), what, 0, "");
    int ok =
        pqm_encaps(&kpk, coins, &kct, s1) && pqm_decaps(&ksk, &kct, s2) && memcmp(s1, s2, 32) == 0;
    snprintf(what, sizeof what, "%s: encaps/decaps round trip agrees", L);
    check(ok, what, 0, "");

    /* Fresh coins give a fresh secret; the same coins repeat it. */
    uint8_t s3[32];
    coins[0] ^= 1;
    pqm_encaps(&kpk, coins, &kct2, s3);
    snprintf(what, sizeof what, "%s: different coins, different secret", L);
    check(memcmp(s1, s3, 32) != 0, what, 0, "");

    /* Tamper with each component at its first, middle and last byte. */
    struct {
        const char *name;
        uint8_t *p;
        size_t len;
    } parts[3];
    unsigned np = 0;
    parts[np].name = "ML-KEM ciphertext";
    parts[np].p = kct.mlkem_ct;
    parts[np++].len = level == PQM_LEVEL_STANDARD ? PQM_MLKEM768_CT_BYTES : PQM_MLKEM1024_CT_BYTES;
    if (level == PQM_LEVEL_MATRIX) {
        parts[np].name = "HQC ciphertext";
        parts[np].p = kct.hqc_ct;
        parts[np++].len = PQM_HQC5_CT_BYTES;
    }
    parts[np].name = "X25519 ephemeral key";
    parts[np].p = kct.x25519_eph;
    parts[np++].len = 31; /* bit 255 of the u-coordinate is ignored by RFC 7748 */
    coins[0] ^= 1;
    for (unsigned i = 0; i < np; i++) {
        size_t at[3] = {0, parts[i].len / 2, parts[i].len - 1};
        int all = 1;
        for (unsigned j = 0; j < 3; j++) {
            pqm_encaps(&kpk, coins, &kct, s1);
            parts[i].p[at[j]] ^= 0x01;
            all &= pqm_decaps(&ksk, &kct, s2) && memcmp(s1, s2, 32) != 0;
        }
        snprintf(what, sizeof what,
                 "%s: tampered %s -> decaps still returns, with a different secret", L,
                 parts[i].name);
        check(all, what, 0, "");
    }
    /* Implicit rejection is deterministic: the same forgery, the same wrong secret. */
    pqm_encaps(&kpk, coins, &kct, s1);
    kct.mlkem_ct[5] ^= 0x80;
    pqm_decaps(&ksk, &kct, s2);
    pqm_decaps(&ksk, &kct, s3);
    snprintf(what, sizeof what, "%s: implicit rejection is deterministic", L);
    check(memcmp(s2, s3, 32) == 0 && memcmp(s1, s2, 32) != 0, what, 0, "");

    /* Fully random ciphertext components (what a fuzzer or an attacker
     * sends): decaps must handle any bytes and never match the honest
     * secret. */
    {
        int all = 1;
        for (unsigned it = 0; it < 24; it++) {
            pqm_encaps(&kpk, coins, &kct, s1);
            rndfill(kct.mlkem_ct, sizeof kct.mlkem_ct);
            if (it & 1) rndfill(kct.hqc_ct, sizeof kct.hqc_ct);
            if (it & 2) rndfill(kct.x25519_eph, sizeof kct.x25519_eph);
            all &= pqm_decaps(&ksk, &kct, s2) && memcmp(s1, s2, 32) != 0;
        }
        snprintf(what, sizeof what, "%s: 24 random ciphertexts decapsulate to unrelated secrets",
                 L);
        check(all, what, 0, "");
    }

    /* Wrong recipient key: a different secret. */
    seed[0] ^= 1;
    pqm_kem_keygen(level, seed, &kpk2, &ksk2);
    pqm_encaps(&kpk, coins, &kct, s1);
    pqm_decaps(&ksk2, &kct, s2);
    snprintf(what, sizeof what, "%s: another recipient's key gets a different secret", L);
    check(memcmp(s1, s2, 32) != 0, what, 0, "");

    /* Level mismatch is an error, not a silent secret. */
    kct.level = (uint8_t) (level == PQM_LEVEL_MATRIX ? PQM_LEVEL_HIGH : PQM_LEVEL_MATRIX);
    snprintf(what, sizeof what, "%s: decaps refuses a ciphertext of another level", L);
    check(!pqm_decaps(&ksk, &kct, s2), what, 0, "");

    /* A low-order X25519 key is refused at encaps. */
    kpk2 = kpk;
    memset(kpk2.x25519_pk, 0, 32);
    snprintf(what, sizeof what, "%s: encaps refuses a low-order X25519 key", L);
    check(!pqm_encaps(&kpk2, coins, &kct2, s1) && s1[0] == 0 && s1[31] == 0, what, 0, "");
    /* An ML-KEM key that fails the FIPS 203 modulus check is refused. */
    kpk2 = kpk;
    kpk2.mlkem_ek[0] = 0xff;
    kpk2.mlkem_ek[1] |= 0x0f;
    snprintf(what, sizeof what, "%s: encaps refuses an ML-KEM key with a coefficient >= q", L);
    check(!pqm_encaps(&kpk2, coins, &kct2, s1), what, 0, "");
}

static void test_sig(pqm_level_t level)
{
    uint8_t seed[32], rnd[32];
    static const uint8_t msg[] = "transfer 10 units to account 7";
    static const uint8_t msg2[] = "transfer 99 units to account 7";
    const size_t ml = sizeof msg - 1;
    const uint8_t *ctx = (const uint8_t *) "economy";
    char what[128];
    const char *L = pqm_level_name(level);
    rndfill(seed, 32);
    rndfill(rnd, 32);
    pqm_sig_keygen(level, seed, &spk, &ssk);
    pqm_sign(&ssk, msg, ml, ctx, 7, rnd, &sig);
    snprintf(what, sizeof what, "%s: hedged signature verifies", L);
    check(pqm_verify(&spk, msg, ml, ctx, 7, &sig), what, 0, "");
    pqm_sign(&ssk, msg, ml, ctx, 7, NULL, &sig2);
    pqm_sign(&ssk, msg, ml, ctx, 7, NULL, &sig3);
    snprintf(what, sizeof what, "%s: deterministic signature verifies and repeats", L);
    check(pqm_verify(&spk, msg, ml, ctx, 7, &sig2) && memcmp(&sig2, &sig3, sizeof sig2) == 0, what,
          0, "");
    snprintf(what, sizeof what, "%s: another message fails", L);
    check(!pqm_verify(&spk, msg2, ml, ctx, 7, &sig), what, 0, "");
    snprintf(what, sizeof what, "%s: another context fails", L);
    check(!pqm_verify(&spk, msg, ml, (const uint8_t *) "economX", 7, &sig) &&
              !pqm_verify(&spk, msg, ml, NULL, 0, &sig),
          what, 0, "");

    /* Tamper with the ML-DSA half. */
    sig2 = sig;
    sig2.mldsa_sig[100] ^= 0x04;
    snprintf(what, sizeof what, "%s: tampered ML-DSA half fails", L);
    check(!pqm_verify(&spk, msg, ml, ctx, 7, &sig2), what, 0, "");
    /* Another key. */
    seed[0] ^= 1;
    pqm_sig_keygen(level, seed, &spk2, &ssk2);
    snprintf(what, sizeof what, "%s: another key fails", L);
    check(!pqm_verify(&spk2, msg, ml, ctx, 7, &sig), what, 0, "");

    if (level == PQM_LEVEL_MATRIX) {
        /* Tamper with the SLH-DSA half only: the ML-DSA half is still valid,
         * and the whole must still fail. */
        sig2 = sig;
        sig2.slh_sig[PQM_SLH256S_SIG_BYTES - 7] ^= 0x10;
        check(!pqm_verify(&spk, msg, ml, ctx, 7, &sig2),
              "MATRIX: tampered SLH-DSA half fails (ML-DSA half alone is not enough)", 0, "");
        sig2 = sig;
        sig2.slh_sig[0] ^= 0x01; /* inside R */
        check(!pqm_verify(&spk, msg, ml, ctx, 7, &sig2), "MATRIX: tampered SLH-DSA R fails", 0, "");
        /* Swap halves between two valid signatures on different messages. */
        pqm_sign(&ssk, msg2, ml, ctx, 7, rnd, &sig3);
        check(pqm_verify(&spk, msg2, ml, ctx, 7, &sig3), "MATRIX: second signature verifies", 0,
              "");
        sig2 = sig;
        memcpy(sig2.slh_sig, sig3.slh_sig, sizeof sig2.slh_sig);
        check(!pqm_verify(&spk, msg, ml, ctx, 7, &sig2) &&
                  !pqm_verify(&spk, msg2, ml, ctx, 7, &sig2),
              "MATRIX: halves swapped between two signatures fail for both messages", 0, "");
        /* Halves from another key pair. */
        pqm_sign(&ssk2, msg, ml, ctx, 7, rnd, &sig3);
        sig2 = sig;
        memcpy(sig2.slh_sig, sig3.slh_sig, sizeof sig2.slh_sig);
        check(!pqm_verify(&spk, msg, ml, ctx, 7, &sig2),
              "MATRIX: an SLH-DSA half from another key fails", 0, "");
        /* Stripping: lift the ML-DSA-87 half and its key into a HIGH
         * object. Its context names MATRIX, SLH-DSA and the MATRIX key,
         * so the lone half does not verify as a HIGH signature. */
        spk2 = spk;
        spk2.level = PQM_LEVEL_HIGH;
        sig2 = sig;
        sig2.level = PQM_LEVEL_HIGH;
        memset(sig2.slh_sig, 0, sizeof sig2.slh_sig);
        check(!pqm_verify(&spk2, msg, ml, ctx, 7, &sig2),
              "MATRIX: stripped to its ML-DSA-87 half and relabelled HIGH, it fails", 0, "");
        check(!pqm_mldsa87_verify(spk.mldsa_pk, msg, ml, NULL, 0, sig.mldsa_sig),
              "MATRIX: the ML-DSA-87 half is not a plain FIPS 204 signature on msg", 0, "");
    }
    /* Level mismatch between key and signature. */
    sig2 = sig;
    sig2.level = (uint8_t) (level == PQM_LEVEL_STANDARD ? PQM_LEVEL_HIGH : PQM_LEVEL_STANDARD);
    snprintf(what, sizeof what, "%s: a signature labelled with another level fails", L);
    check(!pqm_verify(&spk, msg, ml, ctx, 7, &sig2), what, 0, "");
    /* Bad arguments. */
    snprintf(what, sizeof what, "%s: oversize context is refused by sign and verify", L);
    check(!pqm_sign(&ssk, msg, ml, ctx, PQM_CTX_MAX + 1, rnd, &sig2) &&
              !pqm_verify(&spk, msg, ml, ctx, PQM_CTX_MAX + 1, &sig),
          what, 0, "");
    pqm_sig_sk_wipe(&ssk2);
    uint8_t zero[sizeof ssk2] = {0};
    snprintf(what, sizeof what, "%s: pqm_sig_sk_wipe zeroises the secret key", L);
    check(memcmp(&ssk2, zero, sizeof ssk2) == 0, what, 0, "");
}

/* ===================================================================== */
/* 4. serialisation and fuzzing                                           */
/* ===================================================================== */

typedef size_t (*enc_fn)(const void *, uint8_t *, size_t);
typedef bool (*dec_fn)(void *, const uint8_t *, size_t);

#define WRAP(name, T)                                                                              \
    static size_t e_##name(const void *o, uint8_t *b, size_t c)                                    \
    {                                                                                              \
        return name##_encode(o, b, c);                                                             \
    }                                                                                              \
    static bool d_##name(void *o, const uint8_t *b, size_t l)                                      \
    {                                                                                              \
        return name##_decode(o, b, l);                                                             \
    }
WRAP(pqm_kem_pk, pqm_kem_pk_t)
WRAP(pqm_kem_ct, pqm_kem_ct_t)
WRAP(pqm_kem_sk, pqm_kem_sk_t)
WRAP(pqm_sig_pk, pqm_sig_pk_t)
WRAP(pqm_sig, pqm_sig_t)
WRAP(pqm_sig_sk, pqm_sig_sk_t)

typedef struct {
    const char *name;
    pqm_obj_t type;
    void *obj, *tmp;
    size_t size;
    enc_fn enc;
    dec_fn dec;
} codec_t;

static codec_t codecs[6] = {
    {"KEM public key", PQM_OBJ_KEM_PK, &kpk, &kpk2, sizeof kpk, e_pqm_kem_pk, d_pqm_kem_pk},
    {"KEM ciphertext", PQM_OBJ_KEM_CT, &kct, &kct2, sizeof kct, e_pqm_kem_ct, d_pqm_kem_ct},
    {"KEM secret key", PQM_OBJ_KEM_SK, &ksk, &ksk2, sizeof ksk, e_pqm_kem_sk, d_pqm_kem_sk},
    {"signature public key", PQM_OBJ_SIG_PK, &spk, &spk2, sizeof spk, e_pqm_sig_pk, d_pqm_sig_pk},
    {"signature", PQM_OBJ_SIG, &sig, &sig2, sizeof sig, e_pqm_sig, d_pqm_sig},
    {"signature secret key", PQM_OBJ_SIG_SK, &ssk, &ssk2, sizeof ssk, e_pqm_sig_sk, d_pqm_sig_sk},
};

static void make_objects(pqm_level_t level)
{
    uint8_t seed[32] = {7, (uint8_t) level}, ss[32];
    pqm_kem_keygen(level, seed, &kpk, &ksk);
    pqm_encaps(&kpk, seed, &kct, ss);
    pqm_sig_keygen(level, seed, &spk, &ssk);
    pqm_sign(&ssk, seed, 32, NULL, 0, NULL, &sig);
}

static void test_wire(pqm_level_t level)
{
    char what[160];
    make_objects(level);
    for (unsigned i = 0; i < 6; i++) {
        codec_t *c = &codecs[i];
        size_t n = c->enc(c->obj, wire, sizeof wire);
        size_t want = pqm_encoded_size(c->type, level);
        int ok = n == want && n > 0;
        ok &= c->enc(c->obj, wire2, n - 1) == 0; /* too small a buffer: nothing */
        ok &= c->dec(c->tmp, wire, n) && memcmp(c->tmp, c->obj, c->size) == 0;
        ok &= c->enc(c->tmp, wire2, sizeof wire2) == n && memcmp(wire, wire2, n) == 0;
        ok &= wire[0] == 'Z' && wire[1] == 'Q' && wire[2] == PQM_WIRE_VERSION &&
              wire[3] == c->type && wire[4] == level;
        snprintf(what, sizeof what, "%s: %s encodes to %zu bytes and round-trips byte-exactly",
                 pqm_level_name(level), c->name, n);
        check(ok, what, 0, "");
    }
    /* The decoded objects still work. */
    uint8_t ss1[32], ss2[32], coins[32] = {9};
    size_t n = pqm_kem_pk_encode(&kpk, wire, sizeof wire);
    pqm_kem_pk_decode(&kpk2, wire, n);
    n = pqm_kem_sk_encode(&ksk, wire, sizeof wire);
    pqm_kem_sk_decode(&ksk2, wire, n);
    pqm_encaps(&kpk2, coins, &kct, ss1);
    n = pqm_kem_ct_encode(&kct, wire, sizeof wire);
    pqm_kem_ct_decode(&kct2, wire, n);
    pqm_decaps(&ksk2, &kct2, ss2);
    snprintf(what, sizeof what, "%s: KEM through the wire format agrees", pqm_level_name(level));
    check(memcmp(ss1, ss2, 32) == 0, what, 0, "");
    n = pqm_sig_encode(&sig, wire, sizeof wire);
    pqm_sig_decode(&sig2, wire, n);
    n = pqm_sig_pk_encode(&spk, wire, sizeof wire);
    pqm_sig_pk_decode(&spk2, wire, n);
    uint8_t seed[32] = {7, (uint8_t) level};
    snprintf(what, sizeof what, "%s: signature through the wire format verifies",
             pqm_level_name(level));
    check(pqm_verify(&spk2, seed, 32, NULL, 0, &sig2), what, 0, "");
}

/* Mutate a valid encoding in one of several ways; decoders must never
 * read out of bounds (ASan), and anything they accept must be canonical:
 * re-encoding it gives back exactly the input. */
static size_t mutate(uint8_t *b, size_t n, size_t cap)
{
    switch (rnd64() % 9) {
    case 0: /* flip a random bit */
        b[rnd64() % n] ^= (uint8_t) (1u << (rnd64() % 8));
        return n;
    case 1: /* corrupt a header byte */
        b[rnd64() % PQM_HDR_BYTES] = (uint8_t) rnd64();
        return n;
    case 2: { /* corrupt a component header byte */
        size_t at = PQM_HDR_BYTES + rnd64() % PQM_COMP_HDR_BYTES;
        b[at] = (uint8_t) rnd64();
        return n;
    }
    case 3: /* truncate */
        return rnd64() % n;
    case 4: /* extend */
    {
        size_t extra = 1 + rnd64() % 64;
        if (n + extra > cap) extra = cap - n;
        rndfill(b + n, extra);
        return n + extra;
    }
    case 5: /* all random, random length */
    {
        size_t l = rnd64() % (n + 16);
        if (l > cap) l = cap;
        rndfill(b, l);
        return l;
    }
    case 6: /* valid header, random rest */
        rndfill(b + PQM_HDR_BYTES, n - PQM_HDR_BYTES);
        return n;
    case 7: /* change the level byte to another valid level */
        b[4] = (uint8_t) (1 + rnd64() % 3);
        return n;
    default: /* tiny inputs */
        rndfill(b, 8);
        return rnd64() % 8;
    }
}

static void test_fuzz(void)
{
    static uint8_t base[PQM_SIG_MAX_BYTES + 64], buf[PQM_SIG_MAX_BYTES + 128];
    static uint8_t heap_like[PQM_SIG_MAX_BYTES + 128];
    unsigned total = 0, accepted = 0, canonical = 1;
    printf("--- fuzzed decoders ---\n");
    for (pqm_level_t level = PQM_LEVEL_STANDARD; level <= PQM_LEVEL_MATRIX; level++) {
        make_objects(level);
        for (unsigned i = 0; i < 6; i++) {
            codec_t *c = &codecs[i];
            size_t n = c->enc(c->obj, base, sizeof base);
            for (unsigned it = 0; it < 1200; it++) {
                memcpy(buf, base, n);
                size_t l = mutate(buf, n, sizeof buf - 64);
                /* Copy into an exactly-sized region at the end of a buffer
                 * so an over-read runs off the end of the object. */
                uint8_t *in = heap_like + sizeof heap_like - (l ? l : 1);
                memcpy(in, buf, l);
                bool ok = c->dec(c->tmp, l ? in : NULL, l);
                total++;
                if (ok) {
                    accepted++;
                    size_t m = c->enc(c->tmp, wire2, sizeof wire2);
                    if (m != l || memcmp(wire2, in, l) != 0) canonical = 0;
                } else {
                    static uint8_t zero[sizeof(pqm_sig_t)];
                    if (memcmp(c->tmp, zero, c->size) != 0) canonical = 0;
                }
            }
        }
    }
    /* Pure noise across every decoder and every length 0..200. */
    for (unsigned it = 0; it < 4000; it++) {
        size_t l = rnd64() % 200;
        rndfill(buf, l);
        if (l >= 6 && (it & 1)) buf[0] = 'Z', buf[1] = 'Q', buf[2] = 1;
        for (unsigned i = 0; i < 6; i++) {
            total++;
            if (codecs[i].dec(codecs[i].tmp, buf, l)) accepted++;
        }
    }
    char what[160];
    snprintf(what, sizeof what,
             "%u fuzzed inputs: no crash, %u accepted, every accepted input canonical, "
             "every rejected object zeroed",
             total, accepted);
    check(total >= 20000 && canonical, what, 0, "");
}

/* ===================================================================== */
/* 5. benchmarks                                                          */
/* ===================================================================== */

/* Repeat the statement until min_s seconds or max_n runs; dst gets the
 * seconds per run. */
#define BENCH(dst, min_s, max_n, ...)                                                              \
    do {                                                                                           \
        unsigned _n = 0;                                                                           \
        double _t0 = now(), _t;                                                                    \
        do {                                                                                       \
            __VA_ARGS__;                                                                           \
            _n++;                                                                                  \
            _t = now() - _t0;                                                                      \
        } while (_t < (min_s) && _n < (max_n));                                                    \
        (dst) = _t / _n;                                                                           \
    } while (0)

static void ms(double s, char *out, size_t cap)
{
    if (s < 1e-3)
        snprintf(out, cap, "%7.1f us", s * 1e6);
    else
        snprintf(out, cap, "%7.2f ms", s * 1e3);
}

static void bench(void)
{
    printf("\n--- benchmarks (this build's flags; one core) ---\n");
    printf("%-9s %10s %10s %10s | %10s %10s %10s | %7s %7s %7s\n", "level", "kem kgen", "encaps",
           "decaps", "sig kgen", "sign", "verify", "pk B", "ct B", "sig B");
    uint8_t seed[32] = {1}, coins[32] = {2}, ss[32];
    static const uint8_t msg[64] = {3};
    for (pqm_level_t level = PQM_LEVEL_STANDARD; level <= PQM_LEVEL_MATRIX; level++) {
        double t[6];
        char s[6][16];
        BENCH(t[0], 0.2, 2000, pqm_kem_keygen(level, seed, &kpk, &ksk));
        BENCH(t[1], 0.2, 2000, pqm_encaps(&kpk, coins, &kct, ss));
        BENCH(t[2], 0.2, 2000, pqm_decaps(&ksk, &kct, ss));
        unsigned few = level == PQM_LEVEL_MATRIX ? 2 : 2000;
        BENCH(t[3], 0.2, few, pqm_sig_keygen(level, seed, &spk, &ssk));
        BENCH(t[4], 0.2, few, pqm_sign(&ssk, msg, sizeof msg, NULL, 0, coins, &sig));
        bool vok = true;
        BENCH(t[5], 0.2, 2000, vok &= pqm_verify(&spk, msg, sizeof msg, NULL, 0, &sig));
        quiet(vok, "benchmark signatures verify");
        for (int i = 0; i < 6; i++) ms(t[i], s[i], sizeof s[i]);
        printf("%-9s %10s %10s %10s | %10s %10s %10s | %7zu %7zu %7zu\n", pqm_level_name(level),
               s[0], s[1], s[2], s[3], s[4], s[5],
               pqm_encoded_size(PQM_OBJ_KEM_PK, level) + pqm_encoded_size(PQM_OBJ_SIG_PK, level),
               pqm_encoded_size(PQM_OBJ_KEM_CT, level), pqm_encoded_size(PQM_OBJ_SIG, level));
    }
    printf("(pk B = KEM + signature public key; ct B = one KEM ciphertext; all encoded sizes)\n");

    /* Component breakdown for MATRIX, so the cost of each layer is visible. */
    {
        static uint8_t ek[PQM_MLKEM1024_EK_BYTES], dk[PQM_MLKEM1024_DK_BYTES], c[PQM_HQC5_CT_BYTES],
            hp[PQM_HQC5_PK_BYTES], hs[PQM_HQC5_SK_BYTES];
        uint8_t xk[32], xp[32];
        char a[16], b[16], d[16];
        double tt;
        BENCH(tt, 0.1, 2000, pqm_mlkem1024_keygen(seed, coins, ek, dk));
        ms(tt, a, sizeof a);
        BENCH(tt, 0.1, 2000, pqm_mlkem1024_encaps(ek, coins, c, ss));
        ms(tt, b, sizeof b);
        BENCH(tt, 0.1, 2000, pqm_mlkem1024_decaps(dk, c, ss));
        ms(tt, d, sizeof d);
        printf("  layer ML-KEM-1024     keygen %s  encaps %s  decaps %s\n", a, b, d);
        BENCH(tt, 0.1, 2000, pqm_hqc5_keygen(seed, hp, hs));
        ms(tt, a, sizeof a);
        BENCH(tt, 0.1, 2000, pqm_hqc5_encaps(hp, coins, coins, c, ss));
        ms(tt, b, sizeof b);
        BENCH(tt, 0.1, 2000, pqm_hqc5_decaps(hs, c, ss));
        ms(tt, d, sizeof d);
        printf("  layer HQC-5           keygen %s  encaps %s  decaps %s\n", a, b, d);
        BENCH(tt, 0.1, 2000, x25519_public(xp, seed));
        ms(tt, a, sizeof a);
        BENCH(tt, 0.1, 2000, x25519(xk, coins, xp));
        ms(tt, b, sizeof b);
        printf("  layer X25519          keygen %s  shared %s\n", a, b);
    }

    /* Bulk data: ChaCha20-Poly1305, the same at every level. */
    static uint8_t bulk[1u << 20];
    uint8_t key[32] = {4}, nonce[12] = {5}, tag[16];
    double tb;
    BENCH(tb, 0.5, 1000, aead_seal(key, nonce, NULL, 0, bulk, bulk, sizeof bulk, tag));
    double mbs = (double) sizeof bulk / tb / 1e6;
    printf("\n  bulk ChaCha20-Poly1305 (256-bit key): %.0f MB/s at every level\n", mbs);
    /* What a 1 MiB session costs end to end: one handshake + the bulk. */
    double hs_std = 0, hs_mx = 0;
    BENCH(hs_std, 0.1, 500, pqm_kem_keygen(PQM_LEVEL_STANDARD, seed, &kpk, &ksk);
          pqm_encaps(&kpk, coins, &kct, ss); pqm_decaps(&ksk, &kct, ss));
    BENCH(hs_mx, 0.1, 500, pqm_kem_keygen(PQM_LEVEL_MATRIX, seed, &kpk, &ksk);
          pqm_encaps(&kpk, coins, &kct, ss); pqm_decaps(&ksk, &kct, ss));
    printf("  ephemeral handshake (keygen+encaps+decaps): STANDARD %.2f ms, MATRIX %.2f ms\n",
           hs_std * 1e3, hs_mx * 1e3);
    printf("  bulk cost per MiB: %.2f ms at any level; MATRIX's extra handshake cost equals\n"
           "  %.1f MiB of bulk encryption, paid once per session, never per byte\n",
           tb * 1e3, (hs_mx - hs_std) / tb);
}

int main(int argc, char **argv)
{
    (void) argv;
    printf("=== pq_matrix: layered post-quantum KEM and dual signatures ===\n");
    test_kats();
    printf("--- combiner and seed expansion ---\n");
    for (pqm_level_t l = PQM_LEVEL_STANDARD; l <= PQM_LEVEL_MATRIX; l++) test_combiner(l);
    printf("--- hybrid KEM ---\n");
    for (pqm_level_t l = PQM_LEVEL_STANDARD; l <= PQM_LEVEL_MATRIX; l++) test_kem(l);
    printf("--- dual signatures ---\n");
    for (pqm_level_t l = PQM_LEVEL_STANDARD; l <= PQM_LEVEL_MATRIX; l++) test_sig(l);
    check(PQM_LEVEL_ECONOMY_DEFAULT == PQM_LEVEL_MATRIX, "economy keys default to MATRIX", 0, "");
    {
        uint8_t z[sizeof ksk] = {0};
        pqm_kem_sk_wipe(&ksk);
        check(memcmp(&ksk, z, sizeof ksk) == 0, "pqm_kem_sk_wipe zeroises the secret key", 0, "");
    }
    printf("--- wire format ---\n");
    for (pqm_level_t l = PQM_LEVEL_STANDARD; l <= PQM_LEVEL_MATRIX; l++) test_wire(l);
    test_fuzz();
    if (argc < 2) bench(); /* any argument skips the benchmarks */

    printf("\nchecks: %d, failures: %d\n", checks, failures);
    if (failures) {
        printf("[FAIL] pq_matrix: %d failures\n", failures);
        return 1;
    }
    return 0;
}
