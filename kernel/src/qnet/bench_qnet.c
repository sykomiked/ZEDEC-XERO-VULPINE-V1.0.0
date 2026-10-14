/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* bench_qnet.c — host benchmark: greedy vs exact vs annealing netting.
 *
 * WHAT IT DOES: builds fixed-seed random obligation graphs of several sizes
 * and densities plus structured cases (a ring, dense small circles, a
 * layered supply chain with back edges, repeated greedy traps), runs the
 * three solvers and prints a Markdown table: eliminated gross per solver,
 * gap to the exact optimum in percent, and CPU time per run (clock()).
 * Every result is checked with qn_verify; the exact one also with the
 * Bellman-Ford certificate. Doubles are used here only for printing.
 *
 * HONEST LIMITS: timings are single-threaded CPU time on whatever host runs
 * it and vary between machines; the graphs are synthetic, not real ledgers.
 *
 * Build from kernel/:
 *   gcc -std=c11 -O2 -Wall -Wextra -Iinclude src/qnet/bench_qnet.c \
 *       src/qnet/qnet.c -o /tmp/bench_qnet
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "qnet.h"

static qn_workspace_t ws;
static qn_result_t res;
static qn_graph_t G;
static uint64_t brng;

static uint32_t rnd(uint32_t n)
{
    return (uint32_t) (qn_rng_next(&brng) >> 33) % n;
}

static void edge(uint32_t a, uint32_t b, int64_t c)
{
    if (qn_graph_add(&G, a, b, c, NULL) != QN_OK) {
        printf("graph build failed\n");
        exit(2);
    }
}

static void gen_random(uint32_t n, uint32_t m, uint32_t maxcap, uint64_t seed)
{
    uint32_t e;
    brng = seed;
    qn_graph_init(&G, n);
    for (e = 0; e < m; e++) {
        uint32_t a = rnd(n), b = rnd(n - 1u);
        if (b >= a) b++;
        edge(a, b, 1 + (int64_t) rnd(maxcap));
    }
}

static void gen_ring(uint32_t n, uint64_t seed)
{
    uint32_t v;
    brng = seed;
    qn_graph_init(&G, n);
    for (v = 0; v < n; v++) edge(v, (v + 1u) % n, 1 + (int64_t) rnd(1000));
}

/* k disjoint complete digraphs ("circles") of size s. */
static void gen_circles(uint32_t k, uint32_t s, uint64_t seed)
{
    uint32_t c, i, j;
    brng = seed;
    qn_graph_init(&G, k * s);
    for (c = 0; c < k; c++)
        for (i = 0; i < s; i++)
            for (j = 0; j < s; j++)
                if (i != j) edge(c * s + i, c * s + j, 1 + (int64_t) rnd(1000));
}

/* L layers of w firms: each firm owes 3 random firms of the next layer
 * (goods flow down, invoices up), plus `back` random edges from a later
 * layer to an earlier one (credit, returns, services). */
static void gen_supply(uint32_t L, uint32_t w, uint32_t back, uint64_t seed)
{
    uint32_t l, i, k;
    brng = seed;
    qn_graph_init(&G, L * w);
    for (l = 0; l + 1u < L; l++)
        for (i = 0; i < w; i++)
            for (k = 0; k < 3; k++) edge(l * w + i, (l + 1u) * w + rnd(w), 1 + (int64_t) rnd(1000));
    for (k = 0; k < back; k++) {
        uint32_t hi = 1 + rnd(L - 1u), lo = rnd(hi);
        edge(hi * w + rnd(w), lo * w + rnd(w), 1 + (int64_t) rnd(1000));
    }
}

/* r copies of the greedy trap (see test_qnet.c), unit amounts. */
static void gen_traps(uint32_t r)
{
    uint32_t c, b;
    qn_graph_init(&G, 6 * r);
    for (c = 0; c < r; c++) {
        b = 6 * c;
        edge(b + 0, b + 1, 1);
        edge(b + 1, b + 2, 1);
        edge(b + 2, b + 3, 1);
        edge(b + 3, b + 0, 1);
        edge(b + 1, b + 4, 1);
        edge(b + 4, b + 0, 1);
        edge(b + 3, b + 5, 1);
        edge(b + 5, b + 2, 1);
    }
}

