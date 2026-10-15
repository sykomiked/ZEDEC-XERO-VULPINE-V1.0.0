/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_pce.c — the paraconsistent decision engines (pce.h).
 *
 * Probability: masses rebuilt from the lpres tables entry by entry, the
 * join's algebra (commutative, associative, NEITHER the identity, BOTH
 * absorbing) checked exactly on random masses, Zadeh's example through the
 * join and through Dempster's rule, exact Bayes against a hand result,
 * thresholds at their boundaries, logging and replay digests.
 * Tactics, strategy and finance follow in their own sections. */
#include <stdio.h>
#include <string.h>
#include "pce.h"
#include "pce_prob.h"
#include "pce_tac.h"
#include "pce_strat.h"
#include "pce_fin.h"
#include "pce_entry.h"

static int pass, fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("[FAIL] %s (%s:%d)\n", msg, __FILE__, __LINE__);                                \
        }                                                                                          \
    } while (0)

static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}
static uint32_t rndn(uint32_t n)
{
    return (uint32_t) (rnd() % n);
}

static rat_t R(int64_t n, int64_t d)
{
    return rat_make(n, d);
}

static bool req(rat_t a, int64_t n, int64_t d)
{
    return a.valid && rat_eq(a, rat_make(n, d));
}

static bool mass_eq(const pce_mass_t *a, const pce_mass_t *b)
{
    for (uint32_t i = 0; i < 4u; i++)
        if (!rat_eq(a->m[i], b->m[i])) return false;
    return true;
}

static pce_mass_t rnd_mass(void)
{
    pce_mass_t m;
    uint64_t c[4];
    for (uint32_t i = 0; i < 4u; i++) c[i] = rndn(7);
    pce_mass_make(&m, c[1], c[2], c[3], c[0]);
    return m;
}

static pce_mass_t pure(lpres_state_t s)
{
    pce_mass_t m;
    for (uint32_t i = 0; i < 4u; i++) m.m[i] = rat_zero();
    m.m[s] = rat_from_int(1);
    return m;
}

/* ===================================================================== */
static void test_mass_basics(void)
{
    pce_mass_t m;
    CHECK(pce_mass_make(&m, 3, 1, 0, 0) == PCE_OK, "make 3:1");
    CHECK(req(m.m[LPRES_STATE_TRUE], 3, 4) && req(m.m[LPRES_STATE_FALSE], 1, 4), "make 3:1 values");
    CHECK(req(m.m[LPRES_STATE_BOTH], 0, 1) && req(m.m[LPRES_STATE_NEITHER], 0, 1),
          "make 3:1 zeros");
    CHECK(pce_mass_make(&m, 0, 0, 0, 0) == PCE_OK && req(m.m[LPRES_STATE_NEITHER], 1, 1),
          "all zero counts are pure ignorance");
    CHECK(pce_mass_make(&m, 1, 1, 1, 1) == PCE_OK && req(m.m[LPRES_STATE_BOTH], 1, 4),
          "make 1:1:1:1");
    pce_mass_t keep = m;
    CHECK(pce_mass_make(&m, 0x8000000000000000ull, 0, 0, 0) == PCE_E_ARG, "count > INT64_MAX");
    CHECK(pce_mass_make(&m, 0x7FFFFFFFFFFFFFFFull, 1, 0, 0) == PCE_E_ARG, "total overflow f");
    CHECK(pce_mass_make(&m, 0x7FFFFFFFFFFFFFFFull, 0, 1, 0) == PCE_E_ARG, "total overflow b");
    CHECK(pce_mass_make(&m, 0x7FFFFFFFFFFFFFFFull, 0, 0, 1) == PCE_E_ARG, "total overflow n");
    CHECK(pce_mass_make(&m, 0, 0x8000000000000000ull, 0, 0) == PCE_E_ARG, "f > INT64_MAX");
    CHECK(pce_mass_make(&m, 0, 0, 0x8000000000000000ull, 0) == PCE_E_ARG, "b > INT64_MAX");
    CHECK(pce_mass_make(&m, 0, 0, 0, 0x8000000000000000ull) == PCE_E_ARG, "n > INT64_MAX");
    CHECK(mass_eq(&m, &keep), "refused make leaves out untouched");
    CHECK(pce_mass_make(NULL, 1, 0, 0, 0) == PCE_E_ARG, "make NULL");
    CHECK(pce_mass_make(&m, 0x7FFFFFFFFFFFFFFFull, 0, 0, 0) == PCE_OK &&
              req(m.m[LPRES_STATE_TRUE], 1, 1),
          "make at INT64_MAX");

    CHECK(pce_mass_set(&m, R(1, 2), R(1, 2), R(0, 1), R(0, 1)) == PCE_OK, "set halves");
    CHECK(pce_mass_set(&m, R(1, 2), R(1, 3), R(0, 1), R(0, 1)) == PCE_E_ARG, "set sum < 1");
    CHECK(pce_mass_set(&m, R(2, 3), R(1, 2), R(0, 1), R(0, 1)) == PCE_E_ARG, "set sum > 1");
    CHECK(pce_mass_set(&m, R(3, 2), R(-1, 2), R(0, 1), R(0, 1)) == PCE_E_ARG, "set negative");
    CHECK(pce_mass_set(&m, R(1, 0), R(0, 1), R(0, 1), R(1, 1)) == PCE_E_ARG, "set invalid");
    CHECK(pce_mass_set(NULL, R(1, 1), R(0, 1), R(0, 1), R(0, 1)) == PCE_E_ARG, "set NULL");
    CHECK(!pce_mass_valid(NULL), "NULL is not a mass");
}

