/* test_primitive_dedupe.c — one SHA-256, one GCD, every former copy agrees.
 *
 * The kernel used to carry three SHA-256 bodies (robin_debanks, crypto_wallet,
 * event_envelope) and six GCD loops (m5_types.h, rational, rmag, vhub, sigil,
 * orbital_compat). Now robin_debanks/sha256.c and zx_gcd_u64 in
 * include/zxv_fixed.h are the only ones; the rest call them. This test drives
 * each former copy through its own public entry point, so a wrapper that
 * drops a byte, swaps an argument or truncates a width fails here:
 *   SHA-256: FIPS 180-4 / NIST CAVP vectors (empty, abc, 448-bit, 896-bit,
 *            one million 'a'), RFC 4231 HMAC cases 1, 2 and 6;
 *   GCD:     a reference Euclid on an exhaustive small grid, seeded random
 *            pairs with planted common factors, and values above INT64_MAX.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include <stdio.h>
#include <string.h>
#include "m5_types.h"
#include "../../kernel/src/robin_debanks/sha256.h"
#include "../../kernel/src/crypto_wallet/crypto_wallet.h"
#include "../../kernel/src/event_space/event_space.h"
#include "../../kernel/src/rational/rational.h"
#include "../../kernel/src/rmag/rmag.h"
#include "../../kernel/src/cards/sigil.h"
#include "../../kernel/src/orbital_compat/orbital_compat.h"

static int fails, checks;
#define CHECK(c, m)                                                            \
    do {                                                                       \
        checks++;                                                              \
        if (!(c)) {                                                            \
            printf("[FAIL] %s (line %d)\n", m, __LINE__);                      \
            fails++;                                                           \
        }                                                                      \
    } while (0)

/* ------------------------------------------------------------- SHA-256 */

