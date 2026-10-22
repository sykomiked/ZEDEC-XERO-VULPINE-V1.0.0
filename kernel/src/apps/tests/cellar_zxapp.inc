/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Cellar's languages against the C its model runs (cellar_rules.h).
 * Slots: @logic.cellar_repair_safe_ok, and, used as they are,
 * @num.used_x100, @num.banker_share and @num.banker_total. */
#include "cellar_rules.h"

APP_TEST(cellar, 8, 19)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], want[WAVE_ROWS];
    const int64_t M = CELLAR_BYTES_MAX;

    /* repair_safe_ok (CR4): edges, then swept */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 3 ? wrand(-1000, INT64_C(1) << 40) : wrand(0, M);
        b[i] = wrand(-50, 10050);
        c[i] = i % 4 ? 1 : wrand(-1, 3);
    }
    a[0] = 1, b[0] = 0, c[0] = 1;     /* the least room, an empty volume */
    a[1] = M, b[1] = 9500, c[1] = 1;  /* the most room, the fullest volume */
    a[2] = 0, b[2] = 0, c[2] = 1;     /* no room */
    a[3] = -5, b[3] = 0, c[3] = 1;    /* short of the scratch */
    a[4] = 5, b[4] = 9501, c[4] = 1;  /* one past the fullest */
    a[5] = 5, b[5] = -1, c[5] = 1;    /* a negative fill */
    a[6] = 5, b[6] = 0, c[6] = 0;     /* no yes */
    a[7] = 5, b[7] = 0, c[7] = 2;     /* a yes that is no yes */
    a[8] = 5, b[8] = 10000, c[8] = 1; /* a full volume */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(cellar_repair_safe(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE && want[8] == ZXAPP_FALSE &&
              !cellar_repair_safe(M + 1, 0, 1),
          "Cellar: the C rule's own edges (CR4)");
    zxapp_arg_t ra[3] = {
        {"room", a, WAVE_ROWS}, {"fill", b, WAVE_ROWS}, {"approved", c, WAVE_ROWS}};
    CHECK(wave_same(app, "repair_safe_ok", ra, 3, want, WAVE_ROWS, true),
          "Cellar: repair_safe_ok, Eve = KRON = C");
    {
        /* room past 18 digits: Eve's record refuses it, KRON reads it above 0 */
        static const int64_t r[2] = {5, M + 1}, f[2] = {0, 0}, y[2] = {1, 1};
        zxapp_arg_t ba[3] = {{"room", r, 2}, {"fill", f, 2}, {"approved", y, 2}};
        CHECK(wave_lp(app, "repair_safe_ok", ba, 3) == ZXAPP_BOTH,
              "Cellar: room past 18 digits: Eve FALSE, KRON TRUE: BOTH");
        /* no room, or a negative one: both answer not safe, and agree */
        static const int64_t n[2] = {-1, 0}, no[2] = {ZXAPP_FALSE, ZXAPP_FALSE};
        zxapp_arg_t na[3] = {{"room", n, 2}, {"fill", f, 2}, {"approved", y, 2}};
        CHECK(wave_same(app, "repair_safe_ok", na, 3, no, 2, true),
              "Cellar: no room at all: FALSE in both, witnessed");
    }

    /* the volume and each folder's share of it (used_share.used_x100, CR1) */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, i % 2 ? INT64_C(1) << 42 : 999999999);
        b[i] = wrand(0, a[i]);
    }
    a[0] = 1, b[0] = 0;     /* the least volume, full */
    a[1] = 1, b[1] = 1;     /* the least volume, empty */
    a[2] = 457, b[2] = 307; /* a folder of 150 bytes: 3282 */
    a[3] = 3, b[3] = 2;     /* 3333.33.. floored */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = cellar_used_x100(a[i], b[i]);
    CHECK(want[0] == 10000 && want[1] == 0 && want[2] == 3282 && want[3] == 3333 &&
              cellar_used_x100(0, 0) == CELLAR_REFUSED &&
              cellar_used_x100(5, 6) == CELLAR_REFUSED &&
              cellar_used_x100(5, -1) == CELLAR_REFUSED &&
              cellar_used_x100(M + 1, 0) == CELLAR_REFUSED && cellar_used_x100(M, 0) == 10000 &&
              cellar_used_x100(M, M - 1) == 0,
          "Cellar: the C used share's own edges (CR1)");
    zxapp_arg_t ua[2] = {{"total", a, WAVE_ROWS}, {"left", b, WAVE_ROWS}};
    CHECK(wave_same(app, "used_share.used_x100", ua, 2, want, WAVE_ROWS, false),
          "Cellar: the volume in use through used_share.used_x100 = C");

    /* each folder's share of the Hoard (banker.share, CR2) and its total (banker.total, CR3) */
    for (uint32_t r = 0; r < 16u; r++) {
        static int64_t size[16], share[16];
        uint32_t n = 2u + r % 14u;
        for (uint32_t i = 0; i < n; i++) size[i] = r == 0 ? 0 : wrand(0, INT64_C(1) << 36);
        int64_t whole = cellar_total(size, n);
        for (uint32_t i = 0; i < n; i++) share[i] = cellar_share_x100(size[i], whole);
        zxapp_arg_t ta = {"size", size, n};
        zxapp_arg_t sa[2] = {{"size", size, n}, {"total", &whole, 1}};
        CHECK(whole >= 0 && wave_same(app, "banker.total", &ta, 1, &whole, 1, false),
              "Cellar: Hoard %u, its bytes through banker.total = C", r);
        CHECK(wave_same(app, "banker.share", sa, 2, share, n, false),
              "Cellar: Hoard %u, each folder's share through banker.share = C", r);
    }
    {
        static const int64_t s[3] = {150, 300, 7}, big[2] = {M, 1};
        CHECK(cellar_total(s, 3) == 457 && cellar_share_x100(150, 457) == 3282 &&
                  cellar_share_x100(300, 457) == 6564 && cellar_share_x100(0, 0) == 0 &&
                  cellar_share_x100(1, 0) == 10000 && cellar_share_x100(2, 0) == CELLAR_REFUSED &&
                  cellar_share_x100(458, 457) == CELLAR_REFUSED &&
                  cellar_share_x100(-1, 457) == CELLAR_REFUSED &&
                  cellar_total(big, 2) == CELLAR_REFUSED && cellar_total(NULL, 0) == 0,
              "Cellar: the C share's and total's own edges (CR2, CR3)");
    }
}
