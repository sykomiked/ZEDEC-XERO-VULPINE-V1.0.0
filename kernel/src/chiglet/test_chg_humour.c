/* test_chg_humour.c — the Chiglet humour engine, host tests.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * Proves: the quip is deterministic (same outcome+tick+phase -> same line),
 * outcome-sensitive (DECIDED vs UNCERTAIN differ), bounded (never NULL,
 * never longer than CHG_QUIP_LEN), and safe (UNAVAILABLE -> the neutral
 * line, never a joke).
 */
#include <stdio.h>
#include <string.h>

#include "chg_humour.h"

static int checks = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL line %d: %s\n", __LINE__, #cond); \
        return 1; \
    } \
    checks++; \
} while (0)

int main(void){
    chg_result_t r;

    memset(&r, 0, sizeof r);
    r.state = CHG_DECIDED; r.reason = CHG_REASON_NONE; r.label = 1u;
    r.margin = 42; r.k_valid = 4u; r.k_distinct = 4u;

    /* deterministic: same outcome + tick + phase -> same line */
    {
        const char *a = chg_quip(&r, 144u, 3u);
        const char *b = chg_quip(&r, 144u, 3u);
        CHECK(a && b && strcmp(a, b) == 0);
        CHECK(strlen(a) < CHG_QUIP_LEN);
    }

    /* outcome-sensitive: decided vs uncertain differ at the same tick */
    {
        const char *d = chg_quip(&r, 7u, 1u);
        r.state = CHG_UNCERTAIN; r.reason = CHG_REASON_NO_EVIDENCE;
        const char *u = chg_quip(&r, 7u, 1u);
        CHECK(strcmp(d, u) != 0);
    }

    /* reasons route to their own lines: at a fixed (tick, phase) the decided
     * and uncertain families are disjoint and each is stable. */
    {
        const char *decided_q = chg_quip(&r, 9u, 9u);
        r.state = CHG_UNCERTAIN; r.reason = CHG_REASON_REDUNDANT;
        const char *red_q = chg_quip(&r, 9u, 9u);
        r.reason = CHG_REASON_NO_MODEL;
        const char *nom_q = chg_quip(&r, 9u, 9u);
        r.reason = CHG_REASON_NO_EVIDENCE;
        const char *noe_q = chg_quip(&r, 9u, 9u);
        CHECK(strcmp(decided_q, red_q) != 0);
        CHECK(strcmp(red_q, nom_q) != 0);
        CHECK(strcmp(nom_q, noe_q) != 0);
        CHECK(strcmp(chg_quip(&r, 9u, 9u), noe_q) == 0);  /* stable        */
    }

    /* safe: UNAVAILABLE -> the neutral line, never a joke */
    r.state = CHG_UNAVAILABLE;
    CHECK(strcmp(chg_quip(&r, 1u, 1u), "Processing complete.") == 0);

    /* null result -> neutral */
    CHECK(strcmp(chg_quip(NULL, 1u, 1u), "Processing complete.") == 0);

    /* state quips are bounded and non-null */
    {
        const char *q = chg_quip_state(CHG_UNCERTAIN, 42u);
        CHECK(q && strlen(q) < CHG_QUIP_LEN);
        CHECK(strlen(chg_quip_state(CHG_DECIDED, 42u)) < CHG_QUIP_LEN);
        CHECK(strlen(chg_quip_state(CHG_UNAVAILABLE, 42u)) < CHG_QUIP_LEN);
    }

    printf("chg_humour: %d checks passed\n", checks);
    return 0;
}