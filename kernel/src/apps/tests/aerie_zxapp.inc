/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Aerie's languages against the C its model runs (aerie_rules.h).
 * Slots: @num.aerie_frame_tag, @logic.aerie_band_fits,
 * @logic.aerie_line_threshold_ok, @logic.aerie_radio_msg_ok,
 * @logic.aerie_ham_clear_ok, and the parts it
 * uses, @num.halloo_quiet_left and @logic.halloo_ring_ok. */
#include "aerie_rules.h"
#include "halloo_rules.h"

/* one frame's parts: Forth reads one frame from its stack per call */
static bool ae_tag_row(zxapp_t *app, int64_t sync, int64_t shell, int64_t trunk, int64_t want)
{
    int64_t sa[1] = {sync}, ea[1] = {shell}, ta[1] = {trunk}, w[1] = {want};
    zxapp_arg_t a[3] = {{"sync", sa, 1}, {"shell", ea, 1}, {"trunk", ta, 1}};
    if (wave_same(app, "frame_tag", a, 3, w, 1, false)) return true;
    printf("    frame_tag sync %lld shell %lld trunk %lld\n", (long long) sync, (long long) shell,
           (long long) trunk);
    return false;
}

static int32_t ae_tag_lp(zxapp_t *app, int64_t sync, int64_t shell, int64_t trunk)
{
    int64_t sa[1] = {sync}, ea[1] = {shell}, ta[1] = {trunk};
    zxapp_arg_t a[3] = {{"sync", sa, 1}, {"shell", ea, 1}, {"trunk", ta, 1}};
    return wave_lp(app, "frame_tag", a, 3);
}

