#include <stdio.h>
#include <string.h>
#include "mlkem_kpe.h"

static uint32_t rng_state = 999;
static uint8_t next_byte(void) {
    rng_state = rng_state * 1103515245u + 12345u;
    return (uint8_t)(rng_state >> 24);
}

int main(void) {
    int failures = 0;
    printf("KPE_EK_BYTES=%d KPE_DK_BYTES=%d KPE_C1_BYTES=%d KPE_C2_BYTES=%d KPE_CT_BYTES=%d\n",
           KPE_EK_BYTES, KPE_DK_BYTES, KPE_C1_BYTES, KPE_C2_BYTES, KPE_CT_BYTES);

    for (int trial = 0; trial < 10; trial++) {
        uint8_t d[32], m[32], rand_bytes[32];
        for (int i = 0; i < 32; i++) d[i] = next_byte();
        for (int i = 0; i < 32; i++) m[i] = next_byte();
        for (int i = 0; i < 32; i++) rand_bytes[i] = next_byte();

        uint8_t ek[KPE_EK_BYTES], dk[KPE_DK_BYTES];
        kpe_keygen(d, ek, dk);

        uint8_t c[KPE_CT_BYTES];
        kpe_encrypt(ek, m, rand_bytes, c);

        uint8_t recovered[32];
        kpe_decrypt(dk, c, recovered);

        int match = memcmp(m, recovered, 32) == 0;
        if (!match) {
            printf("FAIL: trial %d -- message mismatch\n", trial);
            printf("  original:  ");
            for (int i = 0; i < 32; i++) printf("%02x", m[i]);
            printf("\n  recovered: ");
            for (int i = 0; i < 32; i++) printf("%02x", recovered[i]);
            printf("\n");
            failures++;
        } else {
            printf("PASS: trial %d -- KeyGen/Encrypt/Decrypt round-trip\n", trial);
        }
    }

    if (failures == 0) printf("\n=== ALL K-PKE VALIDATION TESTS PASSED (10/10 trials) ===\n");
    else printf("\n=== %d / 10 TRIAL FAILURE(S) ===\n", failures);
    return failures;
}
