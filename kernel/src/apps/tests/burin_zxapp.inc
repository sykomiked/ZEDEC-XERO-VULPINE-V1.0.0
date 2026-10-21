/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Burin's languages against the C its model runs (burin_rules.h).
 * Slots: @num.burin_bezier_at, @num.burin_hatch_lines, @num.burin_hatch_gap,
 * @logic.burin_path_limits_ok, @logic.burin_drawing_record_ok. */
#include "burin_rules.h"

/* hatch_gap's rows, each an exact fraction: KRON = Trebago (lp TRUE) and
 * every row is span / (lines + 1), reduced, with a positive denominator. */
static bool burin_gap_rows(zxapp_t *a, const int64_t *span, const int64_t *lines, uint32_t rows)
{
    zxapp_arg_t ga[2] = {{"span", span, rows}, {"lines", lines, rows}};
    zxapp_num_t o[WAVE_ROWS];
    uint32_t k = 0;
    int32_t lp = -1;
    int rc = zxapp_call_witnessed(a, &H, "hatch_gap", ga, 2, o, WAVE_ROWS, &k, &lp);
    if (rc != ZXAPP_OK || lp != ZXAPP_TRUE || k != rows) {
        printf("    hatch_gap: rc %d lp %d n %u (%s)\n", rc, (int) lp, k, a->err);
        return false;
    }
    for (uint32_t i = 0; i < rows; i++) {
        int64_t num = 0, den = 0, g = o[i].num, h = o[i].den;
        if (!burin_hatch_gap(span[i], lines[i], &num, &den) || h < 1 || o[i].num * den != num * h)
            return false;
        while (h) { /* reduced: gcd(num, den) is 1 */
            int64_t t = g % h;
            g = h;
            h = t;
        }
        if (g != 1 && g != -1 && o[i].num != 0) return false;
        if (o[i].num == 0 && o[i].den != 1) return false;
    }
    return true;
}

