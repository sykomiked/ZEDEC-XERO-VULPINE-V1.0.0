/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* test_battering_ram.c — assert the NUMBERS, not merely that the code ran.
 *
 * Host-only (TEST_HOST): stdio is allowed HERE and nowhere in the module. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "battering_ram.h"

static int g_asserts = 0;
static int g_fails   = 0;

#define CHECK(cond, msg) do {                                   \
    g_asserts++;                                                \
    if (!(cond)) { g_fails++;                                   \
        printf("FAIL [%d]: %s\n", g_asserts, (msg)); }          \
    else { printf("ok   [%d]: %s\n", g_asserts, (msg)); }       \
} while (0)

/* On the host surplus_real_t is double; compare with a small epsilon. */
static int near_(surplus_real_t a, double b) { return fabs((double)a - b) < 1e-6; }

/* Test double for the external oracle: models a VALID signature. The module
 * never invents this verdict — it delegates to whatever verifier is bound. */
static bool stub_verify_ok(const ext_attestation_t *att) { (void)att; return true; }

int main(void) {
    printf("=== The Battering Ram Exchange ===\n");
    static br_exchange_t ex;

    /* ---- Anchor 1: the cross-capital BASKET — 30/30/20/20 = 100% ---------- */
    br_exchange_init(&ex);
    br_set_verifier(&ex, stub_verify_ok);
    pledge_t basket[4] = {
        { 0x1001u, ZCAP_NATURAL,  SR_FROM_INT(30) },
        { 0x1002u, ZCAP_HUMAN,    SR_FROM_INT(30) },
        { 0x1003u, ZCAP_SOCIAL,   SR_FROM_INT(20) },
        { 0x1004u, ZCAP_CULTURAL, SR_FROM_INT(20) },
    };
    int32_t idx = br_alliance_open(&ex, 42, basket, 4);
    CHECK(idx >= 0, "A1: alliance opens (PROPOSED -> ALLIANCE_FORMED)");
    CHECK(near_(ex.alliances[idx].total_units, 100.0),
          "A1: weights 30+30+20+20 sum to EXACTLY 100%");

    ext_attestation_t att = {0};
    att.outcome_id = 42;
    att.achieved = true;
    allocation_t alloc;
    int32_t rc = br_distribute(&ex, 42, &att, &alloc);
    CHECK(rc == BR_OK, "A1: verified achievement distributes");
    CHECK(near_(alloc.realized, 100.0), "A1: realized == pooled principal 100");
    CHECK(near_(alloc.party[0], 30.0), "A1: Natural share == 30");
    CHECK(near_(alloc.party[1], 30.0), "A1: Human share == 30");
    CHECK(near_(alloc.party[2], 20.0), "A1: Social share == 20");
    CHECK(near_(alloc.party[3], 20.0), "A1: Cultural share == 20");
    double sum = (double)alloc.party[0] + (double)alloc.party[1] +
                 (double)alloc.party[2] + (double)alloc.party[3];
    CHECK(near_((surplus_real_t)sum, 100.0), "A1: shares sum to realized exactly");
    /* proportions are EXACT: each share / realized == its weight */
    CHECK(near_((surplus_real_t)((double)alloc.party[0] / 100.0), 0.30) &&
          near_((surplus_real_t)((double)alloc.party[2] / 100.0), 0.20),
          "A1: split proportions are exactly 30% / 20%");
    CHECK(SR_CMP(alloc.contributor_pool, SR_DIV(alloc.realized, SR_FROM_INT(2))) > 0,
          "A1: contributors retain the MAJORITY of the pool");
    CHECK(ex.alliances[idx].state == BR_DISTRIBUTED, "A1: alliance now DISTRIBUTED");

    /* ---- Anchor 2: revenue split — 11% + 11%, majority retained ----------- */
    allocation_t rev;
    br_revenue_split(SR_FROM_INT(100), &rev);
    CHECK(near_(rev.tribute, 11.0), "A2: tribute == 11 (11% of 100)");
    CHECK(near_(rev.gratuity, 11.0), "A2: gratuity == 11 (11% of 100)");
    CHECK(near_(rev.contributor_pool, 78.0), "A2: contributor pool == 78 (exact remainder)");
    CHECK(near_(SR_ADD(SR_ADD(rev.tribute, rev.gratuity), rev.contributor_pool), 100.0),
          "A2: 11 + 11 + 78 == 100 (nothing leaks)");
    CHECK(br_contributors_hold_majority(&rev), "A2: 78 > 50 — contributors keep the majority");
    /* a different realized, hand-computed */
    br_revenue_split(SR_FROM_INT(200), &rev);
    CHECK(near_(rev.tribute, 22.0) && near_(rev.gratuity, 22.0) &&
          near_(rev.contributor_pool, 156.0), "A2: realized 200 -> 22/22/156");

    /* ---- Anchor 3: a NON-achieved outcome produces ZERO debt -------------- */
    br_exchange_init(&ex);
    br_set_verifier(&ex, stub_verify_ok);
    br_book_credit(&ex, ZCAP_HUMAN, SR_FROM_INT(50));   /* some real holdings */
    pledge_t two[2] = {
        { 0x2001u, ZCAP_HUMAN, SR_FROM_INT(40) },
        { 0x2002u, ZCAP_SOCIAL, SR_FROM_INT(60) },
    };
    idx = br_alliance_open(&ex, 7, two, 2);
    CHECK(idx >= 0, "A3: alliance for the doomed outcome opens");
    ext_attestation_t miss = {0};
    miss.outcome_id = 7;
    miss.achieved = false;                 /* verified NON-achievement */
    allocation_t none;
    rc = br_distribute(&ex, 7, &miss, &none);
    CHECK(rc == BR_NOT_ACHIEVED, "A3: verified miss returns BR_NOT_ACHIEVED");
    CHECK(none.zero_debt, "A3: allocation carries the zero-debt witness");
    CHECK(near_(none.party[0], 0.0) && near_(none.party[1], 0.0),
          "A3: every party share is ZERO — no clawback, no liability");
    CHECK(ex.alliances[idx].state == BR_FORGIVEN, "A3: alliance is FORGIVEN");
    /* no negative balance anywhere on the book */
    int neg = 0;
    for (uint32_t f = 0; f < ZCAP_FORM_COUNT; f++)
        if (SR_CMP(ex.book.bal[f], SR_ZERO) < 0) neg = 1;
    CHECK(neg == 0, "A3: no form balance went negative");
    /* the pay-it-forward path is likewise debt-free */
    pledge_t solo[1] = { { 0x2003u, ZCAP_FINANCIAL, SR_FROM_INT(10) } };
    idx = br_alliance_open(&ex, 8, solo, 1);
    rc = br_payitforward(&ex, 8, PIF_FORGIVEN);
    CHECK(rc == BR_OK && ex.alliances[idx].state == BR_FORGIVEN,
          "A3: pay-it-forward FORGIVES with zero debt");

    /* ---- Anchor 4: symbiotic_ok REJECTS harm and interest ---------------- */
    br_term_t harm = {0};
    harm.give_a = SR_FROM_INT(100);   /* A gives 100 to B */
    harm.give_b = SR_ZERO;            /* B gives nothing back */
    harm.harm_a = SR_FROM_INT(50);    /* and A is harmed */
    harm.reciprocal = false;
    CHECK(!symbiotic_ok(&harm), "A4: asymmetric-harm term is void by definition");

    br_term_t usury = {0};
    usury.give_a = SR_FROM_INT(100);
    usury.give_b = SR_FROM_INT(100);
    usury.interest = SR_FROM_INT(5);  /* interest — usury */
    usury.reciprocal = true;
    CHECK(!symbiotic_ok(&usury), "A4: debt/interest term is void (no usury)");

    br_term_t cruel = {0};
    cruel.give_a = SR_FROM_INT(10);
    cruel.give_b = SR_FROM_INT(10);
    cruel.reciprocal = true;
    cruel.on_suffering = true;         /* a derivative written ON suffering */
    CHECK(!symbiotic_ok(&cruel), "A4: a derivative on suffering is void");

    br_term_t fair = {0};
    fair.give_a = SR_FROM_INT(10);
    fair.give_b = SR_FROM_INT(20);
    fair.reciprocal = true;
    CHECK(symbiotic_ok(&fair), "A4: a fair reciprocal term is admissible");

    /* ---- Anchor 5: swap is ATOMIC — both legs or neither ------------------ */
    br_exchange_init(&ex);
    br_book_credit(&ex, ZCAP_HUMAN, SR_FROM_INT(100));
    surplus_real_t human_before = ex.book.bal[ZCAP_HUMAN];
    surplus_real_t fin_before   = ex.book.bal[ZCAP_FINANCIAL];
    cross_cap_swap_t big = { ZCAP_HUMAN, ZCAP_FINANCIAL, SR_FROM_INT(200), SR_FROM_INT(1) };
    rc = br_swap_settle(&ex, &big);      /* 200 > 100 held: cannot complete */
    CHECK(rc == BR_ERR_INSUFFICIENT, "A5: infeasible swap refused");
    CHECK(SR_CMP(ex.book.bal[ZCAP_HUMAN], human_before) == 0 &&
          SR_CMP(ex.book.bal[ZCAP_FINANCIAL], fin_before) == 0,
          "A5: NEITHER balance moved on the refused swap");
    /* a feasible swap moves BOTH legs at the agreed rate */
    cross_cap_swap_t ok = { ZCAP_HUMAN, ZCAP_SOCIAL, SR_FROM_INT(10), SR_FROM_INT(2) };
    rc = br_swap_settle(&ex, &ok);       /* pay 10 Human, receive 20 Social */
    CHECK(rc == BR_OK, "A5: feasible swap settles");
    CHECK(near_(ex.book.bal[ZCAP_HUMAN], 90.0), "A5: Human 100 - 10 == 90");
    CHECK(near_(ex.book.bal[ZCAP_SOCIAL], 20.0), "A5: Social 0 + 10*2 == 20");

    /* ---- Anchor 5c: a from==to self-swap CANNOT mint value ---------------- */
    {
        surplus_real_t human_before = ex.book.bal[ZCAP_HUMAN];   /* 90 */
        cross_cap_swap_t wash = { ZCAP_HUMAN, ZCAP_HUMAN, SR_FROM_INT(10), SR_FROM_INT(2) };
        int32_t wr = br_swap_settle(&ex, &wash);
        CHECK(wr == BR_ERR_RANGE, "A5c: a from==to self-swap is refused (BR_ERR_RANGE)");
        CHECK(SR_CMP(ex.book.bal[ZCAP_HUMAN], human_before) == 0,
              "A5c: no value minted — the Human balance is unchanged");
    }

    /* ---- Anchor 5b: settle a money leg THROUGH the vino triple rail ------- */
    static triple_ledger_t tl;
    static vino_stores_t vs;
    triple_ledger_init(&tl);
    vino_stores_init(&vs, &tl);
    uint8_t cid[VINO_PROOF_CID_LEN];
    for (uint32_t i = 0; i < VINO_PROOF_CID_LEN; i++) cid[i] = (uint8_t)(i + 1);
    uint64_t vid = 0;
    int32_t mrc = vino_mint(&vs, &vid, SR_FROM_INT(89), SECOND_MINTS_OIL,
                            VINO_ACTIVE_DIGITAL, cid);
    CHECK(mrc == VINO_OK && vid == 1, "A5b: vino voucher minted for the rail");
    br_exchange_init(&ex);
    br_book_credit(&ex, ZCAP_HUMAN, SR_FROM_INT(100));
    br_bind_rail(&ex, &vs, vid, cid);
    surplus_real_t assets_before = tl.total_assets;
    cross_cap_swap_t money_in = { ZCAP_HUMAN, ZCAP_FINANCIAL, SR_FROM_INT(10), SR_FROM_INT(2) };
    rc = br_swap_settle(&ex, &money_in); /* receive 20 Financial -> posts to rail */
    CHECK(rc == BR_OK, "A5b: money-in swap settles through the rail");
    CHECK(near_(ex.book.bal[ZCAP_FINANCIAL], 20.0), "A5b: book Financial += 20");
    CHECK(near_(SR_SUB(tl.total_assets, assets_before), 20.0),
          "A5b: ledger assets increased by 20 (real triple-rail post)");

    /* ---- Anchor 6: a BAD SIGNATURE distributes NOTHING ------------------- */
    br_exchange_init(&ex);
    br_set_verifier(&ex, br_ed25519_attest_verify);   /* the REAL crypto check */
    pledge_t p1[1] = { { 0x3001u, ZCAP_INTELLECTUAL, SR_FROM_INT(70) } };
    idx = br_alliance_open(&ex, 99, p1, 1);
    ext_attestation_t bad = {0};
    bad.outcome_id = 99;
    bad.achieved = true;
    for (int i = 0; i < 32; i++) bad.attestor[i] = (uint8_t)i;   /* junk key */
    for (int i = 0; i < 64; i++) bad.sig[i] = (uint8_t)(0xA0 + i); /* junk sig */
    allocation_t nope;
    rc = br_distribute(&ex, 99, &bad, &nope);
    CHECK(rc == BR_ERR_UNVERIFIED, "A6: bad signature -> BR_ERR_UNVERIFIED");
    CHECK(ex.alliances[idx].state == BR_ALLIANCE_FORMED,
          "A6: unverified attestation consumes NOTHING (still FORMED)");
    /* and with NO verifier bound at all, we fail closed (never assume) */
    br_set_verifier(&ex, NULL);
    rc = br_distribute(&ex, 99, &bad, &nope);
    CHECK(rc == BR_ERR_NO_ORACLE, "A6: no oracle bound -> fail closed (NO_ORACLE)");

    /* ---- Bonus: a future written ON a capital, priced by the REUSED pricer  */
    surplus_real_t f = br_price_capital_future(ZCAP_NATURAL, SR_FROM_INT(100),
                                               SR_FROM_FLOAT(0.05), SR_FROM_INT(1));
    CHECK(near_(f, 105.0), "F: future on 100 @ r=5%, T=1 -> 105 (financial_price_future)");

    printf("\n=== %d assertions, %d failures ===\n", g_asserts, g_fails);
    return g_fails ? 1 : 0;
}
