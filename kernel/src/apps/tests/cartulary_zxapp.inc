/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Cartulary's languages, witnessed, row by row against cartulary_rules.h,
 * the C the model runs; the parts it composes (banker, scent, keepsake,
 * witness) resolved and called; and the BOTH of two sources. Slots:
 * @num.cartulary_retention_left, @logic.cartulary_version_may_go,
 * @logic.cartulary_retention_ok, @num.cartulary_bytes_projected,
 * @num.cartulary_class_year_cell, @logic.cartulary_record_node_ok; and,
 * used, @num.banker_total, @logic.banker_entry_ok, @num.scent_score,
 * @num.keepsake_removed, @logic.keepsake_keep_ok, @logic.witness_chain_ok,
 * @logic.witness_audit_ok. */
#include "cartulary_rules.h"

APP_TEST(cartulary, 23, 145)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];

    /* compose: four parts, every part entry it calls resolves to its part */
    static const char *const used[7][2] = {
        {"banker.total", "banker"},       {"banker.entry_ok", "banker"},
        {"scent.score", "scent"},         {"keepsake.removed", "keepsake"},
        {"keepsake.keep_ok", "keepsake"}, {"witness.chain_ok", "witness"},
        {"witness.audit_ok", "witness"}};
    CHECK(app->resolved && app->nuse == 4 && app->composed == 9 && app->n == 23,
          "Cartulary: composed of 4 parts, 9 own lines and 14 of theirs (%u, %u)", app->composed,
          app->n);
    for (uint32_t i = 0; i < 7u; i++) {
        const zxapp_logic_t *L = zxapp_find(app, used[i][0]);
        CHECK(L && L->origin && !strcmp(L->origin->id, used[i][1]) && L->loaded &&
                  zxapp_contract(app, used[i][0]),
              "Cartulary: %s resolves to %s and loads", used[i][0], used[i][1]);
    }
    zxapp_origin_t org[8];
    CHECK(zxapp_provenance(app, org, 8) == 5 && !org[0].from, "Cartulary: itself and four parts");

    /* CR2: retention_left, one row a call (ALGOL reads one), Sutra = KRON = ALGOL = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t fi = wrand(0, CARTULARY_DAY_MAX), ye = wrand(0, CARTULARY_YEARS_MAX),
                mo = wrand(0, CARTULARY_MONTHS_MAX), to = wrand(0, CARTULARY_DAY_MAX), w = 0;
        if (i == 0) fi = 0, ye = 0, mo = 0, to = 0; /* zero */
        if (i == 1)                                 /* max */
            fi = CARTULARY_DAY_MAX, ye = CARTULARY_YEARS_MAX, mo = CARTULARY_MONTHS_MAX, to = 0;
        if (i == 2) fi = 0, ye = 0, mo = 0, to = CARTULARY_DAY_MAX; /* least */
        if (i == 3) fi = 20000, ye = 100, mo = 0, to = 20000;       /* 100 years: 36525 days */
        CHECK(cartulary_left(fi, ye, mo, to, &w), "Cartulary: CR2 row %u answers", i);
        zxapp_arg_t ra[4] = {
            {"filed", &fi, 1}, {"years", &ye, 1}, {"months", &mo, 1}, {"today", &to, 1}};
        CHECK(wave_same(app, "retention_left", ra, 4, &w, 1, false),
              "Cartulary: retention_left row %u, Sutra = KRON = ALGOL = C", i);
    }
    {
        /* past Sutra's pictures, or negative: Sutra and ALGOL refuse, KRON answers */
        static const int64_t bad[5][4] = {
            {1000000, 1, 0, 0}, {-1, 1, 0, 0}, {0, 1000, 0, 0}, {0, 0, 100, 0}, {0, 1, 0, 1000000}};
        for (uint32_t i = 0; i < 5u; i++) {
            int64_t fi = bad[i][0], ye = bad[i][1], mo = bad[i][2], to = bad[i][3], w = 0;
            zxapp_arg_t ra[4] = {
                {"filed", &fi, 1}, {"years", &ye, 1}, {"months", &mo, 1}, {"today", &to, 1}};
            CHECK(!cartulary_left(fi, ye, mo, to, &w) &&
                      wave_lp(app, "retention_left", ra, 4) == ZXAPP_BOTH,
                  "Cartulary: retention_left outside the pictures (%u): BOTH", i);
        }
    }

    /* CR3: version_may_go, Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2u ? wrand(-60, 60) : wrand(CARTULARY_LEFT_MIN, 9999999);
        b[i] = wrand(0, 2);
        c[i] = wrand(0, 2);
        if (i == 0) a[i] = 0, b[i] = 0, c[i] = 1;                  /* due today */
        if (i == 1) a[i] = CARTULARY_LEFT_MIN, b[i] = 0, c[i] = 1; /* the least left */
        if (i == 2) a[i] = 1, b[i] = 0, c[i] = 1;                  /* a day to go */
        if (i == 3) a[i] = -5, b[i] = 1, c[i] = 1;                 /* a hold wins */
        if (i == 4) a[i] = -5, b[i] = 0, c[i] = 0;                 /* the last one stays */
        want[i] = T_(cartulary_may_go(a[i], b[i], c[i]));
    }
    zxapp_arg_t ga[3] = {{"left", a, WAVE_ROWS}, {"hold", b, WAVE_ROWS}, {"keepok", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE && want[3] == ZXAPP_FALSE &&
              wave_same(app, "version_may_go", ga, 3, want, WAVE_ROWS, true),
          "Cartulary: version_may_go, Eve = KRON = C");
    {
        static const int64_t left[2] = {CARTULARY_LEFT_MIN - 1, -1}, zero[2] = {0, 0},
                             one[2] = {1, 1};
        zxapp_arg_t xa[3] = {{"left", left, 2}, {"hold", zero, 2}, {"keepok", one, 2}};
        CHECK(!cartulary_may_go(left[0], 0, 1) &&
                  wave_lp(app, "version_may_go", xa, 3) == ZXAPP_BOTH,
              "Cartulary: a left below s9(7): Eve refuses, KRON answers: BOTH");
    }

    /* CR4: retention_ok, Eve = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-10, CARTULARY_KEEP_MAX + 10);
        b[i] = wrand(-1, 2);
        if (i < 4u)
            a[i] = (int64_t[]){0, 1, CARTULARY_KEEP_MAX, CARTULARY_KEEP_MAX + 1}[i], b[i] = 1;
        want[i] = T_(cartulary_retention_ok(a[i], b[i]));
    }
    zxapp_arg_t la[2] = {{"days", a, WAVE_ROWS}, {"hold", b, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_FALSE && want[1] == ZXAPP_TRUE && want[3] == ZXAPP_FALSE &&
              wave_same(app, "retention_ok", la, 2, want, WAVE_ROWS, true),
          "Cartulary: retention_ok, Eve = C");

    /* CR5: bytes_projected, one projection a call, Trebago = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        uint32_t n = (uint32_t) wrand(2, 12);
        int64_t ahead = wrand(0, CARTULARY_AHEAD_MAX), w = -1;
        for (uint32_t k = 0; k < n; k++) a[k] = wrand(0, CARTULARY_BYTES_MAX);
        if (i == 0) n = 2, a[0] = 0, a[1] = 0, ahead = 0; /* zero */
        if (i == 1)
            n = 2, a[0] = 0, a[1] = CARTULARY_BYTES_MAX, ahead = CARTULARY_AHEAD_MAX; /* max */
        if (i == 2) n = 3, a[0] = 1000, a[1] = 999, a[2] = 1001, ahead = 1;           /* floored */
        if (i == 3) n = 2, a[0] = CARTULARY_BYTES_MAX, a[1] = 0, ahead = 7;   /* never below 0 */
        if (i == 4) n = 4, a[0] = 9, a[1] = 0, a[2] = 0, a[3] = 0, ahead = 1; /* -3: floor */
        CHECK(cartulary_projected(a, n, ahead, &w), "Cartulary: CR5 row %u answers", i);
        zxapp_arg_t pa[2] = {{"bytes", a, n}, {"ahead", &ahead, 1}};
        CHECK(wave_same(app, "bytes_projected", pa, 2, &w, 1, false),
              "Cartulary: bytes_projected row %u, Trebago = C (%lld)", i, (long long) w);
    }
    {
        int64_t one[1] = {5}, big[2] = {0, CARTULARY_BYTES_MAX + 1}, ok2[2] = {1, 2}, ahead = 1,
                far = CARTULARY_AHEAD_MAX + 1, w = 0;
        zxapp_arg_t p1[2] = {{"bytes", one, 1}, {"ahead", &ahead, 1}};
        zxapp_arg_t p2[2] = {{"bytes", big, 2}, {"ahead", &ahead, 1}};
        zxapp_arg_t p3[2] = {{"bytes", ok2, 2}, {"ahead", &far, 1}};
        CHECK(!cartulary_projected(one, 1, ahead, &w) && !cartulary_projected(big, 2, ahead, &w) &&
                  !cartulary_projected(ok2, 2, far, &w) &&
                  wave_lp(app, "bytes_projected", p1, 2) == ZXAPP_NEITHER &&
                  wave_lp(app, "bytes_projected", p2, 2) == ZXAPP_NEITHER &&
                  wave_lp(app, "bytes_projected", p3, 2) == ZXAPP_NEITHER,
              "Cartulary: one month, a month past 10^12 bytes, 1201 months: refused");
    }

    /* CR6: class_year_cell, KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        a[i] = wrand(1, CARTULARY_CLASSES), b[i] = wrand(2020, 2024);
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = cartulary_cell(a, b, WAVE_ROWS, i);
    zxapp_arg_t ca[2] = {{"cls", a, WAVE_ROWS}, {"year", b, WAVE_ROWS}};
    CHECK(wave_same(app, "class_year_cell", ca, 2, want, WAVE_ROWS, false),
          "Cartulary: class_year_cell, KRON = C");

    /* CR7: record_node_ok, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-12000, 12000);
        b[i] = wrand(-1200000000, 1200000000);
        c[i] = wrand(0, 12000);
        d[i] = wrand(0, 1200000);
        e[i] = wrand(0, 12);
        if (i == 0) a[i] = 9999, b[i] = 999999999, c[i] = 9999, d[i] = 999999, e[i] = 9; /* max */
        if (i == 1) a[i] = 10000, b[i] = 1, c[i] = 1, d[i] = 1, e[i] = 0;                /* past */
        want[i] = T_(cartulary_node_ok(a[i], b[i], c[i], d[i], e[i]));
    }
    zxapp_arg_t na[5] = {{"cls", a, WAVE_ROWS},
                         {"record", b, WAVE_ROWS},
                         {"version", c, WAVE_ROWS},
                         {"filed", d, WAVE_ROWS},
                         {"hold", e, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE &&
              wave_same(app, "record_node_ok", na, 5, want, WAVE_ROWS, true),
          "Cartulary: record_node_ok, MYRA = C");

    /* the parts, called through Cartulary as it exports them */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(0, 1000000000);
    CHECK(cartulary_total(a, WAVE_ROWS, &want[0]), "Cartulary: a class's bytes add up");
    zxapp_arg_t ta[1] = {{"size", a, WAVE_ROWS}};
    CHECK(wave_same(app, "banker.total", ta, 1, want, 1, false),
          "Cartulary: banker.total, a class's bytes");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, CARTULARY_CLASSES);
        b[i] = wrand(1, 999999999);
        c[i] = i % 3u ? wrand(0, INT64_C(999999999999)) : INT64_C(999999999999999999) + (i % 2u);
        d[i] = wrand(0, CARTULARY_DAY_MAX);
        want[i] = T_(cartulary_entry_ok(a[i], b[i], c[i], d[i]));
    }
    zxapp_arg_t ea[4] = {{"folder", a, WAVE_ROWS},
                         {"seq", b, WAVE_ROWS},
                         {"size", c, WAVE_ROWS},
                         {"mtime", d, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[3] == ZXAPP_FALSE &&
              wave_same(app, "banker.entry_ok", ea, 4, want, WAVE_ROWS, true),
          "Cartulary: banker.entry_ok, each index line");
    {
        int64_t s = 0;
        for (uint32_t i = 0; i < 8u; i++) {
            a[i] = wrand(0, 1), b[i] = wrand(0, 1), c[i] = wrand(0, 300);
            s += 1000 * a[i] + 100 * b[i] * (1 - a[i]) + (c[i] < 99 ? c[i] : 99);
        }
        zxapp_arg_t sa[3] = {{"name", a, 8}, {"folder", b, 8}, {"count", c, 8}};
        CHECK(wave_same(app, "scent.score", sa, 3, &s, 1, false),
              "Cartulary: scent.score ranks a hit, KRON = Trebago");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 999999999);
        b[i] = wrand(1, 999999999);
        want[i] = cartulary_removed(a[i], b[i]);
    }
    zxapp_arg_t ka[2] = {{"snapshots", a, WAVE_ROWS}, {"keep", b, WAVE_ROWS}};
    CHECK(wave_same(app, "keepsake.removed", ka, 2, want, WAVE_ROWS, false),
          "Cartulary: keepsake.removed, Sutra = KRON");
    {
        static const int64_t many[2] = {1000000000, 5}, keep[2] = {1, 1};
        zxapp_arg_t xa[2] = {{"snapshots", many, 2}, {"keep", keep, 2}};
        CHECK(wave_lp(app, "keepsake.removed", xa, 2) == ZXAPP_BOTH,
              "Cartulary: keepsake.removed past Sutra's 9(9): BOTH");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i < 3u ? (int64_t[]){0, 1, CARTULARY_KEPT_MAX}[i] : wrand(0, 1000);
        want[i] = T_(cartulary_keep_ok(a[i]));
    }
    zxapp_arg_t ko[1] = {{"keep", a, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_FALSE &&
              wave_same(app, "keepsake.keep_ok", ko, 1, want, WAVE_ROWS, true),
          "Cartulary: keepsake.keep_ok, never all");
    {
        /* a record's history: each version's seq linked to the one before */
        static const int64_t seq[4] = {5, 9, 14, 14}, prev[4] = {0, 5, 9, 9},
                             last[4] = {0, 5, 9, 14};
        int64_t w[4];
        for (uint32_t i = 0; i < 4u; i++) w[i] = T_(witness_linked(seq[i], prev[i], last[i]));
        zxapp_arg_t wa[3] = {{"seq", seq, 4}, {"prev", prev, 4}, {"last", last, 4}};
        CHECK(w[3] == ZXAPP_FALSE && wave_same(app, "witness.chain_ok", wa, 3, w, 4, true),
              "Cartulary: witness.chain_ok over a record's history, KRON = Sutra");
        static const int64_t neg[2] = {2, -1}, one[2] = {1, 1};
        zxapp_arg_t na2[3] = {{"seq", neg, 2}, {"prev", one, 2}, {"last", one, 2}};
        CHECK(wave_lp(app, "witness.chain_ok", na2, 3) == ZXAPP_BOTH,
              "Cartulary: witness.chain_ok, a negative seq: BOTH");
        static const int64_t aseq[2] = {5, WITNESS_SEQ_MAX + 1}, aprev[2] = {0, 0},
                             kind[2] = {1, 1}, stamp[2] = {INT64_C(1728000000000), 0};
        int64_t aw[2] = {T_(witness_record_fits(5, 0, 1, INT64_C(1728000000000))),
                         T_(witness_record_fits(WITNESS_SEQ_MAX + 1, 0, 1, 0))};
        zxapp_arg_t aa[4] = {
            {"seq", aseq, 2}, {"prev", aprev, 2}, {"kind", kind, 2}, {"stamp", stamp, 2}};
        CHECK(aw[0] == ZXAPP_TRUE && aw[1] == ZXAPP_FALSE &&
                  wave_same(app, "witness.audit_ok", aa, 4, aw, 2, true),
              "Cartulary: witness.audit_ok, a filing's journal entry");
    }

    /* CR8, the BOTH test: record 7 version 3 from two sources, content 11
     * and 12, no ruling: BOTH, so the prune truth, schedule over and no
     * hold and a newer version kept, still refuses; a ruling makes it TRUE */
    lpres_state_t st = cartulary_version_state(11, 12, 0);
    CHECK(st == LPRES_STATE_BOTH && st == 3, "Cartulary: two contents, no ruling: BOTH");
    CHECK(lpres_conjoin(st, cartulary_may_go(-1, 0, 1) ? LPRES_STATE_TRUE : LPRES_STATE_FALSE) ==
              LPRES_STATE_BOTH,
          "Cartulary: BOTH and a prune truth: refused");
    CHECK(cartulary_version_state(11, 12, 1) == LPRES_STATE_TRUE &&
              cartulary_version_state(11, 12, 2) == LPRES_STATE_TRUE &&
              cartulary_version_state(11, 12, 3) == LPRES_STATE_BOTH &&
              cartulary_version_state(11, 11, 0) == LPRES_STATE_TRUE &&
              cartulary_version_state(11, 0, 0) == LPRES_STATE_TRUE &&
              cartulary_version_state(0, 0, 0) == LPRES_STATE_NEITHER,
          "Cartulary: only a ruling of 1 or 2 makes one TRUE");
}
