/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_trace.c — tri-trace: setup refusals, a full custody path, replays,
 * every kind of duplicate scan (cloned commission, wrong receiver, a tag
 * seen at two places, a non-holder shipping, scans after dispensing, time
 * running backwards), reviews (forged, cross-item, wrong outcome, without
 * ATTEST, genuine, counterfeit) and offline verification of exported chains
 * (tamper, truncation, wrong tag, missing trust, impersonation). */
#include "zind_test.h"
#include "zttrace.h"

#undef CHECK_RC
#define CHECK_RC(expr, want)                                                                   \
    do {                                                                                       \
        int32_t rc_ = (expr);                                                                  \
        CHECK(rc_ == (want), "%s -> %d (%s), want %d (%s)", #expr, (int) rc_,                  \
              ztt_status_name(rc_), (int) (want), ztt_status_name(want));                     \
    } while (0)

#define NP 5
static const char *k_ids[NP] = {"MAKER", "DIST", "PHARM", "PHARM2", "CLONER"};

typedef struct {
    zt_env e;
    zind_signer *op, *rev, *rogue, *party[NP];
    zxv_hexlog *qlog;
    ztt_registry *r;
    const uint8_t *pks[NP];
    const uint8_t *rev_pks[1];
    ztt_trust trust;
} fx_t;

static int fx_open(fx_t *f) {
    memset(f, 0, sizeof *f);
    if (zt_env_open(&f->e, NULL) != 0) return -1;
    f->op = zt_signer(f->e.cap, 1);
    f->rev = zt_signer(f->e.cap, 2);
    f->rogue = zt_signer(f->e.cap, 3);
    if (!f->op || !f->rev || !f->rogue) return -1;
    if (zxv_hexlog_create(f->e.cap, 1024, &f->qlog) != ZXV_OK) return -1;
    if (ztt_registry_create(f->e.cap, f->op, f->qlog, &f->r) != ZXV_OK) return -1;
    for (int i = 0; i < NP; i++) {
        f->party[i] = zt_signer(f->e.cap, (uint8_t) (10 + i));
        if (!f->party[i]) return -1;
        f->pks[i] = zind_signer_pk(f->party[i]);
        if (ztt_party_add(f->r, k_ids[i], f->pks[i]) != ZXV_OK) return -1;
    }
    if (ztt_reviewer_add(f->r, zind_signer_pk(f->rev)) != ZXV_OK) return -1;
    f->rev_pks[0] = zind_signer_pk(f->rev);
    f->trust.party_ids = k_ids;
    f->trust.party_pks = f->pks;
    f->trust.n_parties = NP;
    f->trust.reviewer_pks = f->rev_pks;
    f->trust.n_reviewers = 1;
    f->trust.operator_pk = zind_signer_pk(f->op);
    return 0;
}

static void fx_close(fx_t *f) {
    ztt_registry_destroy(f->r);
    zxv_hexlog_destroy(f->qlog);
    zind_signer_destroy(f->op);
    zind_signer_destroy(f->rev);
    zind_signer_destroy(f->rogue);
    for (int i = 0; i < NP; i++) zind_signer_destroy(f->party[i]);
    zt_env_close(&f->e);
}

enum { MAKER, DIST, PHARM, PHARM2, CLONER };

static int32_t scan(fx_t *f, const char *item, int who, int32_t kind, const char *to,
                    uint64_t t) {
    return ztt_scan(f->r, item, k_ids[who], f->party[who], kind, to, t);
}

static int32_t state_of(fx_t *f, const char *item, uint8_t *tag, char *holder) {
    int32_t st = -1;
    char h[32];
    (void) ztt_item_state(f->r, item, &st, tag, holder ? holder : h, 32);
    return st;
}

/* Export the item and verify it offline with the fixture's trust set. */
static int32_t verify(fx_t *f, const char *item, ztt_verdict *v) {
    size_t len = 0;
    uint8_t *buf;
    int32_t st;
    if (ztt_item_export(f->r, item, NULL, 0, &len) != ZXV_E_BUFFER) return -999;
    buf = (uint8_t *) malloc(len);
    st = ztt_item_export(f->r, item, buf, len, &len);
    if (st == ZXV_OK) st = ztt_verify_export(f->e.cap, item, buf, len, &f->trust, v);
    free(buf);
    return st;
}

