/* rce_units.h — Reality Core Engine Units & Dimensions
 *
 * The SI unit and dimensional-analysis engine. This is the foundational
 * anchor for Jacob's Ladder physics dimensions and all RCE computations.
 *
 * Implements:
 *   - 7 SI base dimensions (length, mass, time, electric current,
 *     temperature, amount of substance, luminous intensity)
 *   - Derived unit representation as dimension exponents
 *   - Compile-time and runtime dimensional analysis
 *   - Quantity with uncertainty metadata
 *   - Unit compatibility checking and conversion
 *
 * Rule: never flatten source-language metadata before required numeric,
 * layout, exception, ownership, aliasing, array, and calling semantics
 * have been verified.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef RCE_UNITS_H
#define RCE_UNITS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "event_space.h"

/* ===== RCE Constants ===== */

#define RCE_MAX_NAME_LEN    64
#define RCE_MAX_SOURCE_LEN  64

/* ===== SI Base Dimensions ===== */
/* The 7 SI base quantities. All physical quantities are expressed as
 * products of these with integer (or rational) exponents. */

typedef enum {
    RCE_DIM_LENGTH         = 0,  /* meter (m) */
    RCE_DIM_MASS           = 1,  /* kilogram (kg) */
    RCE_DIM_TIME           = 2,  /* second (s) */
    RCE_DIM_ELECTRIC_CURRENT = 3,  /* ampere (A) */
    RCE_DIM_TEMPERATURE    = 4,  /* kelvin (K) */
    RCE_DIM_AMOUNT         = 5,  /* mole (mol) */
    RCE_DIM_LUMINOUS_INTENSITY = 6,  /* candela (cd) */
    RCE_DIM_COUNT          = 7   /* number of base dimensions */
} rce_base_dim_t;

/* ===== Dimension Exponents ===== */
/* A physical dimension is represented as exponents of the 7 base quantities.
 * For example, velocity is [1, 0, -1, 0, 0, 0, 0] (m/s).
 * Force is [1, 1, -2, 0, 0, 0, 0] (kg*m/s^2 = N). */

typedef struct rce_dimension {
    int8_t exponents[RCE_DIM_COUNT];  /* exponents for each base dimension */
} rce_dimension_t;

/* ===== Unit Identifier ===== */
/* Common named units for reference. The engine works with dimensions,
 * not unit names, but named units help with provenance and display. */

typedef enum {
    RCE_UNIT_NONE        = 0,
    /* SI base units */
    RCE_UNIT_METER       = 1,
    RCE_UNIT_KILOGRAM    = 2,
    RCE_UNIT_SECOND      = 3,
    RCE_UNIT_AMPERE      = 4,
    RCE_UNIT_KELVIN      = 5,
    RCE_UNIT_MOLE        = 6,
    RCE_UNIT_CANDELA     = 7,
    /* SI derived units */
    RCE_UNIT_HERTZ       = 8,   /* s^-1 */
    RCE_UNIT_NEWTON      = 9,   /* kg*m*s^-2 */
    RCE_UNIT_PASCAL      = 10,  /* kg*m^-1*s^-2 */
    RCE_UNIT_JOULE       = 11,  /* kg*m^2*s^-2 */
    RCE_UNIT_WATT        = 12,  /* kg*m^2*s^-3 */
    RCE_UNIT_COULOMB     = 13,  /* A*s */
    RCE_UNIT_VOLT        = 14,  /* kg*m^2*s^-3*A^-1 */
    RCE_UNIT_OHM         = 15,  /* kg*m^2*s^-3*A^-2 */
    RCE_UNIT_FARAD       = 16,  /* kg^-1*m^-2*s^4*A^2 */
    RCE_UNIT_HENRY       = 17,  /* kg*m^2*s^-2*A^-2 */
    RCE_UNIT_TESLA       = 18,  /* kg*s^-2*A^-1 */
    RCE_UNIT_WEBER       = 19,  /* kg*m^2*s^-2*A^-1 */
    RCE_UNIT_LUMEN       = 20,  /* cd */
    RCE_UNIT_LUX         = 21,  /* cd*m^-2 */
    RCE_UNIT_BECQUEREL   = 22,  /* s^-1 */
    RCE_UNIT_GRAY        = 23,  /* m^2*s^-2 */
    RCE_UNIT_SIEVERT     = 24,  /* m^2*s^-2 */
    RCE_UNIT_KATAL       = 25,  /* mol*s^-1 */
    /* Non-SI accepted */
    RCE_UNIT_DEGREE_C    = 26,  /* affine: K + 273.15 */
    RCE_UNIT_DEGREE_F    = 27,  /* affine: K * 9/5 - 459.67 */
    RCE_UNIT_RADIAN      = 28,  /* dimensionless */
    RCE_UNIT_STERADIAN   = 29,  /* dimensionless */
    RCE_UNIT_DECIBEL     = 30,  /* logarithmic */
    RCE_UNIT_DBM         = 31,  /* logarithmic relative to 1mW */
    RCE_UNIT_COUNT       = 32   /* number of named units */
} rce_unit_id_t;

/* ===== Physical Quantity ===== */
/* A quantity is a numeric value with a dimension, unit, and uncertainty.
 * The value is stored as a 64-bit fixed-point or float representation.
 * For the kernel, we use a simple struct with a double-equivalent
 * representation (stored as mantissa and exponent for freestanding). */

