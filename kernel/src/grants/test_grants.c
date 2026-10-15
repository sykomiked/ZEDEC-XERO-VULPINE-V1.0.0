/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_grants.c — Olive Oil Barrel grants (grants.h G1-G5) on a real
 * pay_ledger: standing boundaries per metric, appeals, exact pro-rata
 * distribution from the funded pool only, the cap, G_RATE, the aggregate
 * snapshot, virtual-until-backed attestation, dispute and expiry, and exact
 * redemption. The ledger is checked after every step. The signature hook is
 * a stand-in (SHA-256 keyed by the signer); production passes ML-DSA. */
#include <stdio.h>
#include <string.h>
#include "grants.h"
#include "../robin_debanks/sha256.h"

static int pass, fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                                   \
        }                                                                                          \
    } while (0)

static pay_ledger_t L;
static grant_t G;
static uint32_t vissuer, allotment, brl[16], vino[16];
static uint64_t nfund;
enum { OWNER = 7, FUNNEL = 90, UW1 = 91, UW2 = 92, LOT = 100 };

/* stand-in signature: SHA-256(secret(signer) || msg) */
static void sign(uint32_t who, const uint8_t *m, uint32_t n, uint8_t sig[32])
{
    uint8_t b[64];
    memset(b, 0, sizeof b);
    b[0] = (uint8_t) who;
    b[1] = 0x5A;
    memcpy(b + 8, m, n);
    sha256(b, 8 + n, sig);
}
static bool verify(void *ctx, uint32_t who, const uint8_t *m, uint32_t n, const uint8_t *sig,
                   uint32_t sl)
{
    (void) ctx;
    uint8_t want[32];
    if (sl != 32 || n > 56) return false;
    sign(who, m, n, want);
    return !memcmp(want, sig, 32);
}

static pay_status_t issue(uint32_t from, uint32_t to, uint64_t amt)
{
    pay_posting_req_t r;
    pay_receipt_t rc;
    memset(&r, 0, sizeof r);
    nfund++;
    snprintf(r.uetr, sizeof r.uetr, "00000000-0000-4000-8000-%012llx", (unsigned long long) nfund);
    snprintf(r.e2e, sizeof r.e2e, "FUND-%llu", (unsigned long long) nfund);
    memcpy(r.idem_key, &nfund, sizeof nfund);
    r.idem_key[31] = 0xEE;
    return pay_ledger_issue(&L, &r, from, to, amt, &rc);
}

static uint32_t open_acct(uint16_t as, uint32_t owner, uint8_t flags)
{
    uint32_t a = 0;
    pay_ledger_open(&L, owner, as, PAY_CAP_FINANCIAL, flags, &a);
    return a;
}

static uint64_t deb(uint32_t a)
{
    return pay_ledger_account(&L, a)->debit;
}

static uint64_t cred(uint32_t a)
{
    return pay_ledger_account(&L, a)->credit;
}

static void setup(void)
{
    static const uint8_t seed[32] = {4, 5, 6, [31] = 0x9C};
    nfund = 0;
    pay_ledger_init(&L, NULL);
    vissuer = open_acct(L.vfv_asset, 1, PAY_ACCT_ISSUER);
    allotment = open_acct(L.vfv_asset, 2, 0);
    issue(vissuer, allotment, (uint64_t) 1 << 59);
    grant_init(&G, &L, L.vfv_asset, OWNER, FUNNEL, verify, NULL, seed);
    for (uint32_t i = 0; i < 16; i++) {
        brl[i] = open_acct(G.barrel, 200 + i, 0);
        vino[i] = open_acct(L.vfv_asset, 200 + i, 0);
    }
    grant_lot_open(&G, LOT);
    grant_underwriter_add(&G, UW1);
    grant_epoch_begin(&G, 1);
}

static grant_metrics_t good(uint32_t subject)
{
    grant_metrics_t m;
    memset(&m, 0, sizeof m);
    m.subject = subject;
    m.uptime_bps = GRANT_DEF_UPTIME_BPS;
    m.volume = GRANT_DEF_MIN_VOLUME;
    m.stake = GRANT_DEF_MIN_STAKE;
    return m;
}

static grant_recipient_t rcp(uint32_t i, uint64_t w)
{
    grant_recipient_t r;
    r.m = good(300 + i);
    r.account = brl[i];
    r.weight = w;
    return r;
}

static grant_status_t appeal(uint32_t subject, uint64_t epoch, uint32_t mask, uint32_t signer)
{
    uint8_t m[GRANT_APPEAL_MSG], sig[32];
    grant_appeal_msg(subject, epoch, mask, m);
    sign(signer, m, sizeof m, sig);
    return grant_appeal(&G, subject, epoch, mask, sig, 32);
}

static grant_status_t attest(uint32_t lot, uint64_t count, uint32_t uw, uint64_t expiry,
                             uint64_t now, uint32_t signer)
{
    uint8_t m[GRANT_ATTEST_MSG], sig[32];
    grant_attest_msg(lot, count, uw, expiry, m);
    sign(signer, m, sizeof m, sig);
    return grant_attest(&G, lot, count, uw, expiry, now, sig, 32);
}

static grant_status_t dispute(uint32_t lot, uint64_t expiry, uint32_t signer)
{
    uint8_t m[GRANT_DISPUTE_MSG], sig[32];
    grant_dispute_msg(lot, expiry, m);
    sign(signer, m, sizeof m, sig);
    return grant_dispute(&G, lot, sig, 32);
}

static void test_init(void)
{
    setup();
    grant_t g2;
    static const uint8_t seed[32] = {1};
    CHECK(grant_init(NULL, &L, L.vfv_asset, 1, 1, verify, NULL, seed) == GRANT_ERR_ARG &&
              grant_init(&g2, NULL, 0, 1, 1, verify, NULL, seed) == GRANT_ERR_ARG &&
              grant_init(&g2, &L, 0, 1, 1, NULL, NULL, seed) == GRANT_ERR_ARG &&
              grant_init(&g2, &L, (uint16_t) L.n_assets, 1, 1, verify, NULL, seed) == GRANT_ERR_ARG,
          "init argument checks");
    CHECK(grant_init(&g2, &L, L.vfv_asset, 1, 1, verify, NULL, NULL) == GRANT_ERR_LEDGER &&
              g2.L == 0,
          "a second BRL-V on one ledger is refused and the instance stays unusable");
    CHECK(grant_epoch_begin(&g2, 5) == GRANT_ERR_ARG &&
              grant_pool_credit(&g2, allotment, 1, 0) == GRANT_ERR_ARG,
          "an unusable instance refuses work");
    const pay_asset_t *a = pay_ledger_asset(&L, G.barrel);
    CHECK(a && !strcmp(a->code, "BRL-V") && a->kind == PAY_ASSET_UNIT && !a->iso4217 &&
              a->minor == 0,
          "BRL-V is a non-ISO unit asset with whole barrels");
    const pay_account_t *is = pay_ledger_account(&L, G.issuer),
                        *po = pay_ledger_account(&L, G.pool),
                        *re = pay_ledger_account(&L, G.reserve);
    CHECK(is->asset == G.barrel && (is->flags & PAY_ACCT_ISSUER) && is->owner == OWNER,
          "barrel issuer account");
    CHECK(po->asset == L.vfv_asset && re->asset == L.vfv_asset && po->flags == 0 &&
              re->flags == 0 && po->owner == OWNER && G.pool != G.reserve,
          "pool and reserve are plain Vino accounts");
    CHECK(grant_pool_account(&G) == G.pool && grant_pool_account(NULL) == 0xFFFFFFFFu,
          "pool account exposed");
    CHECK(G.th.min_uptime_bps == 9500 && G.th.min_volume == 10000 && G.th.min_stake == 100000 &&
              G.cap_num == 1 && G.cap_den == 1 && G.funnel == FUNNEL,
          "module defaults");
    CHECK(G.seed[0] == 4 && G.seed[2] == 6 && G.seed[31] == 0x9C, "seed copied");
    CHECK(grant_check(&G) && !grant_check(NULL) && !grant_check(&g2), "fresh state checks");
}

