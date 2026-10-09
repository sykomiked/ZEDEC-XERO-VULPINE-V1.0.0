/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_prov.c — host tests for the provider layer: registry, signed asks,
 * matching and the share cap, user filters, fee math, the no-usury guard,
 * co-signed receipts (real ML-DSA-65) with tamper cases, disputes,
 * reputation and Sybil rules, leaving, and the adapter envelope. */
#include <stdio.h>
#include <string.h>
#include "prov.h"
#include "prov_pq.h"
#include "prov_adapter.h"
#include "../pay/pay_util.h"
#include "../pay/pay_tithe.h"
#include "../pay/pay_ledger.h"

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

/* ===== keys ===== */
#define NK 8
static uint8_t PK[NK][PROV_PK_BYTES], SK[NK][PROV_SK_BYTES];
static void keys(void)
{
    for (int i = 0; i < NK; i++) {
        uint8_t seed[32];
        memset(seed, 0x40 + i, 32);
        prov_pq_keygen(seed, PK[i], SK[i]);
    }
}

/* ===== a settle hook that keeps balances (conservation checked) ===== */
static uint64_t bal_user[PROV_MAX_USERS + 1], bal_prov[PROV_MAX_PROVIDERS + 1], bal_escrow,
    bal_commons;
static int n_hold, n_final, n_release;
static bool refuse_all;
static int test_settle(void *ctx, const prov_settlement_t *s)
{
    (void) ctx;
    if (refuse_all) return -1;
    switch (s->kind) {
    case PROV_SETTLE_HOLD:
        if (bal_user[s->user] < s->hold) return -1;
        bal_user[s->user] -= s->hold;
        bal_escrow += s->hold;
        n_hold++;
        return 0;
    case PROV_SETTLE_FINAL:
        if (s->net + s->fee + s->refund != s->hold || bal_escrow < s->hold) return -1;
        bal_escrow -= s->hold;
        bal_prov[s->provider] += s->net;
        bal_commons += s->fee;
        bal_user[s->user] += s->refund;
        n_final++;
        return 0;
    case PROV_SETTLE_RELEASE:
        bal_escrow -= s->hold;
        bal_user[s->user] += s->hold;
        n_release++;
        return 0;
    }
    return -1;
}

static bool test_attest(void *ctx, uint8_t kind, const uint8_t *ev, uint32_t len,
                        const uint8_t subject[32])
{
    (void) ctx, (void) kind, (void) subject;
    return len >= 4 && memcmp(ev, "GOOD", 4) == 0;
}

static prov_net_t g_n;

static void mkdesc(prov_desc_t *d, const char *name, const char *region, const char *juris,
                   bool commercial, const char *spdx, bool no_train, uint8_t network)
{
    memset(d, 0, sizeof *d);
    strcpy(d->name, name);
    d->network = network;
    if (network == PROV_NET_WEB3) {
        d->chain_family = PROV_CHAIN_EVM;
        strcpy(d->chain_id, "1");
        strcpy(d->payout_addr, "0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed");
    }
    strcpy(d->jurisdiction, juris);
    strcpy(d->regions[0].code, region);
    d->n_regions = 1;
    d->sla[0].availability_ppm = 999000;
    d->sla[0].max_latency_ms = 2000;
    d->sla[0].credit_bps = 1000; /* 10% back on a breach */
    d->n_sla = 1;
    prov_offer_t *o = &d->offers[0];
    o->rclass = PROV_RC_INFERENCE;
    o->unit = PROV_UNIT_TOKEN;
    strcpy(o->accel_type, "gpu:generic-80g");
    o->accel_count = 8;
    o->accel_mem_bytes = 80ull << 30;
    o->capacity_per_cycle = 1000000;
    strcpy(o->licence.spdx, spdx);
    o->licence.commercial_use = commercial;
    o->api.shape = PROV_SHAPE_MESSAGES;
    strcpy(o->api.model, "example-model-large");
    strcpy(o->api.usage_in, "usage.input_tokens");
    strcpy(o->api.usage_out, "usage.output_tokens");
    strcpy(o->api.text_path, "content.0.text");
    strcpy(o->api.privacy_header, "X-No-Train");
    o = &d->offers[1];
    o->rclass = PROV_RC_STORAGE;
    o->unit = PROV_UNIT_GIB_HOUR;
    o->capacity_per_cycle = 1000000;
    strcpy(o->licence.spdx, "");
    d->n_offers = 2;
    d->honours_no_train = no_train;
    d->zero_retention = no_train;
    d->version = 1;
}

static uint32_t reg(int k, const prov_desc_t *d)
{
    uint8_t sig[PROV_SIG_BYTES];
    uint32_t h = 0;
    prov_pq_sign_desc(d, PK[k], SK[k], sig);
    int rc = prov_register(&g_n, d, PK[k], sig, &h);
    return rc == PROV_OK ? h : 0;
}

static uint64_t g_nonce[NK];
static uint32_t ask(int k, uint32_t ph, uint8_t offer, uint16_t asset, uint64_t price, uint64_t qty)
{
    uint8_t sig[PROV_SIG_BYTES];
    uint32_t a = 0;
    uint64_t nonce = ++g_nonce[k];
    prov_pq_sign_ask(&g_n, ph, offer, asset, price, qty, nonce, SK[k], sig);
    return prov_ask_post(&g_n, ph, offer, asset, price, qty, nonce, sig, &a) == PROV_OK ? a : 0;
}

static void fresh(const prov_config_t *c)
{
    prov_config_t cfg;
    if (c)
        cfg = *c;
    else
        prov_config_default(&cfg);
    cfg.verify = prov_pq_verify;
    cfg.settle = test_settle;
    if (!cfg.attest) cfg.attest = test_attest;
    prov_net_init(&g_n, &cfg);
    memset(bal_user, 0, sizeof bal_user);
    memset(bal_prov, 0, sizeof bal_prov);
    bal_escrow = bal_commons = 0;
    n_hold = n_final = n_release = 0;
    refuse_all = false;
    memset(g_nonce, 0, sizeof g_nonce);
}

static bool conserved(uint64_t total)
{
    uint64_t s = bal_escrow + bal_commons;
    for (uint32_t i = 0; i <= PROV_MAX_USERS; i++) s += bal_user[i];
    for (uint32_t i = 0; i <= PROV_MAX_PROVIDERS; i++) s += bal_prov[i];
    return s == total;
}

static void job(prov_job_t *j, uint64_t qty, uint64_t ceiling)
{
    prov_job_default(j);
    j->rclass = PROV_RC_INFERENCE;
    j->unit = PROV_UNIT_TOKEN;
    j->asset = 0;
    j->qty = qty;
    j->max_unit_price = ceiling;
}

static uint64_t prov_fills(uint32_t p)
{
    return prov_filled_units(&g_n, p, PROV_RC_INFERENCE, PROV_UNIT_TOKEN, 0);
}

