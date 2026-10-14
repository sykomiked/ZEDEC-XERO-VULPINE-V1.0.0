/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_fxepoch.c — FX uniform-price batch epochs (fxepoch.h F1-F13) on a real
 * pay_ledger with three non-USD fiat assets (EUR, JPY, GBP) and three direct
 * pairs (EUR/JPY, JPY/GBP, GBP/EUR): the off-by-default switch, leverage
 * refusal, every order outcome, hand-worked clearings (max volume, the
 * imbalance tie, the reference and midpoint ties, no cross), cooling epochs
 * walking toward the market, pro rata with leftovers, fill pairing, rounding
 * dust, the fee carry, DvP settlement, resumable settlement and cancel, the
 * netting report, audit corruption, and a randomized property loop against an
 * independent reference. The signature hook is a stand-in (SHA-256 keyed by
 * the signer); production passes ML-DSA. */
#include <stdio.h>
#include <string.h>
#include "fxepoch.h"
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

#define NP  6
#define NA  3
#define NBK 3
static pay_ledger_t L;
static fx_t M;
static uint16_t asset[NA]; /* EUR, JPY, GBP */
static uint32_t iss[NA], pa[NP][NA], esc[NBK][2], feea[NA], spare[NA];
static const int BA[NBK] = {0, 1, 2}, BQ[NBK] = {1, 2, 0};
static const uint64_t SCALE[NBK] = {100, 1000, 100};
static uint64_t nonce_ctr, nfund;
static int nverify;
static const uint8_t SEED[32] = {3, 1, 4, 1, 5, [31] = 0x9F};
#define MKT  77u
#define FUND 1000000000000ull

/* stand-in signature: SHA-256(signer || 0x5A || msg) */
static void sign(uint32_t who, const uint8_t *m, uint32_t n, uint8_t sig[32])
{
    uint8_t b[160];
    memset(b, 0, sizeof b);
    memcpy(b, &who, 4);
    b[4] = 0x5A;
    memcpy(b + 8, m, n);
    sha256(b, 8 + n, sig);
}
static bool verify(void *ctx, uint32_t who, const uint8_t *m, uint32_t n, const uint8_t *sig,
                   uint32_t sl)
{
    uint8_t want[32];
    (*(int *) ctx)++;
    if (sl != 32 || n > 150) return false;
    sign(who, m, n, want);
    return !memcmp(want, sig, 32);
}

static void ids(pay_posting_req_t *r, const char *e2e)
{
    memset(r, 0, sizeof *r);
    nfund++;
    snprintf(r->uetr, sizeof r->uetr, "00000000-0000-4000-8000-%012llx",
             (unsigned long long) nfund);
    if (e2e)
        snprintf(r->e2e, sizeof r->e2e, "%s", e2e);
    else
        snprintf(r->e2e, sizeof r->e2e, "FUND-%llu", (unsigned long long) nfund);
    memcpy(r->idem_key, &nfund, sizeof nfund);
    r->idem_key[31] = 0xEE;
}

static pay_status_t fund(uint32_t from, uint32_t to, uint64_t amt)
{
    pay_posting_req_t r;
    pay_receipt_t rc;
    ids(&r, 0);
    return pay_ledger_issue(&L, &r, from, to, amt, &rc);
}

