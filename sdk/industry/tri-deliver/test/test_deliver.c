/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_deliver.c — tri-deliver: escrow creation and refusals, Ed25519 and
 * ML-DSA-87 handoff proofs, replayed / double / cross-parcel / wrong-courier
 * / out-of-window / forged proofs, disputes held as BOTH, arbiter decisions
 * (forged, replayed, without ATTEST), cancellation, conservation of money
 * and the signed record chain. */
#include "zind_test.h"
#include "ztdeliver.h"

#undef CHECK_RC
#define CHECK_RC(expr, want)                                                                   \
    do {                                                                                       \
        int32_t rc_ = (expr);                                                                  \
        CHECK(rc_ == (want), "%s -> %d (%s), want %d (%s)", #expr, (int) rc_,                  \
              ztd_status_name(rc_), (int) (want), ztd_status_name(want));                     \
    } while (0)

typedef struct {
    zt_env e;
    zind_signer *desk_key, *arbiter, *rogue, *rcpt_ml;
    uint8_t ed_sk[32], ed_pk[32], ed2_sk[32], ed2_pk[32];
    zxv_hexlog *qlog;
    ztd_desk *d;
    uint32_t issuer, sender, payee;
    int64_t funded;
} fx_t;

static int fx_open(fx_t *f) {
    memset(f, 0, sizeof *f);
    if (zt_env_open(&f->e, "EUR") != 0) return -1;
    f->desk_key = zt_signer(f->e.cap, 1);
    f->arbiter = zt_signer(f->e.cap, 2);
    f->rogue = zt_signer(f->e.cap, 3);
    f->rcpt_ml = zt_signer(f->e.cap, 4);
    if (!f->desk_key || !f->arbiter || !f->rogue || !f->rcpt_ml) return -1;
    zt_seed(f->ed_sk, 9);
    zt_seed(f->ed2_sk, 10);
    if (zxv_ed25519_public(f->e.cap, f->ed_sk, f->ed_pk) != ZXV_OK ||
        zxv_ed25519_public(f->e.cap, f->ed2_sk, f->ed2_pk) != ZXV_OK)
        return -1;
    if (zxv_hexlog_create(f->e.cap, 512, &f->qlog) != ZXV_OK) return -1;
    if (ztd_desk_create(f->e.cap, "EUR", f->desk_key, f->qlog, &f->d) != ZXV_OK) return -1;
    if (ztd_arbiter_add(f->d, zind_signer_pk(f->arbiter)) != ZXV_OK) return -1;
    f->issuer = zt_issuer(f->e.cap, "EUR");
    if (zxv_ledger_open_account_cap(f->e.cap, "shop", "EUR", ZXV_ACCT_HOLDER, &f->sender) !=
            ZXV_OK ||
        zxv_ledger_open_account_cap(f->e.cap, "courier-co", "EUR", ZXV_ACCT_HOLDER, &f->payee) !=
            ZXV_OK)
        return -1;
    if (zt_fund(f->e.cap, "EUR", f->issuer, f->sender, 10000000, "seed-funds") != 0) return -1;
    f->funded = 10000000;
    return 0;
}

static void fx_close(fx_t *f) {
    ztd_desk_destroy(f->d);
    zxv_hexlog_destroy(f->qlog);
    zind_signer_destroy(f->desk_key);
    zind_signer_destroy(f->arbiter);
    zind_signer_destroy(f->rogue);
    zind_signer_destroy(f->rcpt_ml);
    zt_env_close(&f->e);
}

static int32_t state_of(fx_t *f, uint32_t p, uint8_t *tag) {
    int32_t st = -1;
    (void) ztd_parcel_state(f->d, p, &st, tag);
    return st;
}

/* escrow on the ledger == what the desk says it owes; money conserved */
static void check_books(fx_t *f, const char *where) {
    int64_t owed = -1, total;
    CHECK_RC(ztd_escrow_owed(f->d, &owed), ZXV_OK);
    CHECK(zt_equity(f->e.cap, ztd_escrow_account(f->d)) == owed, "%s: escrow %lld == owed %lld",
          where, (long long) zt_equity(f->e.cap, ztd_escrow_account(f->d)), (long long) owed);
    total = zt_equity(f->e.cap, f->sender) + zt_equity(f->e.cap, f->payee) +
            zt_equity(f->e.cap, ztd_escrow_account(f->d));
    CHECK(total == f->funded, "%s: money conserved (%lld)", where, (long long) total);
}

