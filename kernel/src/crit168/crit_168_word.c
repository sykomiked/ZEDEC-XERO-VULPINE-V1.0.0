#include "crit_168_word.h"
#include <string.h>
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

/* out[i] = w[i] * e^(2 pi i * i / N), Q16.16. */
void crit_transform(const word168_t *w, zxv_cq16_t *out, int num_octets)
{
    if (num_octets > WORD168_OCTETS) num_octets = WORD168_OCTETS;
    for (int i = 0; i < num_octets; i++) {
        zxv_cq16_t e = cq16_expi_turn(fx_turn_frac((uint64_t) i, (uint64_t) num_octets));
        out[i] = cq16_mul(cq16((int64_t) w->bytes[i] * Q16_ONE, 0), e);
    }
}

/* w[i] = round(Re(in[i] * e^(-2 pi i * i / N))), clamped to a byte. */
void crit_inverse(const zxv_cq16_t *in, word168_t *w, int num_octets)
{
    if (num_octets > WORD168_OCTETS) num_octets = WORD168_OCTETS;
    memset(w->bytes, 0, WORD168_OCTETS);
    for (int i = 0; i < num_octets; i++) {
        uint32_t turn = fx_turn_frac((uint64_t) i, (uint64_t) num_octets);
        zxv_cq16_t v = cq16_mul(in[i], cq16_expi_turn((uint32_t) 0 - turn));
        int64_t b = (v.re + Q16_ONE / 2) >> Q16_SHIFT;
        w->bytes[i] = (uint8_t) (b < 0 ? 0 : (b > 255 ? 255 : b));
    }
}
