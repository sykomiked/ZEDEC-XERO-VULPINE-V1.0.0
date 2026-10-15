/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_pce_fin.c — the financial analysis engine (pce_fin.h) and the block
 * entry table (pce_entry.h).
 *
 * Two statements that agree, two that differ across standards (converging)
 * and under one standard (diverging), a statement that breaks A = L + E
 * (quarantined, logged as forbidden), ratio ranges by their corners with a
 * zero-touching denominator, NPV whose sign depends on whose rate, exact
 * value at risk from one and two models, and every entry run through its
 * contract. */
#include <stdio.h>
#include <string.h>
#include "pce_fin.h"
#include "pce_entry.h"

static int *P, *F;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            (*P)++;                                                                                \
        else {                                                                                     \
            (*F)++;                                                                                \
            printf("[FAIL] %s (%s:%d)\n", msg, __FILE__, __LINE__);                                \
        }                                                                                          \
    } while (0)

static rat_t R(int64_t n, int64_t d)
{
    return rat_make(n, d);
}
static bool req(rat_t a, int64_t n, int64_t d)
{
    return a.valid && rat_eq(a, rat_make(n, d));
}

#define ALL ((1u << PCE_FIN_FIELDS) - 1u)

static pce_stmt_t stmt(int64_t a, int64_t l, int64_t e, uint32_t std)
{
    pce_stmt_t s;
    s.v[PCE_FIN_ASSETS] = a;
    s.v[PCE_FIN_LIABILITIES] = l;
    s.v[PCE_FIN_EQUITY] = e;
    s.v[PCE_FIN_CUR_ASSETS] = 300;
    s.v[PCE_FIN_CUR_LIAB] = 200;
    s.v[PCE_FIN_REVENUE] = 1000;
    s.v[PCE_FIN_NET_INCOME] = 80;
    s.v[PCE_FIN_EBIT] = 150;
    s.v[PCE_FIN_INTEREST] = 30;
    s.present = ALL;
    s.standard = std;
    return s;
}

