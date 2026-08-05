/* mlkem_sample.h — ML-KEM-768 SampleNTT (FIPS 203 Algorithm 7)
 *
 * Generates a uniformly-random NTT-domain polynomial from a 32-byte
 * seed + 2 index bytes, via SHAKE128 rejection sampling. Used to
 * expand the public seed rho into the full k*k matrix A without ever
 * transmitting it.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * License: SEL-3.3 (kernel component)
 */
#ifndef MLKEM_SAMPLE_H
#define MLKEM_SAMPLE_H

#include "mlkem_ntt.h"
#include <stdint.h>

/* rho: 32-byte seed. i, j: single-byte indices (order matters and is
 * caller-defined -- see mlkem_kpe.c for the exact FIPS 203 convention
 * used when building the k*k matrix). Output is already in NTT domain
 * (never needs poly_ntt() applied). */
void sample_ntt(const uint8_t rho[32], uint8_t i, uint8_t j, poly_t *out);

#endif /* MLKEM_SAMPLE_H */
