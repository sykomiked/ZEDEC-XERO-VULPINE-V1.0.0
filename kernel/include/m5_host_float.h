/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* m5_host_float.h -- double / double complex bridge for HOSTED code only.
 *
 * m5_types.h is integer only: phase_t and the complex values in telemetry_t,
 * axiom_matrix_t and shadow_event_t are Q16.16 integers. The root-level OS
 * layer simulations (os_lattice, neon, gridchain, physics_sim, hccs,
 * audiogenomics, governance, security) and their tests run on a host and
 * still compute in double. This header gives them the conversions to and from
 * the integer kernel types. No kernel image may include it: it refuses to
 * compile unless TEST_HOST is defined.
 */
#ifndef M5_HOST_FLOAT_H
#define M5_HOST_FLOAT_H
#ifndef TEST_HOST
#    error "m5_host_float.h is host-only; kernel images are integer only"
#endif
#include <complex.h>
#include <math.h>
#include <stdint.h>
#include "m5_types.h"

static inline double rational_mag(rational_t r)
{
    return r.den == 0 ? 0.0 : (double) r.num / (double) r.den;
}

static inline double trit_to_ell(trit_t t)
{
    return (double) trit_to_ell_q16(t) / 65536.0;
}

/* Round a double to Q16.16 in 64 bits, saturating at the int64 range. */
static inline int64_t m5_q16_from_double(double x)
{
    double s = x * 65536.0;
    if (s != s) return 0;
    if (s >= 9.2e18) return INT64_MAX;
    if (s <= -9.2e18) return INT64_MIN;
    return (int64_t) llround(s);
}

/* Round a double to Q16.16 in 32 bits, saturating at the int32 range. */
static inline int32_t m5_q16_32_from_double(double x)
{
    int64_t v = m5_q16_from_double(x);
    if (v > INT32_MAX) return INT32_MAX;
    if (v < INT32_MIN) return INT32_MIN;
    return (int32_t) v;
}

static inline double m5_double_from_q16(int64_t v)
{
    return (double) v / 65536.0;
}

static inline zxv_cq16_t m5_cq16_from_dc(double complex z)
{
    return cq16(m5_q16_from_double(creal(z)), m5_q16_from_double(cimag(z)));
}

static inline double complex m5_dc_from_cq16(zxv_cq16_t z)
{
    return m5_double_from_q16(z.re) + I * m5_double_from_q16(z.im);
}

static inline phase_t m5_phase_from_double(double r, double i)
{
    phase_t p;
    p.r = m5_q16_32_from_double(r);
    p.i = m5_q16_32_from_double(i);
    return p;
}

static inline double m5_phase_r(phase_t p)
{
    return m5_double_from_q16(p.r);
}

static inline double m5_phase_i(phase_t p)
{
    return m5_double_from_q16(p.i);
}

#endif /* M5_HOST_FLOAT_H */
