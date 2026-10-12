/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_command.c — mesh-command: report validation at every bound, the
 * wire format (a builder written here from the documented layout must
 * produce the signed bytes exactly), setup refusals and capacities,
 * ingest (replays, equivocation, other incidents, unknown units, other
 * keys, flipped bits, truncation, stale reports), the picture's states and
 * tags, holds, commanders' decisions (forged, stale, wrong value, unknown
 * commander, without ATTEST, genuine) and their quarantine-log entries,
 * advisory recommendations and signed approvals (blocked, forged, stale,
 * genuine), and offline journal verification (genuine, reordered trust,
 * missing keys, flipped bits, truncation, and journals re-signed by the
 * operator with records dropped, repeated or reordered). */
#include "zind_test.h"
#include "zmcommand.h"

#undef CHECK_RC
#define CHECK_RC(expr, want)                                                                   \
    do {                                                                                       \
        int32_t rc_ = (expr);                                                                  \
        CHECK(rc_ == (want), "%s -> %d (%s), want %d (%s)", #expr, (int) rc_,                  \
              zmc_status_name(rc_), (int) (want), zmc_status_name(want));                     \
    } while (0)

#define RB 5000 /* a signed report is at most 233 + 4627 bytes */

enum { U_E7, U_L12, U_M3, U_D1, NU };
static const char *k_unit[NU] = {"ENGINE-7", "LADDER-12", "MEDIC-3", "DRONE-1"};
static const char *INC = "WILDFIRE-0817";

typedef struct {
    zt_env e;
    zind_signer *op, *ic, *ops, *rogue, *key[NU];
    zxv_hexlog *qlog;
    zmc_incident *inc;
    uint64_t next_no[NU];
} fx_t;

static int fx_open(fx_t *f) {
    memset(f, 0, sizeof *f);
    if (zt_env_open(&f->e, NULL) != 0) return -1;
    f->op = zt_signer(f->e.cap, 1);
    f->ic = zt_signer(f->e.cap, 2);
    f->rogue = zt_signer(f->e.cap, 3);
    f->ops = zt_signer(f->e.cap, 4);
    if (!f->op || !f->ic || !f->rogue || !f->ops) return -1;
    if (zxv_hexlog_create(f->e.cap, 4096, &f->qlog) != ZXV_OK) return -1;
    if (zmc_incident_create(f->e.cap, INC, f->op, f->qlog, &f->inc) != ZXV_OK) return -1;
    for (int i = 0; i < NU; i++) {
        f->key[i] = zt_signer(f->e.cap, (uint8_t) (10 + i));
        if (!f->key[i]) return -1;
        if (zmc_unit_add(f->inc, k_unit[i], zind_signer_pk(f->key[i])) != ZXV_OK) return -1;
        f->next_no[i] = 1;
    }
    if (zmc_commander_add(f->inc, "IC-RIVERA", zind_signer_pk(f->ic)) != ZXV_OK) return -1;
    if (zmc_commander_add(f->inc, "OPS-CHIEF", zind_signer_pk(f->ops)) != ZXV_OK) return -1;
    return 0;
}

static void fx_close(fx_t *f) {
    for (int i = 0; i < NU; i++) zind_signer_destroy(f->key[i]);
    zmc_incident_destroy(f->inc);
    zxv_hexlog_destroy(f->qlog);
    zind_signer_destroy(f->op);
    zind_signer_destroy(f->ic);
    zind_signer_destroy(f->ops);
    zind_signer_destroy(f->rogue);
    zt_env_close(&f->e);
}

static zmc_report mk(const char *fact, int32_t claim, uint64_t t) {
    zmc_report r;
    memset(&r, 0, sizeof r);
    snprintf(r.fact, sizeof r.fact, "%s", fact);
    r.claim = claim;
    r.observed = t;
    r.lat_e6 = 38123456;
    r.lon_e6 = -122654321;
    snprintf(r.note, sizeof r.note, "seen from %s", fact);
    return r;
}

/* Sign report `no` of unit u (no == 0: the unit's next number). */
static size_t sign_as(fx_t *f, int u, uint64_t no, const char *fact, int32_t claim, uint8_t *buf) {
    zmc_report r;
    size_t len = 0;
    if (no == 0) no = f->next_no[u]++;
    r = mk(fact, claim, 1000 + no);
    if (zmc_report_sign(f->e.cap, f->key[u], INC, k_unit[u], no, &r, buf, RB, &len) != ZXV_OK)
        return 0;
    return len;
}

static int32_t report(fx_t *f, int u, const char *fact, int32_t claim, zmc_ingest_info *info) {
    uint8_t buf[RB];
    size_t len = sign_as(f, u, 0, fact, claim, buf);
    if (len == 0) return ZXV_E_INTERNAL;
    return zmc_ingest(f->inc, buf, len, info);
}

static int32_t lp_of(fx_t *f, const char *fact) {
    zmc_fact_view v;
    if (zmc_fact_state(f->inc, fact, &v) != ZXV_OK) return -1;
    return v.lp;
}

static uint8_t phase_of(uint8_t tag) {
    uint8_t b = 0, p = 0, ph = 0;
    if (zxv_hex_unpack(tag, &b, &p, &ph) != ZXV_OK) return 255;
    return ph;
}

static uint8_t pol_of(uint8_t tag) {
    uint8_t b = 0, p = 0, ph = 0;
    if (zxv_hex_unpack(tag, &b, &p, &ph) != ZXV_OK) return 255;
    return p;
}

static uint32_t qcount(fx_t *f) {
    uint32_t n = 0;
    zxv_hexlog_count(f->qlog, &n);
    return n;
}

/* Sign a decision as `who` (a signer) for commander name `cmd`. */
static int32_t decide(fx_t *f, const zind_signer *who, const char *fact, int32_t dec,
                      const char *cmd, const char *why, uint8_t sig[ZIND_SIG_BYTES]) {
    uint8_t msg[512];
    size_t len = 0;
    int32_t st = zmc_decision_message(f->inc, fact, dec, cmd, why, msg, sizeof msg, &len);
    if (st != ZXV_OK) return st;
    return zind_sign(f->e.cap, who, msg, len, ZMC_DECISION_CTX, sig);
}

static int32_t approve_sig(fx_t *f, const zind_signer *who, const char *task, const char *res,
                           const char *cmd, uint8_t sig[ZIND_SIG_BYTES]) {
    uint8_t msg[256];
    size_t len = 0;
    int32_t st = zmc_approve_message(f->inc, task, res, cmd, msg, sizeof msg, &len);
    if (st != ZXV_OK) return st;
    return zind_sign(f->e.cap, who, msg, len, ZMC_APPROVE_CTX, sig);
}

/* ===== reports: validation and wire format ===== */

static void test_report_check(void) {
    zmc_report r;
    static const char ok_chars[] = "Az09._:/-";
    static const char bad_chars[] = " !\"#$%&'()*+,;<=>?@[\\]^`{|}~";
    char name[40];
    CHECK_RC(zmc_report_check(NULL), ZXV_E_ARG);
    r = mk("road:R12/open", ZXV_LP_TRUE, 1);
    CHECK_RC(zmc_report_check(&r), ZXV_OK);
    /* fact name lengths 1..31 accepted, 0 refused */
    for (int n = 0; n <= 31; n++) {
        memset(name, 'f', (size_t) n);
        name[n] = 0;
        r = mk(name, ZXV_LP_TRUE, 1);
        CHECK_RC(zmc_report_check(&r), n == 0 ? ZXV_E_ARG : ZXV_OK);
    }
    for (size_t i = 0; i < sizeof ok_chars - 1; i++) {
        snprintf(name, sizeof name, "a%cb", ok_chars[i]);
        r = mk(name, ZXV_LP_FALSE, 1);
        CHECK_RC(zmc_report_check(&r), ZXV_OK);
    }
    for (size_t i = 0; i < sizeof bad_chars - 1; i++) {
        snprintf(name, sizeof name, "a%cb", bad_chars[i]);
        r = mk(name, ZXV_LP_FALSE, 1);
        CHECK_RC(zmc_report_check(&r), ZXV_E_ARG);
    }
    /* a fact field with no terminator */
    r = mk("x", ZXV_LP_TRUE, 1);
    memset(r.fact, 'x', sizeof r.fact);
    CHECK_RC(zmc_report_check(&r), ZXV_E_ARG);
    /* claims */
    for (int32_t c = -2; c <= 5; c++) {
        r = mk("x", c, 1);
        CHECK_RC(zmc_report_check(&r), (c == ZXV_LP_TRUE || c == ZXV_LP_FALSE) ? ZXV_OK : ZXV_E_ARG);
    }
    /* positions at and past every bound */
    {
        static const struct {
            int32_t lat, lon, want;
        } k[] = {{90000000, 180000000, ZXV_OK},       {-90000000, -180000000, ZXV_OK},
                 {90000001, 0, ZXV_E_RANGE},          {-90000001, 0, ZXV_E_RANGE},
                 {0, 180000001, ZXV_E_RANGE},         {0, -180000001, ZXV_E_RANGE},
                 {0, 0, ZXV_OK},                      {ZMC_NO_POS, ZMC_NO_POS, ZXV_OK},
                 {ZMC_NO_POS, 0, ZXV_E_ARG},          {0, ZMC_NO_POS, ZXV_E_ARG},
                 {INT32_MAX, INT32_MAX, ZXV_E_RANGE}, {INT32_MIN + 1, 0, ZXV_E_RANGE}};
        for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
            r = mk("x", ZXV_LP_TRUE, 1);
            r.lat_e6 = k[i].lat;
            r.lon_e6 = k[i].lon;
            CHECK_RC(zmc_report_check(&r), k[i].want);
        }
    }
    /* notes: every byte value, and lengths 0..120 */
    for (int b = 1; b < 256; b++) {
        r = mk("x", ZXV_LP_TRUE, 1);
        r.note[0] = (char) b;
        r.note[1] = 0;
        CHECK_RC(zmc_report_check(&r), (b >= 0x20 && b <= 0x7e) ? ZXV_OK : ZXV_E_ARG);
    }
    r = mk("x", ZXV_LP_TRUE, 1);
    r.note[0] = 0;
    CHECK_RC(zmc_report_check(&r), ZXV_OK);
    memset(r.note, 'n', ZMC_NOTE_MAX);
    r.note[ZMC_NOTE_MAX] = 0;
    CHECK_RC(zmc_report_check(&r), ZXV_OK);
    memset(r.note, 'n', sizeof r.note);
    CHECK_RC(zmc_report_check(&r), ZXV_E_ARG);
    /* observed: any clock value is the unit's own */
    r = mk("x", ZXV_LP_TRUE, UINT64_MAX);
    CHECK_RC(zmc_report_check(&r), ZXV_OK);
}

