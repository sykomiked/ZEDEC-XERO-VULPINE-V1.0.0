/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_vino_stores.c — assert the NUMBERS, not that the code merely ran.
 *
 * Host-only (TEST_HOST): stdio is allowed here and NOWHERE in the module. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "vino_stores.h"
#include "sha256.h" /* real kernel SHA-256 — a genuine proof_cid, not invented */

static int g_asserts = 0;
static int g_fails = 0;

#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        g_asserts++;                                                                               \
        if (!(cond)) {                                                                             \
            g_fails++;                                                                             \
            printf("FAIL [%d]: %s\n", g_asserts, (msg));                                           \
        } else {                                                                                   \
            printf("ok   [%d]: %s\n", g_asserts, (msg));                                           \
        }                                                                                          \
    } while (0)

/* surplus_real_t as a double: itself under TEST_HOST, raw/2^32 on the
 * Q32.32 path (a raw cast would read 2000 as 8589934592000). */
static double sr_to_d(surplus_real_t a)
{
#ifdef TEST_HOST
    return (double) a;
#else
    return (double) a / 4294967296.0;
#endif
}

/* Compare with a small epsilon. */
static int near_(surplus_real_t a, double b)
{
    return fabs(sr_to_d(a) - b) < 1e-6;
}

/* A genuine content-addressed witness for the equity rail. */
static void make_cid(const char *tag, uint8_t out[VINO_PROOF_CID_LEN])
{
    sha256((const uint8_t *) tag, (uint32_t) strlen(tag), out);
}

static surplus_real_t coverage_ratio(const triple_ledger_t *tl)
{
    if (SR_CMP(tl->total_liabilities, SR_ZERO) <= 0) return SR_FROM_INT(1000000);
    return SR_DIV(tl->total_assets, tl->total_liabilities);
}

