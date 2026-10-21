/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Crown's languages against the C its model runs (crown_rules.h).
 * Slots: @logic.crown_standing, @num.crown_allot, @num.crown_draw_at,
 * @num.crown_pool_after. */
#include "crown_rules.h"
#include "gatehouse_rules.h"

static const char *const CROWN_FLAG[CROWN_REASONS] = {"uptime", "dropped", "routing", "audit",
                                                      "fraud",  "volume",  "stake",   "invalid"};

APP_TEST(crown, 19, 75)
{
    static int64_t f[CROWN_REASONS][WAVE_ROWS], a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS],
        d[WAVE_ROWS], want[WAVE_ROWS];

    /* standing (CW1): Eve = Sutra = C, every mask grants.eval can answer */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        uint32_t mask = i < 8 ? 1u << i : (i % 3 ? 0u : (uint32_t) wrand(1, 255));
        if (i == 8) mask = 0xFFu; /* every reason */
        int64_t row[CROWN_REASONS];
        crown_reason_flags(mask, row);
        for (uint32_t k = 0; k < CROWN_REASONS; k++) f[k][i] = row[k];
        want[i] = T_(crown_standing(row));
    }
    CHECK(want[0] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE && want[8] == ZXAPP_FALSE &&
              want[10] == ZXAPP_TRUE,
          "Crown: standing, the C's own edges");
    zxapp_arg_t sa[CROWN_REASONS];
    for (uint32_t k = 0; k < CROWN_REASONS; k++)
        sa[k] = (zxapp_arg_t){CROWN_FLAG[k], f[k], WAVE_ROWS};
    CHECK(wave_same(app, "standing", sa, CROWN_REASONS, want, WAVE_ROWS, true),
          "Crown: standing, Eve = Sutra = C");
    {
        /* a flag below 0 is no flag: Sutra refuses it, Eve says no: held */
        static const int64_t z[2] = {0, 0}, neg[2] = {0, -1}, two[2] = {0, 2};
        zxapp_arg_t na[CROWN_REASONS], ta[CROWN_REASONS];
        for (uint32_t k = 0; k < CROWN_REASONS; k++) {
            na[k] = (zxapp_arg_t){CROWN_FLAG[k], k == 4 ? neg : z, 2};
            ta[k] = (zxapp_arg_t){CROWN_FLAG[k], k == 7 ? two : z, 2};
        }
        int64_t row[CROWN_REASONS] = {0, 0, 0, 0, -1, 0, 0, 0};
        CHECK(!crown_standing(row) && wave_lp(app, "standing", na, CROWN_REASONS) == ZXAPP_BOTH,
              "Crown: a fraud flag of -1: Sutra refuses, Eve FALSE: BOTH");
        CHECK(wave_lp(app, "standing", ta, CROWN_REASONS) == ZXAPP_TRUE,
              "Crown: an invalid flag of 2: both say no: TRUE");
    }

    /* allot (CW2): Sutra = Trebago = C, the units and weights swept */
    static const int64_t units_of[4] = {0, 7, 1000, CROWN_NINE};
    for (uint32_t u = 0; u < 4; u++) {
        for (uint32_t i = 0; i < WAVE_ROWS; i++)
            a[i] = u == 3 ? wrand(0, CROWN_NINE / WAVE_ROWS) : wrand(0, i % 2 ? 9 : 1000000);
        if (u == 1) a[0] = 0; /* a recipient with no weight */
        int64_t units = units_of[u], total = 0;
        CHECK(crown_allot(units, a, WAVE_ROWS, want, b, &total) == 0, "Crown: allot %u in range",
              u);
        int64_t sum = 0;
        for (uint32_t i = 0; i < WAVE_ROWS; i++) sum += want[i];
        CHECK(sum == units, "Crown: allot %u sums to the units exactly", u);
        int64_t un[1] = {units}, tn[1] = {total};
        zxapp_arg_t aa[4] = {
            {"units", un, 1}, {"weight", a, WAVE_ROWS}, {"total", tn, 1}, {"extra", b, WAVE_ROWS}};
        CHECK(wave_same(app, "allot", aa, 4, want, WAVE_ROWS, false),
              "Crown: allot of %lld, Sutra = Trebago = C", (long long) units);
    }
    {
        /* the largest remainder and its tie, as grants.h G3 gives them */
        static const int64_t w[3] = {5, 3, 2}, tie[4] = {1, 1, 1, 1},
                             top[3] = {333333333, 333333333, 333333332};
        int64_t s[4], e[4], t = 0;
        CHECK(crown_allot(7, w, 3, s, e, &t) == 0 && s[0] == 4 && s[1] == 2 && s[2] == 1 && t == 10,
              "Crown: 7 by 5:3:2 is 4, 2, 1");
        CHECK(crown_allot(2, tie, 4, s, e, &t) == 0 && s[0] == 1 && s[1] == 1 && s[2] == 0 &&
                  s[3] == 0,
              "Crown: a tie goes to the earlier recipient");
        CHECK(crown_allot(CROWN_NINE, top, 3, s, e, &t) == 0 && s[0] == 333333334 &&
                  s[1] == 333333333 && s[2] == 333333332 && e[0] == 1 && t == CROWN_NINE - 1,
              "Crown: 9(9) units, weights summing to 9(9) - 1");
        static const int64_t zw[2] = {0, 0}, nw[2] = {1, -1}, bw[2] = {CROWN_NINE + 1, 1};
        CHECK(crown_allot(5, zw, 2, s, e, &t) == CROWN_REFUSED &&
                  crown_allot(5, nw, 2, s, e, &t) == CROWN_REFUSED &&
                  crown_allot(CROWN_NINE + 1, w, 3, s, e, &t) == CROWN_REFUSED &&
                  crown_allot(-1, w, 3, s, e, &t) == CROWN_REFUSED &&
                  crown_allot(10, bw, 2, s, e, &t) == CROWN_REFUSED,
              "Crown: allot refuses no weight, a negative, and 9(9) + 1");
        /* a weight past 9(9): both refuse: NEITHER */
        static const int64_t un[1] = {10}, tn[1] = {CROWN_NINE + 2}, ex[2] = {1, 0};
        zxapp_arg_t ba[4] = {
            {"units", un, 1}, {"weight", bw, 2}, {"total", tn, 1}, {"extra", ex, 2}};
        CHECK(wave_lp(app, "allot", ba, 4) == ZXAPP_NEITHER,
              "Crown: weights past 9(9): both refuse: NEITHER");
        /* a total that is not the weights' sum: Sutra answers with it, Trebago refuses: held */
        static const int64_t t3[1] = {9}, t10[1] = {10}, e3[3] = {1, 0, 0}, e0[3] = {0, 0, 1},
                             u7[1] = {7};
        zxapp_arg_t ta[4] = {
            {"units", u7, 1}, {"weight", w, 3}, {"total", t3, 1}, {"extra", e3, 3}};
        CHECK(wave_lp(app, "allot", ta, 4) == ZXAPP_BOTH,
              "Crown: a wrong total: Sutra answers, Trebago refuses: BOTH");
        /* the leftover given to the wrong recipient: the same */
        zxapp_arg_t xa[4] = {
            {"units", u7, 1}, {"weight", w, 3}, {"total", t10, 1}, {"extra", e0, 3}};
        CHECK(wave_lp(app, "allot", xa, 4) == ZXAPP_BOTH,
              "Crown: a wrong extra: Sutra answers, Trebago refuses: BOTH");
        /* no weight at all: both divide by zero: NEITHER */
        static const int64_t t0[1] = {0}, z2[2] = {0, 0};
        zxapp_arg_t za[4] = {
            {"units", u7, 1}, {"weight", zw, 2}, {"total", t0, 1}, {"extra", z2, 2}};
        CHECK(wave_lp(app, "allot", za, 4) == ZXAPP_NEITHER,
              "Crown: no weight: both refuse: NEITHER");
    }

    /* draw_at (CW3): Trebago = Sutra = C, swept, then one whole order */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, CROWN_NINE);
        b[i] = wrand(1, i % 2 ? 64 : CROWN_MEMBERS_MAX);
        c[i] = wrand(0, b[i] - 1);
    }
    a[0] = 0, b[0] = 1, c[0] = 0;                                              /* one member */
    a[1] = CROWN_NINE, b[1] = CROWN_MEMBERS_MAX, c[1] = CROWN_MEMBERS_MAX - 1; /* every max */
    a[2] = 42, b[2] = 5, c[2] = 4;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = crown_draw_at(a[i], b[i], c[i]);
    CHECK(want[0] == 0 && want[2] == 4 && want[1] >= 0, "Crown: draw_at, the C's edges");
    zxapp_arg_t da[3] = {{"seed", a, WAVE_ROWS}, {"members", b, WAVE_ROWS}, {"step", c, WAVE_ROWS}};
    CHECK(wave_same(app, "draw_at", da, 3, want, WAVE_ROWS, false),
          "Crown: draw_at, Trebago = Sutra = C");
    {
        bool seen[WAVE_ROWS] = {false}, once = true;
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = 20261021, b[i] = WAVE_ROWS, c[i] = i;
            want[i] = crown_draw_at(a[i], b[i], c[i]);
            if (want[i] < 0 || seen[want[i]]) once = false;
            if (want[i] >= 0) seen[want[i]] = true;
        }
        CHECK(once, "Crown: one seed's order reaches every member once");
        CHECK(wave_same(app, "draw_at", da, 3, want, WAVE_ROWS, false),
              "Crown: the whole order, Trebago = Sutra = C");
        CHECK(crown_draw_at(CROWN_NINE + 1, 5, 0) == CROWN_REFUSED &&
                  crown_draw_at(1, 0, 0) == CROWN_REFUSED &&
                  crown_draw_at(1, 5, 5) == CROWN_REFUSED &&
                  crown_draw_at(1, CROWN_MEMBERS_MAX + 1, 0) == CROWN_REFUSED &&
                  crown_draw_at(-1, 5, 0) == CROWN_REFUSED,
              "Crown: draw_at refuses a seed past 9(9), no members, a step past the last");
        /* a published seed of 10 digits: Sutra refuses, Trebago answers: held */
        static const int64_t s[2] = {42, 1000000000}, m[2] = {5, 5}, k[2] = {0, 0};
        zxapp_arg_t xa[3] = {{"seed", s, 2}, {"members", m, 2}, {"step", k, 2}};
        CHECK(wave_lp(app, "draw_at", xa, 3) == ZXAPP_BOTH,
              "Crown: a seed past 9(9): Sutra refuses, Trebago answers: BOTH");
        /* a step past the last member: both refuse: NEITHER */
        static const int64_t k5[2] = {0, 5};
        zxapp_arg_t ya[3] = {{"seed", s, 1}, {"members", m, 2}, {"step", k5, 2}};
        ya[0].v = m;
        CHECK(wave_lp(app, "draw_at", ya, 3) == ZXAPP_NEITHER,
              "Crown: a step past the last member: both refuse: NEITHER");
    }

    /* pool_after (CW4): Trebago = C (one language: NEITHER) */
    {
        int64_t p = 0;
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = wrand(0, 1000000);
            p += a[i];
            b[i] = i % 4 == 3 ? p : wrand(0, p); /* an epoch may grant the whole pool */
            p -= b[i];
        }
        int64_t op[1] = {0};
        CHECK(crown_pool_after(0, a, b, WAVE_ROWS, want) == 0 && want[3] == 0,
              "Crown: pool_after in range");
        zxapp_arg_t pa[3] = {
            {"opening", op, 1}, {"inflow", a, WAVE_ROWS}, {"granted", b, WAVE_ROWS}};
        CHECK(wave_same(app, "pool_after", pa, 3, want, WAVE_ROWS, false),
              "Crown: pool_after, Trebago = C");
        static const int64_t o1[1] = {100}, in[2] = {50, 0}, gr[2] = {160, 0};
        zxapp_arg_t over[3] = {{"opening", o1, 1}, {"inflow", in, 2}, {"granted", gr, 2}};
        int64_t out[2];
        CHECK(crown_pool_after(100, in, gr, 2, out) == CROWN_REFUSED &&
                  wave_lp(app, "pool_after", over, 3) != ZXAPP_TRUE,
              "Crown: a grant past the pool is refused");
    }

    /* the parts Crown uses: a grant's cost (Vintner) and leave to post (Gatehouse) */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, CROWN_NINE);
        b[i] = wrand(1, 100000);
        want[i] = a[i] * b[i];
        c[i] = wrand(0, 1), d[i] = wrand(0, 1);
    }
    zxapp_arg_t va[2] = {{"bottles", a, WAVE_ROWS}, {"price", b, WAVE_ROWS}};
    CHECK(wave_same(app, "vintner.sale_value", va, 2, want, WAVE_ROWS, false),
          "Crown: a grant's cost through vintner.sale_value");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = 1;
        want[i] = T_(gatehouse_use_ok(1, c[i], d[i]));
    }
    zxapp_arg_t ga[3] = {
        {"asked", a, WAVE_ROWS}, {"granted", c, WAVE_ROWS}, {"revoked", d, WAVE_ROWS}};
    CHECK(wave_same(app, "gatehouse.use_ok", ga, 3, want, WAVE_ROWS, true),
          "Crown: leave to post grants through gatehouse.use_ok");
}
