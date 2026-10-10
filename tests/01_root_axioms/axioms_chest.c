/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* axioms_chest.c — Tier 1 root axioms for the value-holding tables:
 *   kernel/src/vino             (accounts, transactions, assets, peers)
 *   kernel/src/community_chest  (revenue split, app table, voucher float)
 *   kernel/src/count_house      (stash buckets, mint, fractal children)
 *   kernel/src/porter_house     (seal table, allowlists)
 * Each table is driven to exactly 100% and one past it: the overflowing
 * request must be refused and must not change state (fail closed), and a
 * refused request must never leave a door open.
 */
#include "tier.h"
#include "vino.h"
#include "community_chest.h"
#include "count_house.h"
#include "porter_house.h"
#include "rmag_core.h"
#include "lpres_core.h"

static const tier_known_t KNOWN_FAILURES[] = {
    {"F-VINO-ADDR", "vino_create_account truncates a >=64-char address and creates an "
                    "account that no lookup can ever find"},
    {"F-VINO-NULL", "vino_get_account/vino_transfer dereference a NULL address"},
    {"F-VINO-SELFQ", "a self transfer adds the amount to the account's rmag quota (two "
                     "set_quota calls on one ordinal, the second wins)"},
    {"F-RMAG-OVF", "rmag quota arithmetic overflows int64 for a vino transfer of INT64_MAX "
                   "(see axioms_rational.c)"},
    {"F-PH-FULL", "a seal request refused because the seal table is full leaves the port "
                  "admitting everyone (fail open)"},
    {"F-CC-REJECTED", "cc_purchase_with_vouchers/cc_purchase_app take payment for an app whose "
                      "signature was rejected"},
};

static vino_ledger_t V; /* ~33 MB: static */

static void addr_of(char *out, size_t n, unsigned i)
{
    snprintf(out, n, "acct-%05u", i);
}

static void axiom_vino_accounts(void)
{
    vino_init(&V, 1);
    char a[96];
    CHECK(vino_create_account(&V, NULL, "x") == -1 && vino_create_account(&V, "", "x") == -1,
          "NULL / empty address refused");
    for (unsigned i = 0; i < VINO_MAX_ACCOUNTS; i++) {
        addr_of(a, sizeof a, i);
        if (vino_create_account(&V, a, "n") != (int32_t) i) {
            CHECK(0, "account %u refused below capacity", i);
            break;
        }
    }
    CHECK(V.num_accounts == VINO_MAX_ACCOUNTS, "account table at 100%%");
    CHECK(vino_create_account(&V, "one-more", "n") == -1 && V.num_accounts == VINO_MAX_ACCOUNTS,
          "account MAX+1 refused, nothing written");
    addr_of(a, sizeof a, 0);
    CHECK(vino_get_account(&V, a) == &V.balances[0], "first account reachable");
    addr_of(a, sizeof a, VINO_MAX_ACCOUNTS - 1);
    CHECK(vino_get_account(&V, a) == &V.balances[VINO_MAX_ACCOUNTS - 1], "last account reachable");

    /* address length {MAX-2, MAX-1, MAX, MAX+1} where MAX = VINO_ADDR_LEN */
    vino_init(&V, 1);
    uint64_t lens[] = {VINO_ADDR_LEN - 2, VINO_ADDR_LEN - 1, VINO_ADDR_LEN, VINO_ADDR_LEN + 1};
    for (unsigned i = 0; i < TIER_N(lens); i++) {
        memset(a, 'a' + (char) i, sizeof a);
        a[lens[i]] = 0;
        int32_t id = vino_create_account(&V, a, "n");
        if (lens[i] < VINO_ADDR_LEN)
            CHECK(id >= 0 && vino_get_account(&V, a) != NULL, "address length %llu fits",
                  (unsigned long long) lens[i]);
        else
            CHECK_KNOWN("F-VINO-ADDR", id < 0 || vino_get_account(&V, a) != NULL,
                        "address length %llu: created (id %d) but unreachable",
                        (unsigned long long) lens[i], (int) id);
    }
}

static void call_get_null(void *u)
{
    (void) u;
    (void) vino_get_account(&V, NULL);
}

static void axiom_vino_transfer(void)
{
    vino_init(&V, 1);
    vino_create_account(&V, "alice", "A");
    vino_create_account(&V, "bob", "B");
    vino_account_t *al = vino_get_account(&V, "alice"), *bo = vino_get_account(&V, "bob");
    const uint64_t BAL = 1000;
    uint64_t amts[] = {
        0, 1, BAL - 1, BAL, BAL + 1, (uint64_t) INT64_MAX, (uint64_t) INT64_MAX + 1, UINT64_MAX};
    for (unsigned i = 0; i < TIER_N(amts); i++) {
        al->balance[CAP_FINANCIAL] = BAL;
        bo->balance[CAP_FINANCIAL] = 0;
        uint32_t nt = V.num_txns;
        int32_t r =
            vino_transfer(&V, "alice", "bob", amts[i], CAP_FINANCIAL, RAIL_VINO_NATIVE, "t1");
        bool legal = amts[i] <= BAL;
        CHECK((r >= 0) == legal, "transfer %llu of %llu", (unsigned long long) amts[i],
              (unsigned long long) BAL);
        CHECK(al->balance[CAP_FINANCIAL] + bo->balance[CAP_FINANCIAL] == BAL,
              "transfer %llu conserves", (unsigned long long) amts[i]);
        CHECK(V.num_txns == nt + (legal ? 1u : 0u), "txn recorded iff accepted");
    }
    /* receiver at the u64 edge: MAX - amount, then one more */
    al->balance[CAP_FINANCIAL] = 10;
    bo->balance[CAP_FINANCIAL] = UINT64_MAX - 5;
    CHECK(vino_transfer(&V, "alice", "bob", 5, CAP_FINANCIAL, RAIL_VINO_NATIVE, 0) >= 0 &&
              bo->balance[CAP_FINANCIAL] == UINT64_MAX,
          "credit to exactly UINT64_MAX");
    CHECK(vino_transfer(&V, "alice", "bob", 1, CAP_FINANCIAL, RAIL_VINO_NATIVE, 0) == -1 &&
              bo->balance[CAP_FINANCIAL] == UINT64_MAX && al->balance[CAP_FINANCIAL] == 5,
          "credit past UINT64_MAX refused, no state");
    /* capital index at its bound */
    uint32_t caps[] = {CAP_MAX - 1, CAP_MAX, UINT32_MAX};
    for (unsigned i = 0; i < TIER_N(caps); i++) {
        if (caps[i] < CAP_MAX) al->balance[caps[i]] = 1;
        int32_t r =
            vino_transfer(&V, "alice", "bob", 1, (capital_type_t) caps[i], RAIL_VINO_NATIVE, 0);
        CHECK((r >= 0) == (caps[i] < CAP_MAX), "transfer capital %u", caps[i]);
    }
    uint64_t out = 0;
    CHECK(vino_get_balance(&V, "alice", CAP_MAX - 1, &out) == 0, "get_balance last capital");
    CHECK(vino_get_balance(&V, "nobody", CAP_FINANCIAL, &out) == -1, "get_balance unknown");
    /* F-VINO-BALCAP (fixed): CAP_MAX is refused before any read, so this also
     * runs under the sanitizers */
    out = 77;
    CHECK(vino_get_balance(&V, "alice", CAP_MAX, &out) == -1 && out == 77,
          "get_balance CAP_MAX refused, output untouched");
    CHECK(vino_transfer(&V, "alice", "nobody", 1, CAP_FINANCIAL, RAIL_VINO_NATIVE, 0) == -1,
          "transfer to an unknown address");
    CHECK_KNOWN("F-VINO-NULL", !tier_crashes(call_get_null, NULL),
                "vino_get_account(NULL) must return NULL, not crash");
    /* self-transfer: no value created or destroyed */
    al->balance[CAP_FINANCIAL] = 7;
    CHECK(vino_transfer(&V, "alice", "alice", 7, CAP_FINANCIAL, RAIL_VINO_NATIVE, 0) >= 0 &&
              al->balance[CAP_FINANCIAL] == 7,
          "self transfer is the identity");
}

