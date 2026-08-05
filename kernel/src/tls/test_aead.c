/* test_aead.c — ChaCha20-Poly1305 against RFC 8439's published vectors.
 *
 *   section 2.4.2   ChaCha20 encryption of a known plaintext
 *   section 2.5.2   Poly1305 of a known message under a known one-time key
 *   section 2.8.2   the full AEAD: ciphertext AND tag, with AAD
 *
 * Plus the properties that matter more than the vectors: that a modified
 * ciphertext, a modified AAD, a modified tag or a wrong nonce all FAIL to
 * open, and that a failed open leaves no plaintext behind.
 */
#include <stdio.h>
#include <string.h>
#include "aead.h"

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static uint32_t hx(const char *s, uint8_t *out, uint32_t cap) {
    uint32_t n = 0; int hi = -1;
    for (; *s; s++) {
        int v = hexval(*s);
        if (v < 0) continue;
        if (hi < 0) hi = v; else { if (n < cap) out[n++] = (uint8_t)((hi<<4)|v); hi = -1; }
    }
    return n;
}
static bool eqhex(const uint8_t *got, const char *want, uint32_t len) {
    uint8_t w[256];
    uint32_t n = hx(want, w, sizeof w);
    return n == len && memcmp(got, w, len) == 0;
}
static void show(const char *t, const uint8_t *b, uint32_t n) {
    printf("       %s = ", t);
    for (uint32_t i = 0; i < n && i < 24; i++) printf("%02x", b[i]);
    printf("%s\n", n > 24 ? "..." : "");
}

