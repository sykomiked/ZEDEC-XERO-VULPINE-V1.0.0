/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Margins' languages against the C its model runs (margins_rules.h).
 * Slots: @logic.margins_note_size_ok, @num.margins_pinned_recent_key,
 * @logic.margins_note_tag_row_ok, and, used as they are, Assay's
 * @num.assay_score_rank, Witness's @num.witness_kind_count, Lockbox's
 * @num.lockbox_days_left and @num.used_x100. */
#include "margins_rules.h"

APP_TEST(margins, 22, 38)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], want[WAVE_ROWS];
    const int64_t S = MARGINS_STAMP_MAX;

    /* the C rules' own edges (MR1-MR7) */
    CHECK(margins_size_ok(1, 0, 0) && margins_size_ok(120, 8000, 16) && !margins_size_ok(0, 0, 0) &&
              !margins_size_ok(121, 0, 0) && !margins_size_ok(5, -1, 0) &&
              !margins_size_ok(5, 8001, 0) && !margins_size_ok(5, 0, -1) &&
              !margins_size_ok(5, 0, 17),
          "Margins: size limits at their edges (MR1)");
    CHECK(margins_key(0, 0) == 0 && margins_key(1, 0) == S + 1 && margins_key(0, S) == S &&
              margins_key(1, S) == 2 * S + 1 && margins_key(2, 0) == MARGINS_REFUSED &&
              margins_key(-1, 0) == MARGINS_REFUSED && margins_key(0, -1) == MARGINS_REFUSED &&
              margins_key(0, S + 1) == MARGINS_REFUSED && margins_key(1, 0) > margins_key(0, S),
          "Margins: the order key, every pinned key above every unpinned one (MR2)");
    CHECK(margins_tag_record_ok(1, 1, 1760000000) &&
              margins_tag_record_ok(9999, 9999, 999999999999) &&
              margins_tag_record_ok(-9999, -1, -999999999999) &&
              !margins_tag_record_ok(10000, 1, 1) && !margins_tag_record_ok(1, -10000, 1) &&
              !margins_tag_record_ok(1, 1, 1000000000000),
          "Margins: the tag record's own edges (MR4)");
    CHECK(margins_trash_days(1760000000, 1760000000) == 30 &&
              margins_trash_days(1760000000, 1760000000 + 30 * 86400) == 0 &&
              margins_trash_days(1760000000, 1760000000 + 29 * 86400 + 1) == 0 &&
              margins_trash_days(1760000000, 1760000000 + 29 * 86400 - 1) == 1 &&
              margins_trash_days(-1, 0) == LOCKBOX_REFUSED &&
              margins_trash_days(LOCKBOX_STAMP_MAX - 30 * 86400, 0) >= 0 &&
              margins_trash_days(LOCKBOX_STAMP_MAX - 30 * 86400 + 1, 0) == LOCKBOX_REFUSED,
          "Margins: the Trash's days through Lockbox's rule (MR6)");
    CHECK(margins_fill_x100(0) == 0 && margins_fill_x100(1) == 1 &&
              margins_fill_x100(4000) == 5000 && margins_fill_x100(8000) == 10000 &&
              margins_fill_x100(8001) == MARGINS_REFUSED &&
              margins_fill_x100(-1) == MARGINS_REFUSED,
          "Margins: how full a note is (MR7)");

    /* note_size_ok (MR1): Eve = Sutra = C, inside Sutra's pictures */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 999);
        b[i] = i % 2 ? wrand(0, 9999) : wrand(0, 8000);
        c[i] = wrand(0, 99);
        if (i % 3 == 0) a[i] = wrand(1, 120), c[i] = wrand(0, 16);
    }
    a[0] = 1, b[0] = 0, c[0] = 0;
    a[1] = 120, b[1] = 8000, c[1] = 16;
    a[2] = 0, b[2] = 0, c[2] = 0;
    a[3] = 121, b[3] = 0, c[3] = 0;
    a[4] = 10, b[4] = 8001, c[4] = 0;
    a[5] = 10, b[5] = 0, c[5] = 17;
    a[6] = 999, b[6] = 9999, c[6] = 99;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(margins_size_ok(a[i], b[i], c[i]));
    {
        uint32_t yes = 0;
        for (uint32_t i = 0; i < WAVE_ROWS; i++) yes += want[i] == ZXAPP_TRUE;
        zxapp_arg_t sa[3] = {
            {"title", a, WAVE_ROWS}, {"content", b, WAVE_ROWS}, {"tags", c, WAVE_ROWS}};
        CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
                  want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
                  yes > 8u && yes < WAVE_ROWS - 8u &&
                  wave_same(app, "note_size_ok", sa, 3, want, WAVE_ROWS, true),
              "Margins: note_size_ok, Eve = Sutra = C (%u notes fit)", yes);
    }
    {
        /* a count past Sutra's picture, or below 0: Sutra refuses, Eve answers false: BOTH */
        static const int64_t t1[2] = {10, 10}, b1[2] = {0, -1}, z[2] = {0, 0}, b2[2] = {0, 10000},
                             t3[2] = {10, 1000}, g4[2] = {0, 100};
        zxapp_arg_t x1[3] = {{"title", t1, 2}, {"content", b1, 2}, {"tags", z, 2}};
        zxapp_arg_t x2[3] = {{"title", t1, 2}, {"content", b2, 2}, {"tags", z, 2}};
        zxapp_arg_t x3[3] = {{"title", t3, 2}, {"content", z, 2}, {"tags", z, 2}};
        zxapp_arg_t x4[3] = {{"title", t1, 2}, {"content", z, 2}, {"tags", g4, 2}};
        CHECK(wave_lp(app, "note_size_ok", x1, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "note_size_ok", x2, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "note_size_ok", x3, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "note_size_ok", x4, 3) == ZXAPP_BOTH,
              "Margins: a count past Sutra's picture: Sutra refuses, Eve says no: BOTH");
    }

    /* pinned_recent_key (MR2): KRON = Trebago = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1);
        b[i] = i % 4 ? wrand(1500000000, 2000000000) : wrand(0, S);
    }
    a[0] = 0, b[0] = 0;
    a[1] = 1, b[1] = 0;
    a[2] = 0, b[2] = S;
    a[3] = 1, b[3] = S;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = margins_key(a[i], b[i]);
    zxapp_arg_t ka[2] = {{"pinned", a, WAVE_ROWS}, {"stamp", b, WAVE_ROWS}};
    CHECK(want[3] == 2 * S + 1 &&
              wave_same(app, "pinned_recent_key", ka, 2, want, WAVE_ROWS, false),
          "Margins: pinned_recent_key, KRON = Trebago = C");

    /* the list's order (MR3): those keys ranked by assay.score_rank, KRON = C */
    {
        static int64_t key[WAVE_ROWS], place[WAVE_ROWS];
        for (uint32_t i = 0; i < WAVE_ROWS; i++) key[i] = want[i];
        key[5] = key[6]; /* two notes of one key share a place */
        for (uint32_t i = 0; i < WAVE_ROWS; i++) place[i] = assay_score_rank(key, WAVE_ROWS, i);
        bool pinned_first = true;
        for (uint32_t i = 0; i < WAVE_ROWS; i++)
            for (uint32_t j = 0; j < WAVE_ROWS; j++)
                if (key[i] >= MARGINS_PIN_STEP && key[j] < MARGINS_PIN_STEP && place[i] >= place[j])
                    pinned_first = false;
        zxapp_arg_t ra[1] = {{"score", key, WAVE_ROWS}};
        CHECK(pinned_first && place[5] == place[6] && place[3] == 1 &&
                  wave_same(app, "assay.score_rank", ra, 1, place, WAVE_ROWS, false),
              "Margins: pinned first, then newest, through assay.score_rank");
    }
    {
        /* a pin flag of 2, a stamp below 0 or past the year 9999: Trebago refuses, KRON answers */
        static const int64_t p1[2] = {0, 2}, s1[2] = {1, 1}, p2[2] = {0, 0}, s2[2] = {1, -1},
                             s3[2] = {1, MARGINS_STAMP_MAX + 1}, p4[2] = {0, -1};
        zxapp_arg_t x1[2] = {{"pinned", p1, 2}, {"stamp", s1, 2}};
        zxapp_arg_t x2[2] = {{"pinned", p2, 2}, {"stamp", s2, 2}};
        zxapp_arg_t x3[2] = {{"pinned", p2, 2}, {"stamp", s3, 2}};
        zxapp_arg_t x4[2] = {{"pinned", p4, 2}, {"stamp", s1, 2}};
        CHECK(wave_lp(app, "pinned_recent_key", x1, 2) == ZXAPP_BOTH &&
                  wave_lp(app, "pinned_recent_key", x2, 2) == ZXAPP_BOTH &&
                  wave_lp(app, "pinned_recent_key", x3, 2) == ZXAPP_BOTH &&
                  wave_lp(app, "pinned_recent_key", x4, 2) == ZXAPP_BOTH,
              "Margins: a pin or stamp outside its range: Trebago refuses, KRON answers: BOTH");
    }

    /* note_tag_row_ok (MR4): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-12000, 12000);
        b[i] = wrand(-12000, 12000);
        c[i] = i % 2 ? wrand(-1200000000000, 1200000000000) : wrand(0, S);
    }
    a[0] = 1, b[0] = 1, c[0] = 1760000000;
    a[1] = 9999, b[1] = 9999, c[1] = 999999999999;
    a[2] = 10000, b[2] = 1, c[2] = 1;
    a[3] = 1, b[3] = 10000, c[3] = 1;
    a[4] = 1, b[4] = 1, c[4] = 1000000000000;
    a[5] = -9999, b[5] = -9999, c[5] = -999999999999;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(margins_tag_record_ok(a[i], b[i], c[i]));
    zxapp_arg_t ta[3] = {{"tag", a, WAVE_ROWS}, {"note", b, WAVE_ROWS}, {"stamp", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_TRUE &&
              wave_same(app, "note_tag_row_ok", ta, 3, want, WAVE_ROWS, true),
          "Margins: a tag index row makes a ^ZXMARGINS record, MYRA = C");

    /* the notes of a tag and the pinned notes (MR5) through witness.kind_count */
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t col[24], one[1];
        uint32_t n = 1u + r % 24u;
        for (uint32_t i = 0; i < n; i++) col[i] = r % 2 ? wrand(0, 1) : wrand(1, 6);
        one[0] = r % 2 ? 1 : wrand(1, 6);
        int64_t count = witness_kind_count(col, n, one[0]);
        zxapp_arg_t wa[2] = {{"kind", col, n}, {"want", one, 1}};
        CHECK(wave_same(app, "witness.kind_count", wa, 2, &count, 1, false),
              "Margins: %u rows, those of %lld through witness.kind_count", n, (long long) one[0]);
    }

    /* the Trash's days (MR6) through lockbox.days_left, and fullness (MR7) through used_x100 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t put = wrand(1500000000, 2000000000);
        a[i] = put + (int64_t) MARGINS_TRASH_DAYS * LOCKBOX_DAY;
        b[i] = put + wrand(0, 40 * 86400);
        want[i] = margins_trash_days(put, b[i]);
    }
    zxapp_arg_t la[2] = {{"expires", a, WAVE_ROWS}, {"now", b, WAVE_ROWS}};
    CHECK(wave_same(app, "lockbox.days_left", la, 2, want, WAVE_ROWS, false),
          "Margins: the Trash's days through lockbox.days_left, C++ = KRON = C");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t bytes = wrand(0, MARGINS_BODY_MAX);
        if (i == 0) bytes = 0;
        if (i == 1) bytes = MARGINS_BODY_MAX;
        a[i] = MARGINS_BODY_MAX;
        b[i] = MARGINS_BODY_MAX - bytes;
        want[i] = margins_fill_x100(bytes);
    }
    zxapp_arg_t ua[2] = {{"total", a, WAVE_ROWS}, {"left", b, WAVE_ROWS}};
    CHECK(want[0] == 0 && want[1] == 10000 &&
              wave_same(app, "used_share.used_x100", ua, 2, want, WAVE_ROWS, false),
          "Margins: how full a note is through used_share.used_x100, Sutra = KRON = C");
}
