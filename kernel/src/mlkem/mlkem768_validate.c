#include <stdio.h>
#include <string.h>
#include "mlkem768.h"

static uint32_t rng_state = 424242;
static uint8_t next_byte(void) {
    rng_state = rng_state * 1103515245u + 12345u;
    return (uint8_t)(rng_state >> 24);
}
static void fill_random(uint8_t *buf, int n) {
    for (int i = 0; i < n; i++) buf[i] = next_byte();
}

int main(void) {
    int failures = 0;
    printf("MLKEM768_EK_BYTES=%d MLKEM768_DK_BYTES=%d MLKEM768_CT_BYTES=%d\n",
           MLKEM768_EK_BYTES, MLKEM768_DK_BYTES, MLKEM768_CT_BYTES);
    printf("(known published ML-KEM-768 sizes: EK=1184 DK=2400 CT=1088 -- %s)\n",
           (MLKEM768_EK_BYTES == 1184 && MLKEM768_DK_BYTES == 2400 && MLKEM768_CT_BYTES == 1088)
               ? "MATCH" : "MISMATCH");

    for (int trial = 0; trial < 10; trial++) {
        uint8_t d[32], z[32], m[32];
        fill_random(d, 32);
        fill_random(z, 32);
        fill_random(m, 32);

        uint8_t ek[MLKEM768_EK_BYTES], dk[MLKEM768_DK_BYTES];
        mlkem768_keygen(d, z, ek, dk);

        uint8_t c[MLKEM768_CT_BYTES], ss_enc[32], ss_dec[32];
        mlkem768_encaps(ek, m, c, ss_enc);
        mlkem768_decaps(dk, c, ss_dec);

        int match = memcmp(ss_enc, ss_dec, 32) == 0;
        printf("%s: trial %d -- Encaps/Decaps shared secret agreement\n", match ? "PASS" : "FAIL", trial);
        if (!match) failures++;
    }

    /* Implicit rejection: tampering with the ciphertext must NOT crash,
     * must NOT recover the original secret, and must be deterministic
     * (same tampered ciphertext -> same rejection secret every time,
     * since it's a pseudorandom function of z and c, not actual noise). */
    {
        uint8_t d[32], z[32], m[32];
        fill_random(d, 32); fill_random(z, 32); fill_random(m, 32);
        uint8_t ek[MLKEM768_EK_BYTES], dk[MLKEM768_DK_BYTES];
        mlkem768_keygen(d, z, ek, dk);

        uint8_t c[MLKEM768_CT_BYTES], ss_enc[32];
        mlkem768_encaps(ek, m, c, ss_enc);

        uint8_t c_tampered[MLKEM768_CT_BYTES];
        memcpy(c_tampered, c, MLKEM768_CT_BYTES);
        c_tampered[0] ^= 0xFF;

        uint8_t ss_rej1[32], ss_rej2[32];
        mlkem768_decaps(dk, c_tampered, ss_rej1);
        mlkem768_decaps(dk, c_tampered, ss_rej2);

        int differs_from_real = memcmp(ss_enc, ss_rej1, 32) != 0;
        int deterministic = memcmp(ss_rej1, ss_rej2, 32) == 0;

        printf("%s: implicit rejection produces a DIFFERENT secret than the real one\n",
               differs_from_real ? "PASS" : "FAIL");
        printf("%s: implicit rejection is deterministic for the same tampered ciphertext\n",
               deterministic ? "PASS" : "FAIL");
        if (!differs_from_real) failures++;
        if (!deterministic) failures++;
    }

    if (failures == 0) printf("\n=== ALL ML-KEM-768 TOP-LEVEL VALIDATION TESTS PASSED ===\n");
    else printf("\n=== %d VALIDATION FAILURE(S) ===\n", failures);
    return failures;
}
