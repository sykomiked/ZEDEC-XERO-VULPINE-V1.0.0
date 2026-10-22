/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Sealwax's languages against the C its model runs (sealwax_rules.h).
 * Slots: @logic.sealwax_sign_ready, @num.sealwax_fill_order,
 * @logic.sealwax_field_record_ok, and, used as they are,
 * @num.furrow_money_cell and @num.coffer_holdings. */
#include "sealwax_rules.h"

/* A single-language entry that refuses every language it has. */
static bool sealwax_refused(zxapp_t *a, const char *entry, const zxapp_arg_t *args, uint32_t n)
{
    zxapp_num_t out[WAVE_ROWS];
    uint32_t got = 0;
    int32_t lp = -1;
    return zxapp_call_witnessed(a, &H, entry, args, n, out, WAVE_ROWS, &got, &lp) ==
           ZXAPP_E_RUNTIME;
}

APP_TEST(sealwax, 15, 47)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        g[WAVE_ROWS], h[WAVE_ROWS], want[WAVE_ROWS];

    /* sign_ready (SR3): Eve = KRON = C, the edges first, then swept */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 40);
        b[i] = wrand(0, 3) ? 0 : wrand(0, 5);
        c[i] = wrand(0, 3) ? 0 : wrand(0, 5);
    }
    static const int64_t EDGE[][3] = {{1, 0, 0},  {32, 0, 0}, {33, 0, 0},  {0, 0, 0},
                                      {5, 1, 0},  {5, 0, 1},  {5, 32, 32}, {2147483647, 0, 0},
                                      {-1, 0, 0}, {16, 0, 0}};
    for (uint32_t i = 0; i < sizeof EDGE / sizeof EDGE[0]; i++)
        a[i] = EDGE[i][0], b[i] = EDGE[i][1], c[i] = EDGE[i][2];
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(sealwax_sign_ready(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              !sealwax_sign_ready(5, -1, 0) && !sealwax_sign_ready(5, 0, -1),
          "Sealwax: the C rule's own edges (SR3)");
    zxapp_arg_t ra[3] = {
        {"fields", a, WAVE_ROWS}, {"missing", b, WAVE_ROWS}, {"outside", c, WAVE_ROWS}};
    CHECK(wave_same(app, "sign_ready", ra, 3, want, WAVE_ROWS, true),
          "Sealwax: sign_ready, Eve = KRON = C");
    {
        /* a count below zero: Eve's record refuses it, KRON reads it as below one */
        static const int64_t five[2] = {5, 5}, zero[2] = {0, 0}, neg[2] = {0, -1};
        zxapp_arg_t ma[3] = {{"fields", five, 2}, {"missing", neg, 2}, {"outside", zero, 2}};
        CHECK(wave_lp(app, "sign_ready", ma, 3) == ZXAPP_BOTH,
              "Sealwax: missing -1: Eve FALSE, KRON TRUE: BOTH");
        zxapp_arg_t oa[3] = {{"fields", five, 2}, {"missing", zero, 2}, {"outside", neg, 2}};
        CHECK(wave_lp(app, "sign_ready", oa, 3) == ZXAPP_BOTH,
              "Sealwax: outside -1: Eve FALSE, KRON TRUE: BOTH");
    }

    /* an amount field (SR4) through furrow.money_cell: Sutra = KRON = the
     * model's C over a form's domain, swept, then the edges */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, SEALWAX_UNITS_MAX);
        b[i] = wrand(0, 99);
    }
    a[0] = 0, b[0] = 0;
    a[1] = SEALWAX_UNITS_MAX, b[1] = 99;
    a[2] = 1, b[2] = 0;
    a[3] = 0, b[3] = 1;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = sealwax_amount_minor(a[i], b[i]);
    CHECK(want[1] == SEALWAX_BOUND_MAX && sealwax_amount_minor(-1, 0) == SEALWAX_REFUSED &&
              sealwax_amount_minor(0, 100) == SEALWAX_REFUSED &&
              sealwax_amount_minor(0, -1) == SEALWAX_REFUSED &&
              sealwax_amount_minor(-12, -34) == SEALWAX_REFUSED &&
              sealwax_amount_minor(SEALWAX_UNITS_MAX + 1, 0) == SEALWAX_REFUSED,
          "Sealwax: the C rule's own edges (SR4): never negative, cents 0..99");
    zxapp_arg_t aa[2] = {{"units", a, WAVE_ROWS}, {"cents", b, WAVE_ROWS}};
    CHECK(wave_same(app, "furrow.money_cell", aa, 2, want, WAVE_ROWS, false),
          "Sealwax: an amount field through furrow.money_cell, Sutra = KRON = C");
    {
        /* cents past 99: Furrow's Sutra refuses, its KRON answers: held */
        static const int64_t u1[2] = {1, 1}, c100[2] = {5, 100};
        zxapp_arg_t x1[2] = {{"units", u1, 2}, {"cents", c100, 2}};
        CHECK(wave_lp(app, "furrow.money_cell", x1, 2) == ZXAPP_BOTH,
              "Sealwax: cents 100: Sutra refuses, KRON answers: BOTH");
        /* Furrow's money is signed and answers a negative amount, so the
         * refusal is the model's own C (SR4) */
        static const int64_t un[2] = {-12, 0}, cn[2] = {-34, -1};
        static const int64_t wn[2] = {-1234, -1};
        zxapp_arg_t x2[2] = {{"units", un, 2}, {"cents", cn, 2}};
        CHECK(wave_same(app, "furrow.money_cell", x2, 2, wn, 2, false) &&
                  sealwax_amount_minor(un[0], cn[0]) == SEALWAX_REFUSED &&
                  sealwax_amount_minor(un[1], cn[1]) == SEALWAX_REFUSED,
              "Sealwax: Furrow answers a negative amount; the model's C refuses it");
    }

    /* fill_order (SR5): BASIC (Hatchling) = C, one form per call, 64 calls */
    static const int64_t FEDGE[][3] = {
        {1, 0, 0},           {1, 1, 1},
        {32, 4294967295, 0}, {32, 0, 4294967295},
        {5, 5, 0},           {5, 5, 1},
        {4, 10, 2},          {32, 2863311530, 1431655765},
        {3, 7, 0},           {32, 4294967295, 4294967294},
    };
    bool all = true;
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t n[1], q[1], f[1], w[SEALWAX_FIELDS];
        if (r < sizeof FEDGE / sizeof FEDGE[0]) {
            n[0] = FEDGE[r][0], q[0] = FEDGE[r][1], f[0] = FEDGE[r][2];
        } else {
            n[0] = wrand(1, SEALWAX_FIELDS);
            int64_t top = ((int64_t) 1 << n[0]) - 1;
            q[0] = wrand(0, top), f[0] = wrand(0, top);
        }
        int32_t k = sealwax_fill_order(n[0], q[0], f[0], w);
        zxapp_arg_t fa[3] = {{"count", n, 1}, {"required", q, 1}, {"filled", f, 1}};
        if (k < 1 || !wave_same(app, "fill_order", fa, 3, w, (uint32_t) k, false)) {
            all = false;
            printf("    fill_order count %lld required %lld filled %lld\n", (long long) n[0],
                   (long long) q[0], (long long) f[0]);
        }
    }
    CHECK(all, "Sealwax: fill_order, BASIC = C, 64 forms");
    {
        int64_t w[SEALWAX_FIELDS];
        CHECK(sealwax_fill_order(5, 5, 0, w) == 5 && w[0] == 1 && w[1] == 3 && w[2] == 2 &&
                  w[4] == 5 && sealwax_fill_order(5, 0, 31, w) == 1 && w[0] == 0 &&
                  sealwax_fill_order(0, 0, 0, w) == SEALWAX_REFUSED &&
                  sealwax_fill_order(33, 0, 0, w) == SEALWAX_REFUSED &&
                  sealwax_fill_order(3, 8, 0, w) == SEALWAX_REFUSED &&
                  sealwax_fill_order(3, 0, -1, w) == SEALWAX_REFUSED,
              "Sealwax: the C rule's own edges (SR5)");
        static const int64_t n33[1] = {33}, n3[1] = {3}, z[1] = {0}, m8[1] = {8}, neg[1] = {-1};
        zxapp_arg_t x1[3] = {{"count", n33, 1}, {"required", z, 1}, {"filled", z, 1}};
        zxapp_arg_t x2[3] = {{"count", n3, 1}, {"required", m8, 1}, {"filled", z, 1}};
        zxapp_arg_t x3[3] = {{"count", n3, 1}, {"required", z, 1}, {"filled", neg, 1}};
        CHECK(sealwax_refused(app, "fill_order", x1, 3) &&
                  sealwax_refused(app, "fill_order", x2, 3) &&
                  sealwax_refused(app, "fill_order", x3, 3),
              "Sealwax: 33 fields, a mask past the fields, a negative mask: refused");
    }

    /* field_record_ok (SR6): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1200000000, 1200000000);
        b[i] = wrand(-120, 120);
        c[i] = wrand(-12, 12);
        d[i] = wrand(-12, 12);
        e[i] = wrand(-1200000000000000, 1200000000000000);
        g[i] = wrand(-1200000000000000, 1200000000000000);
        h[i] = wrand(-1200, 1200);
    }
    static const int64_t REDGE[][7] = {
        {0, 0, 0, 0, 0, 0, 0},
        {999999999, 99, 9, 9, 999999999999999, 999999999999999, 999},
        {-999999999, -99, -9, -9, -999999999999999, -999999999999999, -999},
        {1000000000, 1, 1, 1, 0, 1, 1},
        {1, 100, 1, 1, 0, 1, 1},
        {1, 1, 10, 1, 0, 1, 1},
        {1, 1, 1, 10, 0, 1, 1},
        {1, 1, 1, 1, 1000000000000000, 1, 1},
        {1, 1, 1, 1, 0, -1000000000000000, 1},
        {1, 1, 1, 1, 0, 1, 1000},
        {7, 3, 2, 1, 0, 25000, 6},
        {INT64_MAX, 1, 1, 1, 0, 1, INT64_MIN},
    };
    for (uint32_t i = 0; i < sizeof REDGE / sizeof REDGE[0]; i++)
        a[i] = REDGE[i][0], b[i] = REDGE[i][1], c[i] = REDGE[i][2], d[i] = REDGE[i][3],
        e[i] = REDGE[i][4], g[i] = REDGE[i][5], h[i] = REDGE[i][6];
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(sealwax_record_fits(a[i], b[i], c[i], d[i], e[i], g[i], h[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_TRUE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE &&
              want[9] == ZXAPP_FALSE && want[10] == ZXAPP_TRUE && want[11] == ZXAPP_FALSE,
          "Sealwax: the C rule's own edges (SR6)");
    zxapp_arg_t fr[7] = {{"form", a, WAVE_ROWS}, {"field", b, WAVE_ROWS},
                         {"kind", c, WAVE_ROWS}, {"required", d, WAVE_ROWS},
                         {"lo", e, WAVE_ROWS},   {"hi", g, WAVE_ROWS},
                         {"len", h, WAVE_ROWS}};
    CHECK(wave_same(app, "field_record_ok", fr, 7, want, WAVE_ROWS, true),
          "Sealwax: a field makes a ^ZXSEALWAX record, MYRA = C");

    /* coffer.holdings (SR7): a form's total, the exact sum of its amount fields */
    for (uint32_t r = 0; r < 16; r++) {
        int64_t amt[SEALWAX_FIELDS], tot[1];
        uint32_t n = 1u + 2u * r;
        if (n > SEALWAX_FIELDS) n = SEALWAX_FIELDS;
        for (uint32_t i = 0; i < n; i++)
            amt[i] = sealwax_amount_minor(wrand(0, r ? 100000 : SEALWAX_UNITS_MAX), wrand(0, 99));
        CHECK(sealwax_total(amt, n, &tot[0]), "Sealwax: form %u's total in the C (SR7)", r);
        zxapp_arg_t ta[1] = {{"balance", amt, n}};
        CHECK(wave_same(app, "coffer.holdings", ta, 1, tot, 1, false),
              "Sealwax: form %u of %u amounts, its total through coffer.holdings", r, n);
    }
    {
        int64_t over[2] = {INT64_MAX, 1}, t = 0;
        CHECK(!sealwax_total(over, 2, &t) && !sealwax_total(NULL, 1, &t),
              "Sealwax: a total past 64 bits is refused, never wrapped (SR7)");
    }
}
