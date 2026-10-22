/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Cupel's languages, witnessed, row by row against cupel_rules.h, the C
 * the model runs; the parts it composes (furrow, relief, retort, gnomon)
 * resolved and called; one leapfrog step through the languages equal to
 * the C's; and the BOTH of two models' energies. Slots:
 * @num.cupel_pair_force_net, @num.cupel_leapfrog_velocity,
 * @num.cupel_leapfrog_position, @logic.cupel_sim_precision_ok,
 * @num.cupel_lattice_site_index, @logic.cupel_sim_tree_node_ok,
 * @logic.cupel_sim_run_record_ok; and, used, @num.furrow_range_total,
 * @num.relief_vertex_screen_x, @num.relief_vertex_screen_y,
 * @logic.relief_mesh_limits_ok, @num.retort_model_score_total,
 * @logic.retort_run_limits_ok, @logic.retort_run_record_ok,
 * @num.gnomon_triangle_area2, @num.gnomon_side_len_sq. */
#include "cupel_rules.h"

APP_TEST(cupel, 43, 179)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], g[WAVE_ROWS], h[WAVE_ROWS], want[WAVE_ROWS];

    /* compose: four parts, every part entry it calls resolves to its part */
    static const char *const used[9][2] = {
        {"furrow.range_total", "furrow"},       {"relief.vertex_screen_x", "relief"},
        {"relief.vertex_screen_y", "relief"},   {"relief.mesh_limits_ok", "relief"},
        {"retort.model_score_total", "retort"}, {"retort.run_limits_ok", "retort"},
        {"retort.run_record_ok", "retort"},     {"gnomon.triangle_area2", "gnomon"},
        {"gnomon.side_len_sq", "gnomon"}};
    CHECK(app->resolved && app->nuse == 4 && app->composed == 11 && app->n == 43u &&
              app->n <= ZXAPP_LOGIC_MAX,
          "Cupel: composed of 4 parts, 11 own lines and %u of theirs, within %u", app->n - 11u,
          (unsigned) ZXAPP_LOGIC_MAX);
    for (uint32_t i = 0; i < 9u; i++) {
        const zxapp_logic_t *L = zxapp_find(app, used[i][0]);
        CHECK(L && L->origin && !strcmp(L->origin->id, used[i][1]) && L->loaded &&
                  zxapp_contract(app, used[i][0]),
              "Cupel: %s resolves to %s and loads", used[i][0], used[i][1]);
    }

    /* CR1: pair_force_net, Trebago = KRON = C, for several atoms of one table */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 9);
        b[i] = wrand(0, 9);
        c[i] = i % 4u ? wrand(-1000, 1000) : wrand(-CUPEL_FORCE_MAX, CUPEL_FORCE_MAX);
        if (i == 0) a[i] = 3, b[i] = 4, c[i] = CUPEL_FORCE_MAX;  /* the strongest pull */
        if (i == 1) a[i] = 4, b[i] = 3, c[i] = -CUPEL_FORCE_MAX; /* and push */
    }
    for (int64_t w = 0; w < 10; w++) {
        int64_t net = cupel_pair_force_net(w, a, b, c, WAVE_ROWS);
        zxapp_arg_t fa[4] = {
            {"want", &w, 1}, {"p", a, WAVE_ROWS}, {"q", b, WAVE_ROWS}, {"force", c, WAVE_ROWS}};
        CHECK(net != CUPEL_REFUSED && wave_same(app, "pair_force_net", fa, 4, &net, 1, false),
              "Cupel: pair_force_net on atom %lld = %lld, Trebago = KRON = C", (long long) w,
              (long long) net);
    }
    {
        /* the forces on every atom sum to 0: each pair pulls both ways */
        int64_t all = 0;
        for (int64_t w = 0; w < 10; w++) all += cupel_pair_force_net(w, a, b, c, WAVE_ROWS);
        CHECK(all == 0, "Cupel: Newton's third law, the net forces sum to 0");
        static const int64_t pp[2] = {1, 2}, qq[2] = {2, 1}, big[2] = {CUPEL_FORCE_MAX + 1, 0};
        int64_t one = 1;
        zxapp_arg_t ba[4] = {{"want", &one, 1}, {"p", pp, 2}, {"q", qq, 2}, {"force", big, 2}};
        CHECK(cupel_pair_force_net(1, pp, qq, big, 2) == CUPEL_REFUSED &&
                  wave_lp(app, "pair_force_net", ba, 4) == ZXAPP_BOTH,
              "Cupel: a force past 10^6: Trebago refuses the table, KRON adds it: BOTH");
    }

    /* CR2: leapfrog_velocity, Trebago = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-CUPEL_VEL_MAX, CUPEL_VEL_MAX);
        b[i] = i % 2u ? wrand(-5000, 5000) : wrand(-CUPEL_FORCE_MAX, CUPEL_FORCE_MAX);
        c[i] = i % 3u ? 1 : wrand(1, CUPEL_DT_MAX);
        if (i == 0) a[i] = 0, b[i] = 0, c[i] = 1;
        if (i == 1) a[i] = CUPEL_VEL_MAX, b[i] = CUPEL_FORCE_MAX, c[i] = CUPEL_DT_MAX;
        if (i == 2) a[i] = -CUPEL_VEL_MAX, b[i] = -CUPEL_FORCE_MAX, c[i] = CUPEL_DT_MAX;
        want[i] = cupel_leapfrog_velocity(a[i], b[i], c[i]);
    }
    zxapp_arg_t ka[3] = {{"v", a, WAVE_ROWS}, {"force", b, WAVE_ROWS}, {"dt", c, WAVE_ROWS}};
    CHECK(want[1] == INT64_C(1001000000) && want[2] == INT64_C(-1001000000) &&
              wave_same(app, "leapfrog_velocity", ka, 3, want, WAVE_ROWS, false),
          "Cupel: leapfrog_velocity, Trebago = KRON = C");

    /* CR3: leapfrog_position, Trebago = KRON = C, on the same rows */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        d[i] = wrand(-CUPEL_COORD_MAX, CUPEL_COORD_MAX);
        if (i == 1) d[i] = CUPEL_COORD_MAX;
        if (i == 2) d[i] = -CUPEL_COORD_MAX;
        want[i] = cupel_leapfrog_position(d[i], a[i], b[i], c[i]);
    }
    zxapp_arg_t pa[4] = {
        {"x", d, WAVE_ROWS}, {"v", a, WAVE_ROWS}, {"force", b, WAVE_ROWS}, {"dt", c, WAVE_ROWS}};
    CHECK(want[1] == INT64_C(1001001000000) && want[2] == INT64_C(-1001001000000) &&
              wave_same(app, "leapfrog_position", pa, 4, want, WAVE_ROWS, false),
          "Cupel: leapfrog_position, Trebago = KRON = C");
    {
        static const int64_t zero[2] = {0, 0}, one[2] = {1, 1}, vbig[2] = {0, CUPEL_VEL_MAX + 1},
                             fbig[2] = {0, -CUPEL_FORCE_MAX - 1}, dt0[2] = {1, 0},
                             dtbig[2] = {1, CUPEL_DT_MAX + 1}, xbig[2] = {0, CUPEL_COORD_MAX + 1};
        zxapp_arg_t k1[3] = {{"v", vbig, 2}, {"force", zero, 2}, {"dt", one, 2}};
        zxapp_arg_t k2[3] = {{"v", zero, 2}, {"force", fbig, 2}, {"dt", one, 2}};
        zxapp_arg_t k3[3] = {{"v", one, 2}, {"force", one, 2}, {"dt", dt0, 2}};
        zxapp_arg_t k4[3] = {{"v", one, 2}, {"force", one, 2}, {"dt", dtbig, 2}};
        zxapp_arg_t p1[4] = {{"x", xbig, 2}, {"v", zero, 2}, {"force", zero, 2}, {"dt", one, 2}};
        zxapp_arg_t p2[4] = {{"x", zero, 2}, {"v", one, 2}, {"force", one, 2}, {"dt", dt0, 2}};
        CHECK(cupel_leapfrog_velocity(CUPEL_VEL_MAX + 1, 0, 1) == CUPEL_REFUSED &&
                  cupel_leapfrog_velocity(1, 1, 0) == CUPEL_REFUSED &&
                  cupel_leapfrog_position(CUPEL_COORD_MAX + 1, 0, 0, 1) == CUPEL_REFUSED &&
                  wave_lp(app, "leapfrog_velocity", k1, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "leapfrog_velocity", k2, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "leapfrog_velocity", k3, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "leapfrog_velocity", k4, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "leapfrog_position", p1, 4) == ZXAPP_BOTH &&
                  wave_lp(app, "leapfrog_position", p2, 4) == ZXAPP_BOTH,
              "Cupel: outside the step's domain Trebago refuses, KRON answers: BOTH");
    }

    /* one step of a stretched spring through the languages, as the model
     * takes it (CR8, CR1, CR2, CR3): two atoms on the x axis, rest 1000
     * apart, held 1200 apart */
    {
        static const int64_t x0[2] = {0, 1200}, v0[2] = {0, 0}, pp[1] = {0}, qq[1] = {1};
        int64_t fs = 0, ff[1], net[2], vn[2], xn[2];
        CHECK(cupel_spring_force(50, x0[1] - x0[0] - 1000, 1000, &fs) && fs == 10,
              "Cupel: the spring pulls with floor(50 * 200 / 1000) = 10");
        ff[0] = fs;
        for (int64_t w = 0; w < 2; w++) {
            net[w] = cupel_pair_force_net(w, pp, qq, ff, 1);
            zxapp_arg_t fa[4] = {{"want", &w, 1}, {"p", pp, 1}, {"q", qq, 1}, {"force", ff, 1}};
            CHECK(wave_same(app, "pair_force_net", fa, 4, &net[w], 1, false),
                  "Cupel: the spring's net force on atom %lld", (long long) w);
        }
        static int64_t dt2[2] = {1, 1};
        for (uint32_t w = 0; w < 2u; w++) {
            vn[w] = cupel_leapfrog_velocity(v0[w], net[w], 1);
            xn[w] = cupel_leapfrog_position(x0[w], v0[w], net[w], 1);
        }
        zxapp_arg_t sv[3] = {{"v", v0, 2}, {"force", net, 2}, {"dt", dt2, 2}};
        zxapp_arg_t sx[4] = {{"x", x0, 2}, {"v", v0, 2}, {"force", net, 2}, {"dt", dt2, 2}};
        CHECK(net[0] == 10 && net[1] == -10 && vn[0] == 10 && vn[1] == -10 && xn[0] == 10 &&
                  xn[1] == 1190 && wave_same(app, "leapfrog_velocity", sv, 3, vn, 2, false) &&
                  wave_same(app, "leapfrog_position", sx, 4, xn, 2, false),
              "Cupel: one step pulls the two atoms 20 closer, in every language");
    }

    /* CR4: sim_precision_ok, Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 5u ? wrand(1, CUPEL_SCALE_MAX) : wrand(-3, 3);
        b[i] = wrand(-50, CUPEL_STEPS_MAX);
        c[i] = i % 2u ? wrand(-1000, 1000) : wrand(0, CUPEL_BOUND_MAX);
        if (i == 0) a[i] = 1, b[i] = 0, c[i] = 0;
        if (i == 1) a[i] = CUPEL_SCALE_MAX, b[i] = CUPEL_STEPS_MAX, c[i] = CUPEL_BOUND_MAX;
        if (i == 2) a[i] = 1000, b[i] = -1, c[i] = 5; /* past the declared steps */
        if (i == 3) a[i] = 1000, b[i] = 5, c[i] = -1; /* drifted past the bound */
        want[i] = T_(cupel_precision_ok(a[i], b[i], c[i]));
    }
    zxapp_arg_t ea[3] = {
        {"scale", a, WAVE_ROWS}, {"stepgap", b, WAVE_ROWS}, {"driftgap", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE &&
              wave_same(app, "sim_precision_ok", ea, 3, want, WAVE_ROWS, true),
          "Cupel: sim_precision_ok, Eve = KRON = C");
    {
        int64_t gap[2];
        cupel_precision_gaps(1000, 1000, 100, 10000, 10450, gap);
        bool g1 = gap[0] == 0 && gap[1] == -350 && !cupel_precision_ok(1000, gap[0], gap[1]);
        cupel_precision_gaps(1000, 10, 500, 10450, 10000, gap);
        CHECK(g1 && gap[0] == 990 && gap[1] == 50 && cupel_precision_ok(1000, gap[0], gap[1]),
              "Cupel: a drift of 450 past a bound of 100 halts; 450 within 500 goes on");
        static const int64_t sc[2] = {1000, CUPEL_SCALE_MAX + 1}, s1[2] = {1000, 1000},
                             one[2] = {1, 1}, sg[2] = {1, CUPEL_STEPS_MAX + 1},
                             dg[2] = {1, CUPEL_BOUND_MAX + 1};
        zxapp_arg_t e1[3] = {{"scale", sc, 2}, {"stepgap", one, 2}, {"driftgap", one, 2}};
        zxapp_arg_t e2[3] = {{"scale", s1, 2}, {"stepgap", sg, 2}, {"driftgap", one, 2}};
        zxapp_arg_t e3[3] = {{"scale", s1, 2}, {"stepgap", one, 2}, {"driftgap", dg, 2}};
        CHECK(wave_lp(app, "sim_precision_ok", e1, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "sim_precision_ok", e2, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "sim_precision_ok", e3, 3) == ZXAPP_BOTH,
              "Cupel: past a range Eve says no, KRON yes: BOTH");
    }

    /* CR5: lattice_site_index, KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1000, 1000);
        b[i] = wrand(-1000, 1000);
        c[i] = wrand(-1000, 1000);
        d[i] = wrand(1, 8);
        if (i == 0) a[i] = 7, b[i] = 7, c[i] = 7, d[i] = 8;  /* the last site */
        if (i == 1) a[i] = -1, b[i] = 0, c[i] = 0, d[i] = 8; /* wraps to the far face */
        if (i == 2)
            a[i] = CUPEL_COORD_MAX, b[i] = -CUPEL_COORD_MAX, c[i] = 1, d[i] = CUPEL_COORD_MAX;
        want[i] = cupel_lattice_site(a[i], b[i], c[i], d[i]);
    }
    zxapp_arg_t la[4] = {
        {"ix", a, WAVE_ROWS}, {"iy", b, WAVE_ROWS}, {"iz", c, WAVE_ROWS}, {"side", d, WAVE_ROWS}};
    CHECK(want[0] == 511 && want[1] == 448 && want[2] == 1 && /* x and y wrap to 0 */
              wave_same(app, "lattice_site_index", la, 4, want, WAVE_ROWS, false),
          "Cupel: lattice_site_index, KRON = C");

    /* CR6: sim_tree_node_ok, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-120, 120);
        b[i] = wrand(0, 12000);
        c[i] = wrand(0, 1200);
        d[i] = wrand(1, 1200);
        e[i] = wrand(-12000000, 12000000);
        f[i] = wrand(-CUPEL_COORD_MAX, CUPEL_COORD_MAX);
        g[i] = wrand(-CUPEL_COORD_MAX, CUPEL_COORD_MAX);
        if (i == 0) a[i] = 99, b[i] = 9999, c[i] = 999, d[i] = 118, e[i] = 9999999;
        if (i == 1) a[i] = 1, b[i] = 1, c[i] = 1000, d[i] = 1, e[i] = 0;
        want[i] = T_(cupel_tree_node_ok(a[i], b[i], c[i], d[i], e[i], f[i], g[i]));
    }
    zxapp_arg_t ta[7] = {{"model", a, WAVE_ROWS}, {"molecule", b, WAVE_ROWS},
                         {"atom", c, WAVE_ROWS},  {"element", d, WAVE_ROWS},
                         {"x", e, WAVE_ROWS},     {"y", f, WAVE_ROWS},
                         {"z", g, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE &&
              wave_same(app, "sim_tree_node_ok", ta, 7, want, WAVE_ROWS, true),
          "Cupel: sim_tree_node_ok, MYRA = C");

    /* CR7: sim_run_record_ok, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1200);
        b[i] = wrand(0, 120);
        c[i] = wrand(0, 12000000);
        d[i] = i % 3u ? wrand(0, INT64_C(999999999999)) : INT64_C(999999999999999999) + i % 2u;
        e[i] = wrand(0, 1000000);
        f[i] = wrand(0, 11);
        if (i == 1) a[i] = 999, b[i] = 99, c[i] = 9999999, d[i] = 0, e[i] = 0, f[i] = 1;
        want[i] = T_(cupel_run_node_ok(a[i], b[i], c[i], d[i], e[i], f[i]));
    }
    zxapp_arg_t ra[6] = {{"run", a, WAVE_ROWS},   {"model", b, WAVE_ROWS},
                         {"steps", c, WAVE_ROWS}, {"energy", d, WAVE_ROWS},
                         {"drift", e, WAVE_ROWS}, {"halted", f, WAVE_ROWS}};
    CHECK(want[1] == ZXAPP_TRUE &&
              wave_same(app, "sim_run_record_ok", ra, 6, want, WAVE_ROWS, true),
          "Cupel: sim_run_record_ok, MYRA = C");

    /* the parts, called through Cupel as it exports them */
    {
        int64_t tot = 0;
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = wrand(0, 9); /* a row's molecule */
            b[i] = 2;
            c[i] = 6;
            d[i] = wrand(0, INT64_C(3000000000000));
        }
        zxapp_arg_t fa[4] = {{"pos", a, WAVE_ROWS},
                             {"lo", b, WAVE_ROWS},
                             {"hi", c, WAVE_ROWS},
                             {"cell", d, WAVE_ROWS}};
        CHECK(furrow_range_total(a, b, c, d, WAVE_ROWS, &tot) &&
                  wave_same(app, "furrow.range_total", fa, 4, &tot, 1, false),
              "Cupel: furrow.range_total, the energy of molecules 2 .. 6");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-RELIEF_COORD_MAX, RELIEF_COORD_MAX);
        b[i] = wrand(-RELIEF_COORD_MAX, RELIEF_COORD_MAX);
        c[i] = wrand(-RELIEF_COORD_MAX, RELIEF_COORD_MAX);
        d[i] = i % 2u ? RELIEF_ONE : 0;
        e[i] = i % 2u ? 0 : RELIEF_ONE;
        f[i] = wrand(1, RELIEF_SCALE_MAX);
        g[i] = wrand(RELIEF_COORD_MAX + 1, RELIEF_FOCAL_MAX);
        CHECK(relief_screen_x(a[i], c[i], d[i], e[i], f[i], g[i], &want[i]) &&
                  relief_screen_y(a[i], b[i], c[i], d[i], e[i], f[i], g[i], &h[i]),
              "Cupel: an atom in front of the viewer, row %u", i);
    }
    zxapp_arg_t vx[6] = {{"x", a, WAVE_ROWS},   {"z", c, WAVE_ROWS},     {"cos", d, WAVE_ROWS},
                         {"sin", e, WAVE_ROWS}, {"scale", f, WAVE_ROWS}, {"focal", g, WAVE_ROWS}};
    zxapp_arg_t vy[7] = {{"x", a, WAVE_ROWS},    {"y", b, WAVE_ROWS},   {"z", c, WAVE_ROWS},
                         {"cos", d, WAVE_ROWS},  {"sin", e, WAVE_ROWS}, {"scale", f, WAVE_ROWS},
                         {"focal", g, WAVE_ROWS}};
    CHECK(wave_same(app, "relief.vertex_screen_x", vx, 6, want, WAVE_ROWS, false) &&
              wave_same(app, "relief.vertex_screen_y", vy, 7, h, WAVE_ROWS, false),
          "Cupel: relief.vertex_screen_x and _y, atoms on the screen");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 600);
        b[i] = wrand(0, 1100);
        want[i] = T_(relief_mesh_limits_ok(a[i], b[i]));
    }
    zxapp_arg_t ma[2] = {{"vertices", a, WAVE_ROWS}, {"faces", b, WAVE_ROWS}};
    CHECK(wave_same(app, "relief.mesh_limits_ok", ma, 2, want, WAVE_ROWS, true),
          "Cupel: relief.mesh_limits_ok, a model's atoms and springs to draw");
    {
        int64_t w = 2;
        for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(1, 3), b[i] = wrand(0, 100);
        int64_t tot = retort_model_score_total(a, w, b, WAVE_ROWS);
        zxapp_arg_t sa[3] = {{"model", a, WAVE_ROWS}, {"want", &w, 1}, {"score", b, WAVE_ROWS}};
        CHECK(tot != RETORT_REFUSED &&
                  wave_same(app, "retort.model_score_total", sa, 3, &tot, 1, false),
              "Cupel: retort.model_score_total, model 2's score over its runs");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 5000);
        b[i] = i % 2u ? wrand(0, 1600000000) : wrand(-3, 3);
        c[i] = wrand(0, 4000);
        want[i] = T_(retort_run_limits_ok(a[i], b[i], c[i]));
    }
    zxapp_arg_t qa[3] = {
        {"prompt", a, WAVE_ROWS}, {"fuel", b, WAVE_ROWS}, {"seconds", c, WAVE_ROWS}};
    CHECK(wave_same(app, "retort.run_limits_ok", qa, 3, want, WAVE_ROWS, true),
          "Cupel: retort.run_limits_ok, a run's note, work and seconds");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1200);
        b[i] = wrand(0, 120);
        c[i] = i % 2u ? 1 : wrand(0, 11);
        d[i] = wrand(1, 255);
        e[i] = wrand(1, 1600000000);
        f[i] = wrand(0, 12000000);
        g[i] = wrand(0, 101);
        h[i] = 0;
        want[i] = T_(retort_run_record_fits(a[i], b[i], c[i], d[i], e[i], f[i], g[i], h[i]));
    }
    zxapp_arg_t rr[8] = {{"run", a, WAVE_ROWS},    {"model", b, WAVE_ROWS}, {"kind", c, WAVE_ROWS},
                         {"prompt", d, WAVE_ROWS}, {"fuel", e, WAVE_ROWS},  {"ms", f, WAVE_ROWS},
                         {"score", g, WAVE_ROWS},  {"uj", h, WAVE_ROWS}};
    CHECK(wave_same(app, "retort.run_record_ok", rr, 8, want, WAVE_ROWS, true),
          "Cupel: retort.run_record_ok, a run as a contestant on scratch state");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-GNOMON_COORD_MAX, GNOMON_COORD_MAX);
        b[i] = wrand(-GNOMON_COORD_MAX, GNOMON_COORD_MAX);
        c[i] = wrand(-GNOMON_COORD_MAX, GNOMON_COORD_MAX);
        d[i] = wrand(-GNOMON_COORD_MAX, GNOMON_COORD_MAX);
        CHECK(gnomon_triangle_area2(0, 0, a[i], 0, 0, a[i], &want[i]) && want[i] == a[i] * a[i] &&
                  gnomon_side_len_sq(a[i], b[i], c[i], d[i], &h[i]),
              "Cupel: a face and a length, row %u", i);
        e[i] = 0;
    }
    zxapp_arg_t tr[6] = {{"ax", e, WAVE_ROWS}, {"ay", e, WAVE_ROWS}, {"bx", a, WAVE_ROWS},
                         {"by", e, WAVE_ROWS}, {"cx", e, WAVE_ROWS}, {"cy", a, WAVE_ROWS}};
    zxapp_arg_t sl[4] = {
        {"ax", a, WAVE_ROWS}, {"ay", b, WAVE_ROWS}, {"bx", c, WAVE_ROWS}, {"by", d, WAVE_ROWS}};
    CHECK(wave_same(app, "gnomon.triangle_area2", tr, 6, want, WAVE_ROWS, false) &&
              wave_same(app, "gnomon.side_len_sq", sl, 4, h, WAVE_ROWS, false),
          "Cupel: gnomon.triangle_area2 (a square face, twice its half) and gnomon.side_len_sq");

    /* CR9, the BOTH test: model A's energy after 1000 steps is 10000 units
     * and model B's 10450, with a tolerance of 100: BOTH, both shown */
    {
        int64_t shown[2] = {0, 0};
        lpres_state_t st = cupel_energy_state(10000, 10450, 100, shown);
        CHECK(st == LPRES_STATE_BOTH && (int) st == 3 && shown[0] == 10000 && shown[1] == 10450,
              "Cupel: 10000 and 10450 past a tolerance of 100: BOTH, both shown");
        CHECK(lpres_conjoin(st, LPRES_STATE_TRUE) != LPRES_STATE_TRUE &&
                  cupel_energy_state(10000, 10100, 100, shown) == LPRES_STATE_TRUE &&
                  cupel_energy_state(10000, 10101, 100, shown) == LPRES_STATE_BOTH &&
                  cupel_energy_state(10000, 10000, -1, shown) == LPRES_STATE_NEITHER &&
                  cupel_energy_state(INT64_MAX, INT64_MIN, INT64_MAX, shown) == LPRES_STATE_BOTH,
              "Cupel: within the tolerance TRUE, one past it BOTH, no tolerance NEITHER");
    }
}
