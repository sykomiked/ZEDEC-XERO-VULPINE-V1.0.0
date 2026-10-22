/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Neap's languages, witnessed, row by row against neap_rules.h, the C the
 * model runs; the parts it composes (coffer, tallystick, reckoner,
 * furrow) resolved and called; and the BOTH of a statement line the books
 * do not have. Slots: @logic.neap_entry_may_post, @num.neap_equation_gap,
 * @num.neap_straight_charge, @num.neap_declining_charge,
 * @num.neap_gl_line_amount, @num.neap_account_month_total,
 * @logic.neap_coa_node_ok; and, used, @num.coffer_balance,
 * @logic.coffer_transfer_ok, @num.tallystick_taxed_line_total,
 * @logic.tallystick_paid_in_full, @num.reckoner_statement_close,
 * @num.reckoner_band_tax, @logic.reckoner_report_ok,
 * @num.furrow_range_total, @num.furrow_money_cell. */
#include "furrow_rules.h"
#include "lpres.h"
#include "neap_rules.h"
#include "reckoner_rules.h"
#include "tallystick_rules.h"

APP_TEST(neap, 32, 234)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], want[WAVE_ROWS];

    /* compose: four parts, every part entry it exports resolves to its part */
    static const char *const used[9][2] = {{"coffer.balance", "coffer"},
                                           {"coffer.transfer_ok", "coffer"},
                                           {"tallystick.taxed_line_total", "tallystick"},
                                           {"tallystick.paid_in_full", "tallystick"},
                                           {"reckoner.statement_close", "reckoner"},
                                           {"reckoner.band_tax", "reckoner"},
                                           {"reckoner.report_ok", "reckoner"},
                                           {"furrow.range_total", "furrow"},
                                           {"furrow.money_cell", "furrow"}};
    CHECK(app->resolved && app->nuse == 4 && app->composed == 11 && app->n == 32,
          "Neap: composed of 4 parts, 11 own lines and 21 of theirs (%u, %u)", app->composed,
          app->n);
    for (uint32_t i = 0; i < 9u; i++) {
        const zxapp_logic_t *L = zxapp_find(app, used[i][0]);
        CHECK(L && L->origin && !strcmp(L->origin->id, used[i][1]) && L->loaded &&
                  zxapp_contract(app, used[i][0]),
              "Neap: %s resolves to %s and loads", used[i][0], used[i][1]);
    }
    zxapp_origin_t org[8];
    CHECK(zxapp_provenance(app, org, 8) == 5 && !org[0].from, "Neap: itself and four parts");

    /* NR1: entry_may_post, Eve = Sutra = C, every edge of each field */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 4u ? wrand(1, 100000000) : wrand(-5, NEAP_AMOUNT_MAX);
        b[i] = i % 3u ? 0 : wrand(-3, 3);
        c[i] = i % 5u ? 0 : wrand(0, NEAP_SHUT_MAX);
        d[i] = i % 7u ? 1 : wrand(0, NEAP_PERIOD_MAX);
        if (i == 0) a[i] = 1, b[i] = 0, c[i] = 0, d[i] = 1;                   /* the least */
        if (i == 1) a[i] = NEAP_AMOUNT_MAX, b[i] = 0, c[i] = 0, d[i] = 1;     /* max */
        if (i == 2) a[i] = NEAP_AMOUNT_MAX + 1, b[i] = 0, c[i] = 0, d[i] = 1; /* max + 1 */
        if (i == 3) a[i] = 0, b[i] = 0, c[i] = 0, d[i] = 1;                   /* zero */
        if (i == 4) a[i] = -1, b[i] = 0, c[i] = 0, d[i] = 1;                  /* negative */
        if (i == 5) a[i] = 500, b[i] = -NEAP_MINOR_MAX, c[i] = 0, d[i] = 1;   /* unbalanced */
        if (i == 6) a[i] = 500, b[i] = 0, c[i] = NEAP_SHUT_MAX, d[i] = 1;     /* closed */
        if (i == 7) a[i] = 500, b[i] = 0, c[i] = 0, d[i] = 0;                 /* period shut */
        want[i] = T_(neap_entry_may_post(a[i], b[i], c[i], d[i]));
    }
    zxapp_arg_t ea[4] = {{"amount", a, WAVE_ROWS},
                         {"gap", b, WAVE_ROWS},
                         {"shut", c, WAVE_ROWS},
                         {"period", d, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              wave_same(app, "entry_may_post", ea, 4, want, WAVE_ROWS, true),
          "Neap: entry_may_post, Eve = Sutra = C");
    {
        /* past Sutra's pictures: Sutra refuses, Eve answers no: BOTH */
        static const int64_t bad[4][4] = {{500, 0, -1, 1},
                                          {500, NEAP_MINOR_MAX + 1, 0, 1},
                                          {500, 0, NEAP_SHUT_MAX + 1, 1},
                                          {500, 0, 0, NEAP_PERIOD_MAX + 1}};
        for (uint32_t i = 0; i < 4u; i++) {
            int64_t am[2] = {500, bad[i][0]}, ga[2] = {0, bad[i][1]}, sh[2] = {0, bad[i][2]},
                    pe[2] = {1, bad[i][3]};
            zxapp_arg_t xa[4] = {
                {"amount", am, 2}, {"gap", ga, 2}, {"shut", sh, 2}, {"period", pe, 2}};
            CHECK(!neap_entry_witnessed(bad[i][1], bad[i][2], bad[i][3]) &&
                      !neap_entry_may_post(bad[i][0], bad[i][1], bad[i][2], bad[i][3]) &&
                      wave_lp(app, "entry_may_post", xa, 4) == ZXAPP_BOTH,
                  "Neap: entry_may_post outside Sutra's pictures (%u): BOTH", i);
        }
    }

    /* NR2: equation_gap, Sutra = Trebago = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        b[i] = wrand(0, 999999999999999);
        c[i] = wrand(-999999999999999, 999999999999999);
        a[i] = i % 2u
                   ? b[i] + c[i]
                   : wrand(-NEAP_MINOR_MAX + 2000000000000000, NEAP_MINOR_MAX - 2000000000000000);
        if (i == 0) a[i] = 0, b[i] = 0, c[i] = 0;                           /* zero */
        if (i == 1) a[i] = NEAP_MINOR_MAX, b[i] = 0, c[i] = NEAP_MINOR_MAX; /* max */
        if (i == 2) a[i] = -NEAP_MINOR_MAX, b[i] = 0, c[i] = 0;             /* least */
        if (i == 4) a[i] = NEAP_MINOR_MAX - 1, b[i] = -1, c[i] = 0;         /* to the edge */
        want[i] = neap_equation_gap(a[i], b[i], c[i]);
        CHECK(want[i] != NEAP_REFUSED, "Neap: NR2 row %u answers", i);
    }
    zxapp_arg_t qa[3] = {
        {"assets", a, WAVE_ROWS}, {"liabilities", b, WAVE_ROWS}, {"equity", c, WAVE_ROWS}};
    CHECK(want[0] == 0 && want[1] == 0 && want[3] == 0 &&
              wave_same(app, "equation_gap", qa, 3, want, WAVE_ROWS, false),
          "Neap: equation_gap, Sutra = Trebago = C");
    {
        /* past 18 digits: Sutra refuses, Trebago answers */
        static const int64_t as[2] = {0, NEAP_MINOR_MAX + 1}, li[2] = {0, 0}, eq[2] = {0, 0};
        static const int64_t gs[2] = {0, NEAP_MINOR_MAX}, gl[2] = {0, -1}, ge[2] = {0, 0};
        zxapp_arg_t xa[3] = {{"assets", as, 2}, {"liabilities", li, 2}, {"equity", eq, 2}};
        zxapp_arg_t xb[3] = {{"assets", gs, 2}, {"liabilities", gl, 2}, {"equity", ge, 2}};
        CHECK(neap_equation_gap(as[1], 0, 0) == NEAP_REFUSED &&
                  neap_equation_gap(gs[1], gl[1], 0) == NEAP_REFUSED &&
                  wave_lp(app, "equation_gap", xa, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "equation_gap", xb, 3) == ZXAPP_BOTH,
              "Neap: equation_gap past 18 digits, an input or the gap: BOTH");
    }

    /* NR3: straight_charge, Trebago = Sutra = C, and the charges sum to the base */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2u ? wrand(0, 100000000) : wrand(0, NEAP_COST_MAX);
        b[i] = wrand(0, a[i]);
        c[i] = wrand(1, NEAP_LIFE_MAX);
        d[i] = i % 3u ? wrand(1, c[i]) : c[i];
        if (i == 0) a[i] = 0, b[i] = 0, c[i] = 1, d[i] = 1;                         /* zero */
        if (i == 1) a[i] = NEAP_COST_MAX, b[i] = 0, c[i] = NEAP_LIFE_MAX, d[i] = 1; /* max */
        if (i == 2) a[i] = NEAP_COST_MAX, b[i] = 0, c[i] = NEAP_LIFE_MAX, d[i] = NEAP_LIFE_MAX;
        if (i == 3) a[i] = 100000, b[i] = 10000, c[i] = 7, d[i] = 7; /* the remainder */
        if (i == 4) a[i] = 5, b[i] = 5, c[i] = 3, d[i] = 2;          /* nothing to charge */
        want[i] = neap_straight_charge(a[i], b[i], c[i], d[i]);
        CHECK(want[i] != NEAP_REFUSED, "Neap: NR3 row %u answers", i);
    }
    zxapp_arg_t sa[4] = {{"cost", a, WAVE_ROWS},
                         {"salvage", b, WAVE_ROWS},
                         {"life", c, WAVE_ROWS},
                         {"period", d, WAVE_ROWS}};
    CHECK(want[2] == 833333732 && want[3] == 12858 &&
              wave_same(app, "straight_charge", sa, 4, want, WAVE_ROWS, false),
          "Neap: straight_charge, Trebago = Sutra = C");
    {
        /* one asset's whole life: the charges sum to cost - salvage exactly */
        int64_t s = 0;
        for (int64_t p = 1; p <= 7; p++) s += neap_straight_charge(100000, 10000, 7, p);
        CHECK(s == 90000, "Neap: a whole life's charges sum to the base (%lld)", (long long) s);
        static const int64_t co[2] = {100, NEAP_COST_MAX + 1}, sv[2] = {0, 0}, lf[2] = {1, 1},
                             pr[2] = {1, 1};
        zxapp_arg_t xa[4] = {
            {"cost", co, 2}, {"salvage", sv, 2}, {"life", lf, 2}, {"period", pr, 2}};
        CHECK(neap_straight_charge(co[1], 0, 1, 1) == NEAP_REFUSED &&
                  wave_lp(app, "straight_charge", xa, 4) == ZXAPP_BOTH,
              "Neap: straight_charge past Sutra's 9(12): BOTH");
        static const int64_t bad[4][4] = {
            {100, 101, 1, 1}, {100, 0, 0, 1}, {100, 0, 1201, 1}, {100, 0, 3, 4}};
        for (uint32_t i = 0; i < 4u; i++) {
            int64_t c2[2] = {100, bad[i][0]}, s2[2] = {0, bad[i][1]}, l2[2] = {1, bad[i][2]},
                    p2[2] = {1, bad[i][3]};
            zxapp_arg_t ya[4] = {
                {"cost", c2, 2}, {"salvage", s2, 2}, {"life", l2, 2}, {"period", p2, 2}};
            CHECK(neap_straight_charge(bad[i][0], bad[i][1], bad[i][2], bad[i][3]) ==
                          NEAP_REFUSED &&
                      wave_lp(app, "straight_charge", ya, 4) == ZXAPP_NEITHER,
                  "Neap: straight_charge refused by both (%u)", i);
        }
    }

    /* NR4: declining_charge, Trebago = Sutra = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2u ? wrand(0, 100000000) : wrand(0, NEAP_COST_MAX);
        b[i] = i % 3u ? wrand(0, a[i] / 10) : wrand(0, a[i]);
        c[i] = wrand(0, NEAP_RATE_ALL);
        d[i] = i % 5u ? 0 : 1;
        if (i == 0) a[i] = 0, b[i] = 0, c[i] = 0, d[i] = 0;                         /* zero */
        if (i == 1) a[i] = NEAP_COST_MAX, b[i] = 0, c[i] = NEAP_RATE_ALL, d[i] = 0; /* max */
        if (i == 2) a[i] = 100000, b[i] = 99000, c[i] = 2000, d[i] = 0; /* held at salvage */
        if (i == 3) a[i] = 1000, b[i] = 0, c[i] = 3333, d[i] = 0;       /* floored */
        want[i] = neap_declining_charge(a[i], b[i], c[i], d[i]);
        CHECK(want[i] != NEAP_REFUSED, "Neap: NR4 row %u answers", i);
    }
    zxapp_arg_t da[4] = {{"book", a, WAVE_ROWS},
                         {"salvage", b, WAVE_ROWS},
                         {"rate", c, WAVE_ROWS},
                         {"last", d, WAVE_ROWS}};
    CHECK(want[1] == NEAP_COST_MAX && want[2] == 1000 && want[3] == 333 &&
              wave_same(app, "declining_charge", da, 4, want, WAVE_ROWS, false),
          "Neap: declining_charge, Trebago = Sutra = C");
    {
        static const int64_t bk[2] = {100, NEAP_COST_MAX + 1}, sv[2] = {0, 0}, rt[2] = {1000, 1000},
                             ls[2] = {0, 0};
        zxapp_arg_t xa[4] = {{"book", bk, 2}, {"salvage", sv, 2}, {"rate", rt, 2}, {"last", ls, 2}};
        CHECK(neap_declining_charge(bk[1], 0, 1000, 0) == NEAP_REFUSED &&
                  wave_lp(app, "declining_charge", xa, 4) == ZXAPP_BOTH,
              "Neap: declining_charge past Sutra's 9(12): BOTH");
        static const int64_t bad[4][4] = {
            {100, 101, 0, 0}, {100, 0, 10001, 0}, {100, 0, -1, 0}, {100, 0, 0, 2}};
        for (uint32_t i = 0; i < 4u; i++) {
            int64_t b2[2] = {100, bad[i][0]}, s2[2] = {0, bad[i][1]}, r2[2] = {0, bad[i][2]},
                    l2[2] = {0, bad[i][3]};
            zxapp_arg_t ya[4] = {
                {"book", b2, 2}, {"salvage", s2, 2}, {"rate", r2, 2}, {"last", l2, 2}};
            CHECK(neap_declining_charge(bad[i][0], bad[i][1], bad[i][2], bad[i][3]) ==
                          NEAP_REFUSED &&
                      wave_lp(app, "declining_charge", ya, 4) == ZXAPP_NEITHER,
                  "Neap: declining_charge refused by both (%u)", i);
        }
    }

    /* NR5: gl_line_amount, Sutra = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2u ? wrand(0, NEAP_MINOR_MAX) : 0;
        b[i] = i % 2u ? 0 : wrand(0, NEAP_MINOR_MAX);
        c[i] = i % 3u ? 1 : -1;
        if (i == 0) a[i] = NEAP_MINOR_MAX, b[i] = 0, c[i] = 1;  /* max */
        if (i == 1) a[i] = 0, b[i] = NEAP_MINOR_MAX, c[i] = -1; /* max, credit-normal */
        if (i == 2) a[i] = 0, b[i] = 0, c[i] = 1;               /* zero */
        want[i] = neap_gl_line_amount(a[i], b[i], c[i]);
    }
    zxapp_arg_t ga[3] = {
        {"debit", a, WAVE_ROWS}, {"credit", b, WAVE_ROWS}, {"normal", c, WAVE_ROWS}};
    CHECK(want[0] == NEAP_MINOR_MAX && want[1] == NEAP_MINOR_MAX &&
              wave_same(app, "gl_line_amount", ga, 3, want, WAVE_ROWS, false),
          "Neap: gl_line_amount, Sutra = C");
    {
        static const int64_t bad[5][3] = {
            {NEAP_MINOR_MAX + 1, 0, 1}, {0, -1, 1}, {1, 0, 0}, {1, 0, 2}, {1, 0, -2}};
        for (uint32_t i = 0; i < 5u; i++) {
            int64_t d2[2] = {1, bad[i][0]}, c2[2] = {0, bad[i][1]}, n2[2] = {1, bad[i][2]};
            zxapp_arg_t xa[3] = {{"debit", d2, 2}, {"credit", c2, 2}, {"normal", n2, 2}};
            CHECK(neap_gl_line_amount(bad[i][0], bad[i][1], bad[i][2]) == NEAP_REFUSED &&
                      wave_lp(app, "gl_line_amount", xa, 3) == ZXAPP_NEITHER,
                  "Neap: gl_line_amount refused on overflow or a normal that is no side (%u)", i);
        }
    }

    /* NR6: account_month_total, KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        a[i] = wrand(1, 6), b[i] = wrand(1, 12), c[i] = wrand(-1000000000, 1000000000);
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = neap_grid_cell(a, b, c, WAVE_ROWS, i);
    zxapp_arg_t ma[3] = {
        {"account", a, WAVE_ROWS}, {"month", b, WAVE_ROWS}, {"amount", c, WAVE_ROWS}};
    CHECK(wave_same(app, "account_month_total", ma, 3, want, WAVE_ROWS, false),
          "Neap: account_month_total, KRON = C");

    /* NR7: coa_node_ok, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1200000, 1200000);
        b[i] = wrand(-1200000, 1200000);
        c[i] = wrand(-12, 12);
        d[i] = wrand(-12, 12);
        if (i == 0) a[i] = 999999, b[i] = 999999, c[i] = 9, d[i] = 9; /* max */
        if (i == 1) a[i] = 1000000, b[i] = 1, c[i] = 1, d[i] = 1;     /* max + 1 */
        if (i == 2) a[i] = 0, b[i] = 0, c[i] = 0, d[i] = 0;           /* zero */
        if (i == 3) a[i] = -999999, b[i] = 1, c[i] = -9, d[i] = 1;    /* negative */
        want[i] = T_(neap_node_ok(a[i], b[i], c[i], d[i]));
    }
    zxapp_arg_t na[4] = {{"parent", a, WAVE_ROWS},
                         {"code", b, WAVE_ROWS},
                         {"kind", c, WAVE_ROWS},
                         {"open", d, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE &&
              wave_same(app, "coa_node_ok", na, 4, want, WAVE_ROWS, true),
          "Neap: coa_node_ok, MYRA = C");

    /* the parts, called through Neap as it exports them */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-NEAP_MINOR_MAX / 2, NEAP_MINOR_MAX / 2);
        b[i] = wrand(-NEAP_MINOR_MAX / 2, NEAP_MINOR_MAX / 2);
        want[i] = a[i] + b[i];
    }
    zxapp_arg_t ba[2] = {{"before", a, WAVE_ROWS}, {"moved", b, WAVE_ROWS}};
    CHECK(wave_same(app, "coffer.balance", ba, 2, want, WAVE_ROWS, false),
          "Neap: coffer.balance, an account's balance after a posting");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1000000);
        b[i] = wrand(-5, 1000000);
        c[i] = wrand(-5, 1000000);
        d[i] = i % 4u ? 0 : wrand(-2, 2);
        want[i] = T_(a[i] >= 1 && a[i] <= NEAP_AMOUNT_MAX && b[i] >= 0 && c[i] >= 0 && d[i] == 0);
    }
    zxapp_arg_t ta[4] = {{"amount", a, WAVE_ROWS},
                         {"after", b, WAVE_ROWS},
                         {"room", c, WAVE_ROWS},
                         {"gap", d, WAVE_ROWS}};
    CHECK(wave_same(app, "coffer.transfer_ok", ta, 4, want, WAVE_ROWS, true),
          "Neap: coffer.transfer_ok beside entry_may_post");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 999999);
        b[i] = wrand(0, 9999999);
        c[i] = wrand(0, 3);
        d[i] = wrand(3, 100);
        want[i] = tstk_line_total(a[i], b[i], c[i], d[i]);
    }
    zxapp_arg_t la[4] = {{"qty", a, WAVE_ROWS},
                         {"price", b, WAVE_ROWS},
                         {"taxn", c, WAVE_ROWS},
                         {"taxd", d, WAVE_ROWS}};
    CHECK(wave_same(app, "tallystick.taxed_line_total", la, 4, want, WAVE_ROWS, false),
          "Neap: tallystick.taxed_line_total, an invoice line");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1000000);
        b[i] = wrand(-1000, 1000);
        want[i] = T_(tstk_paid_in_full(a[i], b[i]));
    }
    zxapp_arg_t pa[2] = {{"total", a, WAVE_ROWS}, {"over", b, WAVE_ROWS}};
    CHECK(wave_same(app, "tallystick.paid_in_full", pa, 2, want, WAVE_ROWS, true),
          "Neap: tallystick.paid_in_full, an invoice settled");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-NEAP_MINOR_MAX / 4, NEAP_MINOR_MAX / 4);
        b[i] = wrand(0, NEAP_MINOR_MAX / 4);
        c[i] = wrand(0, NEAP_MINOR_MAX / 4);
        want[i] = reckoner_statement_close(a[i], b[i], c[i]);
    }
    zxapp_arg_t ra[3] = {
        {"opening", a, WAVE_ROWS}, {"credits", b, WAVE_ROWS}, {"debits", c, WAVE_ROWS}};
    CHECK(wave_same(app, "reckoner.statement_close", ra, 3, want, WAVE_ROWS, false),
          "Neap: reckoner.statement_close, a period's closing");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, RECKONER_TAX_MAX);
        b[i] = wrand(0, RECKONER_TAX_MAX / 2);
        c[i] = wrand(b[i], RECKONER_TAX_MAX);
        d[i] = wrand(0, RECKONER_RATE_ALL);
        want[i] = reckoner_band_tax(a[i], b[i], c[i], d[i]);
    }
    zxapp_arg_t xa[4] = {{"income", a, WAVE_ROWS},
                         {"lo", b, WAVE_ROWS},
                         {"hi", c, WAVE_ROWS},
                         {"rate", d, WAVE_ROWS}};
    CHECK(wave_same(app, "reckoner.band_tax", xa, 4, want, WAVE_ROWS, false),
          "Neap: reckoner.band_tax, the tax estimate (advice only)");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1, 2);
        b[i] = wrand(-1, 2);
        want[i] = T_(reckoner_report_ok(a[i], b[i]));
    }
    zxapp_arg_t oa[2] = {{"closed", a, WAVE_ROWS}, {"books", b, WAVE_ROWS}};
    CHECK(wave_same(app, "reckoner.report_ok", oa, 2, want, WAVE_ROWS, true),
          "Neap: reckoner.report_ok, a report allowed");
    {
        int64_t s = 0;
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = (int64_t) i + 1;
            b[i] = 10;
            c[i] = 40;
            d[i] = wrand(-1000000000, 1000000000);
        }
        CHECK(furrow_range_total(a, b, c, d, WAVE_ROWS, &s), "Neap: a column's sum answers");
        zxapp_arg_t fa[4] = {{"pos", a, WAVE_ROWS},
                             {"lo", b, WAVE_ROWS},
                             {"hi", c, WAVE_ROWS},
                             {"cell", d, WAVE_ROWS}};
        CHECK(wave_same(app, "furrow.range_total", fa, 4, &s, 1, false),
              "Neap: furrow.range_total, a trial balance's column");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-9999999999, 9999999999);
        b[i] = a[i] < 0 ? -wrand(0, 99) : wrand(0, 99);
        CHECK(furrow_money_cell(a[i], b[i], &want[i]), "Neap: money cell row %u", i);
    }
    zxapp_arg_t ca[2] = {{"units", a, WAVE_ROWS}, {"cents", b, WAVE_ROWS}};
    CHECK(wave_same(app, "furrow.money_cell", ca, 2, want, WAVE_ROWS, false),
          "Neap: furrow.money_cell, an amount typed in units and cents");

    /* NM6, the BOTH test: a statement line of 250.00 and no matching book
     * entry, the two sources about one line joined (lpres.h: knowledge
     * join across sources, then conjoin across lines), give BOTH */
    lpres_state_t stmt = LPRES_STATE_TRUE, books = LPRES_STATE_FALSE;
    lpres_state_t line = lpres_kjoin(stmt, books);
    CHECK(line == LPRES_STATE_BOTH && line == 3, "Neap: a line the books lack: BOTH");
    CHECK(lpres_conjoin(line, LPRES_STATE_TRUE) == LPRES_STATE_BOTH &&
              lpres_disjoin(line, LPRES_STATE_FALSE) == LPRES_STATE_BOTH,
          "Neap: one BOTH line keeps the period held");
    CHECK(lpres_kjoin(LPRES_STATE_TRUE, LPRES_STATE_TRUE) == LPRES_STATE_TRUE,
          "Neap: a line the books have: TRUE");
    {
        /* the hold keeps A = L + E: cash 1000.00 = capital 1000.00, then 250.00 to suspense */
        CHECK(neap_equation_gap(100000, 0, 100000) == 0 &&
                  neap_equation_gap(75000 + 25000, 0, 100000) == 0,
              "Neap: A = L + E before and after the suspense hold");
    }
}
