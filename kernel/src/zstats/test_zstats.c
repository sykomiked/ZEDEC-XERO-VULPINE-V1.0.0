/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zstats.c — the public statistics feed (zstats.h S1-S6) over a real
 * pay_ledger shared by real svault, vswap, vbond, vauction, grants and zenny
 * instances: collection against hand-computed figures, absent sections,
 * G_rate with den 0, the k-anonymity boundary and complementary suppression,
 * the canonical encoding and a fixed digest vector, the signed hash chain,
 * every consistency rule, tampering, the exact JSON and the read-only
 * guarantee. The signature hook is a stand-in (SHA-256 keyed by the signer
 * id); production passes ML-DSA verification. Integers only. */
#include <stdio.h>
#include <string.h>
#include "zstats.h"
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

#define GATE   77u
#define JURIS  0x0050534Eu
#define PUB    42u
#define NU     12
#define OWN(i) (100u + (uint32_t) (i))

static pay_ledger_t L;
static svault_t V;
static vswap_t S;
static vbond_t B;
static vauc_t A;
static grant_t G;
static zenny_t Z;
static uint16_t vno, vbl;
static uint32_t viss, rsv, vcash, biss, vvb, lockM, lockD, block, ars, aes, raf;
static uint32_t uv[NU], ub[NU], brl[NU];
static uint64_t nfund;

/* stand-in signature: SHA-256(secret(signer) || msg) */
static void sign(uint32_t who, const uint8_t *m, uint32_t n, uint8_t sig[32])
{
    uint8_t b[64];
    memset(b, 0, sizeof b);
    b[0] = (uint8_t) who;
    b[1] = 0x5A;
    b[2] = (uint8_t) (who >> 8);
    memcpy(b + 8, m, n);
    sha256(b, 8 + n, sig);
}
static uint32_t nverify;
static bool verify(void *ctx, uint32_t who, const uint8_t *m, uint32_t n, const uint8_t *sig,
                   uint32_t sl)
{
    (void) ctx;
    uint8_t want[32];
    nverify++;
    if (sl != 32 || n > 56) return false;
    sign(who, m, n, want);
    return !memcmp(want, sig, 32);
}

static pay_status_t raw(uint32_t a, int64_t da, int64_t ca, uint32_t b, int64_t db, int64_t cb)
{
    pay_posting_req_t r;
    pay_receipt_t rc;
    memset(&r, 0, sizeof r);
    nfund++;
    snprintf(r.uetr, sizeof r.uetr, "00000000-0000-4000-8000-%012llx", (unsigned long long) nfund);
    snprintf(r.e2e, sizeof r.e2e, "FUND-%llu", (unsigned long long) nfund);
    memcpy(r.idem_key, &nfund, sizeof nfund);
    r.idem_key[31] = 0xEE;
    r.lines[0] = (pay_line_t){a, da, ca};
    r.lines[1] = (pay_line_t){b, db, cb};
    r.n_lines = 2;
    return pay_ledger_post(&L, &r, &rc);
}
static void fund(uint32_t to, uint64_t amt) /* Vino issued */
{
    raw(to, (int64_t) amt, 0, viss, 0, (int64_t) amt);
}
static void vbi(uint32_t to, uint64_t amt) /* V-Bills issued */
{
    raw(to, (int64_t) amt, 0, biss, 0, (int64_t) amt);
}
static uint32_t open_acct(uint16_t as, uint32_t owner, uint8_t flags)
{
    uint32_t a = 0;
    pay_ledger_open(&L, owner, as, PAY_CAP_FINANCIAL, flags, &a);
    return a;
}

static vauc_config_t auc_cfg(void)
{
    vauc_config_t c;
    memset(&c, 0, sizeof c);
    c.floor_num = 1;
    c.floor_den = 1;
    c.backing = 1;
    c.cap = 100;
    c.p_start = 200;
    c.p_step = 10;
    c.p_min = 100;
    c.max_age = 10;
    c.min_quotes = 3;
    return c;
}

static grant_recipient_t rcp(uint32_t i)
{
    grant_recipient_t r;
    memset(&r, 0, sizeof r);
    r.m.subject = 300 + i;
    r.m.uptime_bps = GRANT_DEF_UPTIME_BPS;
    r.m.volume = GRANT_DEF_MIN_VOLUME;
    r.m.stake = GRANT_DEF_MIN_STAKE;
    r.account = brl[i];
    r.weight = 1;
    return r;
}

/* a V-Bill-only chip of `chips` chips (kappa 1) */
static zenny_status_t chip(uint64_t chips, uint64_t nonce)
{
    zenny_req_t r;
    uint8_t d[32], m[ZENNY_SIG_MSG], sig[32];
    memset(&r, 0, sizeof r);
    r.minter = 1;
    r.kappa = 1;
    r.nonce = nonce;
    r.n_legs = 1;
    r.leg[0] = (zenny_leg_t){ZENNY_VBILL, 0, 1, chips, 1};
    r.price[0] = (zenny_price_t){1, 1};
    r.tick = 5;
    zenny_mint_digest(&r, d);
    zenny_sign_msg(ZENNY_SIG_MINT, d, m);
    sign(1, m, sizeof m, sig);
    r.sig_minter = sig;
    r.sig_minter_len = 32;
    uint32_t slot;
    return zenny_mint(&Z, &r, &slot);
}

/* Lock layout. MEDIUM: users 0..m-1 lock 100+i. DEEP: users 6.. lock deep[]
 * (nd of them). Bonds: k=13 users 0..4 qty 10; k=21 users 5..9 qty 20. */
static void world(uint32_t m, const uint64_t *deep, uint32_t nd, uint32_t chips)
{
    static const uint8_t seed[32] = {9, 8, 7}, gseed[32] = {6, 5, 4};
    nfund = 0;
    pay_ledger_init(&L, NULL);
    pay_ledger_add_asset(&L, "VNO", 2, PAY_ASSET_UNIT, &vno);
    pay_ledger_add_asset(&L, "VBL", 0, PAY_ASSET_UNIT, &vbl);
    viss = open_acct(vno, 1, PAY_ACCT_ISSUER);
    rsv = open_acct(vno, 2, 0);
    vcash = open_acct(vno, 3, 0);
    biss = open_acct(vbl, 5, PAY_ACCT_ISSUER);
    vvb = open_acct(vbl, 6, 0);
    lockM = open_acct(vbl, 7, 0);
    lockD = open_acct(vbl, 8, 0);
    block = open_acct(vbl, 9, 0);
    ars = open_acct(vno, 10, 0);
    aes = open_acct(vno, 11, 0);
    raf = open_acct(vno, 12, 0);
    for (int i = 0; i < NU; i++) {
        uv[i] = open_acct(vno, OWN(i), 0);
        ub[i] = open_acct(vbl, OWN(i), 0);
        fund(uv[i], 1000000);
        vbi(ub[i], 1000000);
    }
    fund(rsv, 500000);
    fund(ars, 20000000);
    fund(raf, 100000);

    CHECK(svault_init(&V, &L, rsv, vcash, biss, vvb) == SVAULT_OK, "vault set up");
    vbi(vvb, 300);

    vswap_cfg_t c;
    vswap_cfg_default(&c);
    c.vino = vno;
    c.vbill = vbl;
    c.reserve = rsv;
    c.issuer = biss;
    c.vault = vvb;
    c.lock[VSWAP_TIER_MEDIUM] = lockM;
    c.lock[VSWAP_TIER_DEEP] = lockD;
    c.gateway = GATE;
    c.juris = JURIS;
    c.verify = verify;
    c.seed[0] = 0x51;
    CHECK(vswap_init(&S, &L, &c) == VSWAP_OK, "swap set up");
    uint8_t msg[VSWAP_SWITCH_MSG], sig[32];
    vswap_switch_msg(JURIS, true, 1, msg);
    sign(GATE, msg, sizeof msg, sig);
    CHECK(vswap_enable(&S, JURIS, true, 1, sig, 32) == VSWAP_OK, "swap enabled");
    bool ok = true;
    for (uint32_t i = 0; i < m; i++)
        ok &= vswap_lock(&S, ub[i], uv[i], VSWAP_TIER_MEDIUM, 100 + i, false, 10, NULL) == VSWAP_OK;
    for (uint32_t i = 0; i < nd; i++)
        ok &= vswap_lock(&S, ub[6 + i], uv[6 + i], VSWAP_TIER_DEEP, deep[i], false, 10, NULL) ==
              VSWAP_OK;
    CHECK(ok, "positions locked");

    vbond_cfg_t bc;
    vbond_cfg_default(&bc);
    bc.vino = vno;
    bc.vbill = vbl;
    bc.lock = block;
    bc.gateway = GATE;
    bc.juris = JURIS;
    bc.verify = verify;
    bc.seed[0] = 0xB0;
    CHECK(vbond_init(&B, &L, &bc) == VBOND_OK, "bonds set up");
    vbond_switch_msg(JURIS, true, 1, msg);
    sign(GATE, msg, sizeof msg, sig);
    CHECK(vbond_enable(&B, JURIS, true, 1, sig, 32) == VBOND_OK, "bonds enabled");
    ok = true;
    for (uint32_t i = 0; i < 5; i++)
        ok &= vbond_issue(&B, OWN(i), ub[i], uv[i], 10, 13, 10, NULL) == VBOND_OK;
    for (uint32_t i = 5; i < 10; i++)
        ok &= vbond_issue(&B, OWN(i), ub[i], uv[i], 20, 21, 10, NULL) == VBOND_OK;
    CHECK(ok, "bonds issued");

    vauc_config_t ac = auc_cfg();
    CHECK(vauc_init(&A, &L, &ac, ars, aes, biss, verify, NULL, seed) == VAUC_OK, "auction set up");
    CHECK(vauc_open(&A) == VAUC_OK && vauc_bid(&A, uv[11], ub[11], 10, 200, 11, NULL) == VAUC_OK &&
              vauc_clear(&A, 12) == VAUC_OK && vauc_settle(&A, 12) == VAUC_OK,
          "one auction epoch settled");

    CHECK(grant_init(&G, &L, vno, 400, 401, verify, NULL, gseed) == GRANT_OK &&
              grant_lot_open(&G, 1) == GRANT_OK && grant_epoch_begin(&G, 1) == GRANT_OK,
          "grants set up");
    for (int i = 0; i < NU; i++) brl[i] = open_acct(G.barrel, OWN(i), 0);

    zenny_init(&Z, verify, NULL);
    ok = true;
    for (uint32_t i = 0; i < chips; i++) ok &= chip(10 * (i + 1), i) == ZENNY_OK;
    CHECK(ok, "chips minted");
}

