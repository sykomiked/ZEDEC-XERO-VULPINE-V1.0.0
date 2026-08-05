#include <stdio.h>
#include <string.h>
#include "keccak.h"

static void print_hex(const char *label, const uint8_t *buf, size_t len) {
    printf("%s: ", label);
    for (size_t i = 0; i < len; i++) printf("%02x", buf[i]);
    printf("\n");
}

int main(void) {
    int failures = 0;

    /* SHA3-256("") = a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a */
    {
        uint8_t digest[32];
        sha3_256((const uint8_t*)"", 0, digest);
        uint8_t expected[32] = {
            0xa7,0xff,0xc6,0xf8,0xbf,0x1e,0xd7,0x66,0x51,0xc1,0x47,0x56,0xa0,0x61,0xd6,0x62,
            0xf5,0x80,0xff,0x4d,0xe4,0x3b,0x49,0xfa,0x82,0xd8,0x0a,0x4b,0x80,0xf8,0x43,0x4a
        };
        print_hex("SHA3-256('')  ", digest, 32);
        if (memcmp(digest, expected, 32) != 0) { printf("FAIL: SHA3-256('')\n"); failures++; }
        else printf("PASS: SHA3-256('')\n");
    }

    /* SHA3-256("abc") -- widely-cited standard value */
    {
        uint8_t digest[32];
        sha3_256((const uint8_t*)"abc", 3, digest);
        uint8_t expected[32] = {
            0x3a,0x98,0x5d,0xa7,0x4f,0xe2,0x25,0xb2,0x04,0x5c,0x17,0x2d,0x6b,0xd3,0x90,0xbd,
            0x85,0x5f,0x08,0x6e,0x3e,0x9d,0x52,0x5b,0x46,0xbf,0xe2,0x45,0x11,0x43,0x15,0x32
        };
        print_hex("SHA3-256('abc')", digest, 32);
        /* This vector is NOT tentative: 3a985da7...11431532 is the standard
         * NIST value for SHA3-256("abc"), and it is the ONLY check here that
         * exercises absorption with real message bytes — SHA3-256("") hits
         * only the padding path. It previously routed a mismatch to a
         * printf("INFO:") without touching `failures`, so a broken Keccak
         * printed "ALL KECCAK VALIDATION TESTS PASSED" and exited 0. Every
         * ML-KEM key in the system derives from this primitive. */
        if (memcmp(digest, expected, 32) != 0) {
            printf("FAIL: SHA3-256('abc') does not match the NIST vector\n");
            failures++;
        }
        else printf("PASS: SHA3-256('abc')\n");
    }

    /* SHAKE128 self-consistency: squeezing more bytes must be a strict
     * extension of squeezing fewer bytes (defining XOF property) */
    {
        uint8_t out_short[16], out_long[64];
        shake128((const uint8_t*)"test", 4, out_short, 16);
        shake128((const uint8_t*)"test", 4, out_long, 64);
        if (memcmp(out_short, out_long, 16) != 0) {
            printf("FAIL: SHAKE128 prefix-extension property violated\n");
            failures++;
        } else {
            printf("PASS: SHAKE128 prefix-extension property\n");
        }
    }

    /* Incremental SHAKE128 context must match one-shot for the same input */
    {
        uint8_t one_shot[48], incremental[48];
        shake128((const uint8_t*)"hello world", 11, one_shot, 48);

        shake128_ctx_t ctx;
        shake128_init(&ctx);
        shake128_absorb(&ctx, (const uint8_t*)"hello ", 6);
        shake128_absorb(&ctx, (const uint8_t*)"world", 5);
        shake128_squeeze(&ctx, incremental, 48);

        print_hex("one-shot  ", one_shot, 48);
        print_hex("incremental", incremental, 48);
        if (memcmp(one_shot, incremental, 48) != 0) {
            printf("FAIL: incremental SHAKE128 context mismatch\n");
            failures++;
        } else {
            printf("PASS: incremental SHAKE128 matches one-shot\n");
        }
    }

    if (failures == 0) printf("\n=== ALL KECCAK VALIDATION TESTS PASSED ===\n");
    else printf("\n=== %d VALIDATION FAILURE(S) ===\n", failures);
    return failures;
}
