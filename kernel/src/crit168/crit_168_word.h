/* crit_168_word.h — 168-bit Universal Word and CRIT Transform (M5 spec SS10)
 * CRIT uses complex-exponential (cexp with I) basis: out[k] = sum(w[n] * cexp(2*pi*I*n*k/N))
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef CRIT_168_WORD_H
#define CRIT_168_WORD_H
#include "m5_types.h"
#include <complex.h>

void word168_to_octets(const word168_t *w, uint8_t *octets);
void octets_to_word168(const uint8_t *octets, word168_t *w);
void word168_alternate_endianness(word168_t *w);
void crit_transform(const word168_t *w, double complex *out, int num_octets);
void crit_inverse(const double complex *in, word168_t *w, int num_octets);

#endif