/* a posting whose e2e collides with the module's next one: makes it fail */
static void block_e2e(uint64_t seq)
{
    pay_posting_req_t r;
    pay_receipt_t rc;
    char e[40];
    snprintf(e, sizeof e, "FXEP-%llu", (unsigned long long) seq);
    ids(&r, e);
    pay_ledger_issue(&L, &r, iss[0], spare[0], 1, &rc);
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

static fx_config_t on_cfg(uint32_t band)
{
    fx_config_t c;
    fx_config_default(&c);
    c.enabled = true;
    c.band_bps = band;
    return c;
}

static void setup_ledger(void)
{
    pay_ledger_init(&L, NULL);
    nfund = 0;
    pay_ledger_add_fiat(&L, "EUR", 978, 2, &asset[0]);
    pay_ledger_add_fiat(&L, "JPY", 392, 0, &asset[1]);
    pay_ledger_add_fiat(&L, "GBP", 826, 2, &asset[2]);
    for (int a = 0; a < NA; a++) {
        iss[a] = open_acct(asset[a], 1, PAY_ACCT_ISSUER);
        feea[a] = open_acct(asset[a], 3 + a, 0);
        spare[a] = open_acct(asset[a], 9, 0);
    }
    for (int b = 0; b < NBK; b++) {
        esc[b][0] = open_acct(asset[BA[b]], 2, 0);
        esc[b][1] = open_acct(asset[BQ[b]], 2, 0);
    }
    for (int p = 0; p < NP; p++)
        for (int a = 0; a < NA; a++) {
            pa[p][a] = open_acct(asset[a], 100 + p, 0);
            fund(iss[a], pa[p][a], FUND);
        }
}

static void setup(uint32_t band)
{
    setup_ledger();
    fx_config_t c = on_cfg(band);
    nverify = 0;
    fx_status_t st = fx_init(&M, &L, &c, MKT, verify, &nverify, SEED);
    if (st != FX_OK) printf("setup: init %d\n", st);
    for (int b = 0; b < NBK; b++) {
        st = fx_add_book(&M, esc[b][0], esc[b][1], feea[BQ[b]], SCALE[b], 0);
        if (st != FX_OK) printf("setup: book %d: %d\n", b, st);
    }
}

static fx_status_t ordn(uint32_t book, uint8_t side, int who, uint64_t qty, uint64_t limit,
                        uint64_t nonce, uint32_t *idx)
{
    uint32_t base = pa[who][BA[book]], quote = pa[who][BQ[book]];
    uint8_t m[FX_ORDER_MSG], sig[32];
    fx_order_msg(MKT, M.epoch, book, side, base, quote, qty, limit, nonce, m);
    sign(100 + who, m, sizeof m, sig);
    return fx_order(&M, book, side, base, quote, qty, limit, nonce, 0, sig, 32, idx);
}

static fx_status_t ord(uint32_t book, uint8_t side, int who, uint64_t qty, uint64_t limit)
{
    return ordn(book, side, who, qty, limit, ++nonce_ctr, 0);
}

/* lines of the ledger posting made at ledger sequence `seq` */
static uint32_t lines_at(uint64_t seq)
{
    return L.journal[seq & (PAY_JOURNAL_MAX - 1u)].n_lines;
}

static bool escrows_home(void)
{
    for (uint32_t b = 0; b < M.n_books; b++)
        if (deb(M.book[b].escrow_base) != M.book[b].base_start ||
            deb(M.book[b].escrow_quote) != M.book[b].quote_start)
            return false;
    return true;
}

/* ===================================================================== */

static void test_config(void)
{
    fx_config_t c;
    fx_config_default(&c);
    CHECK(!c.enabled && c.leverage == 0 && c.band_bps == FX_BAND_DEFAULT && c.band_bps == 500 &&
              c.max_orders == FX_MAX_ORDERS,
          "F1 defaults: off, no leverage, 5% band, full order limit");
    fx_config_default(NULL); /* no crash */
    setup_ledger();
    CHECK(fx_init(&M, &L, &c, MKT, verify, &nverify, SEED) == FX_OK && M.ready &&
              M.last_ledger == PAY_OK && M.market_id == MKT && M.state == FX_IDLE,
          "init with the default (disabled) config");
    CHECK(fx_add_book(&M, esc[0][0], esc[0][1], feea[1], 100, 0) == FX_OK, "a book");
    CHECK(fx_open(&M) == FX_ERR_DISABLED && M.state == FX_IDLE && M.epoch == 0,
          "F1 disabled: open refused, fail closed");
    uint8_t sig[32] = {0};
    CHECK(fx_order(&M, 0, FX_SELL, pa[0][0], pa[0][1], 1, 1, 1, 0, sig, 32, 0) == FX_ERR_DISABLED,
          "F1 disabled: order refused");
    CHECK(fx_check(&M), "disabled market audits clean");
    fx_config_t lev = c;
    lev.leverage = 1;
    CHECK(fx_set_config(&M, &lev) == FX_ERR_LEVERAGE && !M.cfg.enabled,
          "F1 leverage 1 refused by set_config");
    lev.leverage = 0xFFFFFFFFu;
    lev.enabled = true;
    CHECK(fx_set_config(&M, &lev) == FX_ERR_LEVERAGE && !M.cfg.enabled,
          "F1 any leverage refused, even when enabling");
    fx_t m2;
    CHECK(fx_init(&m2, &L, &lev, MKT, verify, 0, 0) == FX_ERR_LEVERAGE,
          "F1 leverage refused by init");
    fx_config_t b = c;
    b.band_bps = FX_BPS;
    CHECK(fx_init(&m2, &L, &b, MKT, verify, 0, 0) == FX_ERR_ARG &&
              fx_set_config(&M, &b) == FX_ERR_ARG,
          "F6 band 10000 refused");
    b.band_bps = FX_BPS - 1;
    CHECK(fx_set_config(&M, &b) == FX_OK && M.cfg.band_bps == 9999, "band 9999 accepted");
    b = c;
    b.max_orders = 0;
    CHECK(fx_set_config(&M, &b) == FX_ERR_ARG, "max_orders 0 refused");
    b.max_orders = FX_MAX_ORDERS + 1;
    CHECK(fx_set_config(&M, &b) == FX_ERR_ARG, "max_orders above the cap refused");
    b.max_orders = FX_MAX_ORDERS;
    CHECK(fx_set_config(&M, &b) == FX_OK, "max_orders at the cap accepted");
    b.max_orders = 1;
    CHECK(fx_set_config(&M, &b) == FX_OK && M.cfg.max_orders == 1, "max_orders 1 accepted");
    CHECK(fx_init(NULL, &L, &c, 0, verify, 0, 0) == FX_ERR_ARG &&
              fx_init(&m2, NULL, &c, 0, verify, 0, 0) == FX_ERR_ARG &&
              fx_init(&m2, &L, NULL, 0, verify, 0, 0) == FX_ERR_ARG &&
              fx_init(&m2, &L, &c, 0, NULL, 0, 0) == FX_ERR_ARG,
          "init NULLs refused");
    CHECK(fx_set_config(NULL, &c) == FX_ERR_ARG && fx_set_config(&M, NULL) == FX_ERR_ARG,
          "set_config NULLs refused");
    fx_t z;
    memset(&z, 0, sizeof z);
    CHECK(fx_set_config(&z, &c) == FX_ERR_ARG && fx_open(&z) == FX_ERR_ARG &&
              fx_clear(&z) == FX_ERR_ARG && fx_settle(&z, 0) == FX_ERR_ARG &&
              fx_cancel(&z, 0) == FX_ERR_ARG && !fx_check(&z) &&
              fx_add_book(&z, esc[0][0], esc[0][1], feea[1], 1, 0) == FX_ERR_ARG,
          "a market not set up refuses everything");
    fx_net_t n;
    CHECK(fx_net(&z, &n) == FX_ERR_ARG && fx_net(NULL, &n) == FX_ERR_ARG &&
              fx_net(&M, NULL) == FX_ERR_ARG,
          "fx_net NULLs / not set up refused");
    CHECK(fx_net(&M, &n) == FX_ERR_STATE, "fx_net in IDLE refused");
    CHECK(!fx_check(NULL) && fx_record(NULL, 1, 0) == NULL && fx_record(&M, 0, 0) == NULL,
          "NULL / epoch 0 lookups");
    /* seed NULL is allowed: all zero */
    CHECK(fx_init(&m2, &L, &c, 5, verify, 0, NULL) == FX_OK && m2.seed[0] == 0 && m2.seed[31] == 0,
          "init without a seed");
    CHECK(fx_init(&m2, &L, &c, 5, verify, 0, SEED) == FX_OK && m2.seed[0] == 3 &&
              m2.seed[31] == 0x9F,
          "init copies the seed");
    /* enable, open, then config changes are refused mid-epoch */
    fx_config_t on = on_cfg(0);
    CHECK(fx_set_config(&M, &on) == FX_OK && fx_open(&M) == FX_OK && M.state == FX_OPEN &&
              M.epoch == 1,
          "enabled: open");
    CHECK(fx_set_config(&M, &on) == FX_ERR_STATE, "config change mid-epoch refused");
    lev = on;
    lev.leverage = 2;
    CHECK(fx_set_config(&M, &lev) == FX_ERR_LEVERAGE && M.cfg.leverage == 0,
          "leverage refused mid-epoch too");
    M.cfg.leverage = 1;
    CHECK(!fx_check(&M), "a leveraged config fails the audit");
    M.cfg.leverage = 0;
    CHECK(fx_check(&M), "and passes once restored");
}

static void test_books(void)
{
    setup_ledger();
    fx_config_t c = on_cfg(0);
    CHECK(fx_init(&M, &L, &c, MKT, verify, &nverify, SEED) == FX_OK, "init");
    CHECK(fx_open(&M) == FX_ERR_STATE, "open with no book refused");
    uint32_t bad = L.n_accounts, idx = 99;
    CHECK(fx_add_book(&M, esc[0][0], esc[0][1], feea[1], 0, 0) == FX_ERR_ARG, "scale 0 refused");
    CHECK(fx_add_book(NULL, esc[0][0], esc[0][1], feea[1], 1, 0) == FX_ERR_ARG, "NULL refused");
    CHECK(fx_add_book(&M, bad, esc[0][1], feea[1], 1, 0) == FX_ERR_ARG &&
              fx_add_book(&M, esc[0][0], bad, feea[1], 1, 0) == FX_ERR_ARG &&
              fx_add_book(&M, esc[0][0], esc[0][1], bad, 1, 0) == FX_ERR_ARG,
          "missing account refused");
    CHECK(fx_add_book(&M, esc[0][0], esc[0][0], feea[0], 1, 0) == FX_ERR_ARG,
          "same escrow twice refused");
    CHECK(fx_add_book(&M, esc[0][0], esc[0][1], esc[0][1], 1, 0) == FX_ERR_ARG,
          "fee == quote escrow refused");
    uint32_t e2 = open_acct(asset[0], 2, 0);
    CHECK(fx_add_book(&M, esc[0][0], esc[0][1], esc[0][0], 1, 0) == FX_ERR_ARG,
          "fee == base escrow refused");
    CHECK(fx_add_book(&M, esc[0][0], e2, feea[0], 1, 0) == FX_ERR_ARG,
          "escrows of one asset refused (no pair)");
    CHECK(fx_add_book(&M, esc[0][0], esc[0][1], feea[0], 1, 0) == FX_ERR_ARG,
          "fee account not in the quote asset refused");
    CHECK(fx_add_book(&M, iss[0], esc[0][1], feea[1], 1, 0) == FX_ERR_ARG &&
              fx_add_book(&M, esc[0][0], iss[1], feea[1], 1, 0) == FX_ERR_ARG &&
              fx_add_book(&M, esc[0][0], esc[0][1], iss[1], 1, 0) == FX_ERR_ARG,
          "issuer accounts refused");
    L.acct[esc[0][1]].active = false;
    CHECK(fx_add_book(&M, esc[0][0], esc[0][1], feea[1], 1, 0) == FX_ERR_ARG,
          "inactive account refused");
    L.acct[esc[0][1]].active = true;
    CHECK(M.n_books == 0, "refused books leave nothing");
    fund(iss[0], esc[0][0], 5);
    fund(iss[1], feea[1], 7);
    CHECK(fx_add_book(&M, esc[0][0], esc[0][1], feea[1], 100, &idx) == FX_OK && idx == 0 &&
              M.n_books == 1,
          "book 0 EUR/JPY");
    const fx_book_t *b = &M.book[0];
    CHECK(b->base == asset[0] && b->quote == asset[1] && b->scale == 100 && b->base_start == 5 &&
              b->quote_start == 0 && b->fee_start == 7 && !b->has_ref,
          "book records assets, scale and baselines");
    CHECK(fx_add_book(&M, esc[1][0], esc[1][1], feea[2], 1, 0) == FX_OK, "book 1 JPY/GBP");
    uint16_t chf;
    pay_ledger_add_fiat(&L, "CHF", 756, 2, &chf);
    uint32_t cb = open_acct(chf, 2, 0), cq = open_acct(chf, 2, 0), cf = open_acct(chf, 6, 0);
    uint32_t g2 = open_acct(asset[2], 2, 0), j2 = open_acct(asset[1], 2, 0);
    uint32_t j3 = open_acct(asset[1], 2, 0);
    CHECK(fx_add_book(&M, esc[0][0], cq, cf, 1, 0) == FX_ERR_ARG,
          "escrow reused as base refused (new pair EUR/CHF)");
    CHECK(fx_add_book(&M, cb, esc[1][1], feea[2], 1, 0) == FX_ERR_ARG,
          "escrow reused as quote refused (new pair CHF/GBP)");
    CHECK(fx_add_book(&M, cb, j2, esc[0][1], 1, 0) == FX_ERR_ARG, "an escrow as fee refused");
    CHECK(fx_add_book(&M, feea[2], cq, cf, 1, 0) == FX_ERR_ARG,
          "a fee account as base escrow refused");
    CHECK(fx_add_book(&M, cb, feea[1], feea[1], 1, 0) == FX_ERR_ARG,
          "a fee account as quote escrow refused");
    CHECK(fx_add_book(&M, e2, j2, feea[1], 1, 0) == FX_ERR_ARG, "same pair as book 0 refused");
    CHECK(fx_add_book(&M, j2, g2, feea[2], 1, 0) == FX_ERR_ARG, "same pair as book 1 refused");
    CHECK(fx_add_book(&M, g2, j3, feea[1], 1, 0) == FX_ERR_ARG, "reverse of book 1 refused");
    CHECK(M.n_books == 2, "nothing added");
    uint32_t e4 = open_acct(asset[0], 2, 0);
    CHECK(fx_add_book(&M, g2, e4, feea[0], 100, &idx) == FX_OK && idx == 2,
          "book 2 GBP/EUR (non-USD cross)");
    /* a fourth book sharing JPY's fee account, after an outside deposit */
    uint32_t j4 = open_acct(asset[1], 2, 0);
    fund(iss[1], feea[1], 3);
    CHECK(!fx_check(&M), "an outside deposit into a fee account is flagged");
    CHECK(fx_add_book(&M, cb, j4, feea[1], 1, &idx) == FX_OK && idx == 3 &&
              M.book[3].fee_start == 7,
          "book 3 shares a fee account and keeps its first baseline");
    CHECK(!fx_check(&M), "still flagged");
    M.book[0].fee_start = M.book[3].fee_start = 10;
    CHECK(fx_check(&M), "four books audit clean with the baseline moved");
    M.book[0].fee_total = 4;
    CHECK(!fx_check(&M), "phantom fee on the shared account caught");
    M.book[0].fee_total = 0;
    uint16_t sek;
    pay_ledger_add_fiat(&L, "SEK", 752, 2, &sek);
    uint32_t s1 = open_acct(sek, 2, 0), j5 = open_acct(asset[1], 2, 0);
    CHECK(fx_add_book(&M, s1, j5, feea[1], 1, 0) == FX_ERR_FULL, "fifth book: full");
    M.n_books = 3;
    CHECK(fx_add_book(&M, s1, j5, feea[1], 1, 0) == FX_OK && M.book[3].fee_start == 10,
          "re-added over the slot, shared baseline");
    CHECK(fx_open(&M) == FX_OK, "open");
    CHECK(fx_add_book(&M, s1, j5, feea[1], 1, 0) == FX_ERR_STATE, "no book mid-epoch");
}

static void test_pure(void)
{
    uint64_t v, lo, hi;
    CHECK(fx_quote_value(75, 100, 100, &v) && v == 75, "F3 75 at 100/100");
    CHECK(fx_quote_value(7, 10, 3, &v) && v == 23, "F3 floor(70/3) = 23");
    CHECK(fx_quote_value(1, 1, 1000, &v) && v == 0, "F3 dust floors to 0");
    CHECK(!fx_quote_value(1, 1, 0, &v) && !fx_quote_value(1, 1, 1, NULL), "F3 scale 0 / NULL");
    CHECK(fx_quote_value(UINT64_MAX, UINT64_MAX, UINT64_MAX, &v) && v == UINT64_MAX,
          "F3 128-bit product");
    CHECK(!fx_quote_value(UINT64_MAX, 2, 1, &v), "F3 does not fit");
    CHECK(fx_buy_lock(150, 110, 100, &v) && v == 165, "F4 exact lock");
    CHECK(fx_buy_lock(50, 95, 100, &v) && v == 48, "F4 ceil(47.5) = 48");
    CHECK(fx_buy_lock(1, 1, 1000, &v) && v == 1, "F4 ceil of dust = 1");
    CHECK(fx_buy_lock(3, 1, 3, &v) && v == 1, "F4 exact division, no round-up");
    CHECK(!fx_buy_lock(1, 1, 0, &v) && !fx_buy_lock(1, 1, 1, NULL), "F4 scale 0 / NULL");
    CHECK(!fx_buy_lock(UINT64_MAX, UINT64_MAX, 1, &v), "F4 does not fit");
    CHECK(!fx_buy_lock(UINT64_MAX, 3, 2, &v), "F4 ceil does not fit");
    CHECK(fx_buy_lock(UINT64_MAX, 1, 1, &v) && v == UINT64_MAX, "F4 UINT64_MAX exact");
    CHECK(fx_band(1000, 500, &lo, &hi) && lo == 950 && hi == 1050, "F6 band 5% of 1000");
    CHECK(fx_band(1050, 500, &lo, &hi) && lo == 998 && hi == 1102, "F6 ceil low, floor high");
    CHECK(fx_band(3, 5000, &lo, &hi) && lo == 2 && hi == 4, "F6 ceil(1.5)=2, floor(4.5)=4");
    CHECK(fx_band(1, 9999, &lo, &hi) && lo == 1 && hi == 1, "F6 tiny ref");
    CHECK(fx_band(7, 0, &lo, &hi) && lo == 7 && hi == 7, "F6 band 0: a point");
    CHECK(fx_band(UINT64_MAX, 500, &lo, &hi) && hi == UINT64_MAX &&
              lo == (uint64_t) (((unsigned __int128) UINT64_MAX * 9500 + 9999) / 10000),
          "F6 high bound saturates");
    CHECK(fx_band(10000, 1, &lo, &hi) && lo == 9999 && hi == 10001, "F6 1 bp");
    CHECK(!fx_band(0, 500, &lo, &hi) && !fx_band(1, 10000, &lo, &hi) &&
              !fx_band(1, 500, NULL, &hi) && !fx_band(1, 500, &lo, NULL),
          "F6 refusals");
    uint8_t m[FX_ORDER_MSG];
    fx_order_msg(0x01020304u, 0x1112131415161718ull, 5, 1, 6, 7, 8, 9, 10, m);
    CHECK(FX_ORDER_MSG == 63 && !memcmp(m, "zxv/fxepoch/v1", 14) && m[14] == 4 && m[17] == 1 &&
              m[18] == 0x18 && m[25] == 0x11 && m[26] == 5 && m[30] == 1 && m[31] == 6 &&
              m[35] == 7 && m[39] == 8 && m[47] == 9 && m[55] == 10 && m[62] == 0,
          "order message layout");
}

static void test_orders(void)
{
    setup(0);
    CHECK(fx_check(&M), "fresh market audits");
    CHECK(ord(0, FX_SELL, 0, 1, 1) == FX_ERR_STATE, "order before open refused");
    CHECK(fx_open(&M) == FX_OK, "open");
    CHECK(fx_open(&M) == FX_ERR_STATE, "open twice refused");
    uint64_t s0 = L.seq, e0 = deb(pa[0][0]);
    uint32_t idx = 99;
    CHECK(ordn(0, FX_SELL, 0, 100, 90, 1, &idx) == FX_OK && idx == 0 && M.n_orders == 1, "a sell");
    CHECK(L.seq == s0 + 1 && deb(pa[0][0]) == e0 - 100 && deb(esc[0][0]) == 100,
          "F4 sell locks qty base in ONE transfer");
    const fx_order_t *o = &M.order[0];
    CHECK(o->book == 0 && o->side == FX_SELL && o->owner == 100 && o->qty == 100 &&
              o->limit == 90 && o->locked == 100 && o->seq == 0 && !o->fill && !o->settled &&
              o->nonce == 1 && o->base_acct == pa[0][0] && o->quote_acct == pa[0][1],
          "sell recorded");
    uint64_t j2 = deb(pa[2][1]);
    CHECK(ordn(0, FX_BUY, 2, 150, 110, 1, &idx) == FX_OK && idx == 1,
          "a buy (another owner may reuse nonce 1)");
    CHECK(M.order[1].locked == 165 && deb(pa[2][1]) == j2 - 165 && deb(esc[0][1]) == 165 &&
              M.order[1].seq == 1,
          "F4 buy locks ceil(qty*limit/scale) quote");
    CHECK(M.book[0].sell_qty == 100 && M.book[0].buy_qty == 150 && M.book[0].quote_lock == 165,
          "book tallies");
    CHECK(fx_check(&M), "audit with open locks");
    /* refusals: nothing recorded, nothing posted */
    uint64_t s1 = L.seq, nb = M.n_orders;
    CHECK(ordn(0, FX_SELL, 0, 5, 90, 1, 0) == FX_ERR_DUP, "F4 (owner, nonce) reuse refused");
    CHECK(ordn(0, FX_SELL, 0, 0, 90, 50, 0) == FX_ERR_ARG, "qty 0 refused");
    CHECK(ordn(0, FX_SELL, 0, 1, 0, 50, 0) == FX_ERR_ARG, "limit 0 refused");
    CHECK(ordn(0, 2, 0, 1, 1, 50, 0) == FX_ERR_ARG, "bad side refused");
    uint8_t sig[32] = {0}, msg[FX_ORDER_MSG];
    CHECK(fx_order(&M, 3, FX_SELL, pa[0][0], pa[0][1], 1, 1, 51, 0, sig, 32, 0) == FX_ERR_ARG,
          "unknown book refused");
    CHECK(fx_order(&M, 0, FX_SELL, pa[0][0], pa[0][1], 1, 1, 51, 0, NULL, 32, 0) == FX_ERR_ARG &&
              fx_order(NULL, 0, FX_SELL, pa[0][0], pa[0][1], 1, 1, 51, 0, sig, 32, 0) == FX_ERR_ARG,
          "NULLs refused");
    CHECK(fx_order(&M, 0, FX_SELL, pa[0][1], pa[0][1], 1, 1, 51, 0, sig, 32, 0) == FX_ERR_ARG,
          "base account in the wrong asset refused");
    CHECK(fx_order(&M, 0, FX_SELL, pa[0][0], pa[0][0], 1, 1, 51, 0, sig, 32, 0) == FX_ERR_ARG,
          "quote account in the wrong asset refused");
    CHECK(fx_order(&M, 0, FX_SELL, pa[0][0], pa[1][1], 1, 1, 51, 0, sig, 32, 0) == FX_ERR_ARG,
          "accounts of different owners refused");
    CHECK(fx_order(&M, 0, FX_SELL, L.n_accounts, pa[0][1], 1, 1, 51, 0, sig, 32, 0) == FX_ERR_ARG &&
              fx_order(&M, 0, FX_SELL, pa[0][0], L.n_accounts, 1, 1, 51, 0, sig, 32, 0) ==
                  FX_ERR_ARG,
          "missing accounts refused");
    CHECK(fx_order(&M, 0, FX_SELL, esc[0][0], esc[0][1], 1, 1, 51, 0, sig, 32, 0) == FX_ERR_ARG,
          "escrow accounts refused");
    uint32_t xe = open_acct(asset[0], 4, 0); /* owner of feea[1] */
    CHECK(fx_order(&M, 0, FX_BUY, xe, feea[1], 1, 1, 51, 0, sig, 32, 0) == FX_ERR_ARG,
          "fee account as quote refused");
    uint32_t xj = open_acct(asset[1], 2, 0);
    CHECK(fx_order(&M, 0, FX_BUY, esc[2][1], xj, 1, 1, 51, 0, sig, 32, 0) == FX_ERR_ARG,
          "another book's escrow as base refused");
    uint32_t xf = open_acct(asset[0], 3, 0); /* owner of feea[0] */
    uint32_t xj3 = open_acct(asset[1], 3, 0);
    CHECK(fx_order(&M, 0, FX_BUY, feea[0], xj3, 1, 1, 51, 0, sig, 32, 0) == FX_ERR_ARG,
          "another book's fee account as base refused");
    (void) xf;
    CHECK(fx_order(&M, 0, FX_SELL, pa[0][0], pa[0][1], 1, 1, 51, 0, sig, 32, 0) == FX_ERR_SIG,
          "F4 bad signature refused");
    fx_order_msg(MKT, M.epoch, 0, FX_SELL, pa[0][0], pa[0][1], 1, 1, 51, msg);
    sign(101, msg, sizeof msg, sig);
    CHECK(fx_order(&M, 0, FX_SELL, pa[0][0], pa[0][1], 1, 1, 51, 0, sig, 32, 0) == FX_ERR_SIG,
          "signed by someone else refused");
    sign(100, msg, sizeof msg, sig);
    CHECK(fx_order(&M, 0, FX_SELL, pa[0][0], pa[0][1], 2, 1, 51, 0, sig, 32, 0) == FX_ERR_SIG,
          "altered qty refused");
    CHECK(fx_order(&M, 0, FX_SELL, pa[0][0], pa[0][1], 1, 1, 51, 0, sig, 31, 0) == FX_ERR_SIG,
          "short signature refused");
    fx_order_msg(MKT + 1, M.epoch, 0, FX_SELL, pa[0][0], pa[0][1], 1, 1, 51, msg);
    sign(100, msg, sizeof msg, sig);
    CHECK(fx_order(&M, 0, FX_SELL, pa[0][0], pa[0][1], 1, 1, 51, 0, sig, 32, 0) == FX_ERR_SIG,
          "another market's signature refused");
    CHECK(L.seq == s1 && M.n_orders == nb && fx_check(&M), "refusals left no partial state");
    /* funds: the ledger refuses, nothing recorded */
    CHECK(ord(0, FX_SELL, 0, FUND, 90) == FX_ERR_LEDGER && M.last_ledger == PAY_ERR_FUNDS &&
              M.n_orders == nb && M.book[0].sell_qty == 100,
          "F4 unfunded sell refused by the ledger, nothing recorded");
    CHECK(ord(0, FX_BUY, 1, FUND, 200) == FX_ERR_LEDGER && M.n_orders == nb &&
              M.book[0].buy_qty == 150 && M.book[0].quote_lock == 165,
          "F4 unfunded buy refused, tallies untouched");
    pay_ledger_set_flags(&L, pa[3][0], PAY_ACCT_FROZEN);
    CHECK(ord(0, FX_SELL, 3, 1, 90) == FX_ERR_LEDGER && M.last_ledger != PAY_OK,
          "frozen seller refused");
    pay_ledger_set_flags(&L, pa[3][0], 0);
    CHECK(fx_check(&M), "audit after refused locks");
    /* PAY_BAL_MAX bounds (checked before the ledger) */
    uint64_t big = PAY_BAL_MAX - 100;
    CHECK(ord(0, FX_SELL, 1, big, 1) == FX_ERR_ARG, "sell quantity bound (== BAL_MAX) refused");
    CHECK(ord(0, FX_SELL, 1, big - 1, 1) == FX_ERR_LEDGER, "just below the bound reaches ledger");
    CHECK(ord(0, FX_SELL, 1, UINT64_MAX, 1) == FX_ERR_ARG, "sell sum overflow refused");
    CHECK(ord(0, FX_BUY, 1, PAY_BAL_MAX - 150, 1) == FX_ERR_ARG, "buy qty bound refused");
    CHECK(ord(0, FX_BUY, 1, UINT64_MAX, 1) == FX_ERR_ARG, "buy qty overflow refused");
    CHECK(ord(0, FX_BUY, 1, 100, PAY_BAL_MAX - 165) == FX_ERR_ARG, "quote lock bound refused");
    CHECK(ord(0, FX_BUY, 1, 100, PAY_BAL_MAX - 166) == FX_ERR_LEDGER,
          "just below the quote bound reaches the ledger");
    CHECK(ord(0, FX_BUY, 1, 200, UINT64_MAX) == FX_ERR_ARG, "buy lock does not fit: refused");
    M.book[0].quote_lock = UINT64_MAX - 1;
    CHECK(ord(0, FX_BUY, 1, 1, 200) == FX_ERR_ARG, "quote lock sum overflow refused");
    M.book[0].quote_lock = 165;
    CHECK(M.n_orders == nb && fx_check(&M), "bounds left no partial state");
    /* order limit */
    fx_config_t c = on_cfg(0);
    c.max_orders = 3;
    M.cfg = c; /* (white-box: the limit is a config value) */
    CHECK(ord(0, FX_SELL, 1, 1, 90) == FX_OK && M.n_orders == 3, "third order");
    CHECK(ord(0, FX_SELL, 1, 1, 90) == FX_ERR_FULL && M.n_orders == 3, "F4 per-epoch limit");
    nverify = 0;
    CHECK(ord(0, FX_SELL, 1, 1, 90) == FX_ERR_FULL && nverify == 0,
          "full is refused before any signature work");
    c.max_orders = FX_MAX_ORDERS;
    M.cfg = c;
    /* cross-epoch replay: the same signed order in the next epoch */
    fx_order_msg(MKT, M.epoch, 0, FX_SELL, pa[4][0], pa[4][1], 3, 95, 777, msg);
    sign(104, msg, sizeof msg, sig);
    CHECK(fx_order(&M, 0, FX_SELL, pa[4][0], pa[4][1], 3, 95, 777, 0, sig, 32, 0) == FX_OK,
          "signed order accepted");
    CHECK(fx_order(&M, 0, FX_SELL, pa[4][0], pa[4][1], 3, 95, 777, 0, sig, 32, 0) == FX_ERR_DUP,
          "F4 duplicate in the same epoch refused");
    CHECK(fx_cancel(&M, 1) == FX_OK && escrows_home() && fx_check(&M), "cancel");
    CHECK(fx_open(&M) == FX_OK && M.epoch == 2, "epoch 2");
    CHECK(fx_order(&M, 0, FX_SELL, pa[4][0], pa[4][1], 3, 95, 777, 0, sig, 32, 0) == FX_ERR_SIG,
          "F4 replay into the next epoch refused");
    CHECK(ordn(1, FX_BUY, 4, 3, 95, 777, 0) == FX_OK,
          "a nonce is per epoch: freshly signed, the same nonce is accepted");
    CHECK(fx_cancel(&M, 1) == FX_OK && fx_open(&M) == FX_OK && M.book[1].buy_qty == 0 &&
              M.book[1].quote_lock == 0,
          "open resets every book's tallies");
    /* account 0 is an ordinary account (the EUR issuer here), not a module one */
    {
        uint8_t m0[FX_ORDER_MSG], s0[32];
        CHECK(iss[0] == 0 && pay_ledger_account(&L, iss[1])->owner == 1, "account 0 setup");
        fx_order_msg(MKT, M.epoch, 0, FX_SELL, iss[0], iss[1], 5, 90, 1, m0);
        sign(1, m0, sizeof m0, s0);
        CHECK(fx_order(&M, 0, FX_SELL, iss[0], iss[1], 5, 90, 1, 0, s0, 32, 0) == FX_ERR_LEDGER,
              "account 0 reaches the ledger (no funds), not refused as a module account");
    }
    /* a book index == n_books is unknown even for accounts that match its empty slot */
    {
        uint32_t v1 = open_acct(0, 50, 0), v2 = open_acct(0, 50, 0);
        uint8_t m0[FX_ORDER_MSG], s0[32];
        fx_order_msg(MKT, M.epoch, 3, FX_SELL, v1, v2, 5, 90, 1, m0);
        sign(50, m0, sizeof m0, s0);
        CHECK(fx_order(&M, 3, FX_SELL, v1, v2, 5, 90, 1, 0, s0, 32, 0) == FX_ERR_ARG,
              "book == n_books refused");
    }
    CHECK(M.n_orders == 0 && M.book[0].sell_qty == 0 && M.book[0].buy_qty == 0 &&
              M.book[0].quote_lock == 0,
          "open resets the epoch");
    CHECK(fx_clear(&M) == FX_OK, "clear");
    CHECK(ord(0, FX_SELL, 0, 1, 1) == FX_ERR_STATE, "order while CLEARED refused");
}

/* the first hand-worked clearing */
static void test_clear_basic(void)
{
    setup(0);
    CHECK(fx_clear(&M) == FX_ERR_STATE && fx_settle(&M, 0) == FX_ERR_STATE &&
              fx_cancel(&M, 0) == FX_ERR_STATE,
          "F11 clear/settle/cancel in IDLE refused");
    fx_open(&M);
    CHECK(fx_settle(&M, 0) == FX_ERR_STATE, "settle while OPEN refused");
    uint64_t a0e = deb(pa[0][0]), a0j = deb(pa[0][1]), a1e = deb(pa[1][0]), a1j = deb(pa[1][1]);
    uint64_t a2e = deb(pa[2][0]), a2j = deb(pa[2][1]), a3e = deb(pa[3][0]), a3j = deb(pa[3][1]);
    CHECK(ord(0, FX_SELL, 0, 100, 90) == FX_OK && ord(0, FX_SELL, 1, 100, 100) == FX_OK &&
              ord(0, FX_BUY, 2, 150, 110) == FX_OK && ord(0, FX_BUY, 3, 50, 95) == FX_OK,
          "four orders");
    CHECK(fx_supply(&M, 0, 89) == 0 && fx_supply(&M, 0, 90) == 100 &&
              fx_supply(&M, 0, 100) == 200 && fx_demand(&M, 0, 95) == 200 &&
              fx_demand(&M, 0, 96) == 150 && fx_demand(&M, 0, 111) == 0 &&
              fx_supply(&M, 1, 1000) == 0 && fx_supply(NULL, 0, 1) == 0,
          "F5 S(p), D(p)");
    CHECK(fx_net(&M, &(fx_net_t){0}) == FX_ERR_STATE, "fx_net while OPEN refused");
    CHECK(fx_clear(&M) == FX_OK && M.state == FX_CLEARED, "clear");
    CHECK(fx_clear(&M) == FX_ERR_STATE, "clear twice refused");
    const fx_record_t *r = &M.book[0].cur;
    CHECK(r->market == 100 && r->price == 100 && r->volume == 150 && r->supply == 200 &&
              r->demand == 150 && !r->cooling && r->fills == 2 && r->gross == 150 && r->fee == 0 &&
              r->ref_before == 0 && r->epoch == 1 && r->book == 0,
          "F5 max V, least imbalance, midpoint 105 -> tie -> lowest 100");
    CHECK(M.order[0].fill == 75 && M.order[1].fill == 75 && M.order[2].fill == 150 &&
              M.order[3].fill == 0,
          "F7 long side pro rata, short side in full, ineligible buy 0");
    CHECK(M.n_fills == 2 && M.fill[0].sell == 0 && M.fill[0].buy == 2 && M.fill[0].qty == 75 &&
              M.fill[0].quote == 75 && M.fill[1].sell == 1 && M.fill[1].buy == 2 &&
              M.fill[1].qty == 75 && M.fill[1].book == 0,
          "F8 fills paired in submission order");
    CHECK(M.book[1].cur.volume == 0 && M.book[1].cur.price == 0 && M.book[1].cur.epoch == 1 &&
              M.book[1].cur.book == 1,
          "an empty book clears nothing");
    CHECK(fx_check(&M), "audit after clear");
    fx_net_t n;
    CHECK(fx_net(&M, &n) == FX_OK && n.balanced && n.gross_legs == 4 && n.n_pos == 6 &&
              n.nonzero == 6 && n.net_legs == 4 && n.saved == 0,
          "F12 net report: 2 fills x 2 legs, 6 positions, 2 assets -> 4 net legs");
    CHECK(fx_cancel(&M, 0) == FX_OK && M.state == FX_SETTLED, "(cancel this one)");
    CHECK(deb(pa[0][0]) == a0e && deb(pa[3][1]) == a3j && escrows_home() && fx_check(&M),
          "F11 cancel refunds every lock");
    const fx_record_t *rec = fx_record(&M, 1, 0);
    CHECK(rec && rec->cancelled && rec->volume == 0 && rec->fills == 0 && rec->gross == 0 &&
              !M.book[0].has_ref,
          "cancelled record, reference not set");
    CHECK(fx_net(&M, &n) == FX_OK && n.n_pos == 0 && n.gross_legs == 0 && n.balanced,
          "a cancelled epoch nets nothing");
    /* again, settle this time */
    fx_open(&M);
    ord(0, FX_SELL, 0, 100, 90);
    ord(0, FX_SELL, 1, 100, 100);
    ord(0, FX_BUY, 2, 150, 110);
    ord(0, FX_BUY, 3, 50, 95);
    fx_clear(&M);
    uint64_t s0 = L.seq;
    CHECK(fx_settle(&M, 5) == FX_OK && M.state == FX_SETTLED && M.mode == 0, "settle");
    CHECK(L.seq == s0 + 4, "F10 one posting per order");
    CHECK(lines_at(s0) == 4 && lines_at(s0 + 1) == 4 && lines_at(s0 + 2) == 4 &&
              lines_at(s0 + 3) == 2,
          "F10 lines: sell refund+proceeds 4, buy refund+delivery 4, unfilled buy refund 2");
    CHECK(deb(pa[0][0]) == a0e - 75 && deb(pa[0][1]) == a0j + 75 && deb(pa[1][0]) == a1e - 75 &&
              deb(pa[1][1]) == a1j + 75,
          "F10 sellers: base out (refund of the rest), quote in");
    CHECK(deb(pa[2][0]) == a2e + 150 && deb(pa[2][1]) == a2j - 150 && deb(pa[3][0]) == a3e &&
              deb(pa[3][1]) == a3j,
          "F10 buyer pays exactly the fills, refund of the rest; unfilled buyer whole");
    CHECK(escrows_home() && fx_check(&M), "F10 escrows back home");
    CHECK(M.book[0].has_ref && M.book[0].ref == 100 && M.book[0].gross_total == 150 &&
              M.book[0].volume_total == 150 && M.book[0].fee_total == 0 &&
              M.book[0].carry == 2 * 75 * 8889,
          "F9 committed: reference, totals, carry");
    rec = fx_record(&M, 2, 0);
    CHECK(rec && !rec->cancelled && rec->price == 100 && rec->volume == 150 && rec->fills == 2,
          "F13 record");
    CHECK(fx_record(&M, 2, 1) && fx_record(&M, 2, 2) && !fx_record(&M, 2, 3) &&
              !fx_record(&M, 3, 0),
          "one record per book per epoch");
    CHECK(fx_settle(&M, 5) == FX_ERR_STATE && fx_cancel(&M, 5) == FX_ERR_STATE,
          "settle / cancel after SETTLED refused");
    CHECK(fx_net(&M, &n) == FX_OK && n.balanced && n.gross_legs == 4, "net after settle");
}

/* the price-selection ties */
static void test_ties(void)
{
    setup(0);
    /* imbalance tie: S(p)=10; D = 10[p<=100] + 5[p<=95] */
    fx_open(&M);
    ord(0, FX_SELL, 0, 10, 90);
    ord(0, FX_BUY, 1, 10, 100);
    ord(0, FX_BUY, 2, 5, 95);
    fx_clear(&M);
    CHECK(M.book[0].cur.price == 100 && M.book[0].cur.volume == 10 && M.book[0].cur.supply == 10 &&
              M.book[0].cur.demand == 10,
          "F5 least |S - D| wins among equal V");
    CHECK(M.order[1].fill == 10 && M.order[2].fill == 0, "only eligible buyers fill");
    fx_cancel(&M, 0);
    /* midpoint picks the middle: tie set {90,100,110,130}, mid 110 */
    fx_open(&M);
    ord(0, FX_SELL, 0, 10, 90);
    ord(0, FX_SELL, 1, 5, 110);
    ord(0, FX_BUY, 2, 10, 130);
    ord(0, FX_BUY, 3, 5, 100);
    fx_clear(&M);
    CHECK(M.book[0].cur.market == 110 && M.book[0].cur.price == 110 && M.book[0].cur.volume == 10 &&
              M.book[0].cur.supply == 15 && M.book[0].cur.demand == 10,
          "F5 no reference: closest to the lower midpoint of the tie set");
    fx_cancel(&M, 0);
    /* tie set {90,120}: midpoint 105 equidistant -> lowest */
    fx_open(&M);
    ord(0, FX_SELL, 0, 10, 90);
    ord(0, FX_BUY, 1, 10, 120);
    fx_clear(&M);
    CHECK(M.book[0].cur.price == 90, "F5 equidistant: lowest");
    fx_cancel(&M, 0);
    /* trade at 120 to set a reference: tie {120,130} -> 120 */
    fx_open(&M);
    ord(0, FX_SELL, 0, 10, 120);
    ord(0, FX_BUY, 1, 10, 130);
    fx_clear(&M);
    CHECK(M.book[0].cur.price == 120, "tie {120,130}: 120");
    fx_settle(&M, 0);
    CHECK(M.book[0].has_ref && M.book[0].ref == 120, "reference 120");
    fx_open(&M);
    ord(0, FX_SELL, 0, 10, 90);
    ord(0, FX_BUY, 1, 10, 120);
    fx_clear(&M);
    CHECK(M.book[0].cur.price == 120 && M.book[0].cur.ref_before == 120,
          "F5 with a reference: closest to it");
    fx_settle(&M, 0);
    /* reference 120, tie {100, 140}: equidistant -> lowest */
    fx_open(&M);
    ord(0, FX_SELL, 0, 10, 100);
    ord(0, FX_BUY, 1, 10, 140);
    fx_clear(&M);
    CHECK(M.book[0].cur.price == 100, "F5 equidistant from the reference: lowest");
    fx_cancel(&M, 0);
    /* reference below the set: closest is the lowest anyway; above: highest */
    fx_open(&M);
    ord(0, FX_SELL, 0, 10, 50);
    ord(0, FX_BUY, 1, 10, 60);
    fx_clear(&M);
    CHECK(M.book[0].cur.price == 60, "F5 reference above the set: the highest");
    fx_cancel(&M, 0);
    /* no cross */
    fx_open(&M);
    ord(0, FX_SELL, 0, 10, 200);
    ord(0, FX_BUY, 1, 10, 100);
    fx_clear(&M);
    CHECK(M.book[0].cur.volume == 0 && M.book[0].cur.price == 120 && M.book[0].cur.market == 0 &&
              !M.book[0].cur.cooling && M.n_fills == 0 && M.order[0].fill == 0,
          "F5 no cross: nothing trades, price = reference");
    uint64_t s0 = L.seq;
    fx_settle(&M, 0);
    CHECK(L.seq == s0 + 2 && escrows_home() && M.book[0].ref == 120 && fx_check(&M),
          "no-cross epoch refunds, reference unchanged");
    /* only one side */
    fx_open(&M);
    ord(0, FX_SELL, 0, 10, 200);
    ord(0, FX_SELL, 1, 10, 100);
    fx_clear(&M);
    CHECK(M.book[0].cur.volume == 0 && M.n_fills == 0, "one-sided book: nothing");
    fx_settle(&M, 0);
    /* candidates are this book's own limits only: another book's limit at
     * 100 would beat every own candidate's imbalance */
    setup(0);
    fx_open(&M);
    ord(0, FX_SELL, 0, 10, 80);
    ord(0, FX_BUY, 1, 10, 90);
    ord(0, FX_SELL, 2, 10, 110);
    ord(0, FX_BUY, 3, 10, 120);
    ord(1, FX_SELL, 4, 10, 100);
    ord(1, FX_BUY, 5, 10, 85);
    fx_clear(&M);
    CHECK(M.book[0].cur.price == 90 && M.book[0].cur.volume == 10 && M.book[0].cur.supply == 10 &&
              M.book[0].cur.demand == 20,
          "F5 tie {80,90,110,120}, midpoint 100 -> 90; foreign limits ignored");
    CHECK(M.book[1].cur.volume == 0, "the other book does not cross");
    fx_cancel(&M, 0);
    const fx_record_t *cr = fx_record(&M, 1, 0);
    CHECK(cr && cr->cancelled && cr->price == 0 && cr->ref_before == 0 && M.n_hist == 3,
          "cancelled without a reference: price 0; one record per book");
    /* a first-ever no cross records price 0 */
    setup(0);
    fx_open(&M);
    ord(1, FX_SELL, 0, 10, 200);
    ord(1, FX_BUY, 1, 10, 100);
    fx_clear(&M);
    CHECK(M.book[1].cur.price == 0 && M.book[1].cur.ref_before == 0 && !M.book[1].has_ref_next,
          "no reference and no cross: price 0");
    fx_settle(&M, 0);
    CHECK(!M.book[1].has_ref && fx_check(&M), "still no reference");
    /* max V beats a smaller imbalance */
    fx_open(&M);
    ord(0, FX_SELL, 0, 10, 90);
    ord(0, FX_SELL, 1, 10, 100);
    ord(0, FX_BUY, 2, 15, 100);
    ord(0, FX_BUY, 3, 1, 95);
    fx_clear(&M);
    /* p=90: S10 D16 V10; p=95: S10 D16; p=100: S20 D15 V15 */
    CHECK(M.book[0].cur.price == 100 && M.book[0].cur.volume == 15, "F5 max V first");
    fx_settle(&M, 0);
}

static void test_alloc(void)
{
    setup(0);
    /* leftovers in submission order: three sells of 1, a buy of 2 */
    fx_open(&M);
    ord(0, FX_SELL, 0, 1, 100);
    ord(0, FX_SELL, 1, 1, 100);
    ord(0, FX_SELL, 2, 1, 100);
    ord(0, FX_BUY, 3, 2, 100);
    fx_clear(&M);
    CHECK(M.order[0].fill == 1 && M.order[1].fill == 1 && M.order[2].fill == 0 &&
              M.order[3].fill == 2,
          "F7 leftovers one each in submission order");
    fx_settle(&M, 0);
    CHECK(escrows_home() && fx_check(&M), "settled");
    /* 7 and 5 share 10: floors 5 and 4, leftover to the first */
    fx_open(&M);
    ord(0, FX_SELL, 0, 7, 100);
    ord(0, FX_SELL, 1, 5, 100);
    ord(0, FX_BUY, 2, 10, 100);
    fx_clear(&M);
    CHECK(M.order[0].fill == 6 && M.order[1].fill == 4 && M.order[2].fill == 10, "F7 7/5 of 10");
    fx_settle(&M, 0);
    /* buy side long: buys 3 and 3 share 4 */
    fx_open(&M);
    ord(0, FX_BUY, 0, 3, 100);
    ord(0, FX_SELL, 1, 4, 100);
    ord(0, FX_BUY, 2, 3, 100);
    fx_clear(&M);
    CHECK(M.order[0].fill == 2 && M.order[2].fill == 2 && M.order[1].fill == 4,
          "F7 the buy side shares too");
    CHECK(M.n_fills == 2 && M.fill[0].buy == 0 && M.fill[1].buy == 2 && M.fill[0].qty == 2,
          "fills follow the buy order sequence");
    fx_settle(&M, 0);
    /* pairing across several orders: sells 6,4; buys 3,7 */
    fx_open(&M);
    ord(0, FX_SELL, 0, 6, 100);
    ord(0, FX_SELL, 1, 4, 100);
    ord(0, FX_BUY, 2, 3, 100);
    ord(0, FX_BUY, 3, 7, 100);
    fx_clear(&M);
    CHECK(M.n_fills == 3 && M.fill[0].sell == 0 && M.fill[0].buy == 2 && M.fill[0].qty == 3 &&
              M.fill[1].sell == 0 && M.fill[1].buy == 3 && M.fill[1].qty == 3 &&
              M.fill[2].sell == 1 && M.fill[2].buy == 3 && M.fill[2].qty == 4,
          "F8 two-pointer pairing");
    CHECK(M.book[0].cur.fills == 3 && M.book[0].cur.volume == 10, "record fills");
    fx_net_t n;
    fx_net(&M, &n);
    CHECK(n.balanced && n.gross_legs == 6 && n.n_pos == 8 && n.nonzero == 8 && n.net_legs == 6,
          "F12: 4 participants x 2 assets, 3 fills");
    uint64_t s0 = L.seq;
    fx_settle(&M, 0);
    CHECK(L.seq == s0 + 4 && lines_at(s0) == 2 && lines_at(s0 + 1) == 2 && lines_at(s0 + 2) == 2 &&
              lines_at(s0 + 3) == 2,
          "F10 zero lines omitted: full fills at limit post 2 lines each");
    CHECK(escrows_home() && fx_check(&M), "settled");
    /* a self-trade nets to zero */
    fx_open(&M);
    ord(0, FX_SELL, 0, 10, 100);
    ord(0, FX_BUY, 0, 10, 100);
    fx_clear(&M);
    fx_net(&M, &n);
    CHECK(n.balanced && n.gross_legs == 2 && n.n_pos == 2 && n.nonzero == 0 && n.net_legs == 0 &&
              n.saved == 2,
          "F12 self-trade: 2 gross legs, nothing left to settle net");
    fx_settle(&M, 0);
}

static void test_dust(void)
{
    setup(0);
    /* JPY/GBP scale 1000: 1 JPY at 1 is worth floor(1/1000) = 0 GBP minor */
    fx_open(&M);
    uint64_t sj = deb(pa[0][1]), sg = deb(pa[0][2]), bj = deb(pa[1][1]), bg = deb(pa[1][2]);
    ord(1, FX_SELL, 0, 1, 1);
    ord(1, FX_BUY, 1, 1, 500);
    fx_clear(&M);
    CHECK(M.book[1].cur.price == 1 && M.fill[0].quote == 0 && M.order[1].locked == 1,
          "F8 dust: quote value 0");
    fx_net_t dn;
    CHECK(fx_net(&M, &dn) == FX_OK && dn.gross_legs == 1 && dn.n_pos == 3 && dn.nonzero == 2 &&
              dn.net_legs == 1 && dn.saved == 0,
          "F12 a zero-value fill is one base leg");
    uint64_t s0 = L.seq;
    fx_settle(&M, 0);
    CHECK(L.seq == s0 + 1 && lines_at(s0) == 4, "the seller has nothing to move: no posting");
    CHECK(deb(pa[0][1]) == sj - 1 && deb(pa[0][2]) == sg && deb(pa[1][1]) == bj + 1 &&
              deb(pa[1][2]) == bg,
          "F8 the dust stays with the buyer (whole lock refunded)");
    CHECK(escrows_home() && fx_check(&M), "conserved");
    /* partial dust: 7 at 10 per 3 = 23.33 -> 23 */
    setup(0);
    M.book[0].scale = 3; /* white-box: a book with scale 3 */
    fx_open(&M);
    sg = deb(pa[0][1]);
    bg = deb(pa[1][1]);
    ord(0, FX_SELL, 0, 7, 10);
    ord(0, FX_BUY, 1, 7, 10);
    fx_clear(&M);
    CHECK(M.order[1].locked == 24 && M.fill[0].quote == 23, "lock ceil 24, paid floor 23");
    fx_settle(&M, 0);
    CHECK(deb(pa[0][1]) == sg + 23 && deb(pa[1][1]) == bg - 23 && escrows_home() && fx_check(&M),
          "buyer refunded 1 (the dust)");
}

static void test_fee(void)
{
    setup(0);
    uint64_t f0 = deb(feea[1]), sj = deb(pa[0][1]);
    fx_open(&M);
    ord(0, FX_SELL, 0, 1000000, 200);
    ord(0, FX_BUY, 1, 1000000, 200);
    fx_clear(&M);
    CHECK(M.fill[0].quote == 2000000 && M.fill[0].fee == 1777 && M.book[0].cur.fee == 1777 &&
              M.book[0].carry_next == 8000000 && M.book[0].carry == 0,
          "F9 fee 0.08889% of 2,000,000 = 1777, carry 8,000,000 pending");
    CHECK(fx_check(&M), "audit while CLEARED");
    fx_net_t fn;
    CHECK(fx_net(&M, &fn) == FX_OK && fn.gross_legs == 3 && fn.n_pos == 5 && fn.net_legs == 3 &&
              fn.saved == 0 && fn.balanced,
          "F12 a fill with a fee: base, proceeds and fee legs");
    uint64_t s0 = L.seq;
    fx_settle(&M, 0);
    CHECK(lines_at(s0) == 3 && lines_at(s0 + 1) == 2, "F10 sell: quote out, proceeds, fee");
    CHECK(L.seq == s0 + 2 && deb(feea[1]) == f0 + 1777 && deb(pa[0][1]) == sj + 2000000 - 1777,
          "F9 fee paid by the seller out of proceeds");
    CHECK(M.book[0].fee_total == 1777 && M.book[0].carry == 8000000 &&
              M.book[0].gross_total == 2000000,
          "F9 committed at settlement");
    fx_open(&M);
    ord(0, FX_SELL, 0, 1000000, 200);
    ord(0, FX_BUY, 1, 1000000, 200);
    fx_clear(&M);
    CHECK(M.fill[0].fee == 1778 && M.book[0].carry_next == 6000000, "F9 the carry tips it to 1778");
    fx_settle(&M, 0);
    CHECK(M.book[0].fee_total == 3555 && 3555 == (4000000ull * 8889) / 10000000 &&
              deb(feea[1]) == f0 + 3555 && fx_check(&M),
          "F9 two fills pay what one of their sum would");
    /* a cancelled CLEARED epoch charges nothing and keeps the carry */
    fx_open(&M);
    ord(0, FX_SELL, 0, 1000000, 200);
    ord(0, FX_BUY, 1, 1000000, 200);
    fx_clear(&M);
    CHECK(M.book[0].carry_next != M.book[0].carry, "pending carry");
    fx_cancel(&M, 0);
    CHECK(M.book[0].fee_total == 3555 && M.book[0].carry == 6000000 && deb(feea[1]) == f0 + 3555 &&
              fx_check(&M),
          "F9 cancel charges no fee");
    /* many small fills: the carry is exact */
    setup(0);
    unsigned __int128 gross = 0;
    for (int e = 0; e < 5; e++) {
        fx_open(&M);
        for (int k = 0; k < 4; k++) ord(0, FX_SELL, k, 1000 + 37 * k, 113);
        ord(0, FX_BUY, 4, 3000, 113);
        ord(0, FX_BUY, 5, 1300, 113);
        fx_clear(&M);
        gross += M.book[0].cur.gross;
        fx_settle(&M, 0);
        CHECK(fx_check(&M), "audit");
    }
    CHECK(M.book[0].gross_total == (uint64_t) gross &&
              M.book[0].fee_total == (uint64_t) (gross * 8889 / 10000000) &&
              M.book[0].carry == (uint64_t) (gross * 8889 % 10000000),
          "F9 fee_total == floor(gross_total * 8889 / 10^7)");
    /* a corrupt carry fails the clear (fail closed), nothing kept */
    fx_open(&M);
    ord(0, FX_SELL, 0, 10, 100);
    ord(0, FX_BUY, 1, 10, 100);
    uint64_t c = M.book[0].carry;
    M.book[0].carry = PAY_ASSURE_DEN;
    CHECK(fx_clear(&M) == FX_ERR_STATE && M.state == FX_OPEN && M.n_fills == 0 &&
              M.order[0].fill == 0 && M.order[1].fill == 0 && M.order[0].quote == 0,
          "corrupt carry: clear refused, no fills kept");
    M.book[0].carry = c;
    CHECK(fx_clear(&M) == FX_OK && M.n_fills == 1, "clear after repair");
    fx_settle(&M, 0);
    CHECK(fx_check(&M), "audit");
}

static void test_overflow(void)
{
    setup(0);
    for (int which = 0; which < 3; which++) {
        fx_open(&M);
        ord(0, FX_SELL, 0, 1000, 100);
        ord(0, FX_BUY, 1, 1000, 100);
        ord(1, FX_SELL, 2, 10000000, 1000);
        ord(1, FX_BUY, 3, 10000000, 1000);
        fx_book_t *b = &M.book[1];
        uint64_t save[3] = {b->gross_total, b->fee_total, b->volume_total};
        if (which == 0) b->gross_total = UINT64_MAX - 5;
        if (which == 1) b->fee_total = UINT64_MAX;
        if (which == 2) b->volume_total = UINT64_MAX - 9;
        CHECK(fx_clear(&M) == FX_ERR_OVERFLOW && M.state == FX_OPEN && M.n_fills == 0 &&
                  M.order[0].fill == 0 && M.order[2].fill == 0 && M.order[0].quote == 0 &&
                  M.order[0].fee == 0,
              "a lifetime counter would overflow: clear refused, no fills kept");
        b->gross_total = save[0];
        b->fee_total = save[1];
        b->volume_total = save[2];
        CHECK(fx_check(&M) && fx_cancel(&M, 0) == FX_OK && escrows_home(), "cancel after");
    }
    /* exactly at the edge is fine */
    fx_open(&M);
    ord(1, FX_SELL, 2, 10, 100);
    ord(1, FX_BUY, 3, 10, 100);
    M.book[1].volume_total = UINT64_MAX - 10;
    CHECK(fx_clear(&M) == FX_OK, "volume at exactly UINT64_MAX fits");
    M.book[1].volume_total = 0;
    fx_cancel(&M, 0);
}

static void test_cooling(void)
{
    setup(500);
    fx_open(&M);
    ord(0, FX_SELL, 0, 10, 1000);
    ord(0, FX_BUY, 1, 10, 1000);
    fx_clear(&M);
    CHECK(M.book[0].cur.price == 1000 && !M.book[0].cur.cooling, "no reference: no band");
    fx_settle(&M, 0);
    /* the market moves to 1200..1300 */
    uint64_t want[] = {1050, 1102, 1157};
    for (int e = 0; e < 3; e++) {
        fx_open(&M);
        ord(0, FX_SELL, 0, 10, 1200);
        ord(0, FX_BUY, 1, 10, 1300);
        fx_clear(&M);
        const fx_record_t *r = &M.book[0].cur;
        CHECK(r->market == 1200 && r->price == want[e] && r->cooling && r->volume == 0 &&
                  r->supply == 0 && r->demand == 10 && M.n_fills == 0,
              "F6 COOLING: clamped to the band, recomputed volume 0");
        fx_settle(&M, 0);
        CHECK(M.book[0].ref == want[e] && escrows_home() && fx_check(&M),
              "F6 the clamped price becomes the reference (walks, no jump)");
        CHECK(fx_record(&M, M.epoch, 0)->cooling, "F13 record flags cooling");
    }
    fx_open(&M);
    ord(0, FX_SELL, 0, 10, 1200);
    ord(0, FX_BUY, 1, 10, 1300);
    fx_clear(&M);
    CHECK(M.book[0].cur.price == 1200 && !M.book[0].cur.cooling && M.book[0].cur.volume == 10,
          "F6 the market is inside the band again: trades at p*");
    fx_settle(&M, 0);
    /* clamp from below with a trade at the clamp */
    fx_open(&M);
    ord(0, FX_SELL, 0, 10, 900);
    ord(0, FX_BUY, 1, 10, 1300);
    fx_clear(&M);
    CHECK(M.book[0].cur.market == 1300 && M.book[0].cur.price == 1260 && M.book[0].cur.cooling &&
              M.book[0].cur.volume == 10,
          "F6 clamp to the upper bound with a trade");
    fx_settle(&M, 0);
    fx_open(&M);
    ord(0, FX_SELL, 0, 10, 600);
    ord(0, FX_BUY, 1, 10, 700);
    fx_clear(&M);
    CHECK(M.book[0].cur.market == 700 && M.book[0].cur.price == 1197 && M.book[0].cur.cooling &&
              M.book[0].cur.volume == 0 && M.book[0].cur.supply == 10 && M.book[0].cur.demand == 0,
          "F6 clamp to the lower bound (ceil 1197)");
    fx_settle(&M, 0);
    fx_open(&M);
    ord(0, FX_SELL, 0, 10, 1000);
    ord(0, FX_BUY, 1, 10, 1300);
    fx_clear(&M);
    CHECK(M.book[0].cur.market == 1300 && M.book[0].cur.price == 1256 &&
              M.book[0].cur.volume == 10 && M.book[0].cur.cooling,
          "F6 upward clamp, trade inside both limits");
    fx_settle(&M, 0);
    CHECK(fx_check(&M), "audit");
    /* lower-bound clamp with a trade */
    fx_open(&M);
    ord(0, FX_SELL, 0, 10, 600);
    ord(0, FX_BUY, 1, 10, 1250);
    fx_clear(&M);
    CHECK(M.book[0].cur.market == 1250 && !M.book[0].cur.cooling, "within band");
    fx_cancel(&M, 0);
    fx_open(&M);
    ord(0, FX_SELL, 0, 10, 600);
    ord(0, FX_BUY, 1, 10, 1100);
    fx_clear(&M);
    CHECK(M.book[0].cur.market == 1100 && M.book[0].cur.price == 1194 && M.book[0].cur.cooling &&
              M.book[0].cur.volume == 0,
          "F6 lower clamp above the buy limit: nothing");
    fx_cancel(&M, 0);
    CHECK(M.book[0].ref == 1256, "a cancelled cooling epoch does not move the reference");
    fx_open(&M);
    ord(0, FX_SELL, 0, 10, 600);
    ord(0, FX_BUY, 1, 10, 1200);
    ord(0, FX_BUY, 2, 1, 600);
    fx_clear(&M);
    /* p: 600 (S10 D11 V10 imb1), 1200 (S10 D10 V10 imb0) -> 1200, band [1194,1318] */
    CHECK(M.book[0].cur.price == 1200 && !M.book[0].cur.cooling, "inside");
    fx_cancel(&M, 0);
    /* band 0 disables the clamp */
    fx_config_t c = on_cfg(0);
    fx_set_config(&M, &c);
    fx_open(&M);
    ord(0, FX_SELL, 0, 10, 5000);
    ord(0, FX_BUY, 1, 10, 5000);
    fx_clear(&M);
    CHECK(M.book[0].cur.price == 5000 && !M.book[0].cur.cooling, "band 0: no clamp");
    fx_settle(&M, 0);
}

static void test_settle_resume(void)
{
    setup(0);
    fx_open(&M);
    ord(0, FX_SELL, 0, 100, 100);
    ord(0, FX_BUY, 1, 60, 100);
    ord(0, FX_BUY, 2, 60, 100);
    ord(2, FX_SELL, 3, 50, 100);
    ord(2, FX_BUY, 4, 50, 100);
    fx_clear(&M);
    block_e2e(M.seq);
    uint64_t s0 = L.seq;
    CHECK(fx_settle(&M, 0) == FX_ERR_LEDGER && M.state == FX_CLEARED && M.mode == 1 &&
              M.last_ledger != PAY_OK,
          "F10 a refused posting stops settlement");
    CHECK(L.seq == s0 && !M.order[0].settled, "nothing posted");
    CHECK(fx_check(&M), "audit mid-settlement");
    CHECK(fx_cancel(&M, 0) == FX_ERR_STATE, "cancel refused once settlement began");
    block_e2e(M.seq + 2); /* the third posting fails */
    CHECK(fx_settle(&M, 0) == FX_ERR_LEDGER && M.order[0].settled && M.order[1].settled &&
              !M.order[2].settled && M.state == FX_CLEARED,
          "F10 resumes, stops again part-way");
    CHECK(fx_check(&M), "F13 audit holds part-way (escrows and pending fees)");
    fx_net_t n;
    CHECK(fx_net(&M, &n) == FX_OK && n.balanced, "net while CLEARED");
    pay_ledger_set_flags(&L, esc[2][1], PAY_ACCT_FROZEN);
    CHECK(fx_settle(&M, 0) == FX_ERR_LEDGER && M.order[2].settled && !M.order[3].settled,
          "a frozen escrow stops settlement");
    CHECK(fx_check(&M), "audit");
    pay_ledger_set_flags(&L, esc[2][1], 0);
    CHECK(fx_settle(&M, 0) == FX_OK && M.state == FX_SETTLED && escrows_home() && fx_check(&M),
          "F10 settlement completes, escrows home");
    /* cancel resumes likewise */
    fx_open(&M);
    ord(0, FX_SELL, 0, 100, 100);
    ord(0, FX_BUY, 1, 60, 100);
    ord(0, FX_BUY, 2, 60, 100);
    ord(2, FX_SELL, 3, 50, 100);
    ord(2, FX_BUY, 4, 50, 100);
    fx_clear(&M);
    CHECK(M.book[2].cur.volume == 50, "book 2 trades too");
    block_e2e(M.seq + 1);
    CHECK(fx_cancel(&M, 0) == FX_ERR_LEDGER && M.state == FX_CLEARED && M.mode == 2 &&
              M.order[0].settled && !M.order[1].settled && M.n_fills == 0,
          "F11 cancel stops part-way");
    CHECK(fx_check(&M), "audit mid-cancel");
    CHECK(fx_settle(&M, 0) == FX_ERR_STATE, "settle refused once cancel began");
    CHECK(fx_clear(&M) == FX_ERR_STATE, "clear refused once cancel began");
    CHECK(ord(0, FX_SELL, 0, 1, 1) == FX_ERR_STATE, "orders refused once cancel began");
    CHECK(fx_cancel(&M, 0) == FX_OK && M.state == FX_SETTLED && escrows_home() && fx_check(&M),
          "F11 cancel completes");
    /* cancel while OPEN */
    fx_open(&M);
    ord(1, FX_BUY, 0, 100, 7);
    CHECK(fx_cancel(&M, 0) == FX_OK && escrows_home() && fx_record(&M, M.epoch, 1)->cancelled,
          "cancel while OPEN");
}

/* audit corruption: each field breaks fx_check */
static void test_audit(void)
{
    setup(0);
    fx_open(&M);
    ord(0, FX_SELL, 0, 1000000, 200);
    ord(0, FX_SELL, 1, 7, 200);
    ord(0, FX_BUY, 2, 1000000, 210);
    ord(1, FX_SELL, 3, 5000, 300);
    ord(1, FX_BUY, 4, 5000, 300);
    fx_clear(&M);
    CHECK(fx_check(&M), "baseline clean");
    CHECK(M.order[2].fill > 0 && M.book[1].cur.volume > 0 && M.book[0].cur.fee > 0,
          "audit fixture: later orders and books traded");
#define CORRUPT(lv, val, msg)                                                                      \
    do {                                                                                           \
        __typeof__(lv) _s = (lv);                                                                  \
        (lv) = (val);                                                                              \
        CHECK(!fx_check(&M), msg);                                                                 \
        (lv) = _s;                                                                                 \
    } while (0)
    fx_book_t *b = &M.book[0];
    CORRUPT(b->base_start, b->base_start + 1, "base escrow baseline");
    CORRUPT(b->quote_start, b->quote_start + 1, "quote escrow baseline");
    CORRUPT(b->fee_start, b->fee_start + 1, "fee baseline");
    CORRUPT(b->carry, PAY_ASSURE_DEN, "carry out of range");
    CORRUPT(b->carry, 1, "carry vs totals");
    CORRUPT(b->fee_total, 1, "fee total vs gross");
    CORRUPT(b->gross_total, 1, "gross total vs fee");
    CORRUPT(b->escrow_base, pa[0][0], "escrow account swapped");
    CORRUPT(b->escrow_quote, L.n_accounts, "quote escrow missing");
    CORRUPT(b->fee_acct, L.n_accounts, "fee account missing");
    CORRUPT(b->cur.volume, b->cur.volume + 1, "record volume");
    CORRUPT(b->cur.gross, b->cur.gross + 1, "record gross");
    CORRUPT(b->cur.fee, b->cur.fee + 1, "record fee");
    CORRUPT(b->cur.price, b->cur.price + 1, "fill quote vs price");
    CORRUPT(b->scale, b->scale + 1, "scale");
    fx_order_t *o = &M.order[0];
    CORRUPT(o->locked, o->locked + 1, "sell lock");
    CORRUPT(o->fill, o->fill + 1, "order fill");
    CORRUPT(o->fill, o->qty + 1, "fill above qty");
    CORRUPT(o->quote, o->quote + 1, "order quote");
    CORRUPT(o->fee, o->fee + 1, "order fee");
    CORRUPT(o->fee, o->quote + 1, "fee above quote");
    CORRUPT(o->book, 7, "order book");
    CORRUPT(o->side, 5, "order side");
    CORRUPT(o->qty, 0, "order qty 0");
    CORRUPT(o->limit, 0, "order limit 0");
    o = &M.order[2];
    CORRUPT(o->locked, o->locked + 1, "buy lock");
    CORRUPT(o->limit, o->limit + 1, "buy limit vs lock");
    CORRUPT(o->quote, o->locked + 1, "paid above lock");
    CORRUPT(o->fee, 1, "a buy pays no fee");
    CORRUPT(o->side, FX_SELL, "side flipped");
    CORRUPT(M.order[2].fill, M.order[2].fill - 1, "an unsettled buy's fill");
    CORRUPT(M.order[2].quote, M.order[2].quote - 1, "an unsettled buy's payment");
    CORRUPT(M.order[4].fill, M.order[4].fill - 1, "a later book's order fill");
    CORRUPT(M.book[1].cur.volume, M.book[1].cur.volume + 1, "book 1 record volume");
    CORRUPT(M.book[1].cur.gross, M.book[1].cur.gross + 1, "book 1 record gross");
    CORRUPT(M.book[1].carry, 1, "book 1 carry");
    CORRUPT(M.book[2].base_start, 1, "book 2 baseline");
    CORRUPT(M.order[3].book, 3, "order book == n_books");
    {
        /* carry == 10^7 exactly, with the F9 identity and the fee account's
         * balance both kept: 8888 * 10^7 + 10^7 == 10^7 * 8889 */
        fx_book_t *b1 = &M.book[1];
        uint64_t c = b1->carry, ft = b1->fee_total, fs = b1->fee_start, gt = b1->gross_total;
        b1->carry = PAY_ASSURE_DEN;
        b1->gross_total = PAY_ASSURE_DEN;
        b1->fee_total = 8888;
        b1->fee_start = fs + ft - 8888; /* mod 2^64, like the audit */
        CHECK(!fx_check(&M), "carry == 10^7 caught by its range check alone");
        b1->carry = 0;
        b1->fee_total = 8889;
        b1->fee_start = fs + ft - 8889;
        CHECK(fx_check(&M), "the same totals with carry 0 are consistent");
        b1->carry = c;
        b1->fee_total = ft;
        b1->fee_start = fs;
        b1->gross_total = gt;
        CHECK(fx_check(&M), "restored");
    }
    fx_fill_t *f = &M.fill[0];
    CORRUPT(f->qty, 0, "fill qty 0");
    CORRUPT(f->qty, f->qty + 1, "fill qty");
    CORRUPT(f->quote, f->quote + 1, "fill quote");
    CORRUPT(f->fee, f->fee + 1, "fill fee");
    CORRUPT(f->fee, f->quote + 1, "fill fee above quote");
    CORRUPT(f->sell, 2, "fill sell is a buy");
    CORRUPT(f->buy, 0, "fill buy is a sell");
    CORRUPT(f->sell, 3, "fill sell of another book");
    CORRUPT(f->buy, 4, "fill buy of another book");
    CORRUPT(f->book, 1, "fill book");
    CORRUPT(f->book, 9, "fill book unknown");
    CORRUPT(f->sell, 99, "fill sell unknown");
    CORRUPT(f->buy, 99, "fill buy unknown");
    CORRUPT(M.n_fills, FX_MAX_FILLS + 1, "too many fills");
    CORRUPT(M.n_fills, M.n_fills - 1, "a fill dropped");
    CORRUPT(M.n_books, FX_MAX_BOOKS + 1, "too many books");
    CORRUPT(M.n_orders, FX_MAX_ORDERS + 1, "too many orders");
    CORRUPT(M.ready, false, "not ready");
    CORRUPT(M.state, FX_OPEN, "fills while OPEN");
    {
        uint32_t s = M.order[2].owner;
        M.order[2].owner = 4;
        CHECK(fx_check(&M), "an owner change alone keeps every asset balanced");
        M.order[2].owner = s;
    }
    fx_settle(&M, 0);
    CHECK(fx_check(&M), "settled clean");
    b = &M.book[0];
    CORRUPT(b->base_start, b->base_start - 1 + 2, "escrow after settle");
    CORRUPT(M.order[0].settled, false, "an order not settled");
    CORRUPT(M.order[2].settled, false, "a buy not settled");
    CORRUPT(M.order[0].fill, 0, "sell fill after settle");
    CORRUPT(M.order[2].fill, 0, "buy fill after settle");
    CORRUPT(M.state, FX_CLEARED, "fees double counted as pending");
    /* underflow paths of the expected balance */
    CORRUPT(M.order[0].quote, UINT64_MAX, "quote underflow");
    CORRUPT(M.order[2].fill, UINT64_MAX, "base underflow");
    CORRUPT(M.order[0].locked, UINT64_MAX, "lock overflow");
    CORRUPT(M.order[2].locked, UINT64_MAX, "buy lock overflow");
    CORRUPT(M.book[0].fee_start, UINT64_MAX, "fee baseline overflow");
    CORRUPT(M.book[0].fee_total, UINT64_MAX, "fee total overflow");
    /* a ledger out of balance */
    L.acct[pa[0][0]].debit++;
    CHECK(!fx_check(&M), "ledger imbalance caught");
    L.acct[pa[0][0]].debit--;
    L.acct[esc[0][0]].credit = 1;
    CHECK(!fx_check(&M), "escrow credit caught");
    L.acct[esc[0][0]].credit = 0;
    CHECK(fx_check(&M), "clean again");
    /* pending fee of a settled sell while CLEARED */
    fx_open(&M);
    ord(0, FX_SELL, 0, 1000000, 200);
    ord(0, FX_BUY, 2, 1000000, 200);
    fx_clear(&M);
    block_e2e(M.seq + 1);
    fx_settle(&M, 0);
    CHECK(M.order[0].settled && !M.order[1].settled && fx_check(&M),
          "pending fee counted while CLEARED");
    CORRUPT(M.order[0].fee, M.order[0].fee + 1, "pending fee mismatch");
    uint64_t fsave = M.order[0].fee;
    M.order[0].fee = UINT64_MAX;
    M.fill[0].fee = UINT64_MAX;
    CHECK(!fx_check(&M), "pending fee overflow");
    M.order[0].fee = M.fill[0].fee = fsave;
    fx_settle(&M, 0);
    CHECK(fx_check(&M), "settled clean");
    /* net: an imbalanced fill set */
    fx_net_t n;
    M.fill[0].quote += 0; /* unchanged */
    M.order[1].owner = 77;
    CHECK(fx_net(&M, &n) == FX_OK && n.balanced, "owners only relabel");
    M.order[1].owner = 102;
    M.book[0].fee_acct = L.n_accounts;
    CHECK(fx_net(&M, &n) == FX_ERR_STATE, "net with a missing fee account");
    M.book[0].fee_acct = feea[1];
#undef CORRUPT
}

static void test_max_orders(void)
{
    setup(0);
    fx_open(&M);
    for (uint32_t i = 0; i < FX_MAX_ORDERS; i++)
        CHECK(ord(i % NBK, (uint8_t) (i & 1), (int) (i % NP), 10 + i, 95 + i % 11) == FX_OK,
              "an order up to the limit");
    CHECK(M.n_orders == FX_MAX_ORDERS && fx_check(&M), "a full epoch audits clean");
    CHECK(ord(0, FX_SELL, 0, 1, 100) == FX_ERR_FULL, "one more: full");
    CHECK(fx_clear(&M) == FX_OK && fx_check(&M), "cleared, clean");
    fx_net_t n;
    CHECK(fx_net(&M, &n) == FX_OK && n.balanced, "net");
    CHECK(fx_settle(&M, 0) == FX_OK && escrows_home() && fx_check(&M), "settled, clean");
}

static void test_net_capacity(void)
{
    fx_net_t n;
    setup(0);
    /* positions near the int64 edge */
    fx_open(&M);
    ord(0, FX_SELL, 0, 10, 100);
    ord(0, FX_BUY, 1, 10, 100);
    ord(2, FX_SELL, 0, 10, 100);
    ord(2, FX_BUY, 1, 10, 100);
    fx_clear(&M);
    CHECK(fx_net(&M, &n) == FX_OK && n.balanced && n.gross_legs == 4 && n.n_pos == 6,
          "two books share EUR: P0 and P1 net per asset");
    /* P0: EUR -10 (book0 base) + 10 (book2 quote) = 0; GBP -10; JPY +10 */
    int zeros = 0;
    for (uint32_t i = 0; i < n.n_pos; i++) zeros += n.pos[i].net == 0;
    CHECK(zeros == 2 && n.nonzero == 4 && n.net_legs == 2 && n.saved == 2,
          "F12 multilateral: the EUR legs cancel, 4 gross legs -> 2 net");
    M.fill[0].qty = (uint64_t) INT64_MAX;
    M.fill[1].qty = (uint64_t) INT64_MAX;
    M.fill[1].book = 0;
    CHECK(fx_net(&M, &n) == FX_ERR_OVERFLOW, "position overflow refused");
    fx_cancel(&M, 0);
    /* per-asset sum overflow with every position in range:
     * P: -1 + y, Q: +1 + z, R: -y, S: -z with y + z >= 2^63 */
    setup(0);
    fx_open(&M);
    ord(0, FX_SELL, 0, 1, 100); /* 0: P sells   */
    ord(0, FX_BUY, 1, 1, 100);  /* 1: Q buys    */
    ord(0, FX_SELL, 2, 1, 100); /* 2: R sells   */
    ord(0, FX_BUY, 0, 1, 100);  /* 3: P buys    */
    ord(0, FX_SELL, 3, 1, 100); /* 4: S sells   */
    ord(0, FX_BUY, 1, 1, 100);  /* 5: Q buys    */
    fx_clear(&M);
    {
        uint64_t y = ((uint64_t) 1 << 62) + 1;
        fx_fill_t f0 = {0, 0, 1, 1, 0, 0}, f1 = {0, 2, 3, y, 0, 0}, f2 = {0, 4, 5, y, 0, 0};
        M.fill[0] = f0;
        M.fill[1] = f1;
        M.fill[2] = f2;
        M.n_fills = 3;
        CHECK(fx_net(&M, &n) == FX_ERR_OVERFLOW, "F12 a per-asset sum overflow is refused");
        M.fill[1].qty = M.fill[2].qty = (uint64_t) 1 << 61;
        CHECK(fx_net(&M, &n) == FX_OK && n.balanced, "just inside: fine");
    }
    fx_cancel(&M, 0);
    /* capacity: more distinct positions than FX_NET_MAX */
    setup(0);
    fx_open(&M);
    ord(0, FX_SELL, 0, 10, 100);
    ord(0, FX_BUY, 1, 10, 100);
    fx_clear(&M);
    M.n_fills = FX_MAX_FILLS;
    for (uint32_t i = 0; i < FX_MAX_FILLS; i++) M.fill[i] = M.fill[0];
    for (uint32_t i = 2; i < FX_MAX_ORDERS; i++) {
        M.order[i] = M.order[i & 1];
        M.order[i].owner = 1000 + i;
    }
    M.n_orders = FX_MAX_ORDERS;
    for (uint32_t i = 0; i < FX_MAX_FILLS; i++) {
        M.fill[i].sell = (2 * i) % FX_MAX_ORDERS;
        M.fill[i].buy = (2 * i + 1) % FX_MAX_ORDERS;
    }
    CHECK(fx_net(&M, &n) == FX_OK && n.n_pos == 2 * FX_MAX_ORDERS && n.balanced,
          "the bound holds every order's two positions");
    M.order[0].owner = 5000;
    for (uint32_t i = 0; i < FX_MAX_FILLS; i++) M.fill[i].fee = 1;
    CHECK(fx_net(&M, &n) == FX_OK && n.n_pos == 2 * FX_MAX_ORDERS + 1, "plus a fee owner");
    M.fill[1].book = 1; /* touches new (owner, asset) pairs beyond the bound */
    M.fill[2].book = 2;
    M.fill[3].book = 1;
    M.fill[4].book = 2;
    M.fill[5].book = 1;
    M.fill[6].book = 2;
    CHECK(fx_net(&M, &n) == FX_ERR_FULL, "beyond the bound: FULL");
}

/* ===================================================================== */
/* randomized property loop against an independent reference              */

static uint64_t rs = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 7;
    rs ^= rs << 17;
    return rs;
}
static uint64_t rr(uint64_t n)
{
    return n ? rnd() % n : 0;
}

