/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Lockbox's languages against the C its model runs (lockbox_rules.h).
 * Counts only, never a password. Slots: @num.lockbox_strength,
 * @num.lockbox_days_left, @logic.lockbox_secret_ok. */
#include "lockbox_rules.h"

APP_TEST(lockbox, 5, 9)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], want[WAVE_ROWS];

    /* strength (LR1): Sutra = KRON = C, swept, with every edge */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, i < 32 ? 40 : LOCKBOX_COUNT_MAX);
        b[i] = wrand(0, LOCKBOX_CLASS_MAX);
        c[i] = wrand(0, i % 2 ? 4 : 30);
    }
    a[0] = 0, b[0] = 0, c[0] = 0;                                 /* nothing: 0 */
    a[1] = 20, b[1] = 4, c[1] = 0;                                /* the most: 100 */
    a[2] = 21, b[2] = 5, c[2] = 0;                                /* past both caps: 100 */
    a[3] = 19, b[3] = 3, c[3] = 0;                                /* 57 + 30 = 87 */
    a[4] = 12, b[4] = 1, c[4] = 9;                                /* 46 - 45 = 1 */
    a[5] = 12, b[5] = 1, c[5] = 10;                               /* 46 - 50, held at 0 */
    a[6] = LOCKBOX_COUNT_MAX, b[6] = LOCKBOX_CLASS_MAX, c[6] = 0; /* every max */
    a[7] = 0, b[7] = 0, c[7] = LOCKBOX_COUNT_MAX;                 /* the most repeats */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = lockbox_strength(a[i], b[i], c[i]);
    zxapp_arg_t sa[3] = {
        {"length", a, WAVE_ROWS}, {"classes", b, WAVE_ROWS}, {"repeats", c, WAVE_ROWS}};
    CHECK(wave_same(app, "strength", sa, 3, want, WAVE_ROWS, false),
          "Lockbox: strength, Sutra = KRON = C");
    {
        /* max + 1 past Sutra's 9(4): refused there, KRON answers */
        static const int64_t l[2] = {12, 10000}, k[2] = {3, 3}, r[2] = {0, 0};
        zxapp_arg_t ma[3] = {{"length", l, 2}, {"classes", k, 2}, {"repeats", r, 2}};
        CHECK(wave_lp(app, "strength", ma, 3) == ZXAPP_BOTH,
              "Lockbox: a length past 9(4): Sutra refuses, KRON answers: BOTH");
        /* a negative count: Sutra refuses it too */
        static const int64_t n[2] = {0, -1};
        zxapp_arg_t na[3] = {{"length", l, 2}, {"classes", k, 2}, {"repeats", n, 2}};
        CHECK(wave_lp(app, "strength", na, 3) == ZXAPP_BOTH,
              "Lockbox: negative repeats: Sutra refuses, KRON answers: BOTH");
        CHECK(lockbox_strength(10000, 3, 0) == LOCKBOX_REFUSED &&
                  lockbox_strength(12, 10, 0) == LOCKBOX_REFUSED &&
                  lockbox_strength(12, -1, 0) == LOCKBOX_REFUSED,
              "Lockbox: the C refuses past the pictures");
    }

    /* days_left (LR2): C++ (wasm) = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        b[i] = wrand(0, i < 32 ? 2000000000 : LOCKBOX_STAMP_MAX);
        a[i] = wrand(0, LOCKBOX_STAMP_MAX);
        if (i % 3 == 0) a[i] = b[i] + wrand(0, 40000000); /* within a year or so ahead */
        if (a[i] > LOCKBOX_STAMP_MAX) a[i] = LOCKBOX_STAMP_MAX;
    }
    a[0] = 0, b[0] = 0;                                 /* both 0 */
    a[1] = 86400, b[1] = 0;                             /* one whole day */
    a[2] = 86399, b[2] = 0;                             /* under a day: 0 */
    a[3] = 0, b[3] = 86400;                             /* past: never below 0 */
    a[4] = LOCKBOX_STAMP_MAX, b[4] = 0;                 /* max */
    a[5] = 0, b[5] = LOCKBOX_STAMP_MAX;                 /* max now */
    a[6] = LOCKBOX_STAMP_MAX, b[6] = LOCKBOX_STAMP_MAX; /* due today */
    a[7] = 1800000000 + 30 * 86400, b[7] = 1800000000;  /* 30 days */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = lockbox_days_left(a[i], b[i]);
    zxapp_arg_t da[2] = {{"expires", a, WAVE_ROWS}, {"now", b, WAVE_ROWS}};
    CHECK(wave_same(app, "days_left", da, 2, want, WAVE_ROWS, false),
          "Lockbox: days_left, C++ = KRON = C");
    {
        /* a negative stamp: C++ traps, KRON answers */
        static const int64_t e[2] = {86400, -1}, n[2] = {0, 0};
        zxapp_arg_t ea[2] = {{"expires", e, 2}, {"now", n, 2}};
        CHECK(wave_lp(app, "days_left", ea, 2) == ZXAPP_BOTH,
              "Lockbox: a negative expiry: C++ refuses, KRON answers: BOTH");
        /* max + 1, past year 9999 */
        static const int64_t f[2] = {86400, LOCKBOX_STAMP_MAX + 1};
        zxapp_arg_t fa[2] = {{"expires", f, 2}, {"now", n, 2}};
        CHECK(wave_lp(app, "days_left", fa, 2) == ZXAPP_BOTH,
              "Lockbox: an expiry past year 9999: C++ refuses, KRON answers: BOTH");
        CHECK(lockbox_days_left(-1, 0) == LOCKBOX_REFUSED &&
                  lockbox_days_left(0, LOCKBOX_STAMP_MAX + 1) == LOCKBOX_REFUSED,
              "Lockbox: the C refuses outside the stamps");
    }

    /* secret_ok (LR3): Eve = C, each count against its own range */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1100);
        b[i] = wrand(0, 5);
        c[i] = wrand(0, i % 2 ? 20 : 1100);
    }
    a[0] = 12, b[0] = 1, c[0] = 0;      /* every least */
    a[1] = 1024, b[1] = 4, c[1] = 1023; /* every max */
    a[2] = 11, b[2] = 2, c[2] = 0;      /* one byte short */
    a[3] = 1025, b[3] = 2, c[3] = 0;    /* one byte over */
    a[4] = 16, b[4] = 0, c[4] = 0;      /* no class */
    a[5] = 16, b[5] = 5, c[5] = 0;      /* a fifth class */
    a[6] = 16, b[6] = 2, c[6] = 1024;   /* repeats max + 1 */
    a[7] = 16, b[7] = 2, c[7] = -1;     /* negative */
    a[8] = 0, b[8] = 0, c[8] = 0;       /* nothing */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(lockbox_secret_ok(a[i], b[i], c[i]));
    zxapp_arg_t ka[3] = {
        {"length", a, WAVE_ROWS}, {"classes", b, WAVE_ROWS}, {"repeats", c, WAVE_ROWS}};
    CHECK(wave_same(app, "secret_ok", ka, 3, want, WAVE_ROWS, true), "Lockbox: secret_ok, Eve = C");
}