static void test_analyse(void)
{
    pce_stmt_t s[3] = {stmt(1000, 600, 400, PCE_STD_GAAP), stmt(1000, 600, 400, PCE_STD_IFRS),
                       stmt(1000, 600, 400, PCE_STD_GAAP)};
    pce_fin_report_t r, k;
    CHECK(pce_fin_analyse(s, 2, LPX_PH_MANIFEST, NULL, &r) == PCE_OK, "analyse");
    CHECK(lpx_base(r.v.tag) == LPX_B_TRUE && r.used == 0x3u && r.quarantined == 0u,
          "agreeing statements: TRUE");
    CHECK(r.v.act == LPX_ACT_ESCALATE, "advisory: a person reviews, never automatic");
    CHECK(r.field[PCE_FIN_ASSETS].base == LPX_B_TRUE && r.field[PCE_FIN_ASSETS].n == 2u,
          "field agrees");
    CHECK(r.ratio[PCE_RATIO_CURRENT].base == LPX_B_TRUE && req(r.ratio[PCE_RATIO_CURRENT].lo, 3, 2),
          "current ratio 3/2");
    CHECK(req(r.ratio[PCE_RATIO_DEBT_EQUITY].lo, 3, 2), "debt to equity 3/2");
    CHECK(req(r.ratio[PCE_RATIO_NET_MARGIN].lo, 2, 25), "net margin 8%");
    CHECK(req(r.ratio[PCE_RATIO_INTEREST_COVER].hi, 5, 1), "interest cover 5");

    /* across standards: allowed, converging */
    s[1].v[PCE_FIN_NET_INCOME] = 70;
    s[1].v[PCE_FIN_REVENUE] = 980;
    CHECK(pce_fin_analyse(s, 2, LPX_PH_MANIFEST, NULL, &r) == PCE_OK, "cross-standard");
    CHECK(lpx_base(r.v.tag) == LPX_B_BOTH && lpx_pol(r.v.tag) == LPX_P_NEGENTROPIC &&
              r.v.kind == PDX_KIND_CONSTRUCTIVE && r.v.act == LPX_ACT_WAIT,
          "GAAP vs IFRS: BOTH, settled by a rule (wait)");
    const pce_field_t *ni = &r.field[PCE_FIN_NET_INCOME];
    CHECK(ni->base == LPX_B_BOTH && ni->lo == 70 && ni->hi == 80 && !ni->same_std,
          "both figures kept as a range");
    const pce_ratio_t *nm = &r.ratio[PCE_RATIO_NET_MARGIN];
    CHECK(nm->base == LPX_B_BOTH && req(nm->lo, 7, 100) && req(nm->hi, 80, 980),
          "margin range by its corners");

    /* the same standard: diverging, to a person */
    CHECK(pce_fin_analyse(&s[1], 2, LPX_PH_MANIFEST, NULL, &r) == PCE_OK, "one standard pair");
    s[2].standard = PCE_STD_IFRS;
    CHECK(pce_fin_analyse(&s[1], 2, LPX_PH_MANIFEST, NULL, &r) == PCE_OK &&
              r.field[PCE_FIN_NET_INCOME].same_std && lpx_pol(r.v.tag) == LPX_P_ENTROPIC &&
              r.v.kind == PDX_KIND_DESTRUCTIVE && r.v.act == LPX_ACT_ESCALATE,
          "IFRS vs IFRS differ: diverging, escalate");
    s[2].standard = PCE_STD_GAAP;
    CHECK(pce_fin_analyse(s, 3, LPX_PH_MANIFEST, NULL, &r) == PCE_OK &&
              r.field[PCE_FIN_NET_INCOME].same_std == false && r.field[PCE_FIN_NET_INCOME].n == 3u,
          "GAAP twice equal, IFRS different: still allowed");
    s[2].v[PCE_FIN_NET_INCOME] = 90;
    CHECK(pce_fin_analyse(s, 3, LPX_PH_MANIFEST, NULL, &r) == PCE_OK &&
              r.field[PCE_FIN_NET_INCOME].same_std && r.field[PCE_FIN_NET_INCOME].hi == 90,
          "two GAAP statements differ: flagged");
    s[2].v[PCE_FIN_NET_INCOME] = 80;

    /* A != L + E: quarantined, logged as forbidden, out of every figure */
    pce_stmt_t q[2] = {stmt(1000, 600, 400, PCE_STD_GAAP), stmt(1000, 600, 300, PCE_STD_GAAP)};
    q[1].v[PCE_FIN_REVENUE] = 5;
    pdx_rec_t st[4];
    pdx_log_t lg;
    pdx_log_init(&lg, st, 4);
    CHECK(pce_fin_analyse(q, 2, LPX_PH_MANIFEST, &lg, &r) == PCE_OK, "with a broken statement");
    CHECK(r.quarantined == 0x2u && r.used == 0x1u, "quarantined");
    CHECK(r.field[PCE_FIN_REVENUE].n == 1u && r.field[PCE_FIN_REVENUE].lo == 1000,
          "its figures are left out");
    CHECK(lpx_base(r.v.tag) == LPX_B_TRUE, "the rest still agrees");
    CHECK(lg.n == 2u && lpx_pol(st[0].tag) == LPX_P_FORBIDDEN && st[0].outcome == LPX_ACT_ROLLBACK,
          "forbidden record first");
    CHECK(st[0].aux == 1u && st[1].aux == 0x1u && st[1].op == PCE_OP_FIN, "then the report");
    CHECK(memcmp(st[1].b, r.out, 32) == 0 && pdx_log_verify(&lg), "digest, chain");
    pdx_rec_t st2[2];
    pdx_log_t lg2;
    pdx_log_init(&lg2, st2, 2);
    pce_fin_report_t r2;
    pce_fin_analyse(q, 2, LPX_PH_MANIFEST, &lg2, &r2);
    CHECK(memcmp(r.in, r2.in, 32) == 0 && memcmp(r.out, r2.out, 32) == 0 &&
              memcmp(st[0].b, st2[0].b, 32) == 0,
          "replay");
    pdx_log_t lg3;
    pdx_rec_t st3[1];
    pdx_log_init(&lg3, st3, 1);
    k = r2;
    CHECK(pce_fin_analyse(q, 2, LPX_PH_MANIFEST, &lg3, &r2) == PCE_E_LOG && lg3.n == 0u,
          "no room for both records: nothing written");
    CHECK(memcmp(&k, &r2, sizeof k) == 0, "and out untouched");
    pce_stmt_t onlybad[1] = {stmt(1, 1, 1, PCE_STD_OTHER)};
    CHECK(pce_fin_analyse(onlybad, 1, 0, NULL, &r) == PCE_OK && r.used == 0u &&
              lpx_base(r.v.tag) == LPX_B_NEITHER && r.ratio[0].base == LPX_B_NEITHER,
          "only a broken statement: NEITHER");
    pce_stmt_t partial = stmt(1, 1, 1, PCE_STD_OTHER);
    partial.present = ALL & ~(1u << PCE_FIN_EQUITY);
    CHECK(pce_fin_analyse(&partial, 1, 0, NULL, &r) == PCE_OK && r.used == 1u &&
              r.field[PCE_FIN_EQUITY].base == LPX_B_NEITHER &&
              r.ratio[PCE_RATIO_DEBT_EQUITY].base == LPX_B_NEITHER,
          "identity unchecked without equity; missing field is NEITHER");

    /* a denominator range touching zero is NEITHER */
    pce_stmt_t z[2] = {stmt(1000, 600, 400, PCE_STD_GAAP), stmt(1000, 600, 400, PCE_STD_IFRS)};
    z[0].v[PCE_FIN_INTEREST] = 0;
    CHECK(pce_fin_analyse(z, 1, 0, NULL, &r) == PCE_OK &&
              r.ratio[PCE_RATIO_INTEREST_COVER].base == LPX_B_NEITHER,
          "zero interest: no cover ratio");
    z[0].v[PCE_FIN_INTEREST] = -10;
    CHECK(pce_fin_analyse(z, 2, 0, NULL, &r) == PCE_OK &&
              r.ratio[PCE_RATIO_INTEREST_COVER].base == LPX_B_NEITHER,
          "range across zero: NEITHER, never infinity");
    z[0].v[PCE_FIN_INTEREST] = 10;
    CHECK(pce_fin_analyse(z, 2, 0, NULL, &r) == PCE_OK &&
              req(r.ratio[PCE_RATIO_INTEREST_COVER].lo, 5, 1) &&
              req(r.ratio[PCE_RATIO_INTEREST_COVER].hi, 15, 1),
          "cover between 150/30 and 150/10");
    z[0].v[PCE_FIN_EQUITY] = -400;
    z[0].v[PCE_FIN_LIABILITIES] = 1400;
    z[1].v[PCE_FIN_EQUITY] = -200;
    z[1].v[PCE_FIN_LIABILITIES] = 1200;
    CHECK(pce_fin_analyse(z, 2, 0, NULL, &r) == PCE_OK &&
              req(r.ratio[PCE_RATIO_DEBT_EQUITY].lo, -7, 1) &&
              req(r.ratio[PCE_RATIO_DEBT_EQUITY].hi, -3, 1),
          "negative equity: every combination of the ranges, corners included");

    /* refusals */
    k = r;
    CHECK(pce_fin_analyse(NULL, 1, 0, NULL, &r) == PCE_E_ARG, "NULL");
    CHECK(pce_fin_analyse(s, 0, 0, NULL, &r) == PCE_E_ARG, "no source");
    pce_stmt_t many[PCE_FIN_SRC + 1];
    for (uint32_t i = 0; i <= PCE_FIN_SRC; i++) many[i] = stmt(10, 5, 5, PCE_STD_OTHER);
    CHECK(pce_fin_analyse(many, PCE_FIN_SRC + 1u, 0, NULL, &r) == PCE_E_ARG, "too many");
    CHECK(pce_fin_analyse(s, 1, 4, NULL, &r) == PCE_E_ARG, "phase");
    CHECK(pce_fin_analyse(s, 1, 0, NULL, NULL) == PCE_E_ARG, "NULL out");
    pce_stmt_t bad = stmt(10, 5, 5, 3);
    CHECK(pce_fin_analyse(&bad, 1, 0, NULL, &r) == PCE_E_ARG, "unknown standard");
    bad = stmt(10, 5, 5, PCE_STD_OTHER);
    bad.present |= 1u << PCE_FIN_FIELDS;
    CHECK(pce_fin_analyse(&bad, 1, 0, NULL, &r) == PCE_E_ARG, "unknown field bit");
    bad = stmt(PCE_FIN_MONEY_MAX + 1, 5, 5, PCE_STD_OTHER);
    CHECK(pce_fin_analyse(&bad, 1, 0, NULL, &r) == PCE_E_ARG, "money above the limit");
    bad = stmt(10, -PCE_FIN_MONEY_MAX - 1, 5, PCE_STD_OTHER);
    CHECK(pce_fin_analyse(&bad, 1, 0, NULL, &r) == PCE_E_ARG, "money below the limit");
    bad.present &= ~(1u << PCE_FIN_LIABILITIES);
    CHECK(pce_fin_analyse(&bad, 1, 0, NULL, &r) == PCE_OK, "an unreported field is not checked");
    CHECK(memcmp(&k, &r, sizeof k) != 0, "the last call did write");
    bad = stmt(PCE_FIN_MONEY_MAX, PCE_FIN_MONEY_MAX, 0, PCE_STD_OTHER);
    CHECK(pce_fin_analyse(&bad, 1, 0, NULL, &r) == PCE_OK && r.quarantined == 0u,
          "identity at the money limit");
    bad = stmt(-PCE_FIN_MONEY_MAX, -PCE_FIN_MONEY_MAX, -PCE_FIN_MONEY_MAX, PCE_STD_OTHER);
    CHECK(pce_fin_analyse(&bad, 1, 0, NULL, &r) == PCE_OK && r.quarantined == 1u,
          "broken identity at the limit, no overflow");
    CHECK(pce_fin_analyse(many, PCE_FIN_SRC, 0, NULL, &r) == PCE_OK && r.used == 0xFFu,
          "sources at capacity");
}

