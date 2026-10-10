/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_prov_bridge.c — the provider layer wired to the rest of the kernel:
 * settlement over pay_ledger (VFV and ISO 4217), the capmkt auction with
 * co-signed receipts as delivery evidence, and remote models inside the
 * swarm's tokens-per-cycle budget. */
#include <stdio.h>
#include <string.h>
#include "prov.h"
#include "prov_pq.h"
#include "prov_pay.h"
#include "prov_capmkt.h"
#include "prov_swarm.h"
#include "../pay/pay_assure.h"

static int g_pass, g_fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c) {                                                                                   \
            g_pass++;                                                                              \
            printf("[PASS] %s\n", msg);                                                            \
        } else {                                                                                   \
            g_fail++;                                                                              \
            printf("[FAIL] %s (line %d)\n", msg, __LINE__);                                        \
        }                                                                                          \
    } while (0)

static uint8_t PK[4][PROV_PK_BYTES], SK[4][PROV_SK_BYTES];
static prov_net_t g_n;
static uint64_t g_nonce;

static void mkdesc(prov_desc_t *d, const char *name)
{
    memset(d, 0, sizeof *d);
    strcpy(d->name, name);
    strcpy(d->jurisdiction, "CH");
    strcpy(d->regions[0].code, "CH");
    d->n_regions = 1;
    d->sla[0].availability_ppm = 990000;
    d->n_sla = 1;
    d->offers[0].rclass = PROV_RC_INFERENCE;
    d->offers[0].unit = PROV_UNIT_TOKEN;
    d->offers[0].capacity_per_cycle = 1000000;
    d->offers[0].api.shape = PROV_SHAPE_CHAT;
    strcpy(d->offers[0].licence.spdx, "Apache-2.0");
    d->offers[0].licence.commercial_use = true;
    d->n_offers = 1;
    d->honours_no_train = true;
    d->version = 1;
}

static uint32_t reg(int k, const char *name)
{
    prov_desc_t d;
    uint8_t sig[PROV_SIG_BYTES];
    uint32_t h = 0;
    mkdesc(&d, name);
    prov_pq_sign_desc(&d, PK[k], SK[k], sig);
    return prov_register(&g_n, &d, PK[k], sig, &h) == PROV_OK ? h : 0;
}

static uint32_t ask(int k, uint32_t ph, uint16_t asset, uint64_t price, uint64_t qty)
{
    uint8_t sig[PROV_SIG_BYTES];
    uint32_t a = 0;
    uint64_t nonce = ++g_nonce;
    prov_pq_sign_ask(&g_n, ph, 0, asset, price, qty, nonce, SK[k], sig);
    return prov_ask_post(&g_n, ph, 0, asset, price, qty, nonce, sig, &a) == PROV_OK ? a : 0;
}

static void job(prov_job_t *j, uint16_t asset, uint64_t qty, uint64_t ceiling)
{
    prov_job_default(j);
    j->rclass = PROV_RC_INFERENCE;
    j->unit = PROV_UNIT_TOKEN;
    j->asset = asset;
    j->qty = qty;
    j->max_unit_price = ceiling;
}

static uint32_t first_fill(uint32_t bid)
{
    for (uint32_t k = 1; k <= PROV_MAX_FILLS; k++) {
        const prov_fill_t *f = prov_fill(&g_n, k);
        if (f && f->bid == bid && f->state == PROV_FILL_RESERVED) return k;
    }
    return 0;
}

/* ===== pay_ledger ===== */
static pay_ledger_t L;
static prov_pay_t PP;
static uint64_t g_ctr;

static void mkreq(pay_posting_req_t *r)
{
    uint8_t d[32], s[8];
    memset(r, 0, sizeof *r);
    g_ctr++;
    memcpy(s, &g_ctr, 8);
    prov_sha3(s, 8, d);
    memcpy(r->idem_key, d, 32);
    pay_uetr_from_random(d, r->uetr);
    snprintf(r->e2e, sizeof r->e2e, "T-%llu", (unsigned long long) g_ctr);
    r->initiator = 1;
}

