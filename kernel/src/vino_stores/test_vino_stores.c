/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* test_vino_stores.c — assert the NUMBERS, not that the code merely ran.
 *
 * Host-only (TEST_HOST): stdio is allowed here and NOWHERE in the module. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "vino_stores.h"
#include "sha256.h"   /* real kernel SHA-256 — a genuine proof_cid, not invented */

static int g_asserts = 0;
static int g_fails   = 0;

#define CHECK(cond, msg) do {                                   \
    g_asserts++;                                                \
    if (!(cond)) { g_fails++;                                   \
        printf("FAIL [%d]: %s\n", g_asserts, (msg)); }          \
    else { printf("ok   [%d]: %s\n", g_asserts, (msg)); }       \
} while (0)

/* On the host, surplus_real_t is double; compare with a small epsilon. */
static int near_(surplus_real_t a, double b) { return fabs((double)a - b) < 1e-6; }

/* A genuine content-addressed witness for the equity rail. */
static void make_cid(const char *tag, uint8_t out[VINO_PROOF_CID_LEN]) {
    sha256((const uint8_t *)tag, (uint32_t)strlen(tag), out);
}

static surplus_real_t coverage_ratio(const triple_ledger_t *tl) {
    if (SR_CMP(tl->total_liabilities, SR_ZERO) <= 0) return SR_FROM_INT(1000000);
    return SR_DIV(tl->total_assets, tl->total_liabilities);
}

