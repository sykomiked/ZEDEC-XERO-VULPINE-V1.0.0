/* test_rce_units.c — RCE Units & Dimensions Tests
 *
 * Tests for the Reality Core Engine units and dimensional analysis.
 *
 * Author: 36N9 Genetics, LLC
 * License: SEL-3.3 (kernel component)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "rce_units.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    tests_run++; \
    printf("  [TEST] %s ... ", #name); \
    name(); \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s\n", msg); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define PASS() return

/* ===== Dimension Tests ===== */

TEST(dim_dimensionless_test) {
    rce_dimension_t d = rce_dim_dimensionless();
    ASSERT(rce_dim_is_dimensionless(d), "dimensionless is dimensionless");
    PASS();
}

TEST(dim_base_test) {
    rce_dimension_t length = rce_dim_base(RCE_DIM_LENGTH);
    ASSERT(length.exponents[RCE_DIM_LENGTH] == 1, "length exponent is 1");
    ASSERT(length.exponents[RCE_DIM_MASS] == 0, "mass exponent is 0");
    ASSERT(!rce_dim_is_dimensionless(length), "length is not dimensionless");
    PASS();
}

TEST(dim_multiply_test) {
    rce_dimension_t length = rce_dim_base(RCE_DIM_LENGTH);
    rce_dimension_t time = rce_dim_base(RCE_DIM_TIME);
    rce_dimension_t velocity = rce_dim_divide(length, time);
    ASSERT(velocity.exponents[RCE_DIM_LENGTH] == 1, "velocity length exponent is 1");
    ASSERT(velocity.exponents[RCE_DIM_TIME] == -1, "velocity time exponent is -1");
    PASS();
}

TEST(dim_force_test) {
    /* Force = mass * length / time^2 = [1, 1, -2, 0, 0, 0, 0] */
    rce_dimension_t mass = rce_dim_base(RCE_DIM_MASS);
    rce_dimension_t length = rce_dim_base(RCE_DIM_LENGTH);
    rce_dimension_t time = rce_dim_base(RCE_DIM_TIME);
    rce_dimension_t time_sq = rce_dim_power(time, 2);
    rce_dimension_t ml = rce_dim_multiply(mass, length);
    rce_dimension_t force = rce_dim_divide(ml, time_sq);
    ASSERT(force.exponents[RCE_DIM_LENGTH] == 1, "force length is 1");
    ASSERT(force.exponents[RCE_DIM_MASS] == 1, "force mass is 1");
    ASSERT(force.exponents[RCE_DIM_TIME] == -2, "force time is -2");

    /* Check against named unit */
    rce_dimension_t newton_dim = rce_unit_dimension(RCE_UNIT_NEWTON);
    ASSERT(rce_dim_equal(force, newton_dim), "force equals newton dimension");
    PASS();
}

TEST(dim_energy_test) {
    /* Energy = force * length = [2, 1, -2, 0, 0, 0, 0] */
    rce_dimension_t force = rce_unit_dimension(RCE_UNIT_NEWTON);
    rce_dimension_t length = rce_dim_base(RCE_DIM_LENGTH);
    rce_dimension_t energy = rce_dim_multiply(force, length);
    rce_dimension_t joule = rce_unit_dimension(RCE_UNIT_JOULE);
    ASSERT(rce_dim_equal(energy, joule), "energy equals joule dimension");
    PASS();
}

TEST(dim_power_test) {
    /* Power = energy / time = [2, 1, -3, 0, 0, 0, 0] */
    rce_dimension_t energy = rce_unit_dimension(RCE_UNIT_JOULE);
    rce_dimension_t time = rce_dim_base(RCE_DIM_TIME);
    rce_dimension_t power = rce_dim_divide(energy, time);
    rce_dimension_t watt = rce_unit_dimension(RCE_UNIT_WATT);
    ASSERT(rce_dim_equal(power, watt), "power equals watt dimension");
    PASS();
}