/* ===== tests ===== */

static void t_constants(void)
{
    /* 1/phi^2 in Q32 = floor(2^31 (3 - sqrt 5)) = 3 * 2^31 - ceil(sqrt(5 * 2^62)). */
    pay_u128 v = {0, 0};
    v.hi = 1; /* 5 * 2^62 = 2^64 + 2^62 */
    v.lo = (uint64_t) 1 << 62;
    uint64_t s = pay_isqrt128(v); /* floor(sqrt(5 * 2^62)), never exact */
    uint64_t q = 3ull * (1ull << 31) - (s + 1);
    CHECK(q == PROV_CAP_INV_PHI2_Q32, "1/phi^2 Q32 constant recomputed from an integer sqrt");
    CHECK(prov_cap_units(100, 1, PROV_CAP_INV_PHI2_Q32) == 100, "cap: sole provider may fill all");
    CHECK(prov_cap_units(100, 2, PROV_CAP_INV_PHI2_Q32) == 50, "cap: two providers -> 1/2 each");
    CHECK(prov_cap_units(100, 3, PROV_CAP_INV_PHI2_Q32) == 38, "cap: three -> 38.2% floor");
    CHECK(prov_cap_units(1000, 10, PROV_CAP_INV_PHI2_Q32) == 381,
          "cap: many providers -> floor(1000/phi^2)");
    CHECK(prov_cap_units(7, 3, PROV_CAP_INV_PHI2_Q32) == 3, "cap: equal share ceil(7/3) floor");
    CHECK(prov_cap_units(0, 3, PROV_CAP_INV_PHI2_Q32) == 0, "cap: no demand");
}

static void t_fee_usury(void)
{
    prov_config_t c;
    prov_config_default(&c);
    fresh(&c);
    CHECK(prov_fee(&g_n, 10000) == pay_tithe_phi(10000) && prov_fee(&g_n, 10000) == 161,
          "fee: default is the phi-percent rate (161 on 10000)");
    CHECK(prov_fee(&g_n, 61) == 0, "fee: floor (61 -> 0)");
    uint8_t h1[32], h2[32];
    memcpy(h1, g_n.fee_schedule, 32);
    c.fee_mode = PROV_FEE_BPS;
    c.fee_bps = 30;
    fresh(&c);
    memcpy(h2, g_n.fee_schedule, 32);
    CHECK(prov_fee(&g_n, 10000) == 30 && prov_fee(&g_n, 333) == 0, "fee: flat 30 bps mode, floor");
    CHECK(memcmp(h1, h2, 32) != 0, "fee: schedule hash changes with the schedule");
    prov_config_t bad = c;
    bad.fee_bps = PROV_FEE_MAX_BPS + 1;
    prov_net_t tmp;
    CHECK(prov_net_init(&tmp, &bad) == PROV_ERR_ARG, "fee: above the 5% ceiling refused");
    bad = c;
    bad.cap_q32 = 0;
    CHECK(prov_net_init(&tmp, &bad) == PROV_ERR_ARG, "cap of 0 refused");
    CHECK(prov_charge_check(PROV_CHARGE_INTEREST, 100, 100, false) == PROV_ERR_USURY,
          "usury: interest refused");
    CHECK(prov_charge_check(PROV_CHARGE_LATE_FEE, 100, 1, false) == PROV_ERR_USURY,
          "usury: late fee refused");
    CHECK(prov_charge_check(PROV_CHARGE_HOLDING, 100, 1, false) == PROV_ERR_USURY,
          "usury: time-based balance charge refused");
    CHECK(prov_charge_check(PROV_CHARGE_USAGE, 100, 100, true) == PROV_ERR_USURY,
          "usury: any time-based charge refused");
    CHECK(prov_charge_check(PROV_CHARGE_USAGE, 100, 101, false) == PROV_ERR_USURY,
          "usury: charging above what was held refused");
    CHECK(prov_charge_check(PROV_CHARGE_USAGE, 100, 80, false) == PROV_OK, "usage within hold ok");
    CHECK(prov_charge_check(PROV_CHARGE_NETWORK_FEE, 100, 1, false) == PROV_OK, "fee ok");
    CHECK(prov_charge_check(99, 1, 1, false) == PROV_ERR_ARG, "unknown charge kind refused");
}

static void t_assets(void)
{
    fresh(0);
    uint16_t a;
    CHECK(prov_asset(&g_n, 0) && prov_asset(&g_n, 0)->numeric == PAY_RAIL_DEBIT_CODE,
          "VFV registered at index 0 with the DEBIT rail numeric");
    CHECK(prov_asset_add(&g_n, PROV_ASSET_ISO4217, "EUR", 978, 2, 0, 0, &a) == PROV_OK, "EUR");
    CHECK(prov_asset_add(&g_n, PROV_ASSET_ISO4217, "EUR", 978, 2, 0, 0, &a) == PROV_ERR_DUPLICATE,
          "duplicate asset refused");
    CHECK(prov_asset_add(&g_n, PROV_ASSET_ISO4217, "XAA", PAY_RAIL_CREDIT_CODE, 2, 0, 0, &a) ==
              PROV_ERR_ARG,
          "rail numeric is not an ISO 4217 currency");
    CHECK(prov_asset_add(&g_n, PROV_ASSET_ISO4217, "VFV", 999, 2, 0, 0, &a) == PROV_ERR_ARG,
          "VFV is never fiat");
    CHECK(prov_asset_add(&g_n, PROV_ASSET_ISO4217, "eur", 978, 2, 0, 0, &a) == PROV_ERR_ARG,
          "lower-case code refused");
    CHECK(prov_asset_add(&g_n, PROV_ASSET_CHAIN, "ETH", 0, 18, PROV_CHAIN_EVM, "1", &a) == PROV_OK,
          "chain asset ETH on EVM chain 1");
    CHECK(prov_asset_add(&g_n, PROV_ASSET_CHAIN, "ETH", 0, 18, PROV_CHAIN_EVM, "10", &a) == PROV_OK,
          "same symbol on another chain is a different asset");
    CHECK(prov_asset_add(&g_n, PROV_ASSET_CHAIN, "X", 0, 18, PROV_CHAIN_NONE, "1", &a) ==
              PROV_ERR_ARG,
          "chain asset needs a family");
}

static uint32_t PA, PB, PC, PD; /* provider handles */
static uint32_t U1, U2, U3;