static void axiom_vino_tables(void)
{
    /* issue: the 32-bit count at MAX-1, MAX, MAX+1 */
    vino_init(&V, 1);
    vino_create_account(&V, "alice", "A");
    vino_account_t *al = vino_get_account(&V, "alice");
    CHECK(vino_issue(&V, "alice", ASSET_TOKEN, UINT32_MAX - 1, 0) >= 0, "issue to MAX-1");
    CHECK(vino_issue(&V, "alice", ASSET_TOKEN, 1, 0) >= 0, "issue to MAX");
    CHECK(vino_issue(&V, "alice", ASSET_TOKEN, 1, 0) == -1 &&
              al->asset_balances[ASSET_TOKEN] == UINT32_MAX,
          "issue MAX+1 refused");
    CHECK(vino_issue(&V, "alice", ASSET_MAX, 1, 0) == -1, "issue asset class bound");
    CHECK(vino_issue(&V, "nobody", ASSET_TOKEN, 1, 0) == -1, "issue unknown");
    /* transaction table at 100%: zero-amount self transfers fill it */
    vino_init(&V, 1);
    vino_create_account(&V, "alice", "A");
    al = vino_get_account(&V, "alice");
    al->balance[CAP_FINANCIAL] = 5;
    while (V.num_txns < VINO_MAX_TXNS)
        if (vino_transfer(&V, "alice", "alice", 0, CAP_FINANCIAL, RAIL_VINO_NATIVE, 0) < 0) break;
    CHECK(V.num_txns == VINO_MAX_TXNS, "txn table at 100%%");
    uint8_t head[VINO_HASH_LEN];
    memcpy(head, V.chain_head_hash, sizeof head);
    CHECK(vino_transfer(&V, "alice", "alice", 1, CAP_FINANCIAL, RAIL_VINO_NATIVE, 0) == -1 &&
              vino_issue(&V, "alice", ASSET_TOKEN, 1, 0) == -1 &&
              al->asset_balances[ASSET_TOKEN] == 0 && al->balance[CAP_FINANCIAL] == 5 &&
              memcmp(head, V.chain_head_hash, sizeof head) == 0,
          "full txn table: transfer and issue refused, nothing minted, chain unchanged");
    /* assets and peers at 100% */
    vino_init(&V, 1);
    char s[32];
    for (unsigned i = 0; i < VINO_MAX_ASSETS; i++) {
        snprintf(s, sizeof s, "S%u", i);
        if (vino_register_asset(&V, s, "n", ASSET_TOKEN, 2, 0) != (int32_t) i) break;
    }
    CHECK(V.num_assets == VINO_MAX_ASSETS &&
              vino_register_asset(&V, "X", "n", ASSET_TOKEN, 2, 0) == -1 &&
              V.num_assets == VINO_MAX_ASSETS,
          "asset table full -> -1");
    snprintf(s, sizeof s, "S%u", VINO_MAX_ASSETS - 1);
    CHECK(vino_get_asset(&V, s) == &V.assets[VINO_MAX_ASSETS - 1], "last asset reachable");
    for (unsigned i = 0; i < VINO_MAX_PEERS; i++) {
        snprintf(s, sizeof s, "p%u", i);
        vino_add_peer(&V, s, "ep");
    }
    CHECK(V.num_peers == VINO_MAX_PEERS && vino_add_peer(&V, "x", "ep") == -1 &&
              V.num_peers == VINO_MAX_PEERS,
          "peer table full -> -1");
    CHECK(vino_remove_peer(&V, "nope") == -1 && vino_remove_peer(&V, "p0") == 0, "remove peer");
    char buf[8];
    CHECK(vino_msg_to_iso20022(&V.primary[0], buf, 0) <= 0, "message builder max=0");
}

/* ===== community chest ===== */

static bool yes_app(const cc_app_t *a)
{
    (void) a;
    return true;
}
static bool no_app(const cc_app_t *a)
{
    (void) a;
    return false;
}

static void axiom_cc_split(void)
{
    const uint32_t BPS[] = {0, 1, 6999, 7000, 7001, 8499, 8500, 8501, 10000, 10001, UINT32_MAX};
    for (unsigned i = 0; i < TIER_N(TIER_U64_EDGES); i++)
        for (unsigned j = 0; j < TIER_N(BPS); j++) {
            uint64_t amt = TIER_U64_EDGES[i], plat = 7, roy = 7;
            uint64_t dev = cc_calc_revenue_split(amt, BPS[j], &plat, &roy);
            uint32_t bp = BPS[j] < CC_DEV_SHARE_MIN   ? CC_DEV_SHARE_MIN
                          : BPS[j] > CC_DEV_SHARE_MAX ? CC_DEV_SHARE_MAX
                                                      : BPS[j];
            unsigned __int128 want_dev = (unsigned __int128) amt * bp / 10000;
            unsigned __int128 want_roy = (unsigned __int128) amt * CC_ROYALTY_SHARE / 10000;
            if (want_roy > amt - want_dev) want_roy = amt - want_dev;
            CHECK((unsigned __int128) dev + plat + roy == amt, "split(%llu, %u) sums to the amount",
                  (unsigned long long) amt, BPS[j]);
            CHECK(dev == want_dev && roy == want_roy, "split(%llu, %u) dev/royalty floors",
                  (unsigned long long) amt, BPS[j]);
        }
    CHECK(cc_calc_revenue_split(10000, 7000, NULL, NULL) == 7000, "NULL out pointers allowed");
}

static community_chest_t CC;

static void axiom_cc_tables(void)
{
    word168_t dev = {{1}};
    cc_init(&CC, 1, "store");
    CC.verify_sig = yes_app;
    for (unsigned i = 0; i < CC_MAX_APPS; i++)
        if (cc_list_app(&CC, "app", "d", "dev", &dev, 0, 0, 0, CC_APP_PAID, CC_APP_TOOL, 10, 8000) <
            0) {
            CHECK(0, "listing %u refused below capacity", i);
            break;
        }
    CHECK(CC.num_apps == CC_MAX_APPS, "app table at 100%%");
    CHECK(cc_list_app(&CC, "x", 0, 0, &dev, 0, 0, 0, CC_APP_PAID, CC_APP_TOOL, 1, 8000) == -1 &&
              CC.num_apps == CC_MAX_APPS,
          "listing MAX+1 refused");
    CHECK(cc_get_app(&CC, 1) != NULL && cc_get_app(&CC, CC_MAX_APPS) != NULL &&
              cc_get_app(&CC, CC_MAX_APPS + 1) == NULL && cc_get_app(&CC, 0) == NULL,
          "app ids 1..MAX reachable, 0 and MAX+1 not");
    CHECK(cc_list_app(NULL, "x", 0, 0, &dev, 0, 0, 0, 0, 0, 0, 0) == -1 &&
              cc_list_app(&CC, NULL, 0, 0, &dev, 0, 0, 0, 0, 0, 0, 0) == -1,
          "NULL store / name");
}

static void axiom_cc_vouchers(void)
{
    word168_t dev = {{2}};
    vino_init(&V, 1);
    cc_init(&CC, 1, "store");
    CC.verify_sig = yes_app;
    CHECK(cc_voucher_cash_in(&CC, "alice", 5, CAP_FINANCIAL) == 0, "cash-in without a ledger");
    cc_link_vino(&CC, &V);
    CHECK(cc_voucher_cash_in(&CC, "alice", 0, CAP_FINANCIAL) == 0, "cash-in 0");
    CHECK(cc_voucher_cash_in(&CC, "alice", 5, CAP_MAX) == 0, "cash-in capital bound");
    CHECK(cc_voucher_cash_in(&CC, NULL, 5, CAP_FINANCIAL) == 0, "cash-in NULL address");
    CHECK(cc_voucher_cash_in(&CC, "alice", UINT64_MAX - 1, CAP_FINANCIAL) == UINT64_MAX - 1,
          "cash-in MAX-1");
    CHECK(cc_voucher_cash_in(&CC, "alice", 1, CAP_FINANCIAL) == 1, "cash-in to MAX");
    CHECK(cc_voucher_cash_in(&CC, "alice", 1, CAP_FINANCIAL) == 0 &&
              vino_get_account(&V, "alice")->balance[CAP_FINANCIAL] == UINT64_MAX,
          "cash-in MAX+1 refused");
    CHECK(cc_voucher_cash_out(&CC, "alice", UINT64_MAX, CAP_FINANCIAL) == UINT64_MAX &&
              cc_voucher_cash_out(&CC, "alice", 1, CAP_FINANCIAL) == 0,
          "cash-out exactly the balance, then 1 more refused");
    /* the lifetime cash-in total is now checked money math: once it would
     * wrap, a further cash-in is refused and nothing changes */
    CHECK(cc_voucher_cash_in(&CC, "bob", 99, CAP_FINANCIAL) == 0 &&
              CC.voucher_cashin_total == UINT64_MAX && vino_get_account(&V, "bob") != NULL &&
              vino_get_account(&V, "bob")->balance[CAP_FINANCIAL] == 0,
          "cash-in refused when the lifetime total would wrap");
    /* price boundary on a fresh store: balance = price-1, price */
    vino_init(&V, 1);
    cc_init(&CC, 1, "store");
    CC.verify_sig = yes_app;
    cc_link_vino(&CC, &V);
    int32_t id =
        cc_list_app(&CC, "paid", 0, "d", &dev, 0, 0, 0, CC_APP_PAID, CC_APP_TOOL, 100, 8000);
    cc_voucher_cash_in(&CC, "bob", 99, CAP_FINANCIAL);
    CHECK(cc_purchase_with_vouchers(&CC, (uint32_t) id, "bob") == -2 &&
              vino_get_account(&V, "bob")->balance[CAP_FINANCIAL] == 99 && CC.total_revenue == 0,
          "price-1 refused, nothing moved");
    cc_voucher_cash_in(&CC, "bob", 1, CAP_FINANCIAL);
    CHECK(cc_purchase_with_vouchers(&CC, (uint32_t) id, "bob") == 0 &&
              vino_get_account(&V, "bob")->balance[CAP_FINANCIAL] == 0 &&
              vino_get_account(&V, CC_ESCROW_ADDR)->balance[CAP_FINANCIAL] == 100,
          "exactly the price: buyer -> escrow");
    CHECK(cc_purchase_with_vouchers(&CC, 9999, "bob") == -1 &&
              cc_purchase_with_vouchers(&CC, (uint32_t) id, "nobody") == -2 &&
              cc_purchase_with_vouchers(&CC, (uint32_t) id, NULL) == -2,
          "unknown app / buyer");
    /* a listing whose signature was rejected must not be sold */
    CC.verify_sig = no_app;
    int32_t bad =
        cc_list_app(&CC, "unsigned", 0, "d", &dev, 0, 0, 0, CC_APP_PAID, CC_APP_TOOL, 10, 8000);
    CHECK(bad == -2, "unsigned listing rejected");
    uint32_t bad_id = CC.next_id - 1;
    cc_voucher_cash_in(&CC, "carol", 10, CAP_FINANCIAL);
    int32_t r = cc_purchase_with_vouchers(&CC, bad_id, "carol");
    CHECK_KNOWN("F-CC-REJECTED",
                r != 0 && vino_get_account(&V, "carol")->balance[CAP_FINANCIAL] == 10,
                "purchase of a REJECTED app -> %d (carol left with %llu)", (int) r,
                (unsigned long long) vino_get_account(&V, "carol")->balance[CAP_FINANCIAL]);
    uint64_t rev = CC.total_revenue;
    CHECK_KNOWN("F-CC-REJECTED", cc_purchase_app(&CC, bad_id) != 0 && CC.total_revenue == rev,
                "cc_purchase_app of a REJECTED app books revenue");
}

