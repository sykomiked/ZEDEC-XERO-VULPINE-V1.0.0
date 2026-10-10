/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_capital_canon.c — the nine forms of capital mean the same form in
 * every module. zcap_form_t (zcap_forms.h) is canonical; vino.h, pay_ledger.h,
 * swarm_market.h, finance/capital_forms.h and sdk/m5_api.h alias it (their
 * headers _Static_assert each form). This test checks the runtime side: a
 * value of form X produced by one module lands in, and is named and
 * classified as, form X by the others. */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "zcapital.h"
#include "vino.h"
#include "triple_ledger.h"
#include "capital_forms.h"
#include "swarm_market.h"
#include "pay_ledger.h"
#include "m5_api.h"

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

static vino_ledger_t g_vino; /* ~10 MB */
static pay_ledger_t g_pay;   /* large  */
static swarm_market_t g_mkt;

static int same_ci(const char *a, const char *b)
{
    while (*a && *b) {
        if (toupper((unsigned char) *a) != toupper((unsigned char) *b)) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

int main(void)
{
    /* ---- the order itself ---- */
    static const char *const want[ZCAP_FORM_COUNT] = {"FINANCIAL", "MANUFACTURED", "INTELLECTUAL",
                                                      "HUMAN",     "SOCIAL",       "NATURAL",
                                                      "CULTURAL",  "SPIRITUAL",    "SYSTEM"};
    int names_ok = 1;
    for (unsigned f = 0; f < ZCAP_FORM_COUNT; f++)
        if (strcmp(zcap_form_name(f), want[f]) != 0) names_ok = 0;
    CHECK(names_ok && strcmp(zcap_form_name(ZCAP_FORM_COUNT), "UNKNOWN") == 0,
          "canonical order FINANCIAL..SYSTEM, out of range is UNKNOWN");

    /* ---- classification agrees in every module ---- */
    int crown_ok = 1;
    for (unsigned f = 0; f < ZCAP_FORM_COUNT; f++) {
        bool crown = zcap_form_is_crown(f) != 0;
        if (zcap_is_priceable((zcap_form_t) f) == crown) crown_ok = 0;
        if (zcap_who_owns_this((zcap_form_t) f) == ZCAP_AUTH_CROWN) {
            if (!crown) crown_ok = 0;
        } else if (crown)
            crown_ok = 0;
        if (pay_cap_is_crown((pay_cap_t) f) != crown) crown_ok = 0;
        if (swarm_cap_is_crown((swarm_cap_t) f) != crown) crown_ok = 0;
        if (capital_is_state_reserved((capital_form_t) f) != crown) crown_ok = 0;
        if (capital_is_priceable((capital_form_t) f) == crown) crown_ok = 0;
    }
    CHECK(crown_ok, "zcapital, pay, swarm and finance agree on which forms are Crown");
    CHECK(!capital_is_priceable(CAPITAL_FORM_COUNT), "finance: out-of-range form is not priceable");

    /* ---- names agree: vino, triple_ledger (mixed case) ---- */
    int vn_ok = 1;
    for (unsigned f = 0; f < ZCAP_FORM_COUNT; f++) {
        if (strcmp(vino_capital_name((capital_type_t) f), zcap_form_name(f)) != 0) vn_ok = 0;
        if (!same_ci(capital_type_name((capital_type_t) f), zcap_form_name(f))) vn_ok = 0;
    }
    CHECK(vn_ok, "vino and triple_ledger name every slot with its canonical form");

    /* ---- finance and SDK aliases round-trip ---- */
    int rt_ok = 1;
    for (unsigned f = 0; f < ZCAP_FORM_COUNT; f++) {
        if ((unsigned) capital_to_zcap((capital_form_t) f) != f) rt_ok = 0;
        if ((unsigned) capital_from_zcap(capital_to_zcap((capital_form_t) f)) != f) rt_ok = 0;
        if ((unsigned) capital_settlement_medium((capital_form_t) f) != f) rt_ok = 0;
    }
    CHECK(rt_ok, "capital_to_zcap / capital_from_zcap round-trip every form");
    CHECK((int) CAPITAL_LIVING == (int) ZCAP_HUMAN && (int) CAP_LIVING == (int) ZCAP_NATURAL &&
              (int) M5_FORM_LIVING == (int) ZCAP_HUMAN && (int) M5_CAP_LIVING == (int) ZCAP_NATURAL,
          "the LIVING name clash is pinned: finance LIVING = HUMAN, vino LIVING = NATURAL");
    CHECK((int) CAPITAL_BUILT == (int) CAP_BUILT && (int) CAP_BUILT == (int) ZCAP_SYSTEM &&
              (int) CAPITAL_MATERIAL == (int) CAP_MATERIAL &&
              (int) CAPITAL_KNOWLEDGE == (int) CAP_KNOWLEDGE,
          "shared finance/vino names alias the same canonical form");

    /* ---- a swarm credit of form X becomes a vino transfer of form X ---- */
    vino_init(&g_vino, 1);
    CHECK(vino_create_account(&g_vino, "src", "Source") >= 0 &&
              vino_create_account(&g_vino, "dst", "Dest") >= 0,
          "vino accounts");
    swarm_market_init(&g_mkt);
    CHECK(swarm_market_join(&g_mkt, 7, 1000) == SWARM_OK, "swarm trader joins");
    vino_account_t *src = vino_get_account(&g_vino, "src");
    vino_account_t *dst = vino_get_account(&g_vino, "dst");
    int xfer_ok = 1;
    for (unsigned f = 1; f < ZCAP_FORM_COUNT; f++) { /* swarm never credits FINANCIAL */
        uint64_t amt = 10u + f;
        if (swarm_market_credit(&g_mkt, 7, (swarm_cap_t) f, amt) != SWARM_OK) xfer_ok = 0;
        const swarm_trader_t *t = swarm_market_trader(&g_mkt, 7);
        /* the form index swarm stored it under is passed to vino unchanged */
        unsigned form_from_swarm = 0;
        for (unsigned g = 0; g < SWARM_CAP_COUNT; g++)
            if (t->cap[g] == amt) form_from_swarm = g;
        if (form_from_swarm != f) xfer_ok = 0;
        src->balance[form_from_swarm] = amt;
        if (vino_transfer(&g_vino, "src", "dst", amt, (capital_type_t) form_from_swarm,
                          RAIL_VINO_NATIVE, "canon") < 0)
            xfer_ok = 0;
        /* vino recorded and holds it as the same canonical form */
        uint64_t got = 0;
        if (vino_get_balance(&g_vino, "dst", (capital_type_t) f, &got) != 0 || got != amt)
            xfer_ok = 0;
        if ((unsigned) g_vino.primary[g_vino.num_txns - 1].capital != f) xfer_ok = 0;
        if (strcmp(vino_capital_name(g_vino.primary[g_vino.num_txns - 1].capital),
                   zcap_form_name(f)) != 0)
            xfer_ok = 0;
    }
    for (unsigned f = 0; f < ZCAP_FORM_COUNT; f++)
        if (dst->balance[f] != (f == 0 ? 0u : 10u + f)) xfer_ok = 0;
    CHECK(xfer_ok, "form X credited by swarm is transferred, held and named as form X by vino");
    CHECK(vino_chain_verify(&g_vino, 0) == VINO_CHAIN_OK,
          "...and the vino chain over them verifies");

    /* ---- a zcapital exchange into form X lands in the vino slot of form X ---- */
    {
        zcap_vec_t v;
        memset(&v, 0, sizeof v);
        v.bal[ZCAP_FINANCIAL] = SR_FROM_INT(50);
        CHECK(zcap_exchange(&v, ZCAP_FINANCIAL, ZCAP_INTELLECTUAL, SR_FROM_INT(20)) == ZCAP_OK,
              "zcap exchange FINANCIAL -> INTELLECTUAL");
        CHECK(SR_CMP(v.bal[ZCAP_INTELLECTUAL], SR_FROM_INT(20)) == 0,
              "...20 units now INTELLECTUAL");
        uint64_t u = 20u;
        src->balance[CAP_KNOWLEDGE] = u;
        CHECK(vino_transfer(&g_vino, "src", "dst", u, (capital_type_t) ZCAP_INTELLECTUAL,
                            RAIL_VINO_NATIVE, 0) >= 0 &&
                  dst->balance[CAP_INTELLECTUAL] == 12u + 20u,
              "...the zcap form index moves the vino INTELLECTUAL (KNOWLEDGE) slot");
        CHECK(zcap_exchange(&v, ZCAP_FINANCIAL, ZCAP_CULTURAL, SR_ONE) == ZCAP_INALIENABLE &&
                  capital_is_state_reserved((capital_form_t) ZCAP_CULTURAL),
              "...and a Crown form is refused by zcap and reserved in finance alike");
    }

    /* ---- pay accounts carry the canonical form ---- */
    {
        pay_ledger_init(&g_pay, 0);
        int pay_ok = 1;
        for (unsigned f = 0; f < ZCAP_FORM_COUNT; f++) {
            uint32_t acct = 0;
            if (pay_ledger_open(&g_pay, 100u + f, g_pay.vfv_asset, (pay_cap_t) f, 0, &acct) !=
                PAY_OK)
                pay_ok = 0;
            const pay_account_t *a = pay_ledger_account(&g_pay, acct);
            if (!a || a->cap != f) pay_ok = 0;
        }
        CHECK(pay_ok, "pay opens an account in each canonical form and stores that index");
    }

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
