/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Retort's languages against the C its model runs (retort_rules.h).
 * Slots: @num.retort_model_score_total, @num.retort_latency_fastest_ms,
 * @num.retort_latency_slowest_ms, @logic.retort_run_limits_ok,
 * @num.retort_energy_per_answer_uj, @logic.retort_run_record_ok, and
 * @num.banker_share, each contestant's share of the points. */
#include "retort_rules.h"

/* one witnessed call that must be refused: no rows answered */
static bool rtt_refused(zxapp_t *a, const char *entry, const zxapp_arg_t *args, uint32_t nargs)
{
    zxapp_num_t out[WAVE_ROWS];
    uint32_t n = 0;
    int32_t lp = -1;
    return zxapp_call_witnessed(a, &H, entry, args, nargs, out, WAVE_ROWS, &n, &lp) != ZXAPP_OK;
}

/* energy_per_answer_uj over rows: every answer the C's reduced fraction */
static bool rtt_energy(zxapp_t *a, const int64_t *uj, const int64_t *answers, uint32_t rows)
{
    zxapp_arg_t ea[2] = {{"uj", uj, rows}, {"answers", answers, rows}};
    zxapp_num_t o[WAVE_ROWS];
    uint32_t k = 0;
    int32_t lp = -1;
    int rc = zxapp_call_witnessed(a, &H, "energy_per_answer_uj", ea, 2, o, WAVE_ROWS, &k, &lp);
    if (rc != ZXAPP_OK || k != rows) {
        printf("    energy_per_answer_uj: rc %d n %u (%s)\n", rc, k, a->err);
        return false;
    }
    for (uint32_t i = 0; i < rows; i++) {
        int64_t num = 0, den = 0;
        if (!retort_energy_per_answer(uj[i], answers[i], &num, &den) || o[i].num != num ||
            o[i].den != den) {
            printf("    energy row %u: %lld/%lld, want %lld/%lld\n", i, (long long) o[i].num,
                   (long long) o[i].den, (long long) num, (long long) den);
            return false;
        }
    }
    return true;
}

