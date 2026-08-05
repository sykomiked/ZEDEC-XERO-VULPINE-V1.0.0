/* test_rmag.c — Rational Magnitude Engine (K2) Tests
 *
 * Tests for rational arithmetic and resource budget management.
 *
 * Author: 36N9 Genetics, LLC
 * License: SEL-3.3 (kernel component)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "rmag.h"

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

/* ===== Rational Arithmetic Tests ===== */

TEST(rational_from_uint_test) {
    rmag_rational_t r = rmag_rational_from_uint(42);
    ASSERT(r.numerator == 42, "numerator is 42");
    ASSERT(r.denominator == 1, "denominator is 1");
    ASSERT(!r.negative, "not negative");
    PASS();
}

TEST(rational_from_frac_test) {
    rmag_rational_t r = rmag_rational_from_frac(3, 4, false);
    ASSERT(r.numerator == 3, "numerator is 3");
    ASSERT(r.denominator == 4, "denominator is 4");
    ASSERT(!r.negative, "not negative");

    r = rmag_rational_from_frac(1, 2, true);
    ASSERT(r.negative, "is negative");

    /* Zero denominator defaults to 1 */
    r = rmag_rational_from_frac(5, 0, false);
    ASSERT(r.denominator == 1, "zero denominator becomes 1");
    PASS();
}

TEST(rational_is_zero_test) {
    ASSERT(rmag_rational_is_zero(rmag_rational_from_uint(0)), "0 is zero");
    ASSERT(!rmag_rational_is_zero(rmag_rational_from_uint(1)), "1 is not zero");
    ASSERT(rmag_rational_is_zero(rmag_rational_from_frac(0, 5, false)), "0/5 is zero");
    PASS();
}

TEST(rational_equal_test) {
    rmag_rational_t a = rmag_rational_from_frac(1, 2, false);
    rmag_rational_t b = rmag_rational_from_frac(2, 4, false);
    ASSERT(rmag_rational_equal(a, b), "1/2 == 2/4");

    a = rmag_rational_from_frac(3, 4, false);
    b = rmag_rational_from_frac(1, 2, false);
    ASSERT(!rmag_rational_equal(a, b), "3/4 != 1/2");

    a = rmag_rational_from_frac(1, 2, true);
    b = rmag_rational_from_frac(1, 2, false);
    ASSERT(!rmag_rational_equal(a, b), "-1/2 != 1/2");
    PASS();
}

TEST(rational_less_than_test) {
    rmag_rational_t a = rmag_rational_from_frac(1, 3, false);
    rmag_rational_t b = rmag_rational_from_frac(1, 2, false);
    ASSERT(rmag_rational_less_than(a, b), "1/3 < 1/2");
    ASSERT(!rmag_rational_less_than(b, a), "1/2 not < 1/3");

    a = rmag_rational_from_frac(1, 2, true);
    b = rmag_rational_from_frac(1, 2, false);
    ASSERT(rmag_rational_less_than(a, b), "-1/2 < 1/2");
    PASS();
}

TEST(rational_add_test) {
    rmag_rational_t a = rmag_rational_from_frac(1, 4, false);
    rmag_rational_t b = rmag_rational_from_frac(1, 4, false);
    rmag_rational_t c = rmag_rational_add(a, b);
    ASSERT(rmag_rational_equal(c, rmag_rational_from_frac(1, 2, false)), "1/4+1/4=1/2");

    a = rmag_rational_from_frac(1, 3, false);
    b = rmag_rational_from_frac(1, 6, false);
    c = rmag_rational_add(a, b);
    ASSERT(rmag_rational_equal(c, rmag_rational_from_frac(1, 2, false)), "1/3+1/6=1/2");

    /* Negative + positive */
    a = rmag_rational_from_frac(3, 4, true);
    b = rmag_rational_from_frac(1, 4, false);
    c = rmag_rational_add(a, b);
    ASSERT(rmag_rational_equal(c, rmag_rational_from_frac(1, 2, true)), "-3/4+1/4=-1/2");
    PASS();
}