static void test_join_algebra(void)
{
    pce_mass_t t = pure(LPRES_STATE_TRUE), f = pure(LPRES_STATE_FALSE);
    pce_mass_t b = pure(LPRES_STATE_BOTH), n = pure(LPRES_STATE_NEITHER), r;
    CHECK(pce_mass_join(&t, &f, &r) == PCE_OK && mass_eq(&r, &b), "T join F = B");
    CHECK(pce_mass_join(&t, &n, &r) == PCE_OK && mass_eq(&r, &t), "T join N = T");
    CHECK(pce_mass_join(&f, &f, &r) == PCE_OK && mass_eq(&r, &f), "F join F = F");
    CHECK(pce_mass_meet(&t, &f, &r) == PCE_OK && mass_eq(&r, &n), "T meet F = N");
    CHECK(pce_mass_meet(&b, &t, &r) == PCE_OK && mass_eq(&r, &t), "B meet T = T");
    CHECK(pce_mass_meet(&b, &f, &r) == PCE_OK && mass_eq(&r, &f), "B meet F = F");

    /* pure masses reproduce the bit tables entry by entry */
    for (uint32_t i = 0; i < 4u; i++)
        for (uint32_t j = 0; j < 4u; j++) {
            pce_mass_t a = pure((lpres_state_t) i), c = pure((lpres_state_t) j), o, e;
            pce_mass_join(&a, &c, &o);
            e = pure((lpres_state_t) (i | j));
            CHECK(mass_eq(&o, &e), "join table entry");
            pce_mass_meet(&a, &c, &o);
            e = pure((lpres_state_t) (i & j));
            CHECK(mass_eq(&o, &e), "meet table entry");
            pce_mass_and(&a, &c, &o);
            e = pure(lpres_conjoin((lpres_state_t) i, (lpres_state_t) j));
            CHECK(mass_eq(&o, &e), "and = lpres_conjoin");
            pce_mass_or(&a, &c, &o);
            e = pure(lpres_disjoin((lpres_state_t) i, (lpres_state_t) j));
            CHECK(mass_eq(&o, &e), "or = lpres_disjoin");
        }
    for (uint32_t i = 0; i < 4u; i++) {
        pce_mass_t a = pure((lpres_state_t) i), o, e = pure(lpres_negate((lpres_state_t) i));
        CHECK(pce_mass_not(&a, &o) == PCE_OK && mass_eq(&o, &e), "not = lpres_negate");
    }

    for (int k = 0; k < 300; k++) {
        pce_mass_t x = rnd_mass(), y = rnd_mass(), z = rnd_mass(), p, q, s1, s2;
        CHECK(pce_mass_join(&x, &y, &p) == PCE_OK && pce_mass_valid(&p), "join is a mass");
        pce_mass_join(&y, &x, &q);
        CHECK(mass_eq(&p, &q), "join commutes");
        pce_mass_join(&p, &z, &s1);
        pce_mass_join(&y, &z, &q);
        pce_mass_join(&x, &q, &s2);
        CHECK(mass_eq(&s1, &s2), "join associates");
        pce_mass_join(&x, &n, &p);
        CHECK(mass_eq(&p, &x), "N is the join identity");
        pce_mass_join(&x, &b, &p);
        CHECK(mass_eq(&p, &b), "B absorbs the join");
        pce_mass_meet(&x, &b, &p);
        CHECK(mass_eq(&p, &x), "B is the meet identity");
        /* conflict never shrinks under join: m'[B] >= m[B] */
        pce_mass_join(&x, &y, &p);
        CHECK(rat_cmp(p.m[LPRES_STATE_BOTH], x.m[LPRES_STATE_BOTH]) >= 0, "join keeps conflict");
        /* De Morgan and double negation, exactly */
        pce_mass_t na, nb, l, rr, nn;
        pce_mass_and(&x, &y, &p);
        pce_mass_not(&p, &l);
        pce_mass_not(&x, &na);
        pce_mass_not(&y, &nb);
        pce_mass_or(&na, &nb, &rr);
        /* lpres's tables are min/max on F < N < B < T (owner decision
         * 2026-10-12), so De Morgan holds except for the BOTH/NEITHER pair,
         * and the mass shows exactly that much difference. */
        rat_t cross = rat_add(rat_mul(x.m[LPRES_STATE_BOTH], y.m[LPRES_STATE_NEITHER]),
                              rat_mul(x.m[LPRES_STATE_NEITHER], y.m[LPRES_STATE_BOTH]));
        if (rat_is_zero(cross))
            CHECK(mass_eq(&l, &rr), "not(x and y) = not x or not y");
        else
            CHECK(rat_eq(rat_sub(l.m[LPRES_STATE_NEITHER], rr.m[LPRES_STATE_NEITHER]), cross) &&
                      rat_eq(rat_sub(rr.m[LPRES_STATE_BOTH], l.m[LPRES_STATE_BOTH]), cross),
                  "De Morgan gap is exactly the BOTH x NEITHER mass");
        pce_mass_not(&na, &nn);
        CHECK(mass_eq(&nn, &x), "not not x = x");
        CHECK(pce_mass_and(&x, &y, &p) == PCE_OK && pce_mass_valid(&p), "and is a mass");
        CHECK(pce_mass_or(&x, &y, &p) == PCE_OK && pce_mass_valid(&p), "or is a mass");
        CHECK(pce_mass_meet(&x, &y, &p) == PCE_OK && pce_mass_valid(&p), "meet is a mass");
    }

    pce_mass_t bad = t;
    bad.m[LPRES_STATE_TRUE] = R(1, 2);
    CHECK(pce_mass_join(&bad, &t, &r) == PCE_E_ARG, "join refuses a non-mass (a)");
    CHECK(pce_mass_join(&t, &bad, &r) == PCE_E_ARG, "join refuses a non-mass (b)");
    CHECK(pce_mass_join(&t, &t, NULL) == PCE_E_ARG, "join NULL out");
    CHECK(pce_mass_not(&bad, &r) == PCE_E_ARG, "not refuses a non-mass");
    CHECK(pce_mass_not(&t, NULL) == PCE_E_ARG, "not NULL out");

    /* exact arithmetic overflow is refused, not wrapped */
    pce_mass_t big1, big2;
    const int64_t p1 = 2147483647, p2 = 2147483629; /* primes */
    pce_mass_set(&big1, R(1, p1), R(p1 - 1, p1), R(0, 1), R(0, 1));
    pce_mass_set(&big2, R(1, p2), R(p2 - 1, p2), R(0, 1), R(0, 1));
    pce_mass_t big3, o1, o2;
    if (pce_mass_join(&big1, &big2, &o1) == PCE_OK) {
        int rc = pce_mass_join(&o1, &big1, &o2);
        rc = rc == PCE_OK ? pce_mass_join(&o2, &big2, &big3) : rc;
        CHECK(rc == PCE_E_OVERFLOW, "deep join overflows into a refusal");
    } else {
        CHECK(0, "first big join should fit");
    }
}

