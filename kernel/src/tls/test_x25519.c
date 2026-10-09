/* test_x25519.c — Curve25519 against RFC 7748's published vectors.
 *
 * A wrong scalar multiplication produces output that looks exactly like a
 * correct one: 32 random-looking bytes, no error, no failed check. The only
 * way to know it is right is to reproduce values someone else published.
 *
 *   RFC 7748 section 5.2  two single scalar-mult vectors
 *   RFC 7748 section 6.1  the Diffie-Hellman example (Alice/Bob), which is
 *                         the exact shape TLS uses
 *   RFC 7748 section 5.2  the ITERATED test — 1 and 1000 rounds. This one
 *                         catches carry and reduction bugs that a single
 *                         multiplication happens to survive.
 */
#include <stdio.h>
#include <string.h>
#include "x25519.h"

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static void hx(const char *s, uint8_t *out, uint32_t cap) {
    uint32_t n = 0; int hi = -1;
    for (; *s; s++) {
        int v = hexval(*s);
        if (v < 0) continue;
        if (hi < 0) hi = v; else { if (n < cap) out[n++] = (uint8_t)((hi<<4)|v); hi = -1; }
    }
}
static bool eqhex(const uint8_t *got, const char *want) {
    uint8_t w[32];
    hx(want, w, 32);
    return memcmp(got, w, 32) == 0;
}
static void show(const char *t, const uint8_t *b) {
    printf("       %s = ", t);
    for (int i = 0; i < 32; i++) printf("%02x", b[i]);
    printf("\n");
}

