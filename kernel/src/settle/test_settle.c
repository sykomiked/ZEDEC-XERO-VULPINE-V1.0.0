/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_settle.c — the settlement spine against the real swarm market.
 *
 * Runs the market the way the engine does (bid, open, credit value, close,
 * settle) for thousands of seeded cycles and syncs the spine twice per cycle:
 * with the pot full (after the open) and empty (after the settle). Every sync
 * must reconcile. Then each fail-closed path is driven on purpose. */
#include <stdio.h>
#include <string.h>
#include "settle.h"
#include "swarm_budget.h"
#include "swarm_emotion.h"

static int pass, fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("[FAIL] %s (%s:%d)\n", msg, __FILE__, __LINE__);                                \
        }                                                                                          \
    } while (0)

static uint64_t rs = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 7;
    rs ^= rs << 17;
    return rs;
}

typedef struct {
    swarm_budget_t b;
    swarm_market_t m;
    swarm_emotion_state_t e;
    settle_spine_t s;
    uint32_t ids[SWARM_MAX_MODELS];
    uint32_t n;
} world_t;

static world_t W, W2;

static void build(world_t *w, uint32_t levels, uint8_t seed_byte)
{
    uint8_t seed[32];
    memset(seed, seed_byte, sizeof seed);
    swarm_budget_init(&w->b, levels, 100000);
    swarm_market_init(&w->m);
    swarm_emotion_init(&w->e);
    settle_init(&w->s, seed);
    w->n = 0;
    uint32_t id = 1;
    for (uint32_t d = 0; d < levels; d++)
        for (uint32_t k = 0; k < swarm_level_capacity(d) && w->n < SWARM_MAX_MODELS; k++, id++) {
            uint64_t endow = 500 + rnd() % 2000;
            swarm_budget_register(&w->b, id, d);
            swarm_market_join(&w->m, id, endow);
            settle_join(&w->s, id, endow, 0);
            w->ids[w->n++] = id;
        }
}

/* One engine-shaped cycle; returns false if either sync failed. */
static bool cycle(world_t *w, uint64_t c)
{
    for (uint32_t i = 0; i < w->n; i++) {
        const swarm_trader_t *t = swarm_market_trader(&w->m, w->ids[i]);
        uint64_t money = t ? t->cap[SWARM_CAP_FINANCIAL] : 0;
        swarm_market_bid(&w->m, w->ids[i], money ? rnd() % (money / 2 + 1) : 0);
    }
    swarm_feeling_t mood = {(swarm_emotion_t) (rnd() % SWARM_EMO_COUNT), (uint8_t) (rnd() % 4)};
    swarm_emotion_set_mood(&w->e, mood);
    swarm_market_begin_cycle(&w->b, &w->m, &w->e);
    if (settle_sync(&w->s, &w->m, 2 * c) != SETTLE_OK) return false; /* pot full */
    for (uint32_t i = 0; i < w->n; i++) {
        uint64_t g = 0;
        swarm_budget_consume(&w->b, w->ids[i], swarm_budget_remaining(&w->b, w->ids[i]) / 2, &g);
        if (rnd() % 3) swarm_market_credit(&w->m, w->ids[i], SWARM_CAP_INTELLECTUAL, g / 10 + 1);
        if (rnd() % 5 == 0) swarm_market_credit(&w->m, w->ids[i], SWARM_CAP_SOCIAL, 1);
    }
    swarm_market_credit_frugality(&w->m, &w->b);
    swarm_budget_end_cycle(&w->b);
    swarm_market_settle(&w->m);
    return settle_sync(&w->s, &w->m, 2 * c + 1) == SETTLE_OK; /* pot paid out */
}

