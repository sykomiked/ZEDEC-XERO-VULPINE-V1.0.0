/* test_financial_fabric.c — rails must consult the form table and the
 * state-reserved (inalienable) forms must never change hands.
 *
 * Regression for the old ff_rail_supports_form(), which returned true for
 * every form on every rail ("Simplified"), took a uint8_t rail id that could
 * not hold 888, and let ff_transfer_capital move Crown forms. Each check
 * below fails on that code.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include <stdio.h>
#include <string.h>
#include "financial_fabric.h"

static int fails = 0;
#define CK(c, m)                                                                                   \
    do {                                                                                           \
        if (c)                                                                                     \
            printf("  ok   %s\n", m);                                                              \
        else {                                                                                     \
            printf("  FAIL %s\n", m);                                                              \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static financial_fabric_t ff;

int main(void)
{
    printf("=== financial_fabric: form -> rail table, inalienable forms ===\n");
    rail_system_t *r = &ff.rails;
    ff_init(&ff, NULL);

    /* Table (finance/capital_forms.h RAIL_* codes): Financial, Material,
     * Living, Built on RAIL_FINANCIAL; Knowledge on RAIL_PROVENANCE;
     * state-reserved on RAIL_PROVENANCE / RAIL_EXTERNALITY only. */
    int table_ok = 1;
    for (uint8_t f = 1; f <= 9; f++) {
        bool on_fin = ff_rail_supports_form(r, f, RAIL_FINANCIAL);
        bool on_prov = ff_rail_supports_form(r, f, RAIL_PROVENANCE);
        bool on_ext = ff_rail_supports_form(r, f, RAIL_EXTERNALITY);
        bool want_fin = (f == 5 || f == 6 || f == 7 || f == 9);
        bool want_prov = (f <= 4 || f == 8);
        bool want_ext = (f <= 4);
        if (on_fin != want_fin || on_prov != want_prov || on_ext != want_ext) table_ok = 0;
    }
    CK(table_ok, "every form maps to exactly its rails");
    CK(!ff_rail_supports_form(r, 5, 5) && !ff_rail_supports_form(r, 5, 0) &&
           !ff_rail_supports_form(r, 0, RAIL_FINANCIAL) &&
           !ff_rail_supports_form(r, 10, RAIL_FINANCIAL),
       "unknown rails and forms refused");
    CK(!ff_rail_supports_form(r, 1, RAIL_FINANCIAL), "state-reserved form never on the debit rail");
    CK(RAIL_EXTERNALITY > 255 && strcmp(ff_rail_name(RAIL_EXTERNALITY), "Unknown") != 0 &&
           strcmp(ff_rail_name(10), "Unknown") == 0,
       "rail 888 is addressable (uint16_t)");

    word168_t owner;
    memset(&owner, 0, sizeof owner);
    uint64_t bal[9] = {100, 100, 100, 100, 100, 100, 100, 100, 100};
    int32_t a = ff_create_account(&ff, "alice", &owner, bal);
    int32_t b = ff_create_account(&ff, "bob", &owner, bal);
    CK(a >= 0 && b >= 0, "accounts created");
    ff_account_t *A = ff_get_account(&ff, (uint32_t) a);
    ff_account_t *B = ff_get_account(&ff, (uint32_t) b);

    int crown_ok = 1;
    for (uint8_t f = 1; f <= 4; f++) {
        if (ff_transfer_capital(&ff, (uint32_t) a, (uint32_t) b, f, 10) != FF_EINALIENABLE)
            crown_ok = 0;
        if (ff_route_through_rail(&ff, (uint32_t) a, (uint32_t) b, f, 10, RAIL_PROVENANCE) !=
            FF_EINALIENABLE)
            crown_ok = 0;
        if (ff_route_through_rail(&ff, (uint32_t) a, (uint32_t) b, f, 10, RAIL_EXTERNALITY) !=
            FF_EINALIENABLE)
            crown_ok = 0;
        if (A->balances[f - 1] != 100 || B->balances[f - 1] != 100) crown_ok = 0;
    }
    CK(crown_ok, "state-reserved forms 1..4 refused on every path, balances untouched");
    CK(ff_initiate_settlement(&ff, 1, (uint32_t) a, (uint32_t) b, 2, 10, 1, 0) == FF_EINALIENABLE &&
           A->balances[1] == 100,
       "mesh settlement of a state-reserved form refused");
    CK(ff_create_order_book(&ff, "SOC/FIN", 1, 5) == FF_EINALIENABLE,
       "no market in a state-reserved form");

    CK(ff_route_through_rail(&ff, (uint32_t) a, (uint32_t) b, 5, 10, RAIL_PROVENANCE) == FF_ERAIL &&
           A->balances[4] == 100,
       "Financial on the provenance rail refused");
    CK(ff_route_through_rail(&ff, (uint32_t) a, (uint32_t) b, 5, 10, RAIL_FINANCIAL) == 0 &&
           A->balances[4] == 90 && B->balances[4] == 110,
       "Financial on the debit rail settles");
    CK(ff_route_through_rail(&ff, (uint32_t) a, (uint32_t) b, 8, 10, RAIL_FINANCIAL) == FF_ERAIL,
       "Knowledge on the debit rail refused");
    CK(ff_route_through_rail(&ff, (uint32_t) a, (uint32_t) b, 8, 10, RAIL_PROVENANCE) == 0 &&
           B->balances[7] == 110,
       "Knowledge on the provenance rail settles");
    CK(ff_bridge_asset(&ff, (uint32_t) a, 5, 10, "eth", "0x0") == FF_ENOTSUP &&
           A->balances[4] == 90,
       "bridge fails closed without debiting");

    printf("=== %s (%d failure(s)) ===\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
