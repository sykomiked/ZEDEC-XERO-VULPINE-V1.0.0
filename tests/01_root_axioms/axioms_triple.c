/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* axioms_triple.c — Tier 1 root axioms for kernel/src/finance/triple_ledger.
 * Built twice by verify-all: with -DTEST_HOST (surplus_real_t is a double) and
 * without it (Q32.32 fixed point, the kernel's representation). The amounts
 * used are integers well inside both representations, so every comparison
 * below is exact in both builds.
 */
#include "tier.h"
#include "triple_ledger.h"

static const tier_known_t KNOWN_FAILURES[] = {
    {"F-TL-NULL", "triple_ledger_* dereference a NULL ledger / report pointer"},
    {"F-TL-COVERAGE", "triple_ledger_verify_coverage tests coverage_ratio (already divided by "
                      "1.8) against 1.8 again"},
};

static triple_ledger_t T; /* ~25 MB: static */
#define TL_ACCTS    512u
#define TL_ENTRIES  256u
#define TL_VOUCHERS 64u
#define BAD_ID      0xFFFFFFFFu

static surplus_real_t sum_conventional(void)
{
    surplus_real_t s = SR_ZERO;
    for (uint32_t i = 0; i < T.num_accounts; i++) s = SR_ADD(s, T.accounts[i].conventional_balance);
    return s;
}

static void axiom_accounts(void)
{
    triple_ledger_init(&T);
    for (uint32_t i = 0; i < TL_ACCTS; i++)
        if (triple_ledger_create_account(&T, 1000 + i, CAP_FINANCIAL, "a") != i) {
            CHECK(0, "account %u refused below capacity", i);
            break;
        }
    CHECK(T.num_accounts == TL_ACCTS, "account table at 100%%");
    CHECK(triple_ledger_create_account(&T, 1, CAP_FINANCIAL, "x") == BAD_ID &&
              T.num_accounts == TL_ACCTS,
          "account MAX+1 refused");
    /* name length {62, 63, 64, 200}: always terminated inside name[64] */
    triple_ledger_init(&T);
    char nm[256];
    uint32_t lens[] = {62, 63, 64, 200};
    for (unsigned i = 0; i < TIER_N(lens); i++) {
        memset(nm, 'n', sizeof nm);
        nm[lens[i]] = 0;
        uint32_t id = triple_ledger_create_account(&T, 1, CAP_FINANCIAL, nm);
        size_t l = strnlen(T.accounts[id].name, sizeof T.accounts[id].name);
        CHECK(l < sizeof T.accounts[id].name && l == (lens[i] < 63 ? lens[i] : 63),
              "name of %u chars stored as %zu, terminated", lens[i], l);
    }
    CHECK(triple_ledger_create_account(&T, 1, CAP_FINANCIAL, NULL) != BAD_ID, "NULL name allowed");
}

static void axiom_post_bounds(void)
{
    triple_ledger_init(&T);
    uint32_t a = triple_ledger_create_account(&T, 1, CAP_FINANCIAL, "a");
    uint32_t ids[] = {a, T.num_accounts, T.num_accounts + 1, UINT32_MAX};
    for (unsigned i = 0; i < TIER_N(ids); i++) {
        int32_t r = triple_ledger_post(&T, ids[i], LEDGER_FINANCIAL, SR_ONE, SR_ONE, SR_ZERO,
                                       SR_ONE, SR_ZERO, 0, "p");
        CHECK((r == 0) == (ids[i] < T.num_accounts), "post to account %u", ids[i]);
    }
    uint32_t led[] = {LEDGER_EXTERNALITY, LEDGER_MAX, UINT32_MAX};
    for (unsigned i = 0; i < TIER_N(led); i++) {
        int32_t r = triple_ledger_post(&T, a, (ledger_type_t) led[i], SR_ONE, SR_ONE, SR_ZERO,
                                       SR_ZERO, SR_ZERO, 0, "p");
        CHECK((r == 0) == (led[i] < LEDGER_MAX), "post to ledger %u", led[i]);
    }
    /* entries at 100%: fill, then one more leaves every total untouched */
    while (T.accounts[a].num_entries < TL_ENTRIES)
        if (triple_ledger_post(&T, a, LEDGER_FINANCIAL, SR_ONE, SR_ONE, SR_ZERO, SR_ONE, SR_ZERO, 0,
                               "f") != 0)
            break;
    CHECK(T.accounts[a].num_entries == TL_ENTRIES, "entry table at 100%%");
    surplus_real_t assets = T.total_assets, bal = T.accounts[a].conventional_balance;
    uint64_t next = T.next_entry_id;
    CHECK(triple_ledger_post(&T, a, LEDGER_FINANCIAL, SR_ONE, SR_ONE, SR_ZERO, SR_ONE, SR_ZERO, 0,
                             "x") == -1 &&
              SR_CMP(T.total_assets, assets) == 0 &&
              SR_CMP(T.accounts[a].conventional_balance, bal) == 0 && T.next_entry_id == next,
          "entry MAX+1 refused, no totals or ids consumed");
    /* coverage threshold r*ell vs 1.8: below, at, above */
    triple_ledger_init(&T);
    a = triple_ledger_create_account(&T, 1, CAP_FINANCIAL, "c");
    surplus_real_t r_at = SR_DIV(SR_FROM_INT(18), SR_FROM_INT(10));
    triple_ledger_post(&T, a, LEDGER_PROVENANCE, SR_FROM_INT(1), SR_ONE, SR_ZERO, SR_ZERO, SR_ZERO,
                       0, "below");
    CHECK(!T.accounts[a].entries[0].coverage_verified, "r*ell = 1 < 1.8 not verified");
    triple_ledger_post(&T, a, LEDGER_PROVENANCE, r_at, SR_ONE, SR_ZERO, SR_ZERO, SR_ZERO, 0, "at");
    CHECK(T.accounts[a].entries[1].coverage_verified, "entry: r*ell = 1.8 verified (>=)");
    CHECK(SR_CMP(triple_ledger_account_coverage(&T, a), SR_ONE) == 0,
          "account coverage_ratio = r*ell/1.8 = 1 at the floor");
    CHECK_KNOWN("F-TL-COVERAGE", triple_ledger_verify_coverage(&T, a),
                "verify_coverage compares the ratio r*ell/1.8 against 1.8 (needs r*ell >= 3.24)");
    CHECK(!triple_ledger_verify_coverage(&T, T.num_accounts) &&
              SR_CMP(triple_ledger_account_coverage(&T, T.num_accounts), SR_ZERO) == 0,
          "coverage lookups at the table bound");
    /* provenance / externality never touch the conventional books */
    CHECK(SR_CMP(T.total_assets, SR_ZERO) == 0 &&
              SR_CMP(T.accounts[a].conventional_balance, SR_ZERO) == 0,
          "non-financial ledgers leave conventional totals alone");
}

static void axiom_transfer(void)
{
    triple_ledger_init(&T);
    uint32_t a = triple_ledger_create_account(&T, 1, CAP_FINANCIAL, "a");
    uint32_t b = triple_ledger_create_account(&T, 2, CAP_FINANCIAL, "b");
    surplus_real_t amts[] = {SR_FROM_INT(-1), SR_ZERO, SR_ONE, SR_FROM_INT(1000)};
    for (unsigned i = 0; i < TIER_N(amts); i++) {
        uint32_t ea = T.accounts[a].num_entries;
        int32_t r = triple_ledger_transfer(&T, a, b, CAP_FINANCIAL, amts[i], SR_ONE, SR_ZERO, "t");
        bool legal = SR_CMP(amts[i], SR_ZERO) > 0;
        CHECK((r == 0) == legal, "transfer amount #%u", i);
        CHECK(T.accounts[a].num_entries == ea + (legal ? 3u : 0u), "three legs or none (#%u)", i);
        CHECK(SR_CMP(sum_conventional(), SR_ZERO) == 0, "transfer conserves (#%u)", i);
    }
    CHECK(triple_ledger_transfer(&T, a, a, CAP_FINANCIAL, SR_ONE, SR_ONE, SR_ZERO, "s") == -1,
          "self transfer refused");
    CHECK(triple_ledger_transfer(&T, a, T.num_accounts, CAP_FINANCIAL, SR_ONE, SR_ONE, SR_ZERO,
                                 "s") == -1 &&
              triple_ledger_transfer(&T, T.num_accounts, a, CAP_FINANCIAL, SR_ONE, SR_ONE, SR_ZERO,
                                     "s") == -1,
          "transfer at the account bound");
    /* all six legs or none: the sender at 253 (fits) and 254 (does not) entries */
    uint32_t fill[] = {TL_ENTRIES - 3, TL_ENTRIES - 2};
    for (unsigned i = 0; i < TIER_N(fill); i++) {
        triple_ledger_init(&T);
        a = triple_ledger_create_account(&T, 1, CAP_FINANCIAL, "a");
        b = triple_ledger_create_account(&T, 2, CAP_FINANCIAL, "b");
        while (T.accounts[a].num_entries < fill[i])
            triple_ledger_post(&T, a, LEDGER_PROVENANCE, SR_ONE, SR_ONE, SR_ZERO, SR_ZERO, SR_ZERO,
                               0, "f");
        uint32_t eb = T.accounts[b].num_entries;
        int32_t r = triple_ledger_transfer(&T, a, b, CAP_FINANCIAL, SR_ONE, SR_ONE, SR_ZERO, "t");
        bool fits = fill[i] + 3 <= TL_ENTRIES;
        CHECK((r == 0) == fits, "sender at %u entries: transfer %s", fill[i],
              fits ? "fits" : "refused");
        CHECK(T.accounts[b].num_entries == eb + (fits ? 3u : 0u),
              "receiver gets all legs or none (sender at %u)", fill[i]);
        CHECK(SR_CMP(sum_conventional(), SR_ZERO) == 0, "no half transfer (sender at %u)", fill[i]);
    }
}

static void axiom_vouchers(void)
{
    triple_ledger_init(&T);
    uint32_t a = triple_ledger_create_account(&T, 7, CAP_SOCIAL, "holder");
    uint32_t b = triple_ledger_create_account(&T, 8, CAP_SOCIAL, "other");
    CHECK(triple_ledger_issue_voucher(&T, 1, 99, CAP_SOCIAL, SR_ONE, SR_ONE, "p") == 0,
          "voucher to an entity with no account refused");
    CHECK(triple_ledger_issue_voucher(&T, 1, 7, CAP_FINANCIAL, SR_ONE, SR_ONE, "p") == 0,
          "voucher on a capital the holder has no account for refused");
    uint64_t first = 0, last = 0;
    for (uint32_t i = 0; i < TL_VOUCHERS; i++) {
        uint64_t id = triple_ledger_issue_voucher(&T, 1, 7, CAP_SOCIAL, SR_ONE, SR_ONE, "v");
        if (!id) {
            CHECK(0, "voucher %u refused below capacity", i);
            break;
        }
        if (!first) first = id;
        last = id;
    }
    CHECK(T.accounts[a].num_vouchers == TL_VOUCHERS && last == first + TL_VOUCHERS - 1,
          "voucher table at 100%%, ids dense");
    CHECK(triple_ledger_issue_voucher(&T, 1, 7, CAP_SOCIAL, SR_ONE, SR_ONE, "v") == 0 &&
              T.accounts[a].num_vouchers == TL_VOUCHERS,
          "voucher MAX+1 refused");
    CHECK(triple_ledger_redeem_voucher(&T, first, b) == -1, "a non-holder cannot redeem");
    CHECK(triple_ledger_redeem_voucher(&T, first, T.num_accounts) == -1, "redeem account bound");
    CHECK(triple_ledger_redeem_voucher(&T, 0, a) == -1 &&
              triple_ledger_redeem_voucher(&T, last + 1, a) == -1,
          "voucher ids 0 and last+1 unknown");
    CHECK(triple_ledger_redeem_voucher(&T, first, a) == 0, "holder redeems");
    CHECK(triple_ledger_redeem_voucher(&T, first, a) == -1, "redeem twice refused");
    CHECK(triple_ledger_transfer_voucher(&T, first, 8) == -1, "a redeemed voucher cannot move");
    CHECK(triple_ledger_transfer_voucher(&T, last, 8) == 0 &&
              triple_ledger_redeem_voucher(&T, last, a) == -1 &&
              triple_ledger_redeem_voucher(&T, last, b) == 0,
          "after a transfer only the new holder redeems");
}

static void call_init_null(void *u)
{
    (void) u;
    triple_ledger_init(NULL);
}
static void call_export_null(void *u)
{
    (void) u;
    triple_ledger_export_conventional(&T, NULL);
}

int main(void)
{
    tier_begin(
#ifdef TEST_HOST
        "tier1/axioms_triple (double)",
#else
        "tier1/axioms_triple (Q32.32)",
#endif
        KNOWN_FAILURES, TIER_N(KNOWN_FAILURES));
    axiom_accounts();
    axiom_post_bounds();
    axiom_transfer();
    axiom_vouchers();
    CHECK_KNOWN("F-TL-NULL", !tier_crashes(call_init_null, NULL),
                "triple_ledger_init(NULL) must be a no-op");
    CHECK_KNOWN("F-TL-NULL", !tier_crashes(call_export_null, NULL),
                "export_conventional(tl, NULL) must be a no-op");
    return tier_end();
}
