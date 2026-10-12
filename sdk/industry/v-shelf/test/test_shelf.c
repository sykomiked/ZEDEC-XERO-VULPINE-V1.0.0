/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_shelf.c — v-shelf: setup refusals, whole and fractional units,
 * negative stock, duplicate references, overflow, transfers and shrinkage,
 * cycle counts with variances held for approvers (forged, wrong decision,
 * without ATTEST), conservation of flows and the journal replay. */
#include "zind_test.h"
#include "zvshelf.h"

#undef CHECK_RC
#define CHECK_RC(expr, want)                                                                   \
    do {                                                                                       \
        int32_t rc_ = (expr);                                                                  \
        CHECK(rc_ == (want), "%s -> %d (%s), want %d (%s)", #expr, (int) rc_,                  \
              zvs_status_name(rc_), (int) (want), zvs_status_name(want));                     \
    } while (0)

static zind_qty Q(int64_t n, int64_t d) {
    zind_qty q = ZIND_QTY_ZERO;
    (void) zind_qty_make(n, d, &q);
    return q;
}
static zind_qty P(const char *s) {
    zind_qty q = ZIND_QTY_ZERO;
    (void) zind_qty_parse(s, &q);
    return q;
}
static int qeq(zind_qty a, zind_qty b) { return a.num == b.num && a.den == b.den; }

typedef struct {
    zt_env e;
    zind_signer *key, *mgr, *rogue;
    zxv_hexlog *qlog;
    zvs_store *s;
    uint32_t floor, back, wh;
} fx_t;

static int fx_open(fx_t *f) {
    memset(f, 0, sizeof *f);
    if (zt_env_open(&f->e, NULL) != 0) return -1;
    f->key = zt_signer(f->e.cap, 1);
    f->mgr = zt_signer(f->e.cap, 2);
    f->rogue = zt_signer(f->e.cap, 3);
    if (!f->key || !f->mgr || !f->rogue) return -1;
    if (zxv_hexlog_create(f->e.cap, 256, &f->qlog) != ZXV_OK) return -1;
    if (zvs_store_create(f->e.cap, f->key, f->qlog, &f->s) != ZXV_OK) return -1;
    if (zvs_location_add(f->s, "floor", &f->floor) != ZXV_OK ||
        zvs_location_add(f->s, "backroom", &f->back) != ZXV_OK ||
        zvs_location_add(f->s, "warehouse", &f->wh) != ZXV_OK)
        return -1;
    if (zvs_approver_add(f->s, zind_signer_pk(f->mgr)) != ZXV_OK) return -1;
    return 0;
}

static void fx_close(fx_t *f) {
    zvs_store_destroy(f->s);
    zxv_hexlog_destroy(f->qlog);
    zind_signer_destroy(f->key);
    zind_signer_destroy(f->mgr);
    zind_signer_destroy(f->rogue);
    zt_env_close(&f->e);
}

static zind_qty on_hand(fx_t *f, const char *sku, uint32_t loc) {
    zind_qty q = {-999, 1};
    (void) zvs_on_hand(f->s, sku, loc, &q);
    return q;
}

static void check_flows(fx_t *f, const char *sku) {
    zind_qty r, so, sh, ad, t, x;
    CHECK_RC(zvs_flows(f->s, sku, &r, &so, &sh, &ad), ZXV_OK);
    CHECK_RC(zvs_total(f->s, sku, &t), ZXV_OK);
    CHECK(zind_qty_sub(r, so, &x) == ZXV_OK && zind_qty_sub(x, sh, &x) == ZXV_OK &&
              zind_qty_add(x, ad, &x) == ZXV_OK && qeq(x, t),
          "%s: received - sold - shrunk + adjusted == on hand", sku);
}

static int approve(fx_t *f, const zind_signer *k, const char *sku, uint32_t loc, int32_t dec,
                   uint8_t sig[ZIND_SIG_BYTES]) {
    uint8_t m[128];
    size_t len = 0;
    if (zvs_variance_message(f->s, sku, loc, dec, m, sizeof m, &len) != ZXV_OK) return -1;
    return zind_sign(f->e.cap, k, m, len, ZVS_VARIANCE_CTX, sig) == ZXV_OK ? 0 : -1;
}

