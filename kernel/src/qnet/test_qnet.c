/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_qnet.c — host tests for the cycle-cancellation netting solvers.
 *
 * WHAT IT CHECKS: hand-computed optima (triangle, parallel edges, a graph
 * where greedy is provably suboptimal), brute force over every integer
 * circulation of small random graphs (exact == brute maximum, and the
 * Bellman-Ford certificate accepts exactly the maximum circulations),
 * randomized properties on larger graphs (feasibility, net positions
 * unchanged, exact >= greedy, exact >= anneal, anneal deterministic per
 * seed), overflow boundaries and every error path.
 *
 * HONEST LIMITS: QN_ERR_LIMIT (exact iteration guard) is not reachable at
 * the sizes tested here; the guard is a fixed constant.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "qnet.h"

static int g_pass, g_fail;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (c)                                                                                     \
            g_pass++;                                                                              \
        else {                                                                                     \
            g_fail++;                                                                              \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                    \
        }                                                                                          \
    } while (0)

static qn_workspace_t ws;
static qn_result_t rg, rx, ra, rb;
static qn_graph_t G;

static uint64_t trng = 12345u;
static uint32_t rnd(uint32_t n)
{
    return (uint32_t) (qn_rng_next(&trng) >> 33) % n;
}

static void add(qn_graph_t *g, uint32_t a, uint32_t b, int64_t c)
{
    CHECK(qn_graph_add(g, a, b, c, NULL) == QN_OK);
}

/* All result invariants: verify, nets unchanged, sum nets 0, eliminated. */
static void check_result(const qn_graph_t *g, const qn_result_t *r)
{
    int64_t n0[QN_MAX_NODES], n1[QN_MAX_NODES], el = -1, s0 = 0, s1 = 0, sum = 0;
    uint32_t v, e, same = 1;
    CHECK(qn_verify(g, r->flow, &el) == QN_OK);
    CHECK(el == r->eliminated);
    for (e = 0; e < g->num_edges; e++) {
        if (r->flow[e] < 0 || r->flow[e] > g->cap[e]) same = 0;
        sum += r->flow[e];
    }
    for (e = g->num_edges; e < QN_MAX_EDGES; e++)
        if (r->flow[e] != 0) same = 0;
    CHECK(sum == r->eliminated);
    CHECK(qn_net_positions(g, NULL, n0) == QN_OK);
    CHECK(qn_net_positions(g, r->flow, n1) == QN_OK);
    for (v = 0; v < g->num_nodes; v++) {
        if (n0[v] != n1[v]) same = 0;
        s0 += n0[v];
        s1 += n1[v];
    }
    CHECK(same);
    CHECK(s0 == 0 && s1 == 0);
}

static void run_all(const qn_graph_t *g, const qn_anneal_params_t *p, uint64_t seed)
{
    uint64_t s = seed;
    CHECK(qn_greedy_cycles(g, &ws, &rg) == QN_OK);
    check_result(g, &rg);
    CHECK(qn_exact_optimum(g, &ws, &rx) == QN_OK);
    check_result(g, &rx);
    CHECK(qn_check_optimal(g, rx.flow, &ws) == QN_OK);
    CHECK(qn_anneal(g, p, &s, &ws, &ra) == QN_OK);
    check_result(g, &ra);
    CHECK(rx.eliminated >= rg.eliminated);
    CHECK(rx.eliminated >= ra.eliminated);
}