static int ed_proof(fx_t *f, const uint8_t sk[32], const char *id, const char *courier,
                    uint64_t t, uint8_t sig[64]) {
    uint8_t msg[128];
    size_t len = 0;
    if (ztd_handoff_message(id, courier, t, msg, sizeof msg, &len) != ZXV_OK) return -1;
    return zxv_ed25519_sign(f->e.cap, sk, msg, len, sig) == ZXV_OK ? 0 : -1;
}

static int ml_proof(fx_t *f, const zind_signer *k, const char *id, const char *courier,
                    uint64_t t, uint8_t sig[ZIND_SIG_BYTES]) {
    uint8_t msg[128];
    size_t len = 0;
    if (ztd_handoff_message(id, courier, t, msg, sizeof msg, &len) != ZXV_OK) return -1;
    return zind_sign(f->e.cap, k, msg, len, ZTD_HANDOFF_CTX, sig) == ZXV_OK ? 0 : -1;
}

static int arb_sign(fx_t *f, const zind_signer *k, uint32_t p, int32_t outcome,
                    uint8_t sig[ZIND_SIG_BYTES]) {
    uint8_t msg[128];
    size_t len = 0;
    if (ztd_arbiter_message(f->d, p, outcome, msg, sizeof msg, &len) != ZXV_OK) return -1;
    return zind_sign(f->e.cap, k, msg, len, ZTD_ARBITER_CTX, sig) == ZXV_OK ? 0 : -1;
}

static uint32_t mk(fx_t *f, const char *id, int64_t amt, int ml) {
    uint32_t p = 0;
    CHECK_RC(ztd_parcel_create(f->d, id, f->sender, f->payee, amt,
                               ml ? ZTD_ALG_MLDSA87 : ZTD_ALG_ED25519,
                               ml ? zind_signer_pk(f->rcpt_ml) : f->ed_pk,
                               ml ? ZIND_PK_BYTES : 32, 3600, &p),
             ZXV_OK);
    return p;
}

