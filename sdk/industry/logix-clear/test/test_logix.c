/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_logix.c — Logix-Clear: ISO 6346, the rule table, the lifecycle on
 * the hexagram phase, BOTH holds and broker decisions (forged, replayed,
 * cross-shipment), exact duties and rounding, funds, overflow, and the
 * signed record chain (tamper, untrusted key). */
#include "zind_test.h"
#include "zlogix.h"

#undef CHECK_RC
#define CHECK_RC(expr, want)                                                                   \
    do {                                                                                       \
        int32_t rc_ = (expr);                                                                  \
        CHECK(rc_ == (want), "%s -> %d (%s), want %d (%s)", #expr, (int) rc_,                  \
              zlc_status_name(rc_), (int) (want), zlc_status_name(want));                     \
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

typedef struct {
    zt_env e;
    zind_signer *desk_key, *broker, *rogue;
    zxv_hexlog *qlog;
    zlc_desk *d;
    uint32_t issuer, payer, duty_cn, duty_us, duty_mx;
} fx_t;

static int fx_open(fx_t *f) {
    memset(f, 0, sizeof *f);
    if (zt_env_open(&f->e, "USD") != 0) return -1;
    f->desk_key = zt_signer(f->e.cap, 1);
    f->broker = zt_signer(f->e.cap, 2);
    f->rogue = zt_signer(f->e.cap, 3);
    if (!f->desk_key || !f->broker || !f->rogue) return -1;
    if (zxv_hexlog_create(f->e.cap, 256, &f->qlog) != ZXV_OK) return -1;
    if (zlc_desk_create(f->e.cap, "USD", f->desk_key, f->qlog, &f->d) != ZXV_OK) return -1;
    if (zlc_jurisdiction_add(f->d, "CN", &f->duty_cn) != ZXV_OK) return -1;
    if (zlc_jurisdiction_add(f->d, "US", &f->duty_us) != ZXV_OK) return -1;
    if (zlc_jurisdiction_add(f->d, "MX", &f->duty_mx) != ZXV_OK) return -1;
    if (zlc_broker_add(f->d, zind_signer_pk(f->broker)) != ZXV_OK) return -1;
    f->issuer = zt_issuer(f->e.cap, "USD");
    if (zxv_ledger_open_account_cap(f->e.cap, "importer", "USD", ZXV_ACCT_HOLDER, &f->payer) !=
        ZXV_OK)
        return -1;
    if (zt_fund(f->e.cap, "USD", f->issuer, f->payer, 100000000, "fund-1") != 0) return -1;
    return 0;
}

static void fx_close(fx_t *f) {
    zlc_desk_destroy(f->d);
    zxv_hexlog_destroy(f->qlog);
    zind_signer_destroy(f->desk_key);
    zind_signer_destroy(f->broker);
    zind_signer_destroy(f->rogue);
    zt_env_close(&f->e);
}

static void add_rule(fx_t *f, const char *j, const char *hs, const char *o, int32_t a, zind_qty r) {
    zlc_rule rule = {j, hs, o, a, r};
    CHECK_RC(zlc_rule_add(f->d, &rule), ZXV_OK);
}

/* A container number with a correct check digit from an owner code and a
 * serial number. */
static void make_container(char out[12], const char *owner3, uint32_t serial) {
    snprintf(out, 12, "%sU%06u", owner3, serial % 1000000u);
    out[10] = (char) ('0' + zlc_container_digit(out));
    out[11] = 0;
}

static uint32_t book(fx_t *f, const char *cont, const char *r0, const char *r1) {
    const char *route[2] = {r0, r1};
    uint32_t id = 0;
    CHECK_RC(zlc_book(f->d, cont, route, r1 ? 2 : 1, f->payer, &id), ZXV_OK);
    return id;
}

static int32_t state_of(fx_t *f, uint32_t id, uint8_t *tag) {
    int32_t st = -1;
    uint8_t t = 0xff;
    (void) zlc_shipment_state(f->d, id, &st, &t);
    if (tag) *tag = t;
    return st;
}

static int sign_broker(fx_t *f, const zind_signer *k, uint32_t id, int32_t decision,
                       uint8_t sig[ZIND_SIG_BYTES]) {
    uint8_t msg[128];
    size_t len = 0;
    if (zlc_broker_message(f->d, id, decision, msg, sizeof msg, &len) != ZXV_OK) return -1;
    return zind_sign(f->e.cap, k, msg, len, "logix-clear/broker", sig) == ZXV_OK ? 0 : -1;
}

static void test_iso6346(void) {
    char c[16];
    uint64_t s = 42;
    CHECK_RC(zlc_container_check("CSQU3054383"), ZXV_OK);
    for (char d = '0'; d <= '9'; d++) {
        snprintf(c, sizeof c, "CSQU305438%c", d);
        CHECK_RC(zlc_container_check(c), d == '3' ? ZXV_OK : ZLC_E_CONTAINER);
    }
    CHECK(zlc_container_digit("CSQU305438") == 3, "CSQU305438 -> 3");
    CHECK_RC(zlc_container_check("CSQX3054383"), ZLC_E_CONTAINER);
    CHECK_RC(zlc_container_check("csqu3054383"), ZLC_E_CONTAINER);
    CHECK_RC(zlc_container_check("CSQU305438"), ZLC_E_CONTAINER);
    CHECK_RC(zlc_container_check("CSQU30543833"), ZLC_E_CONTAINER);
    CHECK_RC(zlc_container_check("C5QU3054383"), ZLC_E_CONTAINER);
    CHECK_RC(zlc_container_check("CSQU30A4383"), ZLC_E_CONTAINER);
    CHECK_RC(zlc_container_check(NULL), ZLC_E_CONTAINER);
    CHECK(zlc_container_digit(NULL) == -1 && zlc_container_digit("12345") == -1, "bad digit input");
    /* random owners and serials: the computed digit verifies; any other digit
     * does not (except the 10 -> 0 alias of ISO 6346) */
    for (int i = 0; i < 120; i++) {
        char owner[4];
        int32_t dg;
        for (int k = 0; k < 3; k++) owner[k] = (char) ('A' + zt_rand(&s) % 26);
        owner[3] = 0;
        make_container(c, owner, (uint32_t) zt_rand(&s));
        dg = c[10] - '0';
        CHECK_RC(zlc_container_check(c), ZXV_OK);
        c[10] = (char) ('0' + (dg + 1 + (int) (zt_rand(&s) % 9)) % 10);
        CHECK_RC(zlc_container_check(c), ZLC_E_CONTAINER);
        c[3] = 'J';
        c[10] = (char) ('0' + zlc_container_digit(c));
        CHECK_RC(zlc_container_check(c), ZXV_OK);
    }
}

static void test_setup_refusals(fx_t *f) {
    zlc_desk *d2 = NULL;
    zxv_cap *weak = NULL, *noattest = NULL;
    uint32_t acct = 0, eur = 0, id = 0;
    zlc_rule r;
    char code[24];

    CHECK_RC(zlc_desk_create(f->e.cap, "ABC", f->desk_key, NULL, &d2), ZXV_E_CURRENCY);
    CHECK_RC(zlc_desk_create(f->e.cap, "XAU", f->desk_key, NULL, &d2), ZXV_E_CURRENCY);
    CHECK_RC(zlc_desk_create(f->e.cap, "USD", NULL, NULL, &d2), ZXV_E_ARG);
    CHECK_RC(zxv_cap_attenuate(f->e.cap, ZXV_CAP_LEDGER_READ | ZXV_CAP_CRYPTO_SIGN, &weak), ZXV_OK);
    CHECK_RC(zlc_desk_create(weak, "USD", f->desk_key, NULL, &d2), ZXV_E_CAP);
    CHECK_RC(zxv_cap_attenuate(f->e.cap, ZXV_CAP_ALL & ~ZXV_CAP_ATTEST, &noattest), ZXV_OK);
    CHECK_RC(zlc_desk_create(noattest, "USD", f->desk_key, f->qlog, &d2), ZXV_E_CAP);
    CHECK_RC(zlc_desk_create(noattest, "USD", f->desk_key, NULL, &d2), ZXV_OK);
    zlc_desk_destroy(d2);
    zxv_cap_destroy(weak);
    zxv_cap_destroy(noattest);

    CHECK_RC(zlc_jurisdiction_add(f->d, "U", &acct), ZLC_E_JURIS);
    CHECK_RC(zlc_jurisdiction_add(f->d, "us", &acct), ZLC_E_JURIS);
    CHECK_RC(zlc_jurisdiction_add(f->d, "ABCDEFGHI", &acct), ZLC_E_JURIS);
    CHECK_RC(zlc_jurisdiction_add(f->d, "U$", &acct), ZLC_E_JURIS);
    CHECK_RC(zlc_jurisdiction_add(f->d, NULL, &acct), ZLC_E_JURIS);
    CHECK_RC(zlc_jurisdiction_add(f->d, "US", &acct), ZXV_E_DUPLICATE);
    CHECK_RC(zlc_jurisdiction_add(f->d, "EU27", &acct), ZXV_OK);
    for (int i = 0; i < 40; i++) {
        snprintf(code, sizeof code, "J%02u", (unsigned) i % 100u);
        int32_t st = zlc_jurisdiction_add(f->d, code, NULL);
        CHECK(st == (i < 28 ? ZXV_OK : ZXV_E_CAPACITY), "jurisdiction %d -> %s", i,
              zlc_status_name(st));
    }

    r = (zlc_rule){"XX", "", "*", ZLC_RULE_RELEASE, ZIND_QTY_ZERO};
    CHECK_RC(zlc_rule_add(f->d, &r), ZLC_E_JURIS);
    r = (zlc_rule){"US", "84a", "*", ZLC_RULE_RELEASE, ZIND_QTY_ZERO};
    CHECK_RC(zlc_rule_add(f->d, &r), ZXV_E_ARG);
    r = (zlc_rule){"US", "12345678901", "*", ZLC_RULE_RELEASE, ZIND_QTY_ZERO};
    CHECK_RC(zlc_rule_add(f->d, &r), ZXV_E_ARG);
    r = (zlc_rule){"US", "84", "usa", ZLC_RULE_RELEASE, ZIND_QTY_ZERO};
    CHECK_RC(zlc_rule_add(f->d, &r), ZXV_E_ARG);
    r = (zlc_rule){"US", "84", "cn", ZLC_RULE_RELEASE, ZIND_QTY_ZERO};
    CHECK_RC(zlc_rule_add(f->d, &r), ZXV_E_ARG);
    r = (zlc_rule){"US", "84", "*", ZLC_RULE_RELEASE, Q(-1, 10)};
    CHECK_RC(zlc_rule_add(f->d, &r), ZXV_E_ARG);
    r = (zlc_rule){"US", "84", "*", ZLC_RULE_RELEASE, Q(101, 10)};
    CHECK_RC(zlc_rule_add(f->d, &r), ZXV_E_ARG);
    r = (zlc_rule){"US", "84", "*", ZLC_RULE_REFUSE, Q(1, 10)};
    CHECK_RC(zlc_rule_add(f->d, &r), ZXV_E_ARG);
    r = (zlc_rule){"US", "84", "*", 9, ZIND_QTY_ZERO};
    CHECK_RC(zlc_rule_add(f->d, &r), ZXV_E_ARG);
    r = (zlc_rule){"US", "84", "*", ZLC_RULE_RELEASE, (zind_qty){2, 4}};
    CHECK_RC(zlc_rule_add(f->d, &r), ZXV_E_ARG);
    r = (zlc_rule){"EU27", "84", "*", ZLC_RULE_RELEASE, Q(10, 1)};
    CHECK_RC(zlc_rule_add(f->d, &r), ZXV_OK);
    CHECK_RC(zlc_rule_add(f->d, &r), ZXV_E_DUPLICATE);
    CHECK_RC(zlc_rule_add(f->d, NULL), ZXV_E_ARG);

    CHECK_RC(zlc_broker_add(f->d, zind_signer_pk(f->broker)), ZXV_E_DUPLICATE);
    CHECK_RC(zlc_broker_add(f->d, NULL), ZXV_E_ARG);

    {
        const char *route[9] = {"CN", "US", "MX", "EU27", "J00", "J01", "J02", "J03", "J04"};
        const char *rep[2] = {"US", "US"};
        const char *unk[1] = {"ZZ"};
        CHECK_RC(zlc_book(f->d, "CSQU3054384", route, 2, f->payer, &id), ZLC_E_CONTAINER);
        CHECK_RC(zlc_book(f->d, "CSQU3054383", route, 0, f->payer, &id), ZXV_E_ARG);
        CHECK_RC(zlc_book(f->d, "CSQU3054383", route, 9, f->payer, &id), ZXV_E_ARG);
        CHECK_RC(zlc_book(f->d, "CSQU3054383", rep, 2, f->payer, &id), ZXV_E_DUPLICATE);
        CHECK_RC(zlc_book(f->d, "CSQU3054383", unk, 1, f->payer, &id), ZLC_E_JURIS);
        CHECK_RC(zlc_book(f->d, "CSQU3054383", route, 2, 999999, &id), ZXV_E_NOT_FOUND);
        CHECK_RC(zxv_ccy_enable_cap(f->e.cap, "EUR"), ZXV_OK);
        CHECK_RC(zxv_ledger_open_account_cap(f->e.cap, "eur", "EUR", ZXV_ACCT_HOLDER, &eur),
                 ZXV_OK);
        CHECK_RC(zlc_book(f->d, "CSQU3054383", route, 2, eur, &id), ZXV_E_CURRENCY);
        CHECK_RC(zlc_book(f->d, "CSQU3054383", route, 8, f->payer, &id), ZXV_OK);
        CHECK_RC(zlc_book(f->d, "CSQU3054383", route, 1, f->payer, &id), ZXV_E_DUPLICATE);
    }
    CHECK_RC(zlc_set_rounding(f->d, 6), ZXV_E_ARG);
    CHECK_RC(zlc_set_rounding(NULL, 0), ZXV_E_ARG);
    CHECK_RC(zlc_shipment_state(f->d, 0, NULL, NULL), ZXV_E_NOT_FOUND);
    CHECK_RC(zlc_shipment_state(f->d, 9999, NULL, NULL), ZXV_E_NOT_FOUND);
    CHECK_RC(zlc_release(NULL, 1, NULL), ZXV_E_ARG);
}

static void test_lines_and_lifecycle(fx_t *f) {
    char c[12];
    uint32_t id;
    uint8_t tag = 0, base = 0, pol = 0, ph = 0;
    int32_t act = -1;
    make_container(c, "LXC", 1);
    id = book(f, c, "CN", "US");
    CHECK(state_of(f, id, &tag) == ZLC_ST_BOOKED, "booked");
    CHECK(zxv_hex_unpack(tag, &base, &pol, &ph) == ZXV_OK && base == ZXV_HEX_BASE_NEITHER &&
              ph == ZXV_HEX_PHASE_POTENTIAL,
          "booked = neither/potential (%s)", zxv_hex_name(tag));
    CHECK_RC(zlc_depart(f->d, id), ZXV_E_STATE); /* no lines */
    CHECK_RC(zlc_line_add(f->d, id, "84713", "CN", P("10")), ZXV_E_ARG);
    CHECK_RC(zlc_line_add(f->d, id, "84713012345", "CN", P("10")), ZXV_E_ARG);
    CHECK_RC(zlc_line_add(f->d, id, "8471300", "cn", P("10")), ZXV_E_ARG);
    CHECK_RC(zlc_line_add(f->d, id, "8471300", "CHN", P("10")), ZXV_E_ARG);
    CHECK_RC(zlc_line_add(f->d, id, "8471300", "CN", P("0")), ZXV_E_ARG);
    CHECK_RC(zlc_line_add(f->d, id, "8471300", "CN", P("-5")), ZXV_E_ARG);
    CHECK_RC(zlc_line_add(f->d, id, "8471300", "CN", (zind_qty){2, 4}), ZXV_E_ARG);
    CHECK_RC(zlc_line_add(f->d, 0, "8471300", "CN", P("1")), ZXV_E_NOT_FOUND);
    for (int i = 0; i < 34; i++) {
        int32_t st = zlc_line_add(f->d, id, "8471300", "CN", Q(i + 1, 1));
        CHECK(st == (i < 32 ? ZXV_OK : ZXV_E_CAPACITY), "line %d", i);
    }
    CHECK_RC(zlc_release(f->d, id, NULL), ZXV_E_STATE); /* not in transit */
    CHECK_RC(zlc_ruling_record(f->d, id, "US", ZXV_LP_TRUE), ZXV_E_STATE);
    CHECK_RC(zlc_depart(f->d, id), ZXV_OK);
    CHECK(state_of(f, id, &tag) == ZLC_ST_IN_TRANSIT, "in transit");
    CHECK(zxv_hex_unpack(tag, NULL, NULL, &ph) == ZXV_OK && ph == ZXV_HEX_PHASE_EMERGING,
          "in transit = emerging");
    CHECK(zxv_hex_action(tag, &act) == ZXV_OK && act == ZXV_HEX_ACT_WAIT, "neither waits");
    CHECK_RC(zlc_depart(f->d, id), ZXV_E_STATE);
    CHECK_RC(zlc_line_add(f->d, id, "8471300", "CN", P("1")), ZXV_E_STATE);
    CHECK_RC(zlc_close(f->d, id), ZXV_E_STATE);
    /* no rules for these jurisdictions yet: pending */
    CHECK_RC(zlc_release(f->d, id, NULL), ZLC_E_PENDING);
    CHECK(state_of(f, id, NULL) == ZLC_ST_IN_TRANSIT, "pending changes nothing");
    CHECK_RC(zlc_ruling_record(f->d, id, "MX", ZXV_LP_TRUE), ZLC_E_JURIS); /* not on route */
    CHECK_RC(zlc_ruling_record(f->d, id, "US", ZXV_LP_BOTH), ZXV_E_ARG);
    CHECK_RC(zlc_ruling_record(f->d, id, "US", ZXV_LP_NEITHER), ZXV_E_ARG);
    CHECK_RC(zlc_ruling_record(f->d, id, "CN", ZXV_LP_TRUE), ZXV_OK);
    CHECK_RC(zlc_release(f->d, id, NULL), ZLC_E_PENDING); /* US still silent */
    CHECK_RC(zlc_ruling_record(f->d, id, "US", ZXV_LP_TRUE), ZXV_OK);
    {
        zlc_release_info info;
        memset(&info, 0, sizeof info);
        CHECK_RC(zlc_release(f->d, id, &info), ZXV_OK);
        CHECK(info.lp == ZXV_LP_TRUE && info.total_minor == 0 && info.duty_count == 0,
              "released by rulings, no duty rules");
    }
    CHECK(state_of(f, id, &tag) == ZLC_ST_RELEASED, "released");
    CHECK(zxv_hex_unpack(tag, &base, &pol, &ph) == ZXV_OK && base == ZXV_HEX_BASE_TRUE &&
              ph == ZXV_HEX_PHASE_MANIFEST,
          "released = true/manifest (%s)", zxv_hex_name(tag));
    CHECK(zxv_hex_action(tag, &act) == ZXV_OK && act == ZXV_HEX_ACT_PROCEED, "proceed");
    CHECK_RC(zlc_release(f->d, id, NULL), ZIND_E_REPLAY);
    CHECK_RC(zlc_ruling_record(f->d, id, "US", ZXV_LP_FALSE), ZXV_E_STATE);
    CHECK_RC(zlc_close(f->d, id), ZXV_OK);
    CHECK(state_of(f, id, &tag) == ZLC_ST_CLOSED, "closed");
    CHECK(zxv_hex_unpack(tag, NULL, NULL, &ph) == ZXV_OK && ph == ZXV_HEX_PHASE_DISSOLVING,
          "closed = dissolving");
    CHECK_RC(zlc_close(f->d, id), ZXV_E_STATE);
    CHECK_RC(zlc_release(f->d, id, NULL), ZXV_E_STATE);
    /* the container can be booked again once closed */
    {
        uint32_t id2 = book(f, c, "US", NULL);
        CHECK(id2 == id + 1, "rebooked as a new shipment");
    }
}

static void test_duties(fx_t *f) {
    char c[12];
    uint32_t id;
    zind_qty ex, res;
    int64_t posted = -1, payer0, us0, cn0;
    zlc_release_info info;

    /* CN releases exports of 8471 with no duty; US charges 5% on 8471 from CN,
     * 2.5% from anywhere; the longest prefix wins. */
    add_rule(f, "CN", "8471", "*", ZLC_RULE_RELEASE, ZIND_QTY_ZERO);
    add_rule(f, "US", "8471", "CN", ZLC_RULE_RELEASE, Q(5, 100));
    add_rule(f, "US", "8471", "*", ZLC_RULE_RELEASE, Q(25, 1000));
    add_rule(f, "US", "847130", "*", ZLC_RULE_RELEASE, Q(1, 3));

    make_container(c, "DTY", 7);
    id = book(f, c, "CN", "US");
    CHECK_RC(zlc_line_add(f->d, id, "84714100", "CN", P("1234.50")), ZXV_OK);
    CHECK_RC(zlc_depart(f->d, id), ZXV_OK);
    CHECK_RC(zlc_duty(f->d, id, "US", &ex, &posted, &res), ZXV_OK);
    CHECK(ex.num == 2469 && ex.den == 40, "5%% of 1234.50 = 61.725 exactly (%lld/%lld)",
          (long long) ex.num, (long long) ex.den);
    CHECK_RC(zlc_duty(f->d, id, "CN", &ex, NULL, NULL), ZXV_OK);
    CHECK(ex.num == 0, "no CN duty");
    CHECK_RC(zlc_duty(f->d, id, "MX", &ex, NULL, NULL), ZLC_E_JURIS);
    payer0 = zt_equity(f->e.cap, f->payer);
    us0 = zt_equity(f->e.cap, f->duty_us);
    cn0 = zt_equity(f->e.cap, f->duty_cn);
    /* 61.725 has no exact cent form: refused under the default rule */
    CHECK_RC(zlc_release(f->d, id, &info), ZIND_E_INEXACT);
    CHECK(state_of(f, id, NULL) == ZLC_ST_IN_TRANSIT, "unchanged after INEXACT");
    CHECK(zt_equity(f->e.cap, f->payer) == payer0, "nothing posted");
    CHECK_RC(zlc_set_rounding(f->d, ZIND_ROUND_HALF_EVEN), ZXV_OK);
    CHECK_RC(zlc_release(f->d, id, &info), ZXV_OK);
    CHECK(info.total_minor == 6172 && info.duty_count == 1, "61.725 half-even = 61.72 (%lld)",
          (long long) info.total_minor);
    CHECK_RC(zlc_duty(f->d, id, "US", &ex, &posted, &res), ZXV_OK);
    CHECK(posted == 6172 && res.num == 1 && res.den == 200, "posted 6172, residual 1/200");
    CHECK(zt_equity(f->e.cap, f->payer) == payer0 - 6172, "payer debited exactly");
    CHECK(zt_equity(f->e.cap, f->duty_us) == us0 + 6172, "US duty account credited");
    CHECK(zt_equity(f->e.cap, f->duty_cn) == cn0, "CN untouched");
    CHECK_RC(zxv_ledger_check_cap(f->e.cap), ZXV_OK);

    /* most specific prefix: 847130 at 1/3 */
    make_container(c, "DTY", 8);
    id = book(f, c, "CN", "US");
    CHECK_RC(zlc_line_add(f->d, id, "84713000", "VN", P("300")), ZXV_OK);
    CHECK_RC(zlc_line_add(f->d, id, "84719000", "VN", P("40")), ZXV_OK); /* 2.5% "*" */
    CHECK_RC(zlc_depart(f->d, id), ZXV_OK);
    CHECK_RC(zlc_duty(f->d, id, "US", &ex, NULL, NULL), ZXV_OK);
    CHECK(ex.num == 101 && ex.den == 1, "100 + 1 = 101 (%lld/%lld)", (long long) ex.num,
          (long long) ex.den);
    CHECK_RC(zlc_release(f->d, id, &info), ZXV_OK);
    CHECK(info.total_minor == 10100, "101.00");

    /* rounding rules all reach the ledger as stated */
    {
        static const int modes[] = {ZIND_ROUND_HALF_UP, ZIND_ROUND_CEIL, ZIND_ROUND_FLOOR,
                                    ZIND_ROUND_TRUNC};
        static const int64_t want[] = {6173, 6173, 6172, 6172};
        for (int m = 0; m < 4; m++) {
            make_container(c, "RND", (uint32_t) (100 + m));
            id = book(f, c, "US", NULL);
            CHECK_RC(zlc_line_add(f->d, id, "84714100", "CN", P("1234.50")), ZXV_OK);
            CHECK_RC(zlc_depart(f->d, id), ZXV_OK);
            CHECK_RC(zlc_set_rounding(f->d, modes[m]), ZXV_OK);
            CHECK_RC(zlc_release(f->d, id, &info), ZXV_OK);
            CHECK(info.total_minor == want[m], "mode %d -> %lld", modes[m],
                  (long long) info.total_minor);
        }
    }

    /* insufficient funds: refused before anything is posted */
    {
        uint32_t poor = 0;
        const char *route[1] = {"US"};
        CHECK_RC(zxv_ledger_open_account_cap(f->e.cap, "poor", "USD", ZXV_ACCT_HOLDER, &poor),
                 ZXV_OK);
        CHECK(zt_fund(f->e.cap, "USD", f->issuer, poor, 1000, "fund-poor") == 0, "fund poor");
        make_container(c, "PUR", 1);
        CHECK_RC(zlc_book(f->d, c, route, 1, poor, &id), ZXV_OK);
        CHECK_RC(zlc_line_add(f->d, id, "84714100", "CN", P("1000")), ZXV_OK); /* 50.00 */
        CHECK_RC(zlc_depart(f->d, id), ZXV_OK);
        CHECK_RC(zlc_release(f->d, id, &info), ZXV_E_FUNDS);
        CHECK(state_of(f, id, NULL) == ZLC_ST_IN_TRANSIT, "unchanged after FUNDS");
        CHECK(zt_equity(f->e.cap, poor) == 1000, "poor untouched");
        CHECK(zt_fund(f->e.cap, "USD", f->issuer, poor, 4000, "fund-poor-2") == 0, "top up");
        CHECK_RC(zlc_release(f->d, id, &info), ZXV_OK);
        CHECK(zt_equity(f->e.cap, poor) == 0, "exactly drained");
    }

    /* overflow of the exact duty is refused, never wrapped */
    {
        add_rule(f, "MX", "", "*", ZLC_RULE_RELEASE, Q(10, 1));
        make_container(c, "OVF", 1);
        id = book(f, c, "MX", NULL);
        CHECK_RC(zlc_line_add(f->d, id, "99999999", "US", Q(INT64_MAX / 4, 1)), ZXV_OK);
        CHECK_RC(zlc_depart(f->d, id), ZXV_OK);
        CHECK_RC(zlc_release(f->d, id, &info), ZXV_E_RANGE);
        CHECK(state_of(f, id, NULL) == ZLC_ST_IN_TRANSIT, "unchanged after RANGE");
        CHECK_RC(zlc_duty(f->d, id, "MX", &ex, NULL, NULL), ZXV_E_RANGE);
    }
    CHECK_RC(zxv_ledger_check_cap(f->e.cap), ZXV_OK);
}

static void test_conflicts(fx_t *f) {
    char c[12];
    uint32_t a, b, n0 = 0, n1 = 0, rf;
    uint8_t tag = 0, base = 0, sig[ZIND_SIG_BYTES], sig_b[ZIND_SIG_BYTES];
    int32_t lp = -1, per[4] = {-1, -1, -1, -1}, act = -1;
    int64_t payer0;
    zlc_release_info info;

    CHECK_RC(zlc_set_rounding(f->d, ZIND_ROUND_EXACT), ZXV_OK);
    /* CN refuses exports of 8542 (export control); US would release */
    add_rule(f, "CN", "8542", "*", ZLC_RULE_REFUSE, ZIND_QTY_ZERO);
    add_rule(f, "US", "8542", "*", ZLC_RULE_RELEASE, Q(1, 100));

    make_container(c, "CFL", 1);
    a = book(f, c, "CN", "US");
    CHECK_RC(zlc_line_add(f->d, a, "85423100", "CN", P("5000")), ZXV_OK);
    CHECK_RC(zlc_depart(f->d, a), ZXV_OK);
    CHECK_RC(zlc_evaluate(f->d, a, &lp, per, 4), ZXV_OK);
    CHECK(lp == ZXV_LP_BOTH && per[0] == ZXV_LP_FALSE && per[1] == ZXV_LP_TRUE,
          "CN FALSE + US TRUE = BOTH (%s; %s %s)", zxv_lp_name(lp), zxv_lp_name(per[0]),
          zxv_lp_name(per[1]));
    CHECK_RC(zlc_evaluate(f->d, a, NULL, NULL, 0), ZXV_E_ARG);
    payer0 = zt_equity(f->e.cap, f->payer);
    CHECK_RC(zxv_hexlog_count(f->qlog, &n0), ZXV_OK);
    CHECK_RC(zlc_release(f->d, a, &info), ZIND_E_HELD);
    CHECK(state_of(f, a, &tag) == ZLC_ST_HELD, "held");
    CHECK(zxv_hex_unpack(tag, &base, NULL, NULL) == ZXV_OK && base == ZXV_HEX_BASE_BOTH,
          "held = both (%s)", zxv_hex_name(tag));
    CHECK(zxv_hex_action(tag, &act) == ZXV_OK && act == ZXV_HEX_ACT_ESCALATE,
          "both escalates to a human");
    CHECK_RC(zxv_hexlog_count(f->qlog, &n1), ZXV_OK);
    CHECK(n1 == n0 + 1, "hold quarantined");
    {
        uint8_t rt = 0, rb = 0, rp = 0;
        uint32_t who = 0;
        CHECK_RC(zxv_hexlog_at(f->qlog, n1 - 1, NULL, &rt, NULL, &who, NULL, NULL, NULL), ZXV_OK);
        CHECK(zxv_hex_unpack(rt, &rb, &rp, NULL) == ZXV_OK && rb == ZXV_HEX_BASE_BOTH &&
                  rp == ZXV_HEX_POL_FORBIDDEN && who == a,
              "log record both/forbidden for shipment %u", a);
    }
    /* frozen: never auto-released, whatever else arrives */
    CHECK_RC(zlc_release(f->d, a, &info), ZIND_E_HELD);
    CHECK_RC(zlc_close(f->d, a), ZIND_E_HELD);
    CHECK_RC(zlc_ruling_record(f->d, a, "CN", ZXV_LP_TRUE), ZXV_OK); /* evidence still recorded */
    CHECK_RC(zlc_release(f->d, a, &info), ZIND_E_HELD);
    CHECK(zt_equity(f->e.cap, f->payer) == payer0, "no duty while held");

    /* a second held shipment, for cross-shipment replay */
    make_container(c, "CFL", 2);
    b = book(f, c, "CN", "US");
    CHECK_RC(zlc_line_add(f->d, b, "85423100", "CN", P("100")), ZXV_OK);
    CHECK_RC(zlc_depart(f->d, b), ZXV_OK);
    CHECK_RC(zlc_release(f->d, b, &info), ZIND_E_HELD);

    /* broker decisions */
    CHECK(sign_broker(f, f->broker, a, ZXV_LP_TRUE, sig) == 0, "broker signs");
    CHECK(sign_broker(f, f->broker, b, ZXV_LP_TRUE, sig_b) == 0, "broker signs b");
    CHECK_RC(zlc_broker_resolve(f->d, a, zind_signer_pk(f->rogue), ZXV_LP_TRUE, sig, &info),
             ZLC_E_BROKER);
    CHECK_RC(zlc_broker_resolve(f->d, a, zind_signer_pk(f->broker), ZXV_LP_BOTH, sig, &info),
             ZXV_E_ARG);
    CHECK_RC(zxv_hexlog_count(f->qlog, &n0), ZXV_OK);
    /* the signature for b does not release a; it is quarantined as forged */
    CHECK_RC(zlc_broker_resolve(f->d, a, zind_signer_pk(f->broker), ZXV_LP_TRUE, sig_b, &info),
             ZXV_E_VERIFY);
    /* a signature on "release" does not authorize "refuse" */
    CHECK_RC(zlc_broker_resolve(f->d, a, zind_signer_pk(f->broker), ZXV_LP_FALSE, sig, &info),
             ZXV_E_VERIFY);
    {
        uint8_t bad[ZIND_SIG_BYTES];
        memcpy(bad, sig, sizeof bad);
        bad[7] ^= 0x10;
        CHECK_RC(zlc_broker_resolve(f->d, a, zind_signer_pk(f->broker), ZXV_LP_TRUE, bad, &info),
                 ZXV_E_VERIFY);
    }
    CHECK_RC(zxv_hexlog_count(f->qlog, &n1), ZXV_OK);
    CHECK(n1 == n0 + 3, "three forged decisions quarantined (%u)", n1 - n0);
    CHECK(state_of(f, a, NULL) == ZLC_ST_HELD, "still held");
    /* a desk cap without ATTEST cannot apply a decision */
    {
        zxv_cap *noattest = NULL;
        zlc_desk *d2 = NULL;
        uint32_t s2 = 0, j = 0;
        const char *route[2] = {"CN", "US"};
        zlc_rule r1 = {"CN", "8542", "*", ZLC_RULE_REFUSE, ZIND_QTY_ZERO};
        zlc_rule r2 = {"US", "8542", "*", ZLC_RULE_RELEASE, ZIND_QTY_ZERO};
        CHECK_RC(zxv_cap_attenuate(f->e.cap, ZXV_CAP_ALL & ~ZXV_CAP_ATTEST, &noattest), ZXV_OK);
        CHECK_RC(zlc_desk_create(noattest, "USD", f->desk_key, NULL, &d2), ZXV_OK);
        CHECK_RC(zlc_jurisdiction_add(d2, "CN", &j), ZXV_OK);
        CHECK_RC(zlc_jurisdiction_add(d2, "US", &j), ZXV_OK);
        CHECK_RC(zlc_rule_add(d2, &r1), ZXV_OK);
        CHECK_RC(zlc_rule_add(d2, &r2), ZXV_OK);
        CHECK_RC(zlc_broker_add(d2, zind_signer_pk(f->broker)), ZXV_OK);
        CHECK_RC(zlc_book(d2, "CSQU3054383", route, 2, f->payer, &s2), ZXV_OK);
        CHECK_RC(zlc_line_add(d2, s2, "85423100", "CN", P("1")), ZXV_OK);
        CHECK_RC(zlc_depart(d2, s2), ZXV_OK);
        CHECK_RC(zlc_release(d2, s2, NULL), ZIND_E_HELD); /* held even without a log */
        {
            uint8_t msg[128], s3[ZIND_SIG_BYTES];
            size_t len = 0;
            CHECK_RC(zlc_broker_message(d2, s2, ZXV_LP_TRUE, msg, sizeof msg, &len), ZXV_OK);
            CHECK_RC(zind_sign(f->e.cap, f->broker, msg, len, "logix-clear/broker", s3), ZXV_OK);
            CHECK_RC(zlc_broker_resolve(d2, s2, zind_signer_pk(f->broker), ZXV_LP_TRUE, s3, NULL),
                     ZXV_E_CAP);
        }
        zlc_desk_destroy(d2);
        zxv_cap_destroy(noattest);
    }
    /* the genuine decision releases a and posts its duty */
    CHECK_RC(zlc_broker_resolve(f->d, a, zind_signer_pk(f->broker), ZXV_LP_TRUE, sig, &info),
             ZXV_OK);
    CHECK(info.total_minor == 5000 && info.duty_count == 1, "US 1%% of 5000 = 50.00");
    CHECK(state_of(f, a, &tag) == ZLC_ST_RELEASED, "released by the broker");
    CHECK(zt_equity(f->e.cap, f->payer) == payer0 - 5000, "duty posted after the decision");
    {
        uint32_t cnt = 0;
        zind_record r1, r2;
        zind_chain_count(zlc_records(f->d), &cnt);
        CHECK(zind_chain_at(zlc_records(f->d), cnt - 2, &r1) == ZXV_OK &&
                  r1.kind == ZLC_REC_RELEASE,
              "release record");
        CHECK(zind_chain_at(zlc_records(f->d), cnt - 1, &r2) == ZXV_OK && r2.kind == ZLC_REC_BROKER,
              "broker record follows");
    }
    /* replaying the decision does nothing */
    CHECK_RC(zlc_broker_resolve(f->d, a, zind_signer_pk(f->broker), ZXV_LP_TRUE, sig, &info),
             ZIND_E_REPLAY);
    CHECK_RC(zlc_broker_message(f->d, a, ZXV_LP_TRUE, sig, 10, &(size_t){0}), ZXV_E_STATE);
    /* the broker refuses b */
    CHECK(sign_broker(f, f->broker, b, ZXV_LP_FALSE, sig_b) == 0, "broker refuses b");
    CHECK_RC(zlc_broker_resolve(f->d, b, zind_signer_pk(f->broker), ZXV_LP_FALSE, sig_b, &info),
             ZXV_OK);
    CHECK(state_of(f, b, &tag) == ZLC_ST_REFUSED, "refused by the broker");
    CHECK(zxv_hex_action(tag, &act) == ZXV_OK && act == ZXV_HEX_ACT_REFUSE, "refuse action");
    CHECK_RC(zlc_release(f->d, b, &info), ZLC_E_REFUSED);

    /* one jurisdiction's own evidence conflicting: table TRUE, ruling FALSE */
    make_container(c, "CFL", 3);
    a = book(f, c, "US", NULL);
    CHECK_RC(zlc_line_add(f->d, a, "85423100", "KR", P("10")), ZXV_OK);
    CHECK_RC(zlc_depart(f->d, a), ZXV_OK);
    CHECK_RC(zlc_ruling_record(f->d, a, "US", ZXV_LP_FALSE), ZXV_OK);
    CHECK_RC(zlc_evaluate(f->d, a, &lp, per, 4), ZXV_OK);
    CHECK(lp == ZXV_LP_BOTH && per[0] == ZXV_LP_BOTH, "self-conflict is BOTH");
    CHECK_RC(zlc_release(f->d, a, &info), ZIND_E_HELD);

    /* both jurisdictions refuse: FALSE, not BOTH */
    add_rule(f, "US", "9301", "*", ZLC_RULE_REFUSE, ZIND_QTY_ZERO);
    add_rule(f, "CN", "9301", "*", ZLC_RULE_REFUSE, ZIND_QTY_ZERO);
    make_container(c, "CFL", 4);
    rf = book(f, c, "CN", "US");
    CHECK_RC(zlc_line_add(f->d, rf, "93011000", "CN", P("10")), ZXV_OK);
    CHECK_RC(zlc_depart(f->d, rf), ZXV_OK);
    CHECK_RC(zlc_release(f->d, rf, &info), ZLC_E_REFUSED);
    CHECK(state_of(f, rf, NULL) == ZLC_ST_REFUSED, "refused");
    CHECK_RC(zlc_release(f->d, rf, &info), ZLC_E_REFUSED);
    CHECK_RC(zlc_broker_message(f->d, rf, ZXV_LP_TRUE, sig, sizeof sig, &(size_t){0}), ZXV_E_STATE);

    /* one line inspected, one released: waiting, until a ruling clears it */
    add_rule(f, "US", "0306", "*", ZLC_RULE_INSPECT, ZIND_QTY_ZERO);
    make_container(c, "CFL", 5);
    a = book(f, c, "US", NULL);
    CHECK_RC(zlc_line_add(f->d, a, "03061700", "VN", P("10")), ZXV_OK);
    CHECK_RC(zlc_line_add(f->d, a, "85423100", "VN", P("10")), ZXV_OK);
    CHECK_RC(zlc_depart(f->d, a), ZXV_OK);
    CHECK_RC(zlc_release(f->d, a, &info), ZLC_E_PENDING);
    CHECK_RC(zlc_ruling_record(f->d, a, "US", ZXV_LP_TRUE), ZXV_OK);
    CHECK_RC(zlc_release(f->d, a, &info), ZXV_OK);
    CHECK(info.total_minor == 10, "1%% of the released line only (0.10)");
}

static void test_records(fx_t *f) {
    const zind_chain *c = zlc_records(f->d);
    const uint8_t *trust[1] = {zind_signer_pk(f->desk_key)};
    const uint8_t *wrong[1] = {zind_signer_pk(f->rogue)};
    uint32_t cnt = 0, bad = 0;
    size_t len = 0;
    uint8_t *buf;
    zind_chain *imp = NULL;
    CHECK_RC(zind_chain_count(c, &cnt), ZXV_OK);
    CHECK(cnt > 50, "a long signed history (%u records)", cnt);
    CHECK_RC(zind_chain_verify(f->e.cap, c, trust, 1, &bad), ZXV_OK);
    CHECK_RC(zind_chain_verify(f->e.cap, c, wrong, 1, &bad), ZIND_E_UNTRUSTED);
    CHECK(bad == 0, "first record untrusted");
    CHECK_RC(zind_chain_export(c, NULL, 0, &len), ZXV_E_BUFFER);
    buf = (uint8_t *) malloc(len);
    CHECK_RC(zind_chain_export(c, buf, len, &len), ZXV_OK);
    CHECK_RC(zind_chain_import("logix-clear", buf, len, &imp), ZXV_OK);
    CHECK_RC(zind_chain_verify(f->e.cap, imp, trust, 1, NULL), ZXV_OK);
    zind_chain_destroy(imp);
    /* alter one byte of the payload of every 7th record */
    {
        size_t off = 4 + 1 + 1 + strlen("logix-clear") + 4;
        for (uint32_t i = 0; i < cnt; i++) {
            zind_record r;
            zind_chain_at(c, i, &r);
            if (i % 7 == 0 && r.payload_len > 0) {
                uint8_t *t = (uint8_t *) malloc(len);
                memcpy(t, buf, len);
                t[off + 14 + r.payload_len / 2] ^= 1;
                CHECK_RC(zind_chain_import("logix-clear", t, len, &imp), ZXV_OK);
                CHECK_RC(zind_chain_verify(f->e.cap, imp, trust, 1, &bad), ZIND_E_CHAIN);
                CHECK(bad == i, "tamper found at record %u (got %u)", i, bad);
                zind_chain_destroy(imp);
                free(t);
            }
            off += 14 + r.payload_len + 96 + ZIND_SIG_BYTES;
        }
    }
    free(buf);
    /* every record kind appears */
    {
        int seen[9] = {0};
        for (uint32_t i = 0; i < cnt; i++) {
            zind_record r;
            zind_chain_at(c, i, &r);
            if (r.kind < 9) seen[r.kind] = 1;
        }
        for (int k = ZLC_REC_BOOK; k <= ZLC_REC_CLOSE; k++) CHECK(seen[k], "record kind %d", k);
    }
    CHECK(zlc_records(NULL) == NULL, "NULL desk");
    CHECK(strcmp(zlc_status_name(ZLC_E_PENDING), "ZLC_E_PENDING") == 0, "names");
    CHECK(strcmp(zlc_status_name(ZIND_E_HELD), "ZIND_E_HELD") == 0, "falls back");
    CHECK(strcmp(zlc_status_name(ZXV_E_ARG), zxv_status_name(ZXV_E_ARG)) == 0, "falls back 2");
}

/* Many shipments with random values and rates under HALF_UP: what reaches
 * the ledger plus the residual equals the exact duty, every time. */
static void test_bulk(fx_t *f) {
    uint64_t s = 7;
    int64_t payer0 = zt_equity(f->e.cap, f->payer), us0 = zt_equity(f->e.cap, f->duty_us),
            sum = 0;
    CHECK_RC(zlc_set_rounding(f->d, ZIND_ROUND_HALF_UP), ZXV_OK);
    add_rule(f, "US", "6109", "*", ZLC_RULE_RELEASE, Q(16, 100));
    add_rule(f, "US", "610910", "BD", ZLC_RULE_RELEASE, Q(1, 7));
    for (int i = 0; i < 60; i++) {
        char c[12];
        uint32_t id;
        zind_qty v = Q((int64_t) (zt_rand(&s) % 5000000) + 1, 100), ex, res, mq, back;
        int64_t posted = 0;
        zlc_release_info info;
        make_container(c, "BLK", (uint32_t) i);
        id = book(f, c, "US", NULL);
        CHECK_RC(zlc_line_add(f->d, id, (i % 2) ? "61091000" : "61099000", (i % 3) ? "BD" : "IN",
                              v),
                 ZXV_OK);
        CHECK_RC(zlc_depart(f->d, id), ZXV_OK);
        CHECK_RC(zlc_release(f->d, id, &info), ZXV_OK);
        CHECK_RC(zlc_duty(f->d, id, "US", &ex, &posted, &res), ZXV_OK);
        CHECK(posted == info.total_minor, "posted equals the release total");
        CHECK(zind_qty_from_minor(posted, 2, &mq) == ZXV_OK && zind_qty_add(mq, res, &back) == ZXV_OK &&
                  back.num == ex.num && back.den == ex.den,
              "posted + residual == exact duty for shipment %u", id);
        CHECK(res.num * 200 <= res.den && -res.num * 200 <= res.den, "|residual| <= half a cent");
        sum += posted;
        CHECK_RC(zlc_close(f->d, id), ZXV_OK);
    }
    CHECK(zt_equity(f->e.cap, f->payer) == payer0 - sum, "payer debited by the exact sum");
    CHECK(zt_equity(f->e.cap, f->duty_us) == us0 + sum, "US credited by the exact sum");
    CHECK_RC(zxv_ledger_check_cap(f->e.cap), ZXV_OK);
}

int main(void) {
    fx_t f;
    test_iso6346();
    if (fx_open(&f) != 0) {
        fprintf(stderr, "fixture failed: %s\n", zt_last_err());
        return 2;
    }
    test_setup_refusals(&f);
    test_lines_and_lifecycle(&f);
    test_duties(&f);
    test_conflicts(&f);
    test_bulk(&f);
    test_records(&f);
    fx_close(&f);
    return zt_report("test_logix");
}
