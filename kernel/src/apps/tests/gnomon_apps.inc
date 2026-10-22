/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Gnomon's model against zxvd's real dispatcher (LINK): a polygon drawn
 * and measured, a turtle path walked and closed, slopes reduced, a figure
 * saved to the Hoard and read back. */
#include "gnomon_model.h"

APPS_TEST(gnomon)
{
    static gnomon_t G, G2;
    int64_t t = 0, num = 0, den = 0;
    bool convex = false;

    /* the rules, as the blocks run them */
    {
        static const int64_t x[4] = {0, 4, 4, 0}, y[4] = {0, 0, 3, 3};
        CHECK(gnomon_polygon_area2(x, y, 4, &t) && t == 24 &&
                  gnomon_polygon_area(x, y, 4, &num, &den) && num == 12 && den == 1 &&
                  !gnomon_polygon_area2(x, y, 2, &t),
              "gnomon: the shoelace rule");
        CHECK(gnomon_triangle_area2(0, 0, 1, 0, 0, 1, &t) && t == 1 &&
                  gnomon_side_len_sq(0, 0, 3, 4, &t) && t == 25 &&
                  !gnomon_side_len_sq(0, 0, 1000001, 0, &t),
              "gnomon: the triangle and side rules");
        CHECK(gnomon_figure_record_ok(999999999, 1024, 1, INT64_C(999999999999)) &&
                  !gnomon_figure_record_ok(1000000000, 3, 0, 0) &&
                  !gnomon_figure_record_ok(1, 10000, 0, 0),
              "gnomon: the record's digit widths (MYRA's)");
    }

    /* GM2 and GM4: a polygon point by point */
    gnomon_init(&G, &LINK);
    CHECK(G.n == 0 && G.kind == GNOMON_KIND_POLY, "gnomon: an empty polygon");
    CHECK(gnomon_area2(&G, &t) == ZXM_E_STATE && gnomon_convex(&G, &convex) == ZXM_E_STATE,
          "gnomon: no figure, no measure");
    CHECK(gnomon_add_point(&G, 1000001, 0) == ZXM_E_ARG && G.n == 0,
          "gnomon: a coordinate past 1000000 is refused, nothing added");
    CHECK(gnomon_add_point(&G, 0, 0) == ZXM_OK && gnomon_add_point(&G, 6, 0) == ZXM_OK &&
              gnomon_add_point(&G, 0, 3) == ZXM_OK && G.n == 3,
          "gnomon: a right triangle");
    CHECK(gnomon_area2(&G, &t) == ZXM_OK && t == 18 && gnomon_area(&G, &num, &den) == ZXM_OK &&
              num == 9 && den == 1,
          "gnomon: its area is 9: %lld/%lld", (long long) num, (long long) den);
    CHECK(gnomon_side_sq(&G, 0, &t) == ZXM_OK && t == 36 && gnomon_side_sq(&G, 1, &t) == ZXM_OK &&
              t == 45 && gnomon_side_sq(&G, 2, &t) == ZXM_OK && t == 9 &&
              gnomon_side_sq(&G, 3, &t) == ZXM_E_ARG,
          "gnomon: its squared sides 36 + 9 = 45 (Pythagoras), no fourth");
    CHECK(gnomon_slope(&G, 0, &num, &den) == ZXM_OK && num == 0 && den == 1 &&
              gnomon_slope(&G, 1, &num, &den) == ZXM_OK && num == -1 && den == 2 &&
              gnomon_slope(&G, 2, &num, &den) == ZXM_E_STATE,
          "gnomon: slopes 0 and -3/6 reduced to -1/2; the vertical side has none");
    CHECK(gnomon_turn_at(&G, 0, &t) == ZXM_OK && t == 18 && gnomon_convex(&G, &convex) == ZXM_OK &&
              convex,
          "gnomon: every turn to the left: convex");
    CHECK(gnomon_add_point(&G, 1, 1) == ZXM_OK && gnomon_convex(&G, &convex) == ZXM_OK && !convex &&
              gnomon_turn_at(&G, 3, &t) == ZXM_OK && t < 0,
          "gnomon: a dent at (1, 1) turns right: not convex");
    CHECK(gnomon_area2(&G, &t) == ZXM_OK && t == 15 && gnomon_area(&G, &num, &den) == ZXM_OK &&
              num == 15 && den == 2,
          "gnomon: the dent takes 3/2 from the area, 15/2 left: %lld", (long long) t);
    gnomon_clear(&G);
    CHECK(gnomon_add_point(&G, 0, 0) == ZXM_OK && gnomon_add_point(&G, 1, 0) == ZXM_OK &&
              gnomon_add_point(&G, 0, 1) == ZXM_OK && gnomon_area(&G, &num, &den) == ZXM_OK &&
              num == 1 && den == 2,
          "gnomon: a half unit stays 1/2");
    gnomon_clear(&G);
    for (uint32_t i = 0; i < GNOMON_VERTS_MAX; i++)
        if (gnomon_add_point(&G, (int64_t) i, (int64_t) (i * i % 1000u)) != ZXM_OK) break;
    CHECK(G.n == GNOMON_VERTS_MAX && gnomon_add_point(&G, 0, 0) == ZXM_E_TOOBIG &&
              G.n == GNOMON_VERTS_MAX,
          "gnomon: 1024 vertices, the 1025th refused");

    /* GM3: the turtle walks a 3 by 2 rectangle and comes home */
    gnomon_turtle_start(&G);
    CHECK(G.kind == GNOMON_KIND_TURTLE && G.n == 1 && G.x[0] == 0 && G.y[0] == 0,
          "gnomon: the turtle at the origin");
    CHECK(gnomon_add_point(&G, 1, 1) == ZXM_E_STATE, "gnomon: a turtle path takes no free point");
    CHECK(gnomon_turtle_forward(&G, 0) == ZXM_E_ARG &&
              gnomon_turtle_forward(&G, 1000001) == ZXM_E_ARG && G.n == 1,
          "gnomon: no move, or past the domain, is refused");
    CHECK(gnomon_turtle_forward(&G, 3) == ZXM_OK && gnomon_turtle_turn(&G, 1) == ZXM_OK &&
              gnomon_turtle_forward(&G, 2) == ZXM_OK && gnomon_turtle_turn(&G, 5) == ZXM_OK &&
              gnomon_turtle_forward(&G, 3) == ZXM_OK && gnomon_turtle_turn(&G, -3) == ZXM_OK &&
              G.heading == 3 && gnomon_turtle_forward(&G, 2) == ZXM_OK,
          "gnomon: forward 3, left, 2, left (5 quarters), 3, left (-3 quarters), 2");
    CHECK(G.closed && G.n == 4 && G.x[2] == 3 && G.y[2] == 2 && G.x[3] == 0 && G.y[3] == 2,
          "gnomon: home again, the path closes with 4 vertices");
    CHECK(gnomon_turtle_forward(&G, 1) == ZXM_E_STATE && gnomon_turtle_turn(&G, 1) == ZXM_E_STATE,
          "gnomon: a closed path moves no more");
    CHECK(gnomon_area2(&G, &t) == ZXM_OK && t == 12 && gnomon_convex(&G, &convex) == ZXM_OK &&
              convex,
          "gnomon: the walked rectangle's area is 6");
    CHECK(gnomon_turtle_turn(&G, 0) == ZXM_E_STATE, "gnomon: turned only while walking");

    /* GM5 and GM1: a figure in the Hoard and back, vertex for vertex */
    CHECK(gnomon_record(&G, 7, INT64_C(1700000000)) && !gnomon_record(&G, 1000000000, 1),
          "gnomon: the figure's record");
    CHECK(gnomon_save(&G) == ZXM_E_STATE && gnomon_save_as(&G, "", "") == ZXM_E_ARG,
          "gnomon: no path yet");
    CHECK(gnomon_save_as(&G, "", "walk.gnomon") == ZXM_OK && !G.dirty &&
              !strcmp(G.path, "walk.gnomon"),
          "gnomon: saved: %s", LINK.err);
    gnomon_init(&G2, &LINK);
    CHECK(gnomon_open(&G2, "", "walk.gnomon") == ZXM_OK && G2.n == 4 &&
              G2.kind == GNOMON_KIND_TURTLE && G2.closed && !memcmp(G2.x, G.x, 4 * sizeof G.x[0]) &&
              !memcmp(G2.y, G.y, 4 * sizeof G.y[0]) && !G2.dirty,
          "gnomon: read back: %s", LINK.err);
    gnomon_clear(&G);
    CHECK(gnomon_save_as(&G, "", "empty.gnomon") == ZXM_E_STATE,
          "gnomon: an empty figure is not saved");
    for (int64_t i = 0; i < 600; i++)
        CHECK(gnomon_add_point(&G, i * 1000 - 999999, (i % 7) * -142857) == ZXM_OK,
              "gnomon: point %lld", (long long) i);
    CHECK(gnomon_save_as(&G, "", "big.gnomon") == ZXM_OK, "gnomon: a figure in pieces: %s",
          LINK.err);
    CHECK(gnomon_open(&G2, "", "big.gnomon") == ZXM_OK && G2.n == 600 &&
              G2.kind == GNOMON_KIND_POLY && !G2.closed &&
              !memcmp(G2.x, G.x, 600 * sizeof G.x[0]) && !memcmp(G2.y, G.y, 600 * sizeof G.y[0]),
          "gnomon: 600 vertices read back: %s", LINK.err);
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", "bad.gnomon");
    zxm_p_str(&LINK, "text", "gnomon 1 0 3 0\np 0 0\np 1000001 0\np 0 1\n");
    CHECK(zxm_call(&LINK) == ZXM_OK, "gnomon: a broken figure written: %s", LINK.err);
    CHECK(gnomon_open(&G2, "", "bad.gnomon") == ZXM_E_ARG && G2.n == 600 &&
              !strcmp(G2.path, "big.gnomon"),
          "gnomon: a coordinate past the domain leaves the figure as it was");
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", "closed-poly.gnomon");
    zxm_p_str(&LINK, "text", "gnomon 1 0 1 1\np 0 0\n");
    CHECK(zxm_call(&LINK) == ZXM_OK, "gnomon: a closed polygon written: %s", LINK.err);
    CHECK(gnomon_open(&G2, "", "closed-poly.gnomon") == ZXM_E_ARG && G2.n == 600,
          "gnomon: only a turtle path closes: refused");
    CHECK(gnomon_open(&G2, "", "no-such.gnomon") == ZXM_E_REMOTE && G2.n == 600,
          "gnomon: a missing figure is zxvd's error: %s", LINK.err);
}
