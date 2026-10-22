/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Ferry's languages against the C its model runs (ferry_rules.h).
 * Slots: @num.ferry_target_room, @logic.ferry_target_fits,
 * @logic.ferry_move_ok, and, used as they are, the Banker's
 * @num.banker_total and @num.banker_share and @num.used_x100. */
#include "ferry_rules.h"

APP_TEST(ferry, 12, 12)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], want[WAVE_ROWS];
    const int64_t M = FERRY_BYTES_MAX;

    /* target_room (FR1) and target_fits (FR2) on the same rows: edges, then swept */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t top = i % 3 ? INT64_C(1) << 40 : M;
        a[i] = wrand(0, top);
        b[i] = wrand(0, top);
        c[i] = i % 2 ? wrand(0, a[i]) : wrand(0, INT64_C(1) << 30);
    }
    a[0] = 0, b[0] = 0, c[0] = 0;        /* nothing at all */
    a[1] = 1000, b[1] = 600, c[1] = 400; /* exactly full */
    a[2] = 1000, b[2] = 600, c[2] = 401; /* one byte short */
    a[3] = M, b[3] = 0, c[3] = 0;        /* the most room */
    a[4] = 0, b[4] = M, c[4] = M;        /* the most short */
    a[5] = M, b[5] = M, c[5] = 0;        /* the most, exactly full */
    a[6] = 5, b[6] = 0, c[6] = 5;        /* only the spare */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = ferry_room(a[i], b[i], c[i]);
    CHECK(want[0] == 0 && want[1] == 0 && want[2] == -1 && want[3] == M && want[4] == -2 * M &&
              want[5] == 0 && want[6] == 0 && ferry_room(-1, 0, 0) == FERRY_NO_ROOM &&
              ferry_room(M + 1, 0, 0) == FERRY_NO_ROOM && ferry_room(0, 0, -1) == FERRY_NO_ROOM,
          "Ferry: the C room's own edges (FR1)");
    zxapp_arg_t ra[3] = {{"avail", a, WAVE_ROWS}, {"need", b, WAVE_ROWS}, {"spare", c, WAVE_ROWS}};
    CHECK(wave_same(app, "target_room", ra, 3, want, WAVE_ROWS, false),
          "Ferry: target_room, Sutra = Trebago = C");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(ferry_fits(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_TRUE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_TRUE &&
              want[6] == ZXAPP_TRUE && !ferry_fits(M + 1, 0, 0) && !ferry_fits(5, -1, 0),
          "Ferry: the C fit's own edges (FR2)");
    CHECK(wave_same(app, "target_fits", ra, 3, want, WAVE_ROWS, true),
          "Ferry: target_fits, Sutra = KRON = C");
    {
        /* past Sutra's 9(15): Sutra refuses, the other answers */
        static const int64_t av[2] = {1000, M + 1}, nd[2] = {600, 0}, sp[2] = {400, 0};
        zxapp_arg_t xa[3] = {{"avail", av, 2}, {"need", nd, 2}, {"spare", sp, 2}};
        CHECK(wave_lp(app, "target_room", xa, 3) == ZXAPP_BOTH,
              "Ferry: room past 9(15): Sutra refuses, Trebago answers: BOTH");
        CHECK(wave_lp(app, "target_fits", xa, 3) == ZXAPP_BOTH,
              "Ferry: fit past 9(15): Sutra refuses, KRON answers: BOTH");
        static const int64_t neg[2] = {1000, -1};
        zxapp_arg_t na[3] = {{"avail", av, 2}, {"need", nd, 2}, {"spare", neg, 2}};
        CHECK(wave_lp(app, "target_room", na, 3) != ZXAPP_BOTH,
              "Ferry: a negative spare: Sutra and Trebago both refuse, no disagreement");
    }

    /* move_ok (FR3): every kind with every one-digit approval, then swept */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i < 20 ? (int64_t) (i / 2) : wrand(0, 9);
        b[i] = i < 20 ? (int64_t) (i % 2) : wrand(0, FERRY_APPROVED_MAX);
        want[i] = T_(ferry_move_ok(a[i], b[i]));
    }
    CHECK(want[0] == ZXAPP_FALSE && want[1] == ZXAPP_FALSE && want[2] == ZXAPP_TRUE &&
              want[6] == ZXAPP_TRUE && want[8] == ZXAPP_FALSE && want[9] == ZXAPP_TRUE &&
              want[10] == ZXAPP_FALSE && want[11] == ZXAPP_TRUE && want[12] == ZXAPP_FALSE &&
              want[13] == ZXAPP_FALSE && !ferry_move_ok(FERRY_K_KEY, 2) &&
              !ferry_move_ok(FERRY_K_FILE, 10) && !ferry_move_ok(-1, 1),
          "Ferry: the C rule's own edges (FR3)");
    zxapp_arg_t ma[2] = {{"kind", a, WAVE_ROWS}, {"approved", b, WAVE_ROWS}};
    CHECK(wave_same(app, "move_ok", ma, 2, want, WAVE_ROWS, true),
          "Ferry: move_ok, Sutra = KRON = C");
    {
        static const int64_t k2[2] = {1, 1}, ap[2] = {0, 10};
        zxapp_arg_t xa[2] = {{"kind", k2, 2}, {"approved", ap, 2}};
        CHECK(wave_lp(app, "move_ok", xa, 2) == ZXAPP_BOTH,
              "Ferry: an approval of 10: Sutra refuses, KRON answers: BOTH");
    }

    /* banker.total, banker.share and used_share.used_x100 over plans: the model's C */
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t size[16], share[16], whole[1], left[2], tot[2], prog[2];
        uint32_t n = 1u + r % 16u;
        for (uint32_t i = 0; i < n; i++)
            size[i] = r == 0 ? INT64_C(1) << 36 : wrand(0, INT64_C(1) << 36);
        whole[0] = ferry_total(size, n);
        for (uint32_t i = 0; i < n; i++) share[i] = ferry_share_x100(size[i], whole[0]);
        tot[0] = tot[1] = whole[0] ? whole[0] : 1;
        left[0] = wrand(0, tot[0]);
        left[1] = r == 1 ? tot[1] : 0; /* nothing packed, all packed */
        for (uint32_t i = 0; i < 2; i++) prog[i] = ferry_progress_x100(tot[i], left[i]);
        zxapp_arg_t ta[1] = {{"size", size, n}};
        zxapp_arg_t sa[2] = {{"size", size, n}, {"total", whole, 1}};
        zxapp_arg_t ua[2] = {{"total", tot, 2}, {"left", left, 2}};
        CHECK(whole[0] >= 0 && wave_same(app, "banker.total", ta, 1, whole, 1, false),
              "Ferry: plan %u of %u spaces, its bytes through banker.total", r, n);
        CHECK(wave_same(app, "banker.share", sa, 2, share, n, false),
              "Ferry: plan %u, each space's share through banker.share", r);
        CHECK(wave_same(app, "used_share.used_x100", ua, 2, prog, 2, false),
              "Ferry: plan %u, progress through used_share.used_x100", r);
    }
    {
        static const int64_t s2[2] = {5, 11}, t1[1] = {10}, tt[2] = {1000, 1000}, lf[2] = {0, 1001};
        zxapp_arg_t sa[2] = {{"size", s2, 2}, {"total", t1, 1}};
        CHECK(wave_lp(app, "banker.share", sa, 2) == ZXAPP_BOTH &&
                  ferry_share_x100(11, 10) == FERRY_REFUSED,
              "Ferry: a space past its plan: C++ refuses, KRON answers: BOTH");
        zxapp_arg_t ua[2] = {{"total", tt, 2}, {"left", lf, 2}};
        CHECK(wave_lp(app, "used_share.used_x100", ua, 2) == ZXAPP_BOTH &&
                  ferry_progress_x100(1000, 1001) == FERRY_REFUSED,
              "Ferry: more left than the plan: Sutra refuses, KRON answers: BOTH");
    }
    CHECK(ferry_share_x100(0, 0) == 0 && ferry_share_x100(M, M) == 10000 &&
              ferry_share_x100(1, 3) == 3333 && ferry_progress_x100(M, 0) == 10000 &&
              ferry_progress_x100(0, 0) == FERRY_REFUSED && ferry_total(NULL, 0) == FERRY_REFUSED,
          "Ferry: the C share's, progress's and total's own edges (FR4-FR6)");
}