static void test_create(fx_t *f) {
    ztd_desk *d2 = NULL;
    zxv_cap *weak = NULL;
    uint32_t p = 0, usd = 0;
    char long_id[40];
    CHECK_RC(ztd_desk_create(f->e.cap, "QQQ", f->desk_key, NULL, &d2), ZXV_E_CURRENCY);
    CHECK_RC(ztd_desk_create(f->e.cap, "EUR", NULL, NULL, &d2), ZXV_E_ARG);
    CHECK_RC(zxv_cap_attenuate(f->e.cap, ZXV_CAP_LEDGER_POST | ZXV_CAP_LEDGER_READ, &weak), ZXV_OK);
    CHECK_RC(ztd_desk_create(weak, "EUR", f->desk_key, NULL, &d2), ZXV_E_CAP);
    zxv_cap_destroy(weak);
    CHECK_RC(ztd_arbiter_add(f->d, zind_signer_pk(f->arbiter)), ZXV_E_DUPLICATE);

    memset(long_id, 'p', 33);
    long_id[33] = 0;
    CHECK_RC(ztd_parcel_create(f->d, "", f->sender, f->payee, 100, ZTD_ALG_ED25519, f->ed_pk, 32,
                               60, &p),
             ZXV_E_ARG);
    CHECK_RC(ztd_parcel_create(f->d, "has space", f->sender, f->payee, 100, ZTD_ALG_ED25519,
                               f->ed_pk, 32, 60, &p),
             ZXV_E_ARG);
    CHECK_RC(ztd_parcel_create(f->d, long_id, f->sender, f->payee, 100, ZTD_ALG_ED25519, f->ed_pk,
                               32, 60, &p),
             ZXV_E_ARG);
    CHECK_RC(ztd_parcel_create(f->d, "P1", f->sender, f->payee, 100, ZTD_ALG_ED25519,
                               zind_signer_pk(f->rcpt_ml), ZIND_PK_BYTES, 60, &p),
             ZXV_E_ARG);
    CHECK_RC(ztd_parcel_create(f->d, "P1", f->sender, f->payee, 100, ZTD_ALG_MLDSA87, f->ed_pk, 32,
                               60, &p),
             ZXV_E_ARG);
    CHECK_RC(ztd_parcel_create(f->d, "P1", f->sender, f->payee, 100, 3, f->ed_pk, 32, 60, &p),
             ZXV_E_ARG);
    CHECK_RC(ztd_parcel_create(f->d, "P1", f->sender, f->payee, 0, ZTD_ALG_ED25519, f->ed_pk, 32,
                               60, &p),
             ZXV_E_RANGE);
    CHECK_RC(ztd_parcel_create(f->d, "P1", f->sender, f->payee, -5, ZTD_ALG_ED25519, f->ed_pk, 32,
                               60, &p),
             ZXV_E_RANGE);
    CHECK_RC(ztd_parcel_create(f->d, "P1", f->sender, f->payee, ZXV_AMOUNT_MAX + 1,
                               ZTD_ALG_ED25519, f->ed_pk, 32, 60, &p),
             ZXV_E_RANGE);
    CHECK_RC(ztd_parcel_create(f->d, "P1", f->sender, f->payee, 100, ZTD_ALG_ED25519, f->ed_pk, 32,
                               0, &p),
             ZXV_E_ARG);
    CHECK_RC(ztd_parcel_create(f->d, "P1", f->sender, f->payee, 100, ZTD_ALG_ED25519, f->ed_pk, 32,
                               30ULL * 24 * 3600 + 1, &p),
             ZXV_E_ARG);
    CHECK_RC(ztd_parcel_create(f->d, "P1", f->sender, f->sender, 100, ZTD_ALG_ED25519, f->ed_pk, 32,
                               60, &p),
             ZXV_E_ARG);
    CHECK_RC(ztd_parcel_create(f->d, "P1", f->sender, ztd_escrow_account(f->d), 100,
                               ZTD_ALG_ED25519, f->ed_pk, 32, 60, &p),
             ZXV_E_ARG);
    CHECK_RC(ztd_parcel_create(f->d, "P1", f->sender, 77777, 100, ZTD_ALG_ED25519, f->ed_pk, 32,
                               60, &p),
             ZXV_E_NOT_FOUND);
    CHECK_RC(zxv_ccy_enable_cap(f->e.cap, "USD"), ZXV_OK);
    CHECK_RC(zxv_ledger_open_account_cap(f->e.cap, "usd", "USD", ZXV_ACCT_HOLDER, &usd), ZXV_OK);
    CHECK_RC(ztd_parcel_create(f->d, "P1", f->sender, usd, 100, ZTD_ALG_ED25519, f->ed_pk, 32, 60,
                               &p),
             ZXV_E_CURRENCY);
    /* a sender without funds creates nothing, and the id stays free */
    CHECK_RC(ztd_parcel_create(f->d, "P1", f->sender, f->payee, f->funded + 1, ZTD_ALG_ED25519,
                               f->ed_pk, 32, 60, &p),
             ZXV_E_FUNDS);
    CHECK_RC(ztd_parcel_find(f->d, "P1", &p), ZXV_E_NOT_FOUND);
    check_books(f, "after refusals");
    p = mk(f, "P1", 2500, 0);
    CHECK(p == 1, "first parcel");
    CHECK(zt_equity(f->e.cap, ztd_escrow_account(f->d)) == 2500, "escrow funded");
    CHECK_RC(ztd_parcel_create(f->d, "P1", f->sender, f->payee, 100, ZTD_ALG_ED25519, f->ed_pk, 32,
                               60, &p),
             ZXV_E_DUPLICATE);
    {
        uint32_t q = 0;
        CHECK_RC(ztd_parcel_find(f->d, "P1", &q), ZXV_OK);
        CHECK(q == 1, "find");
        CHECK_RC(ztd_parcel_find(f->d, NULL, &q), ZXV_E_ARG);
    }
    check_books(f, "after create");
}

