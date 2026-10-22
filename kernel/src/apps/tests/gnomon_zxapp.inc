/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Gnomon's languages against the C its model runs (gnomon_rules.h).
 * Slots: @num.gnomon_polygon_area2, @num.gnomon_polygon_area,
 * @num.gnomon_triangle_area2, @num.gnomon_side_len_sq,
 * @logic.gnomon_figure_record_ok. */
#include "gnomon_rules.h"

/* polygon_area2 or polygon_area (half) over vertex rows: KRON and Trebago
 * agree (TRUE) on the C's one answer, or, when the C refuses, the call is
 * held (BOTH) or refused, never answered as TRUE */
static bool gnt_poly(zxapp_t *a, const char *entry, const int64_t *x, const int64_t *y, uint32_t n,
                     bool half)
{
    zxapp_arg_t va[2] = {{"x", x, n}, {"y", y, n}};
    zxapp_num_t o[4];
    uint32_t got = 0;
    int32_t lp = -1;
    int64_t num = 0, den = 1;
    bool c_ok =
        half ? gnomon_polygon_area(x, y, n, &num, &den) : gnomon_polygon_area2(x, y, n, &num);
    int rc = zxapp_call_witnessed(a, &H, entry, va, 2, o, 4, &got, &lp);
    if (!c_ok) return rc != ZXAPP_OK && lp != ZXAPP_TRUE;
    if (rc != ZXAPP_OK || got != 1 || lp != ZXAPP_TRUE || o[0].num != num || o[0].den != den) {
        printf("    %s: rc %d lp %d n %u %lld/%lld, want %lld/%lld (%s)\n", entry, rc, (int) lp,
               got, (long long) o[0].num, (long long) o[0].den, (long long) num, (long long) den,
               a->err);
        return false;
    }
    return true;
}