static void test_setters(void)
{
    setup();
    grant_thresholds_t t = {10000, 5, 6};
    CHECK(grant_set_thresholds(&G, &t) == GRANT_OK && G.th.min_uptime_bps == 10000 &&
              G.th.min_volume == 5 && G.th.min_stake == 6,
          "thresholds set (10000 bps allowed)");
    t.min_uptime_bps = 10001;
    CHECK(grant_set_thresholds(&G, &t) == GRANT_ERR_ARG && G.th.min_uptime_bps == 10000,
          "10001 bps refused");
    CHECK(grant_set_thresholds(&G, NULL) == GRANT_ERR_ARG &&
              grant_set_thresholds(NULL, &t) == GRANT_ERR_ARG,
          "null thresholds");
    CHECK(grant_set_cap(&G, 0, 5) == GRANT_ERR_ARG && grant_set_cap(&G, 6, 5) == GRANT_ERR_ARG &&
              grant_set_cap(NULL, 1, 1) == GRANT_ERR_ARG,
          "cap must be 0 < num <= den");
    CHECK(grant_set_cap(&G, 5, 5) == GRANT_OK && grant_set_cap(&G, 1, 3) == GRANT_OK &&
              G.cap_num == 1 && G.cap_den == 3,
          "cap set");
    CHECK(grant_underwriter_add(&G, UW1) == GRANT_OK && G.n_underw == 1,
          "a listed underwriter is not added twice");
    for (uint32_t i = 1; i < GRANT_MAX_UNDERW; i++)
        CHECK(grant_underwriter_add(&G, 500 + i) == GRANT_OK, "underwriter added");
    CHECK(grant_underwriter_add(&G, 999) == GRANT_ERR_FULL &&
              grant_underwriter_add(&G, 500 + GRANT_MAX_UNDERW - 1) == GRANT_OK &&
              grant_underwriter_add(NULL, 1) == GRANT_ERR_ARG,
          "underwriter list full; a listed one is still fine");
    CHECK(grant_epoch_begin(&G, 1) == GRANT_ERR_EPOCH &&
              grant_epoch_begin(&G, 0) == GRANT_ERR_EPOCH &&
              grant_epoch_begin(NULL, 9) == GRANT_ERR_ARG,
          "epoch must rise");
    CHECK(grant_epoch_begin(&G, 2) == GRANT_OK && G.epoch == 2 && G.hist[2].epoch == 2,
          "epoch 2 begins");
    CHECK(grant_lot_open(&G, LOT) == GRANT_ERR_ARG && grant_lot_open(NULL, 1) == GRANT_ERR_ARG,
          "lot ids are unique");
    for (uint32_t i = 1; i < GRANT_MAX_LOTS; i++)
        CHECK(grant_lot_open(&G, LOT + i) == GRANT_OK, "lot opened");
    CHECK(grant_lot_open(&G, 1) == GRANT_ERR_FULL, "lot table full");
    CHECK(grant_lot(&G, LOT + 3) && grant_lot(&G, LOT + 3)->id == LOT + 3 &&
              grant_lot(&G, LOT + 3)->count == 0 && !grant_lot(&G, 1) && !grant_lot(NULL, LOT),
          "lot lookup");
}

static void test_eval(void)
{
    setup();
    uint32_t r = 0xFFFF;
    grant_metrics_t m = good(1);
    CHECK(grant_eval(&G, &m, 1, &r) && r == 0, "at every threshold: eligible");
    CHECK(grant_eval(&G, &m, 1, NULL), "reasons pointer optional");
    m.uptime_bps = 9499;
    CHECK(!grant_eval(&G, &m, 1, &r) && r == GRANT_FAIL_UPTIME, "uptime 9499 fails");
    m.uptime_bps = 10000;
    CHECK(grant_eval(&G, &m, 1, &r) && r == 0, "uptime 10000 fine");
    m.uptime_bps = 10001;
    CHECK(!grant_eval(&G, &m, 1, &r) && r == GRANT_FAIL_INVALID, "uptime above 10000 is invalid");
    m = good(1);
    m.dropped_reports = 1;
    CHECK(!grant_eval(&G, &m, 1, &r) && r == GRANT_FAIL_DROPPED, "one dropped-tick report");
    m = good(1);
    m.routing_reports = 1;
    CHECK(!grant_eval(&G, &m, 1, &r) && r == GRANT_FAIL_ROUTING, "one malicious-routing report");
    m = good(1);
    m.failed_challenges = 1;
    CHECK(!grant_eval(&G, &m, 1, &r) && r == GRANT_FAIL_AUDIT, "one failed challenge");
    m = good(1);
    m.fraud_findings = 1;
    CHECK(!grant_eval(&G, &m, 1, &r) && r == GRANT_FAIL_FRAUD, "one fraud finding");
    m = good(1);
    m.volume = 9999;
    CHECK(!grant_eval(&G, &m, 1, &r) && r == GRANT_FAIL_VOLUME, "volume 9999 fails");
    m = good(1);
    m.stake = 99999;
    CHECK(!grant_eval(&G, &m, 1, &r) && r == GRANT_FAIL_STAKE, "stake 99999 fails");
    memset(&m, 0, sizeof m);
    m.dropped_reports = m.routing_reports = m.failed_challenges = m.fraud_findings = 3;
    CHECK(!grant_eval(&G, &m, 1, &r) && r == GRANT_FAIL_ALL, "everything wrong: every bit");
    CHECK(!grant_eval(&G, NULL, 1, &r) && r == GRANT_FAIL_INVALID && !grant_eval(NULL, &m, 1, &r) &&
              r == GRANT_FAIL_INVALID,
          "null is invalid");
    grant_thresholds_t t = {0, 0, 0};
    grant_set_thresholds(&G, &t);
    memset(&m, 0, sizeof m);
    CHECK(grant_eval(&G, &m, 1, &r) && r == 0, "zero thresholds admit zero metrics");
}

static void test_appeal(void)
{
    setup();
    grant_metrics_t m = good(42);
    m.volume = 0;
    m.fraud_findings = 2;
    uint32_t r;
    CHECK(appeal(42, 1, GRANT_FAIL_VOLUME, UW1) == GRANT_ERR_SIG, "only the funnel can rule");
    CHECK(appeal(42, 1, 0, FUNNEL) == GRANT_ERR_ARG &&
              appeal(42, 1, GRANT_FAIL_INVALID, FUNNEL) == GRANT_ERR_ARG &&
              appeal(42, 1, 0x100, FUNNEL) == GRANT_ERR_ARG,
          "empty or out-of-range masks refused");
    uint8_t sig[32] = {0};
    CHECK(grant_appeal(&G, 42, 1, 1, NULL, 32) == GRANT_ERR_ARG &&
              grant_appeal(NULL, 42, 1, 1, sig, 32) == GRANT_ERR_ARG,
          "null appeal arguments");
    CHECK(appeal(42, 0, GRANT_FAIL_VOLUME, FUNNEL) == GRANT_ERR_EPOCH, "past epoch refused");
    CHECK(appeal(42, 1, GRANT_FAIL_VOLUME, FUNNEL) == GRANT_OK, "volume appeal accepted");
    CHECK(!grant_eval(&G, &m, 1, &r) && r == GRANT_FAIL_FRAUD, "only the ruled check is cleared");
    CHECK(appeal(42, 1, GRANT_FAIL_FRAUD, FUNNEL) == GRANT_OK, "a second ruling merges");
    CHECK(grant_eval(&G, &m, 1, &r) && r == 0, "both cleared for this epoch");
    uint32_t used = 0;
    for (uint32_t i = 0; i < GRANT_MAX_APPEALS; i++) used += G.appeal[i].used;
    CHECK(used == 1, "merged into one slot");
    CHECK(appeal(44, 1, GRANT_FAIL_STAKE, FUNNEL) == GRANT_OK &&
              appeal(44, 1, GRANT_FAIL_VOLUME, FUNNEL) == GRANT_OK && G.appeal[1].subject == 44 &&
              G.appeal[1].mask == (GRANT_FAIL_STAKE | GRANT_FAIL_VOLUME),
          "a ruling in a later slot merges too");
    grant_metrics_t p = good(44);
    p.stake = 0;
    p.volume = 0;
    CHECK(grant_eval(&G, &p, 1, &r) && r == 0, "a ruling in a later slot is applied");
    G.appeal[1].used = false;
    CHECK(!grant_eval(&G, &m, 2, &r) && r == (GRANT_FAIL_FRAUD | GRANT_FAIL_VOLUME),
          "not for another epoch");
    grant_metrics_t o = m;
    o.subject = 43;
    CHECK(!grant_eval(&G, &o, 1, &r) && r == (GRANT_FAIL_FRAUD | GRANT_FAIL_VOLUME),
          "not for another subject");
    m.uptime_bps = 20000;
    CHECK(!grant_eval(&G, &m, 1, &r) && r == GRANT_FAIL_INVALID, "invalid survives an appeal");
    CHECK(appeal(42, 2, GRANT_FAIL_STAKE, FUNNEL) == GRANT_OK, "future-epoch ruling");
    CHECK(appeal(45, 1, GRANT_FAIL_STAKE, FUNNEL) == GRANT_OK && G.appeal[2].used,
          "a second epoch-1 ruling, in slot 2");
    grant_epoch_begin(&G, 2);
    used = 0;
    for (uint32_t i = 0; i < GRANT_MAX_APPEALS; i++) used += G.appeal[i].used;
    CHECK(used == 1 && !G.appeal[0].used && G.appeal[1].used && G.appeal[1].epoch == 2 &&
              !G.appeal[2].used,
          "epoch 1 rulings freed, epoch 2 ruling kept");
    for (uint32_t i = 1; i < GRANT_MAX_APPEALS; i++)
        CHECK(appeal(1000 + i, 2, GRANT_FAIL_STAKE, FUNNEL) == GRANT_OK, "appeal stored");
    CHECK(appeal(5000, 2, GRANT_FAIL_STAKE, FUNNEL) == GRANT_ERR_FULL, "appeal table full");
    CHECK(appeal(1000 + GRANT_MAX_APPEALS - 1, 2, GRANT_FAIL_VOLUME, FUNNEL) == GRANT_OK,
          "the last stored subject still merges when full");
    CHECK(G.appeal[GRANT_MAX_APPEALS - 1].mask == (GRANT_FAIL_STAKE | GRANT_FAIL_VOLUME),
          "merged mask");
}

