/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_rmag_budget.c — K2 budget enforcement cannot be bypassed by 64-bit
 * wraparound or by "consuming" a negative amount, and rational comparison is
 * exact where the cross products overflow 64 bits. */
#include <stdio.h>
#include "rmag.h"

static int failures;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (c) {                                                                                   \
            printf("[PASS] %s\n", m);                                                              \
        } else {                                                                                   \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static rmag_registry_t reg;

int main(void)
{
    rmag_registry_init(&reg);
    int32_t b =
        rmag_create_budget(&reg, "mem", RMAG_RES_MEMORY, rmag_rational_from_uint(1000), true);
    CHECK(b >= 0, "budget created");
    CHECK(rmag_consume(&reg, (uint32_t) b, rmag_rational_from_uint(500)) == RMAG_RESULT_OK,
          "consume 500 of 1000");
    CHECK(rmag_consume(&reg, (uint32_t) b, rmag_rational_from_uint(UINT64_MAX - 399)) ==
              RMAG_RESULT_DENIED,
          "a huge amount that would wrap the sum is denied");
    CHECK(rmag_consume(&reg, (uint32_t) b, rmag_rational_from_frac(900, 1, true)) ==
              RMAG_RESULT_INVALID,
          "negative consumption refused");
    CHECK(rmag_release(&reg, (uint32_t) b, rmag_rational_from_frac(900, 1, true)) ==
              RMAG_RESULT_INVALID,
          "negative release refused");
    CHECK(rmag_rational_equal(rmag_budget_remaining(&reg, (uint32_t) b),
                              rmag_rational_from_uint(500)),
          "500 remains after the refused calls");
    CHECK(rmag_consume(&reg, (uint32_t) b, rmag_rational_from_uint(501)) == RMAG_RESULT_DENIED,
          "501 more is still over the limit");
    CHECK(rmag_consume(&reg, (uint32_t) b, rmag_rational_from_uint(500)) == RMAG_RESULT_OK,
          "exactly the remaining 500 is allowed");

    /* 2^40/(2^41+1) vs (2^40+1)/(2^41+3): both cross products exceed 2^64 */
    rmag_rational_t x = rmag_rational_from_frac(1ull << 40, (1ull << 41) + 1, false);
    rmag_rational_t y = rmag_rational_from_frac((1ull << 40) + 1, (1ull << 41) + 3, false);
    /* x = 0.49999999999977..., y = 0.49999999999977...+: x < y */
    CHECK(rmag_rational_less_than(x, y) && !rmag_rational_less_than(y, x),
          "exact comparison where num*den overflows 64 bits");
    CHECK(rmag_rational_equal(rmag_rational_from_frac(6, 4, false),
                              rmag_rational_from_frac(3, 2, false)),
          "6/4 == 3/2");
    CHECK(!rmag_rational_equal(rmag_rational_from_frac(3, 2, true),
                               rmag_rational_from_frac(3, 2, false)),
          "-3/2 != 3/2");

    rmag_rational_t big = rmag_rational_multiply(rmag_rational_from_uint(1ull << 40),
                                                 rmag_rational_from_uint(1ull << 40));
    CHECK(big.numerator == UINT64_MAX && big.denominator == 1, "overflowing product saturates");
    rmag_rational_t s = rmag_rational_add(rmag_rational_from_frac(1, 3, false),
                                          rmag_rational_from_frac(1, 6, false));
    CHECK(s.numerator == 1 && s.denominator == 2, "1/3 + 1/6 = 1/2");
    rmag_rational_t d =
        rmag_rational_subtract(rmag_rational_from_uint(2), rmag_rational_from_uint(5));
    CHECK(d.negative && d.numerator == 3 && d.denominator == 1, "2 - 5 = -3");

    printf("%s: %d failure(s)\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
