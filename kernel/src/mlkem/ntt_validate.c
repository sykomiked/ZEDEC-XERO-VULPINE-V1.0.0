#include <stdio.h>
#include <string.h>
#include "mlkem_ntt.h"

/* Direct, obviously-correct (if slow) schoolbook multiplication in
 * Z_q[X]/(X^256+1): the "negacyclic" ring ML-KEM operates over, where
 * X^256 = -1, so terms that overflow degree 255 wrap around with a
 * sign flip. This is the ground truth poly_ntt+poly_basemul+poly_invntt
 * must reproduce exactly. */
static void schoolbook_mul(const poly_t *a, const poly_t *b, poly_t *out) {
    int32_t acc[512] = {0};
    for (int i = 0; i < MLKEM_N; i++) {
        for (int j = 0; j < MLKEM_N; j++) {
            acc[i + j] += (int32_t)a->coeffs[i] * b->coeffs[j];
        }
    }
    for (int i = 0; i < MLKEM_N; i++) {
        int32_t v = acc[i] - acc[i + MLKEM_N]; /* X^256 = -1 wraparound */
        out->coeffs[i] = mlkem_mod_reduce(v);
    }
}

static int poly_eq(const poly_t *a, const poly_t *b) {
    return memcmp(a->coeffs, b->coeffs, sizeof(a->coeffs)) == 0;
}

/* Simple deterministic PRNG for repeatable test polynomials (not
 * cryptographic -- just needs to exercise many distinct coefficients). */
static uint32_t rng_state = 12345;
static uint16_t next_rand(void) {
    rng_state = rng_state * 1103515245u + 12345u;
    return (uint16_t)((rng_state >> 16) % MLKEM_Q);
}

int main(void) {
    int failures = 0;

    /* Test 1: NTT round-trip on the zero polynomial */
    {
        poly_t p = {0};
        poly_ntt(&p);
        poly_invntt(&p);
        poly_t zero = {0};
        if (!poly_eq(&p, &zero)) { printf("FAIL: round-trip zero poly\n"); failures++; }
        else printf("PASS: round-trip zero poly\n");
    }

    /* Test 2: NTT round-trip on p(X) = 1 (constant polynomial) */
    {
        poly_t p = {0};
        p.coeffs[0] = 1;
        poly_t orig = p;
        poly_ntt(&p);
        poly_invntt(&p);
        if (!poly_eq(&p, &orig)) { printf("FAIL: round-trip constant poly\n"); failures++; }
        else printf("PASS: round-trip constant poly\n");
    }

    /* Test 3: NTT round-trip on p(X) = X (single coefficient at degree 1) */
    {
        poly_t p = {0};
        p.coeffs[1] = 1;
        poly_t orig = p;
        poly_ntt(&p);
        poly_invntt(&p);
        if (!poly_eq(&p, &orig)) { printf("FAIL: round-trip X poly\n"); failures++; }
        else printf("PASS: round-trip X poly\n");
    }

    /* Test 4: NTT round-trip on a fully random-ish polynomial (repeated
     * multiple times with different seeds for confidence) */
    {
        int rt_fail = 0;
        for (int trial = 0; trial < 5; trial++) {
            poly_t p;
            for (int i = 0; i < MLKEM_N; i++) p.coeffs[i] = (int16_t)next_rand();
            poly_t orig = p;
            poly_ntt(&p);
            poly_invntt(&p);
            if (!poly_eq(&p, &orig)) {
                printf("FAIL: round-trip random poly (trial %d)\n", trial);
                rt_fail++;
            }
        }
        if (!rt_fail) printf("PASS: round-trip random polynomials (5 trials)\n");
        failures += rt_fail;
    }

    /* Test 5: THE critical end-to-end test -- NTT-domain multiplication
     * (poly_ntt + poly_basemul + poly_invntt) must exactly match direct
     * schoolbook multiplication mod (X^256+1) in Z_3329[X]. This is
     * what actually gets used throughout ML-KEM (matrix-vector products
     * over the module), so if this fails, nothing downstream can work. */
    {
        int mul_fail = 0;
        for (int trial = 0; trial < 5; trial++) {
            poly_t a, b, expected, via_ntt;
            for (int i = 0; i < MLKEM_N; i++) {
                a.coeffs[i] = (int16_t)next_rand();
                b.coeffs[i] = (int16_t)next_rand();
            }

            schoolbook_mul(&a, &b, &expected);

            poly_t a_ntt = a, b_ntt = b;
            poly_ntt(&a_ntt);
            poly_ntt(&b_ntt);
            poly_basemul(&a_ntt, &b_ntt, &via_ntt);
            poly_invntt(&via_ntt);

            if (!poly_eq(&expected, &via_ntt)) {
                printf("FAIL: NTT-domain multiply != schoolbook (trial %d)\n", trial);
                printf("  expected[0..7]: ");
                for (int i = 0; i < 8; i++) printf("%d ", expected.coeffs[i]);
                printf("\n  got[0..7]:      ");
                for (int i = 0; i < 8; i++) printf("%d ", via_ntt.coeffs[i]);
                printf("\n");
                mul_fail++;
            }
        }
        if (!mul_fail) printf("PASS: NTT-domain multiplication matches schoolbook multiplication (5 trials)\n");
        failures += mul_fail;
    }

    /* Test 6: poly_add / poly_sub sanity */
    {
        poly_t a = {0}, b = {0}, sum, diff;
        a.coeffs[0] = 3328; b.coeffs[0] = 5; /* wraps mod 3329 */
        poly_add(&a, &b, &sum);
        poly_sub(&sum, &b, &diff);
        if (sum.coeffs[0] != 4 || diff.coeffs[0] != 3328) {
            printf("FAIL: poly_add/poly_sub wraparound (sum=%d diff=%d)\n", sum.coeffs[0], diff.coeffs[0]);
            failures++;
        } else {
            printf("PASS: poly_add/poly_sub wraparound\n");
        }
    }

    if (failures == 0) printf("\n=== ALL ML-KEM NTT VALIDATION TESTS PASSED ===\n");
    else printf("\n=== %d VALIDATION FAILURE(S) ===\n", failures);
    return failures;
}