static int review_sig(fx_t *f, const zind_signer *k, const char *item, int32_t outcome,
                      const char *holder, uint8_t sig[ZIND_SIG_BYTES]) {
    uint8_t m[192];
    size_t len = 0;
    if (ztt_review_message(f->r, item, outcome, holder, m, sizeof m, &len) != ZXV_OK) return -1;
    return zind_sign(f->e.cap, k, m, len, ZTT_REVIEW_CTX, sig) == ZXV_OK ? 0 : -1;
}

static void test_setup(fx_t *f) {
    ztt_registry *r2 = NULL;
    zxv_cap *weak = NULL;
    char id[24];
    CHECK_RC(zxv_cap_attenuate(f->e.cap, ZXV_CAP_ATTEST, &weak), ZXV_OK);
    CHECK_RC(ztt_registry_create(weak, f->op, NULL, &r2), ZXV_E_CAP);
    zxv_cap_destroy(weak);
    CHECK_RC(zxv_cap_attenuate(f->e.cap, ZXV_CAP_CRYPTO_SIGN, &weak), ZXV_OK);
    CHECK_RC(ztt_registry_create(weak, f->op, f->qlog, &r2), ZXV_E_CAP);
    zxv_cap_destroy(weak);
    CHECK_RC(ztt_registry_create(f->e.cap, NULL, NULL, &r2), ZXV_E_ARG);
    CHECK_RC(ztt_party_add(f->r, "MAKER", zind_signer_pk(f->rogue)), ZXV_E_DUPLICATE);
    CHECK_RC(ztt_party_add(f->r, "OTHER", f->pks[DIST]), ZXV_E_DUPLICATE); /* one key, one party */
    CHECK_RC(ztt_party_add(f->r, "", f->pks[DIST]), ZXV_E_ARG);
    CHECK_RC(ztt_party_add(f->r, "a party", f->pks[DIST]), ZXV_E_ARG);
    CHECK_RC(ztt_party_add(f->r, "ABCDEFGHIJKLMNOPQ", f->pks[DIST]), ZXV_E_ARG);
    CHECK_RC(ztt_party_add(f->r, "X", NULL), ZXV_E_ARG);
    CHECK_RC(ztt_reviewer_add(f->r, zind_signer_pk(f->rev)), ZXV_E_DUPLICATE);
    /* capacity: 64 parties (keys derived cheaply from fresh seeds) */
    {
        ztt_registry *r3 = NULL;
        CHECK_RC(ztt_registry_create(f->e.cap, f->op, NULL, &r3), ZXV_OK);
        for (int i = 0; i < 66; i++) {
            zind_signer *k = zt_signer(f->e.cap, (uint8_t) (100 + i));
            snprintf(id, sizeof id, "P%d", i);
            int32_t st = ztt_party_add(r3, id, zind_signer_pk(k));
            CHECK(st == (i < 64 ? ZXV_OK : ZXV_E_CAPACITY), "party %d -> %s", i,
                  ztt_status_name(st));
            zind_signer_destroy(k);
        }
        ztt_registry_destroy(r3);
    }
    /* scan refusals */
    CHECK_RC(scan(f, "", MAKER, ZTT_EV_COMMISSION, NULL, 1), ZXV_E_ARG);
    CHECK_RC(scan(f, "has space", MAKER, ZTT_EV_COMMISSION, NULL, 1), ZXV_E_ARG);
    CHECK_RC(scan(f, "T-0", MAKER, 0, NULL, 1), ZXV_E_ARG);
    CHECK_RC(scan(f, "T-0", MAKER, ZTT_EV_REVIEW, NULL, 1), ZXV_E_ARG);
    CHECK_RC(ztt_scan(f->r, "T-0", "NOBODY", f->party[MAKER], ZTT_EV_COMMISSION, NULL, 1),
             ZTT_E_PARTY);
    CHECK_RC(ztt_scan(f->r, "T-0", "MAKER", f->party[DIST], ZTT_EV_COMMISSION, NULL, 1),
             ZTT_E_PARTY); /* DIST's key cannot scan as MAKER */
    CHECK_RC(ztt_scan(f->r, "T-0", "MAKER", f->rogue, ZTT_EV_COMMISSION, NULL, 1), ZTT_E_PARTY);
    CHECK_RC(scan(f, "T-0", MAKER, ZTT_EV_SHIP, "DIST", 1), ZTT_E_ITEM); /* never commissioned */
    CHECK_RC(scan(f, "T-0", MAKER, ZTT_EV_RECEIVE, NULL, 1), ZTT_E_ITEM);
    CHECK_RC(scan(f, "T-0", MAKER, ZTT_EV_COMMISSION, "DIST", 1), ZXV_E_ARG);
    CHECK_RC(ztt_item_state(f->r, "T-0", NULL, NULL, NULL, 0), ZTT_E_ITEM);
    CHECK_RC(scan(f, "T-0", MAKER, ZTT_EV_COMMISSION, NULL, 1), ZXV_OK);
    CHECK_RC(scan(f, "T-0", MAKER, ZTT_EV_SHIP, "NOWHERE", 2), ZTT_E_PARTY);
    CHECK_RC(scan(f, "T-0", MAKER, ZTT_EV_SHIP, NULL, 2), ZTT_E_PARTY);
    CHECK_RC(scan(f, "T-0", MAKER, ZTT_EV_DISPENSE, "DIST", 2), ZXV_E_ARG);
    CHECK_RC(ztt_scan(NULL, "T-0", "MAKER", f->party[MAKER], 1, NULL, 1), ZXV_E_ARG);
}

