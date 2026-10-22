/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Gazette's languages against the C its model runs (gazette_rules.h).
 * Slots: @logic.gazette_feed_item_shown, @num.gazette_feed_unread_total,
 * @logic.gazette_feed_read_ok and, used as they are,
 * @num.commons_room_unread and @num.seedbank_cid_byte. */
#include "gazette_rules.h"
#include "seedbank_rules.h"

APP_TEST(gazette, 20, 36)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];

    /* feed_item_shown (GR1): Eve = KRON = C, every flag 0 or 1, plus rows below 0 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 7 == 6 ? wrand(-3, 0) : wrand(0, 1);
        b[i] = wrand(0, 1);
    }
    a[0] = 1, b[0] = 0;  /* the only yes */
    a[1] = 0, b[1] = 0;  /* not followed */
    a[2] = 1, b[2] = 1;  /* muted */
    a[3] = 0, b[3] = 1;  /* neither */
    a[4] = -1, b[4] = 0; /* below 0 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(gazette_feed_item_shown(a[i], b[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && !gazette_feed_item_shown(2, 0) &&
              !gazette_feed_item_shown(1, -1),
          "Gazette: the C show rule's own edges (GR1)");
    zxapp_arg_t sa[2] = {{"followed", a, WAVE_ROWS}, {"muted", b, WAVE_ROWS}};
    CHECK(wave_same(app, "feed_item_shown", sa, 2, want, WAVE_ROWS, true),
          "Gazette: feed_item_shown, Eve = KRON = C");
    {
        /* followed 2 (max + 1) or muted -1: Eve holds each flag to its range and says no,
         * KRON reads past it and says yes */
        static const int64_t f[2][2] = {{1, 2}, {1, 1}};
        static const int64_t m[2][2] = {{0, 0}, {0, -1}};
        for (uint32_t k = 0; k < 2; k++) {
            zxapp_arg_t xa[2] = {{"followed", f[k], 2}, {"muted", m[k], 2}};
            CHECK(wave_lp(app, "feed_item_shown", xa, 2) == ZXAPP_BOTH &&
                      !gazette_feed_item_shown(f[k][1], m[k][1]),
                  "Gazette: feed_item_shown case %u: Eve says no, KRON yes: BOTH", k);
        }
    }

    /* feed_unread_total (GR2): KRON = Trebago = C, one source per row, a sum */
    for (uint32_t r = 0; r < 8; r++) {
        uint32_t n = 2u + r * 8u;
        for (uint32_t i = 0; i < n; i++) {
            a[i] = i % 3 ? wrand(0, 1000) : wrand(0, COMMONS_COUNT_MAX);
            b[i] = i % 4 ? wrand(0, a[i] < 1000 ? a[i] + 5 : a[i]) : wrand(0, COMMONS_COUNT_MAX);
            c[i] = wrand(0, 1);
        }
        if (r == 0) a[0] = COMMONS_COUNT_MAX, b[0] = 0, c[0] = 1, a[1] = 0, b[1] = 0, c[1] = 0;
        if (r == 1) a[0] = 3, b[0] = 7, c[0] = 1, a[1] = COMMONS_COUNT_MAX, b[1] = 0, c[1] = 0;
        int64_t w = gazette_feed_unread_total(a, b, c, n);
        CHECK(w >= 0, "Gazette: the C badge answers rows %u", n);
        zxapp_arg_t ta[3] = {{"newest", a, n}, {"seen", b, n}, {"shown", c, n}};
        CHECK(wave_same(app, "feed_unread_total", ta, 3, &w, 1, false),
              "Gazette: feed_unread_total over %u sources, KRON = Trebago = C", n);
    }
    {
        static const int64_t nw[2] = {COMMONS_COUNT_MAX, 5}, sn[2] = {0, 2}, sh[2] = {1, 1};
        CHECK(gazette_feed_unread_total(nw, sn, sh, 2) == COMMONS_COUNT_MAX + 3 &&
                  gazette_feed_unread_total(NULL, NULL, NULL, 0) == 0,
              "Gazette: the C badge's own edges (GR2)");
        /* shown 2, newest -1, or newest past 12 digits: Trebago refuses, KRON answers */
        static const int64_t xn[3][2] = {{5, 5}, {5, -1}, {5, COMMONS_COUNT_MAX + 1}};
        static const int64_t xs[3][2] = {{0, 0}, {0, 0}, {0, 0}};
        static const int64_t xh[3][2] = {{1, 2}, {1, 1}, {1, 1}};
        for (uint32_t k = 0; k < 3; k++) {
            zxapp_arg_t xa[3] = {{"newest", xn[k], 2}, {"seen", xs[k], 2}, {"shown", xh[k], 2}};
            CHECK(wave_lp(app, "feed_unread_total", xa, 3) == ZXAPP_BOTH &&
                      gazette_feed_unread_total(xn[k], xs[k], xh[k], 2) == GAZETTE_REFUSED,
                  "Gazette: feed_unread_total case %u: Trebago refuses, KRON answers: BOTH", k);
        }
    }

    /* feed_read_ok (GR3): MYRA = C, each piece at its edges */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1100, 1100);
        b[i] = i % 2 ? wrand(0, 128) : wrand(-1100000000000, 1100000000000);
        c[i] = i % 3 ? wrand(0, 999999999) : wrand(-GAZETTE_LAMPORT_FIELD, GAZETTE_LAMPORT_FIELD);
        d[i] = i % 4 ? wrand(1600000000000, 2000000000000) : wrand(-11000000000000, 11000000000000);
        e[i] = wrand(-11, 11);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0; /* every least */
    a[1] = GAZETTE_SOURCE_FIELD, b[1] = GAZETTE_ITEM_FIELD, c[1] = GAZETTE_LAMPORT_FIELD;
    d[1] = GAZETTE_STAMP_FIELD, e[1] = 1;                                    /* every most */
    a[2] = GAZETTE_SOURCE_FIELD + 1, b[2] = 0, c[2] = 0, d[2] = 0, e[2] = 0; /* source past */
    a[3] = 0, b[3] = GAZETTE_ITEM_FIELD + 1, c[3] = 0, d[3] = 0, e[3] = 0;   /* item past */
    a[4] = 0, b[4] = 0, c[4] = GAZETTE_LAMPORT_FIELD + 1, d[4] = 0, e[4] = 0;
    a[5] = 0, b[5] = 0, c[5] = 0, d[5] = GAZETTE_STAMP_FIELD + 1, e[5] = 0;
    a[6] = 0, b[6] = 0, c[6] = 0, d[6] = 0, e[6] = GAZETTE_STATE_FIELD + 1; /* state past i1 */
    a[7] = -5, b[7] = -1, c[7] = -9, d[7] = -9, e[7] = -1; /* sign aside, they fit */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(gazette_read_record_fits(a[i], b[i], c[i], d[i], e[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_TRUE,
          "Gazette: the read-state record's own edges (GR3)");
    zxapp_arg_t rr[5] = {{"source", a, WAVE_ROWS},
                         {"item", b, WAVE_ROWS},
                         {"lamport", c, WAVE_ROWS},
                         {"stamp", d, WAVE_ROWS},
                         {"state", e, WAVE_ROWS}};
    CHECK(wave_same(app, "feed_read_ok", rr, 5, want, WAVE_ROWS, true),
          "Gazette: feed_read_ok, MYRA = C");

    /* commons.room_unread, used as it is: each source's unread */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, i % 2 ? 1000 : COMMONS_COUNT_MAX);
        b[i] = wrand(0, a[i] < 1000 ? a[i] + 3 : a[i]);
        want[i] = commons_room_unread(a[i], b[i]);
    }
    zxapp_arg_t ua[2] = {{"newest", a, WAVE_ROWS}, {"seen", b, WAVE_ROWS}};
    CHECK(wave_same(app, "commons.room_unread", ua, 2, want, WAVE_ROWS, false),
          "Gazette: a source's unread, Commons's rule");

    /* seedbank.cid_byte, used as it is: a space CID's every byte */
    {
        static const char cid[] = "bafkreigh2akiscaildcqabsyg3dfr6chu3fgpregiymsck7e7aqa4s52zy";
        for (uint32_t i = 0; i < SEEDBANK_CID_LEN; i++) {
            a[i] = i;
            b[i] = i == 7u ? '1' : (unsigned char) cid[i]; /* '1' is not base32 */
            c[i] = SEEDBANK_CID_LEN;
            want[i] = seedbank_cid_byte(a[i], b[i], c[i]);
        }
        CHECK(sizeof cid - 1u == SEEDBANK_CID_LEN && want[0] == 1 && want[7] == 0 &&
                  seedbank_cid_ok(cid, SEEDBANK_CID_LEN),
              "Gazette: the test CID is 59 bytes and fits");
        zxapp_arg_t ca[3] = {{"pos", a, SEEDBANK_CID_LEN},
                             {"code", b, SEEDBANK_CID_LEN},
                             {"len", c, SEEDBANK_CID_LEN}};
        CHECK(wave_same(app, "seedbank.cid_byte", ca, 3, want, SEEDBANK_CID_LEN, false),
              "Gazette: a space CID's bytes, Seedbank's rule");
    }
}
