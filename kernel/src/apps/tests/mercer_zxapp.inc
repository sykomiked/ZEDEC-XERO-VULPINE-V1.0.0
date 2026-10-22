/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Mercer's languages, witnessed, row by row against mercer_rules.h, the C
 * the model runs; the parts it composes (coffer, tallystick, fairground,
 * tally, talisman) resolved and called against their own rules; and the
 * BOTH of a sale whose till and receipt disagree.
 * Slots: @num.mercer_stock_after_sale, @num.mercer_cost_plus_price,
 * @logic.mercer_credit_sale_ok, @num.mercer_stock_grid_cell,
 * @logic.mercer_product_node_ok; and, used, @num.coffer_balance,
 * @logic.coffer_transfer_ok, @num.tallystick_taxed_line_total,
 * @logic.tallystick_paid_in_full, @num.tallystick_next_doc_number,
 * @num.fairground_assurance_fee, @logic.fairground_offer_allowed,
 * @num.fairground_offer_unit_price, @num.tally_fx_amount,
 * @num.tally_ratio_add, @logic.talisman_earned. */
#include "fairground_rules.h"
#include "mercer_rules.h"
#include "talisman_rules.h"
#include "tally_rules.h"
#include "tallystick_rules.h"

/* One witnessed call of an entry answering exact rationals: every row the
 * reduced r[i], and the join TRUE (two languages agree). */
static bool mercer_rat_rows(zxapp_t *a, const char *entry, const zxapp_arg_t *args, uint32_t nargs,
                            const rat_t *r, uint32_t rows)
{
    zxapp_num_t o[WAVE_ROWS];
    uint32_t k = 0;
    int32_t lp = -1;
    int rc = zxapp_call_witnessed(a, &H, entry, args, nargs, o, WAVE_ROWS, &k, &lp);
    if (rc != ZXAPP_OK || lp != ZXAPP_TRUE || k != rows) {
        printf("    %s: rc %d lp %d n %u (%s)\n", entry, rc, (int) lp, k, a->err);
        return false;
    }
    for (uint32_t i = 0; i < rows; i++)
        if (o[i].num != r[i].num || o[i].den != r[i].den) {
            printf("    %s row %u: %lld/%lld, want %lld/%lld\n", entry, i, (long long) o[i].num,
                   (long long) o[i].den, (long long) r[i].num, (long long) r[i].den);
            return false;
        }
    return true;
}

