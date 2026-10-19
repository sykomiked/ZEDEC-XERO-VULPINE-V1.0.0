/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* integ_escrow.c — Tier 2 integration: an app-store sale end to end.
 *
 *   community_chest (voucher cash-in, purchase into escrow, revenue split)
 *     -> vino (account balances, hash-chained primary journal, audit copy)
 *     -> rmag_core (the exact rational quota each vino transfer mirrors)
 *     -> finance/triple_ledger (the split booked as conventional legs)
 *
 * The modules are not wired to the triple ledger by the kernel (see
 * docs/AUDIT_REPORT.md, integration gaps): this test plays the operator glue
 * the way a settlement service would, booking each escrowed sale into the
 * triple ledger, and then asserts the invariants that must hold across all of
 * them:
 *   I1 conservation: sum of vino CAP_FINANCIAL balances == cash-in - cash-out
 *   I2 escrow:       escrow balance == sum of accepted prices == cc revenue
 *   I3 split:        revenue == developer + platform + royalty totals
 *   I4 legs:         triple-ledger conventional balances sum to zero and the
 *                    developer/platform/royalty accounts equal the cc totals
 *   I5 journal:      the vino hash chain recomputes from primary[], audit[] is
 *                    a byte copy, and REPLAYING primary[] on a fresh ledger
 *                    reproduces every balance (journal replay == state)
 *   I6 rmag:         the quotas vino mirrors sum to zero (transfers only move)
 *   I7 refusals:     a refused purchase changes no module's state
 * Each scenario runs twice in the same process (static state must not leak
 * from one run into the next) and the results must be identical.
 */
#include "tier.h"
#include "vino.h"
#include "community_chest.h"
#include "triple_ledger.h"
#include "rmag_core.h"

static const tier_known_t KNOWN_FAILURES[] = {
    {"F-CC-CASHIN-UNJOURNALED",
     "cc_voucher_cash_in/cash_out change vino balances directly and journal only a zero-amount "
     "self-transfer, so the vino journal cannot reproduce the balances"},
    {"F-RMAG-REINIT", "rmag_init is a no-op after its first call, so vino_init on a new ledger "
                      "keeps the previous ledger's rmag quota mirror (cross-ledger state)"},
};

static vino_ledger_t V, R; /* ~33 MB each: static */
static triple_ledger_t TL;
static community_chest_t CC;

static bool yes_app(const cc_app_t *a)
{
    (void) a;
    return true;
}

static uint64_t sum_fin(const vino_ledger_t *v)
{
    uint64_t s = 0;
    for (uint32_t i = 0; i < v->num_accounts; i++) s += v->balances[i].balance[CAP_FINANCIAL];
    return s;
}

static uint64_t bal(vino_ledger_t *v, const char *a)
{
    vino_account_t *x = vino_get_account(v, a);
    return x ? x->balance[CAP_FINANCIAL] : 0;
}

/* The V2 entry digest written from the chain format in vino.h, over a byte
 * buffer hashed with vino_hash (SHA-256, KAT-checked in tier 1), so the
 * module's own vino_entry_digest is checked against an independent layout. */
static void put32(uint8_t **p, uint32_t x)
{
    for (int i = 0; i < 4; i++) *(*p)++ = (uint8_t) (x >> (8 * i));
}
static void spec_digest(const vino_transaction_t *t, uint8_t out[VINO_HASH_LEN])
{
    uint8_t buf[14 + 1 + VINO_HASH_LEN + 16 + 8 + 12 + 1 + 4 + 3 * 64], *p = buf;
    memcpy(p, "ZXV-VINO-CHAIN", 14);
    p += 14;
    *p++ = 2;
    memcpy(p, t->prev_hash, VINO_HASH_LEN);
    p += VINO_HASH_LEN;
    put32(&p, t->id);
    put32(&p, (uint32_t) t->type);
    put32(&p, (uint32_t) t->capital);
    put32(&p, (uint32_t) t->asset);
    put32(&p, (uint32_t) t->amount);
    put32(&p, (uint32_t) (t->amount >> 32));
    put32(&p, t->timestamp);
    put32(&p, (uint32_t) t->rail);
    put32(&p, (uint32_t) t->msg_type);
    *p++ = t->confirmed ? 1 : 0;
    put32(&p, t->block_height);
    _Static_assert(sizeof t->from_addr == 64 && sizeof t->to_addr == 64 && sizeof t->memo == 64,
                   "the chain format hashes three 64-byte fields");
    memcpy(p, t->from_addr, 64);
    memcpy(p + 64, t->to_addr, 64);
    memcpy(p + 128, t->memo, 64);
    p += 192;
    vino_hash(buf, (uint32_t) (p - buf), out);
}

