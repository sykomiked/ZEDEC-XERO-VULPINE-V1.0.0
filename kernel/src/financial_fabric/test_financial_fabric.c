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

/* ff_* forms are 1-based: canonical capital_form_t (== zcap_form_t) + 1. */
#define F1(form) ((uint8_t) ((form) + 1))

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
        capital_form_t cf = (capital_form_t) (f - 1);
        bool reserved = cf == CAPITAL_SOCIAL || cf == CAPITAL_NATURAL ||
                        cf == CAPITAL_HERITAGE_INTELLECTUAL ||
                        cf == CAPITAL_GOVERNANCE_INSTITUTIONAL;
        bool on_fin = ff_rail_supports_form(r, f, RAIL_FINANCIAL);
        bool on_prov = ff_rail_supports_form(r, f, RAIL_PROVENANCE);
        bool on_ext = ff_rail_supports_form(r, f, RAIL_EXTERNALITY);
        bool want_fin = (cf == CAPITAL_FINANCIAL || cf == CAPITAL_MATERIAL ||
                         cf == CAPITAL_LIVING || cf == CAPITAL_BUILT);
        bool want_prov = reserved || cf == CAPITAL_KNOWLEDGE;
        bool want_ext = reserved;
        if (on_fin != want_fin || on_prov != want_prov || on_ext != want_ext) table_ok = 0;
    }
    CK(table_ok, "every form maps to exactly its rails");
    CK(!ff_rail_supports_form(r, F1(CAPITAL_FINANCIAL), 5) &&
           !ff_rail_supports_form(r, F1(CAPITAL_FINANCIAL), 0) &&
           !ff_rail_supports_form(r, 0, RAIL_FINANCIAL) &&
           !ff_rail_supports_form(r, 10, RAIL_FINANCIAL),
       "unknown rails and forms refused");
    CK(!ff_rail_supports_form(r, F1(CAPITAL_SOCIAL), RAIL_FINANCIAL),
       "state-reserved form never on the debit rail");
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
    static const capital_form_t reserved_forms[4] = {CAPITAL_SOCIAL, CAPITAL_NATURAL,
                                                     CAPITAL_HERITAGE_INTELLECTUAL,
                                                     CAPITAL_GOVERNANCE_INSTITUTIONAL};
    for (uint32_t k = 0; k < 4; k++) {
        uint8_t f = F1(reserved_forms[k]);
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
    CK(crown_ok, "state-reserved forms refused on every path, balances untouched");
    CK(ff_initiate_settlement(&ff, 1, (uint32_t) a, (uint32_t) b, F1(CAPITAL_NATURAL), 10, 1, 0) ==
               FF_EINALIENABLE &&
           A->balances[CAPITAL_NATURAL] == 100,
       "mesh settlement of a state-reserved form refused");
    CK(ff_create_order_book(&ff, "SOC/FIN", F1(CAPITAL_SOCIAL), F1(CAPITAL_FINANCIAL)) ==
           FF_EINALIENABLE,
       "no market in a state-reserved form");

    const uint8_t fin = F1(CAPITAL_FINANCIAL), kno = F1(CAPITAL_KNOWLEDGE);
    CK(ff_route_through_rail(&ff, (uint32_t) a, (uint32_t) b, fin, 10, RAIL_PROVENANCE) ==
               FF_ERAIL &&
           A->balances[CAPITAL_FINANCIAL] == 100,
       "Financial on the provenance rail refused");
    CK(ff_route_through_rail(&ff, (uint32_t) a, (uint32_t) b, fin, 10, RAIL_FINANCIAL) == 0 &&
           A->balances[CAPITAL_FINANCIAL] == 90 && B->balances[CAPITAL_FINANCIAL] == 110,
       "Financial on the debit rail settles");
    CK(ff_route_through_rail(&ff, (uint32_t) a, (uint32_t) b, kno, 10, RAIL_FINANCIAL) == FF_ERAIL,
       "Knowledge on the debit rail refused");
    CK(ff_route_through_rail(&ff, (uint32_t) a, (uint32_t) b, kno, 10, RAIL_PROVENANCE) == 0 &&
           B->balances[CAPITAL_KNOWLEDGE] == 110,
       "Knowledge on the provenance rail settles");
    CK(ff_bridge_asset(&ff, (uint32_t) a, fin, 10, "eth", "0x0") == FF_ENOTSUP &&
           A->balances[CAPITAL_FINANCIAL] == 90,
       "bridge fails closed without debiting");
    /* the balance index is the canonical form index: a Financial transfer
     * moved exactly balances[ZCAP_FINANCIAL] and no other slot */
    {
        int others = 1;
        for (uint32_t i = 0; i < 9; i++)
            if (i != (uint32_t) ZCAP_FINANCIAL && i != (uint32_t) ZCAP_INTELLECTUAL &&
                (A->balances[i] != 100 || B->balances[i] != 100))
                others = 0;
        CK(others && (int) CAPITAL_FINANCIAL == (int) ZCAP_FINANCIAL,
           "ff balance slots are zcap-indexed: only the moved forms changed");
    }

    printf("=== %s (%d failure(s)) ===\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
