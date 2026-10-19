/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* integ_count_house.c — Tier 2 integration: stash deposits and minting.
 *
 *   vino (peer accounts -> the count-house vault account; minted units issued
 *         to the node's account as ASSET_TOKEN)
 *     -> count_house (signature-gated stash deposits, trust weights, the
 *        collateral-gated mint, audit)
 *
 * The kernel does not connect the two (docs/AUDIT_REPORT.md lists it as an
 * integration gap); the test plays the operator: a peer's deposit is a vino
 * transfer into the vault, confirmed by count_house_deposit, and undone when
 * the count house refuses the proof. Invariants:
 *   K1 vault balance in vino == sum of every stash bucket's token balance
 *   K2 a refused deposit (bad proof) changes neither ledger
 *   K3 total_supply_minted == sum of accepted mints == units issued in vino
 *   K4 after every accepted mint the node audits clean (no hyperinflation);
 *      a refused mint changes nothing
 *   K5 conservation: peer balances + vault == what was funded
 * Runs twice in one process; both runs must agree.
 */
#include "tier.h"
#include "vino.h"
#include "count_house.h"

static const tier_known_t KNOWN_FAILURES[] = {{"-", "unused"}};

static vino_ledger_t V;
static count_house_t CH;
static bool g_accept;
static bool verify(const stash_bucket_t *b)
{
    (void) b;
    return g_accept;
}

static word168_t peer(unsigned i)
{
    word168_t w;
    memset(&w, 0, sizeof w);
    w.bytes[0] = (uint8_t) (i + 1);
    w.bytes[20] = 0x5A;
    return w;
}

static uint64_t fin(const char *a)
{
    vino_account_t *x = vino_get_account(&V, a);
    return x ? x->balance[CAP_FINANCIAL] : 0;
}

static uint64_t stash_total(void)
{
    uint64_t s = 0;
    for (uint32_t i = 0; i < CH.num_buckets; i++) s += CH.buckets[i].token_balance;
    return s;
}

typedef struct {
    uint64_t stash, minted, vault, units;
    uint32_t accepted, refused, mints;
} outcome_t;

#define NPEERS 6
static outcome_t scenario(void)
{
    outcome_t o;
    memset(&o, 0, sizeof o);
    vino_init(&V, 11);
    count_house_init(&CH, 11, "node");
    CH.verify_sig = verify;
    vino_create_account(&V, "ch-vault", "vault");
    vino_create_account(&V, "ch-node", "node");
    char name[NPEERS][16];
    uint64_t funded = 0;
    for (unsigned i = 0; i < NPEERS; i++) {
        snprintf(name[i], sizeof name[i], "peer-%u", i);
        vino_create_account(&V, name[i], "peer");
        /* test setup: fund each peer directly (the funding source is out of scope) */
        vino_get_account(&V, name[i])->balance[CAP_FINANCIAL] = 10000 + 1000 * i;
        funded += 10000 + 1000 * i;
    }
    uint8_t pk[CH_PUBKEY_LEN] = {1}, sig[CH_PROOF_SIG_LEN] = {2};

    /* a deposit schedule: (peer, amount, proof valid?) */
    const struct {
        unsigned p;
        uint64_t amt;
        bool good;
    } D[] = {{0, 500, true}, {1, 1, true},     {2, 9000, false}, {2, 9000, true},
             {3, 0, true},   {0, 9500, true},  {4, 7000, false}, {5, 15000, true},
             {1, 2, false},  {4, 13999, true}, {3, 13000, true}, {1, 10997, true}};
    for (unsigned k = 0; k < TIER_N(D); k++) {
        const char *who = name[D[k].p];
        uint64_t pb = fin(who), vb = fin("ch-vault"), st = stash_total();
        if (vino_transfer(&V, who, "ch-vault", D[k].amt, CAP_FINANCIAL, RAIL_VINO_NATIVE, "stash") <
            0) {
            CHECK(0, "deposit transfer %u refused by vino", k);
            continue;
        }
        g_accept = D[k].good;
        word168_t id = peer(D[k].p);
        int32_t r = count_house_deposit(&CH, &id, pk, D[k].amt, sig);
        if (r < 0) {
            /* the operator unwinds the vino leg of a refused deposit */
            vino_transfer(&V, "ch-vault", who, D[k].amt, CAP_FINANCIAL, RAIL_VINO_NATIVE,
                          "stash-refund");
            o.refused++;
            CHECK(!D[k].good, "only a bad proof is refused (%u)", k);
            CHECK(fin(who) == pb && fin("ch-vault") == vb && stash_total() == st,
                  "K2 refused deposit %u: both ledgers unchanged", k);
        } else {
            o.accepted++;
            CHECK(D[k].good, "a bad proof is never accepted (%u)", k);
        }
        CHECK(fin("ch-vault") == stash_total(), "K1 vault (%llu) == stash (%llu) after %u",
              (unsigned long long) fin("ch-vault"), (unsigned long long) stash_total(), k);
    }
    uint64_t peers = 0;
    for (unsigned i = 0; i < NPEERS; i++) peers += fin(name[i]);
    CHECK(peers + fin("ch-vault") == funded, "K5 peers + vault == funded");

    /* mint against the stash plus reserves until the collateral gate refuses */
    count_house_set_crypto_reserves(&CH, SR_FROM_INT(50));
    uint64_t step = 1000, minted = 0;
    for (int k = 0; k < 256 && step; k++) {
        uint64_t before = CH.total_supply_minted;
        uint64_t got = count_house_mint(&CH, step);
        if (got == 0) {
            CHECK(CH.total_supply_minted == before, "K4 refused mint changes nothing");
            step /= 2;
            continue;
        }
        CHECK(got == step, "mint returns the amount");
        o.mints++;
        minted += got;
        CHECK(count_house_audit(&CH), "K4 audit clean after mint %d (ratio above the floor)", k);
        CHECK(vino_issue(&V, "ch-node", ASSET_TOKEN, got, "mint") >= 0, "issue minted units");
        step *= 2; /* grow until the gate refuses, then bisect down to it */
    }
    CHECK(step == 0, "the mint gate was reached");
    CHECK(o.mints > 0, "some mints accepted");
    CHECK(CH.total_supply_minted == minted, "K3 supply == accepted mints");
    CHECK(vino_get_account(&V, "ch-node")->asset_balances[ASSET_TOKEN] == minted,
          "K3 units issued in vino == supply");
    CHECK(count_house_mint(&CH, 1) == 0 && CH.total_supply_minted == minted,
          "K4 at the gate: one more unit is refused");

    o.stash = stash_total();
    o.minted = CH.total_supply_minted;
    o.vault = fin("ch-vault");
    o.units = vino_get_account(&V, "ch-node")->asset_balances[ASSET_TOKEN];
    return o;
}

int main(void)
{
    tier_begin("tier2/integ_count_house", KNOWN_FAILURES, 0);
    (void) KNOWN_FAILURES;
    outcome_t a = scenario(), b = scenario();
    CHECK(memcmp(&a, &b, sizeof a) == 0, "the scenario is deterministic across runs");
    CHECK(a.accepted > 0 && a.refused > 0, "both accepted and refused deposits exercised");
    return tier_end();
}
