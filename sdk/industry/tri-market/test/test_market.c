/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_market.c — tri-market: order refusals, Ed25519 and ML-DSA-87
 * delivery confirmations, the assurance fee and its split checked against
 * zxv_fee_* (including the carry over many small orders), replayed /
 * double / cross-order / late / forged confirmations, disputes held as
 * BOTH, arbiter rulings (unequal splits, forged, unregistered, replayed,
 * without ATTEST, full refund, full payment), timeouts, cross-currency
 * orders quoted through zxv_vhub, conservation of money and the signed
 * record chain. */
#include "zind_test.h"
#include "ztmarket.h"

#undef CHECK_RC
#define CHECK_RC(expr, want)                                                                   \
    do {                                                                                       \
        int32_t rc_ = (expr);                                                                  \
        CHECK(rc_ == (want), "%s -> %d (%s), want %d (%s)", #expr, (int) rc_,                  \
              ztm_status_name(rc_), (int) (want), ztm_status_name(want));                     \
    } while (0)

#define T0 1700000000ULL
#define DL (T0 + 7 * 86400)

typedef struct {
    zt_env e;
    zind_signer *desk_key, *arbiter, *rogue, *buyer_ml;
    uint8_t ed_sk[32], ed_pk[32], ed2_sk[32], ed2_pk[32];
    zxv_hexlog *qlog;
    ztm_desk *d;
    uint32_t issuer, buyer, seller, seller2, fee[4];
    int64_t funded;
} fx_t;

static int fx_open(fx_t *f) {
    memset(f, 0, sizeof *f);
    if (zt_env_open(&f->e, "EUR") != 0) return -1;
    f->desk_key = zt_signer(f->e.cap, 1);
    f->arbiter = zt_signer(f->e.cap, 2);
    f->rogue = zt_signer(f->e.cap, 3);
    f->buyer_ml = zt_signer(f->e.cap, 4);
    if (!f->desk_key || !f->arbiter || !f->rogue || !f->buyer_ml) return -1;
    zt_seed(f->ed_sk, 9);
    zt_seed(f->ed2_sk, 10);
    if (zxv_ed25519_public(f->e.cap, f->ed_sk, f->ed_pk) != ZXV_OK ||
        zxv_ed25519_public(f->e.cap, f->ed2_sk, f->ed2_pk) != ZXV_OK)
        return -1;
    if (zxv_hexlog_create(f->e.cap, 1024, &f->qlog) != ZXV_OK) return -1;
    if (ztm_desk_create(f->e.cap, "EUR", f->desk_key, f->qlog, &f->d) != ZXV_OK) return -1;
    if (ztm_arbiter_add(f->d, zind_signer_pk(f->arbiter)) != ZXV_OK) return -1;
    if (ztm_fee_accounts(f->d, f->fee) != ZXV_OK) return -1;
    f->issuer = zt_issuer(f->e.cap, "EUR");
    if (zxv_ledger_open_account_cap(f->e.cap, "buyer", "EUR", ZXV_ACCT_HOLDER, &f->buyer) !=
            ZXV_OK ||
        zxv_ledger_open_account_cap(f->e.cap, "seller", "EUR", ZXV_ACCT_HOLDER, &f->seller) !=
            ZXV_OK ||
        zxv_ledger_open_account_cap(f->e.cap, "seller2", "EUR", ZXV_ACCT_HOLDER, &f->seller2) !=
            ZXV_OK)
        return -1;
    if (zt_fund(f->e.cap, "EUR", f->issuer, f->buyer, 100000000, "seed-funds") != 0) return -1;
    f->funded = 100000000;
    return 0;
}

static void fx_close(fx_t *f) {
    ztm_desk_destroy(f->d);
    zxv_hexlog_destroy(f->qlog);
    zind_signer_destroy(f->desk_key);
    zind_signer_destroy(f->arbiter);
    zind_signer_destroy(f->rogue);
    zind_signer_destroy(f->buyer_ml);
    zt_env_close(&f->e);
}

static int32_t state_of(fx_t *f, uint32_t o, uint8_t *tag) {
    int32_t st = -1;
    (void) ztm_order_state(f->d, o, &st, tag);
    return st;
}

static int64_t fees_total(fx_t *f) {
    int64_t t = 0;
    for (int i = 0; i < 4; i++) t += zt_equity(f->e.cap, f->fee[i]);
    return t;
}

static void check_books(fx_t *f, const char *where) {
    int64_t owed = -1, total;
    CHECK_RC(ztm_escrow_owed(f->d, &owed), ZXV_OK);
    CHECK(zt_equity(f->e.cap, ztm_escrow_account(f->d)) == owed, "%s: escrow == owed", where);
    total = zt_equity(f->e.cap, f->buyer) + zt_equity(f->e.cap, f->seller) +
            zt_equity(f->e.cap, f->seller2) + zt_equity(f->e.cap, ztm_escrow_account(f->d)) +
            fees_total(f);
    CHECK(total == f->funded, "%s: money conserved (%lld)", where, (long long) total);
    CHECK_RC(zxv_ledger_check_cap(f->e.cap), ZXV_OK);
}

static uint32_t mk(fx_t *f, const char *id, uint32_t seller, int64_t price, int ml) {
    uint32_t o = 0;
    CHECK_RC(ztm_order_create(f->d, id, f->buyer, seller, price,
                              ml ? ZTM_ALG_MLDSA87 : ZTM_ALG_ED25519,
                              ml ? zind_signer_pk(f->buyer_ml) : f->ed_pk, ml ? ZIND_PK_BYTES : 32,
                              DL, T0, &o),
             ZXV_OK);
    return o;
}