static void test_rng_exp(void)
{
    uint64_t s = 0, a = 7, b = 7;
    uint32_t i, mono = 1, close = 1;
    CHECK(qn_rng_next(NULL) == 0);
    CHECK(qn_rng_next(&s) != 0 && s != 0);
    {
        uint64_t x = 0x9E3779B97F4A7C15ull;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        CHECK(s == x);
    }
    for (i = 0; i < 100; i++) CHECK(qn_rng_next(&a) == qn_rng_next(&b));
    CHECK(qn_exp_q24(0) == QN_EXP_ONE);
    CHECK(qn_exp_q24(QN_EXP_TABLE_N) == 0);
    CHECK(qn_exp_q24(QN_EXP_TABLE_N - 1u) > 0);
    for (i = 0; i < QN_EXP_TABLE_N; i++) {
        double ref = 16777216.0 * exp(-(double) i / 16.0);
        if (fabs((double) qn_exp_q24(i) - ref) > 1.0) close = 0;
        if (i > 0 && qn_exp_q24(i) > qn_exp_q24(i - 1u)) mono = 0;
    }
    CHECK(close);
    CHECK(mono);
    CHECK(qn_exp_q24(1) < qn_exp_q24(0) && qn_exp_q24(200) < qn_exp_q24(100));
    CHECK(qn_exp_q24(16) == 6171993u); /* round(2^24 / e) */
}

static void test_graph_errors(void)
{
    qn_graph_t g;
    uint32_t idx = 99, i;
    CHECK(qn_graph_init(NULL, 3) == QN_ERR_NULL);
    CHECK(qn_graph_init(&g, 0) == QN_ERR_RANGE);
    CHECK(qn_graph_init(&g, QN_MAX_NODES + 1u) == QN_ERR_RANGE);
    CHECK(qn_graph_init(&g, QN_MAX_NODES) == QN_OK);
    CHECK(g.num_nodes == QN_MAX_NODES && g.num_edges == 0);
    CHECK(qn_graph_init(&g, 3) == QN_OK);
    CHECK(qn_graph_add(NULL, 0, 1, 1, NULL) == QN_ERR_NULL);
    CHECK(qn_graph_add(&g, 3, 1, 1, NULL) == QN_ERR_RANGE);
    CHECK(qn_graph_add(&g, 0, 3, 1, NULL) == QN_ERR_RANGE);
    CHECK(qn_graph_add(&g, 1, 1, 1, NULL) == QN_ERR_SELF);
    CHECK(qn_graph_add(&g, 0, 1, 0, NULL) == QN_ERR_CAP);
    CHECK(qn_graph_add(&g, 0, 1, -5, NULL) == QN_ERR_CAP);
    CHECK(g.num_edges == 0);
    CHECK(qn_graph_add(&g, 0, 2, 1, &idx) == QN_OK && idx == 0);
    CHECK(qn_graph_add(&g, 2, 0, 1, &idx) == QN_OK && idx == 1);
    CHECK(g.from[1] == 2 && g.to[1] == 0 && g.cap[1] == 1);
    for (i = 2; i < QN_MAX_EDGES; i++) qn_graph_add(&g, i % 3u, (i + 1u) % 3u, 1, NULL);
    CHECK(g.num_edges == QN_MAX_EDGES);
    CHECK(qn_graph_add(&g, 0, 1, 1, NULL) == QN_ERR_FULL);
    CHECK(qn_graph_check(&g) == QN_OK);
    g.num_nodes = 0;
    CHECK(qn_graph_add(&g, 0, 1, 1, NULL) == QN_ERR_RANGE);
    g.num_nodes = QN_MAX_NODES + 1u;
    CHECK(qn_graph_add(&g, 0, 1, 1, NULL) == QN_ERR_RANGE);

    CHECK(qn_graph_check(NULL) == QN_ERR_NULL);
    qn_graph_init(&g, 3);
    add(&g, 0, 1, 2);
    add(&g, 1, 2, 2);
    CHECK(qn_graph_check(&g) == QN_OK);
    g.num_nodes = 0;
    CHECK(qn_graph_check(&g) == QN_ERR_RANGE);
    g.num_nodes = QN_MAX_NODES + 1u;
    CHECK(qn_graph_check(&g) == QN_ERR_RANGE);
    g.num_nodes = 3;
    g.num_edges = QN_MAX_EDGES + 1u;
    CHECK(qn_graph_check(&g) == QN_ERR_FULL);
    g.num_edges = 2;
    g.from[1] = 3;
    CHECK(qn_graph_check(&g) == QN_ERR_RANGE);
    g.from[1] = 1;
    g.to[1] = 3;
    CHECK(qn_graph_check(&g) == QN_ERR_RANGE);
    g.to[1] = 1;
    CHECK(qn_graph_check(&g) == QN_ERR_SELF);
    g.to[1] = 2;
    g.cap[1] = 0;
    CHECK(qn_graph_check(&g) == QN_ERR_CAP);
    g.cap[1] = -1;
    CHECK(qn_graph_check(&g) == QN_ERR_CAP);
    g.cap[0] = INT64_MAX;
    g.cap[1] = 1;
    CHECK(qn_graph_check(&g) == QN_ERR_OVERFLOW);
    g.cap[0] = INT64_MAX - 1;
    CHECK(qn_graph_check(&g) == QN_OK);
}

