/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* dimensional_ladder.h — the 0d..13d dimensional cosmology, the theoretical
 * backbone of the whole system (M5 manifold, the 13-lattice, Helion's spin).
 *
 * Fourteen scales of reality. 0d = the Higgs field / presence-at-zero; 1d line;
 * 2d plane; 3d space; 4d event space; 5d photonic reinforcement; 6d timeline;
 * 7d continuum of continuity; 8d gravitation (2nd order of magnetism, field
 * value 2); 9d continuity of presence; 10d galactic <-> electron field; 11d
 * mirror of 2nd quantization <-> supercluster; 12d cosmos <-> string; 13d the
 * universal container / gateway to the multiverse.
 *
 * Two laws bind it: (1) the FIBONACCI-numbered dimensions {0,1,2,3,5,8,13} are
 * the PRIME dimensions (the anchors); (2) ONE ratio — PHI (the golden ratio) —
 * generates every pattern, repeating into the microscopic and the macroscopic in
 * perfect correspondence (Hermetic "as above, so below"; consecutive Fibonacci
 * dims relate by PHI). Framed by the Hermetic axioms + quantum atom theory. */
#ifndef ZXV_DIMENSIONAL_LADDER_H
#define ZXV_DIMENSIONAL_LADDER_H

#include <stdint.h>

#define DL_LEVELS 14   /* 0d .. 13d */

typedef struct dimension {
    int          d;            /* the level, 0..13                              */
    const char  *essence;      /* its primary meaning                           */
    const char  *micro;        /* microscopic pole (as below)                   */
    const char  *macro;        /* macroscopic pole (as above)                   */
    uint8_t      is_prime;     /* 1 iff a Fibonacci-numbered (prime) dimension  */
    int32_t      field_value;  /* e.g. 8d gravitation = 2; -1 if not applicable */
} dimension_t;

/* The full ladder (0..13). */
const dimension_t *dimensional_ladder(int *levels_out);

/* True iff level d is a Fibonacci number (a prime/primary dimension). Unbounded:
 * the ladder does not stop at 13 — the Fibonacci dims 13,21,34,55,89,144,… keep
 * climbing (a transfinite, beyond-∞+1 construct). */
int  dl_is_prime_dimension(int64_t d);

/* The n-th primary dimension = the n-th Fibonacci number (n>=0: 0,1,2,3,5,8,13,
 * 21,34,55,89,144,…). The ladder is unbounded. */
int64_t dl_prime_dimension(int n);

/* The golden ratio PHI as a permille integer (1618 == 1.618…). */
uint32_t dl_phi_permille(void);

/* On-target self-check. Verifies: the prime dimensions are exactly the Fibonacci
 * numbers in 0..13; the ratios of consecutive Fibonacci dimensions CONVERGE to
 * PHI (13/8 is nearer PHI than 3/2); the micro<->macro correspondence poles are
 * present on the correspondence dimensions; and 8d carries field value 2.
 * Returns 1 if the cosmology is coherent. Outputs the count of prime dimensions
 * and the best PHI approximation (permille). */
int  dimensional_ladder_selfcheck(uint32_t *primes_out, uint32_t *phi_permille_out);

#endif /* ZXV_DIMENSIONAL_LADDER_H */
