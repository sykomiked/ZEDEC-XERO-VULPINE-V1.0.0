/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Hamlet's languages against the C its model runs (hamlet_rules.h).
 * Slots: @num.hamlet_gift_room, @logic.hamlet_post_show_ok,
 * @logic.hamlet_read_marker_ok, and, used as they are,
 * @num.commons_room_unread and @logic.ramble_consent_covers. */
#include "commons_rules.h"
#include "hamlet_rules.h"
#include "ramble_rules.h"

APP_TEST(hamlet, 16, 96)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], want[WAVE_ROWS];

    /* gift_room (HR1): Sutra = C++ (wasm) = C, from the least gift to 18 digits */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        b[i] = i % 2 ? wrand(1, 1000000) : wrand(1, HAMLET_MINOR_MAX);
        a[i] = i % 3 ? wrand(1, b[i]) : b[i];
    }
    a[0] = 1, b[0] = 1;                               /* the least gift, all of the ceiling */
    a[1] = 1, b[1] = HAMLET_MINOR_MAX;                /* the least gift, the most ceiling */
    a[2] = HAMLET_MINOR_MAX, b[2] = HAMLET_MINOR_MAX; /* the most gift */
    a[3] = 2500, b[3] = 10000;                        /* 25.00 of 100.00 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = hamlet_gift_room(a[i], b[i]);
    CHECK(want[0] == 0 && want[1] == HAMLET_MINOR_MAX - 1 && want[2] == 0 && want[3] == 7500 &&
              hamlet_gift_room(0, 5) == HAMLET_REFUSED &&
              hamlet_gift_room(-1, 5) == HAMLET_REFUSED &&
              hamlet_gift_room(6, 5) == HAMLET_REFUSED &&
              hamlet_gift_room(1, HAMLET_MINOR_MAX + 1) == HAMLET_REFUSED,
          "Hamlet: the C gift rule's own edges (HR1)");
    zxapp_arg_t ga[2] = {{"amount", a, WAVE_ROWS}, {"ceiling", b, WAVE_ROWS}};
    CHECK(wave_same(app, "gift_room", ga, 2, want, WAVE_ROWS, false),
          "Hamlet: gift_room, Sutra = C++ = C");
    {
        /* past 18 digits: Sutra refuses, C++ answers: held */
        static const int64_t x[2][2] = {{1, 1}, {1, HAMLET_MINOR_MAX + 1}};
        static const int64_t y[2][2] = {{1, HAMLET_MINOR_MAX + 1}, {1, HAMLET_MINOR_MAX + 1}};
        for (uint32_t k = 0; k < 2; k++) {
            zxapp_arg_t xa[2] = {{"amount", x[k], 2}, {"ceiling", y[k], 2}};
            CHECK(wave_lp(app, "gift_room", xa, 2) == ZXAPP_BOTH &&
                      hamlet_gift_room(x[k][1], y[k][1]) == HAMLET_REFUSED,
                  "Hamlet: gift_room case %u: Sutra refuses, C++ answers: BOTH", k);
        }
        /* no gift, or past the ceiling: both refuse, nothing to show */
        static const int64_t z[2] = {1, 0}, o[2] = {1, 6}, cz[2] = {5, 5};
        zxapp_arg_t za[2] = {{"amount", z, 2}, {"ceiling", cz, 2}};
        zxapp_arg_t oa[2] = {{"amount", o, 2}, {"ceiling", cz, 2}};
        CHECK(wave_lp(app, "gift_room", za, 2) == ZXAPP_NEITHER &&
                  wave_lp(app, "gift_room", oa, 2) == ZXAPP_NEITHER,
              "Hamlet: a gift of 0, or past the ceiling: both refuse: NEITHER");
    }

    /* post_show_ok (HR2): Eve = MYRA = C over every role and mute count */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = hamlet_role_room(wrand(-1, HAMLET_ROLE_OWNER));
        b[i] = hamlet_mute_room(i % 3 ? 0 : wrand(0, 2));
    }
    a[0] = hamlet_role_room(HAMLET_ROLE_MEMBER), b[0] = hamlet_mute_room(0);
    a[1] = hamlet_role_room(HAMLET_ROLE_OWNER), b[1] = hamlet_mute_room(0);
    a[2] = hamlet_role_room(-1), b[2] = hamlet_mute_room(0);                /* a post to no group */
    a[3] = hamlet_role_room(HAMLET_ROLE_OBSERVER), b[3] = 0;                /* no member */
    a[4] = hamlet_role_room(HAMLET_ROLE_NONE), b[4] = 0;                    /* not in the group */
    a[5] = hamlet_role_room(HAMLET_ROLE_ADMIN), b[5] = hamlet_mute_room(1); /* muted */
    a[6] = hamlet_role_room(HAMLET_ROLE_MEMBER), b[6] = hamlet_mute_room(2);
    a[7] = HAMLET_DAY_MAX + 1, b[7] = 0; /* past MYRA's day and Eve's role: both no */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(hamlet_post_show_ok(a[i], b[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_TRUE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE,
          "Hamlet: the C show rule's own edges (HR2)");
    zxapp_arg_t sa[2] = {{"roleroom", a, WAVE_ROWS}, {"muteroom", b, WAVE_ROWS}};
    CHECK(wave_same(app, "post_show_ok", sa, 2, want, WAVE_ROWS, true),
          "Hamlet: post_show_ok, Eve = MYRA = C");
    {
        /* a margin past its meaning that MYRA reads as a day: Eve no, MYRA yes */
        static const int64_t r[3][2] = {{0, 3}, {0, 0}, {0, HAMLET_DAY_MAX}};
        static const int64_t m[3][2] = {{0, 0}, {0, 1}, {0, 0}};
        for (uint32_t k = 0; k < 3; k++) {
            zxapp_arg_t xa[2] = {{"roleroom", r[k], 2}, {"muteroom", m[k], 2}};
            CHECK(wave_lp(app, "post_show_ok", xa, 2) == ZXAPP_BOTH &&
                      !hamlet_post_show_ok(r[k][1], m[k][1]),
                  "Hamlet: post_show_ok case %u: Eve says no, MYRA fits: BOTH", k);
        }
    }

    /* read_marker_ok (HR3): MYRA = C, each piece at its edges */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2 ? wrand(0, 31) : wrand(-1100, 1100);
        b[i] = i % 3 ? wrand(0, 999999999) : wrand(-HAMLET_LAMPORT_FIELD, HAMLET_LAMPORT_FIELD);
        c[i] = i % 4 ? wrand(1600000000, 2000000000) : wrand(-1100000000000, 1100000000000);
    }
    a[0] = 0, b[0] = 0, c[0] = 0; /* every least */
    a[1] = HAMLET_GROUP_FIELD, b[1] = HAMLET_LAMPORT_FIELD, c[1] = HAMLET_STAMP_FIELD;
    a[2] = HAMLET_GROUP_FIELD + 1, b[2] = 1, c[2] = 0;                 /* group past i3 */
    a[3] = 1, b[3] = HAMLET_LAMPORT_FIELD + 1, c[3] = 0;               /* lamport past i18 */
    a[4] = 1, b[4] = 1, c[4] = HAMLET_STAMP_FIELD + 1;                 /* stamp past i12 */
    a[5] = -HAMLET_GROUP_FIELD, b[5] = -1, c[5] = -HAMLET_STAMP_FIELD; /* sign aside */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(hamlet_read_marker_ok(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_TRUE,
          "Hamlet: the C marker's own edges (HR3)");
    zxapp_arg_t ma[3] = {
        {"group", a, WAVE_ROWS}, {"lamport", b, WAVE_ROWS}, {"stamp", c, WAVE_ROWS}};
    CHECK(wave_same(app, "read_marker_ok", ma, 3, want, WAVE_ROWS, true),
          "Hamlet: read_marker_ok, MYRA = C");

    /* commons.room_unread: a group's unread, KRON = Trebago = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 5000);
        b[i] = wrand(0, a[i] + 5);
        want[i] = commons_room_unread(a[i], b[i]);
    }
    zxapp_arg_t ua[2] = {{"newest", a, WAVE_ROWS}, {"seen", b, WAVE_ROWS}};
    CHECK(wave_same(app, "commons.room_unread", ua, 2, want, WAVE_ROWS, false),
          "Hamlet: a group's unread through commons.room_unread, KRON = Trebago = C");

    /* ramble.consent_covers: a gift's grant, Eve = Sutra = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t ceiling = wrand(0, 1000000), amount = wrand(1, 1000000);
        a[i] = ramble_scope_gap(ramble_need(false, amount), RAMBLE_SCOPE_MONEY);
        b[i] = ramble_margin(ceiling, amount);
        c[i] = wrand(-2, RAMBLE_GRANT_MAX + 2);
        want[i] = T_(ramble_consent_covers(a[i], b[i], c[i]));
    }
    zxapp_arg_t ca[3] = {
        {"scopegap", a, WAVE_ROWS}, {"ceilingleft", b, WAVE_ROWS}, {"secondsleft", c, WAVE_ROWS}};
    CHECK(wave_same(app, "ramble.consent_covers", ca, 3, want, WAVE_ROWS, true),
          "Hamlet: a gift's grant through ramble.consent_covers, Eve = Sutra = C");
}
