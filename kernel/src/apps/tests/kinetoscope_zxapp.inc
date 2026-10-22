/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Kinetoscope's languages against the C its model runs (kinetoscope_rules.h).
 * Slots: @num.kinetoscope_video_fit_w, @num.kinetoscope_video_fit_h,
 * @num.kinetoscope_frame_at_pos, @logic.kinetoscope_hall_rate_ok,
 * @logic.kinetoscope_resume_row_ok and, used as they are, Witness's
 * @num.witness_kind_count and Halloo's @num.halloo_hms. */
#include "kinetoscope_rules.h"

APP_TEST(kinetoscope, 21, 51)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], want[WAVE_ROWS],
        want2[WAVE_ROWS];
    const int64_t SM = KINETOSCOPE_SIDE_MAX;

    /* the C's own fit (KR1, KR2): for many pictures in every window up to
     * 300 by 300, the fit is inside the window, fills one side of it, and
     * keeps the aspect to within a pixel */
    {
        static const int64_t pw[6] = {16, 4, 9, 1, 300, 21}, ph[6] = {9, 3, 16, 1, 1, 9};
        bool rule = true;
        for (uint32_t p = 0; p < 6 && rule; p++)
            for (int64_t w = 0; w <= 300 && rule; w++)
                for (int64_t h = 0; h <= 300 && rule; h++) {
                    int64_t fw = kinetoscope_fit_w(pw[p], ph[p], w, h);
                    int64_t fh = kinetoscope_fit_h(pw[p], ph[p], w, h);
                    rule = fw >= 0 && fh >= 0 && fw <= w && fh <= h && (fw == w || fh == h) &&
                           fh * pw[p] <= fw * ph[p] + ph[p] && fw * ph[p] <= fh * pw[p] + pw[p];
                }
        CHECK(rule, "Kinetoscope: every fit is inside its window, fills a side, keeps the aspect");
        CHECK(kinetoscope_fit_w(0, 9, 10, 10) == KINETOSCOPE_REFUSED &&
                  kinetoscope_fit_w(16, SM + 1, 10, 10) == KINETOSCOPE_REFUSED &&
                  kinetoscope_fit_h(16, 9, -1, 10) == KINETOSCOPE_REFUSED &&
                  kinetoscope_fit_h(16, 9, 10, SM + 1) == KINETOSCOPE_REFUSED &&
                  kinetoscope_fit_w(SM, SM, SM, SM) == SM && kinetoscope_fit_h(1, SM, SM, SM) == SM,
              "Kinetoscope: the C fit's own edges (KR1, KR2)");
    }

    /* video_fit_w and video_fit_h: KRON = C++ = C, swept, then every edge */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, i < 32 ? 4096 : SM);
        b[i] = wrand(1, i < 32 ? 4096 : SM);
        c[i] = wrand(0, SM);
        d[i] = wrand(0, SM);
    }
    a[0] = 1, b[0] = 1, c[0] = 0, d[0] = 0;     /* the least */
    a[1] = SM, b[1] = SM, c[1] = SM, d[1] = SM; /* every max */
    a[2] = 1, b[2] = SM, c[2] = SM, d[2] = SM;  /* the tallest picture */
    a[3] = SM, b[3] = 1, c[3] = SM, d[3] = SM;  /* the widest */
    a[4] = 1920, b[4] = 1080, c[4] = 1280, d[4] = 1024;
    a[5] = 640, b[5] = 480, c[5] = 1920, d[5] = 1080;
    a[6] = 1080, b[6] = 1920, c[6] = 1366, d[6] = 768;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        want[i] = kinetoscope_fit_w(a[i], b[i], c[i], d[i]);
        want2[i] = kinetoscope_fit_h(a[i], b[i], c[i], d[i]);
    }
    CHECK(want[4] == 1280 && want2[4] == 720 && want[5] == 1440 && want2[5] == 1080 &&
              want[6] == 432 && want2[6] == 768 && want[2] == 1 && want2[3] == 1,
          "Kinetoscope: letterbox, pillarbox and the narrowest pictures, by hand");
    {
        zxapp_arg_t fa[4] = {{"vidw", a, WAVE_ROWS},
                             {"vidh", b, WAVE_ROWS},
                             {"winw", c, WAVE_ROWS},
                             {"winh", d, WAVE_ROWS}};
        CHECK(wave_same(app, "video_fit_w", fa, 4, want, WAVE_ROWS, false),
              "Kinetoscope: video_fit_w, KRON = C++ = C");
        CHECK(wave_same(app, "video_fit_h", fa, 4, want2, WAVE_ROWS, false),
              "Kinetoscope: video_fit_h, KRON = C++ = C");
    }
    {
        /* past the domain: the C++ refuses (traps), KRON answers: held */
        static const int64_t vw[2] = {1920, 1920}, vh[2] = {1080, 1080}, ww[2] = {1920, SM + 1},
                             wh[2] = {1080, 1080}, nw[2] = {1920, 1920}, nh[2] = {1080, -1},
                             bw[2] = {1920, SM + 1};
        zxapp_arg_t xa[4] = {{"vidw", vw, 2}, {"vidh", vh, 2}, {"winw", ww, 2}, {"winh", wh, 2}};
        CHECK(wave_lp(app, "video_fit_w", xa, 4) == ZXAPP_BOTH,
              "Kinetoscope: a window past 65535: C++ refuses, KRON answers: BOTH");
        zxapp_arg_t ya[4] = {{"vidw", vw, 2}, {"vidh", vh, 2}, {"winw", nw, 2}, {"winh", nh, 2}};
        CHECK(wave_lp(app, "video_fit_h", ya, 4) == ZXAPP_BOTH,
              "Kinetoscope: a negative window: C++ refuses, KRON answers: BOTH");
        zxapp_arg_t za[4] = {{"vidw", bw, 2}, {"vidh", vh, 2}, {"winw", nw, 2}, {"winh", wh, 2}};
        CHECK(wave_lp(app, "video_fit_w", za, 4) == ZXAPP_BOTH &&
                  wave_lp(app, "video_fit_h", za, 4) == ZXAPP_BOTH,
              "Kinetoscope: a picture past 65535: C++ refuses, KRON answers: BOTH");
    }

    /* frame_at_pos (KR3): exact rationals, Trebago against the C's reduced num/den */
    {
        static const int64_t rn[4] = {30, 30000, 24, 999999}, rd[4] = {1, 1001, 1, 1};
        for (uint32_t r = 0; r < 4; r++) {
            for (uint32_t i = 0; i < WAVE_ROWS; i++) {
                a[i] = wrand(0, i < 32 ? 600000 : KINETOSCOPE_POS_MAX);
                b[i] = rn[r];
                c[i] = rd[r];
            }
            a[0] = 0, a[1] = KINETOSCOPE_POS_MAX, a[2] = 1;
            zxapp_arg_t ra[3] = {
                {"pos", a, WAVE_ROWS}, {"fnum", b, WAVE_ROWS}, {"fden", c, WAVE_ROWS}};
            zxapp_num_t o[WAVE_ROWS];
            uint32_t k = 0, good = 0;
            int rc = zxapp_call(app, &H, "frame_at_pos", ra, 3, o, WAVE_ROWS, &k);
            for (uint32_t i = 0; rc == ZXAPP_OK && k == WAVE_ROWS && i < WAVE_ROWS; i++) {
                int64_t n = 0, dd = 0;
                good += kinetoscope_frame_at(a[i], b[i], c[i], &n, &dd) && o[i].num == n &&
                        o[i].den == dd;
            }
            CHECK(good == WAVE_ROWS,
                  "Kinetoscope: frame_at_pos at %lld/%lld, Trebago = C (%u of %u)",
                  (long long) rn[r], (long long) rd[r], good, (unsigned) WAVE_ROWS);
        }
        /* whole frames at 25 per second: every 40 ms */
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = 40 * wrand(0, 8999999);
            b[i] = 25;
            c[i] = 1;
            want[i] = kinetoscope_frame_index(a[i], 25, 1);
        }
        zxapp_arg_t wa[3] = {{"pos", a, WAVE_ROWS}, {"fnum", b, WAVE_ROWS}, {"fden", c, WAVE_ROWS}};
        CHECK(wave_same(app, "frame_at_pos", wa, 3, want, WAVE_ROWS, false),
              "Kinetoscope: whole frames at 25 a second (Trebago, unwitnessed)");
        CHECK(kinetoscope_frame_index(1001, 30000, 1001) == 30 &&
                  kinetoscope_frame_index(33, 30, 1) == 0 &&
                  kinetoscope_frame_index(34, 30, 1) == 1 &&
                  kinetoscope_frame_index(-1, 30, 1) == KINETOSCOPE_REFUSED &&
                  kinetoscope_frame_index(KINETOSCOPE_POS_MAX + 1, 30, 1) == KINETOSCOPE_REFUSED &&
                  kinetoscope_frame_index(0, 0, 1) == KINETOSCOPE_REFUSED &&
                  kinetoscope_frame_index(0, 30, KINETOSCOPE_FRATE_MAX + 1) == KINETOSCOPE_REFUSED,
              "Kinetoscope: the C frame's own edges (KR3)");
        /* outside the domain Trebago refuses: a negative position, past 100
         * hours, a rate of 0 or past 999999 */
        static const int64_t ep[4] = {-1, KINETOSCOPE_POS_MAX + 1, 1000, 1000},
                             en[4] = {30, 30, 0, KINETOSCOPE_FRATE_MAX + 1}, ed[4] = {1, 1, 1, 1};
        uint32_t refused = 0;
        for (uint32_t i = 0; i < 4; i++) {
            int64_t p2[2] = {0, ep[i]}, n2[2] = {30, en[i]}, d2[2] = {1, ed[i]};
            zxapp_arg_t xa[3] = {{"pos", p2, 2}, {"fnum", n2, 2}, {"fden", d2, 2}};
            zxapp_num_t o[2];
            uint32_t k = 0;
            refused += zxapp_call(app, &H, "frame_at_pos", xa, 3, o, 2, &k) != ZXAPP_OK;
        }
        CHECK(refused == 4, "Kinetoscope: frame_at_pos outside its domain is refused (%u of 4)",
              refused);
    }

    /* hall_rate_ok (KR4): Eve's range = C, swept across and past each end */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-10, KINETOSCOPE_RATE_MAX + 10);
        want[i] = T_(kinetoscope_hall_rate_ok(a[i]));
    }
    a[0] = 0, a[1] = 1, a[2] = KINETOSCOPE_RATE_UNIT, a[3] = KINETOSCOPE_RATE_MAX;
    a[4] = KINETOSCOPE_RATE_MAX + 1, a[5] = -1, a[6] = INT64_C(-1000000000000);
    for (uint32_t i = 0; i < 7; i++) want[i] = T_(kinetoscope_hall_rate_ok(a[i]));
    CHECK(want[0] == ZXAPP_FALSE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_TRUE &&
              want[3] == ZXAPP_TRUE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE,
          "Kinetoscope: the C rate's own edges (KR4)");
    {
        zxapp_arg_t ra = {"rate", a, WAVE_ROWS};
        CHECK(wave_same(app, "hall_rate_ok", &ra, 1, want, WAVE_ROWS, true),
              "Kinetoscope: hall_rate_ok, Eve = C");
    }

    /* resume_row_ok (KR5): MYRA's widths = C, swept and at every edge */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-19999, 19999);
        b[i] = wrand(-1999999999, 1999999999);
        c[i] = wrand(-1999999999, 1999999999);
        d[i] = wrand(INT64_C(-199999999999), INT64_C(199999999999));
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0;
    a[1] = KINETOSCOPE_VIDEO_MAX, b[1] = KINETOSCOPE_MS_MAX, c[1] = KINETOSCOPE_MS_MAX,
    d[1] = KINETOSCOPE_STAMP_MAX;
    a[2] = KINETOSCOPE_VIDEO_MAX + 1, b[2] = 1, c[2] = 1, d[2] = 1;
    a[3] = -KINETOSCOPE_VIDEO_MAX, b[3] = -1, c[3] = 1, d[3] = 1;
    a[4] = 1, b[4] = 1, c[4] = 1, d[4] = KINETOSCOPE_STAMP_MAX + 1;
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(kinetoscope_resume_row_ok(a[i], b[i], c[i], d[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_TRUE && want[4] == ZXAPP_FALSE,
          "Kinetoscope: the C resume record's own edges (KR5)");
    {
        zxapp_arg_t ma[4] = {{"video", a, WAVE_ROWS},
                             {"pos", b, WAVE_ROWS},
                             {"length", c, WAVE_ROWS},
                             {"stamp", d, WAVE_ROWS}};
        CHECK(wave_same(app, "resume_row_ok", ma, 4, want, WAVE_ROWS, true),
              "Kinetoscope: resume_row_ok, MYRA = C");
    }

    /* witness.kind_count (KR6): the seek points of a plan, the model's C */
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t key[WAVE_ROWS], one[1] = {1}, count;
        uint32_t n = 1u + (uint32_t) wrand(0, WAVE_ROWS - 1);
        for (uint32_t i = 0; i < n; i++) key[i] = wrand(0, 1);
        count = kinetoscope_seek_points(key, n);
        zxapp_arg_t ka[2] = {{"kind", key, n}, {"want", one, 1}};
        CHECK(wave_same(app, "witness.kind_count", ka, 2, &count, 1, false),
              "Kinetoscope: plan %u of %u segments, %lld seek points through witness.kind_count", r,
              n, (long long) count);
    }
    {
        static const int64_t k2[2] = {1, 2}, w2[2] = {1, 1};
        zxapp_arg_t ka[2] = {{"kind", k2, 2}, {"want", w2, 2}};
        CHECK(wave_lp(app, "witness.kind_count", ka, 2) == ZXAPP_BOTH,
              "Kinetoscope: two wants at once: Trebago refuses, KRON sums them: BOTH");
    }

    /* halloo.hms (KR7): a position or a length shown */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, KINETOSCOPE_POS_MAX);
        want[i] = kinetoscope_hms(a[i]);
    }
    a[0] = 0, want[0] = 0;
    a[1] = KINETOSCOPE_POS_MAX, want[1] = 995959; /* 99:59:59 */
    a[2] = 5025000, want[2] = 12345;
    CHECK(kinetoscope_hms(a[1]) == want[1] && kinetoscope_hms(a[2]) == want[2],
          "Kinetoscope: the longest position is still inside halloo.hms");
    {
        zxapp_arg_t ha = {"ms", a, WAVE_ROWS};
        CHECK(wave_same(app, "halloo.hms", &ha, 1, want, WAVE_ROWS, false),
              "Kinetoscope: a position through halloo.hms, KRON = Sutra = C");
    }
}