static int ed_conf(fx_t *f, const uint8_t sk[32], uint32_t o, uint64_t t, uint8_t sig[64]) {
    uint8_t m[160];
    size_t len = 0;
    if (ztm_confirm_message(f->d, o, t, m, sizeof m, &len) != ZXV_OK) return -1;
    return zxv_ed25519_sign(f->e.cap, sk, m, len, sig) == ZXV_OK ? 0 : -1;
}

static int ml_conf(fx_t *f, const zind_signer *k, uint32_t o, uint64_t t,
                   uint8_t sig[ZIND_SIG_BYTES]) {
    uint8_t m[160];
    size_t len = 0;
    if (ztm_confirm_message(f->d, o, t, m, sizeof m, &len) != ZXV_OK) return -1;
    return zind_sign(f->e.cap, k, m, len, ZTM_CONFIRM_CTX, sig) == ZXV_OK ? 0 : -1;
}

static int rule_sig(fx_t *f, const zind_signer *k, uint32_t o, int64_t s, int64_t b,
                    uint8_t sig[ZIND_SIG_BYTES]) {
    uint8_t m[160];
    size_t len = 0;
    if (ztm_ruling_message(f->d, o, s, b, m, sizeof m, &len) != ZXV_OK) return -1;
    return zind_sign(f->e.cap, k, m, len, ZTM_RULING_CTX, sig) == ZXV_OK ? 0 : -1;
}

static void test_names(void) {
    static const int32_t codes[] = {ZTM_E_SPLIT, ZTM_E_ARBITER, ZTM_E_EXPIRED, ZTM_E_EARLY,
                                    ZTM_E_NOHUB};
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        CHECK(codes[i] <= -4400 && codes[i] > -4500, "code %d in block", (int) codes[i]);
        CHECK(strncmp(ztm_status_name(codes[i]), "ZTM_E_", 6) == 0, "name");
    }
    CHECK(strcmp(ztm_status_name(ZIND_E_REPLAY), "ZIND_E_REPLAY") == 0, "fallback");
}

static void test_create(fx_t *f) {
    ztm_desk *d2 = NULL;
    zxv_cap *weak = NULL;
    uint32_t o = 0, usd = 0, fa[4];
    char long_id[40];
    CHECK_RC(ztm_desk_create(f->e.cap, "QQQ", f->desk_key, NULL, &d2), ZXV_E_CURRENCY);
    CHECK_RC(ztm_desk_create(f->e.cap, "XAU", f->desk_key, NULL, &d2), ZXV_E_CURRENCY);
    CHECK_RC(ztm_desk_create(f->e.cap, "EUR", NULL, NULL, &d2), ZXV_E_ARG);
    CHECK_RC(zxv_cap_attenuate(f->e.cap, ZXV_CAP_LEDGER_POST | ZXV_CAP_LEDGER_READ, &weak),
             ZXV_OK);
    CHECK_RC(ztm_desk_create(weak, "EUR", f->desk_key, NULL, &d2), ZXV_E_CAP);
    zxv_cap_destroy(weak);
    CHECK_RC(ztm_arbiter_add(f->d, zind_signer_pk(f->arbiter)), ZXV_E_DUPLICATE);
    CHECK_RC(ztm_fee_accounts(f->d, fa), ZXV_OK);
    for (int i = 0; i < 4; i++) {
        int32_t kind = -1;
        CHECK_RC(zxv_ledger_account_kind_cap(f->e.cap, fa[i], &kind), ZXV_OK);
        CHECK(kind == ZXV_ACCT_HOLDER, "fee account %d is a holder", i);
        CHECK(zt_equity(f->e.cap, fa[i]) == 0, "fee account %d empty", i);
    }
    memset(long_id, 'o', 33);
    long_id[33] = 0;
    CHECK_RC(ztm_order_create(f->d, "", f->buyer, f->seller, 100, ZTM_ALG_ED25519, f->ed_pk, 32,
                              DL, T0, &o),
             ZXV_E_ARG);
    CHECK_RC(ztm_order_create(f->d, "a b", f->buyer, f->seller, 100, ZTM_ALG_ED25519, f->ed_pk,
                              32, DL, T0, &o),
             ZXV_E_ARG);
    CHECK_RC(ztm_order_create(f->d, long_id, f->buyer, f->seller, 100, ZTM_ALG_ED25519, f->ed_pk,
                              32, DL, T0, &o),
             ZXV_E_ARG);
    CHECK_RC(ztm_order_create(f->d, "O1", f->buyer, f->seller, 100, ZTM_ALG_ED25519, f->ed_pk, 31,
                              DL, T0, &o),
             ZXV_E_ARG);
    CHECK_RC(ztm_order_create(f->d, "O1", f->buyer, f->seller, 100, ZTM_ALG_MLDSA87, f->ed_pk, 32,
                              DL, T0, &o),
             ZXV_E_ARG);
    CHECK_RC(ztm_order_create(f->d, "O1", f->buyer, f->seller, 100, 7, f->ed_pk, 32, DL, T0, &o),
             ZXV_E_ARG);
    CHECK_RC(ztm_order_create(f->d, "O1", f->buyer, f->seller, 0, ZTM_ALG_ED25519, f->ed_pk, 32,
                              DL, T0, &o),
             ZXV_E_RANGE);
    CHECK_RC(ztm_order_create(f->d, "O1", f->buyer, f->seller, -5, ZTM_ALG_ED25519, f->ed_pk, 32,
                              DL, T0, &o),
             ZXV_E_RANGE);
    CHECK_RC(ztm_order_create(f->d, "O1", f->buyer, f->seller, ZXV_AMOUNT_MAX + 1,
                              ZTM_ALG_ED25519, f->ed_pk, 32, DL, T0, &o),
             ZXV_E_RANGE);
    CHECK_RC(ztm_order_create(f->d, "O1", f->buyer, f->seller, 100, ZTM_ALG_ED25519, f->ed_pk, 32,
                              T0, T0, &o),
             ZXV_E_ARG);
    CHECK_RC(ztm_order_create(f->d, "O1", f->buyer, f->seller, 100, ZTM_ALG_ED25519, f->ed_pk, 32,
                              T0 + ZTM_MAX_WINDOW + 1, T0, &o),
             ZXV_E_ARG);
    CHECK_RC(ztm_order_create(f->d, "O1", f->buyer, f->buyer, 100, ZTM_ALG_ED25519, f->ed_pk, 32,
                              DL, T0, &o),
             ZXV_E_ARG);
    CHECK_RC(ztm_order_create(f->d, "O1", f->buyer, ztm_escrow_account(f->d), 100,
                              ZTM_ALG_ED25519, f->ed_pk, 32, DL, T0, &o),
             ZXV_E_ARG);
    for (int i = 0; i < 4; i++)
        CHECK_RC(ztm_order_create(f->d, "O1", f->buyer, f->fee[i], 100, ZTM_ALG_ED25519, f->ed_pk,
                                  32, DL, T0, &o),
                 ZXV_E_ARG);
    CHECK_RC(ztm_order_create(f->d, "O1", f->buyer, f->seller, 100, ZTM_ALG_ED25519, NULL, 32, DL,
                              T0, &o),
             ZXV_E_ARG);
    CHECK_RC(ztm_order_create(NULL, "O1", f->buyer, f->seller, 100, ZTM_ALG_ED25519, f->ed_pk, 32,
                              DL, T0, &o),
             ZXV_E_ARG);
    CHECK_RC(zxv_ccy_enable_cap(f->e.cap, "USD"), ZXV_OK);
    CHECK_RC(zxv_ledger_open_account_cap(f->e.cap, "usd-seller", "USD", ZXV_ACCT_HOLDER, &usd),
             ZXV_OK);
    CHECK_RC(ztm_order_create(f->d, "O1", f->buyer, usd, 100, ZTM_ALG_ED25519, f->ed_pk, 32, DL,
                              T0, &o),
             ZXV_E_CURRENCY);
    CHECK_RC(ztm_order_create(f->d, "O1", f->seller, f->seller2, 100, ZTM_ALG_ED25519, f->ed_pk,
                              32, DL, T0, &o),
             ZXV_E_FUNDS); /* the seller has nothing to pay with */
    CHECK_RC(ztm_order_create(f->d, "O1", f->buyer, f->seller, 100, ZTM_ALG_ED25519, f->ed_pk, 32,
                              DL, T0, &o),
             ZXV_OK);
    CHECK_RC(ztm_order_create(f->d, "O1", f->buyer, f->seller, 100, ZTM_ALG_ED25519, f->ed_pk, 32,
                              DL, T0, &o),
             ZXV_E_DUPLICATE);
    {
        uint32_t found = 0;
        CHECK_RC(ztm_order_find(f->d, "O1", &found), ZXV_OK);
        CHECK(found == o, "find");
        CHECK_RC(ztm_order_find(f->d, "nope", &found), ZXV_E_NOT_FOUND);
        CHECK_RC(ztm_order_quote(f->d, o, NULL, NULL, NULL, NULL, NULL), ZXV_E_STATE);
    }
    check_books(f, "create");
}

