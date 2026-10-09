#ifndef RANDOMBYTES_H
#define RANDOMBYTES_H

#include <stddef.h>
#include <stdint.h>

/* ZXV: there is no system RNG here. pq_mldsa65.c supplies this, feeding
 * the caller's keygen seed; see pq_mldsa65_keygen. */
#define randombytes zxv_mldsa_randombytes
void randombytes(uint8_t *out, size_t outlen);

#endif