/* community_chest behaviours a mutation run showed unobserved: the app
 * lifecycle state machine, field copies and their bounds, NULL arguments. */
static void axiom_cc_more(void)
{
    static const char L200[] =
        "01234567890123456789012345678901234567890123456789012345678901234567890123456789"
        "01234567890123456789012345678901234567890123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789";
    word168_t dev = {{3}};
    uint8_t hash[CC_CONTENT_HASH_LEN];
    memset(hash, 0xC3, sizeof hash);

    cc_init(NULL, 1, "x");
    memset(&CC, 0xEE, sizeof CC);
    cc_init(&CC, 4, L200);
    CHECK(strlen(CC.name) == CC_MAX_DEVELOPER_LEN - 1 && CC.num_apps == 0 && !CC.apps[0].active &&
              CC.next_id == 1 && CC.vino == NULL,
          "init clears a dirty store; label truncated to LEN-1");
    cc_init(&CC, 4, NULL);
    CHECK(CC.name[0] == 0, "NULL label is empty");
    CC.verify_sig = yes_app;

    /* field copies, bounds and the share clamp */
    int32_t a1 = cc_list_app(&CC, L200, L200, L200, &dev, NULL, hash, NULL, CC_APP_PAID,
                             CC_APP_TOOL, 100, CC_DEV_SHARE_MIN - 1);
    cc_app_t *A = cc_get_app(&CC, 1);
    CHECK(a1 == 1 && A && strlen(A->name) == CC_MAX_NAME_LEN - 1 &&
              strlen(A->description) == CC_MAX_DESC_LEN - 1 &&
              strlen(A->developer) == CC_MAX_DEVELOPER_LEN - 1 &&
              memcmp(A->description, L200, CC_MAX_DESC_LEN - 1) == 0,
          "first app id 1; name/description/developer truncated to LEN-1");
    CHECK(memcmp(A->content_hash, hash, sizeof hash) == 0 && A->seeder_count == 1 &&
              A->dev_share_bp == CC_DEV_SHARE_MIN &&
              A->platform_share_bp == 10000 - CC_DEV_SHARE_MIN - CC_ROYALTY_SHARE,
          "content hash copied, one seeder, share below MIN clamps to MIN");
    int32_t a2 = cc_list_app(&CC, "b", NULL, NULL, &dev, NULL, NULL, NULL, CC_APP_FREE, CC_APP_TOOL,
                             0, CC_DEV_SHARE_MAX + 1);
    cc_app_t *B = cc_get_app(&CC, 2);
    bool zero = true;
    for (int i = 0; i < CC_CONTENT_HASH_LEN; i++) zero &= B->content_hash[i] == 0;
    CHECK(a2 == 2 && B->description[0] == 0 && B->developer[0] == 0 && zero &&
              B->dev_share_bp == CC_DEV_SHARE_MAX && B->platform_share_bp == 0,
          "NULL strings empty, NULL hash zero, share above MAX clamps (platform floors at 0)");
    CHECK(cc_list_app(&CC, "c", 0, 0, &dev, 0, 0, 0, CC_APP_PAID, CC_APP_TOOL, 5,
                      CC_DEV_SHARE_MIN) == 3 &&
              cc_get_app(&CC, 3)->dev_share_bp == CC_DEV_SHARE_MIN &&
              cc_list_app(&CC, "d", 0, 0, &dev, 0, 0, 0, CC_APP_PAID, CC_APP_TOOL, 5,
                          CC_DEV_SHARE_MAX) == 4 &&
              cc_get_app(&CC, 4)->dev_share_bp == CC_DEV_SHARE_MAX,
          "shares exactly MIN and MAX are kept");
    CC.verify_sig = no_app;
    uint32_t n = CC.num_apps;
    CHECK(cc_list_app(&CC, "r", 0, 0, &dev, 0, 0, 0, CC_APP_PAID, CC_APP_TOOL, 5, 8000) == -2 &&
              CC.num_apps == n + 1 && cc_get_app(&CC, 5)->state == CC_APP_REJECTED,
          "a rejected listing is kept and counted");

    /* lifecycle: verify, download, install, deprecate */
    CHECK(!cc_verify_app(NULL, 1) && !cc_verify_app(&CC, 999), "verify NULL / unknown");
    CHECK(!cc_verify_app(&CC, 5) && cc_get_app(&CC, 5)->state == CC_APP_REJECTED,
          "a failing re-verify leaves REJECTED");
    CHECK(cc_download_app(NULL, 1) == -1 && cc_download_app(&CC, 999) == -1, "download NULL");
    CHECK(cc_download_app(&CC, 5) == -2 && cc_get_app(&CC, 5)->download_count == 0,
          "a REJECTED app cannot be downloaded");
    CC.verify_sig = yes_app;
    CHECK(cc_verify_app(&CC, 5) && cc_get_app(&CC, 5)->state == CC_APP_LISTED,
          "a passing re-verify lists a REJECTED app");
    CHECK(cc_install_app(NULL, 1) == -1 && cc_install_app(&CC, 999) == -1, "install NULL");
    CHECK(cc_install_app(&CC, 1) == -2, "a LISTED (not downloaded) app cannot be installed");
    uint64_t td = CC.total_downloads;
    CHECK(cc_download_app(&CC, 1) == 0 && A->state == CC_APP_VERIFIED && A->download_count == 1 &&
              CC.total_downloads == td + 1,
          "download of a LISTED app verifies it");
    CHECK(cc_download_app(&CC, 1) == 0 && A->download_count == 2, "a VERIFIED app re-downloads");
    CHECK(cc_install_app(&CC, 1) == 0 && A->state == CC_APP_INSTALLED && A->install_count == 1,
          "install a VERIFIED app");
    CHECK(cc_download_app(&CC, 1) == -2 && cc_install_app(&CC, 1) == -2,
          "an INSTALLED app is neither downloaded nor installed again");
    CHECK(cc_verify_app(&CC, 1) && A->state == CC_APP_INSTALLED,
          "re-verifying an INSTALLED app keeps its state");
    CC.verify_sig = no_app;
    cc_verify_app(&CC, 3); /* listed, now unverified */
    CHECK(cc_download_app(&CC, 3) == 0 && cc_get_app(&CC, 3)->state == CC_APP_DOWNLOADING,
          "a download whose re-verification fails stays DOWNLOADING");
    CC.verify_sig = yes_app;
    CHECK(cc_deprecate_app(NULL, 1) == -1 && cc_deprecate_app(&CC, 999) == -1 &&
              cc_deprecate_app(&CC, 4) == 0 && cc_get_app(&CC, 4)->state == CC_APP_DEPRECATED,
          "deprecate");
    CHECK(cc_get_app(NULL, 1) == NULL, "get_app NULL");

    /* direct purchase */
    CHECK(cc_purchase_app(NULL, 1) == -1 && cc_purchase_app(&CC, 999) == -1, "purchase NULL");
    CHECK(cc_purchase_app(&CC, 2) == -2, "a FREE app cannot be purchased");
    int32_t fp = cc_list_app(&CC, "fp", 0, 0, &dev, 0, 0, 0, CC_APP_FREE, CC_APP_TOOL, 5, 8000);
    CHECK(fp > 0 && cc_purchase_app(&CC, (uint32_t) fp) == -2 && CC.total_revenue == 0,
          "a FREE app with a price still cannot be purchased");
    int32_t z = cc_list_app(&CC, "z", 0, 0, &dev, 0, 0, 0, CC_APP_PAID, CC_APP_TOOL, 0, 8000);
    CHECK(cc_purchase_app(&CC, (uint32_t) z) == -2, "a PAID app priced 0 cannot be purchased");
    uint64_t p0, r0, d0 = cc_calc_revenue_split(100, CC_DEV_SHARE_MIN, &p0, &r0);
    CHECK(cc_purchase_app(&CC, 1) == 0 && A->total_revenue == 100 && CC.total_revenue == 100 &&
              CC.total_dev_payouts == d0 && CC.total_platform_revenue == p0 &&
              CC.total_royalty_revenue == r0,
          "purchase books revenue and the three shares");
    CHECK(cc_calc_revenue_split(9999, 8000, &p0, &r0) == 7999 && r0 == 1999 && p0 == 1,
          "split of 9999: floors 7999 / 1999, platform takes the dust");
    CHECK(cc_purchase_with_vouchers(NULL, 1, "x") == -1, "voucher purchase NULL store");

    /* vino link and cash in/out edges */
    vino_init(&V, 2);
    cc_link_vino(&CC, NULL);
    cc_link_vino(NULL, &V);
    CHECK(CC.vino == NULL, "link with NULL does nothing");
    cc_link_vino(&CC, &V);
    CHECK(CC.vino == &V && cc_voucher_cash_in(&CC, "", 5, CAP_FINANCIAL) == 0 &&
              CC.voucher_float == 0,
          "cash-in to an address no account can have refused");
    CHECK(cc_voucher_cash_out(NULL, "a", 1, CAP_FINANCIAL) == 0 &&
              cc_voucher_cash_out(&CC, NULL, 1, CAP_FINANCIAL) == 0 &&
              cc_voucher_cash_out(&CC, "nobody", 1, CAP_FINANCIAL) == 0,
          "cash-out NULL / unknown account");
    cc_voucher_cash_in(&CC, "dan", 14, CAP_FINANCIAL);
    vino_issue(&V, "dan", (asset_class_t) 0, 5, "a"); /* what balance[CAP_MAX] would alias */
    CHECK(cc_voucher_cash_out(&CC, "dan", 0, CAP_FINANCIAL) == 0 &&
              cc_voucher_cash_out(&CC, "dan", 1, CAP_MAX) == 0 && CC.voucher_float == 14 &&
              vino_get_account(&V, "dan")->asset_balances[0] == 5,
          "cash-out of 0 or on capital MAX refused");
    CHECK(cc_voucher_cash_out(&CC, "dan", 1, CAP_FINANCIAL) == 1 &&
              cc_voucher_cash_out(&CC, "dan", 3, CAP_FINANCIAL) == 3 && CC.voucher_float == 10 &&
              V.primary[V.num_txns - 1].amount == 0 &&
              strcmp(V.primary[V.num_txns - 1].memo, "voucher-cashout") == 0,
          "cash-out of 1 and 3; the ledger note moves 0");
    CHECK(cc_voucher_cash_out(&CC, "dan", 10, CAP_FINANCIAL) == 10 && CC.voucher_float == 0 &&
              CC.voucher_cashout_total == 14,
          "cash-out of exactly the float empties it");
    vino_get_account(&V, "dan")->balance[CAP_FINANCIAL] = 7; /* credited outside the store */
    CHECK(cc_voucher_cash_out(&CC, "dan", 7, CAP_FINANCIAL) == 7 && CC.voucher_float == 0,
          "cash-out larger than the float leaves the float at 0");
    /* voucher purchase: escrow cannot be created / the ledger is full */
    vino_init(&V, 3);
    cc_init(&CC, 5, "s");
    CC.verify_sig = yes_app;
    cc_link_vino(&CC, &V);
    int32_t pid = cc_list_app(&CC, "p", 0, 0, &dev, 0, 0, 0, CC_APP_PAID, CC_APP_TOOL, 10, 8000);
    cc_voucher_cash_in(&CC, "erin", 30, CAP_FINANCIAL);
    char addr[16];
    for (unsigned i = V.num_accounts; i < VINO_MAX_ACCOUNTS; i++) {
        snprintf(addr, sizeof addr, "f%u", i);
        vino_create_account(&V, addr, "fill");
    }
    CHECK(cc_purchase_with_vouchers(&CC, (uint32_t) pid, "erin") == -2 &&
              vino_get_account(&V, "erin")->balance[CAP_FINANCIAL] == 30 && CC.total_revenue == 0,
          "no room for the escrow account: refused, nothing moved");
    vino_init(&V, 3);
    cc_voucher_cash_in(&CC, "erin", 30, CAP_FINANCIAL);
    uint32_t keep = V.num_txns;
    V.num_txns = VINO_MAX_TXNS;
    CHECK(cc_purchase_with_vouchers(&CC, (uint32_t) pid, "erin") == -2 && CC.total_revenue == 0,
          "ledger full: refused, no revenue booked");
    V.num_txns = keep;
    uint64_t dl = CC.total_downloads;
    CHECK(cc_purchase_with_vouchers(&CC, (uint32_t) pid, "erin") == 0 &&
              CC.total_downloads == dl + 1 && cc_get_app(&CC, (uint32_t) pid)->download_count == 1,
          "a voucher purchase counts one download");

    /* coverage: r = earning/listed, ell = installed/listed over active apps */
    CHECK(cc_update_coverage(NULL) == SR_ZERO, "coverage of NULL");
    cc_init(&CC, 6, "cov");
    CC.verify_sig = yes_app;
    for (int i = 0; i < 4; i++)
        cc_list_app(&CC, "k", 0, 0, &dev, 0, 0, 0, CC_APP_PAID, CC_APP_TOOL, 1, 8000);
    CC.apps[0].total_revenue = 1; /* LISTED, earning   */
    CC.apps[1].state = CC_APP_INSTALLED;
    CC.apps[2].state = CC_APP_VERIFIED;
    CC.apps[3].state = CC_APP_DEPRECATED;
    CC.apps[3].total_revenue = 9;
    CC.apps[9].active = false; /* stale, inactive */
    CC.apps[9].state = CC_APP_INSTALLED;
    CC.apps[9].total_revenue = 9;
    cc_update_coverage(&CC);
    surplus_real_t third = SR_DIV(SR_FROM_INT(1), SR_FROM_INT(3));
    CHECK(CC.m5.r == third && CC.m5.ell == third &&
              CC.coverage_ratio ==
                  SR_DIV(SR_MUL(third, third), SR_DIV(SR_FROM_INT(18), SR_FROM_INT(10))),
          "coverage over LISTED/INSTALLED/VERIFIED active apps only");
    CC.apps[2].state = CC_APP_DEPRECATED;
    CC.apps[0].state = CC_APP_DEPRECATED;
    cc_update_coverage(&CC);
    CHECK(CC.m5.r == SR_ZERO && CC.m5.ell == SR_ONE, "one listed app, installed, no revenue");
    CC.apps[1].total_revenue = 1;
    cc_update_coverage(&CC);
    CHECK(CC.m5.r == SR_ONE, "one listed app earning 1");

    /* the purchase being the ledger's very first transaction (id 0) */
    vino_init(&V, 4);
    cc_init(&CC, 7, "first");
    CC.verify_sig = yes_app;
    cc_link_vino(&CC, &V);
    int32_t f1 = cc_list_app(&CC, "f", 0, 0, &dev, 0, 0, 0, CC_APP_PAID, CC_APP_TOOL, 9, 8000);
    vino_create_account(&V, "gus", "g");
    vino_get_account(&V, "gus")->balance[CAP_FINANCIAL] = 9;
    CHECK(V.num_txns == 0 && cc_purchase_with_vouchers(&CC, (uint32_t) f1, "gus") == 0 &&
              V.num_txns == 1 && CC.total_revenue == 9,
          "a purchase recorded as transaction 0 succeeds");

    community_chest_t *nv = &CC;
    nv->vino = NULL;
    CHECK(cc_voucher_cash_out(nv, "dan", 1, CAP_FINANCIAL) == 0 &&
              cc_purchase_with_vouchers(nv, 1, "dan") == -3,
          "no ledger linked");
}

