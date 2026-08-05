#include <stdio.h>
#include <string.h>
#include "aes256_gcm.h"
#include "sha256.h"

static void print_hex(const char *label, const uint8_t *buf, size_t len) {
    printf("%s: ", label);
    for (size_t i = 0; i < len; i++) printf("%02x", buf[i]);
    printf("\n");
}

int main(void) {
    int failures = 0;

    /* NIST GCM Test Case 13: all-zero 256-bit key, all-zero 96-bit IV,
     * empty plaintext/AAD -> tag 530f8afbc74536b9a963b4f1c4cb738b */
    {
        uint8_t key[32] = {0};
        uint8_t iv[12] = {0};
        uint8_t tag[16];
        aes256_gcm_encrypt(key, iv, NULL, 0, NULL, 0, NULL, tag);
        uint8_t expected[16] = {
            0x53,0x0f,0x8a,0xfb,0xc7,0x45,0x36,0xb9,
            0xa9,0x63,0xb4,0xf1,0xc4,0xcb,0x73,0x8b
        };
        print_hex("GCM Test13 tag  ", tag, 16);
        print_hex("GCM Test13 expect", expected, 16);
        if (memcmp(tag, expected, 16) != 0) {
            printf("FAIL: GCM Test Case 13\n");
            failures++;
        } else {
            printf("PASS: GCM Test Case 13\n");
        }
    }

    /* Round-trip test: encrypt then decrypt with real (non-zero) data */
    {
        uint8_t key[32];
        for (int i = 0; i < 32; i++) key[i] = (uint8_t)i;
        uint8_t iv[12] = {0,0,0,0,0,0,0,0,0,0,0,1};
        const char *msg = "the quick brown fox jumps over the lazy dog";
        size_t len = strlen(msg);
        uint8_t ct[128], pt[128], tag[16];

        aes256_gcm_encrypt(key, iv, NULL, 0, (const uint8_t*)msg, len, ct, tag);
        int ok = aes256_gcm_decrypt(key, iv, NULL, 0, ct, len, tag, pt);
        pt[len] = 0;
        if (!ok || memcmp(pt, msg, len) != 0) {
            printf("FAIL: GCM round-trip (ok=%d, pt=%s)\n", ok, pt);
            failures++;
        } else {
            printf("PASS: GCM round-trip: \"%s\"\n", pt);
        }

        /* Tamper test: flipping a ciphertext byte must fail authentication */
        ct[0] ^= 0xFF;
        ok = aes256_gcm_decrypt(key, iv, NULL, 0, ct, len, tag, pt);
        if (ok) {
            printf("FAIL: tampered ciphertext was accepted!\n");
            failures++;
        } else {
            printf("PASS: tampered ciphertext correctly rejected\n");
        }
    }

    /* SHA-256 standard test vectors */
    {
        uint8_t digest[32];
        sha256((const uint8_t*)"", 0, digest);
        uint8_t expected_empty[32] = {
            0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,
            0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c,0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55
        };
        print_hex("SHA256('')  ", digest, 32);
        if (memcmp(digest, expected_empty, 32) != 0) {
            printf("FAIL: SHA256('')\n");
            failures++;
        } else {
            printf("PASS: SHA256('')\n");
        }

        sha256((const uint8_t*)"abc", 3, digest);
        uint8_t expected_abc[32] = {
            0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
            0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
        };
        print_hex("SHA256('abc')", digest, 32);
        if (memcmp(digest, expected_abc, 32) != 0) {
            printf("FAIL: SHA256('abc')\n");
            failures++;
        } else {
            printf("PASS: SHA256('abc')\n");
        }
    }

    if (failures == 0) {
        printf("\n=== ALL CRYPTO VALIDATION TESTS PASSED ===\n");
    } else {
        printf("\n=== %d VALIDATION FAILURE(S) ===\n", failures);
    }
    return failures;
}
