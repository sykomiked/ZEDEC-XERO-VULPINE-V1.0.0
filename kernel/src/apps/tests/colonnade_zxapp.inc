/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Colonnade's languages against the C its model runs (colonnade_rules.h).
 * Slots: @logic.colonnade_witness_split, @num.colonnade_fuel_left,
 * @num.colonnade_pillar_runs, @num.colonnade_pillar_time_ms,
 * @logic.colonnade_play_limits_ok, @logic.colonnade_pillar_index_ok, and
 * the share of the output limit a run used, @num.used_x100. */
#include "colonnade_rules.h"

/* one witnessed call that must be refused: no rows answered */
static bool clt_refused(zxapp_t *a, const char *entry, const zxapp_arg_t *args, uint32_t nargs)
{
    zxapp_num_t out[WAVE_ROWS];
    uint32_t n = 0;
    int32_t lp = -1;
    return zxapp_call_witnessed(a, &H, entry, args, nargs, out, WAVE_ROWS, &n, &lp) != ZXAPP_OK;
}

APP_TEST(colonnade, 10, 127)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], want[WAVE_ROWS];

    /* CL1 in C: the runs' verdicts joined with lpres_kjoin */
    {
        static const int64_t agree[3] = {LPRES_STATE_TRUE, LPRES_STATE_TRUE, LPRES_STATE_NEITHER};
        static const int64_t split[3] = {LPRES_STATE_TRUE, LPRES_STATE_NEITHER, LPRES_STATE_FALSE};
        static const int64_t no[2] = {LPRES_STATE_FALSE, LPRES_STATE_FALSE};
        static const int64_t bad[2] = {LPRES_STATE_TRUE, 4};
        lpres_state_t s1 = LPRES_STATE_TRUE, s2 = s1, s3 = s1, s4 = s1, s5 = s1;
        CHECK(colonnade_verdict_join(agree, 3, &s1) && s1 == LPRES_STATE_TRUE &&
                  colonnade_verdict_join(split, 3, &s2) && s2 == LPRES_STATE_BOTH &&
                  colonnade_verdict_join(no, 2, &s3) && s3 == LPRES_STATE_FALSE &&
                  colonnade_verdict_join(agree, 0, &s4) && s4 == LPRES_STATE_NEITHER &&
                  !colonnade_verdict_join(bad, 2, &s5) && s5 == LPRES_STATE_TRUE &&
                  !colonnade_verdict_join(NULL, 2, &s5),
              "Colonnade: the runs' verdicts joined (CL1)");
        for (int64_t t = 0; t <= 1; t++)
            for (int64_t f = 0; f <= 1; f++) {
                int64_t v[2] = {t ? LPRES_STATE_TRUE : LPRES_STATE_NEITHER,
                                f ? LPRES_STATE_FALSE : LPRES_STATE_NEITHER};
                lpres_state_t s = LPRES_STATE_NEITHER;
                CHECK(colonnade_verdict_join(v, 2, &s) &&
                          (s == LPRES_STATE_BOTH) == colonnade_witness_split(t, f),
                      "Colonnade: witness_split is the join's BOTH (%lld %lld)", (long long) t,
                      (long long) f);
            }
    }

    /* witness_split (CL1): Eve = KRON = C, every flag pair, then swept */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i < 4 ? (i >> 1) & 1 : wrand(0, 1);
        b[i] = i < 4 ? i & 1 : wrand(0, 1);
        want[i] = T_(colonnade_witness_split(a[i], b[i]));
    }
    CHECK(colonnade_witness_split(1, 1) && !colonnade_witness_split(1, 0) &&
              !colonnade_witness_split(0, 1) && !colonnade_witness_split(0, 0) &&
              !colonnade_witness_split(2, 1) && !colonnade_witness_split(1, -1),
          "Colonnade: the C rule's own edges (CL1)");
    zxapp_arg_t sa[2] = {{"sawtrue", a, WAVE_ROWS}, {"sawfalse", b, WAVE_ROWS}};
    CHECK(wave_same(app, "witness_split", sa, 2, want, WAVE_ROWS, true),
          "Colonnade: witness_split, Eve = KRON = C");
    {
        /* a flag of 2 or -1 is no flag: Eve's 1 .. 1 refuses it, KRON reads above 0 as set */
        static const int64_t one[2] = {1, 1}, two[2] = {1, 2};
        zxapp_arg_t xa[2] = {{"sawtrue", two, 2}, {"sawfalse", one, 2}};
        CHECK(wave_lp(app, "witness_split", xa, 2) == ZXAPP_BOTH,
              "Colonnade: sawtrue 2: Eve FALSE, KRON TRUE: BOTH");
        zxapp_arg_t ya[2] = {{"sawtrue", one, 2}, {"sawfalse", two, 2}};
        CHECK(wave_lp(app, "witness_split", ya, 2) == ZXAPP_BOTH,
              "Colonnade: sawfalse 2: Eve FALSE, KRON TRUE: BOTH");
    }

    /* fuel_left (CL2): Sutra = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 999999999);
        b[i] = wrand(0, a[i]);
    }
    a[0] = 0, b[0] = 0;                                   /* nothing at all */
    a[1] = COLONNADE_FUEL_MAX, b[1] = 0;                  /* the most, none spent */
    a[2] = COLONNADE_FUEL_MAX, b[2] = COLONNADE_FUEL_MAX; /* the most, all spent */
    a[3] = 10, b[3] = 10;                                 /* exactly spent */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = colonnade_fuel_left(a[i], b[i]);
    CHECK(want[0] == 0 && want[1] == COLONNADE_FUEL_MAX && want[2] == 0 && want[3] == 0 &&
              colonnade_fuel_left(10, 11) == COLONNADE_REFUSED &&
              colonnade_fuel_left(-1, 0) == COLONNADE_REFUSED &&
              colonnade_fuel_left(10, -1) == COLONNADE_REFUSED &&
              colonnade_fuel_left(COLONNADE_FUEL_MAX + 1, 0) == COLONNADE_REFUSED,
          "Colonnade: the C rule's own edges (CL2)");
    zxapp_arg_t fa[2] = {{"fuelmax", a, WAVE_ROWS}, {"spent", b, WAVE_ROWS}};
    CHECK(wave_same(app, "fuel_left", fa, 2, want, WAVE_ROWS, false),
          "Colonnade: fuel_left, Sutra = KRON = C");
    {
        /* spent past fuelmax, max + 1 and a negative: Sutra refuses, KRON answers */
        static const int64_t ten[2] = {10, 10}, over[2] = {3, 11}, three[2] = {3, 3};
        static const int64_t big[2] = {10, COLONNADE_FUEL_MAX + 1}, negs[2] = {3, -1};
        zxapp_arg_t p1[2] = {{"fuelmax", ten, 2}, {"spent", over, 2}};
        zxapp_arg_t p2[2] = {{"fuelmax", big, 2}, {"spent", three, 2}};
        zxapp_arg_t p3[2] = {{"fuelmax", ten, 2}, {"spent", negs, 2}};
        CHECK(wave_lp(app, "fuel_left", p1, 2) == ZXAPP_BOTH,
              "Colonnade: 11 spent of 10: Sutra refuses, KRON answers -1: BOTH");
        CHECK(wave_lp(app, "fuel_left", p2, 2) == ZXAPP_BOTH,
              "Colonnade: nineteen digits of fuel: Sutra refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "fuel_left", p3, 2) == ZXAPP_BOTH,
              "Colonnade: -1 spent: Sutra refuses, KRON answers: BOTH");
    }

    /* pillar_runs and pillar_time_ms (CL3): Trebago = C, one total per pillar */
    for (int64_t p = COLONNADE_LINGUA; p < COLONNADE_PILLARS; p++) {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = wrand(COLONNADE_LINGUA, COLONNADE_MYRA);
            c[i] = wrand(0, COLONNADE_MS_MAX);
        }
        a[0] = p, c[0] = COLONNADE_MS_MAX, c[1] = 0; /* the longest and the shortest run */
        int64_t runs = colonnade_pillar_runs(a, p, WAVE_ROWS);
        int64_t ms = colonnade_pillar_time_ms(a, p, c, WAVE_ROWS);
        CHECK(runs >= 1 && ms >= COLONNADE_MS_MAX, "Colonnade: pillar %lld's C totals",
              (long long) p);
        zxapp_arg_t ra[2] = {{"pillar", a, WAVE_ROWS}, {"want", &p, 1}};
        zxapp_arg_t ta[3] = {{"pillar", a, WAVE_ROWS}, {"want", &p, 1}, {"ms", c, WAVE_ROWS}};
        CHECK(wave_same(app, "pillar_runs", ra, 2, &runs, 1, false),
              "Colonnade: pillar_runs of pillar %lld, Trebago = C", (long long) p);
        CHECK(wave_same(app, "pillar_time_ms", ta, 3, &ms, 1, false),
              "Colonnade: pillar_time_ms of pillar %lld, Trebago = C", (long long) p);
    }
    {
        static const int64_t pl[2] = {1, 6}, pn[2] = {-1, 1}, ok[2] = {1, 1};
        static const int64_t long_[2] = {5, COLONNADE_MS_MAX + 1}, negms[2] = {-1, 5};
        static const int64_t five[2] = {5, 5};
        int64_t one = 1;
        CHECK(colonnade_pillar_runs(pl, 1, 2) == COLONNADE_REFUSED &&
                  colonnade_pillar_runs(pn, 1, 2) == COLONNADE_REFUSED &&
                  colonnade_pillar_runs(ok, 1, 0) == 0 &&
                  colonnade_pillar_time_ms(ok, 1, long_, 2) == COLONNADE_REFUSED &&
                  colonnade_pillar_time_ms(ok, 1, negms, 2) == COLONNADE_REFUSED &&
                  colonnade_pillar_time_ms(pl, 1, five, 2) == COLONNADE_REFUSED,
              "Colonnade: the C rule's own edges (CL3)");
        zxapp_arg_t r1[2] = {{"pillar", pl, 2}, {"want", &one, 1}};
        zxapp_arg_t r2[2] = {{"pillar", pn, 2}, {"want", &one, 1}};
        zxapp_arg_t t1[3] = {{"pillar", ok, 2}, {"want", &one, 1}, {"ms", long_, 2}};
        zxapp_arg_t t2[3] = {{"pillar", ok, 2}, {"want", &one, 1}, {"ms", negms, 2}};
        zxapp_arg_t t3[3] = {{"pillar", pl, 2}, {"want", &one, 1}, {"ms", five, 2}};
        CHECK(clt_refused(app, "pillar_runs", r1, 2) && clt_refused(app, "pillar_runs", r2, 2),
              "Colonnade: a pillar 6 or -1: Trebago refuses the count");
        CHECK(clt_refused(app, "pillar_time_ms", t1, 3) &&
                  clt_refused(app, "pillar_time_ms", t2, 3) &&
                  clt_refused(app, "pillar_time_ms", t3, 3),
              "Colonnade: 60001 ms, -1 ms or a pillar 6: Trebago refuses the total");
    }

    /* play_limits_ok (CL4): Eve = C, every edge, then swept */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-2, COLONNADE_OUT_MAX + 2);
        b[i] = wrand(-2, COLONNADE_MS_MAX + 2);
    }
    a[0] = 0, b[0] = 0;
    a[1] = COLONNADE_OUT_MAX, b[1] = COLONNADE_MS_MAX;
    a[2] = COLONNADE_OUT_MAX + 1, b[2] = 0;
    a[3] = 0, b[3] = COLONNADE_MS_MAX + 1;
    a[4] = -1, b[4] = 0;
    a[5] = 0, b[5] = -1;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(colonnade_play_limits_ok(a[i], b[i]));
    CHECK(colonnade_play_limits_ok(0, 0) && colonnade_play_limits_ok(49152, 60000) &&
              !colonnade_play_limits_ok(49153, 0) && !colonnade_play_limits_ok(0, 60001) &&
              !colonnade_play_limits_ok(-1, 0) && !colonnade_play_limits_ok(0, -1),
          "Colonnade: the C rule's own edges (CL4)");
    zxapp_arg_t la[2] = {{"outbytes", a, WAVE_ROWS}, {"ms", b, WAVE_ROWS}};
    CHECK(wave_same(app, "play_limits_ok", la, 2, want, WAVE_ROWS, true),
          "Colonnade: play_limits_ok, Eve = C");

    /* pillar_index_ok (CL5): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1200, 1200);
        b[i] = wrand(-12, 12);
    }
    a[0] = 1, b[0] = COLONNADE_LINGUA;
    a[1] = 108, b[1] = COLONNADE_MYRA;
    a[2] = 999, b[2] = 9;
    a[3] = 1000, b[3] = 1;
    a[4] = 9, b[4] = 10;
    a[5] = -999, b[5] = -9;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(colonnade_pillar_index_fits(a[i], b[i]));
    CHECK(colonnade_pillar_index_fits(999, 9) && !colonnade_pillar_index_fits(1000, 1) &&
              !colonnade_pillar_index_fits(1, 10) && colonnade_pillar_index_fits(-999, -9) &&
              !colonnade_pillar_index_fits(-1000, 0),
          "Colonnade: the C rule's own edges (CL5)");
    zxapp_arg_t ia[2] = {{"lang", a, WAVE_ROWS}, {"pillar", b, WAVE_ROWS}};
    CHECK(wave_same(app, "pillar_index_ok", ia, 2, want, WAVE_ROWS, true),
          "Colonnade: pillar_index_ok, MYRA = C");

    /* the output share (CL6), through the core's used_share: Sutra = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = COLONNADE_OUT_MAX;
        b[i] = wrand(0, COLONNADE_OUT_MAX);
        want[i] = colonnade_used_x100(a[i], b[i]);
    }
    b[0] = COLONNADE_OUT_MAX, b[1] = 0;
    want[0] = colonnade_used_x100(a[0], b[0]);
    want[1] = colonnade_used_x100(a[1], b[1]);
    CHECK(want[0] == 0 && want[1] == 10000 && colonnade_used_x100(0, 0) == COLONNADE_REFUSED &&
              colonnade_used_x100(10, 11) == COLONNADE_REFUSED,
          "Colonnade: the C rule's own edges (CL6)");
    zxapp_arg_t ua[2] = {{"total", a, WAVE_ROWS}, {"left", b, WAVE_ROWS}};
    CHECK(wave_same(app, "used_share.used_x100", ua, 2, want, WAVE_ROWS, false),
          "Colonnade: the output share, used_share.used_x100 = C");
}