static void test_zadeh(void)
{
    /* Two sources, each nearly sure of the opposite. */
    pce_mass_t a, b, j, d;
    rat_t k;
    pce_mass_set(&a, R(99, 100), R(1, 100), R(0, 1), R(0, 1));
    pce_mass_set(&b, R(1, 100), R(99, 100), R(0, 1), R(0, 1));
    CHECK(pce_mass_join(&a, &b, &j) == PCE_OK, "zadeh join");
    CHECK(req(j.m[LPRES_STATE_BOTH], 9802, 10000), "join keeps 98.02% conflict");
    CHECK(req(j.m[LPRES_STATE_TRUE], 99, 10000) && req(j.m[LPRES_STATE_FALSE], 99, 10000),
          "join: little for either side");
    CHECK(pce_mass_dempster(&a, &b, &d, &k) == PCE_OK, "zadeh dempster");
    CHECK(req(k, 9802, 10000), "dempster's K is the same conflict");
    CHECK(req(d.m[LPRES_STATE_TRUE], 1, 2) && req(d.m[LPRES_STATE_FALSE], 1, 2),
          "dempster turns flat contradiction into a coin flip");
    CHECK(req(d.m[LPRES_STATE_BOTH], 0, 1), "dempster has no conflict left");

    pce_thresh_t th = {R(9, 10), R(1, 10)};
    CHECK(pce_mass_base(&j, &th) == LPX_B_BOTH, "join reads BOTH");
    CHECK(pce_mass_base(&d, &th) == LPX_B_NEITHER, "dempster reads as merely unsure");

    /* Dempster agrees with the join when there is no conflict */
    pce_mass_t x, y;
    pce_mass_set(&x, R(1, 2), R(0, 1), R(0, 1), R(1, 2));
    pce_mass_set(&y, R(1, 3), R(0, 1), R(0, 1), R(2, 3));
    CHECK(pce_mass_dempster(&x, &y, &d, &k) == PCE_OK && req(k, 0, 1), "no conflict K = 0");
    CHECK(pce_mass_join(&x, &y, &j) == PCE_OK && mass_eq(&d, &j), "no conflict: rules agree");
    CHECK(pce_mass_dempster(&x, &y, &d, NULL) == PCE_OK, "dempster conflict NULL ok");

    pce_mass_t t = pure(LPRES_STATE_TRUE), f = pure(LPRES_STATE_FALSE), bb = pure(LPRES_STATE_BOTH);
    pce_mass_t keep = d;
    CHECK(pce_mass_dempster(&t, &f, &d, &k) == PCE_E_CONFLICT, "dempster total conflict");
    CHECK(mass_eq(&d, &keep), "dempster refusal leaves out");
    CHECK(pce_mass_dempster(&bb, &t, &d, &k) == PCE_E_ARG, "dempster has no BOTH (a)");
    CHECK(pce_mass_dempster(&t, &bb, &d, &k) == PCE_E_ARG, "dempster has no BOTH (b)");
    CHECK(pce_mass_dempster(&t, &t, NULL, &k) == PCE_E_ARG, "dempster NULL out");
}

