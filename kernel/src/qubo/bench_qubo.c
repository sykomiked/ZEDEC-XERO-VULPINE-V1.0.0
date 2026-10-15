/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* bench_qubo.c — host benchmark: parallel tempering vs local search vs
 * branch and bound on standard QUBO instance families.
 *
 * WHAT IT DOES: builds fixed-seed instances of four families
 *   dense    every coefficient uniform in [-100, 100]
 *   sparse   each pair present with probability 4/n, values in [-100, 100]
 *   maxcut   max-cut on G(n, p = min(1/2, 6/n)), unit weights, as a QUBO
 *            (minimise sum_edges 2 x_i x_j - x_i - x_j = -cut)
 *   planted  frustration-free pair penalties around a hidden x* plus one
 *            pinning term: the optimum is known to be exactly 0
 * at n = 16, 24, 32, 48, 64, runs the four solvers, and prints Markdown:
 * energy found per method, the proven lower bound, certificate status and
 * CPU time (clock()). Brute force is run as a cross-check for n <= 24.
 * Branch and bound is warm-started with the best heuristic answer (the
 * intended workbench workflow: heuristic answer, then certificate).
 * Every result is checked with qubo_verify. Doubles only for printing.
 *
 * HONEST LIMITS: single-threaded CPU time on whatever host runs it; varies
 * between machines. Synthetic instances, not customer problems. Budgets
 * are fixed, not tuned per instance.
 *
 * Build from kernel/:
 *   gcc -std=c11 -O2 -Wall -Wextra -Iinclude src/qubo/bench_qubo.c \
 *       src/qubo/qubo.c -o /tmp/bench_qubo
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "qubo.h"

#define SEEDS     3
#define BNB_NODES 300000ull

static qubo_t P;
static qubo_ws_t WS;
static uint64_t brng;

static uint64_t rnd(uint64_t m)
{
    return (qubo_rng_next(&brng) >> 11) % m;
}
static int64_t rr(int64_t lo, int64_t hi)
{
    return lo + (int64_t) rnd((uint64_t) (hi - lo + 1));
}
static void must(qubo_status_t st, const char *what)
{
    if (st != QUBO_OK) {
        printf("%s failed: %d\n", what, (int) st);
        exit(2);
    }
}

static const char *fam_name[4] = {"dense", "sparse", "maxcut", "planted"};

static void build(int fam, uint32_t n, uint64_t seed)
{
    uint32_t i, j;
    brng = 0x51ED2701u + seed * 0x9E3779B97F4A7C15ull + (uint64_t) fam * 7919u + n;
    must(qubo_init(&P, n), "init");
    if (fam == 0 || fam == 1) {
        for (i = 0; i < n; i++)
            for (j = i; j < n; j++)
                if (fam == 0 || i == j || rnd(n) < 4u) P.q[i][j] = rr(-100, 100);
    } else if (fam == 2) {
        uint64_t pn = 6u, pd = n; /* p = 6/n, capped at 1/2 */
        if (2u * pn > pd) {
            pn = 1;
            pd = 2;
        }
        for (i = 0; i < n; i++)
            for (j = i + 1u; j < n; j++)
                if (rnd(pd) < pn) {
                    must(qubo_add(&P, i, j, 2), "add");
                    must(qubo_add(&P, i, i, -1), "add");
                    must(qubo_add(&P, j, j, -1), "add");
                }
    } else {
        uint64_t plant = qubo_rng_next(&brng) & (n >= 64u ? ~0ull : ((1ull << n) - 1u));
        for (i = 0; i < n; i++)
            for (int d = 0; d < 3; d++) {
                int64_t w = rr(1, 9);
                j = (uint32_t) rnd(n);
                if (j == i) continue;
                if (((plant >> i) & 1u) == ((plant >> j) & 1u)) {
                    must(qubo_add(&P, i, i, w), "add");
                    must(qubo_add(&P, j, j, w), "add");
                    must(qubo_add(&P, i, j, -2 * w), "add");
                } else {
                    P.offset += w;
                    must(qubo_add(&P, i, i, -w), "add");
                    must(qubo_add(&P, j, j, -w), "add");
                    must(qubo_add(&P, i, j, 2 * w), "add");
                }
            }
        if (plant & 1u) {
            P.offset += 5;
            must(qubo_add(&P, 0, 0, -5), "add");
        } else {
            must(qubo_add(&P, 0, 0, 5), "add");
        }
        int64_t e;
        must(qubo_energy(&P, plant, &e), "energy");
        if (e != 0) {
            printf("planted energy %lld != 0\n", (long long) e);
            exit(2);
        }
    }
    must(qubo_check(&P), "check");
}

