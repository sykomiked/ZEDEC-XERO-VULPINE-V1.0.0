/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Toolchest's languages against the C its model runs (toolchest_rules.h).
 * Slots: @num.toolchest_version_fits, @logic.toolchest_install_allowed,
 * @num.toolchest_download_bytes, @num.toolchest_installed_bytes,
 * @logic.toolchest_kit_record_ok, and the core's @num.used_x100, the
 * volume in use once a kit is written. */
#include "toolchest_rules.h"

/* one witnessed call that must be refused: no rows answered */
static bool tch_refused(zxapp_t *a, const char *entry, const zxapp_arg_t *args, uint32_t nargs)
{
    zxapp_num_t out[WAVE_ROWS];
    uint32_t n = 0;
    int32_t lp = -1;
    return zxapp_call_witnessed(a, &H, entry, args, nargs, out, WAVE_ROWS, &n, &lp) != ZXAPP_OK;
}

APP_TEST(toolchest, 9, 126)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], want[WAVE_ROWS];

    /* version_fits (TC1): Sutra = KRON = C, swept, with every edge */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 999), b[i] = wrand(0, 999), c[i] = wrand(0, 999);
        /* most rows share the kit's major, so the minor and patch decide */
        d[i] = i % 5 == 0 ? wrand(0, 999) : a[i];
        e[i] = i % 3 == 0 ? b[i] : wrand(0, 999);
        f[i] = wrand(0, 999);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0, f[0] = 0;             /* 0 on 0 */
    a[1] = 999, b[1] = 999, c[1] = 999, d[1] = 999, e[1] = 999, f[1] = 999; /* max on max */
    a[2] = 1, b[2] = 3, c[2] = 0, d[2] = 1, e[2] = 6, f[2] = 0;     /* an older minor fits */
    a[3] = 1, b[3] = 6, c[3] = 1, d[3] = 1, e[3] = 6, f[3] = 0;     /* a newer patch does not */
    a[4] = 1, b[4] = 0, c[4] = 0, d[4] = 2, e[4] = 0, f[4] = 0;     /* another major */
    a[5] = 2, b[5] = 0, c[5] = 0, d[5] = 1, e[5] = 999, f[5] = 999; /* a newer major */
    a[6] = 1, b[6] = 9, c[6] = 999, d[6] = 1, e[6] = 10, f[6] = 0;  /* 1.10.0 past 1.9.999 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = toolchest_version_fits(a[i], b[i], c[i], d[i], e[i], f[i]);
    CHECK(want[0] == 1 && want[1] == 1 && want[2] == 1 && want[3] == 0 && want[4] == 0 &&
              want[5] == 0 && want[6] == 1 &&
              toolchest_version_fits(1, 0, 0, 1000, 0, 0) == TOOLCHEST_REFUSED &&
              toolchest_version_fits(1, 0, -1, 1, 0, 0) == TOOLCHEST_REFUSED,
          "Toolchest: the C rule's own edges (TC1)");
    zxapp_arg_t va[6] = {{"kmajor", a, WAVE_ROWS}, {"kminor", b, WAVE_ROWS},
                         {"kpatch", c, WAVE_ROWS}, {"dmajor", d, WAVE_ROWS},
                         {"dminor", e, WAVE_ROWS}, {"dpatch", f, WAVE_ROWS}};
    CHECK(wave_same(app, "version_fits", va, 6, want, WAVE_ROWS, false),
          "Toolchest: version_fits, Sutra = KRON = C");
    {
        /* max + 1: a part past Sutra's 9(3) is refused there; KRON answers */
        static const int64_t one[2] = {1, 1}, big[2] = {1, 1000}, zero[2] = {0, 0},
                             neg[2] = {0, -1};
        zxapp_arg_t xa[6] = {{"kmajor", one, 2}, {"kminor", zero, 2}, {"kpatch", zero, 2},
                             {"dmajor", big, 2}, {"dminor", zero, 2}, {"dpatch", zero, 2}};
        CHECK(wave_lp(app, "version_fits", xa, 6) == ZXAPP_BOTH,
              "Toolchest: a part past 999: Sutra refuses, KRON answers: BOTH");
        zxapp_arg_t na[6] = {{"kmajor", one, 2}, {"kminor", zero, 2}, {"kpatch", neg, 2},
                             {"dmajor", one, 2}, {"dminor", zero, 2}, {"dpatch", zero, 2}};
        CHECK(wave_lp(app, "version_fits", na, 6) == ZXAPP_BOTH,
              "Toolchest: a negative part: Sutra refuses, KRON answers: BOTH");
    }

    /* install_allowed (TC2): Eve = KRON = C over verify results and fits 0 and 1 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1);
        b[i] = wrand(0, 1);
        c[i] = wrand(0, 1);
        d[i] = wrand(-999999, 999999999999);
    }
    a[0] = 1, b[0] = 1, c[0] = 1, d[0] = 0;                      /* just room */
    a[1] = 1, b[1] = 1, c[1] = 1, d[1] = TOOLCHEST_ROOM_MAX;     /* the most room */
    a[2] = 1, b[2] = 1, c[2] = 1, d[2] = -1;                     /* one byte short */
    a[3] = 0, b[3] = 1, c[3] = 1, d[3] = 5;                      /* the first failed */
    a[4] = 1, b[4] = 0, c[4] = 1, d[4] = 5;                      /* the second failed */
    a[5] = 1, b[5] = 1, c[5] = 0, d[5] = 5;                      /* does not fit */
    a[6] = -1, b[6] = 1, c[6] = 1, d[6] = 5;                     /* no result: both say no */
    a[7] = 1, b[7] = 1, c[7] = 1, d[7] = INT64_C(-999999999999); /* far short */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(toolchest_install_allowed(a[i], b[i], c[i], d[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE &&
              !toolchest_install_allowed(1, 1, 1, TOOLCHEST_ROOM_MAX + 1) &&
              !toolchest_install_allowed(1, 1, 2, 0),
          "Toolchest: the C rule's own edges (TC2)");
    zxapp_arg_t ia[4] = {{"firstsig", a, WAVE_ROWS},
                         {"secondsig", b, WAVE_ROWS},
                         {"fits", c, WAVE_ROWS},
                         {"room", d, WAVE_ROWS}};
    CHECK(wave_same(app, "install_allowed", ia, 4, want, WAVE_ROWS, true),
          "Toolchest: install_allowed, Eve = KRON = C");
    {
        /* max + 1: a verify result or a fit of 2 is no result; Eve: no fit, KRON: above 0 */
        static const int64_t ones[2] = {1, 1}, two[2] = {1, 2}, room[2] = {0, 0},
                             over[2] = {0, TOOLCHEST_ROOM_MAX + 1};
        zxapp_arg_t xa[4] = {
            {"firstsig", two, 2}, {"secondsig", ones, 2}, {"fits", ones, 2}, {"room", room, 2}};
        CHECK(wave_lp(app, "install_allowed", xa, 4) == ZXAPP_BOTH,
              "Toolchest: a verify result of 2: Eve false, KRON true: BOTH");
        zxapp_arg_t fa[4] = {
            {"firstsig", ones, 2}, {"secondsig", ones, 2}, {"fits", two, 2}, {"room", room, 2}};
        CHECK(wave_lp(app, "install_allowed", fa, 4) == ZXAPP_BOTH, "Toolchest: a fit of 2: BOTH");
        zxapp_arg_t ra[4] = {
            {"firstsig", ones, 2}, {"secondsig", ones, 2}, {"fits", ones, 2}, {"room", over, 2}};
        CHECK(wave_lp(app, "install_allowed", ra, 4) == ZXAPP_BOTH,
              "Toolchest: room past 18 digits: Eve false, KRON true: BOTH");
    }

    /* download_bytes (TC3): Trebago = C over the blocks of a recipe */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(1, TOOLCHEST_BLOCK_MAX);
    a[0] = 1, a[1] = TOOLCHEST_BLOCK_MAX; /* the least and the most block */
    want[0] = toolchest_download_bytes(a, WAVE_ROWS);
    CHECK(want[0] > 0, "Toolchest: the C sum of 64 blocks");
    zxapp_arg_t da = {"blockbytes", a, WAVE_ROWS};
    CHECK(wave_same(app, "download_bytes", &da, 1, want, 1, false),
          "Toolchest: download_bytes, Trebago = C");
    {
        static const int64_t zero[2] = {1, 0}, past[2] = {1, TOOLCHEST_BLOCK_MAX + 1},
                             neg[2] = {5, -5};
        zxapp_arg_t za = {"blockbytes", zero, 2}, pa = {"blockbytes", past, 2},
                    na = {"blockbytes", neg, 2};
        CHECK(toolchest_download_bytes(zero, 2) == TOOLCHEST_REFUSED &&
                  toolchest_download_bytes(past, 2) == TOOLCHEST_REFUSED &&
                  toolchest_download_bytes(neg, 2) == TOOLCHEST_REFUSED &&
                  tch_refused(app, "download_bytes", &za, 1) &&
                  tch_refused(app, "download_bytes", &pa, 1) &&
                  tch_refused(app, "download_bytes", &na, 1),
              "Toolchest: an empty block, one past 2 MiB, a negative one: refused by both");
    }

    /* installed_bytes (TC4): Trebago = C over a kit's files */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(0, TOOLCHEST_FILE_MAX);
    a[0] = 0, a[1] = TOOLCHEST_FILE_MAX;
    want[0] = toolchest_installed_bytes(a, WAVE_ROWS);
    CHECK(want[0] > TOOLCHEST_FILE_MAX, "Toolchest: the C sum of 64 files");
    zxapp_arg_t fa = {"filebytes", a, WAVE_ROWS};
    CHECK(wave_same(app, "installed_bytes", &fa, 1, want, 1, false),
          "Toolchest: installed_bytes, Trebago = C");
    {
        static const int64_t past[2] = {0, TOOLCHEST_FILE_MAX + 1}, neg[2] = {0, -1};
        zxapp_arg_t pa = {"filebytes", past, 2}, na = {"filebytes", neg, 2};
        CHECK(toolchest_installed_bytes(past, 2) == TOOLCHEST_REFUSED &&
                  toolchest_installed_bytes(neg, 2) == TOOLCHEST_REFUSED &&
                  tch_refused(app, "installed_bytes", &pa, 1) &&
                  tch_refused(app, "installed_bytes", &na, 1),
              "Toolchest: a file past 12 digits, a negative one: refused by both");
    }

    /* kit_record_ok (TC5): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 999), b[i] = wrand(0, 99);
        c[i] = wrand(0, 999), d[i] = wrand(0, 999), e[i] = wrand(0, 999);
        f[i] = wrand(0, 999999999999999);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0, f[0] = 0; /* every least */
    a[1] = 999, b[1] = 99, c[1] = 999, d[1] = 999, e[1] = 999, f[1] = 999999999999999;
    a[2] = 1000;                     /* kit max + 1 */
    b[3] = 100;                      /* lang max + 1 */
    c[4] = 1000;                     /* major max + 1 */
    e[5] = 1000;                     /* patch max + 1 */
    f[6] = 1000000000000000;         /* bytes max + 1 */
    a[7] = -5, b[7] = -1, f[7] = -7; /* negative: sign aside */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(toolchest_kit_record_fits(a[i], b[i], c[i], d[i], e[i], f[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_TRUE,
          "Toolchest: the record's own edges (TC5)");
    zxapp_arg_t ka[6] = {{"kit", a, WAVE_ROWS},   {"lang", b, WAVE_ROWS},  {"major", c, WAVE_ROWS},
                         {"minor", d, WAVE_ROWS}, {"patch", e, WAVE_ROWS}, {"bytes", f, WAVE_ROWS}};
    CHECK(wave_same(app, "kit_record_ok", ka, 6, want, WAVE_ROWS, true),
          "Toolchest: kit_record_ok, MYRA = C");

    /* used_share.used_x100 (TC6) over volumes and the room left after a kit */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, INT64_MAX / 10000); /* the C's own bound on a total */
        b[i] = wrand(0, a[i]);
    }
    a[0] = 1, b[0] = 0;              /* one byte, all in use */
    a[1] = 1000, b[1] = 1000;        /* nothing in use */
    a[2] = 10000000, b[2] = 1402848; /* the model's own case: 85.97% */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = toolchest_used_x100(a[i], b[i]);
    CHECK(want[0] == 10000 && want[1] == 0 && want[2] == 8597,
          "Toolchest: the C rule's own edges (TC6)");
    zxapp_arg_t ua[2] = {{"total", a, WAVE_ROWS}, {"left", b, WAVE_ROWS}};
    CHECK(wave_same(app, "used_share.used_x100", ua, 2, want, WAVE_ROWS, false),
          "Toolchest: used_share.used_x100, Sutra = KRON = C");
}