static void test_setup(fx_t *f) {
    zvs_store *s2 = NULL;
    zxv_cap *weak = NULL;
    uint32_t loc = 0;
    char code[24];
    CHECK_RC(zxv_cap_attenuate(f->e.cap, ZXV_CAP_LEDGER_READ | ZXV_CAP_ATTEST, &weak), ZXV_OK);
    CHECK_RC(zvs_store_create(weak, f->key, NULL, &s2), ZXV_E_CAP);
    zxv_cap_destroy(weak);
    CHECK_RC(zxv_cap_attenuate(f->e.cap, ZXV_CAP_CRYPTO_SIGN, &weak), ZXV_OK);
    CHECK_RC(zvs_store_create(weak, f->key, f->qlog, &s2), ZXV_E_CAP);
    zxv_cap_destroy(weak);
    CHECK_RC(zvs_store_create(f->e.cap, NULL, NULL, &s2), ZXV_E_ARG);

    CHECK_RC(zvs_location_add(f->s, "floor", &loc), ZXV_E_DUPLICATE);
    CHECK_RC(zvs_location_add(f->s, "", &loc), ZXV_E_ARG);
    CHECK_RC(zvs_location_add(f->s, "back room", &loc), ZXV_E_ARG);
    CHECK_RC(zvs_location_add(f->s, "abcdefghijklmnopq", &loc), ZXV_E_ARG);
    for (int i = 0; i < 15; i++) {
        snprintf(code, sizeof code, "L%d", i);
        int32_t st = zvs_location_add(f->s, code, &loc);
        CHECK(st == (i < 13 ? ZXV_OK : ZXV_E_CAPACITY), "location %d -> %s", i,
              zvs_status_name(st));
    }
    CHECK_RC(zvs_item_add(f->s, "SKU-COLA-330", ZVS_UNIT_EACH, "ea"), ZXV_OK);
    CHECK_RC(zvs_item_add(f->s, "SKU-COLA-330", ZVS_UNIT_EACH, "ea"), ZXV_E_DUPLICATE);
    CHECK_RC(zvs_item_add(f->s, "SKU-APPLES", ZVS_UNIT_MEASURE, "kg"), ZXV_OK);
    CHECK_RC(zvs_item_add(f->s, "SKU-MILK", ZVS_UNIT_MEASURE, "l"), ZXV_OK);
    CHECK_RC(zvs_item_add(f->s, "bad sku", ZVS_UNIT_EACH, "ea"), ZXV_E_ARG);
    CHECK_RC(zvs_item_add(f->s, "X", 3, "ea"), ZXV_E_ARG);
    CHECK_RC(zvs_item_add(f->s, "X", ZVS_UNIT_EACH, ""), ZXV_E_ARG);
    CHECK_RC(zvs_item_add(f->s, "X", ZVS_UNIT_EACH, "kilograms"), ZXV_E_ARG);
    CHECK_RC(zvs_item_add(NULL, "X", ZVS_UNIT_EACH, "ea"), ZXV_E_ARG);
    CHECK_RC(zvs_approver_add(f->s, zind_signer_pk(f->mgr)), ZXV_E_DUPLICATE);
    CHECK_RC(zvs_approver_add(f->s, NULL), ZXV_E_ARG);
}

