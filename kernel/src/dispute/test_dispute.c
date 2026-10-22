/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_dispute.c — the three-level dispute funnel (dispute.h D1-D10) on a
 * real pay_ledger: level-1 proofs decided by a deterministic predicate,
 * level-2 juries drawn by stake from a VRF output (the real ECVRF of
 * src/vrf, RFC 9381, through vrf_dsp_verify), blind commit-reveal,
 * slashing to the reserve-floor account, 1x/4x/16x appeals, ties, the
 * level-3 gateway, and the loser recorded in kernel/src/cred through
 * cred_fail. Conservation is audited after every step. Integer only: no
 * floating point anywhere. */
#include <stdio.h>
#include <string.h>
#include "dispute.h"
#include "../cred/cred.h"
#include "../robin_debanks/sha256.h"
#include "../vrf/vrf.h"

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

#define NJ     28u
#define BUYER  1u
#define SELLER 2u
#define GW     77u

static pay_ledger_t L;
static dsp_t D;
static uint16_t usd, eur;
static uint32_t iss, escrow, reserve, bacct, sacct, jacct[NJ], spare, eacct;
static uint64_t nreq, reserve0;
static uint8_t SEED[32];

/* ---- stand-ins ---- */
/* signature: SHA-256(signer LE32 || msg); a signer in `bad_signer` never verifies */
static uint32_t bad_signer = 0xffffffffu;
static void sign(uint32_t signer, const uint8_t *m, uint32_t len, uint8_t sig[32])
{
    sha256_ctx_t c;
    uint8_t s[4] = {(uint8_t) signer, (uint8_t) (signer >> 8), (uint8_t) (signer >> 16),
                    (uint8_t) (signer >> 24)};
    sha256_init(&c);
    sha256_update(&c, s, 4);
    sha256_update(&c, m, len);
    sha256_final(&c, sig);
}
static bool verify(void *ctx, uint32_t signer, const uint8_t *m, uint32_t len, const uint8_t *sig,
                   uint32_t sig_len)
{
    uint8_t s[32];
    (void) ctx;
    if (signer == bad_signer || sig_len != 32 || !sig) return false;
    sign(signer, m, len, s);
    return memcmp(s, sig, 32) == 0;
}
/* VRF: the real ECVRF-EDWARDS25519-SHA512-TAI (src/vrf, RFC 9381) through
 * its dispute adapter vrf_dsp_verify: alpha = the 32-byte dispute seed,
 * proof = pi (80 bytes), out = beta[0..31]. VCTX carries the jury VRF key's
 * public half (verify and pred ignore ctx); OSK is a second key whose
 * proofs must be refused. */
static uint8_t VSK[VRF_SK_LEN], OSK[VRF_SK_LEN];
static vrf_dsp_ctx_t VCTX;
static void vrf_keys(void)
{
    uint8_t s[32], opk[VRF_PK_LEN];
    for (int i = 0; i < 32; i++) s[i] = (uint8_t) (i * 11 + 5);
    vrf_keypair(s, VCTX.pk, VSK);
    s[0] ^= 1;
    vrf_keypair(s, opk, OSK);
}
static void vrf_eval(const uint8_t seed[32], uint8_t out[32], uint8_t proof[VRF_PROOF_LEN])
{
    if (!vrf_dsp_out(VSK, seed, proof, out)) {
        fail++;
        printf("FAIL vrf_dsp_out\n");
    }
}
/* level-1 predicate: kind 1 (delivery proof) matching the terms -> seller;
 * kind 2 (carrier non-delivery attestation) matching -> buyer; else none. */
static int pred_calls;
static int pred(void *ctx, const dsp_case_t *c, uint8_t party, uint8_t kind, const uint8_t h[32])
{
    (void) ctx;
    (void) party;
    pred_calls++;
    if (memcmp(h, c->terms, 32) != 0) return DSP_PRED_NONE;
    if (kind == 1) return DSP_PRED_SELLER;
    if (kind == 2) return DSP_PRED_BUYER;
    return 7; /* junk: treated as undecided */
}