TEST(dim_sqrt_test) {
    /* sqrt(area) = length */
    rce_dimension_t length = rce_dim_base(RCE_DIM_LENGTH);
    rce_dimension_t area = rce_dim_power(length, 2);
    rce_dimension_t result;
    ASSERT(rce_dim_sqrt(area, &result), "sqrt of area succeeds");
    ASSERT(rce_dim_equal(result, length), "sqrt(area) = length");

    /* sqrt(velocity) should fail (odd exponent) */
    rce_dimension_t velocity = rce_dim_divide(
        rce_dim_base(RCE_DIM_LENGTH), rce_dim_base(RCE_DIM_TIME));
    ASSERT(!rce_dim_sqrt(velocity, &result), "sqrt of velocity fails (odd exponent)");
    PASS();
}

TEST(dim_equal_test) {
    rce_dimension_t a = rce_unit_dimension(RCE_UNIT_HERTZ);
    rce_dimension_t b = rce_unit_dimension(RCE_UNIT_BECQUEREL);
    ASSERT(rce_dim_equal(a, b), "Hz and Bq have same dimension (s^-1)");

    rce_dimension_t c = rce_unit_dimension(RCE_UNIT_JOULE);
    rce_dimension_t d = rce_unit_dimension(RCE_UNIT_WATT);
    ASSERT(!rce_dim_equal(c, d), "J and W have different dimensions");
    PASS();
}

/* ===== Unit Dimension Tests ===== */

TEST(unit_dimensions_test) {
    rce_dimension_t volt = rce_unit_dimension(RCE_UNIT_VOLT);
    ASSERT(volt.exponents[RCE_DIM_LENGTH] == 2, "volt length is 2");
    ASSERT(volt.exponents[RCE_DIM_MASS] == 1, "volt mass is 1");
    ASSERT(volt.exponents[RCE_DIM_TIME] == -3, "volt time is -3");
    ASSERT(volt.exponents[RCE_DIM_ELECTRIC_CURRENT] == -1, "volt current is -1");

    rce_dimension_t ohm = rce_unit_dimension(RCE_UNIT_OHM);
    rce_dimension_t volt_over_ampere = rce_dim_divide(
        rce_unit_dimension(RCE_UNIT_VOLT),
        rce_unit_dimension(RCE_UNIT_AMPERE));
    ASSERT(rce_dim_equal(ohm, volt_over_ampere), "ohm = V/A");
    PASS();
}

TEST(unit_names_test) {
    ASSERT(strcmp(rce_unit_name(RCE_UNIT_NEWTON), "newton") == 0, "newton name");
    ASSERT(strcmp(rce_unit_symbol(RCE_UNIT_NEWTON), "N") == 0, "newton symbol");
    ASSERT(strcmp(rce_unit_symbol(RCE_UNIT_HERTZ), "Hz") == 0, "hertz symbol");
    ASSERT(strcmp(rce_unit_name(RCE_UNIT_KELVIN), "kelvin") == 0, "kelvin name");
    PASS();
}

/* ===== Quantity Tests ===== */

TEST(quantity_init_test) {
    rce_quantity_t q;
    rce_quantity_init(&q);
    ASSERT(q.mantissa == 0, "mantissa is 0");
    ASSERT(q.exp10 == 0, "exp10 is 0");
    ASSERT(!q.negative, "not negative");
    ASSERT(!q.has_uncertainty, "no uncertainty");
    ASSERT(rce_dim_is_dimensionless(q.dimension), "dimensionless");
    PASS();
}

TEST(quantity_set_test) {
    rce_quantity_t q;
    rce_dimension_t length = rce_dim_base(RCE_DIM_LENGTH);
    rce_quantity_set(&q, 100, -2, false, length, RCE_UNIT_METER);
    ASSERT(q.mantissa == 100, "mantissa is 100");
    ASSERT(q.exp10 == -2, "exp10 is -2");
    ASSERT(!q.negative, "not negative");
    ASSERT(q.unit == RCE_UNIT_METER, "unit is meter");
    ASSERT(q.dimension.exponents[RCE_DIM_LENGTH] == 1, "length dimension");
    PASS();
}

