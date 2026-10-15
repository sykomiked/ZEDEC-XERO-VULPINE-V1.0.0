/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Satchel's languages against the C its model runs (satchel_rules.h).
 * Slots: @logic.satchel_package_ok, @logic.satchel_pkg_record_ok, and,
 * used as they are, the Banker's @num.banker_total and @num.banker_share
 * and Witness's @num.witness_kind_count. */
#include "satchel_rules.h"

APP_TEST(satchel, 12, 7)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];

    /* package_ok (SR1): Eve = KRON = C, every edge, then swept */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-2, SATCHEL_FILES_MAX + 2);
        b[i] = i % 4 ? wrand(1, 9999999) : wrand(-1, SATCHEL_BYTES_MAX + 2);
        c[i] = wrand(-1, b[i] < SATCHEL_FILE_MAX + 2 ? b[i] : SATCHEL_FILE_MAX + 2);
    }
    a[0] = 1, b[0] = 1, c[0] = 1;                                                /* least */
    a[1] = SATCHEL_FILES_MAX, b[1] = SATCHEL_BYTES_MAX, c[1] = SATCHEL_FILE_MAX; /* max */
    a[2] = SATCHEL_FILES_MAX + 1, b[2] = 10, c[2] = 1;                           /* files + 1 */
    a[3] = 1, b[3] = SATCHEL_BYTES_MAX + 1, c[3] = 1;                            /* bytes + 1 */
    a[4] = 1, b[4] = SATCHEL_FILE_MAX + 1, c[4] = SATCHEL_FILE_MAX + 1;          /* a file + 1 */
    a[5] = 0, b[5] = 10, c[5] = 1;                                               /* no files */
    a[6] = 1, b[6] = 0, c[6] = 0;                                                /* no bytes */
    a[7] = -1, b[7] = -1, c[7] = -1;                                             /* negative */
    a[8] = 3, b[8] = 600, c[8] = 600;                                            /* one is all */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(satchel_package_ok(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE && want[8] == ZXAPP_TRUE &&
              !satchel_package_ok(2, 100, 200),
          "Satchel: the C limits' own edges (SR1)");
    zxapp_arg_t pa[3] = {
        {"files", a, WAVE_ROWS}, {"bytes", b, WAVE_ROWS}, {"largest", c, WAVE_ROWS}};
    CHECK(wave_same(app, "package_ok", pa, 3, want, WAVE_ROWS, true),
          "Satchel: package_ok, Eve = KRON = C");
    {
        /* a largest file past the whole: Eve holds each range alone (TRUE), KRON relates them */
        static const int64_t f2[2] = {2, 2}, b2[2] = {100, 100}, l2[2] = {50, 200};
        zxapp_arg_t xa[3] = {{"files", f2, 2}, {"bytes", b2, 2}, {"largest", l2, 2}};
        CHECK(wave_lp(app, "package_ok", xa, 3) == ZXAPP_BOTH,
              "Satchel: largest past the whole: Eve TRUE, KRON FALSE: BOTH");
        static const int64_t f3[2] = {1, 1}, b3[2] = {SATCHEL_BYTES_MAX, SATCHEL_BYTES_MAX},
                             l3[2] = {1, SATCHEL_FILE_MAX + 1};
        zxapp_arg_t ya[3] = {{"files", f3, 2}, {"bytes", b3, 2}, {"largest", l3, 2}};
        CHECK(wave_lp(app, "package_ok", ya, 3) != ZXAPP_BOTH,
              "Satchel: a file past 2 MiB: both refuse, no disagreement");
    }

    /* pkg_record_ok (SR5): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1200000000, 1200000000);
        b[i] = wrand(-120, 120);
        c[i] = wrand(-1200000000, 1200000000);
        d[i] = wrand(-12, 12);
        e[i] = wrand(-1200000000000000, 1200000000000000);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0;
    a[1] = 999999999, b[1] = 99, c[1] = 999999999, d[1] = 9, e[1] = 999999999999999;
    a[2] = 1000000000, b[2] = 1, c[2] = 1, d[2] = 1, e[2] = 1;
    a[3] = 1, b[3] = 100, c[3] = 1, d[3] = 1, e[3] = 1;
    a[4] = 1, b[4] = 1, c[4] = 1000000000, d[4] = 1, e[4] = 1;
    a[5] = 1, b[5] = 1, c[5] = 1, d[5] = 10, e[5] = 1;
    a[6] = 1, b[6] = 1, c[6] = 1, d[6] = 1, e[6] = 1000000000000000;
    a[7] = -999999999, b[7] = -99, c[7] = -999999999, d[7] = -9, e[7] = -999999999999999;
    a[8] = 3, b[8] = 4, c[8] = 600, d[8] = 1, e[8] = 1700000000000;
    a[9] = INT64_MAX, b[9] = 1, c[9] = 1, d[9] = 1, e[9] = INT64_MIN;
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(satchel_record_fits(a[i], b[i], c[i], d[i], e[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_TRUE && want[8] == ZXAPP_TRUE &&
              want[9] == ZXAPP_FALSE,
          "Satchel: the record's own edges (SR5)");
    zxapp_arg_t ra[5] = {{"seq", a, WAVE_ROWS},
                         {"files", b, WAVE_ROWS},
                         {"bytes", c, WAVE_ROWS},
                         {"signed", d, WAVE_ROWS},
                         {"stamp", e, WAVE_ROWS}};
    CHECK(wave_same(app, "pkg_record_ok", ra, 5, want, WAVE_ROWS, true),
          "Satchel: a package makes a ^ZXSATCHEL record, MYRA = C");

    /* banker.total, banker.share and witness.kind_count over packages: the model's C */
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        static int64_t size[SATCHEL_FILES_MAX], kind[SATCHEL_FILES_MAX], share[SATCHEL_FILES_MAX];
        int64_t whole[1], wk[1];
        uint32_t n = 1u + r % SATCHEL_FILES_MAX;
        for (uint32_t i = 0; i < n; i++) {
            size[i] = r == 0 ? SATCHEL_FILE_MAX : wrand(1, SATCHEL_FILE_MAX);
            kind[i] = wrand(0, SATCHEL_KINDS - 1);
        }
        whole[0] = satchel_total(size, n);
        wk[0] = r == 1 ? 9 : wrand(0, SATCHEL_KINDS - 1); /* r 1: a kind no file has */
        for (uint32_t i = 0; i < n; i++) share[i] = satchel_share_x100(size[i], whole[0]);
        int64_t count = witness_kind_count(kind, n, wk[0]);
        zxapp_arg_t ta[1] = {{"size", size, n}};
        zxapp_arg_t sa[2] = {{"size", size, n}, {"total", whole, 1}};
        zxapp_arg_t ka[2] = {{"kind", kind, n}, {"want", wk, 1}};
        CHECK(whole[0] > 0 && wave_same(app, "banker.total", ta, 1, whole, 1, false),
              "Satchel: package %u of %u files, its total through banker.total", r, n);
        CHECK(wave_same(app, "banker.share", sa, 2, share, n, false),
              "Satchel: package %u, each file's share through banker.share", r);
        CHECK(wave_same(app, "witness.kind_count", ka, 2, &count, 1, false),
              "Satchel: package %u, its files of kind %lld through witness.kind_count", r,
              (long long) wk[0]);
    }
    {
        static const int64_t s2[2] = {5, 11}, t1[1] = {10}, k2[2] = {1, 2}, w2[2] = {1, 1};
        zxapp_arg_t sa[2] = {{"size", s2, 2}, {"total", t1, 1}};
        CHECK(wave_lp(app, "banker.share", sa, 2) == ZXAPP_BOTH &&
                  satchel_share_x100(11, 10) == SATCHEL_REFUSED,
              "Satchel: a file past its package: C++ refuses, KRON answers: BOTH");
        zxapp_arg_t ka[2] = {{"kind", k2, 2}, {"want", w2, 2}};
        CHECK(wave_lp(app, "witness.kind_count", ka, 2) == ZXAPP_BOTH,
              "Satchel: two kinds at once: Trebago refuses, KRON sums them: BOTH");
    }
    CHECK(satchel_share_x100(0, 0) == 0 && satchel_share_x100(1, 0) == 10000 &&
              satchel_share_x100(-1, 10) == SATCHEL_REFUSED && satchel_share_x100(1, 3) == 3333 &&
              satchel_total(NULL, 0) == SATCHEL_REFUSED,
          "Satchel: the C share's and total's own edges (SR2, SR3)");
    CHECK(satchel_kind_of("app.zxapp") == SATCHEL_K_MANIFEST &&
              satchel_kind_of("run.wasm") == SATCHEL_K_CODE &&
              satchel_kind_of("icon.svg") == SATCHEL_K_IMAGE &&
              satchel_kind_of("README.md") == SATCHEL_K_TEXT &&
              satchel_kind_of("data.bin") == SATCHEL_K_OTHER &&
              satchel_kind_of(".c") == SATCHEL_K_OTHER && satchel_kind_of(NULL) == SATCHEL_K_OTHER,
          "Satchel: the kinds by name (SR4)");
}
