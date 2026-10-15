/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_capctl.c — capital-control profiles (capctl.h C1-C8) with real
 * ML-DSA-87 signatures (cpk.h K1-K4), and the shared manifest helpers
 * (cp_common.h M1-M3).
 *
 * Every profile here is TEST DATA. The country codes are real ISO 3166 codes
 * (checked against kernel/src/cbank) so the format checks are meaningful,
 * but no limit, corridor or threshold below describes any country's actual
 * rules. */
#include <stdlib.h>
#include "cp_test.h"
#include "capctl.h"
#include "../cbank/cb_ccy.h"

#define T0  1767225600ull /* 2026-01-01T00:00:00Z, day 20454 */
#define DAY 86400ull
#define USD 840u
#define NGN 566u
#define GBP 826u

static cp_t_key_t K_NG, K_GB, K_ANY, K_TAX;
static cpk_ring_t RING;

static void base_profile(capctl_profile_t *p, const char *cc, uint32_t version, const uint8_t *kid)
{
    memset(p, 0, sizeof *p);
    p->hdr.kind = CP_KIND_CAPCTL;
    memcpy(p->hdr.country, cc, 2);
    p->hdr.version = version;
    p->hdr.not_before = T0;
    p->hdr.not_after = T0 + 365 * DAY;
    memcpy(p->hdr.key_id, kid, CP_KEYID_LEN);
    p->ccy = NGN;
    p->flags = CAPCTL_F_OUTBOUND | CAPCTL_F_DEFAULT_ALLOW;
}

static uint8_t M[CAPCTL_MANIFEST_MAX];
static uint8_t SIG[CPK_SIG_LEN];

static capctl_status_t install(capctl_t *c, const capctl_profile_t *p, const cp_t_key_t *k,
                               uint64_t now)
{
    size_t n = capctl_encode(p, M, sizeof M);
    if (!n) return CAPCTL_E_DECODE;
    cp_t_sign(k, M, n, SIG);
    return capctl_install(c, M, n, SIG, sizeof SIG, now);
}

static capctl_transfer_t xfer(const char *from, const char *to, uint16_t ccy, uint64_t amount)
{
    capctl_transfer_t t;
    memset(&t, 0, sizeof t);
    memcpy(t.from, from, 2);
    memcpy(t.to, to, 2);
    t.ccy = ccy;
    t.amount = amount;
    t.sender = 1001;
    t.receiver = 2002;
    return t;
}

static capctl_reason_t eval(const capctl_t *c, const capctl_transfer_t *t, uint64_t now,
                            capctl_decision_t *d)
{
    capctl_result_t r;
    *d = capctl_evaluate(c, t, now, &r);
    return r.reason;
}

static void test_int(void)
{
    uint64_t r;
    CHECK(cp_udivmod64(100, 7, &r) == 14 && r == 2, "udivmod64 100/7 = 14 r 2");
    CHECK(cp_udivmod64(UINT64_MAX, 86400, &r) == 213503982334601ull && r == 25215,
          "udivmod64 2^64-1 / 86400 = 213503982334601 r 25215");
    CHECK(cp_udivmod64(UINT64_MAX, (1ull << 63) + 1, &r) == 1 && r == (1ull << 63) - 2,
          "udivmod64 divisor above 2^63");
    CHECK(cp_udivmod64(5, 0, &r) == 0 && r == 0, "udivmod64 by zero returns 0, rem 0");
    CHECK(cp_udivmod64(T0, DAY, &r) == 20454 && r == 0, "2026-01-01 is unix day 20454");
    uint64_t s = 7;
    CHECK(!cp_add(UINT64_MAX, 1, &s) && s == 7, "cp_add overflow refused, output untouched");
    CHECK(cp_add(UINT64_MAX - 1, 1, &s) && s == UINT64_MAX, "cp_add at the edge");
    CHECK(cp_cc_ok("NG", false) && !cp_cc_ok("ng", false) && !cp_cc_ok("**", false) &&
              cp_cc_ok("**", true) && !cp_cc_ok("*A", true),
          "country code format (uppercase, ** only when wild)");
    {
        cp_hdr_t h, g;
        memset(&h, 0, sizeof h);
        h.kind = CP_KIND_TAXTABLE;
        memcpy(h.country, "US", 2);
        h.version = 1;
        uint8_t b[CP_HDR_LEN];
        const char *regs[4] = {"NY", "N\0Y", "ny", "NYC123"};
        bool want[4] = {true, false, false, true};
        for (int k = 0; k < 4; k++) {
            memset(h.region, 0, sizeof h.region);
            memcpy(h.region, regs[k], k == 1 ? 3 : strlen(regs[k]));
            cp_w_t w = {b, sizeof b, 0, false};
            cp_hdr_put(&w, &h);
            cp_r_t rd = {b, sizeof b, 0, false};
            CHECK(!w.bad && w.len == CP_HDR_LEN && cp_hdr_get(&rd, &g, false) == want[k],
                  "header region %s %s", k == 1 ? "N<0>Y" : regs[k],
                  want[k] ? "accepted" : "refused");
        }
        cp_w_t w = {b, sizeof b - 1, 0, false};
        cp_hdr_put(&w, &h);
        CHECK(w.bad, "header writer one byte short sets bad");
    }
    CHECK(cb_country_by_a2("NG") && cb_country_by_a2("GB") && cb_country_by_a2("US") &&
              cb_country_by_a2("ZA") && cb_country_by_a2("DE") && !cb_country_by_a2("XX"),
          "test country codes exist in the cbank ISO 3166 table");
    const cb_ccy_t *ng = cb_ccy_by_alpha("NGN"), *us = cb_ccy_by_alpha("USD"),
                   *gb = cb_ccy_by_alpha("GBP");
    CHECK(ng && ng->num == NGN && us && us->num == USD && gb && gb->num == GBP,
          "test currency numerics match the cbank ISO 4217 table");
}

