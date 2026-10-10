/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* integ_pay.c — Tier 2 integration: a payment workflow through kernel/src/pay.
 *
 *   pay_tithe (the phi tithe on each payment)
 *     -> pay_ledger (issue, transfer, tithed payment with VFV credit, redeem,
 *        reverse) with its `mirror` hook as the event/audit sink
 *     -> a mirror log captured by the test (what an operator would forward to
 *        an external journal), then REPLAYED onto a fresh ledger.
 *
 * Invariants asserted end to end:
 *   J1 the mirror saw exactly one record per accepted posting, none for a
 *      refused or duplicate one, in sequence order;
 *   J2 replaying the mirrored lines onto a fresh ledger with the same chart of
 *      accounts reproduces every account (debit, credit, equity) exactly:
 *      journal replay == state;
 *   J3 per asset, total DEBIT == total CREDIT and total EQUITY == 0 after every
 *      step (conservation; L1/L2 hold across postings, not just inside one);
 *   J4 the commons account holds exactly the sum of the tithes paid;
 *   J5 the provenance chain verifies after every step, and a reversal restores
 *      the balances the reversed posting changed.
 * The scenario runs twice in one process; the two runs must agree exactly.
 */
#include "tier.h"
#include "pay_ledger.h"
#include "pay_tithe.h"

static const tier_known_t KNOWN_FAILURES[] = {{"-", "unused"}};

#define LOGMAX 256
static pay_journal_t g_log[LOGMAX];
static uint32_t g_nlog;
static void mirror(void *ctx, const pay_journal_t *rec)
{
    (void) ctx;
    if (g_nlog < LOGMAX) g_log[g_nlog] = *rec;
    g_nlog++;
}

static uint64_t g_ctr;
static void mkreq(pay_posting_req_t *r, const char *tag)
{
    uint8_t d[32];
    pay_hbuf h;
    memset(r, 0, sizeof *r);
    pay_hbuf_init(&h);
    pay_hbuf_str(&h, tag);
    pay_hbuf_u64(&h, ++g_ctr);
    pay_hbuf_final(&h, d);
    memcpy(r->idem_key, d, 32);
    pay_uetr_from_random(d, r->uetr);
    snprintf(r->e2e, sizeof r->e2e, "T2-%llu", (unsigned long long) g_ctr);
    r->tick = g_ctr;
    r->initiator = 1;
}

static pay_ledger_t L, R;
static uint16_t usd;
static uint32_t ISS, A, B, C, COM, VISS, VA;

static void chart(pay_ledger_t *l)
{
    pay_ledger_init(l, NULL);
    pay_ledger_add_fiat(l, "USD", 840, 2, &usd);
    pay_ledger_open(l, 100, usd, PAY_CAP_FINANCIAL, PAY_ACCT_ISSUER, &ISS);
    pay_ledger_open(l, 1, usd, PAY_CAP_FINANCIAL, 0, &A);
    pay_ledger_open(l, 2, usd, PAY_CAP_FINANCIAL, 0, &B);
    pay_ledger_open(l, 3, usd, PAY_CAP_FINANCIAL, 0, &C);
    pay_ledger_open(l, 9, usd, PAY_CAP_FINANCIAL, PAY_ACCT_COMMONS, &COM);
    pay_ledger_open(l, 100, l->vfv_asset, PAY_CAP_FINANCIAL, PAY_ACCT_ISSUER, &VISS);
    pay_ledger_open(l, 1, l->vfv_asset, PAY_CAP_FINANCIAL, 0, &VA);
}

static bool conserved(const pay_ledger_t *l)
{
    for (uint16_t as = 0; as < l->n_assets; as++) {
        uint64_t d = 0, c = 0;
        int64_t e = 0;
        pay_ledger_totals(l, as, PAY_CAP_FINANCIAL, &d, &c, &e);
        if (d != c || e != 0) return false;
    }
    return true;
}

static bool same_accounts(const pay_ledger_t *x, const pay_ledger_t *y)
{
    if (x->n_accounts != y->n_accounts) return false;
    for (uint32_t i = 0; i < x->n_accounts; i++)
        if (x->acct[i].debit != y->acct[i].debit || x->acct[i].credit != y->acct[i].credit ||
            x->acct[i].equity != y->acct[i].equity)
            return false;
    return true;
}

static void step_ok(pay_status_t st, const char *what)
{
    CHECK(st == PAY_OK, "%s -> %d", what, (int) st);
    CHECK(pay_ledger_check(&L) && pay_ledger_verify_chain(&L), "J5 %s: ledger healthy", what);
    CHECK(conserved(&L), "J3 %s: debit == credit, equity == 0 per asset", what);
    CHECK(g_nlog == L.seq, "J1 %s: one mirror record per posting (%u vs %llu)", what, g_nlog,
          (unsigned long long) L.seq);
}

typedef struct {
    uint64_t seq, tithes, a, b, c, com, va;
    uint8_t head[PAY_HASH_LEN];
} outcome_t;