int main(void)
{
    printf("=== vino_stores settlement engine ===\n");
    /* These structs are large (the ledger holds 512 accounts); keep them off
     * the stack so ASan does not (rightly) cry stack-overflow. */
    static triple_ledger_t tl;
    static vino_stores_t vs;
    uint8_t cid[VINO_PROOF_CID_LEN];
    make_cid("vino:equity:witness:1", cid);

    triple_ledger_init(&tl);
    vino_stores_init(&vs, &tl);

    uint64_t vid = 0;
    int32_t rc = vino_mint(&vs, &vid, SR_FROM_INT(89), SECOND_MINTS_OIL, VINO_ACTIVE_DIGITAL, cid);
    CHECK(rc == VINO_OK && vid == 1, "mint returns a voucher id");

    /* ---- Anchor 1: a valid act -> coverage >= 1.8x AND equity >= 0 -------- */
    rc = vino_ledger_act(&vs, vid, VINO_CIRCULATE, SR_FROM_INT(2000), SR_FROM_INT(1000),
                         SR_FROM_INT(1000), cid);
    CHECK(rc == VINO_OK, "A1: valid 3-rail act commits");
    CHECK(near_(tl.total_assets, 2000.0), "A1: assets == 2000");
    CHECK(near_(tl.total_liabilities, 1000.0), "A1: liabilities == 1000");
    surplus_real_t equity = SR_SUB(tl.total_assets, tl.total_liabilities);
    CHECK(near_(equity, 1000.0) && SR_CMP(equity, SR_ZERO) >= 0,
          "A1: equity = assets - liabilities == 1000 >= 0");
    surplus_real_t floor18 = SR_DIV(SR_FROM_INT(18), SR_FROM_INT(10));
    CHECK(SR_CMP(coverage_ratio(&tl), floor18) >= 0, "A1: coverage assets/liabilities >= 1.8x");

    /* ---- Anchor 2: an act that would drive coverage < 1.8x posts NOTHING -- */
    uint32_t entries_before = tl.accounts[vs.vino_account].num_entries;
    surplus_real_t assets_before = tl.total_assets;
    surplus_real_t liab_before = tl.total_liabilities;
    /* debit 1000 / credit 1000 -> assets 3000, liab 2000, coverage 1.5 < 1.8 */
    rc = vino_ledger_act(&vs, vid, VINO_CIRCULATE, SR_FROM_INT(1000), SR_FROM_INT(1000), SR_ZERO,
                         cid);
    uint32_t entries_after = tl.accounts[vs.vino_account].num_entries;
    CHECK(rc == VINO_ERR_COVERAGE, "A2: sub-1.8x act refused with COVERAGE");
    CHECK(entries_after == entries_before, "A2: entries_before == entries_after");
    CHECK(near_(tl.total_assets, sr_to_d(assets_before)) &&
              near_(tl.total_liabilities, sr_to_d(liab_before)),
          "A2: totals unchanged after rollback");

    /* ---- Anchor 2b: a supplied equity < 0 is refused, nothing posted ----- */
    entries_before = tl.accounts[vs.vino_account].num_entries;
    rc = vino_ledger_act(&vs, vid, VINO_CIRCULATE, SR_FROM_INT(3000), SR_FROM_INT(1000),
                         SR_FROM_INT(-5), cid);
    entries_after = tl.accounts[vs.vino_account].num_entries;
    CHECK(rc == VINO_ERR_EQUITY, "A2b: negative supplied equity refused");
    CHECK(entries_after == entries_before, "A2b: nothing posted on equity refusal");

    /* ---- Anchor 3: single-active-state — a locked-rail spend fails -------- */
    CHECK(vino_can_spend(vino_find(&vs, vid), VINO_ACTIVE_DIGITAL),
          "A3: digital voucher spends on the digital rail");
    rc = vino_swap_to_physical(&vs, vid);
    CHECK(rc == VINO_OK, "A3: swap to physical succeeds");
    vino_voucher_t *v = vino_find(&vs, vid);
    CHECK(vino_single_active_state_ok(v), "A3: single-active-state holds");
    CHECK(!vino_can_spend(v, VINO_ACTIVE_DIGITAL),
          "A3: DIGITAL spend fails after swap to physical");
    CHECK(vino_can_spend(v, VINO_ACTIVE_PHYSICAL), "A3: PHYSICAL spend now succeeds");

    /* ---- Anchor 4: vino_phi_draw_max(1000) ~= 618 (φ-1 known answer) ------ */
    surplus_real_t draw = vino_phi_draw_max(SR_FROM_INT(1000));
    CHECK(fabs(sr_to_d(draw) - 618.0) < 1.0, "A4: phi_draw_max(1000) ~= 618");

    /* ---- Anchor 5: the Fibonacci denomination ladder ---------------------- */
    static const uint64_t ladder[12] = {1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233};
    int ladder_ok = 1;
    for (uint32_t r = 1; r <= 12; r++) {
        if (vino_ladder_denomination(r) != ladder[r - 1]) ladder_ok = 0;
    }
    CHECK(ladder_ok, "A5: ladder starts 1,2,3,5,8,13,21,34,55,89,144,233");
    for (uint32_t r = 3; r <= VINO_LADDER_RUNGS; r++)
        if (vino_ladder_denomination(r) !=
            vino_ladder_denomination(r - 1) + vino_ladder_denomination(r - 2))
            ladder_ok = 0;
    CHECK(ladder_ok, "A5: every rung is the sum of the two below");
    CHECK(VINO_LADDER_RUNGS == 45 && vino_ladder_denomination(45) == 1836311903ULL,
          "A5: top rung 45 = F(46) = 1,836,311,903");
    CHECK(vino_ladder_denomination(46) == 0, "A5: rung 46 is off the ladder");
    CHECK(vino_ladder_denomination(0) == 0, "A5: V0 ceremonial note is zero-value");

    /* ---- Anchor 5b: Fibonacci-only coins (owner decision 2026-10-11) ----- */
    CHECK(vino_denomination_rung(SR_FROM_INT(1)) == 1, "A5b: 1 is rung 1");
    CHECK(vino_denomination_rung(SR_FROM_INT(2)) == 2, "A5b: 2 is rung 2");
    CHECK(vino_denomination_rung(SR_FROM_INT(89)) == 10, "A5b: 89 is rung 10");
    CHECK(vino_denomination_rung(SR_FROM_INT(1836311903)) == 45, "A5b: top face is rung 45");
    CHECK(vino_denomination_rung(SR_FROM_INT(4)) == 0, "A5b: 4 is not a coin");
    CHECK(vino_denomination_rung(SR_FROM_INT(90)) == 0, "A5b: 90 is not a coin");
    CHECK(vino_denomination_rung(SR_ZERO) == 0, "A5b: 0 is not a coin");
    CHECK(vino_denomination_rung(SR_FROM_INT(-1)) == 0, "A5b: -1 is not a coin");
    CHECK(vino_denomination_rung(SR_DIV(SR_FROM_INT(3), SR_FROM_INT(2))) == 0,
          "A5b: 1.5 is not a coin");

    {
        static vino_stores_t vf;
        static triple_ledger_t tf;
        triple_ledger_init(&tf);
        vino_stores_init(&vf, &tf);
        uint64_t id = 0;
        CHECK(vino_mint(0, &id, SR_FROM_INT(1), SECOND_MINTS_OIL, VINO_ACTIVE_DIGITAL, cid) ==
                  VINO_ERR_NULL,
              "A5b: mint with no engine");
        CHECK(vino_mint(&vf, 0, SR_FROM_INT(1), SECOND_MINTS_OIL, VINO_ACTIVE_DIGITAL, cid) ==
                      VINO_ERR_NULL &&
                  vf.num_vouchers == 0,
              "A5b: mint with no id out");
        CHECK(vino_mint(&vf, &id, SR_FROM_INT(4), SECOND_MINTS_OIL, VINO_ACTIVE_DIGITAL, cid) ==
                      VINO_ERR_DENOM &&
                  vf.num_vouchers == 0 && id == 0,
              "A5b: minting a 4 is refused, nothing registered");
        CHECK(vino_mint(&vf, &id, SR_ZERO, SECOND_MINTS_OIL, VINO_ACTIVE_DIGITAL, cid) ==
                  VINO_ERR_DENOM,
              "A5b: a zero coin outside MINT_ZERO is refused");
        CHECK(vino_mint(&vf, &id, SR_FROM_INT(1), MINT_ZERO, VINO_ACTIVE_DIGITAL, cid) ==
                  VINO_ERR_DENOM,
              "A5b: the ceremonial V0 note cannot carry value");
        CHECK(vino_mint(&vf, &id, SR_ZERO, MINT_ZERO, VINO_ACTIVE_DIGITAL, cid) == VINO_OK &&
                  id == 1,
              "A5b: the ceremonial V0 note mints at zero");
        CHECK(vino_mint(&vf, &id, SR_FROM_INT(233), FIRST_MINTS_VINO, VINO_ACTIVE_PHYSICAL,
                        cid) == VINO_OK &&
                  id == 2,
              "A5b: a 233 coin mints");
        CHECK(vino_mint(&vf, &id, SR_FROM_INT(1836311903), FIRST_MINTS_VINO,
                        VINO_ACTIVE_PHYSICAL, cid) == VINO_OK,
              "A5b: the top coin mints");

        /* Zeckendorf: 100 = 89 + 8 + 3 */
        uint32_t rg[8] = {0}, n = 99;
        CHECK(vino_zeckendorf(100, rg, 8, &n) == VINO_OK && n == 3 && rg[0] == 10 &&
                  rg[1] == 5 && rg[2] == 3,
              "A5b: 100 splits into 89 + 8 + 3");
        CHECK(vino_zeckendorf(4, rg, 8, &n) == VINO_OK && n == 2 && rg[0] == 3 && rg[1] == 1,
              "A5b: 4 splits into 3 + 1");
        CHECK(vino_zeckendorf(0, rg, 8, &n) == VINO_OK && n == 0, "A5b: 0 splits into nothing");
        rg[0] = 77;
        CHECK(vino_zeckendorf(100, rg, 2, &n) == VINO_ERR_CAPACITY && n == 3 && rg[0] == 77,
              "A5b: a short buffer reports the count and writes nothing");
        CHECK(vino_zeckendorf(100, rg, 3, &n) == VINO_OK && n == 3, "A5b: an exact buffer fits");
        CHECK(vino_zeckendorf(1836311903ULL * 2 + 1, rg, 8, &n) == VINO_OK && n == 3 &&
                  rg[0] == 45 && rg[1] == 45 && rg[2] == 1,
              "A5b: past the top face the top rung repeats");
        {   /* F(46) - 1 = F(45) + F(43) + ... + F(3): 22 coins, rungs 44, 42, ..., 2 */
            uint32_t big[32] = {0};
            int odd_ok = 1;
            CHECK(vino_zeckendorf(1836311902ULL, big, 32, &n) == VINO_OK && n == 22,
                  "A5b: top face - 1 needs 22 coins");
            for (uint32_t i = 0; i < 22; i++)
                if (big[i] != 44 - 2 * i) odd_ok = 0;
            CHECK(odd_ok, "A5b: and they are every other rung from 44 down to 2");
        }
        CHECK(vino_zeckendorf(UINT64_MAX, rg, 8, &n) == VINO_ERR_CAPACITY && n > 8,
              "A5b: an enormous amount needs more room than given");
        CHECK(vino_zeckendorf(1, 0, 0, &n) == VINO_ERR_CAPACITY && n == 1, "A5b: count only");
        CHECK(vino_zeckendorf(0, 0, 0, &n) == VINO_OK && n == 0, "A5b: count only, zero");
        CHECK(vino_zeckendorf(1, 0, 1, &n) == VINO_ERR_NULL, "A5b: NULL rungs with room");
        CHECK(vino_zeckendorf(1, rg, 1, 0) == VINO_ERR_NULL, "A5b: NULL count");
        /* every amount 1..2000: unique, non-adjacent, sums back exactly */
        int zk_ok = 1;
        for (uint64_t a = 1; a <= 2000; a++) {
            uint64_t sum = 0;
            if (vino_zeckendorf(a, rg, 8, &n) != VINO_OK) zk_ok = 0;
            for (uint32_t i = 0; i < n && i < 8; i++) {
                sum += vino_ladder_denomination(rg[i]);
                if (i && rg[i] + 1 >= rg[i - 1]) zk_ok = 0; /* strictly descending, gap >= 2 */
            }
            if (sum != a) zk_ok = 0;
        }
        CHECK(zk_ok, "A5b: 1..2000 split exactly into non-adjacent rungs");

        /* mint an arbitrary amount as coins */
        uint64_t ids[8] = {0};
        uint32_t before = vf.num_vouchers;
        CHECK(vino_mint_amount(&vf, 100, SECOND_MINTS_OIL, VINO_ACTIVE_DIGITAL, cid, ids, 8, &n) ==
                      VINO_OK &&
                  n == 3 && vf.num_vouchers == before + 3,
              "A5b: 100 VFV mints as three coins");
        CHECK(SR_CMP(vino_find(&vf, ids[0])->denomination, SR_FROM_INT(89)) == 0 &&
                  SR_CMP(vino_find(&vf, ids[1])->denomination, SR_FROM_INT(8)) == 0 &&
                  SR_CMP(vino_find(&vf, ids[2])->denomination, SR_FROM_INT(3)) == 0,
              "A5b: the coins are 89, 8 and 3");
        CHECK(vino_find(&vf, ids[2])->mint_tier == SECOND_MINTS_OIL &&
                  vino_find(&vf, ids[2])->state == VINO_ACTIVE_DIGITAL,
              "A5b: every coin carries the tier and state");
        before = vf.num_vouchers;
        CHECK(vino_mint_amount(&vf, 100, SECOND_MINTS_OIL, VINO_ACTIVE_DIGITAL, cid, ids, 2, &n) ==
                      VINO_ERR_CAPACITY &&
                  n == 0 && vf.num_vouchers == before,
              "A5b: too few id slots mints nothing");
        CHECK(vino_mint_amount(&vf, 1, MINT_ZERO, VINO_ACTIVE_DIGITAL, cid, ids, 8, &n) ==
                      VINO_ERR_DENOM &&
                  vf.num_vouchers == before,
              "A5b: MINT_ZERO has no value to split");
        CHECK(vino_mint_amount(&vf, 0, SECOND_MINTS_OIL, VINO_ACTIVE_DIGITAL, cid, ids, 8, &n) ==
                      VINO_OK &&
                  n == 0 && vf.num_vouchers == before,
              "A5b: zero mints no coins");
        CHECK(vino_mint_amount(0, 1, SECOND_MINTS_OIL, VINO_ACTIVE_DIGITAL, cid, ids, 8, &n) ==
                  VINO_ERR_NULL,
              "A5b: NULL engine");
        CHECK(vino_mint_amount(&vf, 1, SECOND_MINTS_OIL, VINO_ACTIVE_DIGITAL, cid, ids, 8, 0) ==
                  VINO_ERR_NULL,
              "A5b: NULL count");
        CHECK(vino_mint_amount(&vf, 1, SECOND_MINTS_OIL, VINO_ACTIVE_DIGITAL, cid, 0, 1, &n) ==
                  VINO_ERR_NULL,
              "A5b: NULL ids with room");
        /* fill the table to one free slot: a two-coin amount must not half-mint */
        while (vf.num_vouchers < VINO_STORES_MAX_VOUCHERS - 1)
            vino_mint(&vf, &id, SR_FROM_INT(1), SECOND_MINTS_OIL, VINO_ACTIVE_DIGITAL, cid);
        before = vf.num_vouchers;
        CHECK(vino_mint_amount(&vf, 4, SECOND_MINTS_OIL, VINO_ACTIVE_DIGITAL, cid, ids, 8, &n) ==
                      VINO_ERR_CAPACITY &&
                  n == 0 && vf.num_vouchers == before,
              "A5b: a full table mints nothing (all or nothing)");
        CHECK(vino_mint_amount(&vf, 3, SECOND_MINTS_OIL, VINO_ACTIVE_DIGITAL, cid, ids, 8, &n) ==
                      VINO_OK &&
                  n == 1 && vf.num_vouchers == VINO_STORES_MAX_VOUCHERS,
              "A5b: a one-coin amount fills the last slot");
    }

    /* ---- Anchor 6: the 112% Gratuity split 11/11/11/66/1 (+12) ------------ */
    surplus_real_t g[6];
    rc = vino_gratuity_112(SR_FROM_INT(100), g);
    CHECK(rc == VINO_OK, "A6: gratuity split returns OK");
    CHECK(near_(g[0], 11.0) && near_(g[1], 11.0) && near_(g[2], 11.0) && near_(g[3], 66.0) &&
              near_(g[4], 1.0) && near_(g[5], 12.0),
          "A6: parts are exactly 11/11/11/66/1 (+12) of 100");
    surplus_real_t sum = SR_ZERO;
    for (int i = 0; i < 6; i++) sum = SR_ADD(sum, g[i]);
    CHECK(near_(sum, 112.0), "A6: parts sum to 112% of yield");

    /* ---- Anchor 7: an interest-bearing act is refused via onepolicy ------- */
    entries_before = tl.accounts[vs.vino_account].num_entries;
    /* equity 1500 claimed on backing net (2000-1000)=1000 -> 500 implied interest */
    rc = vino_ledger_act(&vs, vid, VINO_CIRCULATE, SR_FROM_INT(2000), SR_FROM_INT(1000),
                         SR_FROM_INT(1500), cid);
    entries_after = tl.accounts[vs.vino_account].num_entries;
    CHECK(rc == VINO_ERR_USURY, "A7: interest-bearing act refused (onepolicy)");
    CHECK(entries_after == entries_before, "A7: nothing posted on usury refusal");

    /* ---- Anchor 8: an act on an UNKNOWN voucher fails CLOSED, posts nothing -- */
    entries_before = tl.accounts[vs.vino_account].num_entries;
    surplus_real_t assets_before8 = tl.total_assets;
    rc = vino_ledger_act(&vs, 0xDEADBEEFULL, VINO_CIRCULATE, SR_FROM_INT(10), SR_FROM_INT(10),
                         SR_ZERO, cid);
    entries_after = tl.accounts[vs.vino_account].num_entries;
    CHECK(rc == VINO_ERR_NOT_FOUND, "A8: unknown voucher id fails closed (NOT_FOUND)");
    CHECK(entries_after == entries_before, "A8: no ledger entries posted for an unknown id");
    CHECK(SR_CMP(tl.total_assets, assets_before8) == 0,
          "A8: ledger totals unchanged for unknown id");

    printf("\n=== %d assertions, %d failures ===\n", g_asserts, g_fails);
    return g_fails ? 1 : 0;
}
