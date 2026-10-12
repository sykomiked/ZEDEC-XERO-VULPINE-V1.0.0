/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_modem.c — tri-modem: setup refusals; advert sealing and parsing
 * (both algorithms, every malformed field, every truncation, every byte
 * flip); acceptance (replay, older sequence, expiry, future dating, forged,
 * wrong algorithm, amateur radio, unknown neighbour); evidence (contradictory
 * claims and probes held as BOTH, stale probes, owner resolution, forged
 * resolution, override dropped at the next advert); the plan (a worked
 * example in exact rationals, every reason a neighbour is not eligible, the
 * rmag charges) and simulated streets of routers with random WAN outages,
 * checking budgets and max-min fairness on every plan. */
#include "ztmodem.h"
#include "zind_test.h"

#undef CHECK_RC
#define CHECK_RC(expr, want)                                                                   \
    do {                                                                                       \
        int32_t rc_ = (expr);                                                                  \
        CHECK(rc_ == (want), "%s -> %d (%s), want %d (%s)", #expr, (int) rc_,                  \
              ztm_status_name(rc_), (int) (want), ztm_status_name(want));                     \
    } while (0)

#define T0 1760000000ULL

typedef struct {
    char id[ZTM_ID_MAX + 1];
    int32_t alg;
    uint8_t sk[32], pk[32];
    zind_signer *ml;
} nbk_t;

static int nbk_make(const zxv_cap *cap, nbk_t *k, const char *id, int32_t alg, uint8_t v) {
    memset(k, 0, sizeof *k);
    snprintf(k->id, sizeof k->id, "%s", id);
    k->alg = alg;
    if (alg == ZTM_ALG_ED25519) {
        zt_seed(k->sk, v);
        return zxv_ed25519_public(cap, k->sk, k->pk) == ZXV_OK ? 0 : -1;
    }
    k->ml = zt_signer(cap, v);
    return k->ml ? 0 : -1;
}

static const uint8_t *nbk_pk(const nbk_t *k, size_t *len) {
    *len = k->alg == ZTM_ALG_ED25519 ? 32 : ZIND_PK_BYTES;
    return k->alg == ZTM_ALG_ED25519 ? k->pk : zind_signer_pk(k->ml);
}

static ztm_advert adv(const char *id, uint64_t seq, uint64_t issued, int wan, int64_t num,
                      int64_t den, uint32_t lat) {
    ztm_advert a;
    memset(&a, 0, sizeof a);
    snprintf(a.neighbour, sizeof a.neighbour, "%s", id);
    a.seq = seq;
    a.issued = issued;
    a.expires = issued + 600;
    a.wan_up = (uint8_t) wan;
    a.medium = ZTM_MEDIUM_WIFI;
    zind_qty_make(num, den, &a.offer_kbps);
    a.latency_ms = lat;
    return a;
}

static void claim(ztm_advert *a, const char *name, int32_t lp) {
    snprintf(a->claims[a->n_claims].name, sizeof a->claims[0].name, "%s", name);
    a->claims[a->n_claims++].lp = lp;
}

static size_t seal(const zxv_cap *cap, const nbk_t *k, const ztm_advert *a, uint8_t *buf,
                   size_t cap_bytes) {
    size_t len = 0;
    int32_t st = k->alg == ZTM_ALG_ED25519
                     ? ztm_advert_seal_ed25519(cap, a, k->sk, buf, cap_bytes, &len)
                     : ztm_advert_seal_mldsa(cap, a, k->ml, buf, cap_bytes, &len);
    return st == ZXV_OK ? len : 0;
}

static zind_qty q(int64_t n, int64_t d) {
    zind_qty r = ZIND_QTY_ZERO;
    zind_qty_make(n, d, &r);
    return r;
}

static int qeq(zind_qty a, int64_t n, int64_t d) {
    zind_qty b = q(n, d);
    return a.num == b.num && a.den == b.den;
}

#define NN 6
static const char *k_ids[NN] = {"N1", "N2", "N3", "N4", "N5", "N6"};

typedef struct {
    zt_env e;
    uint8_t owner_sk[32], owner_pk[32];
    nbk_t nb[NN], rogue;
    zxv_hexlog *qlog;
    ztm_planner *p;
} fx_t;

static int fx_open(fx_t *f) {
    memset(f, 0, sizeof *f);
    if (zt_env_open(&f->e, NULL) != 0) return -1;
    zt_seed(f->owner_sk, 0xEE);
    if (zxv_ed25519_public(f->e.cap, f->owner_sk, f->owner_pk) != ZXV_OK) return -1;
    if (zxv_hexlog_create(f->e.cap, 4096, &f->qlog) != ZXV_OK) return -1;
    if (ztm_planner_create(f->e.cap, "HOME", f->owner_pk, f->qlog, &f->p) != ZXV_OK) return -1;
    for (int i = 0; i < NN; i++) {
        size_t pl = 0;
        const uint8_t *pk;
        if (nbk_make(f->e.cap, &f->nb[i], k_ids[i], i % 2 ? ZTM_ALG_MLDSA87 : ZTM_ALG_ED25519,
                     (uint8_t) (20 + i)) != 0)
            return -1;
        pk = nbk_pk(&f->nb[i], &pl);
        if (ztm_neighbour_add(f->p, k_ids[i], f->nb[i].alg, pk, pl) != ZXV_OK) return -1;
    }
    return nbk_make(f->e.cap, &f->rogue, "N1", ZTM_ALG_ED25519, 99);
}

static void fx_close(fx_t *f) {
    ztm_planner_destroy(f->p);
    zxv_hexlog_destroy(f->qlog);
    for (int i = 0; i < NN; i++) zind_signer_destroy(f->nb[i].ml);
    zt_env_close(&f->e);
}