static void test_movements(fx_t *f) {
    uint32_t n0 = 0, n1 = 0;
    zind_qty t;
    /* whole units */
    CHECK_RC(zvs_receive(f->s, "PO-1", "SKU-COLA-330", f->back, Q(48, 1)), ZXV_OK);
    CHECK(qeq(on_hand(f, "SKU-COLA-330", f->back), Q(48, 1)), "48 in the backroom");
    CHECK_RC(zvs_receive(f->s, "PO-1", "SKU-COLA-330", f->back, Q(1, 1)), ZIND_E_REPLAY);
    CHECK_RC(zvs_sell(f->s, "PO-1", "SKU-COLA-330", f->back, Q(1, 1)), ZIND_E_REPLAY);
    CHECK_RC(zvs_receive(f->s, "PO-2", "SKU-COLA-330", f->back, Q(1, 2)), ZVS_E_UNIT);
    CHECK_RC(zvs_receive(f->s, "PO-2", "SKU-COLA-330", f->back, Q(0, 1)), ZXV_E_ARG);
    CHECK_RC(zvs_receive(f->s, "PO-2", "SKU-COLA-330", f->back, Q(-3, 1)), ZXV_E_ARG);
    CHECK_RC(zvs_receive(f->s, "PO-2", "SKU-COLA-330", f->back, (zind_qty){4, 2}), ZXV_E_ARG);
    CHECK_RC(zvs_receive(f->s, "PO-2", "SKU-NOPE", f->back, Q(1, 1)), ZVS_E_SKU);
    CHECK_RC(zvs_receive(f->s, "PO-2", "SKU-COLA-330", 99, Q(1, 1)), ZVS_E_LOCATION);
    CHECK_RC(zvs_receive(f->s, "bad ref", "SKU-COLA-330", f->back, Q(1, 1)), ZXV_E_ARG);
    CHECK_RC(zvs_receive(f->s, "", "SKU-COLA-330", f->back, Q(1, 1)), ZXV_E_ARG);
    CHECK_RC(zvs_receive(f->s, NULL, "SKU-COLA-330", f->back, Q(1, 1)), ZXV_E_ARG);

    CHECK_RC(zvs_transfer(f->s, "TR-1", "SKU-COLA-330", f->back, f->back, Q(1, 1)),
             ZVS_E_LOCATION);
    CHECK_RC(zvs_transfer(f->s, "TR-1", "SKU-COLA-330", f->back, 99, Q(1, 1)), ZVS_E_LOCATION);
    CHECK_RC(zvs_transfer(f->s, "TR-1", "SKU-COLA-330", f->back, f->floor, Q(49, 1)),
             ZIND_E_NEGATIVE);
    CHECK_RC(zvs_transfer(f->s, "TR-1", "SKU-COLA-330", f->back, f->floor, Q(24, 1)), ZXV_OK);
    CHECK(qeq(on_hand(f, "SKU-COLA-330", f->back), Q(24, 1)) &&
              qeq(on_hand(f, "SKU-COLA-330", f->floor), Q(24, 1)),
          "24 + 24");
    CHECK_RC(zvs_total(f->s, "SKU-COLA-330", &t), ZXV_OK);
    CHECK(qeq(t, Q(48, 1)), "transfers keep the total");

    /* negative stock is refused and leaves no record; the reference stays free */
    CHECK_RC(zind_chain_count(zvs_records(f->s), &n0), ZXV_OK);
    CHECK_RC(zvs_sell(f->s, "POS-1", "SKU-COLA-330", f->floor, Q(25, 1)), ZIND_E_NEGATIVE);
    CHECK_RC(zind_chain_count(zvs_records(f->s), &n1), ZXV_OK);
    CHECK(n1 == n0, "no record for a refused sale");
    CHECK_RC(zvs_sell(f->s, "POS-1", "SKU-COLA-330", f->floor, Q(24, 1)), ZXV_OK);
    CHECK(qeq(on_hand(f, "SKU-COLA-330", f->floor), ZIND_QTY_ZERO), "floor sold out");
    CHECK_RC(zvs_sell(f->s, "POS-2", "SKU-COLA-330", f->floor, Q(1, 1)), ZIND_E_NEGATIVE);
    CHECK_RC(zvs_shrink(f->s, "SH-1", "SKU-COLA-330", f->back, Q(2, 1), ZVS_SHRINK_DAMAGE),
             ZXV_OK);
    CHECK_RC(zvs_shrink(f->s, "SH-2", "SKU-COLA-330", f->back, Q(2, 1), 0), ZXV_E_ARG);
    CHECK_RC(zvs_shrink(f->s, "SH-2", "SKU-COLA-330", f->back, Q(2, 1), 5), ZXV_E_ARG);
    CHECK_RC(zvs_shrink(f->s, "SH-2", "SKU-COLA-330", f->back, Q(23, 1), ZVS_SHRINK_THEFT),
             ZIND_E_NEGATIVE);
    CHECK(qeq(on_hand(f, "SKU-COLA-330", f->back), Q(22, 1)), "22 left");
    check_flows(f, "SKU-COLA-330");

    /* exact fractions: 3/4 kg + 0.5 kg - 1/3 kg = 11/12 kg */
    CHECK_RC(zvs_receive(f->s, "PO-A1", "SKU-APPLES", f->floor, P("3/4")), ZXV_OK);
    CHECK_RC(zvs_receive(f->s, "PO-A2", "SKU-APPLES", f->floor, P("0.5")), ZXV_OK);
    CHECK(qeq(on_hand(f, "SKU-APPLES", f->floor), Q(5, 4)), "5/4 kg");
    CHECK_RC(zvs_sell(f->s, "POS-A1", "SKU-APPLES", f->floor, Q(1, 3)), ZXV_OK);
    CHECK(qeq(on_hand(f, "SKU-APPLES", f->floor), Q(11, 12)), "11/12 kg exactly");
    CHECK_RC(zvs_sell(f->s, "POS-A2", "SKU-APPLES", f->floor, Q(1, 1)), ZIND_E_NEGATIVE);
    CHECK_RC(zvs_sell(f->s, "POS-A2", "SKU-APPLES", f->floor, Q(11, 12)), ZXV_OK);
    CHECK(qeq(on_hand(f, "SKU-APPLES", f->floor), ZIND_QTY_ZERO), "exactly zero, no dust");
    /* a scale reading of 0.1 kg is exactly 1/10, three of them exactly 3/10 */
    for (int i = 0; i < 3; i++) {
        char ref[32];
        snprintf(ref, sizeof ref, "PO-T%d", i);
        CHECK_RC(zvs_receive(f->s, ref, "SKU-APPLES", f->floor, P("0.1")), ZXV_OK);
    }
    CHECK(qeq(on_hand(f, "SKU-APPLES", f->floor), Q(3, 10)), "0.1 + 0.1 + 0.1 == 0.3 exactly");
    check_flows(f, "SKU-APPLES");

    /* overflow is refused, never wrapped or rounded */
    CHECK_RC(zvs_receive(f->s, "PO-M1", "SKU-MILK", f->wh, Q(INT64_MAX / 2, 1)), ZXV_OK);
    CHECK_RC(zvs_receive(f->s, "PO-M2", "SKU-MILK", f->wh, Q(INT64_MAX / 2, 1)), ZXV_OK);
    CHECK_RC(zvs_receive(f->s, "PO-M3", "SKU-MILK", f->wh, Q(2, 1)), ZXV_E_RANGE);
    CHECK(qeq(on_hand(f, "SKU-MILK", f->wh), Q((INT64_MAX / 2) * 2, 1)), "unchanged on overflow");
    CHECK_RC(zvs_sell(f->s, "POS-M1", "SKU-MILK", f->wh, Q((INT64_MAX / 2) * 2, 1)), ZXV_OK);
    /* the lifetime receipts of milk are now at the edge of the exact range too */
    CHECK_RC(zvs_receive(f->s, "PO-M4", "SKU-MILK", f->wh, Q(5, 1)), ZXV_E_RANGE);
    CHECK_RC(zvs_item_add(f->s, "SKU-OIL", ZVS_UNIT_MEASURE, "l"), ZXV_OK);
    CHECK_RC(zvs_receive(f->s, "PO-M4", "SKU-OIL", f->wh, Q(1, 1000000007)), ZXV_OK);
    CHECK_RC(zvs_receive(f->s, "PO-M5", "SKU-OIL", f->wh, Q(1, 998244353)), ZXV_OK);
    CHECK_RC(zvs_receive(f->s, "PO-M6", "SKU-OIL", f->wh, Q(1, 1000000009)), ZXV_E_RANGE);
    CHECK_RC(zvs_receive(f->s, "PO-M6", "SKU-OIL", f->wh, Q(5, 1)), ZXV_OK); /* ref still free */
    check_flows(f, "SKU-OIL");
    CHECK_RC(zvs_on_hand(f->s, "SKU-NOPE", 0, &t), ZVS_E_SKU);
    CHECK_RC(zvs_on_hand(f->s, "SKU-MILK", 99, &t), ZVS_E_LOCATION);
    CHECK_RC(zvs_on_hand(f->s, "SKU-MILK", 0, NULL), ZXV_E_ARG);
}

