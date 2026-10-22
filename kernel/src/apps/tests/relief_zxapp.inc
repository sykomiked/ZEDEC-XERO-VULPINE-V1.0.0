/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Relief's languages against the C its model runs (relief_rules.h).
 * Slots: @num.relief_vertex_screen_x, @num.relief_vertex_screen_y,
 * @num.relief_depth_layer_offset, @num.relief_mesh_volume,
 * @logic.relief_mesh_limits_ok, @logic.relief_mesh_record_ok. */
#include "relief_rules.h"

/* mesh_volume over corners (three a face) and orient (one a face):
 * Trebago's one answer is the C's reduced fraction (refused: the call
 * fails, as the C refuses) */
static bool rlt_volume(zxapp_t *a, const int64_t *x, const int64_t *y, const int64_t *z,
                       uint32_t corners, const int64_t *orient, uint32_t faces, bool refused)
{
    zxapp_arg_t va[4] = {
        {"x", x, corners}, {"y", y, corners}, {"z", z, corners}, {"orient", orient, faces}};
    zxapp_num_t o[WAVE_ROWS];
    uint32_t n = 0;
    int32_t lp = -1;
    int64_t num = 0, den = 0;
    bool c_ok = relief_mesh_volume(x, y, z, corners, orient, faces, &num, &den);
    int rc = zxapp_call_witnessed(a, &H, "mesh_volume", va, 4, o, WAVE_ROWS, &n, &lp);
    if (refused) return !c_ok && rc != ZXAPP_OK;
    if (!c_ok || rc != ZXAPP_OK || n != 1 || lp != ZXAPP_NEITHER || o[0].num != num ||
        o[0].den != den) {
        printf("    mesh_volume: rc %d lp %d n %u %lld/%lld, want %lld/%lld (%s)\n", rc, (int) lp,
               n, (long long) o[0].num, (long long) o[0].den, (long long) num, (long long) den,
               a->err);
        return false;
    }
    return true;
}