int main(void) {
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
    int32_t rc = vino_mint(&vs, &vid, SR_FROM_INT(89), SECOND_MINTS_OIL,
                           VINO_ACTIVE_DIGITAL, cid);
    CHECK(rc == VINO_OK && vid == 1, "mint returns a voucher id");

    /* ---- Anchor 1: a valid act -> coverage >= 1.8x AND equity >= 0 -------- */
    rc = vino_ledger_act(&vs, vid, VINO_CIRCULATE,
                         SR_FROM_INT(2000), SR_FROM_INT(1000),
                         SR_FROM_INT(1000), cid);
    CHECK(rc == VINO_OK, "A1: valid 3-rail act commits");
    CHECK(near_(tl.total_assets, 2000.0), "A1: assets == 2000");
    CHECK(near_(tl.total_liabilities, 1000.0), "A1: liabilities == 1000");
    surplus_real_t equity = SR_SUB(tl.total_assets, tl.total_liabilities);
    CHECK(near_(equity, 1000.0) && SR_CMP(equity, SR_ZERO) >= 0,
          "A1: equity = assets - liabilities == 1000 >= 0");
    surplus_real_t floor18 = SR_DIV(SR_FROM_INT(18), SR_FROM_INT(10));
    CHECK(SR_CMP(coverage_ratio(&tl), floor18) >= 0,
          "A1: coverage assets/liabilities >= 1.8x");

    /* ---- Anchor 2: an act that would drive coverage < 1.8x posts NOTHING -- */
    uint32_t entries_before = tl.accounts[vs.vino_account].num_entries;
    surplus_real_t assets_before = tl.total_assets;
    surplus_real_t liab_before   = tl.total_liabilities;
    /* debit 1000 / credit 1000 -> assets 3000, liab 2000, coverage 1.5 < 1.8 */
    rc = vino_ledger_act(&vs, vid, VINO_CIRCULATE,
                         SR_FROM_INT(1000), SR_FROM_INT(1000),
                         SR_ZERO, cid);
    uint32_t entries_after = tl.accounts[vs.vino_account].num_entries;
    CHECK(rc == VINO_ERR_COVERAGE, "A2: sub-1.8x act refused with COVERAGE");
    CHECK(entries_after == entries_before, "A2: entries_before == entries_after");
    CHECK(near_(tl.total_assets, (double)assets_before) &&
          near_(tl.total_liabilities, (double)liab_before),
          "A2: totals unchanged after rollback");

    /* ---- Anchor 2b: a supplied equity < 0 is refused, nothing posted ----- */
    entries_before = tl.accounts[vs.vino_account].num_entries;
    rc = vino_ledger_act(&vs, vid, VINO_CIRCULATE,
                         SR_FROM_INT(3000), SR_FROM_INT(1000),
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
    CHECK(vino_can_spend(v, VINO_ACTIVE_PHYSICAL),
          "A3: PHYSICAL spend now succeeds");

    /* ---- Anchor 4: vino_phi_draw_max(1000) ~= 618 (φ-1 known answer) ------ */
    surplus_real_t draw = vino_phi_draw_max(SR_FROM_INT(1000));
    CHECK(fabs((double)draw - 618.0) < 1.0, "A4: phi_draw_max(1000) ~= 618");

    /* ---- Anchor 5: the Fibonacci denomination ladder ---------------------- */
    static const uint64_t ladder[VINO_LADDER_RUNGS] =
        {1,2,3,5,8,13,21,34,55,89,144,233};
    int ladder_ok = 1;
    for (uint32_t r = 1; r <= VINO_LADDER_RUNGS; r++) {
        if (vino_ladder_denomination(r) != ladder[r - 1]) ladder_ok = 0;
    }
    CHECK(ladder_ok, "A5: ladder == 1,2,3,5,8,13,21,34,55,89,144,233");
    CHECK(vino_ladder_denomination(0) == 0, "A5: V0 ceremonial note is zero-value");

    /* ---- Anchor 6: the 112% Gratuity split 11/11/11/66/1 (+12) ------------ */
    surplus_real_t g[6];
    rc = vino_gratuity_112(SR_FROM_INT(100), g);
    CHECK(rc == VINO_OK, "A6: gratuity split returns OK");
    CHECK(near_(g[0],11.0) && near_(g[1],11.0) && near_(g[2],11.0) &&
          near_(g[3],66.0) && near_(g[4],1.0) && near_(g[5],12.0),
          "A6: parts are exactly 11/11/11/66/1 (+12) of 100");
    surplus_real_t sum = SR_ZERO;
    for (int i = 0; i < 6; i++) sum = SR_ADD(sum, g[i]);
    CHECK(near_(sum, 112.0), "A6: parts sum to 112% of yield");

    /* ---- Anchor 7: an interest-bearing act is refused via onepolicy ------- */
    entries_before = tl.accounts[vs.vino_account].num_entries;
    /* equity 1500 claimed on backing net (2000-1000)=1000 -> 500 implied interest */
    rc = vino_ledger_act(&vs, vid, VINO_CIRCULATE,
                         SR_FROM_INT(2000), SR_FROM_INT(1000),
                         SR_FROM_INT(1500), cid);
    entries_after = tl.accounts[vs.vino_account].num_entries;
    CHECK(rc == VINO_ERR_USURY, "A7: interest-bearing act refused (onepolicy)");
    CHECK(entries_after == entries_before, "A7: nothing posted on usury refusal");

    /* ---- Anchor 8: an act on an UNKNOWN voucher fails CLOSED, posts nothing -- */
    entries_before = tl.accounts[vs.vino_account].num_entries;
    surplus_real_t assets_before8 = tl.total_assets;
    rc = vino_ledger_act(&vs, 0xDEADBEEFULL, VINO_CIRCULATE,
                         SR_FROM_INT(10), SR_FROM_INT(10), SR_ZERO, cid);
    entries_after = tl.accounts[vs.vino_account].num_entries;
    CHECK(rc == VINO_ERR_NOT_FOUND, "A8: unknown voucher id fails closed (NOT_FOUND)");
    CHECK(entries_after == entries_before, "A8: no ledger entries posted for an unknown id");
    CHECK(SR_CMP(tl.total_assets, assets_before8) == 0, "A8: ledger totals unchanged for unknown id");

    printf("\n=== %d assertions, %d failures ===\n", g_asserts, g_fails);
    return g_fails ? 1 : 0;
}