static void test_long_run(void)
{
    build(&W, 4, 0xA5);
    CHECK(W.n > 4, "swarm built with several levels");
    CHECK(settle_reconciled(&W.s, &W.m), "books agree after the endowments");
    uint64_t moved_before = W.s.seq;
    bool ok = true;
    uint32_t c;
    for (c = 0; c < 3000 && ok; c++) ok = cycle(&W, c);
    CHECK(ok, "3000 cycles: every sync reconciles");
    CHECK(W.s.cycles == 6000, "two syncs per cycle");
    CHECK(W.s.seq > moved_before + 3000, "money actually moved through the ledger");
    CHECK(swarm_market_conserved(&W.m), "market conserves money (M8)");
    CHECK(pay_ledger_check(&W.s.L), "ledger invariants L2/L3 hold");
    CHECK(pay_ledger_verify_chain(&W.s.L), "provenance chain verifies over the window");
    uint64_t d, cr;
    int64_t eq;
    pay_ledger_totals(&W.s.L, W.s.asset, PAY_CAP_FINANCIAL, &d, &cr, &eq);
    CHECK(d == W.m.money_supply && cr == W.m.money_supply && eq == 0,
          "ledger DEBIT == CREDIT == money_supply, EQUITY == 0");
}

static void test_deterministic(void)
{
    rs = 12345;
    build(&W, 3, 0x11);
    for (uint32_t c = 0; c < 200; c++) cycle(&W, c);
    rs = 12345;
    build(&W2, 3, 0x11);
    for (uint32_t c = 0; c < 200; c++) cycle(&W2, c);
    CHECK(memcmp(W.s.L.chain_head, W2.s.L.chain_head, sizeof W.s.L.chain_head) == 0,
          "same seed and inputs give the same chain hash");
    rs = 12345;
    build(&W2, 3, 0x12);
    for (uint32_t c = 0; c < 200; c++) cycle(&W2, c);
    CHECK(memcmp(W.s.L.chain_head, W2.s.L.chain_head, sizeof W.s.L.chain_head) != 0,
          "a different node seed gives a different chain");
}

static void test_conservation_breach(void)
{
    build(&W, 3, 0x22);
    for (uint32_t c = 0; c < 20; c++) cycle(&W, c);
    uint64_t seq = W.s.seq;
    uint8_t head[PAY_HASH_LEN];
    memcpy(head, W.s.L.chain_head, sizeof head);
    W.m.t[1].cap[SWARM_CAP_FINANCIAL] += 1; /* money from nowhere */
    CHECK(settle_sync(&W.s, &W.m, 999) == SETTLE_ERR_CONSERVATION, "created money is refused");
    CHECK(W.s.halted && W.s.why == SETTLE_ERR_CONSERVATION, "the spine halts");
    CHECK(W.s.seq == seq && memcmp(head, W.s.L.chain_head, sizeof head) == 0,
          "nothing was posted (fail closed)");
    W.m.t[1].cap[SWARM_CAP_FINANCIAL] -= 1;
    CHECK(settle_sync(&W.s, &W.m, 1000) == SETTLE_ERR_HALTED, "a halted spine stays halted");
    CHECK(settle_join(&W.s, 9999, 1, 0) == SETTLE_ERR_HALTED, "and refuses joins");

    build(&W, 3, 0x23);
    W.m.money_supply += 1; /* supply drifts from what the node issued */
    CHECK(settle_sync(&W.s, &W.m, 1) == SETTLE_ERR_CONSERVATION, "supply drift is refused");

    build(&W, 3, 0x24);
    W.m.t[0].cap[SWARM_CAP_FINANCIAL] -= 1; /* money destroyed */
    W.m.money_supply -= 1;
    CHECK(settle_sync(&W.s, &W.m, 1) == SETTLE_ERR_CONSERVATION,
          "destroyed money is refused even when the market's own total agrees");
}

static void test_membership(void)
{
    build(&W, 2, 0x33);
    swarm_market_join(&W.m, 777, 10); /* joined the market, not the spine */
    CHECK(settle_sync(&W.s, &W.m, 1) == SETTLE_ERR_UNKNOWN_MODEL, "unknown model halts");

    build(&W, 2, 0x34);
    W.m.t[0].model_id = 4242; /* a known slot now names a stranger */
    CHECK(settle_sync(&W.s, &W.m, 1) == SETTLE_ERR_UNKNOWN_MODEL, "renamed model halts");

    build(&W, 2, 0x35);
    CHECK(settle_join(&W.s, 1, 5, 0) == SETTLE_ERR_ARG, "duplicate join refused");
    CHECK(settle_join(&W.s, 0, 5, 0) == SETTLE_ERR_ARG, "node id is reserved");
    CHECK(settle_join(&W.s, 0xFFFFFFFEu, 5, 0) == SETTLE_ERR_ARG, "pot id is reserved");
    CHECK(settle_join(&W.s, 50, SETTLE_ISSUE_MAX, 0) == SETTLE_ERR_ARG, "endowment over the cap");
    CHECK(settle_join(&W.s, 51, 0, 0) == SETTLE_OK, "zero endowment joins");
    CHECK(settle_balance(&W.s, 51) == 0 && settle_balance(&W.s, 52) == 0, "balances");
    CHECK(settle_sync(0, &W.m, 0) == SETTLE_ERR_ARG && settle_init(0, 0) == SETTLE_ERR_ARG,
          "NULL arguments");
}