/* The documented layout, written byte by byte. */
static size_t build_body(const char *incident, const char *unit, uint64_t no, const zmc_report *r,
                         uint8_t *b) {
    size_t n = 0, l;
    memcpy(b, "ZMCR", 4);
    n = 4;
    b[n++] = 1;
    l = strlen(incident);
    b[n++] = (uint8_t) l;
    memcpy(b + n, incident, l);
    n += l;
    l = strlen(unit);
    b[n++] = (uint8_t) l;
    memcpy(b + n, unit, l);
    n += l;
    for (int i = 0; i < 8; i++) b[n++] = (uint8_t) (no >> (8 * i));
    l = strlen(r->fact);
    b[n++] = (uint8_t) l;
    memcpy(b + n, r->fact, l);
    n += l;
    b[n++] = (uint8_t) r->claim;
    for (int i = 0; i < 8; i++) b[n++] = (uint8_t) (r->observed >> (8 * i));
    for (int i = 0; i < 4; i++) b[n++] = (uint8_t) ((uint32_t) r->lat_e6 >> (8 * i));
    for (int i = 0; i < 4; i++) b[n++] = (uint8_t) ((uint32_t) r->lon_e6 >> (8 * i));
    l = strlen(r->note);
    b[n++] = (uint8_t) l;
    memcpy(b + n, r->note, l);
    n += l;
    return n;
}

static void test_wire(fx_t *f) {
    uint8_t buf[RB], body[300], subj[32];
    size_t len = 0, n;
    zmc_report r = mk("bridge:B4/passable", ZXV_LP_FALSE, 1723900000);
    uint64_t nos[] = {0, 1, 255, 256, UINT64_C(0x0102030405060708), UINT64_MAX};
    r.lat_e6 = -33868820;
    r.lon_e6 = 151209290;
    for (size_t i = 0; i < sizeof nos / sizeof nos[0]; i++) {
        CHECK_RC(zmc_report_sign(f->e.cap, f->key[U_E7], INC, k_unit[U_E7], nos[i], &r, NULL, 0,
                                 &len),
                 ZXV_E_BUFFER);
        n = build_body(INC, k_unit[U_E7], nos[i], &r, body);
        CHECK(len == n + ZIND_SIG_BYTES, "size %zu, want %zu", len, n + ZIND_SIG_BYTES);
        CHECK_RC(zmc_report_sign(f->e.cap, f->key[U_E7], INC, k_unit[U_E7], nos[i], &r, buf,
                                 len - 1, &len),
                 ZXV_E_BUFFER);
        CHECK_RC(zmc_report_sign(f->e.cap, f->key[U_E7], INC, k_unit[U_E7], nos[i], &r, buf,
                                 sizeof buf, &len),
                 ZXV_OK);
        CHECK(len == n + ZIND_SIG_BYTES && memcmp(buf, body, n) == 0,
              "report %zu differs from the documented layout", i);
        CHECK_RC(zind_subject(ZMC_REPORT_CTX, body, n, subj), ZXV_OK);
        CHECK_RC(zind_verify(f->e.cap, zind_signer_pk(f->key[U_E7]), subj, 32, ZMC_REPORT_CTX,
                             buf + n),
                 ZXV_OK);
        CHECK_RC(zind_verify(f->e.cap, zind_signer_pk(f->key[U_L12]), subj, 32, ZMC_REPORT_CTX,
                             buf + n),
                 ZXV_E_VERIFY);
    }
    /* the largest report */
    memset(r.fact, 'F', ZMC_FACT_MAX);
    r.fact[ZMC_FACT_MAX] = 0;
    memset(r.note, '~', ZMC_NOTE_MAX);
    r.note[ZMC_NOTE_MAX] = 0;
    CHECK_RC(zmc_report_sign(f->e.cap, f->key[U_E7], "IIIIIIIIIIIIIIIIIIIIIIII",
                             "UUUUUUUUUUUUUUUUUUUUUUUU", 9, &r, NULL, 0, &len),
             ZXV_E_BUFFER);
    CHECK(len == 233 + ZIND_SIG_BYTES, "largest report %zu bytes", len);
    /* refusals */
    r = mk("x", ZXV_LP_TRUE, 1);
    CHECK_RC(zmc_report_sign(NULL, f->key[0], INC, "U", 1, &r, buf, RB, &len), ZXV_E_ARG);
    CHECK_RC(zmc_report_sign(f->e.cap, NULL, INC, "U", 1, &r, buf, RB, &len), ZXV_E_ARG);
    CHECK_RC(zmc_report_sign(f->e.cap, f->key[0], INC, "U", 1, &r, buf, RB, NULL), ZXV_E_ARG);
    CHECK_RC(zmc_report_sign(f->e.cap, f->key[0], NULL, "U", 1, &r, buf, RB, &len), ZXV_E_ARG);
    CHECK_RC(zmc_report_sign(f->e.cap, f->key[0], INC, "", 1, &r, buf, RB, &len), ZXV_E_ARG);
    CHECK_RC(zmc_report_sign(f->e.cap, f->key[0], "A B", "U", 1, &r, buf, RB, &len), ZXV_E_ARG);
    CHECK_RC(zmc_report_sign(f->e.cap, f->key[0], INC, "UUUUUUUUUUUUUUUUUUUUUUUUU", 1, &r, buf,
                             RB, &len),
             ZXV_E_ARG);
    CHECK_RC(zmc_report_sign(f->e.cap, f->key[0], INC, "U", 1, NULL, buf, RB, &len), ZXV_E_ARG);
    r.claim = ZXV_LP_BOTH;
    CHECK_RC(zmc_report_sign(f->e.cap, f->key[0], INC, "U", 1, &r, buf, RB, &len), ZXV_E_ARG);
    {
        zxv_cap *weak = NULL;
        r.claim = ZXV_LP_TRUE;
        CHECK_RC(zxv_cap_attenuate(f->e.cap, ZXV_CAP_ATTEST, &weak), ZXV_OK);
        CHECK(zmc_report_sign(weak, f->key[0], INC, "U", 1, &r, buf, RB, &len) != ZXV_OK,
              "signing without CRYPTO_SIGN");
        zxv_cap_destroy(weak);
    }
    CHECK(strcmp(zmc_status_name(ZMC_E_UNIT), "ZMC_E_UNIT") == 0, "name");
    CHECK(strcmp(zmc_status_name(ZMC_E_COMMANDER), "ZMC_E_COMMANDER") == 0, "name");
    CHECK(strcmp(zmc_status_name(ZMC_E_FACT), "ZMC_E_FACT") == 0, "name");
    CHECK(strcmp(zmc_status_name(ZMC_E_TASK), "ZMC_E_TASK") == 0, "name");
    CHECK(strcmp(zmc_status_name(ZMC_E_EQUIVOCATION), "ZMC_E_EQUIVOCATION") == 0, "name");
    CHECK(strcmp(zmc_status_name(ZMC_E_BLOCKED), "ZMC_E_BLOCKED") == 0, "name");
    CHECK(strcmp(zmc_status_name(ZMC_E_INCIDENT), "ZMC_E_INCIDENT") == 0, "name");
    CHECK(strcmp(zmc_status_name(ZIND_E_REPLAY), "ZIND_E_REPLAY") == 0, "fallback name");
}

/* ===== setup ===== */

static void fake_pk(uint8_t *pk, uint32_t i) {
    for (uint32_t k = 0; k < ZIND_PK_BYTES; k++) pk[k] = (uint8_t) (k * 31u + i * 7u + (i >> 8));
    pk[0] = (uint8_t) i;
    pk[1] = (uint8_t) (i >> 8);
    pk[2] = 0xA5;
}