static void test_path(fx_t *f) {
    const char *item = "urn:epc:id:sgtin:0614141.107346.2017";
    uint8_t tag = 0, base = 0, ph = 0;
    char holder[32];
    ztt_verdict v;
    uint32_t q0 = 0, q1 = 0;
    CHECK_RC(zxv_hexlog_count(f->qlog, &q0), ZXV_OK);
    CHECK_RC(scan(f, item, MAKER, ZTT_EV_COMMISSION, NULL, 1000), ZXV_OK);
    CHECK(state_of(f, item, &tag, holder) == ZTT_ST_HELD && strcmp(holder, "MAKER") == 0,
          "held at the maker");
    CHECK(zxv_hex_unpack(tag, &base, NULL, &ph) == ZXV_OK && base == ZXV_HEX_BASE_TRUE &&
              ph == ZXV_HEX_PHASE_POTENTIAL,
          "commissioned = true/potential (%s)", zxv_hex_name(tag));
    CHECK_RC(scan(f, item, MAKER, ZTT_EV_COMMISSION, NULL, 1000), ZIND_E_REPLAY);
    CHECK_RC(scan(f, item, MAKER, ZTT_EV_SHIP, "DIST", 1100), ZXV_OK);
    CHECK(state_of(f, item, &tag, holder) == ZTT_ST_TRANSIT && holder[0] == 0, "in transit");
    CHECK(zxv_hex_unpack(tag, NULL, NULL, &ph) == ZXV_OK && ph == ZXV_HEX_PHASE_EMERGING,
          "emerging");
    CHECK_RC(scan(f, item, MAKER, ZTT_EV_SHIP, "DIST", 1100), ZIND_E_REPLAY);
    CHECK_RC(scan(f, item, DIST, ZTT_EV_RECEIVE, NULL, 1200), ZXV_OK);
    CHECK(state_of(f, item, NULL, holder) == ZTT_ST_HELD && strcmp(holder, "DIST") == 0, "at DIST");
    CHECK_RC(scan(f, item, DIST, ZTT_EV_SHIP, "PHARM", 1300), ZXV_OK);
    CHECK_RC(scan(f, item, PHARM, ZTT_EV_RECEIVE, NULL, 1300), ZXV_OK); /* same second is fine */
    CHECK_RC(verify(f, item, &v), ZXV_OK);
    CHECK(v.lp == ZXV_LP_TRUE && v.state == ZTT_ST_HELD && strcmp(v.holder, "PHARM") == 0 &&
              v.events == 5 && v.conflicts == 0,
          "offline: TRUE, held at PHARM after 5 events");
    CHECK_RC(scan(f, item, PHARM, ZTT_EV_DISPENSE, NULL, 1400), ZXV_OK);
    CHECK(state_of(f, item, &tag, NULL) == ZTT_ST_DISPENSED, "dispensed");
    CHECK(zxv_hex_unpack(tag, NULL, NULL, &ph) == ZXV_OK && ph == ZXV_HEX_PHASE_MANIFEST,
          "dispensed = manifest");
    CHECK_RC(verify(f, item, &v), ZXV_OK);
    CHECK(v.lp == ZXV_LP_TRUE && v.state == ZTT_ST_DISPENSED && v.events == 6, "offline dispensed");
    CHECK_RC(zxv_hexlog_count(f->qlog, &q1), ZXV_OK);
    CHECK(q1 == q0, "a clean path quarantines nothing");
    /* the maker sells directly: commission then dispense (Potential -> Manifest) */
    CHECK_RC(scan(f, "T-DIRECT", MAKER, ZTT_EV_COMMISSION, NULL, 5), ZXV_OK);
    CHECK_RC(scan(f, "T-DIRECT", MAKER, ZTT_EV_DISPENSE, NULL, 6), ZXV_OK);
    CHECK(state_of(f, "T-DIRECT", &tag, NULL) == ZTT_ST_DISPENSED &&
              zxv_hex_unpack(tag, NULL, NULL, &ph) == ZXV_OK && ph == ZXV_HEX_PHASE_MANIFEST,
          "direct sale reaches manifest");
}