static void test_handoff(fx_t *f, int ml) {
    const char *id = ml ? "PM-200" : "PE-100";
    uint32_t p = mk(f, id, 4200, ml), other;
    uint8_t sig[ZIND_SIG_BYTES], sig2[ZIND_SIG_BYTES], tag = 0, base = 0, ph = 0;
    size_t sl = ml ? ZIND_SIG_BYTES : 64;
    int64_t payee0 = zt_equity(f->e.cap, f->payee);
    uint32_t q0 = 0, q1 = 0;
    int32_t act = -1;
    const uint64_t t0 = 1760000000ULL;
    char other_id[16];

    CHECK(state_of(f, p, &tag) == ZTD_ST_ESCROW, "escrow");
    CHECK(zxv_hex_unpack(tag, &base, NULL, &ph) == ZXV_OK && base == ZXV_HEX_BASE_NEITHER &&
              ph == ZXV_HEX_PHASE_POTENTIAL,
          "escrow is held as Potential (%s)", zxv_hex_name(tag));
    /* proofs before dispatch are refused */
    CHECK((ml ? ml_proof(f, f->rcpt_ml, id, "courier-7", t0, sig)
             : ed_proof(f, f->ed_sk, id, "courier-7", t0, sig)) == 0,
          "make proof");
    CHECK_RC(ztd_handoff_submit(f->d, p, "courier-7", t0, sig, sl), ZXV_E_STATE);
    CHECK_RC(ztd_dispatch(f->d, p, "bad courier", t0), ZXV_E_ARG);
    CHECK_RC(ztd_dispatch(f->d, p, "courier-7", UINT64_MAX), ZXV_E_RANGE);
    CHECK_RC(ztd_dispatch(f->d, p, "courier-7", t0), ZXV_OK);
    CHECK_RC(ztd_dispatch(f->d, p, "courier-7", t0), ZXV_E_STATE);
    CHECK(state_of(f, p, &tag) == ZTD_ST_OUT, "out");
    CHECK(zxv_hex_unpack(tag, NULL, NULL, &ph) == ZXV_OK && ph == ZXV_HEX_PHASE_EMERGING,
          "out = emerging");
    CHECK_RC(ztd_cancel(f->d, p), ZXV_E_STATE);

    /* refusals that change nothing */
    CHECK_RC(ztd_handoff_submit(f->d, p, "courier-7", t0, sig, sl - 1), ZXV_E_ARG);
    CHECK_RC(ztd_handoff_submit(f->d, p, "courier-7", t0, NULL, sl), ZXV_E_ARG);
    CHECK_RC(ztd_handoff_submit(f->d, p, "courier-8", t0, sig, sl), ZTD_E_COURIER);
    CHECK_RC(ztd_handoff_submit(f->d, p, "courier-7", t0 - 1, sig, sl), ZTD_E_WINDOW);
    CHECK_RC(ztd_handoff_submit(f->d, p, "courier-7", t0 + 3601, sig, sl), ZTD_E_WINDOW);
    /* the signature covers the time: claiming another time fails */
    CHECK_RC(ztd_handoff_submit(f->d, p, "courier-7", t0 + 5, sig, sl), ZXV_E_VERIFY);
    CHECK_RC(zxv_hexlog_count(f->qlog, &q0), ZXV_OK);
    /* the wrong recipient key */
    CHECK((ml ? ml_proof(f, f->rogue, id, "courier-7", t0, sig2)
             : ed_proof(f, f->ed2_sk, id, "courier-7", t0, sig2)) == 0,
          "rogue proof");
    CHECK_RC(ztd_handoff_submit(f->d, p, "courier-7", t0, sig2, sl), ZXV_E_VERIFY);
    /* one flipped bit */
    memcpy(sig2, sig, sl);
    sig2[sl / 2] ^= 4;
    CHECK_RC(ztd_handoff_submit(f->d, p, "courier-7", t0, sig2, sl), ZXV_E_VERIFY);
    CHECK_RC(zxv_hexlog_count(f->qlog, &q1), ZXV_OK);
    CHECK(q1 == q0 + 2, "forged proofs quarantined (%u)", q1 - q0);
    /* a proof for another parcel, same recipient, courier and time */
    snprintf(other_id, sizeof other_id, "%s-B", id);
    other = mk(f, other_id, 1000, ml);
    CHECK_RC(ztd_dispatch(f->d, other, "courier-7", t0), ZXV_OK);
    CHECK_RC(ztd_handoff_submit(f->d, other, "courier-7", t0, sig, sl), ZXV_E_VERIFY);
    CHECK(state_of(f, p, NULL) == ZTD_ST_OUT, "still out");
    CHECK(zt_equity(f->e.cap, f->payee) == payee0, "payee unpaid so far");
    check_books(f, "before handoff");

    /* the genuine proof, at the edge of the window */
    CHECK((ml ? ml_proof(f, f->rcpt_ml, id, "courier-7", t0 + 3600, sig)
             : ed_proof(f, f->ed_sk, id, "courier-7", t0 + 3600, sig)) == 0,
          "edge proof");
    CHECK_RC(ztd_handoff_submit(f->d, p, "courier-7", t0 + 3600, sig, sl), ZXV_OK);
    CHECK(state_of(f, p, &tag) == ZTD_ST_DELIVERED, "delivered");
    CHECK(zxv_hex_unpack(tag, &base, NULL, &ph) == ZXV_OK && base == ZXV_HEX_BASE_TRUE &&
              ph == ZXV_HEX_PHASE_MANIFEST,
          "delivered = true/manifest");
    CHECK(zxv_hex_action(tag, &act) == ZXV_OK && act == ZXV_HEX_ACT_PROCEED, "proceed");
    CHECK(zt_equity(f->e.cap, f->payee) == payee0 + 4200, "payee paid exactly once");
    /* replay and double handoff */
    CHECK_RC(ztd_handoff_submit(f->d, p, "courier-7", t0 + 3600, sig, sl), ZIND_E_REPLAY);
    CHECK((ml ? ml_proof(f, f->rcpt_ml, id, "courier-7", t0 + 10, sig2)
             : ed_proof(f, f->ed_sk, id, "courier-7", t0 + 10, sig2)) == 0,
          "second genuine proof");
    CHECK_RC(ztd_handoff_submit(f->d, p, "courier-7", t0 + 10, sig2, sl), ZIND_E_REPLAY);
    CHECK(zt_equity(f->e.cap, f->payee) == payee0 + 4200, "still paid once");
    CHECK_RC(ztd_dispute(f->d, p, 1), ZXV_E_STATE); /* settled: outside the kit */
    CHECK_RC(ztd_close(f->d, p), ZXV_OK);
    CHECK(state_of(f, p, &tag) == ZTD_ST_CLOSED, "closed");
    CHECK(zxv_hex_unpack(tag, NULL, NULL, &ph) == ZXV_OK && ph == ZXV_HEX_PHASE_DISSOLVING,
          "closed = dissolving");
    CHECK_RC(ztd_close(f->d, p), ZXV_E_STATE);
    CHECK_RC(ztd_handoff_submit(f->d, p, "courier-7", t0 + 3600, sig, sl), ZIND_E_REPLAY);
    /* deliver the second parcel properly */
    CHECK((ml ? ml_proof(f, f->rcpt_ml, other_id, "courier-7", t0 + 1, sig)
             : ed_proof(f, f->ed_sk, other_id, "courier-7", t0 + 1, sig)) == 0,
          "other proof");
    CHECK_RC(ztd_handoff_submit(f->d, other, "courier-7", t0 + 1, sig, sl), ZXV_OK);
    check_books(f, "after handoff");
}

