/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Meadow's languages against the C its model runs (meadow_rules.h).
 * Slots: @logic.meadow_seat_post_ok, @num.meadow_space_new_events,
 * @num.meadow_space_members_total, @logic.meadow_joined_space_ok and,
 * used as it is, @num.seedbank_cid_byte. */
#include "meadow_rules.h"
#include "seedbank_rules.h"

/* one language alone: does it refuse these rows? */
static bool md_refused(zxapp_t *app, const char *entry, const zxapp_arg_t *args, uint32_t nargs)
{
    zxapp_num_t out[WAVE_ROWS];
    uint32_t n = 0;
    int32_t lp = -1;
    return zxapp_call_witnessed(app, &H, entry, args, nargs, out, WAVE_ROWS, &n, &lp) != ZXAPP_OK;
}

APP_TEST(meadow, 12, 86)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];

    /* seat_post_ok (MR1, MR2): Eve = MYRA = C, every kind, role and size around the edges */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t kind = wrand(0, MEADOW_KINDS - 1), role = wrand(0, MEADOW_ROLES - 1);
        int64_t bytes = i % 3 ? wrand(1, MEADOW_POST_MAX) : wrand(-5, MEADOW_POST_MAX + 5);
        a[i] = meadow_role_room(kind, role);
        b[i] = meadow_len_left(bytes);
        c[i] = meadow_len_past(bytes);
    }
    a[0] = 0, b[0] = MEADOW_POST_MAX - 1, c[0] = 0; /* 1 byte, the least room */
    a[1] = 3, b[1] = 0, c[1] = MEADOW_POST_MAX - 1; /* 1024 bytes, a park's owner */
    a[2] = meadow_role_room(MEADOW_PARK, MEADOW_PUBLIC);
    b[2] = meadow_len_left(MEADOW_POST_MAX + 1), c[2] = meadow_len_past(MEADOW_POST_MAX + 1);
    a[3] = meadow_role_room(MEADOW_PARK, MEADOW_PUBLIC);
    b[3] = meadow_len_left(0), c[3] = meadow_len_past(0);    /* an empty post */
    a[4] = meadow_role_room(MEADOW_CLOUDTOP, MEADOW_PUBLIC); /* -3: the public reads a top */
    b[4] = 500, c[4] = 523;
    a[5] = meadow_role_room(MEADOW_HOME, MEADOW_MEMBER), b[5] = 0, c[5] = 1023; /* -1 */
    a[6] = meadow_role_room(MEADOW_PARK, MEADOW_PUBLIC);
    b[6] = meadow_len_left(-1), c[6] = meadow_len_past(-1); /* a negative size */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(meadow_seat_post_ok(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && !meadow_seat_post_ok(4, 0, 0) &&
              !meadow_seat_post_ok(0, 1024, 0) && !meadow_seat_post_ok(0, 0, 1024),
          "Meadow: the C post rule's own edges (MR2)");
    zxapp_arg_t sa[3] = {
        {"roleroom", a, WAVE_ROWS}, {"lenleft", b, WAVE_ROWS}, {"lenpast", c, WAVE_ROWS}};
    CHECK(wave_same(app, "seat_post_ok", sa, 3, want, WAVE_ROWS, true),
          "Meadow: seat_post_ok, Eve = MYRA = C");
    {
        /* a margin past its meaning (max + 1) is a day to MYRA and no seat or post to Eve */
        static const int64_t r[3][2] = {{0, 4}, {0, 0}, {0, 0}};
        static const int64_t l[3][2] = {{1023, 1023}, {1023, 1024}, {1023, 0}};
        static const int64_t p[3][2] = {{0, 0}, {0, 0}, {0, 1024}};
        for (uint32_t k = 0; k < 3; k++) {
            zxapp_arg_t xa[3] = {{"roleroom", r[k], 2}, {"lenleft", l[k], 2}, {"lenpast", p[k], 2}};
            CHECK(wave_lp(app, "seat_post_ok", xa, 3) == ZXAPP_BOTH &&
                      !meadow_seat_post_ok(r[k][1], l[k][1], p[k][1]),
                  "Meadow: seat_post_ok case %u: MYRA says yes, Eve no: BOTH", k);
        }
    }

    /* space_new_events (MR3): Trebago = C, one space per row */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 3 ? wrand(0, 100000) : wrand(0, MEADOW_INDEX_MAX);
        b[i] = i % 4 ? a[i] + wrand(0, 40) : wrand(a[i], MEADOW_INDEX_MAX);
    }
    a[0] = 0, b[0] = 0;                               /* nothing yet */
    a[1] = 0, b[1] = MEADOW_INDEX_MAX;                /* the most */
    a[2] = MEADOW_INDEX_MAX, b[2] = MEADOW_INDEX_MAX; /* all read at the top */
    a[3] = 3, b[3] = 40;                              /* past the 32 kept */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = meadow_space_new_events(a[i], b[i]);
    CHECK(want[0] == 0 && want[1] == MEADOW_INDEX_MAX && want[2] == 0 && want[3] == 37 &&
              meadow_space_new_events(-1, 0) == MEADOW_REFUSED &&
              meadow_space_new_events(5, 4) == MEADOW_REFUSED &&
              meadow_space_new_events(0, MEADOW_INDEX_MAX + 1) == MEADOW_REFUSED,
          "Meadow: the C new-events rule's own edges (MR3)");
    zxapp_arg_t na[2] = {{"since", a, WAVE_ROWS}, {"next", b, WAVE_ROWS}};
    CHECK(wave_same(app, "space_new_events", na, 2, want, WAVE_ROWS, false),
          "Meadow: space_new_events, Trebago = C");
    {
        /* below 0, next before since, past 18 digits: refused, as the C refuses */
        static const int64_t s[3][2] = {{0, -1}, {0, 5}, {0, 0}};
        static const int64_t x[3][2] = {{1, 0}, {1, 4}, {1, MEADOW_INDEX_MAX + 1}};
        for (uint32_t k = 0; k < 3; k++) {
            zxapp_arg_t xa[2] = {{"since", s[k], 2}, {"next", x[k], 2}};
            CHECK(md_refused(app, "space_new_events", xa, 2) &&
                      meadow_space_new_events(s[k][1], x[k][1]) == MEADOW_REFUSED,
                  "Meadow: space_new_events case %u refused by Trebago and C", k);
        }
    }

    /* space_members_total (MR4): Trebago = C, 1 to 8 spaces (pspace.list's most) */
    for (uint32_t r = 0; r < 16; r++) {
        int64_t m[MEADOW_SPACE_PEOPLE];
        uint32_t n = 1u + r % 8u;
        for (uint32_t i = 0; i < n; i++)
            m[i] = r == 0 ? MEADOW_SPACE_PEOPLE : wrand(0, MEADOW_SPACE_PEOPLE);
        int64_t w = meadow_space_members_total(m, n);
        zxapp_arg_t ma = {"members", m, n};
        CHECK(w >= 0 && wave_same(app, "space_members_total", &ma, 1, &w, 1, false),
              "Meadow: the people across %u spaces, Trebago = C", n);
    }
    {
        static const int64_t over[2] = {1, MEADOW_SPACE_PEOPLE + 1}, under[2] = {1, -1};
        zxapp_arg_t oa = {"members", over, 2}, ua = {"members", under, 2};
        CHECK(md_refused(app, "space_members_total", &oa, 1) &&
                  md_refused(app, "space_members_total", &ua, 1) &&
                  meadow_space_members_total(over, 2) == MEADOW_REFUSED &&
                  meadow_space_members_total(under, 2) == MEADOW_REFUSED,
              "Meadow: 34 people or below 0 in a space refuses the whole sum");
    }

    /* joined_space_ok (MR5): MYRA = C, each piece at its edges */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-11, 11);
        b[i] = wrand(-11, 11);
        c[i] = wrand(-110, 110);
        d[i] = i % 2 ? wrand(0, 999999) : wrand(-MEADOW_INDEX_MAX, MEADOW_INDEX_MAX);
        e[i] = i % 3 ? wrand(1600000000000, 2000000000000) : wrand(-11000000000000, 11000000000000);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0;                             /* every least */
    a[1] = 9, b[1] = 9, c[1] = 99, d[1] = MEADOW_INDEX_MAX, e[1] = 9999999999999; /* every most */
    a[2] = 10, b[2] = 3, c[2] = 1, d[2] = 0, e[2] = 0;                   /* space past i1 */
    a[3] = 1, b[3] = 10, c[3] = 1, d[3] = 0, e[3] = 0;                   /* phase past i1 */
    a[4] = 1, b[4] = 3, c[4] = 100, d[4] = 0, e[4] = 0;                  /* members past i2 */
    a[5] = 1, b[5] = 3, c[5] = 1, d[5] = MEADOW_INDEX_MAX + 1, e[5] = 0; /* last past i18 */
    a[6] = 1, b[6] = 3, c[6] = 1, d[6] = 0, e[6] = 10000000000000;       /* stamp past i13 */
    a[7] = -9, b[7] = -1, c[7] = -99, d[7] = -5, e[7] = -1;              /* sign aside, they fit */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(meadow_joined_space_ok(a[i], b[i], c[i], d[i], e[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_TRUE,
          "Meadow: the joined space record's own edges (MR5)");
    zxapp_arg_t ja[5] = {{"space", a, WAVE_ROWS},
                         {"phase", b, WAVE_ROWS},
                         {"members", c, WAVE_ROWS},
                         {"last", d, WAVE_ROWS},
                         {"stamp", e, WAVE_ROWS}};
    CHECK(wave_same(app, "joined_space_ok", ja, 5, want, WAVE_ROWS, true),
          "Meadow: joined_space_ok, MYRA = C");

    /* seedbank.cid_byte, used as it is: every byte of a space CID's text, and bytes that
     * do not fit, against Seedbank's C */
    {
        static const char CID[] = "bafkreie26lgim2eakycklw4igbbblffbzqt3lsd6drxydsankm3mr73hiy";
        CHECK(seedbank_cid_ok(CID, (uint32_t) strlen(CID)), "Meadow: a CID text fits SR2");
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = i < 59u ? (int64_t) i : wrand(0, 58);
            b[i] = i < 59u ? (unsigned char) CID[i] : wrand(32, 126);
            c[i] = i == 63u ? 58 : 59;
            want[i] = seedbank_cid_byte(a[i], b[i], c[i]);
        }
        zxapp_arg_t ca[3] = {{"pos", a, WAVE_ROWS}, {"code", b, WAVE_ROWS}, {"len", c, WAVE_ROWS}};
        CHECK(wave_same(app, "seedbank.cid_byte", ca, 3, want, WAVE_ROWS, false),
              "Meadow: a space CID's bytes, Seedbank's rule");
    }
}
