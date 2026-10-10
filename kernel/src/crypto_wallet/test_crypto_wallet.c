/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_crypto_wallet.c — host test for the 5-key wallet file system.
 *
 * Checks the module's own SHA-256 and HMAC-SHA256 against published vectors
 * (FIPS 180-2 / RFC 4231), that key derivation is deterministic and separates
 * keys, and that integrity checking catches a tampered MAC.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "crypto_wallet.h"

static int g_fail, g_n;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        g_n++;                                                                                     \
        if (c) {                                                                                   \
            printf("[PASS] %s\n", m);                                                              \
        } else {                                                                                   \
            printf("[FAIL] %s\n", m);                                                              \
            g_fail++;                                                                              \
        }                                                                                          \
    } while (0)

static void hex(const char *h, uint8_t *out)
{
    for (size_t i = 0; h[2 * i]; i++) {
        unsigned v;
        sscanf(h + 2 * i, "%2x", &v);
        out[i] = (uint8_t) v;
    }
}

int main(void)
{
    uint8_t d[32], want[32];

    cw_sha256((const uint8_t *) "abc", 3, d);
    hex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", want);
    CHECK(memcmp(d, want, 32) == 0, "SHA-256(\"abc\") matches FIPS 180-2");

    cw_sha256((const uint8_t *) "", 0, d);
    hex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", want);
    CHECK(memcmp(d, want, 32) == 0, "SHA-256(\"\") matches");

    const char *m56 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    cw_sha256((const uint8_t *) m56, (uint32_t) strlen(m56), d);
    hex("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", want);
    CHECK(memcmp(d, want, 32) == 0, "SHA-256 of the 56-byte two-block vector matches");

    uint8_t key[20];
    memset(key, 0x0b, sizeof(key));
    cw_hmac_sha256(key, 20, (const uint8_t *) "Hi There", 8, d);
    hex("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", want);
    CHECK(memcmp(d, want, 32) == 0, "HMAC-SHA256 RFC 4231 case 1");

    cw_hmac_sha256((const uint8_t *) "Jefe", 4, (const uint8_t *) "what do ya want for nothing?",
                   28, d);
    hex("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", want);
    CHECK(memcmp(d, want, 32) == 0, "HMAC-SHA256 RFC 4231 case 2");

    uint8_t bigkey[131];
    memset(bigkey, 0xaa, sizeof(bigkey));
    const char *m6 = "Test Using Larger Than Block-Size Key - Hash Key First";
    cw_hmac_sha256(bigkey, 131, (const uint8_t *) m6, (uint32_t) strlen(m6), d);
    hex("60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54", want);
    CHECK(memcmp(d, want, 32) == 0, "HMAC-SHA256 RFC 4231 case 6 (key longer than a block)");

    /* a long message must not overflow anything (the old staging buffer did) */
    static uint8_t longmsg[4096];
    memset(longmsg, 'x', sizeof(longmsg));
    cw_hmac_sha256(key, 20, longmsg, sizeof(longmsg), d);
    CHECK(1, "HMAC over a 4 KiB message completes");

    crypto_wallet_system_t *sys = calloc(1, sizeof(*sys));
    crypto_wallet_system_t *sys2 = calloc(1, sizeof(*sys2));
    if (!sys || !sys2) return 1;
    uint8_t seed[32];
    for (int i = 0; i < 32; i++) seed[i] = (uint8_t) i;
    cw_system_init(sys, 1, "w");
    cw_system_set_root_seed(sys, seed, 32);
    cw_system_init(sys2, 1, "w");
    cw_system_set_root_seed(sys2, seed, 32);
    uint32_t w = cw_wallet_create(sys, "alice");
    uint32_t w2 = cw_wallet_create(sys2, "alice");
    cw_wallet_t *a = cw_wallet_get(sys, w), *b = cw_wallet_get(sys2, w2);
    CHECK(a && b, "wallets created");
    if (!a || !b) return 1;
    CHECK(memcmp(a->keys[0].key, b->keys[0].key, 32) == 0,
          "key derivation is deterministic for the same seed");
    CHECK(memcmp(a->keys[0].key, a->keys[1].key, 32) != 0, "K1 and K2 differ");
    CHECK(memcmp(a->keys[0].key, a->keys[0].chain_code, 32) != 0, "key and chain code differ");

    const uint8_t payload[] = "payload bytes";
    int32_t fi = cw_file_add(sys, w, CW_FILE_36N9, payload, sizeof(payload), "/a");
    CHECK(fi >= 0, "file added");
    CHECK(cw_verify_integrity(sys, w), "fresh wallet verifies");
    a->files[fi].signature[0] ^= 1;
    CHECK(!cw_verify_integrity(sys, w), "a flipped MAC bit is detected");
    a->files[fi].signature[0] ^= 1;
    CHECK(cw_verify_integrity(sys, w), "restored MAC verifies again");
    CHECK(cw_file_phase_shift(sys, w, (uint32_t) fi, CW_KEY_GLUT_MINUS) == 0 &&
              cw_verify_integrity(sys, w),
          "phase shift re-MACs under the new key and still verifies");

    free(sys);
    free(sys2);
    printf("%d checks, %d failures\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