typedef struct rce_quantity {
    /* Value stored as sign + 64-bit mantissa + base-10 exponent */
    bool negative;
    uint64_t mantissa;     /* significant digits */
    int16_t exp10;         /* decimal exponent: value = ±mantissa × 10^exp10 */

    /* Dimension */
    rce_dimension_t dimension;

    /* Unit identifier for display/provenance */
    rce_unit_id_t unit;

    /* Uncertainty (±value, same dimension) */
    bool has_uncertainty;
    uint64_t uncertainty_mantissa;
    int16_t uncertainty_exp10;

    /* Provenance */
    char source[RCE_MAX_NAME_LEN];  /* source label */
    bool is_measured;                /* true if from measurement, false if computed */
    bool is_constant;                /* true if a physical constant */
} rce_quantity_t;

/* ===== Dimension Constructors ===== */

/* Dimensionless quantity */
static inline rce_dimension_t rce_dim_dimensionless(void) {
    rce_dimension_t d;
    ev_memset(&d, 0, sizeof(d));
    return d;
}

/* Single base dimension with exponent 1 */
static inline rce_dimension_t rce_dim_base(rce_base_dim_t base) {
    rce_dimension_t d = rce_dim_dimensionless();
    if (base < RCE_DIM_COUNT) d.exponents[base] = 1;
    return d;
}

/* ===== Dimension Operations ===== */

/* Multiply dimensions: add exponents */
static inline rce_dimension_t rce_dim_multiply(rce_dimension_t a, rce_dimension_t b) {
    rce_dimension_t result;
    for (uint32_t i = 0; i < RCE_DIM_COUNT; i++)
        result.exponents[i] = a.exponents[i] + b.exponents[i];
    return result;
}

/* Divide dimensions: subtract exponents */
static inline rce_dimension_t rce_dim_divide(rce_dimension_t a, rce_dimension_t b) {
    rce_dimension_t result;
    for (uint32_t i = 0; i < RCE_DIM_COUNT; i++)
        result.exponents[i] = a.exponents[i] - b.exponents[i];
    return result;
}

/* Power: multiply all exponents by n */
static inline rce_dimension_t rce_dim_power(rce_dimension_t d, int8_t n) {
    rce_dimension_t result;
    for (uint32_t i = 0; i < RCE_DIM_COUNT; i++)
        result.exponents[i] = d.exponents[i] * n;
    return result;
}

/* Square root: divide all exponents by 2 (only if all even) */
static inline bool rce_dim_sqrt(rce_dimension_t d, rce_dimension_t *out) {
    if (!out) return false;
    for (uint32_t i = 0; i < RCE_DIM_COUNT; i++) {
        if (d.exponents[i] % 2 != 0) return false;
        out->exponents[i] = d.exponents[i] / 2;
    }
    return true;
}

/* Check if two dimensions are equal */
static inline bool rce_dim_equal(rce_dimension_t a, rce_dimension_t b) {
    for (uint32_t i = 0; i < RCE_DIM_COUNT; i++) {
        if (a.exponents[i] != b.exponents[i]) return false;
    }
    return true;
}

/* Check if dimensionless */
static inline bool rce_dim_is_dimensionless(rce_dimension_t d) {
    for (uint32_t i = 0; i < RCE_DIM_COUNT; i++) {
        if (d.exponents[i] != 0) return false;
    }
    return true;
}

/* ===== Named Unit Dimensions ===== */

rce_dimension_t rce_unit_dimension(rce_unit_id_t unit);
const char *rce_unit_name(rce_unit_id_t unit);
const char *rce_unit_symbol(rce_unit_id_t unit);

/* ===== Quantity Operations ===== */

void rce_quantity_init(rce_quantity_t *q);

/* Create a quantity from integer mantissa and dimension */
void rce_quantity_set(rce_quantity_t *q, uint64_t mantissa, int16_t exp10,
                      bool negative, rce_dimension_t dim, rce_unit_id_t unit);

/* Check if two quantities have compatible dimensions */
bool rce_quantity_compatible(const rce_quantity_t *a, const rce_quantity_t *b);

/* Multiply two quantities (dimensions add) */
bool rce_quantity_multiply(const rce_quantity_t *a, const rce_quantity_t *b,
                           rce_quantity_t *result);

/* Divide two quantities (dimensions subtract) */
bool rce_quantity_divide(const rce_quantity_t *a, const rce_quantity_t *b,
                         rce_quantity_t *result);

/* Set uncertainty on a quantity */
void rce_quantity_set_uncertainty(rce_quantity_t *q, uint64_t mantissa, int16_t exp10);

/* Format a dimension as a string (e.g., "m^1 kg^1 s^-2") */
void rce_dimension_format(rce_dimension_t d, char *buf, uint32_t buf_len);

/* Format a quantity value as a string */
void rce_quantity_format(const rce_quantity_t *q, char *buf, uint32_t buf_len);

/* ===== SI Defining Constants ===== */
/* These are immutable within a model-pack version. */

typedef struct rce_constant {
    char name[32];
    rce_quantity_t value;
    bool is_defining;     /* SI defining constant — immutable */
    bool is_measured;     /* measured with uncertainty */
    char source[64];      /* publication or dataset source */
} rce_constant_t;

#define RCE_MAX_CONSTANTS 32

typedef struct rce_constant_registry {
    rce_constant_t constants[RCE_MAX_CONSTANTS];
    uint32_t count;
} rce_constant_registry_t;

void rce_constants_init(rce_constant_registry_t *reg);
int32_t rce_constants_register(rce_constant_registry_t *reg,
                               const char *name, const rce_quantity_t *value,
                               bool is_defining, const char *source);
rce_constant_t *rce_constants_get(rce_constant_registry_t *reg, uint32_t idx);
rce_constant_t *rce_constants_find(rce_constant_registry_t *reg, const char *name);

/* Register the 7 SI defining constants (exact values) */
void rce_constants_register_si_defining(rce_constant_registry_t *reg);

#endif /* RCE_UNITS_H */