TEST(rational_subtract_test) {
    rmag_rational_t a = rmag_rational_from_frac(1, 2, false);
    rmag_rational_t b = rmag_rational_from_frac(1, 4, false);
    rmag_rational_t c = rmag_rational_subtract(a, b);
    ASSERT(rmag_rational_equal(c, rmag_rational_from_frac(1, 4, false)), "1/2-1/4=1/4");

    /* Subtract to negative */
    a = rmag_rational_from_frac(1, 4, false);
    b = rmag_rational_from_frac(1, 2, false);
    c = rmag_rational_subtract(a, b);
    ASSERT(rmag_rational_equal(c, rmag_rational_from_frac(1, 4, true)), "1/4-1/2=-1/4");
    PASS();
}

TEST(rational_multiply_test) {
    rmag_rational_t a = rmag_rational_from_frac(2, 3, false);
    rmag_rational_t b = rmag_rational_from_frac(3, 4, false);
    rmag_rational_t c = rmag_rational_multiply(a, b);
    ASSERT(rmag_rational_equal(c, rmag_rational_from_frac(1, 2, false)), "2/3*3/4=1/2");

    /* Negative * positive */
    a = rmag_rational_from_frac(2, 3, true);
    b = rmag_rational_from_frac(3, 4, false);
    c = rmag_rational_multiply(a, b);
    ASSERT(c.negative, "negative * positive is negative");
    ASSERT(rmag_rational_equal(c, rmag_rational_from_frac(1, 2, true)), "2/3*3/4=-1/2");
    PASS();
}

TEST(rational_divide_test) {
    rmag_rational_t a = rmag_rational_from_frac(1, 2, false);
    rmag_rational_t b = rmag_rational_from_frac(1, 4, false);
    rmag_rational_t c = rmag_rational_divide(a, b);
    ASSERT(rmag_rational_equal(c, rmag_rational_from_uint(2)), "1/2 / 1/4 = 2");

    /* Division by zero returns 0 */
    b = rmag_rational_from_uint(0);
    c = rmag_rational_divide(a, b);
    ASSERT(rmag_rational_is_zero(c), "division by zero returns 0");
    PASS();
}

TEST(rational_reduce_test) {
    rmag_rational_t r = rmag_rational_from_frac(6, 8, false);
    r = rmag_rational_reduce(r);
    ASSERT(r.numerator == 3, "6/8 reduces to 3/4");
    ASSERT(r.denominator == 4, "6/8 reduces to 3/4");

    r = rmag_rational_from_frac(0, 5, false);
    r = rmag_rational_reduce(r);
    ASSERT(r.numerator == 0, "0/5 reduces to 0/1");
    ASSERT(r.denominator == 1, "0/5 reduces to 0/1");
    PASS();
}

/* ===== Registry Tests ===== */

TEST(registry_init_test) {
    rmag_registry_t reg;
    rmag_registry_init(&reg);
    ASSERT(reg.source_count == 0, "no sources");
    ASSERT(reg.budget_count == 0, "no budgets");
    PASS();
}

TEST(register_source_test) {
    rmag_registry_t reg;
    rmag_registry_init(&reg);
    int32_t idx = rmag_register_source(&reg, "K2-kernel", 2);
    ASSERT(idx >= 0, "source registered");
    ASSERT(idx == 0, "first source is index 0");
    ASSERT(reg.source_count == 1, "count incremented");
    ASSERT(strcmp(reg.sources[0].name, "K2-kernel") == 0, "name matches");
    ASSERT(reg.sources[0].phase_id == 2, "phase is 2");
    ASSERT(reg.sources[0].active, "source is active");
    PASS();
}

TEST(set_source_budget_test) {
    rmag_registry_t reg;
    rmag_registry_init(&reg);
    int32_t idx = rmag_register_source(&reg, "K2-rce", 2);
    rmag_rational_t budget = rmag_rational_from_uint(1000000);
    ASSERT(rmag_set_source_budget(&reg, (uint32_t)idx, RMAG_RES_MEMORY, budget) == 0,
           "budget set");

    rmag_rational_t remaining = rmag_source_remaining(&reg, (uint32_t)idx, RMAG_RES_MEMORY);
    ASSERT(rmag_rational_equal(remaining, budget), "full budget remaining");
    PASS();
}