static int32_t publish(fx_t *f, int i, const ztm_advert *a, uint64_t now) {
    uint8_t buf[ZTM_ADVERT_MAX];
    size_t len = seal(f->e.cap, &f->nb[i], a, buf, sizeof buf);
    if (!len) return -999;
    return ztm_advert_accept(f->p, buf, len, now);
}

static uint32_t qcount(fx_t *f) {
    uint32_t n = 0;
    zxv_hexlog_count(f->qlog, &n);
    return n;
}

static int last_reason(fx_t *f) {
    uint8_t r = 0;
    uint32_t n = qcount(f);
    if (!n) return -1;
    zxv_hexlog_at(f->qlog, n - 1, NULL, NULL, &r, NULL, NULL, NULL, NULL);
    return r;
}

static void test_setup(void) {
    zt_env e;
    uint8_t opk[32] = {1};
    ztm_planner *p = NULL;
    zxv_cap *weak = NULL;
    CHECK(zt_env_open(&e, NULL) == 0, "env");
    CHECK_RC(ztm_planner_create(e.cap, "", opk, NULL, &p), ZXV_E_ARG);
    CHECK_RC(ztm_planner_create(e.cap, "has space", opk, NULL, &p), ZXV_E_ARG);
    CHECK_RC(ztm_planner_create(e.cap, "HOME", NULL, NULL, &p), ZXV_E_ARG);
    CHECK_RC(ztm_planner_create(NULL, "HOME", opk, NULL, &p), ZXV_E_ARG);
    CHECK_RC(zxv_cap_attenuate(e.cap, ZXV_CAP_CRYPTO_SIGN, &weak), ZXV_OK);
    CHECK_RC(ztm_planner_create(weak, "HOME", opk, NULL, &p), ZXV_E_CAP); /* no BUDGET */
    zxv_cap_destroy(weak);
    CHECK_RC(zxv_cap_attenuate(e.cap, ZXV_CAP_BUDGET, &weak), ZXV_OK);
    CHECK_RC(ztm_planner_create(weak, "HOME", opk, NULL, &p), ZXV_E_CAP); /* no CRYPTO_SIGN */
    zxv_cap_destroy(weak);
    CHECK_RC(ztm_planner_create(e.cap, "HOME", opk, NULL, &p), ZXV_OK);
    {
        uint8_t pk[32] = {7}, big[ZIND_PK_BYTES] = {0};
        char id[24];
        CHECK_RC(ztm_neighbour_add(p, "N1", ZTM_ALG_ED25519, pk, 31), ZXV_E_ARG);
        CHECK_RC(ztm_neighbour_add(p, "N1", ZTM_ALG_MLDSA87, pk, 32), ZXV_E_ARG);
        CHECK_RC(ztm_neighbour_add(p, "N1", 3, pk, 32), ZXV_E_ARG);
        CHECK_RC(ztm_neighbour_add(p, "HOME", ZTM_ALG_ED25519, pk, 32), ZXV_E_ARG); /* itself */
        CHECK_RC(ztm_neighbour_add(p, "bad id", ZTM_ALG_ED25519, pk, 32), ZXV_E_ARG);
        CHECK_RC(ztm_neighbour_add(p, "N1", ZTM_ALG_ED25519, pk, 32), ZXV_OK);
        CHECK_RC(ztm_neighbour_add(p, "N1", ZTM_ALG_ED25519, opk, 32), ZXV_E_DUPLICATE);
        CHECK_RC(ztm_neighbour_add(p, "N2", ZTM_ALG_ED25519, pk, 32), ZXV_E_DUPLICATE);
        CHECK_RC(ztm_neighbour_add(p, "M1", ZTM_ALG_MLDSA87, big, sizeof big), ZXV_OK);
        for (int i = 0; i < 31; i++) {
            pk[1] = (uint8_t) (i + 1);
            snprintf(id, sizeof id, "X%d", i);
            CHECK_RC(ztm_neighbour_add(p, id, ZTM_ALG_ED25519, pk, 32),
                     i < 30 ? ZXV_OK : ZXV_E_CAPACITY);
        }
        CHECK_RC(ztm_agreement_set(p, "nobody", q(1, 1)), ZTM_E_NEIGHBOUR);
        CHECK_RC(ztm_agreement_set(p, "N1", q(-1, 1)), ZXV_E_ARG);
        CHECK_RC(ztm_agreement_set(p, "N1", (zind_qty){1, 0}), ZXV_E_ARG);
        CHECK_RC(ztm_agreement_set(p, "N1", q(5, 2)), ZXV_OK);
        CHECK_RC(ztm_agreement_set(p, "N1", ZIND_QTY_ZERO), ZXV_OK);
    }
    ztm_planner_destroy(p);
    zt_env_close(&e);
}

