/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Relief's model against zxvd's real dispatcher (LINK): a mesh built,
 * measured, turned and seen in perspective, split into depth layers, saved
 * to the Hoard and read back. */
#include "relief_model.h"

APPS_TEST(relief)
{
    static relief_t R, R2;
    static int64_t sx[RELIEF_VERTS_MAX], sy[RELIEF_VERTS_MAX], off[RELIEF_VERTS_MAX];
    static uint8_t lay[RELIEF_VERTS_MAX];
    int64_t num = 0, den = 0, c = 0, s = 0, out = 0;
    uint32_t idx = 0;

    /* the rules, as the blocks run them */
    CHECK(relief_screen_x(1000, 0, 4096, 0, 100, 1000, &out) && out == 1000 &&
              relief_screen_x(-1, 0, 4096, 0, 50, 1000, &out) && out == -1 &&
              relief_screen_y(0, 7, -100000, 4096, 0, 100, 100001, &out) && out == 700007 &&
              !relief_screen_x(0, -100000, 4096, 0, 100, 1, &out),
          "relief: a vertex on the screen, floored; behind the viewer refused");
    CHECK(relief_depth_offset(40, 5, 10, &out) && out == 20 &&
              relief_depth_offset(-40, 5, 10, &out) && out == -20 &&
              relief_depth_offset(40, 10, 10, &out) && out == 0 &&
              !relief_depth_offset(40, 0, 0, &out),
          "relief: the depth effect's offsets");
    CHECK(relief_mesh_limits_ok(4, 4) && relief_mesh_limits_ok(512, 1020) &&
              !relief_mesh_limits_ok(3, 4) && !relief_mesh_limits_ok(4, 1021),
          "relief: mesh limits (Eve's ranges)");
    CHECK(relief_mesh_record_ok(999999999, 999, 9999, INT64_C(999999999999)) &&
              !relief_mesh_record_ok(1000000000, 0, 0, 0) && !relief_mesh_record_ok(0, 0, 10000, 0),
          "relief: the record's digit widths (MYRA's)");

    /* RM3: the turn table, whole degrees */
    relief_turn(0, &c, &s);
    CHECK(c == 4096 && s == 0, "relief: no turn");
    relief_turn(90, &c, &s);
    CHECK(c == 0 && s == 4096, "relief: a quarter turn");
    relief_turn(-90, &c, &s);
    CHECK(c == 0 && s == -4096, "relief: a quarter turn back");
    relief_turn(570, &c, &s);
    CHECK(c == -3547 && s == -2048, "relief: 570 degrees is 210: %lld %lld", (long long) c,
          (long long) s);

    /* RM2: a mesh, its limits refused, never cut */
    relief_init(&R, &LINK);
    CHECK(R.nv == 0 && R.scale == 100 && R.focal == 100000, "relief: an empty mesh");
    CHECK(relief_add_vertex(&R, 100001, 0, 0, &idx) == ZXM_E_ARG && R.nv == 0,
          "relief: a coordinate past a metre is refused");
    CHECK(relief_add_face(&R, 0, 1, 2) == ZXM_E_ARG, "relief: a face needs its vertices");
    CHECK(relief_volume(&R, &num, &den) == ZXM_E_STATE, "relief: no mesh, no volume");
    CHECK(relief_add_box(&R, 0, 0, 0, 0, 10, 10) == ZXM_E_ARG && R.nv == 0,
          "relief: a flat box is refused");
    CHECK(relief_add_box(&R, -1000, -2000, -3000, 1000, 2000, 3000) == ZXM_OK && R.nv == 8 &&
              R.nf == 12 && relief_closed(&R),
          "relief: a closed box");
    CHECK(relief_volume(&R, &num, &den) == ZXM_OK && num == INT64_C(48000000000) && den == 1,
          "relief: 2000 by 4000 by 6000 is %lld/%lld", (long long) num, (long long) den);
    CHECK(relief_add_face(&R, 0, 0, 1) == ZXM_E_ARG && relief_add_face(&R, 0, 1, 8) == ZXM_E_ARG &&
              R.nf == 12,
          "relief: a face with a repeated or missing corner is refused");
    CHECK(relief_add_vertex(&R, 0, 0, 5000, &idx) == ZXM_OK && idx == 8 &&
              relief_add_face(&R, 0, 1, 8) == ZXM_OK && !relief_closed(&R) &&
              relief_volume(&R, &num, &den) == ZXM_E_STATE,
          "relief: a loose face opens the mesh: no volume");
    relief_clear(&R);
    CHECK(R.nv == 0 && R.nf == 0, "relief: cleared");

    /* a tetrahedron: volume 1/6 of its box, an exact fraction */
    CHECK(relief_add_vertex(&R, 0, 0, 0, NULL) == ZXM_OK &&
              relief_add_vertex(&R, 1000, 0, 0, NULL) == ZXM_OK &&
              relief_add_vertex(&R, 0, 1000, 0, NULL) == ZXM_OK &&
              relief_add_vertex(&R, 0, 0, 1000, NULL) == ZXM_OK &&
              relief_add_face(&R, 1, 2, 3) == ZXM_OK && relief_add_face(&R, 0, 2, 1) == ZXM_OK &&
              relief_add_face(&R, 0, 3, 2) == ZXM_OK && relief_add_face(&R, 0, 1, 3) == ZXM_OK,
          "relief: a tetrahedron");
    CHECK(relief_volume(&R, &num, &den) == ZXM_OK && num == 500000000 && den == 3,
          "relief: its volume is 500000000/3: %lld/%lld", (long long) num, (long long) den);
    relief_clear(&R);
    CHECK(relief_add_box(&R, -1000, -1000, -1000, 1000, 1000, 1000) == ZXM_OK, "relief: a cube");

    /* RM3: seen straight on, then turned a quarter */
    CHECK(relief_set_view(&R, 0, 0, 1000) == ZXM_E_ARG &&
              relief_set_view(&R, 0, 100, 0) == ZXM_E_ARG,
          "relief: no scale or no distance is refused");
    CHECK(relief_set_view(&R, 0, 100, 10000) == ZXM_OK, "relief: a view 10 cm away");
    CHECK(relief_project(&R, sx, sy, 4) == ZXM_E_TOOBIG, "relief: no room for the points");
    CHECK(relief_project(&R, sx, sy, RELIEF_VERTS_MAX) == ZXM_OK && sx[0] == -1112 &&
              sy[0] == -1112 && sx[4] == -910 && sy[6] == 909,
          "relief: the near face bigger than the far: %lld %lld %lld %lld", (long long) sx[0],
          (long long) sy[0], (long long) sx[4], (long long) sy[6]);
    uint32_t same = 0;
    for (uint32_t i = 0; i < R.nv; i++) {
        int64_t a = 0, b = 0;
        same += relief_screen_x(R.v[i].x, R.v[i].z, 4096, 0, 100, 10000, &a) && a == sx[i] &&
                relief_screen_y(R.v[i].x, R.v[i].y, R.v[i].z, 4096, 0, 100, 10000, &b) &&
                b == sy[i];
    }
    CHECK(same == 8, "relief: every point is the rule's (%u of 8)", same);
    CHECK(relief_set_view(&R, 450, 200, 10000) == ZXM_OK && R.yaw == 90 &&
              relief_project(&R, sx, sy, RELIEF_VERTS_MAX) == ZXM_OK && sx[0] == -1819 &&
              sx[1] == -2223 && sx[4] == 1818,
          "relief: a quarter turn at 200 percent: %lld %lld %lld", (long long) sx[0],
          (long long) sx[1], (long long) sx[4]);
    CHECK(relief_set_view(&R, 0, 100, 1) == ZXM_OK &&
              relief_project(&R, sx, sy, RELIEF_VERTS_MAX) == ZXM_E_ARG,
          "relief: a viewer inside the cube draws nothing");

    /* RM4: the depth effect, two layers then four */
    CHECK(relief_set_view(&R, 0, 100, 10000) == ZXM_OK &&
              relief_depth_layers(&R, 1, 30, lay, off, RELIEF_VERTS_MAX) == ZXM_E_ARG &&
              relief_depth_layers(&R, 2, 100001, lay, off, RELIEF_VERTS_MAX) == ZXM_E_ARG,
          "relief: one layer, or a shift past its limit, is refused");
    CHECK(relief_depth_layers(&R, 2, 30, lay, off, RELIEF_VERTS_MAX) == ZXM_OK && lay[0] == 0 &&
              off[0] == 30 && lay[4] == 1 && off[4] == 0,
          "relief: the near face moves 30, the far face stays");
    relief_clear(&R);
    for (int64_t k = 0; k < 4; k++)
        CHECK(relief_add_vertex(&R, 0, 0, k * 1000, NULL) == ZXM_OK, "relief: a vertex %lld deep",
              (long long) k * 1000);
    CHECK(relief_depth_layers(&R, 4, -90, lay, off, RELIEF_VERTS_MAX) == ZXM_OK && lay[0] == 0 &&
              lay[1] == 1 && lay[2] == 2 && lay[3] == 3 && off[0] == -90 && off[1] == -60 &&
              off[2] == -30 && off[3] == 0,
          "relief: four layers at -90: %lld %lld %lld %lld", (long long) off[0], (long long) off[1],
          (long long) off[2], (long long) off[3]);

    /* RM6 and RM1: a mesh in the Hoard and back, vertex for vertex */
    relief_clear(&R);
    CHECK(relief_save_as(&R, "", "empty.relief") == ZXM_E_STATE,
          "relief: an empty mesh is not saved");
    CHECK(relief_add_box(&R, 0, 0, 0, 2500, 1500, 500) == ZXM_OK &&
              relief_add_box(&R, 100, 100, 600, 200, 200, 700) == ZXM_OK && R.nv == 16 &&
              R.nf == 24,
          "relief: two boxes");
    CHECK(relief_record(&R, 7, INT64_C(1700000000)) && !relief_record(&R, 1000000000, 1),
          "relief: the mesh's record");
    CHECK(relief_save(&R) == ZXM_E_STATE && relief_save_as(&R, "", "") == ZXM_E_ARG,
          "relief: no path yet");
    CHECK(relief_save_as(&R, "", "blocks.relief") == ZXM_OK && !R.dirty &&
              !strcmp(R.path, "blocks.relief"),
          "relief: saved: %s", LINK.err);
    relief_init(&R2, &LINK);
    CHECK(relief_open(&R2, "", "blocks.relief") == ZXM_OK && R2.nv == 16 && R2.nf == 24 &&
              !memcmp(R2.v, R.v, 16 * sizeof R.v[0]) && !memcmp(R2.f, R.f, 24 * sizeof R.f[0]) &&
              !R2.dirty,
          "relief: read back: %s", LINK.err);
    CHECK(relief_volume(&R2, &num, &den) == ZXM_OK &&
              num == INT64_C(2500) * 1500 * 500 + 100 * 100 * 100 && den == 1,
          "relief: the two boxes' volume read back: %lld", (long long) num);
    CHECK(relief_add_box(&R2, -50, -50, -50, 50, 50, 50) == ZXM_OK && R2.dirty &&
              relief_save(&R2) == ZXM_OK && relief_open(&R, "", "blocks.relief") == ZXM_OK &&
              R.nv == 24 && !R.dirty,
          "relief: changed, saved in place and read again: %s", LINK.err);
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", "not-a-mesh.relief");
    zxm_p_str(&LINK, "text", "relief 1 4 4\nv 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\nf 0 1 1\n");
    CHECK(zxm_call(&LINK) == ZXM_OK, "relief: a broken mesh written: %s", LINK.err);
    CHECK(relief_open(&R, "", "not-a-mesh.relief") == ZXM_E_ARG && R.nv == 24 &&
              !strcmp(R.path, "blocks.relief"),
          "relief: a face with a repeated corner leaves the mesh as it was");
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", "too-big.relief");
    zxm_p_str(&LINK, "text", "relief 1 513 4\n");
    CHECK(zxm_call(&LINK) == ZXM_OK, "relief: an oversized header written: %s", LINK.err);
    CHECK(relief_open(&R, "", "too-big.relief") == ZXM_E_ARG && R.nv == 24,
          "relief: 513 vertices is past the limits: refused");
    CHECK(relief_open(&R, "", "no-such.relief") == ZXM_E_REMOTE && R.nv == 24,
          "relief: a missing mesh is zxvd's error: %s", LINK.err);
}
