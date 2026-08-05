#include "crit_168_word.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

void word168_to_octets(const word168_t *w, uint8_t *octets) {
    memcpy(octets, w->bytes, WORD168_OCTETS);
}

void octets_to_word168(const uint8_t *octets, word168_t *w) {
    memcpy(w->bytes, octets, WORD168_OCTETS);
}

void word168_alternate_endianness(word168_t *w) {
    for (int i = 0; i < WORD168_OCTETS / 2; i++) {
        uint8_t tmp = w->bytes[i];
        w->bytes[i] = w->bytes[WORD168_OCTETS - 1 - i];
        w->bytes[WORD168_OCTETS - 1 - i] = tmp;
    }
}

void crit_transform(const word168_t *w, double complex *out, int num_octets) {
    if (num_octets > WORD168_OCTETS) num_octets = WORD168_OCTETS;
    for (int i = 0; i < num_octets; i++) {
        out[i] = (double complex)w->bytes[i] * cexp(2.0 * I * M_PI * (double)i / (double)num_octets);
    }
}

void crit_inverse(const double complex *in, word168_t *w, int num_octets) {
    if (num_octets > WORD168_OCTETS) num_octets = WORD168_OCTETS;
    memset(w->bytes, 0, WORD168_OCTETS);
    for (int i = 0; i < num_octets; i++) {
        double mag = creal(in[i] * cexp(-2.0 * I * M_PI * (double)i / (double)num_octets));
        w->bytes[i] = (uint8_t)(mag + 0.5);
    }
}