static void test_setup(fx_t *f) {
    zmc_incident *inc = NULL;
    zxv_cap *weak = NULL;
    static uint8_t pk[ZIND_PK_BYTES];
    char id[40];
    CHECK_RC(zmc_incident_create(NULL, INC, f->op, NULL, &inc), ZXV_E_ARG);
    CHECK_RC(zmc_incident_create(f->e.cap, INC, NULL, NULL, &inc), ZXV_E_ARG);
    CHECK_RC(zmc_incident_create(f->e.cap, INC, f->op, NULL, NULL), ZXV_E_ARG);
    CHECK_RC(zmc_incident_create(f->e.cap, NULL, f->op, NULL, &inc), ZXV_E_ARG);
    CHECK_RC(zmc_incident_create(f->e.cap, "", f->op, NULL, &inc), ZXV_E_ARG);
    CHECK_RC(zmc_incident_create(f->e.cap, "has space", f->op, NULL, &inc), ZXV_E_ARG);
    CHECK_RC(zmc_incident_create(f->e.cap, "1234567890123456789012345", f->op, NULL, &inc),
             ZXV_E_ARG);
    CHECK(inc == NULL, "no incident on failure");
    CHECK_RC(zxv_cap_attenuate(f->e.cap, ZXV_CAP_ATTEST, &weak), ZXV_OK);
    CHECK(zmc_incident_create(weak, INC, f->op, NULL, &inc) != ZXV_OK, "needs CRYPTO_SIGN");
    zxv_cap_destroy(weak);
    CHECK_RC(zxv_cap_attenuate(f->e.cap, ZXV_CAP_CRYPTO_SIGN, &weak), ZXV_OK);
    CHECK(zmc_incident_create(weak, INC, f->op, f->qlog, &inc) != ZXV_OK,
          "a quarantine log needs ATTEST");
    CHECK_RC(zmc_incident_create(weak, INC, f->op, NULL, &inc), ZXV_OK);
    zmc_incident_destroy(inc);
    zxv_cap_destroy(weak);
    zmc_incident_destroy(NULL);
    inc = NULL;
    CHECK_RC(zmc_incident_create(f->e.cap, "123456789012345678901234", f->op, NULL, &inc), ZXV_OK);
    /* units */
    fake_pk(pk, 0);
    CHECK_RC(zmc_unit_add(NULL, "U", pk), ZXV_E_ARG);
    CHECK_RC(zmc_unit_add(inc, "U", NULL), ZXV_E_ARG);
    CHECK_RC(zmc_unit_add(inc, "", pk), ZXV_E_ARG);
    CHECK_RC(zmc_unit_add(inc, NULL, pk), ZXV_E_ARG);
    CHECK_RC(zmc_unit_add(inc, "U 1", pk), ZXV_E_ARG);
    for (uint32_t i = 0; i < ZMC_MAX_UNITS; i++) {
        snprintf(id, sizeof id, "U-%u", i);
        fake_pk(pk, i);
        CHECK_RC(zmc_unit_add(inc, id, pk), ZXV_OK);
    }
    fake_pk(pk, 999);
    CHECK_RC(zmc_unit_add(inc, "U-OVER", pk), ZXV_E_CAPACITY);
    CHECK_RC(zmc_unit_add(inc, "U-7", pk), ZXV_E_DUPLICATE);
    fake_pk(pk, 7);
    CHECK_RC(zmc_unit_add(inc, "U-NEW", pk), ZXV_E_DUPLICATE);
    /* commanders: a key already held by a unit is refused */
    CHECK_RC(zmc_commander_add(inc, "IC", pk), ZXV_E_DUPLICATE);
    CHECK_RC(zmc_commander_add(NULL, "IC", pk), ZXV_E_ARG);
    CHECK_RC(zmc_commander_add(inc, "I C", pk), ZXV_E_ARG);
    for (uint32_t i = 0; i < ZMC_MAX_COMMANDERS; i++) {
        snprintf(id, sizeof id, "IC-%u", i);
        fake_pk(pk, 1000 + i);
        CHECK_RC(zmc_commander_add(inc, id, pk), ZXV_OK);
    }
    fake_pk(pk, 2000);
    CHECK_RC(zmc_commander_add(inc, "IC-OVER", pk), ZXV_E_CAPACITY);
    CHECK_RC(zmc_commander_add(inc, "IC-3", pk), ZXV_E_DUPLICATE);
    /* resources and tasks */
    CHECK_RC(zmc_resource_add(NULL, "R", "engine"), ZXV_E_ARG);
    CHECK_RC(zmc_resource_add(inc, "", "engine"), ZXV_E_ARG);
    CHECK_RC(zmc_resource_add(inc, "R", ""), ZXV_E_ARG);
    CHECK_RC(zmc_resource_add(inc, "R", "a kind"), ZXV_E_ARG);
    for (uint32_t i = 0; i < ZMC_MAX_RESOURCES; i++) {
        snprintf(id, sizeof id, "R-%03u", i);
        CHECK_RC(zmc_resource_add(inc, id, (i & 1) ? "engine" : "ambulance"), ZXV_OK);
    }
    CHECK_RC(zmc_resource_add(inc, "R-OVER", "engine"), ZXV_E_CAPACITY);
    CHECK_RC(zmc_resource_add(inc, "R-000", "engine"), ZXV_E_DUPLICATE);
    CHECK_RC(zmc_task_add(NULL, "T", 5, "engine", NULL, 0), ZXV_E_ARG);
    CHECK_RC(zmc_task_add(inc, "T", 0, "engine", NULL, 0), ZXV_E_ARG);
    CHECK_RC(zmc_task_add(inc, "T", 10, "engine", NULL, 0), ZXV_E_ARG);
    CHECK_RC(zmc_task_add(inc, "T", 5, "", NULL, 0), ZXV_E_ARG);
    CHECK_RC(zmc_task_add(inc, "", 5, "engine", NULL, 0), ZXV_E_ARG);
    CHECK_RC(zmc_task_add(inc, "T", 5, "engine", "bad fact", ZXV_LP_TRUE), ZXV_E_ARG);
    CHECK_RC(zmc_task_add(inc, "T", 5, "engine", "road:A", ZXV_LP_BOTH), ZXV_E_ARG);
    CHECK_RC(zmc_task_add(inc, "T", 5, "engine", "road:A", ZXV_LP_NEITHER), ZXV_E_ARG);
    CHECK_RC(zmc_task_add(inc, "T", 5, "engine", "0123456789012345678901234567890123",
                          ZXV_LP_TRUE),
             ZXV_E_ARG);
    for (uint32_t i = 0; i < ZMC_MAX_TASKS; i++) {
        snprintf(id, sizeof id, "T-%03u", i);
        CHECK_RC(zmc_task_add(inc, id, (uint8_t) (1 + i % 9), "engine", "", 0), ZXV_OK);
    }
    CHECK_RC(zmc_task_add(inc, "T-OVER", 5, "engine", NULL, 0), ZXV_E_CAPACITY);
    CHECK_RC(zmc_task_add(inc, "T-000", 5, "engine", NULL, 0), ZXV_E_DUPLICATE);
    {
        uint32_t nf = 99;
        CHECK_RC(zmc_fact_count(inc, &nf), ZXV_OK);
        CHECK(nf == 0, "tasks without a fact add no fact (%u)", nf);
        CHECK_RC(zmc_fact_count(NULL, &nf), ZXV_E_ARG);
        CHECK_RC(zmc_fact_count(inc, NULL), ZXV_E_ARG);
    }
    zmc_incident_destroy(inc);

    /* facts: tasks may name facts nobody has reported yet; 1,024 at most */
    inc = NULL;
    CHECK_RC(zmc_incident_create(f->e.cap, "FACTS", f->op, NULL, &inc), ZXV_OK);
    {
        uint32_t nf = 0;
        char nm[40];
        int ok = 1;
        zmc_fact_view v;
        for (uint32_t i = 0; i < 200; i++) {
            snprintf(id, sizeof id, "T%u", i);
            snprintf(nm, sizeof nm, "fact:%04u", i);
            if (zmc_task_add(inc, id, 1, "k", nm, ZXV_LP_TRUE) != ZXV_OK) ok = 0;
        }
        CHECK(ok, "200 tasks with facts");
        CHECK_RC(zmc_fact_count(inc, &nf), ZXV_OK);
        CHECK(nf == 200, "200 facts (%u)", nf);
        CHECK_RC(zmc_fact_state(inc, "fact:0007", &v), ZXV_OK);
        CHECK(v.lp == ZXV_LP_NEITHER && v.reports == 0 && v.epoch == 0 && v.pos == 0 &&
                  v.neg == 0 && v.decision == ZXV_LP_NEITHER,
              "an unreported fact is NEITHER");
        CHECK(phase_of(v.tag) == ZXV_HEX_PHASE_POTENTIAL, "unreported: Potential");
        CHECK_RC(zmc_fact_state(inc, "fact:9999", &v), ZMC_E_FACT);
        CHECK_RC(zmc_fact_state(inc, NULL, &v), ZMC_E_FACT);
        CHECK_RC(zmc_fact_state(inc, "fact:0007", NULL), ZXV_E_ARG);
        CHECK_RC(zmc_fact_name(inc, 7, nm, sizeof nm), ZXV_OK);
        CHECK(strcmp(nm, "fact:0007") == 0, "fact 7 is %s", nm);
        CHECK_RC(zmc_fact_name(inc, 7, nm, 9), ZXV_E_BUFFER);
        CHECK_RC(zmc_fact_name(inc, 7, nm, 10), ZXV_OK);
        CHECK_RC(zmc_fact_name(inc, 200, nm, sizeof nm), ZXV_E_NOT_FOUND);
        CHECK_RC(zmc_fact_name(inc, 0, NULL, 10), ZXV_E_ARG);
    }
    zmc_incident_destroy(inc);
}

/* 1,024 facts, then the 1,025th is refused both by a task and by a report. */
static void test_fact_capacity(fx_t *f) {
    zmc_incident *inc = NULL;
    uint8_t buf[RB];
    size_t len = 0;
    zmc_report r;
    uint32_t nf = 0, t = 0;
    char id[40], nm[40];
    int ok = 1;
    CHECK_RC(zmc_incident_create(f->e.cap, INC, f->op, NULL, &inc), ZXV_OK);
    CHECK_RC(zmc_unit_add(inc, k_unit[0], zind_signer_pk(f->key[0])), ZXV_OK);
    /* 24 facts from reports, 256 from tasks, the rest from reports */
    for (uint32_t i = 0; i < 24; i++) {
        snprintf(nm, sizeof nm, "rep:%u", i);
        r = mk(nm, (i & 1) ? ZXV_LP_TRUE : ZXV_LP_FALSE, i);
        if (zmc_report_sign(f->e.cap, f->key[0], INC, k_unit[0], i + 1, &r, buf, RB, &len) !=
                ZXV_OK ||
            zmc_ingest(inc, buf, len, NULL) != ZXV_OK)
            ok = 0;
    }
    CHECK(ok, "24 reported facts");
    for (t = 0; t < ZMC_MAX_TASKS; t++) {
        snprintf(id, sizeof id, "T%u", t);
        snprintf(nm, sizeof nm, "task:%u", t);
        if (zmc_task_add(inc, id, 1, "k", nm, ZXV_LP_TRUE) != ZXV_OK) ok = 0;
    }
    CHECK(ok, "256 tasks naming facts");
    CHECK_RC(zmc_fact_count(inc, &nf), ZXV_OK);
    CHECK(nf == 280, "280 facts (%u)", nf);
    for (uint32_t i = 24; nf < ZMC_MAX_FACTS; i++) {
        snprintf(nm, sizeof nm, "rep:%u", i);
        r = mk(nm, ZXV_LP_TRUE, i);
        if (zmc_report_sign(f->e.cap, f->key[0], INC, k_unit[0], i + 1, &r, buf, RB, &len) !=
                ZXV_OK ||
            zmc_ingest(inc, buf, len, NULL) != ZXV_OK) {
            ok = 0;
            break;
        }
        (void) zmc_fact_count(inc, &nf);
    }
    CHECK(ok && nf == ZMC_MAX_FACTS, "1,024 facts (%u)", nf);
    r = mk("one-too-many", ZXV_LP_TRUE, 1);
    CHECK_RC(zmc_report_sign(f->e.cap, f->key[0], INC, k_unit[0], 5000, &r, buf, RB, &len),
             ZXV_OK);
    CHECK_RC(zmc_ingest(inc, buf, len, NULL), ZXV_E_CAPACITY);
    /* a report on a known fact still lands */
    r = mk("rep:3", ZXV_LP_TRUE, 1);
    CHECK_RC(zmc_report_sign(f->e.cap, f->key[0], INC, k_unit[0], 5001, &r, buf, RB, &len),
             ZXV_OK);
    CHECK_RC(zmc_ingest(inc, buf, len, NULL), ZXV_OK);
    CHECK_RC(zmc_fact_count(inc, &nf), ZXV_OK);
    CHECK(nf == ZMC_MAX_FACTS, "still 1,024 (%u)", nf);
    zmc_incident_destroy(inc);
}

/* ===== ingest and the picture ===== */

