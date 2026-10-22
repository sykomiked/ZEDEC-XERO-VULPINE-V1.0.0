/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Cupel's model against zxvd's real dispatcher (LINK): a spring stepped by
 * the leapfrog method with its centre of mass kept exactly, runs halted by
 * their declared steps, by their drift bound and by the domain with
 * nothing changed, a lattice built and measured, a model saved to the
 * Hoard and read back only on the owner's word, runs recorded in the
 * Hoard (files.read, files.write) and scored, the machine's facts
 * (sys.info), and two models' energies held BOTH with both shown. */
#include "cupel_model.h"

static cupel_t CUP, CUP2;

static bool cup_write(const char *path, const char *text)
{
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", path);
    zxm_p_str(&LINK, "text", text);
    return zxm_call(&LINK) == ZXM_OK;
}

/* two atoms 1200 apart on x, a spring of rest 1000 and k 50 between */
static bool cup_spring(cupel_t *c)
{
    static const int64_t x0[3] = {0, 0, 0}, x1[3] = {1200, 0, 0}, v[3] = {0, 0, 0},
                         rest[3] = {1000, 0, 0};
    uint32_t n0 = 9, n1 = 9;
    cupel_init(c, &LINK);
    return cupel_add_atom(c, 1, 6, x0, v, &n0) == ZXM_OK &&
           cupel_add_atom(c, 1, 6, x1, v, &n1) == ZXM_OK && n0 == 0 && n1 == 1 &&
           cupel_add_pair(c, 0, 1, 50, rest) == ZXM_OK;
}