TEST(source_allocate_test) {
    rmag_registry_t reg;
    rmag_registry_init(&reg);
    int32_t idx = rmag_register_source(&reg, "K2-build", 2);
    rmag_rational_t budget = rmag_rational_from_uint(1000);
    rmag_set_source_budget(&reg, (uint32_t)idx, RMAG_RES_COMPUTE_CYCLES, budget);

    rmag_rational_t amount = rmag_rational_from_uint(300);
    ASSERT(rmag_allocate_from_source(&reg, (uint32_t)idx, RMAG_RES_COMPUTE_CYCLES, amount) == RMAG_RESULT_OK,
           "allocate 300");

    rmag_rational_t remaining = rmag_source_remaining(&reg, (uint32_t)idx, RMAG_RES_COMPUTE_CYCLES);
    ASSERT(rmag_rational_equal(remaining, rmag_rational_from_uint(700)), "700 remaining");

    /* Over-allocate */
    amount = rmag_rational_from_uint(800);
    ASSERT(rmag_allocate_from_source(&reg, (uint32_t)idx, RMAG_RES_COMPUTE_CYCLES, amount) == RMAG_RESULT_DENIED,
           "over-allocation denied");
    PASS();
}

TEST(create_budget_test) {
    rmag_registry_t reg;
    rmag_registry_init(&reg);
    rmag_rational_t limit = rmag_rational_from_uint(500);
    int32_t idx = rmag_create_budget(&reg, "mem-budget", RMAG_RES_MEMORY, limit, true);
    ASSERT(idx >= 0, "budget created");
    ASSERT(reg.budget_count == 1, "count incremented");
    ASSERT(strcmp(reg.budgets[0].name, "mem-budget") == 0, "name matches");
    ASSERT(reg.budgets[0].enforce, "enforce is true");
    PASS();
}

TEST(budget_consume_test) {
    rmag_registry_t reg;
    rmag_registry_init(&reg);
    rmag_rational_t limit = rmag_rational_from_uint(500);
    int32_t idx = rmag_create_budget(&reg, "io-budget", RMAG_RES_IO_OPERATIONS, limit, true);

    /* Consume within limit */
    rmag_rational_t amount = rmag_rational_from_uint(200);
    ASSERT(rmag_consume(&reg, (uint32_t)idx, amount) == RMAG_RESULT_OK, "consume 200 OK");

    rmag_rational_t remaining = rmag_budget_remaining(&reg, (uint32_t)idx);
    ASSERT(rmag_rational_equal(remaining, rmag_rational_from_uint(300)), "300 remaining");

    /* Consume exact remaining */
    ASSERT(rmag_consume(&reg, (uint32_t)idx, rmag_rational_from_uint(300)) == RMAG_RESULT_OK,
           "consume 300 OK");

    /* Over-consume */
    ASSERT(rmag_consume(&reg, (uint32_t)idx, rmag_rational_from_uint(1)) == RMAG_RESULT_DENIED,
           "over-consume denied");
    /* Budget is at exactly limit (denied consume didn't update), so not exceeded */
    ASSERT(!rmag_budget_exceeded(&reg, (uint32_t)idx), "budget at limit, not exceeded");
    PASS();
}

TEST(budget_release_test) {
    rmag_registry_t reg;
    rmag_registry_init(&reg);
    rmag_rational_t limit = rmag_rational_from_uint(500);
    int32_t idx = rmag_create_budget(&reg, "release-test", RMAG_RES_MEMORY, limit, true);

    rmag_consume(&reg, (uint32_t)idx, rmag_rational_from_uint(300));
    ASSERT(rmag_release(&reg, (uint32_t)idx, rmag_rational_from_uint(100)) == RMAG_RESULT_OK,
           "release 100 OK");

    rmag_rational_t remaining = rmag_budget_remaining(&reg, (uint32_t)idx);
    ASSERT(rmag_rational_equal(remaining, rmag_rational_from_uint(300)), "300 remaining after release");

    /* Release more than consumed — clamps to 0 */
    ASSERT(rmag_release(&reg, (uint32_t)idx, rmag_rational_from_uint(9999)) == RMAG_RESULT_OK,
           "release excess OK");
    remaining = rmag_budget_remaining(&reg, (uint32_t)idx);
    ASSERT(rmag_rational_equal(remaining, limit), "full budget after clamp");
    PASS();
}

