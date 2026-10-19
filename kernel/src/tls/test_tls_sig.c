/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_tls_sig.c — the signature primitives under the certificate verifier,
 * against EXTERNAL vectors only:
 *   SHA-384 / SHA-512   FIPS 180-4 examples (NIST CSRC "SHA2 Additional")
 *   ECDSA P-256, P-384  Wycheproof (valid, malformed DER, out-of-range r/s,
 *                       edge-case points, hash truncation)
 *   RSA-PSS             Wycheproof (2048/SHA-256, 4096/SHA-512)
 *   RSA PKCS #1 v1.5    Wycheproof (2048/SHA-256 incl. every padding and
 *                       DigestInfo forgery, 3072/SHA-384)
 * Plus the module's own policy floors: RSA-1024 and even exponents refused. */
#include <stdio.h>
#include <string.h>
#include "sha512.h"
#include "digest.h"
#include "ecdsa.h"
#include "rsa.h"
#include "test_tls_sig_vectors.h"

static int failures = 0, checks = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            printf("[FAIL] " __VA_ARGS__);                                                         \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static uint32_t unhex(const char *h, uint8_t *out, uint32_t cap)
{
    uint32_t n = (uint32_t) strlen(h) / 2;
    if (n > cap) return 0xffffffffu;
    for (uint32_t i = 0; i < n; i++) {
        unsigned v;
        sscanf(h + 2 * i, "%2x", &v);
        out[i] = (uint8_t) v;
    }
    return n;
}

static void test_sha2(void)
{
    uint8_t d[64], want[64];
    /* FIPS 180-4 examples: "abc" and the two-block 896-bit message */
    const char *two = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklm"
                      "nopqklmnopqrlmnopqrsmnopqrstnopqrstu";
    tls_sha512((const uint8_t *) "abc", 3, d);
    unhex("ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836"
          "ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
          want, 64);
    CHECK(!memcmp(d, want, 64), "SHA-512(abc)");
    tls_sha512((const uint8_t *) two, (uint32_t) strlen(two), d);
    unhex("8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018501d289e4900f7e4331b"
          "99dec4b5433ac7d329eeb6dd26545e96e55b874be909",
          want, 64);
    CHECK(!memcmp(d, want, 64), "SHA-512(896-bit message)");
    tls_sha384((const uint8_t *) "abc", 3, d);
    unhex("cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358"
          "baeca134c825a7",
          want, 48);
    CHECK(!memcmp(d, want, 48), "SHA-384(abc)");
    tls_sha384((const uint8_t *) two, (uint32_t) strlen(two), d);
    unhex("09330c33f71147e83d192fc782cd1b4753111b173b3b05d22fa08086e3b0f712fcc7c71a557e2db966c3"
          "e9fa91746039",
          want, 48);
    CHECK(!memcmp(d, want, 48), "SHA-384(896-bit message)");
    tls_sha512((const uint8_t *) "", 0, d);
    unhex("cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff83"
          "18d2877eec2f63b931bd47417a81a538327af927da3e",
          want, 64);
    CHECK(!memcmp(d, want, 64), "SHA-512(empty)");

    /* streaming in odd pieces equals one shot, across a block boundary */
    static uint8_t big[1000];
    for (int i = 0; i < 1000; i++) big[i] = (uint8_t) (i * 7 + 1);
    tls_sha512_ctx_t c;
    tls_sha512_init(&c);
    for (int at = 0; at < 1000;) {
        int take = 1 + (at % 129);
        if (at + take > 1000) take = 1000 - at;
        tls_sha512_update(&c, big + at, (uint32_t) take);
        at += take;
    }
    tls_sha512_final(&c, d);
    tls_sha512(big, 1000, want);
    CHECK(!memcmp(d, want, 64), "SHA-512 streaming == one shot");
    printf("[PASS] SHA-384/512: FIPS 180-4 examples and streaming\n");
}