static void grants_run(void)
{
    grant_recipient_t r[3] = {rcp(0), rcp(1), rcp(2)};
    CHECK(grant_pool_credit(&G, raf, 500, 13) == GRANT_OK &&
              grant_epoch_distribute(&G, 1, r, 3, 100, 13, NULL) == GRANT_OK,
          "grant epoch distributed");
}

static const zstats_raffle_t RAF = {7, 5000, 5400, 10000, 12, ZSTATS_RAFFLE_PAID};

static zstats_sources_t all(void)
{
    zstats_sources_t s;
    zstats_sources_default(&s);
    s.L = &L;
    s.reserve_acct = rsv;
    s.claims_asset = vno;
    s.vault = &V;
    s.swap = &S;
    s.bond = &B;
    s.auction = &A;
    s.grants = &G;
    s.zenny = &Z;
    s.raffle = &RAF;
    s.juris = JURIS;
    s.publisher = PUB;
    return s;
}

static const uint64_t DEEP2[2] = {50, 50};

/* ---- S1/S2: hand-computed figures ---- */
static void test_collect(void)
{
    world(6, DEEP2, 2, 5);
    grants_run();
    zstats_sources_t src = all();
    zstats_snapshot_t s;
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_OK, "collect");
    CHECK(s.epoch == 1 && s.tick == 50 && s.juris == JURIS && s.publisher == PUB &&
              s.min_cohort == 5 && s.present == ZSTATS_SEC_ALL,
          "header and every section present");
    /* Vino issued: 12 * 1e6 users + 500000 reserve + 2e7 auction + 100000 raffle */
    CHECK(s.reserve.reserve == 500000 && s.reserve.claims == 32600000 &&
              s.reserve.floor.num == 500000 && s.reserve.floor.den == 32600000 &&
              s.reserve.floor.defined && s.reserve.req_num == 1 && s.reserve.req_den == 1 &&
              !s.reserve.floor_ok,
          "reserve 500000 against 32600000 claims: below a 1/1 floor");
    /* V-Bills: 12 * 1e6 users + 300 vault + 10 auctioned */
    CHECK(s.vault.held == 300 && s.vault.outstanding == 12000310 && s.vault.share.num == 300 &&
              s.vault.share.den == 12000310 && s.vault.share.defined,
          "vault share 300 / 12000310");
    CHECK(s.comp.circulating_vino == 32100000 && s.comp.vbills_outstanding == 12000310,
          "circulating Vino excludes the swap reserve");
    /* tiers: medium 100..105 = 615 (6 positions), deep 2 x 50; bonds 50 + 100 */
    CHECK(s.comp.vbills_locked == 865 && s.comp.vbills_unlocked == 12000310 - 865,
          "locked = tiers + bonds, unlocked the rest");
    CHECK(s.comp.tier[0].count == 6 && s.comp.tier[0].sum == 615 && s.comp.tier[1].count == 0 &&
              s.comp.tier[1].sum == 0,
          "medium published, deep (2 positions) suppressed");
    CHECK(s.bonds.n_terms == 3 && s.bonds.k[0] == 13 && s.bonds.k[1] == 17 && s.bonds.k[2] == 21 &&
              s.bonds.term[0].count == 5 && s.bonds.term[0].sum == 50 &&
              s.bonds.term[1].count == 0 && s.bonds.term[2].count == 5 &&
              s.bonds.term[2].sum == 100,
          "bond terms 13 / 17 / 21");
    CHECK(s.suppressed == (ZSTATS_SUP_TIER(1) | ZSTATS_SUP_TERM(1)),
          "the deep tier and the empty k=17 term are suppressed");
    CHECK(s.auction.has_last && s.auction.last_epoch == 1 && s.auction.price == 100 &&
              s.auction.qty == 10 && s.auction.outstanding == 12000310 &&
              s.auction.reserve == 20001000 && s.auction.reserve_valid &&
              s.auction.floor_num == 1 && s.auction.floor_den == 1 && s.auction.backing == 1 &&
              s.auction.floor_ok,
          "auction: 10 at 100, reserve 20001000, floor holds");
    CHECK(!memcmp(&s.raffle, &RAF, sizeof RAF), "raffle copied as given");
    CHECK(s.barrels.epoch == 1 && s.barrels.inflow == 500 && s.barrels.weight == 3 &&
              s.barrels.rate_num == 500 && s.barrels.rate_den == 3 && s.barrels.rate_defined &&
              s.barrels.granted_barrels == 5 && s.barrels.granted_cost == 500 &&
              s.barrels.pool_balance == 0 && s.barrels.reserve_balance == 500 &&
              s.barrels.barrels_outstanding == 5 && s.barrels.barrels_unbacked == 5 &&
              s.barrels.barrels_backed == 0 && s.barrels.lots == 1,
          "barrels: G_rate 500/3, 5 granted, unbacked");
    CHECK(s.zenny.live == 5 && s.zenny.chips == 150, "five chips, 150 outstanding");
    CHECK(zstats_check(&s) == ZSTATS_OK, "the collected snapshot is consistent");

    /* the required floor at the exact boundary: 500000 * 326 == 32600000 * 5 */
    src.req_num = 5;
    src.req_den = 326;
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_OK && s.reserve.floor_ok, "floor met exactly");
    src.req_num = 6;
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_OK && !s.reserve.floor_ok,
          "one step above is not met");
    src.req_den = 0;
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_ERR_ARG && s.present == 0 && s.epoch == 0,
          "required den 0 refused, output cleared");
    src = all();
    src.reserve_acct = 9999;
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_ERR_ARG, "unknown reserve account");
    src = all();
    src.claims_asset = 40;
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_ERR_ARG, "unknown claims asset");
    src = all();
    src.min_cohort = 0;
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_ERR_ARG, "min_cohort 0 refused");
    CHECK(zstats_collect(&s, 1, 50, NULL) == ZSTATS_ERR_ARG &&
              zstats_collect(NULL, 1, 50, &src) == ZSTATS_ERR_ARG,
          "null arguments");
    zstats_sources_default(NULL);
}

/* ---- the module never changes its sources ---- */
static pay_ledger_t L0;
static svault_t V0;
static vswap_t S0;
static vbond_t B0;
static vauc_t A0;
static grant_t G0;
static zenny_t Z0;
static void test_readonly(void)
{
    world(6, DEEP2, 2, 5);
    grants_run();
    L0 = L;
    V0 = V;
    S0 = S;
    B0 = B;
    A0 = A;
    G0 = G;
    Z0 = Z;
    uint8_t h0[32], h1[32];
    sha256((const uint8_t *) &L, sizeof L, h0);
    zstats_sources_t src = all();
    zstats_snapshot_t s;
    uint8_t enc[ZSTATS_ENC_LEN];
    uint32_t n;
    char js[4096];
    for (int i = 0; i < 3; i++) {
        zstats_collect(&s, 1 + (uint64_t) i, 50, &src);
        zstats_encode(&s, NULL, enc, sizeof enc, &n);
        zstats_json(&s, js, sizeof js, &n);
    }
    sha256((const uint8_t *) &L, sizeof L, h1);
    CHECK(!memcmp(h0, h1, 32) && !memcmp(&L, &L0, sizeof L) && pay_ledger_check(&L) &&
              pay_ledger_verify_chain(&L),
          "the ledger is byte-for-byte unchanged");
    CHECK(!memcmp(&V, &V0, sizeof V) && !memcmp(&S, &S0, sizeof S) && !memcmp(&B, &B0, sizeof B) &&
              !memcmp(&A, &A0, sizeof A) && !memcmp(&G, &G0, sizeof G) &&
              !memcmp(&Z, &Z0, sizeof Z),
          "every module is unchanged");
}

