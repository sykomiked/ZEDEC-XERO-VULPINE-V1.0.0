/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxapp_pce.c — the decision engines as blocks (zxapp_pce.h P1-P4):
 * the shipped apps/blocks/pce_*.zxblock manifests parse, register, compose
 * into an app and answer through the app model. Run by `make test-pce`,
 * which passes kernel/src. */
#include <stdio.h>
#include <string.h>

#include "zxapp.h"
#include "zxapp_pce.h"

static int fails, checks;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL (%s:%d) ", __FILE__, __LINE__);                                         \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static const char *ROOT;
static char TEXT[3][8192];
static zxapp_t BLK[3], APP, BAD;
static zxapp_host_t H;
static zxapp_registry_t R;
static const char *const IDS[3] = {"pce_prob", "pce_strat", "pce_fin"};

static int read_block(uint32_t i)
{
    char path[512];
    snprintf(path, sizeof path, "%s/apps/blocks/%s.zxblock", ROOT, IDS[i]);
    FILE *f = fopen(path, "rb");
    if (f == NULL) return -1;
    size_t n = fread(TEXT[i], 1, sizeof TEXT[i] - 1u, f);
    fclose(f);
    TEXT[i][n] = '\0';
    return zxapp_parse(&BLK[i], TEXT[i], (uint32_t) n);
}

static int call(const char *entry, const zxapp_arg_t *a, uint32_t na, zxapp_num_t *out,
                uint32_t cap, uint32_t *n)
{
    return zxapp_call(&APP, &H, entry, a, na, out, cap, n);
}

static bool is(const zxapp_num_t *x, int64_t num, int64_t den)
{
    return x->num == num && x->den == den;
}

static void test_blocks(void)
{
    zxapp_host_init(&H);
    zxapp_registry_init(&R);
    CHECK(zxapp_pce_register(&H) == ZXAPP_OK && zxapp_host_has(&H, ZXAPP_LANG_C), "c runtime");
    static const uint32_t want[3] = {3, 3, 2};
    for (uint32_t i = 0; i < 3; i++) {
        CHECK(read_block(i) == ZXAPP_OK, "%s parses: %s (line %u)", IDS[i], BLK[i].err,
              BLK[i].err_line);
        CHECK(BLK[i].block && BLK[i].nprov == want[i] && BLK[i].n == want[i], "%s provides %u",
              IDS[i], want[i]);
        CHECK(zxapp_registry_add(&R, &BLK[i]) == ZXAPP_OK, "%s registers: %s", IDS[i], R.err);
    }
    static const char M[] = "zxapp 1\nid planner\nname Planner\n"
                            "use pce_prob 1\nuse pce_strat 1\nuse pce_fin 1\n";
    CHECK(zxapp_parse(&APP, M, (uint32_t) strlen(M)) == ZXAPP_OK, "app parses: %s", APP.err);
    CHECK(zxapp_compose(&APP, &R) == ZXAPP_OK && APP.n == 8, "composed: %s", APP.err);
    CHECK(zxapp_load(&APP, &H) == 8, "all eight entries load: %s", APP.err);
}

static void test_prob(void)
{
    zxapp_num_t o[8];
    uint32_t n = 0;
    /* Zadeh's case: two sources 99% sure of opposite answers. */
    static const int64_t sa[4] = {99, 1, 0, 0}, sb[4] = {1, 99, 0, 0};
    zxapp_arg_t j[2] = {{"a", sa, 4}, {"b", sb, 4}};
    CHECK(call("pce_prob.join", j, 2, o, 8, &n) == ZXAPP_OK && n == 4, "join");
    CHECK(is(&o[0], 99, 10000) && is(&o[1], 99, 10000) && is(&o[2], 4901, 5000) && is(&o[3], 0, 1),
          "98.02%% kept as BOTH, not divided away");
    CHECK(call("pce_prob.join", j, 2, o, 2, &n) == ZXAPP_E_BUFFER && n == 4, "cap: needs 4");

    static const int64_t th[4] = {9, 10, 1, 2}, joined[4] = {99, 99, 9802, 0},
                         unsure[4] = {50, 40, 0, 10};
    zxapp_arg_t v[2] = {{"m", sa, 4}, {"th", th, 4}};
    CHECK(call("pce_prob.verdict", v, 2, o, 8, &n) == ZXAPP_OK && n == 1 && is(&o[0], 1, 0),
          "99%% for: TRUE");
    v[0].v = joined;
    CHECK(call("pce_prob.verdict", v, 2, o, 8, &n) == ZXAPP_OK && is(&o[0], 3, 0),
          "the join: BOTH");
    v[0].v = unsure;
    CHECK(call("pce_prob.verdict", v, 2, o, 8, &n) == ZXAPP_OK && is(&o[0], 0, 0),
          "50/40: NEITHER");

    static const int64_t prior[2] = {1, 1}, like[2] = {9, 1}, den[1] = {10}, never[2] = {0, 0};
    zxapp_arg_t b[3] = {{"prior", prior, 2}, {"like", like, 2}, {"den", den, 1}};
    CHECK(call("pce_prob.bayes", b, 3, o, 8, &n) == ZXAPP_OK && n == 2 && is(&o[0], 9, 10) &&
              is(&o[1], 1, 10),
          "bayes");
    b[1].v = never;
    CHECK(call("pce_prob.bayes", b, 3, o, 8, &n) == ZXAPP_E_RUNTIME, "impossible evidence refused");

    /* contracts: the block's names first (B3), then the engine's shapes (P2) */
    zxapp_arg_t wrong[2] = {{"x", sa, 4}, {"b", sb, 4}};
    CHECK(call("pce_prob.join", wrong, 2, o, 8, &n) == ZXAPP_E_CONTRACT, "names checked");
    zxapp_arg_t shape[2] = {{"a", sa, 3}, {"b", sb, 4}};
    CHECK(call("pce_prob.join", shape, 2, o, 8, &n) == ZXAPP_E_RUNTIME &&
              strstr(APP.err, "outside its contract") != NULL,
          "a mass is four counts: %s", APP.err);
}