static void test_wycheproof(void)
{
    static uint8_t msg[8192], sig[1024], a[1024], b[16], dg[64];
    int counts[4][3] = {{0}}, accepted_acceptable = 0;
    for (size_t i = 0; i < sizeof wp_vecs / sizeof wp_vecs[0]; i++) {
        const wp_vec_t *v = &wp_vecs[i];
        uint32_t ml = unhex(v->msg, msg, sizeof msg);
        uint32_t sl = unhex(v->sig, sig, sizeof sig);
        uint32_t al = unhex(wp_keys[v->key].a, a, sizeof a);
        uint32_t bl = unhex(wp_keys[v->key].b, b, sizeof b);
        uint32_t dl = tls_digest((tls_hash_t) v->hash, msg, ml, dg);
        bool ok;
        if (v->kind == WP_ECDSA)
            ok = ecdsa_verify_digest((ecdsa_curve_t) v->curve, a, al, dg, dl, sig, sl);
        else if (v->kind == WP_PSS)
            ok = rsa_verify_pss(a, al, b, bl, (tls_hash_t) v->hash, dg, sig, sl);
        else
            ok = rsa_verify_pkcs1(a, al, b, bl, (tls_hash_t) v->hash, dg, sig, sl);
        counts[v->kind][v->result]++;
        if (v->result == 2) {
            accepted_acceptable += ok;
            continue; /* "acceptable": either answer is allowed */
        }
        CHECK(ok == (v->result == 1), "wycheproof kind %d hash %d tcId %d: got %s, want %s",
              v->kind, v->hash, v->tcid, ok ? "accept" : "reject",
              v->result == 1 ? "accept" : "reject");
    }
    printf("[INFO] wycheproof ECDSA %d valid / %d invalid, PSS %d / %d, PKCS1 %d / %d, "
           "%d acceptable (%d accepted)\n",
           counts[1][1], counts[1][0], counts[2][1], counts[2][0], counts[3][1], counts[3][0],
           counts[1][2] + counts[2][2] + counts[3][2], accepted_acceptable);
}

static void test_policy(void)
{
    /* a 1024-bit odd modulus is refused whatever the signature */
    uint8_t n1024[128], n2048[256], e3[1] = {3}, e2[1] = {2}, e1[1] = {1}, e5[5] = {1, 0, 0, 0, 1};
    memset(n1024, 0xff, sizeof n1024);
    memset(n2048, 0xff, sizeof n2048);
    CHECK(!rsa_key_acceptable(n1024, sizeof n1024, e3, 1), "RSA-1024 refused");
    CHECK(rsa_key_acceptable(n2048, sizeof n2048, e3, 1), "RSA-2048 e=3 accepted as a key");
    CHECK(!rsa_key_acceptable(n2048, sizeof n2048, e2, 1), "even exponent refused");
    CHECK(!rsa_key_acceptable(n2048, sizeof n2048, e1, 1), "e = 1 refused");
    CHECK(!rsa_key_acceptable(n2048, sizeof n2048, e5, 5), "exponent over 32 bits refused");
    n2048[255] = 0xfe;
    CHECK(!rsa_key_acceptable(n2048, sizeof n2048, e3, 1), "even modulus refused");

    /* ECDSA: a compressed point and the generator with a flipped y are refused */
    uint8_t pub[65];
    memset(pub, 0, sizeof pub);
    pub[0] = 0x02;
    CHECK(!ecdsa_pubkey_valid(ECDSA_P256, pub, 33), "compressed point refused");
    CHECK(!ecdsa_pubkey_valid(ECDSA_P256, pub, 65), "garbage point refused");
    unhex("046b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c2964fe342e2fe1a7f9b8ee7"
          "eb4a7c0f9e162bce33576b315ececbb6406837bf51f5",
          pub, 65);
    CHECK(ecdsa_pubkey_valid(ECDSA_P256, pub, 65), "P-256 generator is on the curve");
    pub[64] ^= 1;
    CHECK(!ecdsa_pubkey_valid(ECDSA_P256, pub, 65), "generator with y+-1 is off the curve");
    CHECK(!ecdsa_pubkey_valid(ECDSA_P384, pub, 65), "P-256 length refused for P-384");
}

int main(void)
{
    test_sha2();
    test_wycheproof();
    test_policy();
    printf("%d checks, %d failures\n", checks, failures);
    if (failures) {
        printf("[FAIL] test_tls_sig\n");
        return 1;
    }
    printf("[PASS] test_tls_sig\n");
    return 0;
}
