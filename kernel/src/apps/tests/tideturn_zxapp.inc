/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Tideturn's languages against the C its model runs (tideturn_rules.h).
 * Slots: @num.tideturn_version_order, @logic.tideturn_dual_signed,
 * @logic.tideturn_update_record_ok, and Witness's
 * @logic.witness_chain_ok, which links the trail. */
#include "tideturn_rules.h"

APP_TEST(tideturn, 10, 6)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], g[WAVE_ROWS], want[WAVE_ROWS];

    /* version_order (TR1): Sutra = KRON = C, swept, with every edge */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 999), b[i] = wrand(0, 999), c[i] = wrand(0, 999);
        /* most rows share a prefix with a, so every part decides somewhere */
        d[i] = i % 4 == 0 ? wrand(0, 999) : a[i];
        e[i] = i % 4 == 1 ? wrand(0, 999) : b[i];
        f[i] = i % 4 == 2 ? wrand(0, 999) : c[i];
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0, f[0] = 0;             /* 0 = 0 */
    a[1] = 999, b[1] = 999, c[1] = 999, d[1] = 999, e[1] = 999, f[1] = 999; /* max = max */
    a[2] = 999, b[2] = 999, c[2] = 999, d[2] = 0, e[2] = 0, f[2] = 0;       /* max over 0 */
    a[3] = 0, b[3] = 999, c[3] = 999, d[3] = 1, e[3] = 0, f[3] = 0;         /* the major decides */
    a[4] = 1, b[4] = 2, c[4] = 3, d[4] = 1, e[4] = 2, f[4] = 4;             /* the patch decides */
    a[5] = 1, b[5] = 10, c[5] = 0, d[5] = 1, e[5] = 9, f[5] = 999; /* 1.10.0 is past 1.9.999 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = tideturn_version_order(a[i], b[i], c[i], d[i], e[i], f[i]);
    CHECK(want[0] == 0 && want[1] == 0 && want[2] == 1 && want[3] == -1 && want[4] == -1 &&
              want[5] == 1 && tideturn_version_order(1000, 0, 0, 1, 0, 0) == TIDETURN_REFUSED &&
              tideturn_version_order(1, 0, 0, 1, 0, -1) == TIDETURN_REFUSED,
          "Tideturn: the C rule's own edges (TR1)");
    zxapp_arg_t va[6] = {{"amajor", a, WAVE_ROWS}, {"aminor", b, WAVE_ROWS},
                         {"apatch", c, WAVE_ROWS}, {"bmajor", d, WAVE_ROWS},
                         {"bminor", e, WAVE_ROWS}, {"bpatch", f, WAVE_ROWS}};
    CHECK(wave_same(app, "version_order", va, 6, want, WAVE_ROWS, false),
          "Tideturn: version_order, Sutra = KRON = C");
    {
        /* max + 1: a part past Sutra's 9(3) is refused there; KRON answers */
        static const int64_t big[2] = {1, 1000}, one[2] = {1, 1}, zero[2] = {0, 0};
        zxapp_arg_t xa[6] = {{"amajor", big, 2}, {"aminor", zero, 2}, {"apatch", zero, 2},
                             {"bmajor", one, 2}, {"bminor", zero, 2}, {"bpatch", zero, 2}};
        CHECK(wave_lp(app, "version_order", xa, 6) == ZXAPP_BOTH,
              "Tideturn: a part past 999: Sutra refuses, KRON answers: BOTH");
        /* negative: Sutra's unsigned picture refuses it */
        static const int64_t neg[2] = {0, -1};
        zxapp_arg_t na[6] = {{"amajor", one, 2}, {"aminor", zero, 2}, {"apatch", neg, 2},
                             {"bmajor", one, 2}, {"bminor", zero, 2}, {"bpatch", zero, 2}};
        CHECK(wave_lp(app, "version_order", na, 6) == ZXAPP_BOTH,
              "Tideturn: a negative part: Sutra refuses, KRON answers: BOTH");
    }

    /* dual_signed (TR2): Eve = KRON = C over verify results 0 and 1 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1);
        b[i] = wrand(0, 1);
    }
    a[0] = 1, b[0] = 1;  /* both verified */
    a[1] = 0, b[1] = 1;  /* the first failed */
    a[2] = 1, b[2] = 0;  /* the second failed */
    a[3] = 0, b[3] = 0;  /* neither */
    a[4] = -1, b[4] = 1; /* no result: KRON's 0 and Eve's range agree */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(tideturn_dual_signed(a[i], b[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && !tideturn_dual_signed(2, 1) &&
              !tideturn_dual_signed(1, 2),
          "Tideturn: the C rule's own edges (TR2)");
    zxapp_arg_t sa[2] = {{"firstsig", a, WAVE_ROWS}, {"secondsig", b, WAVE_ROWS}};
    CHECK(wave_same(app, "dual_signed", sa, 2, want, WAVE_ROWS, true),
          "Tideturn: dual_signed, Eve = KRON = C");
    {
        /* max + 1: a result of 2 is no result; Eve: no fit, KRON: above 0 */
        static const int64_t two[2] = {1, 2}, ones[2] = {1, 1};
        zxapp_arg_t xa[2] = {{"firstsig", two, 2}, {"secondsig", ones, 2}};
        CHECK(wave_lp(app, "dual_signed", xa, 2) == ZXAPP_BOTH,
              "Tideturn: a verify result of 2: Eve false, KRON true: BOTH");
        zxapp_arg_t ya[2] = {{"firstsig", ones, 2}, {"secondsig", two, 2}};
        CHECK(wave_lp(app, "dual_signed", ya, 2) == ZXAPP_BOTH,
              "Tideturn: the second result 2: BOTH");
    }

    /* update_record_ok (TR3): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 999999999);
        b[i] = wrand(0, 999), c[i] = wrand(0, 999), d[i] = wrand(0, 999);
        e[i] = wrand(0, 1);
        f[i] = wrand(1, 7);
        g[i] = wrand(0, 999999999) * 1000;
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0, f[0] = 0, g[0] = 0; /* every least */
    a[1] = TIDETURN_SEQ_MAX, b[1] = 999, c[1] = 999, d[1] = 999, e[1] = 9, f[1] = 9,
    g[1] = TIDETURN_STAMP_MAX;         /* every most */
    a[2] = TIDETURN_SEQ_MAX + 1;       /* seq max + 1 */
    b[3] = 1000;                       /* major max + 1 */
    d[4] = 1000;                       /* patch max + 1 */
    e[5] = 10;                         /* signed max + 1 */
    f[6] = 10;                         /* outcome max + 1 */
    g[7] = TIDETURN_STAMP_MAX + 1;     /* stamp max + 1 */
    a[8] = -5, b[8] = -999, e[8] = -1; /* negative: sign aside */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(tideturn_record_fits(a[i], b[i], c[i], d[i], e[i], f[i], g[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE && want[8] == ZXAPP_TRUE,
          "Tideturn: the record's own edges (TR3)");
    zxapp_arg_t ra[7] = {{"seq", a, WAVE_ROWS},    {"major", b, WAVE_ROWS},
                         {"minor", c, WAVE_ROWS},  {"patch", d, WAVE_ROWS},
                         {"signed", e, WAVE_ROWS}, {"outcome", f, WAVE_ROWS},
                         {"stamp", g, WAVE_ROWS}};
    CHECK(wave_same(app, "update_record_ok", ra, 7, want, WAVE_ROWS, true),
          "Tideturn: update_record_ok, MYRA = C");

    /* witness.chain_ok (WR1) over a trail as the model keeps it: Witness's KRON and Sutra = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = (int64_t) i + 1;
        b[i] = i % 11 == 5 ? (int64_t) i - 1 : (int64_t) i; /* some links skip a record */
        c[i] = (int64_t) i;
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(witness_linked(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[5] == ZXAPP_FALSE, "Tideturn: the trail's own links");
    zxapp_arg_t ca[3] = {{"seq", a, WAVE_ROWS}, {"prev", b, WAVE_ROWS}, {"last", c, WAVE_ROWS}};
    CHECK(wave_same(app, "witness.chain_ok", ca, 3, want, WAVE_ROWS, true),
          "Tideturn: the trail linked through witness.chain_ok");
}