/* ---- absent sections ---- */
static void test_absent(void)
{
    world(6, DEEP2, 2, 5);
    zstats_sources_t src;
    zstats_snapshot_t s;
    zstats_sources_default(&src);
    CHECK(src.reserve_acct == ZSTATS_NONE && src.req_num == 1 && src.req_den == 1 &&
              src.min_cohort == 5 && !src.L && !src.swap && !src.raffle,
          "defaults");
    CHECK(zstats_collect(&s, 2, 3, &src) == ZSTATS_OK && s.present == 0 && s.suppressed == 0,
          "nothing given: nothing present");
    char buf[1024];
    uint32_t n;
    static const char want[] =
        "{\"format\":\"zxv/stats/v1\",\"epoch\":\"2\",\"tick\":\"3\",\"juris\":\"0\","
        "\"publisher\":\"0\",\"min_cohort\":\"5\",\"reserve\":null,\"vault\":null,"
        "\"compression\":null,\"bonds\":null,\"auction\":null,\"raffle\":null,\"barrels\":null,"
        "\"zenny\":null}";
    CHECK(zstats_json(&s, buf, sizeof buf, &n) == ZSTATS_OK && n == sizeof want - 1 &&
              !memcmp(buf, want, n),
          "absent sections render as null");

    /* the ledger alone still gives nothing without the per-section source */
    src.L = &L;
    CHECK(zstats_collect(&s, 2, 3, &src) == ZSTATS_OK && s.present == 0, "ledger alone");
    struct {
        uint32_t bit;
        int which;
    } one[] = {{ZSTATS_SEC_RESERVE, 0}, {ZSTATS_SEC_VAULT, 1},   {ZSTATS_SEC_COMPRESSION, 2},
               {ZSTATS_SEC_BONDS, 3},   {ZSTATS_SEC_AUCTION, 4}, {ZSTATS_SEC_RAFFLE, 5},
               {ZSTATS_SEC_BARRELS, 6}, {ZSTATS_SEC_ZENNY, 7}};
    for (uint32_t i = 0; i < 8; i++) {
        zstats_sources_default(&src);
        src.L = &L;
        switch (one[i].which) {
        case 0:
            src.reserve_acct = rsv;
            src.claims_asset = vno;
            break;
        case 1:
            src.vault = &V;
            break;
        case 2:
            src.swap = &S;
            break;
        case 3:
            src.bond = &B;
            break;
        case 4:
            src.auction = &A;
            break;
        case 5:
            src.raffle = &RAF;
            break;
        case 6:
            src.grants = &G;
            break;
        default:
            src.zenny = &Z;
            break;
        }
        CHECK(zstats_collect(&s, 2, 3, &src) == ZSTATS_OK && s.present == one[i].bit,
              "exactly one section present");
    }
    /* sections needing the ledger are absent without it */
    src = all();
    src.L = NULL;
    CHECK(zstats_collect(&s, 2, 3, &src) == ZSTATS_OK &&
              s.present == (ZSTATS_SEC_ALL &
                            ~(ZSTATS_SEC_RESERVE | ZSTATS_SEC_VAULT | ZSTATS_SEC_COMPRESSION)),
          "no ledger: no reserve, vault or compression");
    /* a vault not set up is absent */
    svault_t v2;
    memset(&v2, 0, sizeof v2);
    src = all();
    src.vault = &v2;
    CHECK(zstats_collect(&s, 2, 3, &src) == ZSTATS_OK && !(s.present & ZSTATS_SEC_VAULT) &&
              s.vault.outstanding == 0,
          "vault not ready: absent");
    /* without the bonds, bonded V-Bills count as unlocked */
    src = all();
    src.bond = NULL;
    CHECK(zstats_collect(&s, 2, 3, &src) == ZSTATS_OK && s.comp.vbills_locked == 715 &&
              s.comp.vbills_unlocked == s.comp.vbills_outstanding - 715 && s.bonds.n_terms == 0,
          "no bond section: locked is the tiers only");
}

/* ---- G4 G_rate ---- */
static void test_grate(void)
{
    world(6, DEEP2, 2, 5);
    zstats_sources_t src = all();
    zstats_snapshot_t s;
    CHECK(grant_pool_credit(&G, raf, 700, 13) == GRANT_OK, "inflow without a distribution");
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_OK && s.barrels.inflow == 700 &&
              s.barrels.weight == 0 && !s.barrels.rate_defined && s.barrels.rate_num == 0 &&
              s.barrels.rate_den == 0,
          "weight 0: G_rate 0/0 undefined");
    char buf[4096];
    uint32_t n;
    CHECK(zstats_json(&s, buf, sizeof buf, &n) == ZSTATS_OK && n < sizeof buf, "rendered");
    buf[n] = 0;
    CHECK(strstr(buf, "\"weight\":\"0\",\"g_rate\":null,") != NULL,
          "an undefined G_rate renders as null");
    grant_recipient_t r[2] = {rcp(0), rcp(1)};
    CHECK(grant_epoch_distribute(&G, 1, r, 2, 100, 13, NULL) == GRANT_OK, "distributed");
    CHECK(zstats_collect(&s, 2, 50, &src) == ZSTATS_OK && s.barrels.rate_defined &&
              s.barrels.rate_num == 700 && s.barrels.rate_den == 2,
          "G_rate 700/2 kept unreduced");
    CHECK(zstats_json(&s, buf, sizeof buf, &n) == ZSTATS_OK, "rendered");
    buf[n] = 0;
    CHECK(strstr(buf, "\"g_rate\":{\"num\":\"700\",\"den\":\"2\"}") != NULL,
          "a defined G_rate renders as num/den");
}

/* ---- S3 privacy ---- */
static void test_privacy(void)
{
    zstats_sources_t src;
    zstats_snapshot_t s;
    /* medium with 5 positions (= min_cohort) and with 4 (= min_cohort - 1) */
    world(5, DEEP2, 2, 5);
    src = all();
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_OK && s.comp.tier[0].count == 5 &&
              s.comp.tier[0].sum == 510 && !(s.suppressed & ZSTATS_SUP_TIER(0)),
          "5 positions: published");
    src.min_cohort = 6;
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_OK && s.comp.tier[0].count == 0 &&
              s.comp.tier[0].sum == 0 && (s.suppressed & ZSTATS_SUP_TIER(0)) && s.min_cohort == 6,
          "the same 5 positions under a floor of 6: suppressed");
    world(4, DEEP2, 2, 5);
    src = all();
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_OK && s.comp.tier[0].count == 0 &&
              s.comp.tier[0].sum == 0 && (s.suppressed & ZSTATS_SUP_TIER(0)),
          "4 positions: suppressed");
    CHECK(s.comp.vbills_locked == 100 + 101 + 102 + 103 + 100 + 150,
          "the total still holds every locked V-Bill");
    src.min_cohort = 1;
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_OK &&
              s.suppressed == (ZSTATS_SUP_TERM(1) | ZSTATS_SUP_TIER(1)) &&
              s.comp.tier[0].count == 4 && s.comp.tier[0].sum == 406,
          "a floor of 1: the empty k=17 term, and deep (2, the smallest) as its complement");

    /* a single large holder in the deep tier, every other group published */
    static const uint64_t whale[1] = {777777};
    world(6, whale, 1, 5);
    /* fill the empty k=17 term so only the whale's group is small */
    bool ok = true;
    for (uint32_t i = 0; i < 5; i++)
        ok &= vbond_issue(&B, OWN(i), ub[i], uv[i], 7, 17, 11, NULL) == VBOND_OK;
    CHECK(ok, "k=17 term filled");
    src = all();
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_OK, "collect with a whale");
    uint64_t pub = 0;
    for (int i = 0; i < 2; i++) pub += s.comp.tier[i].sum;
    for (int i = 0; i < 3; i++) pub += s.bonds.term[i].sum;
    CHECK(s.comp.tier[1].count == 0 && s.comp.tier[1].sum == 0 &&
              (s.suppressed & ZSTATS_SUP_TIER(1)),
          "the whale's tier is suppressed");
    CHECK(s.comp.vbills_locked == 615 + 777777 + 50 + 35 + 100, "the total stays exact");
    CHECK(s.suppressed == (ZSTATS_SUP_TIER(1) | ZSTATS_SUP_TERM(0)) && s.bonds.term[0].count == 0 &&
              s.bonds.term[0].sum == 0 && s.bonds.term[1].count == 5 && s.bonds.term[1].sum == 35,
          "complementary suppression: the smallest other group (k=13 and k=17 tie at 5: k=13)");
    CHECK(s.comp.vbills_locked - pub == 777777 + 50 && s.comp.vbills_locked - pub != 777777,
          "total minus the published groups does not give the whale back");
    uint8_t enc[ZSTATS_ENC_LEN];
    uint32_t n;
    char js[4096];
    CHECK(zstats_encode(&s, NULL, enc, sizeof enc, &n) == ZSTATS_OK, "encoded");
    bool found = false;
    for (uint32_t i = 0; i + 8 <= n; i++) {
        uint64_t v = 0;
        for (int b = 0; b < 8; b++) v |= (uint64_t) enc[i + b] << (8 * b);
        if (v == 777777) found = true;
    }
    CHECK(!found, "the whale's amount appears nowhere in the encoding");
    CHECK(zstats_json(&s, js, sizeof js - 1, &n) == ZSTATS_OK, "rendered");
    js[n] = 0;
    CHECK(!strstr(js, "777777"), "nor in the JSON");

    /* tie on the complementary pick goes to the lower index: medium and
     * k=13 and k=21 have 5 or 6; make medium 5 positions = term k13 (5) */
    world(5, whale, 1, 5);
    for (uint32_t i = 0; i < 6; i++)
        vbond_issue(&B, OWN(i), ub[i], uv[i], 7, 17, 11, NULL); /* k=17: 6 bonds */
    for (uint32_t i = 0; i < 1; i++) vbond_issue(&B, OWN(i), ub[i], uv[i], 9, 21, 11, NULL);
    /* counts: medium 5, deep 1, k13 5, k17 6, k21 6 */
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_OK &&
              s.suppressed == (ZSTATS_SUP_TIER(0) | ZSTATS_SUP_TIER(1)),
          "tie between medium (5) and k=13 (5): the lower index, medium");
    /* the smallest is strictly preferred: k=13 has 5, medium 6 */
    world(6, whale, 1, 5);
    for (uint32_t i = 0; i < 6; i++) vbond_issue(&B, OWN(i), ub[i], uv[i], 7, 17, 11, NULL);
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_OK &&
              s.suppressed == (ZSTATS_SUP_TIER(1) | ZSTATS_SUP_TERM(0)),
          "the smallest other group (k=13, 5) rather than medium (6)");

    /* Zenny: 4 chips suppressed, 5 published */
    world(6, DEEP2, 2, 4);
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_OK && s.zenny.live == 0 && s.zenny.chips == 0 &&
              (s.suppressed & ZSTATS_SUP_ZENNY),
          "4 chips: suppressed");
    CHECK(chip(1, 99) == ZENNY_OK && zstats_collect(&s, 1, 50, &src) == ZSTATS_OK &&
              s.zenny.live == 5 && s.zenny.chips == 101 && !(s.suppressed & ZSTATS_SUP_ZENNY),
          "5 chips: published");

    /* a family of one group needs no complement; a family of two does */
    world(6, DEEP2, 2, 5);
    src = all();
    src.swap = NULL;
    static vbond_t b1;
    b1 = B;
    b1.cfg.n_terms = 1; /* k=13 only (5 bonds) */
    for (uint32_t i = 0; i < VBOND_MAX_BONDS; i++)
        if (b1.bond[i].used && b1.bond[i].term != 0) b1.bond[i].used = false;
    src.bond = &b1;
    src.min_cohort = 6;
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_OK &&
              s.suppressed == (ZSTATS_SUP_TERM(0) | ZSTATS_SUP_ZENNY) &&
              s.present == (ZSTATS_SEC_ALL & ~ZSTATS_SEC_COMPRESSION),
          "a one-group family: its group alone is suppressed");
    b1.cfg.n_terms = 2; /* k=13 (5 bonds), k=17 (none) */
    src.min_cohort = 5;
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_OK &&
              s.suppressed == (ZSTATS_SUP_TERM(0) | ZSTATS_SUP_TERM(1)),
          "a two-group family: the complement is suppressed too");
}

