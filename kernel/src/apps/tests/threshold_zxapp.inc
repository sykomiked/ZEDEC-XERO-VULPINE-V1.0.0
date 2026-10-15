/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Threshold's languages against the C its model runs (threshold_rules.h). */
#include "threshold_rules.h"

APP_TEST(threshold, 6, 3)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];

    /* rights_ok (TH1): Sutra = KRON = C, swept over every digit, with every edge */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 3 ? wrand(1, 3) : wrand(0, 9);
        b[i] = i % 4 ? wrand(0, 1) : wrand(0, 9);
        c[i] = i % 5 ? wrand(1, 3) : wrand(0, 9);
    }
    a[0] = 1, b[0] = 0, c[0] = 2;  /* an owner, another person's lock */
    a[1] = 1, b[1] = 1, c[1] = 1;  /* an owner, their own */
    a[2] = 1, b[2] = 0, c[2] = 3;  /* an owner gives a guest a lock: never */
    a[3] = 2, b[3] = 1, c[3] = 2;  /* a person, their own */
    a[4] = 2, b[4] = 0, c[4] = 2;  /* a person, someone else's */
    a[5] = 2, b[5] = 0, c[5] = 1;  /* a person, the owner's */
    a[6] = 3, b[6] = 1, c[6] = 3;  /* a guest, their own: has none */
    a[7] = 3, b[7] = 0, c[7] = 2;  /* a guest, anyone's */
    a[8] = 0, b[8] = 1, c[8] = 2;  /* no role */
    a[9] = 9, b[9] = 9, c[9] = 9;  /* the largest Sutra holds */
    a[10] = 1, b[10] = 2, c[10] = 2; /* own past 1 */
    a[11] = 1, b[11] = 0, c[11] = 0; /* no target */
    a[12] = 4, b[12] = 1, c[12] = 2; /* a role past guest */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(threshold_rights(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_TRUE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE && want[8] == ZXAPP_FALSE &&
              want[10] == ZXAPP_FALSE && want[11] == ZXAPP_FALSE && want[12] == ZXAPP_FALSE,
          "Threshold: the C rule's own edges (TH1)");
    zxapp_arg_t ra[3] = {{"role", a, WAVE_ROWS}, {"own", b, WAVE_ROWS}, {"target", c, WAVE_ROWS}};
    CHECK(wave_same(app, "rights_ok", ra, 3, want, WAVE_ROWS, true),
          "Threshold: rights over a lock, Sutra = KRON = C");
    {
        static const int64_t rr[2] = {1, 10}, ro[2] = {0, 0}, rt[2] = {2, 2};
        zxapp_arg_t rb[3] = {{"role", rr, 2}, {"own", ro, 2}, {"target", rt, 2}};
        CHECK(wave_lp(app, "rights_ok", rb, 3) == ZXAPP_BOTH,
              "Threshold: a role past 9: Sutra refuses, KRON answers: BOTH, held");
        static const int64_t nr[2] = {2, 2}, no[2] = {1, -1}, nt[2] = {2, 2};
        zxapp_arg_t nb[3] = {{"role", nr, 2}, {"own", no, 2}, {"target", nt, 2}};
        CHECK(wave_lp(app, "rights_ok", nb, 3) == ZXAPP_BOTH,
              "Threshold: a negative flag: Sutra refuses, KRON answers: BOTH, held");
    }

    /* skip_ok (TH2): Eve = KRON = C on 0/1 flags */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2 ? 1 : wrand(-1, 4097);
        b[i] = i % 3 ? 0 : wrand(0, 2);
        c[i] = i % 5 ? 1 : 0;
    }
    a[0] = 1, b[0] = 0, c[0] = 1;    /* one seat, no lock, auto on: skipped */
    a[1] = 0, b[1] = 0, c[1] = 1;    /* an empty device */
    a[2] = 2, b[2] = 0, c[2] = 1;    /* two people */
    a[3] = 1, b[3] = 1, c[3] = 1;    /* a passphrase */
    a[4] = 1, b[4] = 2, c[4] = 1;    /* a passkey */
    a[5] = 1, b[5] = 0, c[5] = 0;    /* the owner turned it off */
    a[6] = 4096, b[6] = 0, c[6] = 1; /* zxvd's most */
    a[7] = 4097, b[7] = 0, c[7] = 1; /* max + 1 */
    a[8] = -1, b[8] = 0, c[8] = 1;   /* negative */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(threshold_skip(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[8] == ZXAPP_FALSE,
          "Threshold: the C rule's own edges (TH2)");
    zxapp_arg_t sa[3] = {{"seats", a, WAVE_ROWS}, {"lock", b, WAVE_ROWS}, {"auto", c, WAVE_ROWS}};
    CHECK(wave_same(app, "skip_ok", sa, 3, want, WAVE_ROWS, true),
          "Threshold: skip the login, Eve = KRON = C");
    {
        static const int64_t ss[2] = {1, 1}, sl[2] = {0, 0}, su[2] = {1, 2};
        zxapp_arg_t sb[3] = {{"seats", ss, 2}, {"lock", sl, 2}, {"auto", su, 2}};
        CHECK(wave_lp(app, "skip_ok", sb, 3) == ZXAPP_BOTH,
              "Threshold: auto 2 is not a flag: Eve says no, KRON says on: BOTH, held");
        static const int64_t ls[2] = {1, 1}, ll[2] = {0, -1}, lu[2] = {1, 1};
        zxapp_arg_t lb[3] = {{"seats", ls, 2}, {"lock", ll, 2}, {"auto", lu, 2}};
        CHECK(wave_lp(app, "skip_ok", lb, 3) == ZXAPP_BOTH,
              "Threshold: lock -1 is no lock to KRON, not one to Eve: BOTH, held");
    }

    /* phrase_ok (TH3): Eve = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(-2, 1100);
    a[0] = 11, a[1] = 12, a[2] = 256, a[3] = 257, a[4] = 0, a[5] = -1, a[6] = 1024;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(threshold_phrase_ok(a[i]));
    CHECK(want[0] == ZXAPP_FALSE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_TRUE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE,
          "Threshold: the C rule's own edges (TH3)");
    zxapp_arg_t pa[1] = {{"length", a, WAVE_ROWS}};
    CHECK(wave_same(app, "phrase_ok", pa, 1, want, WAVE_ROWS, true),
          "Threshold: a passphrase of 12 to 256 bytes, Eve = C");

    /* seat_ok (TH4): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-10000, 10000);
        b[i] = wrand(-10, 10);
        c[i] = wrand(-10, 10);
        d[i] = i % 2 ? wrand(0, 4294967295) : wrand(-10000000000, 10000000000);
        e[i] = wrand(-10000000, 10000000);
    }
    a[0] = 0, b[0] = 1, c[0] = 0, d[0] = 0, e[0] = 0;
    a[1] = 4095, b[1] = 3, c[1] = 2, d[1] = 4294967295, e[1] = 3600000;
    a[2] = 9999, b[2] = 9, c[2] = 9, d[2] = 9999999999, e[2] = 9999999;
    a[3] = 10000, b[3] = 1, c[3] = 0, d[3] = 0, e[3] = 0;
    a[4] = 1, b[4] = 10, c[4] = 0, d[4] = 0, e[4] = 0;
    a[5] = 1, b[5] = 1, c[5] = 10, d[5] = 0, e[5] = 0;
    a[6] = 1, b[6] = 1, c[6] = 0, d[6] = 10000000000, e[6] = 0;
    a[7] = 1, b[7] = 1, c[7] = 0, d[7] = 0, e[7] = 10000000;
    a[8] = -9999, b[8] = -9, c[8] = -9, d[8] = -9999999999, e[8] = -9999999;
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(threshold_seat_fits(a[i], b[i], c[i], d[i], e[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_TRUE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE && want[8] == ZXAPP_TRUE,
          "Threshold: the C rule's own edges (TH4)");
    zxapp_arg_t ka[5] = {{"seat", a, WAVE_ROWS},
                         {"role", b, WAVE_ROWS},
                         {"lock", c, WAVE_ROWS},
                         {"color", d, WAVE_ROWS},
                         {"wait", e, WAVE_ROWS}};
    CHECK(wave_same(app, "seat_ok", ka, 5, want, WAVE_ROWS, true),
          "Threshold: a seat makes a ^ZXSEAT record, MYRA = C");
}
