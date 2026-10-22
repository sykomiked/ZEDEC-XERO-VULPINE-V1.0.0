/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Homestead's languages against the C its model runs (homestead_rules.h).
 * Slots: @logic.homestead_device_cmd_ok, @num.homestead_device_cmd_n,
 * @num.homestead_device_energy, @logic.homestead_home_device_ok,
 * @logic.homestead_home_scene_ok; Warren's @logic.warren_approval_ok is
 * the owner's approval. */
#include "homestead_rules.h"

/* the call's own status: a single-language entry is NEITHER whether it
 * answers or refuses, so a refusal is read from the call itself */
static int hs_rc(zxapp_t *a, const char *entry, const zxapp_arg_t *args, uint32_t nargs)
{
    zxapp_num_t out[WAVE_ROWS];
    uint32_t n = 0;
    int32_t lp = -1;
    return zxapp_call_witnessed(a, &H, entry, args, nargs, out, WAVE_ROWS, &n, &lp);
}

APP_TEST(homestead, 16, 94)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];

    /* device_cmd_ok: a command allowed, Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-50, HOMESTEAD_MARGIN_MAX);
        b[i] = wrand(-50, HOMESTEAD_MARGIN_MAX);
        c[i] = homestead_owed(wrand(1, HOMESTEAD_KINDS), wrand(0, 1), wrand(0, 1));
        d[i] = i % 5 == 0 ? 1 : 0;
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0;                   /* every least */
    a[1] = HOMESTEAD_MARGIN_MAX, b[1] = HOMESTEAD_MARGIN_MAX; /* every max */
    c[1] = -1, d[1] = 0;                                      /* approval not needed */
    a[2] = -1, b[2] = 5, c[2] = 0, d[2] = 0;                  /* below lo */
    a[3] = 5, b[3] = -1, c[3] = 0, d[3] = 0;                  /* above hi */
    a[4] = 5, b[4] = 5, c[4] = 1, d[4] = 0;                   /* a lock, no approval */
    a[5] = 5, b[5] = 5, c[5] = homestead_owed(HOMESTEAD_OVEN, 1, 1), d[5] = 0; /* approved */
    a[6] = 5, b[6] = 5, c[6] = 0, d[6] = 1;                     /* an AI's suggestion */
    a[7] = -HOMESTEAD_MARGIN_MAX, b[7] = 0, c[7] = 0, d[7] = 0; /* far below */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(homestead_command_ok(a[i], b[i], c[i], d[i]));
    zxapp_arg_t ca[4] = {{"above", a, WAVE_ROWS},
                         {"below", b, WAVE_ROWS},
                         {"owed", c, WAVE_ROWS},
                         {"ai", d, WAVE_ROWS}};
    CHECK(wave_same(app, "device_cmd_ok", ca, 4, want, WAVE_ROWS, true),
          "Homestead: device_cmd_ok, Eve = KRON = C");
    CHECK(homestead_owed(HOMESTEAD_LOCK, 1, 0) == 1 &&
              homestead_owed(HOMESTEAD_HEATER, 0, 1) == 1 &&
              homestead_owed(HOMESTEAD_OVEN, 1, 1) == 0 &&
              homestead_owed(HOMESTEAD_LIGHT, 1, 1) == -1 &&
              homestead_owed(HOMESTEAD_BLIND, 0, 0) == 0,
          "Homestead: locks, heaters and ovens owe the owner's approval (HR1)");
    {
        /* max + 1: no device's range is that wide, Eve refuses, KRON reads it as inside */
        static const int64_t m[2] = {5, HOMESTEAD_MARGIN_MAX + 1}, f[2] = {5, 5}, z[2] = {0, 0},
                             o[2] = {0, -2}, x[2] = {0, -1};
        zxapp_arg_t ma[4] = {{"above", m, 2}, {"below", f, 2}, {"owed", z, 2}, {"ai", z, 2}};
        CHECK(wave_lp(app, "device_cmd_ok", ma, 4) == ZXAPP_BOTH,
              "Homestead: a margin past 199998: Eve FALSE, KRON TRUE: BOTH");
        zxapp_arg_t oa[4] = {{"above", f, 2}, {"below", f, 2}, {"owed", o, 2}, {"ai", z, 2}};
        CHECK(wave_lp(app, "device_cmd_ok", oa, 4) == ZXAPP_BOTH,
              "Homestead: owed -2 is no flag: Eve FALSE, KRON TRUE: BOTH");
        zxapp_arg_t xa[4] = {{"above", f, 2}, {"below", f, 2}, {"owed", z, 2}, {"ai", x, 2}};
        CHECK(wave_lp(app, "device_cmd_ok", xa, 4) == ZXAPP_BOTH,
              "Homestead: ai -1 is no flag: Eve FALSE, KRON TRUE: BOTH");
    }

    /* device_cmd_n: one command per call (Forth reads the stack), Forth = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t v[4];
        v[0] = i == 0   ? 0
               : i == 1 ? HOMESTEAD_MARGIN_MAX
               : i == 2 ? -HOMESTEAD_MARGIN_MAX
                        : wrand(-50, 300);
        v[1] = i == 0 ? 0 : i == 1 ? HOMESTEAD_MARGIN_MAX : i == 3 ? -1 : wrand(-50, 300);
        v[2] = i == 0 ? 0 : i == 1 ? -1 : i == 4 ? 1 : wrand(-1, 1);
        v[3] = i < 5 ? 0 : i == 5 ? 1 : wrand(0, 1);
        int64_t w = homestead_command_n(v[0], v[1], v[2], v[3]);
        zxapp_arg_t na[4] = {
            {"above", &v[0], 1}, {"below", &v[1], 1}, {"owed", &v[2], 1}, {"ai", &v[3], 1}};
        CHECK(w >= 0 && wave_same(app, "device_cmd_n", na, 4, &w, 1, false),
              "Homestead: device_cmd_n %lld %lld %lld %lld, Forth = KRON = C", (long long) v[0],
              (long long) v[1], (long long) v[2], (long long) v[3]);
    }
    {
        /* past the domain: Forth refuses, KRON answers */
        static const int64_t far[1] = {HOMESTEAD_MARGIN_MAX + 1}, five[1] = {5}, zero[1] = {0},
                             neg[1] = {-1}, two[1] = {2}, low[1] = {-HOMESTEAD_MARGIN_MAX - 1};
        zxapp_arg_t fa[4] = {
            {"above", far, 1}, {"below", five, 1}, {"owed", zero, 1}, {"ai", zero, 1}};
        CHECK(wave_lp(app, "device_cmd_n", fa, 4) == ZXAPP_BOTH,
              "Homestead: a margin past 199998: Forth refuses, KRON answers: BOTH");
        zxapp_arg_t la[4] = {
            {"above", five, 1}, {"below", low, 1}, {"owed", zero, 1}, {"ai", zero, 1}};
        CHECK(wave_lp(app, "device_cmd_n", la, 4) == ZXAPP_BOTH,
              "Homestead: a margin below -199998: Forth refuses, KRON answers: BOTH");
        zxapp_arg_t xa[4] = {
            {"above", five, 1}, {"below", five, 1}, {"owed", zero, 1}, {"ai", neg, 1}};
        CHECK(wave_lp(app, "device_cmd_n", xa, 4) == ZXAPP_BOTH,
              "Homestead: ai -1: Forth refuses, KRON answers: BOTH");
        zxapp_arg_t oa[4] = {
            {"above", five, 1}, {"below", five, 1}, {"owed", two, 1}, {"ai", zero, 1}};
        CHECK(wave_lp(app, "device_cmd_n", oa, 4) == ZXAPP_BOTH,
              "Homestead: owed 2: Forth refuses, KRON answers: BOTH");
        CHECK(homestead_command_n(HOMESTEAD_MARGIN_MAX + 1, 5, 0, 0) == HOMESTEAD_REFUSED &&
                  homestead_command_n(5, 5, 0, 2) == HOMESTEAD_REFUSED &&
                  homestead_command_n(5, 5, -2, 0) == HOMESTEAD_REFUSED,
              "Homestead: the C refuses the same");
    }

    /* device_energy: one device's readings, exact, Trebago = KRON = C */
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t dev[16], wh[16], wnt[1] = {(int64_t) (r % 4) + 1}, total = 0;
        uint32_t n = r == 0 ? 1 : 2 + r % 15;
        for (uint32_t i = 0; i < n; i++) {
            dev[i] = wrand(1, 5);
            wh[i] = r == 1 ? 999999999 : wrand(0, 50000);
        }
        CHECK(homestead_energy(dev, wh, n, wnt[0], &total) == 0, "Homestead: energy %u in C", r);
        zxapp_arg_t ea[3] = {{"device", dev, n}, {"want", wnt, 1}, {"wh", wh, n}};
        CHECK(wave_same(app, "device_energy", ea, 3, &total, 1, false),
              "Homestead: device_energy %u, Trebago = KRON = C", r);
    }
    {
        /* added from the first, INT64_MAX + 1 passes 64 bits (Trebago refuses);
         * from the last, 1 + -1 first (KRON answers INT64_MAX) */
        static const int64_t dv[3] = {1, 1, 1}, one[1] = {1}, ov[3] = {INT64_MAX, 1, -1};
        zxapp_arg_t oa[3] = {{"device", dv, 3}, {"want", one, 1}, {"wh", ov, 3}};
        CHECK(wave_lp(app, "device_energy", oa, 3) == ZXAPP_BOTH,
              "Homestead: a partial sum past 64 bits one way only: BOTH");
        int64_t t = 0;
        CHECK(homestead_energy(dv, ov, 3, 1, &t) == HOMESTEAD_REFUSED,
              "Homestead: the C adds from the first and refuses it too");
        /* no reading of the device: 0 */
        static const int64_t nine[1] = {9}, z1[1] = {0}, wh2[3] = {5, 6, 7};
        zxapp_arg_t za[3] = {{"device", dv, 3}, {"want", nine, 1}, {"wh", wh2, 3}};
        CHECK(wave_same(app, "device_energy", za, 3, z1, 1, false),
              "Homestead: a device with no readings has 0");
    }

    /* home_device_ok: a device's record, MYRA = Eve = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, i < 48 ? HOMESTEAD_DEVICE_MAX : 99999);
        b[i] = i % 7 == 6 ? wrand(10, 99) : wrand(1, HOMESTEAD_KINDS);
        c[i] = wrand(-HOMESTEAD_POINT_MAX, HOMESTEAD_POINT_MAX);
        d[i] = wrand(-HOMESTEAD_POINT_MAX, HOMESTEAD_POINT_MAX);
        e[i] = wrand(-HOMESTEAD_POINT_MAX, i < 48 ? HOMESTEAD_POINT_MAX : 999999);
    }
    a[0] = 1, b[0] = 1, c[0] = 0, d[0] = 0, e[0] = 0;   /* every least */
    a[1] = HOMESTEAD_DEVICE_MAX, b[1] = HOMESTEAD_OVEN; /* every max */
    c[1] = -HOMESTEAD_POINT_MAX, d[1] = HOMESTEAD_POINT_MAX, e[1] = HOMESTEAD_POINT_MAX;
    a[2] = HOMESTEAD_DEVICE_MAX + 1, b[2] = 1, c[2] = 0, d[2] = 1, e[2] = 1; /* device max + 1 */
    a[3] = 7, b[3] = 4, c[3] = 0, d[3] = 1, e[3] = HOMESTEAD_POINT_MAX + 1;  /* setpoint max + 1 */
    a[4] = 7, b[4] = 4, c[4] = -HOMESTEAD_POINT_MAX - 1, d[4] = 1, e[4] = 1; /* lo least - 1 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(homestead_device_ok(a[i], b[i], c[i], d[i], e[i]));
    zxapp_arg_t da[5] = {{"device", a, WAVE_ROWS},
                         {"kind", b, WAVE_ROWS},
                         {"lo", c, WAVE_ROWS},
                         {"hi", d, WAVE_ROWS},
                         {"setpoint", e, WAVE_ROWS}};
    CHECK(wave_same(app, "home_device_ok", da, 5, want, WAVE_ROWS, true),
          "Homestead: home_device_ok, MYRA = Eve = C");
    {
        /* kind 7 fits MYRA's one digit but is no kind Eve knows: held */
        static const int64_t dv[2] = {7, 7}, k[2] = {4, 7}, lo[2] = {0, 0}, hi[2] = {1, 1},
                             sp[2] = {1, 1}, d0[2] = {7, 0}, k1[2] = {4, 4};
        zxapp_arg_t ka[5] = {
            {"device", dv, 2}, {"kind", k, 2}, {"lo", lo, 2}, {"hi", hi, 2}, {"setpoint", sp, 2}};
        CHECK(wave_lp(app, "home_device_ok", ka, 5) == ZXAPP_BOTH,
              "Homestead: an unknown kind: MYRA TRUE, Eve FALSE: BOTH");
        /* device 0 fits MYRA's digits, no device Eve keeps: held */
        zxapp_arg_t za[5] = {
            {"device", d0, 2}, {"kind", k1, 2}, {"lo", lo, 2}, {"hi", hi, 2}, {"setpoint", sp, 2}};
        CHECK(wave_lp(app, "home_device_ok", za, 5) == ZXAPP_BOTH,
              "Homestead: device 0: MYRA TRUE, Eve FALSE: BOTH");
    }

    /* home_scene_ok: a scene step's record, MYRA = C (one language: unwitnessed) */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, i < 48 ? HOMESTEAD_SCENE_MAX : 9999);
        b[i] = wrand(0, i < 48 ? HOMESTEAD_DEVICE_MAX : 99999);
        c[i] = wrand(-HOMESTEAD_POINT_MAX, i < 48 ? HOMESTEAD_POINT_MAX : 999999);
    }
    a[0] = 0, b[0] = 0, c[0] = 0; /* every least */
    a[1] = HOMESTEAD_SCENE_MAX, b[1] = HOMESTEAD_DEVICE_MAX, c[1] = -HOMESTEAD_POINT_MAX;
    a[2] = HOMESTEAD_SCENE_MAX + 1, b[2] = 1, c[2] = 1;  /* scene max + 1 */
    a[3] = 1, b[3] = HOMESTEAD_DEVICE_MAX + 1, c[3] = 1; /* device max + 1 */
    a[4] = 1, b[4] = 1, c[4] = HOMESTEAD_POINT_MAX + 1;  /* setpoint max + 1 */
    a[5] = 1, b[5] = 1, c[5] = HOMESTEAD_POINT_MAX;      /* setpoint max */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(homestead_scene_ok(a[i], b[i], c[i]));
    zxapp_arg_t sa[3] = {
        {"scene", a, WAVE_ROWS}, {"device", b, WAVE_ROWS}, {"setpoint", c, WAVE_ROWS}};
    CHECK(wave_same(app, "home_scene_ok", sa, 3, want, WAVE_ROWS, true),
          "Homestead: home_scene_ok, MYRA = C");
    CHECK(hs_rc(app, "home_scene_ok", sa, 3) == ZXAPP_OK, "Homestead: home_scene_ok answers");

    /* warren.approval_ok, used for HR1: only the owner's session and yes */
    {
        static const int64_t ow[4] = {1, 1, 0, 0}, ap[4] = {1, 0, 1, 0};
        int64_t w[4];
        for (uint32_t i = 0; i < 4; i++) w[i] = T_(warren_approval_ok(ow[i], ap[i]));
        zxapp_arg_t wa[2] = {{"owner", ow, 4}, {"approved", ap, 4}};
        CHECK(wave_same(app, "warren.approval_ok", wa, 2, w, 4, true),
              "Homestead: Warren's approval, used as is, Eve = KRON = C");
    }
}