static void test_counts(fx_t *f) {
    zind_qty var = {7, 1};
    uint8_t tag = 0, base = 0, sig[ZIND_SIG_BYTES], sig2[ZIND_SIG_BYTES];
    int32_t held = -1, act = -1;
    uint32_t q0 = 0, q1 = 0;

    /* a matching count confirms the book */
    CHECK_RC(zvs_count(f->s, "CC-1", "SKU-COLA-330", f->back, Q(22, 1), &var), ZXV_OK);
    CHECK(var.num == 0, "no variance");
    CHECK_RC(zvs_position_state(f->s, "SKU-COLA-330", f->back, &held, &tag), ZXV_OK);
    CHECK(!held && zxv_hex_unpack(tag, &base, NULL, NULL) == ZXV_OK && base == ZXV_HEX_BASE_TRUE,
          "confirmed (%s)", zxv_hex_name(tag));
    CHECK_RC(zvs_count(f->s, "CC-1", "SKU-COLA-330", f->back, Q(22, 1), &var), ZIND_E_REPLAY);
    CHECK_RC(zvs_count(f->s, "CC-2", "SKU-COLA-330", f->back, Q(-1, 1), &var), ZXV_E_ARG);
    CHECK_RC(zvs_count(f->s, "CC-2", "SKU-COLA-330", f->back, Q(1, 2), &var), ZVS_E_UNIT);
    CHECK_RC(zvs_count(f->s, "CC-2", "SKU-NOPE", f->back, Q(1, 1), &var), ZVS_E_SKU);
    CHECK_RC(zvs_count(f->s, "CC-2", "SKU-COLA-330", 99, Q(1, 1), &var), ZVS_E_LOCATION);
    /* a movement after the count makes it unconfirmed again */
    CHECK_RC(zvs_sell(f->s, "POS-3", "SKU-COLA-330", f->back, Q(2, 1)), ZXV_OK);
    CHECK_RC(zvs_position_state(f->s, "SKU-COLA-330", f->back, &held, &tag), ZXV_OK);
    CHECK(zxv_hex_unpack(tag, &base, NULL, NULL) == ZXV_OK && base == ZXV_HEX_BASE_NEITHER,
          "unconfirmed after a sale");

    /* a mismatch: logged, held as BOTH */
    CHECK_RC(zxv_hexlog_count(f->qlog, &q0), ZXV_OK);
    CHECK_RC(zvs_count(f->s, "CC-2", "SKU-COLA-330", f->back, Q(17, 1), &var), ZVS_E_VARIANCE);
    CHECK(qeq(var, Q(-3, 1)), "variance 17 - 20 = -3");
    CHECK_RC(zxv_hexlog_count(f->qlog, &q1), ZXV_OK);
    CHECK(q1 == q0 + 1, "variance quarantined");
    {
        uint8_t rt = 0, rb = 0, rp = 0, reason = 0;
        CHECK_RC(zxv_hexlog_at(f->qlog, q1 - 1, NULL, &rt, &reason, NULL, NULL, NULL, NULL),
                 ZXV_OK);
        CHECK(zxv_hex_unpack(rt, &rb, &rp, NULL) == ZXV_OK && rb == ZXV_HEX_BASE_BOTH &&
                  rp == ZXV_HEX_POL_FORBIDDEN && reason == ZIND_REASON_MISMATCH,
              "both/forbidden, reason MISMATCH");
    }
    CHECK_RC(zvs_position_state(f->s, "SKU-COLA-330", f->back, &held, &tag), ZXV_OK);
    CHECK(held == 1 && zxv_hex_action(tag, &act) == ZXV_OK && act == ZXV_HEX_ACT_ESCALATE,
          "held, escalated");
    CHECK(qeq(on_hand(f, "SKU-COLA-330", f->back), Q(20, 1)), "book unchanged while held");
    CHECK_RC(zvs_sell(f->s, "POS-4", "SKU-COLA-330", f->back, Q(1, 1)), ZIND_E_HELD);
    CHECK_RC(zvs_receive(f->s, "PO-9", "SKU-COLA-330", f->back, Q(1, 1)), ZIND_E_HELD);
    CHECK_RC(zvs_shrink(f->s, "SH-9", "SKU-COLA-330", f->back, Q(1, 1), ZVS_SHRINK_THEFT),
             ZIND_E_HELD);
    CHECK_RC(zvs_transfer(f->s, "TR-9", "SKU-COLA-330", f->back, f->floor, Q(1, 1)), ZIND_E_HELD);
    CHECK_RC(zvs_receive(f->s, "PO-10", "SKU-COLA-330", f->floor, Q(5, 1)), ZXV_OK);
    CHECK_RC(zvs_transfer(f->s, "TR-10", "SKU-COLA-330", f->floor, f->back, Q(1, 1)),
             ZIND_E_HELD); /* into a held position too */
    CHECK_RC(zvs_count(f->s, "CC-3", "SKU-COLA-330", f->back, Q(17, 1), &var), ZIND_E_HELD);

    /* decisions */
    CHECK_RC(zvs_variance_message(f->s, "SKU-COLA-330", f->floor, ZVS_ACCEPT_COUNT, sig, 100,
                                  &(size_t){0}),
             ZXV_E_STATE);
    CHECK(approve(f, f->mgr, "SKU-COLA-330", f->back, ZVS_ACCEPT_COUNT, sig) == 0, "mgr signs");
    CHECK(approve(f, f->rogue, "SKU-COLA-330", f->back, ZVS_ACCEPT_COUNT, sig2) == 0, "rogue");
    CHECK_RC(zvs_variance_resolve(f->s, "SKU-COLA-330", f->back, zind_signer_pk(f->rogue),
                                  ZVS_ACCEPT_COUNT, sig2),
             ZVS_E_APPROVER);
    CHECK_RC(zxv_hexlog_count(f->qlog, &q0), ZXV_OK);
    CHECK_RC(zvs_variance_resolve(f->s, "SKU-COLA-330", f->back, zind_signer_pk(f->mgr),
                                  ZVS_ACCEPT_COUNT, sig2),
             ZXV_E_VERIFY);
    CHECK_RC(zvs_variance_resolve(f->s, "SKU-COLA-330", f->back, zind_signer_pk(f->mgr),
                                  ZVS_KEEP_BOOK, sig),
             ZXV_E_VERIFY);
    CHECK_RC(zxv_hexlog_count(f->qlog, &q1), ZXV_OK);
    CHECK(q1 == q0 + 2, "forged decisions quarantined");
    CHECK_RC(zvs_variance_resolve(f->s, "SKU-COLA-330", f->back, zind_signer_pk(f->mgr), 3, sig),
             ZXV_E_ARG);
    {
        /* a store whose cap cannot attest cannot apply a decision */
        zxv_cap *noattest = NULL;
        zvs_store *s2 = NULL;
        uint32_t l = 0;
        uint8_t m[128], s3[ZIND_SIG_BYTES];
        size_t len = 0;
        CHECK_RC(zxv_cap_attenuate(f->e.cap, ZXV_CAP_CRYPTO_SIGN, &noattest), ZXV_OK);
        CHECK_RC(zvs_store_create(noattest, f->key, NULL, &s2), ZXV_OK);
        CHECK_RC(zvs_location_add(s2, "x", &l), ZXV_OK);
        CHECK_RC(zvs_item_add(s2, "I", ZVS_UNIT_EACH, "ea"), ZXV_OK);
        CHECK_RC(zvs_approver_add(s2, zind_signer_pk(f->mgr)), ZXV_OK);
        CHECK_RC(zvs_count(s2, "c", "I", l, Q(1, 1), NULL), ZVS_E_VARIANCE);
        CHECK_RC(zvs_variance_message(s2, "I", l, ZVS_KEEP_BOOK, m, sizeof m, &len), ZXV_OK);
        CHECK_RC(zind_sign(f->e.cap, f->mgr, m, len, ZVS_VARIANCE_CTX, s3), ZXV_OK);
        CHECK_RC(zvs_variance_resolve(s2, "I", l, zind_signer_pk(f->mgr), ZVS_KEEP_BOOK, s3),
                 ZXV_E_CAP);
        CHECK_RC(zvs_reconcile(s2, NULL), ZXV_OK);
        zvs_store_destroy(s2);
        zxv_cap_destroy(noattest);
    }
    CHECK_RC(zvs_variance_resolve(f->s, "SKU-COLA-330", f->back, zind_signer_pk(f->mgr),
                                  ZVS_ACCEPT_COUNT, sig),
             ZXV_OK);
    CHECK(qeq(on_hand(f, "SKU-COLA-330", f->back), Q(17, 1)), "book is now the count");
    CHECK_RC(zvs_position_state(f->s, "SKU-COLA-330", f->back, &held, &tag), ZXV_OK);
    CHECK(!held && zxv_hex_unpack(tag, &base, NULL, NULL) == ZXV_OK && base == ZXV_HEX_BASE_TRUE,
          "released and confirmed");
    CHECK_RC(zvs_variance_resolve(f->s, "SKU-COLA-330", f->back, zind_signer_pk(f->mgr),
                                  ZVS_ACCEPT_COUNT, sig),
             ZXV_E_STATE); /* replayed decision: nothing held */
    {
        zind_qty adj;
        CHECK_RC(zvs_flows(f->s, "SKU-COLA-330", NULL, NULL, NULL, &adj), ZXV_OK);
        CHECK(qeq(adj, Q(-3, 1)), "adjusted -3");
    }
    check_flows(f, "SKU-COLA-330");
    CHECK_RC(zvs_sell(f->s, "POS-4", "SKU-COLA-330", f->back, Q(1, 1)), ZXV_OK); /* free again */

    /* a fractional variance and a "keep the book" decision */
    CHECK_RC(zvs_receive(f->s, "PO-A9", "SKU-APPLES", f->back, P("2.5")), ZXV_OK);
    CHECK_RC(zvs_count(f->s, "CC-A1", "SKU-APPLES", f->back, P("2.375"), &var), ZVS_E_VARIANCE);
    CHECK(qeq(var, Q(-1, 8)), "variance -1/8 kg");
    CHECK(approve(f, f->mgr, "SKU-APPLES", f->back, ZVS_KEEP_BOOK, sig) == 0, "keep book");
    CHECK_RC(zvs_variance_resolve(f->s, "SKU-APPLES", f->back, zind_signer_pk(f->mgr),
                                  ZVS_KEEP_BOOK, sig),
             ZXV_OK);
    CHECK(qeq(on_hand(f, "SKU-APPLES", f->back), P("2.5")), "book kept");
    CHECK_RC(zvs_count(f->s, "CC-A2", "SKU-APPLES", f->back, P("2.5"), &var), ZXV_OK);
    check_flows(f, "SKU-APPLES");
    /* a positive variance accepted */
    CHECK_RC(zvs_count(f->s, "CC-A3", "SKU-APPLES", f->floor, P("0.55"), &var), ZVS_E_VARIANCE);
    CHECK(qeq(var, Q(1, 4)), "0.55 - 0.3 = 1/4");
    CHECK(approve(f, f->mgr, "SKU-APPLES", f->floor, ZVS_ACCEPT_COUNT, sig) == 0, "accept");
    CHECK_RC(zvs_variance_resolve(f->s, "SKU-APPLES", f->floor, zind_signer_pk(f->mgr),
                                  ZVS_ACCEPT_COUNT, sig),
             ZXV_OK);
    CHECK(qeq(on_hand(f, "SKU-APPLES", f->floor), P("0.55")), "0.55 kg");
    check_flows(f, "SKU-APPLES");
    CHECK_RC(zvs_reconcile(f->s, NULL), ZXV_OK);
}

