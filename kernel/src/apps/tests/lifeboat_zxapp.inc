/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Lifeboat's languages against the C its model runs (lifeboat_rules.h).
 * Slots: @logic.lifeboat_start_ok, @num.lifeboat_start_n,
 * @num.lifeboat_repair_step, @logic.lifeboat_restore_fits,
 * @logic.lifeboat_recovery_ok, and how full the disk is through
 * @num.used_x100. */
#include "lifeboat_rules.h"

/* one row of four check results: journal books room tested */
static bool lb_row(zxapp_t *app, const char *entry, int64_t j, int64_t b, int64_t r, int64_t t,
                   int64_t want)
{
    int64_t ja[1] = {j}, ba[1] = {b}, ra[1] = {r}, ta[1] = {t}, w[1] = {want};
    zxapp_arg_t a[4] = {{"journal", ja, 1}, {"books", ba, 1}, {"room", ra, 1}, {"tested", ta, 1}};
    if (wave_same(app, entry, a, 4, w, 1, false)) return true;
    printf("    %s journal %lld books %lld room %lld tested %lld\n", entry, (long long) j,
           (long long) b, (long long) r, (long long) t);
    return false;
}

static int32_t lb_one(zxapp_t *app, const char *entry, int64_t j, int64_t b, int64_t r, int64_t t)
{
    int64_t ja[1] = {j}, ba[1] = {b}, ra[1] = {r}, ta[1] = {t};
    zxapp_arg_t a[4] = {{"journal", ja, 1}, {"books", ba, 1}, {"room", ra, 1}, {"tested", ta, 1}};
    return wave_lp(app, entry, a, 4);
}