static outcome_t scenario(void)
{
    outcome_t o;
    memset(&o, 0, sizeof o);
    chart(&L);
    g_nlog = 0;
    L.mirror = mirror;
    pay_posting_req_t rq;
    pay_receipt_t rc;

    mkreq(&rq, "issue");
    step_ok(pay_ledger_issue(&L, &rq, ISS, A, 1000000, &rc), "issue 1,000,000 to A");
    /* the same request again is an idempotent duplicate: no record */
    uint32_t before = g_nlog;
    CHECK(pay_ledger_post(&L, &rq, &rc) == PAY_DUPLICATE && g_nlog == before,
          "J1 duplicate request: no new mirror record");

    /* tithed payments over amounts that exercise the phi tithe's rounding */
    const uint64_t amounts[] = {1, 2, 3, 100, 997, 12345, 99999};
    uint64_t tithes = 0;
    for (unsigned k = 0; k < TIER_N(amounts); k++) {
        uint64_t t = pay_tithe_phi(amounts[k]);
        mkreq(&rq, "tithe");
        step_ok(
            pay_ledger_pay_tithed(&L, &rq, A, k & 1 ? B : C, amounts[k], COM, t, VISS, VA, t, &rc),
            "tithed payment");
        tithes += t;
    }
    CHECK(pay_ledger_account(&L, COM)->debit == tithes, "J4 commons == sum of tithes (%llu)",
          (unsigned long long) tithes);
    CHECK(pay_ledger_account(&L, VA)->debit == tithes &&
              pay_ledger_account(&L, VISS)->credit == tithes,
          "J4 the VFV credit mirrors the tithe on its own asset");

    /* a refused posting (insufficient funds) leaves no record */
    before = g_nlog;
    mkreq(&rq, "nsf");
    CHECK(pay_ledger_transfer(&L, &rq, C, B, 1u << 30, &rc) == PAY_ERR_FUNDS && g_nlog == before,
          "J1 refused posting: no mirror record");

    /* transfer, then reverse it: the touched balances return exactly */
    uint64_t b0 = pay_ledger_account(&L, B)->debit, c0 = pay_ledger_account(&L, C)->debit;
    mkreq(&rq, "xfer");
    step_ok(pay_ledger_transfer(&L, &rq, B, C, 777, &rc), "B -> C 777");
    char uetr[PAY_UETR_LEN + 1];
    memcpy(uetr, rq.uetr, sizeof uetr);
    mkreq(&rq, "rev");
    step_ok(pay_ledger_reverse(&L, &rq, uetr, &rc), "reverse B -> C");
    CHECK(pay_ledger_account(&L, B)->debit == b0 && pay_ledger_account(&L, C)->debit == c0,
          "J5 reversal restores both balances");
    mkreq(&rq, "rev2");
    CHECK(pay_ledger_reverse(&L, &rq, uetr, &rc) == PAY_ERR_STATE, "a second reversal refused");

    /* redeem part of A's balance at par */
    mkreq(&rq, "redeem");
    step_ok(pay_ledger_redeem(&L, &rq, A, ISS, 5000, &rc), "redeem 5000");

    /* J2: replay the mirror log onto a fresh ledger with the same chart */
    chart(&R);
    bool replay_ok = g_nlog <= LOGMAX;
    for (uint32_t i = 0; replay_ok && i < g_nlog; i++) {
        mkreq(&rq, "replay");
        rq.kind = g_log[i].kind;
        rq.n_lines = g_log[i].n_lines;
        memcpy(rq.lines, g_log[i].lines, sizeof rq.lines);
        replay_ok = pay_ledger_post(&R, &rq, &rc) == PAY_OK;
    }
    CHECK(replay_ok && R.seq == L.seq, "J2 every mirrored record replays");
    CHECK(same_accounts(&L, &R), "J2 journal replay == state (every account)");
    CHECK(pay_ledger_verify_chain(&R) && conserved(&R), "J2 the replayed ledger is healthy");
    for (uint32_t i = 1; i < g_nlog && i < LOGMAX; i++)
        CHECK(g_log[i].seq == g_log[i - 1].seq + 1 &&
                  memcmp(g_log[i].prev, g_log[i - 1].hash, PAY_HASH_LEN) == 0,
              "J1 mirror records arrive in order and chain (%u)", i);

    o.seq = L.seq;
    o.tithes = tithes;
    o.a = pay_ledger_account(&L, A)->debit;
    o.b = pay_ledger_account(&L, B)->debit;
    o.c = pay_ledger_account(&L, C)->debit;
    o.com = pay_ledger_account(&L, COM)->debit;
    o.va = pay_ledger_account(&L, VA)->debit;
    memcpy(o.head, L.chain_head, PAY_HASH_LEN);
    return o;
}

int main(void)
{
    tier_begin("tier2/integ_pay", KNOWN_FAILURES, 0);
    (void) KNOWN_FAILURES;
    uint64_t c0 = g_ctr;
    outcome_t x = scenario();
    g_ctr = c0; /* same request identities: the second run must match the first */
    outcome_t y = scenario();
    CHECK(memcmp(&x, &y, sizeof x) == 0, "the scenario is deterministic across runs");
    return tier_end();
}
