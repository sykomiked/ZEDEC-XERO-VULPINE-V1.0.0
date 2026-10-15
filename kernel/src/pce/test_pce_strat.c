/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_pce_strat.c — the strategy engine (pce_strat.h).
 *
 * The three-option example where the rules split (BOTH), a dominant option
 * that ends the split (TRUE), total ignorance where the lower expectation is
 * exactly the worst case, the greedy bounds against a brute-force walk over
 * the corners of the evidence on 500 random cases, prohibited options that
 * are never ranked and never a regret baseline, the trend, the negotiation
 * range, and refusals at every edge. */
#include <stdio.h>
#include <string.h>
#include "pce_strat.h"

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

static uint64_t rs = 0x243F6A8885A308D3ull;
static uint32_t rn(uint32_t n)
{
    rs ^= rs << 13;
    rs ^= rs >> 7;
    rs ^= rs << 17;
    return (uint32_t) (rs % n);
}
static rat_t R(int64_t n, int64_t d)
{
    return rat_make(n, d);
}
static bool req(rat_t a, int64_t n, int64_t d)
{
    return a.valid && rat_eq(a, rat_make(n, d));
}

/* the smallest and largest expectation over the corners of the evidence:
 * every scenario at lo or hi except one, which takes what is left */
static void brute(const int64_t *pay, const rat_t *lo, const rat_t *hi, uint32_t n, rat_t *mn,
                  rat_t *mx)
{
    bool any = false;
    for (uint32_t free_s = 0; free_s < n; free_s++)
        for (uint32_t mask = 0; mask < (1u << n); mask++) {
            rat_t p[PCE_STRAT_SCEN], sum = rat_zero();
            for (uint32_t s = 0; s < n; s++) {
                if (s == free_s) continue;
                p[s] = (mask >> s) & 1u ? hi[s] : lo[s];
                sum = rat_add(sum, p[s]);
            }
            p[free_s] = rat_sub(rat_from_int(1), sum);
            if (rat_cmp(p[free_s], lo[free_s]) < 0 || rat_cmp(p[free_s], hi[free_s]) > 0) continue;
            rat_t e = rat_zero();
            for (uint32_t s = 0; s < n; s++) e = rat_add(e, rat_mul(p[s], rat_from_int(pay[s])));
            if (!any || rat_cmp(e, *mn) < 0) *mn = e;
            if (!any || rat_cmp(e, *mx) > 0) *mx = e;
            any = true;
        }
}

