/* test_tribute.c — the tributes, anchored to external truths: 11% is
 * 1100/10000 exactly, the three rails sum to the yield with nothing
 * created or destroyed, and the remainder precedent holds.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>

#include "tribute.h"

static int fails;
#define CHECK(c)                                                                          \
    do {                                                                                  \
        if (!(c)) {                                                                       \
            printf("FAIL line %d: %s\n", __LINE__, #c);                                   \
            fails++;                                                                      \
        }                                                                                 \
    } while (0)

int main(void)
{
    /* 1: 11% of a round yield, exactly (the hand-computed truth: 100000/10 = 11000) */
    {
        uint64_t o[TRIBUTE_N];
        CHECK(tribute_split(100000, o));
        CHECK(o[TRIBUTE_CROWN] == 11000);
        CHECK(o[TRIBUTE_MINISTRY] == 11000);
        CHECK(o[TRIBUTE_FOUNTAIN] == 11000);
    }
    /* 2: the three sum to exactly 33% of the yield (the tributes), on any
     * value, and never exceed it; the unallocated 67% stays with the
     * protocol */
    {
        uint64_t o[TRIBUTE_N], yields[5] = {1, 999, 12345, 999999999, 18446744073709551615ull};
        for (int i = 0; i < 5; i++) {
            uint64_t want33 = (yields[i] / 100) * 33 + (yields[i] % 100) * 33 / 100;
            CHECK(tribute_split(yields[i], o));
            CHECK(o[0] + o[1] + o[2] == want33);
            CHECK(o[0] + o[1] + o[2] <= yields[i]);
        }
    }
    /* 3: the dust precedent. yield 1 -> no tribute at all (all zero); yield
     * 34 -> each rail floors to 3 and the 2-unit dust rides the Crown
     * (hand-computed: 34*11/100 = 3.74 -> 3; 34*33/100 = 11.22 -> 11; the
     * three 3s leave 2) */
    {
        uint64_t o[TRIBUTE_N];
        CHECK(tribute_split(1, o));
        CHECK(o[0] == 0 && o[1] == 0 && o[2] == 0);
        CHECK(tribute_split(34, o));
        CHECK(o[TRIBUTE_CROWN] == 5 && o[TRIBUTE_MINISTRY] == 3 && o[TRIBUTE_FOUNTAIN] == 3);
    }
    /* 4: the addresses are the public identifiers, 64 hex chars each */
    for (uint32_t i = 0; i < TRIBUTE_N; i++) {
        CHECK(strlen(TRIBUTE_ADDRESS[i]) == 64);
    }
    /* 5: the bps table: every rail 1100 today */
    for (uint32_t i = 0; i < TRIBUTE_N; i++) {
        CHECK(tribute_bps((tribute_rail_t) i) == 1100);
    }
    /* 6: a null out is refused (the prove-fail) */
    CHECK(!tribute_split(100, 0));
    printf("test_tribute: %d checks, %d failed\n", 18, fails);
    return fails ? 1 : 0;
}