static void test_ingest(fx_t *f) {
    uint8_t buf[RB] = {0}, other[RB];
    size_t len, olen;
    zmc_ingest_info info;
    zmc_fact_view v;
    uint32_t q0;

    CHECK_RC(zmc_ingest(NULL, buf, 10, NULL), ZXV_E_ARG);
    CHECK_RC(zmc_ingest(f->inc, NULL, 10, NULL), ZXV_E_ARG);
    CHECK_RC(zmc_ingest(f->inc, buf, 0, NULL), ZXV_E_FORMAT);

    /* one unit: TRUE, Potential */
    CHECK_RC(report(f, U_E7, "road:R12/open", ZXV_LP_TRUE, &info), ZXV_OK);
    CHECK(strcmp(info.fact, "road:R12/open") == 0 && info.lp_before == ZXV_LP_NEITHER &&
              info.lp_after == ZXV_LP_TRUE && !info.stale && !info.new_hold,
          "first report");
    CHECK_RC(zmc_fact_state(f->inc, "road:R12/open", &v), ZXV_OK);
    CHECK(v.lp == ZXV_LP_TRUE && v.pos == 1 && v.neg == 0 && v.reports == 1, "TRUE, 1 unit");
    CHECK(phase_of(v.tag) == ZXV_HEX_PHASE_POTENTIAL, "one unit: Potential");
    CHECK(pol_of(v.tag) == ZXV_HEX_POL_NEGENTROPIC, "NEITHER -> TRUE: converging");
    {
        int32_t lp = -1;
        CHECK_RC(zind_tag_lp(v.tag, &lp), ZXV_OK);
        CHECK(lp == ZXV_LP_TRUE, "tag base TRUE");
    }
    /* a second unit agrees: Emerging */
    CHECK_RC(report(f, U_L12, "road:R12/open", ZXV_LP_TRUE, &info), ZXV_OK);
    CHECK_RC(zmc_fact_state(f->inc, "road:R12/open", &v), ZXV_OK);
    CHECK(v.lp == ZXV_LP_TRUE && v.pos == 2 && phase_of(v.tag) == ZXV_HEX_PHASE_EMERGING &&
              pol_of(v.tag) == ZXV_HEX_POL_NEUTRAL,
          "two units agree");
    /* the same unit again with TRUE: its latest claim replaces its earlier one */
    CHECK_RC(report(f, U_L12, "road:R12/open", ZXV_LP_TRUE, &info), ZXV_OK);
    CHECK_RC(zmc_fact_state(f->inc, "road:R12/open", &v), ZXV_OK);
    CHECK(v.pos == 2 && v.reports == 3, "a unit counts once (pos %u, reports %u)", v.pos,
          v.reports);

    /* replay: the exact bytes, and the same body re-signed */
    len = sign_as(f, U_M3, 0, "gas:main/leak", ZXV_LP_TRUE, buf);
    CHECK(len > 0, "signed");
    CHECK_RC(zmc_ingest(f->inc, buf, len, NULL), ZXV_OK);
    for (int k = 0; k < 3; k++) CHECK_RC(zmc_ingest(f->inc, buf, len, NULL), ZIND_E_REPLAY);
    olen = sign_as(f, U_M3, f->next_no[U_M3] - 1, "gas:main/leak", ZXV_LP_TRUE, other);
    CHECK_RC(zmc_ingest(f->inc, other, olen, NULL), ZIND_E_REPLAY);

    /* equivocation: the same number, another claim or fact */
    q0 = qcount(f);
    olen = sign_as(f, U_M3, f->next_no[U_M3] - 1, "gas:main/leak", ZXV_LP_FALSE, other);
    CHECK_RC(zmc_ingest(f->inc, other, olen, &info), ZMC_E_EQUIVOCATION);
    olen = sign_as(f, U_M3, f->next_no[U_M3] - 1, "road:R12/open", ZXV_LP_FALSE, other);
    CHECK_RC(zmc_ingest(f->inc, other, olen, NULL), ZMC_E_EQUIVOCATION);
    CHECK(qcount(f) == q0 + 2, "equivocations logged (%u)", qcount(f) - q0);
    {
        uint8_t reason = 0, tag = 0;
        uint32_t who = 99;
        CHECK_RC(zxv_hexlog_at(f->qlog, q0, NULL, &tag, &reason, &who, NULL, NULL, NULL), ZXV_OK);
        CHECK(reason == ZIND_REASON_DUPLICATE && who == U_M3, "equivocation entry (%u, %u)",
              reason, who);
    }
    CHECK_RC(zmc_fact_state(f->inc, "road:R12/open", &v), ZXV_OK);
    CHECK(v.lp == ZXV_LP_TRUE && v.neg == 0, "an equivocation changes nothing");

    /* another incident, an unknown unit, another key */
    {
        zmc_report r = mk("road:R12/open", ZXV_LP_FALSE, 5);
        CHECK_RC(zmc_report_sign(f->e.cap, f->key[U_D1], "OTHER-INC", k_unit[U_D1], 77, &r, buf,
                                 RB, &len),
                 ZXV_OK);
        CHECK_RC(zmc_ingest(f->inc, buf, len, NULL), ZMC_E_INCIDENT);
        CHECK_RC(zmc_report_sign(f->e.cap, f->key[U_D1], INC, "DRONE-9", 77, &r, buf, RB, &len),
                 ZXV_OK);
        CHECK_RC(zmc_ingest(f->inc, buf, len, NULL), ZMC_E_UNIT);
        q0 = qcount(f);
        CHECK_RC(zmc_report_sign(f->e.cap, f->rogue, INC, k_unit[U_D1], 77, &r, buf, RB, &len),
                 ZXV_OK);
        CHECK_RC(zmc_ingest(f->inc, buf, len, NULL), ZIND_E_CHAIN);
        CHECK_RC(zmc_report_sign(f->e.cap, f->key[U_E7], INC, k_unit[U_D1], 77, &r, buf, RB,
                                 &len),
                 ZXV_OK);
        CHECK_RC(zmc_ingest(f->inc, buf, len, NULL), ZIND_E_CHAIN);
        CHECK(qcount(f) == q0 + 2, "forged reports logged");
        {
            uint8_t reason = 0;
            CHECK_RC(zxv_hexlog_at(f->qlog, q0, NULL, NULL, &reason, NULL, NULL, NULL, NULL),
                     ZXV_OK);
            CHECK(reason == ZIND_REASON_TAMPER, "tamper reason %u", reason);
        }
        /* the forged number 77 was never held: the genuine report lands */
        CHECK_RC(zmc_report_sign(f->e.cap, f->key[U_D1], INC, k_unit[U_D1], 77, &r, buf, RB,
                                 &len),
                 ZXV_OK);
        CHECK_RC(zmc_ingest(f->inc, buf, len, &info), ZXV_OK);
        CHECK(info.new_hold && info.lp_after == ZXV_LP_BOTH && info.lp_before == ZXV_LP_TRUE,
              "DRONE-1 contradicts: held");
        f->next_no[U_D1] = 78;
    }
    CHECK_RC(zmc_fact_state(f->inc, "road:R12/open", &v), ZXV_OK);
    CHECK(v.lp == ZXV_LP_BOTH && v.pos == 2 && v.neg == 1, "BOTH 2/1");
    CHECK(pol_of(v.tag) == ZXV_HEX_POL_ENTROPIC && phase_of(v.tag) == ZXV_HEX_PHASE_EMERGING,
          "held: diverging, Emerging");

    /* flipped bits anywhere: never accepted, never changes the picture */
    len = sign_as(f, U_D1, 0, "smoke:sector-4", ZXV_LP_TRUE, buf);
    {
        int bad = 0, ok = 1;
        for (size_t k = 0; k < 96; k++) {
            size_t at = (k * 2654435761u) % len;
            int32_t st;
            if (k < 32) at = k * 7 % 233; /* the body */
            if (at >= len) at = len - 1;
            buf[at] ^= (uint8_t) (1u << (k % 8));
            st = zmc_ingest(f->inc, buf, len, NULL);
            buf[at] ^= (uint8_t) (1u << (k % 8));
            if (st == ZXV_OK) ok = 0;
            if (st != ZXV_E_FORMAT && st != ZIND_E_CHAIN && st != ZMC_E_INCIDENT &&
                st != ZMC_E_UNIT)
                bad++;
        }
        CHECK(ok, "a flipped bit was accepted");
        CHECK(bad == 0, "%d flips gave unexpected codes", bad);
        CHECK_RC(zmc_fact_state(f->inc, "smoke:sector-4", &v), ZMC_E_FACT);
    }
    /* truncation and trailing bytes */
    for (size_t cut = 1; cut < 40; cut++) CHECK_RC(zmc_ingest(f->inc, buf, len - cut, NULL), ZXV_E_FORMAT);
    for (size_t cut = 0; cut < 10; cut++) CHECK_RC(zmc_ingest(f->inc, buf, cut, NULL), ZXV_E_FORMAT);
    CHECK_RC(zmc_ingest(f->inc, buf, len + 1, NULL), ZXV_E_FORMAT);
    CHECK_RC(zmc_ingest(f->inc, buf, len, NULL), ZXV_OK);

    /* stale: a lower number than the unit's latest on the fact */
    {
        uint8_t late[RB];
        size_t llen = sign_as(f, U_E7, 0, "power:grid-2/down", ZXV_LP_TRUE, late); /* no n   */
        len = sign_as(f, U_E7, 0, "power:grid-2/down", ZXV_LP_FALSE, buf);         /* no n+1 */
        CHECK_RC(zmc_ingest(f->inc, buf, len, &info), ZXV_OK);
        CHECK(info.lp_after == ZXV_LP_FALSE, "FALSE");
        CHECK_RC(zmc_ingest(f->inc, late, llen, &info), ZXV_OK);
        CHECK(info.stale && info.lp_after == ZXV_LP_FALSE && !info.new_hold, "stale kept aside");
        CHECK_RC(zmc_fact_state(f->inc, "power:grid-2/down", &v), ZXV_OK);
        CHECK(v.lp == ZXV_LP_FALSE && v.reports == 2 && v.neg == 1 && v.pos == 0,
              "a stale report is recorded, not counted");
        CHECK_RC(zmc_ingest(f->inc, late, llen, NULL), ZIND_E_REPLAY);
        /* the unit changes its mind: its newer claim replaces its own older one */
        CHECK_RC(report(f, U_E7, "power:grid-2/down", ZXV_LP_TRUE, &info), ZXV_OK);
        CHECK(info.lp_after == ZXV_LP_TRUE && !info.new_hold, "one unit never contradicts itself");
    }
    /* report numbers at the extremes */
    {
        zmc_report r = mk("water:hydrant-9/ok", ZXV_LP_TRUE, 0);
        CHECK_RC(zmc_report_sign(f->e.cap, f->key[U_L12], INC, k_unit[U_L12], UINT64_MAX, &r,
                                 buf, RB, &len),
                 ZXV_OK);
        CHECK_RC(zmc_ingest(f->inc, buf, len, NULL), ZXV_OK);
        CHECK_RC(zmc_report_sign(f->e.cap, f->key[U_L12], INC, k_unit[U_L12], 0, &r, buf, RB,
                                 &len),
                 ZXV_OK);
        CHECK_RC(zmc_ingest(f->inc, buf, len, &info), ZXV_OK);
        CHECK(info.stale, "0 after UINT64_MAX is stale");
    }
}