APP_TEST(gnomon, 9, 107)
{
    static int64_t ax[WAVE_ROWS], ay[WAVE_ROWS], bx[WAVE_ROWS], by[WAVE_ROWS], cx[WAVE_ROWS],
        cy[WAVE_ROWS], want[WAVE_ROWS], px[GNOMON_VERTS_MAX], py[GNOMON_VERTS_MAX];

    /* polygon_area2 and polygon_area (GR1, GR2): a rectangle both ways
     * round, a triangle of half units, a line, then polygons at random of
     * 3 .. 64 vertices across the domain and one of the most vertices */
    {
        static const int64_t rx[4] = {0, 4, 4, 0}, ry[4] = {0, 0, 3, 3}, cwx[4] = {0, 0, 4, 4},
                             cwy[4] = {0, 3, 3, 0}, tx[3] = {0, 1, 0}, ty[3] = {0, 0, 1},
                             lx[3] = {0, 2, 4}, ly[3] = {0, 2, 4};
        int64_t t = 0, num = 0, den = 0;
        CHECK(gnomon_polygon_area2(rx, ry, 4, &t) && t == 24 &&
                  gnomon_polygon_area2(cwx, cwy, 4, &t) && t == -24 &&
                  gnomon_polygon_area(tx, ty, 3, &num, &den) && num == 1 && den == 2 &&
                  gnomon_polygon_area2(lx, ly, 3, &t) && t == 0,
              "Gnomon: the C shoelace: 24, -24 the other way round, 1/2, 0 on a line");
        CHECK(gnt_poly(app, "polygon_area2", rx, ry, 4, false) &&
                  gnt_poly(app, "polygon_area2", cwx, cwy, 4, false) &&
                  gnt_poly(app, "polygon_area2", lx, ly, 3, false),
              "Gnomon: polygon_area2 edges, KRON = Trebago = C");
        CHECK(gnt_poly(app, "polygon_area", rx, ry, 4, true) &&
                  gnt_poly(app, "polygon_area", tx, ty, 3, true) &&
                  gnt_poly(app, "polygon_area", cwx, cwy, 4, true),
              "Gnomon: polygon_area edges, reduced (KRON = Trebago = C)");
        for (uint32_t r = 0; r < 8; r++) {
            uint32_t n = 3u + (uint32_t) wrand(0, 61);
            int64_t most = r & 1u ? 9 : GNOMON_COORD_MAX;
            for (uint32_t i = 0; i < n; i++) {
                px[i] = wrand(-most, most);
                py[i] = wrand(-most, most);
            }
            CHECK(gnt_poly(app, "polygon_area2", px, py, n, false),
                  "Gnomon: polygon_area2 sweep %u of %u vertices, KRON = Trebago = C", r, n);
            CHECK(gnt_poly(app, "polygon_area", px, py, n, true),
                  "Gnomon: polygon_area sweep %u of %u vertices, KRON = Trebago = C", r, n);
        }
        for (uint32_t i = 0; i < GNOMON_VERTS_MAX; i++) {
            px[i] = i & 1u ? GNOMON_COORD_MAX : -GNOMON_COORD_MAX;
            py[i] = i & 2u ? -GNOMON_COORD_MAX : GNOMON_COORD_MAX;
        }
        CHECK(gnt_poly(app, "polygon_area2", px, py, GNOMON_VERTS_MAX, false),
              "Gnomon: polygon_area2 of 1024 vertices at the corners of the domain");
        CHECK(!gnomon_polygon_area2(px, py, GNOMON_VERTS_MAX + 1u, &t) &&
                  !gnomon_polygon_area2(rx, ry, 2, &t),
              "Gnomon: the C refuses more than 1024 vertices, or fewer than 3");
    }
    {
        /* past the domain, and too few vertices: Trebago refuses, KRON
         * answers: held */
        int64_t x3[3] = {0, GNOMON_COORD_MAX + 1, 0}, y3[3] = {0, 0, 1}, x2[2] = {0, 4},
                y2[2] = {0, 3}, yl[3] = {0, 0, -GNOMON_COORD_MAX - 1}, xl[3] = {0, 1, 0};
        zxapp_arg_t p1[2] = {{"x", x3, 3}, {"y", y3, 3}};
        CHECK(wave_lp(app, "polygon_area2", p1, 2) == ZXAPP_BOTH,
              "Gnomon: a coordinate past 1000000: Trebago refuses, KRON answers: BOTH");
        zxapp_arg_t p2[2] = {{"x", x2, 2}, {"y", y2, 2}};
        CHECK(wave_lp(app, "polygon_area2", p2, 2) == ZXAPP_BOTH,
              "Gnomon: two vertices are no polygon: BOTH");
        zxapp_arg_t p3[2] = {{"x", xl, 3}, {"y", yl, 3}};
        CHECK(wave_lp(app, "polygon_area", p3, 2) == ZXAPP_BOTH,
              "Gnomon: polygon_area below -1000000: BOTH");
        CHECK(gnt_poly(app, "polygon_area2", x3, y3, 3, false) &&
                  gnt_poly(app, "polygon_area", xl, yl, 3, true),
              "Gnomon: and the C refuses them too");
    }

    /* triangle_area2 (GR3): KRON = C++ = C over every coordinate */
    for (uint32_t r = 0; r < 2; r++) {
        int64_t most = r ? 3 : GNOMON_COORD_MAX;
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            ax[i] = wrand(-most, most);
            ay[i] = wrand(-most, most);
            bx[i] = wrand(-most, most);
            by[i] = wrand(-most, most);
            cx[i] = wrand(-most, most);
            cy[i] = wrand(-most, most);
            CHECK(gnomon_triangle_area2(ax[i], ay[i], bx[i], by[i], cx[i], cy[i], &want[i]),
                  "Gnomon: the C triangle rule row %u", i);
        }
        zxapp_arg_t ta[6] = {{"ax", ax, WAVE_ROWS}, {"ay", ay, WAVE_ROWS}, {"bx", bx, WAVE_ROWS},
                             {"by", by, WAVE_ROWS}, {"cx", cx, WAVE_ROWS}, {"cy", cy, WAVE_ROWS}};
        CHECK(wave_same(app, "triangle_area2", ta, 6, want, WAVE_ROWS, false),
              "Gnomon: triangle_area2 sweep %u, KRON = C++ = C", r);
    }
    {
        /* a left turn, a right turn, a line, the widest triangle, a point */
        static const int64_t eax[5] = {0, 0, 0, -1000000, 5}, eay[5] = {0, 0, 0, -1000000, 5},
                             ebx[5] = {1, 0, 2, 1000000, 5}, eby[5] = {0, 1, 2, -1000000, 5},
                             ecx[5] = {0, 1, 4, 1000000, 5}, ecy[5] = {1, 0, 4, 1000000, 5},
                             ew[5] = {1, -1, 0, INT64_C(4000000000000), 0};
        for (uint32_t i = 0; i < 5; i++) {
            int64_t t = 0;
            CHECK(gnomon_triangle_area2(eax[i], eay[i], ebx[i], eby[i], ecx[i], ecy[i], &t) &&
                      t == ew[i],
                  "Gnomon: the C triangle at edge %u: %lld", i, (long long) t);
        }
        zxapp_arg_t ta[6] = {{"ax", eax, 5}, {"ay", eay, 5}, {"bx", ebx, 5},
                             {"by", eby, 5}, {"cx", ecx, 5}, {"cy", ecy, 5}};
        CHECK(wave_same(app, "triangle_area2", ta, 6, ew, 5, false), "Gnomon: triangle edges");
        int64_t t = 0;
        CHECK(!gnomon_triangle_area2(1000001, 0, 0, 0, 0, 0, &t) &&
                  !gnomon_triangle_area2(0, -1000001, 0, 0, 0, 0, &t) &&
                  !gnomon_triangle_area2(0, 0, 1000001, 0, 0, 0, &t) &&
                  !gnomon_triangle_area2(0, 0, 0, 1000001, 0, 0, &t) &&
                  !gnomon_triangle_area2(0, 0, 0, 0, -1000001, 0, &t) &&
                  !gnomon_triangle_area2(0, 0, 0, 0, 0, 1000001, &t),
              "Gnomon: the C triangle refuses each coordinate past the domain");
        int64_t z[2] = {0, 0}, one[2] = {1, 0}, past[2] = {0, 1000001}, up[2] = {1, 1};
        zxapp_arg_t pa[6] = {{"ax", past, 2}, {"ay", z, 2}, {"bx", one, 2},
                             {"by", z, 2},    {"cx", z, 2}, {"cy", up, 2}};
        CHECK(wave_lp(app, "triangle_area2", pa, 6) == ZXAPP_BOTH,
              "Gnomon: a corner past 1000000: C++ traps, KRON answers: BOTH");
    }

    /* side_len_sq (GR4): KRON = C++ = C */
    for (uint32_t r = 0; r < 2; r++) {
        int64_t most = r ? 2 : GNOMON_COORD_MAX;
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            ax[i] = wrand(-most, most);
            ay[i] = wrand(-most, most);
            bx[i] = wrand(-most, most);
            by[i] = wrand(-most, most);
            CHECK(gnomon_side_len_sq(ax[i], ay[i], bx[i], by[i], &want[i]),
                  "Gnomon: the C side rule row %u", i);
        }
        zxapp_arg_t sa[4] = {{"ax", ax, WAVE_ROWS},
                             {"ay", ay, WAVE_ROWS},
                             {"bx", bx, WAVE_ROWS},
                             {"by", by, WAVE_ROWS}};
        CHECK(wave_same(app, "side_len_sq", sa, 4, want, WAVE_ROWS, false),
              "Gnomon: side_len_sq sweep %u, KRON = C++ = C", r);
    }
    {
        static const int64_t eax[4] = {0, 0, -1000000, 7}, eay[4] = {0, 0, -1000000, 7},
                             ebx[4] = {3, 0, 1000000, 7}, eby[4] = {4, 0, 1000000, 7},
                             ew[4] = {25, 0, INT64_C(8000000000000), 0};
        for (uint32_t i = 0; i < 4; i++) {
            int64_t s = 0;
            CHECK(gnomon_side_len_sq(eax[i], eay[i], ebx[i], eby[i], &s) && s == ew[i],
                  "Gnomon: the C side at edge %u", i);
        }
        zxapp_arg_t sa[4] = {{"ax", eax, 4}, {"ay", eay, 4}, {"bx", ebx, 4}, {"by", eby, 4}};
        CHECK(wave_same(app, "side_len_sq", sa, 4, ew, 4, false),
              "Gnomon: side_len_sq edges: 3-4-5, a point, the widest diagonal");
        int64_t s = 0;
        CHECK(!gnomon_side_len_sq(-1000001, 0, 0, 0, &s) &&
                  !gnomon_side_len_sq(0, 1000001, 0, 0, &s) &&
                  !gnomon_side_len_sq(0, 0, 1000001, 0, &s) &&
                  !gnomon_side_len_sq(0, 0, 0, -1000001, &s),
              "Gnomon: the C side refuses each coordinate past the domain");
        int64_t z[2] = {0, 0}, past[2] = {0, -1000001}, three[2] = {3, 0}, four[2] = {4, 0};
        zxapp_arg_t pa[4] = {{"ax", z, 2}, {"ay", past, 2}, {"bx", three, 2}, {"by", four, 2}};
        CHECK(wave_lp(app, "side_len_sq", pa, 4) == ZXAPP_BOTH,
              "Gnomon: a corner below -1000000: C++ traps, KRON answers: BOTH");
    }

    /* figure_record_ok (GR5): MYRA's digit widths, i9 i4 i1 i12 */
    {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            ax[i] = wrand(-1999999999, 1999999999);
            ay[i] = wrand(-19999, 19999);
            bx[i] = wrand(-19, 19);
            by[i] = wrand(-INT64_C(1999999999999), INT64_C(1999999999999));
            want[i] = T_(gnomon_figure_record_ok(ax[i], ay[i], bx[i], by[i]));
        }
        zxapp_arg_t ra[4] = {{"figure", ax, WAVE_ROWS},
                             {"vertices", ay, WAVE_ROWS},
                             {"kind", bx, WAVE_ROWS},
                             {"mtime", by, WAVE_ROWS}};
        CHECK(wave_same(app, "figure_record_ok", ra, 4, want, WAVE_ROWS, true),
              "Gnomon: figure_record_ok sweep (MYRA = C)");
        int64_t ea[6] = {0, 999999999, 1000000000, -999999999, 0, 0},
                eb[6] = {0, 9999, 1, -9999, 10000, 1}, ec[6] = {0, 9, 1, -9, 1, 10},
                ed[6] = {0, INT64_C(999999999999), 1, -1, 1, 1},
                ew[6] = {ZXAPP_TRUE, ZXAPP_TRUE, ZXAPP_FALSE, ZXAPP_TRUE, ZXAPP_FALSE, ZXAPP_FALSE};
        zxapp_arg_t xa[4] = {
            {"figure", ea, 6}, {"vertices", eb, 6}, {"kind", ec, 6}, {"mtime", ed, 6}};
        CHECK(wave_same(app, "figure_record_ok", xa, 4, ew, 6, true),
              "Gnomon: figure_record_ok at 0, the widest values either side, and one past each");
    }
}