static void test_message(void) {
    uint8_t b[128];
    size_t n = 0;
    CHECK_RC(ztd_handoff_message("P", "C", 1, NULL, 0, &n), ZXV_E_BUFFER);
    CHECK(n == 14 + 2 + 2 + 8, "message size %zu", n);
    CHECK_RC(ztd_handoff_message("P", "C", 1, b, n, &n), ZXV_OK);
    CHECK(memcmp(b, "ztd/handoff/v1", 14) == 0 && b[14] == 1 && b[15] == 'P' && b[16] == 1 &&
              b[17] == 'C' && b[18] == 1 && b[25] == 0,
          "canonical layout");
    CHECK_RC(ztd_handoff_message("P", "C", 1, b, n - 1, &n), ZXV_E_BUFFER);
    CHECK_RC(ztd_handoff_message("P Q", "C", 1, b, sizeof b, &n), ZXV_E_ARG);
    CHECK_RC(ztd_handoff_message("P", "", 1, b, sizeof b, &n), ZXV_E_ARG);
    CHECK_RC(ztd_handoff_message("P", "C", 1, b, sizeof b, NULL), ZXV_E_ARG);
}

static void test_disputes(fx_t *f) {
    uint32_t a = mk(f, "D-1", 700, 0), b = mk(f, "D-2", 800, 1), c = mk(f, "D-3", 900, 0);
    uint32_t q0 = 0, q1 = 0;
    uint8_t sig[ZIND_SIG_BYTES], sig_b[ZIND_SIG_BYTES], ed[64], tag = 0, base = 0;
    int64_t sender0, payee0;
    int32_t act = -1;
    const uint64_t t0 = 1760100000ULL;

    CHECK_RC(ztd_dispatch(f->d, b, "courier-1", t0), ZXV_OK);
    CHECK_RC(ztd_dispatch(f->d, c, "courier-1", t0), ZXV_OK);
    CHECK_RC(zxv_hexlog_count(f->qlog, &q0), ZXV_OK);
    CHECK_RC(ztd_dispute(f->d, a, 11), ZXV_OK); /* before dispatch */
    CHECK_RC(ztd_dispute(f->d, b, 12), ZXV_OK); /* out for delivery */
    CHECK_RC(ztd_dispute(f->d, c, 13), ZXV_OK);
    CHECK_RC(zxv_hexlog_count(f->qlog, &q1), ZXV_OK);
    CHECK(q1 == q0 + 3, "disputes quarantined as BOTH");
    CHECK(state_of(f, a, &tag) == ZTD_ST_DISPUTED, "disputed");
    CHECK(zxv_hex_unpack(tag, &base, NULL, NULL) == ZXV_OK && base == ZXV_HEX_BASE_BOTH, "BOTH");
    CHECK(zxv_hex_action(tag, &act) == ZXV_OK && act == ZXV_HEX_ACT_ESCALATE, "escalate");
    /* frozen */
    CHECK_RC(ztd_dispute(f->d, a, 1), ZIND_E_HELD);
    CHECK_RC(ztd_dispatch(f->d, a, "courier-1", t0), ZIND_E_HELD);
    CHECK_RC(ztd_cancel(f->d, a), ZIND_E_HELD);
    CHECK_RC(ztd_close(f->d, a), ZIND_E_HELD);
    CHECK(ml_proof(f, f->rcpt_ml, "D-2", "courier-1", t0 + 5, sig) == 0, "valid proof for b");
    CHECK_RC(ztd_handoff_submit(f->d, b, "courier-1", t0 + 5, sig, ZIND_SIG_BYTES), ZIND_E_HELD);
    CHECK(ed_proof(f, f->ed_sk, "D-3", "courier-1", t0 + 5, ed) == 0, "valid proof for c");
    CHECK_RC(ztd_handoff_submit(f->d, c, "courier-1", t0 + 5, ed, 64), ZIND_E_HELD);
    check_books(f, "while disputed");

    /* arbiter decisions */
    sender0 = zt_equity(f->e.cap, f->sender);
    payee0 = zt_equity(f->e.cap, f->payee);
    CHECK(arb_sign(f, f->arbiter, a, ZTD_REFUND_SENDER, sig) == 0, "arbiter signs a");
    CHECK(arb_sign(f, f->arbiter, b, ZTD_PAY_PAYEE, sig_b) == 0, "arbiter signs b");
    CHECK_RC(ztd_arbiter_resolve(f->d, a, zind_signer_pk(f->rogue), ZTD_REFUND_SENDER, sig),
             ZTD_E_ARBITER);
    CHECK_RC(ztd_arbiter_resolve(f->d, a, zind_signer_pk(f->arbiter), 3, sig), ZXV_E_ARG);
    CHECK_RC(zxv_hexlog_count(f->qlog, &q0), ZXV_OK);
    CHECK_RC(ztd_arbiter_resolve(f->d, a, zind_signer_pk(f->arbiter), ZTD_PAY_PAYEE, sig),
             ZXV_E_VERIFY); /* signed refund, presented as pay */
    CHECK_RC(ztd_arbiter_resolve(f->d, a, zind_signer_pk(f->arbiter), ZTD_PAY_PAYEE, sig_b),
             ZXV_E_VERIFY); /* b's decision on a */
    {
        uint8_t forged[ZIND_SIG_BYTES];
        CHECK(arb_sign(f, f->rogue, a, ZTD_REFUND_SENDER, forged) == 0, "rogue signs");
        CHECK_RC(ztd_arbiter_resolve(f->d, a, zind_signer_pk(f->arbiter), ZTD_REFUND_SENDER,
                                     forged),
                 ZXV_E_VERIFY);
    }
    CHECK_RC(zxv_hexlog_count(f->qlog, &q1), ZXV_OK);
    CHECK(q1 == q0 + 3, "forged decisions quarantined");
    {
        zxv_cap *noattest = NULL;
        ztd_desk *d2 = NULL;
        uint32_t x = 0;
        uint8_t m[128], s2[ZIND_SIG_BYTES];
        size_t len = 0;
        CHECK_RC(zxv_cap_attenuate(f->e.cap, ZXV_CAP_ALL & ~ZXV_CAP_ATTEST, &noattest), ZXV_OK);
        CHECK_RC(ztd_desk_create(noattest, "EUR", f->desk_key, NULL, &d2), ZXV_OK);
        CHECK_RC(ztd_arbiter_add(d2, zind_signer_pk(f->arbiter)), ZXV_OK);
        CHECK_RC(ztd_parcel_create(d2, "X", f->sender, f->payee, 5, ZTD_ALG_ED25519, f->ed_pk, 32,
                                   60, &x),
                 ZXV_OK);
        CHECK_RC(ztd_dispute(d2, x, 0), ZXV_OK);
        CHECK_RC(ztd_arbiter_message(d2, x, ZTD_REFUND_SENDER, m, sizeof m, &len), ZXV_OK);
        CHECK_RC(zind_sign(f->e.cap, f->arbiter, m, len, ZTD_ARBITER_CTX, s2), ZXV_OK);
        CHECK_RC(ztd_arbiter_resolve(d2, x, zind_signer_pk(f->arbiter), ZTD_REFUND_SENDER, s2),
                 ZXV_E_CAP);
        /* tidy the money back for the conservation checks */
        CHECK_RC(zxv_ledger_post_cap(f->e.cap, ztd_escrow_account(d2), f->sender, 5, "EUR",
                                     "tidy-x", NULL),
                 ZXV_OK);
        ztd_desk_destroy(d2);
        zxv_cap_destroy(noattest);
    }
    CHECK_RC(ztd_arbiter_resolve(f->d, a, zind_signer_pk(f->arbiter), ZTD_REFUND_SENDER, sig),
             ZXV_OK);
    CHECK(state_of(f, a, &tag) == ZTD_ST_REFUNDED, "refunded");
    CHECK(zt_equity(f->e.cap, f->sender) == sender0 + 700, "sender refunded exactly");
    CHECK_RC(ztd_arbiter_resolve(f->d, a, zind_signer_pk(f->arbiter), ZTD_REFUND_SENDER, sig),
             ZIND_E_REPLAY);
    CHECK_RC(ztd_arbiter_resolve(f->d, b, zind_signer_pk(f->arbiter), ZTD_PAY_PAYEE, sig_b),
             ZXV_OK);
    CHECK(state_of(f, b, &tag) == ZTD_ST_DELIVERED, "paid by decision");
    CHECK(zt_equity(f->e.cap, f->payee) == payee0 + 800, "payee paid by decision");
    CHECK_RC(ztd_close(f->d, b), ZXV_OK);
    /* a dispute before dispatch, paid by decision, still reaches Manifest */
    {
        uint32_t e = mk(f, "D-4", 50, 0);
        uint8_t ph = 0;
        CHECK_RC(ztd_dispute(f->d, e, 0), ZXV_OK);
        CHECK(arb_sign(f, f->arbiter, e, ZTD_PAY_PAYEE, sig) == 0, "sign e");
        CHECK_RC(ztd_arbiter_resolve(f->d, e, zind_signer_pk(f->arbiter), ZTD_PAY_PAYEE, sig),
                 ZXV_OK);
        CHECK(state_of(f, e, &tag) == ZTD_ST_DELIVERED &&
                  zxv_hex_unpack(tag, NULL, NULL, &ph) == ZXV_OK && ph == ZXV_HEX_PHASE_MANIFEST,
              "manifest after decision");
    }
    CHECK_RC(ztd_arbiter_message(f->d, b, ZTD_PAY_PAYEE, sig, 10, &(size_t){0}), ZXV_E_STATE);
    check_books(f, "after decisions");

    /* cancel */
    {
        uint32_t k = mk(f, "K-1", 333, 0);
        int64_t s0 = zt_equity(f->e.cap, f->sender);
        CHECK_RC(ztd_cancel(f->d, k), ZXV_OK);
        CHECK(state_of(f, k, NULL) == ZTD_ST_CANCELLED, "cancelled");
        CHECK(zt_equity(f->e.cap, f->sender) == s0 + 333, "refunded on cancel");
        CHECK_RC(ztd_cancel(f->d, k), ZXV_E_STATE);
        CHECK_RC(ztd_dispatch(f->d, k, "c", 1), ZXV_E_STATE);
        CHECK_RC(ztd_dispute(f->d, k, 0), ZXV_E_STATE);
        CHECK_RC(ztd_parcel_create(f->d, "K-1", f->sender, f->payee, 1, ZTD_ALG_ED25519, f->ed_pk,
                                   32, 60, &k),
                 ZXV_E_DUPLICATE); /* ids are never reused */
    }
    CHECK_RC(ztd_parcel_state(f->d, 0, NULL, NULL), ZXV_E_NOT_FOUND);
    CHECK_RC(ztd_parcel_state(f->d, 99999, NULL, NULL), ZXV_E_NOT_FOUND);
    CHECK_RC(ztd_dispute(NULL, 1, 0), ZXV_E_ARG);
    check_books(f, "after cancel");
}

