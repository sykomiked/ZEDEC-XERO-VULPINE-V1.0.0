/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_triage.c — vinea-triage: record validation (categories, every vital's
 * unit and range at its bounds, UTF-8 notes), setup refusals, the wire
 * format (a builder written here from the documented layout must produce
 * the outbox's bytes exactly), delivery, replays, gaps and draining,
 * duplicate records, forks (a cloned device), malformed and encrypted
 * bundles, flipped bits, conflicting records held as BOTH and never
 * merged, reviewers' decisions (forged, stale, wrong category, without
 * ATTEST, genuine), late records that were already superseded, signed
 * acknowledgements, capacities, the journal and the quarantine log. */
#include "zind_test.h"
#include "zvtriage.h"

#undef CHECK_RC
#define CHECK_RC(expr, want)                                                                   \
    do {                                                                                       \
        int32_t rc_ = (expr);                                                                  \
        CHECK(rc_ == (want), "%s -> %d (%s), want %d (%s)", #expr, (int) rc_,                  \
              zvt_status_name(rc_), (int) (want), zvt_status_name(want));                     \
    } while (0)

enum { R1, R2, R3, NR };
static const char *k_resp[NR] = {"MEDIC-1", "MEDIC-2", "ENGINE-7"};

typedef struct {
    zt_env e;
    zind_signer *station, *rev, *rogue, *key[NR];
    zxv_hexlog *qlog;
    zvt_station *s;
    zvt_outbox *ob[NR];
} fx_t;

static int fx_open(fx_t *f) {
    memset(f, 0, sizeof *f);
    if (zt_env_open(&f->e, NULL) != 0) return -1;
    f->station = zt_signer(f->e.cap, 1);
    f->rev = zt_signer(f->e.cap, 2);
    f->rogue = zt_signer(f->e.cap, 3);
    if (!f->station || !f->rev || !f->rogue) return -1;
    if (zxv_hexlog_create(f->e.cap, 4096, &f->qlog) != ZXV_OK) return -1;
    if (zvt_station_create(f->e.cap, f->station, f->qlog, &f->s) != ZXV_OK) return -1;
    for (int i = 0; i < NR; i++) {
        f->key[i] = zt_signer(f->e.cap, (uint8_t) (10 + i));
        if (!f->key[i]) return -1;
        if (zvt_responder_add(f->s, k_resp[i], zind_signer_pk(f->key[i])) != ZXV_OK) return -1;
        if (zvt_outbox_create(f->e.cap, k_resp[i], f->key[i], &f->ob[i]) != ZXV_OK) return -1;
    }
    if (zvt_reviewer_add(f->s, "DR-LEE", zind_signer_pk(f->rev)) != ZXV_OK) return -1;
    return 0;
}

static void fx_close(fx_t *f) {
    for (int i = 0; i < NR; i++) {
        zvt_outbox_destroy(f->ob[i]);
        zind_signer_destroy(f->key[i]);
    }
    zvt_station_destroy(f->s);
    zxv_hexlog_destroy(f->qlog);
    zind_signer_destroy(f->station);
    zind_signer_destroy(f->rev);
    zind_signer_destroy(f->rogue);
    zt_env_close(&f->e);
}

static zvt_record mk(const char *patient, uint8_t cat, uint64_t t) {
    zvt_record r;
    memset(&r, 0, sizeof r);
    snprintf(r.patient, sizeof r.patient, "%s", patient);
    r.system = ZVT_SYS_START;
    r.category = cat;
    r.observed = t;
    r.n_vitals = 2;
    r.vitals[0] = (zvt_vital){ZVT_V_RESP, ZVT_U_PER_MIN, 24};
    r.vitals[1] = (zvt_vital){ZVT_V_PULSE, ZVT_U_PER_MIN, 110};
    snprintf(r.note, sizeof r.note, "tag %s", patient);
    return r;
}

/* Stage one record and seal it into its own bundle. */
static int32_t one(fx_t *f, int who, const zvt_record *r, uint8_t hash[32]) {
    int32_t st = zvt_outbox_record(f->ob[who], r, hash);
    if (st != ZXV_OK) return st;
    return zvt_outbox_seal(f->ob[who], r->observed, NULL);
}

/* The newest pending bundle of an outbox, as bytes. */
static uint8_t *last_bundle(fx_t *f, int who, size_t *len) {
    uint32_t n = 0;
    uint8_t *b;
    zvt_outbox_pending(f->ob[who], &n);
    if (n == 0 || zvt_outbox_bundle(f->ob[who], n - 1, NULL, 0, len) != ZXV_E_BUFFER) return NULL;
    b = (uint8_t *) malloc(*len);
    if (zvt_outbox_bundle(f->ob[who], n - 1, b, *len, len) != ZXV_OK) {
        free(b);
        return NULL;
    }
    return b;
}

static int32_t deliver_last(fx_t *f, int who, zvt_receipt *rc) {
    size_t len = 0;
    uint8_t *b = last_bundle(f, who, &len);
    int32_t st;
    if (!b) return -999;
    st = zvt_station_receive(f->s, b, len, rc);
    free(b);
    return st;
}

static zvt_patient_view view(fx_t *f, const char *p) {
    zvt_patient_view v;
    memset(&v, 0, sizeof v);
    (void) zvt_patient_state(f->s, p, &v);
    return v;
}

static uint32_t qcount(fx_t *f) {
    uint32_t n = 0;
    zxv_hexlog_count(f->qlog, &n);
    return n;
}

/* ---- a builder written from the documented wire format ---- */

static size_t tb_record(uint8_t *buf, size_t cap, const char *responder, uint64_t no,
                        const zvt_record *r) {
    zind_wr w;
    zind_wr_init(&w, buf, cap);
    zind_wr_u8(&w, 1);
    zind_wr_str(&w, responder);
    zind_wr_u64(&w, no);
    zind_wr_str(&w, r->patient);
    zind_wr_u8(&w, r->system);
    zind_wr_u8(&w, r->category);
    zind_wr_u64(&w, r->observed);
    zind_wr_bytes(&w, r->based_on, 32);
    zind_wr_u8(&w, r->n_vitals);
    for (int i = 0; i < r->n_vitals; i++) {
        zind_wr_u8(&w, r->vitals[i].kind);
        zind_wr_u8(&w, r->vitals[i].unit);
        zind_wr_u32(&w, (uint32_t) r->vitals[i].value);
    }
    zind_wr_str(&w, r->note);
    return w.overflow ? 0 : w.len;
}

/* bundle of n raw records; returns malloc'd bytes */
static uint8_t *tb_bundle(fx_t *f, const zind_signer *k, uint8_t ver, uint8_t enc,
                          const char *responder, uint64_t seq, const uint8_t prev[32],
                          uint64_t created, int n, uint8_t (*recs)[400], const size_t *lens,
                          size_t *out_len) {
    uint8_t *b = (uint8_t *) malloc(20000 + ZIND_SIG_BYTES), h[32];
    zind_wr w;
    zind_wr_init(&w, b, 20000);
    zind_wr_bytes(&w, "ZVTB", 4);
    zind_wr_u8(&w, ver);
    zind_wr_u8(&w, enc);
    zind_wr_str(&w, responder);
    zind_wr_u64(&w, seq);
    zind_wr_bytes(&w, prev, 32);
    zind_wr_u64(&w, created);
    zind_wr_u16(&w, (uint16_t) n);
    for (int i = 0; i < n; i++) {
        zind_wr_u16(&w, (uint16_t) lens[i]);
        zind_wr_bytes(&w, recs[i], lens[i]);
    }
    zind_subject("vinea-triage/bundle", b, w.len, h);
    zind_sign(f->e.cap, k, h, 32, ZVT_BUNDLE_CTX, b + w.len);
    *out_len = w.len + ZIND_SIG_BYTES;
    return b;
}