/* One scenario that ends in a conflict: `steps` legal scans, then one that
 * conflicts. */
typedef struct {
    const char *name;
    int n;
    struct {
        int who;
        int32_t kind;
        const char *to;
        uint64_t t;
    } s[6];
    int bad_who;
    int32_t bad_kind;
    const char *bad_to;
    uint64_t bad_t;
} scenario_t;

static const scenario_t k_conflicts[] = {
    {"cloned commission", 1, {{MAKER, ZTT_EV_COMMISSION, NULL, 10}}, CLONER, ZTT_EV_COMMISSION,
     NULL, 11},
    {"maker commissions twice", 1, {{MAKER, ZTT_EV_COMMISSION, NULL, 10}}, MAKER,
     ZTT_EV_COMMISSION, NULL, 12},
    {"received by the wrong party",
     2,
     {{MAKER, ZTT_EV_COMMISSION, NULL, 10}, {MAKER, ZTT_EV_SHIP, "DIST", 11}},
     PHARM2,
     ZTT_EV_RECEIVE,
     NULL,
     12},
    {"seen at two places",
     3,
     {{MAKER, ZTT_EV_COMMISSION, NULL, 10},
      {MAKER, ZTT_EV_SHIP, "DIST", 11},
      {DIST, ZTT_EV_RECEIVE, NULL, 12}},
     PHARM,
     ZTT_EV_RECEIVE,
     NULL,
     13},
    {"shipped by a non-holder",
     3,
     {{MAKER, ZTT_EV_COMMISSION, NULL, 10},
      {MAKER, ZTT_EV_SHIP, "DIST", 11},
      {DIST, ZTT_EV_RECEIVE, NULL, 12}},
     MAKER,
     ZTT_EV_SHIP,
     "PHARM",
     13},
    {"shipped while in transit",
     2,
     {{MAKER, ZTT_EV_COMMISSION, NULL, 10}, {MAKER, ZTT_EV_SHIP, "DIST", 11}},
     MAKER,
     ZTT_EV_SHIP,
     "PHARM",
     12},
    {"scanned after dispensing",
     2,
     {{MAKER, ZTT_EV_COMMISSION, NULL, 10}, {MAKER, ZTT_EV_DISPENSE, NULL, 11}},
     PHARM,
     ZTT_EV_RECEIVE,
     NULL,
     12},
    {"dispensed twice",
     2,
     {{MAKER, ZTT_EV_COMMISSION, NULL, 10}, {MAKER, ZTT_EV_DISPENSE, NULL, 11}},
     MAKER,
     ZTT_EV_DISPENSE,
     NULL,
     12},
    {"time runs backwards",
     2,
     {{MAKER, ZTT_EV_COMMISSION, NULL, 10}, {MAKER, ZTT_EV_SHIP, "DIST", 50}},
     DIST,
     ZTT_EV_RECEIVE,
     NULL,
     49},
    {"shipped to itself", 1, {{MAKER, ZTT_EV_COMMISSION, NULL, 10}}, MAKER, ZTT_EV_SHIP, "MAKER",
     11},
};

