/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sha256.h"
#include "hkdf.h"
#include "x25519.h"
#include "aead.h"
static unsigned checks, failures;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(x)) {                                                                                \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)
static int digit(char c)
{
    return c <= '9' ? c - '0' : c - 'a' + 10;
}
static void hex(uint8_t *out, const char *s)
{
    for (unsigned i = 0; s[2 * i]; i++) out[i] = (digit(s[2 * i]) << 4) | digit(s[2 * i + 1]);
}
static void matches(const uint8_t *out, const char *s, size_t n)
{
    uint8_t expected[128];
    hex(expected, s);
    CHECK(memcmp(out, expected, n) == 0);
}
int main(void)
{
    uint8_t out[128], key[32], nonce[12], prk[32];
    /* FIPS 180-4 SHA-256: empty and abc. */
    sha256((const uint8_t *) "", 0, out);
    matches(out, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", 32);
    sha256((const uint8_t *) "abc", 3, out);
    matches(out, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 32);
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *) "a", 1);
    sha256_update(&ctx, (const uint8_t *) "bc", 2);
    sha256_final(&ctx, out);
    matches(out, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 32);
    /* RFC 4231 case 1. */
    memset(key, 0x0b, 20);
    hmac_sha256(key, 20, (const uint8_t *) "Hi There", 8, out);
    matches(out, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", 32);
    /* RFC 5869 case 1. */
    uint8_t ikm[22], salt[13], info[10];
    memset(ikm, 0x0b, sizeof ikm);
    for (unsigned i = 0; i < 13; i++) salt[i] = i;
    for (unsigned i = 0; i < 10; i++) info[i] = 0xf0 + i;
    hkdf_extract(salt, 13, ikm, 22, prk);
    matches(prk, "077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5", 32);
    CHECK(hkdf_expand(prk, info, 10, out, 42));
    matches(out,
            "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865",
            42);
    CHECK(!hkdf_expand(prk, info, 10, out, 8161));
    /* RFC 7748 section 6.1 Alice public key. */
    hex(key, "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    x25519_public(out, key);
    matches(out, "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", 32);
    uint8_t zero[32] = {0};
    CHECK(!x25519_shared(out, key, zero));
    /* RFC 8439 section 2.3.2 ChaCha20 block. */
    for (unsigned i = 0; i < 32; i++) key[i] = i;
    hex(nonce, "000000090000004a00000000");
    chacha20_block(key, 1, nonce, out);
    matches(out,
            "10f1e7e4d13b5915500fdd1fa32071c4c7d1f4c733c068030422aa9ac3d46c4ed2826446079faa0914c2d7"
            "05d98b02a2b5129cd1de164eb9cbd083e8a2503c4e",
            64);
    /* RFC 8439 section 2.5.2 Poly1305. */
    hex(key, "85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b");
    poly1305(key, (const uint8_t *) "Cryptographic Forum Research Group", 34, out);
    matches(out, "a8061dc1305136c6c22b8baf0c0127a9", 16);
    printf("crypto KATs: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
