/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_oc_sutra.c — the real Sutra adapter (orbital_compat_sutra.c) carries its
 * logic state as an lpres_state_t, the type of oc_sutra_src_t.logic_state.
 *
 * The adapter once kept a private enum with NEITHER at 3, which is
 * LPRES_STATE_BOTH: lower dropped a BOTH state as if it were unknown and kept
 * NEITHER, and lift turned a missing state into BOTH. The NEITHER and BOTH
 * cases below fail against that encoding.
 *
 * Built and run by `make -C kernel verify-all`, next to test_orbital_compat. */
#include <stdio.h>
#include "orbital_compat.h"

int32_t sutra_lower(const void *src, uint32_t len, oc_ir_t *out);
int32_t sutra_lift(const oc_ir_t *ir, void *out, uint32_t cap);

static int failures = 0;
static int checks = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

static lpres_state_t round_trip(lpres_state_t s, uint32_t *fields)
{
    oc_sutra_src_t in = {.num = 7, .den = 3, .logic_state = s, .scale = 0};
    oc_ir_t ir;
    oc_sutra_src_t back = {.num = 0, .den = 0, .logic_state = LPRES_STATE_TRUE, .scale = 0};
    if (sutra_lower(&in, sizeof in, &ir) != OC_OK) return (lpres_state_t) -1;
    *fields = ir.num_fields;
    if (sutra_lift(&ir, &back, sizeof back) != (int32_t) sizeof back) return (lpres_state_t) -1;
    return back.logic_state;
}

int main(void)
{
    uint32_t n = 0;

    CHECK(round_trip(LPRES_STATE_NEITHER, &n) == LPRES_STATE_NEITHER && n == 1,
          "NEITHER is the absent state: no logic field, lifts back as NEITHER");
    CHECK(round_trip(LPRES_STATE_BOTH, &n) == LPRES_STATE_BOTH && n == 2,
          "BOTH is carried as a field and lifts back as BOTH");
    CHECK(round_trip(LPRES_STATE_TRUE, &n) == LPRES_STATE_TRUE && n == 2, "TRUE round-trips");
    CHECK(round_trip(LPRES_STATE_FALSE, &n) == LPRES_STATE_FALSE && n == 2, "FALSE round-trips");

    printf("\n%d checks, %s: %d failure(s)\n", checks, failures ? "*** FAILED ***" : "ALL PASS",
           failures);
    return failures ? 1 : 0;
}