/* ===== decisions ===== */

static void test_decisions(fx_t *f) {
    uint8_t sig[ZIND_SIG_BYTES], sig2[ZIND_SIG_BYTES], msg[512];
    size_t len = 0;
    zmc_fact_view v;
    uint32_t q0;
    const char *FACT = "road:R12/open";
    const char *WHY = "drone video at 14:02 shows the culvert collapsed";

    CHECK(lp_of(f, FACT) == ZXV_LP_BOTH, "held from test_ingest");
    /* message refusals */
    CHECK_RC(zmc_decision_message(NULL, FACT, ZXV_LP_FALSE, "IC-RIVERA", WHY, msg, sizeof msg, &len),
             ZXV_E_ARG);
    CHECK_RC(zmc_decision_message(f->inc, FACT, ZXV_LP_FALSE, "IC-RIVERA", WHY, msg, sizeof msg,
                                  NULL),
             ZXV_E_ARG);
    CHECK_RC(zmc_decision_message(f->inc, "road:R99", ZXV_LP_FALSE, "IC-RIVERA", WHY, msg,
                                  sizeof msg, &len),
             ZMC_E_FACT);
    CHECK_RC(zmc_decision_message(f->inc, FACT, ZXV_LP_BOTH, "IC-RIVERA", WHY, msg, sizeof msg, &len),
             ZXV_E_ARG);
    CHECK_RC(zmc_decision_message(f->inc, FACT, ZXV_LP_NEITHER, "IC-RIVERA", WHY, msg, sizeof msg,
                                  &len),
             ZXV_E_ARG);
    CHECK_RC(zmc_decision_message(f->inc, FACT, ZXV_LP_FALSE, "IC-NOBODY", WHY, msg, sizeof msg,
                                  &len),
             ZMC_E_COMMANDER);
    CHECK_RC(zmc_decision_message(f->inc, FACT, ZXV_LP_FALSE, "ENGINE-7", WHY, msg, sizeof msg,
                                  &len),
             ZMC_E_COMMANDER);
    CHECK_RC(zmc_decision_message(f->inc, FACT, ZXV_LP_FALSE, "IC-RIVERA", "", msg, sizeof msg,
                                  &len),
             ZXV_E_ARG);
    CHECK_RC(zmc_decision_message(f->inc, FACT, ZXV_LP_FALSE, "IC-RIVERA", NULL, msg, sizeof msg,
                                  &len),
             ZXV_E_ARG);
    CHECK_RC(zmc_decision_message(f->inc, FACT, ZXV_LP_FALSE, "IC-RIVERA", "tab\there", msg,
                                  sizeof msg, &len),
             ZXV_E_ARG);
    {
        char why[200];
        memset(why, 'w', ZMC_WHY_MAX);
        why[ZMC_WHY_MAX] = 0;
        CHECK_RC(zmc_decision_message(f->inc, FACT, ZXV_LP_FALSE, "IC-RIVERA", why, msg,
                                      sizeof msg, &len),
                 ZXV_OK);
        why[ZMC_WHY_MAX] = 'w';
        why[ZMC_WHY_MAX + 1] = 0;
        CHECK_RC(zmc_decision_message(f->inc, FACT, ZXV_LP_FALSE, "IC-RIVERA", why, msg,
                                      sizeof msg, &len),
                 ZXV_E_ARG);
    }
    CHECK_RC(zmc_decision_message(f->inc, "power:grid-2/down", ZXV_LP_FALSE, "IC-RIVERA", WHY, msg,
                                  sizeof msg, &len),
             ZXV_E_STATE);
    CHECK_RC(zmc_decision_message(f->inc, FACT, ZXV_LP_FALSE, "IC-RIVERA", WHY, NULL, 0, &len),
             ZXV_E_BUFFER);
    {
        size_t need = len;
        CHECK(need == 15 + 1 + strlen(INC) + 1 + strlen(FACT) + 4 + 32 + 1 + 1 + 9 + 1 + strlen(WHY),
              "decision size %zu", need);
        CHECK_RC(zmc_decision_message(f->inc, FACT, ZXV_LP_FALSE, "IC-RIVERA", WHY, msg, need - 1,
                                      &len),
                 ZXV_E_BUFFER);
        CHECK_RC(zmc_decision_message(f->inc, FACT, ZXV_LP_FALSE, "IC-RIVERA", WHY, msg, need,
                                      &len),
                 ZXV_OK);
        CHECK(memcmp(msg, "zmc/decision/v1", 15) == 0 && msg[15] == strlen(INC), "layout");
        CHECK(msg[15 + 1 + strlen(INC) + 1 + strlen(FACT)] == 0, "epoch 0");
        CHECK(msg[15 + 1 + strlen(INC) + 1 + strlen(FACT) + 4 + 32] == ZXV_LP_FALSE, "value");
    }

    /* forged: the rogue key, another commander's key, another value */
    q0 = qcount(f);
    CHECK_RC(decide(f, f->rogue, FACT, ZXV_LP_FALSE, "IC-RIVERA", WHY, sig), ZXV_OK);
    CHECK_RC(zmc_resolve(f->inc, FACT, ZXV_LP_FALSE, "IC-RIVERA", WHY, sig), ZXV_E_VERIFY);
    CHECK_RC(decide(f, f->ops, FACT, ZXV_LP_FALSE, "IC-RIVERA", WHY, sig), ZXV_OK);
    CHECK_RC(zmc_resolve(f->inc, FACT, ZXV_LP_FALSE, "IC-RIVERA", WHY, sig), ZXV_E_VERIFY);
    CHECK_RC(decide(f, f->ic, FACT, ZXV_LP_FALSE, "IC-RIVERA", WHY, sig), ZXV_OK);
    CHECK_RC(zmc_resolve(f->inc, FACT, ZXV_LP_TRUE, "IC-RIVERA", WHY, sig), ZXV_E_VERIFY);
    CHECK_RC(zmc_resolve(f->inc, FACT, ZXV_LP_FALSE, "IC-RIVERA", "another reason", sig),
             ZXV_E_VERIFY);
    CHECK_RC(zmc_resolve(f->inc, FACT, ZXV_LP_FALSE, "OPS-CHIEF", WHY, sig), ZXV_E_VERIFY);
    CHECK_RC(zmc_resolve(f->inc, FACT, ZXV_LP_FALSE, "IC-NOBODY", WHY, sig), ZMC_E_COMMANDER);
    CHECK_RC(zmc_resolve(f->inc, "gas:main/leak", ZXV_LP_FALSE, "IC-RIVERA", WHY, sig), ZXV_E_STATE);
    CHECK_RC(zmc_resolve(NULL, FACT, ZXV_LP_FALSE, "IC-RIVERA", WHY, sig), ZXV_E_ARG);
    CHECK_RC(zmc_resolve(f->inc, FACT, ZXV_LP_FALSE, "IC-RIVERA", WHY, NULL), ZXV_E_ARG);
    {
        uint8_t bad[ZIND_SIG_BYTES];
        int ok = 1;
        for (int k = 0; k < 16; k++) {
            memcpy(bad, sig, sizeof bad);
            bad[(size_t) k * 289u % ZIND_SIG_BYTES] ^= (uint8_t) (1u << (k % 8));
            if (zmc_resolve(f->inc, FACT, ZXV_LP_FALSE, "IC-RIVERA", WHY, bad) != ZXV_E_VERIFY)
                ok = 0;
        }
        CHECK(ok, "a flipped signature bit is refused");
    }
    CHECK(qcount(f) == q0 + 21, "forged decisions logged (%u)", qcount(f) - q0);
    CHECK(lp_of(f, FACT) == ZXV_LP_BOTH, "still held");

    /* stale: new evidence arrives after the commander signed */
    CHECK_RC(report(f, U_M3, FACT, ZXV_LP_FALSE, NULL), ZXV_OK);
    CHECK_RC(zmc_resolve(f->inc, FACT, ZXV_LP_FALSE, "IC-RIVERA", WHY, sig), ZXV_E_VERIFY);
    CHECK_RC(zmc_fact_state(f->inc, FACT, &v), ZXV_OK);
    CHECK(v.lp == ZXV_LP_BOTH && v.pos == 2 && v.neg == 2, "2/2");

    /* without ATTEST */
    {
        zxv_cap *weak = NULL;
        zmc_incident *inc2 = NULL;
        uint8_t b[RB];
        size_t bl = 0;
        zmc_report r;
        CHECK_RC(zxv_cap_attenuate(f->e.cap, ZXV_CAP_CRYPTO_SIGN, &weak), ZXV_OK);
        CHECK_RC(zmc_incident_create(weak, INC, f->op, NULL, &inc2), ZXV_OK);
        CHECK_RC(zmc_unit_add(inc2, k_unit[0], zind_signer_pk(f->key[0])), ZXV_OK);
        CHECK_RC(zmc_unit_add(inc2, k_unit[1], zind_signer_pk(f->key[1])), ZXV_OK);
        CHECK_RC(zmc_commander_add(inc2, "IC-RIVERA", zind_signer_pk(f->ic)), ZXV_OK);
        r = mk("x", ZXV_LP_TRUE, 1);
        CHECK_RC(zmc_report_sign(f->e.cap, f->key[0], INC, k_unit[0], 1, &r, b, RB, &bl), ZXV_OK);
        CHECK_RC(zmc_ingest(inc2, b, bl, NULL), ZXV_OK);
        r.claim = ZXV_LP_FALSE;
        CHECK_RC(zmc_report_sign(f->e.cap, f->key[1], INC, k_unit[1], 1, &r, b, RB, &bl), ZXV_OK);
        CHECK_RC(zmc_ingest(inc2, b, bl, NULL), ZXV_OK);
        CHECK_RC(zmc_decision_message(inc2, "x", ZXV_LP_TRUE, "IC-RIVERA", "seen", msg, sizeof msg,
                                      &len),
                 ZXV_OK);
        CHECK_RC(zind_sign(f->e.cap, f->ic, msg, len, ZMC_DECISION_CTX, sig2), ZXV_OK);
        CHECK_RC(zmc_resolve(inc2, "x", ZXV_LP_TRUE, "IC-RIVERA", "seen", sig2), ZXV_E_CAP);
        zmc_incident_destroy(inc2);
        zxv_cap_destroy(weak);
    }

    /* genuine */
    q0 = qcount(f);
    CHECK_RC(zmc_decision_message(f->inc, FACT, ZXV_LP_FALSE, "IC-RIVERA", WHY, msg, sizeof msg,
                                  &len),
             ZXV_OK);
    CHECK_RC(zind_sign(f->e.cap, f->ic, msg, len, ZMC_DECISION_CTX, sig), ZXV_OK);
    CHECK_RC(zmc_resolve(f->inc, FACT, ZXV_LP_FALSE, "IC-RIVERA", WHY, sig), ZXV_OK);
    CHECK_RC(zmc_fact_state(f->inc, FACT, &v), ZXV_OK);
    CHECK(v.lp == ZXV_LP_FALSE && v.epoch == 1 && v.decision == ZXV_LP_FALSE && v.pos == 0 &&
              v.neg == 0,
          "resolved FALSE, epoch 1");
    CHECK(phase_of(v.tag) == ZXV_HEX_PHASE_MANIFEST && pol_of(v.tag) == ZXV_HEX_POL_NEGENTROPIC,
          "Manifest, converging");
    CHECK(qcount(f) == q0 + 1, "one resolution entry");
    {
        uint8_t reason = 0, tag = 0, subj[32], want[32];
        uint32_t who = 99;
        int32_t lp = -1;
        CHECK_RC(zxv_hexlog_at(f->qlog, q0, NULL, &tag, &reason, &who, subj, NULL, NULL), ZXV_OK);
        CHECK_RC(zxv_sha3_256(msg, len, want), ZXV_OK);
        CHECK(reason == ZMC_REASON_RESOLVED, "reason %u", reason);
        CHECK(who == 0, "who: IC-RIVERA is commander 0 (%u)", who);
        CHECK(memcmp(subj, want, 32) == 0, "subject: SHA3-256 of the signed message");
        CHECK_RC(zind_tag_lp(tag, &lp), ZXV_OK);
        CHECK(lp == ZXV_LP_BOTH, "the entry keeps the held BOTH base");
    }
    /* the same decision again: nothing is held now */
    CHECK_RC(zmc_resolve(f->inc, FACT, ZXV_LP_FALSE, "IC-RIVERA", WHY, sig), ZXV_E_STATE);
    /* a unit agrees in the new epoch: still FALSE */
    CHECK_RC(report(f, U_M3, FACT, ZXV_LP_FALSE, NULL), ZXV_OK);
    CHECK_RC(zmc_fact_state(f->inc, FACT, &v), ZXV_OK);
    CHECK(v.lp == ZXV_LP_FALSE && v.neg == 1 && phase_of(v.tag) == ZXV_HEX_PHASE_MANIFEST,
          "agreement keeps it");
    /* a used number cannot be reused after a decision either */
    {
        uint8_t b[RB];
        size_t bl = sign_as(f, U_E7, 1, FACT, ZXV_LP_TRUE, b);
        CHECK_RC(zmc_ingest(f->inc, b, bl, NULL), ZIND_E_REPLAY);
        bl = sign_as(f, U_E7, 1, FACT, ZXV_LP_FALSE, b);
        CHECK_RC(zmc_ingest(f->inc, b, bl, NULL), ZMC_E_EQUIVOCATION);
    }
    /* a newer contradicting report holds the fact again */
    {
        zmc_ingest_info info;
        CHECK_RC(report(f, U_E7, FACT, ZXV_LP_TRUE, &info), ZXV_OK);
        CHECK(info.new_hold && info.lp_before == ZXV_LP_FALSE && info.lp_after == ZXV_LP_BOTH,
              "held again");
    }
    /* OPS-CHIEF resolves epoch 1 TRUE */
    CHECK_RC(decide(f, f->ops, FACT, ZXV_LP_TRUE, "OPS-CHIEF", "crew walked it at 15:10", sig),
             ZXV_OK);
    CHECK_RC(zmc_resolve(f->inc, FACT, ZXV_LP_TRUE, "OPS-CHIEF", "crew walked it at 15:10", sig),
             ZXV_OK);
    CHECK_RC(zmc_fact_state(f->inc, FACT, &v), ZXV_OK);
    CHECK(v.lp == ZXV_LP_TRUE && v.epoch == 2 && v.decision == ZXV_LP_TRUE, "epoch 2 TRUE");
    CHECK_RC(zxv_hexlog_verify(f->qlog), ZXV_OK);
}

