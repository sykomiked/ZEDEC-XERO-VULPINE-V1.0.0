/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Twelvefold's languages against the C its model runs (twelvefold_rules.h).
 * Slots: @logic.twelvefold_lp_entail_kept, @logic.twelvefold_lp_limits_ok,
 * and Witness's @num.witness_kind_count, used as it is. */
#include "twelvefold_rules.h"

APP_TEST(twelvefold, 9, 132)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], want[WAVE_ROWS];

    /* lp_entail_kept (TR2): all sixteen pairs of codes, then swept; Sutra = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i < 16u ? (int64_t) (i / 4u) : wrand(0, 3);
        b[i] = i < 16u ? (int64_t) (i % 4u) : wrand(0, 3);
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(twelvefold_kept(a[i], b[i]) == 1);
    CHECK(want[1 * 4 + 0] == ZXAPP_FALSE && want[1 * 4 + 2] == ZXAPP_FALSE &&
              want[3 * 4 + 0] == ZXAPP_FALSE && want[3 * 4 + 2] == ZXAPP_FALSE &&
              want[1 * 4 + 1] == ZXAPP_TRUE && want[1 * 4 + 3] == ZXAPP_TRUE &&
              want[3 * 4 + 3] == ZXAPP_TRUE && want[0 * 4 + 2] == ZXAPP_TRUE &&
              want[2 * 4 + 0] == ZXAPP_TRUE && want[0] == ZXAPP_TRUE,
          "Twelvefold: the C rule's own table: only a designated premise with an "
          "undesignated conclusion breaks it (TR2)");
    CHECK(twelvefold_kept(4, 1) == TWELVEFOLD_REFUSED &&
              twelvefold_kept(1, 4) == TWELVEFOLD_REFUSED &&
              twelvefold_kept(-1, 0) == TWELVEFOLD_REFUSED &&
              twelvefold_kept(0, -1) == TWELVEFOLD_REFUSED &&
              twelvefold_kept(INT64_MAX, 1) == TWELVEFOLD_REFUSED && twelvefold_kept(3, 3) == 1 &&
              twelvefold_kept(0, 0) == 1,
          "Twelvefold: the C rule refuses a code outside 0..3 (TR2)");
    zxapp_arg_t ka[2] = {{"premise", a, WAVE_ROWS}, {"conclusion", b, WAVE_ROWS}};
    CHECK(wave_same(app, "lp_entail_kept", ka, 2, want, WAVE_ROWS, true),
          "Twelvefold: lp_entail_kept, Sutra = KRON = C");
    {
        /* a code past 3: Sutra refuses it, KRON reads it as undesignated */
        static const int64_t p[2] = {1, 4}, c[2] = {1, 1};
        zxapp_arg_t ba[2] = {{"premise", p, 2}, {"conclusion", c, 2}};
        CHECK(wave_lp(app, "lp_entail_kept", ba, 2) == ZXAPP_BOTH,
              "Twelvefold: premise code 4: Sutra refuses, KRON answers: BOTH");
        /* a negative code does not fit Sutra's 9 */
        static const int64_t n[2] = {1, -1}, z[2] = {1, 0};
        zxapp_arg_t na[2] = {{"premise", n, 2}, {"conclusion", z, 2}};
        CHECK(wave_lp(app, "lp_entail_kept", na, 2) == ZXAPP_BOTH,
              "Twelvefold: premise code -1: Sutra refuses, KRON answers: BOTH");
    }

    /* lp_limits_ok (TR3): edges, then swept; Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-2, 9);
        b[i] = wrand(-2, 20);
    }
    a[0] = 0, b[0] = 0;                           /* a constant: no variable, no connective */
    a[1] = 6, b[1] = 16;                          /* the most of both */
    a[2] = 7, b[2] = 0;                           /* one variable too many */
    a[3] = 0, b[3] = 17;                          /* one connective too deep */
    a[4] = -1, b[4] = 3;                          /* a negative count */
    a[5] = 3, b[5] = -1;                          /* a negative depth */
    a[6] = INT64_C(999999999999999999), b[6] = 1; /* far past */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(twelvefold_limits_ok(a[i], b[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE,
          "Twelvefold: the C limits' own edges (TR3)");
    zxapp_arg_t la[2] = {{"vars", a, WAVE_ROWS}, {"depth", b, WAVE_ROWS}};
    CHECK(wave_same(app, "lp_limits_ok", la, 2, want, WAVE_ROWS, true),
          "Twelvefold: lp_limits_ok, Eve = KRON = C");

    /* witness.kind_count, used as it is: the assignments giving each value
     * (TF4), and the assignments that break a consequence (TR4) */
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t v[64], wk[1];
        uint32_t n = r < 4u ? 64u : 4u << (2u * (r % 3u)); /* 4, 16 or 64 assignments */
        for (uint32_t i = 0; i < n; i++) v[i] = r == 0 ? 3 : wrand(0, 3);
        wk[0] = (int64_t) (r % 4u);
        int64_t c1 = witness_kind_count(v, n, wk[0]);
        zxapp_arg_t ca[2] = {{"kind", v, n}, {"want", wk, 1}};
        CHECK(wave_same(app, "witness.kind_count", ca, 2, &c1, 1, false),
              "Twelvefold: table %u, assignments giving %lld, KRON = Trebago = C", r,
              (long long) wk[0]);
    }
    for (uint32_t r = 0; r < 16u; r++) {
        int64_t kept[64], wf[1] = {TWELVEFOLD_CODE_F};
        for (uint32_t i = 0; i < 64u; i++) {
            int64_t p = r == 0 ? 2 : wrand(0, 3), c = wrand(0, 3);
            kept[i] = T_(twelvefold_kept(p, c) == 1);
        }
        int64_t breaks = twelvefold_breaks(kept, 64u);
        zxapp_arg_t fa[2] = {{"kind", kept, 64u}, {"want", wf, 1}};
        CHECK(wave_same(app, "witness.kind_count", fa, 2, &breaks, 1, false) && (r || !breaks),
              "Twelvefold: consequence %u, %lld assignments break it, KRON = Trebago = C", r,
              (long long) breaks);
    }
    {
        /* two values asked at once: Trebago's scalar refuses, KRON sums them */
        static const int64_t k2[2] = {1, 2}, wants[2] = {1, 1};
        zxapp_arg_t ba[2] = {{"kind", k2, 2}, {"want", wants, 2}};
        CHECK(wave_lp(app, "witness.kind_count", ba, 2) == ZXAPP_BOTH,
              "Twelvefold: two values at once: Trebago refuses, KRON answers: BOTH");
    }
}