static void test_mismatch(void)
{
    /* the ledger side disagrees: a posting the market never saw */
    build(&W, 2, 0x44);
    pay_posting_req_t req;
    pay_receipt_t rc;
    memset(&req, 0, sizeof req);
    uint8_t r16[16] = {1, 2, 3};
    pay_uetr_from_random(r16, req.uetr);
    strcpy(req.e2e, "outside");
    req.idem_key[0] = 0xEE;
    CHECK(pay_ledger_transfer(&W.s.L, &req, W.s.acct[0], W.s.acct[1], 1, &rc) == PAY_OK,
          "out-of-band posting");
    CHECK(!settle_reconciled(&W.s, &W.m), "books now disagree");
    /* sync re-posts the market's view, so the books agree again: the
     * market is the source of truth for model holdings, the ledger the
     * source of truth for history */
    CHECK(settle_sync(&W.s, &W.m, 1) == SETTLE_OK, "sync restores agreement");
    CHECK(settle_reconciled(&W.s, &W.m), "and reconciles");

    /* R2 after posting: a balance edited behind the ledger's back puts one
     * unit more in the ledger than was ever issued, which no transfer can
     * remove, so the books still disagree after S3 and the spine halts */
    build(&W, 2, 0x45);
    W.s.L.acct[W.s.acct[0]].debit += 1;
    CHECK(settle_sync(&W.s, &W.m, 1) == SETTLE_ERR_MISMATCH && W.s.halted &&
              W.s.why == SETTLE_ERR_MISMATCH,
          "a unit nobody issued: R2 halts the spine");
}