/* ---- the source sanity paths ---- */
static void test_bad_sources(void)
{
    world(6, DEEP2, 2, 5);
    zstats_sources_t src = all();
    zstats_snapshot_t s;
    static vswap_t s2;
    s2 = S;
    s2.pos[0].vbills = s2.pos[0].vbills + 20000000; /* locked above outstanding */
    src.swap = &s2;
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_ERR_STATE && s.present == 0,
          "locked above outstanding");
    s2 = S;
    s2.pos[0].vbills = UINT64_MAX;
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_ERR_OVERFLOW, "a tier sum overflows");
    s2 = S;
    s2.pos[0].tier = VSWAP_TIER_LOW;
    s2.pos[1].tier = 3;
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_OK && s.comp.tier[0].count == 0 &&
              s.comp.vbills_locked == 865 - 100 - 101 && (s.suppressed & ZSTATS_SUP_TIER(0)),
          "positions outside MEDIUM / DEEP are not counted");
    s2 = S;
    s2.cfg.issuer = 9999;
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_ERR_STATE, "swap issuer unknown");
    s2 = S;
    s2.cfg.reserve = 9999;
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_ERR_STATE, "swap reserve unknown");
    s2 = S;
    s2.cfg.reserve = ub[0]; /* a V-Bill account: its DEBIT exceeds the Vino total */
    s2.cfg.vino = vvb == 0 ? 0 : vbl;
    s2.cfg.vino = 40; /* no such asset: total 0 */
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_ERR_STATE, "circulating would go negative");

    src = all();
    static vbond_t b2;
    b2 = B;
    b2.cfg.n_terms = ZSTATS_TERMS + 1;
    src.bond = &b2;
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_ERR_STATE, "too many terms");
    b2 = B;
    b2.bond[0].term = 3;
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_ERR_STATE, "a bond outside the terms");
    b2 = B;
    b2.bond[0].qty = UINT64_MAX;
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_ERR_OVERFLOW, "a term sum overflows");
    b2 = B;
    b2.bond[0].qty = UINT64_MAX - 100;
    b2.bond[5].qty = 200; /* the term sums fit, the bond total does not */
    b2.bond[5].term = 1;
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_ERR_OVERFLOW, "the bond total overflows");
    b2 = B;
    b2.bond[0].qty = UINT64_MAX - 100; /* bonds fit, bonds + tiers do not */
    for (uint32_t i = 1; i < VBOND_MAX_BONDS; i++) b2.bond[i].used = false;
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_ERR_OVERFLOW, "locked overflows");

    src = all();
    static zenny_t z2;
    z2 = Z;
    z2.chip[0].ver[z2.chip[0].n_ver - 1].chips = UINT64_MAX;
    src.zenny = &z2;
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_ERR_OVERFLOW, "chip total overflows");

    /* the vault: h above N is a contradiction; h == N == 0 is an undefined share */
    src = all();
    static svault_t v3;
    v3 = V;
    v3.vbill_issuer = G.issuer; /* 0 barrels outstanding here */
    v3.vbills = ub[11];         /* 1e6 - 10*200 locked... a large DEBIT */
    src.vault = &v3;
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_ERR_STATE, "vault holds above outstanding");
    v3.vbill_issuer = vcash; /* CREDIT 0 */
    v3.vbills = vcash;       /* DEBIT 0 */
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_OK && (s.present & ZSTATS_SEC_VAULT) &&
              s.vault.held == 0 && s.vault.outstanding == 0 && !s.vault.share.defined &&
              s.vault.share.num == 0 && s.vault.share.den == 0,
          "h == N == 0: present, share undefined 0/0");
    /* no claims: an undefined floor */
    src = all();
    src.claims_asset = G.barrel; /* barrels exist only after a distribution */
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_OK && s.reserve.claims == 0 &&
              !s.reserve.floor.defined && s.reserve.floor.num == 0 && s.reserve.floor.den == 0 &&
              s.reserve.floor_ok,
          "no claims: floor 0/0 undefined and met");

    /* a bond term far outside the table is refused before it is touched */
    src = all();
    b2 = B;
    b2.bond[0].term = 200;
    src.bond = &b2;
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_ERR_STATE, "a wild term index");
    b2 = B;
    b2.cfg.n_terms = ZSTATS_TERMS;
    for (uint32_t i = 3; i < ZSTATS_TERMS; i++) b2.cfg.k[i] = 30 + i;
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_OK && s.bonds.n_terms == ZSTATS_TERMS &&
              s.bonds.k[7] == 37,
          "a full table of terms is accepted");

    /* the auction history: the newest record wherever it sits in the ring */
    src = all();
    static vauc_t a3;
    a3 = A;
    memset(a3.hist, 0, sizeof a3.hist);
    src.auction = &a3;
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_OK && !s.auction.has_last &&
              s.auction.last_epoch == 0 && s.auction.price == 0,
          "no finished epoch: no last");
    a3.hist[0] = A.hist[1];
    a3.hist[0].epoch = 8;
    a3.hist[0].price = 123;
    a3.hist[3] = A.hist[1];
    a3.hist[3].epoch = 3;
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_OK && s.auction.has_last &&
              s.auction.last_epoch == 8 && s.auction.price == 123,
          "the newest record in slot 0");
    a3.hist[5] = a3.hist[0];
    a3.hist[5].price = 456;
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_OK && s.auction.price == 123,
          "equal epochs: the first slot");
    /* a reserve that cannot be valued: two receipts of 2^63 each */
    a3 = A;
    a3.feed[0].used = true;
    a3.feed[0].commodity = 1;
    a3.feed[0].scale = 1;
    for (int i = 0; i < 3; i++) {
        a3.feed[0].has[i] = true;
        a3.feed[0].price[i] = (uint64_t) 1 << 62;
        a3.feed[0].tick[i] = 50;
    }
    a3.cust[0].used = true;
    a3.cust[0].id = 9;
    a3.cust[0].haircut_bps = 0;
    for (int i = 0; i < 2; i++) {
        a3.rcpt[i].used = true;
        a3.rcpt[i].id[0] = (uint8_t) (i + 1);
        a3.rcpt[i].custodian = 9;
        a3.rcpt[i].commodity = 1;
        a3.rcpt[i].qty = 2;
        a3.rcpt[i].expiry = 1000;
    }
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_OK && !s.auction.reserve_valid &&
              s.auction.reserve == 0 && !s.auction.floor_ok,
          "an unvaluable reserve: invalid, zero, floor not ok");

    /* an inconsistent source fails the final check: grants backed+unbacked */
    src = all();
    static vauc_t a2;
    a2 = A;
    a2.cfg.floor_num = UINT64_MAX; /* k overflows: the check refuses */
    a2.cfg.backing = 2;
    src.auction = &a2;
    CHECK(zstats_collect(&s, 1, 50, &src) == ZSTATS_ERR_STATE && s.present == 0,
          "a snapshot failing zstats_check is refused");
}

/* ---- a fixed snapshot ---- */
static zstats_snapshot_t fixed(void)
{
    zstats_snapshot_t s;
    memset(&s, 0, sizeof s);
    s.epoch = 3;
    s.tick = UINT64_MAX;
    s.juris = 4000000000u;
    s.publisher = PUB;
    s.min_cohort = 5;
    s.present = ZSTATS_SEC_ALL;
    s.suppressed = ZSTATS_SUP_TIER(1) | ZSTATS_SUP_TERM(1) | ZSTATS_SUP_TERM(2);
    s.reserve.reserve = 500;
    s.reserve.claims = 1000;
    s.reserve.floor = (zstats_rat_t){500, 1000, true};
    s.reserve.req_num = 1;
    s.reserve.req_den = 2;
    s.reserve.floor_ok = true;
    s.vault.held = 30;
    s.vault.outstanding = 300;
    s.vault.share = (zstats_rat_t){30, 300, true};
    s.comp.circulating_vino = 9000;
    s.comp.vbills_outstanding = 300;
    s.comp.vbills_unlocked = 150;
    s.comp.vbills_locked = 150;
    s.comp.tier[0] = (zstats_group_t){5, 70};
    s.bonds.n_terms = 3;
    s.bonds.k[0] = 13;
    s.bonds.k[1] = 17;
    s.bonds.k[2] = 21;
    s.bonds.term[0] = (zstats_group_t){6, 50};
    s.auction.has_last = true;
    s.auction.last_epoch = 2;
    s.auction.price = 100;
    s.auction.qty = 10;
    s.auction.outstanding = 300;
    s.auction.reserve = 1000;
    s.auction.reserve_valid = true;
    s.auction.floor_num = 1;
    s.auction.floor_den = 1;
    s.auction.backing = 1;
    s.auction.floor_ok = true;
    s.raffle = RAF;
    grant_snapshot_t *b = &s.barrels;
    b->epoch = 1;
    b->inflow = 500;
    b->weight = 3;
    b->rate_num = 500;
    b->rate_den = 3;
    b->rate_defined = true;
    b->granted_barrels = 5;
    b->granted_cost = 500;
    b->total_barrels_granted = 5;
    b->reserve_balance = 500;
    b->barrels_outstanding = 5;
    b->barrels_unbacked = 5;
    b->lots = 1;
    s.zenny.live = 5;
    s.zenny.chips = 150;
    return s;
}