static bool chain_ok(const vino_ledger_t *v)
{
    uint8_t prev[VINO_HASH_LEN] = {0};
    for (uint32_t i = 0; i < v->num_txns; i++) {
        const vino_transaction_t *t = &v->primary[i], *a = &v->audit[i];
        uint8_t h[VINO_HASH_LEN], ha[VINO_HASH_LEN];
        if (t->chain_ver != VINO_CHAIN_V2_SHA256) return false;
        if (memcmp(t->prev_hash, prev, VINO_HASH_LEN) != 0) return false;
        spec_digest(t, h);
        if (memcmp(h, t->hash, VINO_HASH_LEN) != 0) return false;
        spec_digest(a, ha); /* the audit copy carries the same fields and hash */
        if (a->chain_ver != t->chain_ver || memcmp(ha, h, VINO_HASH_LEN) != 0 ||
            memcmp(a->hash, t->hash, VINO_HASH_LEN) != 0)
            return false;
        memcpy(prev, h, VINO_HASH_LEN);
    }
    return memcmp(prev, v->chain_head_hash, VINO_HASH_LEN) == 0 && v->num_audit == v->num_txns &&
           vino_chain_verify(v, NULL) == VINO_CHAIN_OK;
}

/* I5: replay the journal on a fresh ledger with the same accounts. */
static bool replay_matches(const vino_ledger_t *v)
{
    vino_init(&R, v->node_id);
    for (uint32_t i = 0; i < v->num_accounts; i++)
        vino_create_account(&R, v->balances[i].address, v->balances[i].name);
    for (uint32_t i = 0; i < v->num_txns; i++) {
        const vino_transaction_t *t = &v->primary[i];
        if (t->type == TXN_TRANSFER) {
            /* replay needs the sender funded: credit what the journal says
             * was moved only through recorded transfers */
            if (vino_transfer(&R, t->from_addr, t->to_addr, t->amount, t->capital, t->rail,
                              t->memo) < 0)
                return false;
        } else if (t->type == TXN_ISSUE) {
            vino_issue(&R, t->to_addr, t->asset, t->amount, t->memo);
        }
    }
    for (uint32_t i = 0; i < v->num_accounts; i++)
        for (uint32_t c = 0; c < CAP_MAX; c++)
            if (R.balances[i].balance[c] != v->balances[i].balance[c]) return false;
    return true;
}

/* I6: vino mirrors every transfer into rmag quotas: they must sum to 0. */
static bool rmag_sums_to_zero(uint32_t n)
{
    int64_t s = 0;
    for (uint32_t i = 0; i <= n; i++) {
        rational_t q = rmag_get_quota(i);
        if (q.den != 1) return false;
        s += q.num;
    }
    return s == 0;
}

typedef struct {
    uint64_t revenue, dev, plat, roy, escrow, float_sum, digest;
    uint32_t sales, refused;
} outcome_t;

static uint32_t tl_acct(const char *name, uint32_t entity)
{
    return triple_ledger_create_account(&TL, entity, CAP_FINANCIAL, name);
}