static void test_read_and_base(void)
{
    pce_mass_t m;
    pce_read_t r;
    pce_mass_set(&m, R(1, 2), R(1, 5), R(1, 10), R(1, 5));
    CHECK(pce_mass_read(&m, &r) == PCE_OK, "read");
    CHECK(req(r.support, 3, 5) && req(r.against, 3, 10), "support, against");
    CHECK(req(r.conflict, 1, 10) && req(r.ignorance, 1, 5), "conflict, ignorance");
    CHECK(req(r.lo, 1, 2) && req(r.hi, 4, 5), "bounds [T, 1-F]");
    CHECK(pce_mass_read(&m, NULL) == PCE_E_ARG, "read NULL");
    CHECK(req(pce_mass_margin(&m), 3, 10), "margin");
    CHECK(!pce_mass_margin(NULL).valid, "margin of nothing is invalid");

    pce_thresh_t th = {R(9, 10), R(1, 10)};
    /* conflict exactly at its threshold is BOTH; just under is not */
    pce_mass_set(&m, R(9, 10), R(0, 1), R(1, 10), R(0, 1));
    CHECK(pce_mass_base(&m, &th) == LPX_B_BOTH, "conflict at threshold");
    pce_mass_set(&m, R(9, 10), R(0, 1), R(9, 100), R(1, 100));
    CHECK(pce_mass_base(&m, &th) == LPX_B_TRUE, "conflict under, T at threshold");
    pce_mass_set(&m, R(89, 100), R(0, 1), R(9, 100), R(2, 100));
    CHECK(pce_mass_base(&m, &th) == LPX_B_NEITHER, "T under threshold");
    pce_mass_set(&m, R(0, 1), R(9, 10), R(0, 1), R(1, 10));
    CHECK(pce_mass_base(&m, &th) == LPX_B_FALSE, "F at threshold");
    pce_mass_set(&m, R(0, 1), R(89, 100), R(0, 1), R(11, 100));
    CHECK(pce_mass_base(&m, &th) == LPX_B_NEITHER, "F under threshold");
    pce_mass_t n = pure(LPRES_STATE_NEITHER);
    CHECK(pce_mass_base(&n, &th) == LPX_B_NEITHER, "ignorance is NEITHER");
    pce_thresh_t bad = {R(0, 1), R(1, 10)};
    CHECK(pce_mass_base(&m, &bad) == LPX_B_NEITHER, "zero threshold refused");
    pce_thresh_t bad2 = {R(9, 10), R(0, 1)};
    CHECK(pce_mass_base(&m, &bad2) == LPX_B_NEITHER, "zero conflict threshold refused");
    pce_thresh_t bad3 = {R(11, 10), R(1, 10)};
    CHECK(pce_mass_base(&m, &bad3) == LPX_B_NEITHER, "threshold above 1 refused");
    CHECK(pce_mass_base(&m, NULL) == LPX_B_NEITHER, "NULL thresholds");
    CHECK(pce_mass_base(NULL, &th) == LPX_B_NEITHER, "NULL mass");
    pce_thresh_t one = {R(1, 1), R(1, 1)};
    pce_mass_t t = pure(LPRES_STATE_TRUE);
    CHECK(pce_mass_base(&t, &one) == LPX_B_TRUE, "threshold of exactly 1 allowed");
}

