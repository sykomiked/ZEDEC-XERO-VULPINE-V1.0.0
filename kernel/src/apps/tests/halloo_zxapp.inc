/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Halloo's languages against the C its model runs (halloo_rules.h).
 * Slots: @num.halloo_quiet_left, @logic.halloo_ring_ok,
 * @logic.halloo_ring_wait_ok, @num.halloo_hms, @logic.halloo_call_ok. */
#include "halloo_rules.h"

APP_TEST(halloo, 9, 28)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], g[WAVE_ROWS], want[WAVE_ROWS];
    static bool truth[WAVE_ROWS];

    /* quiet_left (HR1): KRON = Rust = C, swept, with every edge */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1439);
        b[i] = wrand(0, 1439);
        c[i] = wrand(0, 1439);
    }
    a[0] = 1320, b[0] = 420, c[0] = 1320; /* quiet just began: 540 left */
    a[1] = 1320, b[1] = 420, c[1] = 1400; /* across midnight: 460 */
    a[2] = 1320, b[2] = 420, c[2] = 419;  /* the last minute: 1 */
    a[3] = 1320, b[3] = 420, c[3] = 420;  /* closed: 0 */
    a[4] = 420, b[4] = 1320, c[4] = 900;  /* a daytime window: 420 */
    a[5] = 600, b[5] = 600, c[5] = 600;   /* the whole day: 1440 */
    a[6] = 0, b[6] = 0, c[6] = 1439;      /* the whole day, last minute: 1 */
    a[7] = 0, b[7] = 1, c[7] = 1;         /* one minute long, past: 0 */
    a[8] = 1439, b[8] = 0, c[8] = 1439;   /* the max minute: 1 */
    a[9] = 0, b[9] = 1439, c[9] = 0;      /* all but the last minute: 1439 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        want[i] = halloo_quiet_left(a[i], b[i], c[i]);
        CHECK(want[i] >= 0, "Halloo: quiet_left row %u is inside the domain", i);
    }
    CHECK(want[0] == 540 && want[1] == 460 && want[2] == 1 && want[3] == 0 && want[4] == 420 &&
              want[5] == 1440 && want[6] == 1 && want[7] == 0 && want[8] == 1 && want[9] == 1439,
          "Halloo: quiet_left, the C's own edges");
    zxapp_arg_t qa[3] = {{"opens", a, WAVE_ROWS}, {"closes", b, WAVE_ROWS}, {"now", c, WAVE_ROWS}};
    CHECK(wave_same(app, "quiet_left", qa, 3, want, WAVE_ROWS, false),
          "Halloo: quiet_left, KRON = Rust = C");
    {
        /* a minute of -1 and of 1440 (max + 1): Rust traps, KRON answers: BOTH */
        static const int64_t o[2] = {1320, 1320}, k[2] = {420, 420}, n[2] = {0, -1},
                             o2[2] = {1320, 1440}, z[2] = {0, 0};
        zxapp_arg_t na[3] = {{"opens", o, 2}, {"closes", k, 2}, {"now", n, 2}};
        CHECK(halloo_quiet_left(1320, 420, -1) == HALLOO_REFUSED &&
                  wave_lp(app, "quiet_left", na, 3) == ZXAPP_BOTH,
              "Halloo: a minute of -1: Rust refuses, KRON answers: BOTH");
        zxapp_arg_t oa[3] = {{"opens", o2, 2}, {"closes", k, 2}, {"now", z, 2}};
        CHECK(halloo_quiet_left(1440, 420, 0) == HALLOO_REFUSED &&
                  wave_lp(app, "quiet_left", oa, 3) == ZXAPP_BOTH,
              "Halloo: a minute of 1440: Rust refuses, KRON answers: BOTH");
    }

    /* ring_ok (HR2): Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 3 ? 1 : 0;
        b[i] = i % 5 ? 0 : 1;
        c[i] = i % 4 ? 0 : wrand(1, 1440);
        truth[i] = halloo_ring_ok(a[i], b[i], c[i]);
    }
    a[1] = 1, b[1] = 0, c[1] = 0; /* known, not blocked, no quiet: rings */
    truth[1] = halloo_ring_ok(1, 0, 0);
    CHECK(truth[1] && !truth[0] && !halloo_ring_ok(1, 0, 1440) && !halloo_ring_ok(1, 1, 0) &&
              !halloo_ring_ok(2, 0, 0) && !halloo_ring_ok(1, 0, -1),
          "Halloo: ring_ok, the C's own edges");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(truth[i]);
    zxapp_arg_t ra[3] = {
        {"known", a, WAVE_ROWS}, {"blocked", b, WAVE_ROWS}, {"quiet", c, WAVE_ROWS}};
    CHECK(wave_same(app, "ring_ok", ra, 3, want, WAVE_ROWS, true),
          "Halloo: ring_ok, Eve = KRON = C");
    {
        /* a known of 2 and a quiet of -1: Eve refuses, KRON reads yes: BOTH */
        static const int64_t k[2] = {1, 2}, bl[2] = {0, 0}, q[2] = {0, 0}, k1[2] = {1, 1},
                             qn[2] = {0, -1};
        zxapp_arg_t ka[3] = {{"known", k, 2}, {"blocked", bl, 2}, {"quiet", q, 2}};
        CHECK(!halloo_ring_ok(2, 0, 0) && wave_lp(app, "ring_ok", ka, 3) == ZXAPP_BOTH,
              "Halloo: a known of 2: Eve refuses, KRON rings: BOTH");
        zxapp_arg_t qa2[3] = {{"known", k1, 2}, {"blocked", bl, 2}, {"quiet", qn, 2}};
        CHECK(!halloo_ring_ok(1, 0, -1) && wave_lp(app, "ring_ok", qa2, 3) == ZXAPP_BOTH,
              "Halloo: a quiet of -1: Eve refuses, KRON rings: BOTH");
    }

    /* ring_wait_ok (HR3): Eve = C, at and past both ends */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(0, 40000);
    a[0] = HALLOO_RING_MIN, a[1] = HALLOO_RING_MAX, a[2] = HALLOO_RING_MIN - 1;
    a[3] = HALLOO_RING_MAX + 1, a[4] = 0, a[5] = -1;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        truth[i] = halloo_ring_wait_ok(a[i]);
        want[i] = T_(truth[i]);
    }
    CHECK(truth[0] && truth[1] && !truth[2] && !truth[3] && !truth[4] && !truth[5],
          "Halloo: ring_wait_ok, the C's own edges");
    zxapp_arg_t wa[1] = {{"ringms", a, WAVE_ROWS}};
    CHECK(wave_same(app, "ring_wait_ok", wa, 1, want, WAVE_ROWS, true),
          "Halloo: ring_wait_ok, Eve = C");

    /* hms (HR4): KRON = Sutra = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(0, i < 32 ? 86400000 : HALLOO_MS_MAX);
    a[0] = 0, a[1] = 999, a[2] = 1000, a[3] = 3723000, a[4] = 86399999, a[5] = HALLOO_MS_MAX;
    a[6] = 3600000, a[7] = 59999, a[8] = 60000;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        want[i] = halloo_hms(a[i]);
        CHECK(want[i] >= 0, "Halloo: hms row %u is inside the domain", i);
    }
    CHECK(want[0] == 0 && want[1] == 0 && want[2] == 1 && want[3] == 10203 && want[4] == 235959 &&
              want[5] == 27774639 && want[6] == 10000 && want[7] == 59 && want[8] == 100,
          "Halloo: hms, the C's own edges");
    zxapp_arg_t ha[1] = {{"ms", a, WAVE_ROWS}};
    CHECK(wave_same(app, "hms", ha, 1, want, WAVE_ROWS, false), "Halloo: hms, KRON = Sutra = C");
    {
        /* max + 1 past 9(10), and a negative length: Sutra refuses, KRON answers */
        static const int64_t big[2] = {1000, HALLOO_MS_MAX + 1}, neg[2] = {1000, -1};
        zxapp_arg_t ba[1] = {{"ms", big, 2}};
        CHECK(halloo_hms(HALLOO_MS_MAX + 1) == HALLOO_REFUSED &&
                  wave_lp(app, "hms", ba, 1) == ZXAPP_BOTH,
              "Halloo: a length past 9(10): Sutra refuses, KRON answers: BOTH");
        zxapp_arg_t ne[1] = {{"ms", neg, 2}};
        CHECK(halloo_hms(-1) == HALLOO_REFUSED && wave_lp(app, "hms", ne, 1) == ZXAPP_BOTH,
              "Halloo: a negative length: Sutra refuses, KRON answers: BOTH");
    }

    /* call_ok (HR5): MYRA = Eve = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, HALLOO_SEQ_MAX);
        b[i] = wrand(1, HALLOO_PEER_MAX);
        c[i] = wrand(0, 1);
        d[i] = wrand(0, 1);
        e[i] = wrand(HALLOO_END_NONE, HALLOO_END_CANCELLED);
        f[i] = wrand(0, HALLOO_MS_MAX);
        g[i] = wrand(0, HALLOO_STAMP_MAX);
    }
    a[0] = 1, b[0] = 1, c[0] = 0, d[0] = 0, e[0] = 0, f[0] = 0, g[0] = 0; /* every least */
    a[1] = HALLOO_SEQ_MAX, b[1] = HALLOO_PEER_MAX, c[1] = 1, d[1] = 1;    /* every most */
    e[1] = HALLOO_END_CANCELLED, f[1] = HALLOO_MS_MAX, g[1] = HALLOO_STAMP_MAX;
    a[2] = HALLOO_SEQ_MAX + 1;   /* past 9 digits: both refuse */
    b[3] = 10000000000;          /* past 10 digits: both refuse */
    g[4] = HALLOO_STAMP_MAX + 1; /* past 13 digits: both refuse */
    f[5] = HALLOO_MS_MAX + 1;    /* past 10 digits: both refuse */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        truth[i] = halloo_call_ok(a[i], b[i], c[i], d[i], e[i], f[i], g[i]);
        want[i] = T_(truth[i]);
    }
    CHECK(truth[0] && truth[1] && !truth[2] && !truth[3] && !truth[4] && !truth[5] && truth[6],
          "Halloo: call_ok, the C's own edges");
    zxapp_arg_t ca[7] = {{"seq", a, WAVE_ROWS},   {"peer", b, WAVE_ROWS},   {"dir", c, WAVE_ROWS},
                         {"media", d, WAVE_ROWS}, {"reason", e, WAVE_ROWS}, {"ms", f, WAVE_ROWS},
                         {"stamp", g, WAVE_ROWS}};
    CHECK(wave_same(app, "call_ok", ca, 7, want, WAVE_ROWS, true),
          "Halloo: call_ok, MYRA = Eve = C");
    {
        /* a direction of 2, a reason of 11, a member id of 2^32: MYRA fits, Eve refuses: BOTH */
        static const int64_t s[2] = {1, 1}, p[2] = {7, 7}, p2[2] = {7, 4294967296}, dr[2] = {1, 2},
                             d1[2] = {1, 1}, m[2] = {0, 0}, r[2] = {1, 1}, r2[2] = {1, 11},
                             l[2] = {60000, 60000}, t[2] = {1700000000000, 1700000000000};
        zxapp_arg_t xa[7] = {{"seq", s, 2},    {"peer", p, 2}, {"dir", dr, 2}, {"media", m, 2},
                             {"reason", r, 2}, {"ms", l, 2},   {"stamp", t, 2}};
        CHECK(!halloo_call_ok(1, 7, 2, 0, 1, 60000, 1700000000000) &&
                  wave_lp(app, "call_ok", xa, 7) == ZXAPP_BOTH,
              "Halloo: a direction of 2: MYRA fits it, Eve refuses: BOTH");
        zxapp_arg_t ya[7] = {{"seq", s, 2},     {"peer", p, 2}, {"dir", d1, 2}, {"media", m, 2},
                             {"reason", r2, 2}, {"ms", l, 2},   {"stamp", t, 2}};
        CHECK(!halloo_call_ok(1, 7, 1, 0, 11, 60000, 1700000000000) &&
                  wave_lp(app, "call_ok", ya, 7) == ZXAPP_BOTH,
              "Halloo: a reason of 11: MYRA fits it, Eve refuses: BOTH");
        zxapp_arg_t za[7] = {{"seq", s, 2},    {"peer", p2, 2}, {"dir", d1, 2}, {"media", m, 2},
                             {"reason", r, 2}, {"ms", l, 2},    {"stamp", t, 2}};
        CHECK(!halloo_call_ok(1, 4294967296, 1, 0, 1, 60000, 1700000000000) &&
                  wave_lp(app, "call_ok", za, 7) == ZXAPP_BOTH,
              "Halloo: a member id of 2^32: MYRA fits it, Eve refuses: BOTH");
    }
}