/* ===== count house ===== */

static bool yes_bucket(const stash_bucket_t *b)
{
    (void) b;
    return true;
}
static bool no_bucket(const stash_bucket_t *b)
{
    (void) b;
    return false;
}
static word168_t peer(unsigned i)
{
    word168_t w;
    memset(&w, 0, sizeof w);
    w.bytes[0] = (uint8_t) i;
    w.bytes[1] = (uint8_t) (i >> 8);
    w.bytes[20] = 0xA5;
    return w;
}

static void axiom_count_house(void)
{
    static count_house_t ch;
    uint8_t pk[CH_PUBKEY_LEN] = {0}, sig[CH_PROOF_SIG_LEN] = {0};
    count_house_init(&ch, 7, "ch");
    ch.verify_sig = yes_bucket;
    word168_t p0 = peer(0);
    CHECK(count_house_deposit(NULL, &p0, pk, 1, sig) == -2 &&
              count_house_deposit(&ch, NULL, pk, 1, sig) == -2 &&
              count_house_deposit(&ch, &p0, NULL, 1, sig) == -2 &&
              count_house_deposit(&ch, &p0, pk, 1, NULL) == -2 && ch.num_buckets == 0,
          "NULL arguments refused before any bucket is committed");
    for (unsigned i = 0; i < CH_MAX_STASH_BUCKETS; i++) {
        word168_t p = peer(i);
        if (count_house_deposit(&ch, &p, pk, 10, sig) != (int32_t) i) {
            CHECK(0, "bucket %u refused below capacity", i);
            break;
        }
    }
    CHECK(ch.num_buckets == CH_MAX_STASH_BUCKETS, "stash table at 100%%");
    word168_t pn = peer(CH_MAX_STASH_BUCKETS);
    CHECK(count_house_deposit(&ch, &pn, pk, 10, sig) == -1 &&
              ch.num_buckets == CH_MAX_STASH_BUCKETS && count_house_find_bucket(&ch, &pn) == -1,
          "new peer at MAX+1 refused, nothing committed");
    word168_t pl = peer(CH_MAX_STASH_BUCKETS - 1);
    CHECK(count_house_deposit(&ch, &pl, pk, 5, sig) == CH_MAX_STASH_BUCKETS - 1 &&
              ch.buckets[CH_MAX_STASH_BUCKETS - 1].token_balance == 15,
          "an existing peer still tops up when the table is full");
    /* trust bounds: rises to CH_TRUST_MAX and stops; falls to 0 and stops */
    for (int k = 0; k < 40; k++) count_house_deposit(&ch, &p0, pk, 0, sig);
    CHECK(ch.buckets[0].peer_trust_weight == CH_TRUST_MAX, "trust saturates at MAX");
    ch.verify_sig = no_bucket;
    uint64_t bal = ch.buckets[0].token_balance;
    for (int k = 0; k < 10; k++)
        CHECK(count_house_deposit(&ch, &p0, pk, 1, sig) == -2, "unverified deposit refused");
    CHECK(ch.buckets[0].peer_trust_weight == 0 && ch.buckets[0].token_balance == bal &&
              !ch.buckets[0].sig_verified,
          "trust floors at 0, balance rolled back");
    /* balance at the u64 edge */
    ch.verify_sig = yes_bucket;
    ch.buckets[1].token_balance = UINT64_MAX - 1;
    word168_t p1 = peer(1);
    CHECK(count_house_deposit(&ch, &p1, pk, 1, sig) == 1 &&
              ch.buckets[1].token_balance == UINT64_MAX,
          "deposit to exactly UINT64_MAX");
    int32_t r = count_house_deposit(&ch, &p1, pk, 1, sig);
    CHECK(r == -3 && ch.buckets[1].token_balance == UINT64_MAX,
          "deposit past UINT64_MAX -> %d (want -3), balance %llu", (int) r,
          (unsigned long long) ch.buckets[1].token_balance);

    /* mint: amount 0, and the supply bound */
    count_house_init(&ch, 8, "m");
    count_house_set_crypto_reserves(&ch, SR_FROM_INT(1000000));
    CHECK(count_house_mint(&ch, 0) == 0 && count_house_mint(NULL, 1) == 0, "mint 0 / NULL");
    CHECK(count_house_mint(&ch, 1000) == 1000 && ch.total_supply_minted == 1000, "mint 1000");
    CHECK(count_house_mint(&ch, UINT64_MAX) == 0 && ch.total_supply_minted == 1000,
          "mint past the supply bound refused");
    CHECK(count_house_mint(&ch, (uint64_t) 1 << 40) == 0 && ch.total_supply_minted == 1000,
          "mint past the hyperinflation floor refused");
    CHECK(count_house_audit(&ch) && !ch.irq_hyperinflation_detected, "audit passes");

    /* fractal children: 16, then 17; tier rules */
    static count_house_t parent, kids[CH_MAX_CHILDREN + 1], g;
    count_house_init_fractal(&parent, 1, "alliance", CH_SCALE_ALLIANCE);
    for (unsigned i = 0; i <= CH_MAX_CHILDREN; i++)
        count_house_init_fractal(&kids[i], 10 + i, "leaf", CH_SCALE_0);
    for (unsigned i = 0; i < CH_MAX_CHILDREN; i++)
        CHECK(count_house_add_child(&parent, &kids[i]) == 0, "child %u", i);
    CHECK(count_house_add_child(&parent, &kids[CH_MAX_CHILDREN]) == -1 &&
              parent.num_children == CH_MAX_CHILDREN,
          "child MAX+1 refused");
    count_house_init_fractal(&g, 2, "global", CH_SCALE_GLOBAL);
    CHECK(count_house_add_child(&g, &kids[0]) == -2 && count_house_add_child(&kids[0], &g) == -2,
          "tier skipping refused both ways");
    CHECK(count_house_fib_mint_allowance(&parent, UINT64_MAX) == UINT64_MAX &&
              count_house_fib_mint_allowance(&parent, 0) == 0 &&
              count_house_fib_mint_allowance(&parent, 1) == 3,
          "fib allowance saturates, zero base is zero");
}