/* Random operations against an independent model: every balance matches
 * exactly after every step, and the replay agrees at the end. */
static void test_model(fx_t *f) {
    static const char *skus[3] = {"M-EACH", "M-KG", "M-L"};
    zind_qty model[3][3];
    uint64_t s = 2024;
    int refn = 0;
    CHECK_RC(zvs_item_add(f->s, "M-EACH", ZVS_UNIT_EACH, "ea"), ZXV_OK);
    CHECK_RC(zvs_item_add(f->s, "M-KG", ZVS_UNIT_MEASURE, "kg"), ZXV_OK);
    CHECK_RC(zvs_item_add(f->s, "M-L", ZVS_UNIT_MEASURE, "l"), ZXV_OK);
    for (int i = 0; i < 3; i++)
        for (int l = 0; l < 3; l++) model[i][l] = ZIND_QTY_ZERO;
    for (int step = 0; step < 150; step++) {
        int i = (int) (zt_rand(&s) % 3), op = (int) (zt_rand(&s) % 4);
        uint32_t l = (uint32_t) (zt_rand(&s) % 3), l2 = (l + 1 + (uint32_t) (zt_rand(&s) % 2)) % 3;
        zind_qty q = i == 0 ? Q(1 + (int64_t) (zt_rand(&s) % 9), 1)
                            : Q(1 + (int64_t) (zt_rand(&s) % 50), 1 + (int64_t) (zt_rand(&s) % 12));
        char ref[32];
        int32_t st, want, cmp = 0;
        zind_qty after;
        snprintf(ref, sizeof ref, "MR-%d", refn++);
        if (op == 0) {
            st = zvs_receive(f->s, ref, skus[i], l, q);
            CHECK(st == ZXV_OK, "receive");
            zind_qty_add(model[i][l], q, &model[i][l]);
        } else {
            zind_qty_sub(model[i][l], q, &after);
            want = zind_qty_sign(after) < 0 ? ZIND_E_NEGATIVE : ZXV_OK;
            if (op == 1) st = zvs_sell(f->s, ref, skus[i], l, q);
            else if (op == 2) st = zvs_shrink(f->s, ref, skus[i], l, q, ZVS_SHRINK_EXPIRY);
            else st = zvs_transfer(f->s, ref, skus[i], l, l2, q);
            CHECK(st == want, "step %d op %d -> %s want %s", step, op, zvs_status_name(st),
                  zvs_status_name(want));
            if (st == ZXV_OK) {
                model[i][l] = after;
                if (op == 3) zind_qty_add(model[i][l2], q, &model[i][l2]);
            }
        }
        for (int k = 0; k < 3; k++) {
            zind_qty_cmp(on_hand(f, skus[i], (uint32_t) k), model[i][k], &cmp);
            CHECK(cmp == 0, "step %d: %s at %d matches the model", step, skus[i], k);
        }
    }
    for (int i = 0; i < 3; i++) check_flows(f, skus[i]);
    CHECK_RC(zvs_reconcile(f->s, NULL), ZXV_OK);
}