static void test_credit_and_rate(void)
{
    setup();
    uint64_t n, d;
    bool def;
    CHECK(grant_pool_credit(&G, allotment, 0, 1) == GRANT_ERR_ARG &&
              grant_pool_credit(NULL, allotment, 5, 1) == GRANT_ERR_ARG,
          "zero credit refused");
    CHECK(grant_pool_credit(&G, allotment, ((uint64_t) 1 << 59) + 1, 1) == GRANT_ERR_ARG,
          "above one line's limit refused");
    uint64_t seq = L.seq;
    CHECK(grant_pool_credit(&G, allotment, 500, 1) == GRANT_OK && deb(G.pool) == 500 &&
              L.seq == seq + 1 && G.hist[1].inflow == 500 && grant_check(&G),
          "allotment slice credited");
    CHECK(grant_pool_credit(&G, vino[0], 1, 1) == GRANT_ERR_LEDGER &&
              G.last_ledger == PAY_ERR_FUNDS && G.hist[1].inflow == 500,
          "an unfunded credit posts nothing and counts nothing");
    CHECK(grant_rate(&G, 1, &n, &d, &def) == GRANT_OK && !def && n == 0 && d == 0,
          "no weight yet: 0/0 undefined");
    grant_recipient_t r[3] = {rcp(0, 1), rcp(1, 1), rcp(2, 1)};
    grant_dist_t out;
    CHECK(grant_epoch_distribute(&G, LOT, r, 3, 100, 2, &out) == GRANT_OK && out.barrels == 5,
          "distributed");
    CHECK(grant_rate(&G, 1, &n, &d, &def) == GRANT_OK && def && n == 500 && d == 3,
          "G_RATE = 500/3 exactly");
    CHECK(grant_rate(&G, 2, &n, &d, &def) == GRANT_ERR_NOT_FOUND &&
              grant_rate(&G, 9, &n, &d, &def) == GRANT_ERR_NOT_FOUND,
          "unknown epochs");
    CHECK(grant_rate(NULL, 1, &n, &d, &def) == GRANT_ERR_ARG &&
              grant_rate(&G, 1, NULL, &d, &def) == GRANT_ERR_ARG &&
              grant_rate(&G, 1, &n, NULL, &def) == GRANT_ERR_ARG &&
              grant_rate(&G, 1, &n, &d, NULL) == GRANT_ERR_ARG,
          "null rate args");
    grant_epoch_begin(&G, 2);
    CHECK(grant_pool_credit(&G, allotment, 77, 3) == GRANT_OK && G.hist[2].inflow == 77 &&
              G.hist[1].inflow == 500,
          "inflow is per epoch");
    CHECK(grant_rate(&G, 2, &n, &d, &def) == GRANT_OK && !def && n == 0 && d == 0,
          "inflow without weight is still undefined");
    CHECK(grant_rate(&G, 1, &n, &d, &def) == GRANT_OK && n == 500 && d == 3,
          "the previous epoch is kept");
    grant_epoch_begin(&G, 9);
    CHECK(grant_rate(&G, 1, &n, &d, &def) == GRANT_ERR_NOT_FOUND &&
              grant_rate(&G, 9, &n, &d, &def) == GRANT_OK,
          "epoch 9 replaces epoch 1 in the ring");
    G.hist[9 & (GRANT_HIST - 1)].inflow = UINT64_MAX - 10;
    CHECK(grant_pool_credit(&G, allotment, 11, 4) == GRANT_ERR_OVERFLOW &&
              grant_pool_credit(&G, allotment, 10, 4) == GRANT_OK,
          "inflow overflow refused before posting");
    /* a full line's worth */
    setup();
    CHECK(grant_pool_credit(&G, allotment, (uint64_t) 1 << 59, 1) == GRANT_OK &&
              deb(G.pool) == (uint64_t) 1 << 59 && grant_check(&G),
          "exactly 2^59 moves");
}

static uint64_t ref_share[GRANT_MAX_RECIP];
/* independent reference: floor + largest remainder, ties lower index */
static void reference(uint64_t B, const uint64_t *w, uint32_t n)
{
    uint64_t W = 0, given = 0;
    unsigned __int128 rem[GRANT_MAX_RECIP];
    for (uint32_t i = 0; i < n; i++) W += w[i];
    for (uint32_t i = 0; i < n; i++) {
        unsigned __int128 p = (unsigned __int128) B * w[i];
        ref_share[i] = (uint64_t) (p / W);
        rem[i] = p % W;
        given += ref_share[i];
    }
    while (given < B) {
        uint32_t best = 0;
        for (uint32_t i = 1; i < n; i++)
            if (rem[i] > rem[best]) best = i;
        ref_share[best]++;
        rem[best] = 0;
        given++;
    }
}

