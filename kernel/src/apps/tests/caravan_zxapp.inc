/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Caravan's languages against the C its model runs (caravan_rules.h).
 * Slots: @num.caravan_charged, @num.caravan_girth, @logic.caravan_fits_ok,
 * @num.caravan_fits_n, @logic.caravan_ship_ok. */
#include "caravan_rules.h"

APP_TEST(caravan, 10, 90)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], want[WAVE_ROWS];

    /* charged (CR1): Trebago = Sutra = C, swept, with every edge */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, i < 32 ? 100000 : CARAVAN_GRAMS_MAX);
        b[i] = wrand(0, i % 2 ? 900 : 20000);
        c[i] = wrand(0, i % 2 ? 900 : 20000);
        d[i] = wrand(0, i % 2 ? 900 : 20000);
        e[i] = i % 2 ? wrand(1, 99999) : wrand(10000, 99999);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 5000;              /* nothing at all */
    a[1] = 500, b[1] = 300, c[1] = 200, d[1] = 100, e[1] = 5000;      /* the volume wins */
    a[2] = 2000, b[2] = 300, c[2] = 200, d[2] = 100, e[2] = 5000;     /* the mass wins */
    a[3] = 1200, b[3] = 300, c[3] = 200, d[3] = 100, e[3] = 5000;     /* a tie */
    a[4] = 0, b[4] = 301, c[4] = 201, d[4] = 101, e[4] = 5000;        /* 1222.1.. rounded up */
    a[5] = 0, b[5] = 1, c[5] = 1, d[5] = 1, e[5] = 99999;             /* any volume is 1 g */
    a[6] = CARAVAN_GRAMS_MAX, b[6] = 0, c[6] = 0, d[6] = 0, e[6] = 1; /* max mass */
    a[7] = 0, b[7] = 99999, c[7] = 99999, d[7] = 100, e[7] = 99999;   /* max sides */
    a[8] = 0, b[8] = 1000, c[8] = 1000, d[8] = 999, e[8] = 1;         /* 999000000 g */
    a[9] = 0, b[9] = 99999, c[9] = 9999, d[9] = 99999, e[9] = 99999;  /* near 9(9) grams */
    a[10] = 7, b[10] = 13, c[10] = 17, d[10] = 19, e[10] = 6000;      /* 0.7 g up to 1 g, mass 7 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        want[i] = caravan_charged(a[i], b[i], c[i], d[i], e[i]);
        CHECK(want[i] >= 0, "Caravan: charged row %u is inside the domain", i);
    }
    CHECK(want[0] == 0 && want[1] == 1200 && want[2] == 2000 && want[3] == 1200 &&
              want[4] == 1223 && want[5] == 1 && want[6] == CARAVAN_GRAMS_MAX &&
              want[7] == 9999900 && want[8] == 999000000 && want[9] == 999890001 && want[10] == 7,
          "Caravan: charged, the C's own edges");
    zxapp_arg_t ca[5] = {{"mass", a, WAVE_ROWS},
                         {"length", b, WAVE_ROWS},
                         {"width", c, WAVE_ROWS},
                         {"height", d, WAVE_ROWS},
                         {"divisor", e, WAVE_ROWS}};
    CHECK(wave_same(app, "charged", ca, 5, want, WAVE_ROWS, false),
          "Caravan: charged, Trebago = Sutra = C");
    {
        /* a side past 9(5): Sutra refuses, Trebago answers 400000 g */
        static const int64_t m[2] = {500, 500}, l[2] = {300, 100000}, w[2] = {200, 200},
                             h[2] = {100, 100}, v[2] = {5000, 5000};
        zxapp_arg_t xa[5] = {
            {"mass", m, 2}, {"length", l, 2}, {"width", w, 2}, {"height", h, 2}, {"divisor", v, 2}};
        CHECK(caravan_charged(500, 100000, 200, 100, 5000) == CARAVAN_REFUSED &&
                  wave_lp(app, "charged", xa, 5) == ZXAPP_BOTH,
              "Caravan: a side past 9(5): Sutra refuses, Trebago answers: BOTH");
        /* a charged weight past 9(9) grams: Sutra refuses, Trebago answers 10^11 */
        static const int64_t l2[2] = {300, 10000}, w2[2] = {200, 10000}, h2[2] = {100, 1000},
                             v2[2] = {5000, 1};
        zxapp_arg_t ya[5] = {{"mass", m, 2},
                             {"length", l2, 2},
                             {"width", w2, 2},
                             {"height", h2, 2},
                             {"divisor", v2, 2}};
        CHECK(caravan_charged(500, 10000, 10000, 1000, 1) == CARAVAN_REFUSED &&
                  wave_lp(app, "charged", ya, 5) == ZXAPP_BOTH,
              "Caravan: past 9(9) grams: Sutra refuses, Trebago answers: BOTH");
        /* a negative mass: Sutra's picture refuses it, Trebago's guard too: NEITHER */
        static const int64_t mn[2] = {-1, -1};
        zxapp_arg_t na[5] = {{"mass", mn, 2},
                             {"length", l, 2},
                             {"width", w, 2},
                             {"height", h, 2},
                             {"divisor", v, 2}};
        na[1].v = m; /* sides inside 9(5) */
        CHECK(caravan_charged(-1, 500, 200, 100, 5000) == CARAVAN_REFUSED &&
                  wave_lp(app, "charged", na, 5) == ZXAPP_NEITHER,
              "Caravan: a negative mass: both refuse: NEITHER");
        /* a divisor of 0: both refuse the division */
        static const int64_t z[2] = {0, 0};
        zxapp_arg_t za[5] = {
            {"mass", m, 2}, {"length", m, 2}, {"width", w, 2}, {"height", h, 2}, {"divisor", z, 2}};
        CHECK(caravan_charged(500, 500, 200, 100, 0) == CARAVAN_REFUSED &&
                  wave_lp(app, "charged", za, 5) == ZXAPP_NEITHER,
              "Caravan: a divisor of 0: both refuse: NEITHER");
    }

    /* girth (CR2): Sutra = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, CARAVAN_SIDE_MAX);
        b[i] = wrand(0, i < 32 ? 2000 : CARAVAN_SIDE_MAX);
    }
    a[0] = 0, b[0] = 0;                               /* flat */
    a[1] = 200, b[1] = 100;                           /* a shoebox's 600 */
    a[2] = CARAVAN_SIDE_MAX, b[2] = CARAVAN_SIDE_MAX; /* max: 399996 */
    a[3] = 1, b[3] = 0;                               /* the least */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = caravan_girth(a[i], b[i]);
    CHECK(want[1] == 600 && want[2] == 399996 && want[3] == 2, "Caravan: girth, the C's edges");
    zxapp_arg_t ga[2] = {{"width", a, WAVE_ROWS}, {"height", b, WAVE_ROWS}};
    CHECK(wave_same(app, "girth", ga, 2, want, WAVE_ROWS, false),
          "Caravan: girth, Sutra = KRON = C");
    {
        /* max + 1 past 9(5), and a negative side: Sutra refuses, KRON answers */
        static const int64_t w[2] = {200, 100000}, h[2] = {100, 100}, n[2] = {200, -1};
        zxapp_arg_t wa[2] = {{"width", w, 2}, {"height", h, 2}};
        CHECK(caravan_girth(100000, 100) == CARAVAN_REFUSED &&
                  wave_lp(app, "girth", wa, 2) == ZXAPP_BOTH,
              "Caravan: a side past 9(5): Sutra refuses, KRON answers: BOTH");
        zxapp_arg_t nb[2] = {{"width", n, 2}, {"height", h, 2}};
        CHECK(caravan_girth(-1, 100) == CARAVAN_REFUSED &&
                  wave_lp(app, "girth", nb, 2) == ZXAPP_BOTH,
              "Caravan: a negative side: Sutra refuses, KRON answers: BOTH");
    }

    /* fits_ok (CR3) and fits_n (CR4): Eve = KRON = C and Trebago = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 4 == 1 ? -wrand(1, 1000) : wrand(0, CARAVAN_GRAMS_MAX);
        b[i] = i % 4 == 2 ? -wrand(1, 1000) : wrand(0, CARAVAN_SIDE_MAX);
        c[i] = i % 4 == 3 ? -wrand(1, 1000) : wrand(0, CARAVAN_LG_MAX);
    }
    a[0] = 0, b[0] = 0, c[0] = 0; /* exactly at every limit: inside */
    a[1] = CARAVAN_GRAMS_MAX, b[1] = CARAVAN_SIDE_MAX, c[1] = CARAVAN_LG_MAX; /* every max */
    a[2] = -1, b[2] = 0, c[2] = 0;                                            /* 1 g over */
    a[3] = 0, b[3] = -1, c[3] = 0;                                            /* 1 mm too long */
    a[4] = 0, b[4] = 0, c[4] = -1;                                            /* 1 mm too round */
    a[5] = -1, b[5] = -1, c[5] = -1;                                          /* over everything */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        want[i] = T_(caravan_fits(a[i], b[i], c[i]));
        d[i] = caravan_fits_n(a[i], b[i], c[i]);
    }
    zxapp_arg_t fa[3] = {
        {"massleft", a, WAVE_ROWS}, {"lengthleft", b, WAVE_ROWS}, {"girthleft", c, WAVE_ROWS}};
    CHECK(wave_same(app, "fits_ok", fa, 3, want, WAVE_ROWS, true),
          "Caravan: fits_ok, Eve = KRON = C");
    CHECK(wave_same(app, "fits_n", fa, 3, d, WAVE_ROWS, false),
          "Caravan: fits_n, Trebago = KRON = C");
    {
        /* a margin past its range is no measurement: Eve and Trebago say no, KRON yes */
        static const int64_t z[2] = {0, 0}, big[2] = {0, CARAVAN_LG_MAX + 1},
                             heavy[2] = {0, CARAVAN_GRAMS_MAX + 1},
                             far[2] = {0, CARAVAN_SIDE_MAX + 1};
        zxapp_arg_t ga2[3] = {{"massleft", z, 2}, {"lengthleft", z, 2}, {"girthleft", big, 2}};
        CHECK(!caravan_fits(0, 0, CARAVAN_LG_MAX + 1) &&
                  wave_lp(app, "fits_ok", ga2, 3) == ZXAPP_BOTH,
              "Caravan: length and girth past range: Eve FALSE, KRON TRUE: BOTH");
        zxapp_arg_t ma[3] = {{"massleft", heavy, 2}, {"lengthleft", z, 2}, {"girthleft", z, 2}};
        CHECK(caravan_fits_n(CARAVAN_GRAMS_MAX + 1, 0, 0) == 0 &&
                  wave_lp(app, "fits_n", ma, 3) == ZXAPP_BOTH,
              "Caravan: a mass margin past range: Trebago 0, KRON 1: BOTH");
        zxapp_arg_t la[3] = {{"massleft", z, 2}, {"lengthleft", far, 2}, {"girthleft", z, 2}};
        CHECK(wave_lp(app, "fits_ok", la, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "fits_n", la, 3) == ZXAPP_BOTH,
              "Caravan: a length margin past range: held both ways");
    }

    /* ship_ok (CR5): MYRA = Eve = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, CARAVAN_SHIP_MAX);
        b[i] = wrand(1, CARAVAN_CARRIER_MAX);
        c[i] = wrand(0, CARAVAN_GRAMS_MAX);
        d[i] = wrand(0, CARAVAN_GRAMS_MAX);
        e[i] = wrand(CARAVAN_DRAFTED, CARAVAN_RETURNED);
        f[i] = wrand(0, CARAVAN_STAMP_MAX);
    }
    a[0] = 1, b[0] = 1, c[0] = 0, d[0] = 0, e[0] = 0, f[0] = 0; /* every least */
    a[1] = CARAVAN_SHIP_MAX, b[1] = CARAVAN_CARRIER_MAX, c[1] = CARAVAN_GRAMS_MAX,
    d[1] = CARAVAN_GRAMS_MAX, e[1] = CARAVAN_RETURNED, f[1] = CARAVAN_STAMP_MAX; /* every max */
    a[2] = CARAVAN_SHIP_MAX + 1;                                                 /* ship max + 1 */
    b[3] = CARAVAN_CARRIER_MAX + 1;                                              /* carrier + 1 */
    c[4] = CARAVAN_GRAMS_MAX + 1;                                                /* mass + 1 */
    d[5] = CARAVAN_GRAMS_MAX + 1;                                                /* charged + 1 */
    f[6] = CARAVAN_STAMP_MAX + 1;                                                /* stamp + 1 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(caravan_ship_ok(a[i], b[i], c[i], d[i], e[i], f[i]));
    zxapp_arg_t sa[6] = {{"ship", a, WAVE_ROWS},   {"carrier", b, WAVE_ROWS},
                         {"mass", c, WAVE_ROWS},   {"charged", d, WAVE_ROWS},
                         {"status", e, WAVE_ROWS}, {"stamp", f, WAVE_ROWS}};
    CHECK(wave_same(app, "ship_ok", sa, 6, want, WAVE_ROWS, true),
          "Caravan: ship_ok, MYRA = Eve = C");
    {
        /* status 7 fits MYRA's one digit but is no status Caravan knows: held */
        static const int64_t s[2] = {1, 1}, k[2] = {1, 1}, m[2] = {500, 500}, g[2] = {1200, 1200},
                             st[2] = {0, 7}, t[2] = {1700000000, 1700000000};
        zxapp_arg_t xa[6] = {{"ship", s, 2},    {"carrier", k, 2}, {"mass", m, 2},
                             {"charged", g, 2}, {"status", st, 2}, {"stamp", t, 2}};
        CHECK(!caravan_ship_ok(1, 1, 500, 1200, 7, 1700000000) &&
                  wave_lp(app, "ship_ok", xa, 6) == ZXAPP_BOTH,
              "Caravan: an unknown status: MYRA TRUE, Eve FALSE: BOTH");
    }
}