static void test_codec(void)
{
    capctl_profile_t p, q;
    base_profile(&p, "NG", 3, K_NG.id);
    p.per_tx_limit = 500000;
    p.n_period = 2;
    p.period[0] = (capctl_window_t){1, 1000000};
    p.period[1] = (capctl_window_t){30, 5000000};
    p.report_single = 300000;
    p.n_agg = 1;
    p.agg[0] = (capctl_window_t){30, 2000000};
    p.purpose_threshold = 100000;
    p.n_purpose = 2;
    memcpy(p.purpose[0], "SUPP", 4);
    memcpy(p.purpose[1], "EDUC", 4);
    p.n_corr = 2;
    p.corr[0] = (capctl_corridor_t){{'N', 'G'}, {'*', '*'}, 0, CAPCTL_CORR_ALLOW};
    p.corr[1] = (capctl_corridor_t){{'*', '*'}, {'U', 'S'}, USD, CAPCTL_CORR_BLOCK};
    size_t n = capctl_encode(&p, M, sizeof M);
    size_t want = CP_HDR_LEN + 2 + 1 + 8 + 1 + 2 * 10 + 8 + 1 + 10 + 8 + 1 + 8 + 1 + 2 * 7;
    CHECK(n == want, "encode length is exact (%zu)", n);
    CHECK(M[0] == 'Z' && M[1] == 'X' && M[2] == 'V' && M[3] == 'M' && M[4] == 1 && M[5] == 0 &&
              M[6] == CP_KIND_CAPCTL && M[8] == 'N' && M[9] == 'G',
          "header bytes: magic ZXVM, format 1, kind, country");
    CHECK(capctl_decode(M, n, &q) == CAPCTL_OK && q.per_tx_limit == 500000 &&
              q.period[1].window_days == 30 && q.period[1].amount == 5000000 &&
              q.agg[0].amount == 2000000 && !memcmp(q.purpose[1], "EDUC", 4) &&
              q.corr[1].ccy == USD && q.corr[1].action == CAPCTL_CORR_BLOCK && q.hdr.version == 3 &&
              !memcmp(q.hdr.key_id, K_NG.id, 32),
          "decode round-trips every field");
    uint8_t big[CAPCTL_MANIFEST_MAX + 1];
    memcpy(big, M, n);
    big[n] = 0;
    CHECK(capctl_decode(big, n + 1, &q) == CAPCTL_E_DECODE, "a trailing byte is rejected");
    bool all_short = true;
    for (size_t k = 0; k < n; k++)
        if (capctl_decode(M, k, &q) != CAPCTL_E_DECODE) all_short = false;
    CHECK(all_short, "every truncation (0..%zu bytes) is rejected", n - 1);
    CHECK(capctl_encode(&p, M, n - 1) == 0, "encode into a buffer one byte short fails");

    /* Each invalid field, set through the struct, must make encode refuse;
     * the same field patched into valid bytes must make decode refuse. */
    for (int k = 0; k < 12; k++) {
        capctl_profile_t x = p;
        const char *what = "";
        switch (k) {
        case 0:
            what = "wildcard header country";
            memcpy(x.hdr.country, "**", 2);
            break;
        case 1:
            what = "lowercase header country";
            memcpy(x.hdr.country, "ng", 2);
            break;
        case 2:
            what = "non-empty region";
            x.hdr.region[0] = 'L';
            break;
        case 3:
            what = "kind not CAPCTL";
            x.hdr.kind = CP_KIND_TAXTABLE;
            break;
        case 4:
            what = "unknown flag bit";
            x.flags |= 0x08;
            break;
        case 5:
            what = "no direction flag";
            x.flags = CAPCTL_F_DEFAULT_ALLOW;
            break;
        case 6:
            what = "window of 0 days";
            x.period[0].window_days = 0;
            break;
        case 7:
            what = "window of 367 days";
            x.period[1].window_days = 367;
            break;
        case 8:
            what = "lowercase purpose code";
            memcpy(x.purpose[0], "supp", 4);
            break;
        case 9:
            what = "corridor action 3";
            x.corr[0].action = 3;
            break;
        case 10:
            what = "currency 1000";
            x.ccy = 1000;
            break;
        case 11:
            what = "version 0";
            x.hdr.version = 0;
            break;
        }
        CHECK(capctl_encode(&x, M, sizeof M) == 0, "encode refuses: %s", what);
    }
    /* Byte patches on the valid encoding. */
    n = capctl_encode(&p, M, sizeof M);
    struct {
        size_t off;
        uint8_t val;
        const char *what;
    } patch[] = {
        {0, 'Y', "bad magic"},
        {4, 2, "format 2"},
        {8, '*', "wildcard country byte"},
        {CP_HDR_LEN + 2, 0x08, "unknown flag"},
        {CP_HDR_LEN + 11, 5, "period count 5 > 4"},
        {CP_HDR_LEN + 12, 0, "period window 0"},
        {16, 0, "version 0"},
        {31, 0, "not_after below not_before"},
    };
    for (size_t k = 0; k < sizeof patch / sizeof patch[0]; k++) {
        uint8_t cpy[CAPCTL_MANIFEST_MAX];
        memcpy(cpy, M, n);
        cpy[patch[k].off] = patch[k].val;
        if (patch[k].off == 16) cpy[17] = cpy[18] = cpy[19] = 0;
        CHECK(capctl_decode(cpy, n, &q) == CAPCTL_E_DECODE, "decode refuses: %s", patch[k].what);
    }
}