static void test_distribute_exact(void)
{
    setup();
    grant_pool_credit(&G, allotment, 100, 1);
    grant_recipient_t r[3] = {rcp(0, 1), rcp(1, 1), rcp(2, 1)};
    grant_dist_t out;
    uint64_t seq = L.seq;
    CHECK(grant_epoch_distribute(&G, LOT, r, 3, 10, 5, &out) == GRANT_OK && out.barrels == 10 &&
              out.cost == 100 && out.postings == 1 && out.bad == 3,
          "100 Vino at 10 buys 10 barrels in one posting");
    CHECK(out.share[0] == 4 && out.share[1] == 3 && out.share[2] == 3,
          "ties go to the lower index");
    CHECK(L.seq == seq + 1 && deb(G.pool) == 0 && deb(G.reserve) == 100 && cred(G.issuer) == 10 &&
              deb(brl[0]) == 4 && deb(brl[1]) == 3 && deb(brl[2]) == 3,
          "ledger moved exactly");
    CHECK(grant_holding(&G, brl[0], LOT) == 4 && grant_holding(&G, brl[2], LOT) == 3 &&
              grant_holding(&G, brl[3], LOT) == 0 && grant_holding(NULL, brl[0], LOT) == 0 &&
              grant_lot(&G, LOT)->count == 10 && G.total_barrels == 10 && G.hist[1].barrels == 10 &&
              G.hist[1].cost == 100 && G.hist[1].distributed && G.hist[1].weight == 3,
          "tables mirror the ledger");
    CHECK(grant_check(&G), "ledger and mirror check");
    const pay_journal_t *j = &L.journal[(L.seq - 1) % PAY_JOURNAL_MAX];
    CHECK(j->kind == PAY_KIND_ISSUE && j->n_lines == 6 && j->tick == 5, "issue posting recorded");
    CHECK(grant_epoch_distribute(&G, LOT, r, 3, 10, 5, &out) == GRANT_ERR_STATE,
          "one distribution per epoch");

    /* remainder ordering by size, not index */
    grant_epoch_begin(&G, 2);
    grant_pool_credit(&G, allotment, 100, 1);
    grant_recipient_t s[3] = {rcp(3, 2), rcp(4, 1), rcp(5, 1)};
    CHECK(grant_epoch_distribute(&G, LOT, s, 3, 10, 6, NULL) == GRANT_OK && deb(brl[3]) == 5 &&
              deb(brl[4]) == 3 && deb(brl[5]) == 2 && grant_check(&G),
          "10 by 2:1:1 is 5,3,2");
    grant_epoch_begin(&G, 3);
    grant_pool_credit(&G, allotment, 70, 1);
    grant_recipient_t t[3] = {rcp(6, 1), rcp(7, 2), rcp(8, 4)};
    CHECK(grant_epoch_distribute(&G, LOT, t, 3, 10, 6, &out) == GRANT_OK && out.share[0] == 1 &&
              out.share[1] == 2 && out.share[2] == 4,
          "exact division leaves no remainder");
    grant_epoch_begin(&G, 4);
    grant_pool_credit(&G, allotment, 50, 1);
    grant_recipient_t u[3] = {rcp(9, 1), rcp(10, 5), rcp(11, 4)};
    CHECK(grant_epoch_distribute(&G, LOT, u, 3, 10, 6, &out) == GRANT_OK && out.share[0] == 1 &&
              out.share[1] == 2 && out.share[2] == 2,
          "5 by 1:5:4: remainders 5,5,0 -> 1,2,2");
    CHECK(grant_check(&G), "check");
}

static void test_distribute_batches(void)
{
    setup();
    grant_pool_credit(&G, allotment, 1000003, 1);
    grant_recipient_t r[12];
    uint64_t w[12];
    for (uint32_t i = 0; i < 12; i++) {
        w[i] = 1 + i * 7 % 5;
        r[i] = rcp(i, w[i]);
    }
    grant_dist_t out;
    uint64_t seq = L.seq;
    CHECK(grant_epoch_distribute(&G, LOT, r, 12, 7, 9, &out) == GRANT_OK && out.barrels == 142857 &&
              out.cost == 999999 && out.postings == 3 && L.seq == seq + 3,
          "12 recipients in 3 postings of 5, 5, 2");
    reference(142857, w, 12);
    bool same = true;
    uint64_t sum = 0;
    for (uint32_t i = 0; i < 12; i++) {
        same &= out.share[i] == ref_share[i] && deb(brl[i]) == ref_share[i];
        sum += out.share[i];
    }
    CHECK(same && sum == 142857, "shares match the independent reference");
    CHECK(deb(G.pool) == 4 && deb(G.reserve) == 999999 && cred(G.issuer) == 142857 &&
              grant_check(&G),
          "pool keeps the change");

    /* few barrels: only some recipients get one, still batched */
    setup();
    grant_pool_credit(&G, allotment, 30, 1);
    for (uint32_t i = 0; i < 12; i++) r[i] = rcp(i, 1);
    r[11].weight = 5;
    seq = L.seq;
    CHECK(grant_epoch_distribute(&G, LOT, r, 12, 10, 9, &out) == GRANT_OK && out.barrels == 3 &&
              out.postings == 1 && L.seq == seq + 1,
          "three barrels, one posting");
    CHECK(out.share[11] == 1 && out.share[0] == 1 && out.share[1] == 1 && out.share[2] == 0 &&
              deb(brl[11]) == 1 && deb(brl[2]) == 0 && grant_check(&G),
          "3 by 1x11:5 -> largest remainders");
    /* five shares then zero shares: the tail posts nothing */
    setup();
    grant_pool_credit(&G, allotment, 5, 1);
    for (uint32_t i = 0; i < 7; i++) r[i] = rcp(i, i < 5 ? 100 : 1);
    seq = L.seq;
    CHECK(grant_epoch_distribute(&G, LOT, r, 7, 1, 9, &out) == GRANT_OK && out.postings == 1 &&
              L.seq == seq + 1 && out.share[4] == 1 && out.share[5] == 0 && grant_check(&G),
          "a zero-share tail adds no posting");
    /* six recipients with a share: two postings, the second with one */
    setup();
    grant_pool_credit(&G, allotment, 60, 1);
    for (uint32_t i = 0; i < 6; i++) r[i] = rcp(i, 1);
    CHECK(grant_epoch_distribute(&G, LOT, r, 6, 10, 9, &out) == GRANT_OK && out.postings == 2 &&
              deb(brl[5]) == 1 && deb(G.reserve) == 60 && grant_check(&G),
          "a batch of exactly five then one");
    /* exactly five: one posting */
    setup();
    grant_pool_credit(&G, allotment, 50, 1);
    CHECK(grant_epoch_distribute(&G, LOT, r, 5, 10, 9, &out) == GRANT_OK && out.postings == 1 &&
              grant_check(&G),
          "five fit in one posting");
    /* max recipients */
    setup();
    grant_pool_credit(&G, allotment, 1000, 1);
    grant_recipient_t m[GRANT_MAX_RECIP];
    for (uint32_t i = 0; i < GRANT_MAX_RECIP; i++) {
        m[i] = rcp(i & 15, 1);
        m[i].account = i < 16 ? brl[i] : open_acct(G.barrel, 600 + i, 0);
    }
    CHECK(grant_epoch_distribute(&G, LOT, m, GRANT_MAX_RECIP, 31, 9, &out) == GRANT_OK &&
              out.barrels == 32 && out.postings == 7 && grant_check(&G),
          "32 recipients, 7 postings");
}

