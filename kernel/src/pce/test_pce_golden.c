/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_pce_golden.c — pinned digests and node counts (pce.h E4).
 *
 * A replay is only checkable if the digest of a run never drifts: these are
 * the SHA-256 input and output digests of one fixed run per engine, and the
 * exact node counts of two searches (alpha-beta pruning is part of the
 * answer's cost, so a change to it shows up here). A deliberate change to a
 * digest's layout updates these strings in the same commit. */
#include <stdio.h>
#include <string.h>
#include "pce_prob.h"
#include "pce_tac.h"
#include "pce_strat.h"
#include "pce_fin.h"

static int *P, *F;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            (*P)++;                                                                                \
        else {                                                                                     \
            (*F)++;                                                                                \
            printf("[FAIL] %s (%s:%d)\n", msg, __FILE__, __LINE__);                                \
        }                                                                                          \
    } while (0)

static void hex(const uint8_t d[32], char out[65])
{
    static const char *h = "0123456789abcdef";
    for (uint32_t i = 0; i < 32u; i++) {
        out[2u * i] = h[d[i] >> 4];
        out[2u * i + 1u] = h[d[i] & 15u];
    }
    out[64] = '\0';
}

static void pin(const uint8_t d[32], const char *want, const char *what)
{
    char got[65];
    hex(d, got);
    if (strcmp(got, want) != 0) printf("  %s = \"%s\"\n", what, got);
    CHECK(strcmp(got, want) == 0, what);
}

static rat_t R(int64_t n, int64_t d)
{
    return rat_make(n, d);
}

static uint32_t nim_moves(void *ctx, const uint8_t *s, uint32_t *out, uint32_t cap)
{
    (void) ctx;
    uint32_t n = 0;
    for (uint32_t k = 1; k <= 3u && k <= s[0] && n < cap; k++) out[n++] = k;
    return n;
}
static bool nim_apply(void *ctx, const uint8_t *s, uint32_t m, uint8_t *next)
{
    (void) ctx;
    if (m < 1u || m > 3u || m > s[0]) return false;
    next[0] = (uint8_t) (s[0] - m);
    return true;
}
static int64_t nim_score(void *ctx, const uint8_t *s, uint32_t e)
{
    (void) ctx;
    if (s[0] == 0u) return -1000;
    if (e == 0u) return s[0] % 4u == 0u ? -1 : 1;
    return s[0] % 2u == 0u ? 1 : -1;
}