int main(void) {
    printf("=== X25519 (RFC 7748 published vectors) ===\n");
    uint8_t k[32], u[32], out[32];

    /* ---- RFC 7748 5.2, vector 1 ---- */
    hx("a546e36bf0527c9d3b16154b82465edd"
       "62144c0ac1fc5a18506a2244ba449ac4", k, 32);
    hx("e6db6867583030db3594c1a424b15f7c"
       "726624ec26b3353b10a903a6d0ab1c4c", u, 32);
    x25519(out, k, u);
    show("vec1", out);
    CHECK(eqhex(out, "c3da55379de9c6908e94ea4df28d084f"
                     "32eccf03491c71f754b4075577a28552"),
          "RFC 7748 5.2 vector 1");

    /* ---- RFC 7748 5.2, vector 2 ---- */
    hx("4b66e9d4d1b4673c5ad22691957d6af5"
       "c11b6421e0ea01d42ca4169e7918ba0d", k, 32);
    hx("e5210f12786811d3f4b7959d0538ae2c"
       "31dbe7106fc03c3efc4cd549c715a493", u, 32);
    x25519(out, k, u);
    CHECK(eqhex(out, "95cbde9476e8907d7aade45cb4b873f8"
                     "8b595a68799fa152e6f8f7647aac7957"),
          "RFC 7748 5.2 vector 2 (a u-coordinate with the high bit set, "
          "which must be masked off)");

    /* ---- RFC 7748 6.1: the Diffie-Hellman example ----
     * This is exactly the shape TLS 1.3 uses: each side derives a public key
     * from a private scalar, and both must arrive at the same secret. */
    {
        uint8_t apriv[32], apub[32], bpriv[32], bpub[32], as[32], bs[32];
        hx("77076d0a7318a57d3c16c17251b26645"
           "df4c2f87ebc0992ab177fba51db92c2a", apriv, 32);
        hx("5dab087e624a8a4b79e17f8b83800ee6"
           "6f3bb1292618b6fd1c2f8b27ff88e0eb", bpriv, 32);

        x25519_public(apub, apriv);
        show("A pub", apub);
        CHECK(eqhex(apub, "8520f0098930a754748b7ddcb43ef75a"
                          "0dbf3a0d26381af4eba4a98eaa9b4e6a"),
              "RFC 7748 6.1: Alice's public key from her private scalar");

        x25519_public(bpub, bpriv);
        CHECK(eqhex(bpub, "de9edb7d7b7dc1b4d35b61c2ece43537"
                          "3f8343c85b78674dadfc7e146f882b4f"),
              "RFC 7748 6.1: Bob's public key");

        CHECK(x25519_shared(as, apriv, bpub), "Alice computes a shared secret");
        CHECK(x25519_shared(bs, bpriv, apub), "Bob computes a shared secret");
        show("shared", as);
        CHECK(eqhex(as, "4a5d9d5ba4ce2de1728e3bf480350f25"
                        "e07e21c947d19e3376f09b3c1e161742"),
              "RFC 7748 6.1: the published shared secret");
        CHECK(memcmp(as, bs, 32) == 0,
              "BOTH SIDES agree — which is the only property that matters");
    }

    /* ---- RFC 7748 5.2 iterated test ----
     * k = u = basepoint; each round: (k, u) = (k*u, k). The 1-round and
     * 1000-round results are published. Carry and reduction bugs that a
     * single multiply survives do not survive a thousand. */
    {
        uint8_t kk[32], uu[32], r[32];
        memset(kk, 0, 32); kk[0] = 9;
        memset(uu, 0, 32); uu[0] = 9;

        x25519(r, kk, uu);
        CHECK(eqhex(r, "422c8e7a6227d7bca1350b3e2bb7279f"
                       "7897b87bb6854b783c60e80311ae3079"),
              "RFC 7748 iterated: after 1 round");

        /* continue to 1000 */
        memcpy(uu, kk, 32); memcpy(kk, r, 32);
        for (int i = 1; i < 1000; i++) {
            x25519(r, kk, uu);
            memcpy(uu, kk, 32);
            memcpy(kk, r, 32);
        }
        show("iter1k", kk);
        CHECK(eqhex(kk, "684cf59ba83309552800ef566f2f4d3c"
                        "1c3887c49360e3875f2eb94d99532c51"),
              "RFC 7748 iterated: after 1000 rounds (catches carry/reduction "
              "bugs a single multiplication survives)");
    }

    /* ---- clamping is applied internally ---- */
    {
        uint8_t s1[32], s2[32], o1[32], o2[32], base[32];
        memset(base, 0, 32); base[0] = 9;
        for (int i = 0; i < 32; i++) s1[i] = (uint8_t)(i + 1);
        memcpy(s2, s1, 32);
        /* flip only the bits RFC 7748 says to clamp away */
        s2[0]  |= 7;          /* low 3 bits are cleared   */
        s2[31] &= 127;        /* bit 255 is cleared       */
        s2[31] |= 64;         /* bit 254 is set           */
        x25519(o1, s1, base);
        x25519(o2, s2, base);
        CHECK(memcmp(o1, o2, 32) == 0,
              "the clamped bits do not affect the result — clamping happens "
              "inside x25519(), so a caller cannot forget it");
    }

    /* ---- low-order points must be REJECTED, not silently used ---- */
    {
        uint8_t priv[32], sh[32];
        for (int i = 0; i < 32; i++) priv[i] = (uint8_t)(0x40 + i);

        uint8_t zero_pt[32]; memset(zero_pt, 0, 32);
        CHECK(!x25519_shared(sh, priv, zero_pt),
              "u = 0 is refused (it forces an all-zero shared secret)");

        uint8_t one_pt[32]; memset(one_pt, 0, 32); one_pt[0] = 1;
        CHECK(!x25519_shared(sh, priv, one_pt), "u = 1 is refused");

        /* the order-8 points from RFC 7748 section 6.1's security note */
        uint8_t lo1[32];
        hx("e0eb7a7c3b41b8ae1656e3faf19fc46a"
           "da098deb9c32b1fd866205165f49b800", lo1, 32);
        CHECK(!x25519_shared(sh, priv, lo1), "an order-8 point is refused");

        uint8_t lo2[32];
        hx("5f9c95bca3508c24b1d0b1559c83ef5b"
           "04445cc4581c8e86d8224eddd09f1157", lo2, 32);
        CHECK(!x25519_shared(sh, priv, lo2), "the other order-8 point is refused");

        /* ...but a legitimate peer key is accepted */
        uint8_t good[32];
        hx("de9edb7d7b7dc1b4d35b61c2ece43537"
           "3f8343c85b78674dadfc7e146f882b4f", good, 32);
        CHECK(x25519_shared(sh, priv, good), "a legitimate public key is accepted");
    }

    /* ---- a wrong private key gives a different secret ---- */
    {
        uint8_t p1[32], p2[32], pub[32], s1[32], s2[32];
        for (int i = 0; i < 32; i++) { p1[i] = (uint8_t)(i * 3 + 1); p2[i] = p1[i]; }
        p2[5] ^= 0x10;
        hx("de9edb7d7b7dc1b4d35b61c2ece43537"
           "3f8343c85b78674dadfc7e146f882b4f", pub, 32);
        x25519_shared(s1, p1, pub);
        x25519_shared(s2, p2, pub);
        CHECK(memcmp(s1, s2, 32) != 0,
              "a one-bit change in the private scalar changes the secret");
    }

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
