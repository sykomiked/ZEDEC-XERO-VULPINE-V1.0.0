/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Lore's languages against the C its model runs (lore_rules.h).
 * Slots: @logic.lore_ai_read_ok, @logic.lore_leave_record_ok and, used as
 * it is, @num.lockbox_days_left. */
#include "lore_rules.h"

APP_TEST(lore, 8, 67)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];

    /* the C rules' own edges */
    CHECK(lore_room(0, 0) == 0 && lore_room(4, 0) == 4 && lore_room(0, 4) == -4 &&
              lore_room(1, 2) == -1 && lore_room(5, 0) == LORE_REFUSED &&
              lore_room(0, -1) == LORE_REFUSED && lore_room(-1, 0) == LORE_REFUSED,
          "Lore: the room between a space and a leave's reach (LR1)");
    CHECK(lore_leave_fits(0, 0, 0, 0, 1) && lore_leave_fits(999, 4, 32, 0, LOCKBOX_STAMP_MAX) &&
              !lore_leave_fits(0, 0, 0, 5, 5) && !lore_leave_fits(0, 5, 0, 0, 1) &&
              !lore_leave_fits(0, 0, 33, 0, 1) && !lore_leave_fits(1000, 0, 0, 0, 1) &&
              !lore_leave_fits(0, 0, 0, -1, 1) &&
              !lore_leave_fits(0, 0, 0, 0, LOCKBOX_STAMP_MAX + 1) &&
              !lore_leave_fits(-1, 0, 0, 0, 1),
          "Lore: a leave the owner may give (LR4)");
    CHECK(lore_leave_days_left(1760000000 + 30 * 86400, 1760000000) == 30 &&
              lore_leave_days_left(1760000000, 1760000000) == 0 &&
              lore_leave_days_left(1760000000, 1760000001) == 0 &&
              lore_leave_days_left(-1, 0) == LOCKBOX_REFUSED,
          "Lore: days left on a leave are Lockbox's (LR2)");

    /* ai_read_ok (LR1): Eve = KRON = C, swept near every edge */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t k = wrand(0, 3);
        a[i] = k == 0 ? wrand(-1, 1) : 1;
        b[i] = k == 1 ? wrand(-1, 1) : 1;
        c[i] = k == 2 ? wrand(-6, 6) : wrand(0, LORE_ROOM_MAX);
    }
    a[0] = 1, b[0] = 1, c[0] = 0;                 /* the same kind of space */
    a[1] = 1, b[1] = 1, c[1] = LORE_ROOM_MAX;     /* a park under a home leave */
    a[2] = 0, b[2] = 1, c[2] = 0;                 /* no live leave */
    a[3] = 1, b[3] = 0, c[3] = 0;                 /* the folder not granted */
    a[4] = 1, b[4] = 1, c[4] = -1;                /* a more private space */
    a[5] = 1, b[5] = 1, c[5] = LORE_ROOM_MAX + 1; /* no space is that open */
    a[6] = -1, b[6] = -1, c[6] = 0;               /* no flags */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(lore_ai_read_ok(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE,
          "Lore: the C read rule's own edges (LR1)");
    zxapp_arg_t ra[3] = {
        {"leave", a, WAVE_ROWS}, {"granted", b, WAVE_ROWS}, {"room", c, WAVE_ROWS}};
    CHECK(wave_same(app, "ai_read_ok", ra, 3, want, WAVE_ROWS, true),
          "Lore: ai_read_ok, Eve = KRON = C");
    {
        /* a leave flag of 2 is no flag: Eve says no, KRON reads it as given */
        static const int64_t lv[2] = {1, 2}, gr[2] = {1, 1}, rm[2] = {0, 0};
        zxapp_arg_t xa[3] = {{"leave", lv, 2}, {"granted", gr, 2}, {"room", rm, 2}};
        CHECK(wave_lp(app, "ai_read_ok", xa, 3) == ZXAPP_BOTH && !lore_ai_read_ok(2, 1, 0),
              "Lore: ai_read_ok with a leave flag of 2: Eve no, KRON yes: BOTH");
        static const int64_t lv2[2] = {1, 1}, gr2[2] = {1, 3};
        zxapp_arg_t ya[3] = {{"leave", lv2, 2}, {"granted", gr2, 2}, {"room", rm, 2}};
        CHECK(wave_lp(app, "ai_read_ok", ya, 3) == ZXAPP_BOTH && !lore_ai_read_ok(1, 3, 0),
              "Lore: ai_read_ok with a grant flag of 3: Eve no, KRON yes: BOTH");
    }

    /* leave_record_ok (LR3): MYRA = C, each piece at its edges */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2 ? wrand(0, LORE_LEAVE_FIELD) : wrand(-1100, 1100);
        b[i] = wrand(-11, 11);
        c[i] = i % 3 ? wrand(0, LORE_DEPTH_MAX) : wrand(-110, 110);
        d[i] = i % 4 ? wrand(1600000000, 2000000000) : wrand(-1100000000000, 1100000000000);
        e[i] = i % 5 ? wrand(1600000000, 2000000000) : wrand(-1100000000000, 1100000000000);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0; /* every least */
    a[1] = LORE_LEAVE_FIELD, b[1] = LORE_KIND_FIELD, c[1] = LORE_DEPTH_FIELD;
    d[1] = LORE_STAMP_FIELD, e[1] = LORE_STAMP_FIELD;                      /* every most */
    a[2] = LORE_LEAVE_FIELD + 1, b[2] = 0, c[2] = 0, d[2] = 0, e[2] = 0;   /* leave past i3 */
    a[3] = 1, b[3] = LORE_KIND_FIELD + 1, c[3] = 0, d[3] = 0, e[3] = 0;    /* reach past i1 */
    a[4] = 1, b[4] = 0, c[4] = LORE_DEPTH_FIELD + 1, d[4] = 0, e[4] = 0;   /* depth past i2 */
    a[5] = 1, b[5] = 0, c[5] = 0, d[5] = LORE_STAMP_FIELD + 1, e[5] = 0;   /* given past i12 */
    a[6] = 1, b[6] = 0, c[6] = 0, d[6] = 0, e[6] = LORE_STAMP_FIELD + 1;   /* expires past i12 */
    a[7] = -LORE_LEAVE_FIELD, b[7] = -9, c[7] = -99, d[7] = -1, e[7] = -1; /* sign aside */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(lore_leave_record_ok(a[i], b[i], c[i], d[i], e[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_TRUE,
          "Lore: the C record's own edges (LR3)");
    zxapp_arg_t la[5] = {{"leave", a, WAVE_ROWS},
                         {"reach", b, WAVE_ROWS},
                         {"depth", c, WAVE_ROWS},
                         {"given", d, WAVE_ROWS},
                         {"expires", e, WAVE_ROWS}};
    CHECK(wave_same(app, "leave_record_ok", la, 5, want, WAVE_ROWS, true),
          "Lore: leave_record_ok, MYRA = C");

    /* lockbox.days_left over a leave's life (LR2): C++ = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t given = wrand(1600000000, 2000000000);
        a[i] = given + wrand(1, 400) * 86400 + wrand(0, 86399);
        b[i] = given + wrand(0, 500 * 86400);
        want[i] = lore_leave_days_left(a[i], b[i]);
    }
    a[0] = 1762592000, b[0] = 1760000000; /* 30 days, seen when given */
    a[1] = 1762592000, b[1] = 1762591999; /* its last second */
    a[2] = 1762592000, b[2] = 1762592001; /* past */
    want[0] = lore_leave_days_left(a[0], b[0]);
    want[1] = lore_leave_days_left(a[1], b[1]);
    want[2] = lore_leave_days_left(a[2], b[2]);
    CHECK(want[0] == 30 && want[1] == 0 && want[2] == 0, "Lore: a leave's days, its edges");
    zxapp_arg_t da[2] = {{"expires", a, WAVE_ROWS}, {"now", b, WAVE_ROWS}};
    CHECK(wave_same(app, "lockbox.days_left", da, 2, want, WAVE_ROWS, false),
          "Lore: a leave's days through lockbox.days_left, C++ = KRON = C");
}