static void test_conflicts(fx_t *f) {
    for (size_t k = 0; k < sizeof k_conflicts / sizeof k_conflicts[0]; k++) {
        const scenario_t *sc = &k_conflicts[k];
        char item[24];
        uint8_t tag = 0, base = 0;
        uint32_t q0 = 0, q1 = 0, n0 = 0;
        int32_t act = -1;
        ztt_verdict v;
        snprintf(item, sizeof item, "C-%zu", k);
        for (int i = 0; i < sc->n; i++)
            CHECK_RC(scan(f, item, sc->s[i].who, sc->s[i].kind, sc->s[i].to, sc->s[i].t), ZXV_OK);
        CHECK_RC(zxv_hexlog_count(f->qlog, &q0), ZXV_OK);
        CHECK_RC(scan(f, item, sc->bad_who, sc->bad_kind, sc->bad_to, sc->bad_t), ZTT_E_CONFLICT);
        CHECK_RC(zxv_hexlog_count(f->qlog, &q1), ZXV_OK);
        CHECK(q1 == q0 + 1, "%s: quarantined", sc->name);
        {
            uint8_t rt = 0, rb = 0, reason = 0;
            zxv_hexlog_at(f->qlog, q1 - 1, NULL, &rt, &reason, NULL, NULL, NULL, NULL);
            CHECK(zxv_hex_unpack(rt, &rb, NULL, NULL) == ZXV_OK && rb == ZXV_HEX_BASE_BOTH &&
                      reason == ZIND_REASON_DUPLICATE,
                  "%s: logged as a BOTH duplicate", sc->name);
        }
        CHECK(state_of(f, item, &tag, NULL) == ZTT_ST_CONFLICT, "%s: frozen", sc->name);
        CHECK(zxv_hex_unpack(tag, &base, NULL, NULL) == ZXV_OK && base == ZXV_HEX_BASE_BOTH &&
                  zxv_hex_action(tag, &act) == ZXV_OK && act == ZXV_HEX_ACT_ESCALATE,
              "%s: BOTH, escalated", sc->name);
        /* frozen: nothing more is accepted or recorded */
        {
            size_t l0 = 0, l1 = 0;
            ztt_item_export(f->r, item, NULL, 0, &l0);
            CHECK_RC(scan(f, item, PHARM, ZTT_EV_DISPENSE, NULL, 100), ZIND_E_HELD);
            CHECK_RC(scan(f, item, MAKER, ZTT_EV_SHIP, "DIST", 101), ZIND_E_HELD);
            CHECK_RC(scan(f, item, MAKER, ZTT_EV_COMMISSION, NULL, 102), ZIND_E_HELD);
            ztt_item_export(f->r, item, NULL, 0, &l1);
            CHECK(l0 == l1, "%s: nothing recorded while frozen", sc->name);
        }
        /* the duplicate scan is evidence in the chain, verified offline */
        CHECK_RC(verify(f, item, &v), ZXV_OK);
        CHECK(v.lp == ZXV_LP_BOTH && v.state == ZTT_ST_CONFLICT && v.conflicts == 1 &&
                  v.events == (uint32_t) sc->n + 1,
              "%s: offline BOTH (%s, %u events)", sc->name, zxv_lp_name(v.lp), v.events);
        (void) n0;
    }
}