static const char WANT_JSON[] =
    "{\"format\":\"zxv/stats/v1\",\"epoch\":\"3\",\"tick\":\"18446744073709551615\","
    "\"juris\":\"4000000000\",\"publisher\":\"42\",\"min_cohort\":\"5\","
    "\"reserve\":{\"reserve_vino\":\"500\",\"claims_vino\":\"1000\","
    "\"floor\":{\"num\":\"500\",\"den\":\"1000\"},\"required\":{\"num\":\"1\",\"den\":\"2\"},"
    "\"floor_ok\":true},"
    "\"vault\":{\"vbills_held\":\"30\",\"vbills_outstanding\":\"300\","
    "\"share\":{\"num\":\"30\",\"den\":\"300\"}},"
    "\"compression\":{\"circulating_vino\":\"9000\",\"vbills_outstanding\":\"300\","
    "\"vbills_unlocked\":\"150\",\"vbills_locked\":\"150\",\"tiers\":["
    "{\"tier\":\"medium\",\"positions\":\"5\",\"vbills\":\"70\",\"suppressed\":false},"
    "{\"tier\":\"deep\",\"positions\":\"0\",\"vbills\":\"0\",\"suppressed\":true}]},"
    "\"bonds\":{\"terms\":["
    "{\"k\":\"13\",\"bonds\":\"6\",\"vbills\":\"50\",\"suppressed\":false},"
    "{\"k\":\"17\",\"bonds\":\"0\",\"vbills\":\"0\",\"suppressed\":true},"
    "{\"k\":\"21\",\"bonds\":\"0\",\"vbills\":\"0\",\"suppressed\":true}]},"
    "\"auction\":{\"last\":{\"epoch\":\"2\",\"price\":\"100\",\"qty\":\"10\"},"
    "\"vbills_outstanding\":\"300\",\"reserve\":\"1000\",\"floor\":{\"num\":\"1\",\"den\":\"1\"},"
    "\"backing\":\"1\",\"floor_ok\":true},"
    "\"raffle\":{\"epoch\":\"7\",\"pot\":\"5000\",\"tickets\":\"5400\",\"threshold\":\"10000\","
    "\"entries\":\"12\",\"mode\":\"paid\"},"
    "\"barrels\":{\"epoch\":\"1\",\"inflow\":\"500\",\"weight\":\"3\","
    "\"g_rate\":{\"num\":\"500\",\"den\":\"3\"},\"granted_barrels\":\"5\","
    "\"granted_cost\":\"500\",\"total_barrels_granted\":\"5\",\"pool_balance\":\"0\","
    "\"reserve_balance\":\"500\",\"barrels_outstanding\":\"5\",\"barrels_backed\":\"0\","
    "\"barrels_unbacked\":\"5\",\"lots\":\"1\",\"lots_backed\":\"0\"},"
    "\"zenny\":{\"live_chips\":\"5\",\"chips_outstanding\":\"150\",\"suppressed\":false}}";

/* ---- S6 JSON ---- */
static void test_json(void)
{
    zstats_snapshot_t s = fixed();
    CHECK(zstats_check(&s) == ZSTATS_OK, "the fixed snapshot is consistent");
    static char buf[4096];
    uint32_t n = 7;
    CHECK(zstats_json(&s, buf, sizeof buf, &n) == ZSTATS_OK && n == sizeof WANT_JSON - 1 &&
              !memcmp(buf, WANT_JSON, n),
          "the exact JSON");
    if (memcmp(buf, WANT_JSON, sizeof WANT_JSON - 1)) printf("%.*s\n", (int) n, buf);
    uint32_t full = n;
    bool ok = true;
    for (uint32_t cap = 0; cap < full; cap++) {
        memset(buf, '#', sizeof buf);
        n = 99;
        ok &= zstats_json(&s, buf, cap, &n) == ZSTATS_ERR_SPACE && n == 0;
        ok &= !memcmp(buf, WANT_JSON, cap); /* only the prefix that fits */
        for (uint32_t i = cap; i < sizeof buf; i++) ok &= buf[i] == '#';
    }
    CHECK(ok, "every cap below the length: ZSTATS_ERR_SPACE and nothing past cap");
    memset(buf, '#', sizeof buf);
    CHECK(zstats_json(&s, buf, full, &n) == ZSTATS_OK && n == full && buf[full] == '#',
          "an exact cap fits, no NUL written");
    CHECK(zstats_json(NULL, buf, 9, &n) == ZSTATS_ERR_ARG &&
              zstats_json(&s, NULL, 9, &n) == ZSTATS_ERR_ARG &&
              zstats_json(&s, buf, 9, NULL) == ZSTATS_ERR_ARG,
          "null arguments");

    /* the null and false renderings */
    s.reserve.claims = 0;
    s.reserve.floor = (zstats_rat_t){0, 0, false};
    s.auction.has_last = false;
    s.auction.last_epoch = s.auction.price = s.auction.qty = 0;
    s.auction.reserve_valid = false;
    s.auction.reserve = 0;
    s.auction.floor_ok = false;
    s.raffle.mode = ZSTATS_RAFFLE_SAVINGS;
    s.vault.outstanding = 30;
    s.vault.share = (zstats_rat_t){30, 30, true};
    s.reserve.floor_ok = true;
    s.zenny.live = 0;
    s.zenny.chips = 0;
    s.suppressed |= ZSTATS_SUP_ZENNY;
    CHECK(zstats_check(&s) == ZSTATS_OK, "variant consistent");
    CHECK(zstats_json(&s, buf, sizeof buf - 1, &n) == ZSTATS_OK, "variant rendered");
    buf[n] = 0;
    CHECK(strstr(buf, "\"claims_vino\":\"0\",\"floor\":null,\"required\"") != NULL,
          "undefined floor is null");
    CHECK(strstr(buf, "\"auction\":{\"last\":null,") != NULL, "no last epoch is null");
    CHECK(strstr(buf, "\"reserve\":null,\"floor\":{\"num\":\"1\"") != NULL,
          "an unvalued auction reserve is null");
    CHECK(strstr(buf, "\"backing\":\"1\",\"floor_ok\":false}") != NULL, "floor_ok false");
    CHECK(strstr(buf, "\"mode\":\"savings\"}") != NULL, "savings mode");
    CHECK(strstr(buf, "\"chips_outstanding\":\"0\",\"suppressed\":true}}") != NULL,
          "suppressed zenny");
    CHECK(strstr(buf, "\"share\":{\"num\":\"30\",\"den\":\"30\"}") != NULL, "share 30/30");
    s = fixed();
    s.bonds.n_terms = 1;
    s.bonds.k[1] = s.bonds.k[2] = 0;
    s.suppressed &= ~(ZSTATS_SUP_TERM(1) | ZSTATS_SUP_TERM(2));
    CHECK(zstats_json(&s, buf, sizeof buf - 1, &n) == ZSTATS_OK, "one term rendered");
    buf[n] = 0;
    CHECK(strstr(buf, "\"bonds\":{\"terms\":[{\"k\":\"13\",\"bonds\":\"6\",\"vbills\":\"50\","
                      "\"suppressed\":false}]},") != NULL,
          "only the configured terms are listed");
    s.bonds.n_terms = 0;
    CHECK(zstats_json(&s, buf, sizeof buf - 1, &n) == ZSTATS_OK, "no term rendered");
    buf[n] = 0;
    CHECK(strstr(buf, "\"bonds\":{\"terms\":[]},") != NULL, "an empty term list");
    s.bonds.n_terms = 200; /* never past the table */
    CHECK(zstats_json(&s, buf, sizeof buf - 1, &n) == ZSTATS_OK, "rendered");
}