/* The fee is the library's, split by the library, on the released amount. */
static void expect_release(fx_t *f, uint32_t o, uint32_t seller, int64_t price, uint64_t rem_in,
                           int64_t seller_before, const int64_t buckets_before[4]) {
    uint64_t fee = 0, rem = 0, parts[4];
    int64_t pf = -1, ps = -1, pb = -1;
    CHECK_RC(zxv_fee_assure_carry(rem_in, (uint64_t) price, &fee, &rem), ZXV_OK);
    CHECK_RC(zxv_fee_split(fee, parts), ZXV_OK);
    CHECK(zt_equity(f->e.cap, seller) == seller_before + price - (int64_t) fee,
          "seller paid price - fee (%lld)", (long long) fee);
    for (int i = 0; i < 4; i++)
        CHECK(zt_equity(f->e.cap, f->fee[i]) == buckets_before[i] + (int64_t) parts[i],
              "bucket %d got its split", i);
    CHECK_RC(ztm_order_amounts(f->d, o, NULL, &pf, &ps, &pb), ZXV_OK);
    CHECK(pf == (int64_t) fee && ps == price - (int64_t) fee && pb == 0, "order amounts");
    {
        uint64_t stored = 99;
        CHECK_RC(ztm_fee_carry(f->d, seller, &stored), ZXV_OK);
        CHECK(stored == rem, "carry stored (%llu vs %llu)", (unsigned long long) stored,
              (unsigned long long) rem);
    }
}

static void snap(fx_t *f, int64_t b[4]) {
    for (int i = 0; i < 4; i++) b[i] = zt_equity(f->e.cap, f->fee[i]);
}