static void test_api_errors(void)
{
    qn_graph_t g, bad;
    qn_anneal_params_t p, q;
    int64_t f[QN_MAX_EDGES], nets[QN_MAX_NODES], el = 77;
    uint64_t s = 5;
    qn_graph_init(&g, 3);
    add(&g, 0, 1, 4);
    add(&g, 1, 2, 4);
    add(&g, 2, 0, 4);
    bad = g;
    bad.cap[0] = 0;
    memset(f, 0, sizeof f);
    CHECK(qn_anneal_defaults(NULL) == QN_ERR_NULL);
    CHECK(qn_anneal_defaults(&p) == QN_OK);
    CHECK(p.replicas == 8 && p.sweeps == 200 && p.moves_per_sweep == 64 && p.swap_interval == 1 &&
          p.max_cycle_len == 6 && p.polish == 1);

    CHECK(qn_net_positions(NULL, f, nets) == QN_ERR_NULL);
    CHECK(qn_net_positions(&g, f, NULL) == QN_ERR_NULL);
    CHECK(qn_net_positions(&bad, f, nets) == QN_ERR_CAP);
    f[0] = 5;
    CHECK(qn_net_positions(&g, f, nets) == QN_ERR_INFEASIBLE);
    f[0] = -1;
    CHECK(qn_net_positions(&g, f, nets) == QN_ERR_INFEASIBLE);
    f[0] = 4; /* in bounds but not conserving: nets accepts it */
    CHECK(qn_net_positions(&g, f, nets) == QN_OK);
    CHECK(nets[0] == 4 && nets[1] == -4 && nets[2] == 0);

    CHECK(qn_verify(NULL, f, &el) == QN_ERR_NULL);
    CHECK(qn_verify(&g, NULL, &el) == QN_ERR_NULL);
    CHECK(qn_verify(&bad, f, &el) == QN_ERR_CAP);
    CHECK(qn_verify(&g, f, &el) == QN_ERR_INFEASIBLE && el == 77);
    f[0] = f[1] = f[2] = 4;
    CHECK(qn_verify(&g, f, NULL) == QN_OK);
    CHECK(qn_verify(&g, f, &el) == QN_OK && el == 12);
    f[1] = 5;
    f[0] = f[2] = 5;
    CHECK(qn_verify(&g, f, &el) == QN_ERR_INFEASIBLE);
    f[0] = f[1] = f[2] = -1;
    CHECK(qn_verify(&g, f, &el) == QN_ERR_INFEASIBLE);
    f[0] = f[1] = f[2] = 0;
    f[1] = 1;
    CHECK(qn_verify(&g, f, &el) == QN_ERR_INFEASIBLE);
    f[1] = 0;

    CHECK(qn_check_optimal(NULL, f, &ws) == QN_ERR_NULL);
    CHECK(qn_check_optimal(&g, NULL, &ws) == QN_ERR_NULL);
    CHECK(qn_check_optimal(&g, f, NULL) == QN_ERR_NULL);
    CHECK(qn_check_optimal(&bad, f, &ws) == QN_ERR_CAP);
    CHECK(qn_check_optimal(&g, f, &ws) == QN_ERR_NOT_OPTIMAL);
    f[2] = 1;
    CHECK(qn_check_optimal(&g, f, &ws) == QN_ERR_INFEASIBLE);

    CHECK(qn_greedy_cycles(NULL, &ws, &rg) == QN_ERR_NULL);
    CHECK(qn_greedy_cycles(&g, NULL, &rg) == QN_ERR_NULL);
    CHECK(qn_greedy_cycles(&g, &ws, NULL) == QN_ERR_NULL);
    CHECK(qn_greedy_cycles(&bad, &ws, &rg) == QN_ERR_CAP);
    CHECK(qn_exact_optimum(NULL, &ws, &rx) == QN_ERR_NULL);
    CHECK(qn_exact_optimum(&g, NULL, &rx) == QN_ERR_NULL);
    CHECK(qn_exact_optimum(&g, &ws, NULL) == QN_ERR_NULL);
    CHECK(qn_exact_optimum(&bad, &ws, &rx) == QN_ERR_CAP);

    CHECK(qn_anneal(NULL, &p, &s, &ws, &ra) == QN_ERR_NULL);
    CHECK(qn_anneal(&g, NULL, &s, &ws, &ra) == QN_ERR_NULL);
    CHECK(qn_anneal(&g, &p, NULL, &ws, &ra) == QN_ERR_NULL);
    CHECK(qn_anneal(&g, &p, &s, NULL, &ra) == QN_ERR_NULL);
    CHECK(qn_anneal(&g, &p, &s, &ws, NULL) == QN_ERR_NULL);
    CHECK(qn_anneal(&bad, &p, &s, &ws, &ra) == QN_ERR_CAP);
    s = 0;
    CHECK(qn_anneal(&g, &p, &s, &ws, &ra) == QN_ERR_PARAM);
    s = 5;
#define BADP(field, val)                                                                           \
    do {                                                                                           \
        q = p;                                                                                     \
        q.field = (val);                                                                           \
        CHECK(qn_anneal(&g, &q, &s, &ws, &ra) == QN_ERR_PARAM);                                    \
    } while (0)
    BADP(replicas, 0);
    BADP(replicas, QN_MAX_REPLICAS + 1u);
    BADP(sweeps, 0);
    BADP(moves_per_sweep, 0);
    BADP(swap_interval, 0);
    BADP(max_cycle_len, 1);
    BADP(max_cycle_len, QN_CYC_MAX_LEN + 1u);
    BADP(polish, 2);
    q = p;
    q.replicas = 8;
    q.sweeps = 0x80000000u; /* 8 * 2^31 > limit */
    CHECK(qn_anneal(&g, &q, &s, &ws, &ra) == QN_ERR_PARAM);
    q.sweeps = 1000;
    q.moves_per_sweep = 1000000u; /* 8e9 > limit */
    CHECK(qn_anneal(&g, &q, &s, &ws, &ra) == QN_ERR_PARAM);
    CHECK(s == 5); /* refused calls do not advance the PRNG */
    /* boundary values that are accepted */
    q = p;
    q.replicas = QN_MAX_REPLICAS;
    q.max_cycle_len = QN_CYC_MAX_LEN;
    q.sweeps = 1;
    q.moves_per_sweep = 1;
    CHECK(qn_anneal(&g, &q, &s, &ws, &ra) == QN_OK);
    q.replicas = 1;
    q.max_cycle_len = 2;
    q.polish = 0;
    CHECK(qn_anneal(&g, &q, &s, &ws, &ra) == QN_OK);
    CHECK(ra.eliminated == 0 && ra.iterations == 0); /* no 2-cycles here */
}