typedef struct {
    bool has_ref;
    uint64_t ref, carry, fee_total, gross_total;
} refbook_t;

/* a reference clearing of one book from the orders' inputs only */
static void ref_book(uint32_t bi, const refbook_t *rb, uint32_t band, uint64_t *price, uint64_t *V,
                     bool *cool, uint64_t fill[])
{
    uint64_t cand[FX_MAX_ORDERS];
    int nc = 0;
    for (uint32_t i = 0; i < M.n_orders; i++) {
        if (M.order[i].book != bi) continue;
        int dup = 0;
        for (int k = 0; k < nc; k++) dup |= cand[k] == M.order[i].limit;
        if (!dup) cand[nc++] = M.order[i].limit;
    }
    for (int a = 0; a < nc; a++) /* sort ascending */
        for (int c = a + 1; c < nc; c++)
            if (cand[c] < cand[a]) {
                uint64_t t = cand[a];
                cand[a] = cand[c];
                cand[c] = t;
            }
    unsigned __int128 S[FX_MAX_ORDERS], D[FX_MAX_ORDERS];
    for (int k = 0; k < nc; k++) {
        S[k] = D[k] = 0;
        for (uint32_t i = 0; i < M.n_orders; i++) {
            const fx_order_t *o = &M.order[i];
            if (o->book != bi) continue;
            if (o->side == FX_SELL && o->limit <= cand[k]) S[k] += o->qty;
            if (o->side == FX_BUY && o->limit >= cand[k]) D[k] += o->qty;
        }
    }
    *price = rb->has_ref ? rb->ref : 0;
    *V = 0;
    *cool = false;
    for (uint32_t i = 0; i < M.n_orders; i++) fill[i] = 0;
    /* best key: (V desc, imb asc) */
    int best = -1;
    for (int k = 0; k < nc; k++) {
        unsigned __int128 v = S[k] < D[k] ? S[k] : D[k];
        unsigned __int128 imb = S[k] > D[k] ? S[k] - D[k] : D[k] - S[k];
        if (v == 0) continue;
        if (best < 0) {
            best = k;
            continue;
        }
        unsigned __int128 bv = S[best] < D[best] ? S[best] : D[best];
        unsigned __int128 bi2 = S[best] > D[best] ? S[best] - D[best] : D[best] - S[best];
        if (v > bv || (v == bv && imb < bi2)) best = k;
    }
    if (best < 0) return;
    unsigned __int128 bv = S[best] < D[best] ? S[best] : D[best];
    unsigned __int128 bimb = S[best] > D[best] ? S[best] - D[best] : D[best] - S[best];
    int tie[FX_MAX_ORDERS], nt = 0;
    for (int k = 0; k < nc; k++) {
        unsigned __int128 v = S[k] < D[k] ? S[k] : D[k];
        unsigned __int128 imb = S[k] > D[k] ? S[k] - D[k] : D[k] - S[k];
        if (v == bv && imb == bimb) tie[nt++] = k;
    }
    unsigned __int128 r =
        rb->has_ref ? rb->ref : ((unsigned __int128) cand[tie[0]] + cand[tie[nt - 1]]) / 2;
    uint64_t p = cand[tie[0]];
    unsigned __int128 bd = (unsigned __int128) -1;
    for (int t = 0; t < nt; t++) { /* ascending: strict < keeps the lowest */
        unsigned __int128 c = cand[tie[t]];
        unsigned __int128 d = c > r ? c - r : r - c;
        if (d < bd) {
            bd = d;
            p = cand[tie[t]];
        }
    }
    if (band && rb->has_ref) {
        unsigned __int128 lo = ((unsigned __int128) rb->ref * (10000 - band) + 9999) / 10000;
        unsigned __int128 hi = (unsigned __int128) rb->ref * (10000 + band) / 10000;
        if (p < lo) {
            p = (uint64_t) lo;
            *cool = true;
        }
        if (p > hi) {
            p = (uint64_t) hi;
            *cool = true;
        }
    }
    *price = p;
    unsigned __int128 s = 0, d = 0;
    for (uint32_t i = 0; i < M.n_orders; i++) {
        const fx_order_t *o = &M.order[i];
        if (o->book != bi) continue;
        if (o->side == FX_SELL && o->limit <= p) s += o->qty;
        if (o->side == FX_BUY && o->limit >= p) d += o->qty;
    }
    unsigned __int128 v = s < d ? s : d;
    *V = (uint64_t) v;
    for (int side = 0; side < 2; side++) {
        unsigned __int128 tot = side == FX_SELL ? s : d, given = 0;
        if (!v) break;
        for (uint32_t i = 0; i < M.n_orders; i++) {
            const fx_order_t *o = &M.order[i];
            bool el = o->side == FX_SELL ? o->limit <= p : o->limit >= p;
            if (o->book != bi || o->side != side || !el) continue;
            fill[i] = (uint64_t) (v * o->qty / tot);
            given += fill[i];
        }
        for (uint32_t i = 0; i < M.n_orders && given < v; i++) {
            const fx_order_t *o = &M.order[i];
            bool el = o->side == FX_SELL ? o->limit <= p : o->limit >= p;
            if (o->book != bi || o->side != side || !el) continue;
            fill[i]++;
            given++;
        }
    }
}

