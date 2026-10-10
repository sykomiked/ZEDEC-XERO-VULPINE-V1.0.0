/* test_crit_168_word.c — Tests for CRIT-168 transform
 * Verifies that crit_transform produces nonzero imaginary components via cexp(2*pi*I*n/N).
 */
#include "crit_168_word.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

int main(void)
{
    word168_t w;
    memset(&w, 0, sizeof(w));
    for (int i = 0; i < WORD168_OCTETS; i++) {
        w.bytes[i] = (uint8_t) (i + 1);
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

    zxv_cq16_t freq[WORD168_OCTETS];
    crit_transform(&w, freq, WORD168_OCTETS);

    assert(freq[1].im != 0);
    /* index 0 has angle 0: exactly w[0] + 0i */
    assert(freq[0].re == (int64_t)w.bytes[0] * Q16_ONE && freq[0].im == 0);
    /* every output keeps its magnitude w[i] (|e^(i theta)| = 1) to < 0.01 */
    for (int i = 0; i < WORD168_OCTETS; i++) {
        int64_t want = (int64_t)w.bytes[i] * Q16_ONE;
        int64_t got = (int64_t)cq16_abs(freq[i]);
        assert(got - want < Q16_ONE / 100 && want - got < Q16_ONE / 100);
    }

    word168_t w4;
    crit_inverse(freq, &w4, WORD168_OCTETS);
    for (int i = 0; i < WORD168_OCTETS; i++) {
        assert(w4.bytes[i] == w.bytes[i]); /* exact round trip in Q16.16 */
    }

    /* full byte range round-trips too */
    for (int i = 0; i < WORD168_OCTETS; i++) w.bytes[i] = (uint8_t)(255 - 7 * i);
    crit_transform(&w, freq, WORD168_OCTETS);
    crit_inverse(freq, &w4, WORD168_OCTETS);
    assert(memcmp(w4.bytes, w.bytes, WORD168_OCTETS) == 0);

    printf("All CRIT-168 tests passed\n");
    return 0;
}