static void test_confirm(fx_t *f) {
    uint8_t sig[64], sig2[64], msig[ZIND_SIG_BYTES], tag = 0, base = 0, ph = 0;
    int64_t b4[4], s0;
    uint64_t rem = 0;
    uint32_t q0;
    uint32_t o1 = mk(f, "ED-1", f->seller, 2500000, 0);
    uint32_t o2 = mk(f, "ML-1", f->seller, 1234567, 1);
    uint32_t o3 = mk(f, "ED-2", f->seller, 777, 0);

    CHECK(state_of(f, o1, &tag) == ZTM_ST_ESCROW, "escrow");
    zxv_hex_unpack(tag, &base, NULL, &ph);
    CHECK(base == ZXV_HEX_BASE_NEITHER && ph == ZXV_HEX_PHASE_POTENTIAL, "potential");
    CHECK_RC(ztm_ship(f->d, o1, "TRACK-1", T0 + 10), ZXV_OK);
    CHECK_RC(ztm_ship(f->d, o1, "TRACK-1", T0 + 10), ZXV_E_STATE);
    CHECK_RC(ztm_ship(f->d, o1, "bad ref", T0 + 10), ZXV_E_ARG);
    CHECK(state_of(f, o1, &tag) == ZTM_ST_SHIPPED, "shipped");
    zxv_hex_unpack(tag, NULL, NULL, &ph);
    CHECK(ph == ZXV_HEX_PHASE_EMERGING, "emerging");
    check_books(f, "orders");

    /* forged: another key; wrong order (o1's bytes on o3); late; wrong length */
    q0 = 0;
    zxv_hexlog_count(f->qlog, &q0);
    CHECK(ed_conf(f, f->ed2_sk, o1, T0 + 100, sig) == 0, "sign");
    CHECK_RC(ztm_confirm(f->d, o1, T0 + 100, sig, 64), ZXV_E_VERIFY);
    CHECK(ed_conf(f, f->ed_sk, o1, T0 + 100, sig) == 0, "sign");
    CHECK_RC(ztm_confirm(f->d, o3, T0 + 100, sig, 64), ZXV_E_VERIFY);
    CHECK_RC(ztm_confirm(f->d, o1, T0 + 101, sig, 64), ZXV_E_VERIFY);
    CHECK_RC(ztm_confirm(f->d, o1, T0 + 100, sig, 63), ZXV_E_ARG);
    CHECK_RC(ztm_confirm(f->d, o1, T0 + 100, NULL, 64), ZXV_E_ARG);
    for (int i = 0; i < 16; i++) {
        sig[i * 4] ^= (uint8_t) (1u << (i % 8));
        CHECK_RC(ztm_confirm(f->d, o1, T0 + 100, sig, 64), ZXV_E_VERIFY);
        sig[i * 4] ^= (uint8_t) (1u << (i % 8));
    }
    {
        uint32_t q1 = 0;
        zxv_hexlog_count(f->qlog, &q1);
        CHECK(q1 == q0 + 19, "forged confirmations quarantined (%u)", q1 - q0);
    }
    CHECK(ed_conf(f, f->ed_sk, o1, DL + 1, sig2) == 0, "sign");
    CHECK_RC(ztm_confirm(f->d, o1, DL + 1, sig2, 64), ZTM_E_EXPIRED);
    CHECK(state_of(f, o1, NULL) == ZTM_ST_SHIPPED, "nothing moved");
    check_books(f, "refusals");

    /* the genuine confirmation */
    s0 = zt_equity(f->e.cap, f->seller);
    snap(f, b4);
    CHECK_RC(ztm_fee_carry(f->d, f->seller, &rem), ZXV_OK);
    CHECK_RC(ztm_confirm(f->d, o1, T0 + 100, sig, 64), ZXV_OK);
    expect_release(f, o1, f->seller, 2500000, rem, s0, b4);
    CHECK(state_of(f, o1, &tag) == ZTM_ST_RELEASED, "released");
    zxv_hex_unpack(tag, &base, NULL, &ph);
    CHECK(base == ZXV_HEX_BASE_TRUE && ph == ZXV_HEX_PHASE_MANIFEST, "manifest");
    /* no double release: the same proof, a fresh proof, an expiry, a dispute */
    CHECK_RC(ztm_confirm(f->d, o1, T0 + 100, sig, 64), ZIND_E_REPLAY);
    CHECK(ed_conf(f, f->ed_sk, o1, T0 + 200, sig2) == 0, "sign");
    CHECK_RC(ztm_confirm(f->d, o1, T0 + 200, sig2, 64), ZIND_E_REPLAY);
    CHECK_RC(ztm_expire(f->d, o1, DL + 10), ZIND_E_REPLAY);
    CHECK_RC(ztm_dispute(f->d, o1, ZTM_PARTY_BUYER, 1), ZXV_E_STATE);
    check_books(f, "released");

    /* ML-DSA-87 confirmation straight from escrow (no shipment recorded) */
    s0 = zt_equity(f->e.cap, f->seller);
    snap(f, b4);
    CHECK_RC(ztm_fee_carry(f->d, f->seller, &rem), ZXV_OK);
    CHECK(ml_conf(f, f->rogue, o2, T0 + 5, msig) == 0, "sign");
    CHECK_RC(ztm_confirm(f->d, o2, T0 + 5, msig, ZIND_SIG_BYTES), ZXV_E_VERIFY);
    CHECK(ml_conf(f, f->buyer_ml, o2, T0 + 5, msig) == 0, "sign");
    CHECK_RC(ztm_confirm(f->d, o2, T0 + 5, msig, 64), ZXV_E_ARG);
    CHECK_RC(ztm_confirm(f->d, o2, T0 + 5, msig, ZIND_SIG_BYTES), ZXV_OK);
    expect_release(f, o2, f->seller, 1234567, rem, s0, b4);
    CHECK_RC(ztm_confirm(f->d, o2, T0 + 5, msig, ZIND_SIG_BYTES), ZIND_E_REPLAY);
    /* a 777 order: fee 0 below 1125, so the carry builds up */
    s0 = zt_equity(f->e.cap, f->seller);
    snap(f, b4);
    CHECK_RC(ztm_fee_carry(f->d, f->seller, &rem), ZXV_OK);
    CHECK(ed_conf(f, f->ed_sk, o3, T0 + 5, sig) == 0, "sign");
    CHECK_RC(ztm_confirm(f->d, o3, T0 + 5, sig, 64), ZXV_OK);
    expect_release(f, o3, f->seller, 777, rem, s0, b4);
    /* bytes a buyer app builds itself match the desk's */
    {
        uint8_t h[32], a[160], b[160];
        size_t la = 0, lb = 0;
        CHECK_RC(ztm_order_hash(f->d, o3, h), ZXV_OK);
        CHECK_RC(ztm_confirm_bytes("ED-2", h, 777, T0 + 5, a, sizeof a, &la), ZXV_OK);
        CHECK_RC(ztm_confirm_message(f->d, o3, T0 + 5, b, sizeof b, &lb), ZXV_OK);
        CHECK(la == lb && memcmp(a, b, la) == 0, "same bytes");
        CHECK_RC(ztm_confirm_bytes("ED-2", h, 777, T0 + 5, a, 10, &la), ZXV_E_BUFFER);
        CHECK(la == lb, "size reported");
        CHECK_RC(ztm_confirm_bytes("ED-2", h, 0, T0 + 5, a, sizeof a, &la), ZXV_E_ARG);
        CHECK_RC(ztm_confirm_bytes("E D", h, 1, T0 + 5, a, sizeof a, &la), ZXV_E_ARG);
    }
    check_books(f, "confirm");
}