static void setup_world(const prov_config_t *c)
{
    prov_desc_t d;
    fresh(c);
    mkdesc(&d, "Provider A (large)", "US", "US", true, "Apache-2.0", true, PROV_NET_WEB2);
    PA = reg(0, &d);
    mkdesc(&d, "Provider B", "DE", "DE", true, "MIT", true, PROV_NET_WEB2);
    PB = reg(1, &d);
    mkdesc(&d, "Provider C", "JP", "JP", false, "LicenseRef-research-only", false, PROV_NET_WEB2);
    PC = reg(2, &d);
    mkdesc(&d, "Decentralised GPU network D", "SG", "SG", true, "Apache-2.0", true, PROV_NET_WEB3);
    PD = reg(3, &d);
    prov_user_add(&g_n, PK[4], 1000, true, &U1);
    prov_user_add(&g_n, PK[5], 0, false, &U2);
    prov_user_add(&g_n, PK[6], 1000, true, &U3);
    bal_user[U1] = bal_user[U2] = bal_user[U3] = 10000000;
}

static void t_registry(void)
{
    prov_desc_t d;
    uint8_t sig[PROV_SIG_BYTES];
    uint32_t h;
    setup_world(0);
    CHECK(PA && PB && PC && PD, "four providers registered with signed descriptors");
    CHECK(U1 && U2 && U3, "three users");
    mkdesc(&d, "Provider E", "FR", "FR", true, "MIT", true, PROV_NET_WEB2);
    prov_pq_sign_desc(&d, PK[7], SK[7], sig);
    d.offers[0].capacity_per_cycle = 5; /* changed after signing */
    CHECK(prov_register(&g_n, &d, PK[7], sig, &h) == PROV_ERR_AUTH, "tampered descriptor refused");
    mkdesc(&d, "Provider A (large)", "US", "US", true, "Apache-2.0", true, PROV_NET_WEB2);
    prov_pq_sign_desc(&d, PK[0], SK[0], sig);
    CHECK(prov_register(&g_n, &d, PK[0], sig, &h) == PROV_ERR_STATE, "same version replay refused");
    mkdesc(&d, "Web3 no chain", "FR", "FR", true, "MIT", true, PROV_NET_WEB3);
    d.chain_family = PROV_CHAIN_NONE;
    prov_pq_sign_desc(&d, PK[7], SK[7], sig);
    CHECK(prov_register(&g_n, &d, PK[7], sig, &h) == PROV_ERR_ARG, "web3 provider needs a chain");
    mkdesc(&d, "No API", "FR", "FR", true, "MIT", true, PROV_NET_WEB2);
    d.offers[0].api.shape = PROV_SHAPE_NONE;
    prov_pq_sign_desc(&d, PK[7], SK[7], sig);
    CHECK(prov_register(&g_n, &d, PK[7], sig, &h) == PROV_ERR_ARG,
          "inference offer must declare a request shape");
    /* attestation: opaque evidence, judged only by the hook */
    CHECK(!prov_is_attested(&g_n, PA), "not attested before evidence");
    CHECK(prov_attest_present(&g_n, PA, 0, PROV_ATTK_TEE_VM, (const uint8_t *) "BAD!", 4) ==
                  PROV_ERR_AUTH &&
              prov_provider(&g_n, PA)->attest[0].state == PROV_ATT_FAILED,
          "attestation refused by hook -> FAILED");
    CHECK(prov_attest_present(&g_n, PA, 1, PROV_ATTK_ACCEL_CC, (const uint8_t *) "GOOD-q", 6) ==
                  PROV_OK &&
              prov_is_attested(&g_n, PA),
          "attestation accepted by hook -> VERIFIED");
    prov_config_t c;
    prov_config_default(&c);
    c.attest = 0;
    fresh(&c);
    g_n.cfg.attest = 0;
    mkdesc(&d, "Provider A (large)", "US", "US", true, "Apache-2.0", true, PROV_NET_WEB2);
    h = reg(0, &d);
    CHECK(prov_attest_present(&g_n, h, 0, PROV_ATTK_TEE_VM, (const uint8_t *) "GOOD", 4) ==
                  PROV_ERR_HOOK &&
              prov_provider(&g_n, h)->attest[0].state == PROV_ATT_UNVERIFIED &&
              !prov_is_attested(&g_n, h),
          "no attest hook: evidence carried but never counted as verified");
}

static void t_asks(void)
{
    uint8_t sig[PROV_SIG_BYTES];
    uint32_t a;
    setup_world(0);
    CHECK(ask(0, PA, 0, 0, 10, 1000) != 0, "signed ask accepted");
    prov_pq_sign_ask(&g_n, PA, 0, 0, 5, 1000, 50, SK[1], sig); /* B signs A's listing */
    CHECK(prov_ask_post(&g_n, PA, 0, 0, 5, 1000, 50, sig, &a) == PROV_ERR_AUTH,
          "no resale: another key cannot list a provider's service");
    prov_pq_sign_ask(&g_n, PA, 0, 0, 10, 1000, 1, SK[0], sig);
    CHECK(prov_ask_post(&g_n, PA, 0, 0, 10, 1000, 1, sig, &a) == PROV_ERR_DUPLICATE,
          "replayed ask nonce refused");
    prov_pq_sign_ask(&g_n, PA, 0, 0, 10, 2000000, 9, SK[0], sig);
    CHECK(prov_ask_post(&g_n, PA, 0, 0, 10, 2000000, 9, sig, &a) == PROV_ERR_ARG,
          "ask above declared capacity refused");
    prov_pq_sign_ask(&g_n, PA, 0, 0, 10, 100, 10, SK[0], sig);
    CHECK(prov_ask_post(&g_n, PA, 0, 0, 11, 100, 10, sig, &a) == PROV_ERR_AUTH,
          "price changed after signing refused");
}