/* ===== advice ===== */

static void test_advice(fx_t *f) {
    zmc_rec rec[16];
    size_t n = 0;
    uint8_t sig[ZIND_SIG_BYTES], msg[256];
    size_t len = 0;
    char res[40];
    int32_t assigned = -1;

    CHECK_RC(zmc_resource_add(f->inc, "ENG-21", "engine"), ZXV_OK);
    CHECK_RC(zmc_resource_add(f->inc, "ENG-05", "engine"), ZXV_OK);
    CHECK_RC(zmc_resource_add(f->inc, "AMB-2", "ambulance"), ZXV_OK);
    CHECK_RC(zmc_task_add(f->inc, "evac-north", 9, "engine", "road:R12/open", ZXV_LP_TRUE), ZXV_OK);
    CHECK_RC(zmc_task_add(f->inc, "shut-gas", 7, "engine", "gas:main/leak", ZXV_LP_TRUE), ZXV_OK);
    CHECK_RC(zmc_task_add(f->inc, "hydrant", 7, "engine", "water:hydrant-9/ok", ZXV_LP_FALSE),
             ZXV_OK);
    CHECK_RC(zmc_task_add(f->inc, "triage", 8, "ambulance", NULL, 0), ZXV_OK);
    CHECK_RC(zmc_task_add(f->inc, "spare-amb", 2, "ambulance", "", 0), ZXV_OK);
    CHECK_RC(zmc_task_add(f->inc, "recon", 5, "drone", "bridge:B4/passable", ZXV_LP_TRUE), ZXV_OK);
    CHECK_RC(zmc_task_add(f->inc, "staging", 1, "engine", NULL, 0), ZXV_OK);

    CHECK_RC(zmc_recommend(NULL, rec, 16, &n), ZXV_E_ARG);
    CHECK_RC(zmc_recommend(f->inc, rec, 16, NULL), ZXV_E_ARG);
    CHECK_RC(zmc_recommend(f->inc, NULL, 3, &n), ZXV_E_ARG);
    CHECK_RC(zmc_recommend(f->inc, NULL, 0, &n), ZXV_E_BUFFER);
    CHECK(n == 7, "7 open tasks (%zu)", n);
    CHECK_RC(zmc_recommend(f->inc, rec, 6, &n), ZXV_E_BUFFER);
    CHECK_RC(zmc_recommend(f->inc, rec, 16, &n), ZXV_OK);
    {
        static const struct {
            const char *task, *res;
            int32_t st;
        } want[7] = {{"evac-north", "ENG-05", ZMC_REC_ASSIGN}, /* road TRUE after decision 2 */
                     {"triage", "AMB-2", ZMC_REC_ASSIGN},
                     {"hydrant", "", ZMC_REC_BLOCKED},        /* the hydrant is TRUE (ok)   */
                     {"shut-gas", "ENG-21", ZMC_REC_ASSIGN},
                     {"recon", "", ZMC_REC_UNKNOWN},
                     {"spare-amb", "", ZMC_REC_NO_RESOURCE},
                     {"staging", "", ZMC_REC_NO_RESOURCE}};
        for (int i = 0; i < 7; i++) {
            CHECK(strcmp(rec[i].task, want[i].task) == 0, "rec %d task %s, want %s", i, rec[i].task,
                  want[i].task);
            CHECK(strcmp(rec[i].resource, want[i].res) == 0, "rec %d resource %s, want %s", i,
                  rec[i].resource, want[i].res);
            CHECK(rec[i].status == want[i].st, "rec %d status %d, want %d", i, (int) rec[i].status,
                  (int) want[i].st);
        }
    }
    /* a held fact: the task waits for a decision */
    CHECK_RC(report(f, U_D1, "gas:main/leak", ZXV_LP_FALSE, NULL), ZXV_OK);
    CHECK(lp_of(f, "gas:main/leak") == ZXV_LP_BOTH, "gas held");
    CHECK_RC(zmc_recommend(f->inc, rec, 16, &n), ZXV_OK);
    CHECK(strcmp(rec[2].task, "hydrant") == 0 && strcmp(rec[3].task, "shut-gas") == 0 &&
              rec[3].status == ZMC_REC_HELD && rec[3].resource[0] == 0,
          "held: never recommended");
    CHECK(strcmp(rec[5].task, "spare-amb") == 0 && rec[5].status == ZMC_REC_NO_RESOURCE,
          "spare-amb");
    CHECK(strcmp(rec[6].task, "staging") == 0 && rec[6].status == ZMC_REC_ASSIGN &&
              strcmp(rec[6].resource, "ENG-21") == 0,
          "staging now gets ENG-21");

    /* approvals: message refusals */
    CHECK_RC(zmc_approve_message(f->inc, "nope", "ENG-05", "IC-RIVERA", msg, sizeof msg, &len),
             ZMC_E_TASK);
    CHECK_RC(zmc_approve_message(f->inc, "evac-north", "nope", "IC-RIVERA", msg, sizeof msg, &len),
             ZMC_E_TASK);
    CHECK_RC(zmc_approve_message(f->inc, "evac-north", "ENG-05", "ENGINE-7", msg, sizeof msg, &len),
             ZMC_E_COMMANDER);
    CHECK_RC(zmc_approve_message(NULL, "evac-north", "ENG-05", "IC-RIVERA", msg, sizeof msg, &len),
             ZXV_E_ARG);
    CHECK_RC(zmc_approve_message(f->inc, "evac-north", "ENG-05", "IC-RIVERA", NULL, 0, &len),
             ZXV_E_BUFFER);
    CHECK(len == 14 + 1 + strlen(INC) + 1 + 10 + 1 + 6 + 4 + 1 + 9, "approve size %zu", len);
    CHECK_RC(zmc_approve_message(f->inc, "evac-north", "ENG-05", "IC-RIVERA", msg, sizeof msg, &len),
             ZXV_OK);
    CHECK(msg[len - 10 - 4] == 2, "the approval carries the fact's epoch (2)");

    /* blocked: held fact, wrong kind, unknown fact state */
    CHECK_RC(approve_sig(f, f->ic, "shut-gas", "ENG-21", "IC-RIVERA", sig), ZXV_OK);
    CHECK_RC(zmc_approve(f->inc, "shut-gas", "ENG-21", "IC-RIVERA", sig), ZMC_E_BLOCKED);
    CHECK_RC(approve_sig(f, f->ic, "triage", "ENG-21", "IC-RIVERA", sig), ZXV_OK);
    CHECK_RC(zmc_approve(f->inc, "triage", "ENG-21", "IC-RIVERA", sig), ZMC_E_BLOCKED);
    CHECK_RC(approve_sig(f, f->ic, "hydrant", "ENG-21", "IC-RIVERA", sig), ZXV_OK);
    CHECK_RC(zmc_approve(f->inc, "hydrant", "ENG-21", "IC-RIVERA", sig), ZMC_E_BLOCKED);
    CHECK_RC(approve_sig(f, f->ic, "recon", "ENG-21", "IC-RIVERA", sig), ZXV_OK);
    CHECK_RC(zmc_approve(f->inc, "recon", "ENG-21", "IC-RIVERA", sig), ZMC_E_BLOCKED);
    /* forged */
    CHECK_RC(approve_sig(f, f->rogue, "evac-north", "ENG-05", "IC-RIVERA", sig), ZXV_OK);
    CHECK_RC(zmc_approve(f->inc, "evac-north", "ENG-05", "IC-RIVERA", sig), ZXV_E_VERIFY);
    CHECK_RC(approve_sig(f, f->ic, "evac-north", "ENG-05", "IC-RIVERA", sig), ZXV_OK);
    CHECK_RC(zmc_approve(f->inc, "evac-north", "ENG-21", "IC-RIVERA", sig), ZXV_E_VERIFY);
    CHECK_RC(zmc_approve(f->inc, "evac-north", "ENG-05", "OPS-CHIEF", sig), ZXV_E_VERIFY);
    CHECK_RC(zmc_approve(f->inc, "evac-north", "ENG-05", "IC-NOBODY", sig), ZMC_E_COMMANDER);
    CHECK_RC(zmc_approve(f->inc, "evac-north", "ENG-05", "IC-RIVERA", NULL), ZXV_E_ARG);
    CHECK_RC(zmc_approve(NULL, "evac-north", "ENG-05", "IC-RIVERA", sig), ZXV_E_ARG);
    CHECK_RC(zmc_task_state(f->inc, "evac-north", &assigned, res, sizeof res), ZXV_OK);
    CHECK(assigned == 0 && res[0] == 0, "still open");
    /* genuine */
    CHECK_RC(zmc_approve(f->inc, "evac-north", "ENG-05", "IC-RIVERA", sig), ZXV_OK);
    CHECK_RC(zmc_task_state(f->inc, "evac-north", &assigned, res, sizeof res), ZXV_OK);
    CHECK(assigned == 1 && strcmp(res, "ENG-05") == 0, "assigned ENG-05");
    CHECK_RC(zmc_task_state(f->inc, "evac-north", &assigned, res, 6), ZXV_E_BUFFER);
    CHECK_RC(zmc_task_state(f->inc, "evac-north", &assigned, NULL, 0), ZXV_OK);
    CHECK_RC(zmc_task_state(f->inc, "nope", &assigned, NULL, 0), ZMC_E_TASK);
    CHECK_RC(zmc_task_state(f->inc, "evac-north", NULL, NULL, 0), ZXV_E_ARG);
    /* the same approval again, and a busy resource */
    CHECK_RC(zmc_approve(f->inc, "evac-north", "ENG-05", "IC-RIVERA", sig), ZMC_E_BLOCKED);
    CHECK_RC(approve_sig(f, f->ic, "staging", "ENG-05", "IC-RIVERA", sig), ZXV_OK);
    CHECK_RC(zmc_approve(f->inc, "staging", "ENG-05", "IC-RIVERA", sig), ZMC_E_BLOCKED);
    /* the commander picks ENG-21 for staging rather than the advice (any free one of the kind) */
    CHECK_RC(approve_sig(f, f->ops, "staging", "ENG-21", "OPS-CHIEF", sig), ZXV_OK);
    CHECK_RC(zmc_approve(f->inc, "staging", "ENG-21", "OPS-CHIEF", sig), ZXV_OK);
    CHECK_RC(zmc_recommend(f->inc, rec, 16, &n), ZXV_OK);
    CHECK(n == 5, "5 open tasks (%zu)", n);

    /* stale approval: the fact's epoch moves between signing and applying */
    CHECK_RC(approve_sig(f, f->ic, "triage", "AMB-2", "IC-RIVERA", sig), ZXV_OK);
    CHECK_RC(zmc_approve(f->inc, "triage", "AMB-2", "IC-RIVERA", sig), ZXV_OK);
    CHECK_RC(zmc_resource_add(f->inc, "ENG-30", "engine"), ZXV_OK);
    CHECK_RC(decide(f, f->ic, "gas:main/leak", ZXV_LP_TRUE, "IC-RIVERA", "meter reads 40% LEL", sig),
             ZXV_OK);
    {
        uint8_t asig[ZIND_SIG_BYTES];
        CHECK_RC(zmc_approve_message(f->inc, "shut-gas", "ENG-30", "IC-RIVERA", msg, sizeof msg,
                                     &len),
                 ZXV_OK);
        CHECK_RC(zind_sign(f->e.cap, f->ic, msg, len, ZMC_APPROVE_CTX, asig), ZXV_OK);
        CHECK_RC(zmc_approve(f->inc, "shut-gas", "ENG-30", "IC-RIVERA", asig), ZMC_E_BLOCKED);
        CHECK_RC(zmc_resolve(f->inc, "gas:main/leak", ZXV_LP_TRUE, "IC-RIVERA",
                             "meter reads 40% LEL", sig),
                 ZXV_OK);
        /* signed at epoch 0, the fact is at epoch 1 now */
        CHECK_RC(zmc_approve(f->inc, "shut-gas", "ENG-30", "IC-RIVERA", asig), ZXV_E_VERIFY);
        CHECK_RC(approve_sig(f, f->ic, "shut-gas", "ENG-30", "IC-RIVERA", asig), ZXV_OK);
        CHECK_RC(zmc_approve(f->inc, "shut-gas", "ENG-30", "IC-RIVERA", asig), ZXV_OK);
    }
}