static outcome_t scenario(void)
{
    outcome_t o;
    memset(&o, 0, sizeof o);
    vino_init(&V, 7);
    cc_init(&CC, 7, "store");
    CC.verify_sig = yes_app;
    cc_link_vino(&CC, &V);
    triple_ledger_init(&TL);
    uint32_t t_buyers = tl_acct("buyers-control", 1), t_escrow = tl_acct("escrow", 2),
             t_dev = tl_acct("developers", 3), t_plat = tl_acct("platform", 4),
             t_roy = tl_acct("royalty", 5);

    /* three apps on the share-rule boundaries: min, interior, max (and out of range) */
    word168_t dev = {{9}};
    const uint64_t price[] = {100, 999, 12345, 1};
    const uint32_t share[] = {CC_DEV_SHARE_MIN, 7777, CC_DEV_SHARE_MAX, 20000};
    int32_t id[4];
    for (int i = 0; i < 4; i++) {
        id[i] = cc_list_app(&CC, "app", "d", "dev", &dev, 0, 0, 0, CC_APP_PAID, CC_APP_TOOL,
                            price[i], share[i]);
        CHECK(id[i] > 0, "list app %d", i);
    }
    int32_t free_id =
        cc_list_app(&CC, "free", "d", "dev", &dev, 0, 0, 0, CC_APP_FREE, CC_APP_TOOL, 0, 8000);

    /* buyers cash in different amounts; one cannot afford anything */
    const char *buyer[] = {"b-alice", "b-bob", "b-carol", "b-dave"};
    const uint64_t cash[] = {20000, 1500, 99, 13000};
    uint64_t cashed = 0, out = 0;
    for (int i = 0; i < 4; i++) {
        CHECK(cc_voucher_cash_in(&CC, buyer[i], cash[i], CAP_FINANCIAL) == cash[i], "cash in");
        cashed += cash[i];
    }
    CHECK(sum_fin(&V) == cashed, "I1 after cash-in");

    /* a fixed purchase schedule: (buyer, app) */
    const int sched[][2] = {{0, 0}, {0, 1}, {0, 2}, {1, 1}, {1, 2}, {2, 0}, {3, 2},
                            {3, 3}, {3, 0}, {1, 0}, {2, 3}, {0, 3}, {3, 1}, {2, 2}};
    for (unsigned k = 0; k < TIER_N(sched); k++) {
        const char *b = buyer[sched[k][0]];
        int a = sched[k][1];
        uint64_t before_b = bal(&V, b), before_e = bal(&V, CC_ESCROW_ADDR);
        uint64_t rev0 = CC.total_revenue, dev0 = CC.total_dev_payouts,
                 plat0 = CC.total_platform_revenue, roy0 = CC.total_royalty_revenue;
        uint32_t ntx0 = V.num_txns, tln0 = TL.accounts[t_escrow].num_entries;
        int32_t r = cc_purchase_with_vouchers(&CC, (uint32_t) id[a], b);
        if (r != 0) {
            /* I7: refusal leaves vino, cc and the triple ledger untouched */
            o.refused++;
            CHECK(before_b < price[a], "refused only when the buyer cannot pay (%s, app %d)", b, a);
            CHECK(bal(&V, b) == before_b && bal(&V, CC_ESCROW_ADDR) == before_e &&
                      CC.total_revenue == rev0 && V.num_txns == ntx0 &&
                      TL.accounts[t_escrow].num_entries == tln0,
                  "I7 refused purchase changes nothing");
            continue;
        }
        o.sales++;
        CHECK(bal(&V, b) == before_b - price[a] && bal(&V, CC_ESCROW_ADDR) == before_e + price[a],
              "buyer -> escrow moved exactly the price");
        /* the operator books the sale: buyers -> escrow, then the three shares out */
        uint64_t d = CC.total_dev_payouts - dev0, p = CC.total_platform_revenue - plat0,
                 y = CC.total_royalty_revenue - roy0;
        CHECK(d + p + y == CC.total_revenue - rev0, "I3 this sale's shares sum to its price");
        surplus_real_t one = SR_FROM_INT(1);
        CHECK(triple_ledger_transfer(&TL, t_buyers, t_escrow, CAP_FINANCIAL,
                                     SR_FROM_INT((int64_t) price[a]), one, SR_ZERO, "sale") == 0,
              "book sale");
        if (d)
            triple_ledger_transfer(&TL, t_escrow, t_dev, CAP_FINANCIAL, SR_FROM_INT((int64_t) d),
                                   one, SR_ZERO, "dev");
        if (p)
            triple_ledger_transfer(&TL, t_escrow, t_plat, CAP_FINANCIAL, SR_FROM_INT((int64_t) p),
                                   one, SR_ZERO, "platform");
        if (y)
            triple_ledger_transfer(&TL, t_escrow, t_roy, CAP_FINANCIAL, SR_FROM_INT((int64_t) y),
                                   one, SR_ZERO, "royalty");
    }
    CHECK(cc_purchase_with_vouchers(&CC, (uint32_t) free_id, buyer[0]) == 0, "free app");

    /* one buyer cashes out what is left */
    uint64_t left = bal(&V, buyer[0]);
    CHECK(cc_voucher_cash_out(&CC, buyer[0], left, CAP_FINANCIAL) == left, "cash out");
    out += left;

    /* ---- the invariants ---- */
    CHECK(sum_fin(&V) == cashed - out, "I1 conservation: balances %llu == in %llu - out %llu",
          (unsigned long long) sum_fin(&V), (unsigned long long) cashed, (unsigned long long) out);
    CHECK(CC.voucher_cashin_total == cashed && CC.voucher_cashout_total == out &&
              CC.voucher_float == cashed - out,
          "I1 the store's float agrees with the ledger");
    CHECK(bal(&V, CC_ESCROW_ADDR) == CC.total_revenue, "I2 escrow == revenue");
    CHECK(CC.total_revenue ==
              CC.total_dev_payouts + CC.total_platform_revenue + CC.total_royalty_revenue,
          "I3 revenue == dev + platform + royalty");
    surplus_real_t s = SR_ZERO;
    for (uint32_t i = 0; i < TL.num_accounts; i++)
        s = SR_ADD(s, TL.accounts[i].conventional_balance);
    CHECK(SR_CMP(s, SR_ZERO) == 0, "I4 conventional balances sum to zero");
    CHECK(SR_CMP(TL.accounts[t_dev].conventional_balance,
                 SR_FROM_INT((int64_t) CC.total_dev_payouts)) == 0 &&
              SR_CMP(TL.accounts[t_plat].conventional_balance,
                     SR_FROM_INT((int64_t) CC.total_platform_revenue)) == 0 &&
              SR_CMP(TL.accounts[t_roy].conventional_balance,
                     SR_FROM_INT((int64_t) CC.total_royalty_revenue)) == 0 &&
              SR_CMP(TL.accounts[t_escrow].conventional_balance, SR_ZERO) == 0,
          "I4 the legs equal the store's split totals and escrow is fully paid out");
    CHECK(SR_CMP(TL.total_assets, TL.total_liabilities) == 0, "I4 assets == liabilities");
    CHECK(chain_ok(&V), "I5 vino hash chain and audit copy recompute");
    CHECK(rmag_sums_to_zero(V.num_accounts + 1), "I6 rmag quotas sum to zero");
    CHECK_KNOWN("F-CC-CASHIN-UNJOURNALED", replay_matches(&V),
                "I5 replaying vino's journal reproduces the balances (cash-in is not journaled)");

    o.revenue = CC.total_revenue;
    o.dev = CC.total_dev_payouts;
    o.plat = CC.total_platform_revenue;
    o.roy = CC.total_royalty_revenue;
    o.escrow = bal(&V, CC_ESCROW_ADDR);
    o.float_sum = sum_fin(&V);
    for (uint32_t i = 0; i < VINO_HASH_LEN; i++) o.digest = o.digest * 131 + V.chain_head_hash[i];
    return o;
}

