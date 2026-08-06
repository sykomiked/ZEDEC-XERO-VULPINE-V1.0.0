/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* test_alloc.c — assert the NUMBERS, not just the control flow. Anchored against
 * hand-computed proportions and the SHA-256 provenance, and it greps the module
 * source to PROVE there is no kill switch (a claim you can only make by absence). */

#include <stdio.h>
#include <string.h>
#include "alloc.h"

/* Paths to the module source, so the "no kill switch" test can grep them. Default
 * to the in-tree relative paths (make runs from kernel/); overridable via -D. */
#ifndef ALLOC_C_PATH
#define ALLOC_C_PATH "src/alloc/alloc.c"
#endif
#ifndef ALLOC_H_PATH
#define ALLOC_H_PATH "src/alloc/alloc.h"
#endif

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL: %s (line %d)\n", (msg), __LINE__); } \
} while (0)

/* host-only exact compare for surplus_real_t (double under TEST_HOST) */
static int sr_eq(surplus_real_t a, surplus_real_t b) { return SR_CMP(a, b) == 0; }

int main(void) {
    printf("=== alloc: symbiosis engine + a coin no king can seize ===\n");

    /* ---------------------------------------------------------------- *
     * (1) DISTRIBUTION sums EXACTLY to the pool, split by supplied units *
     * ---------------------------------------------------------------- */
    {
        alloc_pool_t p;
        alloc_pool_init(&p);
        /* Three contributors of NATURAL capital: 30, 20, 50 units (total 100). */
        CHECK(alloc_contribute(&p, 1001, ZCAP_NATURAL, SR_FROM_INT(30)) >= 0, "contribute A");
        CHECK(alloc_contribute(&p, 1002, ZCAP_NATURAL, SR_FROM_INT(20)) >= 0, "contribute B");
        CHECK(alloc_contribute(&p, 1003, ZCAP_NATURAL, SR_FROM_INT(50)) >= 0, "contribute C");

        alloc_result_t r;
        int32_t rc = alloc_distribute(&p, ZCAP_NATURAL, &r);
        CHECK(rc == ALLOC_OK, "distribute returns OK");
        CHECK(r.count == 3, "three shares");
        /* proportional: 30/100, 20/100, 50/100 of a pool of 100 == 30, 20, 50 */
        CHECK(sr_eq(r.shares[0].amount, SR_FROM_INT(30)), "share A == 30");
        CHECK(sr_eq(r.shares[1].amount, SR_FROM_INT(20)), "share B == 20");
        CHECK(sr_eq(r.shares[2].amount, SR_FROM_INT(50)), "share C == 50");
        /* the anchor: the shares sum to the pool EXACTLY */
        CHECK(sr_eq(r.total, SR_FROM_INT(100)), "shares sum EXACTLY to pool (100)");
        surplus_real_t sum = SR_ZERO;
        for (uint32_t i = 0; i < r.count; i++) sum = SR_ADD(sum, r.shares[i].amount);
        CHECK(sr_eq(sum, p.pooled[ZCAP_NATURAL]), "re-summed shares == pooled");

        /* accumulation: a repeat contribution folds into the same slot */
        CHECK(alloc_contribute(&p, 1001, ZCAP_NATURAL, SR_FROM_INT(10)) >= 0, "re-contribute A");
        CHECK(p.count[ZCAP_NATURAL] == 3, "still three contributors after accumulate");
        CHECK(sr_eq(p.pooled[ZCAP_NATURAL], SR_FROM_INT(110)), "pool grew to 110");
    }

    /* an empty pool is never fabricated into a payout */
    {
        alloc_pool_t p; alloc_pool_init(&p);
        alloc_result_t r;
        CHECK(alloc_distribute(&p, ZCAP_HUMAN, &r) == -ALLOC_ERR_EMPTY, "empty pool -> EMPTY, no invented payout");
    }

    /* ---------------------------------------------------------------- *
     * (2) a NON-RECIPROCAL allocation is REFUSED via onepolicy          *
     * ---------------------------------------------------------------- */
    {
        /* reciprocal transfer, no harm, within floor, no deduction -> sustainable */
        alloc_txn_t ok = (alloc_txn_t){0};
        ok.term.give_a = SR_FROM_INT(50);
        ok.term.give_b = SR_FROM_INT(50);
        ok.term.reciprocal = true;
        ok.stock_before = SR_FROM_INT(100);
        ok.draw = SR_FROM_INT(10);
        ok.yield_floor = SR_FROM_INT(80);
        ok.principal = SR_FROM_INT(100);
        ok.deduction = SR_ZERO;
        CHECK(alloc_sustainable_ok(&ok) == true, "reciprocal, in-floor, no fee -> sustainable");

        /* make it NON-reciprocal: value flows one way, nothing binds a return */
        alloc_txn_t nonrecip = ok;
        nonrecip.term.reciprocal = false;
        nonrecip.term.give_b = SR_ZERO;   /* pure take: A gives, B returns nothing */
        CHECK(op_symbiotic_ok(&nonrecip.term) == false, "onepolicy: non-reciprocal is not a term");
        CHECK(alloc_sustainable_ok(&nonrecip) == false, "non-reciprocal allocation REFUSED (onepolicy)");
    }

    /* ---------------------------------------------------------------- *
     * (3) a 12% deduction is refused; 11% (the customary rate) accepted *
     * ---------------------------------------------------------------- */
    {
        alloc_txn_t t = (alloc_txn_t){0};
        t.term.give_a = SR_FROM_INT(100);
        t.term.give_b = SR_FROM_INT(100);
        t.term.reciprocal = true;
        t.stock_before = SR_FROM_INT(1000);
        t.draw = SR_ZERO;
        t.yield_floor = SR_FROM_INT(0);
        t.principal = SR_FROM_INT(100);

        /* 11% of 100 == 11 : the customary rate, accepted */
        t.deduction = SR_FROM_INT(11);
        CHECK(alloc_sustainable_ok(&t) == true, "11% deduction ACCEPTED (customary rate)");

        /* 12% of 100 == 12 : a taking, refused */
        t.deduction = SR_FROM_INT(12);
        CHECK(alloc_sustainable_ok(&t) == false, "12% deduction REFUSED (over customary rate)");

        /* one tick over 11% is still over the line */
        t.deduction = SR_ADD(SR_FROM_INT(11), SR_FROM_FLOAT(0.5));
        CHECK(alloc_sustainable_ok(&t) == false, "11.5% deduction REFUSED");
    }

    /* ---------------------------------------------------------------- *
     * (4) a draw below the SUPPLIED sustainable-yield floor is refused  *
     * ---------------------------------------------------------------- */
    {
        alloc_txn_t t = (alloc_txn_t){0};
        t.term.give_a = SR_FROM_INT(10);
        t.term.give_b = SR_FROM_INT(10);
        t.term.reciprocal = true;
        t.principal = SR_FROM_INT(10);
        t.deduction = SR_ZERO;
        t.stock_before = SR_FROM_INT(100);
        t.yield_floor = SR_FROM_INT(70);   /* supplied floor (ops boundary) */

        t.draw = SR_FROM_INT(30);          /* 100 - 30 == 70 == floor : OK (>=) */
        CHECK(alloc_sustainable_ok(&t) == true, "draw to exactly the floor is OK");

        t.draw = SR_FROM_INT(31);          /* 100 - 31 == 69 < 70 : refused */
        CHECK(alloc_sustainable_ok(&t) == false, "draw BELOW the yield floor REFUSED");
    }

    /* ---------------------------------------------------------------- *
     * (5) financial CAN be spent for another form; a Crown form CANNOT   *
     *     be bought (compose zcapital inalienability)                    *
     * ---------------------------------------------------------------- */
    {
        zcap_vec_t v = (zcap_vec_t){0};
        v.bal[ZCAP_FINANCIAL] = SR_FROM_INT(100);

        /* financial -> manufactured (priceable): allowed, value conserved */
        zcap_result_t rc1 = alloc_acquire_with_financial(&v, ZCAP_MANUFACTURED, SR_FROM_INT(30));
        CHECK(rc1 == ZCAP_OK, "financial spent for MANUFACTURED -> OK");
        CHECK(sr_eq(v.bal[ZCAP_FINANCIAL], SR_FROM_INT(70)), "financial debited to 70");
        CHECK(sr_eq(v.bal[ZCAP_MANUFACTURED], SR_FROM_INT(30)), "manufactured credited 30");

        /* financial -> cultural (Crown, non-priceable): REFUSED, holding untouched */
        surplus_real_t fin_before = v.bal[ZCAP_FINANCIAL];
        surplus_real_t cul_before = v.bal[ZCAP_CULTURAL];
        zcap_result_t rc2 = alloc_acquire_with_financial(&v, ZCAP_CULTURAL, SR_FROM_INT(10));
        CHECK(rc2 == ZCAP_INALIENABLE, "you cannot BUY a culture -> INALIENABLE");
        CHECK(sr_eq(v.bal[ZCAP_FINANCIAL], fin_before), "financial UNCHANGED after refusal");
        CHECK(sr_eq(v.bal[ZCAP_CULTURAL], cul_before), "cultural UNCHANGED after refusal");
        CHECK(zcap_is_priceable(ZCAP_CULTURAL) == false, "cultural is a non-priceable Crown form");
    }

    /* ---------------------------------------------------------------- *
     * (6) DOUBLE-SPEND: transferring an already-spent id is refused and  *
     *     balances are unchanged                                         *
     * ---------------------------------------------------------------- */
    uint8_t genesis[32];
    for (int i = 0; i < 32; i++) genesis[i] = (uint8_t)(0xA0 + i);
    {
        ptoken_ledger_t l;
        ptoken_ledger_init(&l);

        ptoken_t tok;
        CHECK(ptoken_mint(&l, 7, genesis, &tok) == PTOKEN_OK, "mint token to owner 7");
        CHECK(tok.owner == 7 && tok.spent == false, "minted token owned by 7, unspent");
        CHECK(ptoken_balance(&l, 7) == 1, "owner 7 balance == 1");

        /* provenance is a real content-address: SHA-256(genesis||owner||nonce),
         * recomputed independently here and asserted equal — not our own echo */
        uint8_t expect[32];
        {
            uint8_t buf[32 + 4 + 8];
            uint32_t j = 0;
            for (int i = 0; i < 32; i++) buf[j++] = genesis[i];
            for (int i = 0; i < 4; i++) buf[j++] = (uint8_t)(7u >> (8 * i));
            for (int i = 0; i < 8; i++) buf[j++] = (uint8_t)(0ull >> (8 * i));
            /* recompute via the same primitive the module uses */
            extern void sha256(const uint8_t *, size_t, uint8_t[32]);
            sha256(buf, sizeof(buf), expect);
        }
        CHECK(memcmp(tok.id, expect, 32) == 0, "token id == SHA-256 of its provenance");

        /* transfer 7 -> 9 : old spent, new appended to 9 */
        CHECK(ptoken_transfer(&l, tok.id, 7, 9) == PTOKEN_OK, "transfer 7 -> 9 OK");
        CHECK(ptoken_balance(&l, 7) == 0, "owner 7 balance now 0");
        CHECK(ptoken_balance(&l, 9) == 1, "owner 9 balance now 1");

        /* DOUBLE-SPEND: transfer the SAME (now spent) id again -> refused */
        uint32_t bal7 = ptoken_balance(&l, 7);
        uint32_t bal9 = ptoken_balance(&l, 9);
        uint32_t count_before = l.count;
        int32_t rc = ptoken_transfer(&l, tok.id, 7, 5);
        CHECK(rc == -PTOKEN_ERR_SPENT, "second transfer of same id -> ERR_SPENT (double-spend refused)");
        CHECK(ptoken_balance(&l, 7) == bal7, "balance 7 UNCHANGED after refused double-spend");
        CHECK(ptoken_balance(&l, 9) == bal9, "balance 9 UNCHANGED after refused double-spend");
        CHECK(l.count == count_before, "ledger did not grow on refused double-spend");

        /* a non-owner cannot move the live token either (no seizure path) */
        uint8_t live_id[32];
        memcpy(live_id, l.entries[l.count - 1].id, 32);   /* the token owner 9 holds */
        CHECK(ptoken_transfer(&l, live_id, 3, 3) == -PTOKEN_ERR_NOT_OWNER,
              "a non-owner cannot transfer a token they do not hold");
        CHECK(ptoken_balance(&l, 9) == 1, "owner 9 still holds the token after failed grab");

        /* an unknown id is not found (distinct from spent) */
        uint8_t bogus[32];
        for (int i = 0; i < 32; i++) bogus[i] = 0xFF;
        CHECK(ptoken_transfer(&l, bogus, 9, 1) == -PTOKEN_ERR_NOT_FOUND, "unknown id -> NOT_FOUND");
    }

    /* ---------------------------------------------------------------- *
     * (7) NO KILL SWITCH — by API and by a SOURCE GREP                   *
     *     There is no freeze/burn/revoke/admin-override symbol anywhere. *
     * ---------------------------------------------------------------- */
    {
        /* the forbidden tokens: if any appears in the module source, this fails */
        static const char *forbidden[] = {
            "freeze", "burn", "revoke", "admin", "override", "seize(", "kill_switch"
        };
        const char *paths[2] = { ALLOC_C_PATH, ALLOC_H_PATH };
        int found_any = 0;
        int scanned = 0;
        for (int pi = 0; pi < 2; pi++) {
            FILE *f = fopen(paths[pi], "rb");
            if (!f) { printf("  NOTE: could not open %s for grep\n", paths[pi]); continue; }
            scanned++;
            static char blob[1 << 18];
            size_t n = fread(blob, 1, sizeof(blob) - 1, f);
            fclose(f);
            blob[n] = 0;
            for (size_t fi = 0; fi < sizeof(forbidden) / sizeof(forbidden[0]); fi++) {
                if (strstr(blob, forbidden[fi]) != NULL) {
                    printf("  FAIL: forbidden kill-switch token '%s' found in %s\n",
                           forbidden[fi], paths[pi]);
                    found_any = 1;
                }
            }
        }
        CHECK(scanned == 2, "both module sources opened for the kill-switch grep");
        CHECK(found_any == 0, "NO kill switch: no freeze/burn/revoke/admin/override symbol in source");
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