static void test_bulk(fx_t *f) {
    uint64_t s = 99;
    for (int i = 0; i < 40; i++) {
        char id[16];
        int ml = i % 4 == 0;
        int64_t amt = 1 + (int64_t) (zt_rand(&s) % 50000);
        uint32_t p;
        uint64_t t = 1760200000ULL + (uint64_t) i * 100;
        uint8_t sig[ZIND_SIG_BYTES];
        snprintf(id, sizeof id, "B-%d", i);
        p = mk(f, id, amt, ml);
        CHECK_RC(ztd_dispatch(f->d, p, "courier-b", t), ZXV_OK);
        switch (zt_rand(&s) % 3) {
        case 0:
            CHECK((ml ? ml_proof(f, f->rcpt_ml, id, "courier-b", t + 60, sig)
                      : ed_proof(f, f->ed_sk, id, "courier-b", t + 60, sig)) == 0,
                  "proof");
            CHECK_RC(ztd_handoff_submit(f->d, p, "courier-b", t + 60, sig, ml ? ZIND_SIG_BYTES : 64),
                     ZXV_OK);
            CHECK_RC(ztd_handoff_submit(f->d, p, "courier-b", t + 60, sig, ml ? ZIND_SIG_BYTES : 64),
                     ZIND_E_REPLAY);
            break;
        case 1:
            CHECK_RC(ztd_dispute(f->d, p, 7), ZXV_OK);
            CHECK(arb_sign(f, f->arbiter, p, (i % 2) ? ZTD_PAY_PAYEE : ZTD_REFUND_SENDER, sig) == 0,
                  "decision");
            CHECK_RC(ztd_arbiter_resolve(f->d, p, zind_signer_pk(f->arbiter),
                                         (i % 2) ? ZTD_PAY_PAYEE : ZTD_REFUND_SENDER, sig),
                     ZXV_OK);
            break;
        default: break; /* left out for delivery */
        }
        check_books(f, "bulk");
    }
    CHECK_RC(zxv_ledger_check_cap(f->e.cap), ZXV_OK);
}