static void test_triangle(void)
{
    qn_graph_t g;
    qn_anneal_params_t p;
    int64_t n[QN_MAX_NODES];
    uint64_t s = 42;
    qn_graph_init(&g, 3);
    add(&g, 0, 1, 5);
    add(&g, 1, 2, 3);
    add(&g, 2, 0, 4);
    CHECK(qn_net_positions(&g, NULL, n) == QN_OK);
    CHECK(n[0] == -1 && n[1] == 2 && n[2] == -1);
    qn_anneal_defaults(&p);
    run_all(&g, &p, 9);
    CHECK(rg.eliminated == 9 && rx.eliminated == 9 && ra.eliminated == 9);
    CHECK(rg.flow[0] == 3 && rg.flow[1] == 3 && rg.flow[2] == 3);
    CHECK(rx.flow[0] == 3 && rx.flow[1] == 3 && rx.flow[2] == 3);
    CHECK(rg.cycles == 1 && rg.iterations == 2);
    CHECK(rx.cycles == 1 && rx.iterations == 2);
    CHECK(qn_net_positions(&g, rx.flow, n) == QN_OK);
    CHECK(n[0] == -1 && n[1] == 2 && n[2] == -1);
    /* annealing alone (no polish) finds the triangle too */
    p.polish = 0;
    p.replicas = 2;
    p.sweeps = 10;
    CHECK(qn_anneal(&g, &p, &s, &ws, &ra) == QN_OK);
    check_result(&g, &ra);
    CHECK(ra.eliminated == 9);
    CHECK(ra.iterations == 2u * 10u * 64u);
}