/* Many small orders to one seller pay exactly what one order of their sum pays. */
static void test_fee_carry(fx_t *f) {
    uint64_t total_fee = 0, one = 0, rem = 0;
    int64_t b4[4];
    uint64_t parts_sum[4] = {0, 0, 0, 0};
    snap(f, b4);
    for (int i = 0; i < 300; i++) {
        char id[32];
        uint8_t sig[64];
        int64_t pf = 0;
        uint32_t o;
        snprintf(id, sizeof id, "SMALL-%d", i);
        o = mk(f, id, f->seller2, 1000, 0);
        CHECK(ed_conf(f, f->ed_sk, o, T0 + 50, sig) == 0, "sign");
        CHECK_RC(ztm_confirm(f->d, o, T0 + 50, sig, 64), ZXV_OK);
        CHECK_RC(ztm_order_amounts(f->d, o, NULL, &pf, NULL, NULL), ZXV_OK);
        total_fee += (uint64_t) pf;
        {
            uint64_t p[4];
            zxv_fee_split((uint64_t) pf, p);
            for (int k = 0; k < 4; k++) parts_sum[k] += p[k];
        }
    }
    CHECK_RC(zxv_fee_assure(300000, &one), ZXV_OK);
    CHECK(total_fee == one, "300 x 10.00 pay the fee of 3000.00 (%llu vs %llu)",
          (unsigned long long) total_fee, (unsigned long long) one);
    CHECK_RC(ztm_fee_carry(f->d, f->seller2, &rem), ZXV_OK);
    CHECK(rem == (300000ULL * 8889) % 10000000ULL, "carry %llu", (unsigned long long) rem);
    CHECK(zt_equity(f->e.cap, f->seller2) == 300000 - (int64_t) one, "seller2 net");
    for (int k = 0; k < 4; k++)
        CHECK(zt_equity(f->e.cap, f->fee[k]) - b4[k] == (int64_t) parts_sum[k], "bucket %d", k);
    check_books(f, "carry");
}