/* ---- S4 encoding ---- */
static void test_encoding(void)
{
    zstats_snapshot_t s = fixed(), d;
    uint8_t prev[32], p2[32], enc[ZSTATS_ENC_LEN + 8];
    uint32_t n = 3;
    for (int i = 0; i < 32; i++) prev[i] = (uint8_t) (0xA0 + i);
    CHECK(zstats_encode(&s, prev, enc, ZSTATS_ENC_LEN - 1, &n) == ZSTATS_ERR_SPACE && n == 0,
          "cap one short");
    CHECK(zstats_encode(&s, prev, enc, ZSTATS_ENC_LEN, &n) == ZSTATS_OK && n == ZSTATS_ENC_LEN,
          "encoded at exactly the documented length");
    CHECK(!memcmp(enc, "zxv/stats/v1", 12) && enc[12] == 1 && enc[13] == 0 &&
              enc[14] == (ZSTATS_ENC_LEN & 0xFF) && enc[15] == (ZSTATS_ENC_LEN >> 8) &&
              enc[16] == 0 && enc[17] == 0,
          "tag, version 1, length");
    CHECK(enc[18] == 0x00 && enc[19] == 0x28 && enc[20] == 0x6B && enc[21] == 0xEE &&
              enc[22] == PUB && enc[23] == 0 && !memcmp(enc + 26, prev, 32),
          "juris 4000000000 and publisher little-endian, then prev");
    CHECK(enc[58] == 3 && enc[59] == 0 && enc[66] == 0xFF && enc[73] == 0xFF &&
              enc[74] == ZSTATS_SEC_ALL && enc[78] == 0x02 && enc[79] == 0x06 && enc[80] == 0 &&
              enc[82] == 5,
          "epoch, tick, present, suppressed, min_cohort");
    /* reserve section starts at 86: reserve 500 = 0x01F4 */
    CHECK(enc[86] == 0xF4 && enc[87] == 0x01 && enc[94] == 0xE8 && enc[95] == 0x03 &&
              enc[118] == 1 && enc[119] == 1 && enc[127] == 2 && enc[135] == 1,
          "the reserve section's bytes");
    CHECK(enc[ZSTATS_ENC_LEN - 12] == 5 && enc[ZSTATS_ENC_LEN - 8] == 150 &&
              enc[ZSTATS_ENC_LEN - 1] == 0,
          "zenny is last");

    memset(&d, 0x77, sizeof d);
    memset(p2, 0, sizeof p2);
    CHECK(zstats_decode(enc, n, &d, p2) == ZSTATS_OK && !memcmp(p2, prev, 32), "decoded");
    uint8_t e2[ZSTATS_ENC_LEN];
    uint32_t n2;
    CHECK(zstats_encode(&d, p2, e2, sizeof e2, &n2) == ZSTATS_OK && n2 == n && !memcmp(e2, enc, n),
          "round trip: re-encoding gives the same bytes");
    CHECK(
        d.epoch == s.epoch && d.tick == s.tick && d.juris == s.juris &&
            d.publisher == s.publisher && d.present == s.present && d.suppressed == s.suppressed &&
            d.min_cohort == s.min_cohort && d.reserve.floor.num == 500 && d.reserve.floor.defined &&
            d.reserve.floor_ok && d.vault.share.den == 300 && d.comp.tier[0].count == 5 &&
            d.comp.tier[0].sum == 70 && d.bonds.k[2] == 21 && d.bonds.n_terms == 3 &&
            d.auction.has_last && d.auction.floor_ok && d.auction.reserve_valid &&
            !memcmp(&d.raffle, &RAF, sizeof RAF) && d.barrels.rate_defined && d.barrels.lots == 1 &&
            d.barrels.rate_den == 3 && d.zenny.chips == 150 && d.zenny.live == 5,
        "round trip: the fields");
    CHECK(zstats_decode(enc, n, &d, NULL) == ZSTATS_OK, "prev optional on decode");
    CHECK(zstats_encode(&s, NULL, e2, sizeof e2, &n2) == ZSTATS_OK && n2 == n &&
              zstats_decode(e2, n2, &d, p2) == ZSTATS_OK && p2[0] == 0 && p2[31] == 0,
          "a NULL prev encodes as 32 zero bytes");

    /* refusals */
    uint8_t bad[ZSTATS_ENC_LEN + 8];
    memcpy(bad, enc, n);
    bad[0] ^= 1;
    CHECK(zstats_decode(bad, n, &d, p2) == ZSTATS_ERR_FORMAT, "wrong tag (first byte)");
    memcpy(bad, enc, n);
    bad[11] = '2';
    CHECK(zstats_decode(bad, n, &d, p2) == ZSTATS_ERR_FORMAT, "wrong tag (v2)");
    memcpy(bad, enc, n);
    bad[12] = 2;
    CHECK(zstats_decode(bad, n, &d, p2) == ZSTATS_ERR_FORMAT, "wrong version");
    memcpy(bad, enc, n);
    bad[13] = 1;
    CHECK(zstats_decode(bad, n, &d, p2) == ZSTATS_ERR_FORMAT, "wrong version (high byte)");
    memcpy(bad, enc, n);
    bad[14] ^= 1;
    CHECK(zstats_decode(bad, n, &d, p2) == ZSTATS_ERR_FORMAT, "wrong length field");
    memcpy(bad, enc, n);
    bad[17] = 1;
    CHECK(zstats_decode(bad, n, &d, p2) == ZSTATS_ERR_FORMAT, "wrong length field (high byte)");
    memcpy(bad, enc, n);
    bad[n] = 0;
    CHECK(zstats_decode(bad, n + 1, &d, p2) == ZSTATS_ERR_FORMAT, "a trailing byte");
    CHECK(zstats_decode(enc, n - 1, &d, p2) == ZSTATS_ERR_FORMAT, "one byte short");
    memcpy(bad, enc, n);
    bad[118] = 2; /* reserve.floor.defined */
    CHECK(zstats_decode(bad, n, &d, p2) == ZSTATS_ERR_FORMAT, "a bool byte of 2");
    bad[118] = 0x80;
    CHECK(zstats_decode(bad, n, &d, p2) == ZSTATS_ERR_FORMAT, "a bool byte of 0x80");
    memcpy(bad, enc, n);
    bad[ZSTATS_ENC_LEN - 1] = 0xFF; /* a value byte, not a bool: decodes */
    CHECK(zstats_decode(bad, n, &d, p2) == ZSTATS_OK && d.zenny.chips == (150 | (0xFFull << 56)),
          "the last value byte decodes as the top of chips");
    CHECK(zstats_decode(NULL, n, &d, p2) == ZSTATS_ERR_ARG &&
              zstats_decode(enc, n, NULL, p2) == ZSTATS_ERR_ARG &&
              zstats_encode(NULL, prev, e2, sizeof e2, &n2) == ZSTATS_ERR_ARG &&
              zstats_encode(&s, prev, NULL, sizeof e2, &n2) == ZSTATS_ERR_ARG &&
              zstats_encode(&s, prev, e2, sizeof e2, NULL) == ZSTATS_ERR_ARG,
          "null arguments");

    /* digest: a fixed vector */
    uint8_t dg[32], h[32];
    CHECK(zstats_digest(&s, prev, dg) == ZSTATS_OK, "digest");
    sha256(enc, n, h);
    CHECK(!memcmp(dg, h, 32), "the digest is SHA-256 of the encoding");
    static const uint8_t VEC[32] = {0x7f, 0x31, 0xa7, 0xe7, 0x1e, 0x2f, 0x63, 0xc6,
                                    0x13, 0xc0, 0xa1, 0xd6, 0x43, 0x00, 0xb0, 0xed,
                                    0x57, 0x2a, 0x4f, 0x86, 0xaf, 0xb9, 0x1f, 0xe1,
                                    0xf2, 0xc6, 0x7c, 0x4e, 0x9c, 0x51, 0xe3, 0xac};
    CHECK(!memcmp(dg, VEC, 32), "the digest matches the fixed vector");
    if (memcmp(dg, VEC, 32)) {
        for (int i = 0; i < 32; i++) printf("0x%02x,", dg[i]);
        printf("\n");
    }
    CHECK(zstats_digest(NULL, prev, dg) == ZSTATS_ERR_ARG &&
              zstats_digest(&s, prev, NULL) == ZSTATS_ERR_ARG,
          "digest null arguments");
    s.tick--;
    CHECK(zstats_digest(&s, prev, h) == ZSTATS_OK && memcmp(h, dg, 32), "any field moves it");
}

/* ---- S5 sign, verify, chain ---- */
typedef struct {
    uint8_t enc[ZSTATS_ENC_LEN];
    uint32_t n;
    uint8_t sig[32];
    uint8_t digest[32];
} pub_t;

static void publish(pub_t *p, const zstats_snapshot_t *s, const uint8_t prev[32], uint32_t signer)
{
    uint8_t m[ZSTATS_SIG_MSG];
    zstats_encode(s, prev, p->enc, sizeof p->enc, &p->n);
    zstats_digest(s, prev, p->digest);
    zstats_sign_msg(p->digest, m);
    sign(signer, m, sizeof m, p->sig);
}