APP_TEST(retort, 13, 70)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], g[WAVE_ROWS], h[WAVE_ROWS], want[WAVE_ROWS];

    /* model_score_total (RT1): Trebago = KRON = C, one total per contestant */
    for (uint32_t r = 0; r < 8u; r++) {
        int64_t who = (int64_t) r;
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = wrand(0, 7);
            b[i] = wrand(0, RETORT_SCORE_MAX);
        }
        b[0] = 0, b[1] = RETORT_SCORE_MAX, a[1] = who; /* the least and the most score */
        want[0] = retort_model_score_total(a, who, b, WAVE_ROWS);
        CHECK(want[0] >= RETORT_SCORE_MAX, "Retort: contestant %u's C total", r);
        zxapp_arg_t sa[3] = {{"model", a, WAVE_ROWS}, {"want", &who, 1}, {"score", b, WAVE_ROWS}};
        CHECK(wave_same(app, "model_score_total", sa, 3, want, 1, false),
              "Retort: model_score_total of contestant %u, Trebago = KRON = C", r);
    }
    {
        static const int64_t m[5] = {1, 2, 1, 3, 1}, s[5] = {50, 70, 100, 0, 0};
        static const int64_t m2[2] = {4, 4}, s2[2] = {5, 6};
        static const int64_t over[2] = {50, 101}, neg[2] = {-1, 5}, m3[2] = {1, 2};
        static const int64_t one = 1, nine = 9;
        CHECK(retort_model_score_total(m, 1, s, 5) == 150 &&
                  retort_model_score_total(m2, 9, s2, 2) == 0 &&
                  retort_model_score_total(m3, 1, over, 2) == RETORT_REFUSED &&
                  retort_model_score_total(m3, 1, neg, 2) == RETORT_REFUSED &&
                  retort_model_score_total(NULL, 1, s, 5) == RETORT_REFUSED,
              "Retort: the C rule's own edges (RT1)");
        int64_t w150 = 150, w0 = 0;
        zxapp_arg_t xa[3] = {{"model", m, 5}, {"want", &one, 1}, {"score", s, 5}};
        zxapp_arg_t za[3] = {{"model", m2, 2}, {"want", &nine, 1}, {"score", s2, 2}};
        CHECK(wave_same(app, "model_score_total", xa, 3, &w150, 1, false) &&
                  wave_same(app, "model_score_total", za, 3, &w0, 1, false),
              "Retort: 150 points for contestant 1, none for 9");
        /* max + 1 and a negative: Trebago refuses the whole total, KRON adds it */
        zxapp_arg_t oa[3] = {{"model", m3, 2}, {"want", &one, 1}, {"score", over, 2}};
        zxapp_arg_t na[3] = {{"model", m3, 2}, {"want", &one, 1}, {"score", neg, 2}};
        CHECK(wave_lp(app, "model_score_total", oa, 3) == ZXAPP_BOTH,
              "Retort: a score of 101: Trebago refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "model_score_total", na, 3) == ZXAPP_BOTH,
              "Retort: a score of -1: Trebago refuses, KRON answers: BOTH");
    }

    /* latency_fastest_ms and latency_slowest_ms (RT2): wasm = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, RETORT_LATENCY_MAX);
        b[i] = i % 4 == 0 ? a[i] : wrand(0, RETORT_LATENCY_MAX);
    }
    a[0] = 0, b[0] = 0;                                   /* 0 on 0 */
    a[1] = RETORT_LATENCY_MAX, b[1] = RETORT_LATENCY_MAX; /* max on max */
    a[2] = RETORT_LATENCY_MAX, b[2] = 0;                  /* the fold's first step */
    a[3] = 0, b[3] = RETORT_LATENCY_MAX;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        want[i] = retort_latency_fastest(a[i], b[i]);
        c[i] = retort_latency_slowest(a[i], b[i]);
    }
    CHECK(want[0] == 0 && want[1] == RETORT_LATENCY_MAX && want[2] == 0 && want[3] == 0 &&
              c[2] == RETORT_LATENCY_MAX && c[3] == RETORT_LATENCY_MAX &&
              retort_latency_fastest(10, RETORT_LATENCY_MAX + 1) == RETORT_REFUSED &&
              retort_latency_fastest(-1, 5) == RETORT_REFUSED &&
              retort_latency_slowest(10, RETORT_LATENCY_MAX + 1) == RETORT_REFUSED &&
              retort_latency_slowest(-1, 5) == RETORT_REFUSED,
          "Retort: the C rule's own edges (RT2)");
    zxapp_arg_t fa[2] = {{"fastest", a, WAVE_ROWS}, {"ms", b, WAVE_ROWS}};
    zxapp_arg_t la[2] = {{"slowest", a, WAVE_ROWS}, {"ms", b, WAVE_ROWS}};
    CHECK(wave_same(app, "latency_fastest_ms", fa, 2, want, WAVE_ROWS, false),
          "Retort: latency_fastest_ms, wasm = KRON = C");
    CHECK(wave_same(app, "latency_slowest_ms", la, 2, c, WAVE_ROWS, false),
          "Retort: latency_slowest_ms, wasm = KRON = C");
    {
        /* max + 1 and a negative: the wasm module traps, KRON answers */
        static const int64_t ten[2] = {10, 10}, past[2] = {5, RETORT_LATENCY_MAX + 1},
                             negf[2] = {10, -1}, fives[2] = {5, 5};
        zxapp_arg_t p1[2] = {{"fastest", ten, 2}, {"ms", past, 2}};
        zxapp_arg_t p2[2] = {{"fastest", negf, 2}, {"ms", fives, 2}};
        zxapp_arg_t p3[2] = {{"slowest", ten, 2}, {"ms", past, 2}};
        zxapp_arg_t p4[2] = {{"slowest", negf, 2}, {"ms", fives, 2}};
        CHECK(wave_lp(app, "latency_fastest_ms", p1, 2) == ZXAPP_BOTH &&
                  wave_lp(app, "latency_fastest_ms", p2, 2) == ZXAPP_BOTH,
              "Retort: fastest past an hour or negative: wasm refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "latency_slowest_ms", p3, 2) == ZXAPP_BOTH &&
                  wave_lp(app, "latency_slowest_ms", p4, 2) == ZXAPP_BOTH,
              "Retort: slowest past an hour or negative: BOTH");
    }

    /* run_limits_ok (RT3): Eve = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-5, RETORT_PROMPT_MAX + 5);
        b[i] = wrand(-5, RETORT_FUEL_MAX);
        c[i] = wrand(-5, RETORT_SECONDS_MAX + 5);
    }
    a[0] = 1, b[0] = 1, c[0] = 1; /* every least */
    a[1] = RETORT_PROMPT_MAX, b[1] = RETORT_FUEL_MAX, c[1] = RETORT_SECONDS_MAX;
    a[2] = 0, b[2] = 1, c[2] = 1;                     /* no prompt */
    a[3] = RETORT_PROMPT_MAX + 1, b[3] = 1, c[3] = 1; /* prompt max + 1 */
    a[4] = 100, b[4] = 0, c[4] = 1;                   /* no fuel */
    a[5] = 100, b[5] = RETORT_FUEL_MAX + 1, c[5] = 1; /* fuel past 18 digits */
    a[6] = 100, b[6] = 5, c[6] = 0;                   /* no seconds */
    a[7] = 100, b[7] = 5, c[7] = RETORT_SECONDS_MAX + 1;
    a[8] = -1, b[8] = -1, c[8] = -1; /* negative */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(retort_run_limits_ok(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE && want[8] == ZXAPP_FALSE,
          "Retort: the C rule's own edges (RT3)");
    zxapp_arg_t ra[3] = {
        {"prompt", a, WAVE_ROWS}, {"fuel", b, WAVE_ROWS}, {"seconds", c, WAVE_ROWS}};
    CHECK(wave_same(app, "run_limits_ok", ra, 3, want, WAVE_ROWS, true),
          "Retort: run_limits_ok, Eve = C");

    /* energy_per_answer_uj (RT4): Trebago = C, exact fractions */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, RETORT_UJ_MAX);
        b[i] = wrand(1, RETORT_ANSWERS_MAX);
    }
    a[0] = 0, b[0] = 1;                              /* no energy */
    a[1] = RETORT_UJ_MAX, b[1] = 1;                  /* the most energy, one answer */
    a[2] = 10, b[2] = 3;                             /* 10/3 */
    a[3] = 1, b[3] = RETORT_ANSWERS_MAX;             /* the least share */
    a[4] = RETORT_UJ_MAX, b[4] = RETORT_ANSWERS_MAX; /* both at max */
    a[5] = 1000, b[5] = 4;                           /* 250 */
    CHECK(rtt_energy(app, a, b, WAVE_ROWS), "Retort: energy_per_answer_uj, Trebago = C");
    {
        int64_t n = 0, dn = 0;
        CHECK(retort_energy_per_answer(10, 3, &n, &dn) && n == 10 && dn == 3 &&
                  retort_energy_per_answer(1000, 4, &n, &dn) && n == 250 && dn == 1 &&
                  !retort_energy_per_answer(1, 0, &n, &dn) &&
                  !retort_energy_per_answer(-1, 1, &n, &dn) &&
                  !retort_energy_per_answer(RETORT_UJ_MAX + 1, 1, &n, &dn) &&
                  !retort_energy_per_answer(1, RETORT_ANSWERS_MAX + 1, &n, &dn),
              "Retort: the C rule's own edges (RT4)");
        static const int64_t u0[2] = {5, 5}, n0[2] = {1, 0}, un[2] = {5, -1}, n1[2] = {1, 1},
                             ub[2] = {5, RETORT_UJ_MAX + 1}, nb[2] = {1, RETORT_ANSWERS_MAX + 1};
        zxapp_arg_t z1[2] = {{"uj", u0, 2}, {"answers", n0, 2}};
        zxapp_arg_t z2[2] = {{"uj", un, 2}, {"answers", n1, 2}};
        zxapp_arg_t z3[2] = {{"uj", ub, 2}, {"answers", n1, 2}};
        zxapp_arg_t z4[2] = {{"uj", u0, 2}, {"answers", nb, 2}};
        CHECK(rtt_refused(app, "energy_per_answer_uj", z1, 2) &&
                  rtt_refused(app, "energy_per_answer_uj", z2, 2) &&
                  rtt_refused(app, "energy_per_answer_uj", z3, 2) &&
                  rtt_refused(app, "energy_per_answer_uj", z4, 2),
              "Retort: no answers, negative energy, energy or answers past range: refused");
    }

    /* run_record_ok (RT5): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 999), b[i] = wrand(0, 99), c[i] = wrand(0, 9), d[i] = wrand(0, 9999);
        e[i] = wrand(0, RETORT_FUEL_MAX), f[i] = wrand(0, 9999999), g[i] = wrand(-1, 100);
        h[i] = wrand(0, RETORT_UJ_MAX);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0, f[0] = 0, g[0] = 0, h[0] = 0;
    a[1] = 999, b[1] = 99, c[1] = 9, d[1] = 9999, e[1] = RETORT_FUEL_MAX, f[1] = 9999999;
    g[1] = 999, h[1] = RETORT_UJ_MAX;
    a[2] = 1000;              /* run max + 1 */
    b[3] = 100;               /* model max + 1 */
    c[4] = 10;                /* kind max + 1 */
    d[5] = 10000;             /* prompt max + 1 */
    f[6] = 10000000;          /* ms max + 1 */
    g[7] = 1000;              /* score max + 1 */
    h[8] = RETORT_UJ_MAX + 1; /* uj past 18 digits */
    e[9] = RETORT_FUEL_MAX + 1;
    g[10] = RETORT_UNGRADED; /* negative: sign aside */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(retort_run_record_fits(a[i], b[i], c[i], d[i], e[i], f[i], g[i], h[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE && want[8] == ZXAPP_FALSE &&
              want[9] == ZXAPP_FALSE && want[10] == ZXAPP_TRUE,
          "Retort: the record's own edges (RT5)");
    zxapp_arg_t ka[8] = {{"run", a, WAVE_ROWS},    {"model", b, WAVE_ROWS}, {"kind", c, WAVE_ROWS},
                         {"prompt", d, WAVE_ROWS}, {"fuel", e, WAVE_ROWS},  {"ms", f, WAVE_ROWS},
                         {"score", g, WAVE_ROWS},  {"uj", h, WAVE_ROWS}};
    CHECK(wave_same(app, "run_record_ok", ka, 8, want, WAVE_ROWS, true),
          "Retort: run_record_ok, MYRA = C");

    /* banker.share (RT6): each contestant's share of all the points given */
    for (uint32_t r = 0; r < 8u; r++) {
        static int64_t pts[8], share[8]; /* retort_model.h RETORT_CONTESTANTS */
        uint32_t n = 2u + r % 7u;
        int64_t whole = 0;
        for (uint32_t i = 0; i < n; i++) pts[i] = r == 0 ? 0 : wrand(0, 64 * RETORT_SCORE_MAX);
        for (uint32_t i = 0; i < n; i++) whole += pts[i];
        for (uint32_t i = 0; i < n; i++) share[i] = retort_share_x100(pts[i], whole);
        zxapp_arg_t sa[2] = {{"size", pts, n}, {"total", &whole, 1}};
        CHECK(wave_same(app, "banker.share", sa, 2, share, n, false),
              "Retort: round %u, each contestant's share through banker.share = C", r);
    }
    CHECK(retort_share_x100(150, 450) == 3333 && retort_share_x100(300, 450) == 6666 &&
              retort_share_x100(0, 0) == 0 && retort_share_x100(1, 0) == 10000 &&
              retort_share_x100(2, 0) == RETORT_REFUSED &&
              retort_share_x100(-1, 5) == RETORT_REFUSED,
          "Retort: the share's own edges (RT6)");
}