static void test_disputes(fx_t *f) {
    zxv_cap *noattest = NULL;
    ztm_desk *d2 = NULL;
    uint8_t sig[ZIND_SIG_BYTES], tag = 0, base = 0, csig[64];
    int64_t s0, by0, b4[4];
    uint64_t rem = 0;
    uint32_t q0 = 0, q1 = 0;
    uint32_t o = mk(f, "DISP-1", f->seller, 1000000, 0);
    uint32_t o2 = mk(f, "DISP-2", f->seller, 5000, 0);
    uint32_t o3 = mk(f, "DISP-3", f->seller, 9000, 0);

    CHECK_RC(ztm_dispute(f->d, o, 3, 1), ZXV_E_ARG);
    zxv_hexlog_count(f->qlog, &q0);
    CHECK_RC(ztm_dispute(f->d, o, ZTM_PARTY_BUYER, 42), ZXV_OK);
    zxv_hexlog_count(f->qlog, &q1);
    CHECK(q1 == q0 + 1, "dispute logged as held");
    CHECK(state_of(f, o, &tag) == ZTM_ST_DISPUTED, "disputed");
    zxv_hex_unpack(tag, &base, NULL, NULL);
    CHECK(base == ZXV_HEX_BASE_BOTH, "BOTH");
    CHECK_RC(ztm_dispute(f->d, o, ZTM_PARTY_SELLER, 1), ZIND_E_HELD);
    CHECK(ed_conf(f, f->ed_sk, o, T0 + 5, csig) == 0, "sign");
    CHECK_RC(ztm_confirm(f->d, o, T0 + 5, csig, 64), ZIND_E_HELD);
    CHECK_RC(ztm_expire(f->d, o, DL + 100), ZIND_E_HELD);
    CHECK_RC(ztm_ship(f->d, o, "T", T0), ZIND_E_HELD);
    check_books(f, "frozen");

    /* rulings that do not split exactly */
    {
        static const int64_t bad[][2] = {{600000, 300000}, {600000, 500000}, {-1, 1000001},
                                         {1000001, -1},    {0, 0},           {1000000, 1},
                                         {INT64_MAX, 0}};
        uint8_t m[160];
        size_t len = 0;
        for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            CHECK_RC(ztm_ruling_message(f->d, o, bad[i][0], bad[i][1], m, sizeof m, &len),
                     ZTM_E_SPLIT);
            CHECK_RC(ztm_rule(f->d, o, zind_signer_pk(f->arbiter), bad[i][0], bad[i][1], sig),
                     ZTM_E_SPLIT);
        }
        CHECK_RC(ztm_ruling_message(f->d, o2, 1, 4999, m, sizeof m, &len), ZXV_E_STATE);
    }
    /* forged, unregistered, other split, other order */
    CHECK(rule_sig(f, f->rogue, o, 600000, 400000, sig) == 0, "sign");
    CHECK_RC(ztm_rule(f->d, o, zind_signer_pk(f->arbiter), 600000, 400000, sig), ZXV_E_VERIFY);
    CHECK_RC(ztm_rule(f->d, o, zind_signer_pk(f->rogue), 600000, 400000, sig), ZTM_E_ARBITER);
    CHECK(rule_sig(f, f->arbiter, o, 600000, 400000, sig) == 0, "sign");
    CHECK_RC(ztm_rule(f->d, o, zind_signer_pk(f->arbiter), 400000, 600000, sig), ZXV_E_VERIFY);
    CHECK_RC(ztm_rule(f->d, o2, zind_signer_pk(f->arbiter), 600000, 400000, sig), ZXV_E_STATE);
    CHECK_RC(ztm_dispute(f->d, o3, ZTM_PARTY_SELLER, 7), ZXV_OK);
    CHECK_RC(ztm_rule(f->d, o3, zind_signer_pk(f->arbiter), 5400, 3600, sig), ZXV_E_VERIFY);
    CHECK(state_of(f, o, NULL) == ZTM_ST_DISPUTED, "still frozen");
    check_books(f, "forged rulings");

    /* without ATTEST the desk cannot apply a ruling */
    CHECK_RC(zxv_cap_attenuate(f->e.cap, ZXV_CAP_ALL & ~ZXV_CAP_ATTEST, &noattest), ZXV_OK);
    CHECK_RC(ztm_desk_create(noattest, "EUR", f->rogue, NULL, &d2), ZXV_OK);
    if (d2) {
        uint32_t od = 0;
        uint8_t m[160], s2[ZIND_SIG_BYTES];
        size_t len = 0;
        CHECK_RC(ztm_arbiter_add(d2, zind_signer_pk(f->arbiter)), ZXV_OK);
        CHECK_RC(ztm_order_create(d2, "NA-1", f->buyer, f->seller, 100, ZTM_ALG_ED25519,
                                  f->ed_pk, 32, DL, T0, &od),
                 ZXV_OK);
        CHECK_RC(ztm_dispute(d2, od, ZTM_PARTY_BUYER, 1), ZXV_OK);
        CHECK_RC(ztm_ruling_message(d2, od, 50, 50, m, sizeof m, &len), ZXV_OK);
        CHECK_RC(zind_sign(f->e.cap, f->arbiter, m, len, ZTM_RULING_CTX, s2), ZXV_OK);
        CHECK_RC(ztm_rule(d2, od, zind_signer_pk(f->arbiter), 50, 50, s2), ZXV_E_CAP);
        CHECK(zt_equity(f->e.cap, ztm_escrow_account(d2)) == 100, "d2 escrow untouched");
        /* return the money so the books of this fixture still add up: refund after deadline */
        CHECK_RC(ztm_expire(d2, od, DL + 1), ZIND_E_HELD);
        ztm_desk_destroy(d2);
        f->funded -= 100; /* parked in d2's escrow for good in this test */
    }
    zxv_cap_destroy(noattest);

    /* the genuine split 600000 / 400000 */
    s0 = zt_equity(f->e.cap, f->seller);
    by0 = zt_equity(f->e.cap, f->buyer);
    snap(f, b4);
    CHECK_RC(ztm_fee_carry(f->d, f->seller, &rem), ZXV_OK);
    CHECK_RC(ztm_rule(f->d, o, zind_signer_pk(f->arbiter), 600000, 400000, sig), ZXV_OK);
    {
        uint64_t fee = 0, r2 = 0, parts[4];
        int64_t pf, ps, pb;
        zxv_fee_assure_carry(rem, 600000, &fee, &r2);
        zxv_fee_split(fee, parts);
        CHECK(zt_equity(f->e.cap, f->seller) == s0 + 600000 - (int64_t) fee, "seller part");
        CHECK(zt_equity(f->e.cap, f->buyer) == by0 + 400000, "buyer part, no fee");
        for (int i = 0; i < 4; i++)
            CHECK(zt_equity(f->e.cap, f->fee[i]) == b4[i] + (int64_t) parts[i], "bucket %d", i);
        CHECK_RC(ztm_order_amounts(f->d, o, NULL, &pf, &ps, &pb), ZXV_OK);
        CHECK(pf == (int64_t) fee && ps == 600000 - (int64_t) fee && pb == 400000, "amounts");
    }
    CHECK(state_of(f, o, &tag) == ZTM_ST_RULED, "ruled");
    CHECK_RC(ztm_rule(f->d, o, zind_signer_pk(f->arbiter), 600000, 400000, sig), ZIND_E_REPLAY);
    CHECK_RC(ztm_confirm(f->d, o, T0 + 5, csig, 64), ZIND_E_REPLAY);
    check_books(f, "ruled");

    /* a full refund (no fee) and a full payment */
    by0 = zt_equity(f->e.cap, f->buyer);
    snap(f, b4);
    CHECK(rule_sig(f, f->arbiter, o3, 0, 9000, sig) == 0, "sign");
    CHECK_RC(ztm_rule(f->d, o3, zind_signer_pk(f->arbiter), 0, 9000, sig), ZXV_OK);
    CHECK(zt_equity(f->e.cap, f->buyer) == by0 + 9000, "full refund");
    CHECK(fees_total(f) == b4[0] + b4[1] + b4[2] + b4[3], "no fee on a refund");
    CHECK(state_of(f, o3, &tag) == ZTM_ST_RULED, "ruled");
    zxv_hex_unpack(tag, &base, NULL, NULL);
    CHECK(base == ZXV_HEX_BASE_FALSE, "refund ruling is FALSE");
    CHECK_RC(ztm_dispute(f->d, o2, ZTM_PARTY_BUYER, 2), ZXV_OK);
    CHECK(rule_sig(f, f->arbiter, o2, 5000, 0, sig) == 0, "sign");
    CHECK_RC(ztm_rule(f->d, o2, zind_signer_pk(f->arbiter), 5000, 0, sig), ZXV_OK);
    check_books(f, "full rulings");
}