static void test_reviews(fx_t *f) {
    uint8_t sig[ZIND_SIG_BYTES], sig2[ZIND_SIG_BYTES], tag = 0, base = 0, pol = 0;
    char holder[32];
    ztt_verdict v;
    uint32_t q0 = 0, q1 = 0;
    /* C-3 ("seen at two places") is genuine: custody resumes at DIST */
    CHECK_RC(ztt_review_message(f->r, "T-DIRECT", ZTT_GENUINE, "DIST", sig, 192, &(size_t){0}),
             ZXV_E_STATE);
    CHECK_RC(ztt_review_message(f->r, "C-3", ZTT_GENUINE, "NOBODY", sig, 192, &(size_t){0}),
             ZTT_E_PARTY);
    CHECK_RC(ztt_review_message(f->r, "C-3", ZTT_COUNTERFEIT, "DIST", sig, 192, &(size_t){0}),
             ZXV_E_ARG);
    CHECK_RC(ztt_review_message(f->r, "C-3", 3, "DIST", sig, 192, &(size_t){0}), ZXV_E_ARG);
    CHECK_RC(ztt_review_message(f->r, "C-NONE", ZTT_GENUINE, "DIST", sig, 192, &(size_t){0}),
             ZTT_E_ITEM);
    CHECK(review_sig(f, f->rev, "C-3", ZTT_GENUINE, "DIST", sig) == 0, "reviewer signs");
    CHECK(review_sig(f, f->rogue, "C-3", ZTT_GENUINE, "DIST", sig2) == 0, "rogue signs");
    CHECK_RC(ztt_review_resolve(f->r, "C-3", zind_signer_pk(f->rogue), ZTT_GENUINE, "DIST", sig2),
             ZTT_E_REVIEWER);
    CHECK_RC(zxv_hexlog_count(f->qlog, &q0), ZXV_OK);
    CHECK_RC(ztt_review_resolve(f->r, "C-3", zind_signer_pk(f->rev), ZTT_GENUINE, "DIST", sig2),
             ZXV_E_VERIFY);
    CHECK_RC(ztt_review_resolve(f->r, "C-3", zind_signer_pk(f->rev), ZTT_GENUINE, "PHARM", sig),
             ZXV_E_VERIFY); /* signed for DIST, presented for PHARM */
    CHECK_RC(ztt_review_resolve(f->r, "C-4", zind_signer_pk(f->rev), ZTT_GENUINE, "DIST", sig),
             ZXV_E_VERIFY); /* signed for another item */
    CHECK_RC(zxv_hexlog_count(f->qlog, &q1), ZXV_OK);
    CHECK(q1 == q0 + 3, "forged reviews quarantined");
    {
        zxv_cap *noattest = NULL;
        ztt_registry *r2 = NULL;
        uint8_t m[192], s3[ZIND_SIG_BYTES];
        size_t len = 0;
        CHECK_RC(zxv_cap_attenuate(f->e.cap, ZXV_CAP_CRYPTO_SIGN, &noattest), ZXV_OK);
        CHECK_RC(ztt_registry_create(noattest, f->op, NULL, &r2), ZXV_OK);
        CHECK_RC(ztt_party_add(r2, "MAKER", f->pks[MAKER]), ZXV_OK);
        CHECK_RC(ztt_reviewer_add(r2, zind_signer_pk(f->rev)), ZXV_OK);
        CHECK_RC(ztt_scan(r2, "X", "MAKER", f->party[MAKER], ZTT_EV_COMMISSION, NULL, 1), ZXV_OK);
        CHECK_RC(ztt_scan(r2, "X", "MAKER", f->party[MAKER], ZTT_EV_COMMISSION, NULL, 2),
                 ZTT_E_CONFLICT); /* frozen even without a quarantine log */
        CHECK_RC(ztt_review_message(r2, "X", ZTT_COUNTERFEIT, "", m, sizeof m, &len), ZXV_OK);
        CHECK_RC(zind_sign(f->e.cap, f->rev, m, len, ZTT_REVIEW_CTX, s3), ZXV_OK);
        CHECK_RC(ztt_review_resolve(r2, "X", zind_signer_pk(f->rev), ZTT_COUNTERFEIT, "", s3),
                 ZXV_E_CAP);
        ztt_registry_destroy(r2);
        zxv_cap_destroy(noattest);
    }
    CHECK_RC(ztt_review_resolve(f->r, "C-3", zind_signer_pk(f->rev), ZTT_GENUINE, "DIST", sig),
             ZXV_OK);
    CHECK(state_of(f, "C-3", &tag, holder) == ZTT_ST_HELD && strcmp(holder, "DIST") == 0,
          "custody resumes at DIST");
    CHECK(zxv_hex_unpack(tag, &base, NULL, NULL) == ZXV_OK && base == ZXV_HEX_BASE_TRUE, "TRUE");
    CHECK_RC(ztt_review_resolve(f->r, "C-3", zind_signer_pk(f->rev), ZTT_GENUINE, "DIST", sig),
             ZXV_E_STATE);
    CHECK_RC(scan(f, "C-3", DIST, ZTT_EV_SHIP, "PHARM", 20), ZXV_OK);
    CHECK_RC(scan(f, "C-3", PHARM, ZTT_EV_RECEIVE, NULL, 21), ZXV_OK);
    CHECK_RC(verify(f, "C-3", &v), ZXV_OK);
    CHECK(v.lp == ZXV_LP_TRUE && v.conflicts == 1 && strcmp(v.holder, "PHARM") == 0,
          "offline: reviewed and consistent, one conflict in its history");

    /* C-0 (cloned commission) is counterfeit: Forbidden for good */
    CHECK(review_sig(f, f->rev, "C-0", ZTT_COUNTERFEIT, "", sig) == 0, "counterfeit decision");
    CHECK_RC(ztt_review_resolve(f->r, "C-0", zind_signer_pk(f->rev), ZTT_COUNTERFEIT, "", sig),
             ZXV_OK);
    CHECK(state_of(f, "C-0", &tag, NULL) == ZTT_ST_COUNTERFEIT, "counterfeit");
    CHECK(zxv_hex_unpack(tag, &base, &pol, NULL) == ZXV_OK && pol == ZXV_HEX_POL_FORBIDDEN,
          "forbidden polarity (%s)", zxv_hex_name(tag));
    {
        int32_t act = -1;
        CHECK(zxv_hex_action(tag, &act) == ZXV_OK && act == ZXV_HEX_ACT_ROLLBACK, "rollback");
    }
    CHECK_RC(scan(f, "C-0", MAKER, ZTT_EV_SHIP, "DIST", 30), ZIND_E_FROZEN);
    CHECK_RC(scan(f, "C-0", CLONER, ZTT_EV_DISPENSE, NULL, 30), ZIND_E_FROZEN);
    CHECK_RC(ztt_review_message(f->r, "C-0", ZTT_GENUINE, "MAKER", sig, 192, &(size_t){0}),
             ZXV_E_STATE);
    CHECK_RC(verify(f, "C-0", &v), ZXV_OK);
    CHECK(v.lp == ZXV_LP_FALSE && v.state == ZTT_ST_COUNTERFEIT, "offline: FALSE");
}

