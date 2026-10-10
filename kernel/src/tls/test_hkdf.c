/* test_hkdf.c — the TLS 1.3 key schedule against PUBLISHED vectors.
 *
 * Every expected value here was produced by someone else's implementation and
 * published in an RFC. That is the whole point: a key schedule checked only
 * against itself is a key schedule that agrees with no peer on earth.
 *
 *   RFC 4231  HMAC-SHA-256 test cases 1, 2, 6
 *   RFC 5869  HKDF-SHA256 test cases 1 and 3 (case 3 has an EMPTY salt,
 *             which is the case TLS 1.3's first Extract depends on)
 *   RFC 8448  "Example Handshake Traces for TLS 1.3" section 3 — the byte
 *             exact secrets of a real recorded handshake
 */
#include <stdio.h>
#include <string.h>
#include "hkdf.h"

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
/* parse hex, ignoring spaces/newlines */
static uint32_t hx(const char *s, uint8_t *out, uint32_t cap)
{
    uint32_t n = 0;
    int hi = -1;
    for (; *s; s++) {
        int v = hexval(*s);
        if (v < 0) continue;
        if (hi < 0)
            hi = v;
        else {
            if (n < cap) out[n++] = (uint8_t) ((hi << 4) | v);
            hi = -1;
        }
    }
    return n;
}
static bool eqhex(const uint8_t *got, const char *want_hex, uint32_t len)
{
    uint8_t w[512];
    uint32_t n = hx(want_hex, w, sizeof w);
    if (n != len) return false;
    return memcmp(got, w, len) == 0;
}
static void show(const char *tag, const uint8_t *b, uint32_t n)
{
    printf("       %s = ", tag);
    for (uint32_t i = 0; i < n && i < 32; i++) printf("%02x", b[i]);
    printf("%s\n", n > 32 ? "..." : "");
}