static void test_seal_parse(fx_t *f) {
    uint8_t buf[ZTM_ADVERT_MAX];
    size_t len;
    ztm_advert a = adv("N1", 1, T0, 1, 1000, 3, 12), b;
    int32_t alg = 0;
    claim(&a, "internet", ZXV_LP_TRUE);
    claim(&a, "dns", ZXV_LP_FALSE);
    for (int i = 0; i < 2; i++) {
        len = seal(f->e.cap, &f->nb[i], &a, buf, sizeof buf);
        CHECK(len > 0, "sealed with alg %d", f->nb[i].alg);
        CHECK_RC(ztm_advert_parse(buf, len, &b, &alg), ZXV_OK);
        CHECK(alg == f->nb[i].alg && strcmp(b.neighbour, "N1") == 0 && b.seq == 1 &&
                  b.expires == T0 + 600 && qeq(b.offer_kbps, 1000, 3) && b.n_claims == 2 &&
                  b.claims[1].lp == ZXV_LP_FALSE && strcmp(b.claims[0].name, "internet") == 0,
              "round trip, alg %d", alg);
    }
    /* every truncation of an Ed25519 advert is malformed */
    len = seal(f->e.cap, &f->nb[0], &a, buf, sizeof buf);
    for (size_t n = 0; n < len; n++) CHECK_RC(ztm_advert_parse(buf, n, &b, &alg), ZXV_E_FORMAT);
    {
        uint8_t big[ZTM_ADVERT_MAX + 1];
        memcpy(big, buf, len);
        big[len] = 0;
        CHECK_RC(ztm_advert_parse(big, len + 1, &b, &alg), ZXV_E_FORMAT);
        buf[0] = 'X';
        CHECK_RC(ztm_advert_parse(buf, len, &b, &alg), ZXV_E_FORMAT);
    }
    CHECK_RC(ztm_advert_seal_ed25519(f->e.cap, &a, f->nb[0].sk, buf, 10, &len), ZXV_E_BUFFER);
    CHECK_RC(ztm_advert_parse(NULL, 10, &b, &alg), ZXV_E_ARG);
    /* invalid fields are refused at sealing */
    {
        ztm_advert bad[12];
        for (int i = 0; i < 12; i++) bad[i] = a;
        bad[0].seq = 0;
        bad[1].expires = bad[1].issued;
        bad[2].expires = bad[2].issued + ZTM_LIFETIME_MAX + 1;
        bad[3].wan_up = 2;
        bad[4].medium = 5;
        bad[5].offer_kbps = q(-1, 1);
        bad[6].claims[0].lp = ZXV_LP_NEITHER;
        bad[7].claims[1].lp = ZXV_LP_BOTH;
        snprintf(bad[8].claims[0].name, sizeof bad[8].claims[0].name, "no spaces");
        bad[9].neighbour[0] = 0;
        bad[10].n_claims = ZTM_MAX_DESTS + 1;
        bad[11].offer_kbps = (zind_qty){2, 4}; /* not reduced */
        for (int i = 0; i < 12; i++) {
            CHECK_RC(ztm_advert_seal_ed25519(f->e.cap, &bad[i], f->nb[0].sk, buf, sizeof buf, &len),
                     ZXV_E_ARG);
            CHECK_RC(ztm_advert_seal_mldsa(f->e.cap, &bad[i], f->nb[1].ml, buf, sizeof buf, &len),
                     ZXV_E_ARG);
        }
        a.expires = a.issued + ZTM_LIFETIME_MAX; /* the limit itself is fine */
        CHECK(seal(f->e.cap, &f->nb[0], &a, buf, sizeof buf) > 0, "a one-day advert");
    }
}

static void test_accept(fx_t *f) {
    uint8_t buf[ZTM_ADVERT_MAX];
    size_t len;
    ztm_advert a = adv("N1", 5, T0, 1, 100, 1, 10);
    ztm_view v;
    uint32_t q0;
    int all = 1;
    claim(&a, "internet", ZXV_LP_TRUE);
    len = seal(f->e.cap, &f->nb[0], &a, buf, sizeof buf);
    /* every single-byte corruption is refused, nothing is taken */
    for (size_t i = 0; i < len; i++) {
        int32_t st;
        buf[i] ^= 0x01;
        st = ztm_advert_accept(f->p, buf, len, T0);
        buf[i] ^= 0x01;
        if (st == ZXV_OK || st == ZIND_E_HELD) all = 0;
        CHECK(st == ZXV_E_VERIFY || st == ZXV_E_FORMAT || st == ZTM_E_NEIGHBOUR,
              "byte %zu flipped -> %s", i, ztm_status_name(st));
    }
    CHECK(all, "no corrupted advert accepted");
    CHECK_RC(ztm_neighbour_view(f->p, "N1", "internet", T0, &v), ZXV_OK);
    CHECK(v.advert_seq == 0 && v.why == ZTM_E_EXPIRED, "nothing taken from corrupted bytes");
    CHECK_RC(ztm_advert_accept(f->p, buf, len, T0), ZXV_OK);
    q0 = qcount(f);
    CHECK_RC(ztm_advert_accept(f->p, buf, len, T0), ZIND_E_REPLAY);
    CHECK(qcount(f) == q0 + 1 && last_reason(f) == ZIND_REASON_REPLAY, "replay logged");
    a.seq = 4;
    CHECK_RC(publish(f, 0, &a, T0), ZIND_E_REPLAY); /* older */
    a.seq = 5;
    a.latency_ms = 11; /* same seq, different content */
    CHECK_RC(publish(f, 0, &a, T0), ZIND_E_REPLAY);
    a.seq = 6;
    CHECK_RC(publish(f, 0, &a, T0 + 601), ZTM_E_EXPIRED);
    CHECK_RC(publish(f, 0, &a, T0 - ZTM_SKEW_S - 1), ZTM_E_EXPIRED); /* dated ahead */
    CHECK_RC(publish(f, 0, &a, T0 - ZTM_SKEW_S), ZXV_OK);            /* within the skew */
    a.seq = 7;
    CHECK_RC(publish(f, 0, &a, T0 + 600), ZXV_OK); /* the last second */
    CHECK_RC(ztm_neighbour_view(f->p, "N1", "internet", T0 + 600, &v), ZXV_OK);
    CHECK(v.advert_seq == 7, "seq 7 current");
    /* forged: N1's name, the rogue's key */
    a.seq = 8;
    len = seal(f->e.cap, &f->rogue, &a, buf, sizeof buf);
    q0 = qcount(f);
    CHECK_RC(ztm_advert_accept(f->p, buf, len, T0), ZXV_E_VERIFY);
    CHECK(qcount(f) == q0 + 1 && last_reason(f) == ZIND_REASON_TAMPER, "forgery logged");
    /* N2 is ML-DSA: an Ed25519 advert in its name is refused */
    {
        ztm_advert a2 = adv("N2", 1, T0, 1, 50, 1, 5);
        nbk_t k = f->nb[0];
        len = seal(f->e.cap, &k, &a2, buf, sizeof buf);
        CHECK_RC(ztm_advert_accept(f->p, buf, len, T0), ZXV_E_VERIFY);
        CHECK_RC(publish(f, 1, &a2, T0), ZXV_OK);
    }
    /* amateur radio: refused, nothing taken */
    a.seq = 9;
    a.medium = ZTM_MEDIUM_AMATEUR;
    CHECK_RC(publish(f, 0, &a, T0), ZTM_E_MEDIUM);
    CHECK_RC(ztm_neighbour_view(f->p, "N1", "internet", T0, &v), ZXV_OK);
    CHECK(v.advert_seq == 7, "the amateur advert was not taken");
    a.medium = ZTM_MEDIUM_WIFI;
    CHECK_RC(publish(f, 0, &a, T0), ZXV_OK); /* seq 9 was not consumed */
    {
        ztm_advert a3 = adv("ZZ", 1, T0, 1, 50, 1, 5);
        nbk_t k = f->nb[0];
        snprintf(k.id, sizeof k.id, "ZZ");
        len = seal(f->e.cap, &k, &a3, buf, sizeof buf);
        CHECK_RC(ztm_advert_accept(f->p, buf, len, T0), ZTM_E_NEIGHBOUR);
    }
    CHECK_RC(ztm_advert_accept(NULL, buf, len, T0), ZXV_E_ARG);
}