/* ===== the journal ===== */

static uint8_t *export_journal(const zind_chain *c, size_t *len) {
    uint8_t *b;
    if (zind_chain_export(c, NULL, 0, len) != ZXV_E_BUFFER) return NULL;
    b = (uint8_t *) malloc(*len);
    if (b && zind_chain_export(c, b, *len, len) != ZXV_OK) {
        free(b);
        return NULL;
    }
    return b;
}

/* Re-sign the journal with the operator key, keeping records in `keep`. */
static uint8_t *resign(fx_t *f, const uint32_t *keep, uint32_t n, size_t *len) {
    zind_chain *c = NULL;
    zind_record r;
    uint8_t *b = NULL;
    if (zind_chain_create(ZMC_JOURNAL, 4096, &c) != ZXV_OK) return NULL;
    for (uint32_t i = 0; i < n; i++) {
        if (zind_chain_at(zmc_journal(f->inc), keep[i], &r) != ZXV_OK ||
            zind_chain_append(f->e.cap, c, f->op, r.kind, r.payload, r.payload_len, NULL, NULL) !=
                ZXV_OK) {
            zind_chain_destroy(c);
            return NULL;
        }
    }
    b = export_journal(c, len);
    zind_chain_destroy(c);
    return b;
}

static void test_journal(fx_t *f) {
    const char *uids[NU], *cids[2] = {"IC-RIVERA", "OPS-CHIEF"};
    const uint8_t *upks[NU], *cpks[2];
    zmc_trust t;
    zmc_verdict v;
    uint8_t *j;
    size_t jl = 0;
    uint32_t n = 0, kinds[8] = {0};
    zind_record r;
    const uint8_t *opk[1];

    for (int i = 0; i < NU; i++) {
        uids[i] = k_unit[i];
        upks[i] = zind_signer_pk(f->key[i]);
    }
    cpks[0] = zind_signer_pk(f->ic);
    cpks[1] = zind_signer_pk(f->ops);
    memset(&t, 0, sizeof t);
    t.unit_ids = uids;
    t.unit_pks = upks;
    t.n_units = NU;
    t.commander_ids = cids;
    t.commander_pks = cpks;
    t.n_commanders = 2;
    t.operator_pk = zind_signer_pk(f->op);

    CHECK(zmc_journal(NULL) == NULL, "no journal");
    CHECK_RC(zind_chain_count(zmc_journal(f->inc), &n), ZXV_OK);
    for (uint32_t i = 0; i < n; i++) {
        if (zind_chain_at(zmc_journal(f->inc), i, &r) == ZXV_OK && r.kind < 8) kinds[r.kind]++;
    }
    CHECK(kinds[ZMC_J_INCIDENT] == 1, "one incident record");
    CHECK(kinds[ZMC_J_DECISION] == 3, "3 decisions (%u)", kinds[ZMC_J_DECISION]);
    CHECK(kinds[ZMC_J_APPROVAL] == 4, "4 approvals (%u)", kinds[ZMC_J_APPROVAL]);
    CHECK(kinds[ZMC_J_RESOURCE] == 4, "4 resources (%u)", kinds[ZMC_J_RESOURCE]);
    CHECK(kinds[ZMC_J_TASK] == 7, "7 tasks (%u)", kinds[ZMC_J_TASK]);
    opk[0] = zind_signer_pk(f->op);
    CHECK_RC(zind_chain_verify(f->e.cap, zmc_journal(f->inc), opk, 1, NULL), ZXV_OK);

    j = export_journal(zmc_journal(f->inc), &jl);
    CHECK(j != NULL, "export");
    if (!j) return;
    CHECK_RC(zmc_verify_journal(f->e.cap, j, jl, &t, &v), ZXV_OK);
    CHECK(strcmp(v.incident, INC) == 0, "incident %s", v.incident);
    CHECK(v.records == n && v.reports == kinds[ZMC_J_REPORT], "records %u reports %u", v.records,
          v.reports);
    CHECK(v.decisions == 3 && v.approvals == 4, "decisions %u approvals %u", v.decisions,
          v.approvals);
    {
        uint32_t nf = 0;
        zmc_fact_count(f->inc, &nf);
        CHECK(v.facts == nf, "facts %u, want %u", v.facts, nf);
    }
    CHECK(v.held == 3, "held %u", v.held);
    CHECK(v.bad_index == UINT32_MAX, "no bad index");

    /* the trust set in another order replays the same */
    {
        const char *ru[NU];
        const uint8_t *rp[NU];
        const char *rc[2] = {cids[1], cids[0]};
        const uint8_t *rcp[2] = {cpks[1], cpks[0]};
        zmc_trust t2 = t;
        for (int i = 0; i < NU; i++) {
            ru[i] = uids[NU - 1 - i];
            rp[i] = upks[NU - 1 - i];
        }
        t2.unit_ids = ru;
        t2.unit_pks = rp;
        t2.commander_ids = rc;
        t2.commander_pks = rcp;
        CHECK_RC(zmc_verify_journal(f->e.cap, j, jl, &t2, &v), ZXV_OK);
        CHECK(v.decisions == 3 && v.approvals == 4, "reordered trust");
        /* a missing unit, a missing commander, a commander's key swapped */
        t2 = t;
        t2.n_units = NU - 1;
        CHECK_RC(zmc_verify_journal(f->e.cap, j, jl, &t2, &v), ZIND_E_UNTRUSTED);
        CHECK(v.bad_index != UINT32_MAX && v.bad_index > 0, "bad index %u", v.bad_index);
        t2 = t;
        t2.n_commanders = 1;
        CHECK_RC(zmc_verify_journal(f->e.cap, j, jl, &t2, &v), ZIND_E_UNTRUSTED);
        t2 = t;
        t2.commander_ids = rc;
        CHECK_RC(zmc_verify_journal(f->e.cap, j, jl, &t2, &v), ZIND_E_UNTRUSTED);
        t2 = t;
        t2.operator_pk = zind_signer_pk(f->rogue);
        CHECK_RC(zmc_verify_journal(f->e.cap, j, jl, &t2, &v), ZIND_E_UNTRUSTED);
        CHECK(v.bad_index == 0, "operator: record 0");
        t2 = t;
        t2.unit_ids = ru; /* names and keys disagree */
        CHECK(zmc_verify_journal(f->e.cap, j, jl, &t2, &v) != ZXV_OK, "mismatched unit keys");
        t2 = t;
        t2.operator_pk = NULL;
        CHECK_RC(zmc_verify_journal(f->e.cap, j, jl, &t2, &v), ZXV_E_ARG);
        CHECK_RC(zmc_verify_journal(NULL, j, jl, &t, &v), ZXV_E_ARG);
        CHECK_RC(zmc_verify_journal(f->e.cap, NULL, jl, &t, &v), ZXV_E_ARG);
        CHECK_RC(zmc_verify_journal(f->e.cap, j, jl, NULL, &v), ZXV_E_ARG);
        CHECK_RC(zmc_verify_journal(f->e.cap, j, jl, &t, NULL), ZXV_E_ARG);
        {
            zxv_cap *weak = NULL;
            CHECK_RC(zxv_cap_attenuate(f->e.cap, ZXV_CAP_ATTEST, &weak), ZXV_OK);
            CHECK(zmc_verify_journal(weak, j, jl, &t, &v) != ZXV_OK, "needs CRYPTO_SIGN");
            zxv_cap_destroy(weak);
        }
    }
    /* flipped bits and truncation */
    {
        int ok = 1;
        for (size_t k = 0; k < 64; k++) {
            size_t at = (k * 2654435761u + 13) % jl;
            int32_t st;
            j[at] ^= (uint8_t) (1u << (k % 8));
            st = zmc_verify_journal(f->e.cap, j, jl, &t, &v);
            j[at] ^= (uint8_t) (1u << (k % 8));
            if (st == ZXV_OK) ok = 0;
        }
        CHECK(ok, "a flipped bit verified");
        for (size_t cut = 1; cut < 64; cut += 7)
            CHECK_RC(zmc_verify_journal(f->e.cap, j, jl - cut, &t, &v), ZXV_E_FORMAT);
        CHECK_RC(zmc_verify_journal(f->e.cap, j, jl, &t, &v), ZXV_OK);
    }

    /* re-signed by the operator: the records themselves must replay */
    {
        uint32_t keep[512], k = 0, first_dec = UINT32_MAX, first_rep = UINT32_MAX,
                                   first_app = UINT32_MAX, first_task = UINT32_MAX;
        uint8_t *b;
        size_t bl = 0;
        for (uint32_t i = 0; i < n; i++) {
            zind_chain_at(zmc_journal(f->inc), i, &r);
            if (r.kind == ZMC_J_DECISION && first_dec == UINT32_MAX) first_dec = i;
            if (r.kind == ZMC_J_REPORT && first_rep == UINT32_MAX) first_rep = i;
            if (r.kind == ZMC_J_APPROVAL && first_app == UINT32_MAX) first_app = i;
            if (r.kind == ZMC_J_TASK && first_task == UINT32_MAX) first_task = i;
        }
        CHECK(n < 512 && first_dec != UINT32_MAX && first_rep != UINT32_MAX &&
                  first_app != UINT32_MAX && first_task != UINT32_MAX,
              "kinds found");
        /* an exact copy re-signed verifies */
        for (uint32_t i = 0; i < n; i++) keep[i] = i;
        b = resign(f, keep, n, &bl);
        CHECK(b && zmc_verify_journal(f->e.cap, b, bl, &t, &v) == ZXV_OK, "re-signed copy");
        free(b);
        /* drop a report that the first decision counted */
        {
            uint32_t drop = UINT32_MAX;
            for (uint32_t i = first_dec; i-- > 1;) {
                zind_chain_at(zmc_journal(f->inc), i, &r);
                if (r.kind == ZMC_J_REPORT) {
                    drop = i;
                    break;
                }
            }
            k = 0;
            for (uint32_t i = 0; i < n; i++)
                if (i != drop) keep[k++] = i;
            b = resign(f, keep, k, &bl);
            CHECK_RC(zmc_verify_journal(f->e.cap, b, bl, &t, &v), ZIND_E_CHAIN);
            CHECK(v.bad_index != UINT32_MAX && v.bad_index <= first_dec, "bad at %u (dec %u)",
                  v.bad_index, first_dec);
            free(b);
        }
        /* a report repeated */
        k = 0;
        for (uint32_t i = 0; i < n; i++) {
            keep[k++] = i;
            if (i == first_rep) keep[k++] = i;
        }
        b = resign(f, keep, k, &bl);
        CHECK_RC(zmc_verify_journal(f->e.cap, b, bl, &t, &v), ZIND_E_CHAIN);
        CHECK(v.bad_index == first_rep + 1, "repeat at %u", v.bad_index);
        free(b);
        /* a decision repeated */
        k = 0;
        for (uint32_t i = 0; i < n; i++) {
            keep[k++] = i;
            if (i == first_dec) keep[k++] = i;
        }
        b = resign(f, keep, k, &bl);
        CHECK_RC(zmc_verify_journal(f->e.cap, b, bl, &t, &v), ZIND_E_CHAIN);
        CHECK(v.bad_index == first_dec + 1, "decision repeat at %u", v.bad_index);
        free(b);
        /* an approval before its task */
        k = 0;
        keep[k++] = 0;
        keep[k++] = first_app;
        b = resign(f, keep, k, &bl);
        CHECK_RC(zmc_verify_journal(f->e.cap, b, bl, &t, &v), ZIND_E_CHAIN);
        CHECK(v.bad_index == 1, "approval first at %u", v.bad_index);
        free(b);
        /* no incident record first */
        keep[0] = first_task;
        b = resign(f, keep, 1, &bl);
        CHECK_RC(zmc_verify_journal(f->e.cap, b, bl, &t, &v), ZXV_E_FORMAT);
        free(b);
        /* the same task twice */
        k = 0;
        for (uint32_t i = 0; i < n; i++) {
            keep[k++] = i;
            if (i == first_task) keep[k++] = i;
        }
        b = resign(f, keep, k, &bl);
        CHECK_RC(zmc_verify_journal(f->e.cap, b, bl, &t, &v), ZIND_E_CHAIN);
        free(b);
        /* an empty journal */
        b = resign(f, keep, 0, &bl);
        CHECK_RC(zmc_verify_journal(f->e.cap, b, bl, &t, &v), ZXV_E_FORMAT);
        free(b);
        /* records with unknown kinds or bad payloads */
        {
            zind_chain *c = NULL;
            uint8_t junk[40] = {0};
            static const uint16_t kinds2[] = {0, 7, ZMC_J_REPORT, ZMC_J_DECISION, ZMC_J_RESOURCE,
                                              ZMC_J_TASK, ZMC_J_APPROVAL, 999};
            for (size_t q = 0; q < sizeof kinds2 / sizeof kinds2[0]; q++) {
                CHECK_RC(zind_chain_create(ZMC_JOURNAL, 8, &c), ZXV_OK);
                zind_chain_at(zmc_journal(f->inc), 0, &r);
                CHECK_RC(zind_chain_append(f->e.cap, c, f->op, r.kind, r.payload, r.payload_len,
                                           NULL, NULL),
                         ZXV_OK);
                CHECK_RC(zind_chain_append(f->e.cap, c, f->op, kinds2[q], junk, sizeof junk, NULL,
                                           NULL),
                         ZXV_OK);
                b = export_journal(c, &bl);
                CHECK_RC(zmc_verify_journal(f->e.cap, b, bl, &t, &v), ZXV_E_FORMAT);
                CHECK(v.bad_index == 1, "kind %u at %u", kinds2[q], v.bad_index);
                free(b);
                zind_chain_destroy(c);
            }
        }
    }
    free(j);
    CHECK_RC(zxv_hexlog_verify(f->qlog), ZXV_OK);
}

int main(void) {
    fx_t f;
    test_report_check();
    if (fx_open(&f) != 0) {
        printf("fixture failed: %s\n", zt_last_err());
        return 2;
    }
    test_wire(&f);
    test_setup(&f);
    test_fact_capacity(&f);
    test_ingest(&f);
    test_decisions(&f);
    test_advice(&f);
    test_journal(&f);
    fx_close(&f);
    return zt_report("test_command");
}