static void test_records(fx_t *f) {
    const zind_chain *c = ztd_records(f->d);
    const uint8_t *trust[1] = {zind_signer_pk(f->desk_key)};
    const uint8_t *wrong[1] = {zind_signer_pk(f->arbiter)};
    uint32_t n = 0, bad = 0;
    int seen[8] = {0};
    CHECK_RC(zind_chain_count(c, &n), ZXV_OK);
    CHECK(n > 100, "history (%u)", n);
    CHECK_RC(zind_chain_verify(f->e.cap, c, trust, 1, &bad), ZXV_OK);
    CHECK_RC(zind_chain_verify(f->e.cap, c, wrong, 1, &bad), ZIND_E_UNTRUSTED);
    for (uint32_t i = 0; i < n; i++) {
        zind_record r;
        CHECK_RC(zind_chain_at(c, i, &r), ZXV_OK);
        if (r.kind < 8) seen[r.kind] = 1;
    }
    for (int k = ZTD_REC_CREATE; k <= ZTD_REC_CLOSE; k++) CHECK(seen[k], "record kind %d", k);
    {
        size_t len = 0;
        uint8_t *buf;
        zind_chain *imp = NULL;
        zind_chain_export(c, NULL, 0, &len);
        buf = (uint8_t *) malloc(len);
        CHECK_RC(zind_chain_export(c, buf, len, &len), ZXV_OK);
        for (size_t off = 200; off < len; off += len / 23) {
            buf[off] ^= 0x01;
            CHECK_RC(zind_chain_import("tri-deliver", buf, len, &imp), ZXV_OK);
            CHECK(zind_chain_verify(f->e.cap, imp, trust, 1, NULL) != ZXV_OK,
                  "tamper at %zu detected", off);
            zind_chain_destroy(imp);
            buf[off] ^= 0x01;
        }
        CHECK_RC(zind_chain_import("tri-deliver", buf, len, &imp), ZXV_OK);
        CHECK_RC(zind_chain_verify(f->e.cap, imp, trust, 1, NULL), ZXV_OK);
        zind_chain_destroy(imp);
        free(buf);
    }
    CHECK_RC(zxv_hexlog_verify(f->qlog), ZXV_OK);
    CHECK(strcmp(ztd_status_name(ZTD_E_WINDOW), "ZTD_E_WINDOW") == 0, "names");
    CHECK(strcmp(ztd_status_name(ZIND_E_REPLAY), "ZIND_E_REPLAY") == 0, "names 2");
    CHECK(ztd_records(NULL) == NULL && ztd_escrow_account(NULL) == UINT32_MAX, "NULL desk");
}

int main(void) {
    fx_t f;
    test_message();
    if (fx_open(&f) != 0) {
        fprintf(stderr, "fixture failed: %s\n", zt_last_err());
        return 2;
    }
    test_create(&f);
    test_handoff(&f, 0);
    test_handoff(&f, 1);
    test_disputes(&f);
    test_bulk(&f);
    test_records(&f);
    fx_close(&f);
    return zt_report("test_deliver");
}
