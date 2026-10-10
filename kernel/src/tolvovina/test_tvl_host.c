/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_tvl_host.c — run the four TOL VOVINA UPAAH LOT self-checks on the host.
 *
 * Until now these ran only at arm64 boot, so nothing in verify-all noticed if
 * one regressed. This builds the same freestanding sources WITHOUT TEST_HOST
 * (tvl_geom requires the integer Q32.32 surplus_real_t) and requires 4/4. */
#include <stdio.h>
#include "tvl_bringup.h"

static void out(const char *s)
{
    fputs(s, stdout);
}

int main(void)
{
    unsigned n = tvl_bringup(out);
    printf("\ntest_tvl_host: %u/4 modules self-checked\n", n);
    return n == 4u ? 0 : 1;
}