APP_TEST(lifeboat, 11, 11)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], g[WAVE_ROWS], want[WAVE_ROWS];
    const int64_t MAX = LIFEBOAT_BYTES_MAX;

    /* the C rules' own edges (LB2-LB5) */
    CHECK(lifeboat_start_ok(1, 1, 1, 1) && lifeboat_start_ok(1, 1, MAX, 1) &&
              !lifeboat_start_ok(1, 1, 0, 1) && !lifeboat_start_ok(1, 1, MAX + 1, 1) &&
              !lifeboat_start_ok(2, 1, 5, 1) && !lifeboat_start_ok(1, 1, 5, -1) &&
              lifeboat_start_n(1, 1, 5, 1) == 1 && lifeboat_start_n(0, 1, 5, 1) == 0 &&
              lifeboat_start_n(1, 1, -MAX, 1) == 0 &&
              lifeboat_start_n(1, 1, -MAX - 1, 1) == LIFEBOAT_REFUSED &&
              lifeboat_start_n(1, 2, 5, 1) == LIFEBOAT_REFUSED &&
              lifeboat_repair_step(1, 1, 5, 1) == LIFEBOAT_STEP_NONE &&
              lifeboat_repair_step(0, 0, 0, 0) == LIFEBOAT_STEP_SPACE &&
              lifeboat_repair_step(0, 0, 1, 0) == LIFEBOAT_STEP_TEST &&
              lifeboat_repair_step(0, 0, 1, 1) == LIFEBOAT_STEP_RESTORE &&
              lifeboat_repair_step(0, 1, 1, 1) == LIFEBOAT_STEP_JOURNAL &&
              lifeboat_repair_step(1, 1, 5, 2) == LIFEBOAT_REFUSED &&
              lifeboat_restore_fits(0, 0, 0) && lifeboat_restore_fits(1, MAX, MAX - 1) &&
              !lifeboat_restore_fits(1, MAX, MAX) && !lifeboat_restore_fits(-1, 5, 0) &&
              !lifeboat_restore_fits(101, 200, 100) && lifeboat_restore_fits(100, 200, 100),
          "Lifeboat: the C rules' own edges (LB2-LB5)");

    /* start_ok (LB2): Eve = KRON = C, every flag pattern, rooms at their edges, then swept */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i < 16 ? (i >> 3) & 1 : wrand(0, 1);
        b[i] = i < 16 ? (i >> 2) & 1 : wrand(0, 1);
        d[i] = i < 16 ? (i >> 1) & 1 : wrand(0, 1);
        c[i] = i < 16 ? (i & 1 ? 5 : 0) : wrand(-1000000, MAX);
        if (i > 15 && i % 3 == 0) a[i] = b[i] = d[i] = 1; /* more safe rows */
    }
    c[16] = 1, c[17] = MAX, c[18] = -MAX, c[19] = 0, c[20] = -1;
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(lifeboat_start_ok(a[i], b[i], c[i], d[i]));
    zxapp_arg_t sa[4] = {{"journal", a, WAVE_ROWS},
                         {"books", b, WAVE_ROWS},
                         {"room", c, WAVE_ROWS},
                         {"tested", d, WAVE_ROWS}};
    CHECK(want[15] == ZXAPP_TRUE && want[14] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE &&
              wave_same(app, "start_ok", sa, 4, want, WAVE_ROWS, true),
          "Lifeboat: start_ok, Eve = KRON = C");
    {
        /* a flag of 2, or a room past 18 digits: Eve's record refuses it, KRON reads it as yes */
        static const int64_t one[2] = {1, 1}, two[2] = {1, 2}, five[2] = {5, 5},
                             big[2] = {5, 1000000000000000000};
        zxapp_arg_t ta[4] = {
            {"journal", two, 2}, {"books", one, 2}, {"room", five, 2}, {"tested", one, 2}};
        CHECK(wave_lp(app, "start_ok", ta, 4) == ZXAPP_BOTH,
              "Lifeboat: journal 2: Eve FALSE, KRON TRUE: BOTH");
        zxapp_arg_t ra[4] = {
            {"journal", one, 2}, {"books", one, 2}, {"room", big, 2}, {"tested", one, 2}};
        CHECK(wave_lp(app, "start_ok", ra, 4) == ZXAPP_BOTH,
              "Lifeboat: a room past 18 digits: Eve FALSE, KRON TRUE: BOTH");
    }

    /* start_n (LB3) and repair_step (LB4): Forth (Cairndrake) = KRON = C and
     * BASIC (Hatchling) = KRON = C, one row per call, 64 calls each */
    static const int64_t EDGE[][4] = {
        {1, 1, 1, 1},
        {1, 1, 0, 1},
        {0, 0, 0, 0},
        {0, 0, 1, 0},
        {0, 0, 1, 1},
        {0, 1, 1, 1},
        {1, 1, 999999999999999999, 1},
        {1, 1, -999999999999999999, 1},
        {1, 0, 5, 0},
        {1, 1, -1, 0},
        {0, 1, 5, 0},
        {1, 0, 5, 1},
    };
    bool sn = true, rs = true;
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t j, k, m, t;
        if (r < sizeof EDGE / sizeof EDGE[0]) {
            j = EDGE[r][0], k = EDGE[r][1], m = EDGE[r][2], t = EDGE[r][3];
        } else {
            j = wrand(0, 1), k = wrand(0, 1), m = wrand(-MAX, MAX), t = wrand(0, 1);
        }
        sn = lb_row(app, "start_n", j, k, m, t, lifeboat_start_n(j, k, m, t)) && sn;
        rs = lb_row(app, "repair_step", j, k, m, t, lifeboat_repair_step(j, k, m, t)) && rs;
    }
    CHECK(sn, "Lifeboat: start_n, Forth = KRON = C, 64 rows");
    CHECK(rs, "Lifeboat: repair_step, BASIC = KRON = C, 64 rows");
    for (uint32_t r = 0; r < 16; r++) {
        int64_t j = (r >> 3) & 1, k = (r >> 2) & 1, m = (r >> 1) & 1 ? 7 : -7, t = r & 1;
        CHECK((lifeboat_repair_step(j, k, m, t) == 0) == lifeboat_start_ok(j, k, m, t) &&
                  lifeboat_start_n(j, k, m, t) == (lifeboat_start_ok(j, k, m, t) ? 1 : 0),
              "Lifeboat: step 0 exactly when safe, pattern %u", r);
    }
    CHECK(lb_one(app, "start_n", 2, 1, 5, 1) == ZXAPP_BOTH &&
              lb_one(app, "start_n", 1, 1, 1000000000000000000, 1) == ZXAPP_BOTH &&
              lb_one(app, "start_n", 1, -1, 5, 1) == ZXAPP_BOTH,
          "Lifeboat: start_n past its domain: Forth refuses, KRON answers: BOTH");
    CHECK(lb_one(app, "repair_step", 1, 1, 5, 2) == ZXAPP_BOTH &&
              lb_one(app, "repair_step", 1, -1, 5, 1) == ZXAPP_BOTH &&
              lb_one(app, "repair_step", 1, 1, -1000000000000000000, 1) == ZXAPP_BOTH,
          "Lifeboat: repair_step past its domain: BASIC refuses, KRON answers: BOTH");

    /* restore_fits (LB5): Sutra = KRON = C, inside 9(18) */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        e[i] = wrand(0, 999999999999);                                   /* reserve */
        a[i] = wrand(0, 999999999999);                                   /* need */
        b[i] = i % 2 ? a[i] + e[i] + wrand(-1000, 1000) : wrand(0, MAX); /* free, near the edge */
        if (b[i] < 0) b[i] = 0;
    }
    a[0] = 0, b[0] = 0, e[0] = 0;
    a[1] = MAX - 1, b[1] = MAX, e[1] = 1;
    a[2] = MAX, b[2] = MAX, e[2] = 0;
    a[3] = 0, b[3] = MAX, e[3] = MAX;
    a[4] = 1, b[4] = MAX - 1, e[4] = MAX - 1;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(lifeboat_restore_fits(a[i], b[i], e[i]));
    zxapp_arg_t fa[3] = {{"need", a, WAVE_ROWS}, {"free", b, WAVE_ROWS}, {"reserve", e, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[4] == ZXAPP_FALSE &&
              wave_same(app, "restore_fits", fa, 3, want, WAVE_ROWS, true),
          "Lifeboat: restore_fits, Sutra = KRON = C");
    {
        /* past 9(18): Sutra refuses the picture or the sum, KRON compares */
        static const int64_t n1[2] = {1, 1}, nbig[2] = {1, 999999999999999999}, nneg[2] = {1, -1},
                             f10[2] = {10, 10}, fbig[2] = {10, 999999999999999999}, r1[2] = {1, 1},
                             rbig[2] = {1, 1000000000000000000};
        zxapp_arg_t xa[3] = {{"need", n1, 2}, {"free", f10, 2}, {"reserve", rbig, 2}};
        CHECK(wave_lp(app, "restore_fits", xa, 3) == ZXAPP_BOTH,
              "Lifeboat: a reserve past 18 digits: Sutra refuses, KRON answers: BOTH");
        zxapp_arg_t sa2[3] = {{"need", nbig, 2}, {"free", fbig, 2}, {"reserve", r1, 2}};
        CHECK(wave_lp(app, "restore_fits", sa2, 3) == ZXAPP_BOTH,
              "Lifeboat: need + reserve past 18 digits: Sutra refuses, KRON answers: BOTH");
        zxapp_arg_t na[3] = {{"need", nneg, 2}, {"free", f10, 2}, {"reserve", r1, 2}};
        CHECK(wave_lp(app, "restore_fits", na, 3) == ZXAPP_BOTH,
              "Lifeboat: a negative need: Sutra refuses, KRON answers: BOTH");
    }

    /* recovery_ok (LB6): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1200000000, 1200000000);
        b[i] = wrand(-12, 12);
        c[i] = wrand(-12, 12);
        d[i] = wrand(-12, 12);
        e[i] = wrand(-12, 12);
        f[i] = wrand(-MAX, MAX);
        g[i] = wrand(-1200000000000000, 1200000000000000);
    }
    a[0] = 1, b[0] = 0, c[0] = 1, d[0] = 1, e[0] = 1, f[0] = 5, g[0] = 1700000000000;
    a[1] = 999999999, b[1] = 4, c[1] = 0, d[1] = 0, e[1] = 0, f[1] = -MAX, g[1] = 999999999999999;
    a[2] = 1000000000, b[2] = 0, c[2] = 1, d[2] = 1, e[2] = 1, f[2] = 5, g[2] = 1;
    a[3] = 1, b[3] = 10, c[3] = 1, d[3] = 1, e[3] = 1, f[3] = 5, g[3] = 1;
    a[4] = 1, b[4] = 0, c[4] = 1, d[4] = 1, e[4] = 1, f[4] = 5, g[4] = 1000000000000000;
    a[5] = 1, b[5] = 0, c[5] = 1, d[5] = 1, e[5] = 1, f[5] = MAX, g[5] = 1;
    a[6] = -999999999, b[6] = -9, c[6] = -9, d[6] = -9, e[6] = -9, f[6] = 0, g[6] = 0;
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(lifeboat_record_fits(a[i], b[i], c[i], d[i], e[i], f[i], g[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_TRUE &&
              want[6] == ZXAPP_TRUE,
          "Lifeboat: the C rule's own edges (LB6)");
    zxapp_arg_t ka[7] = {{"seq", a, WAVE_ROWS},     {"step", b, WAVE_ROWS},
                         {"journal", c, WAVE_ROWS}, {"books", d, WAVE_ROWS},
                         {"tested", e, WAVE_ROWS},  {"room", f, WAVE_ROWS},
                         {"stamp", g, WAVE_ROWS}};
    CHECK(wave_same(app, "recovery_ok", ka, 7, want, WAVE_ROWS, true),
          "Lifeboat: a check makes a ^ZXLIFEBOAT record, MYRA = C");

    /* used_share.used_x100, used for the disk: Sutra = KRON = the model's C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 99999999999999); /* total bytes, under 100 TB */
        b[i] = wrand(0, a[i]);           /* free */
        want[i] = lifeboat_used_x100(a[i], b[i]);
    }
    a[0] = 1000, b[0] = 250, want[0] = lifeboat_used_x100(1000, 250);
    a[1] = 1000, b[1] = 1000, want[1] = lifeboat_used_x100(1000, 1000);
    zxapp_arg_t ua[2] = {{"total", a, WAVE_ROWS}, {"left", b, WAVE_ROWS}};
    CHECK(want[0] == 7500 && want[1] == 0 &&
              wave_same(app, "used_share.used_x100", ua, 2, want, WAVE_ROWS, false),
          "Lifeboat: how full the disk is, through used_share.used_x100");
    CHECK(lifeboat_used_x100(0, 0) == LIFEBOAT_REFUSED &&
              lifeboat_used_x100(10, 11) == LIFEBOAT_REFUSED &&
              lifeboat_used_x100(10, -1) == LIFEBOAT_REFUSED &&
              lifeboat_used_x100(INT64_MAX, 0) == LIFEBOAT_REFUSED &&
              lifeboat_used_x100(3, 2) == 3333,
          "Lifeboat: the C share's own edges");
}
