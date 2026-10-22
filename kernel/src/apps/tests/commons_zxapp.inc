/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Commons's languages against the C its model runs (commons_rules.h).
 * Slots: @logic.commons_room_post_ok, @logic.commons_message_text_ok,
 * @logic.commons_reaction_text_ok, @num.commons_room_unread,
 * @logic.commons_room_record_ok, @logic.commons_message_record_ok and,
 * used as it is, @num.witness_kind_count. */
#include "commons_rules.h"
#include "witness_rules.h"

APP_TEST(commons, 13, 27)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];

    /* room_post_ok (CR1): Eve = KRON = C, every flag 0 or 1, plus rows below 0 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1);
        b[i] = wrand(0, 1);
        c[i] = i % 5 == 4 ? wrand(-3, 0) : wrand(0, 1);
    }
    a[0] = 1, b[0] = 1, c[0] = 1;  /* the only yes */
    a[1] = 0, b[1] = 1, c[1] = 1;  /* not a member */
    a[2] = 1, b[2] = 0, c[2] = 1;  /* not joined */
    a[3] = 1, b[3] = 1, c[3] = 0;  /* the switch off */
    a[4] = 0, b[4] = 0, c[4] = 0;  /* nothing */
    a[5] = -1, b[5] = 1, c[5] = 1; /* below 0 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(commons_room_post_ok(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              !commons_room_post_ok(2, 1, 1),
          "Commons: the C post rule's own edges (CR1)");
    zxapp_arg_t pa[3] = {
        {"member", a, WAVE_ROWS}, {"joined", b, WAVE_ROWS}, {"enabled", c, WAVE_ROWS}};
    CHECK(wave_same(app, "room_post_ok", pa, 3, want, WAVE_ROWS, true),
          "Commons: room_post_ok, Eve = KRON = C");
    {
        /* a flag of 2 (max + 1): Eve holds it to 1 and says no, KRON reads it as yes */
        static const int64_t m[3][2] = {{1, 2}, {1, 1}, {1, 1}};
        static const int64_t j[3][2] = {{1, 1}, {1, 2}, {1, 1}};
        static const int64_t s[3][2] = {{1, 1}, {1, 1}, {1, 2}};
        for (uint32_t k = 0; k < 3; k++) {
            zxapp_arg_t xa[3] = {{"member", m[k], 2}, {"joined", j[k], 2}, {"enabled", s[k], 2}};
            CHECK(wave_lp(app, "room_post_ok", xa, 3) == ZXAPP_BOTH &&
                      !commons_room_post_ok(m[k][1], j[k][1], s[k][1]),
                  "Commons: room_post_ok flag %u at 2: Eve says no, KRON yes: BOTH", k);
        }
    }

    /* message_text_ok and reaction_text_ok (CR2): Eve = C, swept across each edge */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-5, (int64_t) COMMONS_TEXT_MAX + 5);
        b[i] = wrand(-3, (int64_t) COMMONS_REACT_MAX + 3);
    }
    a[0] = 0, a[1] = 1, a[2] = COMMONS_TEXT_MAX, a[3] = COMMONS_TEXT_MAX + 1, a[4] = -1;
    a[5] = INT64_MAX;
    b[0] = 0, b[1] = 1, b[2] = COMMONS_REACT_MAX, b[3] = COMMONS_REACT_MAX + 1, b[4] = -1;
    b[5] = INT64_MIN;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        want[i] = T_(commons_message_text_ok(a[i]));
        d[i] = T_(commons_reaction_text_ok(b[i]));
    }
    CHECK(want[0] == ZXAPP_FALSE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_TRUE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && d[0] == ZXAPP_FALSE &&
              d[1] == ZXAPP_TRUE && d[2] == ZXAPP_TRUE && d[3] == ZXAPP_FALSE,
          "Commons: the C text limits' own edges (CR2)");
    zxapp_arg_t ta = {"bytes", a, WAVE_ROWS};
    CHECK(wave_same(app, "message_text_ok", &ta, 1, want, WAVE_ROWS, true),
          "Commons: message_text_ok, Eve = C");
    zxapp_arg_t ra = {"bytes", b, WAVE_ROWS};
    CHECK(wave_same(app, "reaction_text_ok", &ra, 1, d, WAVE_ROWS, true),
          "Commons: reaction_text_ok, Eve = C");

    /* room_unread (CR3): KRON = Trebago = C, one room per row */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 3 ? wrand(0, 1000) : wrand(0, COMMONS_COUNT_MAX);
        b[i] = i % 4 ? wrand(0, a[i] + 5) : wrand(0, COMMONS_COUNT_MAX);
    }
    a[0] = 0, b[0] = 0;                                 /* an empty room */
    a[1] = COMMONS_COUNT_MAX, b[1] = 0;                 /* the most unread */
    a[2] = COMMONS_COUNT_MAX, b[2] = COMMONS_COUNT_MAX; /* all read at the top */
    a[3] = 3, b[3] = 7;                                 /* read past the known: none */
    a[4] = 0, b[4] = COMMONS_COUNT_MAX;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = commons_room_unread(a[i], b[i]);
    CHECK(want[0] == 0 && want[1] == COMMONS_COUNT_MAX && want[2] == 0 && want[3] == 0 &&
              want[4] == 0 && commons_room_unread(-1, 0) == COMMONS_REFUSED &&
              commons_room_unread(0, COMMONS_COUNT_MAX + 1) == COMMONS_REFUSED,
          "Commons: the C unread rule's own edges (CR3)");
    zxapp_arg_t ua[2] = {{"newest", a, WAVE_ROWS}, {"seen", b, WAVE_ROWS}};
    CHECK(wave_same(app, "room_unread", ua, 2, want, WAVE_ROWS, false),
          "Commons: room_unread, KRON = Trebago = C");
    {
        /* past 12 digits, or below 0: Trebago refuses, KRON answers */
        static const int64_t nw[3][2] = {{5, COMMONS_COUNT_MAX + 1}, {5, 5}, {5, -1}};
        static const int64_t sn[3][2] = {{0, 0}, {0, -1}, {0, 0}};
        for (uint32_t k = 0; k < 3; k++) {
            zxapp_arg_t xa[2] = {{"newest", nw[k], 2}, {"seen", sn[k], 2}};
            CHECK(wave_lp(app, "room_unread", xa, 2) == ZXAPP_BOTH &&
                      commons_room_unread(nw[k][1], sn[k][1]) == COMMONS_REFUSED,
                  "Commons: room_unread case %u: Trebago refuses, KRON answers: BOTH", k);
        }
    }

    /* room_record_ok (CR4): MYRA = C, each piece at its edges */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1100, 1100);
        b[i] = wrand(-11, 11);
        c[i] = i % 2 ? wrand(0, 999999) : wrand(-1100000000000, 1100000000000);
        d[i] = i % 3 ? wrand(0, 999999) : wrand(-1100000000000, 1100000000000);
        e[i] = wrand(-1100000, 1100000);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0; /* every least */
    a[1] = COMMONS_ROOM_FIELD, b[1] = 9, c[1] = COMMONS_COUNT_MAX, d[1] = COMMONS_COUNT_MAX;
    e[1] = COMMONS_MEMBERS_MAX;                                            /* every most */
    a[2] = COMMONS_ROOM_FIELD + 1, b[2] = 0, c[2] = 0, d[2] = 0, e[2] = 0; /* room past i3 */
    a[3] = 1, b[3] = 10, c[3] = 0, d[3] = 0, e[3] = 0;                     /* kind past i1 */
    a[4] = 1, b[4] = 1, c[4] = COMMONS_COUNT_MAX + 1, d[4] = 0, e[4] = 0;  /* newest past i12 */
    a[5] = 1, b[5] = 1, c[5] = 0, d[5] = COMMONS_COUNT_MAX + 1, e[5] = 0;  /* seen past i12 */
    a[6] = 1, b[6] = 1, c[6] = 0, d[6] = 0, e[6] = COMMONS_MEMBERS_MAX + 1;
    a[7] = -5, b[7] = -1, c[7] = -9, d[7] = -9, e[7] = -1; /* sign aside, they fit */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(commons_room_record_fits(a[i], b[i], c[i], d[i], e[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_TRUE,
          "Commons: the room record's own edges (CR4)");
    zxapp_arg_t rr[5] = {{"room", a, WAVE_ROWS},
                         {"kind", b, WAVE_ROWS},
                         {"newest", c, WAVE_ROWS},
                         {"seen", d, WAVE_ROWS},
                         {"members", e, WAVE_ROWS}};
    CHECK(wave_same(app, "room_record_ok", rr, 5, want, WAVE_ROWS, true),
          "Commons: room_record_ok, MYRA = C");

    /* message_record_ok (CR5): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1100, 1100);
        b[i] = i % 2 ? wrand(0, 50) : wrand(-1100000000000, 1100000000000);
        c[i] = i % 3 ? wrand(1, 999999999) : wrand(-COMMONS_LAMPORT_MAX, COMMONS_LAMPORT_MAX);
        d[i] = i % 4 ? wrand(1600000000000, 2000000000000) : wrand(-11000000000000, 11000000000000);
        e[i] = wrand(-11000, 11000);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0; /* every least */
    a[1] = COMMONS_ROOM_FIELD, b[1] = COMMONS_COUNT_MAX, c[1] = COMMONS_LAMPORT_MAX;
    d[1] = COMMONS_STAMP_MAX, e[1] = COMMONS_BYTES_FIELD;                   /* every most */
    a[2] = 1, b[2] = COMMONS_COUNT_MAX + 1, c[2] = 1, d[2] = 0, e[2] = 1;   /* pos past i12 */
    a[3] = 1, b[3] = 1, c[3] = COMMONS_LAMPORT_MAX + 1, d[3] = 0, e[3] = 1; /* lamport past */
    a[4] = 1, b[4] = 1, c[4] = 1, d[4] = COMMONS_STAMP_MAX + 1, e[4] = 1;   /* stamp past */
    a[5] = 1, b[5] = 1, c[5] = 1, d[5] = 0, e[5] = COMMONS_BYTES_FIELD + 1; /* bytes past */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(commons_message_record_fits(a[i], b[i], c[i], d[i], e[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE,
          "Commons: the message record's own edges (CR5)");
    zxapp_arg_t mr[5] = {{"room", a, WAVE_ROWS},
                         {"pos", b, WAVE_ROWS},
                         {"lamport", c, WAVE_ROWS},
                         {"stamp", d, WAVE_ROWS},
                         {"bytes", e, WAVE_ROWS}};
    CHECK(wave_same(app, "message_record_ok", mr, 5, want, WAVE_ROWS, true),
          "Commons: message_record_ok, MYRA = C");

    /* witness.kind_count, used as it is: a space's new events coded 0 mine, 1 a post by
     * another, 2 a watch-hall change; how many are posts by others */
    for (uint32_t r = 0; r < 8; r++) {
        int64_t k[32], one[1] = {1}; /* pspace.events: 32 at most */
        uint32_t n = 2u + r * 4u;
        for (uint32_t i = 0; i < n; i++) k[i] = wrand(0, 2);
        int64_t w = witness_kind_count(k, n, 1);
        zxapp_arg_t ka[2] = {{"kind", k, n}, {"want", one, 1}};
        CHECK(wave_same(app, "witness.kind_count", ka, 2, &w, 1, false),
              "Commons: posts by others among %u events, Witness's count", n);
    }
}