static void test_decide_and_log(void)
{
    pce_thresh_t th = {R(9, 10), R(1, 10)};
    pce_mass_t prev, cur;
    pce_verdict_t v;
    pce_mass_set(&prev, R(8, 10), R(1, 10), R(0, 1), R(1, 10));
    pce_mass_set(&cur, R(95, 100), R(0, 1), R(0, 1), R(5, 100));
    CHECK(pce_mass_decide(&prev, &cur, &th, false, LPX_PH_MANIFEST, 0, NULL, &v) == PCE_OK,
          "decide");
    CHECK(lpx_base(v.tag) == LPX_B_TRUE && lpx_pol(v.tag) == LPX_P_NEGENTROPIC,
          "true, margin growing");
    CHECK(v.act == LPX_ACT_PROCEED && v.kind == PDX_KIND_NONE, "proceeds, not a paradox");
    CHECK(pce_mass_decide(&prev, &cur, &th, false, LPX_PH_MANIFEST, PCE_ADV_DEVICE, NULL, &v) ==
                  PCE_OK &&
              v.act == LPX_ACT_ESCALATE,
          "device work escalates instead of proceeding");
    CHECK(pce_mass_decide(&cur, &prev, &th, false, LPX_PH_MANIFEST, 0, NULL, &v) == PCE_OK &&
              lpx_pol(v.tag) == LPX_P_ENTROPIC && v.act == LPX_ACT_WAIT,
          "weakening: not TRUE (NEITHER waits)");
    CHECK(lpx_base(v.tag) == LPX_B_NEITHER, "0.8 is under 0.9");
    CHECK(pce_mass_decide(NULL, &cur, &th, true, LPX_PH_MANIFEST, 0, NULL, &v) == PCE_OK &&
              lpx_pol(v.tag) == LPX_P_ENTROPIC && v.act == LPX_ACT_WAIT,
          "stale truth waits");
    CHECK(pce_mass_decide(NULL, &cur, &th, false, LPX_PH_EMERGING, 0, NULL, &v) == PCE_OK &&
              lpx_pol(v.tag) == LPX_P_NEUTRAL && lpx_phase(v.tag) == LPX_PH_EMERGING,
          "no prev: neutral, phase kept");
    CHECK(pce_mass_decide(&cur, &cur, &th, false, LPX_PH_MANIFEST, 0, NULL, &v) == PCE_OK &&
              lpx_pol(v.tag) == LPX_P_NEUTRAL,
          "same margin: neutral");

    /* a converging dispute waits (constructive), a diverging one escalates */
    pce_mass_t d0, d1;
    pce_mass_set(&d0, R(3, 10), R(3, 10), R(4, 10), R(0, 1));
    pce_mass_set(&d1, R(5, 10), R(1, 10), R(4, 10), R(0, 1));
    CHECK(pce_mass_decide(&d0, &d1, &th, false, LPX_PH_MANIFEST, 0, NULL, &v) == PCE_OK &&
              lpx_base(v.tag) == LPX_B_BOTH && v.kind == PDX_KIND_CONSTRUCTIVE &&
              v.act == LPX_ACT_WAIT,
          "converging dispute: constructive, wait");
    CHECK(pce_mass_decide(&d1, &d0, &th, false, LPX_PH_MANIFEST, 0, NULL, &v) == PCE_OK &&
              v.kind == PDX_KIND_DESTRUCTIVE && v.act == LPX_ACT_ESCALATE,
          "diverging dispute: destructive, escalate");
    CHECK(pce_mass_decide(NULL, &d0, &th, false, LPX_PH_POTENTIAL, 0, NULL, &v) == PCE_OK &&
              v.kind == PDX_KIND_SPECIAL,
          "fresh steady dispute: special (unclassified)");

    pce_verdict_t keep = v;
    CHECK(pce_mass_decide(NULL, &cur, &th, false, 4, 0, NULL, &v) == PCE_E_ARG, "bad phase");
    CHECK(pce_mass_decide(NULL, &cur, NULL, false, 0, 0, NULL, &v) == PCE_E_ARG, "no thresh");
    pce_mass_t bad = cur;
    bad.m[0] = R(1, 1);
    CHECK(pce_mass_decide(&bad, &cur, &th, false, 0, 0, NULL, &v) == PCE_E_ARG, "bad prev");
    CHECK(pce_mass_decide(NULL, &bad, &th, false, 0, 0, NULL, &v) == PCE_E_ARG, "bad cur");
    CHECK(pce_mass_decide(NULL, &cur, &th, false, 0, 0, NULL, NULL) == PCE_E_ARG, "NULL out");
    CHECK(v.tag == keep.tag && v.act == keep.act, "refusals leave out");

    /* logging: one record per decision, chain verifies, replay matches */
    pdx_rec_t st[4], st2[4];
    pdx_log_t log, log2;
    pdx_log_init(&log, st, 4);
    pdx_log_init(&log2, st2, 4);
    CHECK(pce_mass_decide(&prev, &cur, &th, false, LPX_PH_MANIFEST, 0, &log, &v) == PCE_OK &&
              log.n == 1u,
          "logged");
    CHECK(st[0].op == PCE_OP_PROB && st[0].tag == v.tag && st[0].outcome == v.act,
          "record carries op, tag, action");
    pce_mass_decide(&prev, &cur, &th, false, LPX_PH_MANIFEST, 0, &log2, &v);
    CHECK(memcmp(st[0].a, st2[0].a, 32) == 0 && memcmp(st[0].b, st2[0].b, 32) == 0,
          "replay gives the same digests");
    pce_mass_decide(&prev, &cur, &th, true, LPX_PH_MANIFEST, 0, &log2, &v);
    CHECK(memcmp(st[0].a, st2[1].a, 32) != 0, "a changed input changes the digest");
    pce_mass_decide(NULL, &cur, &th, false, LPX_PH_MANIFEST, 0, &log2, &v);
    pce_mass_decide(&cur, &cur, &th, false, LPX_PH_MANIFEST, 0, &log2, &v);
    CHECK(memcmp(st2[2].a, st2[3].a, 32) != 0, "no prev differs from prev = cur");
    CHECK(pdx_log_verify(&log2), "chain verifies");
    keep = v;
    CHECK(pce_mass_decide(&prev, &cur, &th, false, LPX_PH_MANIFEST, 0, &log2, &v) == PCE_E_LOG,
          "full log fails closed");
    CHECK(v.tag == keep.tag, "full log leaves out");
    pce_mass_decide(&prev, &cur, &th, false, LPX_PH_MANIFEST, PCE_ADV_MONEY, &log, &v);
    CHECK(memcmp(st[0].a, st[1].a, 32) != 0, "advisory bits are part of the input digest");
    pce_verdict_t vv = pce_verdict(LPX_B_TRUE, LPX_P_NEUTRAL, 0, 0);
    CHECK(pce_log(&log, PCE_OP_PROB, NULL, 0, NULL, NULL) == PCE_E_ARG, "log NULL verdict");
    CHECK(pce_log(NULL, PCE_OP_PROB, &vv, 0, NULL, NULL) == PCE_OK, "no log is fine");
}

