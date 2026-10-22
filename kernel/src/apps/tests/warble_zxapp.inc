/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Warble's languages against the C its model runs (warble_rules.h).
 * Slots: @num.warble_shuffle_at, @num.warble_playlist_ms,
 * @logic.warble_volume_cap_ok, @logic.warble_track_record_ok and, used as
 * they are, Halloo's @num.halloo_hms and Kinetoscope's
 * @logic.kinetoscope_hall_rate_ok. */
#include "warble_rules.h"

APP_TEST(warble, 24, 50)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], want[WAVE_ROWS];

    /* shuffle_at (WR1): Rust (wasm) = KRON = C, every edge, then swept */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, WARBLE_SEED_MAX);
        b[i] = wrand(1, i < 32 ? 200 : WARBLE_COUNT_MAX);
        c[i] = wrand(0, b[i] - 1);
    }
    a[0] = 0, b[0] = 1, c[0] = 0;                              /* the least */
    a[1] = WARBLE_SEED_MAX, b[1] = WARBLE_COUNT_MAX, c[1] = 0; /* max seed and count */
    a[2] = WARBLE_SEED_MAX, b[2] = WARBLE_COUNT_MAX,
    c[2] = WARBLE_COUNT_MAX - 1;                 /* the last place */
    a[3] = WARBLE_MIX_PRIME, b[3] = 2, c[3] = 1; /* a seed that folds to 0 */
    a[4] = 42, b[4] = 5, c[4] = 4;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = warble_shuffle_at(a[i], b[i], c[i]);
    CHECK(want[0] == 0 && want[4] == 0 && warble_shuffle_at(42, 5, 0) == 2 &&
              warble_shuffle_at(-1, 5, 0) == WARBLE_REFUSED &&
              warble_shuffle_at(WARBLE_SEED_MAX + 1, 5, 0) == WARBLE_REFUSED &&
              warble_shuffle_at(1, 0, 0) == WARBLE_REFUSED &&
              warble_shuffle_at(1, WARBLE_COUNT_MAX + 1, 0) == WARBLE_REFUSED &&
              warble_shuffle_at(1, 5, 5) == WARBLE_REFUSED &&
              warble_shuffle_at(1, 5, -1) == WARBLE_REFUSED,
          "Warble: the C shuffle's own edges (WR1)");
    {
        /* every track once, for every count up to 64 and two seeds each */
        bool perm = true;
        for (int64_t n = 1; n <= 64 && perm; n++)
            for (int64_t s = 0; s < 2 && perm; s++) {
                uint64_t seen = 0;
                for (int64_t i = 0; i < n; i++) {
                    int64_t x = warble_shuffle_at(s * 7777777, n, i);
                    if (x < 0 || x >= n || (seen >> x & 1u))
                        perm = false;
                    else
                        seen |= UINT64_C(1) << x;
                }
            }
        CHECK(perm, "Warble: the shuffle plays every track once (WR1)");
        /* the seed changes the order, not only where it starts */
        int64_t o1[8], o2[8];
        bool rotated = false;
        for (int64_t i = 0; i < 8; i++)
            o1[i] = warble_shuffle_at(1, 8, i), o2[i] = warble_shuffle_at(2, 8, i);
        for (int64_t r = 0; r < 8; r++) {
            bool same = true;
            for (int64_t i = 0; i < 8; i++) same = same && o1[i] == o2[(i + r) % 8];
            rotated = rotated || same;
        }
        CHECK(!rotated, "Warble: two seeds, two orders, neither a turn of the other");
    }
    zxapp_arg_t sa[3] = {{"seed", a, WAVE_ROWS}, {"count", b, WAVE_ROWS}, {"step", c, WAVE_ROWS}};
    CHECK(wave_same(app, "shuffle_at", sa, 3, want, WAVE_ROWS, false),
          "Warble: shuffle_at, Rust = KRON = C");
    {
        /* past its ranges: Rust traps (refused), KRON answers: held */
        static const int64_t s2[2] = {42, WARBLE_SEED_MAX + 1}, n2[2] = {5, 5}, p2[2] = {0, 0};
        zxapp_arg_t xa[3] = {{"seed", s2, 2}, {"count", n2, 2}, {"step", p2, 2}};
        CHECK(wave_lp(app, "shuffle_at", xa, 3) == ZXAPP_BOTH,
              "Warble: a seed past 9(9): Rust refuses, KRON answers: BOTH");
        static const int64_t s3[2] = {42, 42}, p3[2] = {0, 5};
        zxapp_arg_t ya[3] = {{"seed", s3, 2}, {"count", n2, 2}, {"step", p3, 2}};
        CHECK(wave_lp(app, "shuffle_at", ya, 3) == ZXAPP_BOTH,
              "Warble: a place past the count: Rust refuses, KRON answers: BOTH");
        static const int64_t s4[2] = {42, -1};
        zxapp_arg_t za[3] = {{"seed", s4, 2}, {"count", n2, 2}, {"step", p2, 2}};
        CHECK(wave_lp(app, "shuffle_at", za, 3) == ZXAPP_BOTH,
              "Warble: a negative seed: Rust refuses, KRON answers: BOTH");
    }

    /* playlist_ms (WR2): 64 playlists, each an exact sum, Trebago = KRON = C */
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t ms[WARBLE_PLAYLIST_MAX];
        uint32_t n = r < 2 ? WARBLE_PLAYLIST_MAX : (uint32_t) wrand(1, 40);
        for (uint32_t i = 0; i < n; i++)
            ms[i] = r == 0   ? 0
                    : r == 1 ? WARBLE_TRACK_MS_MAX
                             : wrand(0, i % 3 ? 600000 : WARBLE_TRACK_MS_MAX);
        int64_t total = warble_playlist_ms(ms, n);
        zxapp_arg_t pa = {"ms", ms, n};
        CHECK(total >= 0 && wave_same(app, "playlist_ms", &pa, 1, &total, 1, false),
              "Warble: playlist %u, Trebago = KRON = C", r);
    }
    {
        static const int64_t one[2] = {1000, 0}, over[2] = {1000, WARBLE_TRACK_MS_MAX + 1},
                             neg[2] = {1000, -1};
        CHECK(warble_playlist_ms(over, 2) == WARBLE_REFUSED &&
                  warble_playlist_ms(neg, 2) == WARBLE_REFUSED &&
                  warble_playlist_ms(one, 2) == 1000 && warble_playlist_ms(NULL, 0) == 0 &&
                  warble_playlist_ms(NULL, 1) == WARBLE_REFUSED,
              "Warble: the C playlist length's own edges (WR2)");
        zxapp_arg_t oa = {"ms", over, 2}, na = {"ms", neg, 2};
        CHECK(wave_lp(app, "playlist_ms", &oa, 1) == ZXAPP_BOTH,
              "Warble: a track past a day: Trebago refuses, KRON sums: BOTH");
        CHECK(wave_lp(app, "playlist_ms", &na, 1) == ZXAPP_BOTH,
              "Warble: a negative length: Trebago refuses, KRON sums: BOTH");
    }

    /* volume_cap_ok (WR3): Eve = KRON = C wherever both can say it */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        b[i] = wrand(-2, WARBLE_VOLUME_MAX + 2);
        a[i] =
            i % 4 ? wrand(-2, b[i] >= 0 && b[i] <= WARBLE_VOLUME_MAX ? b[i] : WARBLE_VOLUME_MAX + 2)
                  : wrand(-2, WARBLE_VOLUME_MAX + 2);
        if (a[i] > b[i] && a[i] >= 0 && a[i] <= WARBLE_VOLUME_MAX && b[i] >= 0 &&
            b[i] <= WARBLE_VOLUME_MAX)
            a[i] = b[i]; /* past the cap is held, below */
    }
    a[0] = 0, b[0] = 0;                                     /* the least */
    a[1] = WARBLE_VOLUME_MAX, b[1] = WARBLE_VOLUME_MAX;     /* max */
    a[2] = WARBLE_VOLUME_MAX + 1, b[2] = WARBLE_VOLUME_MAX; /* max + 1 */
    a[3] = 50, b[3] = WARBLE_VOLUME_MAX + 1;
    a[4] = -1, b[4] = 50; /* negative */
    a[5] = 0, b[5] = -1;
    a[6] = 60, b[6] = 60;                        /* at the cap */
    a[7] = -1000000000000, b[7] = 1000000000000; /* far out */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(warble_volume_cap_ok(a[i], b[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_TRUE && want[7] == ZXAPP_FALSE && !warble_volume_cap_ok(61, 60),
          "Warble: the C volume limits' own edges (WR3)");
    zxapp_arg_t va[2] = {{"volume", a, WAVE_ROWS}, {"cap", b, WAVE_ROWS}};
    CHECK(wave_same(app, "volume_cap_ok", va, 2, want, WAVE_ROWS, true),
          "Warble: volume_cap_ok, Eve = KRON = C");
    {
        /* a volume past the cap: Eve holds each range alone (TRUE), KRON relates them */
        static const int64_t v2[2] = {50, 80}, c2[2] = {60, 60};
        zxapp_arg_t xa[2] = {{"volume", v2, 2}, {"cap", c2, 2}};
        CHECK(wave_lp(app, "volume_cap_ok", xa, 2) == ZXAPP_BOTH,
              "Warble: a volume past the owner's cap: Eve TRUE, KRON FALSE: BOTH");
    }

    /* track_record_ok (WR4): MYRA = Eve = C wherever both can say it */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, WARBLE_TRACKS - 1);
        b[i] = wrand(0, WARBLE_TRACK_MS_MAX);
        c[i] = wrand(0, WARBLE_MTIME_MAX);
    }
    a[0] = 0, b[0] = 0, c[0] = 0;                                                  /* every least */
    a[1] = WARBLE_TRACKS - 1, b[1] = WARBLE_TRACK_MS_MAX, c[1] = WARBLE_MTIME_MAX; /* every max */
    a[2] = 10000, b[2] = 1000, c[2] = 1;                /* past MYRA's i4 too */
    a[3] = 1, b[3] = 100000000, c[3] = 1;               /* past i8 */
    a[4] = 1, b[4] = 1000, c[4] = WARBLE_MTIME_MAX + 1; /* past i11 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(warble_track_record_ok(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE &&
              !warble_track_record_ok(WARBLE_TRACKS, 0, 0) && !warble_track_record_ok(0, -1, 0) &&
              !warble_track_record_ok(0, WARBLE_TRACK_MS_MAX + 1, 0),
          "Warble: the C track record's own edges (WR4)");
    zxapp_arg_t ta[3] = {{"track", a, WAVE_ROWS}, {"ms", b, WAVE_ROWS}, {"mtime", c, WAVE_ROWS}};
    CHECK(wave_same(app, "track_record_ok", ta, 3, want, WAVE_ROWS, true),
          "Warble: track_record_ok, MYRA = Eve = C");
    {
        /* inside MYRA's digits, outside Eve's ranges: held */
        static const int64_t t2[2] = {1, 1}, m2[2] = {1000, 90000000},
                             s2[2] = {1760000000, 1760000000};
        zxapp_arg_t xa[3] = {{"track", t2, 2}, {"ms", m2, 2}, {"mtime", s2, 2}};
        CHECK(wave_lp(app, "track_record_ok", xa, 3) == ZXAPP_BOTH,
              "Warble: a track longer than a day: MYRA TRUE, Eve FALSE: BOTH");
        static const int64_t t3[2] = {1, (int64_t) WARBLE_TRACKS}, m3[2] = {1000, 1000};
        zxapp_arg_t ya[3] = {{"track", t3, 2}, {"ms", m3, 2}, {"mtime", s2, 2}};
        CHECK(wave_lp(app, "track_record_ok", ya, 3) == ZXAPP_BOTH,
              "Warble: a place past the library: MYRA TRUE, Eve FALSE: BOTH");
    }

    /* halloo.hms (WR5): the playlist's length shown, KRON = Sutra = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, (int64_t) WARBLE_PLAYLIST_MAX * WARBLE_TRACK_MS_MAX);
        want[i] = warble_length_hms(a[i]);
    }
    a[0] = 0, want[0] = 0;
    a[1] = (int64_t) WARBLE_PLAYLIST_MAX * WARBLE_TRACK_MS_MAX, want[1] = 24000000; /* 100 days */
    a[2] = 420503, want[2] = 700;
    CHECK(warble_length_hms(a[1]) == want[1] && warble_length_hms(a[2]) == want[2],
          "Warble: the longest playlist is still inside halloo.hms");
    zxapp_arg_t ha = {"ms", a, WAVE_ROWS};
    CHECK(wave_same(app, "halloo.hms", &ha, 1, want, WAVE_ROWS, false),
          "Warble: the playlist's length through halloo.hms, KRON = Sutra = C");

    /* kinetoscope.hall_rate_ok (WR6): the hall's rates, swept across and
     * past each end, Eve = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-10, KINETOSCOPE_RATE_MAX + 10);
        want[i] = T_(warble_hall_rate_ok(a[i]));
    }
    a[0] = 0, a[1] = 1, a[2] = KINETOSCOPE_RATE_UNIT, a[3] = KINETOSCOPE_RATE_MAX,
    a[4] = KINETOSCOPE_RATE_MAX + 1;
    for (uint32_t i = 0; i < 5; i++) want[i] = T_(warble_hall_rate_ok(a[i]));
    CHECK(want[0] == ZXAPP_FALSE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_TRUE &&
              want[3] == ZXAPP_TRUE && want[4] == ZXAPP_FALSE,
          "Warble: a hall's rate, 1..262144, its own edges (WR6)");
    zxapp_arg_t ra = {"rate", a, WAVE_ROWS};
    CHECK(wave_same(app, "kinetoscope.hall_rate_ok", &ra, 1, want, WAVE_ROWS, true),
          "Warble: a hall's rate through kinetoscope.hall_rate_ok, Eve = C");
}