static void test_strat(void)
{
    zxapp_num_t o[4];
    uint32_t n = 0;
    static const int64_t agree[4] = {100, 30, 20, 25}, split[4] = {100, 0, 40, 40}, nopt[1] = {2},
                         lo[2] = {1, 1}, hi[2] = {1, 1}, den[1] = {2};
    zxapp_arg_t a[5] = {
        {"payoff", agree, 4}, {"nopt", nopt, 1}, {"lo", lo, 2}, {"hi", hi, 2}, {"den", den, 1}};
    CHECK(call("pce_strat.decide", a, 5, o, 4, &n) == ZXAPP_OK && n == 1 && is(&o[0], 1, 0),
          "three rules agree: TRUE");
    CHECK(call("pce_strat.leader", a, 5, o, 4, &n) == ZXAPP_OK && is(&o[0], 0, 1), "option 0");
    a[0].v = split;
    CHECK(call("pce_strat.decide", a, 5, o, 4, &n) == ZXAPP_OK && is(&o[0], 3, 0),
          "expected value against worst case: BOTH");
    CHECK(call("pce_strat.leader", a, 5, o, 4, &n) == ZXAPP_OK && is(&o[0], 0, 1),
          "two rules of three");

    static const int64_t s1[2] = {100, 110}, b1[2] = {150, 140}, s2[2] = {100, 200},
                         b2[2] = {150, 150}, s3[2] = {200, 300}, b3[2] = {150, 150};
    zxapp_arg_t z[2] = {{"seller_min", s1, 2}, {"buyer_max", b1, 2}};
    CHECK(call("pce_strat.zopa", z, 2, o, 4, &n) == ZXAPP_OK && is(&o[0], 1, 0), "a deal: TRUE");
    z[0].v = s2;
    z[1].v = b2;
    CHECK(call("pce_strat.zopa", z, 2, o, 4, &n) == ZXAPP_OK && is(&o[0], 3, 0),
          "estimates disagree: BOTH");
    z[0].v = s3;
    z[1].v = b3;
    CHECK(call("pce_strat.zopa", z, 2, o, 4, &n) == ZXAPP_OK && is(&o[0], 2, 0), "no deal: FALSE");
}

static void test_fin(void)
{
    zxapp_num_t o[4];
    uint32_t n = 0;
    static const int64_t cf[2] = {-100, 110}, rn[2] = {0, 2}, rd[2] = {1, 10};
    zxapp_arg_t a[3] = {{"cf", cf, 2}, {"rate_num", rn, 2}, {"rate_den", rd, 2}};
    CHECK(call("pce_fin.npv", a, 3, o, 4, &n) == ZXAPP_OK && n == 2 && is(&o[0], 10, 1) &&
              is(&o[1], -25, 3),
          "npv at 0%% and 20%%");
    CHECK(call("pce_fin.npv_sign", a, 3, o, 4, &n) == ZXAPP_OK && is(&o[0], 3, 0),
          "whose rate decides: BOTH");
    a[0].n = 2;
    a[1].n = 1;
    a[2].n = 1;
    CHECK(call("pce_fin.npv_sign", a, 3, o, 4, &n) == ZXAPP_OK && is(&o[0], 1, 0), "0%%: TRUE");
}

static void test_refusals(void)
{
    static const char *const bad[] = {
        "zxblock 1\nid b\nversion 1.0.0\nprovides x num a b\nlogic c x <<\nprob.nope\n.\n",
        "zxblock 1\nid b\nversion 1.0.0\nprovides x num a b\nlogic c x <<\nprob.join fin.npv\n.\n",
        "zxblock 1\nid b\nversion 1.0.0\nprovides x num a b\nlogic c x <<\n\n.\n",
    };
    for (uint32_t i = 0; i < 3; i++) {
        CHECK(zxapp_parse(&BAD, bad[i], (uint32_t) strlen(bad[i])) == ZXAPP_OK, "parses %u", i);
        CHECK(zxapp_load(&BAD, &H) == ZXAPP_E_RUNTIME, "refused at load %u: %s", i, BAD.err);
    }
    static const char ok[] = "zxblock 1\nid b\nversion 1.0.0\nprovides x num a b\n"
                             "logic c x <<\n  prob.join \n.\n";
    CHECK(zxapp_parse(&BAD, ok, (uint32_t) strlen(ok)) == ZXAPP_OK && zxapp_load(&BAD, &H) == 1,
          "blank space around the name is fine");
    zxapp_unload(&BAD, &H);
}

int main(int argc, char **argv)
{
    ROOT = argc > 1 ? argv[1] : "src";
    test_blocks();
    test_prob();
    test_strat();
    test_fin();
    test_refusals();
    zxapp_unload(&APP, &H);
    printf("%d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