/* Greedy trap: the long cycle 0-1-2-3-0 (found first because edges are
 * scanned in index order) uses one edge of each short 3-cycle 0-1-4-0 and
 * 2-3-5-2. Greedy cancels it (4 units) and the short cycles die; the
 * optimum cancels both 3-cycles (6 units). */
static void build_trap(qn_graph_t *g)
{
    qn_graph_init(g, 6);
    add(g, 0, 1, 1); /* e0 */
    add(g, 1, 2, 1); /* e1 long */
    add(g, 2, 3, 1); /* e2 */
    add(g, 3, 0, 1); /* e3 long */
    add(g, 1, 4, 1); /* e4 */
    add(g, 4, 0, 1); /* e5 */
    add(g, 3, 5, 1); /* e6 */
    add(g, 5, 2, 1); /* e7 */
}

static void test_trap(void)
{
    qn_graph_t g;
    qn_anneal_params_t p;
    uint64_t s = 1;
    build_trap(&g);
    qn_anneal_defaults(&p);
    run_all(&g, &p, 3);
    CHECK(rg.eliminated == 4);
    CHECK(rx.eliminated == 6);
    CHECK(rg.eliminated < rx.eliminated);
    CHECK(rg.flow[1] == 1 && rg.flow[3] == 1 && rg.flow[4] == 0 && rg.flow[6] == 0);
    CHECK(rx.flow[1] == 0 && rx.flow[3] == 0 && rx.flow[4] == 1 && rx.flow[6] == 1);
    CHECK(qn_check_optimal(&g, rg.flow, &ws) == QN_ERR_NOT_OPTIMAL);
    CHECK(qn_check_optimal(&g, rx.flow, &ws) == QN_OK);
    CHECK(ra.eliminated == 6);
    /* cycles of length <= 2 do not exist: annealing alone does nothing */
    p.max_cycle_len = 2;
    p.polish = 0;
    CHECK(qn_anneal(&g, &p, &s, &ws, &ra) == QN_OK);
    CHECK(ra.eliminated == 0 && ra.cycles == 0 && ra.iterations == 0);
    /* ...but polish then equals greedy */
    p.polish = 1;
    CHECK(qn_anneal(&g, &p, &s, &ws, &ra) == QN_OK);
    CHECK(ra.eliminated == 4);
    /* length 3 is enough to see both short cycles */
    p.max_cycle_len = 3;
    p.polish = 0;
    CHECK(qn_anneal(&g, &p, &s, &ws, &ra) == QN_OK);
    CHECK(ra.eliminated == 6);
}