static void test_timeouts(fx_t *f) {
    uint8_t sig[64], tag = 0, base = 0;
    int64_t by0;
    uint32_t o = mk(f, "TIME-1", f->seller, 4321, 0);
    uint32_t o2 = mk(f, "TIME-2", f->seller, 1111, 0);
    CHECK_RC(ztm_expire(f->d, o, T0), ZTM_E_EARLY);
    CHECK_RC(ztm_expire(f->d, o, DL), ZTM_E_EARLY);
    CHECK_RC(ztm_ship(f->d, o2, "SHIP-2", T0 + 1), ZXV_OK);
    by0 = zt_equity(f->e.cap, f->buyer);
    CHECK_RC(ztm_expire(f->d, o, DL + 1), ZXV_OK);
    CHECK_RC(ztm_expire(f->d, o2, DL + 1), ZXV_OK);
    CHECK(zt_equity(f->e.cap, f->buyer) == by0 + 4321 + 1111, "refunded in full");
    CHECK(state_of(f, o, &tag) == ZTM_ST_REFUNDED, "refunded");
    zxv_hex_unpack(tag, &base, NULL, NULL);
    CHECK(base == ZXV_HEX_BASE_FALSE, "FALSE");
    CHECK_RC(ztm_expire(f->d, o, DL + 2), ZIND_E_REPLAY);
    /* a genuine confirmation dated before the deadline arrives too late */
    CHECK(ed_conf(f, f->ed_sk, o, T0 + 5, sig) == 0, "sign");
    CHECK_RC(ztm_confirm(f->d, o, T0 + 5, sig, 64), ZIND_E_REPLAY);
    CHECK_RC(ztm_dispute(f->d, o, ZTM_PARTY_BUYER, 1), ZXV_E_STATE);
    CHECK_RC(ztm_expire(f->d, 0, DL + 2), ZXV_E_NOT_FOUND);
    CHECK_RC(ztm_expire(f->d, 99999, DL + 2), ZXV_E_NOT_FOUND);
    check_books(f, "timeouts");
}

static int sign_peg(fx_t *f, const uint8_t sk[4896], uint8_t slot, uint64_t num, uint64_t den,
                    uint64_t ep, uint8_t sig[ZIND_SIG_BYTES]) {
    uint8_t m[40];
    if (zxv_vhub_peg_msg(slot, num, den, ep, m) != ZXV_OK) return -1;
    return zxv_mldsa87_sign_deterministic(f->e.cap, sk, m, 40, NULL, 0, sig) == ZXV_OK ? 0 : -1;
}

