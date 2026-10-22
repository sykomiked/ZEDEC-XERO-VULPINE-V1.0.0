/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Tally's languages, witnessed, row by row against tally_rules.h, the C the
 * model runs. Slots: @num.tally_ratio_add, @num.tally_ratio_sub,
 * @num.tally_ratio_mul, @num.tally_ratio_div, @num.tally_fx_amount,
 * @num.tally_fx_rest, @num.tally_unit_convert; and @fin.npv, used. */
#include "tally_rules.h"

/* One witnessed call of a rational entry: lp must be want_lp, and when it
 * is TRUE (or NEITHER for one language) every row the exact reduced r[i]. */
static bool tally_rows(zxapp_t *a, const char *entry, const zxapp_arg_t *args, uint32_t nargs,
                       const rat_t *r, uint32_t rows, int32_t want_lp)
{
    zxapp_num_t o[WAVE_ROWS];
    uint32_t k = 0;
    int32_t lp = -1;
    int rc = zxapp_call_witnessed(a, &H, entry, args, nargs, o, WAVE_ROWS, &k, &lp);
    if (rc != ZXAPP_OK || lp != want_lp || k != rows) {
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

APP_TEST(tally, 16, 109)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], want[WAVE_ROWS];
    static rat_t r[WAVE_ROWS];
    static const char *const ops[4] = {"ratio_add", "ratio_sub", "ratio_mul", "ratio_div"};

    /* TL1: the four operations, Trebago = KRON = C, exact and reduced */
    for (int64_t op = TALLY_ADD; op <= TALLY_DIV; op++) {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = wrand(-1000000000, 1000000000);
            b[i] = wrand(1, 1000000000) * (wrand(0, 1) ? 1 : -1);
            c[i] = wrand(-1000000000, 1000000000);
            d[i] = wrand(1, 1000000000) * (wrand(0, 1) ? 1 : -1);
            if (c[i] == 0) c[i] = 1; /* a b of 0 is no divisor */
        }
        a[0] = 0, b[0] = 1, c[0] = 1, d[0] = 1; /* zero */
        a[1] = 1, b[1] = 3, c[1] = 1, d[1] = 6; /* thirds and sixths */
        a[2] = TALLY_PART_MAX, b[2] = 1, c[2] = TALLY_PART_MAX, d[2] = TALLY_PART_MAX; /* max */
        a[3] = -TALLY_PART_MAX, b[3] = TALLY_PART_MAX, c[3] = -1, d[3] = 1;            /* min */
        a[4] = 2, b[4] = -4, c[4] = -6, d[4] = 9;                                      /* signs */
        for (uint32_t i = 0; i < WAVE_ROWS; i++)
            CHECK(tally_ratio(op, a[i], b[i], c[i], d[i], &r[i]), "Tally: TL1 row %u answers", i);
        zxapp_arg_t ra[4] = {
            {"an", a, WAVE_ROWS}, {"ad", b, WAVE_ROWS}, {"bn", c, WAVE_ROWS}, {"bd", d, WAVE_ROWS}};
        CHECK(tally_rows(app, ops[op - 1], ra, 4, r, WAVE_ROWS, ZXAPP_TRUE),
              "Tally: %s, Trebago = KRON = C", ops[op - 1]);
    }
    {
        /* 1/3 stays 1/3: a third plus nothing, and a sixth plus a sixth */
        static const int64_t an[2] = {1, 1}, ad[2] = {3, 6}, bn[2] = {0, 1}, bd[2] = {5, 6};
        rat_t third[2] = {rat_make(1, 3), rat_make(1, 3)};
        zxapp_arg_t ta[4] = {{"an", an, 2}, {"ad", ad, 2}, {"bn", bn, 2}, {"bd", bd, 2}};
        CHECK(tally_rows(app, "ratio_add", ta, 4, third, 2, ZXAPP_TRUE), "Tally: 1/3 stays 1/3");
        /* a part one past 18 digits: Trebago refuses, KRON answers: held */
        static const int64_t big[2] = {1, TALLY_PART_MAX + 1}, neg[2] = {1, -TALLY_PART_MAX - 1};
        rat_t x;
        CHECK(!tally_ratio(TALLY_ADD, big[1], 1, 1, 1, &x) &&
                  !tally_ratio(TALLY_SUB, 1, 1, neg[1], 1, &x),
              "Tally: C refuses a part past 18 digits");
        static const int64_t one[2] = {1, 1};
        zxapp_arg_t pa[4] = {{"an", big, 2}, {"ad", one, 2}, {"bn", one, 2}, {"bd", one, 2}};
        for (uint32_t op = 0; op < 4; op++)
            CHECK(wave_lp(app, ops[op], pa, 4) == ZXAPP_BOTH, "Tally: %s, an past 18 digits: BOTH",
                  ops[op]);
        zxapp_arg_t na[4] = {{"an", one, 2}, {"ad", one, 2}, {"bn", one, 2}, {"bd", neg, 2}};
        CHECK(wave_lp(app, "ratio_sub", na, 4) == ZXAPP_BOTH,
              "Tally: bd past 18 digits below: BOTH");
        /* a denominator of 0, and b of 0 to divide by: both refuse, nothing to show */
        static const int64_t z[2] = {1, 0};
        zxapp_arg_t za[4] = {{"an", one, 2}, {"ad", z, 2}, {"bn", one, 2}, {"bd", one, 2}};
        CHECK(wave_lp(app, "ratio_add", za, 4) == ZXAPP_NEITHER, "Tally: ad 0: NEITHER");
        zxapp_arg_t zb[4] = {{"an", one, 2}, {"ad", one, 2}, {"bn", z, 2}, {"bd", one, 2}};
        CHECK(wave_lp(app, "ratio_div", zb, 4) == ZXAPP_NEITHER, "Tally: divide by 0: NEITHER");
        CHECK(!tally_ratio(TALLY_ADD, 1, 0, 1, 1, &x) && !tally_ratio(TALLY_DIV, 1, 1, 0, 1, &x) &&
                  !tally_ratio(5, 1, 1, 1, 1, &x) && !tally_ratio(0, 1, 1, 1, 1, &x),
              "Tally: C refuses a 0 denominator, a 0 divisor and no operation");
        /* past int64 / int64: both refuse */
        static const int64_t m[2] = {1, TALLY_PART_MAX};
        zxapp_arg_t ma[4] = {{"an", m, 2}, {"ad", one, 2}, {"bn", m, 2}, {"bd", one, 2}};
        CHECK(wave_lp(app, "ratio_mul", ma, 4) == ZXAPP_NEITHER &&
                  !tally_ratio(TALLY_MUL, TALLY_PART_MAX, 1, TALLY_PART_MAX, 1, &x),
              "Tally: a product past int64: refused by all");
    }

    /* TL3: fx_amount, one row per call (ALGOL reads one), Sutra = KRON = ALGOL = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t am = wrand(0, TALLY_AMOUNT_MAX), rn = wrand(1, TALLY_RATE_MAX),
                rd = wrand(1, TALLY_RATE_MAX);
        if (i == 0) am = 0, rn = 1, rd = 1;                             /* zero */
        if (i == 1) am = TALLY_AMOUNT_MAX, rn = TALLY_RATE_MAX, rd = 1; /* max */
        if (i == 2) am = 1, rn = 1, rd = TALLY_RATE_MAX;                /* least */
        if (i == 3) am = 1000, rn = 10873, rd = 10000;                  /* 1.0873 */
        int64_t w = tally_fx_amount(am, rn, rd);
        zxapp_arg_t fa[3] = {{"amount", &am, 1}, {"rnum", &rn, 1}, {"rden", &rd, 1}};
        CHECK(w >= 0 && wave_same(app, "fx_amount", fa, 3, &w, 1, false),
              "Tally: fx_amount row %u, Sutra = KRON = ALGOL = C", i);
    }
    {
        /* past Sutra's 9(12), or negative: Sutra and ALGOL refuse, KRON answers */
        int64_t am = TALLY_AMOUNT_MAX + 1, rn = 1, rd = 1;
        zxapp_arg_t fa[3] = {{"amount", &am, 1}, {"rnum", &rn, 1}, {"rden", &rd, 1}};
        CHECK(wave_lp(app, "fx_amount", fa, 3) == ZXAPP_BOTH &&
                  tally_fx_amount(am, rn, rd) == TALLY_REFUSED,
              "Tally: an amount past 9(12): BOTH");
        am = -1;
        CHECK(wave_lp(app, "fx_amount", fa, 3) == ZXAPP_BOTH &&
                  tally_fx_amount(am, rn, rd) == TALLY_REFUSED,
              "Tally: a negative amount: BOTH");
        /* a rate part of 0: refused by all three */
        am = 5, rn = 0;
        CHECK(wave_lp(app, "fx_amount", fa, 3) == ZXAPP_NEITHER &&
                  tally_fx_amount(am, rn, rd) == TALLY_REFUSED,
              "Tally: rnum 0: NEITHER");
        rn = 1, rd = 0;
        CHECK(wave_lp(app, "fx_amount", fa, 3) == ZXAPP_NEITHER &&
                  tally_fx_amount(am, rn, rd) == TALLY_REFUSED,
              "Tally: rden 0: NEITHER");
        rd = TALLY_RATE_MAX + 1;
        CHECK(wave_lp(app, "fx_amount", fa, 3) == ZXAPP_BOTH &&
                  tally_fx_amount(am, rn, rd) == TALLY_REFUSED,
              "Tally: rden past 9(6): BOTH");
    }

    /* TL4: fx_rest, Sutra = KRON = C, and TL3 * rden + TL4 is amount * rnum */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, TALLY_AMOUNT_MAX);
        b[i] = wrand(1, TALLY_RATE_MAX);
        c[i] = wrand(1, i < 32 ? 1000 : TALLY_RATE_MAX);
    }
    a[0] = 0, b[0] = 1, c[0] = 1;
    a[1] = TALLY_AMOUNT_MAX, b[1] = TALLY_RATE_MAX, c[1] = TALLY_RATE_MAX - 1;
    a[2] = 1, b[2] = 1, c[2] = 3;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        want[i] = tally_fx_remainder(a[i], b[i], c[i]);
        CHECK(want[i] >= 0 && want[i] < c[i] &&
                  tally_fx_amount(a[i], b[i], c[i]) * c[i] + want[i] == a[i] * b[i],
              "Tally: TL3 and TL4 make the whole product, row %u", i);
    }
    zxapp_arg_t xa[3] = {{"amount", a, WAVE_ROWS}, {"rnum", b, WAVE_ROWS}, {"rden", c, WAVE_ROWS}};
    CHECK(wave_same(app, "fx_rest", xa, 3, want, WAVE_ROWS, false),
          "Tally: fx_rest, Sutra = KRON = C");
    {
        static const int64_t am[2] = {1, TALLY_AMOUNT_MAX + 1}, rn[2] = {1, 1}, rd[2] = {3, 3};
        zxapp_arg_t oa[3] = {{"amount", am, 2}, {"rnum", rn, 2}, {"rden", rd, 2}};
        CHECK(wave_lp(app, "fx_rest", oa, 3) == ZXAPP_BOTH &&
                  tally_fx_remainder(am[1], 1, 3) == TALLY_REFUSED,
              "Tally: fx_rest past 9(12): Sutra refuses, KRON answers: BOTH");
    }

    /* TL2: unit_convert in KRON alone (NEITHER: one language), against C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1000000, 1000000);
        do {
            b[i] = wrand(0, TALLY_UNITS - 1);
            c[i] = wrand(0, TALLY_UNITS - 1);
        } while (tally_units[b[i]].dim != tally_units[c[i]].dim);
    }
    a[0] = 1, b[0] = 4, c[0] = 2;    /* an inch is 127/5000 m */
    a[1] = 1, b[1] = 5, c[1] = 4;    /* a foot is 12 inches */
    a[2] = 0, b[2] = 20, c[2] = 17;  /* nothing */
    a[3] = -3, b[3] = 20, c[3] = 19; /* negative */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        CHECK(tally_unit_convert(a[i], b[i], c[i], &r[i]), "Tally: TL2 row %u answers", i);
    CHECK(r[0].num == 127 && r[0].den == 5000 && r[1].num == 12 && r[1].den == 1 && r[3].num == -72,
          "Tally: TL2's factors are exact");
    zxapp_arg_t ua[3] = {{"qty", a, WAVE_ROWS}, {"src", b, WAVE_ROWS}, {"dst", c, WAVE_ROWS}};
    CHECK(tally_rows(app, "unit_convert", ua, 3, r, WAVE_ROWS, ZXAPP_NEITHER),
          "Tally: unit_convert, KRON = C");
    {
        /* a metre is no gram, and unit 21 is no unit: refused by KRON and C */
        static const int64_t q[2] = {1, 1}, s[2] = {2, 2}, t[2] = {2, 9}, u[2] = {2, 21};
        zxapp_arg_t da[3] = {{"qty", q, 2}, {"src", s, 2}, {"dst", t, 2}};
        zxapp_arg_t ia[3] = {{"qty", q, 2}, {"src", s, 2}, {"dst", u, 2}};
        zxapp_num_t o[WAVE_ROWS];
        uint32_t k = 0;
        int32_t lp = -1;
        rat_t x;
        CHECK(zxapp_call_witnessed(app, &H, "unit_convert", da, 3, o, WAVE_ROWS, &k, &lp) !=
                      ZXAPP_OK &&
                  !tally_unit_convert(1, 2, 9, &x),
              "Tally: units of two dimensions refused");
        CHECK(zxapp_call_witnessed(app, &H, "unit_convert", ia, 3, o, WAVE_ROWS, &k, &lp) !=
                      ZXAPP_OK &&
                  !tally_unit_convert(1, 2, TALLY_UNITS, &x) && !tally_unit_convert(1, -1, 2, &x),
              "Tally: no such unit refused");
    }

    /* pce_fin.npv, used (TM7): exact, one answer per rate, on the c runtime alone */
    {
        static const int64_t cf[3] = {-100, 0, 121}, rn[2] = {1, 0}, rd[2] = {10, 1};
        rat_t v[2] = {rat_make(0, 1), rat_make(21, 1)};
        zxapp_arg_t na[3] = {{"cf", cf, 3}, {"rate_num", rn, 2}, {"rate_den", rd, 2}};
        CHECK(tally_rows(app, "pce_fin.npv", na, 3, v, 2, ZXAPP_NEITHER),
              "Tally: npv of -100, 0, 121 is 0 at 1/10 and 21 at 0");
    }
}