static void test_classics(void)
{
    /* the textbook screening test: 1 in 1000, 99% sensitive, 5% false positives */
    rat_t prior[2] = {R(1, 1000), R(999, 1000)}, like[2] = {R(99, 100), R(5, 100)}, post[2];
    CHECK(pce_bayes(prior, like, 2, post) == PCE_OK, "bayes");
    CHECK(req(post[0], 99, 5094) && req(post[1], 4995, 5094), "posterior exactly 99/5094");
    rat_t zero[2] = {R(0, 1), R(0, 1)}, keep0 = post[0];
    CHECK(pce_bayes(prior, zero, 2, post) == PCE_E_CONFLICT, "impossible evidence is a conflict");
    CHECK(rat_eq(post[0], keep0), "conflict leaves post");
    rat_t badp[2] = {R(1, 2), R(1, 3)};
    CHECK(pce_bayes(badp, like, 2, post) == PCE_E_ARG, "prior must sum to 1");
    rat_t badl[2] = {R(3, 2), R(0, 1)};
    CHECK(pce_bayes(prior, badl, 2, post) == PCE_E_ARG, "likelihood above 1");
    rat_t negl[2] = {R(-1, 2), R(1, 2)};
    CHECK(pce_bayes(prior, negl, 2, post) == PCE_E_ARG, "negative likelihood");
    CHECK(pce_bayes(prior, NULL, 2, post) == PCE_E_ARG, "NULL like");
    CHECK(pce_bayes(prior, like, 2, NULL) == PCE_E_ARG, "NULL post");
    CHECK(pce_bayes(prior, like, 0, post) == PCE_E_ARG, "n = 0");
    rat_t one[1] = {R(1, 1)}, l1[1] = {R(1, 1)}, p1[1];
    CHECK(pce_bayes(one, l1, 1, p1) == PCE_OK && req(p1[0], 1, 1), "single hypothesis");

    rat_t r;
    CHECK(pce_rate(3, 1, 1, 1, &r) == PCE_OK && req(r, 2, 3), "rate (1+3)/(1+1+3+1)");
    CHECK(pce_rate(0, 0, 1, 1, &r) == PCE_OK && req(r, 1, 2), "uniform prior mean");
    CHECK(pce_rate(1, 1, 0, 0, &r) == PCE_E_ARG, "improper prior refused");
    CHECK(pce_rate(1, 1, 0, 1, &r) == PCE_OK && req(r, 1, 3), "a = 0 allowed");
    CHECK(pce_rate(1, 1, 1, 0, &r) == PCE_OK && req(r, 2, 3), "b = 0 allowed");
    CHECK(pce_rate(1, 1, 1, 1, NULL) == PCE_E_ARG, "rate NULL");
    CHECK(pce_rate(0x7FFFFFFFFFFFFFFFull, 0, 1, 0, &r) == PCE_E_OVERFLOW, "rate overflow s");
    CHECK(pce_rate(0, 0, 0x7FFFFFFFFFFFFFFFull, 1, &r) == PCE_E_OVERFLOW, "rate overflow b");
    CHECK(pce_rate(0, 1, 0x7FFFFFFFFFFFFFFFull, 0, &r) == PCE_E_OVERFLOW, "rate overflow f");
    CHECK(pce_rate(0x8000000000000000ull, 0, 1, 1, &r) == PCE_E_ARG, "rate s too big");
    CHECK(pce_rate(0, 0x8000000000000000ull, 1, 1, &r) == PCE_E_ARG, "rate f too big");
    CHECK(pce_rate(0, 0, 0x8000000000000000ull, 1, &r) == PCE_E_ARG, "rate a too big");
    CHECK(pce_rate(0, 0, 1, 0x8000000000000000ull, &r) == PCE_E_ARG, "rate b too big");

    rat_t p[3] = {R(1, 2), R(1, 4), R(1, 4)};
    int64_t v[3] = {10, -4, 7};
    CHECK(pce_expect(p, v, 3, &r) == PCE_OK && req(r, 23, 4), "expectation 5 - 1 + 7/4");
    CHECK(pce_expect(p, NULL, 3, &r) == PCE_E_ARG, "expect NULL values");
    CHECK(pce_expect(p, v, 3, NULL) == PCE_E_ARG, "expect NULL out");
    CHECK(pce_expect(p, v, 2, &r) == PCE_E_ARG, "expect: truncated p is not a distribution");
    int64_t huge[2] = {0x7FFFFFFFFFFFFFFFll, 0x7FFFFFFFFFFFFFFFll};
    rat_t half[2] = {R(1, 2), R(1, 2)};
    CHECK(pce_expect(half, huge, 2, &r) == PCE_OK && req(r, 0x7FFFFFFFFFFFFFFFll, 1),
          "expect at the edge stays exact");
    int64_t over[2] = {0x7FFFFFFFFFFFFFFFll, 0x7FFFFFFFFFFFFFFFll};
    rat_t whole[2] = {R(1, 1), R(0, 1)};
    rat_t tiny[2] = {R(2, 3), R(1, 3)};
    CHECK(pce_expect(whole, over, 2, &r) == PCE_OK, "one point mass at the edge");
    CHECK(pce_expect(tiny, over, 2, &r) == PCE_E_OVERFLOW,
          "two thirds of the edge overflows: refused, not wrapped");

    rat_t q[3] = {R(1, 4), R(1, 4), R(1, 2)};
    CHECK(pce_tv(p, q, 3, &r) == PCE_OK && req(r, 1, 4), "total variation");
    CHECK(pce_tv(p, p, 3, &r) == PCE_OK && req(r, 0, 1), "tv to itself is 0");
    CHECK(pce_tv(p, q, 3, NULL) == PCE_E_ARG, "tv NULL out");
    CHECK(pce_tv(p, badp, 2, &r) == PCE_E_ARG, "tv refuses a non-distribution");
    rat_t tv;
    CHECK(pce_witness(p, q, 3, R(1, 4), &tv) == LPX_B_TRUE && req(tv, 1, 4),
          "witness at tolerance agrees");
    CHECK(pce_witness(p, q, 3, R(1, 5), &tv) == LPX_B_BOTH, "witness beyond tolerance: BOTH");
    CHECK(pce_witness(p, q, 3, R(1, 5), NULL) == LPX_B_BOTH, "witness tv NULL ok");
    CHECK(pce_witness(p, badp, 2, R(1, 5), &tv) == LPX_B_NEITHER, "witness of a non-dist");
    CHECK(pce_witness(p, q, 3, R(-1, 5), &tv) == LPX_B_NEITHER, "negative tolerance");

    CHECK(pce_dist_check(NULL, 1) == PCE_E_ARG, "dist NULL");
    rat_t many[PCE_N_MAX + 1];
    for (uint32_t i = 0; i <= PCE_N_MAX; i++) many[i] = R(0, 1);
    many[0] = R(1, 1);
    CHECK(pce_dist_check(many, PCE_N_MAX) == PCE_OK, "dist at capacity");
    CHECK(pce_dist_check(many, PCE_N_MAX + 1) == PCE_E_ARG, "dist past capacity");
    rat_t inv[2] = {R(1, 0), R(1, 1)};
    CHECK(pce_dist_check(inv, 2) == PCE_E_ARG, "dist with an invalid entry");
}

