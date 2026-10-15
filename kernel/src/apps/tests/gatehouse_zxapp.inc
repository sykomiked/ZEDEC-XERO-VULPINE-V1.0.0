/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Gatehouse's languages against the C its model runs (gatehouse_rules.h).
 * Slots: @logic.gatehouse_use_ok, @num.gatehouse_use_allowed,
 * @num.gatehouse_holders. */
#include "gatehouse_rules.h"

APP_TEST(gatehouse, 6, 8)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], want[WAVE_ROWS];

    /* GR1: the model's names are zxapp.c's one table, in its order */
    uint32_t ncaps = 0;
    const char *const *caps = zxapp_caps(&ncaps);
    bool same = ncaps == GATEHOUSE_CAPS;
    for (uint32_t i = 0; same && i < ncaps; i++) same = !strcmp(caps[i], GATEHOUSE_CAP_NAME[i]);
    CHECK(same, "Gatehouse: its capabilities are zxapp_caps(), in order (%u)", ncaps);

    /* use_ok and use_allowed (GR3): every flag row, then a sweep */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1);
        b[i] = wrand(0, 1);
        c[i] = wrand(0, 1);
    }
    for (uint32_t i = 0; i < 8; i++) /* all eight rows of three flags */
        a[i] = (int64_t) (i & 1u), b[i] = (int64_t) (i >> 1 & 1u), c[i] = (int64_t) (i >> 2);
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(gatehouse_use_ok(a[i], b[i], c[i]));
    zxapp_arg_t ua[3] = {
        {"asked", a, WAVE_ROWS}, {"granted", b, WAVE_ROWS}, {"revoked", c, WAVE_ROWS}};
    CHECK(wave_same(app, "use_ok", ua, 3, want, WAVE_ROWS, true),
          "Gatehouse: use_ok, Eve = KRON = C");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = gatehouse_use_allowed(a[i], b[i], c[i]);
    CHECK(wave_same(app, "use_allowed", ua, 3, want, WAVE_ROWS, false),
          "Gatehouse: use_allowed, C++ (wasm) = KRON = C");
    {
        /* a flag that is no flag (max + 1 = 2, -1, the widest) is never read as yes */
        static const int64_t edge[4] = {2, -1, INT64_MAX, -INT64_MAX};
        for (uint32_t e = 0; e < 4; e++) {
            for (uint32_t k = 0; k < 3; k++) {
                int64_t x[2] = {1, 1}, y[2] = {1, 1}, z[2] = {0, 0};
                int64_t *p = k == 0 ? x : k == 1 ? y : z;
                p[1] = edge[e];
                zxapp_arg_t na[3] = {{"asked", x, 2}, {"granted", y, 2}, {"revoked", z, 2}};
                int32_t lp = wave_lp(app, "use_ok", na, 3);
                /* Eve's record never fits it; KRON reads above 0 as yes, so
                 * only a no-flag that KRON also reads as no is agreed (no) */
                bool kron_yes = k == 2 ? edge[e] < 1 : edge[e] > 0;
                CHECK(lp == (kron_yes ? ZXAPP_BOTH : ZXAPP_TRUE),
                      "Gatehouse: use_ok with flag %u = %lld: Eve no, KRON %s", k,
                      (long long) edge[e], kron_yes ? "yes: BOTH" : "no: agreed");
                CHECK(wave_lp(app, "use_allowed", na, 3) == ZXAPP_BOTH,
                      "Gatehouse: use_allowed with flag %u = %lld: C++ refuses, KRON answers: BOTH",
                      k, (long long) edge[e]);
                CHECK(gatehouse_use_allowed(x[1], y[1], z[1]) == GATEHOUSE_REFUSED &&
                          !gatehouse_use_ok(x[1], y[1], z[1]),
                      "Gatehouse: the C refuses it too");
            }
        }
    }

    /* holders (GR4): 64 capabilities, each a count over up to 64 apps */
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t h[WAVE_ROWS];
        uint32_t n = r < 2 ? 2u : (uint32_t) wrand(2, WAVE_ROWS);
        for (uint32_t i = 0; i < n; i++) h[i] = r == 0 ? 0 : r == 1 ? 1 : wrand(0, 1);
        int64_t w = gatehouse_holders(h, n);
        zxapp_arg_t ha = {"held", h, n};
        CHECK(wave_same(app, "holders", &ha, 1, &w, 1, false),
              "Gatehouse: holders %u of %u apps, KRON = Trebago = C", r, n);
    }
    {
        /* a flag past 1: KRON holds it at 1, Trebago adds it as it is */
        static const int64_t two[2] = {1, 2}, minus[2] = {1, -1}, top[2] = {0, INT64_MAX};
        zxapp_arg_t t1 = {"held", two, 2}, t2 = {"held", minus, 2}, t3 = {"held", top, 2};
        CHECK(wave_lp(app, "holders", &t1, 1) == ZXAPP_BOTH &&
                  wave_lp(app, "holders", &t2, 1) == ZXAPP_BOTH &&
                  wave_lp(app, "holders", &t3, 1) == ZXAPP_BOTH,
              "Gatehouse: holders with a flag that is no flag: KRON and Trebago disagree: BOTH");
        CHECK(gatehouse_holders(two, 2) == GATEHOUSE_REFUSED &&
                  gatehouse_holders(minus, 2) == GATEHOUSE_REFUSED,
              "Gatehouse: the C refuses it");
    }
}