static void test_evidence(fx_t *f) {
    ztm_advert a = adv("N3", 1, T0, 1, 80, 1, 10);
    ztm_view v;
    uint8_t msg[200], sig[64];
    size_t ml = 0;
    uint32_t q0;
    CHECK_RC(ztm_agreement_set(f->p, "N3", q(1000, 1)), ZXV_OK);
    /* one advert claiming a destination both ways */
    claim(&a, "internet", ZXV_LP_TRUE);
    claim(&a, "voip", ZXV_LP_TRUE);
    claim(&a, "voip", ZXV_LP_FALSE);
    q0 = qcount(f);
    CHECK_RC(publish(f, 2, &a, T0), ZIND_E_HELD);
    CHECK(qcount(f) == q0 + 1 && last_reason(f) == ZIND_REASON_HELD_BOTH, "hold logged");
    CHECK_RC(ztm_neighbour_view(f->p, "N3", "voip", T0, &v), ZXV_OK);
    CHECK(v.held && v.lp == ZXV_LP_BOTH && !v.eligible && v.why == ZIND_E_HELD, "voip held");
    CHECK_RC(ztm_neighbour_view(f->p, "N3", "internet", T0, &v), ZXV_OK);
    CHECK(!v.held && v.lp == ZXV_LP_TRUE && v.eligible && qeq(v.budget_kbps, 80, 1),
          "internet still fine");
    /* a probe that agrees changes nothing; one that contradicts holds */
    CHECK_RC(ztm_probe_record(f->p, "N3", "internet", ZXV_LP_TRUE, T0 + 1), ZXV_OK);
    q0 = qcount(f);
    CHECK_RC(ztm_probe_record(f->p, "N3", "internet", ZXV_LP_FALSE, T0 + 2), ZIND_E_HELD);
    CHECK(qcount(f) == q0 + 1, "logged once");
    CHECK_RC(ztm_probe_record(f->p, "N3", "internet", ZXV_LP_TRUE, T0 + 3), ZIND_E_HELD);
    CHECK(qcount(f) == q0 + 1, "not re-logged while held");
    CHECK_RC(ztm_neighbour_view(f->p, "N3", "internet", T0 + 3, &v), ZXV_OK);
    CHECK(v.held && !v.eligible, "internet held");
    /* the hold survives a new advert: only the owner resolves it */
    a.seq = 2;
    a.n_claims = 1;
    CHECK_RC(publish(f, 2, &a, T0 + 4), ZXV_OK);
    CHECK_RC(ztm_neighbour_view(f->p, "N3", "internet", T0 + 4, &v), ZXV_OK);
    CHECK(v.held, "still held after a fresh advert");
    CHECK_RC(ztm_neighbour_view(f->p, "N3", "voip", T0 + 4, &v), ZXV_OK);
    CHECK(v.held, "voip still held");
    /* owner messages and resolution */
    CHECK_RC(ztm_owner_message(f->p, "N3", "dns", ZTM_USE, msg, sizeof msg, &ml), ZXV_E_STATE);
    CHECK_RC(ztm_owner_message(f->p, "N3", "internet", 3, msg, sizeof msg, &ml), ZXV_E_ARG);
    CHECK_RC(ztm_owner_message(f->p, "N9", "internet", ZTM_USE, msg, sizeof msg, &ml),
             ZTM_E_NEIGHBOUR);
    CHECK_RC(ztm_owner_message(f->p, "N3", "internet", ZTM_USE, msg, 8, &ml), ZXV_E_BUFFER);
    CHECK_RC(ztm_owner_message(f->p, "N3", "internet", ZTM_AVOID, msg, sizeof msg, &ml), ZXV_OK);
    CHECK_RC(zxv_ed25519_sign(f->e.cap, f->nb[0].sk, msg, ml, sig), ZXV_OK); /* not the owner */
    q0 = qcount(f);
    CHECK_RC(ztm_hold_resolve(f->p, "N3", "internet", ZTM_AVOID, sig), ZTM_E_OWNER);
    CHECK(qcount(f) == q0 + 1 && last_reason(f) == ZIND_REASON_TAMPER, "forged resolution logged");
    CHECK_RC(zxv_ed25519_sign(f->e.cap, f->owner_sk, msg, ml, sig), ZXV_OK);
    CHECK_RC(ztm_hold_resolve(f->p, "N3", "internet", ZTM_USE, sig), ZTM_E_OWNER); /* signed AVOID */
    CHECK_RC(ztm_hold_resolve(f->p, "N3", "internet", ZTM_AVOID, sig), ZXV_OK);
    CHECK_RC(ztm_hold_resolve(f->p, "N3", "internet", ZTM_AVOID, sig), ZXV_E_STATE);
    CHECK_RC(ztm_neighbour_view(f->p, "N3", "internet", T0 + 5, &v), ZXV_OK);
    CHECK(!v.held && v.lp == ZXV_LP_FALSE && v.why == ZXV_E_NOT_FOUND, "AVOID: not used");
    /* USE for voip */
    CHECK_RC(ztm_owner_message(f->p, "N3", "voip", ZTM_USE, msg, sizeof msg, &ml), ZXV_OK);
    CHECK_RC(zxv_ed25519_sign(f->e.cap, f->owner_sk, msg, ml, sig), ZXV_OK);
    CHECK_RC(ztm_hold_resolve(f->p, "N3", "voip", ZTM_USE, sig), ZXV_OK);
    CHECK_RC(ztm_neighbour_view(f->p, "N3", "voip", T0 + 5, &v), ZXV_OK);
    CHECK(v.eligible && v.lp == ZXV_LP_TRUE, "USE: voip via N3");
    /* the override lasts until the next advert */
    a.seq = 3;
    CHECK_RC(publish(f, 2, &a, T0 + 6), ZXV_OK);
    CHECK_RC(ztm_neighbour_view(f->p, "N3", "voip", T0 + 6, &v), ZXV_OK);
    CHECK(!v.eligible && v.lp == ZXV_LP_NEITHER, "voip unclaimed in seq 3");
    CHECK_RC(ztm_neighbour_view(f->p, "N3", "internet", T0 + 6, &v), ZXV_OK);
    CHECK(v.eligible && v.lp == ZXV_LP_TRUE, "internet back from the advert");
    /* a stale probe is no evidence */
    CHECK_RC(ztm_probe_record(f->p, "N3", "dns", ZXV_LP_TRUE, T0 + 6), ZXV_OK);
    CHECK_RC(ztm_neighbour_view(f->p, "N3", "dns", T0 + 6, &v), ZXV_OK);
    CHECK(v.lp == ZXV_LP_TRUE && v.eligible, "dns via probe");
    CHECK_RC(ztm_neighbour_view(f->p, "N3", "dns", T0 + 6 + ZTM_PROBE_TTL_S + 1, &v), ZXV_OK);
    CHECK(v.lp == ZXV_LP_NEITHER && !v.eligible, "stale probe");
    a.seq = 4;
    claim(&a, "dns", ZXV_LP_FALSE);
    CHECK_RC(publish(f, 2, &a, T0 + 6 + ZTM_PROBE_TTL_S + 1), ZXV_OK); /* stale: no conflict */
    CHECK_RC(publish(f, 2, &a, T0 + 7), ZIND_E_REPLAY);
    a.seq = 5;
    CHECK_RC(ztm_probe_record(f->p, "N3", "dns", ZXV_LP_TRUE, T0 + 7), ZIND_E_HELD);
    /* probe refusals */
    CHECK_RC(ztm_probe_record(f->p, "N3", "dns", ZXV_LP_BOTH, T0), ZXV_E_ARG);
    CHECK_RC(ztm_probe_record(f->p, "N3", "", ZXV_LP_TRUE, T0), ZXV_E_ARG);
    CHECK_RC(ztm_probe_record(f->p, "NX", "dns", ZXV_LP_TRUE, T0), ZTM_E_NEIGHBOUR);
    {
        char d[16];
        int32_t st = ZXV_OK;
        for (int i = 0; i < 20 && st != ZXV_E_CAPACITY; i++) {
            snprintf(d, sizeof d, "d%d", i);
            st = ztm_probe_record(f->p, "N4", d, ZXV_LP_TRUE, T0);
        }
        CHECK(st == ZXV_E_CAPACITY, "evidence table is bounded");
    }
    CHECK_RC(ztm_neighbour_view(f->p, "NX", "dns", T0, &v), ZTM_E_NEIGHBOUR);
}