TEST(quantity_compatible_test) {
    rce_quantity_t a, b;
    rce_quantity_set(&a, 1, 0, false, rce_unit_dimension(RCE_UNIT_JOULE), RCE_UNIT_JOULE);
    rce_quantity_set(&b, 2, 0, false, rce_unit_dimension(RCE_UNIT_WATT), RCE_UNIT_WATT);
    ASSERT(!rce_quantity_compatible(&a, &b), "J and W are not compatible");

    rce_quantity_set(&b, 2, 0, false, rce_unit_dimension(RCE_UNIT_JOULE), RCE_UNIT_JOULE);
    ASSERT(rce_quantity_compatible(&a, &b), "J and J are compatible");
    PASS();
}

TEST(quantity_multiply_test) {
    rce_quantity_t force, length, energy;
    rce_quantity_set(&force, 10, 0, false, rce_unit_dimension(RCE_UNIT_NEWTON), RCE_UNIT_NEWTON);
    rce_quantity_set(&length, 5, 0, false, rce_dim_base(RCE_DIM_LENGTH), RCE_UNIT_METER);

    ASSERT(rce_quantity_multiply(&force, &length, &energy), "multiply succeeds");
    ASSERT(energy.mantissa == 50, "result mantissa is 50");
    ASSERT(rce_dim_equal(energy.dimension, rce_unit_dimension(RCE_UNIT_JOULE)),
           "result dimension is joules");
    PASS();
}

TEST(quantity_divide_test) {
    rce_quantity_t energy, time, power;
    rce_quantity_set(&energy, 100, 0, false, rce_unit_dimension(RCE_UNIT_JOULE), RCE_UNIT_JOULE);
    rce_quantity_set(&time, 4, 0, false, rce_dim_base(RCE_DIM_TIME), RCE_UNIT_SECOND);

    ASSERT(rce_quantity_divide(&energy, &time, &power), "divide succeeds");
    ASSERT(power.mantissa == 25, "result mantissa is 25");
    ASSERT(rce_dim_equal(power.dimension, rce_unit_dimension(RCE_UNIT_WATT)),
           "result dimension is watts");

    /* Division by zero */
    rce_quantity_t zero;
    rce_quantity_set(&zero, 0, 0, false, rce_dim_base(RCE_DIM_TIME), RCE_UNIT_SECOND);
    ASSERT(!rce_quantity_divide(&energy, &zero, &power), "division by zero fails");
    PASS();
}

TEST(quantity_uncertainty_test) {
    rce_quantity_t q;
    rce_quantity_set(&q, 299792458, 0, false,
                     rce_unit_dimension(RCE_UNIT_METER), RCE_UNIT_METER);
    rce_quantity_set_uncertainty(&q, 1, 0);
    ASSERT(q.has_uncertainty, "uncertainty set");
    ASSERT(q.uncertainty_mantissa == 1, "uncertainty mantissa is 1");
    PASS();
}

/* ===== Constant Registry Tests ===== */

TEST(constants_init_test) {
    rce_constant_registry_t reg;
    rce_constants_init(&reg);
    ASSERT(reg.count == 0, "count is 0");
    PASS();
}