static void test_npv(void)
{
    /* invest 1000, receive 600 twice */
    int64_t cf[3] = {-1000, 600, 600};
    rat_t rates[3] = {R(5, 100), R(10, 100), R(15, 100)};
    pce_npv_t r, k;
    CHECK(pce_fin_npv(cf, 3, rates, 1, 0, NULL, &r) == PCE_OK, "npv");
    /* -1000 + 600 (20/21 + 400/441) = -1000 + 492000/441 = 17000/147 */
    CHECK(req(r.npv[0], 17000, 147), "exact NPV at 5%");
    CHECK(lpx_base(r.v.tag) == LPX_B_TRUE && r.v.act == LPX_ACT_ESCALATE,
          "positive: TRUE, still a person's call");
    CHECK(pce_fin_npv(cf, 3, rates, 3, 0, NULL, &r) == PCE_OK, "three analysts");
    CHECK(r.npv[1].num > 0 && r.npv[2].num < 0, "positive at 10%, negative at 15%");
    CHECK(lpx_base(r.v.tag) == LPX_B_BOTH && r.v.act == LPX_ACT_ESCALATE,
          "sign depends on whose rate: BOTH");
    CHECK(rat_eq(r.lo, r.npv[2]) && rat_eq(r.hi, r.npv[0]), "range over the rates");
    CHECK(pce_fin_npv(cf, 3, &rates[2], 1, 0, NULL, &r) == PCE_OK &&
              lpx_base(r.v.tag) == LPX_B_FALSE && r.v.act == LPX_ACT_REFUSE,
          "negative at every rate: FALSE");
    int64_t even[2] = {-100, 110};
    rat_t ten[1] = {R(1, 10)};
    CHECK(pce_fin_npv(even, 2, ten, 1, 0, NULL, &r) == PCE_OK && req(r.npv[0], 0, 1) &&
              lpx_base(r.v.tag) == LPX_B_NEITHER,
          "break-even: NEITHER");
    rat_t two[2] = {R(1, 10), R(0, 1)};
    CHECK(pce_fin_npv(even, 2, two, 2, 0, NULL, &r) == PCE_OK && lpx_base(r.v.tag) == LPX_B_BOTH,
          "zero at one rate, positive at another: BOTH");
    int64_t single[1] = {-5};
    CHECK(pce_fin_npv(single, 1, ten, 1, 0, NULL, &r) == PCE_OK && req(r.npv[0], -5, 1),
          "one flow, today");
    rat_t neg[1] = {R(-1, 2)};
    CHECK(pce_fin_npv(even, 2, neg, 1, 0, NULL, &r) == PCE_OK && req(r.npv[0], 120, 1),
          "a negative rate above -1 is allowed");
    /* too large to keep exactly: NEITHER, never rounded */
    int64_t lng[PCE_FIN_CF];
    for (uint32_t t = 0; t < PCE_FIN_CF; t++) lng[t] = 1000003;
    rat_t odd[2] = {R(1, 97), R(1, 89)};
    CHECK(pce_fin_npv(lng, PCE_FIN_CF, odd, 2, 0, NULL, &r) == PCE_OK &&
              lpx_base(r.v.tag) == LPX_B_NEITHER && !r.npv[0].valid,
          "overflow: NEITHER, the slot is invalid");
    k = r;
    CHECK(pce_fin_npv(cf, 3, rates, 0, 0, NULL, &r) == PCE_E_ARG, "no rate");
    CHECK(pce_fin_npv(cf, 0, rates, 1, 0, NULL, &r) == PCE_E_ARG, "no flows");
    CHECK(pce_fin_npv(cf, PCE_FIN_CF + 1u, rates, 1, 0, NULL, &r) == PCE_E_ARG, "too many flows");
    rat_t mrates[PCE_FIN_RATES + 1];
    for (uint32_t i = 0; i <= PCE_FIN_RATES; i++) mrates[i] = R(1, 10);
    CHECK(pce_fin_npv(cf, 3, mrates, PCE_FIN_RATES + 1u, 0, NULL, &r) == PCE_E_ARG,
          "too many rates");
    rat_t m1[1] = {R(-1, 1)};
    CHECK(pce_fin_npv(cf, 3, m1, 1, 0, NULL, &r) == PCE_E_ARG, "rate of -100%");
    rat_t iv[1] = {R(1, 0)};
    CHECK(pce_fin_npv(cf, 3, iv, 1, 0, NULL, &r) == PCE_E_ARG, "invalid rate");
    int64_t big[1] = {PCE_FIN_MONEY_MAX + 1};
    CHECK(pce_fin_npv(big, 1, ten, 1, 0, NULL, &r) == PCE_E_ARG, "flow above the limit");
    CHECK(pce_fin_npv(NULL, 1, ten, 1, 0, NULL, &r) == PCE_E_ARG, "NULL flows");
    CHECK(pce_fin_npv(cf, 3, NULL, 1, 0, NULL, &r) == PCE_E_ARG, "NULL rates");
    CHECK(pce_fin_npv(cf, 3, rates, 1, 4, NULL, &r) == PCE_E_ARG, "phase");
    CHECK(pce_fin_npv(cf, 3, rates, 1, 0, NULL, NULL) == PCE_E_ARG, "NULL out");
    CHECK(memcmp(&k, &r, sizeof k) == 0, "refusals leave out");
    CHECK(pce_fin_npv(cf, 3, mrates, PCE_FIN_RATES, 0, NULL, &r) == PCE_OK &&
              r.nrates == PCE_FIN_RATES,
          "rates at capacity");
    pdx_rec_t st[1];
    pdx_log_t lg;
    pdx_log_init(&lg, st, 1);
    pce_npv_t r2;
    CHECK(pce_fin_npv(cf, 3, rates, 3, 0, &lg, &r) == PCE_OK && st[0].aux == 3u, "logged");
    CHECK(pce_fin_npv(cf, 3, rates, 3, 0, NULL, &r2) == PCE_OK && memcmp(r.out, r2.out, 32) == 0,
          "replay");
    CHECK(pce_fin_npv(cf, 3, rates, 2, 0, NULL, &r2) == PCE_OK && memcmp(r.in, r2.in, 32) != 0,
          "rates in the digest");
    CHECK(pce_fin_npv(cf, 3, rates, 3, 0, &lg, &r2) == PCE_E_LOG, "full log");
}