enum { S_GREEDY, S_EXACT, S_ANNEAL, S_ANNEAL_NP };

static int64_t run(int which, double *ms)
{
    qn_anneal_params_t p;
    clock_t t0, t1;
    int reps = 0;
    int64_t el = -1;
    qn_anneal_defaults(&p);
    if (which == S_ANNEAL_NP) p.polish = 0;
    t0 = clock();
    do {
        uint64_t seed = 0xC0FFEEu;
        qn_status_t st;
        if (which == S_GREEDY)
            st = qn_greedy_cycles(&G, &ws, &res);
        else if (which == S_EXACT)
            st = qn_exact_optimum(&G, &ws, &res);
        else
            st = qn_anneal(&G, &p, &seed, &ws, &res);
        if (st != QN_OK || qn_verify(&G, res.flow, &el) != QN_OK || el != res.eliminated) {
            printf("solver %d failed: %d\n", which, (int) st);
            exit(1);
        }
        if (which == S_EXACT && qn_check_optimal(&G, res.flow, &ws) != QN_OK) {
            printf("certificate failed\n");
            exit(1);
        }
        reps++;
        t1 = clock();
    } while ((double) (t1 - t0) < 0.05 * (double) CLOCKS_PER_SEC && reps < 1000);
    *ms = 1000.0 * (double) (t1 - t0) / (double) CLOCKS_PER_SEC / (double) reps;
    return el;
}

static double gap(int64_t x, int64_t opt)
{
    return opt ? 100.0 * (double) (opt - x) / (double) opt : 0.0;
}

static void report(const char *name)
{
    double tg, tx, ta, tn;
    int64_t g = run(S_GREEDY, &tg), x = run(S_EXACT, &tx), a = run(S_ANNEAL, &ta),
            n = run(S_ANNEAL_NP, &tn);
    printf("| %-22s | %2u | %3u | %8lld | %8lld | %5.2f | %8lld | %5.2f | %8lld | %5.2f | %7.3f | "
           "%7.3f | %7.2f | %7.2f |\n",
           name, G.num_nodes, G.num_edges, (long long) x, (long long) g, gap(g, x), (long long) a,
           gap(a, x), (long long) n, gap(n, x), tg, tx, ta, tn);
}

int main(void)
{
    char name[64];
    static const struct {
        uint32_t n, m, cap;
    } rc[] = {{8, 16, 1000},   {8, 32, 1000},   {16, 32, 1000},  {16, 96, 1000}, {32, 64, 1000},
              {32, 256, 1000}, {64, 128, 1000}, {64, 512, 1000}, {16, 96, 9},    {64, 512, 9}};
    uint32_t i, s;
    printf("| case | n | m | exact | greedy | gap %% | anneal | gap %% | anneal-np | gap %% | "
           "greedy ms | exact ms | anneal ms | anneal-np ms |\n");
    printf("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (i = 0; i < sizeof rc / sizeof rc[0]; i++)
        for (s = 1; s <= 2; s++) {
            snprintf(name, sizeof name, "random cap%u s%u", rc[i].cap, s);
            gen_random(rc[i].n, rc[i].m, rc[i].cap, 1000u * s + i);
            report(name);
        }
    gen_ring(32, 7);
    report("ring 32");
    gen_circles(1, 6, 8);
    report("circle K6");
    gen_circles(8, 5, 9);
    report("8 circles K5");
    gen_supply(4, 8, 8, 10);
    report("supply 4x8 +8 back");
    gen_supply(4, 16, 32, 11);
    report("supply 4x16 +32 back");
    gen_traps(10);
    report("10 greedy traps");
    return 0;
}