static void test_edges(void)
{
    /* the whole seed is used: seeds differing only in the last byte give
     * different chains */
    uint8_t s1[32], s2[32];
    memset(s1, 7, sizeof s1);
    memcpy(s2, s1, sizeof s2);
    s2[31] ^= 1;
    static settle_spine_t a, b;
    settle_init(&a, s1);
    settle_init(&b, s2);
    settle_join(&a, 1, 10, 0);
    settle_join(&b, 1, 10, 0);
    CHECK(memcmp(a.L.chain_head, b.L.chain_head, sizeof a.L.chain_head) != 0,
          "the last seed byte changes the chain");

    /* the books: SWC is a 0-decimal unit; model accounts are plain holders */
    const pay_asset_t *as = pay_ledger_asset(&a.L, a.asset);
    CHECK(as && strcmp(as->code, "SWC") == 0 && as->minor == 0 && as->kind == PAY_ASSET_UNIT &&
              !as->iso4217,
          "SWC asset");
    const pay_account_t *ma = pay_ledger_account(&a.L, a.acct[0]);
    CHECK(ma && ma->flags == 0 && ma->owner == 1, "model account is a plain holder");
    const pay_account_t *ia = pay_ledger_account(&a.L, a.issuer);
    CHECK(ia && (ia->flags & PAY_ACCT_ISSUER) && ia->credit == 10, "node issuer carries the claim");

    /* NULL arguments */
    CHECK(settle_balance(0, 1) == 0, "balance(NULL)");
    CHECK(settle_join(0, 1, 1, 0) == SETTLE_ERR_ARG, "join(NULL)");
    CHECK(!settle_reconciled(0, &W.m) && !settle_reconciled(&a, 0), "reconciled(NULL)");
    CHECK(settle_sync(&a, 0, 0) == SETTLE_ERR_ARG && !a.halted, "sync(NULL market) does not halt");

    /* capacity: SWARM_MAX_MODELS accounts, then FULL */
    settle_init(&a, s1);
    settle_status_t st = SETTLE_OK;
    for (uint32_t i = 1; i <= SWARM_MAX_MODELS && st == SETTLE_OK; i++)
        st = settle_join(&a, i, 1, 0);
    CHECK(st == SETTLE_OK && a.n == SWARM_MAX_MODELS, "fills to SWARM_MAX_MODELS");
    CHECK(settle_join(&a, 1000, 1, 0) == SETTLE_ERR_FULL && !a.halted, "then FULL, not halted");

    /* the issue cap is exact: issued must stay below SETTLE_ISSUE_MAX, so
     * the largest endowment still fits one ledger line */
    settle_init(&a, s1);
    CHECK(settle_join(&a, 1, SETTLE_ISSUE_MAX - 1, 0) == SETTLE_OK && !a.halted,
          "SETTLE_ISSUE_MAX - 1 issues in one posting");
    CHECK(settle_join(&a, 2, 1, 0) == SETTLE_ERR_ARG && !a.halted, "one more unit is refused");
    CHECK(settle_join(&a, 3, 0, 0) == SETTLE_OK, "a zero endowment still joins at the cap");
    settle_init(&a, s1);
    CHECK(settle_join(&a, 1, SETTLE_ISSUE_MAX, 0) == SETTLE_ERR_ARG && a.n == 0,
          "SETTLE_ISSUE_MAX itself is refused");
    CHECK(settle_join(&a, 1, UINT64_MAX, 0) == SETTLE_ERR_ARG, "and so is a wrapping one");

    /* reconciliation looks at every model: disagree on the first, then the last */
    build(&W, 3, 0x55);
    CHECK(settle_reconciled(&W.s, &W.m), "agree");
    W.m.t[0].cap[SWARM_CAP_FINANCIAL] += 1;
    CHECK(!settle_reconciled(&W.s, &W.m), "first model disagrees");
    W.m.t[0].cap[SWARM_CAP_FINANCIAL] -= 1;
    W.m.t[W.n - 1].cap[SWARM_CAP_FINANCIAL] += 1;
    CHECK(!settle_reconciled(&W.s, &W.m), "last model disagrees");
    W.m.t[W.n - 1].cap[SWARM_CAP_FINANCIAL] -= 1;
    W.m.pot += 1;
    CHECK(!settle_reconciled(&W.s, &W.m), "pot disagrees");
    W.m.pot -= 1;
    W.m.n -= 1;
    CHECK(!settle_reconciled(&W.s, &W.m), "model count disagrees");
    W.m.n += 1;

    /* a ledger refusal halts: freeze a payer and make it pay */
    build(&W, 3, 0x56);
    pay_ledger_set_flags(&W.s.L, W.s.acct[0], PAY_ACCT_FROZEN);
    uint64_t give = W.m.t[0].cap[SWARM_CAP_FINANCIAL] / 2;
    W.m.t[0].cap[SWARM_CAP_FINANCIAL] -= give;
    W.m.t[1].cap[SWARM_CAP_FINANCIAL] += give;
    CHECK(settle_sync(&W.s, &W.m, 1) == SETTLE_ERR_LEDGER && W.s.why == SETTLE_ERR_LEDGER,
          "a refused posting halts the spine");

    /* the transfers: one payer to several payees moves exact amounts */
    build(&W, 3, 0x57);
    uint64_t total = W.m.t[0].cap[SWARM_CAP_FINANCIAL];
    W.m.t[0].cap[SWARM_CAP_FINANCIAL] = 0;
    W.m.t[1].cap[SWARM_CAP_FINANCIAL] += total / 3;
    W.m.t[2].cap[SWARM_CAP_FINANCIAL] += total - total / 3 - 1;
    W.m.pot += 1;
    uint64_t seq = W.s.seq;
    CHECK(settle_sync(&W.s, &W.m, 1) == SETTLE_OK && settle_reconciled(&W.s, &W.m),
          "one-to-many sync reconciles");
    CHECK(W.s.seq - seq == 3, "exactly one posting per payee");
    CHECK(settle_balance(&W.s, W.ids[0]) == 0, "payer emptied");
}

int main(void)
{
    test_long_run();
    test_deterministic();
    test_conservation_breach();
    test_membership();
    test_mismatch();
    test_edges();
    printf("%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