static void test_risk(void)
{
    int64_t loss[4] = {0, 100, 500, 2000};
    rat_t p[4] = {R(80, 100), R(10, 100), R(7, 100), R(3, 100)};
    rat_t q[4] = {R(80, 100), R(10, 100), R(5, 100), R(5, 100)};
    pce_risk_t r, k;
    CHECK(pce_fin_risk(loss, p, NULL, 4, R(95, 100), 0, NULL, &r) == PCE_OK, "risk");
    CHECK(req(r.expected[0], 105, 1) && r.var[0] == 500, "expected loss 105, VaR95 500");
    CHECK(lpx_base(r.v.tag) == LPX_B_NEITHER && r.nmodels == 1u, "one model: unwitnessed");
    CHECK(pce_fin_risk(loss, p, q, 4, R(95, 100), 0, NULL, &r) == PCE_OK, "two models");
    CHECK(req(r.expected[1], 135, 1) && r.var[1] == 500, "second model");
    CHECK(lpx_base(r.v.tag) == LPX_B_BOTH, "expected losses differ: BOTH");
    CHECK(pce_fin_risk(loss, p, p, 4, R(95, 100), 0, NULL, &r) == PCE_OK &&
              lpx_base(r.v.tag) == LPX_B_TRUE && r.v.act == LPX_ACT_ESCALATE,
          "same model twice: TRUE, advisory");
    CHECK(pce_fin_risk(loss, p, NULL, 4, R(97, 100), 0, NULL, &r) == PCE_OK && r.var[0] == 500,
          "level exactly at a step");
    CHECK(pce_fin_risk(loss, p, NULL, 4, R(971, 1000), 0, NULL, &r) == PCE_OK && r.var[0] == 2000,
          "just past a step");
    CHECK(pce_fin_risk(loss, p, NULL, 4, R(1, 1), 0, NULL, &r) == PCE_OK && r.var[0] == 2000,
          "level 1: the worst loss");
    CHECK(pce_fin_risk(loss, p, NULL, 4, R(1, 1000), 0, NULL, &r) == PCE_OK && r.var[0] == 0,
          "a tiny level: the smallest loss");
    int64_t un[3] = {50, -20, 10};
    rat_t pu[3] = {R(1, 3), R(1, 3), R(1, 3)};
    CHECK(pce_fin_risk(un, pu, NULL, 3, R(2, 3), 0, NULL, &r) == PCE_OK && r.var[0] == 10,
          "losses sorted first (a gain is a negative loss)");
    int64_t tie[3] = {5, 5, 1};
    CHECK(pce_fin_risk(tie, pu, NULL, 3, R(1, 2), 0, NULL, &r) == PCE_OK && r.var[0] == 5,
          "tied losses");
    rat_t qx[4] = {R(70, 100), R(10, 100), R(17, 100), R(3, 100)};
    CHECK(pce_fin_risk(loss, p, qx, 4, R(95, 100), 0, NULL, &r) == PCE_OK && r.var[1] == 500 &&
              lpx_base(r.v.tag) == LPX_B_BOTH,
          "same VaR, different expectation: BOTH");
    k = r;
    CHECK(pce_fin_risk(loss, p, NULL, 4, R(0, 1), 0, NULL, &r) == PCE_E_ARG, "level 0");
    CHECK(pce_fin_risk(loss, p, NULL, 4, R(101, 100), 0, NULL, &r) == PCE_E_ARG, "level above 1");
    CHECK(pce_fin_risk(loss, p, NULL, 4, R(1, 0), 0, NULL, &r) == PCE_E_ARG, "invalid level");
    rat_t bad[4] = {R(1, 2), R(1, 2), R(1, 2), R(0, 1)};
    CHECK(pce_fin_risk(loss, bad, NULL, 4, R(1, 2), 0, NULL, &r) == PCE_E_ARG, "p not a dist");
    CHECK(pce_fin_risk(loss, p, bad, 4, R(1, 2), 0, NULL, &r) == PCE_E_ARG, "q not a dist");
    int64_t bl[1] = {PCE_FIN_MONEY_MAX + 1};
    rat_t one[1] = {R(1, 1)};
    CHECK(pce_fin_risk(bl, one, NULL, 1, R(1, 2), 0, NULL, &r) == PCE_E_ARG, "loss too big");
    CHECK(pce_fin_risk(NULL, p, NULL, 4, R(1, 2), 0, NULL, &r) == PCE_E_ARG, "NULL loss");
    CHECK(pce_fin_risk(loss, NULL, NULL, 4, R(1, 2), 0, NULL, &r) == PCE_E_ARG, "NULL p");
    CHECK(pce_fin_risk(loss, p, NULL, 0, R(1, 2), 0, NULL, &r) == PCE_E_ARG, "n 0");
    CHECK(pce_fin_risk(loss, p, NULL, PCE_FIN_OUTCOMES + 1u, R(1, 2), 0, NULL, &r) == PCE_E_ARG,
          "n too big");
    CHECK(pce_fin_risk(loss, p, NULL, 4, R(1, 2), 4, NULL, &r) == PCE_E_ARG, "phase");
    CHECK(pce_fin_risk(loss, p, NULL, 4, R(1, 2), 0, NULL, NULL) == PCE_E_ARG, "NULL out");
    CHECK(memcmp(&k, &r, sizeof k) == 0, "refusals leave out");
    pdx_rec_t st[1];
    pdx_log_t lg;
    pdx_log_init(&lg, st, 1);
    pce_risk_t r2;
    CHECK(pce_fin_risk(loss, p, q, 4, R(95, 100), 0, &lg, &r) == PCE_OK && st[0].aux == 2u,
          "logged");
    CHECK(pce_fin_risk(loss, p, q, 4, R(95, 100), 0, NULL, &r2) == PCE_OK &&
              memcmp(r.in, r2.in, 32) == 0 && memcmp(r.out, r2.out, 32) == 0,
          "replay");
    CHECK(pce_fin_risk(loss, p, NULL, 4, R(95, 100), 0, NULL, &r2) == PCE_OK &&
              memcmp(r.in, r2.in, 32) != 0,
          "the second model is in the digest");
    CHECK(pce_fin_risk(loss, p, q, 4, R(95, 100), 0, &lg, &r2) == PCE_E_LOG, "full log");
}

