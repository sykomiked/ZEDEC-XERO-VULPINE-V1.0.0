/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Footlight's languages against the C its model runs (footlight_rules.h).
 * Slots: @num.footlight_letterbox_bar, @num.footlight_rehearsal_over,
 * @num.footlight_slide_start, @num.footlight_next_slide,
 * @logic.footlight_slide_record_ok, and, used as they are, @num.halloo_hms
 * and Kinetoscope's @num.kinetoscope_video_fit_w and
 * @num.kinetoscope_video_fit_h (a 16:9 slide's fit). */
#include "footlight_rules.h"

/* One Lingua call per row (zxapp_lingua.c L1): BASIC against the C. */
static bool fl_next_same(zxapp_t *app, int64_t cur, int64_t count, int64_t mode)
{
    int64_t cv[1] = {cur}, nv[1] = {count}, mv[1] = {mode},
            w[1] = {footlight_next(cur, count, mode)};
    zxapp_arg_t a[3] = {{"cur", cv, 1}, {"count", nv, 1}, {"mode", mv, 1}};
    if (w[0] >= 0 && wave_same(app, "next_slide", a, 3, w, 1, false)) return true;
    printf("    next_slide %lld %lld %lld\n", (long long) cur, (long long) count, (long long) mode);
    return false;
}

/* a call one language alone refuses gives no answer */
static bool fl_refused(zxapp_t *app, const char *entry, const zxapp_arg_t *args, uint32_t nargs)
{
    zxapp_num_t out[WAVE_ROWS];
    uint32_t n = 0;
    int32_t lp = -1;
    int rc = zxapp_call_witnessed(app, &H, entry, args, nargs, out, WAVE_ROWS, &n, &lp);
    return rc != ZXAPP_OK;
}

/* one rehearsal of n slides: Trebago's answers against the C */
static bool fl_rehearsal_same(zxapp_t *app, const int64_t *secs, uint32_t n, int64_t talk)
{
    int64_t over[1], tk[1] = {talk}, start[FOOTLIGHT_DECK_MAX];
    if (!footlight_rehearsal_over(secs, n, talk, &over[0]) ||
        !footlight_slide_start(secs, n, start))
        return false;
    zxapp_arg_t ra[2] = {{"secs", secs, n}, {"talk", tk, 1}};
    zxapp_arg_t sa[1] = {{"secs", secs, n}};
    bool ok = wave_same(app, "rehearsal_over", ra, 2, over, 1, false);
    if (n <= WAVE_ROWS) ok = wave_same(app, "slide_start", sa, 1, start, n, false) && ok;
    if (!ok) printf("    rehearsal of %u slides, talk %lld\n", n, (long long) talk);
    return ok;
}

APP_TEST(footlight, 22, 41)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        g[WAVE_ROWS], want[WAVE_ROWS];
    const int64_t SM = FOOTLIGHT_SIDE_MAX;

    /* the C's own fit (FL1-FL3): every window up to 400 by 400 shows a slide
     * no larger than the window, within a pixel of 16:9 (16h <= 9w + 9 and
     * 9w <= 16h + 16), centred to the pixel */
    bool rule = true;
    for (int64_t w = 0; w <= 400 && rule; w++)
        for (int64_t h = 0; h <= 400 && rule; h++) {
            int64_t fw = footlight_fit_w(w, h), fh = footlight_fit_h(w, h);
            int64_t l = footlight_bar(w, fw), t = footlight_bar(h, fh);
            rule = fw >= 0 && fh >= 0 && fw <= w && fh <= h && (fw == w || fh == h) &&
                   16 * fh <= 9 * fw + 9 && 9 * fw <= 16 * fh + 16 && l >= 0 && t >= 0 &&
                   (w - fw) - 2 * l <= 1 && (h - fh) - 2 * t <= 1;
            if (!rule)
                printf("    window %lld x %lld: %lld x %lld\n", (long long) w, (long long) h,
                       (long long) fw, (long long) fh);
        }
    CHECK(rule,
          "Footlight: a fitted slide is whole, 16:9 to the pixel and centred, 401 x 401 windows");
    CHECK(footlight_fit_w(1920, 1080) == 1920 && footlight_fit_h(1920, 1080) == 1080 &&
              footlight_fit_w(1366, 768) == 1365 && footlight_fit_h(1366, 768) == 768 &&
              footlight_fit_w(1080, 1920) == 1080 && footlight_fit_h(1080, 1920) == 607 &&
              footlight_fit_w(SM, SM) == SM && footlight_fit_h(SM, SM) == 36863 &&
              footlight_fit_w(-1, 9) == FOOTLIGHT_REFUSED &&
              footlight_fit_w(16, SM + 1) == FOOTLIGHT_REFUSED &&
              footlight_fit_h(SM + 1, 9) == FOOTLIGHT_REFUSED &&
              footlight_fit_h(16, -1) == FOOTLIGHT_REFUSED && footlight_bar(1200, 1080) == 60 &&
              footlight_bar(1081, 0) == 540 && footlight_bar(1080, 1200) == FOOTLIGHT_REFUSED &&
              footlight_bar(SM + 1, 0) == FOOTLIGHT_REFUSED &&
              footlight_bar(10, -1) == FOOTLIGHT_REFUSED,
          "Footlight: the C fit's own edges (FL1-FL3)");

    /* kinetoscope.video_fit_w and video_fit_h of a 16 by 9 picture (FL1,
     * FL2): windows swept, then the edges */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2 ? wrand(0, 4096) : wrand(0, SM);
        b[i] = i % 3 ? wrand(0, 4096) : wrand(0, SM);
        c[i] = FOOTLIGHT_ASPECT_W;
        d[i] = FOOTLIGHT_ASPECT_H;
    }
    a[0] = 0, b[0] = 0;       /* no window */
    a[1] = SM, b[1] = SM;     /* the largest */
    a[2] = 1920, b[2] = 1080; /* exactly 16:9 */
    a[3] = 1366, b[3] = 768;  /* a pixel off */
    a[4] = 1920, b[4] = 1200; /* 16:10: letterboxed */
    a[5] = 1080, b[5] = 1920; /* a portrait screen */
    a[6] = 2560, b[6] = 1080; /* 21:9: pillarboxed */
    a[7] = SM, b[7] = 0;
    a[8] = 0, b[8] = SM;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = footlight_fit_w(a[i], b[i]);
    zxapp_arg_t wa[4] = {{"vidw", c, WAVE_ROWS},
                         {"vidh", d, WAVE_ROWS},
                         {"winw", a, WAVE_ROWS},
                         {"winh", b, WAVE_ROWS}};
    CHECK(want[6] == 1920 &&
              wave_same(app, "kinetoscope.video_fit_w", wa, 4, want, WAVE_ROWS, false),
          "Footlight: a slide's width through kinetoscope.video_fit_w, KRON = C++ = C");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = footlight_fit_h(a[i], b[i]);
    CHECK(want[4] == 1080 &&
              wave_same(app, "kinetoscope.video_fit_h", wa, 4, want, WAVE_ROWS, false),
          "Footlight: a slide's height through kinetoscope.video_fit_h, KRON = C++ = C");
    {
        static const int64_t vw[2] = {FOOTLIGHT_ASPECT_W, FOOTLIGHT_ASPECT_W},
                             vh[2] = {FOOTLIGHT_ASPECT_H, FOOTLIGHT_ASPECT_H};
        static const int64_t ww[2] = {1920, SM + 1}, wh[2] = {1080, 1080};
        zxapp_arg_t xa[4] = {{"vidw", vw, 2}, {"vidh", vh, 2}, {"winw", ww, 2}, {"winh", wh, 2}};
        CHECK(wave_lp(app, "kinetoscope.video_fit_w", xa, 4) == ZXAPP_BOTH &&
                  wave_lp(app, "kinetoscope.video_fit_h", xa, 4) == ZXAPP_BOTH,
              "Footlight: a window past 65535 wide: C++ refuses, KRON answers: BOTH");
        static const int64_t nw[2] = {1920, 1920}, nh[2] = {1080, -1};
        zxapp_arg_t na[4] = {{"vidw", vw, 2}, {"vidh", vh, 2}, {"winw", nw, 2}, {"winh", nh, 2}};
        CHECK(wave_lp(app, "kinetoscope.video_fit_w", na, 4) == ZXAPP_BOTH &&
                  wave_lp(app, "kinetoscope.video_fit_h", na, 4) == ZXAPP_BOTH,
              "Footlight: a negative height: C++ refuses, KRON answers: BOTH");
    }

    /* letterbox_bar (FL3): each side of a fitted window, then the edges */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t w = wrand(0, i % 2 ? 4096 : SM), h = wrand(0, i % 3 ? 4096 : SM);
        a[i] = i % 2 ? w : h;
        b[i] = i % 2 ? footlight_fit_w(w, h) : footlight_fit_h(w, h);
    }
    a[0] = 0, b[0] = 0;
    a[1] = SM, b[1] = 0;
    a[2] = SM, b[2] = SM;
    a[3] = 1200, b[3] = 1080;
    a[4] = 1081, b[4] = 0; /* the odd pixel after the slide */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = footlight_bar(a[i], b[i]);
    zxapp_arg_t ba[2] = {{"span", a, WAVE_ROWS}, {"fit", b, WAVE_ROWS}};
    CHECK(want[1] == 32767 && want[3] == 60 && want[4] == 540 &&
              wave_same(app, "letterbox_bar", ba, 2, want, WAVE_ROWS, false),
          "Footlight: letterbox_bar, KRON = C++ = C");
    {
        static const int64_t sp[2] = {1200, 1080}, ft[2] = {1080, 1200};
        zxapp_arg_t xa[2] = {{"span", sp, 2}, {"fit", ft, 2}};
        CHECK(wave_lp(app, "letterbox_bar", xa, 2) == ZXAPP_BOTH,
              "Footlight: a fit past its span: C++ refuses, KRON answers a negative bar: BOTH");
        static const int64_t sb[2] = {1200, SM + 1}, fz[2] = {0, 0};
        zxapp_arg_t za[2] = {{"span", sb, 2}, {"fit", fz, 2}};
        CHECK(wave_lp(app, "letterbox_bar", za, 2) == ZXAPP_BOTH,
              "Footlight: a span past 65535: C++ refuses, KRON answers: BOTH");
    }

    /* rehearsal_over and slide_start (FL4, FL5): Trebago against the C */
    {
        static int64_t secs[FOOTLIGHT_DECK_MAX];
        bool all = true;
        for (uint32_t r = 0; r < 24; r++) {
            uint32_t n = r == 0 ? 1u : r == 1 ? WAVE_ROWS : 1u + (uint32_t) wrand(0, WAVE_ROWS - 1);
            for (uint32_t i = 0; i < n; i++)
                secs[i] = r == 1 ? FOOTLIGHT_SECS_MAX : wrand(1, r % 2 ? 300 : FOOTLIGHT_SECS_MAX);
            int64_t talk = r == 0 ? 1 : r == 1 ? FOOTLIGHT_TALK_MAX : wrand(1, FOOTLIGHT_TALK_MAX);
            all = fl_rehearsal_same(app, secs, n, talk) && all;
        }
        CHECK(all, "Footlight: 24 rehearsals, rehearsal_over and slide_start, Trebago = C");
        for (uint32_t i = 0; i < FOOTLIGHT_DECK_MAX; i++) secs[i] = FOOTLIGHT_SECS_MAX;
        int64_t over = 0, start[FOOTLIGHT_DECK_MAX];
        CHECK(footlight_rehearsal_over(secs, FOOTLIGHT_DECK_MAX, FOOTLIGHT_TALK_MAX, &over) &&
                  over == 3596400 - 86400 &&
                  footlight_slide_start(secs, FOOTLIGHT_DECK_MAX, start) &&
                  start[FOOTLIGHT_DECK_MAX - 1] == 3592800 &&
                  fl_rehearsal_same(app, secs, FOOTLIGHT_DECK_MAX, FOOTLIGHT_TALK_MAX),
              "Footlight: the longest rehearsal, 999 slides of an hour, Trebago = C");
        static const int64_t s3[3] = {60, 90, 120};
        int64_t st[3];
        CHECK(footlight_rehearsal_over(s3, 3, 300, &over) && over == -30 &&
                  footlight_slide_start(s3, 3, st) && st[0] == 0 && st[1] == 60 && st[2] == 150 &&
                  !footlight_rehearsal_over(s3, 0, 300, &over) &&
                  !footlight_rehearsal_over(s3, 3, 0, &over) &&
                  !footlight_rehearsal_over(s3, 3, FOOTLIGHT_TALK_MAX + 1, &over) &&
                  !footlight_rehearsal_over(secs, FOOTLIGHT_DECK_MAX + 1, 300, &over),
              "Footlight: the C rehearsal's own edges (FL4, FL5)");
        static const int64_t bad0[3] = {60, 0, 120}, bad1[3] = {60, FOOTLIGHT_SECS_MAX + 1, 1},
                             tk[1] = {300}, tk0[1] = {0};
        zxapp_arg_t r0[2] = {{"secs", bad0, 3}, {"talk", tk, 1}};
        zxapp_arg_t r1[2] = {{"secs", bad1, 3}, {"talk", tk, 1}};
        zxapp_arg_t r2[2] = {{"secs", s3, 3}, {"talk", tk0, 1}};
        zxapp_arg_t q0[1] = {{"secs", bad0, 3}};
        zxapp_arg_t q1[1] = {{"secs", bad1, 3}};
        CHECK(!footlight_rehearsal_over(bad0, 3, 300, &over) &&
                  !footlight_rehearsal_over(bad1, 3, 300, &over) &&
                  fl_refused(app, "rehearsal_over", r0, 2) &&
                  fl_refused(app, "rehearsal_over", r1, 2) &&
                  fl_refused(app, "rehearsal_over", r2, 2) &&
                  fl_refused(app, "slide_start", q0, 1) && fl_refused(app, "slide_start", q1, 1),
              "Footlight: a slide of 0 seconds or past an hour, or no talk: refused, never summed");
    }

    /* next_slide (FL6): BASIC = C, one row per call, 64 calls */
    {
        static const int64_t NS[][3] = {{1, 5, 0},     {5, 5, 0},    {5, 5, 2}, {3, 5, 1},
                                        {1, 1, 0},     {1, 1, 1},    {1, 1, 2}, {999, 999, 0},
                                        {999, 999, 2}, {998, 999, 0}};
        bool all = true;
        for (uint32_t r = 0; r < WAVE_ROWS; r++) {
            int64_t count = r < sizeof NS / sizeof NS[0] ? NS[r][1] : wrand(1, FOOTLIGHT_DECK_MAX);
            int64_t cur = r < sizeof NS / sizeof NS[0] ? NS[r][0] : wrand(1, count);
            int64_t mode = r < sizeof NS / sizeof NS[0] ? NS[r][2] : wrand(0, 2);
            all = fl_next_same(app, cur, count, mode) && all;
        }
        CHECK(all, "Footlight: next_slide, BASIC = C, 64 steps");
        CHECK(footlight_next(1, 5, 0) == 2 && footlight_next(5, 5, 0) == FOOTLIGHT_END &&
                  footlight_next(5, 5, 2) == 1 && footlight_next(3, 5, 1) == 3 &&
                  footlight_next(0, 5, 0) == FOOTLIGHT_REFUSED &&
                  footlight_next(6, 5, 1) == FOOTLIGHT_REFUSED &&
                  footlight_next(1, 0, 0) == FOOTLIGHT_REFUSED &&
                  footlight_next(1, FOOTLIGHT_DECK_MAX + 1, 0) == FOOTLIGHT_REFUSED &&
                  footlight_next(1, 5, 3) == FOOTLIGHT_REFUSED &&
                  footlight_next(1, 5, -1) == FOOTLIGHT_REFUSED,
              "Footlight: the C run order's own edges (FL6)");
        static const int64_t RF[][3] = {{0, 5, 0}, {6, 5, 1}, {1, 0, 0}, {1, 1000, 0}, {1, 5, 3}};
        bool refused = true;
        for (uint32_t r = 0; r < sizeof RF / sizeof RF[0]; r++) {
            int64_t cv[1] = {RF[r][0]}, nv[1] = {RF[r][1]}, mv[1] = {RF[r][2]};
            zxapp_arg_t xa[3] = {{"cur", cv, 1}, {"count", nv, 1}, {"mode", mv, 1}};
            refused = fl_refused(app, "next_slide", xa, 3) && refused;
        }
        CHECK(refused,
              "Footlight: a slide outside its deck, a deck past 999, a fourth mode: refused");
    }

    /* slide_record_ok (FL7): MYRA's digit widths, i9 i4 i4 i1 i7 i15 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1999999999, 1999999999);
        b[i] = wrand(-19999, 19999);
        c[i] = wrand(-19999, 19999);
        d[i] = wrand(-19, 19);
        e[i] = wrand(-19999999, 19999999);
        g[i] = i % 2 ? wrand(0, INT64_C(1999999999999999)) : wrand(0, INT64_C(999999999999));
        want[i] = T_(footlight_slide_record_ok(a[i], b[i], c[i], d[i], e[i], g[i]));
    }
    zxapp_arg_t ra[6] = {{"deck", a, WAVE_ROWS},  {"slide", b, WAVE_ROWS},
                         {"secs", c, WAVE_ROWS},  {"layout", d, WAVE_ROWS},
                         {"bytes", e, WAVE_ROWS}, {"stamp", g, WAVE_ROWS}};
    CHECK(wave_same(app, "slide_record_ok", ra, 6, want, WAVE_ROWS, true),
          "Footlight: slide_record_ok sweep (MYRA = C)");
    {
        static const int64_t rd[7] = {0, 999999999, -999999999, 1000000000, 1, 1, 1},
                             rs[7] = {0, 9999, -9999, 1, 10000, 1, 1},
                             rc[7] = {0, 9999, -9999, 1, 1, 1, 1}, rl[7] = {0, 9, -9, 1, 1, 10, 1},
                             rb[7] = {0, 9999999, -9999999, 1, 1, 1, 10000000},
                             rt[7] = {
                                 0, INT64_C(999999999999999), INT64_C(-999999999999999), 1, 1, 1,
                                 1};
        int64_t rw[7];
        for (uint32_t i = 0; i < 7; i++)
            rw[i] = T_(footlight_slide_record_ok(rd[i], rs[i], rc[i], rl[i], rb[i], rt[i]));
        zxapp_arg_t xa[6] = {{"deck", rd, 7},   {"slide", rs, 7}, {"secs", rc, 7},
                             {"layout", rl, 7}, {"bytes", rb, 7}, {"stamp", rt, 7}};
        CHECK(rw[0] == ZXAPP_TRUE && rw[1] == ZXAPP_TRUE && rw[2] == ZXAPP_TRUE &&
                  rw[3] == ZXAPP_FALSE && rw[4] == ZXAPP_FALSE && rw[5] == ZXAPP_FALSE &&
                  rw[6] == ZXAPP_FALSE && wave_same(app, "slide_record_ok", xa, 6, rw, 7, true),
              "Footlight: slide_record_ok at 0, the widest values either side, and one digit past");
    }

    /* halloo.hms (FL8): a talk, a rehearsal and its time over shown, KRON = Sutra = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2 ? 1000 * wrand(0, FOOTLIGHT_TALK_MAX) : 1000 * wrand(0, 3596400);
        want[i] = footlight_hms(a[i]);
    }
    a[0] = 0, want[0] = footlight_hms(0);
    a[1] = INT64_C(3596400000), want[1] = footlight_hms(a[1]); /* the longest rehearsal */
    a[2] = 270000, want[2] = footlight_hms(a[2]);
    zxapp_arg_t ha = {"ms", a, WAVE_ROWS};
    CHECK(want[1] == 9990000 && want[2] == 430 &&
              wave_same(app, "halloo.hms", &ha, 1, want, WAVE_ROWS, false),
          "Footlight: lengths shown through halloo.hms, KRON = Sutra = C");
}