static void test_journal(fx_t *f) {
    const zind_chain *c = zvs_records(f->s);
    const uint8_t *trust[1] = {zind_signer_pk(f->key)};
    uint32_t n = 0, bad = 0;
    int seen[11] = {0};
    size_t len = 0;
    uint8_t *buf;
    zind_chain *imp = NULL;
    CHECK_RC(zind_chain_count(c, &n), ZXV_OK);
    for (uint32_t i = 0; i < n; i++) {
        zind_record r;
        zind_chain_at(c, i, &r);
        if (r.kind <= 10) seen[r.kind] = 1;
    }
    for (int k = ZVS_REC_LOCATION; k <= ZVS_REC_RECOUNT; k++) CHECK(seen[k], "record kind %d", k);
    CHECK_RC(zvs_reconcile(f->s, &bad), ZXV_OK);
    CHECK(bad == UINT32_MAX, "no bad record");
    zind_chain_export(c, NULL, 0, &len);
    buf = (uint8_t *) malloc(len);
    CHECK_RC(zind_chain_export(c, buf, len, &len), ZXV_OK);
    for (size_t off = 100; off < len; off += len / 19) {
        int32_t st;
        buf[off] ^= 0x80;
        imp = NULL;
        st = zind_chain_import("v-shelf", buf, len, &imp);
        CHECK(st == ZXV_E_FORMAT ||
                  (st == ZXV_OK && zind_chain_verify(f->e.cap, imp, trust, 1, NULL) != ZXV_OK),
              "tamper at %zu refused by import or verify", off);
        zind_chain_destroy(imp);
        buf[off] ^= 0x80;
    }
    free(buf);
    CHECK_RC(zvs_reconcile(NULL, NULL), ZXV_E_ARG);
    CHECK(strcmp(zvs_status_name(ZVS_E_VARIANCE), "ZVS_E_VARIANCE") == 0, "name");
    CHECK(strcmp(zvs_status_name(ZIND_E_NEGATIVE), "ZIND_E_NEGATIVE") == 0, "name 2");
    CHECK_RC(zxv_hexlog_verify(f->qlog), ZXV_OK);
}

int main(void) {
    fx_t f;
    if (fx_open(&f) != 0) {
        fprintf(stderr, "fixture failed: %s\n", zt_last_err());
        return 2;
    }
    test_setup(&f);
    test_movements(&f);
    test_counts(&f);
    test_model(&f);
    test_journal(&f);
    fx_close(&f);
    return zt_report("test_shelf");
}