static void t_match_cap(void)
{
    prov_job_t j;
    uint32_t b, nf;
    setup_world(0);
    ask(0, PA, 0, 0, 10, 1000);
    ask(1, PB, 0, 0, 12, 1000);
    ask(3, PD, 0, 0, 15, 1000);
    job(&j, 1000, 20);
    CHECK(prov_bid_post(&g_n, U1, &j, &b) == PROV_OK, "bid posted");
    uint64_t before = bal_user[U1];
    CHECK(prov_match(&g_n, PROV_RC_INFERENCE, PROV_UNIT_TOKEN, 0, &nf) == PROV_OK && nf == 3,
          "three fills");
    printf("  A=%llu B=%llu D=%llu\n", (unsigned long long) prov_fills(PA),
           (unsigned long long) prov_fills(PB), (unsigned long long) prov_fills(PD));
    CHECK(prov_fills(PA) == 381 && prov_fills(PB) == 381 && prov_fills(PD) == 238,
          "cheapest providers capped at floor(1000/phi^2); excess flows to the next ask");
    CHECK(before - bal_user[U1] == 381 * 10 + 381 * 12 + 238 * 15,
          "each fill executes at the provider's own ask price");
    CHECK(!g_n.bids[b - 1].open, "bid fully filled and closed");
    CHECK(conserved(30000000), "hold conservation");

    /* sole provider: no cap */
    setup_world(0);
    ask(0, PA, 0, 0, 10, 5000);
    job(&j, 1000, 20);
    prov_bid_post(&g_n, U1, &j, &b);
    prov_match(&g_n, PROV_RC_INFERENCE, PROV_UNIT_TOKEN, 0, &nf);
    CHECK(prov_fills(PA) == 1000, "a sole provider may fill everything (not banned)");

    /* price ceiling: only A is cheap enough; cap still binds; rest waits */
    setup_world(0);
    ask(0, PA, 0, 0, 10, 1000);
    ask(1, PB, 0, 0, 12, 1000);
    ask(3, PD, 0, 0, 15, 1000);
    job(&j, 1000, 11);
    prov_bid_post(&g_n, U1, &j, &b);
    prov_match(&g_n, PROV_RC_INFERENCE, PROV_UNIT_TOKEN, 0, &nf);
    CHECK(prov_fills(PA) == 381 && prov_fills(PB) == 0 && prov_fills(PD) == 0,
          "ceiling respected: nobody above 11 is used");
    CHECK(g_n.bids[b - 1].open && g_n.bids[b - 1].filled == 381,
          "unfilled demand stays open for later supply, uncharged");
    CHECK(bal_user[U1] == 10000000 - 3810, "only matched units are held");
    /* next cycle the cap resets */
    prov_next_cycle(&g_n);
    prov_match(&g_n, PROV_RC_INFERENCE, PROV_UNIT_TOKEN, 0, &nf);
    CHECK(prov_fills(PA) > 0 && g_n.bids[b - 1].filled > 381, "new cycle: matching resumes");

    /* budget cap */
    setup_world(0);
    ask(0, PA, 0, 0, 10, 5000);
    job(&j, 1000, 20);
    j.max_total = 5000;
    prov_bid_post(&g_n, U1, &j, &b);
    prov_match(&g_n, PROV_RC_INFERENCE, PROV_UNIT_TOKEN, 0, &nf);
    CHECK(prov_fills(PA) == 500 && bal_user[U1] == 10000000 - 5000,
          "budget cap: 5000 at 10/unit buys 500 units, never more");

    /* all-or-none */
    setup_world(0);
    ask(0, PA, 0, 0, 10, 1000);
    ask(1, PB, 0, 0, 12, 1000);
    job(&j, 800, 20);
    j.allow_split = false;
    prov_bid_post(&g_n, U1, &j, &b);
    prov_match(&g_n, PROV_RC_INFERENCE, PROV_UNIT_TOKEN, 0, &nf);
    CHECK(nf == 0 && prov_fills(PA) == 0, "no-split job larger than the cap is not split");
    job(&j, 300, 20);
    j.allow_split = false;
    prov_bid_post(&g_n, U2, &j, &b);
    prov_match(&g_n, PROV_RC_INFERENCE, PROV_UNIT_TOKEN, 0, &nf);
    CHECK(nf == 1, "no-split job within one provider's cap fills whole");

    /* price-time priority among bids: higher ceiling first */
    setup_world(0);
    ask(0, PA, 0, 0, 10, 100);
    uint32_t b1, b2;
    job(&j, 100, 12);
    prov_bid_post(&g_n, U1, &j, &b1);
    job(&j, 100, 15);
    prov_bid_post(&g_n, U2, &j, &b2);
    prov_match(&g_n, PROV_RC_INFERENCE, PROV_UNIT_TOKEN, 0, &nf);
    CHECK(g_n.bids[b2 - 1].filled == 100 && g_n.bids[b1 - 1].filled == 0,
          "price-time priority: the higher ceiling is served first");
    /* time priority among equal asks */
    setup_world(0);
    ask(1, PB, 0, 0, 10, 100);
    ask(0, PA, 0, 0, 10, 100);
    job(&j, 100, 10);
    prov_bid_post(&g_n, U1, &j, &b);
    prov_match(&g_n, PROV_RC_INFERENCE, PROV_UNIT_TOKEN, 0, &nf);
    CHECK(prov_fills(PB) == 50 && prov_fills(PA) == 50,
          "equal price: earlier ask first, capped at the equal share");
    /* unfunded user: hold refused, no fill */
    setup_world(0);
    ask(0, PA, 0, 0, 10, 100);
    bal_user[U3] = 5;
    job(&j, 100, 10);
    prov_bid_post(&g_n, U3, &j, &b);
    prov_match(&g_n, PROV_RC_INFERENCE, PROV_UNIT_TOKEN, 0, &nf);
    CHECK(nf == 0 && prov_fills(PA) == 0, "hold refused by settlement: no fill");
}

static void t_filters(void)
{
    prov_job_t j;
    uint32_t b, nf;
    setup_world(0);
    uint32_t aA = ask(0, PA, 0, 0, 10, 1000), aB = ask(1, PB, 0, 0, 12, 1000);
    uint32_t aC = ask(2, PC, 0, 0, 8, 1000), aD = ask(3, PD, 0, 0, 15, 1000);
    job(&j, 10, 20);
    CHECK(prov_eligible(&g_n, &j, aA) && prov_eligible(&g_n, &j, aC), "no filters: all eligible");
    strcpy(j.regions[0].code, "DE");
    j.n_regions = 1;
    CHECK(!prov_eligible(&g_n, &j, aA) && prov_eligible(&g_n, &j, aB),
          "data residency: only the DE offer");
    job(&j, 10, 20);
    strcpy(j.jurisdictions[0].code, "SG");
    j.n_jurisdictions = 1;
    CHECK(prov_eligible(&g_n, &j, aD) && !prov_eligible(&g_n, &j, aB), "jurisdiction filter");
    job(&j, 10, 20);
    j.require_commercial = true;
    CHECK(!prov_eligible(&g_n, &j, aC) && prov_eligible(&g_n, &j, aA),
          "commercial-use licence filter");
    job(&j, 10, 20);
    strcpy(j.spdx_allow[0], "MIT");
    j.n_spdx = 1;
    CHECK(prov_eligible(&g_n, &j, aB) && !prov_eligible(&g_n, &j, aA), "SPDX allowlist");
    job(&j, 10, 20);
    j.no_train = true;
    CHECK(!prov_eligible(&g_n, &j, aC) && prov_eligible(&g_n, &j, aA),
          "no-train request never routed to a provider that does not honour it");
    job(&j, 10, 20);
    j.require_attested = true;
    CHECK(!prov_eligible(&g_n, &j, aA), "attested-only: nobody attested yet");
    prov_attest_present(&g_n, PA, 0, PROV_ATTK_TEE_VM, (const uint8_t *) "GOOD", 4);
    CHECK(prov_eligible(&g_n, &j, aA) && !prov_eligible(&g_n, &j, aB),
          "attested-only: verified provider eligible");
    job(&j, 10, 20);
    j.net_mask = 2;
    CHECK(prov_eligible(&g_n, &j, aD) && !prov_eligible(&g_n, &j, aA), "web3-only filter");
    job(&j, 10, 20);
    j.min_availability_ppm = 999500;
    CHECK(!prov_eligible(&g_n, &j, aA), "minimum SLA filter");
    /* the cheapest (C) is skipped for a no-train job; filters feed matching */
    job(&j, 100, 20);
    j.no_train = true;
    prov_bid_post(&g_n, U1, &j, &b);
    prov_match(&g_n, PROV_RC_INFERENCE, PROV_UNIT_TOKEN, 0, &nf);
    CHECK(prov_fills(PC) == 0 && prov_fills(PA) > 0, "matching applies the privacy filter");
    CHECK(g_n.fills[0].no_train, "no-train flag carried into the fill");
}

