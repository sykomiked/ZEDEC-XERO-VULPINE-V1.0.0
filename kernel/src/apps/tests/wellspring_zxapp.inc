/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Wellspring's languages, witnessed, row by row against the rules of
 * wellspring_model.c (written out here, since this file cannot include the
 * model's header: it reaches zxm.h). Slots: @num.wellspring_band,
 * @logic.wellspring_safe_ok, @num.wellspring_week,
 * @num.wellspring_progress_x100, @logic.wellspring_log_ok. */

static int64_t ws_band(int64_t reading, int64_t lo, int64_t hi) /* wellspring_band */
{
    return reading < lo ? -1 : reading > hi ? 1 : 0;
}

static bool ws_safe(int64_t bpm, int64_t sleep, int64_t water) /* wellspring_safe */
{
    return bpm >= 40 && bpm <= 100 && sleep >= 420 && sleep <= 540 && water >= 1500 &&
           water <= 4000;
}

static bool ws_log_ok(int64_t day, int64_t kind, int64_t amount) /* wellspring_log_ok */
{
    return day >= 0 && day <= 999999 && kind >= 1 && kind <= 4 && amount >= 0 &&
           amount <= 999999999;
}

APP_TEST(wellspring, 10, 89)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], want[WAVE_ROWS];

    /* band: a reading against any range, Rust (wasm) = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        b[i] = wrand(-1000, 5000);
        c[i] = b[i] + wrand(0, 3000);
        a[i] = wrand(b[i] - 500, c[i] + 500);
    }
    a[0] = 40, b[0] = 40, c[0] = 100;               /* the low end is inside */
    a[1] = 100, b[1] = 40, c[1] = 100;              /* the high end is inside */
    a[2] = 39, b[2] = 40, c[2] = 100;               /* one below */
    a[3] = 101, b[3] = 40, c[3] = 100;              /* one above */
    a[4] = 0, b[4] = 0, c[4] = 0;                   /* a range of one value */
    a[5] = -1, b[5] = 0, c[5] = 0;                  /* negative */
    a[6] = 999999999, b[6] = 0, c[6] = 999999999;   /* max */
    a[7] = 1000000000, b[7] = 0, c[7] = 999999999;  /* max + 1 */
    a[8] = -999999999, b[8] = -999999999, c[8] = 0; /* a negative range */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = ws_band(a[i], b[i], c[i]);
    zxapp_arg_t ba[3] = {{"reading", a, WAVE_ROWS}, {"lo", b, WAVE_ROWS}, {"hi", c, WAVE_ROWS}};
    CHECK(wave_same(app, "band", ba, 3, want, WAVE_ROWS, false),
          "Wellspring: band, Rust = KRON = C");
    {
        /* lo above hi is no range: Rust traps (refused), KRON answers */
        static const int64_t r[2] = {50, 50}, lo[2] = {40, 101}, hi[2] = {100, 100};
        zxapp_arg_t na[3] = {{"reading", r, 2}, {"lo", lo, 2}, {"hi", hi, 2}};
        CHECK(wave_lp(app, "band", na, 3) == ZXAPP_BOTH,
              "Wellspring: a range upside down: Rust refuses, KRON answers: BOTH");
    }

    /* safe_ok: a day's readings against the declared ranges, Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(30, 110);
        b[i] = wrand(400, 560);
        c[i] = wrand(1400, 4100);
    }
    a[0] = 40, b[0] = 420, c[0] = 1500;          /* every low end */
    a[1] = 100, b[1] = 540, c[1] = 4000;         /* every high end */
    a[2] = 39, b[2] = 480, c[2] = 2000;          /* heart rate one below */
    a[3] = 101, b[3] = 480, c[3] = 2000;         /* one above */
    a[4] = 60, b[4] = 419, c[4] = 2000;          /* sleep one below */
    a[5] = 60, b[5] = 541, c[5] = 2000;          /* one above */
    a[6] = 60, b[6] = 480, c[6] = 1499;          /* water one below */
    a[7] = 60, b[7] = 480, c[7] = 4001;          /* one above */
    a[8] = 0, b[8] = 0, c[8] = 0;                /* nothing */
    a[9] = -60, b[9] = 480, c[9] = 2000;         /* negative */
    a[10] = 60, b[10] = 999999999, c[10] = 2000; /* far past */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(ws_safe(a[i], b[i], c[i]));
    zxapp_arg_t sa[3] = {{"bpm", a, WAVE_ROWS}, {"sleep", b, WAVE_ROWS}, {"water", c, WAVE_ROWS}};
    CHECK(wave_same(app, "safe_ok", sa, 3, want, WAVE_ROWS, true),
          "Wellspring: safe_ok, Eve = KRON = C");
    {
        /* one heart rate for two days: Eve shares it (EV1), KRON does not stretch it */
        static const int64_t p[1] = {60}, s[2] = {480, 480}, wt[2] = {2000, 2000};
        zxapp_arg_t sb[3] = {{"bpm", p, 1}, {"sleep", s, 2}, {"water", wt, 2}};
        CHECK(wave_lp(app, "safe_ok", sb, 3) == ZXAPP_BOTH,
              "Wellspring: one reading for two days: Eve answers, KRON refuses: BOTH");
    }

    /* week: 64 weeks, each an exact sum, Trebago = KRON = C */
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t d[7], s = 0;
        for (uint32_t i = 0; i < 7; i++)
            s += d[i] = r == 0 ? 0 : r == 1 ? 999999999 : wrand(0, 5000);
        zxapp_arg_t wa = {"amount", d, 7};
        CHECK(wave_same(app, "week", &wa, 1, &s, 1, false),
              "Wellspring: week %u, Trebago = KRON = C", r);
    }
    {
        static const int64_t top[2] = {INT64_MAX - 5, 5}, tw[1] = {INT64_MAX};
        zxapp_arg_t ta = {"amount", top, 2};
        CHECK(wave_same(app, "week", &ta, 1, tw, 1, false), "Wellspring: a week at INT64_MAX");
        /* added from the first, INT64_MAX + 1 passes 64 bits (Trebago refuses);
         * from the last, 1 + -1 first (KRON answers INT64_MAX) */
        static const int64_t over[3] = {INT64_MAX, 1, -1};
        zxapp_arg_t oa = {"amount", over, 3};
        CHECK(wave_lp(app, "week", &oa, 1) == ZXAPP_BOTH,
              "Wellspring: a partial sum past 64 bits one way only: BOTH");
    }

    /* progress_x100: toward a goal, floored, KRON = Sutra = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 999999999);
        b[i] = wrand(1, i < 32 ? 20000 : 999999999);
    }
    a[0] = 0, b[0] = 150;       /* nothing done */
    a[1] = 150, b[1] = 150;     /* met: 10000 */
    a[2] = 999999999, b[2] = 1; /* max over the least goal */
    a[3] = 1, b[3] = 999999999; /* the least over the max: 0 */
    a[4] = 149, b[4] = 150;     /* 9933.33.. floored */
    a[5] = 225, b[5] = 150;     /* passed: 15000 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = a[i] * 10000 / b[i];
    zxapp_arg_t pa[2] = {{"done", a, WAVE_ROWS}, {"goal", b, WAVE_ROWS}};
    CHECK(wave_same(app, "progress_x100", pa, 2, want, WAVE_ROWS, false),
          "Wellspring: progress_x100, KRON = Sutra = C");
    {
        /* max + 1 past Sutra's 9(9): refused there, KRON answers */
        static const int64_t d[2] = {1, 1000000000}, g[2] = {150, 150};
        zxapp_arg_t da[2] = {{"done", d, 2}, {"goal", g, 2}};
        CHECK(wave_lp(app, "progress_x100", da, 2) == ZXAPP_BOTH,
              "Wellspring: done past 9(9): Sutra refuses, KRON answers: BOTH");
        /* a negative amount done: Sutra refuses it too */
        static const int64_t n[2] = {1, -150};
        zxapp_arg_t ne[2] = {{"done", n, 2}, {"goal", g, 2}};
        CHECK(wave_lp(app, "progress_x100", ne, 2) == ZXAPP_BOTH,
              "Wellspring: negative done: Sutra refuses, KRON answers: BOTH");
        /* no goal: both refuse the division, nothing to show */
        static const int64_t z[2] = {0, 0};
        zxapp_arg_t za[2] = {{"done", g, 2}, {"goal", z, 2}};
        CHECK(wave_lp(app, "progress_x100", za, 2) == ZXAPP_NEITHER,
              "Wellspring: a goal of 0: both refuse: NEITHER");
    }

    /* log_ok: an entry fits its record, MYRA = Eve = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 999999);
        b[i] = wrand(1, 4);
        c[i] = wrand(0, 999999999);
    }
    a[0] = 0, b[0] = 1, c[0] = 0;              /* every least */
    a[1] = 999999, b[1] = 4, c[1] = 999999999; /* every max */
    a[2] = 1000000, b[2] = 2, c[2] = 480;      /* day max + 1 */
    a[3] = 20000, b[3] = 3, c[3] = 1000000000; /* amount max + 1 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(ws_log_ok(a[i], b[i], c[i]));
    zxapp_arg_t la[3] = {{"day", a, WAVE_ROWS}, {"kind", b, WAVE_ROWS}, {"amount", c, WAVE_ROWS}};
    CHECK(wave_same(app, "log_ok", la, 3, want, WAVE_ROWS, true),
          "Wellspring: log_ok, MYRA = Eve = C");
    {
        /* kind 5 fits MYRA's one digit but is no kind Eve knows: held */
        static const int64_t d[2] = {20000, 20000}, k[2] = {1, 5}, m[2] = {30, 30};
        zxapp_arg_t ka[3] = {{"day", d, 2}, {"kind", k, 2}, {"amount", m, 2}};
        CHECK(wave_lp(app, "log_ok", ka, 3) == ZXAPP_BOTH,
              "Wellspring: an unknown kind: MYRA TRUE, Eve FALSE: BOTH");
    }
}