static capctl_tally_t TALLY[4];

static void test_install(void)
{
    capctl_t c;
    capctl_profile_t p;
    capctl_init(&c, cpk_verify, &RING, TALLY, 4);
    base_profile(&p, "NG", 5, K_NG.id);
    size_t n = capctl_encode(&p, M, sizeof M);
    cp_t_sign(&K_NG, M, n, SIG);

    CHECK(capctl_install(&c, M, n, NULL, 0, T0) == CAPCTL_E_SIG, "no signature: refused");
    uint8_t s2[CPK_SIG_LEN];
    memcpy(s2, SIG, sizeof s2);
    s2[100] ^= 1;
    CHECK(capctl_install(&c, M, n, s2, sizeof s2, T0) == CAPCTL_E_SIG, "flipped signature bit");
    CHECK(capctl_install(&c, M, n, SIG, sizeof SIG - 1, T0) == CAPCTL_E_SIG,
          "short signature length");
    uint8_t m2[CAPCTL_MANIFEST_MAX];
    memcpy(m2, M, n);
    m2[CP_HDR_LEN + 3] ^= 1; /* per_tx_limit low byte: still decodes */
    CHECK(capctl_decode(m2, n, &p) == CAPCTL_OK &&
              capctl_install(&c, m2, n, SIG, sizeof SIG, T0) == CAPCTL_E_SIG,
          "tampered body that still decodes: signature fails");
    capctl_t c0;
    capctl_init(&c0, NULL, NULL, NULL, 0);
    CHECK(capctl_install(&c0, M, n, SIG, sizeof SIG, T0) == CAPCTL_E_SIG,
          "no verify hook: every manifest refused");

    /* K2 scope: GB's key cannot sign NG; a tax-only key cannot sign capctl. */
    base_profile(&p, "NG", 5, K_GB.id);
    CHECK(install(&c, &p, &K_GB, T0) == CAPCTL_E_SIG, "key scoped to GB cannot sign NG");
    base_profile(&p, "NG", 5, K_TAX.id);
    CHECK(install(&c, &p, &K_TAX, T0) == CAPCTL_E_SIG, "key scoped to tax tables cannot sign");
    base_profile(&p, "NG", 5, K_NG.id);
    CHECK(install(&c, &p, &K_GB, T0) == CAPCTL_E_SIG, "header names NG's key, signed by GB's");

    base_profile(&p, "NG", 5, K_NG.id);
    CHECK(install(&c, &p, &K_NG, T0 - 1) == CAPCTL_E_WINDOW, "one second before not_before");
    CHECK(install(&c, &p, &K_NG, T0 + 365 * DAY + 1) == CAPCTL_E_WINDOW,
          "one second after not_after");
    CHECK(capctl_require(&c, "NG", 6) == CAPCTL_OK && install(&c, &p, &K_NG, T0) == CAPCTL_E_FLOOR,
          "version 5 below floor 6");
    CHECK(capctl_require(&c, "NG", 5) == CAPCTL_OK && install(&c, &p, &K_NG, T0) == CAPCTL_OK &&
              capctl_profile(&c, "NG")->hdr.version == 5,
          "installs at exactly not_before and the floor");
    CHECK(install(&c, &p, &K_NG, T0) == CAPCTL_E_VERSION, "same version again refused");
    base_profile(&p, "NG", 4, K_NG.id);
    capctl_require(&c, "NG", 1);
    CHECK(install(&c, &p, &K_NG, T0) == CAPCTL_E_VERSION &&
              capctl_profile(&c, "NG")->hdr.version == 5,
          "older version refused, installed one untouched");
    base_profile(&p, "NG", 6, K_ANY.id);
    p.per_tx_limit = 77;
    CHECK(install(&c, &p, &K_ANY, T0 + 365 * DAY) == CAPCTL_OK &&
              capctl_profile(&c, "NG")->per_tx_limit == 77,
          "newer version by a ** key at exactly not_after replaces it");
    CHECK(capctl_require(&c, "ng", 1) == CAPCTL_E_ARG, "require refuses a lowercase code");
    CHECK(capctl_profile(&c, "GB") == NULL, "no profile for GB");
    capctl_t full;
    capctl_init(&full, cpk_verify, &RING, NULL, 0);
    for (uint32_t i = 0; i < CAPCTL_MAX_REQ; i++) {
        char cc[2] = {(char) ('A' + i / 26), (char) ('A' + i % 26)};
        capctl_require(&full, cc, 1);
    }
    CHECK(capctl_require(&full, "ZZ", 1) == CAPCTL_E_FULL &&
              capctl_require(&full, "AA", 2) == CAPCTL_OK && full.req[0].min_version == 2,
          "floor table full; an existing entry can still be updated");
}