/* one matched fill for receipts */
static uint32_t one_fill(uint64_t qty, uint64_t price)
{
    prov_job_t j;
    uint32_t b, nf;
    setup_world(0);
    ask(0, PA, 0, 0, price, qty);
    job(&j, qty, price);
    j.no_train = true;
    j.no_retain = true;
    memset(j.request_hash, 0xAB, 32);
    prov_bid_post(&g_n, U1, &j, &b);
    prov_match(&g_n, PROV_RC_INFERENCE, PROV_UNIT_TOKEN, 0, &nf);
    for (uint32_t i = 1; i <= PROV_MAX_FILLS; i++)
        if (prov_fill(&g_n, i)) return i;
    return 0;
}

static void usage(prov_usage_t *u, uint64_t units, uint8_t outcome)
{
    memset(u, 0, sizeof *u);
    u->units = units;
    u->sla_outcome = outcome;
    u->latency_ms = 120;
    memset(u->response_hash, 0xCD, 32);
    u->start_tick = 100;
    u->end_tick = 160;
}

static prov_receipt_t R1, R2, Rbad;

static void t_receipts(void)
{
    prov_usage_t u;
    uint32_t f = one_fill(1000, 10);
    CHECK(f != 0, "fill made");
    usage(&u, 2000, PROV_SLA_MET);
    CHECK(prov_receipt_build(&g_n, f, &u, &R1) == PROV_ERR_ARG, "units above reservation refused");
    usage(&u, 700, PROV_SLA_MET);
    CHECK(prov_receipt_build(&g_n, f, &u, &R1) == PROV_OK, "receipt built");
    CHECK(R1.gross == 7000 && R1.fee == pay_tithe_phi(7000) && R1.net == 7000 - R1.fee &&
              R1.refund == 3000 && R1.hold == 10000,
          "receipt money: gross, fee on gross, net, refund of unused hold");
    CHECK((R1.flags & PROV_RF_NO_TRAIN) && (R1.flags & PROV_RF_NO_RETAIN),
          "privacy flags recorded in the receipt");
    uint8_t body[PROV_RECEIPT_BODY + 4];
    prov_receipt_t D;
    CHECK(prov_receipt_encode(&R1, body, sizeof body) == (int32_t) PROV_RECEIPT_BODY,
          "encode: fixed body size");
    CHECK(prov_receipt_decode(body, PROV_RECEIPT_BODY, &D) == PROV_OK, "decode");
    uint8_t d1[32], d2[32];
    prov_receipt_digest(&R1, d1);
    prov_receipt_digest(&D, d2);
    CHECK(memcmp(d1, d2, 32) == 0 && D.units == 700 && D.fee == R1.fee, "round trip");
    CHECK(prov_receipt_decode(body, PROV_RECEIPT_BODY - 1, &D) == PROV_ERR_PARSE,
          "decode: short body refused");
    body[0] = 2;
    CHECK(prov_receipt_decode(body, PROV_RECEIPT_BODY, &D) == PROV_ERR_PARSE,
          "decode: unknown version refused");
    body[0] = 1;
    body[4 + 224 + 3] = 0x80;
    CHECK(prov_receipt_decode(body, PROV_RECEIPT_BODY, &D) == PROV_ERR_PARSE,
          "decode: unknown flag bits refused");

    /* half-signed: dispute, no money */
    prov_receipt_t half = R1;
    prov_pq_sign_receipt(&half, SK[0], true);
    CHECK(prov_receipt_verify(&g_n, &half) == PROV_ERR_AUTH, "verify: user signature missing");
    int fin0 = n_final;
    CHECK(prov_settle(&g_n, f, &half) == PROV_ERR_AUTH &&
              prov_fill(&g_n, f)->state == PROV_FILL_DISPUTED && n_final == fin0,
          "half-signed receipt opens a dispute and moves no money");

    /* fully co-signed resolves it */
    prov_pq_sign_receipt(&R1, SK[0], true);
    prov_pq_sign_receipt(&R1, SK[4], false);
    CHECK(prov_receipt_verify(&g_n, &R1) == PROV_OK, "co-signed receipt verifies");
    CHECK(prov_settle(&g_n, f, &R1) == PROV_ERR_STATE, "settle refuses a disputed fill");
    uint64_t p0 = bal_prov[PA], c0 = bal_commons, u0 = bal_user[U1];
    CHECK(prov_dispute_resolve(&g_n, f, &R1, false) == PROV_OK, "dispute resolved by receipt");
    CHECK(bal_prov[PA] - p0 == R1.net && bal_commons - c0 == R1.fee &&
              bal_user[U1] - u0 == R1.refund,
          "settlement: provider net, commons fee, user refund");
    CHECK(conserved(30000000), "conservation after settlement");
    CHECK(prov_dispute_resolve(&g_n, f, &R1, false) == PROV_ERR_DUPLICATE &&
              prov_settle(&g_n, f, &R1) == PROV_ERR_DUPLICATE,
          "idempotent: second settlement refused");
    CHECK(R1.net * 100 >= R1.gross * 98, "provider keeps over 98% of gross under the default fee");

    /* tamper cases on a fresh fill */
    f = one_fill(1000, 10);
    usage(&u, 500, PROV_SLA_MET);
    prov_receipt_build(&g_n, f, &u, &R2);
    prov_pq_sign_receipt(&R2, SK[0], true);
    prov_pq_sign_receipt(&R2, SK[4], false);
    Rbad = R2;
    Rbad.units = 600; /* arithmetic no longer matches */
    CHECK(prov_settle(&g_n, f, &Rbad) == PROV_ERR_TAMPER &&
              prov_fill(&g_n, f)->state == PROV_FILL_RESERVED,
          "tamper: units changed -> TAMPER, fill untouched");
    Rbad = R2;
    Rbad.units = 600;
    Rbad.gross = 6000;
    Rbad.fee = pay_tithe_phi(6000);
    Rbad.net = 6000 - Rbad.fee;
    Rbad.refund = 4000;
    CHECK(prov_receipt_verify(&g_n, &Rbad) == PROV_ERR_AUTH,
          "tamper: consistent but re-priced body fails both signatures");
    Rbad = R2;
    Rbad.fee = 0;
    Rbad.net = R2.gross;
    CHECK(prov_settle(&g_n, f, &Rbad) == PROV_ERR_TAMPER, "tamper: fee skipped -> TAMPER");
    Rbad = R2;
    memset(Rbad.fee_schedule, 0, 32);
    CHECK(prov_receipt_verify(&g_n, &Rbad) == PROV_ERR_TAMPER,
          "tamper: other fee schedule -> TAMPER");
    Rbad = R2;
    Rbad.flags &= (uint8_t) ~PROV_RF_NO_TRAIN;
    CHECK(prov_settle(&g_n, f, &Rbad) == PROV_ERR_TAMPER, "tamper: no-train flag dropped");
    Rbad = R2;
    Rbad.job_id[0] ^= 1;
    CHECK(prov_settle(&g_n, f, &Rbad) == PROV_ERR_TAMPER, "tamper: receipt for another job");
    Rbad = R2;
    memcpy(Rbad.sig_user, R2.sig_provider, PROV_SIG_BYTES);
    CHECK(prov_receipt_verify(&g_n, &Rbad) == PROV_ERR_AUTH,
          "tamper: provider signature copied into the user slot");
    Rbad = R2;
    Rbad.response_hash[5] ^= 0x10;
    CHECK(prov_settle(&g_n, f, &Rbad) == PROV_ERR_AUTH &&
              prov_fill(&g_n, f)->state == PROV_FILL_DISPUTED,
          "tamper: response hash changed -> signatures fail -> dispute");
    CHECK(prov_dispute_resolve(&g_n, f, &R2, true) == PROV_OK &&
              prov_provider(&g_n, PA)->rep.disputes_lost == 1,
          "dispute lost by provider is recorded");

    /* SLA breach credit */
    f = one_fill(1000, 10);
    usage(&u, 1000, PROV_SLA_LATENCY);
    prov_receipt_build(&g_n, f, &u, &R2);
    CHECK(R2.sla_credit == 1000 && R2.fee == pay_tithe_phi(9000) && R2.net == 9000 - R2.fee &&
              R2.refund == 1000,
          "SLA breach: provider's declared 10% credit back to the user, fee on the rest");
    prov_pq_sign_receipt(&R2, SK[0], true);
    prov_pq_sign_receipt(&R2, SK[4], false);
    CHECK(prov_settle(&g_n, f, &R2) == PROV_OK, "breach receipt settles");
    CHECK(prov_provider(&g_n, PA)->rep.sla_breached == 1, "breach counted in reputation");

    /* settlement hook refusal leaves state */
    f = one_fill(100, 10);
    usage(&u, 100, PROV_SLA_MET);
    prov_receipt_build(&g_n, f, &u, &R2);
    prov_pq_sign_receipt(&R2, SK[0], true);
    prov_pq_sign_receipt(&R2, SK[4], false);
    refuse_all = true;
    CHECK(prov_settle(&g_n, f, &R2) == PROV_ERR_SETTLE &&
              prov_fill(&g_n, f)->state == PROV_FILL_RESERVED,
          "settle hook refusal changes nothing");
    refuse_all = false;
    CHECK(prov_settle(&g_n, f, &R2) == PROV_OK, "retry settles");
}