TEST(budget_no_enforce_test) {
    rmag_registry_t reg;
    rmag_registry_init(&reg);
    rmag_rational_t limit = rmag_rational_from_uint(100);
    int32_t idx = rmag_create_budget(&reg, "no-enforce", RMAG_RES_EVENT_COUNT, limit, false);

    /* Consume over limit without enforce — should succeed */
    ASSERT(rmag_consume(&reg, (uint32_t)idx, rmag_rational_from_uint(200)) == RMAG_RESULT_OK,
           "consume over limit without enforce OK");
    ASSERT(rmag_budget_exceeded(&reg, (uint32_t)idx), "budget exceeded but not denied");
    ASSERT(rmag_rational_is_zero(rmag_budget_remaining(&reg, (uint32_t)idx)), "remaining is 0");
    PASS();
}

TEST(budget_not_found_test) {
    rmag_registry_t reg;
    rmag_registry_init(&reg);
    ASSERT(rmag_consume(&reg, 0, rmag_rational_from_uint(1)) == RMAG_RESULT_NOT_FOUND,
           "nonexistent budget returns not_found");
    ASSERT(rmag_release(&reg, 99, rmag_rational_from_uint(1)) == RMAG_RESULT_NOT_FOUND,
           "nonexistent budget release returns not_found");
    PASS();
}

TEST(rational_fractions_consume_test) {
    rmag_registry_t reg;
    rmag_registry_init(&reg);
    rmag_rational_t limit = rmag_rational_from_frac(7, 2, false);  /* 3.5 */
    int32_t idx = rmag_create_budget(&reg, "frac-budget", RMAG_RES_ENERGY, limit, true);

    /* Consume 1/2 four times = 2.0 */
    for (int i = 0; i < 4; i++) {
        ASSERT(rmag_consume(&reg, (uint32_t)idx, rmag_rational_from_frac(1, 2, false)) == RMAG_RESULT_OK,
               "consume 1/2 OK");
    }

    /* Consume 3/2 = 1.5, total = 3.5 = limit */
    ASSERT(rmag_consume(&reg, (uint32_t)idx, rmag_rational_from_frac(3, 2, false)) == RMAG_RESULT_OK,
           "consume 3/2 reaches limit exactly");

    /* Any more should be denied */
    ASSERT(rmag_consume(&reg, (uint32_t)idx, rmag_rational_from_frac(1, 100, false)) == RMAG_RESULT_DENIED,
           "tiny over-consume denied");
    PASS();
}

TEST(name_functions_test) {
    ASSERT(strcmp(rmag_resource_name(RMAG_RES_MEMORY), "memory") == 0, "memory name");
    ASSERT(strcmp(rmag_resource_name(RMAG_RES_COMPUTE_CYCLES), "compute_cycles") == 0, "compute name");
    ASSERT(strcmp(rmag_resource_name(RMAG_RES_NESTING_DEPTH), "nesting_depth") == 0, "nesting name");
    ASSERT(strcmp(rmag_result_name(RMAG_RESULT_OK), "ok") == 0, "ok name");
    ASSERT(strcmp(rmag_result_name(RMAG_RESULT_DENIED), "denied") == 0, "denied name");
    PASS();
}

/* ===== Main ===== */

int main(void) {
    printf("\n=== ZXV RMAG (K2) Tests ===\n\n");

    RUN(rational_from_uint_test);
    RUN(rational_from_frac_test);
    RUN(rational_is_zero_test);
    RUN(rational_equal_test);
    RUN(rational_less_than_test);
    RUN(rational_add_test);
    RUN(rational_subtract_test);
    RUN(rational_multiply_test);
    RUN(rational_divide_test);
    RUN(rational_reduce_test);
    RUN(registry_init_test);
    RUN(register_source_test);
    RUN(set_source_budget_test);
    RUN(source_allocate_test);
    RUN(create_budget_test);
    RUN(budget_consume_test);
    RUN(budget_release_test);
    RUN(budget_no_enforce_test);
    RUN(budget_not_found_test);
    RUN(rational_fractions_consume_test);
    RUN(name_functions_test);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);
    if (tests_failed == 0) {
        printf("ALL TESTS PASSED\n");
    }
    return tests_failed > 0 ? 1 : 0;
}