/* max-min fairness and budgets of one plan, against the views */
static int plan_ok(fx_t *f, const ztm_planner *p, const ztm_demand *d, uint32_t n,
                   const ztm_plan *pl, const char *dest, uint64_t now) {
    (void) f;
    for (uint32_t i = 0; i < pl->n; i++) {
        ztm_view v;
        int32_t c = 0;
        if (strcmp(pl->a[i].flow, d[i].flow) != 0) return 0;
        if (ztm_neighbour_view(p, pl->a[i].relay, dest, now, &v) != ZXV_OK || !v.eligible) return 0;
        if (zind_qty_sign(pl->a[i].alloc_kbps) <= 0) return 0;
        if (zind_qty_cmp(pl->a[i].alloc_kbps, d[i].demand_kbps, &c) != ZXV_OK || c > 0) return 0;
    }
    for (uint32_t i = 0; i < pl->n; i++) {
        ztm_view v;
        zind_qty sum = ZIND_QTY_ZERO, maxa = ZIND_QTY_ZERO, rem, want;
        int limited = 0;
        int32_t c = 0;
        ztm_neighbour_view(p, pl->a[i].relay, dest, now, &v);
        for (uint32_t j = 0; j < pl->n; j++) {
            if (strcmp(pl->a[j].relay, pl->a[i].relay) != 0) continue;
            if (zind_qty_add(sum, pl->a[j].alloc_kbps, &sum) != ZXV_OK) return 0;
            zind_qty_cmp(pl->a[j].alloc_kbps, maxa, &c);
            if (c > 0) maxa = pl->a[j].alloc_kbps;
        }
        zind_qty_cmp(sum, v.budget_kbps, &c);
        if (c > 0) return 0; /* over budget */
        for (uint32_t j = 0; j < pl->n; j++) {
            if (strcmp(pl->a[j].relay, pl->a[i].relay) != 0) continue;
            zind_qty_cmp(pl->a[j].alloc_kbps, d[j].demand_kbps, &c);
            if (c < 0) {
                limited = 1;
                /* a limited flow gets the largest share on its relay */
                zind_qty_cmp(pl->a[j].alloc_kbps, maxa, &c);
                if (c != 0) return 0;
            }
        }
        if (limited) {
            zind_qty_cmp(sum, v.budget_kbps, &c);
            if (c != 0) return 0; /* a limited relay is used up exactly */
        }
        if (ztm_budget_remaining(p, pl->a[i].relay, &rem) != ZXV_OK) return 0;
        if (zind_qty_sub(v.budget_kbps, sum, &want) != ZXV_OK) return 0;
        zind_qty_cmp(rem, want, &c);
        if (c != 0) return 0; /* rmag charged exactly */
    }
    (void) n;
    return 1;
}