static void t_reputation(void)
{
    prov_config_t c;
    prov_usage_t u;
    prov_job_t j;
    uint32_t b, nf;
    prov_config_default(&c);
    c.sybil = PROV_SYBIL_STAKE;
    c.min_stake = 100;
    c.rep_max_per_user = 2;
    setup_world(&c);
    CHECK(prov_rep_score_q16(&prov_provider(&g_n, PA)->rep) == 32768,
          "new provider starts at the neutral prior (0.5)");
    static prov_receipt_t rs[6];
    uint8_t upk_idx[6];
    int nr = 0;
    ask(0, PA, 0, 0, 10, 100000);
    /* U1 (staked) x3, U2 (no stake) x2 */
    uint32_t users[5] = {U1, U1, U1, U2, U2};
    int ukey[5] = {4, 4, 4, 5, 5};
    for (int i = 0; i < 5; i++) {
        job(&j, 10, 10);
        prov_bid_post(&g_n, users[i], &j, &b);
        prov_match(&g_n, PROV_RC_INFERENCE, PROV_UNIT_TOKEN, 0, &nf);
        uint32_t f = 0;
        for (uint32_t k = 1; k <= PROV_MAX_FILLS; k++) {
            const prov_fill_t *x = prov_fill(&g_n, k);
            if (x && x->bid == b && x->state == PROV_FILL_RESERVED) f = k;
        }
        usage(&u, 10, PROV_SLA_MET);
        prov_receipt_build(&g_n, f, &u, &rs[nr]);
        prov_pq_sign_receipt(&rs[nr], SK[0], true);
        prov_pq_sign_receipt(&rs[nr], SK[ukey[i]], false);
        prov_settle(&g_n, f, &rs[nr]);
        upk_idx[nr] = (uint8_t) ukey[i];
        nr++;
    }
    const prov_rep_t *r = &prov_provider(&g_n, PA)->rep;
    CHECK(r->receipts == 2 && r->sla_met == 2,
          "Sybil: unstaked counterparties do not count; one user counts at most 2 per cycle");
    CHECK(prov_rep_score_q16(r) == (3u << 16) / 4u, "score (met+1)/(met+breached+2)");
    /* export and rebuild elsewhere */
    uint8_t ex[8][32];
    uint32_t ne = prov_rep_export(&g_n, PA, ex, 8);
    uint8_t dg[32];
    prov_receipt_digest(&rs[4], dg);
    CHECK(ne == 5 && memcmp(ex[0], dg, 32) == 0, "export: every settled receipt, newest first");
    static uint8_t upks[7][PROV_PK_BYTES];
    for (int i = 0; i < nr; i++) memcpy(upks[i], PK[upk_idx[i]], PROV_PK_BYTES);
    prov_rep_t rb;
    CHECK(prov_rep_rebuild(prov_pq_verify, 0, prov_provider(&g_n, PA)->id, PK[0], rs,
                           (const uint8_t(*)[PROV_PK_BYTES]) upks, (uint32_t) nr, &rb) == 5 &&
              rb.units_served == 50,
          "rebuild: portable receipts re-verify on another node");
    rs[5] = rs[0];
    memcpy(upks[5], upks[0], PROV_PK_BYTES);
    CHECK(prov_rep_rebuild(prov_pq_verify, 0, prov_provider(&g_n, PA)->id, PK[0], rs,
                           (const uint8_t(*)[PROV_PK_BYTES]) upks, 6, &rb) == 5,
          "rebuild: a duplicated receipt counts once");
    rs[5].units = 9;
    rs[5].gross = 90;
    rs[5].fee = pay_tithe_phi(90);
    rs[5].net = 90 - rs[5].fee;
    rs[5].refund = rs[5].hold - 90;
    CHECK(prov_rep_rebuild(prov_pq_verify, 0, prov_provider(&g_n, PA)->id, PK[0], rs,
                           (const uint8_t(*)[PROV_PK_BYTES]) upks, 6, &rb) == 5,
          "rebuild: a forged receipt does not count");
    CHECK(prov_rep_rebuild(prov_pq_verify, 0, prov_provider(&g_n, PA)->id, PK[1], rs,
                           (const uint8_t(*)[PROV_PK_BYTES]) upks, 5, &rb) == 0,
          "rebuild: wrong provider key counts nothing");
    /* next cycle: U1 counts again */
    prov_next_cycle(&g_n);
    job(&j, 10, 10);
    prov_bid_post(&g_n, U1, &j, &b);
    prov_match(&g_n, PROV_RC_INFERENCE, PROV_UNIT_TOKEN, 0, &nf);
    uint32_t f = 0;
    for (uint32_t k = 1; k <= PROV_MAX_FILLS; k++) {
        const prov_fill_t *x = prov_fill(&g_n, k);
        if (x && x->bid == b && x->state == PROV_FILL_RESERVED) f = k;
    }
    usage(&u, 10, PROV_SLA_MET);
    prov_receipt_t r6;
    prov_receipt_build(&g_n, f, &u, &r6);
    prov_pq_sign_receipt(&r6, SK[0], true);
    prov_pq_sign_receipt(&r6, SK[4], false);
    prov_settle(&g_n, f, &r6);
    CHECK(prov_provider(&g_n, PA)->rep.receipts == 3, "per-cycle limit resets next cycle");
    /* self-dealing */
    uint32_t self;
    CHECK(prov_user_add(&g_n, PK[1], 1000, true, &self) == PROV_OK, "provider B's key as a user");
    bal_user[self] = 100000;
    ask(1, PB, 0, 0, 10, 100);
    job(&j, 10, 10);
    prov_bid_post(&g_n, self, &j, &b);
    prov_match(&g_n, PROV_RC_INFERENCE, PROV_UNIT_TOKEN, 0, &nf);
    f = 0;
    for (uint32_t k = 1; k <= PROV_MAX_FILLS; k++) {
        const prov_fill_t *x = prov_fill(&g_n, k);
        if (x && x->bid == b && x->provider == PB && x->state == PROV_FILL_RESERVED) f = k;
    }
    usage(&u, f ? prov_fill(&g_n, f)->qty : 1, PROV_SLA_MET);
    prov_receipt_build(&g_n, f, &u, &r6);
    prov_pq_sign_receipt(&r6, SK[1], true);
    prov_pq_sign_receipt(&r6, SK[1], false);
    CHECK(f && prov_settle(&g_n, f, &r6) == PROV_OK && prov_provider(&g_n, PB)->rep.receipts == 0,
          "self-dealing pays the fee but earns no reputation");
}