static double ms(clock_t a, clock_t b)
{
    return 1000.0 * (double) (b - a) / (double) CLOCKS_PER_SEC;
}

typedef struct {
    int pt_hit, ls_hit, opt, runs;
    double t_pt, t_ls, t_bnb;
} agg_t;

int main(void)
{
    static const uint32_t sizes[5] = {16, 24, 32, 48, 64};
    static agg_t agg[4][5];
    qubo_anneal_params_t ap = {16, 1000, 0};
    qubo_ls_params_t lp = {4000, 1ull << 40};
    int fam, si;
    uint64_t seed;

    printf("| family | n | PT energy | LS energy | B&B energy | proven bound | B&B status | "
           "brute force | PT ms | LS ms | B&B ms |\n");
    printf("|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (fam = 0; fam < 4; fam++)
        for (si = 0; si < 5; si++)
            for (seed = 0; seed < SEEDS; seed++) {
                uint32_t n = sizes[si];
                qubo_result_t rp, rl, rb, rf;
                uint64_t s, warm;
                clock_t t0, t1, t2, t3;
                int64_t ref;
                agg_t *a = &agg[fam][si];
                build(fam, n, seed);
                t0 = clock();
                s = 1000u + seed;
                must(qubo_anneal(&P, &ap, &s, &WS, &rp), "anneal");
                t1 = clock();
                s = 2000u + seed;
                must(qubo_local_search(&P, &lp, &s, &WS, &rl), "ls");
                t2 = clock();
                warm = rp.energy <= rl.energy ? rp.x : rl.x;
                must(qubo_bnb(&P, BNB_NODES, &warm, &WS, &rb), "bnb");
                t3 = clock();
                must(qubo_verify(&P, &rp), "verify pt");
                must(qubo_verify(&P, &rl), "verify ls");
                must(qubo_verify(&P, &rb), "verify bnb");
                if (rp.energy < rb.lower_bound || rl.energy < rb.lower_bound) {
                    printf("BOUND VIOLATED\n");
                    return 2;
                }
                if (fam == 3 && rb.lower_bound > 0) {
                    printf("PLANTED BOUND VIOLATED\n");
                    return 2;
                }
                char bf[32] = "-";
                if (n <= 24u) {
                    must(qubo_brute_force(&P, 1ull << 24, &WS, &rf), "brute");
                    if (rb.cert == QUBO_CERT_OPTIMAL && rf.energy != rb.energy) {
                        printf("B&B != brute force\n");
                        return 2;
                    }
                    snprintf(bf, sizeof bf, "%lld", (long long) rf.energy);
                }
                /* reference: proven optimum, or the best energy anyone found */
                ref = rb.energy;
                if (rp.energy == ref) a->pt_hit++;
                if (rl.energy == ref) a->ls_hit++;
                if (rb.cert == QUBO_CERT_OPTIMAL) a->opt++;
                a->runs++;
                a->t_pt += ms(t0, t1);
                a->t_ls += ms(t1, t2);
                a->t_bnb += ms(t2, t3);
                if (seed == 0)
                    printf("| %s | %u | %lld | %lld | %lld | %lld | %s | %s | %.1f | %.1f | %.1f "
                           "|\n",
                           fam_name[fam], n, (long long) rp.energy, (long long) rl.energy,
                           (long long) rb.energy, (long long) rb.lower_bound,
                           rb.cert == QUBO_CERT_OPTIMAL ? "OPTIMAL" : "GAP", bf, ms(t0, t1),
                           ms(t1, t2), ms(t2, t3));
                fflush(stdout);
            }

    printf("\nSummary over %d seeds per row (reference = B&B incumbent, which is the "
           "proven optimum when B&B status is OPTIMAL)\n\n",
           SEEDS);
    printf("| family | n | B&B proved optimal | PT hit ref | LS hit ref | mean PT ms | "
           "mean LS ms | mean B&B ms |\n");
    printf("|---|---|---|---|---|---|---|---|\n");
    for (fam = 0; fam < 4; fam++)
        for (si = 0; si < 5; si++) {
            agg_t *a = &agg[fam][si];
            printf("| %s | %u | %d/%d | %d/%d | %d/%d | %.1f | %.1f | %.1f |\n", fam_name[fam],
                   sizes[si], a->opt, a->runs, a->pt_hit, a->runs, a->ls_hit, a->runs,
                   a->t_pt / a->runs, a->t_ls / a->runs, a->t_bnb / a->runs);
        }
    return 0;
}