static void test_eval(void)
{
    capctl_t c;
    capctl_profile_t p;
    capctl_decision_t d;
    capctl_result_t r;
    capctl_init(&c, cpk_verify, &RING, TALLY, 2);
    capctl_transfer_t t = xfer("NG", "GB", NGN, 1000);

    CHECK(c.enabled == false && eval(&c, &t, T0, &d) == CAPCTL_R_DISABLED && d == CAPCTL_ALLOW,
          "off by default: ALLOW, reason disabled");
    capctl_require(&c, "NG", 1);
    CHECK(eval(&c, &t, T0, &d) == CAPCTL_R_DISABLED && d == CAPCTL_ALLOW,
          "disabled even with a required jurisdiction and no profile");
    capctl_enable(&c, true);
    CHECK(capctl_evaluate(&c, &t, T0, &r) == CAPCTL_DENY && r.reason == CAPCTL_R_PROFILE_MISSING &&
              !memcmp(r.country, "NG", 2),
          "enabled, NG required, no profile: DENY profile_missing from NG");
    capctl_transfer_t dom = xfer("NG", "NG", NGN, 1000);
    CHECK(eval(&c, &dom, T0, &d) == CAPCTL_R_DOMESTIC && d == CAPCTL_ALLOW,
          "domestic transfer: ALLOW");
    capctl_transfer_t gbus = xfer("GB", "US", GBP, 1000);
    CHECK(eval(&c, &gbus, T0, &d) == CAPCTL_R_NO_PROFILE && d == CAPCTL_ALLOW,
          "neither side required nor profiled: ALLOW no_profile");

    base_profile(&p, "NG", 1, K_NG.id);
    p.per_tx_limit = 500000;
    p.n_period = 2;
    p.period[0] = (capctl_window_t){1, 1000000};
    p.period[1] = (capctl_window_t){30, 1500000};
    p.report_single = 300000;
    p.n_agg = 1;
    p.agg[0] = (capctl_window_t){30, 1200000};
    p.purpose_threshold = 100000;
    p.n_purpose = 2;
    memcpy(p.purpose[0], "SUPP", 4);
    memcpy(p.purpose[1], "EDUC", 4);
    p.flags = CAPCTL_F_OUTBOUND; /* unlisted corridors denied */
    p.n_corr = 3;
    p.corr[0] = (capctl_corridor_t){{'N', 'G'}, {'*', '*'}, 0, CAPCTL_CORR_ALLOW};
    p.corr[1] = (capctl_corridor_t){{'*', '*'}, {'U', 'S'}, USD, CAPCTL_CORR_BLOCK};
    p.corr[2] = (capctl_corridor_t){{'N', 'G'}, {'Z', 'A'}, 0, CAPCTL_CORR_BLOCK};
    CHECK(install(&c, &p, &K_NG, T0) == CAPCTL_OK, "NG test profile installed");

    CHECK(eval(&c, &t, T0, &d) == CAPCTL_R_OK && d == CAPCTL_ALLOW, "small NG->GB: ALLOW");
    capctl_transfer_t za = xfer("NG", "ZA", NGN, 1000);
    CHECK(eval(&c, &za, T0, &d) == CAPCTL_R_CORRIDOR_BLOCKED && d == CAPCTL_DENY,
          "NG->ZA block rule beats the NG->** allow rule");
    capctl_transfer_t us = xfer("NG", "US", USD, 10);
    us.base_amount = 1000;
    CHECK(eval(&c, &us, T0, &d) == CAPCTL_R_CORRIDOR_BLOCKED, "**->US in USD blocked");
    us.ccy = GBP;
    CHECK(eval(&c, &us, T0, &d) == CAPCTL_R_OK && d == CAPCTL_ALLOW,
          "**->US in GBP not matched by the USD-only block");
    us.base_amount = 0;
    CHECK(eval(&c, &us, T0, &d) == CAPCTL_R_FX_REQUIRED && d == CAPCTL_DENY,
          "foreign currency with no base amount: fx_required");

    capctl_transfer_t tx = xfer("NG", "GB", NGN, 500000);
    memcpy(tx.purpose, "SUPP", 4);
    CHECK(eval(&c, &tx, T0, &d) == CAPCTL_R_REPORT && d == CAPCTL_ALLOW_WITH_REPORT,
          "amount == per-tx limit allowed (and reported: >= 300000)");
    tx.amount = 500001;
    CHECK(eval(&c, &tx, T0, &d) == CAPCTL_R_PER_TX_LIMIT && d == CAPCTL_DENY,
          "per-tx limit + 1: DENY");
    tx.amount = 99999;
    memset(tx.purpose, 0, 4);
    CHECK(eval(&c, &tx, T0, &d) == CAPCTL_R_OK, "below purpose threshold: no code needed");
    tx.amount = 100000;
    CHECK(eval(&c, &tx, T0, &d) == CAPCTL_R_PURPOSE_REQUIRED, "at threshold with no code: DENY");
    memcpy(tx.purpose, "GDDS", 4);
    CHECK(eval(&c, &tx, T0, &d) == CAPCTL_R_PURPOSE_NOT_ALLOWED, "code not on the list: DENY");
    memcpy(tx.purpose, "EDUC", 4);
    CHECK(eval(&c, &tx, T0, &d) == CAPCTL_R_OK && d == CAPCTL_ALLOW, "listed code: ALLOW");
    memcpy(tx.purpose, "ED*C", 4);
    CHECK(eval(&c, &tx, T0, &d) == CAPCTL_R_BAD_ARG && d == CAPCTL_DENY, "malformed code: bad_arg");

    /* C7 report record. */
    capctl_transfer_t rp = xfer("NG", "GB", NGN, 300000);
    memcpy(rp.purpose, "SUPP", 4);
    capctl_evaluate(&c, &rp, T0 + 5, &r);
    CHECK(r.decision == CAPCTL_ALLOW_WITH_REPORT && r.n_reports == 1 &&
              !memcmp(r.report[0].country, "NG", 2) && r.report[0].dir == CAPCTL_DIR_OUT &&
              r.report[0].flags == CAPCTL_REP_SINGLE && r.report[0].person == 1001 &&
              r.report[0].base_amount == 300000 && r.report[0].amount == 300000 &&
              r.report[0].profile_version == 1 && r.report[0].time == T0 + 5 &&
              !memcmp(r.report[0].purpose, "SUPP", 4) && !memcmp(r.report[0].to, "GB", 2),
          "report record at exactly report_single carries every field");
    rp.amount = 299999;
    CHECK(capctl_evaluate(&c, &rp, T0, &r) == CAPCTL_ALLOW && r.n_reports == 0,
          "one below report_single: plain ALLOW");

    /* C6 rolling windows. Day limit 1,000,000; 30-day limit 1,500,000;
     * aggregate report at 1,200,000 over 30 days. */
    capctl_transfer_t a = xfer("NG", "GB", NGN, 250000);
    memcpy(a.purpose, "SUPP", 4);
    for (int i = 0; i < 3; i++)
        CHECK(capctl_commit(&c, &a, T0 + 100) == CAPCTL_OK, "commit 250000");
    CHECK(capctl_used(&c, "NG", CAPCTL_DIR_OUT, 1001, 1, T0 + 100) == 750000, "day usage 750000");
    a.amount = 250000;
    CHECK(eval(&c, &a, T0 + DAY - 1, &d) == CAPCTL_R_OK, "to exactly the day limit: ALLOW");
    a.amount = 250001;
    CHECK(eval(&c, &a, T0 + DAY - 1, &d) == CAPCTL_R_PERIOD_LIMIT,
          "one over the day limit: DENY period_limit");
    CHECK(eval(&c, &a, T0 + DAY, &d) == CAPCTL_R_OK, "next UTC day: the day window is empty");
    a.amount = 250000;
    CHECK(capctl_commit(&c, &a, T0 + DAY) == CAPCTL_OK &&
              capctl_used(&c, "NG", CAPCTL_DIR_OUT, 1001, 30, T0 + DAY) == 1000000,
          "30-day usage spans both days");
    a.amount = 200000;
    capctl_evaluate(&c, &a, T0 + 2 * DAY, &r);
    CHECK(r.decision == CAPCTL_ALLOW_WITH_REPORT && r.report[0].flags == CAPCTL_REP_AGGREGATE,
          "used + amount reaches 1,200,000: aggregate report");
    a.amount = 199999;
    CHECK(capctl_evaluate(&c, &a, T0 + 2 * DAY, &r) == CAPCTL_ALLOW, "one below: no report");
    a.amount = 250000;
    CHECK(capctl_commit(&c, &a, T0 + 2 * DAY) == CAPCTL_OK,
          "commit 250000 on day 2 (30-day 1.25M)");
    CHECK(eval(&c, &a, T0 + 2 * DAY, &d) == CAPCTL_R_REPORT && d == CAPCTL_ALLOW_WITH_REPORT,
          "to exactly the 30-day limit: allowed (with the aggregate report)");
    a.amount = 250001;
    CHECK(eval(&c, &a, T0 + 2 * DAY, &d) == CAPCTL_R_PERIOD_LIMIT && d == CAPCTL_DENY,
          "one over the 30-day limit (day limit not reached): DENY period_limit");
    CHECK(capctl_used(&c, "NG", CAPCTL_DIR_OUT, 1001, 30, T0 + 29 * DAY) == 1250000 &&
              capctl_used(&c, "NG", CAPCTL_DIR_OUT, 1001, 30, T0 + 30 * DAY) == 500000 &&
              capctl_used(&c, "NG", CAPCTL_DIR_OUT, 1001, 30, T0 + 31 * DAY) == 250000 &&
              capctl_used(&c, "NG", CAPCTL_DIR_OUT, 1001, 30, T0 + 32 * DAY) == 0,
          "day D counts through D+29 and drops out on D+30");
    CHECK(capctl_used(&c, "NG", CAPCTL_DIR_OUT, 1001, 30, T0 - DAY) == 0,
          "buckets in the future of `now` do not count");
    capctl_transfer_t b = xfer("NG", "GB", NGN, 1000);
    b.sender = 1002;
    CHECK(capctl_commit(&c, &b, T0) == CAPCTL_OK, "second person gets the second tally slot");
    b.sender = 1003;
    capctl_evaluate(&c, &b, T0, &r);
    CHECK(r.decision == CAPCTL_DENY && r.reason == CAPCTL_R_TALLY_FULL &&
              capctl_commit(&c, &b, T0) == CAPCTL_E_FULL,
          "third person with a full tally store: DENY tally_full, commit E_FULL");
    CHECK(capctl_used(&c, "NG", CAPCTL_DIR_OUT, 1001, 366, T0 + 400 * DAY) == 0,
          "a year later the ring has nothing in range");

    /* C4 expiry and rollback at evaluation time. */
    CHECK(eval(&c, &t, T0 + 365 * DAY + 1, &d) == CAPCTL_R_PROFILE_EXPIRED && d == CAPCTL_DENY,
          "profile past not_after: DENY profile_expired");
    capctl_require(&c, "NG", 2);
    CHECK(eval(&c, &t, T0, &d) == CAPCTL_R_PROFILE_ROLLBACK && d == CAPCTL_DENY,
          "floor raised above the installed version: DENY profile_rollback");
    capctl_require(&c, "NG", 1);

    /* C3 inbound side. */
    capctl_profile_t g;
    base_profile(&g, "GB", 1, K_GB.id);
    g.ccy = GBP;
    g.flags = CAPCTL_F_INBOUND | CAPCTL_F_DEFAULT_ALLOW;
    g.per_tx_limit = 100;
    CHECK(install(&c, &g, &K_GB, T0) == CAPCTL_OK, "GB inbound-only test profile installed");
    capctl_transfer_t in = xfer("NG", "GB", NGN, 1000);
    in.base_amount = 101;
    CHECK(capctl_evaluate(&c, &in, T0, &r) == CAPCTL_DENY && r.reason == CAPCTL_R_PER_TX_LIMIT &&
              !memcmp(r.country, "GB", 2),
          "receiving side's inbound limit denies, country GB");
    in.base_amount = 100;
    CHECK(capctl_evaluate(&c, &in, T0, &r) == CAPCTL_ALLOW && r.reason == CAPCTL_R_OK,
          "inbound at exactly the limit: ALLOW");
    capctl_transfer_t out = xfer("GB", "DE", GBP, 1000000);
    CHECK(eval(&c, &out, T0, &d) == CAPCTL_R_OK && d == CAPCTL_ALLOW,
          "GB profile is inbound-only: outbound GB->DE not checked by it");
    capctl_profile_t g2 = g;
    g2.hdr.version = 2;
    g2.flags = CAPCTL_F_INBOUND | CAPCTL_F_OUTBOUND | CAPCTL_F_DEFAULT_ALLOW;
    g2.report_single = 50;
    CHECK(install(&c, &g2, &K_GB, T0) == CAPCTL_OK, "GB v2 adds outbound and reports");
    capctl_transfer_t both = xfer("NG", "GB", NGN, 300000);
    memcpy(both.purpose, "SUPP", 4);
    both.base_amount = 60;
    both.sender = 1002; /* 1001 has used its 30-day limit */
    capctl_evaluate(&c, &both, T0 + 3 * DAY, &r);
    CHECK(r.decision == CAPCTL_ALLOW_WITH_REPORT && r.n_reports == 2 &&
              !memcmp(r.report[0].country, "NG", 2) && !memcmp(r.report[1].country, "GB", 2) &&
              r.report[0].person == 1002 && r.report[1].dir == CAPCTL_DIR_IN &&
              r.report[1].person == 2002 && r.report[1].base_amount == 60 &&
              r.report[1].profile_ccy == GBP,
          "both sides report: two records, sender then receiver");

    /* C8 overflow. */
    capctl_t o;
    capctl_tally_t ot[1];
    capctl_init(&o, cpk_verify, &RING, ot, 1);
    capctl_enable(&o, true);
    base_profile(&p, "NG", 1, K_NG.id);
    p.n_period = 1;
    p.period[0] = (capctl_window_t){2, UINT64_MAX};
    CHECK(install(&o, &p, &K_NG, T0) == CAPCTL_OK, "profile with a 2^64-1 limit");
    capctl_transfer_t h = xfer("NG", "GB", NGN, UINT64_MAX - 5);
    CHECK(capctl_commit(&o, &h, T0) == CAPCTL_OK, "commit 2^64-6 on day D");
    h.amount = 10;
    CHECK(capctl_commit(&o, &h, T0) == CAPCTL_E_ARG, "same-day bucket overflow refused");
    CHECK(capctl_commit(&o, &h, T0 + DAY) == CAPCTL_OK, "commit 10 on day D+1");
    h.amount = 1;
    CHECK(eval(&o, &h, T0 + DAY, &d) == CAPCTL_R_OVERFLOW && d == CAPCTL_DENY,
          "window sum overflows: DENY overflow (never wraps)");
    CHECK(capctl_used(&o, "NG", CAPCTL_DIR_OUT, 1001, 2, T0 + DAY) == UINT64_MAX,
          "capctl_used saturates on overflow");

    /* Bad transfers. */
    capctl_transfer_t z = xfer("NG", "GB", NGN, 0);
    CHECK(eval(&c, &z, T0, &d) == CAPCTL_R_BAD_ARG && d == CAPCTL_DENY, "amount 0: bad_arg");
    z = xfer("ng", "GB", NGN, 1);
    CHECK(eval(&c, &z, T0, &d) == CAPCTL_R_BAD_ARG, "lowercase country: bad_arg");
    z = xfer("NG", "**", NGN, 1);
    CHECK(eval(&c, &z, T0, &d) == CAPCTL_R_BAD_ARG, "wildcard in a transfer: bad_arg");
    z = xfer("NG", "GB", 1000, 1);
    CHECK(eval(&c, &z, T0, &d) == CAPCTL_R_BAD_ARG, "currency 1000: bad_arg");
    CHECK(capctl_evaluate(&c, NULL, T0, &r) == CAPCTL_DENY && r.reason == CAPCTL_R_BAD_ARG,
          "NULL transfer: DENY");
    CHECK(capctl_commit(&c, &z, T0) == CAPCTL_E_ARG, "commit refuses a bad transfer");

    /* Corridor default deny. */
    capctl_profile_t s;
    base_profile(&s, "DE", 1, K_ANY.id);
    s.flags = CAPCTL_F_OUTBOUND;
    s.n_corr = 1;
    s.corr[0] = (capctl_corridor_t){{'D', 'E'}, {'G', 'B'}, 0, CAPCTL_CORR_ALLOW};
    CHECK(install(&c, &s, &K_ANY, T0) == CAPCTL_OK, "DE allow-list profile");
    capctl_transfer_t de = xfer("DE", "NG", GBP, 5);
    de.base_amount = 5;
    de.receiver = 4;
    CHECK(eval(&c, &de, T0, &d) == CAPCTL_R_CORRIDOR_NOT_ALLOWED && d == CAPCTL_DENY,
          "unlisted corridor with default deny: DENY corridor_not_allowed");

    CHECK(!strcmp(capctl_reason_name(CAPCTL_R_PERIOD_LIMIT), "period_limit") &&
              !strcmp(capctl_reason_name(CAPCTL_R_COUNT), "unknown") &&
              !strcmp(capctl_decision_name(CAPCTL_ALLOW_WITH_REPORT), "ALLOW_WITH_REPORT"),
          "reason and decision names");
}

int main(void)
{
    printf("=== test_capctl: capital-control profiles (TEST DATA, ML-DSA-87) ===\n");
    cp_t_keygen(&K_NG, 1);
    cp_t_keygen(&K_GB, 2);
    cp_t_keygen(&K_ANY, 3);
    cp_t_keygen(&K_TAX, 4);
    cpk_init(&RING);
    CHECK(cpk_add(&RING, K_NG.pk, 1u << CP_KIND_CAPCTL, "NG") &&
              cpk_add(&RING, K_GB.pk, 1u << CP_KIND_CAPCTL, "GB") &&
              cpk_add(&RING, K_ANY.pk, 1u << CP_KIND_CAPCTL, "**") &&
              cpk_add(&RING, K_TAX.pk, 1u << CP_KIND_TAXTABLE, "NG"),
          "keyring: four scoped ML-DSA-87 keys");
    CHECK(!cpk_add(&RING, K_NG.pk, 1u << CP_KIND_CAPCTL, "NG"), "keyring refuses a duplicate key");
    CHECK(!cpk_add(&RING, K_NG.pk, 0, "FR"), "keyring refuses a key with no kinds");
    test_int();
    test_codec();
    test_install();
    test_eval();
    return cp_test_done("test_capctl");
}