void test_pce_golden(int *pass_, int *fail_)
{
    P = pass_;
    F = fail_;
    pdx_rec_t st[8];
    pdx_log_t lg;
    pdx_log_init(&lg, st, 8);

    pce_mass_t a, b;
    pce_thresh_t th = {R(9, 10), R(1, 10)};
    pce_verdict_t v;
    pce_mass_set(&a, R(3, 10), R(3, 10), R(4, 10), R(0, 1));
    pce_mass_set(&b, R(5, 10), R(1, 10), R(4, 10), R(0, 1));
    pce_mass_decide(&a, &b, &th, false, LPX_PH_MANIFEST, PCE_ADV_DEVICE, &lg, &v);
    pin(st[0].a, "89cbb38bd002a230fc936dde8debf94402a4e0afccecbf7d6450a33f3069fabe", "prob in");
    pin(st[0].b, "b6a6307f85439d09c325d2b3751342fd12bb5518bed91383795df56e86b6f928", "prob out");

    pce_game_t nim = {1, 2, NULL, nim_moves, nim_apply, nim_score};
    pce_tac_opts_t o = {PCE_PURPOSE_GAME, 6, 100000, 0, LPX_PH_MANIFEST};
    uint8_t s9[1] = {9};
    pce_tac_result_t t;
    pce_tac_search(&nim, s9, &o, NULL, &t);
    pin(t.in, "5b3c9394f16660afa09f753ede42ebe64532deac3431f9009b936280ea111d42", "tac in");
    pin(t.out, "e4f905cccc4af3926ede67e824959ca95a4ebc694410ba9226f5a77d5a4e95cc", "tac out");
    CHECK(t.nodes == 687u && t.robust == 1u, "nim 9 at depth 6: 687 nodes with pruning, take 1");

    int64_t pay[9] = {100, 20, -50, 50, 30, 0, 20, 20, 20};
    rat_t lo[3] = {R(1, 4), R(1, 4), R(0, 1)}, hi[3] = {R(1, 2), R(1, 2), R(1, 2)};
    uint32_t fl[3] = {0, PCE_ADV_MONEY, PCE_OPT_PROHIBITED};
    pce_strat_in_t in = {3, 3, pay, lo, hi, fl, PCE_PURPOSE_BUSINESS, LPX_PH_MANIFEST};
    pce_strat_result_t sr;
    pce_strat_decide(&in, NULL, NULL, &sr);
    pin(sr.in, "4bfb3d7a98d741363707d1bcac7a05a41db1ab77fb5334e1c2148fdab597788f", "strat in");
    pin(sr.out, "d9628f0b4ab66ea7ed9ab320d7b816a38ec45bf325e7cda74dafb95b323ff82c", "strat out");
    pce_est_t e[2] = {{100, 150}, {110, 140}};
    pce_zopa_t z;
    pce_strat_zopa(e, 2, PCE_PURPOSE_NEGOTIATION, PCE_ADV_MONEY, 1, NULL, &z);
    pin(z.in, "d12e14ce86240f2edf9994371bb753c9f80acbcc66ce3570a99b4255e0baccde", "zopa in");
    pin(z.out, "9fb7f7e5b8d5508ffc34aef17ad53962d5a6217d9f11d02c7587f0c200ab1be6", "zopa out");

    pce_stmt_t fs[2];
    for (uint32_t i = 0; i < 2u; i++) {
        for (uint32_t f = 0; f < PCE_FIN_FIELDS; f++) fs[i].v[f] = (int64_t) (100 + 10 * f + i);
        fs[i].present = (1u << PCE_FIN_FIELDS) - 1u;
        fs[i].standard = i;
    }
    fs[0].v[PCE_FIN_ASSETS] = fs[0].v[PCE_FIN_LIABILITIES] + fs[0].v[PCE_FIN_EQUITY];
    pce_fin_report_t fr;
    pce_fin_analyse(fs, 2, LPX_PH_MANIFEST, &lg, &fr);
    pin(fr.in, "ba4c5fbaf385f322e21af38752dc1d32042e23b55ac38a6e3ad6c534741ec49d", "fin in");
    pin(fr.out, "72a9f3c361f2b0f60a999b2e2202b4f68c2fb1cd65506a6cec379ff7453185c8", "fin out");
    pin(st[1].b, "7b15e0409eb1edc0c28ba52273830a97e802bdfd70e179fea2668c97251e600c",
        "fin quarantine record");
    int64_t cf[3] = {-1000, 600, 600};
    rat_t rates[2] = {R(5, 100), R(15, 100)};
    pce_npv_t np;
    pce_fin_npv(cf, 3, rates, 2, 0, NULL, &np);
    pin(np.in, "a772f7de7568efc679490d6d58a3a253776e6191c862dba963ca38f351df222e", "npv in");
    pin(np.out, "e153f75f3baecd925c5fe993c43fd0edd3c8727a9388d632254c7d201ade0581", "npv out");
    int64_t loss[3] = {0, 100, 500};
    rat_t p[3] = {R(8, 10), R(1, 10), R(1, 10)}, q[3] = {R(7, 10), R(2, 10), R(1, 10)};
    pce_risk_t rk;
    pce_fin_risk(loss, p, q, 3, R(9, 10), 0, NULL, &rk);
    pin(rk.in, "bdaf68362867a6f27929329957e5d287fd028f9d2b31449479a46751bdb9cc89", "risk in");
    pin(rk.out, "76e9728ffa202efdd57179a75c89fc969addb4e4889e1cf23d6a593d3a481905", "risk out");
}
