/* rce_units.c — Reality Core Engine Units & Dimensions
 *
 * Implements the SI unit and dimensional-analysis engine.
 *
 * Author: 36N9 Genetics, LLC
 * License: SEL-3.3 (kernel component)
 */

#include "rce_units.h"

/* ===== Helpers ===== */

static void copy_str(char *dst, const char *src, uint32_t max) {
    uint32_t i;
    for (i = 0; i + 1 < max && src && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static int str_eq(const char *a, const char *b) {
    uint32_t i;
    for (i = 0; a[i] != '\0' && b[i] != '\0'; i++) {
        if (a[i] != b[i]) return 0;
    }
    return a[i] == '\0' && b[i] == '\0';
}

/* ===== Named Unit Dimensions ===== */

rce_dimension_t rce_unit_dimension(rce_unit_id_t unit) {
    rce_dimension_t d = rce_dim_dimensionless();
    switch (unit) {
        /* SI base units */
        case RCE_UNIT_METER:    d.exponents[RCE_DIM_LENGTH] = 1; break;
        case RCE_UNIT_KILOGRAM: d.exponents[RCE_DIM_MASS] = 1; break;
        case RCE_UNIT_SECOND:   d.exponents[RCE_DIM_TIME] = 1; break;
        case RCE_UNIT_AMPERE:   d.exponents[RCE_DIM_ELECTRIC_CURRENT] = 1; break;
        case RCE_UNIT_KELVIN:   d.exponents[RCE_DIM_TEMPERATURE] = 1; break;
        case RCE_UNIT_MOLE:     d.exponents[RCE_DIM_AMOUNT] = 1; break;
        case RCE_UNIT_CANDELA:  d.exponents[RCE_DIM_LUMINOUS_INTENSITY] = 1; break;
        /* SI derived units */
        case RCE_UNIT_HERTZ:     d.exponents[RCE_DIM_TIME] = -1; break;
        case RCE_UNIT_NEWTON:    d.exponents[RCE_DIM_LENGTH] = 1; d.exponents[RCE_DIM_MASS] = 1; d.exponents[RCE_DIM_TIME] = -2; break;
        case RCE_UNIT_PASCAL:    d.exponents[RCE_DIM_LENGTH] = -1; d.exponents[RCE_DIM_MASS] = 1; d.exponents[RCE_DIM_TIME] = -2; break;
        case RCE_UNIT_JOULE:     d.exponents[RCE_DIM_LENGTH] = 2; d.exponents[RCE_DIM_MASS] = 1; d.exponents[RCE_DIM_TIME] = -2; break;
        case RCE_UNIT_WATT:      d.exponents[RCE_DIM_LENGTH] = 2; d.exponents[RCE_DIM_MASS] = 1; d.exponents[RCE_DIM_TIME] = -3; break;
        case RCE_UNIT_COULOMB:   d.exponents[RCE_DIM_ELECTRIC_CURRENT] = 1; d.exponents[RCE_DIM_TIME] = 1; break;
        case RCE_UNIT_VOLT:      d.exponents[RCE_DIM_LENGTH] = 2; d.exponents[RCE_DIM_MASS] = 1; d.exponents[RCE_DIM_TIME] = -3; d.exponents[RCE_DIM_ELECTRIC_CURRENT] = -1; break;
        case RCE_UNIT_OHM:       d.exponents[RCE_DIM_LENGTH] = 2; d.exponents[RCE_DIM_MASS] = 1; d.exponents[RCE_DIM_TIME] = -3; d.exponents[RCE_DIM_ELECTRIC_CURRENT] = -2; break;
        case RCE_UNIT_FARAD:     d.exponents[RCE_DIM_LENGTH] = -2; d.exponents[RCE_DIM_MASS] = -1; d.exponents[RCE_DIM_TIME] = 4; d.exponents[RCE_DIM_ELECTRIC_CURRENT] = 2; break;
        case RCE_UNIT_HENRY:     d.exponents[RCE_DIM_LENGTH] = 2; d.exponents[RCE_DIM_MASS] = 1; d.exponents[RCE_DIM_TIME] = -2; d.exponents[RCE_DIM_ELECTRIC_CURRENT] = -2; break;
        case RCE_UNIT_TESLA:     d.exponents[RCE_DIM_MASS] = 1; d.exponents[RCE_DIM_TIME] = -2; d.exponents[RCE_DIM_ELECTRIC_CURRENT] = -1; break;
        case RCE_UNIT_WEBER:     d.exponents[RCE_DIM_LENGTH] = 2; d.exponents[RCE_DIM_MASS] = 1; d.exponents[RCE_DIM_TIME] = -2; d.exponents[RCE_DIM_ELECTRIC_CURRENT] = -1; break;
        case RCE_UNIT_LUMEN:     d.exponents[RCE_DIM_LUMINOUS_INTENSITY] = 1; break;
        case RCE_UNIT_LUX:       d.exponents[RCE_DIM_LUMINOUS_INTENSITY] = 1; d.exponents[RCE_DIM_LENGTH] = -2; break;
        case RCE_UNIT_BECQUEREL: d.exponents[RCE_DIM_TIME] = -1; break;
        case RCE_UNIT_GRAY:      d.exponents[RCE_DIM_LENGTH] = 2; d.exponents[RCE_DIM_TIME] = -2; break;
        case RCE_UNIT_SIEVERT:   d.exponents[RCE_DIM_LENGTH] = 2; d.exponents[RCE_DIM_TIME] = -2; break;
        case RCE_UNIT_KATAL:     d.exponents[RCE_DIM_AMOUNT] = 1; d.exponents[RCE_DIM_TIME] = -1; break;
        /* Dimensionless / affine / logarithmic */
        case RCE_UNIT_RADIAN:
        case RCE_UNIT_STERADIAN:
        case RCE_UNIT_DECIBEL:
        case RCE_UNIT_DBM:
            break;  /* dimensionless */
        case RCE_UNIT_DEGREE_C:
        case RCE_UNIT_DEGREE_F:
            d.exponents[RCE_DIM_TEMPERATURE] = 1;
            break;
        default:
            break;
    }
    return d;
}

const char *rce_unit_name(rce_unit_id_t unit) {
    switch (unit) {
        case RCE_UNIT_METER:    return "meter";
        case RCE_UNIT_KILOGRAM: return "kilogram";
        case RCE_UNIT_SECOND:   return "second";
        case RCE_UNIT_AMPERE:   return "ampere";
        case RCE_UNIT_KELVIN:   return "kelvin";
        case RCE_UNIT_MOLE:     return "mole";
        case RCE_UNIT_CANDELA:  return "candela";
        case RCE_UNIT_HERTZ:    return "hertz";
        case RCE_UNIT_NEWTON:   return "newton";
        case RCE_UNIT_PASCAL:   return "pascal";
        case RCE_UNIT_JOULE:    return "joule";
        case RCE_UNIT_WATT:     return "watt";
        case RCE_UNIT_COULOMB:  return "coulomb";
        case RCE_UNIT_VOLT:     return "volt";
        case RCE_UNIT_OHM:      return "ohm";
        case RCE_UNIT_FARAD:    return "farad";
        case RCE_UNIT_HENRY:    return "henry";
        case RCE_UNIT_TESLA:    return "tesla";
        case RCE_UNIT_WEBER:    return "weber";
        case RCE_UNIT_LUMEN:    return "lumen";
        case RCE_UNIT_LUX:      return "lux";
        case RCE_UNIT_BECQUEREL:return "becquerel";
        case RCE_UNIT_GRAY:     return "gray";
        case RCE_UNIT_SIEVERT:  return "sievert";
        case RCE_UNIT_KATAL:    return "katal";
        case RCE_UNIT_DEGREE_C: return "degree_celsius";
        case RCE_UNIT_DEGREE_F: return "degree_fahrenheit";
        case RCE_UNIT_RADIAN:   return "radian";
        case RCE_UNIT_STERADIAN:return "steradian";
        case RCE_UNIT_DECIBEL:  return "decibel";
        case RCE_UNIT_DBM:      return "dBm";
        default:                return "unknown";
    }
}

const char *rce_unit_symbol(rce_unit_id_t unit) {
    switch (unit) {
        case RCE_UNIT_METER:    return "m";
        case RCE_UNIT_KILOGRAM: return "kg";
        case RCE_UNIT_SECOND:   return "s";
        case RCE_UNIT_AMPERE:   return "A";
        case RCE_UNIT_KELVIN:   return "K";
        case RCE_UNIT_MOLE:     return "mol";
        case RCE_UNIT_CANDELA:  return "cd";
        case RCE_UNIT_HERTZ:    return "Hz";
        case RCE_UNIT_NEWTON:   return "N";
        case RCE_UNIT_PASCAL:   return "Pa";
        case RCE_UNIT_JOULE:    return "J";
        case RCE_UNIT_WATT:     return "W";
        case RCE_UNIT_COULOMB:  return "C";
        case RCE_UNIT_VOLT:     return "V";
        case RCE_UNIT_OHM:      return "Ω";
        case RCE_UNIT_FARAD:    return "F";
        case RCE_UNIT_HENRY:    return "H";
        case RCE_UNIT_TESLA:    return "T";
        case RCE_UNIT_WEBER:    return "Wb";
        case RCE_UNIT_LUMEN:    return "lm";
        case RCE_UNIT_LUX:      return "lx";
        case RCE_UNIT_BECQUEREL:return "Bq";
        case RCE_UNIT_GRAY:     return "Gy";
        case RCE_UNIT_SIEVERT:  return "Sv";
        case RCE_UNIT_KATAL:    return "kat";
        case RCE_UNIT_DEGREE_C: return "°C";
        case RCE_UNIT_DEGREE_F: return "°F";
        case RCE_UNIT_RADIAN:   return "rad";
        case RCE_UNIT_STERADIAN:return "sr";
        case RCE_UNIT_DECIBEL:  return "dB";
        case RCE_UNIT_DBM:      return "dBm";
        default:                return "?";
    }
}

/* ===== Quantity Operations ===== */

void rce_quantity_init(rce_quantity_t *q) {
    if (!q) return;
    ev_memset(q, 0, sizeof(*q));
    q->dimension = rce_dim_dimensionless();
}

void rce_quantity_set(rce_quantity_t *q, uint64_t mantissa, int16_t exp10,
                      bool negative, rce_dimension_t dim, rce_unit_id_t unit) {
    if (!q) return;
    rce_quantity_init(q);
    q->mantissa = mantissa;
    q->exp10 = exp10;
    q->negative = negative;
    q->dimension = dim;
    q->unit = unit;
}

bool rce_quantity_compatible(const rce_quantity_t *a, const rce_quantity_t *b) {
    if (!a || !b) return false;
    return rce_dim_equal(a->dimension, b->dimension);
}

bool rce_quantity_multiply(const rce_quantity_t *a, const rce_quantity_t *b,
                           rce_quantity_t *result) {
    if (!a || !b || !result) return false;
    rce_quantity_init(result);
    /* Multiply mantissas (simplified — no overflow handling for kernel) */
    result->mantissa = a->mantissa * b->mantissa;
    result->exp10 = a->exp10 + b->exp10;
    result->negative = a->negative != b->negative;
    result->dimension = rce_dim_multiply(a->dimension, b->dimension);
    result->unit = RCE_UNIT_NONE;
    return true;
}

bool rce_quantity_divide(const rce_quantity_t *a, const rce_quantity_t *b,
                         rce_quantity_t *result) {
    if (!a || !b || !result) return false;
    if (b->mantissa == 0) return false;  /* division by zero */
    rce_quantity_init(result);
    result->mantissa = a->mantissa / b->mantissa;  /* integer division */
    result->exp10 = a->exp10 - b->exp10;
    result->negative = a->negative != b->negative;
    result->dimension = rce_dim_divide(a->dimension, b->dimension);
    result->unit = RCE_UNIT_NONE;
    return true;
}

void rce_quantity_set_uncertainty(rce_quantity_t *q, uint64_t mantissa, int16_t exp10) {
    if (!q) return;
    q->has_uncertainty = true;
    q->uncertainty_mantissa = mantissa;
    q->uncertainty_exp10 = exp10;
}

/* ===== Formatting ===== */

static void format_int(char *buf, uint32_t buf_len, uint64_t val) {
    if (buf_len == 0) return;
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[20];
    int pos = 0;
    while (val > 0 && pos < 20) {
        tmp[pos++] = '0' + (val % 10);
        val /= 10;
    }
    uint32_t out = 0;
    for (int i = pos - 1; i >= 0 && out < buf_len - 1; i--)
        buf[out++] = tmp[i];
    buf[out] = '\0';
}

void rce_dimension_format(rce_dimension_t d, char *buf, uint32_t buf_len) {
    if (!buf || buf_len == 0) return;
    buf[0] = '\0';
    uint32_t pos = 0;
    static const char *dim_names[] = {"m", "kg", "s", "A", "K", "mol", "cd"};
    bool first = true;
    for (uint32_t i = 0; i < RCE_DIM_COUNT; i++) {
        if (d.exponents[i] == 0) continue;
        if (!first && pos < buf_len - 1) buf[pos++] = ' ';
        first = false;
        if (pos >= buf_len - 1) break;
        buf[pos] = '\0';
        /* Append dimension name */
        const char *name = dim_names[i];
        while (*name && pos < buf_len - 1) buf[pos++] = *name++;
        /* Append exponent if not 1 */
        if (d.exponents[i] != 1) {
            buf[pos++] = '^';
            if (d.exponents[i] < 0) {
                buf[pos++] = '-';
                format_int(buf + pos, buf_len - pos, (uint64_t)(-d.exponents[i]));
                while (buf[pos]) pos++;
            } else {
                format_int(buf + pos, buf_len - pos, (uint64_t)d.exponents[i]);
                while (buf[pos]) pos++;
            }
        }
    }
    if (first && pos < buf_len) { buf[pos++] = '1'; buf[pos] = '\0'; }
    else buf[pos] = '\0';
}

void rce_quantity_format(const rce_quantity_t *q, char *buf, uint32_t buf_len) {
    if (!buf || buf_len == 0 || !q) return;
    uint32_t pos = 0;
    if (q->negative && pos < buf_len - 1) buf[pos++] = '-';
    format_int(buf + pos, buf_len - pos, q->mantissa);
    while (buf[pos]) pos++;
    if (q->exp10 != 0 && pos < buf_len - 1) {
        buf[pos++] = 'e';
        if (q->exp10 < 0) {
            buf[pos++] = '-';
            format_int(buf + pos, buf_len - pos, (uint64_t)(-q->exp10));
        } else {
            format_int(buf + pos, buf_len - pos, (uint64_t)q->exp10);
        }
        while (buf[pos]) pos++;
    }
    if (q->unit != RCE_UNIT_NONE && pos < buf_len - 2) {
        buf[pos++] = ' ';
        const char *sym = rce_unit_symbol(q->unit);
        while (*sym && pos < buf_len - 1) buf[pos++] = *sym++;
    }
    buf[pos] = '\0';
}

/* ===== Constant Registry ===== */

void rce_constants_init(rce_constant_registry_t *reg) {
    if (!reg) return;
    ev_memset(reg, 0, sizeof(*reg));
}

int32_t rce_constants_register(rce_constant_registry_t *reg,
                               const char *name, const rce_quantity_t *value,
                               bool is_defining, const char *source) {
    if (!reg || !name || !value) return -1;
    if (reg->count >= RCE_MAX_CONSTANTS) return -1;
    rce_constant_t *c = &reg->constants[reg->count];
    ev_memset(c, 0, sizeof(*c));
    copy_str(c->name, name, sizeof(c->name));
    c->value = *value;
    c->is_defining = is_defining;
    c->is_measured = value->has_uncertainty;
    if (source) copy_str(c->source, source, sizeof(c->source));
    return (int32_t)reg->count++;
}

rce_constant_t *rce_constants_get(rce_constant_registry_t *reg, uint32_t idx) {
    if (!reg || idx >= reg->count) return NULL;
    return &reg->constants[idx];
}

rce_constant_t *rce_constants_find(rce_constant_registry_t *reg, const char *name) {
    if (!reg || !name) return NULL;
    for (uint32_t i = 0; i < reg->count; i++) {
        if (str_eq(reg->constants[i].name, name))
            return &reg->constants[i];
    }
    return NULL;
}

/* ===== SI Defining Constants ===== */
/* The 7 SI defining constants have exact values (zero uncertainty).
 * These are immutable within a model-pack version. */

void rce_constants_register_si_defining(rce_constant_registry_t *reg) {
    if (!reg) return;
    rce_quantity_t q;
    rce_dimension_t dim;

    /* 1. Speed of light in vacuum: c = 299792458 m/s (exact) */
    dim = rce_dim_dimensionless();
    dim.exponents[RCE_DIM_LENGTH] = 1;
    dim.exponents[RCE_DIM_TIME] = -1;
    rce_quantity_set(&q, 299792458, 0, false, dim, RCE_UNIT_METER);
    rce_constants_register(reg, "speed_of_light", &q, true, "SI 2019");

    /* 2. Planck constant: h = 6.62607015e-34 J*s (exact) */
    dim = rce_dim_dimensionless();
    dim.exponents[RCE_DIM_LENGTH] = 2;
    dim.exponents[RCE_DIM_MASS] = 1;
    dim.exponents[RCE_DIM_TIME] = -1;
    rce_quantity_set(&q, 662607015, -34, false, dim, RCE_UNIT_JOULE);
    rce_constants_register(reg, "planck_constant", &q, true, "SI 2019");

    /* 3. Elementary charge: e = 1.602176634e-19 C (exact) */
    dim = rce_dim_dimensionless();
    dim.exponents[RCE_DIM_ELECTRIC_CURRENT] = 1;
    dim.exponents[RCE_DIM_TIME] = 1;
    rce_quantity_set(&q, 1602176634, -19, false, dim, RCE_UNIT_COULOMB);
    rce_constants_register(reg, "elementary_charge", &q, true, "SI 2019");

    /* 4. Boltzmann constant: k = 1.380649e-23 J/K (exact) */
    dim = rce_dim_dimensionless();
    dim.exponents[RCE_DIM_LENGTH] = 2;
    dim.exponents[RCE_DIM_MASS] = 1;
    dim.exponents[RCE_DIM_TIME] = -2;
    dim.exponents[RCE_DIM_TEMPERATURE] = -1;
    rce_quantity_set(&q, 1380649, -23, false, dim, RCE_UNIT_JOULE);
    rce_constants_register(reg, "boltzmann_constant", &q, true, "SI 2019");

    /* 5. Avogadro constant: N_A = 6.02214076e23 mol^-1 (exact) */
    dim = rce_dim_dimensionless();
    dim.exponents[RCE_DIM_AMOUNT] = -1;
    rce_quantity_set(&q, 602214076, 23, false, dim, RCE_UNIT_MOLE);
    rce_constants_register(reg, "avogadro_constant", &q, true, "SI 2019");

    /* 6. Luminous efficacy: K_cd = 683 lm/W (exact) */
    dim = rce_dim_dimensionless();
    dim.exponents[RCE_DIM_LUMINOUS_INTENSITY] = 1;
    dim.exponents[RCE_DIM_LENGTH] = -2;
    dim.exponents[RCE_DIM_MASS] = -1;
    dim.exponents[RCE_DIM_TIME] = 3;
    rce_quantity_set(&q, 683, 0, false, dim, RCE_UNIT_LUMEN);
    rce_constants_register(reg, "luminous_efficacy", &q, true, "SI 2019");

    /* 7. Cs hyperfine frequency: Δν_Cs = 9192631770 Hz (exact) */
    dim = rce_dim_dimensionless();
    dim.exponents[RCE_DIM_TIME] = -1;
    rce_quantity_set(&q, 9192631770, 0, false, dim, RCE_UNIT_HERTZ);
    rce_constants_register(reg, "cs_hyperfine_frequency", &q, true, "SI 2019");
}
