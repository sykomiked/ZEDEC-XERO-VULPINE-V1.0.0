/* test_rur.c — RUR Type System Tests
 *
 * Tests for the Root Universal Representation type system.
 *
 * Author: 36N9 Genetics, LLC
 * License: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "rur.h"

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

/* ===== Tests ===== */

TEST(rur_init_test) {
    rur_registry_t reg;
    rur_registry_init(&reg);
    ASSERT(reg.count == 0, "registry count is 0");
    PASS();
}

TEST(rur_register_module_test) {
    rur_registry_t reg;
    rur_registry_init(&reg);
    int32_t idx = rur_register_module(&reg, "test-mod", "sutra", "v1");
    ASSERT(idx >= 0, "module registered");
    ASSERT(idx == 0, "first module is index 0");
    ASSERT(reg.count == 1, "count incremented");

    rur_module_t *mod = rur_get_module(&reg, 0);
    ASSERT(mod != NULL, "module retrieved");
    ASSERT(strcmp(mod->name, "test-mod") == 0, "name matches");
    ASSERT(strcmp(mod->source_language, "sutra") == 0, "language matches");
    ASSERT(mod->active, "module is active");
    ASSERT(mod->rur_version == 1, "rur version is 1");
    PASS();
}

TEST(rur_beam_test) {
    rur_registry_t reg;
    rur_registry_init(&reg);
    int32_t idx = rur_register_module(&reg, "beam-test", "cobol", "ibm-cobol");
    rur_module_t *mod = rur_get_module(&reg, (uint32_t)idx);

    ASSERT(rur_set_beam(mod, RUR_BEAM_RECORD_TRANSACTION, true), "set record beam");
    ASSERT(mod->beams[RUR_BEAM_RECORD_TRANSACTION], "record beam enabled");
    ASSERT(!mod->beams[RUR_BEAM_ARRAY_RANK], "array beam not enabled");

    ASSERT(rur_set_beam(mod, RUR_BEAM_EVENT_CONTRACT, true), "set event beam");
    ASSERT(mod->beams[RUR_BEAM_EVENT_CONTRACT], "event beam enabled");

    /* Test invalid beam */
    ASSERT(!rur_set_beam(mod, RUR_BEAM_COUNT, true), "invalid beam rejected");
    PASS();
}

TEST(rur_triad_digests_test) {
    rur_registry_t reg;
    rur_registry_init(&reg);
    int32_t idx = rur_register_module(&reg, "digest-test", "fortran", "f2018");
    rur_module_t *mod = rur_get_module(&reg, (uint32_t)idx);

    uint8_t s_plus[32], s_minus[32], s_zero[32];
    memset(s_plus, 0xAA, 32);
    memset(s_minus, 0xBB, 32);
    memset(s_zero, 0xCC, 32);

    ASSERT(rur_set_triad_digests(mod, s_plus, s_minus, s_zero, true), "set triad digests");
    ASSERT(mod->s_plus_digest[0] == 0xAA, "s+ digest correct");
    ASSERT(mod->s_minus_digest[0] == 0xBB, "s- digest correct");
    ASSERT(mod->s_zero_digest[0] == 0xCC, "s0 digest correct");
    ASSERT(mod->has_s_zero, "has_s_zero flag set");

    /* Test without S0 */
    ASSERT(rur_set_triad_digests(mod, s_plus, s_minus, NULL, false), "set without S0");
    ASSERT(!mod->has_s_zero, "has_s_zero flag cleared");
    ASSERT(mod->s_zero_digest[0] == 0x00, "s0 digest zeroed");
    PASS();
}

TEST(rur_value_init_test) {
    rur_value_t val;
    rur_value_init(&val);
    ASSERT(val.scalar_type == RUR_SCALAR_NONE, "scalar type is none");
    ASSERT(val.agg_type == RUR_AGG_NONE, "agg type is none");
    ASSERT(val.is_mutable, "default mutable");
    ASSERT(val.is_deterministic, "default deterministic");
    PASS();
}

TEST(rur_scalar_init_test) {
    rur_value_t val;
    rur_value_init_scalar(&val, RUR_SCALAR_SINT, 32);
    ASSERT(val.scalar_type == RUR_SCALAR_SINT, "type is sint");
    ASSERT(val.int_width == 32, "width is 32");
    ASSERT(val.is_mutable, "mutable by default");

    rur_value_init_scalar(&val, RUR_SCALAR_FLOAT, 64);
    ASSERT(val.scalar_type == RUR_SCALAR_FLOAT, "type is float");
    ASSERT(val.float_format == 0, "float format not set by scalar init");
    PASS();
}