static void test_distribute_guards(void)
{
    setup();
    grant_pool_credit(&G, allotment, 1000, 1);
    grant_recipient_t r[GRANT_MAX_RECIP + 1];
    for (uint32_t i = 0; i < 4; i++) r[i] = rcp(i, 1);
    grant_dist_t out;
    uint64_t seq = L.seq;
    CHECK(grant_epoch_distribute(&G, LOT, r, 0, 10, 1, &out) == GRANT_ERR_ARG &&
              grant_epoch_distribute(&G, LOT, r, GRANT_MAX_RECIP + 1, 10, 1, &out) ==
                  GRANT_ERR_ARG &&
              grant_epoch_distribute(&G, LOT, r, 4, 0, 1, &out) == GRANT_ERR_ARG &&
              grant_epoch_distribute(&G, LOT, NULL, 4, 10, 1, &out) == GRANT_ERR_ARG &&
              grant_epoch_distribute(NULL, LOT, r, 4, 10, 1, &out) == GRANT_ERR_ARG,
          "argument checks");
    CHECK(grant_epoch_distribute(&G, 1, r, 4, 10, 1, &out) == GRANT_ERR_NOT_FOUND, "unknown lot");
    r[1].account = vino[1];
    CHECK(grant_epoch_distribute(&G, LOT, r, 4, 10, 1, &out) == GRANT_ERR_ARG,
          "a Vino account cannot hold barrels");
    r[1].account = G.issuer;
    CHECK(grant_epoch_distribute(&G, LOT, r, 4, 10, 1, &out) == GRANT_ERR_ARG,
          "the issuer is not a recipient");
    r[1].account = 100000;
    CHECK(grant_epoch_distribute(&G, LOT, r, 4, 10, 1, &out) == GRANT_ERR_ARG,
          "nonexistent account");
    uint32_t crown = 0;
    pay_ledger_open(&L, 250, G.barrel, PAY_CAP_SOCIAL, 0, &crown);
    r[1].account = crown;
    CHECK(grant_epoch_distribute(&G, LOT, r, 4, 10, 1, &out) == GRANT_ERR_ARG,
          "an account of another capital form");
    L.acct[brl[1]].active = false;
    r[1].account = brl[1];
    CHECK(grant_epoch_distribute(&G, LOT, r, 4, 10, 1, &out) == GRANT_ERR_ARG, "inactive account");
    L.acct[brl[1]].active = true;
    r[3].account = brl[1];
    CHECK(grant_epoch_distribute(&G, LOT, r, 4, 10, 1, &out) == GRANT_ERR_ARG, "duplicate account");
    r[3].account = brl[0];
    CHECK(grant_epoch_distribute(&G, LOT, r, 4, 10, 1, &out) == GRANT_ERR_ARG,
          "duplicate of the first account");
    r[0].m.volume = 0;
    r[3].account = brl[3];
    CHECK(grant_epoch_distribute(&G, LOT, r, 4, 10, 1, &out) == GRANT_ERR_INELIGIBLE &&
              out.bad == 0,
          "the first recipient is checked too");
    r[0].m.volume = GRANT_DEF_MIN_VOLUME;
    r[3] = rcp(3, 0);
    CHECK(grant_epoch_distribute(&G, LOT, r, 4, 10, 1, &out) == GRANT_ERR_ARG, "zero weight");
    r[3].weight = UINT64_MAX;
    CHECK(grant_epoch_distribute(&G, LOT, r, 4, 10, 1, &out) == GRANT_ERR_OVERFLOW,
          "weight sum overflow");
    r[3].weight = UINT64_MAX - 3;
    r[2].m.stake = 0;
    CHECK(grant_epoch_distribute(&G, LOT, r, 4, 10, 1, &out) == GRANT_ERR_INELIGIBLE &&
              out.bad == 2 && L.seq == seq && G.hist[1].weight == 0,
          "an ineligible recipient refuses the whole call");
    CHECK(appeal(302, 1, GRANT_FAIL_STAKE, FUNNEL) == GRANT_OK, "appeal for subject 302");
    r[3].weight = 1;
    CHECK(grant_epoch_distribute(&G, LOT, r, 4, 1001, 1, &out) == GRANT_ERR_EMPTY && L.seq == seq &&
              G.hist[1].weight == 4 && !G.hist[1].distributed,
          "the budget buys nothing: weight recorded, nothing posts");
    CHECK(grant_epoch_distribute(&G, LOT, r, 4, 1000, 1, &out) == GRANT_OK && out.barrels == 1 &&
              deb(G.pool) == 0 && grant_check(&G),
          "after the appeal: the whole pool buys one barrel");
    for (uint32_t i = 0; i < 4; i++) {
        const pay_account_t *a = pay_ledger_account(&L, r[i].account);
        CHECK(a->debit < PAY_BAL_MAX, "sane");
    }

    /* the cap */
    setup();
    grant_pool_credit(&G, allotment, 1000, 1);
    grant_set_cap(&G, 1, 3);
    for (uint32_t i = 0; i < 2; i++) r[i] = rcp(i, 1);
    CHECK(grant_epoch_distribute(&G, LOT, r, 2, 10, 1, &out) == GRANT_OK && out.barrels == 33 &&
              out.cost == 330 && deb(G.pool) == 670 && grant_check(&G),
          "cap 1/3 of 1000 = 333 buys 33 barrels");
    grant_epoch_begin(&G, 2);
    grant_set_cap(&G, 2, 3);
    CHECK(grant_epoch_distribute(&G, LOT, r, 2, 1, 1, &out) == GRANT_OK && out.barrels == 446 &&
              deb(G.pool) == 224 && grant_check(&G),
          "cap 2/3 of 670 = 446");

    /* pool never negative, grants never exceed funding: random sweep */
    uint64_t x = 88172645463325252ull;
    for (uint32_t it = 0; it < 60; it++) {
        setup();
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        uint64_t fund = 1 + x % 5000000, price = 1 + (x >> 20) % 9000;
        uint32_t n = 1 + (uint32_t) ((x >> 40) % 16);
        uint64_t w[16];
        for (uint32_t i = 0; i < n; i++) {
            w[i] = 1 + ((x >> (i * 3)) & 63);
            r[i] = rcp(i, w[i]);
        }
        grant_pool_credit(&G, allotment, fund, 1);
        grant_status_t st = grant_epoch_distribute(&G, LOT, r, n, price, 1, &out);
        bool ok = st == (fund / price ? GRANT_OK : GRANT_ERR_EMPTY);
        if (st == GRANT_OK) {
            reference(fund / price, w, n);
            for (uint32_t i = 0; i < n; i++) ok &= out.share[i] == ref_share[i];
            ok &= out.cost <= fund && deb(G.pool) == fund - out.cost &&
                  deb(G.reserve) == out.cost && out.cost == out.barrels * price &&
                  fund - out.cost < price && cred(G.issuer) == out.barrels;
        }
        CHECK(ok && grant_check(&G), "random distribution exact and funded");
    }
}

static grant_t *mirror_g;
static uint32_t mirror_kill;
static int mirror_calls;
static void mirror(void *ctx, const pay_journal_t *rec)
{
    (void) ctx;
    (void) rec;
    if (++mirror_calls == 1) L.acct[mirror_kill].active = false;
}

static void test_distribute_partial(void)
{
    setup();
    grant_pool_credit(&G, allotment, 70, 1);
    grant_recipient_t r[7];
    for (uint32_t i = 0; i < 7; i++) r[i] = rcp(i, 1);
    grant_dist_t out;
    mirror_g = &G;
    mirror_kill = brl[6];
    mirror_calls = 0;
    L.mirror = mirror;
    CHECK(grant_epoch_distribute(&G, LOT, r, 7, 10, 1, &out) == GRANT_ERR_LEDGER &&
              out.postings == 1 && G.last_ledger == PAY_ERR_NO_ACCOUNT,
          "the second batch is refused by the ledger");
    L.mirror = NULL;
    L.acct[brl[6]].active = true;
    CHECK(deb(G.pool) == 20 && deb(G.reserve) == 50 && cred(G.issuer) == 5 &&
              grant_lot(&G, LOT)->count == 5 && G.total_barrels == 5 && G.hist[1].barrels == 5 &&
              G.hist[1].cost == 50 && deb(brl[5]) == 0 && grant_holding(&G, brl[5], LOT) == 0 &&
              grant_check(&G),
          "the first batch stands, complete and paid for");
    CHECK(grant_epoch_distribute(&G, LOT, r, 7, 10, 1, &out) == GRANT_ERR_STATE,
          "the epoch is spent");

    /* holding slots: exactly enough, then one short */
    setup();
    grant_pool_credit(&G, allotment, 1000, 1);
    for (uint32_t i = 2; i < GRANT_MAX_HOLD; i++)
        G.hold[i] = (grant_hold_t){true, 0xFFFF0000u + i, 7777, 0};
    for (uint32_t i = 0; i < 3; i++) r[i] = rcp(i, 1);
    CHECK(grant_epoch_distribute(&G, LOT, r, 3, 10, 1, &out) == GRANT_ERR_FULL,
          "three new holders, two slots");
    CHECK(grant_epoch_distribute(&G, LOT, r, 3, 400, 1, &out) == GRANT_OK && out.barrels == 2,
          "two barrels need only two slots");
    for (uint32_t i = 2; i < GRANT_MAX_HOLD; i++) G.hold[i].used = false;
    CHECK(grant_check(&G), "check");
    grant_epoch_begin(&G, 2);
    for (uint32_t i = 2; i < GRANT_MAX_HOLD; i++)
        G.hold[i] = (grant_hold_t){true, 0xFFFF0000u + i, 7777, 0};
    CHECK(grant_epoch_distribute(&G, LOT, r, 2, 10, 1, &out) == GRANT_OK && out.share[0] == 10 &&
              grant_holding(&G, brl[0], LOT) == 11,
          "existing holders need no new slot");
    for (uint32_t i = 2; i < GRANT_MAX_HOLD; i++) G.hold[i].used = false;
    CHECK(grant_check(&G), "check");
    /* the running total cannot wrap */
    grant_epoch_begin(&G, 3);
    grant_pool_credit(&G, allotment, 100, 1);
    uint64_t keep = G.total_barrels;
    G.total_barrels = UINT64_MAX - 9;
    CHECK(grant_epoch_distribute(&G, LOT, r, 2, 1, 1, &out) == GRANT_ERR_OVERFLOW,
          "total overflow refused");
    G.total_barrels = UINT64_MAX - deb(G.pool);
    CHECK(grant_epoch_distribute(&G, LOT, r, 2, 1, 1, &out) == GRANT_OK &&
              G.total_barrels == UINT64_MAX,
          "the total reaches the maximum exactly");
    G.total_barrels = keep;
}