APP_TEST(burin, 8, 54)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], s[WAVE_ROWS],
        n[WAVE_ROWS], want[WAVE_ROWS];

    /* bezier_at (BR1): whole sweeps at every scale of step count */
    static const int64_t most[4] = {1024, 3, 64, 1};
    for (uint32_t r = 0; r < 4; r++) {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = wrand(-BURIN_COORD_MAX, BURIN_COORD_MAX);
            b[i] = wrand(-BURIN_COORD_MAX, BURIN_COORD_MAX);
            c[i] = wrand(-BURIN_COORD_MAX, BURIN_COORD_MAX);
            d[i] = r == 3 ? wrand(-9, 9) : wrand(-BURIN_COORD_MAX, BURIN_COORD_MAX);
            n[i] = wrand(1, most[r]);
            s[i] = wrand(0, n[i]);
            want[i] = burin_bezier_at(a[i], b[i], c[i], d[i], s[i], n[i]);
        }
        zxapp_arg_t ba[6] = {{"pa", a, WAVE_ROWS}, {"pb", b, WAVE_ROWS},   {"pc", c, WAVE_ROWS},
                             {"pd", d, WAVE_ROWS}, {"step", s, WAVE_ROWS}, {"steps", n, WAVE_ROWS}};
        CHECK(wave_same(app, "bezier_at", ba, 6, want, WAVE_ROWS, false),
              "Burin: bezier_at sweep %u, C++ = KRON = C", r);
    }
    {
        /* the edges: both ends of t, the corners of the domain, floors of a
         * negative eighth, and one step along a straight line */
        static const int64_t ea[8] = {-1000000, 0, 0, 0, 0, 1000000, -1000000, 0};
        static const int64_t eb[8] = {1000000, 0, 0, 100, 0, -1000000, -1000000, 0};
        static const int64_t ec[8] = {1000000, 0, 0, 200, 100, 1000000, -1000000, 0};
        static const int64_t ed[8] = {1000000, 1, -1, 300, 100, -1000000, -1000000, 1000000};
        static const int64_t es[8] = {0, 1, 1, 1, 1, 1024, 512, 1024};
        static const int64_t en[8] = {1024, 2, 2, 3, 2, 1024, 1024, 1024};
        static const int64_t ew[8] = {-1000000, 0, -1, 100, 50, -1000000, -1000000, 1000000};
        for (uint32_t i = 0; i < 8; i++) {
            CHECK(burin_bezier_at(ea[i], eb[i], ec[i], ed[i], es[i], en[i]) == ew[i],
                  "Burin: the C rule at bezier edge %u", i);
            int64_t va[2] = {ea[i], ea[i]}, vb[2] = {eb[i], eb[i]}, vc[2] = {ec[i], ec[i]},
                    vd[2] = {ed[i], ed[i]}, vs[2] = {es[i], es[i]}, vn[2] = {en[i], en[i]},
                    w[2] = {ew[i], ew[i]};
            zxapp_arg_t ea2[6] = {{"pa", va, 2}, {"pb", vb, 2},   {"pc", vc, 2},
                                  {"pd", vd, 2}, {"step", vs, 2}, {"steps", vn, 2}};
            CHECK(wave_same(app, "bezier_at", ea2, 6, w, 2, false), "Burin: bezier_at edge %u", i);
        }
        CHECK(burin_bezier_at(1000001, 0, 0, 0, 0, 1) == BURIN_REFUSED &&
                  burin_bezier_at(0, 0, 0, -1000001, 0, 1) == BURIN_REFUSED &&
                  burin_bezier_at(0, 0, 0, 0, 0, 0) == BURIN_REFUSED &&
                  burin_bezier_at(0, 0, 0, 0, 2, 1) == BURIN_REFUSED &&
                  burin_bezier_at(0, 0, 0, 0, -1, 1) == BURIN_REFUSED &&
                  burin_bezier_at(0, 0, 0, 0, 0, 1025) == BURIN_REFUSED,
              "Burin: the C rule refuses outside its domain");
    }
    {
        /* past the domain: the C++ refuses (-1000001), KRON answers: held */
        int64_t z[2] = {0, 0}, one[2] = {1, 1}, two[2] = {2, 2}, big[2] = {0, 2000000},
                eight[2] = {8, 8}, wide[2] = {2, 2000}, back[2] = {1, -1};
        zxapp_arg_t pa[6] = {{"pa", big, 2}, {"pb", z, 2},     {"pc", z, 2},
                             {"pd", z, 2},   {"step", one, 2}, {"steps", two, 2}};
        CHECK(wave_lp(app, "bezier_at", pa, 6) == ZXAPP_BOTH,
              "Burin: a control point past 1000000: C++ refuses, KRON answers: BOTH");
        zxapp_arg_t wa[6] = {{"pa", z, 2},     {"pb", z, 2},     {"pc", z, 2},
                             {"pd", eight, 2}, {"step", one, 2}, {"steps", wide, 2}};
        CHECK(wave_lp(app, "bezier_at", wa, 6) == ZXAPP_BOTH, "Burin: 2000 steps: BOTH");
        zxapp_arg_t na[6] = {{"pa", z, 2},     {"pb", z, 2},      {"pc", z, 2},
                             {"pd", eight, 2}, {"step", back, 2}, {"steps", two, 2}};
        CHECK(wave_lp(app, "bezier_at", na, 6) == ZXAPP_BOTH, "Burin: a step before 0: BOTH");
    }

    /* hatch_lines (BR2): spans across the domain at wide, narrow and 1 gaps */
    static const int64_t gaps[3] = {BURIN_SPAN_MAX, 999, 1};
    for (uint32_t r = 0; r < 3; r++) {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            s[i] = i ? wrand(0, BURIN_SPAN_MAX) : 0;
            n[i] = wrand(1, gaps[r]);
            want[i] = burin_hatch_lines(s[i], n[i]);
        }
        zxapp_arg_t ha[2] = {{"span", s, WAVE_ROWS}, {"gap", n, WAVE_ROWS}};
        CHECK(wave_same(app, "hatch_lines", ha, 2, want, WAVE_ROWS, false),
              "Burin: hatch_lines sweep %u, Trebago = KRON = C", r);
    }
    {
        static const int64_t es[8] = {0, 1, 100, 99, 101, 9999999, 9999999, 20};
        static const int64_t eg[8] = {10, 10, 20, 20, 20, 1, 9999999, 20};
        static const int64_t ew[8] = {0, 0, 4, 4, 5, 9999998, 0, 0};
        for (uint32_t i = 0; i < 8; i++) {
            s[i] = es[i];
            n[i] = eg[i];
            CHECK(burin_hatch_lines(es[i], eg[i]) == ew[i], "Burin: the C rule at hatch edge %u",
                  i);
        }
        zxapp_arg_t ha[2] = {{"span", s, 8}, {"gap", n, 8}};
        CHECK(wave_same(app, "hatch_lines", ha, 2, ew, 8, false), "Burin: hatch_lines edges");
        CHECK(burin_hatch_lines(-1, 1) == -1 && burin_hatch_lines(10000000, 1) == -1 &&
                  burin_hatch_lines(1, 0) == -1 && burin_hatch_lines(1, 10000000) == -1,
              "Burin: hatch_lines refuses outside its domain");
        int64_t sv[2] = {100, -5}, gv[2] = {20, 20}, past[2] = {100, 10000000},
                wide[2] = {20, 10000000}, zero[2] = {20, 0};
        zxapp_arg_t na[2] = {{"span", sv, 2}, {"gap", gv, 2}};
        CHECK(wave_lp(app, "hatch_lines", na, 2) == ZXAPP_BOTH,
              "Burin: a negative span: Trebago refuses, KRON answers 0: BOTH");
        zxapp_arg_t pa[2] = {{"span", past, 2}, {"gap", gv, 2}};
        CHECK(wave_lp(app, "hatch_lines", pa, 2) == ZXAPP_BOTH, "Burin: a span past 9999999: BOTH");
        zxapp_arg_t wa[2] = {{"span", gv, 2}, {"gap", wide, 2}};
        CHECK(wave_lp(app, "hatch_lines", wa, 2) == ZXAPP_BOTH, "Burin: a gap past 9999999: BOTH");
        zxapp_num_t o[2];
        uint32_t k = 0;
        zxapp_arg_t za[2] = {{"span", gv, 2}, {"gap", zero, 2}};
        CHECK(zxapp_call(app, &H, "hatch_lines", za, 2, o, 2, &k) != ZXAPP_OK,
              "Burin: a gap of 0 is refused by both");
    }

    /* hatch_gap (BR3): exact fractions, never rounded */
    for (uint32_t r = 0; r < 3; r++) {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            s[i] = wrand(0, r == 2 ? 999 : BURIN_SPAN_MAX);
            n[i] = wrand(1, r == 1 ? 9 : BURIN_HATCH_MAX);
        }
        CHECK(burin_gap_rows(app, s, n, WAVE_ROWS), "Burin: hatch_gap sweep %u, exact", r);
    }
    {
        /* whole spacings through wave_same, and BR2 at that spacing gives
         * the count back */
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            n[i] = wrand(1, 99);
            want[i] = wrand(1, 99999);
            s[i] = want[i] * (n[i] + 1);
        }
        n[0] = BURIN_HATCH_MAX;
        want[0] = 999;
        s[0] = 999 * (BURIN_HATCH_MAX + 1);
        uint32_t back = 0;
        for (uint32_t i = 0; i < WAVE_ROWS; i++) back += burin_hatch_lines(s[i], want[i]) == n[i];
        CHECK(back == WAVE_ROWS, "Burin: BR2 at BR3's spacing gives the count (%u of %u)", back,
              (unsigned) WAVE_ROWS);
        zxapp_arg_t ga[2] = {{"span", s, WAVE_ROWS}, {"lines", n, WAVE_ROWS}};
        CHECK(wave_same(app, "hatch_gap", ga, 2, want, WAVE_ROWS, false),
              "Burin: whole hatch spacings, Trebago = KRON = C");
        static const int64_t es[4] = {100, 0, 7, 9999999}, el[4] = {4, 1, 2, 9999};
        CHECK(burin_gap_rows(app, es, el, 4), "Burin: hatch_gap edges, 7/3 and 9999999/10000");
        int64_t num = 0, den = 0;
        CHECK(!burin_hatch_gap(-1, 1, &num, &den) && !burin_hatch_gap(10000000, 1, &num, &den) &&
                  !burin_hatch_gap(1, 0, &num, &den) && !burin_hatch_gap(1, 10000, &num, &den),
              "Burin: hatch_gap refuses outside its domain");
        int64_t sv[2] = {100, 100}, none[2] = {4, 0}, many[2] = {4, 10000}, neg[2] = {100, -1},
                four[2] = {4, 4};
        zxapp_arg_t na[2] = {{"span", sv, 2}, {"lines", none, 2}};
        CHECK(wave_lp(app, "hatch_gap", na, 2) == ZXAPP_BOTH,
              "Burin: no lines: Trebago refuses, KRON answers the span: BOTH");
        zxapp_arg_t ma[2] = {{"span", sv, 2}, {"lines", many, 2}};
        CHECK(wave_lp(app, "hatch_gap", ma, 2) == ZXAPP_BOTH, "Burin: 10000 lines: BOTH");
        zxapp_arg_t sa[2] = {{"span", neg, 2}, {"lines", four, 2}};
        CHECK(wave_lp(app, "hatch_gap", sa, 2) == ZXAPP_BOTH, "Burin: a negative span: BOTH");
    }

    /* path_limits_ok (BR4): Eve's ranges, at and past every edge, and a sweep */
    {
        static const int64_t ep[10] = {2, 769, 1, 770, 2, 2, 2, 2, 2, 2};
        static const int64_t ew[10] = {1, 1000000, 1, 1, 0, 1000001, 1, 1, 1, 1};
        static const int64_t eh[10] = {1, 1000000, 1, 1, 1, 1, 0, 1000001, 1, 1};
        static const int64_t ek[10] = {1, 10000, 1, 1, 1, 1, 1, 1, 0, 10001};
        for (uint32_t i = 0; i < 10; i++) {
            a[i] = ep[i];
            b[i] = ew[i];
            c[i] = eh[i];
            d[i] = ek[i];
            want[i] = T_(burin_path_limits_ok(ep[i], ew[i], eh[i], ek[i]));
        }
        CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
                  want[9] == ZXAPP_FALSE,
              "Burin: path_limits_ok, the C's own edges");
        zxapp_arg_t la[4] = {
            {"points", a, 10}, {"width", b, 10}, {"height", c, 10}, {"stroke", d, 10}};
        CHECK(wave_same(app, "path_limits_ok", la, 4, want, 10, true),
              "Burin: path_limits_ok edges (Eve = C)");
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = wrand(-5, 800);
            b[i] = wrand(-5, 1000005);
            c[i] = wrand(-5, 1000005);
            d[i] = wrand(-5, 10005);
            want[i] = T_(burin_path_limits_ok(a[i], b[i], c[i], d[i]));
        }
        zxapp_arg_t sa[4] = {{"points", a, WAVE_ROWS},
                             {"width", b, WAVE_ROWS},
                             {"height", c, WAVE_ROWS},
                             {"stroke", d, WAVE_ROWS}};
        CHECK(wave_same(app, "path_limits_ok", sa, 4, want, WAVE_ROWS, true),
              "Burin: path_limits_ok sweep (Eve = C)");
    }

    /* drawing_record_ok (BR5): MYRA's digit widths, i9 i3 i5 i12 */
    {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = wrand(-1999999999, 1999999999);
            b[i] = wrand(-1999, 1999);
            c[i] = wrand(-199999, 199999);
            d[i] = wrand(-INT64_C(1999999999999), INT64_C(1999999999999));
            want[i] = T_(burin_record_ok(a[i], b[i], c[i], d[i]));
        }
        zxapp_arg_t ra[4] = {{"path", a, WAVE_ROWS},
                             {"points", b, WAVE_ROWS},
                             {"stroke", c, WAVE_ROWS},
                             {"mtime", d, WAVE_ROWS}};
        CHECK(wave_same(app, "drawing_record_ok", ra, 4, want, WAVE_ROWS, true),
              "Burin: drawing_record_ok sweep (MYRA = C)");
        int64_t ea[6] = {0, 999999999, 1000000000, -999999999, 0, 0},
                eb[6] = {0, 999, 1, -999, 1000, 1}, ec[6] = {0, 99999, 1, -99999, 1, 100000},
                ed[6] = {0, INT64_C(999999999999), 1, -1, 1, 1},
                ew[6] = {ZXAPP_TRUE, ZXAPP_TRUE, ZXAPP_FALSE, ZXAPP_TRUE, ZXAPP_FALSE, ZXAPP_FALSE};
        zxapp_arg_t xa[4] = {
            {"path", ea, 6}, {"points", eb, 6}, {"stroke", ec, 6}, {"mtime", ed, 6}};
        CHECK(wave_same(app, "drawing_record_ok", xa, 4, ew, 6, true),
              "Burin: drawing_record_ok at 0, the widest values either side, and one past each");
    }
}
