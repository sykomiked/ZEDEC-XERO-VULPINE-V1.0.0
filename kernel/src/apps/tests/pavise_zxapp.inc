/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Pavise's languages, witnessed, row by row against pavise_rules.h, the C
 * the model runs; the parts it composes (gatehouse, lockbox, witness,
 * hedgerow, tallystick) resolved and called; and the BOTH of two policies.
 * Slots: @logic.pavise_perimeter_allow, @logic.pavise_access_ledger_ok,
 * @num.pavise_perimeter_exposure, @num.pavise_perm_app_total,
 * @logic.pavise_policy_rule_ok, @logic.pavise_trust_node_ok; and, used,
 * @logic.gatehouse_use_ok, @num.gatehouse_holders, @num.lockbox_days_left,
 * @logic.lockbox_secret_ok, @logic.witness_chain_ok, @logic.witness_audit_ok,
 * @logic.hedgerow_path_open, @num.hedgerow_addr_scope,
 * @logic.hedgerow_reach_rule_ok, @num.tallystick_next_doc_number. */
#include "pavise_rules.h"

APP_TEST(pavise, 36, 153)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];

    /* compose: five parts, every part entry it calls resolves to its part */
    static const char *const used[10][2] = {
        {"gatehouse.use_ok", "gatehouse"},      {"gatehouse.holders", "gatehouse"},
        {"lockbox.days_left", "lockbox"},       {"lockbox.secret_ok", "lockbox"},
        {"witness.chain_ok", "witness"},        {"witness.audit_ok", "witness"},
        {"hedgerow.path_open", "hedgerow"},     {"hedgerow.addr_scope", "hedgerow"},
        {"hedgerow.reach_rule_ok", "hedgerow"}, {"tallystick.next_doc_number", "tallystick"}};
    CHECK(app->resolved && app->nuse == 5 && app->composed == 7 && app->n == 36,
          "Pavise: composed of 5 parts, 7 own lines and 29 of theirs (%u, %u)", app->composed,
          app->n);
    for (uint32_t i = 0; i < 10u; i++) {
        const zxapp_logic_t *L = zxapp_find(app, used[i][0]);
        CHECK(L && L->origin && !strcmp(L->origin->id, used[i][1]) && L->loaded &&
                  zxapp_contract(app, used[i][0]),
              "Pavise: %s resolves to %s and loads", used[i][0], used[i][1]);
    }
    zxapp_origin_t org[8];
    CHECK(zxapp_provenance(app, org, 8) == 6 && !org[0].from, "Pavise: itself and five parts");

    /* PR1: perimeter_allow, Eve = KRON = C; flags 0..1, days 0..max */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 3u ? 1 : wrand(0, 1);
        b[i] = i % 4u ? 1 : wrand(0, 1);
        c[i] = i % 5u ? 1 : wrand(0, 1);
        d[i] = i % 2u ? wrand(0, 60) : wrand(0, PAVISE_KEYDAYS_MAX);
        e[i] = i % 6u ? 0 : wrand(0, 1);
        if (i == 0) a[i] = b[i] = c[i] = 1, d[i] = 1, e[i] = 0;                  /* a day left */
        if (i == 1) a[i] = b[i] = c[i] = 1, d[i] = PAVISE_KEYDAYS_MAX, e[i] = 0; /* max */
        if (i == 2) a[i] = b[i] = c[i] = 1, d[i] = 0, e[i] = 0;                  /* expired */
        if (i == 3) a[i] = b[i] = c[i] = 1, d[i] = 30, e[i] = 1;                 /* denied */
        want[i] = T_(pavise_allow(a[i], b[i], c[i], d[i], e[i]));
    }
    zxapp_arg_t al[5] = {{"gate", a, WAVE_ROWS},
                         {"reach", b, WAVE_ROWS},
                         {"keyok", c, WAVE_ROWS},
                         {"keydays", d, WAVE_ROWS},
                         {"deny", e, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE &&
              wave_same(app, "perimeter_allow", al, 5, want, WAVE_ROWS, true),
          "Pavise: perimeter_allow, Eve = KRON = C");
    {
        /* a flag that is no flag, days past year 9999: Eve no, KRON yes */
        static const int64_t edge[5][5] = {{2, 1, 1, 30, 0},
                                           {1, 2, 1, 30, 0},
                                           {1, 1, 7, 30, 0},
                                           {1, 1, 1, PAVISE_KEYDAYS_MAX + 1, 0},
                                           {1, 1, 1, 30, -1}};
        static const int64_t good[5] = {1, 1, 1, 30, 0};
        for (uint32_t k = 0; k < 5u; k++) {
            int64_t x[5][2]; /* KRON needs two rows: a good one, then the edge */
            for (uint32_t f = 0; f < 5u; f++) x[f][0] = good[f], x[f][1] = edge[k][f];
            zxapp_arg_t xa[5] = {{"gate", x[0], 2},
                                 {"reach", x[1], 2},
                                 {"keyok", x[2], 2},
                                 {"keydays", x[3], 2},
                                 {"deny", x[4], 2}};
            CHECK(!pavise_allow(x[0][1], x[1][1], x[2][1], x[3][1], x[4][1]) &&
                      wave_lp(app, "perimeter_allow", xa, 5) == ZXAPP_BOTH,
                  "Pavise: perimeter_allow edge %u: Eve refuses the record, KRON answers: BOTH", k);
        }
    }

    /* PR3: access_ledger_ok, Sutra = C, inside the pictures */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, PAVISE_DECISION_MAX);
        b[i] = wrand(0, PAVISE_APP_MAX);
        c[i] = wrand(0, 21);
        d[i] = wrand(0, 5);
        if (i == 0) a[i] = 1, b[i] = 1, c[i] = 0, d[i] = 0; /* the least */
        if (i == 1) a[i] = PAVISE_DECISION_MAX, b[i] = PAVISE_APP_MAX, c[i] = 18, d[i] = 3;
        if (i == 2) a[i] = 0, b[i] = 12, c[i] = 0, d[i] = 1;  /* no decision 0 */
        if (i == 3) a[i] = 5, b[i] = 12, c[i] = 19, d[i] = 1; /* no capability 19 */
        want[i] = T_(pavise_ledger_ok(a[i], b[i], c[i], d[i]));
    }
    zxapp_arg_t la[4] = {{"decision", a, WAVE_ROWS},
                         {"app", b, WAVE_ROWS},
                         {"cap", c, WAVE_ROWS},
                         {"verdict", d, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE &&
              wave_same(app, "access_ledger_ok", la, 4, want, WAVE_ROWS, true),
          "Pavise: access_ledger_ok, Sutra = C");
    {
        /* past a picture, or negative: Sutra refuses, no answer */
        static const int64_t bad[4][4] = {
            {1000000000, 1, 0, 0}, {1, 10000, 0, 0}, {1, 1, 100, 0}, {-1, 1, 0, 0}};
        for (uint32_t i = 0; i < 4u; i++) {
            zxapp_arg_t ba[4] = {{"decision", &bad[i][0], 1},
                                 {"app", &bad[i][1], 1},
                                 {"cap", &bad[i][2], 1},
                                 {"verdict", &bad[i][3], 1}};
            CHECK(!pavise_ledger_ok(bad[i][0], bad[i][1], bad[i][2], bad[i][3]) &&
                      wave_lp(app, "access_ledger_ok", ba, 4) == ZXAPP_NEITHER,
                  "Pavise: access_ledger_ok outside the pictures (%u): refused", i);
        }
    }

    /* PR4: perimeter_exposure, one score a call, Trebago = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        uint32_t np = (uint32_t) wrand(1, 8), nk = (uint32_t) wrand(1, 8),
                 na = (uint32_t) wrand(1, 8);
        for (uint32_t k = 0; k < 8u; k++) {
            a[k] = wrand(0, 1);
            b[k] = k % 2u ? wrand(0, 60) : wrand(0, PAVISE_KEYDAYS_MAX);
            c[k] = wrand(0, 1);
        }
        if (i == 0) np = nk = na = 1, a[0] = 0, b[0] = PAVISE_KEYDAYS_MAX, c[0] = 1; /* zero */
        if (i == 1) np = nk = na = 1, a[0] = 1, b[0] = 0, c[0] = 0;                  /* all */
        if (i == 2) np = nk = na = 2, b[0] = 30, b[1] = 31; /* the 30-day edge */
        int64_t w = pavise_exposure(a, np, b, nk, c, na);
        CHECK(w >= 0, "Pavise: PR4 row %u answers", i);
        if (i == 0) CHECK(w == 0, "Pavise: nothing exposed scores 0");
        if (i == 1) CHECK(w == 10, "Pavise: one of each scores 2 + 3 + 5");
        zxapp_arg_t ea[3] = {{"paths", a, np}, {"keydays", b, nk}, {"signed", c, na}};
        CHECK(wave_same(app, "perimeter_exposure", ea, 3, &w, 1, false),
              "Pavise: perimeter_exposure row %u, Trebago = C (%lld)", i, (long long) w);
    }
    {
        static const int64_t ok1[2] = {0, 1}, two[2] = {0, 2}, far[2] = {5, PAVISE_KEYDAYS_MAX + 1},
                             near[2] = {5, 40}, neg[2] = {1, -1};
        zxapp_arg_t x1[3] = {{"paths", two, 2}, {"keydays", near, 2}, {"signed", ok1, 2}};
        zxapp_arg_t x2[3] = {{"paths", ok1, 2}, {"keydays", far, 2}, {"signed", ok1, 2}};
        zxapp_arg_t x3[3] = {{"paths", ok1, 2}, {"keydays", near, 2}, {"signed", neg, 2}};
        CHECK(pavise_exposure(two, 2, near, 2, ok1, 2) == PAVISE_REFUSED &&
                  pavise_exposure(ok1, 2, far, 2, ok1, 2) == PAVISE_REFUSED &&
                  pavise_exposure(ok1, 2, near, 2, neg, 2) == PAVISE_REFUSED &&
                  wave_lp(app, "perimeter_exposure", x1, 3) == ZXAPP_NEITHER &&
                  wave_lp(app, "perimeter_exposure", x2, 3) == ZXAPP_NEITHER &&
                  wave_lp(app, "perimeter_exposure", x3, 3) == ZXAPP_NEITHER,
              "Pavise: a path flag of 2, days past year 9999, a signed flag of -1: refused");
    }

    /* PR5: perm_app_total, KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(1, 6), b[i] = wrand(0, 1);
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = pavise_app_total(a, b, WAVE_ROWS, i);
    zxapp_arg_t ma[2] = {{"app", a, WAVE_ROWS}, {"allow", b, WAVE_ROWS}};
    CHECK(wave_same(app, "perm_app_total", ma, 2, want, WAVE_ROWS, false),
          "Pavise: perm_app_total, KRON = C");

    /* PR6: policy_rule_ok, Eve = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1, 101);
        b[i] = wrand(-1, 10001);
        c[i] = wrand(-1, 20);
        d[i] = wrand(-1, 2);
        if (i == 0) a[i] = 1, b[i] = 1, c[i] = 0, d[i] = 0;
        if (i == 1) a[i] = PAVISE_SOURCE_MAX, b[i] = PAVISE_APP_MAX, c[i] = 18, d[i] = 1;
        if (i == 2) a[i] = 100, b[i] = 1, c[i] = 0, d[i] = 0;
        want[i] = T_(pavise_policy_ok(a[i], b[i], c[i], d[i]));
    }
    zxapp_arg_t pa[4] = {{"source", a, WAVE_ROWS},
                         {"app", b, WAVE_ROWS},
                         {"cap", c, WAVE_ROWS},
                         {"effect", d, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              wave_same(app, "policy_rule_ok", pa, 4, want, WAVE_ROWS, true),
          "Pavise: policy_rule_ok, Eve = C");

    /* PR7: trust_node_ok, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-12000, 12000);
        b[i] = wrand(-12000, 12000);
        c[i] = wrand(-12000, 12000);
        d[i] = wrand(0, 12);
        e[i] = wrand(0, INT64_C(120000000000));
        if (i == 0) a[i] = b[i] = c[i] = 9999, d[i] = 9, e[i] = INT64_C(99999999999); /* max */
        if (i == 1) a[i] = 10000, b[i] = 1, c[i] = 1, d[i] = 1, e[i] = 1;             /* past */
        want[i] = T_(pavise_node_ok(a[i], b[i], c[i], d[i], e[i]));
    }
    zxapp_arg_t na[5] = {{"device", a, WAVE_ROWS},
                         {"app", b, WAVE_ROWS},
                         {"key", c, WAVE_ROWS},
                         {"trust", d, WAVE_ROWS},
                         {"since", e, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE &&
              wave_same(app, "trust_node_ok", na, 5, want, WAVE_ROWS, true),
          "Pavise: trust_node_ok, MYRA = C");

    /* the parts, called through Pavise as it exports them */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1), b[i] = wrand(0, 1), c[i] = wrand(0, 1);
        want[i] = T_(gatehouse_use_ok(a[i], b[i], c[i]));
    }
    zxapp_arg_t ga[3] = {
        {"asked", a, WAVE_ROWS}, {"granted", b, WAVE_ROWS}, {"revoked", c, WAVE_ROWS}};
    CHECK(wave_same(app, "gatehouse.use_ok", ga, 3, want, WAVE_ROWS, true),
          "Pavise: gatehouse.use_ok, is this use allowed");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(0, 1);
    want[0] = gatehouse_holders(a, WAVE_ROWS);
    zxapp_arg_t ha[1] = {{"held", a, WAVE_ROWS}};
    CHECK(wave_same(app, "gatehouse.holders", ha, 1, want, 1, false),
          "Pavise: gatehouse.holders, apps per capability");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        b[i] = wrand(0, INT64_C(2000000000));
        a[i] = b[i] + wrand(0, 40) * PAVISE_SOON * 86400 / 30 + wrand(0, 86399);
        want[i] = lockbox_days_left(a[i], b[i]);
    }
    zxapp_arg_t da[2] = {{"expires", a, WAVE_ROWS}, {"now", b, WAVE_ROWS}};
    CHECK(wave_same(app, "lockbox.days_left", da, 2, want, WAVE_ROWS, false),
          "Pavise: lockbox.days_left, a key's days left");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1100), b[i] = wrand(0, 5), c[i] = wrand(0, 1100);
        want[i] = T_(lockbox_secret_ok(a[i], b[i], c[i]));
    }
    zxapp_arg_t sa[3] = {
        {"length", a, WAVE_ROWS}, {"classes", b, WAVE_ROWS}, {"repeats", c, WAVE_ROWS}};
    CHECK(wave_same(app, "lockbox.secret_ok", sa, 3, want, WAVE_ROWS, true),
          "Pavise: lockbox.secret_ok, a key phrase's limits");
    {
        /* the access ledger's chain: numbers from tallystick.next_doc_number */
        int64_t last[6], num[6], prev[6], w[6];
        for (uint32_t i = 0; i < 6u; i++) {
            last[i] = i ? num[i - 1u] : 0;
            num[i] = tstk_next_number(last[i]);
            prev[i] = last[i];
            w[i] = num[i];
        }
        zxapp_arg_t ta[1] = {{"last", last, 6}};
        CHECK(w[5] == 6 && wave_same(app, "tallystick.next_doc_number", ta, 1, w, 6, false),
              "Pavise: tallystick.next_doc_number numbers the ledger 1..6");
        prev[3] = 1; /* a reused number breaks the chain */
        for (uint32_t i = 0; i < 6u; i++) w[i] = T_(witness_linked(num[i], prev[i], last[i]));
        zxapp_arg_t ca[3] = {{"seq", num, 6}, {"prev", prev, 6}, {"last", last, 6}};
        CHECK(w[2] == ZXAPP_TRUE && w[3] == ZXAPP_FALSE &&
                  wave_same(app, "witness.chain_ok", ca, 3, w, 6, true),
              "Pavise: witness.chain_ok over the ledger, KRON = Sutra");
        static const int64_t seq[2] = {5, WITNESS_SEQ_MAX + 1}, pv[2] = {4, 0}, kind[2] = {1, 1},
                             stamp[2] = {INT64_C(1700000000000), 0};
        int64_t aw[2] = {T_(witness_record_fits(5, 4, 1, INT64_C(1700000000000))),
                         T_(witness_record_fits(WITNESS_SEQ_MAX + 1, 0, 1, 0))};
        zxapp_arg_t aa[4] = {
            {"seq", seq, 2}, {"prev", pv, 2}, {"kind", kind, 2}, {"stamp", stamp, 2}};
        CHECK(aw[0] == ZXAPP_TRUE && aw[1] == ZXAPP_FALSE &&
                  wave_same(app, "witness.audit_ok", aa, 4, aw, 2, true),
              "Pavise: witness.audit_ok, a journal entry");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t mode = wrand(0, 2), bind = wrand(0, 2), far = wrand(0, 2);
        a[i] = wrand(0, 1);
        b[i] = mode - far;
        c[i] = bind - far;
        want[i] = T_(hedgerow_reach(a[i], mode, bind, far));
    }
    zxapp_arg_t ra[3] = {
        {"sw", a, WAVE_ROWS}, {"moderoom", b, WAVE_ROWS}, {"bindroom", c, WAVE_ROWS}};
    CHECK(wave_same(app, "hedgerow.path_open", ra, 3, want, WAVE_ROWS, true),
          "Pavise: hedgerow.path_open, is this path reachable");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        e[i] = i % 2u ? 4 : 6;
        a[i] = wrand(0, 255), b[i] = wrand(0, 255), c[i] = wrand(0, 255), d[i] = wrand(0, 255);
        if (i == 0) a[i] = 192, b[i] = 168, c[i] = 1, d[i] = 2;
        if (i == 1) a[i] = 127, b[i] = 0, c[i] = 0, d[i] = 1;
        want[i] = hedgerow_addr_scope(e[i], a[i], b[i], c[i], d[i]);
    }
    zxapp_arg_t sc[5] = {{"fam", e, WAVE_ROWS},
                         {"a", a, WAVE_ROWS},
                         {"b", b, WAVE_ROWS},
                         {"c", c, WAVE_ROWS},
                         {"d", d, WAVE_ROWS}};
    CHECK(want[0] == HEDGEROW_SCOPE_PUBLIC && want[1] == HEDGEROW_SCOPE_LOOPBACK &&
              wave_same(app, "hedgerow.addr_scope", sc, 5, want, WAVE_ROWS, false),
          "Pavise: hedgerow.addr_scope, a peer's address class");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 999999999);
        b[i] = wrand(1, 6);
        c[i] = wrand(0, 1);
        d[i] = wrand(0, 65535);
        e[i] = wrand(0, INT64_C(99999999999));
        want[i] = T_(hedgerow_rule_ok(a[i], b[i], c[i], d[i], e[i]));
    }
    zxapp_arg_t qa[5] = {{"seq", a, WAVE_ROWS},
                         {"path", b, WAVE_ROWS},
                         {"open", c, WAVE_ROWS},
                         {"port", d, WAVE_ROWS},
                         {"stamp", e, WAVE_ROWS}};
    CHECK(wave_same(app, "hedgerow.reach_rule_ok", qa, 5, want, WAVE_ROWS, true),
          "Pavise: hedgerow.reach_rule_ok, a filed opening");

    /* PR2, the BOTH test: policy A allows app 12 the network, policy B
     * denies it: the clause is BOTH, and with every other part holding the
     * access is BOTH, never TRUE, until the owner rules */
    lpres_state_t cl = pavise_clause(1, 2, 0);
    CHECK(cl == LPRES_STATE_BOTH && (int) cl == 3, "Pavise: A allows, B denies: BOTH");
    lpres_state_t v = pavise_decision(1, 1, 1, 200, cl);
    CHECK(v == LPRES_STATE_BOTH && v != LPRES_STATE_TRUE, "Pavise: the access is BOTH: denied");
    CHECK(pavise_decision(1, 1, 1, 200, pavise_clause(1, 2, 1)) == LPRES_STATE_TRUE &&
              pavise_decision(1, 1, 1, 200, pavise_clause(1, 2, 2)) == LPRES_STATE_FALSE &&
              pavise_decision(1, 1, 1, 200, pavise_clause(0, 0, 0)) == LPRES_STATE_TRUE &&
              pavise_decision(1, 1, 1, 200, pavise_clause(1, 1, 0)) == LPRES_STATE_TRUE &&
              pavise_decision(1, 1, 1, 200, pavise_clause(2, 0, 0)) == LPRES_STATE_FALSE &&
              pavise_decision(0, 1, 1, 200, cl) == LPRES_STATE_FALSE &&
              lpres_conjoin(v, LPRES_STATE_NEITHER) == LPRES_STATE_NEITHER,
          "Pavise: only the owner's ruling settles it; no policy is no denial; a closed gate "
          "is FALSE");
}