APP_TEST(relief, 8, 60)
{
    static int64_t x[WAVE_ROWS], y[WAVE_ROWS], z[WAVE_ROWS], c[WAVE_ROWS], s[WAVE_ROWS],
        k[WAVE_ROWS], f[WAVE_ROWS], wx[WAVE_ROWS], wy[WAVE_ROWS];

    /* vertex_screen_x and vertex_screen_y (RR1, RR2): sweeps from far views
     * to views close enough that a vertex nearly touches the viewer */
    static const int64_t focal_most[4] = {RELIEF_FOCAL_MAX, 200000, 30000, 1000};
    for (uint32_t r = 0; r < 4; r++) {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            int64_t depth = 0;
            do {
                x[i] = wrand(-RELIEF_COORD_MAX, RELIEF_COORD_MAX);
                y[i] = wrand(-RELIEF_COORD_MAX, RELIEF_COORD_MAX);
                z[i] = wrand(-RELIEF_COORD_MAX, RELIEF_COORD_MAX);
                c[i] = wrand(-RELIEF_ONE, RELIEF_ONE);
                s[i] = wrand(-RELIEF_ONE, RELIEF_ONE);
                k[i] = wrand(1, RELIEF_SCALE_MAX);
                f[i] = wrand(1, focal_most[r]);
            } while (!relief_view_ok(x[i], z[i], c[i], s[i], k[i], f[i], &depth));
            CHECK(relief_screen_x(x[i], z[i], c[i], s[i], k[i], f[i], &wx[i]) &&
                      relief_screen_y(x[i], y[i], z[i], c[i], s[i], k[i], f[i], &wy[i]),
                  "Relief: the C rule answers sweep %u row %u", r, i);
        }
        zxapp_arg_t xa[6] = {{"x", x, WAVE_ROWS},     {"z", z, WAVE_ROWS},
                             {"cos", c, WAVE_ROWS},   {"sin", s, WAVE_ROWS},
                             {"scale", k, WAVE_ROWS}, {"focal", f, WAVE_ROWS}};
        zxapp_arg_t ya[7] = {{"x", x, WAVE_ROWS},    {"y", y, WAVE_ROWS},   {"z", z, WAVE_ROWS},
                             {"cos", c, WAVE_ROWS},  {"sin", s, WAVE_ROWS}, {"scale", k, WAVE_ROWS},
                             {"focal", f, WAVE_ROWS}};
        CHECK(wave_same(app, "vertex_screen_x", xa, 6, wx, WAVE_ROWS, false),
              "Relief: vertex_screen_x sweep %u, C++ = KRON = C", r);
        CHECK(wave_same(app, "vertex_screen_y", ya, 7, wy, WAVE_ROWS, false),
              "Relief: vertex_screen_y sweep %u, C++ = KRON = C", r);
    }
    {
        /* the edges: 0, a quarter turn, the floor of a negative half, every
         * value at its most, a vertex one unit from the viewer, the least
         * scale and focal, and a half turn */
        static const int64_t ex[8] = {0, 1000, -1, 100000, 0, -100000, 1, 1000};
        static const int64_t ey[8] = {0, 500, -1, 100000, 7, -100000, -1, -3};
        static const int64_t ez[8] = {0, 1000, 0, 100000, -100000, -100000, 0, 0};
        static const int64_t ec[8] = {4096, 0, 4096, 4096, 4096, -4096, 4096, -4096};
        static const int64_t es[8] = {0, 4096, 0, 4096, 0, -4096, 0, 0};
        static const int64_t ek[8] = {100, 100, 50, 1000, 100, 1000, 1, 100};
        static const int64_t ef[8] = {1000, 2000, 1000, 1000000, 100001, 1000000, 1, 1000};
        static const int64_t ewx[8] = {0, 2000, -1, 2000000, 0, 2000000, 0, -1000};
        static const int64_t ewy[8] = {0, 1000, -1, 1000000, 700007, -1000000, -1, -3};
        for (uint32_t i = 0; i < 8; i++) {
            int64_t a = 0, b = 0;
            CHECK(relief_screen_x(ex[i], ez[i], ec[i], es[i], ek[i], ef[i], &a) && a == ewx[i] &&
                      relief_screen_y(ex[i], ey[i], ez[i], ec[i], es[i], ek[i], ef[i], &b) &&
                      b == ewy[i],
                  "Relief: the C rule at view edge %u: %lld, %lld", i, (long long) a,
                  (long long) b);
        }
        zxapp_arg_t xa[6] = {{"x", ex, 8},   {"z", ez, 8},     {"cos", ec, 8},
                             {"sin", es, 8}, {"scale", ek, 8}, {"focal", ef, 8}};
        zxapp_arg_t ya[7] = {{"x", ex, 8},   {"y", ey, 8},     {"z", ez, 8},    {"cos", ec, 8},
                             {"sin", es, 8}, {"scale", ek, 8}, {"focal", ef, 8}};
        CHECK(wave_same(app, "vertex_screen_x", xa, 6, ewx, 8, false), "Relief: screen x edges");
        CHECK(wave_same(app, "vertex_screen_y", ya, 7, ewy, 8, false), "Relief: screen y edges");
        int64_t out = 0;
        CHECK(!relief_screen_x(100001, 0, 4096, 0, 100, 1000, &out) &&
                  !relief_screen_x(0, -100001, 4096, 0, 100, 1000, &out) &&
                  !relief_screen_x(0, 0, 4097, 0, 100, 1000, &out) &&
                  !relief_screen_x(0, 0, 4096, -4097, 100, 1000, &out) &&
                  !relief_screen_x(0, 0, 4096, 0, 0, 1000, &out) &&
                  !relief_screen_x(0, 0, 4096, 0, 1001, 1000, &out) &&
                  !relief_screen_x(0, 0, 4096, 0, 100, 0, &out) &&
                  !relief_screen_x(0, 0, 4096, 0, 100, 1000001, &out) &&
                  !relief_screen_x(0, -1, 4096, 0, 100, 1, &out) &&
                  !relief_screen_y(0, 100001, 0, 4096, 0, 100, 1000, &out) &&
                  !relief_screen_y(0, 0, -1, 4096, 0, 100, 1, &out),
              "Relief: the C rule refuses outside its domain and at the viewer");
    }
    {
        /* past the domain, and at or behind the viewer: the C++ traps,
         * KRON answers: held */
        int64_t z2[2] = {0, 0}, one[2] = {4096, 4096}, ten[2] = {10, 10}, past[2] = {1000, 100001},
                hundred[2] = {100, 100}, wide[2] = {100, 1001}, kf[2] = {1000, 1000},
                near[2] = {1, 1}, back[2] = {0, -100000}, edge[2] = {0, -1};
        zxapp_arg_t p1[6] = {{"x", past, 2}, {"z", z2, 2},          {"cos", one, 2},
                             {"sin", z2, 2}, {"scale", hundred, 2}, {"focal", kf, 2}};
        CHECK(wave_lp(app, "vertex_screen_x", p1, 6) == ZXAPP_BOTH,
              "Relief: x past 100000: C++ refuses, KRON answers: BOTH");
        zxapp_arg_t p2[6] = {{"x", z2, 2},   {"z", back, 2},        {"cos", one, 2},
                             {"sin", z2, 2}, {"scale", hundred, 2}, {"focal", near, 2}};
        CHECK(wave_lp(app, "vertex_screen_x", p2, 6) == ZXAPP_BOTH,
              "Relief: a vertex behind the viewer: BOTH");
        zxapp_arg_t p3[7] = {{"x", z2, 2},   {"y", ten, 2},      {"z", z2, 2},    {"cos", one, 2},
                             {"sin", z2, 2}, {"scale", wide, 2}, {"focal", kf, 2}};
        CHECK(wave_lp(app, "vertex_screen_y", p3, 7) == ZXAPP_BOTH,
              "Relief: a scale past 1000 percent: BOTH");
        zxapp_arg_t p4[7] = {{"x", z2, 2},      {"y", ten, 2},  {"z", back, 2},
                             {"cos", one, 2},   {"sin", z2, 2}, {"scale", hundred, 2},
                             {"focal", near, 2}};
        CHECK(wave_lp(app, "vertex_screen_y", p4, 7) == ZXAPP_BOTH,
              "Relief: screen y of a vertex behind the viewer: BOTH");
        zxapp_arg_t p5[7] = {{"x", z2, 2},      {"y", ten, 2},  {"z", edge, 2},
                             {"cos", one, 2},   {"sin", z2, 2}, {"scale", hundred, 2},
                             {"focal", near, 2}};
        CHECK(wave_lp(app, "vertex_screen_y", p5, 7) != ZXAPP_TRUE,
              "Relief: a vertex on the viewer is refused by both, never answered");
    }

    /* depth_layer_offset (RR3): KRON alone against the C */
    for (uint32_t r = 0; r < 2; r++) {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            x[i] = wrand(-RELIEF_SHIFT_MAX, RELIEF_SHIFT_MAX);
            f[i] = wrand(1, r ? 16 : RELIEF_FAR_MAX);
            z[i] = wrand(0, f[i]);
            CHECK(relief_depth_offset(x[i], z[i], f[i], &wx[i]), "Relief: depth rule row %u", i);
        }
        zxapp_arg_t da[3] = {
            {"shift", x, WAVE_ROWS}, {"depth", z, WAVE_ROWS}, {"far", f, WAVE_ROWS}};
        CHECK(wave_same(app, "depth_layer_offset", da, 3, wx, WAVE_ROWS, false),
              "Relief: depth_layer_offset sweep %u, KRON = C", r);
    }
    {
        static const int64_t es[6] = {40, 40, -40, 7, 100000, -100000};
        static const int64_t ed[6] = {0, 10, 5, 1, 0, 1000000};
        static const int64_t ef[6] = {10, 10, 10, 3, 1000000, 1000000};
        static const int64_t ew[6] = {40, 0, -20, 4, 100000, 0};
        for (uint32_t i = 0; i < 6; i++) {
            int64_t o = 0;
            CHECK(relief_depth_offset(es[i], ed[i], ef[i], &o) && o == ew[i],
                  "Relief: the C depth rule at edge %u", i);
        }
        zxapp_arg_t da[3] = {{"shift", es, 6}, {"depth", ed, 6}, {"far", ef, 6}};
        CHECK(wave_same(app, "depth_layer_offset", da, 3, ew, 6, false),
              "Relief: depth_layer_offset edges: nearest moves the shift, farthest still");
        int64_t o = 0;
        CHECK(!relief_depth_offset(100001, 0, 1, &o) && !relief_depth_offset(-100001, 0, 1, &o) &&
                  !relief_depth_offset(1, 0, 0, &o) && !relief_depth_offset(1, 0, 1000001, &o) &&
                  !relief_depth_offset(1, -1, 5, &o) && !relief_depth_offset(1, 6, 5, &o),
              "Relief: the depth rule refuses outside its domain");
        int64_t sh[2] = {5, 5}, dz[2] = {0, 0}, nofar[2] = {0, 0};
        zxapp_num_t out[2];
        uint32_t n = 0;
        zxapp_arg_t za[3] = {{"shift", sh, 2}, {"depth", dz, 2}, {"far", nofar, 2}};
        CHECK(zxapp_call(app, &H, "depth_layer_offset", za, 3, out, 2, &n) != ZXAPP_OK,
              "Relief: a far of 0 is refused");
    }

    /* mesh_volume (RR4): Trebago alone against the C, exact */
    {
        enum { FACES = WAVE_ROWS, CORNERS = 3 * WAVE_ROWS };
        static int64_t vx[CORNERS], vy[CORNERS], vz[CORNERS], vo[FACES];
        /* a box 3 by 5 by 7 hundredths away from the origin: 105 exactly */
        static const int64_t bx[8][3] = {{10, 20, 30}, {13, 20, 30}, {13, 25, 30}, {10, 25, 30},
                                         {10, 20, 37}, {13, 20, 37}, {13, 25, 37}, {10, 25, 37}};
        static const uint8_t bf[12][3] = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7},
                                          {0, 1, 5}, {0, 5, 4}, {3, 7, 6}, {3, 6, 2},
                                          {0, 4, 7}, {0, 7, 3}, {1, 2, 6}, {1, 6, 5}};
        for (uint32_t j = 0; j < 12; j++) {
            for (uint32_t q = 0; q < 3; q++) {
                vx[3 * j + q] = bx[bf[j][q]][0];
                vy[3 * j + q] = bx[bf[j][q]][1];
                vz[3 * j + q] = bx[bf[j][q]][2];
            }
            vo[j] = 1;
        }
        int64_t num = 0, den = 0;
        CHECK(relief_mesh_volume(vx, vy, vz, 36, vo, 12, &num, &den) && num == 105 && den == 1,
              "Relief: the C volume of a 3 by 5 by 7 box is 105: %lld/%lld", (long long) num,
              (long long) den);
        CHECK(rlt_volume(app, vx, vy, vz, 36, vo, 12, false),
              "Relief: mesh_volume of a box, Trebago = C");
        for (uint32_t j = 0; j < 12; j++) vo[j] = -1;
        CHECK(relief_mesh_volume(vx, vy, vz, 36, vo, 12, &num, &den) && num == -105 &&
                  rlt_volume(app, vx, vy, vz, 36, vo, 12, false),
              "Relief: the box wound inside out is -105");
        vo[0] = 0;
        CHECK(rlt_volume(app, vx, vy, vz, 36, vo, 12, false), "Relief: a face left out");
        /* faces at random, every coordinate across the domain: any sum */
        for (uint32_t r = 0; r < 3; r++) {
            for (uint32_t i = 0; i < CORNERS; i++) {
                int64_t most = r == 2 ? 9 : RELIEF_COORD_MAX;
                vx[i] = wrand(-most, most);
                vy[i] = wrand(-most, most);
                vz[i] = wrand(-most, most);
            }
            for (uint32_t j = 0; j < FACES; j++) vo[j] = wrand(-1, 1);
            CHECK(rlt_volume(app, vx, vy, vz, CORNERS, vo, FACES, false),
                  "Relief: mesh_volume sweep %u, Trebago = C", r);
        }
        /* every coordinate at its most: the largest face there is */
        static const int64_t ex[3] = {100000, -100000, -100000}, ey[3] = {-100000, 100000, -100000},
                             ez[3] = {-100000, -100000, 100000}, one = 1;
        CHECK(rlt_volume(app, ex, ey, ez, 3, &one, 1, false),
              "Relief: mesh_volume at the corners of the domain");
        for (uint32_t i = 0; i < 6; i++) vx[i] = vy[i] = vz[i] = 0;
        vo[0] = 1;
        vo[1] = 1;
        CHECK(rlt_volume(app, vx, vy, vz, 3, vo, 1, false) &&
                  relief_mesh_volume(vx, vy, vz, 3, vo, 1, &num, &den) && num == 0 && den == 1,
              "Relief: a face through the origin adds 0");
        vy[4] = 100001;
        CHECK(rlt_volume(app, vx, vy, vz, 6, vo, 2, true),
              "Relief: a coordinate past 100000: both refuse");
        vy[4] = -100001;
        CHECK(rlt_volume(app, vx, vy, vz, 6, vo, 2, true),
              "Relief: a coordinate below -100000: both refuse");
        vy[4] = 0;
        vo[1] = 2;
        CHECK(rlt_volume(app, vx, vy, vz, 6, vo, 2, true), "Relief: an orient of 2: both refuse");
        vo[1] = 1;
        CHECK(rlt_volume(app, vx, vy, vz, 6, vo, 2, false) &&
                  rlt_volume(app, vx, vy, vz, 5, vo, 2, true) &&
                  rlt_volume(app, vx, vy, vz, 6, vo, 1, true),
              "Relief: corners not three to a face: both refuse");
        CHECK(!relief_mesh_volume(vx, vy, vz, 3 * (uint32_t) RELIEF_FACES_MAX + 3u, vo,
                                  (uint32_t) RELIEF_FACES_MAX + 1u, &num, &den),
              "Relief: the C refuses more faces than a mesh holds");
    }

    /* mesh_limits_ok (RR5): Eve's ranges, at and past every edge, and a sweep */
    {
        static const int64_t ev[8] = {4, 512, 3, 513, 4, 4, 0, -4};
        static const int64_t ef[8] = {4, 1020, 4, 4, 3, 1021, 0, 4};
        for (uint32_t i = 0; i < 8; i++) {
            x[i] = ev[i];
            y[i] = ef[i];
            wx[i] = T_(relief_mesh_limits_ok(ev[i], ef[i]));
        }
        CHECK(wx[0] == ZXAPP_TRUE && wx[1] == ZXAPP_TRUE && wx[2] == ZXAPP_FALSE &&
                  wx[5] == ZXAPP_FALSE,
              "Relief: mesh_limits_ok, the C's own edges");
        zxapp_arg_t la[2] = {{"vertices", x, 8}, {"faces", y, 8}};
        CHECK(wave_same(app, "mesh_limits_ok", la, 2, wx, 8, true),
              "Relief: mesh_limits_ok edges (Eve = C)");
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            x[i] = wrand(-5, 530);
            y[i] = wrand(-5, 1040);
            wx[i] = T_(relief_mesh_limits_ok(x[i], y[i]));
        }
        zxapp_arg_t sa[2] = {{"vertices", x, WAVE_ROWS}, {"faces", y, WAVE_ROWS}};
        CHECK(wave_same(app, "mesh_limits_ok", sa, 2, wx, WAVE_ROWS, true),
              "Relief: mesh_limits_ok sweep (Eve = C)");
    }

    /* mesh_record_ok (RR6): MYRA's digit widths, i9 i3 i4 i12 */
    {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            x[i] = wrand(-1999999999, 1999999999);
            y[i] = wrand(-1999, 1999);
            z[i] = wrand(-19999, 19999);
            c[i] = wrand(-INT64_C(1999999999999), INT64_C(1999999999999));
            wx[i] = T_(relief_mesh_record_ok(x[i], y[i], z[i], c[i]));
        }
        zxapp_arg_t ra[4] = {{"mesh", x, WAVE_ROWS},
                             {"vertices", y, WAVE_ROWS},
                             {"faces", z, WAVE_ROWS},
                             {"mtime", c, WAVE_ROWS}};
        CHECK(wave_same(app, "mesh_record_ok", ra, 4, wx, WAVE_ROWS, true),
              "Relief: mesh_record_ok sweep (MYRA = C)");
        int64_t ea[6] = {0, 999999999, 1000000000, -999999999, 0, 0},
                eb[6] = {0, 999, 1, -999, 1000, 1}, ec[6] = {0, 9999, 1, -9999, 1, 10000},
                ed[6] = {0, INT64_C(999999999999), 1, -1, 1, 1},
                ew[6] = {ZXAPP_TRUE, ZXAPP_TRUE, ZXAPP_FALSE, ZXAPP_TRUE, ZXAPP_FALSE, ZXAPP_FALSE};
        zxapp_arg_t xa[4] = {
            {"mesh", ea, 6}, {"vertices", eb, 6}, {"faces", ec, 6}, {"mtime", ed, 6}};
        CHECK(wave_same(app, "mesh_record_ok", xa, 4, ew, 6, true),
              "Relief: mesh_record_ok at 0, the widest values either side, and one past each");
    }
}