static uint64_t bal(uint32_t acct)
{
    return pay_ledger_account(&L, acct)->debit;
}

/* the four fee buckets together */
static uint64_t bal4(const uint32_t a[PAY_ASSURE_BUCKETS])
{
    uint64_t s = 0;
    for (int k = 0; k < PAY_ASSURE_BUCKETS; k++) s += bal(a[k]);
    return s;
}

static void t_pay(void)
{
    prov_config_t c;
    pay_platform_t plat;
    pay_platform_default(&plat);
    pay_ledger_init(&L, &plat);
    uint16_t eur_l;
    pay_ledger_add_fiat(&L, "EUR", 978, 2, &eur_l);
    uint16_t vfv_l = L.vfv_asset;
    uint32_t iss_v, iss_e, uv, ue, pv, pe, ev, ee, cv[PAY_ASSURE_BUCKETS], ce[PAY_ASSURE_BUCKETS];
    pay_ledger_open(&L, 900, vfv_l, PAY_CAP_FINANCIAL, PAY_ACCT_ISSUER, &iss_v);
    pay_ledger_open(&L, 901, eur_l, PAY_CAP_FINANCIAL, PAY_ACCT_ISSUER | PAY_ACCT_EXTERNAL, &iss_e);
    pay_ledger_open(&L, 10, vfv_l, PAY_CAP_FINANCIAL, 0, &uv);
    pay_ledger_open(&L, 10, eur_l, PAY_CAP_FINANCIAL, 0, &ue);
    pay_ledger_open(&L, 20, vfv_l, PAY_CAP_FINANCIAL, 0, &pv);
    pay_ledger_open(&L, 20, eur_l, PAY_CAP_FINANCIAL, 0, &pe);
    pay_ledger_open(&L, 30, vfv_l, PAY_CAP_FINANCIAL, 0, &ev);
    pay_ledger_open(&L, 30, eur_l, PAY_CAP_FINANCIAL, 0, &ee);
    for (int k = 0; k < PAY_ASSURE_BUCKETS; k++) {
        pay_ledger_open(&L, 40 + (uint32_t) k, vfv_l, PAY_CAP_FINANCIAL, PAY_ACCT_COMMONS, &cv[k]);
        pay_ledger_open(&L, 40 + (uint32_t) k, eur_l, PAY_CAP_FINANCIAL, PAY_ACCT_COMMONS, &ce[k]);
    }
    pay_posting_req_t rq;
    pay_receipt_t rc;
    mkreq(&rq);
    pay_ledger_issue(&L, &rq, iss_v, uv, 1000000, &rc);
    mkreq(&rq);
    pay_ledger_issue(&L, &rq, iss_e, ue, 50000, &rc);
    CHECK(bal(uv) == 1000000 && bal(ue) == 50000, "user funded in VFV and EUR on the pay rails");

    prov_config_default(&c);
    c.verify = prov_pq_verify;
    c.settle = prov_pay_settle;
    c.settle_ctx = &PP;
    prov_net_init(&g_n, &c);
    uint16_t eur;
    prov_asset_add(&g_n, PROV_ASSET_ISO4217, "EUR", 978, 2, 0, 0, &eur);
    uint32_t P = reg(0, "Provider P"), U = 0;
    prov_user_add(&g_n, PK[1], 0, false, &U);
    prov_pay_init(&PP, &L, 77);
    PP.escrow[0] = ev, PP.escrow[eur] = ee;
    for (int k = 0; k < PAY_ASSURE_BUCKETS; k++)
        PP.fee_acct[0][k] = cv[k], PP.fee_acct[eur][k] = ce[k];
    PP.user[0][U] = uv, PP.user[eur][U] = ue;
    PP.provider[0][P] = pv, PP.provider[eur][P] = pe;

    prov_job_t j;
    uint32_t b, nf;
    ask(0, P, 0, 7, 100000);
    job(&j, 0, 10000, 10);
    prov_bid_post(&g_n, U, &j, &b);
    prov_match(&g_n, PROV_RC_INFERENCE, PROV_UNIT_TOKEN, 0, &nf);
    CHECK(nf == 1 && bal(uv) == 1000000 - 70000 && bal(ev) == 70000,
          "HOLD posted: user -> escrow on the ledger");
    uint32_t f = first_fill(b);
    prov_usage_t u;
    memset(&u, 0, sizeof u);
    u.units = 6000;
    prov_receipt_t r;
    prov_receipt_build(&g_n, f, &u, &r);
    prov_pq_sign_receipt(&r, SK[0], true);
    prov_pq_sign_receipt(&r, SK[1], false);
    CHECK(prov_settle(&g_n, f, &r) == PROV_OK, "co-signed receipt settles over pay_ledger");
    uint64_t fee = pay_assure_fee(42000);
    CHECK(bal(pv) == 42000 - fee && bal4(cv) == fee && bal(ev) == 0 && bal(uv) == 1000000 - 42000,
          "FINAL: provider net, fee to the four buckets, unused hold back, escrow empty");
    CHECK(fee == 37 && bal(cv[PAY_ASSURE_RESERVE_FLOOR]) == 20 &&
              bal(cv[PAY_ASSURE_VBILL_DIVIDEND]) == 9 && bal(cv[PAY_ASSURE_INFRA_BOUNTY]) == 5 &&
              bal(cv[PAY_ASSURE_REGEN_CAPITAL]) == 3,
          "fee 37 on 42000 splits 20 / 9 / 5 / 3 (remainder to the reserve floor)");
    CHECK(pay_ledger_check(&L) && pay_ledger_verify_chain(&L), "ledger invariants and chain hold");
    prov_settlement_t s;
    memset(&s, 0, sizeof s);
    s.kind = PROV_SETTLE_FINAL;
    s.asset = 0;
    s.user = U;
    s.provider = P;
    s.fill = f;
    s.hold = r.hold;
    s.gross = r.gross;
    s.fee = r.fee;
    s.net = r.net;
    s.refund = r.refund;
    prov_receipt_digest(&r, s.ref);
    CHECK(prov_pay_settle(&PP, &s) == 0 && PP.last == PAY_DUPLICATE && bal(pv) == 42000 - fee,
          "replayed settlement is an idempotent no-op on the ledger");
    s.gross = s.hold + 1;
    CHECK(prov_pay_settle(&PP, &s) == -1, "settle hook refuses a charge above the hold (no usury)");

    /* EUR market on the same rails */
    ask(0, P, eur, 3, 100000);
    job(&j, eur, 10000, 3);
    prov_bid_post(&g_n, U, &j, &b);
    prov_match(&g_n, PROV_RC_INFERENCE, PROV_UNIT_TOKEN, eur, &nf);
    f = first_fill(b);
    u.units = 10000;
    prov_receipt_build(&g_n, f, &u, &r);
    prov_pq_sign_receipt(&r, SK[0], true);
    prov_pq_sign_receipt(&r, SK[1], false);
    CHECK(prov_settle(&g_n, f, &r) == PROV_OK && bal(pe) == 30000 - pay_assure_fee(30000) &&
              bal4(ce) == pay_assure_fee(30000) && bal(ue) == 20000,
          "ISO 4217 (EUR) job settles over the same rails");
    /* not enough money: hold refused, no fill */
    job(&j, eur, 100000, 3);
    prov_bid_post(&g_n, U, &j, &b);
    prov_match(&g_n, PROV_RC_INFERENCE, PROV_UNIT_TOKEN, eur, &nf);
    CHECK(nf == 0 && bal(ue) == 20000, "insufficient funds: ledger refuses the hold, no fill");
    CHECK(pay_ledger_check(&L), "ledger invariants after all postings");
}