static void t_leave(void)
{
    prov_job_t j;
    uint32_t b, nf;
    prov_desc_t d;
    setup_world(0);
    uint32_t a = ask(0, PA, 0, 0, 10, 1000);
    job(&j, 100, 10);
    prov_bid_post(&g_n, U1, &j, &b);
    prov_match(&g_n, PROV_RC_INFERENCE, PROV_UNIT_TOKEN, 0, &nf);
    CHECK(bal_user[U1] == 10000000 - 1000, "hold placed");
    CHECK(prov_provider_leave(&g_n, PA) == PROV_OK, "provider leaves at any time");
    CHECK(bal_user[U1] == 10000000 && n_release == 1, "unstarted holds released in full, no fee");
    CHECK(!g_n.asks[a - 1].live && g_n.bids[b - 1].open, "asks withdrawn; user demand re-opens");
    CHECK(ask(0, PA, 0, 0, 10, 10) == 0, "a departed provider cannot list");
    mkdesc(&d, "Provider A (large)", "US", "US", true, "Apache-2.0", true, PROV_NET_WEB2);
    d.version = 2;
    CHECK(reg(0, &d) == PA && prov_provider(&g_n, PA)->active, "and may come back (no lock-out)");
}

/* ===== adapter ===== */
static void t_adapter(void)
{
    prov_env_t e, d;
    static uint8_t buf[4096];
    static char body[4096];
    prov_api_t api;
    memset(&api, 0, sizeof api);
    memset(&e, 0, sizeof e);
    e.shape = PROV_SHAPE_MESSAGES;
    e.flags = PROV_EF_NO_TRAIN;
    e.max_tokens = 256;
    memset(e.job_id, 7, 32);
    e.system = prov_s("Be brief.");
    e.msgs[0].role = PROV_ROLE_USER;
    e.msgs[0].text = prov_s("Hello \"world\"");
    e.msgs[1].role = PROV_ROLE_ASSISTANT;
    e.msgs[1].text = prov_s("Hi.");
    e.n_msgs = 2;
    int32_t n = prov_env_encode(&e, buf, sizeof buf);
    CHECK(n > 0 && prov_env_decode(buf, (uint32_t) n, &d) == PROV_OK, "envelope round trip");
    CHECK(d.shape == e.shape && d.flags == e.flags && d.max_tokens == 256 && d.n_msgs == 2 &&
              d.msgs[0].text.n == e.msgs[0].text.n &&
              memcmp(d.msgs[0].text.p, "Hello \"world\"", d.msgs[0].text.n) == 0 &&
              memcmp(d.job_id, e.job_id, 32) == 0,
          "envelope fields preserved");
    uint8_t h1[32], h2[32];
    static uint8_t scratch[4096];
    prov_env_hash(&e, scratch, sizeof scratch, h1);
    prov_env_hash(&d, scratch, sizeof scratch, h2);
    CHECK(memcmp(h1, h2, 32) == 0, "re-encoded envelope hashes the same (request_hash)");
    CHECK(prov_env_decode(buf, (uint32_t) n - 1, &d) == PROV_ERR_PARSE, "truncated refused");
    buf[n] = 0;
    CHECK(prov_env_decode(buf, (uint32_t) n + 1, &d) == PROV_ERR_PARSE, "trailing byte refused");
    buf[0] = 'Y';
    CHECK(prov_env_decode(buf, (uint32_t) n, &d) == PROV_ERR_PARSE, "bad magic refused");
    buf[0] = 'Z';
    buf[5] = 9;
    CHECK(prov_env_decode(buf, (uint32_t) n, &d) == PROV_ERR_PARSE, "unknown shape refused");
    CHECK(prov_env_encode(&e, buf, 10) == PROV_ERR_SPACE, "encode into a small buffer refused");

    api.shape = PROV_SHAPE_MESSAGES;
    strcpy(api.model, "example-model-large");
    strcpy(api.usage_in, "usage.input_tokens");
    strcpy(api.usage_out, "usage.output_tokens");
    strcpy(api.text_path, "content.0.text");
    strcpy(api.privacy_header, "X-No-Train");
    n = prov_adapter_body(&api, &e, body, sizeof body);
    printf("  %s\n", body);
    CHECK(n > 0 && strcmp(body, "{\"model\":\"example-model-large\",\"max_tokens\":256,\"system\":"
                                "\"Be brief.\",\"messages\":[{\"role\":\"user\",\"content\":"
                                "\"Hello \\\"world\\\"\"},{\"role\":\"assistant\",\"content\":"
                                "\"Hi.\"}]}") == 0,
          "messages-style body");
    char hdr[64];
    CHECK(prov_adapter_privacy_header(&api, &e, hdr, sizeof hdr) == 15 &&
              strcmp(hdr, "X-No-Train: 1\r\n") == 0,
          "declared privacy header emitted for no-train");
    api.shape = PROV_SHAPE_CHAT;
    CHECK(prov_adapter_body(&api, &e, body, sizeof body) == PROV_ERR_UNSUPPORTED,
          "shape mismatch refused");
    e.shape = PROV_SHAPE_CHAT;
    e.model = prov_s("override-model");
    n = prov_adapter_body(&api, &e, body, sizeof body);
    CHECK(n > 0 && strcmp(body, "{\"model\":\"override-model\",\"messages\":[{\"role\":\"system\","
                                "\"content\":\"Be brief.\"},{\"role\":\"user\",\"content\":"
                                "\"Hello \\\"world\\\"\"},{\"role\":\"assistant\",\"content\":"
                                "\"Hi.\"}],\"max_tokens\":256}") == 0,
          "chat-style body (system as a leading message)");
    memset(&e, 0, sizeof e);
    e.shape = PROV_SHAPE_COMPLETION;
    e.prompt = prov_s("Once upon");
    e.max_tokens = 5;
    api.shape = PROV_SHAPE_COMPLETION;
    n = prov_adapter_body(&api, &e, body, sizeof body);
    CHECK(n > 0 && strcmp(body, "{\"model\":\"example-model-large\",\"prompt\":\"Once upon\","
                                "\"max_tokens\":5}") == 0,
          "completion body");
    CHECK(prov_adapter_privacy_header(&api, &e, hdr, sizeof hdr) == 0 && hdr[0] == 0,
          "no privacy header without the flag");
    memset(&e, 0, sizeof e);
    e.shape = PROV_SHAPE_EMBEDDINGS;
    e.inputs[0] = prov_s("a");
    e.inputs[1] = prov_s("b c");
    e.n_inputs = 2;
    api.shape = PROV_SHAPE_EMBEDDINGS;
    n = prov_adapter_body(&api, &e, body, sizeof body);
    CHECK(n > 0 &&
              strcmp(body, "{\"model\":\"example-model-large\",\"input\":[\"a\",\"b c\"]}") == 0,
          "embeddings body");
    n = prov_env_encode(&e, buf, sizeof buf);
    CHECK(n > 0 && prov_env_decode(buf, (uint32_t) n, &d) == PROV_OK && d.n_inputs == 2 &&
              d.inputs[1].n == 3,
          "embeddings envelope round trip");
    memset(&e, 0, sizeof e);
    e.shape = PROV_SHAPE_RAW;
    e.prompt.p = "\x01\x02\x00\x03";
    e.prompt.n = 4;
    api.shape = PROV_SHAPE_RAW;
    n = prov_adapter_body(&api, &e, body, sizeof body);
    CHECK(n == 4 && memcmp(body, "\x01\x02\x00\x03", 4) == 0, "raw passthrough");
    n = prov_env_encode(&e, buf, sizeof buf);
    CHECK(n > 0 && prov_env_decode(buf, (uint32_t) n, &d) == PROV_OK && d.prompt.n == 4 &&
              memcmp(d.prompt.p, "\x01\x02\x00\x03", 4) == 0,
          "raw envelope round trip with binary bytes");

    /* responses */
    prov_resp_t r;
    uint64_t units;
    const char *j1 = "{\"id\":\"x\",\"content\":[{\"type\":\"text\",\"text\":\"Hi \\u00e9\"}],"
                     "\"usage\":{\"input_tokens\":12,\"output_tokens\":34}}";
    api.shape = PROV_SHAPE_MESSAGES;
    CHECK(prov_adapter_parse(&api, j1, (uint32_t) strlen(j1), &r) == PROV_OK && r.has_in &&
              r.has_out && r.units_in == 12 && r.units_out == 34 && r.has_text &&
              strcmp(r.text, "Hi \xc3\xa9") == 0,
          "messages-style response parsed via declared paths");
    CHECK(prov_adapter_units(&r, &units) == PROV_OK && units == 46, "metered units = in + out");
    strcpy(api.usage_in, "usage.prompt_tokens");
    strcpy(api.usage_out, "usage.completion_tokens");
    strcpy(api.text_path, "choices.0.message.content");
    const char *j2 = "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"yo\"}}],"
                     "\"usage\":{\"prompt_tokens\":5,\"completion_tokens\":7,\"total\":12}}";
    CHECK(prov_adapter_parse(&api, j2, (uint32_t) strlen(j2), &r) == PROV_OK && r.units_in == 5 &&
              r.units_out == 7 && strcmp(r.text, "yo") == 0,
          "chat-style response parsed via declared paths");
    const char *j3 = "{\"usage\":{\"prompt_tokens\":-5}}";
    CHECK(prov_adapter_parse(&api, j3, (uint32_t) strlen(j3), &r) == PROV_ERR_PARSE,
          "negative usage refused");
    const char *j4 = "{\"usage\":{\"prompt_tokens\":5,}";
    CHECK(prov_adapter_parse(&api, j4, (uint32_t) strlen(j4), &r) == PROV_ERR_PARSE,
          "malformed JSON refused");
    const char *j5 = "{\"other\":1}";
    CHECK(prov_adapter_parse(&api, j5, (uint32_t) strlen(j5), &r) == PROV_OK && !r.has_in &&
              prov_adapter_units(&r, &units) == PROV_ERR_NOT_FOUND,
          "missing usage: no units invented");
    const char *j6 = "{\"usage\":{\"prompt_tokens\":5,\"prompt_tokens\":500}}";
    CHECK(prov_adapter_parse(&api, j6, (uint32_t) strlen(j6), &r) == PROV_ERR_PARSE,
          "duplicate usage key refused (never silently picks a value)");
}

int main(void)
{
    keys();
    t_constants();
    t_fee_usury();
    t_assets();
    t_registry();
    t_asks();
    t_match_cap();
    t_filters();
    t_receipts();
    t_reputation();
    t_leave();
    t_adapter();
    printf("test_prov: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