static void test_backing(void)
{
    setup();
    grant_pool_credit(&G, allotment, 1000, 1);
    grant_recipient_t r[2] = {rcp(0, 3), rcp(1, 1)};
    grant_epoch_distribute(&G, LOT, r, 2, 10, 1, NULL); /* 75 + 25 barrels */
    const char *st = 0;
    CHECK(!strcmp(grant_label(&G, LOT, 5, &st),
                  "Virtual Barrel Allocation (Unbacked Synthetic Unit)") &&
              !strcmp(st, "Pending Physical Underwriter Attestation"),
          "unbacked label and status, exactly");
    CHECK(!strcmp(grant_label(&G, 4242, 5, NULL), GRANT_LABEL_UNBACKED),
          "an unknown lot reads unbacked");
    CHECK(!grant_backed(&G, LOT, 5) && !grant_lock_ok(&G, LOT, 5) && !grant_lock_ok(&G, 4242, 5),
          "unbacked: not lockable");
    uint64_t seq = L.seq;
    CHECK(grant_redeem(&G, brl[0], vino[0], LOT, 1, 10, 5) == GRANT_ERR_UNBACKED && L.seq == seq,
          "unbacked: not redeemable");
    CHECK(grant_redeem(&G, brl[0], vino[0], 4242, 1, 10, 5) == GRANT_ERR_NOT_FOUND, "unknown lot");
    CHECK(dispute(LOT, 0, FUNNEL) == GRANT_ERR_UNBACKED, "nothing to dispute yet");

    CHECK(attest(4242, 100, UW1, 50, 5, UW1) == GRANT_ERR_NOT_FOUND, "attest unknown lot");
    CHECK(attest(LOT, 99, UW1, 50, 5, UW1) == GRANT_ERR_ARG &&
              attest(LOT, 101, UW1, 50, 5, UW1) == GRANT_ERR_ARG,
          "count must equal the lot");
    grant_lot_open(&G, LOT + 1);
    CHECK(attest(LOT + 1, 0, UW1, 50, 5, UW1) == GRANT_ERR_ARG, "an empty lot cannot be attested");
    CHECK(attest(LOT, 100, UW1, 5, 5, UW1) == GRANT_ERR_EXPIRED, "expiry == now is expired");
    CHECK(attest(LOT, 100, UW2, 50, 5, UW2) == GRANT_ERR_SIG &&
              attest(LOT, 100, 0, 50, 5, 0) == GRANT_ERR_SIG,
          "an unlisted underwriter (id 0 too)");
    CHECK(attest(LOT, 100, UW1, 50, 5, UW2) == GRANT_ERR_SIG, "signed by someone else");
    uint8_t m[GRANT_ATTEST_MSG], sig[32];
    grant_attest_msg(LOT, 100, UW1, 50, m);
    sign(UW1, m, sizeof m, sig);
    CHECK(grant_attest(&G, LOT, 100, UW1, 51, 5, sig, 32) == GRANT_ERR_SIG,
          "signature over another expiry");
    CHECK(grant_attest(&G, LOT, 100, UW1, 50, 5, NULL, 32) == GRANT_ERR_ARG &&
              grant_attest(NULL, LOT, 100, UW1, 50, 5, sig, 32) == GRANT_ERR_ARG,
          "null attest args");
    CHECK(!grant_lot(&G, LOT)->attested && !grant_backed(&G, LOT, 5), "still unbacked");
    CHECK(attest(LOT, 100, UW1, 50, 6, UW1) == GRANT_OK, "attested until tick 50");
    const grant_lot_t *lt = grant_lot(&G, LOT);
    CHECK(lt->attested && !lt->disputed && lt->underwriter == UW1 && lt->att_count == 100 &&
              lt->expiry == 50,
          "attestation recorded");
    CHECK(grant_backed(&G, LOT, 49) && grant_lock_ok(&G, LOT, 49) && !grant_backed(&G, LOT, 50),
          "backed strictly before expiry");
    CHECK(!strcmp(grant_label(&G, LOT, 49, &st), GRANT_LABEL_BACKED) &&
              !strcmp(st, GRANT_STATUS_BACKED) &&
              !strcmp(grant_label(&G, LOT, 50, &st), GRANT_LABEL_UNBACKED) &&
              !strcmp(st, GRANT_STATUS_UNBACKED),
          "label follows expiry");
    grant_dist_t out;
    grant_epoch_begin(&G, 2);
    grant_pool_credit(&G, allotment, 100, 1);
    CHECK(grant_epoch_distribute(&G, LOT, r, 2, 10, 1, &out) == GRANT_ERR_STATE,
          "an attested lot takes no new barrels");

    grant_snapshot_t s;
    grant_snapshot(&G, 10, &s);
    CHECK(s.barrels_outstanding == 100 && s.barrels_backed == 100 && s.barrels_unbacked == 0 &&
              s.lots == 2 && s.lots_backed == 1,
          "snapshot counts backed barrels");
    grant_snapshot(&G, 50, &s);
    CHECK(s.barrels_backed == 0 && s.barrels_unbacked == 100 && s.lots_backed == 0,
          "expired is unbacked in the snapshot");

    /* redemption */
    seq = L.seq;
    uint64_t res = deb(G.reserve);
    CHECK(grant_redeem(&G, brl[0], vino[0], LOT, 30, 9, 10) == GRANT_OK && L.seq == seq + 1,
          "30 barrels redeemed at 9 in one posting");
    CHECK(deb(brl[0]) == 45 && cred(G.issuer) == 70 && deb(G.reserve) == res - 270 &&
              deb(vino[0]) == 270 && grant_holding(&G, brl[0], LOT) == 45 &&
              grant_lot(&G, LOT)->count == 70 && grant_check(&G),
          "redeemed exactly");
    const pay_journal_t *j = &L.journal[(L.seq - 1) % PAY_JOURNAL_MAX];
    CHECK(j->kind == PAY_KIND_REDEEM && j->n_lines == 4 && j->tick == 10, "redeem posting");
    CHECK(grant_redeem(&G, brl[1], vino[1], LOT, 26, 9, 10) == GRANT_ERR_FUNDS &&
              grant_redeem(&G, brl[2], vino[2], LOT, 1, 9, 10) == GRANT_ERR_FUNDS,
          "cannot redeem more than held");
    CHECK(grant_redeem(&G, brl[1], brl[0], LOT, 1, 9, 10) == GRANT_ERR_ARG &&
              grant_redeem(&G, brl[1], 100000, LOT, 1, 9, 10) == GRANT_ERR_ARG,
          "payout must be a Vino account");
    CHECK(grant_redeem(&G, brl[1], vino[1], LOT, 0, 9, 10) == GRANT_ERR_ARG &&
              grant_redeem(&G, brl[1], vino[1], LOT, 1, 0, 10) == GRANT_ERR_ARG &&
              grant_redeem(NULL, brl[1], vino[1], LOT, 1, 1, 10) == GRANT_ERR_ARG,
          "redeem argument checks");
    CHECK(grant_redeem(&G, brl[1], vino[1], LOT, 2, UINT64_MAX / 2 + 1, 10) == GRANT_ERR_OVERFLOW &&
              grant_redeem(&G, brl[1], vino[1], LOT, 1, ((uint64_t) 1 << 59) + 1, 10) ==
                  GRANT_ERR_OVERFLOW,
          "payout overflow");
    CHECK(grant_redeem(&G, brl[1], vino[1], LOT, 1, (uint64_t) 1 << 59, 10) == GRANT_ERR_LEDGER &&
              G.last_ledger == PAY_ERR_FUNDS && grant_holding(&G, brl[1], LOT) == 25,
          "the reserve pays only what it holds");
    CHECK(grant_redeem(&G, brl[1], vino[1], LOT, 25, 1, 10) == GRANT_OK &&
              grant_holding(&G, brl[1], LOT) == 0 && grant_check(&G),
          "a full redemption frees the holding");
    uint32_t used = 0;
    for (uint32_t i = 0; i < GRANT_MAX_HOLD; i++) used += G.hold[i].used;
    CHECK(used == 1, "one holding left");
    CHECK(grant_redeem(&G, brl[0], vino[0], LOT, 1, 1, 50) == GRANT_ERR_EXPIRED &&
              grant_redeem(&G, brl[0], vino[0], LOT, 1, 1, 49) == GRANT_OK,
          "expiry boundary");
    L.acct[brl[0]].flags |= PAY_ACCT_FROZEN;
    CHECK(grant_redeem(&G, brl[0], vino[0], LOT, 1, 1, 20) == GRANT_ERR_LEDGER &&
              grant_holding(&G, brl[0], LOT) == 44 && grant_lot(&G, LOT)->count == 44,
          "a ledger refusal changes no table");
    L.acct[brl[0]].flags = 0;

    /* dispute */
    CHECK(dispute(LOT, 50, UW1) == GRANT_ERR_SIG && dispute(LOT, 49, FUNNEL) == GRANT_ERR_SIG,
          "only the funnel, over this attestation");
    CHECK(dispute(4242, 50, FUNNEL) == GRANT_ERR_NOT_FOUND, "dispute unknown lot");
    CHECK(grant_dispute(&G, LOT, NULL, 32) == GRANT_ERR_ARG &&
              grant_dispute(NULL, LOT, sig, 32) == GRANT_ERR_ARG,
          "null dispute args");
    CHECK(dispute(LOT, 50, FUNNEL) == GRANT_OK && grant_lot(&G, LOT)->disputed, "disputed");
    CHECK(!grant_backed(&G, LOT, 20) && !grant_lock_ok(&G, LOT, 20) &&
              grant_redeem(&G, brl[0], vino[0], LOT, 1, 1, 20) == GRANT_ERR_DISPUTED,
          "a disputed lot reverts to unbacked");
    CHECK(grant_redeem(&G, brl[0], vino[0], LOT, 1, 1, 60) == GRANT_ERR_DISPUTED,
          "disputed is reported before expired");
    CHECK(attest(LOT, 44, UW1, 50, 20, UW1) == GRANT_ERR_EPOCH,
          "the disputed attestation cannot be replayed");
    CHECK(attest(LOT, 44, UW1, 49, 20, UW1) == GRANT_ERR_EPOCH, "nor an older one");
    grant_underwriter_add(&G, UW2);
    CHECK(attest(LOT, 44, UW2, 51, 20, UW2) == GRANT_OK && grant_backed(&G, LOT, 50) &&
              !grant_lot(&G, LOT)->disputed && grant_lot(&G, LOT)->underwriter == UW2,
          "a fresh attestation re-backs the lot");
    CHECK(grant_check(&G), "check");
}