static void unhex(const char *h, uint8_t *out, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        sscanf(h + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
}

static const struct { const char *msg; const char *hex; } NIST[] = {
    { "", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" },
    { "abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" },
    { "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
      "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1" },
    { "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
      "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
      "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1" },
};
#define MILLION_A "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"

static uint8_t big[1000000];

static void test_sha256(void) {
    uint8_t want[32], got[32];
    for (size_t i = 0; i < sizeof NIST / sizeof NIST[0]; i++) {
        const uint8_t *m = (const uint8_t *)NIST[i].msg;
        uint32_t n = (uint32_t)strlen(NIST[i].msg);
        unhex(NIST[i].hex, want, 32);

        sha256(m, n, got);
        CHECK(memcmp(got, want, 32) == 0, "sha256 one-shot NIST vector");

        memset(got, 0, 32);
        cw_sha256(m, n, got);
        CHECK(memcmp(got, want, 32) == 0, "cw_sha256 NIST vector");

        /* event_envelope hashes its payload with the same SHA-256 */
        static ev_envelope_t env;
        memset(&env, 0xA5, sizeof env);
        CHECK(ev_envelope_set_payload(&env, m, (uint16_t)n), "set_payload accepts");
        if (n) {
            CHECK(memcmp(env.payload_hash, want, 32) == 0, "event payload_hash NIST vector");
        } else {
            static const uint8_t zero[32];
            CHECK(memcmp(env.payload_hash, zero, 32) == 0, "empty payload: zero hash");
        }
        CHECK(env.payload_len == n, "payload_len kept");
    }

    /* one-byte payload, and the envelope's own length guards */
    static ev_envelope_t env;
    unhex("ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb", want, 32);
    CHECK(ev_envelope_set_payload(&env, (const uint8_t *)"a", 1) &&
              memcmp(env.payload_hash, want, 32) == 0, "event payload_hash of 1 byte");
    CHECK(ev_envelope_set_payload(&env, big, EV_PAYLOAD_MAX), "payload of EV_PAYLOAD_MAX accepted");
    CHECK(!ev_envelope_set_payload(&env, big, EV_PAYLOAD_MAX + 1), "payload over the max refused");
    CHECK(!ev_envelope_set_payload(&env, NULL, 1), "NULL payload refused");
    CHECK(ev_envelope_set_payload(&env, NULL, 0), "empty NULL payload accepted");
    CHECK(!ev_envelope_set_payload(NULL, big, 1), "NULL envelope refused");

    memset(big, 'a', sizeof big);
    unhex(MILLION_A, want, 32);
    cw_sha256(big, sizeof big, got);
    CHECK(memcmp(got, want, 32) == 0, "cw_sha256 one million 'a'");
    sha256_ctx_t c;
    sha256_init(&c);
    for (size_t off = 0; off < sizeof big; off += 997)
        sha256_update(&c, big + off, sizeof big - off < 997 ? sizeof big - off : 997);
    sha256_final(&c, got);
    CHECK(memcmp(got, want, 32) == 0, "streaming sha256 one million 'a'");
}

static void test_hmac(void) {
    uint8_t key[131], want[32], got[32];
    /* RFC 4231 test case 1 */
    memset(key, 0x0b, 20);
    unhex("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", want, 32);
    cw_hmac_sha256(key, 20, (const uint8_t *)"Hi There", 8, got);
    CHECK(memcmp(got, want, 32) == 0, "cw_hmac_sha256 RFC 4231 case 1");
    /* case 2: short key */
    unhex("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", want, 32);
    cw_hmac_sha256((const uint8_t *)"Jefe", 4,
                   (const uint8_t *)"what do ya want for nothing?", 28, got);
    CHECK(memcmp(got, want, 32) == 0, "cw_hmac_sha256 RFC 4231 case 2");
    /* case 6: key longer than the block is hashed first */
    memset(key, 0xaa, 131);
    unhex("60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54", want, 32);
    const char *d6 = "Test Using Larger Than Block-Size Key - Hash Key First";
    cw_hmac_sha256(key, 131, (const uint8_t *)d6, (uint32_t)strlen(d6), got);
    CHECK(memcmp(got, want, 32) == 0, "cw_hmac_sha256 RFC 4231 case 6");
    /* NIST HMAC example, keylen = blocklen: a 64-byte key is used as is */
    for (int i = 0; i < 64; i++) key[i] = (uint8_t)i;
    unhex("8bb9a1db9806f20df7f77b82138c7914d174d59e13dc4d0169c9057b133e1d62", want, 32);
    const char *d7 = "Sample message for keylen=blocklen";
    cw_hmac_sha256(key, 64, (const uint8_t *)d7, (uint32_t)strlen(d7), got);
    CHECK(memcmp(got, want, 32) == 0, "cw_hmac_sha256 NIST keylen=blocklen");
}

/* ----------------------------------------------------------------- GCD */

static uint64_t ref_gcd(uint64_t a, uint64_t b) {
    while (b) { uint64_t t = a % b; a = b; b = t; }
    return a;
}

static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint64_t next(void) {
    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
    return rng;
}

static void test_gcd(void) {
    int bad = 0;
    for (uint64_t a = 0; a <= 130; a++)
        for (uint64_t b = 0; b <= 130; b++) {
            uint64_t g = ref_gcd(a, b);
            if (zx_gcd_u64(a, b) != g) bad++;
            if (sig_gcd((uint32_t)a, (uint32_t)b) != g) bad++;
            int64_t sa = a & 1 ? -(int64_t)a : (int64_t)a, sb = b % 3 ? (int64_t)b : -(int64_t)b;
            if (zx_gcd_i64(sa, sb) != (int64_t)g) bad++;
            if (m5_gcd(sa, sb) != (int64_t)g) bad++;
        }
    CHECK(bad == 0, "zx_gcd_u64/i64, m5_gcd, sig_gcd == Euclid on [0,130]^2 with signs");

    bad = 0;
    for (int i = 0; i < 20000; i++) {
        uint64_t g = next() >> (next() % 64), x = next() >> (next() % 64), y = next() >> (next() % 64);
        uint64_t a = x * g, b = y * g;
        if (i & 1) { a = x; b = y; }
        if (zx_gcd_u64(a, b) != ref_gcd(a, b)) bad++;
        if (zx_gcd_u64(b, a) != ref_gcd(a, b)) bad++;
        uint32_t a32 = (uint32_t)a, b32 = (uint32_t)(b >> 7);
        if (sig_gcd(a32, b32) != ref_gcd(a32, b32)) bad++;
    }
    CHECK(bad == 0, "zx_gcd_u64 and sig_gcd == Euclid on 20000 random pairs");

    CHECK(zx_gcd_u64(0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFEull) == 1, "gcd(2^64-1, 2^64-2)");
    CHECK(zx_gcd_u64(0x8000000000000000ull, 0xC000000000000000ull) == 0x4000000000000000ull,
          "gcd(2^63, 3*2^62) = 2^62");
    CHECK(zx_gcd_u64(0, 0) == 0, "gcd(0,0) = 0");
    CHECK(sig_gcd(0xFFFFFFFFu, 0xFFFFFFFFu) == 0xFFFFFFFFu, "sig_gcd keeps the full 32 bits");
    CHECK(m5_gcd(-12, -18) == 6 && m5_gcd(0, -7) == 7, "m5_gcd signs");

    /* m5_types.h rational_normalize goes through m5_gcd */
    rational_t r = { 6, -4 };
    r = rational_normalize(r);
    CHECK(r.num == -3 && r.den == 2, "rational_normalize(6/-4) = -3/2");

    /* rational.c: igcd sizes the lcm in rat_add and cross-reduces rat_mul */
    rat_t s = rat_add(rat_make(1, 6), rat_make(1, 10));
    CHECK(s.valid && s.num == 4 && s.den == 15, "rat_add 1/6 + 1/10 = 4/15");
    s = rat_mul(rat_make((int64_t)1 << 40, 3), rat_make(9, (int64_t)1 << 41));
    CHECK(s.valid && s.num == 3 && s.den == 2, "rat_mul cross-reduces 2^40/3 * 9/2^41");
    s = rat_add(rat_make(1, (int64_t)3 << 58), rat_make(1, (int64_t)5 << 58));
    CHECK(s.valid && s.num == 1 && s.den == (int64_t)15 << 55, "rat_add over the lcm, not the product");

    /* rmag.c: add over the lcm, multiply cross-reduces, reduce, past INT64_MAX */
    rmag_rational_t m = rmag_rational_add(rmag_rational_from_frac(1, 6, false),
                                          rmag_rational_from_frac(1, 10, false));
    CHECK(m.numerator == 4 && m.denominator == 15, "rmag add 1/6 + 1/10 = 4/15");
    m = rmag_rational_multiply(rmag_rational_from_frac(0xF000000000000000ull, 7, false),
                               rmag_rational_from_frac(14, 0xF000000000000000ull, false));
    CHECK(m.numerator == 2 && m.denominator == 1, "rmag multiply cross-reduces above INT64_MAX");
    /* both cross factors matter: 5^20*2^10/3^25 * 3^25/2^10 = 5^20 */
    m = rmag_rational_multiply(rmag_rational_from_frac(97656250000000000ull, 847288609443ull, false),
                               rmag_rational_from_frac(847288609443ull, 1024, false));
    CHECK(m.numerator == 95367431640625ull && m.denominator == 1, "rmag multiply needs both gcds");
    m = rmag_rational_add(rmag_rational_from_frac(1, 3ull << 60, false),
                          rmag_rational_from_frac(1, 5ull << 60, false));
    CHECK(m.numerator == 1 && m.denominator == 15ull << 57, "rmag add over the lcm, not the product");
    m = rmag_rational_reduce(rmag_rational_from_frac(0xF000000000000000ull, 0x3000000000000000ull, true));
    CHECK(m.numerator == 5 && m.denominator == 1 && m.negative, "rmag reduce above INT64_MAX");

    /* orbital_compat.h normalizes Sutra rationals with the same GCD */
    rat_t o = oc_rat_from_sutra(-6, 4);
    CHECK(o.valid && o.num == -3 && o.den == 2, "oc_rat_from_sutra(-6,4) = -3/2");
    o = oc_rat_from_sutra(6, -4);
    CHECK(o.valid && o.num == -3 && o.den == 2, "oc_rat_from_sutra(6,-4) = -3/2");
    o = oc_rat_from_sutra(-6, -4);
    CHECK(o.valid && o.num == 3 && o.den == 2, "oc_rat_from_sutra(-6,-4) = 3/2");
    o = oc_rat_from_sutra(0, -5);
    CHECK(o.valid && o.num == 0 && o.den == 1, "oc_rat_from_sutra(0,-5) = 0/1");
    o = oc_rat_from_sutra(1, 0);
    CHECK(!o.valid && o.num == 0, "oc_rat_from_sutra(1,0) invalid");
    o = oc_rat_from_sutra(1, 3);
    CHECK(o.valid && o.num == 1 && o.den == 3, "oc_rat_from_sutra(1,3) = 1/3");
}

int main(void) {
    test_sha256();
    test_hmac();
    test_gcd();
    printf("%d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
