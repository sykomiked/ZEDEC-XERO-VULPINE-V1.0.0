/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include "rational.h"
int main(void)
{
    assert(!rat_make(1, 0).valid);
    assert(!rat_make(INT64_MIN, 1).valid);
    assert(!rat_make(1, INT64_MIN).valid);
    assert(!rat_add(rat_make(INT64_MAX, 1), rat_make(1, 1)).valid);
    assert(!rat_mul(rat_make(INT64_MAX, 1), rat_make(2, 1)).valid);
    assert(rat_cmp(rat_make(INT64_MAX, INT64_MAX - 1), rat_make(INT64_MAX - 1, INT64_MAX)) == 1);
    rat_t r = rat_mul(rat_make(INT64_MAX, 2), rat_make(2, INT64_MAX));
    assert(r.num == 1 && r.den == 1);
    r = rat_make(INT64_MIN, 2);
    assert(r.valid && r.num == INT64_MIN / 2 && r.den == 1);
    r = rat_make(INT64_MIN, INT64_MIN);
    assert(r.valid && r.num == 1 && r.den == 1);
    r = rat_make(0, INT64_MIN);
    assert(r.valid && r.num == 0 && r.den == 1);
    puts("rational: 10 sanitizer boundary checks passed");
    return 0;
}
