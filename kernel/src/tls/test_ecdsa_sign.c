/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_ecdsa_sign.c — the P-256 signer (ecdsa.h S1-S3) against RFC 6979
 * A.2.5 (the published key, public point, and the deterministic signatures
 * for "sample" and "test" with SHA-256), then round trips through the
 * Wycheproof-tested verifier and the refusals. */
#include <stdio.h>
#include <string.h>
#include "ecdsa.h"
#include "../robin_debanks/sha256.h"

static int failures = 0, checks = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void unhex(const char *h, uint8_t *o, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        sscanf(h + 2 * i, "%2x", &v);
        o[i] = (uint8_t) v;
    }
}

/* r and s out of a DER signature, as 32 big-endian bytes each */
static int der_rs(const uint8_t *d, uint32_t n, uint8_t r[32], uint8_t s[32])
{
    uint32_t at = 2;
    if (n < 8 || d[0] != 0x30 || d[1] != n - 2) return 0;
    for (int k = 0; k < 2; k++) {
        uint8_t *o = k ? s : r;
        if (d[at] != 0x02) return 0;
        uint32_t l = d[at + 1];
        at += 2;
        const uint8_t *v = d + at;
        at += l;
        if (l == 33) v++, l--;
        if (l > 32) return 0;
        memset(o, 0, 32);
        memcpy(o + 32 - l, v, l);
    }
    return at == n;
}

int main(void)
{
    uint8_t d[32], pub[65], want[65], h[32], sig[ECDSA_P256_SIG_MAX], r[32], s[32], wr[32], ws[32];
    unhex("C9AFA9D845BA75166B5C215767B1D6934E50C3DB36E89B127B8A622B120F6721", d, 32);
    want[0] = 4;
    unhex("60FED4BA255A9D31C961EB74C6356D68C049B8923B61FA6CE669622E60F29FB6", want + 1, 32);
    unhex("7903FE1008B8BC99A41AE9E95628BC64F2F1B20C2D7E9F5177A3C294D4462299", want + 33, 32);
    CHECK(ecdsa_p256_public(d, pub), "public key from d");
    CHECK(!memcmp(pub, want, 65), "RFC 6979 A.2.5 public point");
    CHECK(ecdsa_pubkey_valid(ECDSA_P256, pub, 65), "public point on the curve");

    static const struct {
        const char *msg, *r, *s;
    } V[] = {
        {"sample", "EFD48B2AACB6A8FD1140DD9CD45E81D69D2C877B56AAF991C34D0EA84EAF3716",
         "F7CB1C942D657C41D436C7A1B6E29F65F3E900DBB9AFF4064DC4AB2F843ACDA8"},
        {"test", "F1ABB023518351CD71D881567B1EA663ED3EFCF6C5132B354F28D3B0B7D38367",
         "019F4113742A2B14BD25926B49C649155F267E60D3814B4C0CC84250E46F0083"},
    };
    for (int i = 0; i < 2; i++) {
        sha256((const uint8_t *) V[i].msg, (uint32_t) strlen(V[i].msg), h);
        uint32_t n = ecdsa_p256_sign_digest(d, h, 32, sig, sizeof sig);
        CHECK(n > 0 && n <= ECDSA_P256_SIG_MAX, "signs");
        unhex(V[i].r, wr, 32);
        unhex(V[i].s, ws, 32);
        CHECK(der_rs(sig, n, r, s), "minimal DER");
        CHECK(!memcmp(r, wr, 32), "RFC 6979 A.2.5 r");
        CHECK(!memcmp(s, ws, 32), "RFC 6979 A.2.5 s");
        CHECK(ecdsa_verify_digest(ECDSA_P256, pub, 65, h, 32, sig, n), "verifies");
        h[0] ^= 1;
        CHECK(!ecdsa_verify_digest(ECDSA_P256, pub, 65, h, 32, sig, n), "other digest refused");
    }

    /* many keys: each signature verifies, and only under its own key */
    for (int k = 0; k < 24; k++) {
        uint8_t dk[32], pk[65];
        sha256((const uint8_t *) &k, sizeof k, dk);
        if (!ecdsa_p256_public(dk, pk)) continue;
        sha256(dk, 32, h);
        uint32_t n = ecdsa_p256_sign_digest(dk, h, 32, sig, sizeof sig);
        CHECK(n && ecdsa_verify_digest(ECDSA_P256, pk, 65, h, 32, sig, n), "random key round trip");
        CHECK(!ecdsa_verify_digest(ECDSA_P256, pub, 65, h, 32, sig, n), "wrong key refused");
        /* a SHA-512 digest: its leftmost 256 bits are signed */
        uint8_t big[64];
        memcpy(big, h, 32);
        memset(big + 32, 0xAB, 32);
        n = ecdsa_p256_sign_digest(dk, big, 64, sig, sizeof sig);
        CHECK(n && ecdsa_verify_digest(ECDSA_P256, pk, 65, big, 64, sig, n),
              "long digest truncated");
    }

    /* S3 refusals: d = 0, d = n, d > n, a short buffer, NULLs */
    uint8_t zero[32] = {0}, nn[32], ff[32];
    unhex("FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551", nn, 32);
    memset(ff, 0xFF, 32);
    CHECK(!ecdsa_p256_public(zero, pub), "d = 0 refused");
    CHECK(!ecdsa_p256_public(nn, pub), "d = n refused");
    CHECK(!ecdsa_p256_public(ff, pub), "d > n refused");
    CHECK(ecdsa_p256_sign_digest(zero, h, 32, sig, sizeof sig) == 0, "sign with d = 0 refused");
    CHECK(ecdsa_p256_sign_digest(d, h, 32, sig, ECDSA_P256_SIG_MAX - 1) == 0,
          "short buffer refused");
    CHECK(ecdsa_p256_sign_digest(NULL, h, 32, sig, sizeof sig) == 0, "NULL key refused");
    nn[31] -= 1; /* n - 1 is the largest scalar */
    CHECK(ecdsa_p256_public(nn, pub), "d = n - 1 accepted");

    printf("%d checks, %d failures\n", checks, failures);
    if (failures) {
        printf("[FAIL] test_ecdsa_sign\n");
        return 1;
    }
    printf("[PASS] test_ecdsa_sign\n");
    return 0;
}