static void test_offline(fx_t *f) {
    const char *item = "C-3";
    size_t len = 0;
    uint8_t *buf, *t;
    ztt_verdict v;
    CHECK_RC(ztt_item_export(f->r, item, NULL, 0, &len), ZXV_E_BUFFER);
    buf = (uint8_t *) malloc(len);
    t = (uint8_t *) malloc(len);
    CHECK_RC(ztt_item_export(f->r, item, buf, len, &len), ZXV_OK);
    CHECK_RC(ztt_verify_export(f->e.cap, item, buf, len, &f->trust, &v), ZXV_OK);
    CHECK_RC(ztt_verify_export(f->e.cap, "C-4", buf, len, &f->trust, &v), ZTT_E_ITEM);
    CHECK(v.bad_index == 0, "wrong tag at record 0");
    /* tamper across the export */
    {
        int caught = 0, tried = 0;
        for (size_t off = 0; off < len; off += len / 61 + 1) {
            int32_t st;
            memcpy(t, buf, len);
            t[off] ^= 0x10;
            st = ztt_verify_export(f->e.cap, item, t, len, &f->trust, &v);
            tried++;
            caught += st == ZXV_E_FORMAT || st == ZIND_E_CHAIN || st == ZIND_E_UNTRUSTED ||
                      st == ZTT_E_ITEM;
            CHECK(st != ZXV_OK, "tamper at %zu detected (%s)", off, ztt_status_name(st));
        }
        CHECK(caught == tried, "every tamper refused with a reason");
    }
    for (size_t cut = 0; cut < len; cut += len / 13 + 1)
        CHECK_RC(ztt_verify_export(f->e.cap, item, buf, cut, &f->trust, &v), ZXV_E_FORMAT);
    /* missing trust */
    {
        ztt_trust tr = f->trust;
        const char *ids_swapped[NP] = {"MAKER", "PHARM", "DIST", "PHARM2", "CLONER"};
        tr.n_parties = 2; /* PHARM unknown */
        CHECK_RC(ztt_verify_export(f->e.cap, item, buf, len, &tr, &v), ZIND_E_UNTRUSTED);
        tr = f->trust;
        tr.n_reviewers = 0;
        CHECK_RC(ztt_verify_export(f->e.cap, item, buf, len, &tr, &v), ZIND_E_UNTRUSTED);
        tr = f->trust;
        tr.operator_pk = zind_signer_pk(f->rogue);
        CHECK_RC(ztt_verify_export(f->e.cap, item, buf, len, &tr, &v), ZIND_E_UNTRUSTED);
        tr = f->trust;
        tr.party_ids = ids_swapped; /* DIST's records would be "signed by PHARM" */
        CHECK_RC(ztt_verify_export(f->e.cap, item, buf, len, &tr, &v), ZIND_E_UNTRUSTED);
        tr = f->trust;
        tr.reviewer_pks = (const uint8_t *const[]){zind_signer_pk(f->rogue)};
        CHECK_RC(ztt_verify_export(f->e.cap, item, buf, len, &tr, &v), ZIND_E_UNTRUSTED);
        CHECK_RC(ztt_verify_export(f->e.cap, item, buf, len, NULL, &v), ZXV_E_ARG);
    }
    free(buf);
    free(t);

    /* impersonation built by hand: DIST's key signs a record naming MAKER */
    {
        zind_chain *c = NULL;
        uint8_t p[128], *x;
        zind_wr w;
        size_t l = 0;
        CHECK_RC(zind_chain_create("tri-trace", 4, &c), ZXV_OK);
        zind_wr_init(&w, p, sizeof p);
        zind_wr_u8(&w, 1);
        zind_wr_u8(&w, ZTT_EV_COMMISSION);
        zind_wr_str(&w, "FAKE-1");
        zind_wr_str(&w, "MAKER");
        zind_wr_str(&w, "");
        zind_wr_u64(&w, 1);
        CHECK_RC(zind_chain_append(f->e.cap, c, f->party[DIST], ZTT_EV_COMMISSION, p, w.len, NULL,
                                   NULL),
                 ZXV_OK);
        zind_chain_export(c, NULL, 0, &l);
        x = (uint8_t *) malloc(l);
        CHECK_RC(zind_chain_export(c, x, l, &l), ZXV_OK);
        CHECK_RC(ztt_verify_export(f->e.cap, "FAKE-1", x, l, &f->trust, &v), ZIND_E_UNTRUSTED);
        CHECK(v.bad_index == 0, "impersonation at record 0");
        free(x);
        zind_chain_destroy(c);
        /* signed by the right party but with a kind field that disagrees */
        CHECK_RC(zind_chain_create("tri-trace", 4, &c), ZXV_OK);
        zind_wr_init(&w, p, sizeof p);
        zind_wr_u8(&w, 1);
        zind_wr_u8(&w, ZTT_EV_SHIP);
        zind_wr_str(&w, "FAKE-2");
        zind_wr_str(&w, "MAKER");
        zind_wr_str(&w, "");
        zind_wr_u64(&w, 1);
        CHECK_RC(zind_chain_append(f->e.cap, c, f->party[MAKER], ZTT_EV_COMMISSION, p, w.len,
                                   NULL, NULL),
                 ZXV_OK);
        zind_chain_export(c, NULL, 0, &l);
        x = (uint8_t *) malloc(l);
        CHECK_RC(zind_chain_export(c, x, l, &l), ZXV_OK);
        CHECK_RC(ztt_verify_export(f->e.cap, "FAKE-2", x, l, &f->trust, &v), ZXV_E_FORMAT);
        free(x);
        zind_chain_destroy(c);
    }
}

