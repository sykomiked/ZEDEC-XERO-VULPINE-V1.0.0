/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Hedgerow's languages against the C its model runs (hedgerow_rules.h).
 * Slots: @logic.hedgerow_path_open, @num.hedgerow_path_open_n,
 * @num.hedgerow_addr_scope, @logic.hedgerow_reach_rule_ok, and the
 * entry it uses, @logic.gatehouse_use_ok. */
#include "gatehouse_rules.h"
#include "hedgerow_rules.h"

APP_TEST(hedgerow, 14, 30)
{
    static int64_t s[WAVE_ROWS], m[WAVE_ROWS], b[WAVE_ROWS], want[WAVE_ROWS];
    static int64_t f[WAVE_ROWS], o1[WAVE_ROWS], o2[WAVE_ROWS], o3[WAVE_ROWS], o4[WAVE_ROWS];
    static int64_t q[WAVE_ROWS], pa[WAVE_ROWS], op[WAVE_ROWS], po[WAVE_ROWS], st[WAVE_ROWS];

    /* path_open and path_open_n (HR3): all 50 rows of a flag and two rooms
     * of -2..2, then rooms from the ladders themselves (HR1, HR2) */
    for (uint32_t i = 0; i < 50u; i++)
        s[i] = (int64_t) (i % 2u), m[i] = (int64_t) (i / 2u % 5u) - 2,
        b[i] = (int64_t) (i / 10u) - 2;
    for (uint32_t i = 50; i < WAVE_ROWS; i++) {
        int64_t mode = wrand(0, 2), bind = wrand(0, 2), far = wrand(0, 2);
        s[i] = wrand(0, 1);
        m[i] = mode - far;
        b[i] = bind - far;
        CHECK(hedgerow_reach(s[i], mode, bind, far) == hedgerow_path_open(s[i], m[i], b[i]),
              "Hedgerow: reach from the ladders is path_open of the rooms");
    }
    zxapp_arg_t pa3[3] = {
        {"sw", s, WAVE_ROWS}, {"moderoom", m, WAVE_ROWS}, {"bindroom", b, WAVE_ROWS}};
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(hedgerow_path_open(s[i], m[i], b[i]));
    CHECK(wave_same(app, "path_open", pa3, 3, want, WAVE_ROWS, true),
          "Hedgerow: path_open, Eve = KRON = C");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = hedgerow_path_open_n(s[i], m[i], b[i]);
    CHECK(wave_same(app, "path_open_n", pa3, 3, want, WAVE_ROWS, false),
          "Hedgerow: path_open_n, C++ (wasm) = KRON = C");
    {
        /* a switch that is no flag, a room off the ladder: never read as open */
        static const int64_t edge[6] = {2, -1, 3, -3, 1000000, -1000000};
        for (uint32_t e = 0; e < 6; e++) {
            for (uint32_t k = 0; k < 3; k++) {
                /* a room of -2..2 is on the ladder: no edge */
                if (k != 0 && edge[e] >= -2 && edge[e] <= 2) continue;
                int64_t x[2] = {1, 1}, y[2] = {0, 0}, z[2] = {0, 0};
                int64_t *p = k == 0 ? x : k == 1 ? y : z;
                p[1] = edge[e];
                zxapp_arg_t na[3] = {{"sw", x, 2}, {"moderoom", y, 2}, {"bindroom", z, 2}};
                bool c_open = hedgerow_path_open(x[1], y[1], z[1]);
                /* KRON reads a switch above 0 as on and a room above -1 as room */
                bool kron_yes = k == 0 ? edge[e] > 0 : edge[e] > -1;
                CHECK(!c_open &&
                          wave_lp(app, "path_open", na, 3) == (kron_yes ? ZXAPP_BOTH : ZXAPP_TRUE),
                      "Hedgerow: path_open with input %u = %lld: Eve no, KRON %s", k,
                      (long long) edge[e], kron_yes ? "yes: BOTH" : "no: agreed");
                CHECK(hedgerow_path_open_n(x[1], y[1], z[1]) == HEDGEROW_REFUSED &&
                          wave_lp(app, "path_open_n", na, 3) == ZXAPP_BOTH,
                      "Hedgerow: path_open_n with input %u = %lld: C and C++ refuse, KRON "
                      "answers: BOTH",
                      k, (long long) edge[e]);
            }
        }
        int64_t x[2] = {1, INT64_MAX}, y[2] = {0, 0}, z[2] = {0, 0};
        zxapp_arg_t na[3] = {{"sw", x, 2}, {"moderoom", y, 2}, {"bindroom", z, 2}};
        CHECK(wave_lp(app, "path_open", na, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "path_open_n", na, 3) == ZXAPP_BOTH,
              "Hedgerow: the widest switch is held in both forms");
    }

    /* addr_scope (HR4): the edges of every class, then a sweep */
    static const int64_t E[][6] = {
        {4, 8, 8, 8, 8, 0},     {4, 127, 0, 0, 1, 1},       {4, 126, 255, 255, 255, 0},
        {4, 128, 0, 0, 0, 0},   {4, 10, 0, 0, 0, 2},        {4, 172, 15, 0, 0, 0},
        {4, 172, 16, 0, 0, 2},  {4, 172, 31, 255, 0, 2},    {4, 172, 32, 0, 0, 0},
        {4, 192, 168, 1, 1, 2}, {4, 192, 167, 1, 1, 0},     {4, 169, 254, 0, 1, 3},
        {4, 169, 253, 0, 1, 0}, {4, 0, 0, 0, 0, 5},         {4, 223, 255, 255, 255, 0},
        {4, 224, 0, 0, 1, 5},   {4, 255, 255, 255, 255, 5}, {6, 0, 0, 0, 1, 1},
        {6, 0, 0, 0, 0, 5},     {6, 0, 0, 255, 255, 5},     {6, 254, 128, 0, 1, 3},
        {6, 254, 191, 0, 1, 3}, {6, 254, 127, 0, 1, 0},     {6, 254, 192, 0, 1, 0},
        {6, 252, 0, 0, 1, 4},   {6, 253, 255, 0, 1, 4},     {6, 251, 0, 0, 1, 0},
        {6, 255, 2, 0, 1, 5},   {6, 32, 1, 0, 1, 0},        {6, 0, 1, 0, 1, 0},
    };
    const uint32_t NE = sizeof E / sizeof E[0];
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        if (i < NE) {
            f[i] = E[i][0], o1[i] = E[i][1], o2[i] = E[i][2], o3[i] = E[i][3], o4[i] = E[i][4];
            CHECK(hedgerow_addr_scope(f[i], o1[i], o2[i], o3[i], o4[i]) == E[i][5],
                  "Hedgerow: the C scopes edge %u as %lld", i, (long long) E[i][5]);
        } else {
            static const int64_t lead[8] = {10, 127, 169, 172, 192, 252, 254, 255};
            f[i] = wrand(0, 1) ? 4 : 6;
            o1[i] = wrand(0, 1) ? lead[wrand(0, 7)] : wrand(0, 255);
            o2[i] = wrand(0, 255);
            o3[i] = wrand(0, 255);
            o4[i] = wrand(0, 255);
        }
        want[i] = hedgerow_addr_scope(f[i], o1[i], o2[i], o3[i], o4[i]);
    }
    zxapp_arg_t sa[5] = {{"fam", f, WAVE_ROWS},
                         {"a", o1, WAVE_ROWS},
                         {"b", o2, WAVE_ROWS},
                         {"c", o3, WAVE_ROWS},
                         {"d", o4, WAVE_ROWS}};
    CHECK(wave_same(app, "addr_scope", sa, 5, want, WAVE_ROWS, false),
          "Hedgerow: addr_scope, Rust (wasm) = KRON = C");
    {
        /* an octet past 255 or below 0, a family not 4 or 6: no address */
        static const int64_t bad[][5] = {{4, 256, 0, 0, 0}, {4, 8, -1, 0, 0},
                                         {6, 0, 0, 0, 256}, {6, 254, 128, 0, 1000},
                                         {5, 8, 8, 8, 8},   {0, 8, 8, 8, 8}};
        for (uint32_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            int64_t col[5][2];
            for (uint32_t k = 0; k < 5; k++) col[k][0] = 8, col[k][1] = bad[i][k];
            col[0][0] = 4;
            zxapp_arg_t ba[5] = {{"fam", col[0], 2},
                                 {"a", col[1], 2},
                                 {"b", col[2], 2},
                                 {"c", col[3], 2},
                                 {"d", col[4], 2}};
            CHECK(hedgerow_addr_scope(bad[i][0], bad[i][1], bad[i][2], bad[i][3], bad[i][4]) ==
                          HEDGEROW_REFUSED &&
                      wave_lp(app, "addr_scope", ba, 5) == ZXAPP_BOTH,
                  "Hedgerow: addr_scope of no address %u: C and Rust refuse, KRON answers: BOTH",
                  i);
        }
    }
    CHECK(hedgerow_far_of_scope(HEDGEROW_SCOPE_LOOPBACK) == 0 &&
              hedgerow_far_of_scope(HEDGEROW_SCOPE_PRIVATE) == 1 &&
              hedgerow_far_of_scope(HEDGEROW_SCOPE_LINK) == 1 &&
              hedgerow_far_of_scope(HEDGEROW_SCOPE_UNIQUE) == 1 &&
              hedgerow_far_of_scope(HEDGEROW_SCOPE_PUBLIC) == 2 &&
              hedgerow_far_of_scope(HEDGEROW_SCOPE_NONE) == HEDGEROW_REFUSED &&
              hedgerow_far_of_scope(-1) == HEDGEROW_REFUSED,
          "Hedgerow: far of each scope (HR5)");

    /* reach_rule_ok (HR6): rows that fit, and rows with a field one digit too long */
    static const int64_t LONG[5] = {1000000000, 10, 10, 100000, 100000000000};
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        q[i] = i == 0 ? 1 : i == 1 ? 999999999 : wrand(1, 999999999);
        pa[i] = i < 2 ? (i ? 6 : 1) : wrand(1, 6);
        op[i] = i < 2 ? (int64_t) i : wrand(0, 1);
        po[i] = i < 2 ? (i ? 65535 : 0) : wrand(0, 65535);
        st[i] = i < 2 ? (i ? 99999999999 : 0) : wrand(0, 99999999999);
        if (i >= 2 && i % 3u == 0) {
            int64_t *field[5] = {&q[i], &pa[i], &op[i], &po[i], &st[i]};
            uint32_t k = (i / 3u) % 5u;
            *field[k] = LONG[k];
        }
        want[i] = T_(hedgerow_rule_ok(q[i], pa[i], op[i], po[i], st[i]));
    }
    zxapp_arg_t ra[5] = {{"seq", q, WAVE_ROWS},
                         {"path", pa, WAVE_ROWS},
                         {"open", op, WAVE_ROWS},
                         {"port", po, WAVE_ROWS},
                         {"stamp", st, WAVE_ROWS}};
    CHECK(wave_same(app, "reach_rule_ok", ra, 5, want, WAVE_ROWS, true),
          "Hedgerow: reach_rule_ok, MYRA = Eve = C");
    {
        /* the digits fit, the meaning does not: held */
        static const int64_t odd[][5] = {{0, 1, 0, 0, 0}, {1, 7, 0, 0, 0},     {1, 0, 0, 0, 0},
                                         {1, 1, 2, 0, 0}, {1, 1, 0, 65536, 0}, {1, 1, 0, 99999, 0}};
        for (uint32_t i = 0; i < sizeof odd / sizeof odd[0]; i++) {
            int64_t col[5][2];
            for (uint32_t k = 0; k < 5; k++) col[k][0] = k == 0 || k == 1 ? 1 : 0;
            for (uint32_t k = 0; k < 5; k++) col[k][1] = odd[i][k];
            zxapp_arg_t oa[5] = {{"seq", col[0], 2},
                                 {"path", col[1], 2},
                                 {"open", col[2], 2},
                                 {"port", col[3], 2},
                                 {"stamp", col[4], 2}};
            CHECK(!hedgerow_rule_ok(odd[i][0], odd[i][1], odd[i][2], odd[i][3], odd[i][4]) &&
                      wave_lp(app, "reach_rule_ok", oa, 5) == ZXAPP_BOTH,
                  "Hedgerow: rule %u has MYRA's digits and not Eve's meaning: BOTH", i);
        }
    }

    /* gatehouse.use_ok (GR3), as the model asks it before an opening:
     * asked is 1, granted is the owner's word, revoked a close since */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        s[i] = 1;
        m[i] = i < 4 ? (int64_t) (i & 1u) : wrand(0, 1);
        b[i] = i < 4 ? (int64_t) (i >> 1) : wrand(0, 1);
        want[i] = T_(gatehouse_use_ok(s[i], m[i], b[i]));
    }
    zxapp_arg_t ga[3] = {
        {"asked", s, WAVE_ROWS}, {"granted", m, WAVE_ROWS}, {"revoked", b, WAVE_ROWS}};
    CHECK(wave_same(app, "gatehouse.use_ok", ga, 3, want, WAVE_ROWS, true),
          "Hedgerow: an opening is granted through gatehouse.use_ok");
}