/* vino issue -> triple ledger voucher legs: every unit minted in vino is one
 * unit of a triple-ledger voucher, and the two agree after transfers. */
static void scenario_mint(void)
{
    vino_init(&V, 3);
    triple_ledger_init(&TL);
    const char *who[] = {"m-issuer", "m-a", "m-b"};
    uint32_t acct[3];
    for (int i = 0; i < 3; i++) {
        CHECK(vino_create_account(&V, who[i], "m") >= 0, "mint account");
        acct[i] = triple_ledger_create_account(&TL, (uint32_t) (100 + i), CAP_FINANCIAL, who[i]);
    }
    const uint64_t mint[] = {1, 7, 1000, 65535};
    uint64_t total = 0;
    for (unsigned k = 0; k < TIER_N(mint); k++) {
        int t = 1 + (int) (k & 1);
        CHECK(vino_issue(&V, who[t], ASSET_TOKEN, mint[k], "mint") >= 0, "vino issue %llu",
              (unsigned long long) mint[k]);
        /* the mint is a liability of the issuer and an asset of the holder */
        triple_ledger_post(&TL, acct[t], LEDGER_FINANCIAL, SR_FROM_INT(1), SR_FROM_INT(1), SR_ZERO,
                           SR_FROM_INT((int64_t) mint[k]), SR_ZERO, acct[0], "mint");
        triple_ledger_post(&TL, acct[0], LEDGER_FINANCIAL, SR_FROM_INT(1), SR_FROM_INT(1), SR_ZERO,
                           SR_ZERO, SR_FROM_INT((int64_t) mint[k]), acct[t], "mint");
        total += mint[k];
    }
    uint64_t held = 0;
    for (int i = 1; i < 3; i++) {
        vino_account_t *a = vino_get_account(&V, who[i]);
        held += a->asset_balances[ASSET_TOKEN];
        CHECK(SR_CMP(TL.accounts[acct[i]].conventional_balance,
                     SR_FROM_INT((int64_t) a->asset_balances[ASSET_TOKEN])) == 0,
              "holder %s: vino units == triple-ledger asset leg", who[i]);
    }
    CHECK(held == total, "every minted unit is held");
    CHECK(SR_CMP(TL.accounts[acct[0]].conventional_balance, SR_FROM_INT(-(int64_t) total)) == 0,
          "issuer liability == units minted");
    CHECK(chain_ok(&V) && V.num_txns == TIER_N(mint), "every mint is journaled and chained");
    /* the vino 32-bit unit count at its edge: refused without a trace */
    vino_account_t *a = vino_get_account(&V, who[1]);
    uint32_t room = UINT32_MAX - a->asset_balances[ASSET_TOKEN];
    uint32_t ntx = V.num_txns;
    CHECK(vino_issue(&V, who[1], ASSET_TOKEN, (uint64_t) room + 1, "over") < 0 && V.num_txns == ntx,
          "a mint past the unit counter (MAX+1) is refused and leaves no journal record");
}

int main(void)
{
    tier_begin("tier2/integ_escrow", KNOWN_FAILURES, TIER_N(KNOWN_FAILURES));
    outcome_t a = scenario();
    scenario_mint();
    /* a NEW ledger must not inherit the previous ledger's rmag quotas */
    vino_init(&V, 8);
    bool clean = true;
    for (uint32_t i = 0; i < 64; i++) clean &= rmag_get_quota(i).num == 0;
    CHECK_KNOWN("F-RMAG-REINIT", clean, "after vino_init every rmag quota is 0/1");
    outcome_t b = scenario(); /* again, after another scenario touched the statics */
    CHECK(memcmp(&a, &b, sizeof a) == 0, "the scenario is deterministic across runs");
    CHECK(a.sales > 0 && a.refused > 0, "the schedule exercises both sales and refusals (%u/%u)",
          a.sales, a.refused);
    return tier_end();
}