/* ---- ledger ---- */
static pay_posting_req_t *mk(void)
{
    static pay_posting_req_t r;
    uint8_t in[9], d[32];
    memset(&r, 0, sizeof r);
    nreq++;
    memcpy(in, &nreq, 8);
    in[8] = 'T';
    sha256(in, sizeof in, d);
    memcpy(r.idem_key, d, 32);
    pay_uetr_from_random(d + 8, r.uetr);
    snprintf(r.e2e, sizeof r.e2e, "TD-%llu", (unsigned long long) nreq);
    return &r;
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
static void fund(uint32_t a, uint64_t amt)
{
    pay_receipt_t rc;
    pay_ledger_issue(&L, mk(), iss, a, amt, &rc);
}

static bool audit(void)
{
    return dsp_check(&D) && pay_ledger_check(&L) && deb(reserve) - reserve0 == D.slashed_total &&
           deb(escrow) == D.locked_total - D.returned_total - D.slashed_total;
}

static void ledger_setup(void)
{
    pay_ledger_init(&L, NULL);
    nreq = 0;
    pay_ledger_add_fiat(&L, "USD", 840, 2, &usd);
    pay_ledger_add_fiat(&L, "EUR", 978, 2, &eur);
    iss = open_acct(usd, 9, PAY_ACCT_ISSUER);
    escrow = open_acct(usd, 900, 0);
    reserve = open_acct(usd, 901, 0);
    bacct = open_acct(usd, BUYER, 0);
    sacct = open_acct(usd, SELLER, 0);
    for (uint32_t k = 0; k < NJ; k++) jacct[k] = open_acct(usd, 100 + k, 0);
    spare = open_acct(usd, 5, 0);
    eacct = open_acct(eur, 6, 0);
    fund(bacct, 10000000);
    fund(sacct, 10000000);
    for (uint32_t k = 0; k < NJ; k++) fund(jacct[k], 1000000);
    fund(reserve, 777);
    reserve0 = deb(reserve);
    for (int i = 0; i < 32; i++) SEED[i] = (uint8_t) (i * 7 + 1);
}

static void setup(const dsp_config_t *cfg, uint32_t njurors)
{
    ledger_setup();
    CHECK(dsp_init(&D, &L, cfg, escrow, reserve, GW, verify, vrf_dsp_verify, pred, &VCTX, SEED) ==
              DSP_OK,
          "init");
    for (uint32_t k = 0; k < njurors; k++) dsp_juror_add(&D, 100 + k, jacct[k], 10 + k * 3);
}

static uint8_t ORD[32], TERMS[32];
static void order_id(uint32_t n)
{
    memset(ORD, 0, 32);
    memcpy(ORD, &n, 4);
    ORD[31] = 0xd1;
    memset(TERMS, 0xab, 32);
    TERMS[0] = (uint8_t) n;
}

static uint32_t open_case(uint32_t n, uint64_t tick)
{
    uint32_t id = 0;
    order_id(n);
    dsp_open(&D, ORD, TERMS, BUYER, SELLER, bacct, sacct, 50000, tick, &id);
    return id;
}

static dsp_status_t proof(uint32_t id, uint8_t party, uint8_t kind, const uint8_t h[32])
{
    uint8_t m[DSP_PROOF_MSG], sig[32];
    dsp_proof_msg(id, dsp_case(&D, id)->order, party, kind, h, m);
    sign(party == DSP_BUYER ? BUYER : SELLER, m, sizeof m, sig);
    return dsp_proof(&D, id, party, kind, h, sig, 32);
}

static dsp_status_t do_select(uint32_t id, uint64_t tick)
{
    uint8_t seed[32], out[32], pr[VRF_PROOF_LEN];
    dsp_vrf_seed(&D, id, seed);
    vrf_eval(seed, out, pr);
    return dsp_select(&D, id, pr, VRF_PROOF_LEN, out, tick);
}

static void salt_of(uint32_t juror, uint32_t id, uint8_t salt[32])
{
    memset(salt, 0x33, 32);
    memcpy(salt, &juror, 4);
    memcpy(salt + 4, &id, 4);
}

static dsp_status_t do_commit(uint32_t id, uint32_t juror, uint8_t vote, uint64_t tick)
{
    uint8_t salt[32], cm[32], m[DSP_COMMIT_MSG], sig[32];
    salt_of(juror, id, salt);
    dsp_vote_commit(id, juror, vote, salt, cm);
    dsp_commit_msg(id, dsp_case(&D, id)->round, juror, cm, m);
    sign(juror, m, sizeof m, sig);
    return dsp_commit(&D, id, juror, cm, sig, 32, tick);
}

static dsp_status_t do_reveal(uint32_t id, uint32_t juror, uint8_t vote, uint64_t tick)
{
    uint8_t salt[32];
    salt_of(juror, id, salt);
    return dsp_reveal(&D, id, juror, vote, salt, tick);
}

static uint32_t seat_id(uint32_t id, uint32_t d)
{
    const dsp_case_t *c = dsp_case(&D, id);
    return D.pool[c->seat[d]].id;
}

/* ---- reference selection (independent) ---- */
static uint32_t ref_select(const uint8_t out[32], const uint64_t *w, const bool *elig_in,
                           uint32_t n, uint32_t J, uint8_t *seat)
{
    bool elig[64];
    unsigned __int128 W = 0;
    uint32_t ctr = 0;
    for (uint32_t k = 0; k < n; k++) {
        elig[k] = elig_in[k];
        if (elig[k]) W += w[k];
    }
    for (uint32_t d = 0; d < J; d++) {
        uint64_t x, r;
        for (;;) {
            uint8_t m[19 + 32 + 4], h[32];
            memcpy(m, "zxv/dispute-jury/v1", 19);
            memcpy(m + 19, out, 32);
            m[51] = (uint8_t) ctr;
            m[52] = (uint8_t) (ctr >> 8);
            m[53] = (uint8_t) (ctr >> 16);
            m[54] = (uint8_t) (ctr >> 24);
            ctr++;
            sha256(m, sizeof m, h);
            memcpy(&x, h, 8); /* little-endian host */
            unsigned __int128 two64 = (unsigned __int128) 1 << 64;
            unsigned __int128 lim = two64 - two64 % W;
            if ((unsigned __int128) x < lim) break;
        }
        r = (uint64_t) (x % W);
        uint32_t k = 0;
        for (;; k++) {
            if (!elig[k]) continue;
            if (r < w[k]) break;
            r -= w[k];
        }
        seat[d] = (uint8_t) k;
        elig[k] = false;
        W -= w[k];
    }
    return ctr;
}

/* ---- tests ---- */
static void test_config(void)
{
    dsp_config_t f, g;
    dsp_config_default(&f);
    dsp_config_default(NULL);
    CHECK(f.base_bond == 1000 && f.juror_bond == 500 && f.jury[0] == 3 && f.jury[1] == 7 &&
              f.jury[2] == 15 && f.commit_ticks == 259200 && f.reveal_ticks == 172800 &&
              f.appeal_ticks == 259200 && !f.cred_weighted,
          "documented defaults");
    CHECK(dsp_config_ok(&f) && !dsp_config_ok(NULL), "defaults valid");
#define BAD(field, val, msg)                                                                       \
    do {                                                                                           \
        g = f;                                                                                     \
        g.field = val;                                                                             \
        CHECK(!dsp_config_ok(&g), msg);                                                            \
    } while (0)
#define GOOD(field, val, msg)                                                                      \
    do {                                                                                           \
        g = f;                                                                                     \
        g.field = val;                                                                             \
        CHECK(dsp_config_ok(&g), msg);                                                             \
    } while (0)
    BAD(base_bond, 0, "base 0");
    BAD(base_bond, PAY_BAL_MAX / 16, "base * 16 reaches PAY_BAL_MAX");
    GOOD(base_bond, PAY_BAL_MAX / 16 - 1, "largest base");
    BAD(juror_bond, 0, "juror bond 0");
    BAD(juror_bond, PAY_BAL_MAX / DSP_MAX_JURY, "juror bond too big");
    GOOD(juror_bond, PAY_BAL_MAX / DSP_MAX_JURY - 1, "largest juror bond");
    BAD(commit_ticks, 0, "commit 0");
    BAD(reveal_ticks, 0, "reveal 0");
    BAD(appeal_ticks, 0, "appeal 0");
    BAD(jury[0], 4, "even jury");
    BAD(jury[0], 0, "empty jury");
    BAD(jury[1], 1, "shrinking jury");
    BAD(jury[2], 17, "jury above max");
    GOOD(jury[0], 1, "jury of one");
    BAD(jury[1], 15, "3 + 15 + 15 = 33 > pool");
    g = f;
    g.jury[0] = 5;
    g.jury[1] = 13;
    g.jury[2] = 15;
    CHECK(!dsp_config_ok(&g), "sum 33 > pool");
    g.jury[0] = 3;
    CHECK(dsp_config_ok(&g), "sum 31 ok");
    g.jury[0] = 5;
    g.jury[1] = 11;
    CHECK(dsp_config_ok(&g), "sum 31 ok again");
    g.jury[1] = 15;
    g.jury[0] = 1;
    g.jury[2] = 15;
    CHECK(dsp_config_ok(&g), "1, 15, 15 = 31: equal sizes allowed");
    g.jury[0] = 3;
    CHECK(!dsp_config_ok(&g), "3, 15, 15 = 33 refused");
    CHECK(dsp_appeal_bond(&f, 1) == 1000 && dsp_appeal_bond(&f, 2) == 4000 &&
              dsp_appeal_bond(&f, 3) == 16000,
          "1x / 4x / 16x");
    CHECK(dsp_appeal_bond(&f, 0) == 0 && dsp_appeal_bond(&f, 4) == 0 &&
              dsp_appeal_bond(NULL, 1) == 0,
          "no bond outside rounds 1..3");
}

static void test_init_pool(void)
{
    ledger_setup();
    dsp_config_t f;
    dsp_config_default(&f);
    CHECK(dsp_init(NULL, &L, NULL, escrow, reserve, GW, verify, vrf_dsp_verify, pred, &VCTX,
                   SEED) == DSP_ERR_ARG &&
              dsp_init(&D, NULL, NULL, escrow, reserve, GW, verify, vrf_dsp_verify, pred, &VCTX,
                       SEED) == DSP_ERR_ARG &&
              dsp_init(&D, &L, NULL, escrow, reserve, GW, NULL, vrf_dsp_verify, pred, &VCTX,
                       SEED) == DSP_ERR_ARG &&
              dsp_init(&D, &L, NULL, escrow, reserve, GW, verify, NULL, pred, &VCTX, SEED) ==
                  DSP_ERR_ARG &&
              dsp_init(&D, &L, NULL, escrow, reserve, GW, verify, vrf_dsp_verify, pred, NULL,
                       NULL) == DSP_ERR_ARG,
          "NULL args");
    f.jury[0] = 2;
    CHECK(dsp_init(&D, &L, &f, escrow, reserve, GW, verify, vrf_dsp_verify, pred, &VCTX, SEED) ==
              DSP_ERR_ARG,
          "bad cfg");
    CHECK(dsp_init(&D, &L, NULL, escrow, escrow, GW, verify, vrf_dsp_verify, pred, &VCTX, SEED) ==
              DSP_ERR_ARG,
          "escrow == reserve");
    CHECK(dsp_init(&D, &L, NULL, L.n_accounts, reserve, GW, verify, vrf_dsp_verify, pred, &VCTX,
                   SEED) == DSP_ERR_ARG &&
              dsp_init(&D, &L, NULL, escrow, L.n_accounts, GW, verify, vrf_dsp_verify, pred, &VCTX,
                       SEED) == DSP_ERR_ARG,
          "missing account");
    CHECK(dsp_init(&D, &L, NULL, escrow, eacct, GW, verify, vrf_dsp_verify, pred, &VCTX, SEED) ==
              DSP_ERR_ARG,
          "asset mismatch");
    CHECK(!D.ready && !dsp_check(&D), "refused init leaves no usable module");
    CHECK(dsp_init(&D, &L, NULL, iss, reserve, GW, verify, vrf_dsp_verify, pred, &VCTX, SEED) ==
              DSP_ERR_ARG,
          "issuer escrow");
    CHECK(dsp_init(&D, &L, NULL, spare, iss, GW, verify, vrf_dsp_verify, pred, &VCTX, SEED) ==
              DSP_ERR_ARG,
          "issuer reserve");
    CHECK(dsp_init(&D, &L, NULL, bacct, reserve, GW, verify, vrf_dsp_verify, pred, &VCTX, SEED) ==
              DSP_ERR_STATE,
          "escrow not empty");
    CHECK(dsp_init(&D, &L, NULL, escrow, reserve, GW, verify, vrf_dsp_verify, NULL, &VCTX, SEED) ==
                  DSP_OK &&
              D.ready && D.next_id == 1 && D.asset == usd && D.cfg.jury[2] == 15 &&
              D.pred == NULL && memcmp(D.seed, SEED, 32) == 0 && D.gateway == GW,
          "init with defaults");
    CHECK(dsp_check(&D), "fresh audit");
    /* pool */
    CHECK(dsp_juror_add(NULL, 100, jacct[0], 5) == DSP_ERR_ARG, "add NULL");
    CHECK(dsp_juror_add(&D, 100, jacct[0], 0) == DSP_ERR_ARG, "stake 0");
    CHECK(dsp_juror_add(&D, 100, jacct[0], DSP_STAKE_MAX + 1) == DSP_ERR_ARG, "stake too big");
    CHECK(dsp_juror_add(&D, 100, eacct, 5) == DSP_ERR_ARG, "other asset");
    CHECK(dsp_juror_add(&D, 100, escrow, 5) == DSP_ERR_ARG &&
              dsp_juror_add(&D, 100, reserve, 5) == DSP_ERR_ARG,
          "module accounts refused");
    CHECK(dsp_juror_add(&D, 100, L.n_accounts, 5) == DSP_ERR_ARG, "missing account");
    CHECK(D.n_pool == 0, "nothing added");
    CHECK(dsp_juror_add(&D, 100, jacct[0], DSP_STAKE_MAX) == DSP_OK && D.n_pool == 1 &&
              D.pool[0].cred_bps == 10000 && D.pool[0].active && D.pool[0].stake == DSP_STAKE_MAX,
          "add max stake");
    CHECK(dsp_juror_add(&D, 100, jacct[1], 5) == DSP_ERR_DUP, "dup juror id");
    for (uint32_t k = 1; k < DSP_MAX_POOL; k++) dsp_juror_add(&D, 100 + k, jacct[k % NJ], 5);
    CHECK(D.n_pool == DSP_MAX_POOL && dsp_juror_add(&D, 999, jacct[0], 5) == DSP_ERR_FULL,
          "pool full");
    CHECK(dsp_juror_weight(&D, 0) == DSP_STAKE_MAX && dsp_juror_weight(&D, 1) == 5 &&
              dsp_juror_weight(&D, DSP_MAX_POOL) == 0 && dsp_juror_weight(NULL, 0) == 0,
          "weights");
    CHECK(dsp_juror_set_cred(&D, 101, 10001) == DSP_ERR_ARG &&
              dsp_juror_set_cred(NULL, 101, 5) == DSP_ERR_ARG,
          "cred bps bounds");
    CHECK(dsp_juror_set_cred(&D, 5000, 5) == DSP_ERR_UNKNOWN, "cred unknown");
    CHECK(dsp_juror_set_cred(&D, 100, 2500) == DSP_OK && dsp_juror_weight(&D, 0) == DSP_STAKE_MAX,
          "cred ignored unless cred_weighted");
    D.cfg.cred_weighted = true;
    CHECK(dsp_juror_weight(&D, 0) == DSP_STAKE_MAX / 4, "cred weighted: floor(stake*bps/1e4)");
    CHECK(dsp_juror_set_cred(&D, 101, 10000) == DSP_OK, "cred 10000 accepted");
    CHECK(dsp_juror_weight(&D, 1) == 5, "full cred keeps stake");
    dsp_juror_set_cred(&D, 101, 1999);
    CHECK(dsp_juror_weight(&D, 1) == 0, "floor to 0");
    dsp_juror_set_cred(&D, 101, 2000);
    CHECK(dsp_juror_weight(&D, 1) == 1, "exactly 1");
    D.cfg.cred_weighted = false;
    CHECK(dsp_juror_remove(&D, 100) == DSP_OK && !D.pool[0].active && dsp_juror_weight(&D, 0) == 0,
          "remove");
    CHECK(dsp_juror_remove(&D, 4242) == DSP_ERR_UNKNOWN &&
              dsp_juror_remove(NULL, 100) == DSP_ERR_ARG,
          "remove unknown");
    D.ready = false;
    CHECK(dsp_juror_add(&D, 4000, jacct[0], 5) == DSP_ERR_ARG, "not ready");
}

static void test_open(void)
{
    setup(NULL, 0);
    uint32_t id = 99;
    order_id(1);
    CHECK(dsp_open(NULL, ORD, TERMS, BUYER, SELLER, bacct, sacct, 1, 0, &id) == DSP_ERR_ARG,
          "NULL");
    CHECK(dsp_open(&D, NULL, TERMS, BUYER, SELLER, bacct, sacct, 1, 0, &id) == DSP_ERR_ARG &&
              dsp_open(&D, ORD, NULL, BUYER, SELLER, bacct, sacct, 1, 0, &id) == DSP_ERR_ARG,
          "NULL ids");
    CHECK(dsp_open(&D, ORD, TERMS, BUYER, BUYER, bacct, sacct, 1, 0, &id) == DSP_ERR_ARG,
          "same party");
    CHECK(dsp_open(&D, ORD, TERMS, BUYER, SELLER, bacct, bacct, 1, 0, &id) == DSP_ERR_ARG,
          "same acct");
    CHECK(dsp_open(&D, ORD, TERMS, BUYER, SELLER, escrow, sacct, 1, 0, &id) == DSP_ERR_ARG &&
              dsp_open(&D, ORD, TERMS, BUYER, SELLER, bacct, reserve, 1, 0, &id) == DSP_ERR_ARG,
          "module accounts");
    CHECK(dsp_open(&D, ORD, TERMS, BUYER, SELLER, eacct, sacct, 1, 0, &id) == DSP_ERR_ARG &&
              dsp_open(&D, ORD, TERMS, BUYER, SELLER, bacct, eacct, 1, 0, &id) == DSP_ERR_ARG,
          "other asset");
    CHECK(id == 99, "nothing opened");
    CHECK(dsp_open(&D, ORD, TERMS, BUYER, SELLER, bacct, sacct, 50000, 7, &id) == DSP_OK &&
              id == 1 && D.c[0].id == 1 && D.c[1].state == DSP_ST_FREE,
          "open into the first free slot");
    const dsp_case_t *c = dsp_case(&D, 1);
    CHECK(c && c->state == DSP_ST_PROOFS && c->level == 1 && c->round == 0 && c->buyer == BUYER &&
              c->seller == SELLER && c->escrow == 50000 && c->opened == 7 &&
              memcmp(c->order, ORD, 32) == 0 && memcmp(c->terms, TERMS, 32) == 0 &&
              c->buyer_acct == bacct && c->seller_acct == sacct,
          "case fields");
    CHECK(dsp_open(&D, ORD, TERMS, 3, 4, bacct, sacct, 1, 0, &id) == DSP_ERR_DUP, "dup order");
    CHECK(dsp_case(&D, 0) == NULL && dsp_case(&D, 2) == NULL && dsp_case(NULL, 1) == NULL,
          "lookup");
    for (uint32_t n = 2; n <= DSP_MAX_CASES; n++) open_case(n, 0);
    order_id(100);
    CHECK(dsp_open(&D, ORD, TERMS, BUYER, SELLER, bacct, sacct, 1, 0, NULL) == DSP_ERR_FULL,
          "full");
    CHECK(dsp_release(&D, 1) == DSP_ERR_STATE, "release only FINAL");
    CHECK(dsp_release(&D, 77) == DSP_ERR_UNKNOWN && dsp_release(NULL, 1) == DSP_ERR_UNKNOWN,
          "release unknown");
    memset(TERMS, 0xab, 32);
    TERMS[0] = 3;
    CHECK(proof(3, DSP_SELLER, 1, TERMS) == DSP_OK, "case 3 final at level 1");
    CHECK(dsp_release(&D, 3) == DSP_OK && dsp_case(&D, 3) == NULL, "released");
    CHECK(dsp_open(&D, ORD, TERMS, BUYER, SELLER, bacct, sacct, 1, 0, &id) == DSP_OK &&
              id == DSP_MAX_CASES + 1 && D.c[2].id == id,
          "slot reused, ids keep counting");
    CHECK(dsp_check(&D), "audit");
    D.ready = false;
    order_id(101);
    CHECK(dsp_open(&D, ORD, TERMS, BUYER, SELLER, bacct, sacct, 1, 0, &id) == DSP_ERR_ARG,
          "not ready");
}

static void test_level1(void)
{
    setup(NULL, 0);
    pred_calls = 0;
    uint32_t id = open_case(1, 0);
    uint8_t h[32], m[DSP_PROOF_MSG], sig[32];
    memset(h, 1, 32);
    dsp_proof_msg(id, ORD, DSP_BUYER, 1, h, m);
    CHECK(memcmp(m, "zxv/dispute-proof/v1", 20) == 0 && m[20] == 1 && m[21] == 0 &&
              memcmp(m + 24, ORD, 32) == 0 && m[56] == 1 && m[57] == 1 &&
              memcmp(m + 58, h, 32) == 0,
          "proof message layout");
    sign(BUYER, m, sizeof m, sig);
    CHECK(dsp_proof(&D, id, DSP_BUYER, 1, NULL, sig, 32) == DSP_ERR_ARG, "NULL hash");
    CHECK(dsp_proof(&D, id, 0, 1, h, sig, 32) == DSP_ERR_ARG &&
              dsp_proof(&D, id, 3, 1, h, sig, 32) == DSP_ERR_ARG,
          "bad party");
    CHECK(dsp_proof(&D, 9, DSP_BUYER, 1, h, sig, 32) == DSP_ERR_UNKNOWN, "unknown case");
    CHECK(dsp_proof(&D, id, DSP_SELLER, 1, h, sig, 32) == DSP_ERR_SIG, "signed by the other party");
    CHECK(dsp_proof(&D, id, DSP_BUYER, 2, h, sig, 32) == DSP_ERR_SIG, "kind not what was signed");
    CHECK(dsp_case(&D, id)->n_proofs == 0 && pred_calls == 0, "refused proofs left nothing");
    CHECK(dsp_proof(&D, id, DSP_BUYER, 1, h, sig, 32) == DSP_PENDING && pred_calls == 1 &&
              dsp_case(&D, id)->n_proofs == 1,
          "mismatching hash: undecided");
    CHECK(proof(id, DSP_SELLER, 3, TERMS) == DSP_PENDING &&
              dsp_case(&D, id)->state == DSP_ST_PROOFS,
          "junk predicate value: undecided");
    dsp_ruling_t r;
    CHECK(dsp_ruling(&D, id, &r) == DSP_ERR_STATE, "no ruling yet");
    CHECK(proof(id, DSP_BUYER, 2, TERMS) == DSP_OK, "non-delivery attestation decides");
    const dsp_case_t *c = dsp_case(&D, id);
    CHECK(c->state == DSP_ST_FINAL && c->award_bps == 10000 && c->level == 1, "final at level 1");
    CHECK(dsp_ruling(&D, id, &r) == DSP_OK && r.award_bps == 10000 && r.level == 1 &&
              r.round == 0 && r.loser == DSP_SELLER && r.loser_id == SELLER,
          "ruling: seller lost");
    CHECK(proof(id, DSP_SELLER, 1, TERMS) == DSP_ERR_STATE, "no proofs after final");
    CHECK(dsp_escalate(&D, id, DSP_SELLER, 0) == DSP_ERR_STATE, "no escalation after final");
    CHECK(D.locked_total == 0 && audit(), "no bonds at level 1");
    uint32_t id2 = open_case(2, 0);
    CHECK(proof(id2, DSP_SELLER, 1, TERMS) == DSP_OK && dsp_ruling(&D, id2, &r) == DSP_OK &&
              r.award_bps == 0 && r.loser == DSP_BUYER && r.loser_id == BUYER,
          "delivery proof decides for the seller");
    /* no predicate */
    D.pred = NULL;
    uint32_t id3 = open_case(3, 0);
    CHECK(proof(id3, DSP_SELLER, 1, TERMS) == DSP_PENDING, "no predicate: never decides");
    /* digest */
    uint8_t d1[32], d2[32];
    CHECK(dsp_ruling_digest(&D, id, NULL) == DSP_ERR_ARG &&
              dsp_ruling_digest(&D, id3, d1) == DSP_ERR_STATE &&
              dsp_ruling_digest(&D, 99, d1) == DSP_ERR_UNKNOWN,
          "digest refusals");
    CHECK(dsp_ruling(&D, id, NULL) == DSP_ERR_ARG && dsp_ruling(&D, 99, &r) == DSP_ERR_UNKNOWN,
          "ruling refusals");
    CHECK(dsp_ruling_digest(&D, id, d1) == DSP_OK && dsp_ruling_digest(&D, id2, d2) == DSP_OK &&
              memcmp(d1, d2, 32) != 0,
          "digests differ");
    uint8_t mm[63], e[32];
    memcpy(mm, "zxv/dispute-ruling/v1", 21);
    mm[21] = (uint8_t) id;
    mm[22] = mm[23] = mm[24] = 0;
    memcpy(mm + 25, dsp_case(&D, id)->order, 32);
    uint32_t aw = 10000;
    memcpy(mm + 57, &aw, 4);
    mm[61] = 1;
    mm[62] = 0;
    sha256(mm, sizeof mm, e);
    CHECK(memcmp(e, d1, 32) == 0, "digest layout");
}

/* check the jury against the reference draw */
static void test_level2_flow(void)
{
    setup(NULL, NJ);
    cred_t CR;
    cred_init(&CR, NULL);
    cred_subject_add(&CR, BUYER, true);
    cred_subject_add(&CR, SELLER, false);
    cred_subject_add(&CR, 3, false);
    uint8_t rc[32];
    memset(rc, 9, 32);
    cred_fulfil(&CR, BUYER, SELLER, 100000, rc);
    uint64_t before = cred_local(&CR, BUYER, SELLER);

    uint32_t id = open_case(1, 0);
    uint64_t b0 = deb(bacct), s0 = deb(sacct);
    CHECK(dsp_escalate(&D, id, 0, 10) == DSP_ERR_ARG &&
              dsp_escalate(&D, 99, DSP_BUYER, 10) == DSP_ERR_UNKNOWN,
          "escalate refusals");
    CHECK(dsp_select(&D, id, NULL, 0, rc, 10) == DSP_ERR_STATE, "select before escalation");
    CHECK(dsp_escalate(&D, id, DSP_BUYER, 10) == DSP_OK, "buyer escalates to level 2");
    const dsp_case_t *c = dsp_case(&D, id);
    CHECK(c->level == 2 && c->round == 1 && c->state == DSP_ST_SELECT &&
              c->appellant == DSP_BUYER && c->appeal_bond == 1000 && c->appeal_live &&
              deb(bacct) == b0 - 1000 && deb(escrow) == 1000,
          "1x bond locked");
    CHECK(dsp_case_locked(&D, id) == 1000 && dsp_case_locked(&D, 77) == 0 && audit(), "audit");
    CHECK(dsp_escalate(&D, id, DSP_SELLER, 10) == DSP_ERR_STATE, "no double escalation");
    CHECK(proof(id, DSP_SELLER, 1, TERMS) == DSP_ERR_STATE, "no proofs at level 2");
    /* VRF */
    uint8_t seed[32], out[32], pr[VRF_PROOF_LEN];
    dsp_vrf_seed(&D, id, seed);
    vrf_eval(seed, out, pr);
    CHECK(dsp_select(&D, id, pr, VRF_PROOF_LEN, NULL, 11) == DSP_ERR_ARG, "NULL out");
    CHECK(dsp_select(&D, 99, pr, VRF_PROOF_LEN, out, 11) == DSP_ERR_UNKNOWN, "unknown");
    out[0] ^= 1;
    CHECK(dsp_select(&D, id, pr, VRF_PROOF_LEN, out, 11) == DSP_ERR_VRF,
          "wrong VRF output refused");
    out[0] ^= 1;
    uint8_t opr[VRF_PROOF_LEN], oout[32];
    CHECK(vrf_dsp_out(OSK, seed, opr, oout), "other key proves");
    CHECK(dsp_select(&D, id, opr, VRF_PROOF_LEN, oout, 11) == DSP_ERR_VRF, "wrong VRF key refused");
    CHECK(dsp_select(&D, id, opr, VRF_PROOF_LEN, out, 11) == DSP_ERR_VRF,
          "other key's proof with the right output refused");
    CHECK(dsp_select(&D, id, pr, VRF_PROOF_LEN - 1, out, 11) == DSP_ERR_VRF, "short proof refused");
    pr[60] ^= 1;
    CHECK(dsp_select(&D, id, pr, VRF_PROOF_LEN, out, 11) == DSP_ERR_VRF, "tampered proof refused");
    pr[60] ^= 1;
    CHECK(dsp_select(&D, id, pr, VRF_PROOF_LEN, out, UINT64_MAX - 5) == DSP_ERR_OVERFLOW,
          "deadline overflow");
    CHECK(c->state == DSP_ST_SELECT && D.locked_total == 1000, "refused selects left nothing");
    /* reference draw */
    uint64_t w[DSP_MAX_POOL];
    bool el[DSP_MAX_POOL];
    for (uint32_t k = 0; k < D.n_pool; k++) {
        w[k] = D.pool[k].stake;
        el[k] = true;
    }
    uint8_t seat[DSP_MAX_JURY];
    ref_select(out, w, el, D.n_pool, 3, seat);
    CHECK(dsp_select(&D, id, pr, VRF_PROOF_LEN, out, 100) == DSP_OK && c->state == DSP_ST_VOTE &&
              c->n_jury == 3,
          "jury of 3 drawn");
    CHECK(memcmp(seat, c->seat, 3) == 0, "draw matches the reference");
    CHECK(c->commit_deadline == 100 + 259200 && c->reveal_deadline == 100 + 259200 + 172800,
          "deadlines");
    CHECK(D.locked_total == 1000 + 1500 && deb(escrow) == 2500 && dsp_case_locked(&D, id) == 2500 &&
              audit(),
          "juror bonds locked");
    CHECK(c->used == ((1u << seat[0]) | (1u << seat[1]) | (1u << seat[2])), "used mask");
    CHECK(dsp_select(&D, id, pr, VRF_PROOF_LEN, out, 100) == DSP_ERR_STATE, "no second select");
    uint32_t j0 = seat_id(id, 0), j1 = seat_id(id, 1), j2 = seat_id(id, 2);
    /* commits */
    uint8_t cm[32], sig[32], m[DSP_COMMIT_MSG], salt[32];
    salt_of(j0, id, salt);
    dsp_vote_commit(id, j0, DSP_BUYER, salt, cm);
    dsp_commit_msg(id, 1, j0, cm, m);
    CHECK(memcmp(m, "zxv/dispute-commit/v1", 21) == 0 && m[21] == (uint8_t) id && m[25] == 1 &&
              memcmp(m + 30, cm, 32) == 0,
          "commit message layout");
    sign(j1, m, sizeof m, sig);
    CHECK(dsp_commit(&D, id, j0, cm, sig, 32, 200) == DSP_ERR_SIG,
          "commit signed by another juror");
    CHECK(dsp_commit(&D, id, j0, NULL, sig, 32, 200) == DSP_ERR_ARG, "NULL commit");
    CHECK(dsp_commit(&D, 99, j0, cm, sig, 32, 200) == DSP_ERR_UNKNOWN, "unknown case");
    CHECK(dsp_commit(&D, id, 4242, cm, sig, 32, 200) == DSP_ERR_PARTY, "not on the jury");
    CHECK(do_commit(id, j0, DSP_BUYER, c->commit_deadline) == DSP_ERR_DEADLINE,
          "commit at deadline");
    CHECK(do_commit(id, j0, DSP_BUYER, 200) == DSP_OK && c->committed[0], "j0 commits");
    CHECK(do_commit(id, j0, DSP_SELLER, 201) == DSP_ERR_DUP, "no re-commit");
    CHECK(do_reveal(id, j0, DSP_BUYER, 300) == DSP_ERR_DEADLINE, "reveal before commit deadline");
    CHECK(dsp_tally(&D, id, 300) == DSP_ERR_DEADLINE, "tally too early");
    CHECK(do_commit(id, j1, DSP_BUYER, 300) == DSP_OK &&
              do_commit(id, j2, DSP_SELLER, 301) == DSP_OK,
          "all committed");
    CHECK(do_reveal(id, j0, DSP_BUYER, 302) == DSP_OK, "reveal opens once everyone committed");
    CHECK(do_reveal(id, j0, DSP_BUYER, 303) == DSP_ERR_DUP, "no re-reveal");
    CHECK(do_reveal(id, j1, DSP_SELLER, 303) == DSP_ERR_COMMIT, "a vote other than committed");
    salt_of(j1, id, salt);
    salt[31] ^= 1;
    CHECK(dsp_reveal(&D, id, j1, DSP_BUYER, salt, 303) == DSP_ERR_COMMIT, "wrong salt");
    CHECK(dsp_reveal(&D, id, j1, 0, salt, 303) == DSP_ERR_ARG &&
              dsp_reveal(&D, id, j1, 3, salt, 303) == DSP_ERR_ARG &&
              dsp_reveal(&D, id, j1, 1, NULL, 303) == DSP_ERR_ARG,
          "reveal args");
    CHECK(dsp_reveal(&D, 99, j1, 1, salt, 303) == DSP_ERR_UNKNOWN &&
              do_reveal(id, 4242, 1, 303) == DSP_ERR_PARTY,
          "reveal unknowns");
    CHECK(do_reveal(id, j1, DSP_BUYER, c->reveal_deadline) == DSP_ERR_DEADLINE,
          "reveal at deadline");
    CHECK(do_reveal(id, j1, DSP_BUYER, 304) == DSP_OK &&
              do_reveal(id, j2, DSP_SELLER, 305) == DSP_OK,
          "all revealed");
    uint64_t r0 = deb(reserve), a0 = deb(jacct[j0 - 100]), a2 = deb(jacct[j2 - 100]);
    CHECK(dsp_tally(&D, 99, 306) == DSP_ERR_UNKNOWN, "tally unknown");
    CHECK(dsp_tally(&D, id, 306) == DSP_OK, "tally (everyone revealed: no wait)");
    CHECK(c->state == DSP_ST_APPEAL && c->winner == DSP_BUYER && c->award_bps == 10000 &&
              c->appeal_deadline == 306 + 259200,
          "buyer wins round 1 two to one");
    CHECK(deb(reserve) == r0 + 500 && deb(jacct[j0 - 100]) == a0 + 500 &&
              deb(jacct[j2 - 100]) == a2,
          "minority slashed to the reserve, majority returned (no reward)");
    CHECK(deb(bacct) == b0 && !c->appeal_live && deb(escrow) == 0 && audit(),
          "winning appellant's bond returned");
    CHECK(dsp_tally(&D, id, 307) == DSP_ERR_STATE, "no second tally");
    /* appeals */
    CHECK(dsp_escalate(&D, id, DSP_BUYER, 400) == DSP_ERR_PARTY, "the winner cannot appeal");
    CHECK(dsp_escalate(&D, id, DSP_SELLER, c->appeal_deadline) == DSP_ERR_DEADLINE,
          "appeal too late");
    CHECK(dsp_finalize(&D, id, c->appeal_deadline - 1) == DSP_ERR_DEADLINE, "finalize too early");
    CHECK(dsp_escalate(&D, id, DSP_SELLER, 400) == DSP_OK && c->round == 2 &&
              c->appeal_bond == 4000 && deb(sacct) == s0 - 4000 && c->appellant == DSP_SELLER &&
              c->winner == DSP_NONE,
          "seller appeals with 4x");
    CHECK(do_select(id, 500) == DSP_OK && c->n_jury == 7, "jury of 7");
    bool disjoint = true;
    for (uint32_t d = 0; d < 7; d++)
        disjoint =
            disjoint && c->seat[d] != seat[0] && c->seat[d] != seat[1] && c->seat[d] != seat[2];
    CHECK(disjoint, "fresh jurors for the appeal");
    CHECK(D.locked_total - D.returned_total - D.slashed_total == 4000 + 3500 && audit(), "audit");
    /* 4 for seller, 2 buyer, 1 silent */
    for (uint32_t d = 0; d < 6; d++)
        do_commit(id, seat_id(id, d), d < 4 ? DSP_SELLER : DSP_BUYER, 600);
    CHECK(do_reveal(id, seat_id(id, 0), DSP_SELLER, 601) == DSP_ERR_DEADLINE,
          "not all committed: wait");
    uint64_t t = c->commit_deadline;
    for (uint32_t d = 0; d < 6; d++)
        do_reveal(id, seat_id(id, d), d < 4 ? DSP_SELLER : DSP_BUYER, t);
    CHECK(dsp_tally(&D, id, t) == DSP_ERR_DEADLINE, "one missing: wait for the deadline");
    r0 = deb(reserve);
    CHECK(dsp_tally(&D, id, c->reveal_deadline) == DSP_OK && c->winner == DSP_SELLER &&
              c->award_bps == 0,
          "seller wins round 2");
    CHECK(deb(reserve) == r0 + 3 * 500 && deb(sacct) == s0 && audit(),
          "2 minority + 1 silent slashed; appellant seller's 4x returned");
    /* buyer appeals with 16x and loses */
    uint64_t tb = c->appeal_deadline - 1;
    CHECK(dsp_escalate(&D, id, DSP_BUYER, tb) == DSP_OK && c->round == 3 &&
              c->appeal_bond == 16000 && deb(bacct) == b0 - 16000,
          "16x");
    CHECK(do_select(id, tb) == DSP_OK && c->n_jury == 15, "jury of 15 (25 of the 28 used)");
    for (uint32_t d = 0; d < 15; d++)
        do_commit(id, seat_id(id, d), d < 8 ? DSP_SELLER : DSP_BUYER, tb);
    for (uint32_t d = 0; d < 15; d++)
        do_reveal(id, seat_id(id, d), d < 8 ? DSP_SELLER : DSP_BUYER, tb);
    r0 = deb(reserve);
    CHECK(dsp_tally(&D, id, tb) == DSP_OK && c->winner == DSP_SELLER, "seller wins 8-7");
    CHECK(deb(reserve) == r0 + 7 * 500 + 16000 && deb(bacct) == b0 - 16000 && audit(),
          "failed 16x appeal slashed with the 7 minority bonds");
    /* to level 3 */
    CHECK(dsp_escalate(&D, id, DSP_BUYER, c->appeal_deadline - 1) == DSP_OK && c->level == 3 &&
              c->state == DSP_ST_GATEWAY && deb(bacct) == b0 - 16000,
          "after round 3: level 3, no bond");
    CHECK(dsp_escalate(&D, id, DSP_SELLER, 0) == DSP_ERR_STATE &&
              dsp_finalize(&D, id, UINT64_MAX) == DSP_ERR_STATE,
          "gateway pending");
    uint8_t gm[DSP_GW_MSG], gs[32];
    dsp_gw_msg(id, 6000, gm);
    CHECK(memcmp(gm, "zxv/dispute-gw/v1", 17) == 0 && gm[17] == (uint8_t) id &&
              gm[21] == (6000 & 255) && gm[22] == (6000 >> 8),
          "gateway message layout");
    sign(GW, gm, sizeof gm, gs);
    CHECK(dsp_gateway_rule(&D, id, 10001, gs, 32) == DSP_ERR_ARG, "award above 10000");
    CHECK(dsp_gateway_rule(&D, 99, 6000, gs, 32) == DSP_ERR_UNKNOWN, "unknown");
    CHECK(dsp_gateway_rule(&D, id, 6001, gs, 32) == DSP_ERR_SIG, "award not what was signed");
    sign(BUYER, gm, sizeof gm, gs);
    CHECK(dsp_gateway_rule(&D, id, 6000, gs, 32) == DSP_ERR_SIG, "not the gateway's key");
    sign(GW, gm, sizeof gm, gs);
    CHECK(dsp_gateway_rule(&D, id, 6000, gs, 32) == DSP_OK && c->state == DSP_ST_FINAL,
          "gateway rules");
    CHECK(dsp_gateway_rule(&D, id, 6000, gs, 32) == DSP_ERR_STATE, "once");
    dsp_ruling_t r;
    CHECK(dsp_ruling(&D, id, &r) == DSP_OK && r.award_bps == 6000 && r.level == 3 && r.round == 0 &&
              r.loser == DSP_SELLER && r.loser_id == SELLER,
          "final: level 3, 60% to the buyer, seller lost");
    uint64_t ab, as;
    CHECK(dsp_award_amount(c->escrow, r.award_bps, &ab, &as) && ab == 30000 && as == 20000,
          "amounts");
    CHECK(D.locked_total == D.returned_total + D.slashed_total && deb(escrow) == 0 && audit(),
          "every bond returned or slashed at close");
    /* wire the loser into cred */
    uint8_t dg[32];
    dsp_ruling_digest(&D, id, dg);
    CHECK(cred_fail(&CR, BUYER, r.loser_id, dg) == CRED_OK &&
              cred_local(&CR, BUYER, SELLER) == before - 16,
          "loser recorded via cred_fail (ruling digest as the ruling id)");
    CHECK(cred_fail(&CR, BUYER, r.loser_id, dg) == CRED_ERR_DUP, "the same ruling only once");
}

static void test_ties(void)
{
    setup(NULL, NJ);
    uint32_t id = open_case(1, 0);
    dsp_escalate(&D, id, DSP_SELLER, 0);
    do_select(id, 0);
    const dsp_case_t *c = dsp_case(&D, id);
    uint32_t j0 = seat_id(id, 0), j1 = seat_id(id, 1);
    do_commit(id, j0, DSP_BUYER, 1);
    do_commit(id, j1, DSP_SELLER, 1);
    do_reveal(id, j0, DSP_BUYER, c->commit_deadline);
    do_reveal(id, j1, DSP_SELLER, c->commit_deadline);
    uint64_t r0 = deb(reserve), s0 = deb(sacct);
    CHECK(dsp_tally(&D, id, c->reveal_deadline) == DSP_TIE, "1-1 with one silent: tie");
    CHECK(deb(reserve) == r0 + 500 && deb(sacct) == s0 + 1000 && audit(),
          "silent juror slashed; revealers and the appeal bond returned");
    CHECK(c->state == DSP_ST_SELECT && c->round == 2 && !c->appeal_live &&
              c->appellant == DSP_NONE && D.locked_total - D.returned_total - D.slashed_total == 0,
          "next round at no bond cost");
    CHECK(c->appeal_bond == 0 && c->n_jury == 0 && dsp_case_locked(&D, id) == 0, "round reset");
    CHECK(do_reveal(id, j0, DSP_BUYER, 1) == DSP_ERR_STATE &&
              do_commit(id, j0, DSP_BUYER, 1) == DSP_ERR_STATE,
          "no votes while the jury is being drawn");
    CHECK(do_select(id, 10) == DSP_OK && c->n_jury == 7, "7 drawn");
    r0 = deb(reserve);
    CHECK(dsp_tally(&D, id, c->reveal_deadline) == DSP_TIE && deb(reserve) == r0 + 7 * 500,
          "nobody voted: tie, all slashed");
    CHECK(do_select(id, 10) == DSP_OK && c->n_jury == 15 && c->round == 3, "round 3");
    for (uint32_t d = 0; d < 15; d++)
        do_commit(id, seat_id(id, d), d & 1 ? DSP_BUYER : DSP_SELLER, 11);
    for (uint32_t d = 0; d < 14; d++)
        do_reveal(id, seat_id(id, d), d & 1 ? DSP_BUYER : DSP_SELLER, 12);
    r0 = deb(reserve);
    CHECK(dsp_tally(&D, id, c->reveal_deadline) == DSP_TIE && c->state == DSP_ST_GATEWAY &&
              c->level == 3,
          "tie in round 3: level 3");
    CHECK(deb(reserve) == r0 + 500 && audit(), "only the silent one slashed");
    uint8_t gm[DSP_GW_MSG], gs[32];
    dsp_gw_msg(id, 5000, gm);
    sign(GW, gm, sizeof gm, gs);
    dsp_ruling_t r;
    CHECK(dsp_gateway_rule(&D, id, 5000, gs, 32) == DSP_OK && dsp_ruling(&D, id, &r) == DSP_OK &&
              r.loser == DSP_NONE && r.loser_id == 0 && r.award_bps == 5000,
          "exact half: no loser");
    id = open_case(2, 0);
    dsp_escalate(&D, id, DSP_SELLER, 0);
    c = dsp_case(&D, id);
    CHECK(do_select(id, 0) == DSP_OK && c->n_jury == 3,
          "a new case draws from the whole pool again");
}

static void test_pool_rules(void)
{
    dsp_config_t f;
    dsp_config_default(&f);
    setup(&f, 5);
    uint32_t id = open_case(1, 0);
    dsp_escalate(&D, id, DSP_BUYER, 0);
    const dsp_case_t *c = dsp_case(&D, id);
    /* make three ineligible: one is the buyer's id, one shares the seller's account, one broke */
    D.pool[0].id = BUYER;
    D.pool[1].acct = sacct;
    pay_receipt_t rc;
    pay_ledger_transfer(&L, mk(), jacct[2], spare, deb(jacct[2]) - 499, &rc);
    CHECK(do_select(id, 0) == DSP_ERR_POOL && c->state == DSP_ST_SELECT && audit(),
          "2 eligible < 3");
    pay_ledger_transfer(&L, mk(), spare, jacct[2], 1, &rc);
    CHECK(do_select(id, 0) == DSP_OK, "exactly the bond: eligible, 3 of 3");
    bool none = true;
    for (uint32_t d = 0; d < 3; d++) none = none && c->seat[d] != 0 && c->seat[d] != 1;
    CHECK(none && deb(jacct[2]) == 0, "the buyer's id and the seller's account never sit");
    CHECK(c->n_jury == 3 && audit(), "drawn");
    /* bonds for a broke juror: shared account that cannot cover two */
    setup(&f, 4);
    id = open_case(1, 0);
    dsp_escalate(&D, id, DSP_BUYER, 0);
    c = dsp_case(&D, id);
    for (uint32_t k = 0; k < 4; k++) D.pool[k].acct = jacct[0];
    pay_ledger_transfer(&L, mk(), jacct[0], spare, deb(jacct[0]) - 1200, &rc);
    uint64_t before = deb(jacct[0]);
    CHECK(do_select(id, 0) == DSP_ERR_LEDGER && D.last_ledger == PAY_ERR_FUNDS,
          "third bond refused");
    CHECK(deb(jacct[0]) == before && c->state == DSP_ST_SELECT && c->n_jury == 0 && audit(),
          "the two locked bonds were returned");
    CHECK(D.locked_total == 1000 + 1000 && D.returned_total == 1000, "counters");
    pay_ledger_transfer(&L, mk(), spare, jacct[0], 300, &rc);
    CHECK(do_select(id, 0) == DSP_OK && deb(jacct[0]) == 0 && audit(), "funded: drawn");
    /* frozen escrow: tally resumes */
    for (uint32_t d = 0; d < 3; d++) do_commit(id, seat_id(id, d), d ? DSP_BUYER : DSP_SELLER, 1);
    for (uint32_t d = 0; d < 3; d++) do_reveal(id, seat_id(id, d), d ? DSP_BUYER : DSP_SELLER, 2);
    pay_ledger_set_flags(&L, escrow, PAY_ACCT_FROZEN);
    uint64_t l0 = D.slashed_total + D.returned_total;
    CHECK(dsp_tally(&D, id, 3) == DSP_ERR_LEDGER && c->state == DSP_ST_VOTE && !c->settled[0] &&
              D.slashed_total + D.returned_total == l0 && audit(),
          "frozen escrow: nothing settles");
    pay_ledger_set_flags(&L, escrow, 0);
    uint32_t iss2 = open_acct(usd, 8, PAY_ACCT_ISSUER);
    uint64_t top = PAY_BAL_MAX - 1 - deb(bacct);
    for (uint64_t left = top; left;) {
        uint64_t x = left < ((uint64_t) 1 << 59) ? left : (uint64_t) 1 << 59;
        pay_ledger_issue(&L, mk(), iss2, bacct, x, &rc);
        left -= x;
    }
    CHECK(dsp_tally(&D, id, 3) == DSP_ERR_LEDGER && D.last_ledger == PAY_ERR_OVERFLOW &&
              c->settled[0] && c->settled[1] && c->settled[2] && c->appeal_live &&
              c->state == DSP_ST_VOTE && dsp_case_locked(&D, id) == 1000 && audit(),
          "jurors settled, the appeal bond return refused: partial progress kept");
    for (uint64_t left = top; left;) {
        uint64_t x = left < ((uint64_t) 1 << 59) ? left : (uint64_t) 1 << 59;
        pay_ledger_redeem(&L, mk(), bacct, iss2, x, &rc);
        left -= x;
    }
    CHECK(audit(), "audit with the bond still live");
    uint64_t b1 = deb(bacct);
    CHECK(dsp_tally(&D, id, 3) == DSP_OK && c->winner == DSP_BUYER && !c->appeal_live &&
              deb(bacct) == b1 + 1000 && audit(),
          "resumed: only the appeal bond posted");
    setup(NULL, 4);
    id = open_case(1, 0);
    dsp_escalate(&D, id, DSP_BUYER, 0);
    do_select(id, 0);
    c = dsp_case(&D, id);
    L.acct[reserve].active = false;
    CHECK(dsp_tally(&D, id, c->reveal_deadline) == DSP_ERR_LEDGER && !c->settled[0],
          "slash refused");
    L.acct[reserve].active = true;
    CHECK(dsp_tally(&D, id, c->reveal_deadline) == DSP_TIE && audit(), "slash resumed");
    /* cred-weighted: a zero-weight juror never sits */
    f.cred_weighted = true;
    setup(&f, 4);
    dsp_juror_set_cred(&D, 101, 0);
    id = open_case(1, 0);
    dsp_escalate(&D, id, DSP_BUYER, 0);
    c = dsp_case(&D, id);
    CHECK(do_select(id, 0) == DSP_OK, "drawn");
    none = true;
    for (uint32_t d = 0; d < 3; d++) none = none && c->seat[d] != 1;
    CHECK(none, "cred 0 never drawn");
    /* removed juror */
    setup(NULL, 4);
    dsp_juror_remove(&D, 100);
    id = open_case(1, 0);
    dsp_escalate(&D, id, DSP_BUYER, 0);
    c = dsp_case(&D, id);
    CHECK(do_select(id, 0) == DSP_OK && c->seat[0] != 0 && c->seat[1] != 0 && c->seat[2] != 0,
          "inactive juror never drawn");
    /* escalation refused by the ledger */
    setup(NULL, 4);
    id = open_case(1, 0);
    pay_ledger_set_flags(&L, bacct, PAY_ACCT_FROZEN);
    CHECK(dsp_escalate(&D, id, DSP_BUYER, 0) == DSP_ERR_LEDGER &&
              dsp_case(&D, id)->state == DSP_ST_PROOFS && D.locked_total == 0 && audit(),
          "frozen appellant: no escalation");
    pay_ledger_set_flags(&L, bacct, 0);
    /* tally overflow */
    CHECK(dsp_escalate(&D, id, DSP_BUYER, 0) == DSP_OK && do_select(id, 0) == DSP_OK, "select");
    c = dsp_case(&D, id);
    CHECK(dsp_tally(&D, id, UINT64_MAX) == DSP_ERR_OVERFLOW && c->state == DSP_ST_VOTE,
          "appeal deadline overflow");
    CHECK(dsp_tally(&D, id, UINT64_MAX - 259200) == DSP_TIE, "largest tick that fits");
    /* finalize */
    setup(NULL, 4);
    id = open_case(1, 0);
    dsp_escalate(&D, id, DSP_BUYER, 0);
    do_select(id, 0);
    c = dsp_case(&D, id);
    for (uint32_t d = 0; d < 3; d++) do_commit(id, seat_id(id, d), DSP_SELLER, 1);
    for (uint32_t d = 0; d < 3; d++) do_reveal(id, seat_id(id, d), DSP_SELLER, 2);
    uint64_t b0 = deb(bacct), r0 = deb(reserve);
    CHECK(dsp_tally(&D, id, 3) == DSP_OK && c->winner == DSP_SELLER && deb(bacct) == b0 &&
              deb(reserve) == r0 + 1000,
          "failed 1x escalation slashed");
    CHECK(dsp_finalize(&D, 99, 0) == DSP_ERR_UNKNOWN, "finalize unknown");
    CHECK(dsp_finalize(&D, id, c->appeal_deadline) == DSP_OK && c->state == DSP_ST_FINAL,
          "final at deadline");
    CHECK(dsp_finalize(&D, id, c->appeal_deadline) == DSP_ERR_STATE, "once");
    dsp_ruling_t r;
    CHECK(dsp_ruling(&D, id, &r) == DSP_OK && r.level == 2 && r.round == 1 && r.award_bps == 0 &&
              r.loser == DSP_BUYER && r.loser_id == BUYER,
          "level 2 round 1 ruling");
    CHECK(dsp_escalate(&D, id, DSP_BUYER, 0) == DSP_ERR_STATE, "no appeal after final");
    CHECK(do_reveal(id, 100, 1, 0) == DSP_ERR_STATE &&
              dsp_tally(&D, id, UINT64_MAX) == DSP_ERR_STATE,
          "no votes after final");
}

static void test_more(void)
{
    /* the seller's id and the buyer's account are excluded too */
    setup(NULL, 6);
    uint32_t id = open_case(1, 0);
    dsp_escalate(&D, id, DSP_SELLER, 0);
    const dsp_case_t *c = dsp_case(&D, id);
    CHECK(c->n_jury == 0 && c->jbond == 0, "SELECT: no jury yet");
    D.pool[0].id = SELLER;
    D.pool[1].acct = bacct;
    D.pool[2].active = false;
    CHECK(do_select(id, 0) == DSP_OK, "3 eligible");
    CHECK(c->seat[0] >= 3 && c->seat[1] >= 3 && c->seat[2] >= 3,
          "seller id, buyer account, inactive excluded");
    uint32_t j0 = seat_id(id, 0), j1 = seat_id(id, 1), j2 = seat_id(id, 2);
    do_commit(id, j0, DSP_BUYER, 1);
    do_commit(id, j1, DSP_BUYER, 1);
    CHECK(do_reveal(id, j2, DSP_BUYER, c->commit_deadline) == DSP_ERR_STATE,
          "never committed: cannot reveal");
    CHECK(do_reveal(id, j0, DSP_BUYER, c->reveal_deadline - 1) == DSP_OK,
          "last tick of the reveal window");
    CHECK(dsp_tally(&D, id, c->reveal_deadline - 1) == DSP_ERR_DEADLINE, "one unrevealed: wait");
    uint64_t r0 = deb(reserve);
    CHECK(dsp_tally(&D, id, c->reveal_deadline) == DSP_OK && c->winner == DSP_BUYER &&
              deb(reserve) == r0 + 500 + 500 + 1000 && audit(),
          "1-0 for the buyer; the silent two and the seller's 1x slashed");
    /* gateway thresholds (white box: put cases at level 3) */
    uint8_t gm[DSP_GW_MSG], gs[32];
    const uint32_t aw[5] = {0, 4999, 5000, 5001, 10000};
    const uint8_t lo[5] = {DSP_BUYER, DSP_BUYER, DSP_NONE, DSP_SELLER, DSP_SELLER};
    bool ok = true;
    for (int i = 0; i < 5; i++) {
        uint32_t g = open_case(10 + (uint32_t) i, 0);
        dsp_case_t *w = (dsp_case_t *) dsp_case(&D, g);
        w->state = DSP_ST_GATEWAY;
        w->level = 3;
        w->round = 3;
        dsp_gw_msg(g, aw[i], gm);
        sign(GW, gm, sizeof gm, gs);
        dsp_ruling_t r;
        ok = ok && dsp_gateway_rule(&D, g, aw[i], gs, 32) == DSP_OK &&
             dsp_ruling(&D, g, &r) == DSP_OK && r.loser == lo[i] && r.award_bps == aw[i] &&
             r.level == 3 && r.round == 0 &&
             r.loser_id == (lo[i] == DSP_BUYER    ? BUYER
                            : lo[i] == DSP_SELLER ? SELLER
                                                  : 0);
        dsp_release(&D, g);
    }
    CHECK(ok, "gateway award -> loser at 0, 4999, 5000, 5001, 10000");
    /* huge stakes: rejection sampling and power-of-two totals, against the reference */
    int bad = 0, rejected = 0;
    for (int pass_ = 0; pass_ < 2; pass_++) {
        dsp_config_t f;
        dsp_config_default(&f);
        f.jury[0] = 15;
        f.jury[1] = 15;
        f.jury[2] = 1;
        ledger_setup();
        dsp_init(&D, &L, NULL, escrow, reserve, GW, verify, vrf_dsp_verify, pred, &VCTX, SEED);
        D.cfg.jury[0] = 15;
        uint32_t nj = pass_ ? 16 : NJ;
        for (uint32_t k = 0; k < nj; k++)
            dsp_juror_add(&D, 100 + k, jacct[k], pass_ ? DSP_STAKE_MAX : DSP_STAKE_MAX - k * 12345);
        for (uint32_t n = 0; n < 40; n++) {
            uint32_t g = open_case(100 + n, 0);
            dsp_escalate(&D, g, DSP_BUYER, 0);
            uint8_t seed[32], out[32], pr[VRF_PROOF_LEN], seat[DSP_MAX_JURY];
            uint64_t w[DSP_MAX_POOL];
            bool el[DSP_MAX_POOL];
            for (uint32_t k = 0; k < nj; k++) {
                w[k] = D.pool[k].stake;
                el[k] = true;
            }
            dsp_vrf_seed(&D, g, seed);
            vrf_eval(seed, out, pr);
            uint32_t ctr = ref_select(out, w, el, nj, 15, seat);
            rejected += ctr > 15;
            dsp_status_t st = dsp_select(&D, g, pr, VRF_PROOF_LEN, out, 0);
            bad += st != DSP_OK || memcmp(seat, dsp_case(&D, g)->seat, 15) != 0;
            dsp_case_t *w2 = (dsp_case_t *) dsp_case(&D, g);
            for (uint32_t d = 0; d < 15; d++) w2->revealed[d] = false;
            dsp_tally(&D, g, UINT64_MAX - 259200);
            if (w2->state == DSP_ST_SELECT) w2->state = DSP_ST_FINAL;
            w2->appeal_live = false;
            dsp_release(&D, g);
            if (!pass_ && n >= 5 && rejected > 1) break; /* until rejections were seen */
        }
    }
    CHECK(bad == 0, "huge stakes: same draws as the reference");
    CHECK(rejected > 0, "rejection sampling exercised");
    /* a full pool of 32 */
    setup(NULL, 0);
    for (uint32_t k = 0; k < DSP_MAX_POOL; k++) dsp_juror_add(&D, 300 + k, jacct[k % NJ], 1 + k);
    uint32_t g = open_case(7, 0);
    dsp_escalate(&D, g, DSP_BUYER, 0);
    CHECK(D.n_pool == DSP_MAX_POOL && do_select(g, 0) == DSP_OK && audit(),
          "select from a full pool");
}

static void test_more2(void)
{
    setup(NULL, 6);
    uint32_t id = 0;
    order_id(1);
    CHECK(dsp_open(&D, ORD, TERMS, BUYER, SELLER, bacct, sacct, 1, 0, NULL) == DSP_OK,
          "open, no id out");
    ORD[31] ^= 1;
    CHECK(dsp_open(&D, ORD, TERMS, BUYER, SELLER, bacct, sacct, 1, 0, &id) == DSP_OK && id == 2,
          "orders differing in the last byte are different orders");
    /* the posting identifiers are derived from the seed and the counter */
    CHECK(dsp_escalate(&D, id, DSP_BUYER, 5) == DSP_OK &&
              dsp_escalate(&D, 1, DSP_SELLER, 6) == DSP_OK && D.seq == 2,
          "two postings");
    bool ok = true;
    for (uint64_t q = 0; q < 2; q++) {
        uint8_t in[41], ik[32], ud[32];
        char uetr[PAY_UETR_LEN + 1], e2e[16];
        memcpy(in, SEED, 32);
        memcpy(in + 33, &q, 8);
        in[32] = 'I';
        sha256(in, 41, ik);
        in[32] = 'U';
        sha256(in, 41, ud);
        pay_uetr_from_random(ud, uetr);
        snprintf(e2e, sizeof e2e, "DSP-%llu", (unsigned long long) q);
        const pay_journal_t *j = pay_ledger_find_uetr(&L, uetr);
        ok = ok && j && memcmp(j->idem_key, ik, 32) == 0 && strcmp(j->e2e, e2e) == 0 &&
             j->tick == 5 + q && j->kind == PAY_KIND_TRANSFER;
    }
    CHECK(ok, "idempotency key, UETR, e2e and tick as documented");
    /* not seated, commit tampering, all-committed rule */
    D.pool[0].id = SELLER; /* a party's id is never seated */
    uint32_t g = open_case(3, 0);
    dsp_escalate(&D, g, DSP_BUYER, 0);
    CHECK(do_select(g, 0) == DSP_OK, "drawn");
    const dsp_case_t *c = dsp_case(&D, g);
    CHECK(do_commit(g, SELLER, DSP_SELLER, 1) == DSP_ERR_PARTY, "an unseated id cannot commit");
    uint32_t j0 = seat_id(g, 0), j1 = seat_id(g, 1), j2 = seat_id(g, 2);
    do_commit(g, j1, DSP_BUYER, 1);
    do_commit(g, j2, DSP_BUYER, 1);
    CHECK(do_reveal(g, j1, DSP_BUYER, 2) == DSP_ERR_DEADLINE, "seat 0 has not committed: wait");
    uint8_t salt[32], cm[32], m[DSP_COMMIT_MSG], sig[32];
    salt_of(j0, g, salt);
    dsp_vote_commit(g, j0, DSP_BUYER, salt, cm);
    cm[31] ^= 1;
    dsp_commit_msg(g, c->round, j0, cm, m);
    sign(j0, m, sizeof m, sig);
    CHECK(dsp_commit(&D, g, j0, cm, sig, 32, 2) == DSP_OK, "a commit differing in its last byte");
    CHECK(do_reveal(g, j0, DSP_BUYER, 3) == DSP_ERR_COMMIT, "does not open with the real vote");
    CHECK(do_reveal(g, j1, DSP_BUYER, 3) == DSP_OK, "everyone committed: reveal opens");
}

static void test_award(void)
{
    uint64_t b = 1, s = 1;
    CHECK(dsp_award_amount(100, 10001, &b, &s) == false && b == 1, "award above 10000");
    CHECK(!dsp_award_amount(100, 1, NULL, &s) && !dsp_award_amount(100, 1, &b, NULL), "NULL");
    CHECK(dsp_award_amount(0, 5000, &b, &s) && b == 0 && s == 0, "zero escrow");
    CHECK(dsp_award_amount(3, 5000, &b, &s) && b == 1 && s == 2, "rounding unit to the seller");
    CHECK(dsp_award_amount(UINT64_MAX, 10000, &b, &s) && b == UINT64_MAX && s == 0, "max");
    CHECK(dsp_award_amount(UINT64_MAX, 1, &b, &s) && b == UINT64_MAX / 10000 && s == UINT64_MAX - b,
          "1 bps");
    CHECK(dsp_award_amount(12345, 0, &b, &s) && b == 0 && s == 12345, "0 bps");
}

static void test_messages(void)
{
    uint8_t salt[32], a[32], b[32], m[60];
    memset(salt, 0x44, 32);
    dsp_vote_commit(0x01020304, 0x0a0b0c0d, 2, salt, a);
    memcpy(m, "zxv/dispute-vote/v1", 19);
    m[19] = 4;
    m[20] = 3;
    m[21] = 2;
    m[22] = 1;
    m[23] = 0x0d;
    m[24] = 0x0c;
    m[25] = 0x0b;
    m[26] = 0x0a;
    m[27] = 2;
    memcpy(m + 28, salt, 32);
    sha256(m, 60, b);
    CHECK(memcmp(a, b, 32) == 0, "vote commit layout");
    dsp_vote_commit(0x01020304, 0x0a0b0c0d, 1, salt, b);
    CHECK(memcmp(a, b, 32) != 0, "vote changes the commit");
    /* seed */
    setup(NULL, 0);
    uint32_t id = open_case(5, 0);
    uint8_t sm[88], e[32];
    memcpy(sm, "zxv/dispute-seed/v1", 19);
    memcpy(sm + 19, SEED, 32);
    memcpy(sm + 51, &id, 4);
    sm[55] = 0;
    memcpy(sm + 56, ORD, 32);
    sha256(sm, 88, e);
    dsp_vrf_seed(&D, id, a);
    CHECK(memcmp(a, e, 32) == 0, "seed layout (round 0)");
    dsp_escalate(&D, id, DSP_BUYER, 0);
    dsp_vrf_seed(&D, id, b);
    sm[55] = 1;
    sha256(sm, 88, e);
    CHECK(memcmp(b, e, 32) == 0, "seed binds the round");
    memset(sm + 55, 0, 33);
    memcpy(sm + 51, (uint32_t[]){99}, 4);
    sha256(sm, 88, e);
    dsp_vrf_seed(&D, 99, a);
    CHECK(memcmp(a, e, 32) == 0, "unknown case: zeros");
    memset(sm + 19, 0, 32);
    sha256(sm, 88, e);
    dsp_vrf_seed(NULL, 99, a);
    CHECK(memcmp(a, e, 32) == 0, "NULL module: zero seed");
}

/* ---- randomized property loop ---- */
static uint64_t rs = 0x2545F4914F6CDD1Dull;
static uint64_t rnd(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 7;
    rs ^= rs << 17;
    return rs;
}

static void test_random(void)
{
    int bad_audit = 0, bad_slash = 0, bad_sel = 0, bad_final = 0, bad_reserve = 0;
    for (int round = 0; round < 25; round++) {
        dsp_config_t f;
        dsp_config_default(&f);
        f.cred_weighted = rnd() & 1;
        f.juror_bond = 1 + rnd() % 900;
        f.base_bond = 1 + rnd() % 5000;
        setup(&f, NJ);
        for (uint32_t k = 0; k < NJ; k++) {
            D.pool[k].stake = 1 + rnd() % (k & 1 ? 1000 : (uint64_t) 1 << 50);
            if (f.cred_weighted) dsp_juror_set_cred(&D, 100 + k, (uint32_t) (rnd() % 10001));
        }
        uint32_t id = open_case((uint32_t) round + 1, 0);
        uint64_t tick = 10;
        dsp_escalate(&D, id, rnd() & 1 ? DSP_BUYER : DSP_SELLER, tick);
        const dsp_case_t *c = dsp_case(&D, id);
        for (int step = 0; step < 6 && c->state != DSP_ST_FINAL && c->state != DSP_ST_GATEWAY;
             step++) {
            if (c->state == DSP_ST_SELECT) {
                uint64_t w[DSP_MAX_POOL];
                bool el[DSP_MAX_POOL];
                uint8_t seed[32], out[32], pr[VRF_PROOF_LEN], seat[DSP_MAX_JURY];
                uint32_t cnt = 0;
                for (uint32_t k = 0; k < D.n_pool; k++) {
                    const dsp_juror_t *j = &D.pool[k];
                    w[k] = f.cred_weighted
                               ? (uint64_t) ((unsigned __int128) j->stake * j->cred_bps / 10000)
                               : j->stake;
                    el[k] = w[k] && !(c->used & (1u << k)) && deb(j->acct) >= f.juror_bond;
                    cnt += el[k];
                }
                dsp_vrf_seed(&D, id, seed);
                vrf_eval(seed, out, pr);
                uint32_t J = f.jury[c->round - 1];
                dsp_status_t st = dsp_select(&D, id, pr, VRF_PROOF_LEN, out, tick);
                if (cnt < J) {
                    bad_sel += st != DSP_ERR_POOL;
                    break;
                }
                ref_select(out, w, el, D.n_pool, J, seat);
                bad_sel += st != DSP_OK || memcmp(seat, c->seat, J) != 0;
                if (st != DSP_OK) break;
            }
            bad_audit += !audit();
            uint32_t n = c->n_jury, nb = 0, ns = 0, silent = 0;
            uint8_t v[DSP_MAX_JURY];
            bool rev[DSP_MAX_JURY];
            for (uint32_t d = 0; d < n; d++) {
                v[d] = rnd() % 3 ? DSP_BUYER : DSP_SELLER;
                rev[d] = rnd() % 5 != 0;
                if (rnd() % 7)
                    do_commit(id, seat_id(id, d), v[d], tick + 1);
                else
                    rev[d] = false;
            }
            for (uint32_t d = 0; d < n; d++) {
                if (rev[d]) {
                    if (do_reveal(id, seat_id(id, d), v[d], c->commit_deadline) != DSP_OK)
                        bad_audit++;
                    nb += v[d] == DSP_BUYER;
                    ns += v[d] == DSP_SELLER;
                } else {
                    silent++;
                }
            }
            uint8_t win = nb > ns ? DSP_BUYER : ns > nb ? DSP_SELLER : DSP_NONE;
            uint64_t expect = (uint64_t) silent * f.juror_bond;
            if (win == DSP_BUYER) expect += (uint64_t) ns * f.juror_bond;
            if (win == DSP_SELLER) expect += (uint64_t) nb * f.juror_bond;
            if (c->appeal_live && win != DSP_NONE && win != c->appellant) expect += c->appeal_bond;
            uint64_t r0 = deb(reserve);
            tick = c->reveal_deadline;
            dsp_status_t st = dsp_tally(&D, id, tick);
            bad_slash += deb(reserve) - r0 != expect;
            bad_slash += (win == DSP_NONE) != (st == DSP_TIE);
            bad_audit += !audit();
            if (st == DSP_OK) {
                if (rnd() % 3 == 0) {
                    bad_final += dsp_finalize(&D, id, c->appeal_deadline) != DSP_OK;
                } else {
                    uint8_t loser = c->winner == DSP_BUYER ? DSP_SELLER : DSP_BUYER;
                    dsp_status_t e = dsp_escalate(&D, id, loser, tick + 1);
                    if (e == DSP_ERR_LEDGER) break;
                    bad_final += e != DSP_OK;
                }
            }
            bad_audit += !audit();
        }
        bad_reserve += deb(reserve) - reserve0 != D.slashed_total;
        if (c->state == DSP_ST_FINAL || c->state == DSP_ST_GATEWAY)
            bad_final += dsp_case_locked(&D, id) != 0 || deb(escrow) != 0;
    }
    CHECK(bad_sel == 0, "random: selection matches the reference");
    CHECK(bad_slash == 0, "random: exactly the silent, the minority and failed appeals slashed");
    CHECK(bad_final == 0, "random: appeals and finality");
    CHECK(bad_audit == 0, "random: audit after every step");
    CHECK(bad_reserve == 0, "random: reserve inflow == slashed total");
}

static void test_check(void)
{
    setup(NULL, NJ);
    uint32_t id = open_case(1, 0);
    dsp_escalate(&D, id, DSP_BUYER, 0);
    do_select(id, 0);
    CHECK(audit(), "clean");
    D.locked_total++;
    CHECK(!dsp_check(&D), "counter off");
    D.locked_total--;
    D.c[0].jbond++;
    CHECK(!dsp_check(&D), "case live sum off");
    D.c[0].jbond--;
    D.c[0].state = DSP_ST_APPEAL;
    CHECK(!dsp_check(&D), "live bonds in a ruled case");
    D.c[0].state = DSP_ST_VOTE;
    uint32_t id2 = open_case(2, 0);
    dsp_escalate(&D, id2, DSP_SELLER, 0);
    CHECK(D.c[1].id == id2 && audit(), "second case at slot 1");
    D.c[1].state = DSP_ST_FINAL;
    CHECK(!dsp_check(&D), "live bond in a FINAL case at slot 1");
    D.c[1].state = DSP_ST_PROOFS;
    CHECK(!dsp_check(&D), "live bond at level 1");
    D.c[1].state = DSP_ST_GATEWAY;
    CHECK(!dsp_check(&D), "live bond at the gateway");
    D.c[1].state = DSP_ST_SELECT;
    CHECK(dsp_check(&D), "restored");
    D.c[1].appeal_bond++;
    CHECK(!dsp_check(&D), "slot 1 sum off");
    D.c[1].appeal_bond--;
    pay_receipt_t rc;
    pay_ledger_transfer(&L, mk(), spare, escrow, 0, &rc);
    fund(escrow, 1);
    CHECK(!dsp_check(&D), "stray money in the escrow");
    pay_ledger_transfer(&L, mk(), escrow, spare, 1, &rc);
    CHECK(dsp_check(&D), "restored");
    D.returned_total = D.locked_total + 1;
    CHECK(!dsp_check(&D), "returned above locked");
    D.returned_total = 0;
    D.slashed_total = UINT64_MAX;
    D.returned_total = 1;
    CHECK(!dsp_check(&D), "counter overflow");
    D.slashed_total = D.returned_total = 0;
    pay_ledger_set_flags(&L, escrow, PAY_ACCT_ISSUER);
    fund(escrow, 0);
    pay_ledger_issue(&L, mk(), escrow, spare, 5, &rc);
    CHECK(!dsp_check(&D), "escrow credit");
    pay_ledger_redeem(&L, mk(), spare, escrow, 5, &rc);
    pay_ledger_set_flags(&L, escrow, 0);
    CHECK(dsp_check(&D), "restored");
    D.ready = false;
    CHECK(!dsp_check(&D) && !dsp_check(NULL), "not ready");
}

int main(void)
{
    vrf_keys();
    test_config();
    test_init_pool();
    test_open();
    test_level1();
    test_level2_flow();
    test_ties();
    test_pool_rules();
    test_more();
    test_more2();
    test_award();
    test_messages();
    test_random();
    test_check();
    printf("PASS %d FAIL %d\n", pass, fail);
    return fail ? 1 : 0;
}