static void test_bulk(fx_t *f) {
    uint64_t s = 77;
    for (int i = 0; i < 30; i++) {
        char item[24];
        int holder = MAKER;
        uint64_t t = 1000;
        int hops = (int) (zt_rand(&s) % 4);
        ztt_verdict v;
        snprintf(item, sizeof item, "BULK-%03d", i);
        CHECK_RC(scan(f, item, MAKER, ZTT_EV_COMMISSION, NULL, t), ZXV_OK);
        for (int h = 0; h < hops; h++) {
            int to = 1 + (int) (zt_rand(&s) % 3);
            if (to == holder) to = (to % 3) + 1;
            t += 1 + zt_rand(&s) % 100;
            CHECK_RC(scan(f, item, holder, ZTT_EV_SHIP, k_ids[to], t), ZXV_OK);
            t += zt_rand(&s) % 100;
            CHECK_RC(scan(f, item, to, ZTT_EV_RECEIVE, NULL, t), ZXV_OK);
            holder = to;
        }
        if (zt_rand(&s) % 2) CHECK_RC(scan(f, item, holder, ZTT_EV_DISPENSE, NULL, t + 1), ZXV_OK);
        CHECK_RC(verify(f, item, &v), ZXV_OK);
        CHECK(v.lp == ZXV_LP_TRUE && v.conflicts == 0, "%s consistent offline", item);
        CHECK(strcmp(v.holder, k_ids[holder]) == 0, "%s holder %s", item, v.holder);
    }
    CHECK_RC(zxv_hexlog_verify(f->qlog), ZXV_OK);
    CHECK(strcmp(ztt_status_name(ZTT_E_CONFLICT), "ZTT_E_CONFLICT") == 0, "name");
    CHECK(strcmp(ztt_status_name(ZIND_E_FROZEN), "ZIND_E_FROZEN") == 0, "name 2");
}

int main(void) {
    fx_t f;
    if (fx_open(&f) != 0) {
        fprintf(stderr, "fixture failed: %s\n", zt_last_err());
        return 2;
    }
    test_setup(&f);
    test_path(&f);
    test_conflicts(&f);
    test_reviews(&f);
    test_offline(&f);
    test_bulk(&f);
    fx_close(&f);
    return zt_report("test_trace");
}