/* ===== records ===== */

static void test_records(void) {
    zvt_record r = mk("P-1", ZVT_CAT_IMMEDIATE, 1), b;
    static const struct {
        uint8_t kind, unit;
        int32_t lo, hi;
    } k_v[] = {{ZVT_V_RESP, ZVT_U_PER_MIN, 0, 150},   {ZVT_V_PULSE, ZVT_U_PER_MIN, 0, 350},
               {ZVT_V_SBP, ZVT_U_MMHG, 0, 400},       {ZVT_V_SPO2, ZVT_U_PERCENT, 0, 100},
               {ZVT_V_CAPREFILL, ZVT_U_MS, 0, 20000}, {ZVT_V_GCS, ZVT_U_SCORE, 3, 15},
               {ZVT_V_TEMP, ZVT_U_DECI_C, 150, 460}};
    CHECK_RC(zvt_record_check(&r), ZXV_OK);
    CHECK_RC(zvt_record_check(NULL), ZXV_E_ARG);
    b = r;
    b.patient[0] = 0;
    CHECK_RC(zvt_record_check(&b), ZVT_E_RECORD);
    b = r;
    memcpy(b.patient, "P 1", 4);
    CHECK_RC(zvt_record_check(&b), ZVT_E_RECORD);
    b = r;
    memset(b.patient, 'p', sizeof b.patient);
    CHECK_RC(zvt_record_check(&b), ZVT_E_RECORD); /* not terminated */
    b.patient[32] = 0;
    CHECK_RC(zvt_record_check(&b), ZXV_OK); /* 32 is the most */
    for (int sys = 0; sys < 5; sys++)
        for (int cat = 0; cat < 8; cat++) {
            int ok = (sys == ZVT_SYS_START || sys == ZVT_SYS_SALT) && cat >= 1 && cat <= 5 &&
                     !(cat == ZVT_CAT_EXPECTANT && sys != ZVT_SYS_SALT);
            b = r;
            b.system = (uint8_t) sys;
            b.category = (uint8_t) cat;
            CHECK_RC(zvt_record_check(&b), ok ? ZXV_OK : ZVT_E_RECORD);
        }
    for (size_t k = 0; k < sizeof k_v / sizeof k_v[0]; k++) {
        b = r;
        b.n_vitals = 1;
        b.vitals[0] = (zvt_vital){k_v[k].kind, k_v[k].unit, k_v[k].lo};
        CHECK_RC(zvt_record_check(&b), ZXV_OK);
        b.vitals[0].value = k_v[k].hi;
        CHECK_RC(zvt_record_check(&b), ZXV_OK);
        b.vitals[0].value = k_v[k].lo - 1;
        CHECK_RC(zvt_record_check(&b), ZVT_E_RECORD);
        b.vitals[0].value = k_v[k].hi + 1;
        CHECK_RC(zvt_record_check(&b), ZVT_E_RECORD);
        b.vitals[0].value = INT32_MIN;
        CHECK_RC(zvt_record_check(&b), ZVT_E_RECORD);
        b.vitals[0].value = k_v[k].lo;
        for (uint8_t u = 0; u < 8; u++) {
            b.vitals[0].unit = u;
            CHECK_RC(zvt_record_check(&b), u == k_v[k].unit ? ZXV_OK : ZVT_E_RECORD);
        }
    }
    b = r;
    b.vitals[1].kind = ZVT_V_RESP; /* twice */
    CHECK_RC(zvt_record_check(&b), ZVT_E_RECORD);
    b = r;
    b.vitals[0].kind = 0;
    CHECK_RC(zvt_record_check(&b), ZVT_E_RECORD);
    b.vitals[0].kind = 8;
    CHECK_RC(zvt_record_check(&b), ZVT_E_RECORD);
    b = r;
    b.n_vitals = 9;
    CHECK_RC(zvt_record_check(&b), ZVT_E_RECORD);
    b = r;
    b.n_vitals = 7;
    for (int i = 0; i < 7; i++) b.vitals[i] = (zvt_vital){k_v[i].kind, k_v[i].unit, k_v[i].hi};
    CHECK_RC(zvt_record_check(&b), ZXV_OK);
    b.n_vitals = 0;
    CHECK_RC(zvt_record_check(&b), ZXV_OK);
    {
        static const struct {
            const char *note;
            int ok;
        } k_n[] = {
            {"", 1},
            {"conscious, bleeding left leg", 1},
            {"caf\xc3\xa9 \xe2\x80\x93 \xe4\xb8\xad \xf0\x9f\x99\x82", 1}, /* é – 中 🙂 */
            {"tab\there", 0},
            {"bell\x07", 0},
            {"del\x7f", 0},
            {"overlong \xc0\x80", 0},
            {"overlong \xe0\x80\xaf", 0},
            {"surrogate \xed\xa0\x80", 0},
            {"lone \x80", 0},
            {"cut \xe2\x82", 0},
            {"C1 \xc2\x85", 0},
            {"too big \xf4\x90\x80\x80", 0},
            {"bad lead \xf8\x88\x80\x80\x80", 0},
            {"newline\n", 0},
        };
        for (size_t k = 0; k < sizeof k_n / sizeof k_n[0]; k++) {
            b = r;
            snprintf(b.note, sizeof b.note, "%s", k_n[k].note);
            CHECK(zvt_record_check(&b) == (k_n[k].ok ? ZXV_OK : ZVT_E_RECORD), "note %zu (%s)", k,
                  k_n[k].ok ? "accepted" : "refused");
        }
        b = r;
        memset(b.note, 'n', 200);
        b.note[200] = 0;
        CHECK_RC(zvt_record_check(&b), ZXV_OK);
        memset(b.note, 'n', sizeof b.note);
        CHECK_RC(zvt_record_check(&b), ZVT_E_RECORD);
    }
}

/* ===== setup ===== */