static void test_random(void)
{
    setup(0);
    refbook_t rb[NBK];
    memset(rb, 0, sizeof rb);
    int epochs = 600, bad_clear = 0, bad_settle = 0, bad_net = 0, cooled = 0, traded = 0,
        cancelled = 0;
    uint64_t saved_total = 0;
    for (int e = 0; e < epochs; e++) {
        uint32_t band = (uint32_t) (rr(4) == 0 ? 0 : 50 * (1 + rr(10)));
        fx_config_t c = on_cfg(band);
        c.max_orders = (uint32_t) (1 + rr(FX_MAX_ORDERS));
        if (fx_set_config(&M, &c) != FX_OK) bad_clear++;
        if (fx_open(&M) != FX_OK) bad_clear++;
        uint64_t before[NP][NA], fbefore[NA];
        for (int p = 0; p < NP; p++)
            for (int a = 0; a < NA; a++) before[p][a] = deb(pa[p][a]);
        for (int a = 0; a < NA; a++) fbefore[a] = deb(feea[a]);
        int n = (int) rr(c.max_orders + 3);
        for (int k = 0; k < n; k++) {
            uint32_t book = (uint32_t) rr(NBK);
            uint8_t side = (uint8_t) rr(2);
            int who = (int) rr(NP);
            uint64_t qty = 1 + rr(rr(3) ? 3000 : 3000000);
            uint64_t mid = rb[book].has_ref ? rb[book].ref : 100;
            uint64_t lim = mid / 2 + 1 + rr(mid + 20);
            fx_status_t st = ord(book, side, who, qty, lim);
            bool want_full = M.n_orders >= c.max_orders;
            if (st != FX_OK && !(st == FX_ERR_FULL && want_full)) bad_clear++;
        }
        if (!fx_check(&M)) bad_clear++;
        if (rr(10) == 0) { /* cancel while OPEN */
            if (fx_cancel(&M, 0) != FX_OK) bad_settle++;
            cancelled++;
        } else {
            uint64_t price[NBK], V[NBK], fill[NBK][FX_MAX_ORDERS];
            bool cool[NBK];
            for (uint32_t b = 0; b < NBK; b++)
                ref_book(b, &rb[b], band, &price[b], &V[b], &cool[b], fill[b]);
            if (fx_clear(&M) != FX_OK) bad_clear++;
            for (uint32_t b = 0; b < NBK; b++) {
                const fx_record_t *r = &M.book[b].cur;
                if (r->price != price[b] || r->volume != V[b] || r->cooling != cool[b]) bad_clear++;
                cooled += cool[b];
                traded += V[b] != 0;
            }
            for (uint32_t i = 0; i < M.n_orders; i++)
                if (M.order[i].fill != fill[M.order[i].book][i]) bad_clear++;
            /* reference pairing and fees */
            uint64_t carry[NBK], fees[NBK], gross[NBK];
            for (uint32_t b = 0; b < NBK; b++) {
                carry[b] = rb[b].carry;
                fees[b] = gross[b] = 0;
            }
            uint32_t nf = 0;
            for (uint32_t b = 0; b < NBK; b++) {
                uint32_t s = 0, d = 0;
                uint64_t rsell = 0, rbuy = 0;
                for (;;) {
                    while (!rsell && s < M.n_orders) {
                        if (M.order[s].book == b && M.order[s].side == FX_SELL) rsell = fill[b][s];
                        if (!rsell) s++;
                    }
                    while (!rbuy && d < M.n_orders) {
                        if (M.order[d].book == b && M.order[d].side == FX_BUY) rbuy = fill[b][d];
                        if (!rbuy) d++;
                    }
                    if (!rsell || !rbuy) break;
                    uint64_t x = rsell < rbuy ? rsell : rbuy;
                    uint64_t q = (uint64_t) ((unsigned __int128) x * price[b] / SCALE[b]);
                    unsigned __int128 t = (unsigned __int128) q * 8889 + carry[b];
                    uint64_t fee = (uint64_t) (t / 10000000);
                    carry[b] = (uint64_t) (t % 10000000);
                    const fx_fill_t *f = &M.fill[nf++];
                    if (f->book != b || f->sell != s || f->buy != d || f->qty != x ||
                        f->quote != q || f->fee != fee)
                        bad_clear++;
                    fees[b] += fee;
                    gross[b] += q;
                    rsell -= x;
                    rbuy -= x;
                    if (!rsell) s++;
                    if (!rbuy) d++;
                }
            }
            if (nf != M.n_fills) bad_clear++;
            if (!fx_check(&M)) bad_clear++;
            fx_net_t net;
            if (fx_net(&M, &net) != FX_OK || !net.balanced) bad_net++;
            saved_total += net.saved;
            if (rr(12) == 0) { /* cancel after clearing */
                if (fx_cancel(&M, 0) != FX_OK) bad_settle++;
                cancelled++;
                if (!escrows_home()) bad_settle++;
                for (int p = 0; p < NP; p++)
                    for (int a = 0; a < NA; a++)
                        if (deb(pa[p][a]) != before[p][a]) bad_settle++;
            } else {
                if (rr(8) == 0) { /* a failure part-way, then resume */
                    uint64_t blk = M.seq + rr(4);
                    block_e2e(blk);
                    fx_status_t st = fx_settle(&M, 0);
                    if (st != FX_OK && (st != FX_ERR_LEDGER || !fx_check(&M))) bad_settle++;
                    if (st == FX_OK && M.seq <= blk) M.seq = blk + 1; /* skip the unused block */
                }
                if (M.state == FX_CLEARED && fx_settle(&M, 0) != FX_OK) bad_settle++;
                if (!escrows_home()) bad_settle++;
                /* every participant's ledger change == its net position */
                for (int p = 0; p < NP; p++)
                    for (int a = 0; a < NA; a++) {
                        int64_t want = 0;
                        for (uint32_t i = 0; i < net.n_pos; i++)
                            if (net.pos[i].owner == (uint32_t) (100 + p) &&
                                net.pos[i].asset == asset[a])
                                want = net.pos[i].net;
                        if ((int64_t) (deb(pa[p][a]) - before[p][a]) != want) bad_net++;
                    }
                for (int a = 0; a < NA; a++) {
                    int64_t want = 0;
                    for (uint32_t i = 0; i < net.n_pos; i++)
                        if (net.pos[i].owner == (uint32_t) (3 + a) && net.pos[i].asset == asset[a])
                            want = net.pos[i].net;
                    if ((int64_t) (deb(feea[a]) - fbefore[a]) != want) bad_net++;
                }
                for (uint32_t b = 0; b < NBK; b++) {
                    rb[b].carry = carry[b];
                    rb[b].fee_total += fees[b];
                    rb[b].gross_total += gross[b];
                    if (V[b] || cool[b]) {
                        rb[b].has_ref = true;
                        rb[b].ref = price[b];
                    }
                    const fx_book_t *bk = &M.book[b];
                    if (bk->carry != rb[b].carry || bk->fee_total != rb[b].fee_total ||
                        bk->gross_total != rb[b].gross_total || bk->has_ref != rb[b].has_ref ||
                        bk->ref != rb[b].ref)
                        bad_settle++;
                }
            }
        }
        if (M.state != FX_SETTLED || !fx_check(&M)) bad_settle++;
    }
    CHECK(bad_clear == 0,
          "random: clearing matches the reference (price, V, cooling, fills, fees)");
    CHECK(bad_settle == 0, "random: settlement conserves, escrows home, totals committed");
    CHECK(bad_net == 0, "random: net positions balanced and equal to ledger changes");
    CHECK(traded > 100 && cooled > 10 && cancelled > 20 && saved_total > 0,
          "random: the loop exercised trades, cooling, cancels and netting savings");
    printf("random: %d epochs, %d book-trades, %d cooling, %d cancels, %llu legs saved\n", epochs,
           traded, cooled, cancelled, (unsigned long long) saved_total);
    CHECK(pay_ledger_check(&L), "ledger balanced at the end");
}

int main(void)
{
    test_config();
    test_books();
    test_pure();
    test_orders();
    test_clear_basic();
    test_ties();
    test_alloc();
    test_dust();
    test_fee();
    test_overflow();
    test_cooling();
    test_settle_resume();
    test_audit();
    test_max_orders();
    test_net_capacity();
    test_random();
    printf("fxepoch: %d passed, %d failed\n", pass, fail);
    return fail != 0;
}