static void test_entries(void)
{
    uint32_t n = 0;
    const pce_contract_t *c = pce_entries(&n);
    CHECK(n == 8u && c != NULL, "eight entries");
    CHECK(pce_entries(NULL) == c, "entries without a count");
    for (uint32_t i = 0; i < n; i++)
        CHECK(pce_entry_find(c[i].entry) == &c[i] && c[i].ninputs <= PCE_ENTRY_INPUTS,
              "each entry is found by its name");
    CHECK(pce_entry_find("prob") == NULL && pce_entry_find("prob.join.x") == NULL &&
              pce_entry_find(NULL) == NULL,
          "no partial names");

    pce_num_t out[8];
    uint32_t no = 0;
    int64_t a[4] = {99, 1, 0, 0}, b[4] = {1, 99, 0, 0};
    pce_arg_t ja[2] = {{"a", a, 4}, {"b", b, 4}};
    CHECK(pce_entry_call("prob.join", ja, 2, out, 8, &no) == PCE_OK && no == 4u, "prob.join");
    CHECK(out[2].num == 4901 && out[2].den == 5000 && out[0].num == 99 && out[0].den == 10000,
          "joined: 98.02% on BOTH");
    pce_arg_t wrong[2] = {{"b", b, 4}, {"a", a, 4}};
    CHECK(pce_entry_call("prob.join", wrong, 2, out, 8, &no) == PCE_E_ARG, "inputs out of order");
    CHECK(pce_entry_call("prob.join", ja, 1, out, 8, &no) == PCE_E_ARG, "too few inputs");
    CHECK(pce_entry_call("prob.join", NULL, 2, out, 8, &no) == PCE_E_ARG, "NULL args");
    pce_arg_t short_a[2] = {{"a", a, 3}, {"b", b, 4}};
    CHECK(pce_entry_call("prob.join", short_a, 2, out, 8, &no) == PCE_E_ARG, "a mass is four");
    int64_t na[4] = {-1, 1, 0, 0};
    pce_arg_t neg[2] = {{"a", na, 4}, {"b", b, 4}};
    CHECK(pce_entry_call("prob.join", neg, 2, out, 8, &no) == PCE_E_ARG, "negative count");
    pce_arg_t emp[2] = {{"a", a, 0}, {"b", b, 4}};
    CHECK(pce_entry_call("prob.join", emp, 2, out, 8, &no) == PCE_E_ARG, "empty vector");
    pce_arg_t nul[2] = {{"a", NULL, 4}, {"b", b, 4}};
    CHECK(pce_entry_call("prob.join", nul, 2, out, 8, &no) == PCE_E_ARG, "NULL vector");
    pce_arg_t noname[2] = {{NULL, a, 4}, {"b", b, 4}};
    CHECK(pce_entry_call("prob.join", noname, 2, out, 8, &no) == PCE_E_ARG, "unnamed input");
    CHECK(pce_entry_call("prob.join", ja, 2, out, 3, &no) == PCE_E_BUFFER && no == 4u,
          "too small: says how many");
    CHECK(pce_entry_call("prob.join", ja, 2, NULL, 0, &no) == PCE_E_BUFFER, "no buffer");
    CHECK(pce_entry_call("prob.join", ja, 2, out, 8, NULL) == PCE_E_ARG, "NULL count");
    CHECK(pce_entry_call("nope", ja, 2, out, 8, &no) == PCE_E_NAME, "unknown entry");

    int64_t m[4] = {1, 1, 8, 0}, th[4] = {9, 10, 1, 10};
    pce_arg_t va[2] = {{"m", m, 4}, {"th", th, 4}};
    CHECK(pce_entry_call("prob.verdict", va, 2, out, 8, &no) == PCE_OK && no == 1u &&
              out[0].den == 0 && out[0].num == LPRES_STATE_BOTH,
          "verdict BOTH as a truth");
    int64_t mt[4] = {19, 1, 0, 0};
    va[0].v = mt;
    CHECK(pce_entry_call("prob.verdict", va, 2, out, 8, &no) == PCE_OK &&
              out[0].num == LPRES_STATE_TRUE,
          "verdict TRUE");
    int64_t mf[4] = {0, 1, 0, 0};
    va[0].v = mf;
    CHECK(pce_entry_call("prob.verdict", va, 2, out, 8, &no) == PCE_OK &&
              out[0].num == LPRES_STATE_FALSE,
          "verdict FALSE");
    int64_t mn[4] = {0, 0, 0, 1};
    va[0].v = mn;
    CHECK(pce_entry_call("prob.verdict", va, 2, out, 8, &no) == PCE_OK &&
              out[0].num == LPRES_STATE_NEITHER,
          "verdict NEITHER");
    int64_t bth[4] = {9, 0, 1, 10};
    va[1].v = bth;
    CHECK(pce_entry_call("prob.verdict", va, 2, out, 8, &no) == PCE_E_ARG, "bad threshold");
    va[1].n = 3;
    CHECK(pce_entry_call("prob.verdict", va, 2, out, 8, &no) == PCE_E_ARG, "short threshold");

    int64_t pr[2] = {1, 999}, lk[2] = {99, 5}, den[1] = {100};
    pce_arg_t ba[3] = {{"prior", pr, 2}, {"like", lk, 2}, {"den", den, 1}};
    CHECK(pce_entry_call("prob.bayes", ba, 3, out, 8, &no) == PCE_OK && no == 2u &&
              out[0].num == 11 && out[0].den == 566,
          "bayes as a block: 99/5094 in lowest terms");
    int64_t z2[2] = {0, 0};
    ba[1].v = z2;
    CHECK(pce_entry_call("prob.bayes", ba, 3, out, 8, &no) == PCE_E_CONFLICT,
          "impossible evidence is a conflict");
    ba[1].v = lk;
    ba[0].v = z2;
    CHECK(pce_entry_call("prob.bayes", ba, 3, out, 8, &no) == PCE_E_ARG, "zero prior");
    int64_t np[2] = {-1, 2};
    ba[0].v = np;
    CHECK(pce_entry_call("prob.bayes", ba, 3, out, 8, &no) == PCE_E_ARG, "negative prior");
    int64_t hp[2] = {INT64_MAX, 1};
    ba[0].v = hp;
    CHECK(pce_entry_call("prob.bayes", ba, 3, out, 8, &no) == PCE_E_ARG, "prior sum overflow");
    ba[0].v = pr;
    int64_t d0[1] = {0};
    ba[2].v = d0;
    CHECK(pce_entry_call("prob.bayes", ba, 3, out, 8, &no) == PCE_E_ARG, "den 0");
    ba[2].v = den;
    ba[1].n = 1;
    CHECK(pce_entry_call("prob.bayes", ba, 3, out, 8, &no) == PCE_E_ARG, "length mismatch");
    ba[1].n = 2;
    ba[2].n = 2;
    CHECK(pce_entry_call("prob.bayes", ba, 3, out, 8, &no) == PCE_E_ARG, "den is one value");

    int64_t pay[9] = {100, 20, -50, 50, 30, 0, 20, 20, 20}, nopt[1] = {3};
    int64_t lo[3] = {1, 1, 1}, hi[3] = {1, 1, 1}, d3[1] = {3};
    pce_arg_t sa[5] = {
        {"payoff", pay, 9}, {"nopt", nopt, 1}, {"lo", lo, 3}, {"hi", hi, 3}, {"den", d3, 1}};
    CHECK(pce_entry_call("strat.decide", sa, 5, out, 8, &no) == PCE_OK &&
              out[0].num == LPRES_STATE_BOTH && out[0].den == 0,
          "strat.decide: BOTH");
    CHECK(pce_entry_call("strat.leader", sa, 5, out, 8, &no) == PCE_OK && out[0].num == 1 &&
              out[0].den == 1,
          "strat.leader: balanced");
    sa[1].v = d3; /* nopt 3 still */
    int64_t n4[1] = {4};
    sa[1].v = n4;
    CHECK(pce_entry_call("strat.decide", sa, 5, out, 8, &no) == PCE_E_ARG, "payoff size");
    int64_t n0[1] = {0};
    sa[1].v = n0;
    CHECK(pce_entry_call("strat.decide", sa, 5, out, 8, &no) == PCE_E_ARG, "nopt 0");
    sa[1].v = nopt;
    sa[3].n = 2;
    CHECK(pce_entry_call("strat.decide", sa, 5, out, 8, &no) == PCE_E_ARG, "lo/hi lengths");
    sa[3].n = 3;
    sa[4].v = d0;
    CHECK(pce_entry_call("strat.decide", sa, 5, out, 8, &no) == PCE_E_ARG, "den 0");
    sa[4].v = d3;
    int64_t one1[1] = {1}, zz[1] = {0}, oo[1] = {1};
    pce_arg_t s1[5] = {
        {"payoff", one1, 1}, {"nopt", one1, 1}, {"lo", zz, 1}, {"hi", oo, 1}, {"den", one1, 1}};
    CHECK(pce_entry_call("strat.decide", s1, 5, out, 8, &no) == PCE_OK &&
              out[0].num == LPRES_STATE_TRUE,
          "one option: TRUE");
    CHECK(pce_entry_call("strat.leader", s1, 5, out, 8, &no) == PCE_OK && out[0].num == 0,
          "leader 0");

    int64_t smin[2] = {100, 110}, bmax[2] = {150, 140};
    pce_arg_t za[2] = {{"seller_min", smin, 2}, {"buyer_max", bmax, 2}};
    CHECK(pce_entry_call("strat.zopa", za, 2, out, 8, &no) == PCE_OK &&
              out[0].num == LPRES_STATE_TRUE,
          "zopa: TRUE");
    za[1].n = 1;
    CHECK(pce_entry_call("strat.zopa", za, 2, out, 8, &no) == PCE_E_ARG, "zopa lengths");

    int64_t cf[3] = {-1000, 600, 600}, rn_[3] = {5, 10, 15}, rd[3] = {100, 100, 100};
    pce_arg_t na3[3] = {{"cf", cf, 3}, {"rate_num", rn_, 3}, {"rate_den", rd, 3}};
    CHECK(pce_entry_call("fin.npv", na3, 3, out, 8, &no) == PCE_OK && no == 3u &&
              out[0].num == 17000 && out[0].den == 147,
          "fin.npv");
    CHECK(pce_entry_call("fin.npv_sign", na3, 3, out, 8, &no) == PCE_OK && no == 1u &&
              out[0].num == LPRES_STATE_BOTH,
          "fin.npv_sign: BOTH");
    na3[2].n = 2;
    CHECK(pce_entry_call("fin.npv", na3, 3, out, 8, &no) == PCE_E_ARG, "rate lengths");
    na3[2].n = 3;
    int64_t rd0[3] = {100, 0, 100};
    na3[2].v = rd0;
    CHECK(pce_entry_call("fin.npv", na3, 3, out, 8, &no) == PCE_E_ARG, "rate den 0");
    int64_t lng[PCE_FIN_CF], rn2[1] = {1}, rd2[1] = {97};
    for (uint32_t t = 0; t < PCE_FIN_CF; t++) lng[t] = 1000003;
    pce_arg_t big[3] = {{"cf", lng, PCE_FIN_CF}, {"rate_num", rn2, 1}, {"rate_den", rd2, 1}};
    CHECK(pce_entry_call("fin.npv", big, 3, out, 8, &no) == PCE_E_OVERFLOW,
          "too large to be exact: refused");
    CHECK(pce_entry_call("fin.npv_sign", big, 3, out, 8, &no) == PCE_E_OVERFLOW, "the sign too");
}

void test_pce_fin(int *pass_, int *fail_)
{
    P = pass_;
    F = fail_;
    test_analyse();
    test_npv();
    test_risk();
}

void test_pce_entry(int *pass_, int *fail_)
{
    P = pass_;
    F = fail_;
    test_entries();
}
