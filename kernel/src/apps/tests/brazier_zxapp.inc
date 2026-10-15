/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Brazier's languages, witnessed, row by row against the rules of
 * brazier_model.c (written out here, since this file cannot include the
 * model's header: it reaches zxm.h). Slots: @num.brazier_minutes_left,
 * @logic.brazier_heat_ok, @logic.brazier_heat_safe, and the battery's
 * level through the used_share block, @num.used_x100. */

static int64_t bz_level_x100(int64_t now, int64_t full) /* brazier_level_x100 */
{
    return now * 10000 / full;
}

static int64_t bz_minutes_left(int64_t charge, int64_t draw) /* brazier_minutes_left */
{
    return charge * 60 / draw;
}

static bool bz_heat_ok(int64_t temp) /* brazier_heat_ok: 0.0..45.0 C */
{
    return temp >= 0 && temp <= 450;
}

static bool bz_heat_safe(int64_t temp) /* brazier_heat_safe: -20.0..60.0 C */
{
    return temp >= -200 && temp <= 600;
}

APP_TEST(brazier, 8, 21)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], want[WAVE_ROWS];

    /* the battery's level: used_share with total = full, left = full - now,
     * Sutra = KRON = C (brazier_level_x100) */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, i < 32 ? 100000 : 999999999); /* full, mWh */
        b[i] = wrand(0, a[i]);                        /* now */
    }
    a[0] = 50000, b[0] = 0;             /* empty */
    a[1] = 50000, b[1] = 50000;         /* full */
    a[2] = 50000, b[2] = 35700;         /* 71.40% */
    a[3] = 3, b[3] = 1;                 /* 33.33.. floored */
    a[4] = 999999999, b[4] = 999999998; /* one short of the max: 9999 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        want[i] = bz_level_x100(b[i], a[i]);
        b[i] = a[i] - b[i]; /* left: the capacity not holding charge */
    }
    zxapp_arg_t ua[2] = {{"total", a, WAVE_ROWS}, {"left", b, WAVE_ROWS}};
    CHECK(wave_same(app, "used_share.used_x100", ua, 2, want, WAVE_ROWS, false),
          "Brazier: battery level, Sutra = KRON = C");
    {
        /* more charge than the full capacity: left is negative, Sutra refuses, KRON answers */
        static const int64_t t[2] = {50000, 50000}, l[2] = {0, -1};
        zxapp_arg_t oa[2] = {{"total", t, 2}, {"left", l, 2}};
        CHECK(wave_lp(app, "used_share.used_x100", oa, 2) == ZXAPP_BOTH,
              "Brazier: charge past full: Sutra refuses, KRON answers: BOTH");
    }

    /* minutes_left: charge over draw, floored, Sutra = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 999999999);
        b[i] = wrand(1, i < 32 ? 100000 : 999999999);
    }
    a[0] = 0, b[0] = 1000;              /* empty: 0 */
    a[1] = 1000, b[1] = 1000;           /* an hour */
    a[2] = 999999999, b[2] = 1;         /* every max over the least draw */
    a[3] = 1, b[3] = 999999999;         /* the least over the max draw: 0 */
    a[4] = 35700, b[4] = 14090;         /* 152.02.. floored */
    a[5] = 999999999, b[5] = 999999999; /* 60 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = bz_minutes_left(a[i], b[i]);
    zxapp_arg_t ma[2] = {{"charge", a, WAVE_ROWS}, {"draw", b, WAVE_ROWS}};
    CHECK(wave_same(app, "minutes_left", ma, 2, want, WAVE_ROWS, false),
          "Brazier: minutes_left, Sutra = KRON = C");
    {
        /* max + 1 past Sutra's 9(9): refused there, KRON answers */
        static const int64_t c[2] = {1, 1000000000}, d[2] = {1, 1};
        zxapp_arg_t ca[2] = {{"charge", c, 2}, {"draw", d, 2}};
        CHECK(wave_lp(app, "minutes_left", ca, 2) == ZXAPP_BOTH,
              "Brazier: charge past 9(9): Sutra refuses, KRON answers: BOTH");
        /* a negative draw (charging): Sutra refuses it, KRON answers */
        static const int64_t n[2] = {1000, -1000}, e[2] = {1000, 1000};
        zxapp_arg_t na[2] = {{"charge", e, 2}, {"draw", n, 2}};
        CHECK(wave_lp(app, "minutes_left", na, 2) == ZXAPP_BOTH,
              "Brazier: a negative draw: Sutra refuses, KRON answers: BOTH");
    }

    /* heat_ok and heat_safe: tenths of a degree against each limit, Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(-400, 900);
    a[0] = 0;      /* the warning's low end is inside */
    a[1] = 450;    /* its high end */
    a[2] = -1;     /* one below */
    a[3] = 451;    /* one above */
    a[4] = -200;   /* the critical low end is inside */
    a[5] = 600;    /* its high end */
    a[6] = -201;   /* one below */
    a[7] = 601;    /* one above */
    a[8] = -2732;  /* absolute zero */
    a[9] = 999999; /* far past */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(bz_heat_ok(a[i]));
    zxapp_arg_t ta = {"temp", a, WAVE_ROWS};
    CHECK(wave_same(app, "heat_ok", &ta, 1, want, WAVE_ROWS, true),
          "Brazier: heat_ok, Eve = KRON = C");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(bz_heat_safe(a[i]));
    CHECK(wave_same(app, "heat_safe", &ta, 1, want, WAVE_ROWS, true),
          "Brazier: heat_safe, Eve = KRON = C");
}
