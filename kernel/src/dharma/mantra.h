/* mantra.h — Mantra: the Dimension of Sound (Signal / Data)
 *
 * Mantra: Manas (mind) + Tra (instrument) -- "an instrument to direct
 * the mind." In this architecture, a mantra is a precise vibrational
 * formula: a phase vector P = sum_k a_k * zeta_13^k (a cyc13_t),
 * exactly sephirot.h's exact-rational cyclotomic group ring element.
 *
 * mantra_encode lifts a raw incoming seed (bija) -- a network packet,
 * sensor telemetry, a user prompt, any byte sequence -- into such a
 * vector: the dynamic signal that will vibrate through Yantra's
 * static hardware container (see yantra.h).
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef MANTRA_H
#define MANTRA_H

#include "m5_types.h"
#include "sephirot.h"

/* Deterministic, lossless-enough encoding: byte i contributes its
 * value to coefficient index (i mod 13), so the vector's shape
 * reflects the seed's own byte distribution rather than an opaque
 * hash. Two seeds with the same distribution across residue classes
 * mod 13 produce the same mantra -- an intentional, physically
 * meaningful form of "resonance": inputs that share vibrational
 * structure land on the same phase vector. */
cyc13_t mantra_encode(const uint8_t *seed, uint32_t len);

/* Overall amplitude ("how loud" this mantra is): the sum of the
 * magnitudes of all 13 coefficients. */
rational_t mantra_amplitude(cyc13_t vector);

#endif
