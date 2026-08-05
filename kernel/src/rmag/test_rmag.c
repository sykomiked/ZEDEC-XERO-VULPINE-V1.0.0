/*
 * test_rmag.c — Rational Magnitude Engine (RMAG) test
 * Author: H.M. Michael-Laurence: Curzi (c)
 */

#include "m5_types.h"
#include "rmag_core.h"
#include <assert.h>
#include <stdio.h>

int main() {
    rmag_init(1024);

    rational_t one_third = {1, 3};
    rational_t one = {1, 1};

    for (ordinal_t i = 0; i < 3; i++) {
        rmag_set_quota(i, one_third);
    }

    rational_t sum = {0, 1};
    for (ordinal_t i = 0; i < 3; i++) {
        sum = rmag_add_quotas(sum, rmag_get_quota(i));
    }

    assert(sum.num == one.num && sum.den == one.den);

    printf("1/3 + 1/3 + 1/3 == 1/1 exactly\n");

    return 0;
}