/* ===== capmkt ===== */
static cm_market_t M;
static prov_cm_t BR;

static void t_capmkt(void)
{
    prov_config_t c;
    cm_params_t cp;
    prov_config_default(&c);
    c.verify = prov_pq_verify;
    c.settle = prov_pay_settle; /* unused by the capmkt path */
    prov_net_init(&g_n, &c);
    g_nonce = 0;
    prov_cm_bind(&BR, &g_n, &M, &cp);
    cm_init(&M, &cp);
    uint32_t A = reg(0, "Provider A"), B = reg(2, "Provider B"), U = 0;
    prov_user_add(&g_n, PK[1], 0, false, &U);
    uint16_t eur;
    prov_asset_add(&g_n, PROV_ASSET_ISO4217, "EUR", 978, 2, 0, 0, &eur);
    cm_key_t key = {CM_RES_INFERENCE, CM_TENOR_DAY, 0};
    uint32_t aa = ask(0, A, 0, 1, 10000), ab = ask(2, B, 0, 1, 10000), ae = ask(0, A, eur, 1, 10);
    uint32_t oa, ob, ox;
    CHECK(prov_cm_ask(&BR, aa, key, 1000, 1000000000, &oa) == PROV_OK &&
              prov_cm_ask(&BR, ab, key, 1000, 1000000000, &ob) == PROV_OK,
          "signed prov asks moved into the capmkt book (1 unit = 1000 tokens)");
    CHECK(!g_n.asks[aa - 1].live, "moved ask is off prov's book (never sold twice)");
    CHECK(prov_cm_ask(&BR, ae, key, 1, 1000000000, &oa) == PROV_ERR_UNSUPPORTED,
          "capmkt is VFV-only; EUR asks stay on prov_match");
    uint8_t uid[16];
    prov_cm_id(g_n.users[U - 1].id, uid);
    cm_deposit(&M, uid, 1000000);
    prov_job_t j;
    job(&j, 0, 15000, 2);
    CHECK(prov_cm_bid(&BR, U, &j, key, 1000, 1000000000, &ox) == PROV_OK, "bid mirrored");
    static cm_result_t res;
    CHECK(cm_clear(&M, key, 1000, &res) == CM_OK && res.volume == 15, "capmkt clears 15 units");
    const cm_contract_t *ct = 0;
    for (uint32_t i = 1; i <= 8 && !ct; i++) {
        const cm_contract_t *t = cm_contract(&M, i);
        if (t && memcmp(t->buyer, uid, 16) == 0) ct = t;
    }
    CHECK(ct != 0, "contract created");
    if (!ct) return;
    int pk_idx = memcmp(ct->provider, g_n.prov[A - 1].id, 16) == 0 ? 0 : 2;
    prov_usage_t u;
    memset(&u, 0, sizeof u);
    u.units = 4;
    u.start_tick = 1000;
    u.end_tick = 2000;
    prov_receipt_t r;
    CHECK(prov_cm_receipt(&BR, ct->id, PROV_RC_INFERENCE, PROV_UNIT_TOKEN, &u, &r) == PROV_OK &&
              r.gross == 4 * ct->price && r.fee == prov_fee(&g_n, r.gross),
          "receipt for a capmkt contract");
    prov_pq_sign_receipt(&r, SK[pk_idx], true);
    CHECK(prov_cm_accept(&BR, ct->id, &r) == PROV_ERR_AUTH, "half-signed receipt not accepted");
    prov_pq_sign_receipt(&r, SK[1], false);
    CHECK(prov_cm_accept(&BR, ct->id, &r) == PROV_OK, "co-signed receipt accepted");
    CHECK(prov_cm_accept(&BR, ct->id, &r) == PROV_ERR_DUPLICATE, "same receipt twice refused");
    cm_proof_t pr;
    memset(&pr, 0, sizeof pr);
    pr.contract_id = ct->id;
    pr.seq = 1;
    pr.units = 4;
    pr.at_ms = 1500;
    memcpy(pr.prev, ct->proof_tip, 32);
    prov_receipt_digest(&r, pr.evidence);
    const cm_account_t *pa = cm_account(&M, ct->provider);
    uint64_t before = pa ? pa->available : 0;
    CHECK(cm_deliver(&M, &pr) == CM_OK, "capmkt pays on delivery against the receipt digest");
    pa = cm_account(&M, ct->provider);
    CHECK(pa && pa->available - before == r.net && M.commons == r.fee,
          "capmkt payout equals the receipt's net; capmkt fee equals the prov fee");
    cm_proof_t p2 = pr;
    p2.seq = 2;
    cm_proof_hash(&pr, p2.prev);
    CHECK(cm_deliver(&M, &p2) == CM_ERR_PROOF, "a receipt pays one proof only");
    memset(p2.evidence, 0x77, 32);
    CHECK(cm_deliver(&M, &p2) == CM_ERR_PROOF, "unknown evidence refused");
    CHECK(cm_conserved(&M), "capmkt conservation");
}

