#include <stdio.h>
#include <string.h>
#include "mlkem_sample.h"

int main(void) {
    int failures = 0;
    uint8_t rho[32];
    for (int i = 0; i < 32; i++) rho[i] = (uint8_t)(i * 7 + 3);

    poly_t a, b, c;
    sample_ntt(rho, 0, 0, &a);
    sample_ntt(rho, 0, 0, &b);
    sample_ntt(rho, 0, 1, &c);

    int in_range = 1;
    for (int i = 0; i < MLKEM_N; i++) {
        if (a.coeffs[i] < 0 || a.coeffs[i] >= MLKEM_Q) in_range = 0;
    }
    printf("%s: sample_ntt output always in [0, Q)\n", in_range ? "PASS" : "FAIL");
    if (!in_range) failures++;

    int deterministic = memcmp(a.coeffs, b.coeffs, sizeof(a.coeffs)) == 0;
    printf("%s: sample_ntt deterministic for same (rho,i,j)\n", deterministic ? "PASS" : "FAIL");
    if (!deterministic) failures++;

    int differs = memcmp(a.coeffs, c.coeffs, sizeof(a.coeffs)) != 0;
    printf("%s: sample_ntt differs for different j\n", differs ? "PASS" : "FAIL");
    if (!differs) failures++;

    if (failures == 0) printf("\n=== ALL SAMPLE_NTT VALIDATION TESTS PASSED ===\n");
    else printf("\n=== %d VALIDATION FAILURE(S) ===\n", failures);
    return failures;
}