static void test_setup(fx_t *f) {
    zvt_station *s2 = NULL;
    zvt_outbox *o2 = NULL;
    zxv_cap *weak = NULL;
    uint8_t fake[ZIND_PK_BYTES];
    char id[40];
    CHECK_RC(zxv_cap_attenuate(f->e.cap, ZXV_CAP_ATTEST, &weak), ZXV_OK);
    CHECK_RC(zvt_station_create(weak, f->station, NULL, &s2), ZXV_E_CAP);
    CHECK_RC(zvt_outbox_create(weak, "X", f->key[R1], &o2), ZXV_E_CAP);
    zxv_cap_destroy(weak);
    CHECK_RC(zxv_cap_attenuate(f->e.cap, ZXV_CAP_CRYPTO_SIGN, &weak), ZXV_OK);
    CHECK_RC(zvt_station_create(weak, f->station, f->qlog, &s2), ZXV_E_CAP); /* qlog needs ATTEST */
    zxv_cap_destroy(weak);
    CHECK_RC(zvt_station_create(NULL, f->station, NULL, &s2), ZXV_E_ARG);
    CHECK_RC(zvt_station_create(f->e.cap, NULL, NULL, &s2), ZXV_E_ARG);
    CHECK_RC(zvt_outbox_create(f->e.cap, "", f->key[R1], &o2), ZXV_E_ARG);
    CHECK_RC(zvt_outbox_create(f->e.cap, "has space", f->key[R1], &o2), ZXV_E_ARG);
    CHECK_RC(zvt_outbox_create(f->e.cap, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456", f->key[R1], &o2),
             ZXV_E_ARG);
    CHECK_RC(zvt_outbox_create(f->e.cap, "X", NULL, &o2), ZXV_E_ARG);
    CHECK_RC(zvt_responder_add(f->s, k_resp[R1], zind_signer_pk(f->rogue)), ZXV_E_DUPLICATE);
    CHECK_RC(zvt_responder_add(f->s, "OTHER", zind_signer_pk(f->key[R2])), ZXV_E_DUPLICATE);
    CHECK_RC(zvt_responder_add(f->s, "DR-LEE", zind_signer_pk(f->rogue)), ZXV_E_DUPLICATE);
    CHECK_RC(zvt_responder_add(f->s, "OTHER", zind_signer_pk(f->rev)), ZXV_E_DUPLICATE);
    CHECK_RC(zvt_responder_add(f->s, "", zind_signer_pk(f->rogue)), ZXV_E_ARG);
    CHECK_RC(zvt_responder_add(f->s, "X", NULL), ZXV_E_ARG);
    CHECK_RC(zvt_reviewer_add(f->s, "DR-LEE", zind_signer_pk(f->rogue)), ZXV_E_DUPLICATE);
    CHECK_RC(zvt_reviewer_add(f->s, k_resp[R2], zind_signer_pk(f->rogue)), ZXV_E_DUPLICATE);
    CHECK_RC(zvt_reviewer_add(f->s, "DR-X", zind_signer_pk(f->key[R1])), ZXV_E_DUPLICATE);
    CHECK_RC(zvt_reviewer_add(f->s, "a b", zind_signer_pk(f->rogue)), ZXV_E_ARG);
    /* capacity: 256 responders (registration stores keys; it does not check them) */
    CHECK_RC(zvt_station_create(f->e.cap, f->station, NULL, &s2), ZXV_OK);
    for (int i = 0; i < 258; i++) {
        int32_t st;
        memset(fake, 0, sizeof fake);
        fake[0] = (uint8_t) i;
        fake[1] = (uint8_t) (i >> 8);
        snprintf(id, sizeof id, "U%d", i);
        st = zvt_responder_add(s2, id, fake);
        CHECK(st == (i < 256 ? ZXV_OK : ZXV_E_CAPACITY), "responder %d -> %s", i,
              zvt_status_name(st));
    }
    for (int i = 0; i < 18; i++) {
        int32_t st;
        memset(fake, 0xee, sizeof fake);
        fake[0] = (uint8_t) i;
        snprintf(id, sizeof id, "R%d", i);
        st = zvt_reviewer_add(s2, id, fake);
        CHECK(st == (i < 16 ? ZXV_OK : ZXV_E_CAPACITY), "reviewer %d -> %s", i,
              zvt_status_name(st));
    }
    zvt_station_destroy(s2);
    /* queries on unknowns */
    {
        zvt_patient_view v;
        zvt_responder_view rv;
        char buf[64];
        CHECK_RC(zvt_patient_state(f->s, "NOBODY", &v), ZVT_E_PATIENT);
        CHECK_RC(zvt_patient_head(f->s, "NOBODY", 0, NULL, NULL, 0, NULL, NULL, NULL),
                 ZVT_E_PATIENT);
        CHECK_RC(zvt_responder_state(f->s, "NOBODY", &rv), ZVT_E_RESPONDER);
        CHECK_RC(zvt_station_ack(f->s, "NOBODY", (uint8_t *) buf, sizeof buf, &(size_t){0}), ZVT_E_RESPONDER);
        CHECK_RC(zvt_patient_id(f->s, 0, buf, sizeof buf), ZXV_E_NOT_FOUND);
        CHECK_RC(zvt_resolve_message(f->s, "NOBODY", 1, "DR-LEE", NULL, 0, &(size_t){0}),
                 ZVT_E_PATIENT);
    }
    /* outbox refusals */
    {
        zvt_record r = mk("P-0", ZVT_CAT_MINOR, 1);
        uint32_t n = 9;
        CHECK_RC(zvt_outbox_seal(f->ob[R1], 1, NULL), ZXV_E_STATE);
        r.category = 0;
        CHECK_RC(zvt_outbox_record(f->ob[R1], &r, NULL), ZVT_E_RECORD);
        CHECK_RC(zvt_outbox_staged(f->ob[R1], &n), ZXV_OK);
        CHECK(n == 0, "a refused record is not staged");
        CHECK_RC(zvt_outbox_bundle(f->ob[R1], 0, NULL, 0, &(size_t){0}), ZXV_E_NOT_FOUND);
    }
}

/* ===== wire format, delivery, replays, gaps ===== */

static void test_delivery(fx_t *f) {
    zvt_record r = mk("P-100", ZVT_CAT_IMMEDIATE, 1000);
    uint8_t h0[32], zero[32] = {0}, recb[1][400], *mine, *theirs;
    size_t lens[1], ml = 0, tl = 0;
    zvt_receipt rc;
    zvt_patient_view v;
    uint8_t base = 0, ph = 0;
    uint64_t bundles = 0, replays = 0;

    CHECK_RC(zvt_outbox_record(f->ob[R1], &r, h0), ZXV_OK);
    CHECK_RC(zvt_outbox_seal(f->ob[R1], 1000, NULL), ZXV_OK);
    /* the outbox's bytes are exactly the documented layout */
    mine = last_bundle(f, R1, &ml);
    lens[0] = tb_record(recb[0], sizeof recb[0], k_resp[R1], 0, &r);
    theirs = tb_bundle(f, f->key[R1], 1, 0, k_resp[R1], 0, zero, 1000, 1, recb, lens, &tl);
    CHECK(mine && ml == tl && memcmp(mine, theirs, tl - ZIND_SIG_BYTES) == 0,
          "the outbox writes the documented layout (%zu vs %zu bytes)", ml, tl);
    {
        uint8_t hh[32];
        zind_subject("vinea-triage/rec", recb[0], lens[0], hh);
        CHECK(memcmp(hh, h0, 32) == 0, "the record hash is zind_subject(vinea-triage/rec, bytes)");
    }
    free(theirs);
    CHECK_RC(zvt_station_receive(f->s, mine, ml, &rc), ZXV_OK);
    CHECK(rc.bundles == 1 && rc.records == 1 && rc.duplicates == 0 && rc.new_conflicts == 0 &&
              rc.seq == 0 && strcmp(rc.responder, k_resp[R1]) == 0,
          "receipt for the first bundle");
    v = view(f, "P-100");
    CHECK(v.lp == ZXV_LP_TRUE && v.category == ZVT_CAT_IMMEDIATE && v.heads == 1 &&
              v.records == 1 && !v.resolved,
          "P-100 TRUE IMMEDIATE");
    CHECK(zxv_hex_unpack(v.tag, &base, NULL, &ph) == ZXV_OK && base == ZXV_HEX_BASE_TRUE &&
              ph == ZXV_HEX_PHASE_POTENTIAL,
          "first record: true/potential (%s)", zxv_hex_name(v.tag));
    /* the same bundle again, three times: dropped */
    for (int i = 0; i < 3; i++) CHECK_RC(zvt_station_receive(f->s, mine, ml, &rc), ZIND_E_REPLAY);
    CHECK_RC(zvt_station_stats(f->s, &bundles, &replays, NULL, NULL), ZXV_OK);
    CHECK(bundles == 1 && replays == 3, "stats: 1 bundle, 3 replays");
    v = view(f, "P-100");
    CHECK(v.records == 1, "replays add nothing");
    free(mine);

    /* the same responder re-triages: its own newer record replaces the older */
    r = mk("P-100", ZVT_CAT_DELAYED, 1100);
    CHECK_RC(one(f, R1, &r, NULL), ZXV_OK);
    CHECK_RC(deliver_last(f, R1, &rc), ZXV_OK);
    v = view(f, "P-100");
    CHECK(v.lp == ZXV_LP_TRUE && v.category == ZVT_CAT_DELAYED && v.heads == 1 && v.records == 2,
          "own update: TRUE DELAYED, one head");
    CHECK(zxv_hex_unpack(v.tag, NULL, NULL, &ph) == ZXV_OK && ph == ZXV_HEX_PHASE_EMERGING,
          "updated: emerging");
    {
        zvt_record got;
        char who[40];
        uint8_t cat = 0;
        int dec = 1;
        CHECK_RC(zvt_patient_head(f->s, "P-100", 0, &got, who, sizeof who, NULL, &cat, &dec),
                 ZXV_OK);
        CHECK(strcmp(who, k_resp[R1]) == 0 && cat == ZVT_CAT_DELAYED && !dec &&
                  got.observed == 1100 && got.vitals[1].value == 110 &&
                  strcmp(got.note, "tag P-100") == 0,
              "head 0 is MEDIC-1's record, as sent");
        CHECK_RC(zvt_patient_head(f->s, "P-100", 1, NULL, NULL, 0, NULL, NULL, NULL),
                 ZXV_E_NOT_FOUND);
        CHECK_RC(zvt_patient_head(f->s, "P-100", 0, NULL, who, 3, NULL, NULL, NULL),
                 ZXV_E_BUFFER);
    }

    /* out of order: four bundles from ENGINE-7, delivered 2, 3, 2, 0, 1 */
    {
        uint8_t *b[4];
        size_t bl[4];
        zvt_responder_view rv;
        for (int i = 0; i < 4; i++) {
            char p[16];
            snprintf(p, sizeof p, "Q-%d", i);
            r = mk(p, ZVT_CAT_MINOR, 2000 + (uint64_t) i);
            CHECK_RC(one(f, R3, &r, NULL), ZXV_OK);
            b[i] = last_bundle(f, R3, &bl[i]);
        }
        CHECK_RC(zvt_station_receive(f->s, b[2], bl[2], &rc), ZVT_E_GAP);
        CHECK_RC(zvt_station_receive(f->s, b[3], bl[3], &rc), ZVT_E_GAP);
        CHECK_RC(zvt_station_receive(f->s, b[2], bl[2], &rc), ZIND_E_REPLAY);
        CHECK_RC(zvt_responder_state(f->s, k_resp[R3], &rv), ZXV_OK);
        CHECK(rv.next_seq == 0 && rv.pending == 2 && rv.lp == ZXV_LP_TRUE, "two waiting");
        CHECK_RC(zvt_patient_state(f->s, "Q-2", &v), ZVT_E_PATIENT);
        CHECK_RC(zvt_station_receive(f->s, b[0], bl[0], &rc), ZXV_OK);
        CHECK(rc.bundles == 1, "bundle 0 alone");
        CHECK_RC(zvt_station_receive(f->s, b[1], bl[1], &rc), ZXV_OK);
        CHECK(rc.bundles == 3 && rc.records == 3, "bundle 1 drains 2 and 3 (%u)", rc.bundles);
        CHECK_RC(zvt_responder_state(f->s, k_resp[R3], &rv), ZXV_OK);
        CHECK(rv.next_seq == 4 && rv.pending == 0, "in order to 4");
        for (int i = 0; i < 4; i++) {
            char p[16];
            snprintf(p, sizeof p, "Q-%d", i);
            CHECK(view(f, p).lp == ZXV_LP_TRUE, "%s present", p);
            CHECK_RC(zvt_station_receive(f->s, b[i], bl[i], &rc), ZIND_E_REPLAY);
            free(b[i]);
        }
    }
    /* acknowledgements */
    {
        uint8_t ack[6000];
        size_t al = 0;
        uint32_t n = 0;
        CHECK_RC(zvt_outbox_pending(f->ob[R3], &n), ZXV_OK);
        CHECK(n == 4, "four unacknowledged");
        CHECK_RC(zvt_station_ack(f->s, k_resp[R3], NULL, 0, &al), ZXV_E_BUFFER);
        CHECK_RC(zvt_station_ack(f->s, k_resp[R3], ack, sizeof ack, &al), ZXV_OK);
        CHECK_RC(zvt_outbox_ack(f->ob[R3], zind_signer_pk(f->rogue), ack, al), ZXV_E_VERIFY);
        CHECK_RC(zvt_outbox_ack(f->ob[R1], zind_signer_pk(f->station), ack, al), ZVT_E_RESPONDER);
        ack[al / 2] ^= 1;
        CHECK_RC(zvt_outbox_ack(f->ob[R3], zind_signer_pk(f->station), ack, al), ZXV_E_VERIFY);
        ack[al / 2] ^= 1;
        CHECK_RC(zvt_outbox_ack(f->ob[R3], zind_signer_pk(f->station), ack, al - 1), ZXV_E_FORMAT);
        CHECK_RC(zvt_outbox_ack(f->ob[R3], zind_signer_pk(f->station), ack, 10), ZXV_E_FORMAT);
        CHECK_RC(zvt_outbox_pending(f->ob[R3], &n), ZXV_OK);
        CHECK(n == 4, "a refused acknowledgement drops nothing");
        CHECK_RC(zvt_outbox_ack(f->ob[R3], zind_signer_pk(f->station), ack, al), ZXV_OK);
        CHECK_RC(zvt_outbox_pending(f->ob[R3], &n), ZXV_OK);
        CHECK(n == 0, "acknowledged bundles are dropped");
        CHECK_RC(zvt_outbox_ack(f->ob[R3], zind_signer_pk(f->station), ack, al), ZXV_OK);
        /* an acknowledgement of a history the device never made */
        {
            uint8_t fake[200], hh[32], *body = fake;
            zind_wr w;
            zind_wr_init(&w, body, sizeof fake);
            zind_wr_bytes(&w, "ZVTA", 4);
            zind_wr_u8(&w, 1);
            zind_wr_str(&w, k_resp[R3]);
            zind_wr_u64(&w, 3);
            zind_wr_bytes(&w, zero, 32);
            memcpy(ack, fake, w.len);
            zind_subject("vinea-triage/ack", ack, w.len, hh);
            zind_sign(f->e.cap, f->station, hh, 32, ZVT_ACK_CTX, ack + w.len);
            CHECK_RC(zvt_outbox_ack(f->ob[R3], zind_signer_pk(f->station), ack,
                                    w.len + ZIND_SIG_BYTES),
                     ZIND_E_CHAIN);
            fake[5 + 1 + strlen(k_resp[R3])] = 9; /* next_seq 9: more than it made */
            memcpy(ack, fake, w.len);
            zind_subject("vinea-triage/ack", ack, w.len, hh);
            zind_sign(f->e.cap, f->station, hh, 32, ZVT_ACK_CTX, ack + w.len);
            CHECK_RC(zvt_outbox_ack(f->ob[R3], zind_signer_pk(f->station), ack,
                                    w.len + ZIND_SIG_BYTES),
                     ZIND_E_CHAIN);
        }
    }
}

/* ===== malformed, forged, encrypted and flipped bundles ===== */

static void test_bundles(fx_t *f) {
    zvt_record r = mk("P-200", ZVT_CAT_MINOR, 3000);
    uint8_t recb[33][400], zero[32] = {0}, *b;
    size_t lens[33], bl = 0;
    zvt_receipt rc;
    zvt_responder_view rv;
    uint32_t q0 = qcount(f);
    uint64_t bad0 = 0, bad1 = 0;
    const char *MB = "MEDIC-B";
    zind_signer *kb = zt_signer(f->e.cap, 50);
    CHECK_RC(zvt_responder_add(f->s, MB, zind_signer_pk(kb)), ZXV_OK);
    for (int i = 0; i < 33; i++) {
        r.observed = 3000 + (uint64_t) i;
        lens[i] = tb_record(recb[i], sizeof recb[i], MB, (uint64_t) i, &r);
    }
    zvt_station_stats(f->s, NULL, NULL, NULL, &bad0);
    /* unknown responder */
    b = tb_bundle(f, kb, 1, 0, "STRANGER", 0, zero, 1, 1, recb, lens, &bl);
    CHECK_RC(zvt_station_receive(f->s, b, bl, &rc), ZVT_E_RESPONDER);
    free(b);
    /* signed by someone else */
    b = tb_bundle(f, f->rogue, 1, 0, MB, 0, zero, 1, 1, recb, lens, &bl);
    CHECK_RC(zvt_station_receive(f->s, b, bl, &rc), ZIND_E_CHAIN);
    free(b);
    b = tb_bundle(f, f->key[R1], 1, 0, MB, 0, zero, 1, 1, recb, lens, &bl);
    CHECK_RC(zvt_station_receive(f->s, b, bl, &rc), ZIND_E_CHAIN);
    free(b);
    zvt_station_stats(f->s, NULL, NULL, NULL, &bad1);
    CHECK(bad1 == bad0 + 2 && qcount(f) == q0 + 2, "two bad signatures counted and logged");
    {
        uint8_t t = 0, base = 0, pol = 0, reason = 0;
        zxv_hexlog_at(f->qlog, qcount(f) - 1, NULL, &t, &reason, NULL, NULL, NULL, NULL);
        zxv_hex_unpack(t, &base, &pol, NULL);
        CHECK(pol == ZXV_HEX_POL_FORBIDDEN && reason == ZIND_REASON_TAMPER, "logged as tamper");
    }
    /* encrypted: refused, not decrypted, not guessed */
    b = tb_bundle(f, kb, 1, 1, MB, 0, zero, 1, 1, recb, lens, &bl);
    CHECK_RC(zvt_station_receive(f->s, b, bl, &rc), ZXV_E_NOTIMPL);
    free(b);
    /* malformed */
    b = tb_bundle(f, kb, 2, 0, MB, 0, zero, 1, 1, recb, lens, &bl);
    CHECK_RC(zvt_station_receive(f->s, b, bl, &rc), ZXV_E_FORMAT);
    free(b);
    b = tb_bundle(f, kb, 1, 0, MB, 0, zero, 1, 0, recb, lens, &bl);
    CHECK_RC(zvt_station_receive(f->s, b, bl, &rc), ZXV_E_FORMAT); /* no records */
    free(b);
    b = tb_bundle(f, kb, 1, 0, MB, 0, zero, 1, 33, recb, lens, &bl);
    CHECK_RC(zvt_station_receive(f->s, b, bl, &rc), ZXV_E_FORMAT); /* 33 records */
    free(b);
    b = tb_bundle(f, kb, 1, 0, MB, 0, zero, 1, 1, recb, lens, &bl);
    b[0] = 'X';
    CHECK_RC(zvt_station_receive(f->s, b, bl, &rc), ZXV_E_FORMAT);
    CHECK_RC(zvt_station_receive(f->s, b, 100, &rc), ZXV_E_FORMAT);
    CHECK_RC(zvt_station_receive(f->s, b, 0, &rc), ZXV_E_FORMAT);
    CHECK_RC(zvt_station_receive(NULL, b, bl, &rc), ZXV_E_ARG);
    free(b);
    {
        /* a record by another responder inside MEDIC-2's bundle */
        uint8_t other[1][400];
        size_t ol[1];
        ol[0] = tb_record(other[0], sizeof other[0], k_resp[R1], 0, &r);
        b = tb_bundle(f, kb, 1, 0, MB, 0, zero, 1, 1, other, ol, &bl);
        CHECK_RC(zvt_station_receive(f->s, b, bl, &rc), ZXV_E_FORMAT);
        free(b);
        /* a signed but invalid record (category 9) */
        r.category = 9;
        ol[0] = tb_record(other[0], sizeof other[0], MB, 0, &r);
        b = tb_bundle(f, kb, 1, 0, MB, 0, zero, 1, 1, other, ol, &bl);
        CHECK_RC(zvt_station_receive(f->s, b, bl, &rc), ZXV_E_FORMAT);
        free(b);
        r.category = ZVT_CAT_MINOR;
        /* trailing bytes after a record */
        ol[0] = tb_record(other[0], sizeof other[0], MB, 0, &r) + 1;
        b = tb_bundle(f, kb, 1, 0, MB, 0, zero, 1, 1, other, ol, &bl);
        CHECK_RC(zvt_station_receive(f->s, b, bl, &rc), ZXV_E_FORMAT);
        free(b);
    }
    CHECK_RC(zvt_responder_state(f->s, MB, &rv), ZXV_OK);
    CHECK(rv.next_seq == 0 && rv.pending == 0, "nothing refused was kept");
    /* flipped bits across a genuine bundle: always refused, never applied */
    b = tb_bundle(f, kb, 1, 0, MB, 0, zero, 1, 3, recb, lens, &bl);
    for (int k = 0; k < 64; k++) {
        size_t off = (size_t) k * (bl - 1) / 63;
        int32_t st;
        b[off] ^= 0x04;
        st = zvt_station_receive(f->s, b, bl, &rc);
        CHECK(st != ZXV_OK && st != ZVT_E_GAP && st != ZIND_E_REPLAY,
              "bit flipped at %zu of %zu refused (%s)", off, bl, zvt_status_name(st));
        b[off] ^= 0x04;
    }
    CHECK_RC(zvt_responder_state(f->s, MB, &rv), ZXV_OK);
    CHECK(rv.next_seq == 0 && rv.pending == 0 && rv.lp == ZXV_LP_TRUE, "flips changed nothing");
    CHECK_RC(zvt_station_receive(f->s, b, bl, &rc), ZXV_OK);
    CHECK(rc.records == 3, "the genuine bundle applies");
    free(b);
    /* records already held, in a later bundle: dropped as duplicates */
    {
        uint8_t prev[32], two[2][400];
        size_t tl[2];
        zvt_responder_view v2;
        uint64_t dups = 0;
        const zind_chain *j = zvt_station_journal(f->s);
        uint32_t jn = 0;
        zind_chain_count(j, &jn);
        {
            zind_record jr;
            zind_chain_at(j, jn - 1, &jr);
            /* the journal's last bundle record names the bundle hash */
            memcpy(prev, jr.payload + 1 + strlen(MB) + 8, 32);
        }
        memcpy(two[0], recb[1], lens[1]);
        tl[0] = lens[1];
        r.observed = 3999;
        tl[1] = tb_record(two[1], sizeof two[1], MB, 7, &r);
        b = tb_bundle(f, kb, 1, 0, MB, 1, prev, 2, 2, two, tl, &bl);
        CHECK_RC(zvt_station_receive(f->s, b, bl, &rc), ZXV_OK);
        CHECK(rc.records == 1 && rc.duplicates == 1, "one new, one duplicate (%u/%u)", rc.records,
              rc.duplicates);
        free(b);
        zvt_station_stats(f->s, NULL, NULL, &dups, NULL);
        CHECK(dups == 1, "stats: one duplicate record");
        CHECK_RC(zvt_responder_state(f->s, MB, &v2), ZXV_OK);
        CHECK(v2.next_seq == 2, "MEDIC-B at 2");
    }
    zind_signer_destroy(kb);
}

/* ===== a cloned device: forks ===== */

static void test_fork(fx_t *f) {
    zind_signer *k = zt_signer(f->e.cap, 60);
    zvt_outbox *a = NULL, *clone = NULL;
    zvt_station *s = f->s;
    zvt_record r;
    uint8_t *b1, *b2;
    size_t l1 = 0, l2 = 0;
    uint32_t n = 0, q0, jn0 = 0, jn1 = 0;
    zvt_responder_view rv;
    CHECK_RC(zvt_responder_add(s, "MEDIC-9", zind_signer_pk(k)), ZXV_OK);
    CHECK_RC(zvt_outbox_create(f->e.cap, "MEDIC-9", k, &a), ZXV_OK);
    CHECK_RC(zvt_outbox_create(f->e.cap, "MEDIC-9", k, &clone), ZXV_OK);
    r = mk("F-1", ZVT_CAT_MINOR, 10);
    CHECK_RC(zvt_outbox_record(a, &r, NULL), ZXV_OK);
    CHECK_RC(zvt_outbox_seal(a, 10, NULL), ZXV_OK);
    r = mk("F-2", ZVT_CAT_DEAD, 11);
    CHECK_RC(zvt_outbox_record(clone, &r, NULL), ZXV_OK);
    CHECK_RC(zvt_outbox_seal(clone, 11, NULL), ZXV_OK);
    zvt_outbox_bundle(a, 0, NULL, 0, &l1);
    b1 = (uint8_t *) malloc(l1);
    zvt_outbox_bundle(a, 0, b1, l1, &l1);
    zvt_outbox_bundle(clone, 0, NULL, 0, &l2);
    b2 = (uint8_t *) malloc(l2);
    zvt_outbox_bundle(clone, 0, b2, l2, &l2);
    CHECK_RC(zvt_station_receive(s, b1, l1, NULL), ZXV_OK);
    q0 = qcount(f);
    zind_chain_count(zvt_station_journal(s), &jn0);
    CHECK_RC(zvt_station_receive(s, b2, l2, NULL), ZVT_E_FORK);
    CHECK(qcount(f) == q0 + 1, "the fork is logged");
    zind_chain_count(zvt_station_journal(s), &jn1);
    CHECK(jn1 == jn0 + 1, "the fork is journaled");
    {
        zind_record jr;
        zind_chain_at(zvt_station_journal(s), jn1 - 1, &jr);
        CHECK(jr.kind == ZVT_J_FORK, "journal kind FORK");
    }
    CHECK_RC(zvt_responder_state(s, "MEDIC-9", &rv), ZXV_OK);
    CHECK(rv.lp == ZXV_LP_BOTH, "MEDIC-9 held as BOTH");
    CHECK_RC(zvt_patient_state(s, "F-2", &(zvt_patient_view){0}), ZVT_E_PATIENT);
    /* everything from a held responder waits for a person */
    r = mk("F-3", ZVT_CAT_MINOR, 12);
    CHECK_RC(zvt_outbox_record(a, &r, NULL), ZXV_OK);
    CHECK_RC(zvt_outbox_seal(a, 12, NULL), ZXV_OK);
    free(b1);
    zvt_outbox_pending(a, &n);
    zvt_outbox_bundle(a, n - 1, NULL, 0, &l1);
    b1 = (uint8_t *) malloc(l1);
    zvt_outbox_bundle(a, n - 1, b1, l1, &l1);
    CHECK_RC(zvt_station_receive(s, b1, l1, NULL), ZIND_E_HELD);
    CHECK_RC(zvt_station_receive(s, b2, l2, NULL), ZIND_E_HELD);
    free(b1);
    free(b2);
    /* a fork found through a record number used twice with other content */
    {
        zind_signer *k2 = zt_signer(f->e.cap, 61);
        uint8_t recs[1][400], zero[32] = {0}, h[32];
        size_t lens[1], bl = 0;
        uint8_t *b;
        CHECK_RC(zvt_responder_add(s, "MEDIC-10", zind_signer_pk(k2)), ZXV_OK);
        r = mk("G-1", ZVT_CAT_MINOR, 20);
        lens[0] = tb_record(recs[0], sizeof recs[0], "MEDIC-10", 5, &r);
        b = tb_bundle(f, k2, 1, 0, "MEDIC-10", 0, zero, 20, 1, recs, lens, &bl);
        CHECK_RC(zvt_station_receive(s, b, bl, NULL), ZXV_OK);
        zind_subject("vinea-triage/bundle", b, bl - ZIND_SIG_BYTES, h);
        free(b);
        r = mk("G-1", ZVT_CAT_DEAD, 21); /* record 5 again, different content */
        lens[0] = tb_record(recs[0], sizeof recs[0], "MEDIC-10", 5, &r);
        b = tb_bundle(f, k2, 1, 0, "MEDIC-10", 1, h, 21, 1, recs, lens, &bl);
        CHECK_RC(zvt_station_receive(s, b, bl, NULL), ZVT_E_FORK);
        free(b);
        CHECK(view(f, "G-1").category == ZVT_CAT_MINOR, "the equivocating record is not applied");
        /* a bundle whose prev does not match: also a fork */
        zind_signer_destroy(k2);
    }
    {
        zind_signer *k3 = zt_signer(f->e.cap, 62);
        uint8_t recs[1][400], bogus[32];
        size_t lens[1], bl = 0;
        uint8_t *b;
        memset(bogus, 0xab, 32);
        CHECK_RC(zvt_responder_add(s, "MEDIC-11", zind_signer_pk(k3)), ZXV_OK);
        r = mk("H-1", ZVT_CAT_MINOR, 30);
        lens[0] = tb_record(recs[0], sizeof recs[0], "MEDIC-11", 0, &r);
        b = tb_bundle(f, k3, 1, 0, "MEDIC-11", 0, bogus, 30, 1, recs, lens, &bl);
        CHECK_RC(zvt_station_receive(s, b, bl, NULL), ZVT_E_FORK);
        free(b);
        zind_signer_destroy(k3);
    }
    zvt_outbox_destroy(a);
    zvt_outbox_destroy(clone);
    zind_signer_destroy(k);
}

/* ===== conflicts held as BOTH, and reviewers ===== */

static int32_t review(fx_t *f, const zind_signer *k, const char *patient, uint8_t cat,
                      const char *who, uint8_t sig[ZIND_SIG_BYTES]) {
    uint8_t m[2048];
    size_t len = 0;
    int32_t st = zvt_resolve_message(f->s, patient, cat, who, m, sizeof m, &len);
    if (st != ZXV_OK) return st;
    return zind_sign(f->e.cap, k, m, len, ZVT_RESOLVE_CTX, sig);
}

static void test_conflicts(fx_t *f) {
    zvt_record r;
    zvt_receipt rc;
    zvt_patient_view v;
    uint8_t h1[32], h2[32], hd[32], sig[ZIND_SIG_BYTES], base = 0, ph = 0, rt = 0, reason = 0;
    uint32_t q0;
    int32_t act = -1;
    int dec = 0;

    r = mk("C-1", ZVT_CAT_IMMEDIATE, 5000);
    CHECK_RC(one(f, R1, &r, h1), ZXV_OK);
    CHECK_RC(deliver_last(f, R1, &rc), ZXV_OK);
    q0 = qcount(f);
    /* another responder, not building on MEDIC-1's record, says DELAYED */
    r = mk("C-1", ZVT_CAT_DELAYED, 5001);
    CHECK_RC(one(f, R2, &r, h2), ZXV_OK);
    CHECK_RC(deliver_last(f, R2, &rc), ZXV_OK);
    CHECK(rc.new_conflicts == 1, "a new conflict is reported");
    v = view(f, "C-1");
    CHECK(v.lp == ZXV_LP_BOTH && v.category == 0 && v.heads == 2 &&
              v.categories == ((1u << ZVT_CAT_IMMEDIATE) | (1u << ZVT_CAT_DELAYED)),
          "held as BOTH with both categories, nothing merged");
    CHECK(zxv_hex_unpack(v.tag, &base, NULL, NULL) == ZXV_OK && base == ZXV_HEX_BASE_BOTH &&
              zxv_hex_action(v.tag, &act) == ZXV_OK && act == ZXV_HEX_ACT_ESCALATE,
          "BOTH escalates");
    CHECK(qcount(f) == q0 + 1, "the hold is logged");
    zxv_hexlog_at(f->qlog, q0, NULL, &rt, &reason, NULL, NULL, NULL, NULL);
    CHECK(zxv_hex_unpack(rt, &base, NULL, NULL) == ZXV_OK && base == ZXV_HEX_BASE_BOTH &&
              reason == ZIND_REASON_HELD_BOTH,
          "logged as a held BOTH");
    /* agreement does not clear it, nor does time: still BOTH */
    r = mk("C-1", ZVT_CAT_DELAYED, 5002);
    CHECK_RC(one(f, R3, &r, NULL), ZXV_OK);
    CHECK_RC(deliver_last(f, R3, &rc), ZXV_OK);
    CHECK(rc.new_conflicts == 0 && view(f, "C-1").lp == ZXV_LP_BOTH && view(f, "C-1").heads == 3,
          "a third, agreeing record stands beside; still BOTH");
    CHECK(qcount(f) == q0 + 1, "no second log entry while held");

    /* reviewers */
    CHECK_RC(review(f, f->rev, "C-1", ZVT_CAT_IMMEDIATE, "DR-LEE", sig), ZXV_OK);
    CHECK_RC(zvt_resolve(f->s, "C-1", ZVT_CAT_IMMEDIATE, "DR-NOBODY", sig), ZVT_E_REVIEWER);
    CHECK_RC(zvt_resolve(f->s, "C-1", ZVT_CAT_DELAYED, "DR-LEE", sig), ZXV_E_VERIFY);
    CHECK_RC(zvt_resolve(f->s, "C-1", 0, "DR-LEE", sig), ZXV_E_ARG);
    CHECK_RC(zvt_resolve(f->s, "C-1", 6, "DR-LEE", sig), ZXV_E_ARG);
    CHECK_RC(zvt_resolve(f->s, "C-X", ZVT_CAT_IMMEDIATE, "DR-LEE", sig), ZVT_E_PATIENT);
    CHECK_RC(zvt_resolve(f->s, "P-100", ZVT_CAT_IMMEDIATE, "DR-LEE", sig), ZXV_E_STATE);
    {
        uint8_t fsig[ZIND_SIG_BYTES];
        uint32_t q1 = qcount(f);
        CHECK_RC(review(f, f->rogue, "C-1", ZVT_CAT_IMMEDIATE, "DR-LEE", fsig), ZXV_OK);
        CHECK_RC(zvt_resolve(f->s, "C-1", ZVT_CAT_IMMEDIATE, "DR-LEE", fsig), ZXV_E_VERIFY);
        CHECK(qcount(f) == q1 + 1, "a forged decision is logged");
    }
    /* stale: a new record arrives after the reviewer signed */
    r = mk("C-1", ZVT_CAT_MINOR, 5003);
    CHECK_RC(one(f, R3, &r, NULL), ZXV_OK);
    CHECK_RC(deliver_last(f, R3, &rc), ZXV_OK);
    CHECK_RC(zvt_resolve(f->s, "C-1", ZVT_CAT_IMMEDIATE, "DR-LEE", sig), ZXV_E_VERIFY);
    CHECK(view(f, "C-1").lp == ZXV_LP_BOTH, "a stale decision changes nothing");
    /* genuine */
    CHECK_RC(review(f, f->rev, "C-1", ZVT_CAT_IMMEDIATE, "DR-LEE", sig), ZXV_OK);
    CHECK_RC(zvt_resolve(f->s, "C-1", ZVT_CAT_IMMEDIATE, "DR-LEE", sig), ZXV_OK);
    v = view(f, "C-1");
    CHECK(v.lp == ZXV_LP_TRUE && v.category == ZVT_CAT_IMMEDIATE && v.resolved && v.heads == 1,
          "the reviewer's category stands");
    CHECK(zxv_hex_unpack(v.tag, NULL, NULL, &ph) == ZXV_OK && ph == ZXV_HEX_PHASE_MANIFEST,
          "decided: manifest");
    CHECK_RC(zvt_resolve(f->s, "C-1", ZVT_CAT_IMMEDIATE, "DR-LEE", sig), ZXV_E_STATE);
    {
        char who[40];
        CHECK_RC(zvt_patient_head(f->s, "C-1", 0, NULL, who, sizeof who, hd, NULL, &dec), ZXV_OK);
        CHECK(dec == 1 && strcmp(who, "DR-LEE") == 0, "the head is DR-LEE's decision");
    }
    /* a responder who builds on the decision replaces it */
    r = mk("C-1", ZVT_CAT_IMMEDIATE, 5100);
    memcpy(r.based_on, hd, 32);
    CHECK_RC(one(f, R2, &r, NULL), ZXV_OK);
    CHECK_RC(deliver_last(f, R2, &rc), ZXV_OK);
    v = view(f, "C-1");
    CHECK(v.lp == ZXV_LP_TRUE && !v.resolved && v.heads == 1, "built on the decision");
    /* a late record made without seeing the decision stands beside it */
    r = mk("C-1", ZVT_CAT_DEAD, 5101);
    CHECK_RC(one(f, R1, &r, NULL), ZXV_OK);
    CHECK_RC(deliver_last(f, R1, &rc), ZXV_OK);
    CHECK(rc.new_conflicts == 1 && view(f, "C-1").lp == ZXV_LP_BOTH, "held again, not merged");

    /* a record that a later record already superseded never becomes a head */
    {
        uint8_t hx[32];
        size_t lx = 0;
        uint8_t *bx;
        r = mk("L-1", ZVT_CAT_DELAYED, 6000);
        CHECK_RC(zvt_outbox_record(f->ob[R1], &r, hx), ZXV_OK); /* staged, not yet sent */
        CHECK_RC(zvt_outbox_seal(f->ob[R1], 6000, NULL), ZXV_OK);
        bx = last_bundle(f, R1, &lx);
        r = mk("L-1", ZVT_CAT_IMMEDIATE, 6001);
        memcpy(r.based_on, hx, 32); /* MEDIC-2 heard about it by voice and re-triaged */
        CHECK_RC(one(f, R2, &r, NULL), ZXV_OK);
        CHECK_RC(deliver_last(f, R2, &rc), ZXV_OK);
        CHECK(view(f, "L-1").category == ZVT_CAT_IMMEDIATE, "L-1 IMMEDIATE");
        CHECK_RC(zvt_station_receive(f->s, bx, lx, &rc), ZXV_OK);
        v = view(f, "L-1");
        CHECK(v.lp == ZXV_LP_TRUE && v.category == ZVT_CAT_IMMEDIATE && v.heads == 1 &&
                  v.records == 2 && rc.new_conflicts == 0,
              "the late, superseded record is kept but not a head");
        free(bx);
    }
    /* SALT categories */
    r = mk("S-1", ZVT_CAT_EXPECTANT, 7000);
    r.system = ZVT_SYS_SALT;
    CHECK_RC(one(f, R1, &r, NULL), ZXV_OK);
    CHECK_RC(deliver_last(f, R1, &rc), ZXV_OK);
    CHECK(view(f, "S-1").category == ZVT_CAT_EXPECTANT, "SALT EXPECTANT");

    /* without ATTEST a station cannot apply a decision */
    {
        zxv_cap *weak = NULL;
        zvt_station *s2 = NULL;
        zvt_outbox *o = NULL;
        size_t len = 0, ml = 0;
        uint8_t m[2048], *b;
        CHECK_RC(zxv_cap_attenuate(f->e.cap, ZXV_CAP_CRYPTO_SIGN, &weak), ZXV_OK);
        CHECK_RC(zvt_station_create(weak, f->station, NULL, &s2), ZXV_OK);
        CHECK_RC(zvt_responder_add(s2, "A", zind_signer_pk(f->key[R1])), ZXV_OK);
        CHECK_RC(zvt_responder_add(s2, "B", zind_signer_pk(f->key[R2])), ZXV_OK);
        CHECK_RC(zvt_reviewer_add(s2, "DR-LEE", zind_signer_pk(f->rev)), ZXV_OK);
        for (int i = 0; i < 2; i++) {
            CHECK_RC(zvt_outbox_create(f->e.cap, i ? "B" : "A", f->key[i], &o), ZXV_OK);
            r = mk("W-1", (uint8_t) (i ? ZVT_CAT_MINOR : ZVT_CAT_DEAD), 1);
            CHECK_RC(zvt_outbox_record(o, &r, NULL), ZXV_OK);
            CHECK_RC(zvt_outbox_seal(o, 1, NULL), ZXV_OK);
            zvt_outbox_bundle(o, 0, NULL, 0, &len);
            b = (uint8_t *) malloc(len);
            zvt_outbox_bundle(o, 0, b, len, &len);
            CHECK_RC(zvt_station_receive(s2, b, len, NULL), ZXV_OK);
            free(b);
            zvt_outbox_destroy(o);
        }
        CHECK_RC(zvt_resolve_message(s2, "W-1", ZVT_CAT_MINOR, "DR-LEE", m, sizeof m, &ml), ZXV_OK);
        CHECK_RC(zind_sign(f->e.cap, f->rev, m, ml, ZVT_RESOLVE_CTX, sig), ZXV_OK);
        CHECK_RC(zvt_resolve(s2, "W-1", ZVT_CAT_MINOR, "DR-LEE", sig), ZXV_E_CAP);
        CHECK_RC(zvt_resolve_message(s2, "W-1", ZVT_CAT_MINOR, "DR-LEE", m, 10, &ml),
                 ZXV_E_BUFFER);
        zvt_station_destroy(s2);
        zxv_cap_destroy(weak);
    }
}

/* ===== capacities, the journal and the log ===== */

static void test_capacity_and_audit(fx_t *f) {
    zind_signer *k = zt_signer(f->e.cap, 70);
    zvt_outbox *o = NULL;
    zvt_record r = mk("CAP-1", ZVT_CAT_MINOR, 1);
    uint32_t n = 0;
    uint64_t seq = 0;
    CHECK_RC(zvt_outbox_create(f->e.cap, "BULK", k, &o), ZXV_OK);
    /* 40 staged records seal into 32 + 8 */
    for (int i = 0; i < 40; i++) CHECK_RC(zvt_outbox_record(o, &r, NULL), ZXV_OK);
    CHECK_RC(zvt_outbox_seal(o, 1, &seq), ZXV_OK);
    CHECK_RC(zvt_outbox_staged(o, &n), ZXV_OK);
    CHECK(seq == 0 && n == 8, "32 records per bundle, 8 left");
    CHECK_RC(zvt_outbox_seal(o, 2, &seq), ZXV_OK);
    CHECK_RC(zvt_outbox_staged(o, &n), ZXV_OK);
    CHECK(seq == 1 && n == 0, "then the rest");
    /* the staging bound */
    for (int i = 0; i < 1024; i++) {
        if (zvt_outbox_record(o, &r, NULL) != ZXV_OK) {
            CHECK(0, "staging %d", i);
            break;
        }
    }
    CHECK_RC(zvt_outbox_record(o, &r, NULL), ZXV_E_CAPACITY);
    zvt_outbox_destroy(o);
    /* the unacknowledged bound */
    CHECK_RC(zvt_outbox_create(f->e.cap, "BULK", k, &o), ZXV_OK);
    for (int i = 0; i < 256; i++) {
        if (zvt_outbox_record(o, &r, NULL) != ZXV_OK || zvt_outbox_seal(o, 3, NULL) != ZXV_OK) {
            CHECK(0, "bundle %d", i);
            break;
        }
    }
    CHECK_RC(zvt_outbox_record(o, &r, NULL), ZXV_OK);
    CHECK_RC(zvt_outbox_seal(o, 3, NULL), ZXV_E_CAPACITY);
    CHECK_RC(zvt_outbox_pending(o, &n), ZXV_OK);
    CHECK(n == 256, "256 unacknowledged");
    /* the station holds at most 32 bundles behind a gap */
    CHECK_RC(zvt_responder_add(f->s, "BULK", zind_signer_pk(k)), ZXV_OK);
    for (uint32_t i = 1; i <= 33; i++) {
        size_t len = 0;
        uint8_t *b;
        int32_t st;
        zvt_outbox_bundle(o, i, NULL, 0, &len);
        b = (uint8_t *) malloc(len);
        zvt_outbox_bundle(o, i, b, len, &len);
        st = zvt_station_receive(f->s, b, len, NULL);
        CHECK(st == (i <= 32 ? ZVT_E_GAP : ZXV_E_CAPACITY), "pending %u -> %s", i,
              zvt_status_name(st));
        free(b);
    }
    {
        size_t len = 0;
        uint8_t *b;
        zvt_receipt rc;
        zvt_responder_view rv;
        zvt_outbox_bundle(o, 0, NULL, 0, &len);
        b = (uint8_t *) malloc(len);
        zvt_outbox_bundle(o, 0, b, len, &len);
        CHECK_RC(zvt_station_receive(f->s, b, len, &rc), ZXV_OK);
        CHECK(rc.bundles == 33, "the missing bundle drains 32 more (%u)", rc.bundles);
        CHECK_RC(zvt_responder_state(f->s, "BULK", &rv), ZXV_OK);
        CHECK(rv.next_seq == 33 && rv.pending == 0, "BULK at 33");
        free(b);
    }
    zvt_outbox_destroy(o);
    zind_signer_destroy(k);

    /* the journal verifies with the station key; the log verifies */
    {
        const uint8_t *trust[1] = {zind_signer_pk(f->station)};
        const zind_chain *j = zvt_station_journal(f->s);
        uint32_t jn = 0, kinds[4] = {0}, pc = 0;
        uint64_t bundles = 0;
        char id[40];
        CHECK_RC(zind_chain_verify(f->e.cap, j, trust, 1, NULL), ZXV_OK);
        const uint8_t *wrong[1] = {zind_signer_pk(f->rogue)};
        CHECK_RC(zind_chain_verify(f->e.cap, j, wrong, 1, NULL), ZIND_E_UNTRUSTED);
        zind_chain_count(j, &jn);
        for (uint32_t i = 0; i < jn; i++) {
            zind_record jr;
            zind_chain_at(j, i, &jr);
            if (jr.kind < 4) kinds[jr.kind]++;
        }
        zvt_station_stats(f->s, &bundles, NULL, NULL, NULL);
        CHECK(kinds[ZVT_J_BUNDLE] == bundles && kinds[ZVT_J_RESOLVE] == 1 &&
                  kinds[ZVT_J_FORK] == 3,
              "journal: %u bundles (%llu), %u decisions, %u forks", kinds[1],
              (unsigned long long) bundles, kinds[2], kinds[3]);
        CHECK_RC(zxv_hexlog_verify(f->qlog), ZXV_OK);
        CHECK_RC(zvt_patient_count(f->s, &pc), ZXV_OK);
        CHECK(pc > 10, "patients held (%u)", pc);
        CHECK_RC(zvt_patient_id(f->s, 0, id, sizeof id), ZXV_OK);
        CHECK(strcmp(id, "P-100") == 0, "patients in order of first record");
        CHECK_RC(zvt_patient_id(f->s, 0, id, 3), ZXV_E_BUFFER);
    }
    zvt_station_destroy(NULL);
    zvt_outbox_destroy(NULL);
}

int main(void) {
    fx_t f;
    test_records();
    if (fx_open(&f) != 0) {
        printf("fixture failed: %s\n", zt_last_err());
        return 2;
    }
    test_setup(&f);
    test_delivery(&f);
    test_bundles(&f);
    test_fork(&f);
    test_conflicts(&f);
    test_capacity_and_audit(&f);
    fx_close(&f);
    return zt_report("test_triage");
}