static void test_plan(void) {
    fx_t f;
    ztm_plan pl;
    ztm_demand d[4] = {{"A", {10, 1}}, {"B", {50, 1}}, {"C", {80, 1}}, {"D", {5, 1}}};
    ztm_advert a;
    zind_qty rem;
    uint8_t base = 0, pol = 0;
    CHECK(fx_open(&f) == 0, "fixture");
    CHECK_RC(ztm_plan_make(f.p, "internet", T0, 0, d, 4, &pl), ZTM_E_NO_RELAY);
    CHECK(pl.n == 0 && pl.eligible == 0, "empty plan");
    /* N1 offers 100 under an agreement of 60, N2 offers 30 */
    a = adv("N1", 1, T0, 1, 100, 1, 20);
    claim(&a, "internet", ZXV_LP_TRUE);
    CHECK_RC(publish(&f, 0, &a, T0), ZXV_OK);
    a = adv("N2", 1, T0, 1, 30, 1, 10);
    claim(&a, "internet", ZXV_LP_TRUE);
    CHECK_RC(publish(&f, 1, &a, T0), ZXV_OK);
    CHECK_RC(ztm_plan_make(f.p, "internet", T0, 0, d, 4, &pl), ZTM_E_NO_RELAY); /* no agreements */
    CHECK_RC(ztm_agreement_set(f.p, "N1", q(60, 1)), ZXV_OK);
    CHECK_RC(ztm_agreement_set(f.p, "N2", q(1000, 1)), ZXV_OK);
    CHECK_RC(ztm_plan_make(f.p, "internet", T0, 1, d, 4, &pl), ZTM_E_WAN_UP);
    CHECK_RC(ztm_plan_make(f.p, "internet", T0, 0, d, 4, &pl), ZXV_OK);
    CHECK(pl.n == 4 && pl.eligible == 2, "planned");
    CHECK(strcmp(pl.a[3].relay, "N1") == 0 && qeq(pl.a[3].alloc_kbps, 5, 1), "D: N1, 5");
    CHECK(strcmp(pl.a[0].relay, "N2") == 0 && qeq(pl.a[0].alloc_kbps, 10, 1),
          "A: N2 (10 either way, equal fair share 30, lower latency), 10");
    CHECK(strcmp(pl.a[1].relay, "N1") == 0 && qeq(pl.a[1].alloc_kbps, 55, 2), "B: N1, 55/2");
    CHECK(strcmp(pl.a[2].relay, "N1") == 0 && qeq(pl.a[2].alloc_kbps, 55, 2), "C: N1, 55/2");
    CHECK(pl.satisfied == 2, "two flows satisfied");
    CHECK(zxv_hex_unpack(pl.tag, &base, &pol, NULL) == ZXV_OK && base == ZXV_HEX_BASE_TRUE &&
              pol == ZXV_HEX_POL_ENTROPIC,
          "partial plan = true/entropic");
    CHECK_RC(ztm_budget_remaining(f.p, "N1", &rem), ZXV_OK);
    CHECK(qeq(rem, 0, 1), "N1 used up exactly");
    CHECK_RC(ztm_budget_remaining(f.p, "N2", &rem), ZXV_OK);
    CHECK(qeq(rem, 20, 1), "N2 has 20 left");
    CHECK(plan_ok(&f, f.p, d, 4, &pl, "internet", T0), "fair and within budget");
    /* a smaller demand set is fully satisfied; the old charges were released */
    CHECK_RC(ztm_plan_make(f.p, "internet", T0, 0, d, 2, &pl), ZXV_OK);
    CHECK(pl.satisfied == 2 && zxv_hex_unpack(pl.tag, &base, &pol, NULL) == ZXV_OK &&
              pol == ZXV_HEX_POL_NEUTRAL,
          "all satisfied = true/neutral");
    CHECK(strcmp(pl.a[1].relay, "N1") == 0, "B (50) goes where 50 fits, not to N2 (30)");
    CHECK_RC(ztm_budget_remaining(f.p, "N1", &rem), ZXV_OK);
    CHECK(qeq(rem, 0, 1), "N1: 60 - 10 - 50");
    CHECK_RC(ztm_budget_remaining(f.p, "N2", &rem), ZXV_OK);
    CHECK(qeq(rem, 30, 1), "N2 untouched, its old charge released");
    CHECK(plan_ok(&f, f.p, d, 2, &pl, "internet", T0), "fair");
    /* thirds: exact rationals */
    {
        ztm_demand t[3] = {{"x", {100, 1}}, {"y", {100, 1}}, {"z", {100, 1}}};
        CHECK_RC(ztm_agreement_set(f.p, "N2", ZIND_QTY_ZERO), ZXV_OK);
        CHECK_RC(ztm_plan_make(f.p, "internet", T0, 0, t, 3, &pl), ZXV_OK);
        CHECK(qeq(pl.a[0].alloc_kbps, 20, 1) && qeq(pl.a[2].alloc_kbps, 20, 1), "60/3 each");
        CHECK_RC(ztm_agreement_set(f.p, "N1", q(100, 1)), ZXV_OK);
        CHECK_RC(ztm_plan_make(f.p, "internet", T0, 0, t, 3, &pl), ZXV_OK);
        CHECK(qeq(pl.a[0].alloc_kbps, 100, 3) && qeq(pl.a[1].alloc_kbps, 100, 3) &&
                  qeq(pl.a[2].alloc_kbps, 100, 3),
              "100/3 each, exactly");
        CHECK_RC(ztm_budget_remaining(f.p, "N1", &rem), ZXV_OK);
        CHECK(qeq(rem, 0, 1), "exactly used");
        CHECK(plan_ok(&f, f.p, t, 3, &pl, "internet", T0), "fair");
    }
    /* every way a neighbour drops out */
    {
        ztm_view v;
        CHECK_RC(ztm_plan_make(f.p, "internet", T0 + 601, 0, d, 4, &pl), ZTM_E_NO_RELAY);
        CHECK_RC(ztm_neighbour_view(f.p, "N1", "internet", T0 + 601, &v), ZXV_OK);
        CHECK(v.why == ZTM_E_EXPIRED, "expired");
        a = adv("N1", 2, T0, 0, 100, 1, 20); /* its WAN went down */
        claim(&a, "internet", ZXV_LP_TRUE);
        CHECK_RC(publish(&f, 0, &a, T0), ZXV_OK);
        CHECK_RC(ztm_neighbour_view(f.p, "N1", "internet", T0, &v), ZXV_OK);
        CHECK(v.why == ZTM_E_NO_RELAY, "WAN down");
        a = adv("N1", 3, T0, 1, 0, 1, 20); /* offers nothing */
        claim(&a, "internet", ZXV_LP_TRUE);
        CHECK_RC(publish(&f, 0, &a, T0), ZXV_OK);
        CHECK_RC(ztm_neighbour_view(f.p, "N1", "internet", T0, &v), ZXV_OK);
        CHECK(v.why == ZTM_E_NO_RELAY, "zero offer");
        a = adv("N1", 4, T0, 1, 100, 1, 20);
        claim(&a, "internet", ZXV_LP_FALSE);
        CHECK_RC(publish(&f, 0, &a, T0), ZXV_OK);
        CHECK_RC(ztm_neighbour_view(f.p, "N1", "internet", T0, &v), ZXV_OK);
        CHECK(v.why == ZXV_E_NOT_FOUND, "cannot reach");
        CHECK_RC(ztm_neighbour_view(f.p, "N1", "elsewhere", T0, &v), ZXV_OK);
        CHECK(v.why == ZXV_E_NOT_FOUND && v.lp == ZXV_LP_NEITHER, "unknown destination");
        CHECK_RC(ztm_agreement_set(f.p, "N1", ZIND_QTY_ZERO), ZXV_OK);
        CHECK_RC(ztm_neighbour_view(f.p, "N1", "internet", T0, &v), ZXV_OK);
        CHECK(v.why == ZTM_E_AGREEMENT, "agreement withdrawn");
        CHECK_RC(ztm_plan_make(f.p, "internet", T0, 0, d, 4, &pl), ZTM_E_NO_RELAY);
        CHECK(zxv_hex_unpack(pl.tag, &base, NULL, NULL) == ZXV_OK && base == ZXV_HEX_BASE_FALSE,
              "no relay = FALSE");
    }
    /* demand refusals */
    {
        ztm_demand bad[2] = {{"A", {10, 1}}, {"A", {5, 1}}};
        CHECK_RC(ztm_plan_make(f.p, "internet", T0, 0, bad, 2, &pl), ZXV_E_DUPLICATE);
        bad[1].demand_kbps = ZIND_QTY_ZERO;
        snprintf(bad[1].flow, sizeof bad[1].flow, "B");
        CHECK_RC(ztm_plan_make(f.p, "internet", T0, 0, bad, 2, &pl), ZXV_E_ARG);
        bad[1].demand_kbps = (zind_qty){-1, 1};
        CHECK_RC(ztm_plan_make(f.p, "internet", T0, 0, bad, 2, &pl), ZXV_E_ARG);
        CHECK_RC(ztm_plan_make(f.p, "bad dest", T0, 0, bad, 1, &pl), ZXV_E_ARG);
        CHECK_RC(ztm_plan_make(f.p, "internet", T0, 0, NULL, 1, &pl), ZXV_E_ARG);
        CHECK_RC(ztm_plan_make(f.p, "internet", T0, 0, bad, ZTM_MAX_FLOWS + 1, &pl), ZXV_E_ARG);
        CHECK_RC(ztm_budget_remaining(f.p, "nobody", &rem), ZTM_E_NEIGHBOUR);
    }
    fx_close(&f);
}