static void test_common(void)
{
    CHECK(!pce_purpose_ok(0) && pce_purpose_ok(1) && pce_purpose_ok(7) && !pce_purpose_ok(8),
          "purpose range");
    CHECK(strcmp(pce_purpose_name(PCE_PURPOSE_LOGISTICS), "logistics") == 0, "purpose name");
    CHECK(strcmp(pce_purpose_name(PCE_PURPOSE_FINANCE), "finance") == 0, "finance name");
    CHECK(strcmp(pce_purpose_name(99), "?") == 0, "unknown purpose name");
    CHECK(strcmp(pce_purpose_name(0), "?") == 0, "zero purpose name");
    /* the verdict is lpx_action, raised only from PROCEED */
    for (uint8_t t = 0; t < 64u; t++) {
        pce_verdict_t a = pce_verdict(lpx_base(t), lpx_pol(t), lpx_phase(t), 0);
        CHECK(a.tag == t && a.act == lpx_action(t) && a.kind == pdx_kind_of_tag(t),
              "verdict = lpx_action");
        for (uint32_t adv = 1; adv < 8u; adv++) {
            pce_verdict_t b = pce_verdict(lpx_base(t), lpx_pol(t), lpx_phase(t), adv);
            lpx_action_t want = lpx_action(t) == LPX_ACT_PROCEED ? LPX_ACT_ESCALATE : lpx_action(t);
            CHECK(b.act == want, "advisory raises PROCEED only");
        }
        pce_verdict_t c = pce_verdict(lpx_base(t), lpx_pol(t), lpx_phase(t), 8u);
        CHECK(c.act == lpx_action(t), "unknown advisory bits are ignored");
    }
    CHECK(pce_trend(R(1, 2), R(3, 4), false) == LPX_P_NEGENTROPIC, "trend up");
    CHECK(pce_trend(R(3, 4), R(1, 2), false) == LPX_P_ENTROPIC, "trend down");
    CHECK(pce_trend(R(1, 2), R(1, 2), false) == LPX_P_NEUTRAL, "trend flat");
    CHECK(pce_trend(R(1, 2), R(3, 4), true) == LPX_P_ENTROPIC, "stale is entropic");
    CHECK(pce_trend(R(1, 0), R(3, 4), false) == LPX_P_ENTROPIC, "invalid m0");
    CHECK(pce_trend(R(1, 2), R(1, 0), false) == LPX_P_ENTROPIC, "invalid m1");
    CHECK(!pce_rat_min(R(1, 0), R(1, 2)).valid && !pce_rat_min(R(1, 2), R(1, 0)).valid,
          "min never hides an overflow");
    CHECK(!pce_rat_max(R(1, 0), R(1, 2)).valid && !pce_rat_max(R(1, 2), R(1, 0)).valid,
          "max never hides an overflow");
    CHECK(req(pce_rat_min(R(1, 3), R(1, 2)), 1, 3) && req(pce_rat_max(R(1, 3), R(1, 2)), 1, 2),
          "min, max");
    CHECK(pce_rat_nonneg(R(0, 1)) && !pce_rat_nonneg(R(-1, 2)) && !pce_rat_nonneg(R(1, 0)),
          "nonneg");
    pce_hash_t h;
    uint8_t d1[32], d2[32];
    pce_hash_begin(&h, 1);
    pce_hash_rat(&h, R(1, 0));
    pce_hash_end(&h, d1);
    pce_hash_begin(&h, 1);
    pce_hash_rat(&h, R(0, 1));
    pce_hash_end(&h, d2);
    CHECK(memcmp(d1, d2, 32) != 0, "invalid and zero hash apart");
    pce_hash_begin(&h, 2);
    pce_hash_rat(&h, R(0, 1));
    pce_hash_end(&h, d1);
    CHECK(memcmp(d1, d2, 32) != 0, "op is in the digest");
}

void test_pce_tac(int *pass_, int *fail_);
void test_pce_strat(int *pass_, int *fail_);
void test_pce_fin(int *pass_, int *fail_);
void test_pce_entry(int *pass_, int *fail_);
void test_pce_golden(int *pass_, int *fail_);

int main(void)
{
    test_common();
    test_mass_basics();
    test_join_algebra();
    test_zadeh();
    test_read_and_base();
    test_decide_and_log();
    test_classics();
    test_pce_tac(&pass, &fail);
    test_pce_strat(&pass, &fail);
    test_pce_fin(&pass, &fail);
    test_pce_entry(&pass, &fail);
    test_pce_golden(&pass, &fail);
    printf("%d passed, %d failed\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