static void test_small_cases(void)
{
    qn_graph_t g;
    qn_anneal_params_t p;
    qn_anneal_defaults(&p);
    /* parallel edges both ways */
    qn_graph_init(&g, 2);
    add(&g, 0, 1, 5);
    add(&g, 1, 0, 3);
    add(&g, 0, 1, 2);
    run_all(&g, &p, 4);
    CHECK(rx.eliminated == 6 && rg.eliminated == 6 && ra.eliminated == 6);
    CHECK(rx.flow[1] == 3 && rx.flow[0] + rx.flow[2] == 3);
    /* acyclic: nothing to cancel, zero flow is optimal */
    qn_graph_init(&g, 4);
    add(&g, 0, 1, 5);
    add(&g, 1, 2, 5);
    add(&g, 0, 2, 5);
    add(&g, 2, 3, 5);
    run_all(&g, &p, 4);
    CHECK(rx.eliminated == 0 && rg.eliminated == 0 && ra.eliminated == 0);
    CHECK(rg.cycles == 0 && rx.cycles == 0 && rg.iterations == 1);
    /* single node, no edges */
    qn_graph_init(&g, 1);
    run_all(&g, &p, 4);
    CHECK(rx.eliminated == 0);
    /* backward residual arcs needed: greedy-like start must be undone.
     * Exact must reroute through backward arcs; check against hand value. */
    build_trap(&g);
    run_all(&g, &p, 11);
    CHECK(rx.eliminated == 6);
}

static void test_overflow_boundary(void)
{
    qn_graph_t g;
    qn_anneal_params_t p;
    const int64_t a = (int64_t) 1 << 62;
    qn_anneal_defaults(&p);
    qn_graph_init(&g, 3);
    add(&g, 0, 1, a - 1);
    add(&g, 1, 0, a);
    CHECK(qn_graph_check(&g) == QN_OK);
    run_all(&g, &p, 8);
    CHECK(rx.eliminated == 2 * (a - 1));
    CHECK(rg.eliminated == 2 * (a - 1));
    CHECK(ra.eliminated == 2 * (a - 1));
    add(&g, 1, 2, 1);
    CHECK(qn_graph_check(&g) == QN_ERR_OVERFLOW);
    CHECK(qn_exact_optimum(&g, &ws, &rx) == QN_ERR_OVERFLOW);
    CHECK(qn_greedy_cycles(&g, &ws, &rg) == QN_ERR_OVERFLOW);
    {
        uint64_t s = 3;
        CHECK(qn_anneal(&g, &p, &s, &ws, &ra) == QN_ERR_OVERFLOW);
    }
    /* large but heterogeneous amounts: anneal with polish 0 must stay
     * feasible (exercises the scaled acceptance arithmetic) */
    qn_graph_init(&g, 4);
    add(&g, 0, 1, a / 4);
    add(&g, 1, 2, 3);
    add(&g, 2, 0, a / 8);
    add(&g, 1, 0, 1000);
    add(&g, 2, 3, a / 16);
    add(&g, 3, 1, 77);
    p.polish = 0;
    run_all(&g, &p, 21);
    /* 2-cycle 0-1-0 cancels 1000 (2000 gross); edge 1->2 (3) closes one
     * 3-cycle either way: 9 more. */
    CHECK(rx.eliminated == 2009);
    CHECK(ra.eliminated <= 2009);
}

/* Brute force every integer circulation on tiny graphs. */
static void test_brute(void)
{
    uint32_t trial, ok_cert = 1, ok_max = 1, ok_g = 1, ok_a = 1;
    qn_anneal_params_t p;
    qn_anneal_defaults(&p);
    p.sweeps = 30;
    p.moves_per_sweep = 16;
    for (trial = 0; trial < 300; trial++) {
        qn_graph_t g;
        uint32_t n = 2 + rnd(3), m = 1 + rnd(7), e, total = 1;
        int64_t f[QN_MAX_EDGES], best = -1;
        uint32_t k;
        uint64_t s = 1000u + trial;
        qn_graph_init(&g, n);
        for (e = 0; e < m; e++) {
            uint32_t a = rnd(n), b = rnd(n - 1u);
            if (b >= a) b++;
            add(&g, a, b, 1 + rnd(3));
            total *= (uint32_t) (g.cap[e] + 1);
        }
        /* pass 1: maximum */
        for (k = 0; k < total; k++) {
            uint32_t x = k;
            int64_t el;
            for (e = 0; e < m; e++) {
                f[e] = x % (uint32_t) (g.cap[e] + 1);
                x /= (uint32_t) (g.cap[e] + 1);
            }
            if (qn_verify(&g, f, &el) == QN_OK && el > best) best = el;
        }
        /* pass 2: certificate holds exactly on the maximum circulations */
        for (k = 0; k < total; k++) {
            uint32_t x = k;
            int64_t el;
            for (e = 0; e < m; e++) {
                f[e] = x % (uint32_t) (g.cap[e] + 1);
                x /= (uint32_t) (g.cap[e] + 1);
            }
            if (qn_verify(&g, f, &el) != QN_OK) continue;
            if ((qn_check_optimal(&g, f, &ws) == QN_OK) != (el == best)) ok_cert = 0;
        }
        CHECK(qn_exact_optimum(&g, &ws, &rx) == QN_OK);
        CHECK(qn_greedy_cycles(&g, &ws, &rg) == QN_OK);
        CHECK(qn_anneal(&g, &p, &s, &ws, &ra) == QN_OK);
        if (rx.eliminated != best) ok_max = 0;
        if (rg.eliminated > best) ok_g = 0;
        if (ra.eliminated > best) ok_a = 0;
    }
    CHECK(ok_cert);
    CHECK(ok_max);
    CHECK(ok_g);
    CHECK(ok_a);
}