/* A street of routers. Each has its own planner with every other router as
 * a neighbour; each tick some WANs go down or come back, every router
 * publishes, and every router whose WAN is down plans. */
#define NR 7
static void test_street(void) {
    zt_env e;
    nbk_t k[NR];
    ztm_planner *p[NR];
    uint8_t owner_sk[32], owner_pk[32];
    char ids[NR][16];
    int wan[NR], plans = 0, fair = 1, never_down = 1, never_self = 1;
    uint64_t s = 0x57EE7ULL, seqn = 0;
    CHECK(zt_env_open(&e, NULL) == 0, "env");
    zt_seed(owner_sk, 0x0E);
    zxv_ed25519_public(e.cap, owner_sk, owner_pk);
    for (int i = 0; i < NR; i++) {
        snprintf(ids[i], sizeof ids[i], "H%d", i);
        CHECK(nbk_make(e.cap, &k[i], ids[i], i == 3 ? ZTM_ALG_MLDSA87 : ZTM_ALG_ED25519,
                       (uint8_t) (60 + i)) == 0,
              "keys %d", i);
        CHECK_RC(ztm_planner_create(e.cap, ids[i], owner_pk, NULL, &p[i]), ZXV_OK);
        wan[i] = 1;
    }
    for (int i = 0; i < NR; i++)
        for (int j = 0; j < NR; j++) {
            size_t pl = 0;
            const uint8_t *pk;
            if (i == j) continue;
            pk = nbk_pk(&k[j], &pl);
            CHECK_RC(ztm_neighbour_add(p[i], ids[j], k[j].alg, pk, pl), ZXV_OK);
            CHECK_RC(ztm_agreement_set(p[i], ids[j], q((int64_t) (5 + (i * 7 + j) % 11) * 1000, 3)),
                     ZXV_OK);
        }
    for (int tick = 0; tick < 40; tick++) {
        uint64_t now = T0 + (uint64_t) tick * 60, r;
        for (int i = 0; i < NR; i++) {
            r = zt_rand(&s);
            if (r % 5 == 0) wan[i] = !wan[i];
        }
        seqn++;
        for (int j = 0; j < NR; j++) {
            ztm_advert a;
            uint8_t buf[ZTM_ADVERT_MAX];
            size_t len;
            r = zt_rand(&s);
            a = adv(ids[j], seqn, now, wan[j], (int64_t) (r % 40000) + 1, 1 + (int64_t) (r % 3),
                    (uint32_t) (r % 50));
            claim(&a, "internet", wan[j] ? ZXV_LP_TRUE : ZXV_LP_FALSE);
            len = seal(e.cap, &k[j], &a, buf, sizeof buf);
            for (int i = 0; i < NR; i++) {
                if (i == j) continue;
                CHECK_RC(ztm_advert_accept(p[i], buf, len, now), ZXV_OK);
                if (tick % 8 == 0) CHECK_RC(ztm_advert_accept(p[i], buf, len, now), ZIND_E_REPLAY);
            }
        }
        for (int i = 0; i < NR; i++) {
            ztm_demand d[6];
            ztm_plan pl;
            int32_t st;
            int nd;
            r = zt_rand(&s);
            nd = 1 + (int) (r % 6);
            for (int f = 0; f < nd; f++) {
                r = zt_rand(&s);
                snprintf(d[f].flow, sizeof d[f].flow, "f%d", f);
                d[f].demand_kbps = q((int64_t) (r % 9000) + 1, 1 + (int64_t) (r % 4));
            }
            st = ztm_plan_make(p[i], "internet", now, wan[i], d, (uint32_t) nd, &pl);
            if (wan[i]) {
                CHECK(st == ZTM_E_WAN_UP, "WAN up: no plan");
                continue;
            }
            {
                int any = 0;
                for (int j = 0; j < NR; j++) any |= j != i && wan[j];
                CHECK(st == (any ? ZXV_OK : ZTM_E_NO_RELAY), "tick %d router %d -> %s", tick, i,
                      ztm_status_name(st));
            }
            if (st != ZXV_OK) continue;
            plans++;
            for (uint32_t a = 0; a < pl.n; a++) {
                int j = pl.a[a].relay[1] - '0';
                if (!wan[j]) never_down = 0;
                if (j == i) never_self = 0;
            }
            if (!plan_ok(NULL, p[i], d, (uint32_t) nd, &pl, "internet", now)) fair = 0;
            CHECK(fair && never_down && never_self, "tick %d router %d plan sound", tick, i);
        }
    }
    CHECK(plans > 20 && fair && never_down && never_self,
          "%d fallback plans: none through a router whose WAN is down, all fair", plans);
    for (int i = 0; i < NR; i++) {
        ztm_planner_destroy(p[i]);
        zind_signer_destroy(k[i].ml);
    }
    zt_env_close(&e);
}

int main(void) {
    fx_t f;
    test_setup();
    CHECK(fx_open(&f) == 0, "fixture");
    test_seal_parse(&f);
    test_accept(&f);
    test_evidence(&f);
    fx_close(&f);
    test_plan();
    test_street();
    CHECK(strcmp(ztm_status_name(ZTM_E_MEDIUM), "ZTM_E_MEDIUM") == 0, "status names");
    CHECK(strcmp(ztm_status_name(ZIND_E_REPLAY), "ZIND_E_REPLAY") == 0, "falls back");
    return zt_report("test_modem");
}
