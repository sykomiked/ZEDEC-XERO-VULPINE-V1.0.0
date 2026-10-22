/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_hkdf_props.c — the HKDF / TLS 1.3 key schedule API (hkdf.c) at its
 * edges: key lengths around the HMAC block, every Expand length up to the
 * 255-block limit, the label and buffer bounds, the schedule's refusals and
 * the values each step returns.
 *
 * test_hkdf.c already checks the RFC 5869 and RFC 8448 vectors. The values
 * here come from Python's hmac and hashlib (gen_hkdf_ref.py, the RFC text
 * written out), never from hkdf.c; input bytes are pat(n, seed): byte i is
 * (seed + 7*i) mod 256. */
#include <stdio.h>
#include <string.h>
#include "hkdf.h"
#include "../robin_debanks/sha256.h"

static int failures = 0;
static unsigned checks = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(cond)) {                                                                             \
            printf("  FAIL: %s (line %d)\n", msg, __LINE__);                                       \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static const struct {
    const char *name, *hex;
} REF[] = {
    {"hmac_k0", "53f1d10bd29a033a7a727cf14671bd6ae142cc93667c2b8495c43a5fddc1781b"},
    {"hmac_k1", "95cfb4c702c513b7f0d65909a363867ef89317ac75d4280cb0b39cafeeb745e1"},
    {"hmac_k63", "9d25c44e706312c26308e0d1ea9d8fcba1359849e19a94225ba0662af021780e"},
    {"hmac_k64", "9b156917f4f17113f157d3db9a2c979a5197b6568e4522ef9aaacb255b36fe77"},
    {"hmac_k65", "1cf142af4204425808c3ad94c280a5835d26beb94b3387e192d28847ae1845fb"},
    {"hmac_k130", "a4c83f954b4f7ed97e83d37b7eb6930c122ee4250e8d545e49a386f45812bd5d"},
    {"hmac_empty", "d0d6d58c87cbf4a52bfab018ab92a073effa0bffba4fde91df30c8278175d293"},
    {"extract_nosalt", "27b32344b5942e4939f2236c68ab245460f0dc477af0e1fa183fc14d2c89441d"},
    {"extract_salt", "4c1b914ef9e0837d350d86c566679fdcb08d77d17b98ad08b95bc747659ba15e"},
    {"extract_salt1", "d3a4ad4076cc963c0e88baaf14a8c06bc8cc787a7e3f040e870e6e6ac05327f0"},
    {"expand_1", "77"},
    {"expand_31", "771f7237f965562ac3a429ffda161018ae48b023a94019cee6f18b3de2c22d"},
    {"expand_32", "771f7237f965562ac3a429ffda161018ae48b023a94019cee6f18b3de2c22dee"},
    {"expand_33", "771f7237f965562ac3a429ffda161018ae48b023a94019cee6f18b3de2c22deef6"},
    {"expand_64", "771f7237f965562ac3a429ffda161018ae48b023a94019cee6f18b3de2c22deef6e0efcd3ac7c542"
                  "418d1c72028eedbca69f2a736f4a710ea70514ffd849385d"},
    {"expand_65", "771f7237f965562ac3a429ffda161018ae48b023a94019cee6f18b3de2c22deef6e0efcd3ac7c542"
                  "418d1c72028eedbca69f2a736f4a710ea70514ffd849385da7"},
    {"expand_300",
     "771f7237f965562ac3a429ffda161018ae48b023a94019cee6f18b3de2c22deef6e0efcd3ac7c542418d1c72028ee"
     "dbca69f2a736f4a710ea70514ffd849385da796d221fb4897724407399e0bca39de3bab1e3e3727dd26c5fe5dc88b"
     "4a621ee644b495333f71c08c73ba38eeb7eab21ba07df19c515888766ffb09ead59d0dff83870fe61dab584bb13f6"
     "2887ff139b8e7b0ffce518249cbe8c628805a5a00f03edbcf9acb88d5a501aae159e01f9bfb8dc1b77469a245368f"
     "673874191fcb4e630fcb1b051554a21f2098910876c89eb72ddd1d6f7cc4818b233c6bbc8c01d304157bdefe2f583"
     "85c95c89ecd57709c66eed0a321ac73962fa3e15a057c98a3c9bd391a4c8ed67f30616ed5e59f3368fb7bdbfa6d59"
     "0b99b13855e8dee54c9360d9dc432ada33cb0fe9b3"},
    {"expand_max_tail", "f149449f01aa66a8012d3f84b98c6c4d8bcf0f65a1d2e7fb7b9dafb0757c2315"},
    {"expand_info479_tail",
     "b017f42abad7ff566cba5a3517977a1b3995c2cd9e73c7760fae3f94f705119acf5a215a43abfa40"},
    {"label_300", "912de0ac71bc3c447ae2585eda6f8aae"},
    {"label_ctx", "963bcdd6d1a39a4cbcf57bed95ea64a3f86fce0e00e95812f7a20b540306c795"},
    {"label_64", "8e33e0367600e6647732b9be10ef5452ba11d277e0a6d5a9b6cd98e3f0b6efec"},
    {"label_empty", "e872b3c402aedc65e7af154f008c5cf3fad39c2b65a8a1bf71e952eac0a2fb7e"},
    {"early_psk", "bd208e4cd5d524f3e7bb2edbb9d325a597a9e9771644875eb4bf125eec2d26dd"},
    {"early_zero", "33ad0a1c607ec03b09e6cd9893680ce210adf300aa1f2660e1b22e10f170f92a"},
    {"hs", "659be510b78e81ad7431a983a294b162ef56c957cac9da6f166393fcb3c01e43"},
    {"c_hs", "9900a8e5d83ec483e9bcd507707abaaf8c0be24e3a6c5c29d34b155b8ec62475"},
    {"s_hs", "a19d9101130d908f78a3378eef3b1361af1ee1185254958555c24ecd53cd9d8d"},
    {"ms", "7da7ece4537abf9841d70fc8d4b6a3c171ec1499b348271831adbbe3d35f8b7f"},
    {"c_ap", "30df2a6ba5c944f83131744ad1eb74bec6b60548549b3d1d14300612390c72ff"},
    {"s_ap", "128e19803fa6267dbc7c5795559b30f1a56e99c0b33f50d094ec9a1fedd1bd18"},
    {"key16", "f2c1f75333372b561215eb84d84e2900"},
    {"iv12", "62718df696c77f7cd9bc28a3"},
    {"finished", "c513307d408b238dd7d007b77957a1efeacecdc515dfa61cc05de730952e8e43"},
};

static const char *ref(const char *name)
{
    for (unsigned i = 0; i < sizeof REF / sizeof REF[0]; i++)
        if (strcmp(REF[i].name, name) == 0) return REF[i].hex;
    printf("  no reference value %s\n", name);
    return "";
}

/* the bytes equal the named reference (its length is the hex length / 2) */
static int is(const uint8_t *b, const char *name)
{
    const char *h = ref(name);
    size_t n = strlen(h);
    if (!n) return 0;
    for (size_t i = 0; i < n / 2; i++) {
        unsigned v;
        if (sscanf(h + 2 * i, "%2x", &v) != 1 || b[i] != (uint8_t) v) return 0;
    }
    return 1;
}

static void pat(uint8_t *b, uint32_t n, uint32_t seed)
{
    for (uint32_t i = 0; i < n; i++) b[i] = (uint8_t) (seed + 7u * i);
}

static uint8_t big[255 * 32 + 64];

static void test_hmac_extract(void)
{
    uint8_t key[130], msg[100], out[32];
    pat(msg, 100, 9);
    static const uint32_t KL[] = {0, 1, 63, 64, 65, 130};
    for (unsigned i = 0; i < 6; i++) {
        char nm[16];
        snprintf(nm, sizeof nm, "hmac_k%u", KL[i]);
        pat(key, KL[i], 3);
        hmac_sha256(key, KL[i], msg, 100, out);
        CHECK(is(out, nm), "HMAC across the 64-byte block edge");
    }
    hmac_sha256(0, 0, msg, 100, out);
    CHECK(is(out, "hmac_k0"), "a NULL key is the empty key");
    pat(key, 20, 3);
    hmac_sha256(key, 20, msg, 0, out);
    CHECK(is(out, "hmac_empty"), "an empty message");
    hmac_sha256(key, 20, 0, 0, out);
    CHECK(is(out, "hmac_empty"), "a NULL message is the empty message");
    hmac_sha256(key, 20, 0, 5, out);
    CHECK(is(out, "hmac_empty"), "a NULL message with a length is still the empty message");
    hmac_sha256(0, 5, msg, 100, out);
    CHECK(is(out, "hmac_k0"), "a NULL key with a length is still the empty key");

    uint8_t salt[32], ikm[22], prk[32];
    pat(ikm, 22, 1);
    pat(salt, 13, 5);
    hkdf_extract(salt, 13, ikm, 22, prk);
    CHECK(is(prk, "extract_salt"), "Extract with a salt");
    hkdf_extract(salt, 1, ikm, 22, prk);
    CHECK(is(prk, "extract_salt1"), "a one-byte salt is a salt, not no salt");
    hkdf_extract(0, 32, ikm, 22, prk);
    CHECK(is(prk, "extract_nosalt"), "a NULL salt with a length is no salt");
    hkdf_extract(0, 100, ikm, 22, prk); /* longer than a block: never read */
    CHECK(is(prk, "extract_nosalt"), "a NULL salt longer than a block is no salt");
    hkdf_extract(0, 0, ikm, 22, prk);
    CHECK(is(prk, "extract_nosalt"), "no salt is 32 zero bytes");
    hkdf_extract(salt, 0, ikm, 22, prk);
    CHECK(is(prk, "extract_nosalt"), "a zero-length salt is no salt");
    memset(salt, 0, 32);
    hkdf_extract(salt, 32, ikm, 22, prk);
    CHECK(is(prk, "extract_nosalt"), "and equals 32 explicit zeros");
}

static void test_expand(void)
{
    uint8_t salt[13], ikm[22], prk[32], info[480];
    pat(salt, 13, 5);
    pat(ikm, 22, 1);
    hkdf_extract(salt, 13, ikm, 22, prk);
    pat(info, 10, 2);
    static const uint32_t LEN[] = {1, 31, 32, 33, 64, 65, 300};
    for (unsigned i = 0; i < 7; i++) {
        char nm[16];
        snprintf(nm, sizeof nm, "expand_%u", LEN[i]);
        memset(big, 0xEE, sizeof big);
        CHECK(hkdf_expand(prk, info, 10, big, LEN[i]) && is(big, nm), "Expand length");
        CHECK(big[LEN[i]] == 0xEE, "nothing written past out_len");
    }
    CHECK(hkdf_expand(prk, info, 10, big, 255 * 32) && is(big + 255 * 32 - 32, "expand_max_tail"),
          "the 255th block (counter 255) is right");
    CHECK(!hkdf_expand(prk, info, 10, big, 255 * 32 + 1), "256 blocks are refused");
    CHECK(hkdf_expand(prk, info, 10, big, 0), "zero bytes is a valid, empty Expand");
    CHECK(!hkdf_expand(0, info, 10, big, 32) && !hkdf_expand(prk, info, 10, 0, 32),
          "NULL key or output refused");
    pat(info, 480, 4);
    CHECK(hkdf_expand(prk, info, 479, big, 40) && is(big, "expand_info479_tail"),
          "the longest info (479 bytes) still expands correctly");
    CHECK(!hkdf_expand(prk, info, 480, big, 40), "480 bytes of info are refused");
    uint8_t a[40], b[40];
    CHECK(hkdf_expand(prk, 0, 3, a, 40) && hkdf_expand(prk, 0, 0, b, 40) && memcmp(a, b, 40) == 0,
          "NULL info with a length is empty info");
}

static void test_label(void)
{
    uint8_t enc[600], ctx[300];
    pat(ctx, 300, 6);
    char lab[300];
    memset(lab, 'a', sizeof lab);
    lab[249] = 0;
    uint32_t n = tls13_hkdf_label(enc, sizeof enc, 0x1234, lab, ctx, 255);
    CHECK(n == 2 + 1 + 255 + 1 + 255, "the longest label (6+249) and context (255) fit");
    CHECK(enc[0] == 0x12 && enc[1] == 0x34 && enc[2] == 255 && enc[3 + 255] == 255 &&
              memcmp(enc + 3, "tls13 aaa", 9) == 0 && memcmp(enc + 4 + 255, ctx, 255) == 0,
          "layout: length (big-endian), label length, label, context length, context");
    CHECK(tls13_hkdf_label(enc, n, 0x1234, lab, ctx, 255) == n, "a buffer of exactly the size");
    CHECK(tls13_hkdf_label(enc, n - 1, 0x1234, lab, ctx, 255) == 0, "one byte short is refused");
    lab[249] = 'a';
    lab[250] = 0;
    CHECK(tls13_hkdf_label(enc, sizeof enc, 32, lab, ctx, 0) == 0, "a 6+250 byte label is refused");
    CHECK(tls13_hkdf_label(enc, sizeof enc, 32, "x", ctx, 256) == 0,
          "a 256-byte context is refused");
    CHECK(tls13_hkdf_label(0, 64, 32, "x", 0, 0) == 0 &&
              tls13_hkdf_label(enc, 64, 32, 0, 0, 0) == 0,
          "NULL output or label refused");

    uint8_t salt[13], ikm[22], prk[32], out[300];
    pat(salt, 13, 5);
    pat(ikm, 22, 1);
    hkdf_extract(salt, 13, ikm, 22, prk);
    CHECK(tls13_expand_label(prk, "key", 0, 0, out, 300) && is(out + 300 - 16, "label_300"),
          "an output length past 255 sets the high length byte");
    static const char empty_then_junk[4] = {0, 'z', 'z', 'z'};
    CHECK(tls13_hkdf_label(enc, sizeof enc, 32, empty_then_junk, 0, 0) == 10 && enc[2] == 6 &&
              enc[9] == 0,
          "an empty label is just the prefix (the bytes after its NUL are not read)");
    CHECK(tls13_expand_label(prk, "", 0, 0, out, 32) && is(out, "label_empty"), "empty label");
    CHECK(tls13_expand_label(prk, "c hs traffic", ctx, 32, out, 32) && is(out, "label_ctx"),
          "a 32-byte context");
    memset(lab, 'a', 64);
    lab[64] = 0;
    CHECK(tls13_expand_label(prk, lab, ctx, 32, out, 32) && is(out, "label_64"),
          "the longest label (64) with a 32-byte context");
    lab[64] = 'a';
    lab[65] = 0;
    CHECK(!tls13_expand_label(prk, lab, ctx, 32, out, 32), "a 65-byte label does not fit: refused");
    CHECK(!tls13_expand_label(prk, "x", ctx, 300, out, 32), "a context too long: refused");
    CHECK(!tls13_expand_label(prk, "x", 0, 0, big, 255 * 32 + 1), "too much output: refused");
}

static void test_schedule(void)
{
    uint8_t psk[32], dhe[32], t1[200], t2[500];
    pat(psk, 32, 11);
    pat(dhe, 32, 12);
    pat(t1, 200, 13);
    pat(t2, 500, 14);
    tls13_schedule_t s;
    tls13_early_secret(0, psk, 32);
    memset(&s, 0xAB, sizeof s);
    tls13_early_secret(&s, 0, 0);
    CHECK(is(s.early, "early_zero") && !s.have_handshake && !s.have_master,
          "no PSK: the well-known early secret, and the rest cleared");
    tls13_early_secret(&s, psk, 0);
    CHECK(is(s.early, "early_zero"), "a zero-length PSK is no PSK");
    tls13_early_secret(&s, psk, 32);
    CHECK(is(s.early, "early_psk"), "with a PSK");

    CHECK(!tls13_master_secret(&s, t2, 500) && !s.have_master, "no master before the handshake");
    CHECK(!tls13_handshake_secret(0, dhe, 32, t1, 200) &&
              !tls13_handshake_secret(&s, 0, 32, t1, 200) &&
              !tls13_handshake_secret(&s, dhe, 0, t1, 200),
          "the handshake secret needs a schedule and a DHE secret");
    CHECK(!s.have_handshake, "refusals leave no handshake");
    CHECK(tls13_handshake_secret(&s, dhe, 32, t1, 200), "handshake secret");
    CHECK(s.have_handshake && is(s.handshake, "hs") && is(s.c_hs_traffic, "c_hs") &&
              is(s.s_hs_traffic, "s_hs"),
          "handshake, client and server handshake traffic secrets");
    CHECK(!tls13_master_secret(0, t2, 500), "master needs a schedule");
    CHECK(tls13_master_secret(&s, t2, 500), "master secret");
    CHECK(s.have_master && is(s.master, "ms") && is(s.c_ap_traffic, "c_ap") &&
              is(s.s_ap_traffic, "s_ap"),
          "master, client and server application traffic secrets");

    uint8_t key[16], iv[12], fin[32], th[32];
    memset(key, 0, 16);
    CHECK(tls13_traffic_keys(s.s_hs_traffic, key, 16, iv, 12) && is(key, "key16") && is(iv, "iv12"),
          "traffic key and iv");
    memset(iv, 0, 12);
    CHECK(tls13_traffic_keys(s.s_hs_traffic, 0, 16, iv, 12) && is(iv, "iv12"), "iv alone");
    memset(key, 0, 16);
    CHECK(tls13_traffic_keys(s.s_hs_traffic, key, 16, 0, 12) && is(key, "key16"), "key alone");
    CHECK(!tls13_traffic_keys(0, key, 16, iv, 12), "NULL secret refused");
    CHECK(!tls13_traffic_keys(0, 0, 0, 0, 0), "NULL secret refused even with nothing to derive");
    CHECK(!tls13_finished(0, th, fin), "Finished needs a secret");
    CHECK(!tls13_traffic_keys(s.s_hs_traffic, big, 255 * 32 + 1, 0, 0) &&
              !tls13_traffic_keys(s.s_hs_traffic, 0, 0, big, 255 * 32 + 1),
          "an impossible key or iv length is refused");
    sha256(t1, 200, th);
    CHECK(tls13_finished(s.s_hs_traffic, th, fin) && is(fin, "finished"), "Finished MAC");
}

int main(void)
{
    printf("test_hkdf_props: HKDF and the TLS 1.3 schedule at their edges\n");
    test_hmac_extract();
    test_expand();
    test_label();
    test_schedule();
    printf("%u checks, %d failures\n", checks, failures);
    if (failures) {
        printf("[FAIL] test_hkdf_props\n");
        return 1;
    }
    printf("[PASS] test_hkdf_props\n");
    return 0;
}