static void test_snapshot(void)
{
    /* two histories with the same aggregates, different holders */
    grant_snapshot_t a, b;
    for (int k = 0; k < 2; k++) {
        setup();
        grant_pool_credit(&G, allotment, 1000, 1);
        grant_recipient_t r[2] = {rcp(k ? 5 : 0, 1), rcp(k ? 9 : 1, 3)};
        r[0].m.subject = k ? 77 : 1;
        grant_epoch_distribute(&G, LOT, r, 2, 10, 1, NULL);
        grant_snapshot(&G, 3, k ? &b : &a);
    }
    CHECK(!memcmp(&a, &b, sizeof a), "the snapshot depends on aggregates only");
    CHECK(a.epoch == 1 && a.inflow == 1000 && a.weight == 4 && a.rate_defined &&
              a.rate_num == 1000 && a.rate_den == 4 && a.granted_barrels == 100 &&
              a.granted_cost == 1000 && a.total_barrels_granted == 100 && a.pool_balance == 0 &&
              a.reserve_balance == 1000 && a.barrels_outstanding == 100 && a.barrels_backed == 0 &&
              a.barrels_unbacked == 100 && a.lots == 1 && a.lots_backed == 0,
          "snapshot fields");
    grant_epoch_begin(&G, 2);
    grant_snapshot(&G, 3, &b);
    CHECK(b.epoch == 2 && b.inflow == 0 && b.weight == 0 && !b.rate_defined && b.rate_num == 0 &&
              b.granted_barrels == 0 && b.granted_cost == 0 && b.total_barrels_granted == 100 &&
              b.barrels_outstanding == 100,
          "a new epoch starts its counters at zero");
    grant_pool_credit(&G, allotment, 33, 3);
    grant_snapshot(&G, 3, &b);
    CHECK(b.inflow == 33 && b.pool_balance == 33, "the snapshot reads the current epoch");
    memset(&b, 0xAB, sizeof b);
    grant_snapshot(NULL, 3, &b);
    CHECK(b.epoch == 0 && b.lots == 0 && b.pool_balance == 0, "null instance: zero snapshot");
    grant_snapshot(&G, 3, NULL);
    CHECK(grant_check(&G), "check");
}

