/* test_crit_168_word.c — Tests for CRIT-168 transform
 * Verifies that crit_transform produces nonzero imaginary components via cexp(2*pi*I*n/N).
 */
#include "crit_168_word.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <math.h>

int main(void) {
    word168_t w;
    memset(&w, 0, sizeof(w));
    for (int i = 0; i < WORD168_OCTETS; i++) {
        w.bytes[i] = (uint8_t)(i + 1);
    }

    uint8_t octets[WORD168_OCTETS];
    word168_to_octets(&w, octets);
    assert(memcmp(octets, w.bytes, WORD168_OCTETS) == 0);

    word168_t w2;
    octets_to_word168(octets, &w2);
    assert(memcmp(&w2, &w, sizeof(word168_t)) == 0);

    word168_t w3 = w;
    word168_alternate_endianness(&w3);
    for (int i = 0; i < WORD168_OCTETS / 2; i++) {
        assert(w3.bytes[i] == w.bytes[WORD168_OCTETS - 1 - i]);
    }

    double complex freq[WORD168_OCTETS];
    crit_transform(&w, freq, WORD168_OCTETS);

    assert(cimag(freq[1]) != 0.0);

    word168_t w4;
    crit_inverse(freq, &w4, WORD168_OCTETS);
    for (int i = 0; i < WORD168_OCTETS; i++) {
        assert(abs((int)w4.bytes[i] - (int)w.bytes[i]) <= 1);
    }

    printf("All CRIT-168 tests passed\n");
    return 0;
}