static void random_graph(qn_graph_t *g, uint32_t n, uint32_t m, int64_t maxcap)
{
    uint32_t e;
    qn_graph_init(g, n);
    for (e = 0; e < m; e++) {
        uint32_t a = rnd(n), b = rnd(n - 1u);
        if (b >= a) b++;
        add(g, a, b, 1 + (int64_t) rnd((uint32_t) maxcap));
    }
}

static void test_random_props(void)
{
    uint32_t trial, greedy_lt = 0, anneal_gt_greedy = 0;
    qn_anneal_params_t p;
    qn_anneal_defaults(&p);
    p.sweeps = 40;
    for (trial = 0; trial < 60; trial++) {
        uint32_t n = 4 + rnd(29), m = n + rnd(4u * n);
        uint64_t s1 = 77u + trial, s2 = 77u + trial;
        uint32_t e, same = 1;
        random_graph(&G, n, m, trial % 2 ? 1000 : 9);
        run_all(&G, &p, 500u + trial);
        if (rg.eliminated < rx.eliminated) greedy_lt++;
        if (ra.eliminated > rg.eliminated) anneal_gt_greedy++;
        /* determinism: same seed -> identical result and PRNG state */
        CHECK(qn_anneal(&G, &p, &s1, &ws, &ra) == QN_OK);
        CHECK(qn_anneal(&G, &p, &s2, &ws, &rb) == QN_OK);
        for (e = 0; e < QN_MAX_EDGES; e++)
            if (ra.flow[e] != rb.flow[e]) same = 0;
        CHECK(same && ra.eliminated == rb.eliminated && s1 == s2);
        CHECK(ra.cycles == rb.cycles && ra.iterations == rb.iterations);
        /* exact twice gives the same answer */
        CHECK(qn_exact_optimum(&G, &ws, &rb) == QN_OK);
        CHECK(rb.eliminated == rx.eliminated);
        /* the greedy remainder is acyclic: greedy on it cancels nothing */
        CHECK(qn_greedy_cycles(&G, &ws, &rb) == QN_OK);
        CHECK(rb.eliminated == rg.eliminated);
    }
    /* the generator must actually exercise the greedy gap */
    CHECK(greedy_lt > 0);
    printf("random: greedy<exact in %u/60, anneal>greedy in %u/60\n", greedy_lt, anneal_gt_greedy);
}

static void test_full_capacity(void)
{
    qn_anneal_params_t p;
    qn_anneal_defaults(&p);
    p.sweeps = 20;
    random_graph(&G, QN_MAX_NODES, QN_MAX_EDGES, 100000);
    CHECK(G.num_edges == QN_MAX_EDGES);
    run_all(&G, &p, 99);
    CHECK(rx.eliminated > 0);
}