/* ===== swarm ===== */
static swarm_budget_t SB;

static void t_swarm(void)
{
    prov_config_t c;
    prov_config_default(&c);
    c.verify = prov_pq_verify;
    c.settle = prov_pay_settle;
    prov_net_init(&g_n, &c);
    uint32_t U = 0;
    prov_user_add(&g_n, PK[1], 0, false, &U);
    swarm_budget_init(&SB, 2, 1000);
    swarm_budget_register(&SB, 1, 0); /* local conductor */
    prov_swarm_link_t l;
    prov_job_t j;
    job(&j, 0, 1, 5);
    j.no_train = true;
    j.require_commercial = true;
    CHECK(prov_swarm_attach(&SB, &l, 2, 1, U, &j) == PROV_OK, "remote model attached at level 1");
    prov_job_t bad = j;
    bad.rclass = PROV_RC_STORAGE;
    prov_swarm_link_t l2;
    CHECK(prov_swarm_attach(&SB, &l2, 3, 1, U, &bad) == PROV_ERR_ARG,
          "only token inference can be a swarm model");
    uint32_t bid;
    uint64_t g;
    CHECK(prov_swarm_request(&g_n, &SB, &l, 10, 0, &g, &bid) == PROV_ERR_STATE,
          "no cycle open: nothing charged");
    swarm_budget_begin_cycle(&SB);
    CHECK(swarm_budget_remaining(&SB, 1) == 667 && swarm_budget_remaining(&SB, 2) == 333,
          "Fibonacci split 2:1 - the remote model is budgeted exactly like a local one");
    CHECK(prov_swarm_request(&g_n, &SB, &l, 500, 0, &g, &bid) == PROV_OK && g == 333,
          "request capped at the model's allotment");
    CHECK(g_n.bids[bid - 1].job.qty == 333 && g_n.bids[bid - 1].job.no_train &&
              g_n.bids[bid - 1].job.require_commercial &&
              g_n.bids[bid - 1].job.max_total == 333 * 5,
          "bid carries the granted tokens and the user's protections");
    CHECK(prov_swarm_request(&g_n, &SB, &l, 1, 0, &g, &bid) == PROV_ERR_BUDGET,
          "allotment exhausted: no bid");
    swarm_budget_end_cycle(&SB);
    swarm_budget_begin_cycle(&SB);
    CHECK(swarm_budget_remaining(&SB, 2) == 333, "next cycle: fresh allotment (R6, no carry)");
}

int main(void)
{
    for (int i = 0; i < 4; i++) {
        uint8_t seed[32];
        memset(seed, 0x70 + i, 32);
        prov_pq_keygen(seed, PK[i], SK[i]);
    }
    t_pay();
    t_capmkt();
    t_swarm();
    printf("test_prov_bridge: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