TEST(rur_array_init_test) {
    rur_value_t val;
    uint32_t shape[] = {3, 4, 5};
    rur_value_init_array(&val, RUR_SCALAR_FLOAT, 3, shape);
    ASSERT(val.agg_type == RUR_AGG_ARRAY, "agg type is array");
    ASSERT(val.element_type == RUR_SCALAR_FLOAT, "element type is float");
    ASSERT(val.rank == 3, "rank is 3");
    ASSERT(val.shape[0] == 3, "shape[0] is 3");
    ASSERT(val.shape[1] == 4, "shape[1] is 4");
    ASSERT(val.shape[2] == 5, "shape[2] is 5");
    ASSERT(val.origin[0] == 0, "origin[0] is 0 (default)");
    PASS();
}

TEST(rur_decimal_init_test) {
    rur_value_t val;
    rur_value_init_decimal(&val, 18, 2, RUR_ROUND_NEAREST);
    ASSERT(val.scalar_type == RUR_SCALAR_DECIMAL, "type is decimal");
    ASSERT(val.precision == 18, "precision is 18");
    ASSERT(val.scale == 2, "scale is 2");
    ASSERT(val.rounding == RUR_ROUND_NEAREST, "rounding is nearest");
    ASSERT(val.overflow_policy == RUR_OVERFLOW_ERROR, "overflow is error");
    PASS();
}

TEST(rur_conversion_safe_test) {
    rur_value_t from, to;

    /* int32 to int64: safe (widening) */
    rur_value_init_scalar(&from, RUR_SCALAR_SINT, 32);
    rur_value_init_scalar(&to, RUR_SCALAR_SINT, 64);
    ASSERT(rur_conversion_is_safe(&from, &to), "int32 to int64 is safe");

    /* int64 to int32: NOT safe (narrowing) */
    rur_value_init_scalar(&from, RUR_SCALAR_SINT, 64);
    rur_value_init_scalar(&to, RUR_SCALAR_SINT, 32);
    ASSERT(!rur_conversion_is_safe(&from, &to), "int64 to int32 is not safe");

    /* decimal to float: NOT safe (lossy) */
    rur_value_init_decimal(&from, 18, 2, RUR_ROUND_NEAREST);
    rur_value_init_scalar(&to, RUR_SCALAR_FLOAT, 64);
    to.float_format = 64;
    ASSERT(!rur_conversion_is_safe(&from, &to), "decimal to float is not safe");

    /* decimal to rational: safe (exact) */
    rur_value_init_decimal(&from, 18, 2, RUR_ROUND_NEAREST);
    rur_value_init_scalar(&to, RUR_SCALAR_RATIONAL, 0);
    ASSERT(rur_conversion_is_safe(&from, &to), "decimal to rational is safe");

    /* capability handle: never convertible */
    rur_value_init_scalar(&from, RUR_SCALAR_CAPABILITY, 0);
    rur_value_init_scalar(&to, RUR_SCALAR_SINT, 32);
    ASSERT(!rur_conversion_is_safe(&from, &to), "capability to int is not safe");
    PASS();
}

TEST(rur_names_test) {
    ASSERT(strcmp(rur_beam_name(RUR_BEAM_RECORD_TRANSACTION), "record_transaction") == 0, "beam name");
    ASSERT(strcmp(rur_beam_name(RUR_BEAM_ARRAY_RANK), "array_rank") == 0, "beam name 2");
    ASSERT(strcmp(rur_scalar_type_name(RUR_SCALAR_DECIMAL), "decimal") == 0, "scalar name");
    ASSERT(strcmp(rur_aggregate_type_name(RUR_AGG_ARRAY), "array") == 0, "agg name");
    ASSERT(strcmp(rur_logic_state_name(RUR_LOGIC_BOTH), "contradiction") == 0, "logic name");
    ASSERT(strcmp(rur_op_family_name(RUR_OP_EVENT_EMIT), "event_emit") == 0, "op name");
    PASS();
}

TEST(rur_max_modules_test) {
    rur_registry_t reg;
    rur_registry_init(&reg);
    char name[32];

    for (uint32_t i = 0; i < RUR_MAX_MODULES; i++) {
        snprintf(name, sizeof(name), "mod-%u", i);
        int32_t idx = rur_register_module(&reg, name, "sutra", "v1");
        ASSERT(idx >= 0, "module registered");
    }

    /* Next should fail */
    int32_t idx = rur_register_module(&reg, "overflow", "sutra", "v1");
    ASSERT(idx < 0, "overflow module rejected");
    ASSERT(reg.count == RUR_MAX_MODULES, "count at max");
    PASS();
}

/* ===== Main ===== */

int main(void) {
    printf("\n=== ZXV RUR Type System Tests ===\n\n");

    RUN(rur_init_test);
    RUN(rur_register_module_test);
    RUN(rur_beam_test);
    RUN(rur_triad_digests_test);
    RUN(rur_value_init_test);
    RUN(rur_scalar_init_test);
    RUN(rur_array_init_test);
    RUN(rur_decimal_init_test);
    RUN(rur_conversion_safe_test);
    RUN(rur_names_test);
    RUN(rur_max_modules_test);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);
    if (tests_failed == 0) {
        printf("ALL TESTS PASSED\n");
    }
    return tests_failed > 0 ? 1 : 0;
}