int main(void)
{
    printf("=== TLS 1.3 key schedule (RFC 4231 / 5869 / 8448 vectors) ===\n");
    uint8_t buf[256], key[256], data[256], prk[32], okm[128];
    uint32_t kn;

    /* ================= RFC 4231: HMAC-SHA-256 ================= */
    {
        /* Case 1: key = 20 x 0x0b, data = "Hi There" */
        kn = hx("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b", key, sizeof key);
        hmac_sha256(key, kn, (const uint8_t *) "Hi There", 8, buf);
        show("hmac t1", buf, 32);
        CHECK(eqhex(buf,
                    "b0344c61d8db38535ca8afceaf0bf12b"
                    "881dc200c9833da726e9376c2e32cff7",
                    32),
              "RFC 4231 case 1 (20-byte key)");

        /* Case 2: key = "Jefe", data = "what do ya want for nothing?" */
        hmac_sha256((const uint8_t *) "Jefe", 4, (const uint8_t *) "what do ya want for nothing?",
                    28, buf);
        CHECK(eqhex(buf,
                    "5bdcc146bf60754e6a042426089575c7"
                    "5a003f089d2739839dec58b964ec3843",
                    32),
              "RFC 4231 case 2 (short key, text data)");

        /* Case 6: key = 131 x 0xaa — LONGER than the 64-byte block, so it
         * must be hashed down first. This is the branch a naive HMAC skips. */
        for (uint32_t i = 0; i < 131; i++) key[i] = 0xaa;
        hmac_sha256(key, 131,
                    (const uint8_t *) "Test Using Larger Than Block-Size Key - Hash Key First", 54,
                    buf);
        CHECK(eqhex(buf,
                    "60e431591ee0b67f0d8a26aacbf5b77f"
                    "8e0bc6213728c5140546040f0ee37f54",
                    32),
              "RFC 4231 case 6 (131-byte key is hashed down, not truncated)");
    }

    /* ================= RFC 5869: HKDF-SHA256 ================= */
    {
        /* Test case 1 */
        uint8_t salt[64], info[64];
        uint32_t ikmn = hx("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b", data, sizeof data);
        uint32_t sn = hx("000102030405060708090a0b0c", salt, sizeof salt);
        uint32_t in = hx("f0f1f2f3f4f5f6f7f8f9", info, sizeof info);
        hkdf_extract(salt, sn, data, ikmn, prk);
        show("prk tc1", prk, 32);
        CHECK(eqhex(prk,
                    "077709362c2e32df0ddc3f0dc47bba63"
                    "90b6c73bb50f9c3122ec844ad7c2b3e5",
                    32),
              "RFC 5869 case 1: Extract");
        CHECK(hkdf_expand(prk, info, in, okm, 42), "Expand succeeds");
        CHECK(eqhex(okm,
                    "3cb25f25faacd57a90434f64d0362f2a"
                    "2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
                    "34007208d5b887185865",
                    42),
              "RFC 5869 case 1: Expand to 42 bytes (spans two HMAC blocks)");

        /* Test case 3: EMPTY salt and EMPTY info. TLS 1.3's very first
         * Extract passes no salt, so this branch is load-bearing. */
        ikmn = hx("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b", data, sizeof data);
        hkdf_extract(0, 0, data, ikmn, prk);
        CHECK(eqhex(prk,
                    "19ef24a32c717b167f33a91d6f648bdf"
                    "96596776afdb6377ac434c1c293ccb04",
                    32),
              "RFC 5869 case 3: a MISSING salt means HashLen zero bytes");
        CHECK(hkdf_expand(prk, 0, 0, okm, 42), "Expand with empty info");
        CHECK(eqhex(okm,
                    "8da4e775a563c18f715f802a063c5a31"
                    "b8a11f5c5ee1879ec3454e5f3c738d2d"
                    "9d201395faa4b61a96c8",
                    42),
              "RFC 5869 case 3: Expand");
    }

    /* ================= HkdfLabel encoding ================= */
    {
        /* RFC 8446 7.1. The "tls13 " prefix is part of the structure. */
        uint8_t enc[128];
        uint32_t n = tls13_hkdf_label(enc, sizeof enc, 32, "derived", 0, 0);
        show("HkdfLabel", enc, n);
        CHECK(n == 2 + 1 + 6 + 7 + 1, "HkdfLabel for 'derived' with empty context is 17 bytes");
        CHECK(enc[0] == 0 && enc[1] == 32, "length field is the output length");
        CHECK(enc[2] == 13, "label length counts the 'tls13 ' prefix (6+7)");
        CHECK(memcmp(enc + 3, "tls13 derived", 13) == 0,
              "the label is literally 'tls13 derived' — omitting the prefix "
              "produces key material no peer can reproduce");
        CHECK(enc[16] == 0, "empty context is a zero length byte");

        uint8_t ctx[32];
        memset(ctx, 0xAB, sizeof ctx);
        n = tls13_hkdf_label(enc, sizeof enc, 12, "iv", ctx, 32);
        CHECK(enc[2] == 8 && memcmp(enc + 3, "tls13 iv", 8) == 0 && enc[11] == 32 &&
                  enc[12] == 0xAB,
              "a context is length-prefixed and copied verbatim");
    }

    /* ================= RFC 8448 section 3: a REAL handshake =================
     * "Simple 1-RTT Handshake". These are the secrets from a recorded
     * exchange between two independent implementations. If our ladder differs
     * anywhere, one of these will not match. */
    {
        tls13_schedule_t s;

        /* --- early secret (no PSK) --- */
        tls13_early_secret(&s, 0, 0);
        show("early", s.early, 32);
        CHECK(eqhex(s.early,
                    "33ad0a1c607ec03b09e6cd9893680ce2"
                    "10adf300aa1f2660e1b22e10f170f92a",
                    32),
              "RFC 8448: Early Secret (the no-PSK constant)");

        /* --- the 'derived' salt for the next Extract --- */
        uint8_t derived[32];
        CHECK(tls13_derive_secret(s.early, "derived", (const uint8_t *) "", 0, derived),
              "Derive-Secret(early, 'derived', '') computes");
        CHECK(eqhex(derived,
                    "6f2615a108c702c5678f54fc9dbab697"
                    "16c076189c48250cebeac3576c3611ba",
                    32),
              "RFC 8448: the 'derived' secret");

        /* --- handshake secret from the recorded ECDHE shared secret --- */
        uint8_t dhe[32];
        hx("8bd4054fb55b9d63fdfbacf9f04b9f0d"
           "35e6d63f537563efd46272900f89492d",
           dhe, sizeof dhe);
        /* the recorded ClientHello..ServerHello transcript HASH */
        uint8_t th_ch_sh[32];
        hx("860c06edc07858ee8e78f0e7428c58ed"
           "d6b43f2ca3e6e95f02ed063cf0e1cad8",
           th_ch_sh, sizeof th_ch_sh);

        hkdf_extract(derived, 32, dhe, 32, s.handshake);
        show("handshake", s.handshake, 32);
        CHECK(eqhex(s.handshake,
                    "1dc826e93606aa6fdc0aadc12f741b01"
                    "046aa6b99f691ed221a9f0ca043fbeac",
                    32),
              "RFC 8448: Handshake Secret from the real ECDHE output");

        uint8_t c_hs[32], s_hs[32];
        CHECK(tls13_derive_secret_h(s.handshake, "c hs traffic", th_ch_sh, c_hs),
              "client handshake traffic secret computes");
        CHECK(eqhex(c_hs,
                    "b3eddb126e067f35a780b3abf45e2d8f"
                    "3b1a950738f52e9600746a0e27a55a21",
                    32),
              "RFC 8448: client_handshake_traffic_secret");
        CHECK(tls13_derive_secret_h(s.handshake, "s hs traffic", th_ch_sh, s_hs),
              "server handshake traffic secret computes");
        CHECK(eqhex(s_hs,
                    "b67b7d690cc16c4e75e54213cb2d37b4"
                    "e9c912bcded9105d42befd59d391ad38",
                    32),
              "RFC 8448: server_handshake_traffic_secret");

        /* --- the record keys derived from that traffic secret --- */
        uint8_t wkey[16], wiv[12];
        CHECK(tls13_traffic_keys(s_hs, wkey, 16, wiv, 12), "server handshake write key/iv derive");
        show("s hs key", wkey, 16);
        CHECK(eqhex(wkey, "3fce516009c21727d0f2e4e86ee403bc", 16),
              "RFC 8448: server handshake write key");
        CHECK(eqhex(wiv, "5d313eb2671276ee13000b30", 12), "RFC 8448: server handshake write iv");

        CHECK(tls13_traffic_keys(c_hs, wkey, 16, wiv, 12), "client handshake write key/iv derive");
        CHECK(eqhex(wkey, "dbfaa693d1762c5b666af5d950258d01", 16),
              "RFC 8448: client handshake write key");
        CHECK(eqhex(wiv, "5bd3c71b836e0b76bb73265f", 12), "RFC 8448: client handshake write iv");

        /* --- master secret + application traffic secrets --- */
        s.have_handshake = true;
        uint8_t derived2[32];
        CHECK(tls13_derive_secret(s.handshake, "derived", (const uint8_t *) "", 0, derived2),
              "second 'derived' computes");
        uint8_t zeros[32];
        memset(zeros, 0, 32);
        hkdf_extract(derived2, 32, zeros, 32, s.master);
        show("master", s.master, 32);
        CHECK(eqhex(s.master,
                    "18df06843d13a08bf2a449844c5f8a47"
                    "8001bc4d4c627984d5a41da8d0402919",
                    32),
              "RFC 8448: Master Secret");

        /* transcript hash through the server Finished, as printed in the RFC's
         * "derive secret tls13 c ap traffic" block */
        uint8_t th_sf[32];
        hx("9608102a0f1ccc6db6250b7b7e417b1a"
           "000eaada3daae4777a7686c9ff83df13",
           th_sf, sizeof th_sf);
        uint8_t c_ap[32], s_ap[32];
        CHECK(tls13_derive_secret_h(s.master, "c ap traffic", th_sf, c_ap) &&
                  tls13_derive_secret_h(s.master, "s ap traffic", th_sf, s_ap),
              "application traffic secrets compute");
        CHECK(eqhex(c_ap,
                    "9e40646ce79a7f9dc05af8889bce6552"
                    "875afa0b06df0087f792ebb7c17504a5",
                    32),
              "RFC 8448: client_application_traffic_secret_0");
        CHECK(eqhex(s_ap,
                    "a11af9f05531f856ad47116b45a95032"
                    "8204b4f44bfb6b3a4b4f1f3fcb631643",
                    32),
              "RFC 8448: server_application_traffic_secret_0");

        /* --- the HkdfLabel bytes, pinned against the RFC itself ---
         * RFC 8448 prints the `info` passed to HKDF-Expand at each step. That
         * is our HkdfLabel encoding, byte for byte, produced by a different
         * implementation. Matching it means the "tls13 " prefix, both length
         * bytes and the context placement are all right. */
        {
            uint8_t enc[128];
            uint32_t n = tls13_hkdf_label(enc, sizeof enc, 32, "c ap traffic", th_sf, 32);
            CHECK(n == 54, "RFC 8448: the 'c ap traffic' info is 54 octets");
            CHECK(eqhex(enc,
                        "0020"
                        "12"
                        "746c73313320632061702074726166666963"
                        "20"
                        "9608102a0f1ccc6db6250b7b7e417b1a"
                        "000eaada3daae4777a7686c9ff83df13",
                        54),
                  "RFC 8448: our HkdfLabel bytes for 'c ap traffic' match the "
                  "info the RFC's implementation used");

            n = tls13_hkdf_label(enc, sizeof enc, 32, "finished", 0, 0);
            CHECK(n == 18, "RFC 8448: the 'finished' info is 18 octets");
            CHECK(eqhex(enc,
                        "0020"
                        "0e"
                        "746c7331332066696e6973686564"
                        "00",
                        18),
                  "RFC 8448: our HkdfLabel bytes for 'finished' match");
        }

        /* --- the Finished keys, byte-exact from the RFC --- */
        uint8_t fkey[32];
        CHECK(tls13_expand_label(s_hs, "finished", 0, 0, fkey, 32), "server finished_key derives");
        show("s fin key", fkey, 32);
        CHECK(eqhex(fkey,
                    "008d3b66f816ea559f96b537e885c31f"
                    "c068bf492c652f01f288a1d8cdc19fc8",
                    32),
              "RFC 8448: the SERVER's finished_key");
        uint8_t cfkey[32];
        CHECK(tls13_expand_label(c_hs, "finished", 0, 0, cfkey, 32), "client finished_key derives");
        CHECK(eqhex(cfkey,
                    "b80ad01015fb2f0bd65ff7d4da5d6bf8"
                    "3f84821d1f87fdc7d3c75b5a7b42d9c4",
                    32),
              "RFC 8448: the CLIENT's finished_key");

        /* verify_data = HMAC(finished_key, transcript_hash). The RFC does not
         * print the transcript hash that goes into this HMAC, so rather than
         * invent one, the two halves are anchored separately and then shown to
         * compose: the KEY is byte-exact above, the HMAC itself is byte-exact
         * against RFC 4231 at the top of this file, and tls13_finished() is
         * shown here to be exactly their composition. No step is unverified. */
        {
            uint8_t probe[32], viaapi[32], direct[32];
            for (uint32_t i = 0; i < 32; i++) probe[i] = (uint8_t) (i * 7 + 3);
            CHECK(tls13_finished(s_hs, probe, viaapi), "tls13_finished computes");
            hmac_sha256(fkey, 32, probe, 32, direct);
            CHECK(memcmp(viaapi, direct, 32) == 0,
                  "tls13_finished() IS HMAC(the RFC's finished_key, transcript) "
                  "— key and HMAC are each byte-exact against an RFC above");
        }
    }

    /* ================= constant-time compare ================= */
    {
        uint8_t a[32], b[32];
        memset(a, 0x5A, 32);
        memset(b, 0x5A, 32);
        CHECK(ct_equal(a, b, 32), "identical buffers compare equal");
        b[31] ^= 0x01;
        CHECK(!ct_equal(a, b, 32), "a difference in the LAST byte is detected");
        b[31] ^= 0x01;
        b[0] ^= 0x80;
        CHECK(!ct_equal(a, b, 32), "a difference in the FIRST byte is detected");
        CHECK(!ct_equal(0, b, 32) && !ct_equal(a, 0, 32), "null is not equal");
    }

    /* ================= bounds ================= */
    {
        uint8_t big[64];
        CHECK(!tls13_expand_label(big, "x", 0, 0, okm, 0x10000),
              "an absurd output length is refused");
        char longlabel[300];
        memset(longlabel, 'a', sizeof longlabel - 1);
        longlabel[sizeof longlabel - 1] = 0;
        uint8_t enc[64];
        CHECK(tls13_hkdf_label(enc, sizeof enc, 32, longlabel, 0, 0) == 0,
              "a label that overflows the length byte is refused");
        CHECK(tls13_hkdf_label(enc, 4, 32, "derived", 0, 0) == 0,
              "a too-small output buffer is refused");
        /* Expand must produce the same first bytes regardless of how much is
         * asked for — a counter bug shows up here immediately. */
        uint8_t o1[32], o2[64];
        memset(prk, 0x11, 32);
        hkdf_expand(prk, (const uint8_t *) "abc", 3, o1, 32);
        hkdf_expand(prk, (const uint8_t *) "abc", 3, o2, 64);
        CHECK(memcmp(o1, o2, 32) == 0,
              "the first block is identical whether 32 or 64 bytes are asked for");
    }

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
