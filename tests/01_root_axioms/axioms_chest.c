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

static const tier_known_t KNOWN_FAILURES[] = {
    {"F-VINO-ADDR", "vino_create_account truncates a >=64-char address and creates an "
                    "account that no lookup can ever find"},
    {"F-VINO-BALCAP", "vino_get_balance does not bound-check `capital` (out-of-range read)"},
    {"F-VINO-NULL", "vino_get_account/vino_transfer dereference a NULL address"},
    {"F-CH-WRAP", "count_house_deposit adds amount to the balance with no overflow check"},
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
    /* CAP_MAX lands inside the account struct (asset_balances), so ASan does not
     * see it, which is why it goes unnoticed; UBSan's array-bounds check does,
     * so under a sanitizer the out-of-range read is recorded, not executed */
    TIER_UB_KNOWN("F-VINO-BALCAP", ok,
                  { ok = vino_get_balance(&V, "alice", CAP_MAX, &out) == -1; });
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
    /* price boundary: balance = price-1, price */
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
    CHECK_KNOWN("F-CH-WRAP", r < 0 && ch.buckets[1].token_balance == UINT64_MAX,
                "deposit past UINT64_MAX -> %d, balance %llu", (int) r,
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

int main(void)
{
    tier_begin("tier1/axioms_chest", KNOWN_FAILURES, TIER_N(KNOWN_FAILURES));
    axiom_vino_accounts();
    axiom_vino_transfer();
    axiom_vino_tables();
    axiom_cc_split();
    axiom_cc_tables();
    axiom_cc_vouchers();
    axiom_count_house();
    axiom_porter_house();
    return tier_end();
}