TEST(constants_si_defining_test) {
    rce_constant_registry_t reg;
    rce_constants_init(&reg);
    rce_constants_register_si_defining(&reg);
    ASSERT(reg.count == 7, "7 SI defining constants registered");

    rce_constant_t *c = rce_constants_find(&reg, "speed_of_light");
    ASSERT(c != NULL, "speed_of_light found");
    ASSERT(c->is_defining, "is defining constant");
    ASSERT(!c->is_measured, "defining constant has no uncertainty");
    ASSERT(c->value.mantissa == 299792458, "c value is 299792458");
    ASSERT(strcmp(c->source, "SI 2019") == 0, "source is SI 2019");

    c = rce_constants_find(&reg, "planck_constant");
    ASSERT(c != NULL, "planck_constant found");
    ASSERT(c->value.mantissa == 662607015, "h mantissa is 662607015");
    ASSERT(c->value.exp10 == -34, "h exp10 is -34");

    c = rce_constants_find(&reg, "elementary_charge");
    ASSERT(c != NULL, "elementary_charge found");
    ASSERT(c->value.mantissa == 1602176634, "e mantissa is 1602176634");
    ASSERT(c->value.exp10 == -19, "e exp10 is -19");

    c = rce_constants_find(&reg, "nonexistent");
    ASSERT(c == NULL, "nonexistent constant not found");
    PASS();
}

TEST(constants_register_custom_test) {
    rce_constant_registry_t reg;
    rce_constants_init(&reg);

    rce_quantity_t q;
    rce_quantity_set(&q, 96485, 0, false,
                     rce_unit_dimension(RCE_UNIT_COULOMB), RCE_UNIT_COULOMB);
    rce_quantity_set_uncertainty(&q, 33256, -6);

    int32_t idx = rce_constants_register(&reg, "faraday_constant", &q, false, "CODATA");
    ASSERT(idx >= 0, "custom constant registered");

    rce_constant_t *c = rce_constants_get(&reg, (uint32_t)idx);
    ASSERT(c != NULL, "constant retrieved");
    ASSERT(!c->is_defining, "not a defining constant");
    ASSERT(c->is_measured, "is measured (has uncertainty)");
    ASSERT(strcmp(c->source, "CODATA") == 0, "source is CODATA");
    PASS();
}

/* ===== Format Tests ===== */

TEST(dimension_format_test) {
    char buf[64];
    rce_dimension_t velocity = rce_dim_divide(
        rce_dim_base(RCE_DIM_LENGTH), rce_dim_base(RCE_DIM_TIME));
    rce_dimension_format(velocity, buf, sizeof(buf));
    ASSERT(strstr(buf, "m") != NULL, "format contains m");
    ASSERT(strstr(buf, "s^-1") != NULL, "format contains s^-1");
    PASS();
}

TEST(quantity_format_test) {
    char buf[64];
    rce_quantity_t q;
    rce_quantity_set(&q, 299792458, 0, false,
                     rce_unit_dimension(RCE_UNIT_METER), RCE_UNIT_METER);
    rce_quantity_format(&q, buf, sizeof(buf));
    ASSERT(strstr(buf, "299792458") != NULL, "format contains value");
    ASSERT(strstr(buf, "m") != NULL, "format contains unit symbol");
    PASS();
}

/* ===== Main ===== */

int main(void) {
    printf("\n=== ZXV RCE Units & Dimensions Tests ===\n\n");

    RUN(dim_dimensionless_test);
    RUN(dim_base_test);
    RUN(dim_multiply_test);
    RUN(dim_force_test);
    RUN(dim_energy_test);
    RUN(dim_power_test);
    RUN(dim_sqrt_test);
    RUN(dim_equal_test);
    RUN(unit_dimensions_test);
    RUN(unit_names_test);
    RUN(quantity_init_test);
    RUN(quantity_set_test);
    RUN(quantity_compatible_test);
    RUN(quantity_multiply_test);
    RUN(quantity_divide_test);
    RUN(quantity_uncertainty_test);
    RUN(constants_init_test);
    RUN(constants_si_defining_test);
    RUN(constants_register_custom_test);
    RUN(dimension_format_test);
    RUN(quantity_format_test);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);
    if (tests_failed == 0) {
        printf("ALL TESTS PASSED\n");
    }
    return tests_failed > 0 ? 1 : 0;
}