void test_pce_strat(int *pass_, int *fail_)
{
    P = pass_;
    F = fail_;
    /* aggressive, balanced, safe against boom, steady, bust */
    int64_t pay[3 * 3] = {100, 20, -50, 50, 30, 0, 20, 20, 20};
    rat_t third[3] = {R(1, 3), R(1, 3), R(1, 3)};
    pce_strat_in_t in = {3, 3, pay, third, third, NULL, PCE_PURPOSE_BUSINESS, LPX_PH_MANIFEST};
    pce_strat_result_t r, r2;
    CHECK(pce_strat_decide(&in, NULL, NULL, &r) == PCE_OK, "decide");
    CHECK(req(r.lower[0], 70, 3) && req(r.lower[1], 80, 3) && req(r.lower[2], 20, 1),
          "expectations exact");
    CHECK(rat_eq(r.lower[1], r.upper[1]), "precise evidence: lower = upper");
    CHECK(r.worst[0] == -50 && r.worst[1] == 0 && r.worst[2] == 20, "worst cases");
    CHECK(r.regret[0] == 70 && r.regret[1] == 50 && r.regret[2] == 80, "regrets");
    CHECK(r.pick[PCE_RULE_LOWER_EV] == 1u && r.pick[PCE_RULE_MAXIMIN] == 2u &&
              r.pick[PCE_RULE_REGRET] == 1u,
          "the rules split: balanced, safe, balanced");
    CHECK(r.leader == 1u && r.votes == 2u, "leader with two votes");
    CHECK(lpx_base(r.v.tag) == LPX_B_BOTH && r.v.act == LPX_ACT_ESCALATE,
          "split rules: BOTH, a person weighs the trade-off");
    CHECK(r.contention == 0x2u, "only balanced stays in contention on exact evidence");
    CHECK(r.refused == 0u, "nothing refused");

    /* a fourth option better everywhere ends the split */
    int64_t pay4[4 * 3] = {100, 20, -50, 50, 30, 0, 20, 20, 20, 100, 30, 25};
    pce_strat_in_t in4 = in;
    in4.nopt = 4;
    in4.payoff = pay4;
    CHECK(pce_strat_decide(&in4, &r, NULL, &r2) == PCE_OK, "decide with a dominant option");
    CHECK(r2.leader == 3u && r2.votes == 3u && lpx_base(r2.v.tag) == LPX_B_TRUE, "TRUE");
    CHECK(r2.regret[3] == 0 && r2.contention == 0x8u, "no regret, alone in contention");
    CHECK(lpx_pol(r2.v.tag) == LPX_P_NEGENTROPIC && r2.v.act == LPX_ACT_PROCEED,
          "votes gathered: converging");
    pce_strat_result_t r3;
    CHECK(pce_strat_decide(&in, &r2, NULL, &r3) == PCE_OK && lpx_pol(r3.v.tag) == LPX_P_ENTROPIC,
          "votes scattered: diverging");
    CHECK(pce_strat_decide(&in, &r, NULL, &r3) == PCE_OK && lpx_pol(r3.v.tag) == LPX_P_NEUTRAL,
          "same votes: steady");
    CHECK(memcmp(r.out, r3.out, 32) != 0 || memcmp(r.in, r3.in, 32) != 0,
          "the previous result is part of the input digest");

    /* advisory leader */
    uint32_t fl[4] = {0, 0, 0, PCE_ADV_LIFE_SAFETY};
    in4.flags = fl;
    CHECK(pce_strat_decide(&in4, NULL, NULL, &r2) == PCE_OK && lpx_base(r2.v.tag) == LPX_B_TRUE &&
              r2.v.act == LPX_ACT_ESCALATE,
          "life-safety leader escalates");
    fl[3] = 0;
    fl[0] = PCE_ADV_DEVICE;
    CHECK(pce_strat_decide(&in4, NULL, NULL, &r2) == PCE_OK && r2.v.act == LPX_ACT_PROCEED,
          "only the leader's flags count");

    /* a prohibited option is never ranked, never a baseline */
    int64_t payp[3 * 3] = {1000000, 1000000, 1000000, 50, 30, 0, 20, 20, 20};
    uint32_t pf[3] = {PCE_OPT_PROHIBITED, 0, 0};
    pce_strat_in_t ip = in;
    ip.payoff = payp;
    ip.flags = pf;
    CHECK(pce_strat_decide(&ip, NULL, NULL, &r2) == PCE_OK, "with a prohibited option");
    CHECK(r2.refused == 0x1u && (r2.contention & 1u) == 0u, "refused, out of contention");
    for (uint32_t k = 0; k < PCE_RULES; k++) CHECK(r2.pick[k] != 0u, "never picked");
    CHECK(r2.regret[1] == 20 && r2.regret[2] == 30, "regret ignores the refused option");
    CHECK(r2.leader == 1u, "balanced leads");
    uint32_t allp[3] = {PCE_OPT_PROHIBITED, PCE_OPT_PROHIBITED, PCE_OPT_PROHIBITED};
    ip.flags = allp;
    CHECK(pce_strat_decide(&ip, NULL, NULL, &r2) == PCE_OK && r2.leader == PCE_STRAT_NONE &&
              lpx_base(r2.v.tag) == LPX_B_NEITHER && r2.refused == 0x7u && r2.votes == 0u,
          "nothing allowed: NEITHER");
    CHECK(pce_strat_decide(&in, &r2, NULL, &r3) == PCE_OK && lpx_pol(r3.v.tag) == LPX_P_NEUTRAL,
          "no previous leader: no trend");

    /* total ignorance: lower expectation is the worst case, upper the best */
    rat_t z3[3] = {R(0, 1), R(0, 1), R(0, 1)}, o3[3] = {R(1, 1), R(1, 1), R(1, 1)};
    pce_strat_in_t ig = in;
    ig.lo = z3;
    ig.hi = o3;
    CHECK(pce_strat_decide(&ig, NULL, NULL, &r2) == PCE_OK, "ignorance");
    for (uint32_t o = 0; o < 3u; o++) {
        CHECK(rat_eq(r2.lower[o], rat_from_int(r2.worst[o])), "lower = worst under ignorance");
        int64_t mx = pay[o * 3];
        for (uint32_t s = 1; s < 3u; s++)
            if (pay[o * 3 + s] > mx) mx = pay[o * 3 + s];
        CHECK(rat_eq(r2.upper[o], rat_from_int(mx)), "upper = best under ignorance");
    }
    CHECK(r2.pick[PCE_RULE_LOWER_EV] == 2u && lpx_base(r2.v.tag) == LPX_B_BOTH,
          "ignorance: lower-EV turns cautious, still split with regret");
    CHECK(r2.contention == 0x7u, "nothing is ruled out by evidence that says nothing");

    /* an impossible scenario is ignored */
    rat_t lo2[3] = {R(1, 2), R(1, 2), R(0, 1)}, hi2[3] = {R(1, 2), R(1, 2), R(0, 1)};
    pce_strat_in_t im = in;
    im.lo = lo2;
    im.hi = hi2;
    CHECK(pce_strat_decide(&im, NULL, NULL, &r2) == PCE_OK && r2.worst[0] == 20 &&
              r2.regret[0] == 10 && r2.regret[2] == 80,
          "bust cannot happen: no -50 worst case");
    CHECK(r2.pick[PCE_RULE_MAXIMIN] == 0u || r2.pick[PCE_RULE_MAXIMIN] == 1u,
          "maximin among possible scenarios");

    /* greedy bounds = brute force over the corners */
    for (uint32_t k = 0; k < 500u; k++) {
        uint32_t n = 1u + rn(4);
        rat_t lo[4], hi[4];
        int64_t pv[4];
        int64_t d = 12;
        /* build bounds around a random distribution so they are feasible */
        int64_t base[4], left = d;
        for (uint32_t s = 0; s < n; s++) {
            base[s] = s + 1u == n ? left : (int64_t) rn((uint32_t) left + 1u);
            left -= base[s];
        }
        for (uint32_t s = 0; s < n; s++) {
            int64_t a = base[s] - (int64_t) rn(3), b = base[s] + (int64_t) rn(3);
            if (a < 0) a = 0;
            if (b > d) b = d;
            lo[s] = R(a, d);
            hi[s] = R(b, d);
            pv[s] = (int64_t) rn(201) - 100;
        }
        rat_t lw, up, bmn, bmx;
        CHECK(pce_strat_bounds(pv, lo, hi, n, &lw, &up) == PCE_OK, "bounds");
        brute(pv, lo, hi, n, &bmn, &bmx);
        CHECK(rat_eq(lw, bmn), "greedy lower = brute-force minimum");
        CHECK(rat_eq(up, bmx), "greedy upper = brute-force maximum");
    }

    /* ties keep the lowest index and agree */
    int64_t tie[2 * 2] = {5, 5, 5, 5};
    rat_t h2[2] = {R(1, 2), R(1, 2)};
    pce_strat_in_t ti = {2, 2, tie, h2, h2, NULL, PCE_PURPOSE_SCHEDULING, LPX_PH_MANIFEST};
    CHECK(pce_strat_decide(&ti, NULL, NULL, &r2) == PCE_OK && r2.leader == 0u && r2.votes == 3u &&
              r2.contention == 0x3u,
          "ties: lowest index, both in contention");

    /* edges */
    int64_t ext[2 * 2] = {PCE_STRAT_PAYOFF_MAX, -PCE_STRAT_PAYOFF_MAX, -PCE_STRAT_PAYOFF_MAX,
                          PCE_STRAT_PAYOFF_MAX};
    ti.payoff = ext;
    CHECK(pce_strat_decide(&ti, NULL, NULL, &r2) == PCE_OK &&
              r2.regret[0] == 2 * PCE_STRAT_PAYOFF_MAX,
          "regret at the payoff limit, no overflow");
    ext[0] = PCE_STRAT_PAYOFF_MAX + 1;
    CHECK(pce_strat_decide(&ti, NULL, NULL, &r2) == PCE_E_ARG, "payoff above the limit");
    ext[0] = -PCE_STRAT_PAYOFF_MAX - 1;
    CHECK(pce_strat_decide(&ti, NULL, NULL, &r2) == PCE_E_ARG, "payoff below the limit");
    pce_strat_result_t keep = r2;
    pce_strat_in_t b = in;
    b.purpose = PCE_PURPOSE_FINANCE;
    CHECK(pce_strat_decide(&b, NULL, NULL, &r2) == PCE_E_PURPOSE, "finance is not strategy");
    b.purpose = 0;
    CHECK(pce_strat_decide(&b, NULL, NULL, &r2) == PCE_E_PURPOSE, "no purpose");
    b = in;
    b.nopt = 0;
    CHECK(pce_strat_decide(&b, NULL, NULL, &r2) == PCE_E_ARG, "no options");
    b.nopt = PCE_STRAT_OPTS + 1u;
    CHECK(pce_strat_decide(&b, NULL, NULL, &r2) == PCE_E_ARG, "too many options");
    b = in;
    b.nscen = 0;
    CHECK(pce_strat_decide(&b, NULL, NULL, &r2) == PCE_E_ARG, "no scenarios");
    b = in;
    b.phase = 4;
    CHECK(pce_strat_decide(&b, NULL, NULL, &r2) == PCE_E_ARG, "phase");
    b = in;
    b.payoff = NULL;
    CHECK(pce_strat_decide(&b, NULL, NULL, &r2) == PCE_E_ARG, "NULL payoff");
    b = in;
    b.lo = NULL;
    CHECK(pce_strat_decide(&b, NULL, NULL, &r2) == PCE_E_ARG, "NULL lo");
    rat_t big[3] = {R(1, 2), R(1, 2), R(1, 2)};
    b = in;
    b.lo = big;
    b.hi = big;
    CHECK(pce_strat_decide(&b, NULL, NULL, &r2) == PCE_E_ARG, "sum lo above 1");
    rat_t small[3] = {R(1, 4), R(1, 4), R(1, 4)};
    b.lo = small;
    b.hi = small;
    CHECK(pce_strat_decide(&b, NULL, NULL, &r2) == PCE_E_ARG, "sum hi below 1");
    rat_t inv_lo[3] = {R(1, 2), R(0, 1), R(0, 1)}, inv_hi[3] = {R(1, 4), R(1, 1), R(1, 1)};
    b.lo = inv_lo;
    b.hi = inv_hi;
    CHECK(pce_strat_decide(&b, NULL, NULL, &r2) == PCE_E_ARG, "lo above hi");
    rat_t over_hi[3] = {R(3, 2), R(0, 1), R(0, 1)};
    b.lo = z3;
    b.hi = over_hi;
    CHECK(pce_strat_decide(&b, NULL, NULL, &r2) == PCE_E_ARG, "hi above 1");
    rat_t neg_lo[3] = {R(-1, 2), R(0, 1), R(0, 1)};
    b.lo = neg_lo;
    b.hi = o3;
    CHECK(pce_strat_decide(&b, NULL, NULL, &r2) == PCE_E_ARG, "negative lo");
    rat_t bad_hi[3] = {R(1, 0), R(1, 1), R(1, 1)};
    b.lo = z3;
    b.hi = bad_hi;
    CHECK(pce_strat_decide(&b, NULL, NULL, &r2) == PCE_E_ARG, "invalid hi");
    CHECK(pce_strat_decide(NULL, NULL, NULL, &r2) == PCE_E_ARG, "NULL in");
    CHECK(pce_strat_decide(&in, NULL, NULL, NULL) == PCE_E_ARG, "NULL out");
    CHECK(memcmp(&keep, &r2, sizeof keep) == 0, "refusals leave out");
    rat_t lw, up;
    CHECK(pce_strat_bounds(NULL, third, third, 3, &lw, &up) == PCE_E_ARG, "bounds NULL payoff");
    CHECK(pce_strat_bounds(pay, third, third, 3, NULL, &up) == PCE_E_ARG, "bounds NULL lower");
    CHECK(pce_strat_bounds(pay, third, third, 3, &lw, NULL) == PCE_E_ARG, "bounds NULL upper");
    CHECK(pce_strat_bounds(pay, third, third, PCE_STRAT_SCEN + 1u, &lw, &up) == PCE_E_ARG,
          "bounds: too many scenarios");
    int64_t hugep[3] = {PCE_STRAT_PAYOFF_MAX + 1, 0, 0};
    CHECK(pce_strat_bounds(hugep, third, third, 3, &lw, &up) == PCE_E_ARG, "bounds payoff");
    rat_t odd[3] = {R(1, 2147483647), R(1, 2147483629), R(0, 1)};
    rat_t oddh[3] = {R(1, 1), R(1, 1), R(1, 1)};
    int64_t pp[3] = {PCE_STRAT_PAYOFF_MAX, PCE_STRAT_PAYOFF_MAX - 1, 3};
    int rc = pce_strat_bounds(pp, odd, oddh, 3, &lw, &up);
    CHECK(rc == PCE_OK || rc == PCE_E_OVERFLOW, "large exact bounds compute or refuse");
    if (rc == PCE_OK) CHECK(lw.valid && up.valid, "valid when computed");

    /* logging and replay */
    pdx_rec_t sa[1];
    pdx_log_t la;
    pdx_log_init(&la, sa, 1);
    CHECK(pce_strat_decide(&in, NULL, &la, &r2) == PCE_OK && la.n == 1u, "logged");
    CHECK(pce_strat_decide(&in, NULL, NULL, &r3) == PCE_OK && memcmp(r2.in, r3.in, 32) == 0 &&
              memcmp(r2.out, r3.out, 32) == 0,
          "replay matches");
    CHECK(sa[0].op == PCE_OP_STRAT && sa[0].aux == 2u && memcmp(sa[0].a, r2.in, 32) == 0,
          "the record");
    CHECK(pce_strat_decide(&in4, NULL, NULL, &r3) == PCE_OK && memcmp(r2.in, r3.in, 32) != 0,
          "other inputs, other digest");
    keep = r3;
    CHECK(pce_strat_decide(&in, NULL, &la, &r3) == PCE_E_LOG, "full log");
    CHECK(memcmp(&keep, &r3, sizeof keep) == 0, "full log leaves out");

    /* negotiation range */
    pce_zopa_t z, zk;
    pce_est_t e1[3] = {{100, 150}, {110, 140}, {90, 130}};
    CHECK(pce_strat_zopa(e1, 3, PCE_PURPOSE_NEGOTIATION, 0, LPX_PH_MANIFEST, NULL, &z) == PCE_OK,
          "zopa");
    CHECK(lpx_base(z.v.tag) == LPX_B_TRUE && z.range_lo == 110 && z.range_hi == 130 &&
              z.yes == 3u && z.no == 0u,
          "a deal in 110..130 under every estimate");
    CHECK(z.v.act == LPX_ACT_PROCEED, "proceeds when not advisory");
    CHECK(pce_strat_zopa(e1, 3, PCE_PURPOSE_NEGOTIATION, PCE_ADV_MONEY, 0, NULL, &z) == PCE_OK &&
              z.v.act == LPX_ACT_ESCALATE,
          "money: a person approves");
    pce_est_t e2[2] = {{100, 120}, {130, 150}};
    CHECK(pce_strat_zopa(e2, 2, PCE_PURPOSE_BUSINESS, 0, 0, NULL, &z) == PCE_OK &&
              lpx_base(z.v.tag) == LPX_B_BOTH && z.yes == 2u && z.range_lo == 0 && z.range_hi == 0,
          "a deal under each view but no common price: BOTH");
    pce_est_t e3[2] = {{100, 120}, {130, 110}};
    CHECK(pce_strat_zopa(e3, 2, PCE_PURPOSE_NEGOTIATION, 0, 0, NULL, &z) == PCE_OK &&
              lpx_base(z.v.tag) == LPX_B_BOTH && z.yes == 1u && z.no == 1u,
          "estimates disagree: BOTH");
    pce_est_t e4[2] = {{130, 120}, {140, 110}};
    CHECK(pce_strat_zopa(e4, 2, PCE_PURPOSE_NEGOTIATION, 0, 0, NULL, &z) == PCE_OK &&
              lpx_base(z.v.tag) == LPX_B_FALSE && z.v.act == LPX_ACT_REFUSE,
          "no deal under any estimate: FALSE");
    pce_est_t e5[1] = {{120, 120}};
    CHECK(pce_strat_zopa(e5, 1, PCE_PURPOSE_NEGOTIATION, 0, 0, NULL, &z) == PCE_OK &&
              lpx_base(z.v.tag) == LPX_B_TRUE && z.range_lo == 120 && z.range_hi == 120,
          "a single price is a range");
    CHECK(pce_strat_zopa(NULL, 0, PCE_PURPOSE_NEGOTIATION, 0, 0, NULL, &z) == PCE_OK &&
              lpx_base(z.v.tag) == LPX_B_NEITHER && z.v.act == LPX_ACT_WAIT,
          "no estimate: NEITHER");
    zk = z;
    CHECK(pce_strat_zopa(e1, 3, PCE_PURPOSE_GAME, 0, 0, NULL, &z) == PCE_E_PURPOSE, "purpose");
    CHECK(pce_strat_zopa(NULL, 1, PCE_PURPOSE_NEGOTIATION, 0, 0, NULL, &z) == PCE_E_ARG, "NULL");
    CHECK(pce_strat_zopa(e1, PCE_STRAT_EST + 1u, PCE_PURPOSE_NEGOTIATION, 0, 0, NULL, &z) ==
              PCE_E_ARG,
          "too many estimates");
    CHECK(pce_strat_zopa(e1, 3, PCE_PURPOSE_NEGOTIATION, 0, 4, NULL, &z) == PCE_E_ARG, "phase");
    CHECK(pce_strat_zopa(e1, 3, PCE_PURPOSE_NEGOTIATION, 0, 0, NULL, NULL) == PCE_E_ARG, "NULL");
    CHECK(memcmp(&zk, &z, sizeof z) == 0, "zopa refusals leave out");
    pdx_rec_t zs[1];
    pdx_log_t zl;
    pdx_log_init(&zl, zs, 1);
    pce_zopa_t z2;
    CHECK(pce_strat_zopa(e1, 3, PCE_PURPOSE_NEGOTIATION, 0, 0, &zl, &z) == PCE_OK && zl.n == 1u &&
              zs[0].aux == 3u,
          "zopa logged");
    CHECK(pce_strat_zopa(e1, 3, PCE_PURPOSE_NEGOTIATION, 0, 0, NULL, &z2) == PCE_OK &&
              memcmp(z.in, z2.in, 32) == 0 && memcmp(z.out, z2.out, 32) == 0,
          "zopa replay");
    CHECK(pce_strat_zopa(e1, 3, PCE_PURPOSE_BUSINESS, 0, 0, NULL, &z2) == PCE_OK &&
              memcmp(z.in, z2.in, 32) != 0,
          "purpose in the digest");
    CHECK(pce_strat_zopa(e1, 3, PCE_PURPOSE_NEGOTIATION, 0, 0, &zl, &z2) == PCE_E_LOG,
          "zopa full log");
}