/* count_house behaviours a mutation run showed unobserved (TEST_HOST: the
 * surplus type is double, and every ratio below is exactly representable). */
static void axiom_count_house_more(void)
{
    static count_house_t ch, parent, kid0, kid1;
    uint8_t pk[CH_PUBKEY_LEN], sig[CH_PROOF_SIG_LEN] = {0};
    memset(pk, 0x5A, sizeof pk);

    /* NULL everywhere fails closed, nothing crashes */
    count_house_init(NULL, 1, "x");
    count_house_init_fractal(NULL, 1, "x", CH_SCALE_0);
    count_house_set_crypto_reserves(NULL, SR_ONE);
    word168_t p0 = peer(0), p1 = peer(1), p2 = peer(2);
    CHECK(count_house_find_bucket(NULL, &p0) == -1 && count_house_valuation(NULL) == SR_ZERO &&
              !count_house_audit(NULL) && count_house_fractal_valuation(NULL) == SR_ZERO &&
              !count_house_fractal_audit(NULL) && count_house_fib_mint_allowance(NULL, 5) == 0,
          "NULL count house");

    /* init over a dirty struct clears it; label bounds */
    memset(&ch, 0xFF, sizeof ch);
    count_house_init(&ch, 3, "0123456789012345678901234567890123456789");
    CHECK(ch.num_buckets == 0 && !ch.buckets[0].active && ch.children[0] == NULL &&
              ch.fractal_level == 0 && strlen(ch.name) == CH_MAX_LABEL_LEN - 1 &&
              memcmp(ch.name, "0123456789012345678901234567890", CH_MAX_LABEL_LEN - 1) == 0,
          "init clears a dirty struct and truncates the label to LEN-1");
    count_house_init(&ch, 3, "0123456789012345678901234567890"); /* exactly LEN-1 */
    CHECK(strcmp(ch.name, "0123456789012345678901234567890") == 0, "label of LEN-1 kept whole");
    count_house_init(&ch, 3, NULL);
    CHECK(ch.name[0] == 0 && count_house_find_bucket(&ch, NULL) == -1, "NULL label is empty");

    /* a new peer whose first deposit fails keeps a zero key of record */
    ch.verify_sig = no_bucket;
    CHECK(count_house_deposit(&ch, &p0, pk, 50, sig) == -2 && ch.num_buckets == 1, "fail new");
    bool zero = true;
    for (int i = 0; i < CH_PUBKEY_LEN; i++) zero &= ch.buckets[0].peer_pubkey[i] == 0;
    CHECK(zero && ch.buckets[0].token_balance == 0, "failed first deposit: key and balance zero");

    /* backing counts only active, signature-verified buckets, weighted by trust */
    ch.verify_sig = yes_bucket;
    CHECK(count_house_deposit(&ch, &p1, pk, 1000, sig) == 1, "verified deposit");
    CHECK(count_house_deposit(&ch, &p2, pk, 1000, sig) == 2, "second verified deposit");
    for (int k = 0; k < 6; k++) count_house_deposit(&ch, &p2, pk, 0, sig); /* trust 450 */
    ch.verify_sig = no_bucket;
    count_house_deposit(&ch, &p2, pk, 1, sig); /* p2: balance stays, unverified, trust 250 */
    CHECK(!ch.buckets[2].sig_verified && ch.buckets[2].peer_trust_weight == 250,
          "p2 unverified with trust left");
    count_house_valuation(&ch);
    CHECK(ch.v_local == 1000.0 * (CH_TRUST_INITIAL + CH_TRUST_INCREMENT) / CH_TRUST_MAX,
          "valuation counts only the verified bucket (%g)", ch.v_local);
    ch.buckets[1].active = false;
    count_house_valuation(&ch);
    CHECK(ch.v_local == 0.0, "an inactive verified bucket is not counted");
    ch.buckets[1].active = true;
    /* a stale active entry past num_buckets is never scanned */
    stash_bucket_t *stale = &ch.buckets[ch.num_buckets];
    stale->active = stale->sig_verified = true;
    stale->token_balance = 1u << 20;
    stale->peer_trust_weight = CH_TRUST_MAX;
    stale->peer_node_id = peer(9);
    count_house_valuation(&ch);
    word168_t p9 = peer(9);
    CHECK(ch.v_local == 150.0 && count_house_find_bucket(&ch, &p9) == -1,
          "scans stop at num_buckets");
    memset(stale, 0, sizeof *stale);
    ch.buckets[1].active = false;
    CHECK(count_house_find_bucket(&ch, &p1) == -1, "an inactive bucket is not found");
    ch.buckets[1].active = true;

    /* mint gate: exact threshold, smallest amount, the supply bound */
    count_house_init(&ch, 4, "g");
    CHECK(count_house_mint(&ch, 1) == 0, "no reserves: mint 1 refused");
    count_house_set_crypto_reserves(&ch, SR_FROM_INT(1));
    CHECK(count_house_mint(&ch, 1001) == 0 && count_house_mint(&ch, 1000) == 1000,
          "ratio exactly 0.001 is allowed, below it refused");
    CHECK(!ch.irq_hyperinflation_detected && ch.irq_priority_dropped,
          "at 0.001: not hyperinflated, priority dropped");
    count_house_set_crypto_reserves(&ch, SR_FROM_INT(100));
    CHECK(!ch.irq_priority_dropped && ch.collateral_ratio == 0.1, "at 0.10 exactly: not dropped");
    count_house_set_crypto_reserves(&ch, SR_FROM_INT(500));
    CHECK(ch.m5.ell == 0.5 && ch.v_fractal == ch.v_local, "ell = ratio inside [0,1]; leaf sync");
    count_house_set_crypto_reserves(&ch, SR_FROM_INT(5000));
    CHECK(ch.m5.ell == SR_ONE && ch.collateral_ratio == 5.0, "ell clamps at 1");
    count_house_set_crypto_reserves(&ch, SR_FROM_INT(-5));
    CHECK(ch.m5.ell == SR_ZERO && ch.irq_hyperinflation_detected, "ell clamps at 0");
    count_house_init(&ch, 5, "s");
    count_house_set_crypto_reserves(&ch, SR_FROM_INT(INT64_MAX));
    CHECK(count_house_mint(&ch, 1) == 1, "mint of 1");
    CHECK(count_house_mint(&ch, (uint64_t) INT64_MAX) == 0 &&
              count_house_mint(&ch, (uint64_t) INT64_MAX) == 0,
          "supply bound + 1 refused");
    CHECK(count_house_mint(&ch, (uint64_t) INT64_MAX - 1) == (uint64_t) INT64_MAX - 1 &&
              ch.total_supply_minted == (uint64_t) INT64_MAX,
          "mint up to exactly the supply bound");
    CHECK(count_house_mint(&ch, 1) == 0, "nothing past the bound");

    /* fractal: unknown scale, level clamp, allowance edges, parent sync, audit */
    count_house_init_fractal(&ch, 6, "u", (ch_scale_t) 99);
    CHECK(ch.fractal_level == 0 && count_house_fib_mint_allowance(&ch, 7) == 7,
          "unknown scale: level 0, allowance F(1)*base");
    ch.fractal_level = CH_FIB_MINT_MAX_LEVEL + 1;
    CHECK(count_house_fib_mint_allowance(&ch, 1) == edp_fibonacci(CH_FIB_MINT_MAX_LEVEL + 1),
          "level clamps at MAX_LEVEL");
    count_house_init_fractal(&parent, 7, "p", CH_SCALE_GLOBAL); /* fib 8 */
    CHECK(count_house_fib_mint_allowance(&parent, UINT64_MAX / 8) == (UINT64_MAX / 8) * 8 &&
              count_house_fib_mint_allowance(&parent, UINT64_MAX / 8 + 1) == UINT64_MAX,
          "allowance: exact at MAX/fib, saturates at MAX/fib+1");
    count_house_init_fractal(&parent, 7, "p", CH_SCALE_ALLIANCE); /* fib 3 */
    count_house_init_fractal(&kid0, 8, "k0", CH_SCALE_0);
    count_house_init_fractal(&kid1, 9, "k1", CH_SCALE_0);
    count_house_set_crypto_reserves(&kid0, SR_FROM_INT(30));
    CHECK(count_house_add_child(NULL, &kid0) == -1 && count_house_add_child(&parent, NULL) == -1,
          "add_child NULL");
    count_house_add_child(&parent, &kid0);
    count_house_add_child(&parent, &kid1);
    CHECK(parent.v_fractal == 10.0, "v_fractal = (0 + 30 + 0) / 3");
    count_house_set_crypto_reserves(&parent, SR_FROM_INT(3));
    CHECK(parent.v_fractal == 10.0 && parent.v_local == 3.0,
          "reserves on a parent leave v_fractal for fractal_valuation");
    CHECK(count_house_fractal_valuation(&parent) == 11.0, "recomputed (3+30+0)/3");
    CHECK(count_house_fractal_audit(&parent), "healthy tree audits");
    kid0.total_supply_minted = 1000000; /* reserves 30 -> 0.00003 */
    CHECK(!count_house_fractal_audit(&parent), "a hyperinflated first child fails the tree");
}