APP_TEST(aerie, 17, 35)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], g[WAVE_ROWS], want[WAVE_ROWS];
    static bool truth[WAVE_ROWS];

    /* the C rules' own edges (AR1-AR5) */
    int64_t ps = -1, pe = -1, pt = -1;
    CHECK(aerie_frame_tag(0, 0, 0) == 0 && aerie_frame_tag(1, 9, 9) == 199 &&
              aerie_frame_tag(1, 2, 3) == 123 && aerie_frame_tag(2, 0, 0) == AERIE_REFUSED &&
              aerie_frame_tag(0, 10, 0) == AERIE_REFUSED &&
              aerie_frame_tag(0, 0, -1) == AERIE_REFUSED && aerie_tag_split(123, &ps, &pe, &pt) &&
              ps == 1 && pe == 2 && pt == 3 && !aerie_tag_split(200, &ps, &pe, &pt) &&
              !aerie_tag_split(-1, &ps, &pe, &pt) && aerie_band_fits(512, 560) &&
              !aerie_band_fits(513, 560) && aerie_band_fits(2048, AERIE_FRAME_MAX) &&
              !aerie_band_fits(2049, AERIE_FRAME_MAX) && !aerie_band_fits(-1, 560) &&
              !aerie_band_fits(0, AERIE_OVERHEAD) && aerie_threshold_ok(AERIE_THR_DEFAULT) &&
              !aerie_threshold_ok(0) && !aerie_threshold_ok(AERIE_THR_MAX + 1) &&
              aerie_msg_ok(999999999, 1, 4, 1, 4294967295LL, 3360, 9999999999999LL) &&
              !aerie_msg_ok(1, 0, 0, 0, 0, 10000, 0) && aerie_ham_clear_ok(0, 0) &&
              aerie_ham_clear_ok(0, 1) && aerie_ham_clear_ok(1, 0) && !aerie_ham_clear_ok(1, 1) &&
              !aerie_ham_clear_ok(2, 0) && !aerie_ham_clear_ok(0, -1),
          "Aerie: the C rules' own edges (AR1-AR5)");

    /* frame_tag (AR1): Forth (Cairndrake) = KRON = C, one frame per call, 64 calls */
    {
        static const int64_t EDGE[][3] = {{0, 0, 0}, {1, 9, 9}, {0, 9, 9}, {1, 0, 0},
                                          {0, 0, 9}, {1, 2, 3}, {0, 5, 0}, {1, 9, 0}};
        bool all = true;
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            int64_t s = i < 8 ? EDGE[i][0] : wrand(0, 1), sh = i < 8 ? EDGE[i][1] : wrand(0, 9),
                    t = i < 8 ? EDGE[i][2] : wrand(0, 9);
            all = ae_tag_row(app, s, sh, t, aerie_frame_tag(s, sh, t)) && all;
        }
        CHECK(all, "Aerie: frame_tag, Forth = KRON = C (64 calls)");
        /* past each part's range, and below it: Forth refuses, KRON answers */
        CHECK(ae_tag_lp(app, 2, 0, 0) == ZXAPP_BOTH,
              "Aerie: a sync of 2 (max + 1): Forth refuses, KRON answers: BOTH");
        CHECK(ae_tag_lp(app, 0, 10, 0) == ZXAPP_BOTH && ae_tag_lp(app, 0, 0, 10) == ZXAPP_BOTH,
              "Aerie: a shell or trunk of 10 (max + 1): BOTH");
        CHECK(ae_tag_lp(app, -1, 0, 0) == ZXAPP_BOTH && ae_tag_lp(app, 0, -1, 0) == ZXAPP_BOTH &&
                  ae_tag_lp(app, 0, 0, -1) == ZXAPP_BOTH,
              "Aerie: a negative part: Forth refuses, KRON answers: BOTH");
    }

    /* band_fits (AR2): Eve = KRON = C, every message no longer than its band's frame allows */
    static const int64_t FRAMES[] = {560, 1072, 1400, 2096};
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        b[i] = i % 3 ? FRAMES[wrand(0, 3)] : wrand(-10, 2200);
        int64_t room = b[i] - AERIE_OVERHEAD;
        a[i] = i % 4 ? wrand(-2, room < 0 ? 0 : room) : wrand(-5, 2100);
        if (a[i] > 0 && b[i] - a[i] < AERIE_OVERHEAD && b[i] >= AERIE_FRAME_MIN &&
            b[i] <= AERIE_FRAME_MAX && a[i] <= AERIE_PAYLOAD_MAX)
            a[i] = room; /* in Eve's ranges but too long: cut to fit (the BOTH check is below) */
    }
    a[0] = 0, b[0] = 560;                                        /* nothing, the least band */
    a[1] = 512, b[1] = 560;                                      /* exactly the control band */
    a[2] = 2048, b[2] = AERIE_FRAME_MAX;                         /* every max */
    a[3] = 2049, b[3] = AERIE_FRAME_MAX;                         /* bytes max + 1 */
    a[4] = 0, b[4] = AERIE_FRAME_MAX + 1;                        /* frame max + 1 */
    a[5] = 0, b[5] = AERIE_FRAME_MIN;                            /* the least frame */
    a[6] = 0, b[6] = AERIE_OVERHEAD;                             /* frame min - 1 */
    a[7] = -1, b[7] = 560;                                       /* negative */
    a[8] = 1, b[8] = AERIE_FRAME_MIN;                            /* one byte in the least */
    a[9] = INT64_MAX, b[9] = INT64_MAX, a[10] = 5, b[10] = -560; /* int64 max, a negative frame */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        truth[i] = aerie_band_fits(a[i], b[i]);
        want[i] = T_(truth[i]);
    }
    CHECK(truth[0] && truth[1] && truth[2] && !truth[3] && !truth[4] && truth[5] && !truth[6] &&
              !truth[7] && truth[8] && !truth[9],
          "Aerie: band_fits, the C's own edges");
    zxapp_arg_t fa[2] = {{"bytes", a, WAVE_ROWS}, {"frame", b, WAVE_ROWS}};
    CHECK(wave_same(app, "band_fits", fa, 2, want, WAVE_ROWS, true),
          "Aerie: band_fits, Eve = KRON = C");
    {
        /* 600 bytes on the control band: each range holds (Eve TRUE), KRON FALSE */
        static const int64_t by[2] = {600, 600}, fr[2] = {560, 2096}, by2[2] = {10, 513},
                             fr2[2] = {560, 560};
        zxapp_arg_t xa[2] = {{"bytes", by, 2}, {"frame", fr, 2}};
        CHECK(!aerie_band_fits(600, 560) && wave_lp(app, "band_fits", xa, 2) == ZXAPP_BOTH,
              "Aerie: a message too long for its band: Eve TRUE, KRON FALSE: BOTH");
        zxapp_arg_t ya[2] = {{"bytes", by2, 2}, {"frame", fr2, 2}};
        CHECK(wave_lp(app, "band_fits", ya, 2) == ZXAPP_BOTH,
              "Aerie: one byte past the control band: BOTH");
    }

    /* line_threshold_ok (AR3): Eve = C, at and past both ends */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(-100, 70000);
    a[0] = AERIE_THR_MIN, a[1] = AERIE_THR_MAX, a[2] = AERIE_THR_DEFAULT;
    a[3] = 0, a[4] = AERIE_THR_MAX + 1, a[5] = -1, a[6] = 2147483647, a[7] = INT64_MAX;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        truth[i] = aerie_threshold_ok(a[i]);
        want[i] = T_(truth[i]);
    }
    CHECK(truth[0] && truth[1] && truth[2] && !truth[3] && !truth[4] && !truth[5] && !truth[6] &&
              !truth[7],
          "Aerie: line_threshold_ok, the C's own edges");
    zxapp_arg_t ta[1] = {{"thr", a, WAVE_ROWS}};
    CHECK(wave_same(app, "line_threshold_ok", ta, 1, want, WAVE_ROWS, true),
          "Aerie: line_threshold_ok, Eve = C");

    /* radio_msg_ok (AR4): MYRA = C, every field at its digits and one past */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, AERIE_SEQ_MAX);
        b[i] = wrand(0, 4);
        c[i] = wrand(0, 1);
        d[i] = wrand(0, 4294967295LL);
        e[i] = wrand(0, AERIE_FREIGHT_MAX);
        f[i] = wrand(0, AERIE_STAMP_MAX);
        g[i] = wrand(AERIE_PRIVATE, AERIE_AMATEUR);
    }
    /* every max */
    g[1] = 9;
    a[1] = AERIE_SEQ_MAX;
    b[1] = 9;
    c[1] = 9;
    d[1] = 9999999999LL;
    e[1] = 9999;
    f[1] = AERIE_STAMP_MAX;
    a[2] = AERIE_SEQ_MAX + 1;   /* seq max + 1 */
    b[3] = 10;                  /* band max + 1 */
    c[4] = 10;                  /* dir max + 1 */
    d[5] = 10000000000LL;       /* sender max + 1 */
    e[6] = 10000;               /* bytes max + 1 */
    f[7] = AERIE_STAMP_MAX + 1; /* stamp max + 1 */
    /* every zero */
    a[8] = 0;
    b[8] = 0;
    c[8] = 0;
    d[8] = 0;
    e[8] = 0;
    f[8] = 0;
    g[8] = 0;
    e[9] = -9999; /* negative, inside its digits */
    g[10] = 10;   /* bearer max + 1 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        truth[i] = aerie_msg_ok(a[i], g[i], b[i], c[i], d[i], e[i], f[i]);
        want[i] = T_(truth[i]);
    }
    CHECK(truth[0] && truth[1] && !truth[2] && !truth[3] && !truth[4] && !truth[5] && !truth[6] &&
              !truth[7] && truth[8] && truth[9] && !truth[10],
          "Aerie: radio_msg_ok, the C's own edges");
    zxapp_arg_t ma[7] = {{"seq", a, WAVE_ROWS},  {"bearer", g, WAVE_ROWS}, {"band", b, WAVE_ROWS},
                         {"dir", c, WAVE_ROWS},  {"sender", d, WAVE_ROWS}, {"bytes", e, WAVE_ROWS},
                         {"stamp", f, WAVE_ROWS}};
    CHECK(wave_same(app, "radio_msg_ok", ma, 7, want, WAVE_ROWS, true),
          "Aerie: radio_msg_ok, MYRA = C");

    /* ham_clear_ok (AR5): Sutra = KRON = C, every pair of flags, and flags
     * that are no flags but fit Sutra's 9(1) */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i < 4 ? (int64_t) (i >> 1) : i % 5 == 0 ? wrand(2, 9) : wrand(0, 1);
        b[i] = i < 4 ? (int64_t) (i & 1) : i % 7 == 0 ? wrand(2, 9) : wrand(0, 1);
        truth[i] = aerie_ham_clear_ok(a[i], b[i]);
        want[i] = T_(truth[i]);
    }
    a[4] = 9, b[4] = 0; /* the most Sutra holds: no flag, refused by both */
    truth[4] = aerie_ham_clear_ok(a[4], b[4]);
    want[4] = T_(truth[4]);
    CHECK(truth[0] && truth[1] && truth[2] && !truth[3] && !truth[4],
          "Aerie: ham_clear_ok, the C's own edges: amateur and sealed is refused");
    zxapp_arg_t ha[2] = {{"amateur", a, WAVE_ROWS}, {"sealed", b, WAVE_ROWS}};
    CHECK(wave_same(app, "ham_clear_ok", ha, 2, want, WAVE_ROWS, true),
          "Aerie: ham_clear_ok, Sutra = KRON = C");
    {
        /* a flag below 0 or past 9: Sutra's 9(1) refuses it, KRON answers no */
        static const int64_t am[2] = {1, -1}, se[2] = {0, 0}, am2[2] = {0, 0}, se2[2] = {0, 10};
        zxapp_arg_t xa[2] = {{"amateur", am, 2}, {"sealed", se, 2}};
        CHECK(!aerie_ham_clear_ok(-1, 0) && wave_lp(app, "ham_clear_ok", xa, 2) == ZXAPP_BOTH,
              "Aerie: a negative amateur flag: Sutra refuses, KRON answers: BOTH");
        zxapp_arg_t ya[2] = {{"amateur", am2, 2}, {"sealed", se2, 2}};
        CHECK(!aerie_ham_clear_ok(0, 10) && wave_lp(app, "ham_clear_ok", ya, 2) == ZXAPP_BOTH,
              "Aerie: a sealed flag of 10: Sutra refuses, KRON answers: BOTH");
    }

    /* the parts it uses: Halloo's quiet minutes and ring rule, as Aerie's model calls them */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = 1320, b[i] = 420, c[i] = wrand(0, 1439);
        want[i] = halloo_quiet_left(a[i], b[i], c[i]);
    }
    zxapp_arg_t qa[3] = {{"opens", a, WAVE_ROWS}, {"closes", b, WAVE_ROWS}, {"now", c, WAVE_ROWS}};
    CHECK(wave_same(app, "halloo.quiet_left", qa, 3, want, WAVE_ROWS, false),
          "Aerie: halloo.quiet_left through use, KRON = Rust = C");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1), b[i] = wrand(0, 1), c[i] = i % 2 ? 0 : wrand(0, 1440);
        want[i] = T_(halloo_ring_ok(a[i], b[i], c[i]));
    }
    zxapp_arg_t ra[3] = {
        {"known", a, WAVE_ROWS}, {"blocked", b, WAVE_ROWS}, {"quiet", c, WAVE_ROWS}};
    CHECK(wave_same(app, "halloo.ring_ok", ra, 3, want, WAVE_ROWS, true),
          "Aerie: halloo.ring_ok through use, Eve = KRON = C");
}
