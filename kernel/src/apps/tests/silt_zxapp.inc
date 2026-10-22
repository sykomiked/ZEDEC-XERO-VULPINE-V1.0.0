/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Silt's languages, witnessed, row by row against silt_rules.h, the C the
 * model runs; the parts it composes (sealwax, broadsheet, witness, banker,
 * cartulary, tallystick) resolved and called; and the BOTH of two records
 * under one reference. Slots: @logic.silt_record_may_dispose,
 * @num.silt_backlog_clear_days, @num.silt_register_number,
 * @logic.silt_case_file_node_ok; and, used, @logic.sealwax_sign_ready,
 * @logic.broadsheet_well_formed, @logic.witness_chain_ok,
 * @num.witness_kind_count, @num.banker_share, @num.banker_total,
 * @logic.banker_entry_ok, @num.cartulary_retention_left,
 * @num.cartulary_class_year_cell, @num.tallystick_next_doc_number. */
#include "silt_rules.h"

APP_TEST(silt, 51, 242)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], want[WAVE_ROWS];

    /* compose: six parts, every part entry it calls resolves to its part */
    static const char *const used[10][2] = {{"sealwax.sign_ready", "sealwax"},
                                            {"broadsheet.well_formed", "broadsheet"},
                                            {"witness.chain_ok", "witness"},
                                            {"witness.kind_count", "witness"},
                                            {"banker.share", "banker"},
                                            {"banker.total", "banker"},
                                            {"banker.entry_ok", "banker"},
                                            {"cartulary.retention_left", "cartulary"},
                                            {"cartulary.class_year_cell", "cartulary"},
                                            {"tallystick.next_doc_number", "tallystick"}};
    CHECK(app->resolved && app->nuse == 6 && app->composed == 6 && app->n == 51,
          "Silt: composed of 6 parts, 6 own lines and 45 of theirs (%u, %u)", app->composed,
          app->n);
    for (uint32_t i = 0; i < 10u; i++) {
        const zxapp_logic_t *L = zxapp_find(app, used[i][0]);
        CHECK(L && L->origin && !strcmp(L->origin->id, used[i][1]) && L->loaded &&
                  zxapp_contract(app, used[i][0]),
              "Silt: %s resolves to %s and loads", used[i][0], used[i][1]);
    }
    zxapp_origin_t org[12];
    uint32_t norg = zxapp_provenance(app, org, 12);
    CHECK(norg == 9 && !org[0].from, "Silt: itself, six parts and what they are made of (%u)",
          norg);

    /* SR2: record_may_dispose, Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2u ? wrand(-60, 60) : wrand(SILT_LEFT_MIN, 9999999);
        b[i] = wrand(0, 2);
        c[i] = wrand(0, 2);
        if (i == 0) a[i] = 0, b[i] = 0, c[i] = 1;             /* due today */
        if (i == 1) a[i] = SILT_LEFT_MIN, b[i] = 0, c[i] = 1; /* the least left */
        if (i == 2) a[i] = 1, b[i] = 0, c[i] = 1;             /* a day to go */
        if (i == 3) a[i] = -5, b[i] = 1, c[i] = 1;            /* a legal hold wins */
        if (i == 4) a[i] = -5, b[i] = 0, c[i] = 0;            /* not approved */
        want[i] = T_(silt_may_dispose(a[i], b[i], c[i]));
    }
    zxapp_arg_t da[3] = {
        {"left", a, WAVE_ROWS}, {"hold", b, WAVE_ROWS}, {"approved", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE &&
              wave_same(app, "record_may_dispose", da, 3, want, WAVE_ROWS, true),
          "Silt: record_may_dispose, Eve = KRON = C");
    {
        static const int64_t left[2] = {SILT_LEFT_MIN - 1, -1}, zero[2] = {0, 0}, one[2] = {1, 1};
        zxapp_arg_t xa[3] = {{"left", left, 2}, {"hold", zero, 2}, {"approved", one, 2}};
        CHECK(!silt_may_dispose(left[0], 0, 1) &&
                  wave_lp(app, "record_may_dispose", xa, 3) == ZXAPP_BOTH,
              "Silt: a left below s9(7): Eve refuses, KRON answers: BOTH");
    }

    /* SR3: backlog_clear_days, one forecast a call, Trebago = C, exact */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        uint32_t n = (uint32_t) wrand(1, 30);
        int64_t backlog = wrand(0, SILT_BACKLOG_MAX), in = 0, out = 0;
        for (uint32_t k = 0; k < n; k++) {
            a[k] = wrand(0, 500);
            b[k] = a[k] + wrand(0, 300);
        }
        if (i == 0) n = 1, a[0] = 0, b[0] = 1, backlog = 0; /* nothing waiting */
        if (i == 1)                                         /* the most */
            n = 1, a[0] = 0, b[0] = SILT_PER_DAY_MAX, backlog = SILT_BACKLOG_MAX;
        if (i == 2)
            n = 3, a[0] = 1, a[1] = 2, a[2] = 0, b[0] = 2, b[1] = 2, b[2] = 2,
            backlog = 10; /* falls by 1 in 3 days: 30 */
        for (uint32_t k = 0; k < n; k++) in += a[k], out += b[k];
        if (out <= in) b[0] += 1;
        rat_t w = rat_zero();
        CHECK(silt_backlog_days(a, b, n, backlog, &w), "Silt: SR3 row %u answers", i);
        zxapp_arg_t ba[3] = {{"arrived", a, n}, {"cleared", b, n}, {"backlog", &backlog, 1}};
        zxapp_num_t got[WAVE_ROWS];
        uint32_t ngot = 0;
        int32_t lp = -1;
        CHECK(zxapp_call_witnessed(app, &H, "backlog_clear_days", ba, 3, got, WAVE_ROWS, &ngot,
                                   &lp) == ZXAPP_OK &&
                  lp == ZXAPP_NEITHER && ngot == 1 && got[0].num == w.num && got[0].den == w.den,
              "Silt: backlog_clear_days row %u, Trebago = C (%lld/%lld)", i, (long long) w.num,
              (long long) w.den);
    }
    {
        static const int64_t same[2] = {5, 5}, neg[2] = {-1, 5}, big[2] = {0, 1000000},
                             two[2] = {6, 6};
        int64_t backlog = 10, over = SILT_BACKLOG_MAX + 1;
        rat_t w = rat_zero();
        zxapp_arg_t p1[3] = {{"arrived", same, 2}, {"cleared", same, 2}, {"backlog", &backlog, 1}};
        zxapp_arg_t p2[3] = {{"arrived", neg, 2}, {"cleared", two, 2}, {"backlog", &backlog, 1}};
        zxapp_arg_t p3[3] = {{"arrived", same, 2}, {"cleared", big, 2}, {"backlog", &backlog, 1}};
        zxapp_arg_t p4[3] = {{"arrived", same, 2}, {"cleared", two, 2}, {"backlog", &over, 1}};
        CHECK(!silt_backlog_days(same, same, 2, 10, &w) &&
                  !silt_backlog_days(neg, two, 2, 10, &w) &&
                  !silt_backlog_days(same, big, 2, 10, &w) &&
                  !silt_backlog_days(same, two, 2, over, &w) &&
                  wave_lp(app, "backlog_clear_days", p1, 3) == ZXAPP_NEITHER &&
                  wave_lp(app, "backlog_clear_days", p2, 3) == ZXAPP_NEITHER &&
                  wave_lp(app, "backlog_clear_days", p3, 3) == ZXAPP_NEITHER &&
                  wave_lp(app, "backlog_clear_days", p4, 3) == ZXAPP_NEITHER,
              "Silt: a backlog that does not fall, a negative day, a day past 999999, a backlog "
              "past 999999999: refused");
    }

    /* SR1: register_number, one row a call (ALGOL reads one), Sutra = ALGOL = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t ye = wrand(0, SILT_YEAR_MAX), sq = wrand(1, SILT_SEQ_MAX);
        if (i == 0) ye = 0, sq = 1;                                  /* the least */
        if (i == 1) ye = SILT_YEAR_MAX, sq = SILT_SEQ_MAX;           /* the most */
        if (i == 2) ye = 2026, sq = silt_next_seq(0);                /* the year's first */
        if (i == 3) ye = 2026, sq = silt_next_seq(SILT_SEQ_MAX - 1); /* its last */
        int64_t w = silt_register_number(ye, sq);
        zxapp_arg_t ra[2] = {{"year", &ye, 1}, {"seq", &sq, 1}};
        CHECK(w >= 0 && wave_same(app, "register_number", ra, 2, &w, 1, false),
              "Silt: register_number row %u, Sutra = ALGOL = C (%lld)", i, (long long) w);
    }
    {
        /* seq 0: Sutra's picture holds it, ALGOL's proof refuses it: held */
        int64_t ye = 2026, sq = 0;
        zxapp_arg_t ra[2] = {{"year", &ye, 1}, {"seq", &sq, 1}};
        CHECK(silt_register_number(ye, sq) < 0 &&
                  wave_lp(app, "register_number", ra, 2) == ZXAPP_BOTH,
              "Silt: register number 0: Sutra answers, ALGOL refuses: BOTH");
        /* past the pictures: both refuse */
        static const int64_t bad[4][2] = {{10000, 1}, {-1, 1}, {2026, 1000000}, {2026, -1}};
        for (uint32_t i = 0; i < 4u; i++) {
            int64_t y2 = bad[i][0], s2 = bad[i][1];
            zxapp_arg_t xa[2] = {{"year", &y2, 1}, {"seq", &s2, 1}};
            CHECK(silt_register_number(y2, s2) < 0 &&
                      wave_lp(app, "register_number", xa, 2) == ZXAPP_NEITHER,
                  "Silt: register_number past the pictures (%u): both refuse", i);
        }
        CHECK(silt_next_seq(SILT_SEQ_MAX) < 0 && silt_next_seq(-1) < 0 && silt_next_seq(0) == 1,
              "Silt: a full year's register refuses the next seq");
    }

    /* SR4: case_file_node_ok, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-12000, 12000);
        b[i] = wrand(-1200000, 1200000);
        c[i] = wrand(0, 12000);
        d[i] = wrand(0, INT64_C(12000000000));
        e[i] = wrand(0, 1200000);
        f[i] = wrand(0, 12);
        if (i == 0)
            a[i] = 9999, b[i] = 999999, c[i] = 9999, d[i] = SILT_REGNO_MAX, e[i] = 999999,
            f[i] = 9; /* max */
        if (i == 1) a[i] = 1, b[i] = 1, c[i] = 1, d[i] = SILT_REGNO_MAX + 1, e[i] = 1, f[i] = 0;
        if (i == 2) a[i] = 0, b[i] = 0, c[i] = 0, d[i] = 0, e[i] = 0, f[i] = 0; /* zero */
        if (i == 3) a[i] = -9999, b[i] = 1, c[i] = 1, d[i] = 1, e[i] = 1, f[i] = 10;
        want[i] = T_(silt_case_node_ok(a[i], b[i], c[i], d[i], e[i], f[i]));
    }
    zxapp_arg_t na[6] = {{"series", a, WAVE_ROWS}, {"caseno", b, WAVE_ROWS},
                         {"doc", c, WAVE_ROWS},    {"regno", d, WAVE_ROWS},
                         {"dated", e, WAVE_ROWS},  {"hold", f, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[2] == ZXAPP_TRUE &&
              want[3] == ZXAPP_FALSE &&
              wave_same(app, "case_file_node_ok", na, 6, want, WAVE_ROWS, true),
          "Silt: case_file_node_ok, MYRA = C");

    /* the parts, called through Silt as it exports them */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 40);
        b[i] = i % 4u ? wrand(0, 40) : 0;
        c[i] = i % 3u ? 0 : wrand(0, 2);
        if (i == 0) a[i] = 1, b[i] = 0, c[i] = 0;
        if (i == 1) a[i] = SEALWAX_FIELDS, b[i] = 0, c[i] = 0;
        if (i == 2) a[i] = SEALWAX_FIELDS + 1, b[i] = 0, c[i] = 0;
        want[i] = T_(sealwax_sign_ready(a[i], b[i], c[i]));
    }
    zxapp_arg_t sa[3] = {
        {"fields", a, WAVE_ROWS}, {"missing", b, WAVE_ROWS}, {"outside", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              wave_same(app, "sealwax.sign_ready", sa, 3, want, WAVE_ROWS, true),
          "Silt: sealwax.sign_ready, a receipt ready to stamp");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1);
        d[i] = wrand(1, 1000000);
        b[i] = wrand(-2, d[i] + 2);
        c[i] = d[i] - b[i];
        want[i] = T_(broadsheet_well_formed(a[i], b[i], c[i]) == 1);
    }
    zxapp_arg_t wa[3] = {{"header", a, WAVE_ROWS}, {"off", b, WAVE_ROWS}, {"room", c, WAVE_ROWS}};
    CHECK(wave_same(app, "broadsheet.well_formed", wa, 3, want, WAVE_ROWS, true),
          "Silt: broadsheet.well_formed, a received PDF's startxref");
    {
        /* the register's lines, each linked to the one before */
        static const int64_t seq[4] = {1, 2, 3, 3}, prev[4] = {0, 1, 2, 1}, last[4] = {0, 1, 2, 3};
        int64_t w[4];
        for (uint32_t i = 0; i < 4u; i++) w[i] = T_(witness_linked(seq[i], prev[i], last[i]));
        zxapp_arg_t ca[3] = {{"seq", seq, 4}, {"prev", prev, 4}, {"last", last, 4}};
        CHECK(w[3] == ZXAPP_FALSE && wave_same(app, "witness.chain_ok", ca, 3, w, 4, true),
              "Silt: witness.chain_ok over the register, KRON = Sutra");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(SILT_FILED, SILT_DISPOSED);
    {
        int64_t filed = SILT_FILED, k = witness_kind_count(a, WAVE_ROWS, filed);
        zxapp_arg_t ka[2] = {{"kind", a, WAVE_ROWS}, {"want", &filed, 1}};
        CHECK(wave_same(app, "witness.kind_count", ka, 2, &k, 1, false),
              "Silt: witness.kind_count, the records filed, KRON = Trebago");
    }
    {
        int64_t sum = 0;
        for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(0, 1000000), sum += a[i];
        for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = silt_share(a[i], sum);
        zxapp_arg_t ha[2] = {{"size", a, WAVE_ROWS}, {"total", &sum, 1}};
        CHECK(wave_same(app, "banker.share", ha, 2, want, WAVE_ROWS, false),
              "Silt: banker.share, each record's share of the register's bytes");
        zxapp_arg_t ta[1] = {{"size", a, WAVE_ROWS}};
        CHECK(cartulary_total(a, WAVE_ROWS, &want[0]) && want[0] == sum &&
                  wave_same(app, "banker.total", ta, 1, want, 1, false),
              "Silt: banker.total, the register's bytes");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 9999);
        b[i] = wrand(1, SILT_SEQ_MAX);
        c[i] = i % 3u ? wrand(0, INT64_C(999999999999)) : INT64_C(999999999999999999) + (i % 2u);
        d[i] = wrand(0, SILT_DAYS_MAX * 3000);
        want[i] = T_(cartulary_entry_ok(a[i], b[i], c[i], d[i]));
    }
    zxapp_arg_t ea[4] = {{"folder", a, WAVE_ROWS},
                         {"seq", b, WAVE_ROWS},
                         {"size", c, WAVE_ROWS},
                         {"mtime", d, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[3] == ZXAPP_FALSE &&
              wave_same(app, "banker.entry_ok", ea, 4, want, WAVE_ROWS, true),
          "Silt: banker.entry_ok, each register line");
    for (uint32_t i = 0; i < 8u; i++) {
        int64_t fi = wrand(0, CARTULARY_DAY_MAX), ye = wrand(0, 100), mo = wrand(0, 11),
                to = wrand(0, CARTULARY_DAY_MAX), w = 0;
        CHECK(cartulary_left(fi, ye, mo, to, &w), "Silt: CR2 row %u answers", i);
        zxapp_arg_t ra[4] = {
            {"filed", &fi, 1}, {"years", &ye, 1}, {"months", &mo, 1}, {"today", &to, 1}};
        CHECK(wave_same(app, "cartulary.retention_left", ra, 4, &w, 1, false),
              "Silt: cartulary.retention_left row %u, a record's retention", i);
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(1, 4), b[i] = wrand(2020, 2026);
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = cartulary_cell(a, b, WAVE_ROWS, i);
    zxapp_arg_t ga[2] = {{"cls", a, WAVE_ROWS}, {"year", b, WAVE_ROWS}};
    CHECK(wave_same(app, "cartulary.class_year_cell", ga, 2, want, WAVE_ROWS, false),
          "Silt: cartulary.class_year_cell, the filing grid of series by year");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, SILT_SEQ_MAX - 1);
        want[i] = tstk_next_number(a[i]);
    }
    zxapp_arg_t na2[1] = {{"last", a, WAVE_ROWS}};
    CHECK(wave_same(app, "tallystick.next_doc_number", na2, 1, want, WAVE_ROWS, false),
          "Silt: tallystick.next_doc_number, the year's next seq");

    /* SR5, the BOTH test: two records under one reference, dates 20000 and
     * 20031: BOTH, so disposal, over and approved and no hold, refuses */
    static const int64_t one_date[2] = {20000, 20000}, two_dates[2] = {20000, 20031};
    lpres_state_t st = silt_reference_state(two_dates, 2);
    CHECK(st == LPRES_STATE_BOTH && (int) st == 3, "Silt: one reference, two dates: BOTH (3)");
    CHECK(lpres_conjoin(st, silt_may_dispose(-1, 0, 1) ? LPRES_STATE_TRUE : LPRES_STATE_FALSE) !=
              LPRES_STATE_TRUE,
          "Silt: BOTH and a disposal truth: not disposed");
    CHECK(silt_reference_state(one_date, 2) == LPRES_STATE_TRUE &&
              silt_reference_state(one_date, 1) == LPRES_STATE_TRUE &&
              silt_reference_state(one_date, 0) == LPRES_STATE_NEITHER,
          "Silt: one date is TRUE, no record NEITHER");
}