static void test_check_and_msgs(void)
{
    setup();
    grant_pool_credit(&G, allotment, 100, 1);
    grant_recipient_t r[2] = {rcp(0, 1), rcp(1, 1)};
    grant_epoch_distribute(&G, LOT, r, 2, 10, 1, NULL);
    grant_lot_open(&G, LOT + 1);
    CHECK(grant_check(&G), "baseline");
    G.hold[0].count++;
    CHECK(!grant_check(&G), "holding vs lot");
    G.lot[0].count++;
    CHECK(!grant_check(&G), "lot vs issuer");
    G.hold[0].count--;
    G.lot[0].count--;
    G.hold[1].lot = LOT + 1;
    G.lot[0].count -= 5;
    G.lot[1].count += 5;
    CHECK(grant_check(&G), "per-lot sums are by lot id");
    G.lot[0].count += 5;
    G.lot[1].count -= 5;
    CHECK(!grant_check(&G), "a holding filed under the wrong lot");
    G.lot[0].count -= 5;
    G.lot[1].count += 5;
    G.hold[1].lot = LOT;
    G.lot[0].count += 5;
    G.lot[1].count -= 5;
    G.hold[1].account = brl[0];
    CHECK(!grant_check(&G), "holder vs ledger DEBIT");
    G.hold[1].account = brl[1];
    G.hold[0].account = brl[5];
    CHECK(!grant_check(&G), "the first holding row is checked against the ledger");
    G.hold[0].account = brl[0];
    G.hold[1].account = brl[5];
    CHECK(!grant_check(&G), "a later holding row is checked against the ledger");
    G.hold[1].account = brl[1];
    L.acct[0].debit++;
    CHECK(!grant_check(&G), "ledger check included");
    L.acct[0].debit--;
    G.hold[1].used = false;
    G.lot[0].count -= 5;
    CHECK(!grant_check(&G), "a missing holding is seen through the ledger");
    G.hold[1].used = true;
    G.lot[0].count += 5;
    CHECK(grant_check(&G), "restored");

    uint8_t m[GRANT_ATTEST_MSG], ref[GRANT_ATTEST_MSG];
    memcpy(ref, "zxv/grant-attest/v1", 19);
    for (int i = 0; i < 4; i++) {
        ref[19 + i] = (uint8_t) (0x11223344u >> (8 * i));
        ref[31 + i] = (uint8_t) (0x55667788u >> (8 * i));
    }
    for (int i = 0; i < 8; i++) {
        ref[23 + i] = (uint8_t) (0x0102030405060708ull >> (8 * i));
        ref[35 + i] = (uint8_t) (0xA1B2C3D4E5F60718ull >> (8 * i));
    }
    grant_attest_msg(0x11223344u, 0x0102030405060708ull, 0x55667788u, 0xA1B2C3D4E5F60718ull, m);
    CHECK(!memcmp(m, ref, sizeof m), "attestation message bytes");
    uint8_t d[GRANT_DISPUTE_MSG], dr[GRANT_DISPUTE_MSG];
    memcpy(dr, "zxv/grant-dispute/v1", 20);
    for (int i = 0; i < 4; i++) dr[20 + i] = (uint8_t) (0xCAFEBABEu >> (8 * i));
    for (int i = 0; i < 8; i++) dr[24 + i] = (uint8_t) (0x8877665544332211ull >> (8 * i));
    grant_dispute_msg(0xCAFEBABEu, 0x8877665544332211ull, d);
    CHECK(!memcmp(d, dr, sizeof d), "dispute message bytes");
    uint8_t a[GRANT_APPEAL_MSG], ar[GRANT_APPEAL_MSG];
    memcpy(ar, "zxv/grant-appeal/v1", 19);
    for (int i = 0; i < 4; i++) {
        ar[19 + i] = (uint8_t) (0xDEADBEEFu >> (8 * i));
        ar[31 + i] = (uint8_t) (0x00000041u >> (8 * i));
    }
    for (int i = 0; i < 8; i++) ar[23 + i] = (uint8_t) (0x1122334455667788ull >> (8 * i));
    grant_appeal_msg(0xDEADBEEFu, 0x1122334455667788ull, 0x41, a);
    CHECK(!memcmp(a, ar, sizeof a), "appeal message bytes");

    /* identifiers are distinct per posting and the e2e names the module */
    const pay_journal_t *j1 = &L.journal[(L.seq - 1) % PAY_JOURNAL_MAX],
                        *j0 = &L.journal[(L.seq - 2) % PAY_JOURNAL_MAX];
    CHECK(!strncmp(j1->e2e, "GRANT-1", 8) && !strncmp(j0->e2e, "GRANT-0", 8) &&
              strcmp(j0->uetr, j1->uetr) && pay_uetr_valid(j1->uetr) &&
              memcmp(j0->idem_key, j1->idem_key, 32),
          "identifiers");
    {
        uint8_t in[41], want[32], u[32];
        char uetr[PAY_UETR_LEN + 1];
        memcpy(in, G.seed, 32);
        in[32] = 'I';
        for (int i = 0; i < 8; i++) in[33 + i] = (uint8_t) (1ull >> (8 * i));
        sha256(in, 41, want);
        in[32] = 'U';
        sha256(in, 41, u);
        pay_uetr_from_random(u, uetr);
        CHECK(!memcmp(j1->idem_key, want, 32) && !strcmp(j1->uetr, uetr),
              "identifiers are SHA-256(seed || tag || seq)");
        CHECK(j0->n_lines == 2 && j0->kind == PAY_KIND_TRANSFER, "a pool credit is two lines");
    }
    CHECK(pay_ledger_verify_chain(&L), "provenance chain intact");
    /* a lot whose id is 0, like every empty table row */
    setup();
    grant_lot_open(&G, 0);
    grant_pool_credit(&G, allotment, 20, 1);
    grant_recipient_t z[1] = {rcp(0, 1)};
    CHECK(grant_epoch_distribute(&G, 0, z, 1, 10, 1, NULL) == GRANT_OK &&
              grant_holding(&G, brl[0], 0) == 2 && grant_check(&G),
          "lot id 0 checks; empty rows are skipped");
}

/* the allotment posts the pool line itself, then records the inflow */
static pay_status_t direct(uint32_t to, uint64_t amt)
{
    pay_posting_req_t r;
    pay_receipt_t rc;
    memset(&r, 0, sizeof r);
    nfund++;
    snprintf(r.uetr, sizeof r.uetr, "00000000-0000-4000-8000-%012llx", (unsigned long long) nfund);
    snprintf(r.e2e, sizeof r.e2e, "DIRECT-%llu", (unsigned long long) nfund);
    memcpy(r.idem_key, &nfund, sizeof nfund);
    r.idem_key[31] = 0xD1;
    return pay_ledger_transfer(&L, &r, allotment, to, amt, &rc);
}

static void test_record(void)
{
    setup();
    CHECK(grant_pool_record(NULL, 5) == GRANT_ERR_ARG && grant_pool_record(&G, 0) == GRANT_ERR_ARG,
          "record: bad arguments refused");
    CHECK(grant_pool_record(&G, 1) == GRANT_ERR_FUNDS && G.hist[1].inflow == 0,
          "nothing landed: nothing can be recorded");
    CHECK(direct(G.pool, 300) == PAY_OK, "the allotment's own posting lands 300");
    CHECK(grant_pool_record(&G, 301) == GRANT_ERR_FUNDS && G.hist[1].inflow == 0,
          "more than landed is refused");
    CHECK(grant_pool_record(&G, 300) == GRANT_OK && G.hist[1].inflow == 300 && G.pool_seen == 300,
          "exactly what landed is counted");
    CHECK(grant_pool_record(&G, 1) == GRANT_ERR_FUNDS && G.hist[1].inflow == 300,
          "the same money cannot be counted twice");
    CHECK(grant_pool_credit(&G, allotment, 100, 1) == GRANT_OK && G.pool_seen == 400 &&
              grant_pool_record(&G, 1) == GRANT_ERR_FUNDS,
          "a credit is counted once, by the credit");
    grant_recipient_t r[2] = {rcp(0, 1), rcp(1, 1)};
    CHECK(grant_epoch_distribute(&G, LOT, r, 2, 100, 2, NULL) == GRANT_OK && deb(G.pool) == 0 &&
              G.pool_seen == 0,
          "spending the pool lowers what is counted");
    grant_epoch_begin(&G, 2);
    CHECK(direct(G.pool, 50) == PAY_OK && grant_pool_record(&G, 50) == GRANT_OK &&
              G.hist[2].inflow == 50 && grant_check(&G),
          "the next epoch records its own inflow");
    G.pool_seen = deb(G.pool) + 1; /* corrupt: counted more than the pool holds */
    CHECK(grant_pool_record(&G, 1) == GRANT_ERR_FUNDS, "a corrupt count fails closed");
    G.pool_seen = 0;
    G.hist[2].inflow = UINT64_MAX;
    CHECK(grant_pool_record(&G, 1) == GRANT_ERR_OVERFLOW && G.pool_seen == 0,
          "inflow overflow refused, nothing counted");
    /* a spend larger than what was counted floors the count at zero */
    setup();
    CHECK(direct(G.pool, 100) == PAY_OK && grant_pool_credit(&G, allotment, 100, 1) == GRANT_OK &&
              G.pool_seen == 100,
          "100 counted, 100 more landed uncounted");
    CHECK(grant_epoch_distribute(&G, LOT, r, 2, 75, 2, NULL) == GRANT_OK && deb(G.pool) == 50 &&
              G.pool_seen == 0 && grant_pool_record(&G, 50) == GRANT_OK && G.pool_seen == 50,
          "spend 150 of 100 counted: count floors at 0, the 50 left can still be recorded");
    /* a partial spend lowers the count by exactly the spend */
    setup();
    CHECK(grant_pool_credit(&G, allotment, 300, 1) == GRANT_OK && grant_set_cap(&G, 1, 2) == GRANT_OK,
          "300 counted, half may be spent");
    CHECK(grant_epoch_distribute(&G, LOT, r, 2, 100, 2, NULL) == GRANT_OK && deb(G.pool) == 200 &&
              G.pool_seen == 200 && grant_pool_record(&G, 1) == GRANT_ERR_FUNDS,
          "one barrel at 100 leaves 200 counted, nothing uncounted");
}

int main(void)
{
    test_init();
    test_setters();
    test_eval();
    test_appeal();
    test_credit_and_rate();
    test_distribute_exact();
    test_distribute_batches();
    test_distribute_guards();
    test_distribute_partial();
    test_backing();
    test_snapshot();
    test_check_and_msgs();
    test_record();
    printf("test_grants: PASS %d FAIL %d\n", pass, fail);
    return fail ? 1 : 0;
}