/* ===== porter house ===== */

static void axiom_porter_house(void)
{
    static porter_house_t ph;
    porter_house_init(&ph, 1, "door");
    word168_t a = peer(1), b = peer(2);
    /* ports at the u16 edges */
    CHECK(porter_house_seal_port(&ph, 0, PH_SEAL_CLOSED, 0) == 0 &&
              porter_house_seal_port(&ph, 65535, PH_SEAL_CLOSED, 0) == 1 &&
              !porter_house_admit(&ph, 0, &a, 1000) && !porter_house_admit(&ph, 65535, &a, 1000),
          "ports 0 and 65535 seal and refuse");
    /* trust threshold: min-1, min, MAX */
    porter_house_seal_port(&ph, 80, PH_SEAL_TRUSTED, 500);
    CHECK(!porter_house_admit(&ph, 80, &a, 499) && porter_house_admit(&ph, 80, &a, 500) &&
              porter_house_admit(&ph, 80, &a, UINT32_MAX),
          "trusted seal at min-1 / min / MAX");
    /* allowlist at 100% */
    porter_house_seal_port(&ph, 443, PH_SEAL_ALLOWLIST, 0);
    for (unsigned i = 0; i < PH_MAX_ALLOWLIST; i++) {
        word168_t p = peer(100 + i);
        CHECK(porter_house_allowlist_add(&ph, 443, &p) == 0, "allowlist %u", i);
    }
    word168_t over = peer(100 + PH_MAX_ALLOWLIST);
    CHECK(porter_house_allowlist_add(&ph, 443, &over) == -2 &&
              !porter_house_admit(&ph, 443, &over, 1000),
          "allowlist MAX+1 refused and that peer stays out");
    word168_t last = peer(100 + PH_MAX_ALLOWLIST - 1);
    CHECK(porter_house_admit(&ph, 443, &last, 0) && !porter_house_admit(&ph, 443, NULL, 1000) &&
              !porter_house_admit(&ph, 443, &b, 1000),
          "last listed peer admitted; NULL and unlisted refused");
    CHECK(porter_house_allowlist_add(&ph, 9, &a) == -1, "allowlist on an unsealed port");
    /* unknown mode fails closed */
    porter_house_seal_port(&ph, 81, (ph_seal_mode_t) 9, 0);
    CHECK(!porter_house_admit(&ph, 81, &a, 1000), "unknown seal mode refuses");
    /* seal table at 100%, then a TRUSTED seal on a new port */
    while (ph.num_seals < PH_MAX_SEALS)
        porter_house_seal_port(&ph, (uint16_t) (1000 + ph.num_seals), PH_SEAL_CLOSED, 0);
    CHECK(ph.num_seals == PH_MAX_SEALS, "seal table at 100%%");
    CHECK(porter_house_seal_port(&ph, 7777, PH_SEAL_TRUSTED, 900) == -1 &&
              ph.num_seals == PH_MAX_SEALS,
          "seal MAX+1 refused");
    CHECK_KNOWN("F-PH-FULL", !porter_house_admit(&ph, 7777, &a, 0),
                "port whose TRUSTED seal was refused admits a zero-trust peer");
    CHECK(porter_house_close_port(&ph, 7778) == -1, "close_port with no OPEN slot reports -1");
    CHECK_KNOWN("F-PH-FULL", !porter_house_admit(&ph, 7778, &a, 0),
                "port whose lockdown was refused admits everyone");
    porter_house_open_port(&ph, (uint16_t) (1000 + PH_MAX_SEALS - 1)); /* the last filler */
    CHECK(porter_house_close_port(&ph, 7779) == 0 && !porter_house_admit(&ph, 7779, &a, 1000),
          "close_port reuses an OPEN slot when full");
    CHECK(!porter_house_admit(NULL, 1, &a, 1) && porter_house_seal_port(NULL, 1, 0, 0) == -1,
          "NULL porter house");
}