APPS_TEST(cupel)
{
    int64_t e = 0, area = 0, sx = 0, sy = 0, shown[2] = {0, 0};
    cupel_run_t r;

    /* the rules */
    {
        static const int64_t p[3] = {0, 1, 2}, q[3] = {1, 2, 0}, f[3] = {5, -7, 3};
        int64_t s[3] = {200, 0, 0}, out = 0;
        CHECK(cupel_pair_force_net(0, p, q, f, 3) == 2 &&
                  cupel_pair_force_net(1, p, q, f, 3) == -12 &&
                  cupel_pair_force_net(2, p, q, f, 3) == 10 &&
                  cupel_leapfrog_velocity(5, -2, 3) == -1 &&
                  cupel_leapfrog_position(100, 5, -2, 3) == 97 &&
                  cupel_leapfrog_velocity(0, 0, 0) == CUPEL_REFUSED &&
                  cupel_lattice_site(-1, -1, -1, 3) == 26 &&
                  cupel_lattice_site(0, 0, 0, 0) == CUPEL_REFUSED &&
                  cupel_spring_force(50, -201, 1000, &out) && out == -11 &&
                  cupel_spring_energy2(50, s, 1000, &out) && out == 2000 &&
                  cupel_score(0, 1000, false) == 100 && cupel_score(450, 1000, false) == 55 &&
                  cupel_score(5, 1000, true) == 0 && cupel_score(0, 0, false) == 100,
              "cupel: the rules");
    }

    /* CM2: the model and its limits; a refusal changes nothing */
    cupel_init(&CUP, &LINK);
    {
        static const int64_t far[3] = {CUPEL_COORD_MAX + 1, 0, 0}, z[3] = {0, 0, 0},
                             fast[3] = {0, 0, CUPEL_VEL_MAX + 1};
        CHECK(cupel_set_bounds(&CUP, 0, 1, 1000, 1000, 60) == ZXM_E_ARG &&
                  cupel_set_bounds(&CUP, 1000, 0, 1000, 1000, 60) == ZXM_E_ARG &&
                  cupel_set_bounds(&CUP, 1000, 1, CUPEL_STEPS_MAX + 1, 1000, 60) == ZXM_E_ARG &&
                  cupel_set_bounds(&CUP, 1000, 1, 1000, CUPEL_BOUND_MAX + 1, 60) == ZXM_E_ARG &&
                  cupel_set_bounds(&CUP, 1000, 1, 1000, 1000, 3601) == ZXM_E_ARG &&
                  CUP.scale == 1000 && CUP.declared == 1000 && CUP.seconds == 60,
              "cupel: bounds past CR4 or the run limits are refused, the old ones kept");
        CHECK(cupel_set_model(&CUP, 100, "x") == ZXM_E_ARG &&
                  cupel_set_model(&CUP, 7, "a\tb") == ZXM_E_ARG &&
                  cupel_set_model(&CUP, 7, "") == ZXM_E_ARG && CUP.model == 1 &&
                  cupel_set_model(&CUP, 7, "Water") == ZXM_OK && CUP.model == 7,
              "cupel: a model number 0..99 and a one-line name");
        CHECK(cupel_add_atom(&CUP, 1, 8, far, z, NULL) == ZXM_E_ARG &&
                  cupel_add_atom(&CUP, 1, 8, z, fast, NULL) == ZXM_E_ARG &&
                  cupel_add_atom(&CUP, 1, 119, z, z, NULL) == ZXM_E_ARG &&
                  cupel_add_atom(&CUP, 10000, 8, z, z, NULL) == ZXM_E_ARG && CUP.natom == 0,
              "cupel: an atom outside the domain or the tree is refused");
        CHECK(cupel_add_pair(&CUP, 0, 1, 1, z) == ZXM_E_ARG && cupel_begin(&CUP) == ZXM_E_STATE &&
                  cupel_step(&CUP) == ZXM_E_STATE,
              "cupel: no atoms, no spring and no step");
    }

    /* CM4, CM5: a stretched spring, step by step */
    CHECK(cup_spring(&CUP), "cupel: two atoms and a spring");
    CHECK(cupel_energy(&CUP, 0, CUPEL_MOLECULE, &e) == ZXM_OK && e == 1000,
          "cupel: the spring's energy is floor(50 * 200^2 / 1000) / 2 = 1000: %lld", (long long) e);
    CHECK(cupel_energy(&CUP, 2, 9, &e) == ZXM_OK && e == 0,
          "cupel: molecules 2..9 hold none of it");
    {
        static const int64_t rest[3] = {0, 0, 0};
        CHECK(cupel_add_pair(&CUP, 1, 1, 50, rest) == ZXM_E_ARG &&
                  cupel_add_pair(&CUP, 0, 2, 50, rest) == ZXM_E_ARG &&
                  cupel_add_pair(&CUP, 0, 1, CUPEL_K_MAX + 1, rest) == ZXM_E_ARG && CUP.npair == 1,
              "cupel: a spring to itself, to no atom, or too stiff, is refused");
    }
    CHECK(cupel_begin(&CUP) == ZXM_OK && CUP.e0 == 1000 && cupel_step(&CUP) == ZXM_OK &&
              CUP.taken == 1 && CUP.atom[0].x[0] == 10 && CUP.atom[1].x[0] == 1190 &&
              CUP.atom[0].v[0] == 10 && CUP.atom[1].v[0] == -10 && CUP.energy == 910 &&
              CUP.halt == CUPEL_GOING,
          "cupel: one step pulls them 20 closer; the energy 910, a drift of 90: %lld",
          (long long) CUP.energy);
    {
        bool com = true;
        int64_t lo = INT64_MAX, hi = INT64_MIN;
        for (int i = 0; i < 400 && CUP.halt == CUPEL_GOING; i++) {
            if (cupel_step(&CUP) != ZXM_OK) break;
            com = com && CUP.atom[0].x[0] + CUP.atom[1].x[0] == 1200 &&
                  CUP.atom[0].v[0] + CUP.atom[1].v[0] == 0 && CUP.atom[0].x[1] == 0;
            lo =
                CUP.atom[1].x[0] - CUP.atom[0].x[0] < lo ? CUP.atom[1].x[0] - CUP.atom[0].x[0] : lo;
            hi =
                CUP.atom[1].x[0] - CUP.atom[0].x[0] > hi ? CUP.atom[1].x[0] - CUP.atom[0].x[0] : hi;
        }
        CHECK(com && CUP.taken == 401 && CUP.halt == CUPEL_GOING && lo < 1000 && hi > 1150 &&
                  hi <= 1250 && lo >= 750,
              "cupel: 401 steps: it oscillates about its rest (%lld .. %lld), the centre of mass "
              "fixed exactly, inside the drift bound",
              (long long) lo, (long long) hi);
    }

    /* CR4: the bounds halt a run */
    CHECK(cup_spring(&CUP) && cupel_set_bounds(&CUP, 1000, 1, 1000, 10, 60) == ZXM_OK &&
              cupel_begin(&CUP) == ZXM_OK && cupel_step(&CUP) == ZXM_OK &&
              CUP.halt == CUPEL_HALT_DRIFT && CUP.taken == 1 && CUP.atom[0].x[0] == 10 &&
              cupel_step(&CUP) == ZXM_E_STATE && CUP.taken == 1,
          "cupel: a drift of 90 past a bound of 10 halts the run; the step is kept to be seen");
    CHECK(cup_spring(&CUP) && cupel_set_bounds(&CUP, 1000, 1, 3, 1000, 60) == ZXM_OK &&
              cupel_run(&CUP, 5, "five of three", &r) == ZXM_OK && r.steps == 3 && r.halted &&
              CUP.halt == CUPEL_HALT_STEPS && r.score == 0 && CUP.atom[0].x[0] != 0,
          "cupel: five steps asked of three declared: three taken, then halted, scored 0");
    {
        static const int64_t x0[3] = {0, 0, 0}, x1[3] = {CUPEL_COORD_MAX, 0, 0}, v0[3] = {0, 0, 0},
                             v1[3] = {CUPEL_VEL_MAX, 0, 0}, rest[3] = {CUPEL_COORD_MAX, 0, 0};
        cupel_init(&CUP, &LINK);
        CHECK(cupel_add_atom(&CUP, 1, 1, x0, v0, NULL) == ZXM_OK &&
                  cupel_add_atom(&CUP, 1, 1, x1, v1, NULL) == ZXM_OK &&
                  cupel_add_pair(&CUP, 0, 1, 1, rest) == ZXM_OK && cupel_begin(&CUP) == ZXM_OK &&
                  cupel_step(&CUP) == ZXM_E_ARG && CUP.halt == CUPEL_HALT_RANGE && CUP.taken == 0 &&
                  CUP.atom[1].x[0] == CUPEL_COORD_MAX && CUP.atom[1].v[0] == CUPEL_VEL_MAX,
              "cupel: a step that would leave the domain halts with nothing changed");
    }

    /* CM3: a lattice, numbered by its sites */
    cupel_init(&CUP, &LINK);
    CHECK(cupel_face_area(&CUP, &area) == ZXM_E_STATE, "cupel: no lattice, no face");
    CHECK(cupel_lattice(&CUP, 3, 1000, 18, 40) == ZXM_OK && CUP.natom == 27 && CUP.npair == 54 &&
              CUP.atom[26].x[0] == 2000 && CUP.atom[26].x[1] == 2000 && CUP.atom[26].x[2] == 2000 &&
              CUP.atom[5].x[0] == 0 && CUP.atom[5].x[1] == 1000 && CUP.atom[5].x[2] == 2000 &&
              cupel_drawable(&CUP),
          "cupel: 27 argon atoms, 54 springs, atom (x * 3 + y) * 3 + z, drawable");
    CHECK(cupel_face_area(&CUP, &area) == ZXM_OK && area == INT64_C(4000000),
          "cupel: its face, 2000 by 2000, twice its half triangle");
    CHECK(cupel_lattice(&CUP, 8, 1000, 18, 40) == ZXM_E_TOOBIG && CUP.natom == 27 &&
              cupel_lattice(&CUP, 3, 600000, 18, 40) == ZXM_E_ARG && CUP.natom == 27,
          "cupel: 1344 springs or a lattice past the domain is refused, the old one kept");
    CHECK(cupel_begin(&CUP) == ZXM_OK && CUP.e0 == 0 && cupel_step(&CUP) == ZXM_OK &&
              CUP.energy == 0 && CUP.atom[13].x[0] == 1000,
          "cupel: a lattice at rest stays at rest");
    CHECK(cupel_screen(&CUP, 26, RELIEF_ONE, 0, 100, 1000, &sx, &sy) == ZXM_OK && sx == 666 &&
              sy == 666 && cupel_screen(&CUP, 27, RELIEF_ONE, 0, 100, 1000, &sx, &sy) == ZXM_E_ARG,
          "cupel: atom 26 on the screen through relief's view: %lld, %lld", (long long) sx,
          (long long) sy);

    /* CM1: saved and read back, only on the owner's word */
    CHECK(cup_spring(&CUP) && cupel_set_model(&CUP, 3, "Spring") == ZXM_OK, "cupel: a model");
    int before = sent;
    CHECK(cupel_save(&CUP, "", "spring.cupel", false) == ZXM_E_STATE && sent == before &&
              CUP.path[0] == 0,
          "cupel: no owner's word, nothing written");
    CHECK(cupel_save(&CUP, "", "spring.cupel", true) == ZXM_OK && !CUP.dirty &&
              !strcmp(CUP.path, "spring.cupel"),
          "cupel: saved: %s", LINK.err);
    cupel_init(&CUP2, &LINK);
    CHECK(cupel_open(&CUP2, "", "spring.cupel") == ZXM_OK && CUP2.model == 3 &&
              !strcmp(CUP2.name, "Spring") && CUP2.natom == 2 && CUP2.npair == 1 &&
              CUP2.atom[1].x[0] == 1200 && CUP2.pair[0].k == 50 && CUP2.pair[0].rest[0] == 1000 &&
              CUP2.scale == 1000 && CUP2.declared == 1000,
          "cupel: read back atom for atom: %s", LINK.err);
    CHECK(cup_write("bad.cupel",
                    "cupel 1 3 1000 1 1000 1000 60 1 0\nn Bad\na 1 6 1000001 0 0 0 0 0\n") &&
              cupel_open(&CUP2, "", "bad.cupel") == ZXM_E_ARG && CUP2.natom == 2 &&
              !strcmp(CUP2.path, "spring.cupel"),
          "cupel: an atom past the domain leaves the open model as it was");
    CHECK(cupel_open(&CUP2, "", "no-such.cupel") == ZXM_E_REMOTE && CUP2.natom == 2,
          "cupel: a missing model is zxvd's error: %s", LINK.err);
    {
        /* a lattice of 343 atoms and 882 springs goes in pieces */
        CHECK(cupel_lattice(&CUP, 7, 1000, 18, 40) == ZXM_OK &&
                  cupel_save(&CUP, "", "big.cupel", true) == ZXM_OK &&
                  cupel_open(&CUP2, "", "big.cupel") == ZXM_OK && CUP2.natom == 343 &&
                  CUP2.npair == 882 && !memcmp(CUP2.atom, CUP.atom, 343 * sizeof CUP.atom[0]),
              "cupel: 343 atoms read back: %s", LINK.err);
    }

    /* CM6, CM7: runs, timed by sys.info, recorded only on the owner's word */
    CHECK(cupel_machine(&CUP) == ZXM_OK && CUP.machine.known && CUP.machine.cpus >= 1 &&
              CUP.machine.arch[0] && CUP.machine.mem_total > 0,
          "cupel: the machine's facts: %s, %llu cpus", CUP.machine.arch,
          (unsigned long long) CUP.machine.cpus);
    CHECK(cupel_load_runs(&CUP) == ZXM_OK && CUP.runs_loaded && CUP.nrun == 0,
          "cupel: no runs yet is none: %s", LINK.err);
    CHECK(cup_spring(&CUP) && cupel_load_runs(&CUP) == ZXM_OK &&
              cupel_run(&CUP, 0, "none", &r) == ZXM_E_ARG &&
              cupel_run(&CUP, 10, "", &r) == ZXM_E_ARG,
          "cupel: a run of no steps, or with no note, is refused");
    CHECK(cupel_run(&CUP, 200, "spring A", &r) == ZXM_OK && r.run == 0 && r.model == 1 &&
              r.steps == 200 && !r.halted && r.drift <= 1000 && r.score >= 0 && r.score <= 100 &&
              r.score == cupel_score(r.drift, 1000, false) && r.work == 600 && r.ms >= 0,
          "cupel: 200 steps, energy %lld, drift %lld, score %lld", (long long) r.energy,
          (long long) r.drift, (long long) r.score);
    before = sent;
    CHECK(cupel_record(&CUP, &r, false) == ZXM_E_STATE && sent == before && CUP.nrun == 0,
          "cupel: no owner's word, no record, nothing sent");
    CHECK(cupel_record(&CUP, &r, true) == ZXM_OK && CUP.nrun == 1, "cupel: recorded: %s", LINK.err);
    CHECK(cupel_record(&CUP, &r, true) == ZXM_E_ARG && CUP.nrun == 1,
          "cupel: the same run twice is refused");
    {
        int64_t first = r.score;
        CHECK(cupel_set_model(&CUP, 2, "spring B") == ZXM_OK &&
                  cupel_run(&CUP, 50, "spring B", &r) == ZXM_OK && r.run == 1 && r.model == 2 &&
                  cupel_record(&CUP, &r, true) == ZXM_OK,
              "cupel: a second model's run: %s", LINK.err);
        cupel_init(&CUP2, &LINK);
        CHECK(cupel_load_runs(&CUP2) == ZXM_OK && CUP2.nrun == 2 && CUP2.run[0].steps == 200 &&
                  CUP2.run[0].score == first && !strcmp(CUP2.run[0].note, "spring A") &&
                  CUP2.run[1].model == 2 && CUP2.run[1].energy == r.energy,
              "cupel: the runs read back from the Hoard: %s", LINK.err);
        CHECK(cupel_model_total(&CUP2, 1) == first && cupel_model_total(&CUP2, 2) == r.score &&
                  cupel_model_total(&CUP2, 9) == 0,
              "cupel: each model's score over its runs (retort.model_score_total)");
    }
    CHECK(cup_write(CUPEL_RUNS_FILE, "cupel-runs 1\nr 0 1 5 10 2 0 101 0 15 x\n") &&
              cupel_load_runs(&CUP2) == ZXM_E_ARG && CUP2.nrun == 2,
          "cupel: a run scored past 100 is no record; the runs read before are kept");

    /* CM8, the BOTH test: model A's energy after 1000 steps is 10000 units,
     * model B's 10450, with a tolerance of 100: BOTH, and both are shown */
    CHECK(cupel_compare(10000, 10450, 100, shown) == LPRES_STATE_BOTH && shown[0] == 10000 &&
              shown[1] == 10450,
          "cupel: 10000 and 10450, tolerance 100: BOTH, both shown");
    CHECK(cupel_compare(10000, 10090, 100, shown) == LPRES_STATE_TRUE &&
              cupel_compare(10000, 10450, -1, shown) == LPRES_STATE_NEITHER && shown[1] == 10450,
          "cupel: within the tolerance TRUE; no tolerance NEITHER, still shown");
}