static void test_metropolis(void)
{
    uint64_t s = 99, s0;
    uint32_t i, acc0 = 0, acc16 = 0, acc32 = 0;
    CHECK(qn_metropolis_accept(NULL, 0) == 0);
    s0 = s;
    CHECK(qn_metropolis_accept(&s, QN_EXP_TABLE_N) == 0);
    CHECK(qn_metropolis_accept(&s, ~0ull) == 0);
    CHECK(s == s0); /* early rejects draw nothing */
    for (i = 0; i < 100000u; i++) {
        acc0 += (uint32_t) qn_metropolis_accept(&s, 0);
        acc16 += (uint32_t) qn_metropolis_accept(&s, 16);
        acc32 += (uint32_t) qn_metropolis_accept(&s, 32);
    }
    CHECK(s != s0);
    CHECK(acc0 == 100000u);
    /* e^-1 = 0.3679, e^-2 = 0.1353; 100k draws: sd about 0.0015 */
    CHECK(acc16 > 36000u && acc16 < 37600u);
    CHECK(acc32 > 13000u && acc32 < 14100u);
    printf("metropolis: x=1 %u, x=2 %u per 100000\n", acc16, acc32);
}

/* Regression: the annealer is deterministic, so a fixed graph and seed give
 * fixed outputs. These numbers were recorded from this implementation; a
 * change to the move, acceptance or exchange rules changes them. */
static uint64_t flow_hash(const qn_result_t *r)
{
    uint64_t h = 1469598103934665603ull;
    uint32_t e;
    for (e = 0; e < QN_MAX_EDGES; e++) h = (h ^ (uint64_t) r->flow[e]) * 1099511628211ull;
    return h;
}

static void test_golden(void)
{
    qn_anneal_params_t p;
    uint64_t s = 2024;
    qn_anneal_defaults(&p);
    p.polish = 0;
    p.sweeps = 50;
    trng = 4242;
    random_graph(&G, 24, 120, 1000);
    CHECK(qn_anneal(&G, &p, &s, &ws, &ra) == QN_OK);
    check_result(&G, &ra);
    CHECK(ra.eliminated == 28066);
    CHECK(ra.cycles == 5956u);
    CHECK(ra.iterations == 25600u);
    CHECK(s == 5732118736900408124ull);
    CHECK(flow_hash(&ra) == 9957886569382536525ull);
    /* dense, tiny amounts (many ties), long cycles: fills the cycle list */
    trng = 777;
    random_graph(&G, QN_MAX_NODES, QN_MAX_EDGES, 3);
    p.max_cycle_len = QN_CYC_MAX_LEN;
    p.sweeps = 20;
    s = 31337;
    CHECK(qn_anneal(&G, &p, &s, &ws, &ra) == QN_OK);
    check_result(&G, &ra);
    CHECK(ws.num_cycles == 2683u);
    CHECK(ra.eliminated == 205 && ra.cycles == 208u && ra.iterations == 10240u);
    CHECK(flow_hash(&ra) == 12352427802871113822ull);
    /* huge amounts: exercises the scaled acceptance arithmetic */
    qn_graph_init(&G, 5);
    add(&G, 0, 1, (int64_t) 1 << 60);
    add(&G, 1, 2, (int64_t) 1 << 59);
    add(&G, 2, 0, (int64_t) 1 << 58);
    add(&G, 1, 0, ((int64_t) 1 << 57) + 12345);
    add(&G, 2, 3, (int64_t) 1 << 56);
    add(&G, 3, 4, 999999999);
    add(&G, 4, 1, (int64_t) 1 << 55);
    p.max_cycle_len = 6;
    s = 5;
    CHECK(qn_anneal(&G, &p, &s, &ws, &ra) == QN_OK);
    check_result(&G, &ra);
    CHECK(ra.eliminated == 1152921508606871662ll && ra.cycles == 3698u);
    CHECK(flow_hash(&ra) == 1372561744975398897ull);
}

int main(void)
{
    test_rng_exp();
    test_graph_errors();
    test_api_errors();
    test_triangle();
    test_trap();
    test_small_cases();
    test_overflow_boundary();
    test_brute();
    test_random_props();
    test_full_capacity();
    test_metropolis();
    test_golden();
    printf("%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
