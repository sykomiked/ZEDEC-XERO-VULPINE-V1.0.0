/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* swarm_harmonic.h — cycles, not clocks: the swarm keeps time like music.
 *
 * Time is counted in phase ticks. The slowest cycle is the FUNDAMENTAL;
 * harmonic n (1 .. 11) runs n times as often, so its period is
 * FUNDAMENTAL / n ticks. The fundamental is 27720 ticks = lcm(1 .. 11), so
 * every period is a whole number of ticks and all eleven harmonics line up
 * again at every fundamental.
 *
 *   H1  harmonic n is due on tick t iff t is a multiple of 27720 / n.
 *   H2  bands: harmonics 9-11 are REFLEX (witnessing, routing, sensing
 *       stress), 4-8 are THOUGHT (answers, tool chains), 1-3 are GROWTH
 *       (memory, learning, self-improvement).
 * Freestanding: no libc, no floating point, no 64-bit division helpers.
 */
#ifndef SWARM_HARMONIC_H
#define SWARM_HARMONIC_H

#include <stdint.h>

#define SWARM_HARMONICS          11u
#define SWARM_FUNDAMENTAL_TICKS  27720u   /* lcm(1, 2, ..., 11) */

typedef enum {
    SWARM_BAND_NONE    = 0,
    SWARM_BAND_GROWTH  = 1,   /* harmonics 1-3 */
    SWARM_BAND_THOUGHT = 2,   /* harmonics 4-8 */
    SWARM_BAND_REFLEX  = 3    /* harmonics 9-11 */
} swarm_band_t;

/* Period of harmonic n in ticks; 0 if n is not 1 .. 11. */
uint32_t     swarm_harmonic_period(uint32_t n);

/* H1: bit (n-1) is set iff harmonic n is due on `tick`. */
uint32_t     swarm_harmonics_due(uint64_t tick);

/* H2: the band of harmonic n. */
swarm_band_t swarm_harmonic_band(uint32_t n);

#endif /* SWARM_HARMONIC_H */
