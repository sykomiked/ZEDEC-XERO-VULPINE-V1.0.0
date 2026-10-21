/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Burin's model against zxvd's real dispatcher (LINK): a drawing made,
 * flattened and hatched, saved to the Hoard and read back, written as a
 * PDF and printed on the fake printer (test_fake_device.h, which
 * test_apps.c includes; started here and stopped at the end). */
#include "burin_model.h"

APPS_TEST(burin)
{
    static burin_t B, B2;
    static int32_t fx[4096], fy[4096];
    uint32_t n = 0, added = 0;

    /* the rules, as the blocks run them */
    CHECK(burin_bezier_at(0, 100, 200, 300, 1, 3) == 100 &&
              burin_bezier_at(0, 0, 0, -1, 1, 2) == -1 &&
              burin_bezier_at(0, 0, 100, 100, 1, 2) == 50 &&
              burin_bezier_at(7, 1, 2, 9, 0, 5) == 7 && burin_bezier_at(7, 1, 2, 9, 5, 5) == 9 &&
              burin_bezier_at(0, 0, 0, 0, 0, 1025) == BURIN_REFUSED,
          "burin: a point on a cubic, floored, refused outside its domain");
    int64_t num = 0, den = 0;
    CHECK(burin_hatch_lines(100, 20) == 4 && burin_hatch_lines(0, 5) == 0 &&
              burin_hatch_lines(101, 20) == 5 && burin_hatch_lines(1, 0) == -1 &&
              burin_hatch_gap(100, 4, &num, &den) && num == 100 && den == 5 &&
              !burin_hatch_gap(100, 0, &num, &den),
          "burin: hatch lines strictly inside a span, and their even spacing");
    CHECK(burin_path_limits_ok(2, 1, 1, 1) && burin_path_limits_ok(769, 1000000, 1000000, 10000) &&
              !burin_path_limits_ok(770, 1, 1, 1) && !burin_path_limits_ok(2, 0, 1, 1) &&
              !burin_path_limits_ok(2, 1, 1, 10001),
          "burin: path limits (Eve's ranges)");
    CHECK(burin_record_ok(999999999, 999, 99999, INT64_C(999999999999)) &&
              !burin_record_ok(1000000000, 0, 0, 0) && !burin_record_ok(0, 1000, 0, 0) &&
              !burin_record_ok(0, 0, 100000, 0) &&
              !burin_record_ok(0, 0, 0, INT64_C(1000000000000)),
          "burin: the path record's digit widths (MYRA's)");

    /* a drawing: nothing until one is made; limits are refused, never cut */
    burin_init(&B, &LINK);
    CHECK(burin_move_to(&B, 0, 0, 100) == ZXM_E_STATE && burin_save(&B) == ZXM_E_STATE &&
              burin_export_pdf(&B, "x.pdf", BURIN_TOL) == ZXM_E_STATE,
          "burin: no drawing yet");
    CHECK(burin_new(&B, 0, 84200) == ZXM_E_ARG && burin_new(&B, 59500, 1000001) == ZXM_E_ARG &&
              B.width == 0,
          "burin: a page outside its limits is refused");
    CHECK(burin_new(&B, 59500, 84200) == ZXM_OK && B.width == 59500 && B.npaths == 0 && B.dirty,
          "burin: an A4 page");
    CHECK(burin_line_to(&B, 10, 10) == ZXM_E_STATE, "burin: a segment needs a path");
    CHECK(burin_move_to(&B, 1000, 1000, 0) == ZXM_E_ARG &&
              burin_move_to(&B, 1000001, 1000, 50) == ZXM_E_ARG && B.npaths == 0,
          "burin: a stroke of 0 or a point off every page is refused");
    CHECK(burin_move_to(&B, 1000, 1000, 50) == ZXM_OK &&
              burin_curve_to(&B, 1000, 41000, 41000, 41000, 41000, 1000) == ZXM_OK &&
              burin_line_to(&B, 1000, 1000) == ZXM_OK && B.npaths == 1 &&
              burin_points(&B.p[0]) == 5,
          "burin: an arch, a cubic and a line back");
    CHECK(burin_curve_to(&B, 0, 0, 0, 0, 0, 2000000) == ZXM_E_ARG && B.p[0].nseg == 2,
          "burin: a point past the limits is refused and nothing changes");

    /* BM3: flattening follows bezier_at, at the fewest steps in tolerance */
    burin_seg_t arch = B.p[0].seg[0];
    int64_t steps = burin_flat_steps(&arch, 1000, 1000, BURIN_TOL);
    CHECK(steps == 35 && 3 * 40000 <= 4 * BURIN_TOL * steps * steps &&
              3 * 40000 > 4 * BURIN_TOL * (steps - 1) * (steps - 1) &&
              burin_flat_steps(&arch, 1000, 1000, 1000000) == 1,
          "burin: the arch takes %lld steps at a quarter point", (long long) steps);
    uint32_t on = 0;
    CHECK(burin_flatten(&B, 0, BURIN_TOL, fx, fy, 4096, &n) == ZXM_OK && n == 1u + 35u + 1u &&
              fx[0] == 1000 && fy[0] == 1000 && fx[35] == 41000 && fy[35] == 1000 &&
              fx[36] == 1000 && fy[36] == 1000,
          "burin: the arch flattened: %u points", n);
    for (uint32_t k = 1; k <= 35; k++)
        on += fx[k] == burin_bezier_at(1000, 1000, 41000, 41000, k, 35) &&
              fy[k] == burin_bezier_at(1000, 41000, 41000, 1000, k, 35) && fy[k] <= 31000;
    CHECK(on == 35, "burin: every step on the curve, none above its top (%u of 35)", on);
    CHECK(burin_flatten(&B, 0, BURIN_TOL, fx, fy, 10, &n) == ZXM_E_TOOBIG &&
              burin_flatten(&B, 1, BURIN_TOL, fx, fy, 4096, &n) == ZXM_E_ARG,
          "burin: no room, and no such path");

    /* BM4: hatching, at a spacing and evenly */
    CHECK(burin_hatch_spaced(&B, 2000, 2000, 1000, 3000, 100, 20, &added) == ZXM_E_ARG &&
              burin_hatch_spaced(&B, 2000, 2000, 3000, 3000, 0, 20, &added) == ZXM_E_ARG &&
              burin_hatch_spaced(&B, 2000, 2000, 3000, 3000, 100, 0, &added) == ZXM_E_ARG &&
              B.npaths == 1,
          "burin: a rectangle inside out, a gap of 0 and a stroke of 0 are refused");
    CHECK(burin_hatch_spaced(&B, 2000, 2000, 3000, 3000, 100, 20, &added) == ZXM_OK && added == 9 &&
              B.npaths == 10 && B.p[1].y0 == 2100 && B.p[9].y0 == 2900 && B.p[9].seg[0].x == 3000 &&
              B.p[9].seg[0].y == 2900,
          "burin: 9 lines at 1 point inside 10 points, none on an edge");
    CHECK(burin_hatch_spaced(&B, 0, 0, 10, 100000, 1, 20, &added) == ZXM_E_TOOBIG && B.npaths == 10,
          "burin: more lines than paths left: refused before any is drawn");
    CHECK(burin_hatch_even(&B, 5000, 1000, 9000, 1700, 0, 20, &added) == ZXM_E_ARG &&
              burin_hatch_even(&B, 5000, 1000, 9000, 1700, 6, 20, &added) == ZXM_OK && added == 6 &&
              B.npaths == 16 && B.p[10].y0 == 1100 && B.p[15].y0 == 1600,
          "burin: six lines set evenly, every 700/7 = 100");
    CHECK(burin_hatch_even(&B, 0, 0, 10, 100, 3, 20, &added) == ZXM_OK && B.p[16].y0 == 25 &&
              B.p[17].y0 == 50 && B.p[18].y0 == 75 &&
              burin_hatch_even(&B, 0, 0, 10, 10, 2, 20, &added) == ZXM_OK && B.p[19].y0 == 3 &&
              B.p[20].y0 == 6,
          "burin: an even spacing that is a fraction (10/3) floors each line");
    CHECK(burin_path_record(&B, 0, INT64_C(1700000000)) && !burin_path_record(&B, 99, 1) &&
              !burin_path_record(&B, 0, INT64_C(1000000000000)),
          "burin: a path's record");

    /* BM1: into the Hoard and back, point for point */
    CHECK(burin_save_as(&B, "", "") == ZXM_E_ARG, "burin: no path");
    CHECK(burin_save_as(&B, "", "plate-one.burin") == ZXM_OK && !B.dirty &&
              !strcmp(B.path, "plate-one.burin"),
          "burin: saved: %s", LINK.err);
    burin_init(&B2, &LINK);
    CHECK(burin_open(&B2, "", "plate-one.burin") == ZXM_OK && B2.width == 59500 &&
              B2.height == 84200 && B2.npaths == B.npaths && !B2.dirty &&
              !memcmp(B2.p, B.p, B.npaths * sizeof B.p[0]),
          "burin: read back: %s", LINK.err);
    CHECK(burin_line_to(&B2, 0, 0) == ZXM_OK && B2.dirty && burin_save(&B2) == ZXM_OK &&
              burin_open(&B, "", "plate-one.burin") == ZXM_OK && B.p[B.npaths - 1u].nseg == 2 &&
              !B.dirty,
          "burin: changed, saved in place and read again: %s", LINK.err);
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", "not-a-plate.burin");
    zxm_p_str(&LINK, "text", "burin 1 100 100\np 0 1 1\n");
    CHECK(zxm_call(&LINK) == ZXM_OK, "burin: a broken drawing written: %s", LINK.err);
    uint32_t kept = B.npaths;
    CHECK(burin_open(&B, "", "not-a-plate.burin") == ZXM_E_ARG && B.npaths == kept &&
              !strcmp(B.path, "plate-one.burin"),
          "burin: a stroke of 0 in a file leaves the drawing as it was");
    CHECK(burin_open(&B, "", "no-such.burin") == ZXM_E_REMOTE && B.npaths == kept,
          "burin: a missing drawing is zxvd's error: %s", LINK.err);

    /* BM5: the PDF, read back as zxvd keeps it, then printed */
    CHECK(burin_export_pdf(&B, "plate-one.txt", BURIN_TOL) == ZXM_E_ARG &&
              burin_export_pdf(&B, "plate-one.pdf", 0) == ZXM_E_ARG,
          "burin: not a .pdf, or no tolerance");
    CHECK(burin_export_pdf(&B, "plate-one.pdf", BURIN_TOL) == ZXM_OK, "burin: PDF written: %s",
          LINK.err);
    zxm_begin(&LINK, "files.read");
    zxm_p_str(&LINK, "path", "plate-one.pdf");
    zxm_p_u64(&LINK, "max", 600);
    CHECK(zxm_call(&LINK) == ZXM_OK, "burin: the PDF read: %s", LINK.err);
    static char head[1024];
    int32_t t = zxm_get(&LINK, LINK.result, "text");
    int32_t hn = t < 0 ? -1 : w4_json_str(LINK.resp, &LINK.tok[t], head, sizeof head);
    CHECK(hn > 0 && !strncmp(head, "%PDF-1.4\n1 0 obj\n", 17) &&
              strstr(head, "/MediaBox [0 0 595.00 842.00]") &&
              strstr(head, "0.50 w\n10.00 10.00 m\n"),
          "burin: a PDF page of A4, the arch's stroke and start: %.120s", head);
    char uri[96];
    CHECK(fake_start(), "burin: the fake printer");
    snprintf(uri, sizeof uri, "ipp://127.0.0.1:%d/ipp/print", FAKE_PORT);
    CHECK(burin_print(&B, "", "plate-one.pdf", 1) == ZXM_E_ARG &&
              burin_print(&B, uri, "plate-one.pdf", 1000) == ZXM_E_ARG && B.job == 0,
          "burin: no printer, or too many copies");
    CHECK(burin_print(&B, uri, "plate-one.burin", 0) == ZXM_E_REMOTE && B.job == 0,
          "burin: a drawing file is no format a printer takes: %s", LINK.err);
    CHECK(burin_print(&B, uri, "plate-one.pdf", 2) == ZXM_OK && B.job == 77 &&
              !strcmp(B.job_state, "pending") && strstr(LINK.req, "\"copies\":2"),
          "burin: printed: job %lld %s (%s)", (long long) B.job, B.job_state, LINK.err);
    CHECK(strstr(fake_get("/fake/last"), "fmt=application/pdf") &&
              strstr(fake_get("/fake/last"), "%PDF-1.4"),
          "burin: what the printer got: %s", fake_get("/fake/last"));
    fake_stop();
}
