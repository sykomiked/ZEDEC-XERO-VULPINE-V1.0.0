/* uvn.h — The Universal Vector Number System
 *
 * Rather than scattering plain int32_t counters, double-precision
 * magnitude comparisons, and ad-hoc rational_t pairs throughout the
 * OS layer, every quantity that carries meaning here -- a charge, a
 * causal weight, a coherence, a conductance -- is represented as a
 * uvn_t: sephirot.h's cyc13_t (a 13-dimensional exact-rational
 * vector), with a plain scalar as its degenerate case (a vector that
 * is nonzero only at index 0). A scalar and a full phase
 * superposition are thus the SAME type at different levels of
 * differentiation, not two representations requiring constant,
 * error-prone conversion at every module boundary.
 *
 * Every operation here is exact-rational; none of them introduce a
 * double-precision intermediate, including magnitude/absolute-value,
 * which earlier code in this OS layer (bodhi.c, mantra.c) had been
 * computing via rational_mag()'s double division purely to get a
 * sign-stripped comparison value. uvn_magnitude replaces that with
 * pure integer-exact arithmetic.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef UVN_H
#define UVN_H

#include "m5_types.h"
#include "sephirot.h"

typedef struct uvn {
    cyc13_t v;
} uvn_t;

/* Lifts a plain rational/integer into a pure index-0 (scalar) vector. */
uvn_t uvn_scalar(rational_t r);
uvn_t uvn_from_int(int64_t n);

/* Exact rational absolute value -- no double involved. */
rational_t rational_abs_exact(rational_t r);

/* Exact-rational total magnitude: the sum, across all 13 axes, of
 * the exact absolute value of each coefficient. */
rational_t uvn_magnitude(uvn_t u);

/* The index-0 coefficient alone -- the vector's "scalar part". */
rational_t uvn_scalar_value(uvn_t u);

uvn_t uvn_add(uvn_t a, uvn_t b);
uvn_t uvn_scale(uvn_t a, rational_t s);
bool uvn_equal(uvn_t a, uvn_t b);
bool uvn_is_zero(uvn_t a);

#endif