static void test_quoted(fx_t *f) {
    zxv_vhub *hub = NULL;
    static const uint8_t seed[32] = {3};
    uint8_t pk0[ZIND_PK_BYTES], sk0[4896], pk1[ZIND_PK_BYTES], sk1[4896], s[32];
    uint8_t sig[ZIND_SIG_BYTES], csig[64];
    uint32_t o = 0;
    int64_t price = 0;
    zt_seed(s, 70);
    CHECK_RC(zxv_mldsa87_keygen_seed(f->e.cap, s, pk0, sk0), ZXV_OK);
    zt_seed(s, 71);
    CHECK_RC(zxv_mldsa87_keygen_seed(f->e.cap, s, pk1, sk1), ZXV_OK);
    CHECK_RC(ztm_order_create_quoted(f->d, "Q-0", f->buyer, f->seller, 1, 500, ZTM_ALG_ED25519,
                                     f->ed_pk, 32, DL, T0, &o),
             ZTM_E_NOHUB);
    CHECK_RC(zxv_vhub_create(f->e.cap, seed, &hub), ZXV_OK);
    CHECK_RC(zxv_vhub_add_ccy(f->e.cap, hub, 0, "EURMIRROR", 2, pk0, 1000000, 1000000), ZXV_OK);
    CHECK_RC(zxv_vhub_add_ccy(f->e.cap, hub, 1, "LISTX", 2, pk1, 1000000, 1000000), ZXV_OK);
    CHECK_RC(ztm_hub_attach(f->d, NULL, 0, 2), ZXV_E_ARG);
    CHECK_RC(ztm_hub_attach(f->d, hub, 0, 0), ZXV_E_CURRENCY);
    CHECK_RC(ztm_hub_attach(f->d, hub, 5, 2), ZXV_E_NOT_FOUND);
    CHECK_RC(ztm_hub_attach(f->d, hub, 0, 2), ZXV_OK);
    CHECK_RC(ztm_hub_attach(f->d, hub, 0, 2), ZXV_E_DUPLICATE);
    /* no pegs yet */
    CHECK_RC(ztm_order_create_quoted(f->d, "Q-0", f->buyer, f->seller, 1, 500, ZTM_ALG_ED25519,
                                     f->ed_pk, 32, DL, T0, &o),
             ZXV_E_PEG);
    /* the mirror: 1.00 EUR per V-Bill; the list currency: 2.50 LISTX per V-Bill */
    CHECK(sign_peg(f, sk0, 0, 100, 1, 1, sig) == 0, "sign");
    CHECK_RC(zxv_vhub_set_peg(f->e.cap, hub, 0, 100, 1, 1, sig), ZXV_OK);
    CHECK(sign_peg(f, sk1, 1, 250, 1, 4, sig) == 0, "sign");
    CHECK_RC(zxv_vhub_set_peg(f->e.cap, hub, 1, 250, 1, 4, sig), ZXV_OK);
    for (uint64_t lp = 1; lp <= 2000; lp += 37) {
        char id[32];
        int32_t st;
        snprintf(id, sizeof id, "Q-%llu", (unsigned long long) lp);
        st = ztm_order_create_quoted(f->d, id, f->buyer, f->seller, 1, lp, ZTM_ALG_ED25519,
                                     f->ed_pk, 32, DL, T0, &o);
        if (lp % 250 == 0) {
            CHECK(st == ZXV_OK, "exact list price %llu", (unsigned long long) lp);
        } else {
            CHECK(st == ZIND_E_INEXACT, "inexact list price %llu -> %d", (unsigned long long) lp,
                  (int) st);
        }
    }
    CHECK_RC(ztm_order_create_quoted(f->d, "Q-OK", f->buyer, f->seller, 1, 5000, ZTM_ALG_ED25519,
                                     f->ed_pk, 32, DL, T0, &o),
             ZXV_OK);
    CHECK_RC(ztm_order_amounts(f->d, o, &price, NULL, NULL, NULL), ZXV_OK);
    CHECK(price == 2000, "50.00 LISTX = 20 V-Bills = 20.00 EUR (%lld)", (long long) price);
    {
        uint8_t ls = 9;
        uint64_t lpr = 0, vb = 0, le = 0, pe = 0, qa = 0, qv = 0, qr = 0;
        CHECK_RC(ztm_order_quote(f->d, o, &ls, &lpr, &vb, &le, &pe), ZXV_OK);
        CHECK(ls == 1 && lpr == 5000 && vb == 20 && le == 4 && pe == 1, "pinned quote");
        CHECK_RC(zxv_vhub_quote(f->e.cap, hub, 1, 0, 5000, &qa, &qv, &qr), ZXV_OK);
        CHECK(qa == 5000 && qv == vb && (int64_t) qr == price, "the library's quote");
    }
    CHECK_RC(ztm_order_create_quoted(f->d, "Q-0", f->buyer, f->seller, 0, 5000, ZTM_ALG_ED25519,
                                     f->ed_pk, 32, DL, T0, &o),
             ZXV_E_ARG); /* list slot == pay slot */
    CHECK_RC(ztm_order_create_quoted(f->d, "Q-0", f->buyer, f->seller, 1, 0, ZTM_ALG_ED25519,
                                     f->ed_pk, 32, DL, T0, &o),
             ZXV_E_RANGE);
    CHECK_RC(ztm_order_create_quoted(f->d, "Q-0", f->buyer, f->seller, 6, 5000, ZTM_ALG_ED25519,
                                     f->ed_pk, 32, DL, T0, &o),
             ZXV_E_NOT_FOUND);
    /* a quoted order confirms like any other */
    CHECK_RC(ztm_order_find(f->d, "Q-OK", &o), ZXV_OK);
    CHECK(ed_conf(f, f->ed_sk, o, T0 + 9, csig) == 0, "sign");
    CHECK_RC(ztm_confirm(f->d, o, T0 + 9, csig, 64), ZXV_OK);
    check_books(f, "quoted");
    /* the hub ledger was only read */
    {
        uint64_t postings = 99;
        CHECK_RC(zxv_vhub_check(f->e.cap, hub, &postings), ZXV_OK);
        CHECK(postings == 4, "hub postings unchanged (%llu)", (unsigned long long) postings);
    }
    ztm_desk_destroy(f->d); /* the hub must outlive the desk */
    f->d = NULL;
    zxv_vhub_destroy(hub);
}

static void test_chain(fx_t *f) {
    const zind_chain *c = ztm_records(f->d);
    const uint8_t *trust[1] = {zind_signer_pk(f->desk_key)};
    const uint8_t *wrong[1] = {zind_signer_pk(f->rogue)};
    uint32_t n = 0, bad = 0;
    CHECK_RC(zind_chain_count(c, &n), ZXV_OK);
    CHECK(n > 300, "records (%u)", n);
    CHECK_RC(zind_chain_verify(f->e.cap, c, trust, 1, &bad), ZXV_OK);
    CHECK_RC(zind_chain_verify(f->e.cap, c, wrong, 1, &bad), ZIND_E_UNTRUSTED);
    {
        size_t len = 0;
        uint8_t *buf;
        zind_chain *imp = NULL;
        zind_chain_export(c, NULL, 0, &len);
        buf = (uint8_t *) malloc(len);
        if (buf) {
            CHECK_RC(zind_chain_export(c, buf, len, &len), ZXV_OK);
            CHECK_RC(zind_chain_import("tri-market", buf, len, &imp), ZXV_OK);
            CHECK_RC(zind_chain_verify(f->e.cap, imp, trust, 1, &bad), ZXV_OK);
            zind_chain_destroy(imp);
            imp = NULL;
            CHECK_RC(zind_chain_import("tri-deliver", buf, len, &imp), ZXV_E_FORMAT);
            buf[len / 2] ^= 0x20;
            if (zind_chain_import("tri-market", buf, len, &imp) == ZXV_OK) {
                CHECK(zind_chain_verify(f->e.cap, imp, trust, 1, &bad) != ZXV_OK, "tamper found");
                zind_chain_destroy(imp);
            } else {
                CHECK(1, "tamper refused at import");
            }
            free(buf);
        }
    }
}

int main(void) {
    fx_t f;
    test_names();
    if (fx_open(&f) != 0) {
        fprintf(stderr, "fixture failed: %s\n", zt_last_err());
        return 1;
    }
    test_create(&f);
    test_confirm(&f);
    test_fee_carry(&f);
    test_disputes(&f);
    test_timeouts(&f);
    test_chain(&f);
    test_quoted(&f);
    fx_close(&f);
    return zt_report("test_market");
}