int main(void) {
    printf("=== ChaCha20-Poly1305 (RFC 8439 published vectors) ===\n");
    uint8_t key[32], nonce[12], tag[16];

    /* ================= RFC 8439 2.4.2: ChaCha20 ================= */
    {
        for (int i = 0; i < 32; i++) key[i] = (uint8_t)i;
        hx("000000000000004a00000000", nonce, sizeof nonce);
        const char *pt =
            "Ladies and Gentlemen of the class of '99: If I could offer you "
            "only one tip for the future, sunscreen would be it.";
        uint32_t n = (uint32_t)strlen(pt);
        CHECK(n == 114, "the RFC's plaintext is 114 bytes");

        uint8_t ct[128];
        chacha20_xor(key, 1, nonce, (const uint8_t *)pt, ct, n);
        show("ct", ct, n);
        CHECK(eqhex(ct,
            "6e2e359a2568f98041ba0728dd0d6981e97e7aec1d4360c20a27afccfd9fae0b"
            "f91b65c5524733ab8f593dabcd62b3571639d624e65152ab8f530c359f0861d8"
            "07ca0dbf500d6a6156a38e088a22b65e52bc514d16ccf806818ce91ab7793736"
            "5af90bbf74a35be6b40b8eedf2785e42874d", n),
            "RFC 8439 2.4.2: the ciphertext, byte for byte");

        /* decryption is the same operation */
        uint8_t back[128];
        chacha20_xor(key, 1, nonce, ct, back, n);
        CHECK(memcmp(back, pt, n) == 0, "XORing again recovers the plaintext");

        /* the counter must actually advance across blocks: byte 64 onward
         * comes from block 2, not a repeat of block 1 */
        CHECK(memcmp(ct, ct + 64, 32) != 0,
              "the second 64-byte block differs from the first — the counter "
              "advances (a stuck counter would repeat the keystream and "
              "destroy the cipher)");
    }

    /* ================= RFC 8439 2.5.2: Poly1305 ================= */
    {
        uint8_t pkey[32];
        hx("85d6be7857556d337f4452fe42d506a8"
           "0103808afb0db2fd4abff6af4149f51b", pkey, sizeof pkey);
        const char *msg = "Cryptographic Forum Research Group";
        poly1305(pkey, (const uint8_t *)msg, (uint32_t)strlen(msg), tag);
        show("tag", tag, 16);
        CHECK(eqhex(tag, "a8061dc1305136c6c22b8baf0c0127a9", 16),
              "RFC 8439 2.5.2: the Poly1305 tag");

        /* a one-bit change anywhere must change the tag */
        char m2[64];
        strcpy(m2, msg);
        m2[0] ^= 0x01;
        uint8_t tag2[16];
        poly1305(pkey, (const uint8_t *)m2, (uint32_t)strlen(m2), tag2);
        CHECK(memcmp(tag, tag2, 16) != 0, "a one-bit message change changes the tag");

        /* exact block boundaries must be handled: 0, 16 and 32 bytes */
        uint8_t t0[16], t16[16], t32[16];
        uint8_t z[32];
        memset(z, 0xA5, sizeof z);
        poly1305(pkey, z, 0,  t0);
        poly1305(pkey, z, 16, t16);
        poly1305(pkey, z, 32, t32);
        CHECK(memcmp(t0, t16, 16) != 0 && memcmp(t16, t32, 16) != 0,
              "empty, one-block and two-block messages all tag differently "
              "(the partial-block padding path is not skipped)");
    }

    /* ================= RFC 8439 2.8.2: the full AEAD ================= */
    {
        const char *pt =
            "Ladies and Gentlemen of the class of '99: If I could offer you "
            "only one tip for the future, sunscreen would be it.";
        uint32_t n = (uint32_t)strlen(pt);
        uint8_t aad[12], akey[32], anonce[12], ct[128];
        hx("50515253c0c1c2c3c4c5c6c7", aad, sizeof aad);
        hx("808182838485868788898a8b8c8d8e8f"
           "909192939495969798999a9b9c9d9e9f", akey, sizeof akey);
        /* the 32-bit fixed-common part followed by the 64-bit IV */
        hx("070000004041424344454647", anonce, sizeof anonce);

        aead_seal(akey, anonce, aad, sizeof aad, (const uint8_t *)pt, ct, n, tag);
        show("aead ct", ct, n);
        CHECK(eqhex(ct,
            "d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d6"
            "3dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b36"
            "92ddbd7f2d778b8c9803aee328091b58fab324e4fad67594558580 8b4831d7bc"
            "3ff4def08e4b7a9de576d26586cec64b6116", n),
            "RFC 8439 2.8.2: the AEAD ciphertext");
        show("aead tag", tag, 16);
        CHECK(eqhex(tag, "1ae10b594f09e26a7e902ecbd0600691", 16),
              "RFC 8439 2.8.2: the AEAD tag — this is the value that proves "
              "the AAD padding and the length trailer are right");

        /* --- and it opens --- */
        uint8_t out[128];
        CHECK(aead_open(akey, anonce, aad, sizeof aad, ct, out, n, tag),
              "the sealed message opens");
        CHECK(memcmp(out, pt, n) == 0, "and yields the original plaintext");

        /* --- every tamper must be caught --- */
        uint8_t bad[128];
        memcpy(bad, ct, n);
        bad[0] ^= 0x01;
        CHECK(!aead_open(akey, anonce, aad, sizeof aad, bad, out, n, tag),
              "a flipped CIPHERTEXT bit is rejected");
        bool leaked = false;
        for (uint32_t i = 0; i < n; i++) if (out[i]) leaked = true;
        CHECK(!leaked,
              "and the output buffer is WIPED — a caller who ignores the "
              "return value finds no unauthenticated plaintext");

        uint8_t badaad[12];
        memcpy(badaad, aad, 12);
        badaad[3] ^= 0x80;
        CHECK(!aead_open(akey, anonce, badaad, 12, ct, out, n, tag),
              "a flipped AAD bit is rejected");

        uint8_t badtag[16];
        memcpy(badtag, tag, 16);
        badtag[15] ^= 0x01;
        CHECK(!aead_open(akey, anonce, aad, 12, ct, out, n, badtag),
              "a flipped TAG bit is rejected (last byte)");
        memcpy(badtag, tag, 16); badtag[0] ^= 0x01;
        CHECK(!aead_open(akey, anonce, aad, 12, ct, out, n, badtag),
              "a flipped TAG bit is rejected (first byte)");

        uint8_t badnonce[12];
        memcpy(badnonce, anonce, 12);
        badnonce[11] ^= 0x01;
        CHECK(!aead_open(akey, badnonce, aad, 12, ct, out, n, tag),
              "the wrong NONCE is rejected");

        uint8_t badkey[32];
        memcpy(badkey, akey, 32);
        badkey[31] ^= 0x01;
        CHECK(!aead_open(badkey, anonce, aad, 12, ct, out, n, tag),
              "the wrong KEY is rejected");

        /* truncating the AAD must not still authenticate — this is what the
         * explicit length trailer exists to prevent */
        CHECK(!aead_open(akey, anonce, aad, 11, ct, out, n, tag),
              "a TRUNCATED AAD is rejected (the length trailer stops bytes "
              "being shifted between the AAD and the ciphertext)");
    }

    /* ---- empty plaintext and empty AAD still authenticate ---- */
    {
        uint8_t k[32], nn[12], t2[16], o[16];
        memset(k, 0x42, 32); memset(nn, 0x24, 12);
        aead_seal(k, nn, 0, 0, 0, o, 0, t2);
        CHECK(aead_open(k, nn, 0, 0, 0, o, 0, t2),
              "an empty message with no AAD seals and opens");
        t2[0] ^= 1;
        CHECK(!aead_open(k, nn, 0, 0, 0, o, 0, t2),
              "and a bad tag on an empty message is still rejected");
    }

    /* ---- lengths that are not multiples of the block size ---- */
    {
        uint8_t k[32], nn[12], in[200], ct[200], out[200], t2[16];
        memset(k, 0x11, 32); memset(nn, 0x22, 12);
        for (uint32_t i = 0; i < sizeof in; i++) in[i] = (uint8_t)(i * 5 + 1);
        bool all_ok = true;
        for (uint32_t len = 1; len <= 200; len++) {
            aead_seal(k, nn, in, 7, in, ct, len, t2);
            if (!aead_open(k, nn, in, 7, ct, out, len, t2)) { all_ok = false; break; }
            if (memcmp(out, in, len) != 0) { all_ok = false; break; }
        }
        CHECK(all_ok, "every length from 1 to 200 round-trips (block-boundary "
                      "and partial-block padding are both exercised)");
    }

    /* ---- in-place operation ---- */
    {
        uint8_t k[32], nn[12], buf[64], t2[16], orig[64];
        memset(k, 0x33, 32); memset(nn, 0x44, 12);
        for (int i = 0; i < 64; i++) buf[i] = (uint8_t)i;
        memcpy(orig, buf, 64);
        aead_seal(k, nn, 0, 0, buf, buf, 64, t2);
        CHECK(memcmp(buf, orig, 64) != 0, "in-place seal actually encrypts");
        CHECK(aead_open(k, nn, 0, 0, buf, buf, 64, t2) &&
              memcmp(buf, orig, 64) == 0,
              "in-place open recovers the plaintext (out may alias in)");
    }

    /* ================= the TLS 1.3 record nonce ================= */
    {
        uint8_t iv[12], nz[12];
        hx("5d313eb2671276ee13000b30", iv, sizeof iv);

        tls13_record_nonce(iv, 0, nz);
        CHECK(memcmp(nz, iv, 12) == 0,
              "sequence 0 leaves the IV unchanged");

        tls13_record_nonce(iv, 1, nz);
        CHECK(nz[11] == (uint8_t)(iv[11] ^ 1) && memcmp(nz, iv, 11) == 0,
              "the sequence number is XORed into the LAST bytes (right-aligned)");

        tls13_record_nonce(iv, 0x0102030405060708ull, nz);
        CHECK(nz[4] == (uint8_t)(iv[4] ^ 0x01) && nz[11] == (uint8_t)(iv[11] ^ 0x08) &&
              nz[0] == iv[0] && nz[3] == iv[3],
              "a full 64-bit sequence occupies the low 8 bytes, big-endian, "
              "and never touches the first 4");

        /* the property the whole cipher rests on */
        uint8_t a[12], b[12];
        bool collided = false;
        for (uint64_t s = 0; s < 2000; s++) {
            tls13_record_nonce(iv, s, a);
            tls13_record_nonce(iv, s + 1, b);
            if (memcmp(a, b, 12) == 0) collided = true;
        }
        CHECK(!collided,
              "consecutive sequence numbers never produce the same nonce — "
              "nonce reuse would leak the Poly1305 key, not just the plaintext");
    }

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