static void test_chain(void)
{
    zstats_chain_t c;
    zstats_snapshot_t s = fixed(), out;
    pub_t p1, p2, p3;
    uint8_t m[ZSTATS_SIG_MSG];
    zstats_sign_msg(p1.digest, m);
    CHECK(!memcmp(m, "zxv/stats-sig/v1", 16) && !memcmp(m + 16, p1.digest, 32),
          "the signed message: tag || digest");
    memset(&c, 0x33, sizeof c);
    zstats_chain_init(&c, s.juris, PUB);
    zstats_chain_init(NULL, 0, 0);
    CHECK(c.juris == s.juris && c.publisher == PUB && !c.started && c.epoch == 0 &&
              c.head[0] == 0 && c.head[31] == 0,
          "chain starts at zero");
    publish(&p1, &s, NULL, PUB);
    nverify = 0;
    memset(&out, 0, sizeof out);
    CHECK(zstats_verify(&c, p1.enc, p1.n, p1.sig, 32, verify, NULL, &out) == ZSTATS_OK &&
              nverify == 1 && out.epoch == 3 && c.started && c.epoch == 3 &&
              !memcmp(c.head, p1.digest, 32),
          "the first snapshot is accepted and becomes the head");
    s.epoch = 4;
    publish(&p2, &s, p1.digest, PUB);
    zstats_chain_t c0 = c;
    pub_t t = p2;
    t.sig[5] ^= 1;
    CHECK(zstats_verify(&c, t.enc, t.n, t.sig, 32, verify, NULL, &out) == ZSTATS_ERR_SIG &&
              !memcmp(&c, &c0, sizeof c),
          "a bad signature is refused, the chain unchanged");
    CHECK(zstats_verify(&c, t.enc, t.n, p2.sig, 31, verify, NULL, &out) == ZSTATS_ERR_SIG,
          "a short signature is refused");
    publish(&t, &s, p1.digest, PUB + 1);
    CHECK(zstats_verify(&c, t.enc, t.n, t.sig, 32, verify, NULL, &out) == ZSTATS_ERR_SIG,
          "signed by someone else");
    publish(&t, &s, p2.digest, PUB); /* wrong prev */
    CHECK(zstats_verify(&c, t.enc, t.n, t.sig, 32, verify, NULL, &out) == ZSTATS_ERR_CHAIN,
          "a broken chain is refused");
    publish(&t, &s, NULL, PUB);
    CHECK(zstats_verify(&c, t.enc, t.n, t.sig, 32, verify, NULL, &out) == ZSTATS_ERR_CHAIN,
          "a restart from zeros is refused");
    uint8_t near[32];
    memcpy(near, p1.digest, 32);
    near[31] ^= 1;
    publish(&t, &s, near, PUB);
    CHECK(zstats_verify(&c, t.enc, t.n, t.sig, 32, verify, NULL, &out) == ZSTATS_ERR_CHAIN,
          "a prev differing in its last byte is refused");
    s.epoch = 3;
    publish(&t, &s, p1.digest, PUB);
    CHECK(zstats_verify(&c, t.enc, t.n, t.sig, 32, verify, NULL, &out) == ZSTATS_ERR_EPOCH,
          "an equal epoch is refused");
    s.epoch = 2;
    publish(&t, &s, p1.digest, PUB);
    CHECK(zstats_verify(&c, t.enc, t.n, t.sig, 32, verify, NULL, &out) == ZSTATS_ERR_EPOCH,
          "a lower epoch is refused");
    s.epoch = 4;
    s.reserve.floor_ok = false;
    publish(&t, &s, p1.digest, PUB);
    CHECK(zstats_verify(&c, t.enc, t.n, t.sig, 32, verify, NULL, &out) == ZSTATS_ERR_CONSIST,
          "a signed but inconsistent snapshot is refused");
    s = fixed();
    s.epoch = 4;
    s.publisher = PUB + 1;
    publish(&t, &s, p1.digest, PUB + 1);
    CHECK(zstats_verify(&c, t.enc, t.n, t.sig, 32, verify, NULL, &out) == ZSTATS_ERR_ARG,
          "another publisher's feed");
    s.publisher = PUB;
    s.juris = 1;
    publish(&t, &s, p1.digest, PUB);
    CHECK(zstats_verify(&c, t.enc, t.n, t.sig, 32, verify, NULL, &out) == ZSTATS_ERR_ARG,
          "another jurisdiction's feed");
    CHECK(!memcmp(&c, &c0, sizeof c), "no refusal moved the chain");
    CHECK(zstats_verify(&c, p2.enc, p2.n + 1, p2.sig, 32, verify, NULL, &out) == ZSTATS_ERR_FORMAT,
          "a trailing byte is refused");

    /* tamper: flip every byte of the encoding in turn */
    bool ok = true;
    for (uint32_t i = 0; i < p2.n; i++) {
        t = p2;
        t.enc[i] ^= 0x01;
        ok &= zstats_verify(&c, t.enc, t.n, t.sig, 32, verify, NULL, &out) != ZSTATS_OK;
        t.enc[i] ^= 0x81;
        ok &= zstats_verify(&c, t.enc, t.n, t.sig, 32, verify, NULL, &out) != ZSTATS_OK;
    }
    CHECK(ok && !memcmp(&c, &c0, sizeof c), "every flipped byte is refused");

    memset(&out, 0, sizeof out);
    CHECK(zstats_verify(&c, p2.enc, p2.n, p2.sig, 32, verify, NULL, &out) == ZSTATS_OK &&
              out.epoch == 4 && c.epoch == 4 && !memcmp(c.head, p2.digest, 32),
          "the second snapshot links");
    s = fixed();
    s.epoch = 9;
    publish(&p3, &s, p2.digest, PUB);
    CHECK(zstats_verify(&c, p3.enc, p3.n, p3.sig, 32, verify, NULL, NULL) == ZSTATS_OK &&
              c.epoch == 9,
          "epochs may skip; snap optional");
    CHECK(zstats_verify(&c, p2.enc, p2.n, p2.sig, 32, verify, NULL, &out) != ZSTATS_OK,
          "a replay is refused");
    CHECK(zstats_verify(NULL, p2.enc, p2.n, p2.sig, 32, verify, NULL, &out) == ZSTATS_ERR_ARG &&
              zstats_verify(&c, NULL, p2.n, p2.sig, 32, verify, NULL, &out) == ZSTATS_ERR_ARG &&
              zstats_verify(&c, p2.enc, p2.n, NULL, 32, verify, NULL, &out) == ZSTATS_ERR_ARG &&
              zstats_verify(&c, p2.enc, p2.n, p2.sig, 32, NULL, NULL, &out) == ZSTATS_ERR_ARG,
          "null arguments");

    /* a fresh chain accepts a genesis snapshot at epoch 0 */
    zstats_chain_init(&c, s.juris, PUB);
    s.epoch = 0;
    publish(&t, &s, NULL, PUB);
    CHECK(zstats_verify(&c, t.enc, t.n, t.sig, 32, verify, NULL, &out) == ZSTATS_OK && c.started,
          "genesis at epoch 0");
    s.epoch = 0;
    publish(&p3, &s, t.digest, PUB);
    CHECK(zstats_verify(&c, p3.enc, p3.n, p3.sig, 32, verify, NULL, &out) == ZSTATS_ERR_EPOCH,
          "then epoch 0 again is refused");

    /* the collected world signs and verifies end to end */
    world(6, DEEP2, 2, 5);
    grants_run();
    zstats_sources_t src = all();
    zstats_collect(&s, 1, 50, &src);
    zstats_chain_init(&c, JURIS, PUB);
    publish(&t, &s, NULL, PUB);
    CHECK(zstats_verify(&c, t.enc, t.n, t.sig, 32, verify, NULL, &out) == ZSTATS_OK &&
              out.comp.vbills_locked == 865,
          "a collected snapshot verifies");
}

/* ---- S5 every consistency rule ---- */
static int bad(zstats_snapshot_t s)
{
    return zstats_check(&s) == ZSTATS_ERR_CONSIST;
}
static int good(zstats_snapshot_t s)
{
    return zstats_check(&s) == ZSTATS_OK;
}

/* every group published: medium 5/70, deep 5/20, k13 6/50, k17 5/30, k21 5/30 */
static zstats_snapshot_t open_family(void)
{
    zstats_snapshot_t s = fixed();
    s.suppressed = 0;
    s.comp.tier[1] = (zstats_group_t){5, 20};
    s.bonds.term[1] = (zstats_group_t){5, 30};
    s.bonds.term[2] = (zstats_group_t){5, 30};
    s.comp.vbills_locked = 200;
    s.comp.vbills_unlocked = 100;
    return s;
}