/* vino_hash is SHA-256 (the V2 audit chain). FIPS 180-2 known answers. */
static const struct {
    const char *msg;
    const char *hex;
} SHA_KAT[] = {
    {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
    {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
    {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
     "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
};
static bool hex_eq(const uint8_t *h, const char *hex)
{
    for (int i = 0; i < VINO_HASH_LEN; i++) {
        unsigned v = 0;
        for (int k = 0; k < 2; k++) {
            char c = hex[2 * i + k];
            v = v * 16 + (unsigned) (c <= '9' ? c - '0' : c - 'a' + 10);
        }
        if (h[i] != v) return false;
    }
    return true;
}

static bool rat_eq(rational_t a, rational_t b)
{
    return a.num == b.num && a.den == b.den;
}

/* Each check here pins one behaviour that a mutation run showed no other test
 * observed (see tests/MUTATION_REPORT.md). */
static void axiom_vino_more(void)
{
    /* hash: SHA-256 known answers, and every length 0..40 gives a distinct digest */
    uint8_t buf[40], h1[VINO_HASH_LEN], hs[41][VINO_HASH_LEN];
    for (unsigned i = 0; i < TIER_N(SHA_KAT); i++) {
        vino_hash(SHA_KAT[i].msg, (uint32_t) strlen(SHA_KAT[i].msg), h1);
        CHECK(hex_eq(h1, SHA_KAT[i].hex), "vino_hash is SHA-256 (KAT %u)", i);
    }
    for (unsigned i = 0; i < sizeof buf; i++) buf[i] = (uint8_t) (i * 37 + 11);
    bool hok = true;
    for (uint32_t n = 0; n <= sizeof buf; n++) {
        vino_hash(buf, n, hs[n]);
        for (uint32_t m = 0; m < n; m++) hok &= memcmp(hs[m], hs[n], VINO_HASH_LEN) != 0;
    }
    CHECK(hok, "vino_hash covers exactly len bytes: lengths 0..40 all differ");
    uint8_t one[1] = {0x80};
    vino_hash(one, 1, h1);
    bool high = false;
    for (int i = 0; i < VINO_HASH_LEN; i++) high |= (h1[i] & 0x01) != 0;
    CHECK(high, "hash bytes use the full low byte");

    vino_init(&V, 9);
    CHECK(V.consensus_threshold == 67 && V.node_id == 9 && !V.is_validator,
          "init: threshold 67, node id, not a validator");
    CHECK(vino_create_account(&V, "a", NULL) == 0 && V.balances[0].name[0] == 0,
          "NULL name stored as empty");
    CHECK(vino_create_account(&V, "b", "Bee") == 1 && strcmp(V.balances[1].name, "Bee") == 0,
          "name stored");
    CHECK(vino_create_account(&V, "cc", "C") == 2, "third account");
    CHECK(vino_get_account(&V, "") == NULL, "empty address finds nothing");
    CHECK(vino_get_account(&V, "cc") == &V.balances[2], "lookup of the last account");
    V.balances[0].balance[CAP_FINANCIAL] = 1000;
    V.balances[1].balance[CAP_FINANCIAL] = 1000;

    /* range of the amount and of the capital index */
    V.balances[0].balance[CAP_SOCIAL] = UINT64_MAX;
    CHECK(vino_transfer(&V, "a", "b", (uint64_t) INT64_MAX + 1, CAP_SOCIAL, 0, 0) == -1,
          "amount INT64_MAX+1 refused (rmag rational is signed)");
    /* INT64_MAX passes vino's own bound, but the rmag mirror then computes
     * quota - INT64_MAX in int64 (F-RMAG-OVF, reached through vino): a fixed
     * vino refuses the amount, or the quota arithmetic stays in range */
    TIER_UB_KNOWN("F-RMAG-OVF", ok, {
        vino_ledger_t *W = &V;
        rational_t q1 = rmag_get_quota(1);
        rational_t q2 = rmag_get_quota(2);
        int32_t r = vino_transfer(W, "a", "b", (uint64_t) INT64_MAX, CAP_SOCIAL, 0, 0);
        ok = r < 0 || (q1.den == 1 && q1.num >= -1 && q2.num <= 0);
        rmag_set_quota(1, q1);
        rmag_set_quota(2, q2);
        if (r >= 0) {
            W->balances[1].balance[CAP_SOCIAL] = 0;
            W->num_txns--;
            W->num_audit--;
            W->txn_count = 0;
            W->total_volume[CAP_SOCIAL] = 0;
            W->balances[0].nonce = 0;
        }
    });
    uint32_t n0 = V.num_txns;
    CHECK(vino_transfer(&V, "a", "b", 0, CAP_MAX, 0, 0) == -1 && V.num_txns == n0,
          "capital CAP_MAX refused even for amount 0");
    V.balances[0].balance[CAP_SOCIAL] = 0;

    /* rmag mirror: account index i is rmag ordinal i+1 */
    rational_t qa = rmag_get_quota(1), qb = rmag_get_quota(2), q0 = rmag_get_quota(0);
    lpres_set_presence(2, TRIT_GLUT_NEUTRAL);
    trit_t pb = lpres_get_presence(2);
    int32_t id = vino_transfer(&V, "a", "b", 7, CAP_FINANCIAL, RAIL_SWIFT, "rent");
    CHECK(id >= 0, "transfer accepted");
    CHECK(rat_eq(rmag_get_quota(1), rmag_sub_quotas(qa, (rational_t){7, 1})) &&
              rat_eq(rmag_get_quota(2), rmag_add_quotas(qb, (rational_t){7, 1})) &&
              rat_eq(rmag_get_quota(0), q0),
          "transfer moves rmag quota from ordinal 1 to ordinal 2 (index+1)");
    CHECK(pb != TRIT_FALSE && lpres_get_presence(2) == pb,
          "a non-FALSE presence is not overwritten by the auto-attest");
    CHECK(lpres_get_presence(1) == TRIT_TRUE, "a FALSE presence is auto-attested TRUE");
    lpres_set_presence(1, TRIT_GLUT_NEUTRAL);
    trit_t pa = lpres_get_presence(1);
    CHECK(vino_transfer(&V, "a", "b", 1, CAP_FINANCIAL, 0, 0) >= 0 && pa != TRIT_TRUE &&
              lpres_get_presence(1) == pa,
          "a sender's non-FALSE presence is not overwritten");
    vino_transfer(&V, "b", "a", 1, CAP_FINANCIAL, 0, 0);
    V.txn_count -= 2;
    V.total_volume[CAP_FINANCIAL] -= 2;
    V.balances[0].nonce -= 1;
    V.balances[1].nonce -= 1;
    vino_transaction_t *t = &V.primary[id];
    CHECK(strcmp(t->memo, "rent") == 0 && t->rail == RAIL_SWIFT && t->amount == 7 &&
              t->type == TXN_TRANSFER,
          "transfer record holds memo, rail, amount");
    CHECK(V.balances[0].nonce == 1 && V.balances[1].nonce == 0, "sender nonce increments");
    CHECK(V.txn_count == 1 && V.total_volume[CAP_FINANCIAL] == 7, "txn_count and volume");
    CHECK(vino_transfer(&V, "a", "b", 3, CAP_FINANCIAL, 0, NULL) >= 0 &&
              V.primary[V.num_txns - 1].memo[0] == 0,
          "NULL memo leaves the memo empty");
    CHECK(V.txn_count == 2 && V.total_volume[CAP_FINANCIAL] == 10 && V.balances[0].nonce == 2,
          "counters accumulate");
    /* self transfer with a single account in slot 0 and a 1-char address */
    qa = rmag_get_quota(1);
    CHECK(vino_transfer(&V, "a", "a", 5, CAP_FINANCIAL, 0, 0) >= 0 &&
              V.balances[0].balance[CAP_FINANCIAL] == 990,
          "self transfer is the identity on the balance");
    CHECK_KNOWN("F-VINO-SELFQ", rat_eq(rmag_get_quota(1), qa),
                "self transfer leaves the rmag quota unchanged");

    id = vino_issue(&V, "cc", ASSET_TOKEN, 4, "mint");
    CHECK(id >= 0 && strcmp(V.primary[id].memo, "mint") == 0 && V.txn_count == 4,
          "issue records memo and counts");
    id = vino_issue(&V, "cc", ASSET_TOKEN, 1, NULL);
    CHECK(id >= 0 && V.primary[id].memo[0] == 0, "issue with NULL memo");

    /* assets */
    CHECK(vino_register_asset(&V, "AAA", "a", ASSET_TOKEN, 2, 10) == 0 &&
              vino_register_asset(&V, "BBB", "b", ASSET_BOND, 0, 5) == 1,
          "assets registered");
    CHECK(vino_get_asset(&V, "AAA") == &V.assets[0] && vino_get_asset(&V, "BBB") == &V.assets[1] &&
              vino_get_asset(&V, "") == NULL && vino_get_asset(&V, "ZZZ") == NULL,
          "asset lookup first/last/empty/unknown");

    /* peers */
    CHECK(vino_add_peer(&V, "p1", "e1") == 0 && vino_add_peer(&V, "p2", "e2") == 1, "peers");
    CHECK(V.peers[0].trust_score == 50 && V.peers[0].connected, "new peer: trust 50, connected");
    CHECK(vino_remove_peer(&V, "p2") == 0 && !V.peers[1].connected && V.peers[0].connected,
          "remove the second peer");
    CHECK(vino_remove_peer(&V, "p1") == 0 && !V.peers[0].connected, "remove the first peer");
    CHECK(vino_remove_peer(&V, "") == -1 && vino_remove_peer(&V, "zz") == -1, "unknown peer -1");

    /* consensus */
    CHECK(vino_propose_block(&V) == -1 && V.block_height == 0, "non-validator cannot propose");
    vino_set_validator(&V, true, 10);
    CHECK(vino_propose_block(&V) == 1 && vino_propose_block(&V) == 2 && V.block_height == 2,
          "validator proposes, height increments");
    vino_set_validator(&V, false, 0);
    CHECK(vino_propose_block(&V) == -1, "validator revoked");

    /* message adapters: buffer minimums and exact output */
    char out[256];
    static const struct {
        int32_t (*fn)(const vino_transaction_t *, char *, uint32_t);
        uint32_t min;
        const char *text;
    } M[] = {
        {vino_msg_to_iso20022, 128, "<Doc:Document><PmtInf><Amt>"},
        {vino_msg_to_camt053, 128, "<BkToCstmrStmt><Stmt><Ntry>"},
        {vino_msg_to_mt103, 64, "{1:F01}{2:I103BANKDEFFXXXXN}{3:{108:VINO}}{4:"},
        {vino_msg_to_pacs008, 64, "<pacs.008><CdtTrfTxInf>"},
    };
    for (unsigned i = 0; i < TIER_N(M); i++) {
        memset(out, 'X', sizeof out);
        CHECK(M[i].fn(t, out, M[i].min - 1) == -1 && out[0] == 'X',
              "adapter %u: max_out MIN-1 refused, nothing written", i);
        memset(out, 0, sizeof out);
        int32_t r = M[i].fn(t, out, M[i].min);
        CHECK(r == (int32_t) strlen(M[i].text) && strcmp(out, M[i].text) == 0,
              "adapter %u: max_out MIN gives the exact fragment", i);
    }

    /* name tables: last valid index and MAX */
    CHECK(strcmp(vino_capital_name(CAP_MAX), "UNKNOWN") == 0 &&
              strcmp(vino_capital_name((capital_type_t) (CAP_MAX - 1)), "SYSTEM") == 0,
          "capital names at MAX-1 / MAX");
    CHECK(strcmp(vino_asset_class_name(ASSET_MAX), "?") == 0 &&
              strcmp(vino_asset_class_name((asset_class_t) (ASSET_MAX - 1)), "Stablecoin") == 0,
          "asset names at MAX-1 / MAX");
    CHECK(strcmp(vino_rail_name(RAIL_MAX), "?") == 0 &&
              strcmp(vino_rail_name((payment_rail_t) (RAIL_MAX - 1)), "Interac") == 0,
          "rail names at MAX-1 / MAX");
    CHECK(strcmp(vino_msg_standard_name(MSG_MAX), "?") == 0 &&
              strcmp(vino_msg_standard_name((msg_standard_t) (MSG_MAX - 1)), "SPFS") == 0,
          "message names at MAX-1 / MAX");
}

/* porter_house behaviours a mutation run showed unobserved. */
static void axiom_porter_more(void)
{
    static porter_house_t ph;
    word168_t p1 = peer(1), p2 = peer(2), p3 = peer(3);
    CHECK(strcmp(ph_seal_mode_name(PH_SEAL_OPEN), "OPEN") == 0 &&
              strcmp(ph_seal_mode_name(PH_SEAL_CLOSED), "CLOSED") == 0 &&
              strcmp(ph_seal_mode_name((ph_seal_mode_t) 4), "UNKNOWN") == 0,
          "seal mode names at 0, last, last+1");
    porter_house_init(NULL, 1, "x");
    porter_house_open_port(NULL, 1);
    CHECK(porter_house_find_seal(NULL, 1) == -1 && porter_house_close_port(NULL, 1) == -1 &&
              porter_house_allowlist_add(NULL, 1, &p1) == -1 &&
              porter_house_update_coverage(NULL) == SR_ZERO,
          "NULL porter house");
    memset(&ph, 0x77, sizeof ph);
    porter_house_init(&ph, 2, "0123456789012345678901234567890123456789");
    CHECK(ph.num_seals == 0 && !ph.seals[0].active && strlen(ph.name) == PH_MAX_LABEL_LEN - 1,
          "init clears a dirty porter house; label truncated to LEN-1");
    porter_house_seal_port(&ph, 1, PH_SEAL_ALLOWLIST, 0);
    CHECK(porter_house_allowlist_add(&ph, 1, NULL) == -1 && ph.seals[0].allowlist_count == 0,
          "allowlist NULL peer on a sealed port");
    porter_house_init(&ph, 2, "0123456789012345678901234567890123456789");

    /* open_port of an unsealed port creates nothing; admit counts it */
    porter_house_open_port(&ph, 5);
    CHECK(ph.num_seals == 0 && porter_house_admit(&ph, 5, &p1, 0) && ph.total_admitted == 1,
          "open of an unsealed port is a no-op; the port admits");

    /* close_port: new seal in an empty table, existing seal keeps min trust */
    CHECK(porter_house_close_port(&ph, 6) == 0 && ph.num_seals == 1 && ph.seals[0].port == 6 &&
              ph.seals[0].mode == PH_SEAL_CLOSED && ph.seals[0].min_trust_weight == 0,
          "close of an unsealed port in an empty table seals slot 0, min trust 0");
    porter_house_seal_port(&ph, 6, PH_SEAL_TRUSTED, 500);
    CHECK(porter_house_close_port(&ph, 6) == 0 && ph.seals[0].mode == PH_SEAL_CLOSED &&
              ph.seals[0].min_trust_weight == 500 && ph.num_seals == 1,
          "close of a sealed port only changes its mode");

    /* allowlist: idempotent over every entry, ignores stale entries */
    porter_house_seal_port(&ph, 7, PH_SEAL_ALLOWLIST, 0);
    int32_t ai = porter_house_find_seal(&ph, 7);
    CHECK(porter_house_allowlist_add(&ph, 7, &p1) == 0 &&
              porter_house_allowlist_add(&ph, 7, &p2) == 0 &&
              porter_house_allowlist_add(&ph, 7, &p2) == 0 && ph.seals[ai].allowlist_count == 2,
          "re-adding the second entry adds nothing");
    ph.seals[ai].allowlist_count = 1; /* p2 is now a stale entry */
    CHECK(!porter_house_admit(&ph, 7, &p2, 1000), "a stale allowlist entry does not admit");
    CHECK(porter_house_allowlist_add(&ph, 7, &p2) == 0 && ph.seals[ai].allowlist_count == 2,
          "a stale entry is re-added for real");
    CHECK(porter_house_admit(&ph, 7, &p2, 0) && !porter_house_admit(&ph, 7, &p3, 1000) &&
              !porter_house_admit(&ph, 7, NULL, 1000),
          "allowlist admits listed peers only");

    /* a stale active seal past num_seals is never found */
    ph.seals[ph.num_seals].active = true;
    ph.seals[ph.num_seals].port = 9;
    ph.seals[ph.num_seals].mode = PH_SEAL_CLOSED;
    CHECK(porter_house_find_seal(&ph, 9) == -1 && porter_house_admit(&ph, 9, &p1, 0),
          "find stops at num_seals");

    /* coverage: staffed = non-OPEN active seals among num_seals */
    porter_house_seal_port(&ph, 8, PH_SEAL_OPEN, 0); /* seals: 6 CLOSED, 7 ALLOW, 8 OPEN */
    ph.seals[ph.num_seals].active = true;            /* a stale CLOSED seal past num_seals */
    ph.seals[ph.num_seals].mode = PH_SEAL_CLOSED;
    porter_house_update_coverage(&ph);
    surplus_real_t two3 = SR_DIV(SR_FROM_INT(2), SR_FROM_INT(3));
    CHECK(ph.m5.ell == two3, "ell = 2 staffed of 3 (slot 0 included, stale slot ignored)");
    memset(&ph.seals[ph.num_seals], 0, sizeof ph.seals[0]);

    /* full table: reuse the OPEN slot 0 for a lockdown, resetting its stats */
    porter_house_init(&ph, 3, "full");
    porter_house_seal_port(&ph, 100, PH_SEAL_TRUSTED, 300);
    porter_house_allowlist_add(&ph, 100, &p1);
    porter_house_admit(&ph, 100, &p1, 400);
    porter_house_admit(&ph, 100, &p1, 100);
    porter_house_open_port(&ph, 100);
    while (ph.num_seals < PH_MAX_SEALS)
        porter_house_seal_port(&ph, (uint16_t) (200 + ph.num_seals), PH_SEAL_CLOSED, 0);
    CHECK(porter_house_close_port(&ph, 50) == 0 && ph.seals[0].port == 50 &&
              ph.seals[0].mode == PH_SEAL_CLOSED && ph.seals[0].min_trust_weight == 0 &&
              ph.seals[0].allowlist_count == 0 && ph.seals[0].admitted_count == 0 &&
              ph.seals[0].rejected_count == 0,
          "full table: the OPEN slot 0 is reused with its stats cleared");
}

int main(void)
{
    tier_begin("tier1/axioms_chest", KNOWN_FAILURES, TIER_N(KNOWN_FAILURES));
    axiom_vino_accounts();
    axiom_vino_transfer();
    axiom_vino_tables();
    axiom_vino_more();
    axiom_cc_split();
    axiom_cc_tables();
    axiom_cc_vouchers();
    axiom_cc_more();
    axiom_count_house();
    axiom_count_house_more();
    axiom_porter_house();
    axiom_porter_more();
    return tier_end();
}