APP_TEST(mercer, 44, 171)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], want[WAVE_ROWS];
    static rat_t r[WAVE_ROWS];

    /* compose: five parts, every part entry it calls resolves to its part */
    static const char *const used[11][2] = {{"coffer.balance", "coffer"},
                                            {"coffer.transfer_ok", "coffer"},
                                            {"tallystick.taxed_line_total", "tallystick"},
                                            {"tallystick.paid_in_full", "tallystick"},
                                            {"tallystick.next_doc_number", "tallystick"},
                                            {"fairground.assurance_fee", "fairground"},
                                            {"fairground.offer_allowed", "fairground"},
                                            {"fairground.offer_unit_price", "fairground"},
                                            {"tally.fx_amount", "tally"},
                                            {"tally.ratio_add", "tally"},
                                            {"talisman.earned", "talisman"}};
    CHECK(app->resolved && app->nuse == 5 && app->composed == 8 && app->n == 44,
          "Mercer: composed of 5 parts, 8 own lines and 36 of theirs (%u, %u)", app->composed,
          app->n);
    for (uint32_t i = 0; i < 11u; i++) {
        const zxapp_logic_t *L = zxapp_find(app, used[i][0]);
        CHECK(L && L->origin && !strcmp(L->origin->id, used[i][1]) && L->loaded &&
                  zxapp_contract(app, used[i][0]),
              "Mercer: %s resolves to %s and loads", used[i][0], used[i][1]);
    }
    zxapp_origin_t org[8];
    CHECK(zxapp_provenance(app, org, 8) == 6 && !org[0].from, "Mercer: itself and five parts");

    /* MR1: stock_after_sale, Sutra = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2u ? wrand(1, 1000) : wrand(1, MERCER_STOCK_MAX);
        b[i] = wrand(1, a[i]);
        if (i == 0) a[i] = 1, b[i] = 1;                               /* the last unit */
        if (i == 1) a[i] = MERCER_STOCK_MAX, b[i] = 1;                /* max */
        if (i == 2) a[i] = MERCER_STOCK_MAX, b[i] = MERCER_STOCK_MAX; /* all of it */
        want[i] = mercer_stock_after(a[i], b[i]);
    }
    zxapp_arg_t sa[2] = {{"stock", a, WAVE_ROWS}, {"sold", b, WAVE_ROWS}};
    CHECK(want[0] == 0 && want[1] == MERCER_STOCK_MAX - 1 && want[2] == 0 &&
              wave_same(app, "stock_after_sale", sa, 2, want, WAVE_ROWS, false),
          "Mercer: stock_after_sale, Sutra = KRON = C");
    {
        /* below zero, no unit sold, a negative stock: both refuse */
        static const int64_t bad[3][2] = {{3, 4}, {3, 0}, {-1, 1}};
        for (uint32_t k = 0; k < 3u; k++) {
            int64_t st[2] = {5, bad[k][0]}, so[2] = {1, bad[k][1]};
            zxapp_arg_t xa[2] = {{"stock", st, 2}, {"sold", so, 2}};
            CHECK(mercer_stock_after(bad[k][0], bad[k][1]) == MERCER_REFUSED &&
                      wave_lp(app, "stock_after_sale", xa, 2) == ZXAPP_NEITHER,
                  "Mercer: stock_after_sale edge %u: refused, never below zero", k);
        }
        static const int64_t big[2] = {5, MERCER_STOCK_MAX + 1}, one[2] = {1, 1};
        zxapp_arg_t ba[2] = {{"stock", big, 2}, {"sold", one, 2}};
        CHECK(mercer_stock_after(MERCER_STOCK_MAX + 1, 1) == MERCER_REFUSED &&
                  wave_lp(app, "stock_after_sale", ba, 2) == ZXAPP_BOTH,
              "Mercer: a stock past 9(9): Sutra refuses, KRON answers: BOTH");
    }

    /* MR2: cost_plus_price, Trebago = Sutra = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2u ? wrand(0, 100000) : wrand(0, MERCER_COST_MAX);
        b[i] = wrand(0, MERCER_RATE_MAX);
        c[i] = i % 3u ? wrand(1, 1000) : wrand(1, MERCER_RATE_MAX);
        if (i == 0) a[i] = 1000, b[i] = 25, c[i] = 100;                       /* 25% over 1000 */
        if (i == 1) a[i] = 1, b[i] = 1, c[i] = 2;                             /* half: up */
        if (i == 2) a[i] = 1, b[i] = 1, c[i] = 3;                             /* a third: down */
        if (i == 3) a[i] = MERCER_COST_MAX, b[i] = MERCER_RATE_MAX, c[i] = 1; /* max */
        if (i == 4) a[i] = 0, b[i] = 0, c[i] = 1;                             /* free */
        want[i] = mercer_cost_plus(a[i], b[i], c[i]);
    }
    zxapp_arg_t pa[3] = {{"cost", a, WAVE_ROWS}, {"mnum", b, WAVE_ROWS}, {"mden", c, WAVE_ROWS}};
    CHECK(want[0] == 1250 && want[1] == 2 && want[2] == 1 && want[3] == INT64_C(99999999900000) &&
              want[4] == 0 && wave_same(app, "cost_plus_price", pa, 3, want, WAVE_ROWS, false),
          "Mercer: cost_plus_price, Trebago = Sutra = C");
    {
        static const int64_t bad[3][3] = {{10, 1, 0}, {-1, 1, 1}, {10, 100000, 1}};
        for (uint32_t k = 0; k < 3u; k++) {
            int64_t co[2] = {10, bad[k][0]}, mn[2] = {1, bad[k][1]}, md[2] = {1, bad[k][2]};
            zxapp_arg_t xa[3] = {{"cost", co, 2}, {"mnum", mn, 2}, {"mden", md, 2}};
            CHECK(mercer_cost_plus(bad[k][0], bad[k][1], bad[k][2]) == MERCER_REFUSED &&
                      wave_lp(app, "cost_plus_price", xa, 3) == ZXAPP_NEITHER,
                  "Mercer: cost_plus_price edge %u: refused by both", k);
        }
        static const int64_t co[2] = {1, MERCER_COST_MAX + 1}, mn[2] = {0, 0}, md[2] = {1, 1};
        zxapp_arg_t ba[3] = {{"cost", co, 2}, {"mnum", mn, 2}, {"mden", md, 2}};
        CHECK(mercer_cost_plus(MERCER_COST_MAX + 1, 0, 1) == MERCER_REFUSED &&
                  wave_lp(app, "cost_plus_price", ba, 3) == ZXAPP_BOTH,
              "Mercer: a cost past 9(9): Sutra refuses, Trebago answers: BOTH");
    }

    /* MR3: credit_sale_ok, Eve = Sutra = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 4u ? wrand(1, 1000000) : wrand(-1, MERCER_AMOUNT_MAX + 1);
        b[i] = i % 5u ? wrand(0, 2000000) : wrand(-1, MERCER_AMOUNT_MAX + 1);
        c[i] = i % 3u ? wrand(-1000, 1000000) : wrand(-1, INT64_C(999999999999999999));
        if (i == 0) a[i] = 1, b[i] = 1, c[i] = 0; /* to the limit */
        if (i == 1) a[i] = MERCER_AMOUNT_MAX, b[i] = MERCER_AMOUNT_MAX, c[i] = 0;
        if (i == 2) a[i] = 500, b[i] = 1000, c[i] = -1;                 /* one past */
        if (i == 3) a[i] = 0, b[i] = 1000, c[i] = 500;                  /* no sale */
        if (i == 4) a[i] = 500, b[i] = MERCER_AMOUNT_MAX + 1, c[i] = 0; /* no limit */
        want[i] = T_(mercer_credit_ok(a[i], b[i], c[i]));
    }
    zxapp_arg_t ca[3] = {{"sale", a, WAVE_ROWS}, {"limit", b, WAVE_ROWS}, {"room", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE &&
              wave_same(app, "credit_sale_ok", ca, 3, want, WAVE_ROWS, true),
          "Mercer: credit_sale_ok, Eve = Sutra = C");
    {
        static const int64_t edge[2][3] = {{500, 1000, INT64_C(1000000000000000000)},
                                           {INT64_C(-1000000000000000000), 1000, 0}};
        for (uint32_t k = 0; k < 2u; k++) {
            int64_t s1[2] = {500, edge[k][0]}, l1[2] = {1000, edge[k][1]}, r1[2] = {0, edge[k][2]};
            zxapp_arg_t xa[3] = {{"sale", s1, 2}, {"limit", l1, 2}, {"room", r1, 2}};
            CHECK(!mercer_credit_ok(edge[k][0], edge[k][1], edge[k][2]) &&
                      wave_lp(app, "credit_sale_ok", xa, 3) == ZXAPP_BOTH,
                  "Mercer: credit_sale_ok edge %u past 18 digits: Sutra refuses, Eve says no: "
                  "BOTH",
                  k);
        }
    }

    /* MR4: stock_grid_cell, KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        a[i] = wrand(1, 5), b[i] = wrand(1, 3), c[i] = wrand(0, MERCER_STOCK_MAX);
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = mercer_grid_cell(a, b, c, WAVE_ROWS, i);
    zxapp_arg_t ga[3] = {{"product", a, WAVE_ROWS}, {"place", b, WAVE_ROWS}, {"qty", c, WAVE_ROWS}};
    CHECK(wave_same(app, "stock_grid_cell", ga, 3, want, WAVE_ROWS, false),
          "Mercer: stock_grid_cell, KRON = C");

    /* MR5: product_node_ok, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-12000, 12000);
        b[i] = wrand(-1200000, 1200000);
        c[i] = wrand(0, INT64_C(1200000000000000));
        d[i] = wrand(-1200000000, 1200000000);
        if (i == 0) a[i] = 9999, b[i] = 999999, c[i] = INT64_C(999999999999999), d[i] = 999999999;
        if (i == 1) a[i] = 10000, b[i] = 1, c[i] = 1, d[i] = 1; /* a category past 4 digits */
        want[i] = T_(mercer_node_ok(a[i], b[i], c[i], d[i]));
    }
    zxapp_arg_t na[4] = {{"category", a, WAVE_ROWS},
                         {"product", b, WAVE_ROWS},
                         {"price", c, WAVE_ROWS},
                         {"stock", d, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE &&
              wave_same(app, "product_node_ok", na, 4, want, WAVE_ROWS, true),
          "Mercer: product_node_ok, MYRA = C");

    /* the parts, called through Mercer as it exports them */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-INT64_C(100000000000000000), INT64_C(100000000000000000));
        b[i] = wrand(-INT64_C(100000000000000000), INT64_C(100000000000000000));
        want[i] = a[i] + b[i];
    }
    zxapp_arg_t cb[2] = {{"before", a, WAVE_ROWS}, {"moved", b, WAVE_ROWS}};
    CHECK(wave_same(app, "coffer.balance", cb, 2, want, WAVE_ROWS, false),
          "Mercer: coffer.balance, a credit room");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 4u ? wrand(1, 1000000) : wrand(-1, MERCER_AMOUNT_MAX + 1);
        b[i] = wrand(-2, INT64_C(100000000000000000));
        c[i] = wrand(-2, INT64_C(100000000000000000));
        d[i] = i % 3u ? 0 : wrand(-1, 1);
        want[i] = T_(a[i] >= 1 && a[i] <= MERCER_AMOUNT_MAX && b[i] >= 0 && c[i] >= 0 && d[i] == 0);
    }
    zxapp_arg_t ta[4] = {{"amount", a, WAVE_ROWS},
                         {"after", b, WAVE_ROWS},
                         {"room", c, WAVE_ROWS},
                         {"gap", d, WAVE_ROWS}};
    CHECK(wave_same(app, "coffer.transfer_ok", ta, 4, want, WAVE_ROWS, true),
          "Mercer: coffer.transfer_ok, a payment's limits");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, TSTK_QTY_MAX);
        b[i] = wrand(0, TSTK_PRICE_MAX);
        d[i] = wrand(1, TSTK_RATE_MAX);
        c[i] = wrand(0, d[i]);
        want[i] = tstk_line_total(a[i], b[i], c[i], d[i]);
    }
    zxapp_arg_t la[4] = {{"qty", a, WAVE_ROWS},
                         {"price", b, WAVE_ROWS},
                         {"taxn", c, WAVE_ROWS},
                         {"taxd", d, WAVE_ROWS}};
    CHECK(wave_same(app, "tallystick.taxed_line_total", la, 4, want, WAVE_ROWS, false),
          "Mercer: tallystick.taxed_line_total, a sale's amount");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1, TSTK_TOTAL_MAX + 1);
        b[i] = wrand(-2, INT64_C(100000000000000000));
        want[i] = T_(tstk_paid_in_full(a[i], b[i]));
    }
    zxapp_arg_t fa[2] = {{"total", a, WAVE_ROWS}, {"over", b, WAVE_ROWS}};
    CHECK(wave_same(app, "tallystick.paid_in_full", fa, 2, want, WAVE_ROWS, true),
          "Mercer: tallystick.paid_in_full, before a receipt");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, TSTK_NUMBER_MAX - 1);
        want[i] = tstk_next_number(a[i]);
    }
    zxapp_arg_t xa1[1] = {{"last", a, WAVE_ROWS}};
    CHECK(wave_same(app, "tallystick.next_doc_number", xa1, 1, want, WAVE_ROWS, false),
          "Mercer: tallystick.next_doc_number, sale and receipt numbers");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, FAIRGROUND_FEE_WITNESSED);
        want[i] = fairground_assurance_fee(a[i]);
    }
    zxapp_arg_t ea[1] = {{"amount", a, WAVE_ROWS}};
    CHECK(wave_same(app, "fairground.assurance_fee", ea, 1, want, WAVE_ROWS, false),
          "Mercer: fairground.assurance_fee, a listing's fee");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1, FAIRGROUND_NINE);
        b[i] = wrand(-1, FAIRGROUND_NINE);
        c[i] = wrand(-1, FAIRGROUND_NINE);
        d[i] = wrand(-1, FAIRGROUND_NINE);
        want[i] = T_(fairground_offer_allowed(a[i], b[i], c[i], d[i]));
    }
    zxapp_arg_t oa[4] = {{"price", a, WAVE_ROWS},
                         {"units", b, WAVE_ROWS},
                         {"priceroom", c, WAVE_ROWS},
                         {"unitsroom", d, WAVE_ROWS}};
    CHECK(wave_same(app, "fairground.offer_allowed", oa, 4, want, WAVE_ROWS, true),
          "Mercer: fairground.offer_allowed, may it be listed");
    {
        /* the day's sales: the price per unit across them, one exact fraction */
        int64_t num = 0, den = 0;
        for (uint32_t i = 0; i < 8u; i++) a[i] = wrand(1, 100000), b[i] = wrand(1, 50);
        CHECK(fairground_unit_price(a, b, 8, &num, &den), "Mercer: FR3 answers");
        zxapp_num_t o[WAVE_ROWS];
        uint32_t k = 0;
        int32_t lp = -1;
        zxapp_arg_t ua[2] = {{"price", a, 8}, {"units", b, 8}};
        int rc = zxapp_call_witnessed(app, &H, "fairground.offer_unit_price", ua, 2, o, WAVE_ROWS,
                                      &k, &lp);
        CHECK(rc == ZXAPP_OK && k == 1 && o[0].num == num && o[0].den == den,
              "Mercer: fairground.offer_unit_price, %lld/%lld", (long long) o[0].num,
              (long long) o[0].den);
    }
    for (uint32_t i = 0; i < 8u; i++) {
        /* one row a call: the ALGOL 60 witness reads one row (zxapp_lingua.c L1) */
        int64_t am = i ? wrand(0, TALLY_AMOUNT_MAX) : 1250,
                rn = i ? wrand(1, TALLY_RATE_MAX) : 10873,
                rd = i ? wrand(1, TALLY_RATE_MAX) : 10000;
        int64_t w = tally_fx_amount(am, rn, rd);
        zxapp_arg_t xa[3] = {{"amount", &am, 1}, {"rnum", &rn, 1}, {"rden", &rd, 1}};
        CHECK((i || w == 1359) && wave_same(app, "tally.fx_amount", xa, 3, &w, 1, false),
              "Mercer: tally.fx_amount row %u, a second currency (%lld)", i, (long long) w);
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = 1, b[i] = 1, c[i] = wrand(0, MERCER_RATE_MAX), d[i] = wrand(1, MERCER_RATE_MAX);
        CHECK(tally_ratio(TALLY_ADD, a[i], b[i], c[i], d[i], &r[i]), "Mercer: TL1 row %u", i);
    }
    zxapp_arg_t ra[4] = {
        {"an", a, WAVE_ROWS}, {"ad", b, WAVE_ROWS}, {"bn", c, WAVE_ROWS}, {"bd", d, WAVE_ROWS}};
    CHECK(mercer_rat_rows(app, "tally.ratio_add", ra, 4, r, WAVE_ROWS),
          "Mercer: tally.ratio_add, the factor a margin puts on cost");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-2, INT64_C(100000000000000000));
        b[i] = wrand(-2, INT64_C(100000000000000000));
        c[i] = wrand(-2, TALISMAN_BPS_MAX);
        want[i] = T_(talisman_earned(a[i], b[i], c[i]));
    }
    zxapp_arg_t ka[3] = {{"cred", a, WAVE_ROWS}, {"bond", b, WAVE_ROWS}, {"uptime", c, WAVE_ROWS}};
    CHECK(wave_same(app, "talisman.earned", ka, 3, want, WAVE_ROWS, true),
          "Mercer: talisman.earned, the tier on a listing");

    /* MR6, the BOTH test: the till rings sale 88 as 3 units for 1500, the
     * buyer's receipt says 2 units for 1000. Both amounts and the number
     * come from the composed entries; the sale is BOTH, never TRUE, so it
     * is held and not posted until the owner rules. */
    {
        static const int64_t last[2] = {86, 87}, qty[2] = {3, 2}, price[2] = {500, 500},
                             taxn[2] = {0, 0}, taxd[2] = {1, 1};
        int64_t nw[2] = {87, 88}, amt[2] = {1500, 1000};
        zxapp_arg_t na2[1] = {{"last", last, 2}};
        zxapp_arg_t qa[4] = {
            {"qty", qty, 2}, {"price", price, 2}, {"taxn", taxn, 2}, {"taxd", taxd, 2}};
        CHECK(wave_same(app, "tallystick.next_doc_number", na2, 1, nw, 2, false) &&
                  wave_same(app, "tallystick.taxed_line_total", qa, 4, amt, 2, false),
              "Mercer: sale 88: the till's 3 for 1500, the receipt's 2 for 1000");
        lpres_state_t s = mercer_sale_state(true, qty[0], amt[0], qty[1], amt[1]);
        CHECK(s == LPRES_STATE_BOTH && (int) s == 3 && s != LPRES_STATE_TRUE &&
                  lpres_conjoin(s, LPRES_STATE_TRUE) == LPRES_STATE_BOTH,
              "Mercer: the till and the receipt disagree: BOTH, held");
        CHECK(mercer_sale_state(true, 3, 1500, 3, 1500) == LPRES_STATE_TRUE &&
                  mercer_sale_state(false, 3, 1500, 0, 0) == LPRES_STATE_TRUE &&
                  mercer_sale_state(true, 3, 1500, 3, 1000) == LPRES_STATE_BOTH &&
                  mercer_receipt_says(true, 3, 1500, 2, 1500) == LPRES_STATE_FALSE,
              "Mercer: an agreeing receipt or none leaves the till's TRUE; any difference is "
              "BOTH");
    }
}