static void test_consistency(void)
{
    zstats_snapshot_t s, f = fixed();
    CHECK(zstats_check(NULL) == ZSTATS_ERR_ARG, "null");
    CHECK(good(f), "fixed is consistent");
    s = f;
    s.present |= 0x100;
    CHECK(bad(s), "an unknown present bit");
    s = f;
    s.suppressed |= 0x4;
    CHECK(bad(s), "an unknown suppressed bit");
    s = f;
    s.suppressed |= 0x20000;
    CHECK(bad(s), "an unknown high suppressed bit");
    s = f;
    s.min_cohort = 0;
    CHECK(bad(s), "min_cohort 0");

    /* every section: absent with figures is refused, absent and zero is fine */
    static const uint32_t sup_of[8] = {
        0, 0, ZSTATS_SUP_TIER(1), ZSTATS_SUP_TERM(1) | ZSTATS_SUP_TERM(2), 0, 0, 0, 0};
    for (uint32_t i = 0; i < 8; i++) {
        s = f;
        s.present &= ~(1u << i);
        s.suppressed &= ~sup_of[i];
        CHECK(bad(s), "an absent section with figures");
    }
    s = f;
    s.present = 0;
    s.suppressed = 0;
    memset(&s.reserve, 0, sizeof s.reserve);
    memset(&s.vault, 0, sizeof s.vault);
    memset(&s.comp, 0, sizeof s.comp);
    memset(&s.bonds, 0, sizeof s.bonds);
    memset(&s.auction, 0, sizeof s.auction);
    memset(&s.raffle, 0, sizeof s.raffle);
    memset(&s.barrels, 0, sizeof s.barrels);
    memset(&s.zenny, 0, sizeof s.zenny);
    CHECK(good(s), "every section absent and zero");
    zstats_snapshot_t z = s;
    s = z;
    s.reserve.floor_ok = true;
    CHECK(bad(s), "absent reserve: a bool set");
    s = z;
    s.reserve.reserve = 1;
    CHECK(bad(s), "absent reserve: the first byte");
    s = z;
    s.suppressed = ZSTATS_SUP_TIER(0);
    CHECK(bad(s), "the medium tier bit without the compression section");
    s = z;
    s.suppressed = ZSTATS_SUP_TERM(0);
    CHECK(bad(s), "a term bit without the bond section");
    /* a family of exactly two (the tiers, no bonds): a lone suppression */
    s = open_family();
    s.present &= ~ZSTATS_SEC_BONDS;
    memset(&s.bonds, 0, sizeof s.bonds);
    s.comp.vbills_locked = 90;
    s.comp.vbills_unlocked = 210;
    CHECK(good(s), "tiers only, both published");
    s.comp.tier[1] = (zstats_group_t){0, 0};
    s.suppressed = ZSTATS_SUP_TIER(1);
    CHECK(bad(s), "tiers only: a lone suppressed tier is refused");
    s = z;
    s.auction.floor_ok = true;
    CHECK(bad(s), "absent auction: its last bool set");
    s = z;
    s.bonds.term[7].sum = 1;
    CHECK(bad(s), "absent bonds: the last term's sum");
    s = z;
    s.zenny.chips = 1;
    CHECK(bad(s), "absent zenny: chips");
    s = z;
    s.barrels.lots_backed = 1;
    s.barrels.lots = 1;
    CHECK(bad(s), "absent barrels: the last field");

    /* reserve */
    s = f;
    s.reserve.req_den = 0;
    CHECK(bad(s), "required den 0");
    s = f;
    s.reserve.floor.num = 499;
    CHECK(bad(s), "floor num is not the reserve");
    s = f;
    s.reserve.floor.den = 999;
    CHECK(bad(s), "floor den is not the claims");
    s = f;
    s.reserve.floor.defined = false;
    CHECK(bad(s), "a defined floor flagged undefined");
    s = f;
    s.reserve.floor_ok = false;
    CHECK(bad(s), "floor_ok does not match (true expected)");
    s = f;
    s.reserve.req_num = 2; /* 500*2 >= 1000*2 is false */
    CHECK(bad(s), "floor_ok does not match (false expected)");
    s.reserve.floor_ok = false;
    CHECK(good(s), "then false matches");
    s = f;
    s.reserve.req_num = 1;
    s.reserve.req_den = 1;
    s.reserve.floor_ok = false; /* 500 >= 1000 false */
    CHECK(good(s), "1/1 not met");
    s.reserve.reserve = 1000;
    s.reserve.floor.num = 1000;
    CHECK(bad(s), "1000 >= 1000 is met: false is wrong");
    s.reserve.floor_ok = true;
    CHECK(good(s), "exactly at the floor is met");
    s = f;
    s.reserve.claims = 0;
    s.reserve.floor = (zstats_rat_t){0, 0, false};
    CHECK(good(s), "no claims: undefined floor, met");
    s.reserve.floor.num = 500;
    CHECK(bad(s), "undefined floor with num != 0");
    s.reserve.floor = (zstats_rat_t){500, 0, true};
    CHECK(bad(s), "a defined floor with den 0");

    /* vault */
    s = f;
    s.vault.held = 301;
    s.vault.share.num = 301;
    CHECK(bad(s), "the vault holds more than outstanding");
    s = f;
    s.vault.held = 300;
    s.vault.share.num = 300;
    CHECK(good(s), "the vault may hold all of it");
    s = f;
    s.vault.share.den = 299;
    CHECK(bad(s), "vault share den");
    s = f;
    s.vault.share.num = 31;
    CHECK(bad(s), "vault share num");

    /* compression and the lock family */
    s = f;
    s.comp.vbills_unlocked++;
    CHECK(bad(s), "locked + unlocked != outstanding");
    s = f;
    s.comp.vbills_locked = UINT64_MAX;
    s.comp.vbills_unlocked = 2;
    s.comp.vbills_outstanding = 1;
    CHECK(bad(s), "locked + unlocked overflows");
    s = f;
    s.comp.tier[0].sum = 160;
    s.comp.vbills_locked = 151;
    s.comp.vbills_unlocked = 149;
    CHECK(bad(s), "published groups above the locked total");
    s = f;
    s.comp.tier[0].sum = 100;
    CHECK(good(s), "published groups exactly the locked total, with suppression");
    s = f;
    s.comp.tier[0].count = 4;
    CHECK(bad(s), "a published group below min_cohort");
    s = f;
    s.comp.tier[1].count = 1;
    CHECK(bad(s), "a suppressed group with a count");
    s = f;
    s.comp.tier[1].sum = 1;
    CHECK(bad(s), "a suppressed group with a sum");
    s = f;
    s.bonds.term[2].sum = 1;
    CHECK(bad(s), "a suppressed term with a sum");
    s = open_family();
    CHECK(good(s), "an open family is consistent");
    s.comp.vbills_locked = 201;
    s.comp.vbills_unlocked = 99;
    CHECK(bad(s), "no suppression: published groups must equal the total");
    s = open_family();
    s.comp.tier[1] = (zstats_group_t){0, 0};
    s.suppressed = ZSTATS_SUP_TIER(1);
    CHECK(bad(s), "a lone suppressed group would be read off the total");
    s.bonds.term[1] = (zstats_group_t){0, 0};
    s.suppressed |= ZSTATS_SUP_TERM(1);
    CHECK(good(s), "two suppressed groups are fine");
    s = open_family();
    s.comp.tier[0].sum = UINT64_MAX;
    CHECK(bad(s), "published groups overflow");
    s = open_family();
    s.comp.tier[0].count = 5; /* exactly min_cohort: published */
    CHECK(good(s), "a group at exactly min_cohort is published");
    s.min_cohort = 6;
    CHECK(bad(s), "the same group under a floor of 6 must be suppressed");

    /* bonds */
    s = f;
    s.bonds.n_terms = ZSTATS_TERMS + 1;
    CHECK(bad(s), "more terms than the table");
    s = f;
    s.bonds.n_terms = ZSTATS_TERMS;
    for (uint32_t i = 3; i < ZSTATS_TERMS; i++) s.suppressed |= ZSTATS_SUP_TERM(i);
    CHECK(good(s), "a full table of terms");
    s = f;
    s.bonds.n_terms = 2;
    s.suppressed &= ~ZSTATS_SUP_TERM(2);
    CHECK(bad(s), "a k beyond n_terms");
    s.bonds.k[2] = 0;
    CHECK(good(s), "cleared: fine");
    s.bonds.term[2].count = 1;
    CHECK(bad(s), "a count beyond n_terms");
    s.bonds.term[2].count = 0;
    s.bonds.term[2].sum = 1;
    CHECK(bad(s), "a sum beyond n_terms");
    s = f;
    s.bonds.n_terms = 2;
    s.bonds.k[2] = 0;
    CHECK(bad(s), "a suppressed bit beyond n_terms");
    s = f;
    s.present &= ~ZSTATS_SEC_COMPRESSION;
    memset(&s.comp, 0, sizeof s.comp);
    CHECK(bad(s), "tier bits without the compression section");
    s.suppressed &= ~ZSTATS_SUP_TIER(1);
    CHECK(good(s), "bonds alone");
    s.bonds.n_terms = 1;
    s.bonds.k[1] = s.bonds.k[2] = 0;
    s.suppressed = 0;
    s.bonds.term[0] = (zstats_group_t){0, 0};
    s.suppressed = ZSTATS_SUP_TERM(0);
    CHECK(good(s), "a one-group family may have its one group suppressed");

    /* auction */
    s = f;
    s.auction.floor_den = 0;
    CHECK(bad(s), "auction floor den 0");
    s = f;
    s.auction.backing = 0;
    CHECK(bad(s), "auction backing 0");
    s = f;
    s.auction.floor_num = (uint64_t) 1 << 63;
    s.auction.backing = 2;
    s.auction.floor_ok = false;
    CHECK(bad(s), "floor_num * backing overflows");
    s = f;
    s.auction.has_last = false;
    CHECK(bad(s), "no last epoch but its figures");
    s.auction.last_epoch = 0;
    CHECK(bad(s), "price and qty still set");
    s.auction.price = 0;
    CHECK(bad(s), "qty still set");
    s.auction.qty = 0;
    CHECK(good(s), "all cleared");
    s = f;
    s.auction.has_last = false;
    s.auction.last_epoch = s.auction.qty = 0;
    CHECK(bad(s), "only the price set");
    s = f;
    s.auction.has_last = false;
    s.auction.price = s.auction.qty = 0;
    CHECK(bad(s), "only the epoch set");
    s = f;
    s.auction.reserve_valid = false;
    s.auction.floor_ok = false;
    CHECK(bad(s), "an unvalued reserve with a value");
    s.auction.reserve = 0;
    CHECK(good(s), "an unvalued reserve: zero, floor not ok");
    s.auction.floor_ok = true;
    CHECK(bad(s), "an unvalued reserve cannot meet the floor");
    s = f;
    s.auction.floor_ok = false;
    CHECK(bad(s), "auction floor_ok false is wrong");
    s = f;
    s.auction.outstanding = 1001;
    CHECK(bad(s), "1000 < 1001: floor_ok true is wrong");
    s.auction.floor_ok = false;
    CHECK(good(s), "and false is right");
    s.auction.outstanding = 1000;
    CHECK(bad(s), "1000 >= 1000: false is wrong");
    s = f;
    s.auction.floor_num = 3;
    s.auction.backing = 2;
    s.auction.floor_den = 1;
    s.auction.outstanding = 166; /* 1000 >= 6*166 = 996 */
    CHECK(good(s), "k = num * backing used");
    s.auction.outstanding = 167; /* 1002 */
    CHECK(bad(s), "k = 6: 1000 < 1002");
    s = f;
    s.auction.floor_den = 2;
    s.auction.outstanding = 2000; /* 1000*2 >= 2000 */
    CHECK(good(s), "den used on the reserve side");

    /* raffle */
    s = f;
    s.raffle.mode = 2;
    CHECK(bad(s), "an unknown raffle mode");
    s.raffle.mode = 0;
    CHECK(good(s), "savings mode");

    /* barrels */
    s = f;
    s.barrels.barrels_backed = 1;
    CHECK(bad(s), "backed + unbacked != outstanding");
    s = f;
    s.barrels.barrels_backed = UINT64_MAX;
    s.barrels.barrels_unbacked = 1;
    s.barrels.barrels_outstanding = 0;
    CHECK(bad(s), "backed + unbacked overflows");
    s = f;
    s.barrels.lots_backed = 2;
    CHECK(bad(s), "more backed lots than lots");
    s.barrels.lots = 2;
    CHECK(good(s), "every lot backed");
    s = f;
    s.barrels.rate_num = 499;
    CHECK(bad(s), "G_rate num is not the inflow");
    s = f;
    s.barrels.rate_den = 4;
    CHECK(bad(s), "G_rate den is not the weight");
    s = f;
    s.barrels.rate_defined = false;
    CHECK(bad(s), "a defined G_rate flagged undefined");
    s = f;
    s.barrels.weight = 0;
    s.barrels.rate_den = 0;
    s.barrels.rate_num = 0;
    s.barrels.rate_defined = false;
    CHECK(good(s), "weight 0: G_rate 0/0 undefined");
    s.barrels.rate_defined = true;
    CHECK(bad(s), "a G_rate with den 0 flagged defined");
    s.barrels.rate_defined = false;
    s.barrels.rate_num = 500;
    CHECK(bad(s), "an undefined G_rate with a numerator");

    /* zenny */
    s = f;
    s.zenny.live = 4;
    CHECK(bad(s), "4 live chips published");
    s = f;
    s.suppressed |= ZSTATS_SUP_ZENNY;
    CHECK(bad(s), "suppressed zenny with figures");
    s.zenny.live = 0;
    CHECK(bad(s), "suppressed zenny with chips");
    s.zenny.chips = 0;
    CHECK(good(s), "suppressed zenny, zero");
    s.zenny.live = 0;
    s.zenny.chips = 0;
    s.present &= ~ZSTATS_SEC_ZENNY;
    CHECK(bad(s), "the zenny bit without the section");
    s.suppressed &= ~ZSTATS_SUP_ZENNY;
    CHECK(good(s), "absent zenny, no bit");
}

int main(void)
{
    test_collect();
    test_readonly();
    test_absent();
    test_grate();
    test_privacy();
    test_bad_sources();
    test_json();
    test_encoding();
    test_chain();
    test_consistency();
    printf("PASS %d FAIL %d\n", pass, fail);
    return fail ? 1 : 0;
}
