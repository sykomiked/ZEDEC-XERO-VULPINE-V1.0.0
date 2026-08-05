#include <stdio.h>
#include <string.h>
#include "genomic_codon.h"

int main(void) {
    int failures = 0;

    /* Round-trip: every one of the 64 codon indices must encode/decode exactly */
    for (int i = 0; i < 64; i++) {
        codon_t c = codon_from_index((uint8_t)i);
        uint8_t back = codon_to_index(c);
        if (back != i) {
            printf("FAIL: codon round-trip mismatch at index %d -> %d\n", i, back);
            failures++;
        }
    }
    printf("%s: all 64 codon indices round-trip correctly\n", failures ? "FAIL" : "PASS");

    /* Fibonacci dimension ladder: 5, 8, 13, 21, 34, 55, 89, 144 */
    uint32_t expected[8] = {5, 8, 13, 21, 34, 55, 89, 144};
    int fib_fail = 0;
    for (int i = 0; i < 8; i++) {
        uint32_t got = fib_dimension((uint32_t)i);
        if (got != expected[i]) {
            printf("FAIL: fib_dimension(%d) = %u, expected %u\n", i, got, expected[i]);
            fib_fail++;
        }
    }
    if (!fib_fail) printf("PASS: Fibonacci dimension ladder (5,8,13,21,34,55,89,144) matches M5->M8->M13 chain\n");
    failures += fib_fail;

    /* Base frequency table sanity (values as specified, in millihertz) */
    if (genomic_base_freq_mhz(BASE_A) != 545600 ||
        genomic_base_freq_mhz(BASE_C) != 537800 ||
        genomic_base_freq_mhz(BASE_G) != 550000 ||
        genomic_base_freq_mhz(BASE_T) != 543400) {
        printf("FAIL: base frequency table mismatch\n");
        failures++;
    } else {
        printf("PASS: base frequency table matches specification\n");
    }

    /* Domain tag: deterministic (same input -> same output) and
     * dimension-sensitive (different dimension -> different output) */
    codon_t seq[3] = {
        codon_from_index(5), codon_from_index(42), codon_from_index(63)
    };
    uint8_t tag1[32], tag2[32], tag3[32];
    genomic_domain_tag(seq, 3, fib_dimension(2) /* =13 */, tag1, 32);
    genomic_domain_tag(seq, 3, fib_dimension(2) /* =13 */, tag2, 32);
    genomic_domain_tag(seq, 3, fib_dimension(4) /* =34 */, tag3, 32);

    if (memcmp(tag1, tag2, 32) != 0) {
        printf("FAIL: genomic_domain_tag not deterministic\n");
        failures++;
    } else {
        printf("PASS: genomic_domain_tag is deterministic\n");
    }
    if (memcmp(tag1, tag3, 32) == 0) {
        printf("FAIL: genomic_domain_tag insensitive to dimension parameter\n");
        failures++;
    } else {
        printf("PASS: genomic_domain_tag correctly varies with dimension (M13 vs M34)\n");
    }

    if (failures == 0) printf("\n=== ALL GENOMIC CODON VALIDATION TESTS PASSED ===\n");
    else printf("\n=== %d VALIDATION FAILURE(S) ===\n", failures);
    return failures;
}
