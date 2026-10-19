/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* bench_gridlock.c — host benchmark: removal heuristic vs exact gridlock
 * resolution.
 *
 * WHAT IT DOES: builds fixed-seed random payment queues (20..256 payments
 * over 10..64 participants, amounts 1..1000) at three liquidity levels
 * (each participant's balance = 0 %, 10 % or 40 % of the value it has
 * queued), and prints Markdown tables:
 *   - free mode: settled value for the removal heuristic (LAST and LARGEST
 *     rules) and for gl_solve (optimum, or best found + proven upper bound
 *     when the node budget runs out), gaps in percent, nodes, CPU time;
 *   - FIFO mode: heuristic vs gl_solve (identical by the theorem in
 *     gridlock.c) and the free-mode optimum, to show what FIFO costs.
 * Every result is re-checked with gl_verify. Doubles are used only for
 * printing.
 *
 * HONEST LIMITS: synthetic queues, not real RTGS data; timings are
 * single-threaded clock() on whatever host runs it.
 *
 * Build from kernel/:
 *   gcc -std=c11 -O2 -Wall -Wextra -Iinclude src/gridlock/bench_gridlock.c \
 *       src/gridlock/gridlock.c -o /tmp/bench_gridlock
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "gridlock.h"

static gl_instance_t I;
static gl_workspace_t ws;
static gl_result_t r1, r2, r3;
static gl_opts_t O;
static uint64_t brng;

static uint32_t rnd(uint32_t n)
{
    brng ^= brng << 13;
    brng ^= brng >> 7;
    brng ^= brng << 17;
    return (uint32_t) (brng >> 33) % n;
}

static void gen(uint32_t np, uint32_t n, uint32_t liq_pct, uint64_t seed)
{
    int64_t out[GL_MAX_PART] = {0};
    uint32_t i, p;
    brng = seed;
    gl_init(&I, np);
    for (i = 0; i < n; i++) {
        uint32_t a = rnd(np), b = rnd(np - 1u);
        int64_t amt = 1 + (int64_t) rnd(1000);
        if (b >= a) b++;
        if (gl_add_payment(&I, a, b, amt, rnd(GL_MAX_CLASSES), i, NULL) != GL_OK) exit(2);
        out[a] += amt;
    }
    for (p = 0; p < np; p++) gl_set_balance(&I, p, out[p] * (int64_t) liq_pct / 100, 0);
}

static double ms_since(clock_t t0)
{
    return (double) (clock() - t0) * 1000.0 / (double) CLOCKS_PER_SEC;
}

static void must(gl_status_t st, const gl_result_t *r)
{
    if (st != GL_OK || gl_verify(&I, &O, r->settle, NULL, NULL, NULL) != GL_OK) {
        printf("solver or verification failed (%d)\n", (int) st);
        exit(3);
    }
}

static double gap(int64_t best, int64_t x)
{
    return best > 0 ? 100.0 * (double) (best - x) / (double) best : 0.0;
}

int main(int argc, char **argv)
{
    static const uint32_t sizes[][2] = {{10, 20},  {10, 50},  {32, 50}, {32, 100},
                                        {64, 100}, {32, 256}, {64, 256}};
    static const uint32_t liq[] = {0, 10, 40};
    uint64_t budget = argc > 1 ? strtoull(argv[1], NULL, 10) : 2000000u;
    uint32_t si, li, seed;
    unsigned opt_count = 0, rows = 0, h_less = 0;
    double sum_gap_last = 0, sum_gap_large = 0;

    printf("Free mode, objective = total value, node budget %llu\n\n", (unsigned long long) budget);
    printf("| parts | pays | liq %% | seed | queued | heur LAST | gap %% | heur LARGEST | gap %% | "
           "exact/best | upper bound | bound gap %% | status | nodes | heur ms | exact ms |\n");
    printf("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (si = 0; si < sizeof sizes / sizeof sizes[0]; si++)
        for (li = 0; li < sizeof liq / sizeof liq[0]; li++)
            for (seed = 1; seed <= 2; seed++) {
                int64_t queued = 0;
                uint32_t i;
                clock_t t0;
                double th, tx;
                gen(sizes[si][0], sizes[si][1], liq[li], 0x5EED0000ull + seed * 7919u + si * 31u);
                for (i = 0; i < I.num_pay; i++) queued += I.amount[i];
                gl_opts_default(&O);
                O.node_budget = budget;
                t0 = clock();
                must(gl_heuristic(&I, &O, &ws, &r1), &r1);
                th = ms_since(t0);
                O.removal = GL_RM_LARGEST;
                must(gl_heuristic(&I, &O, &ws, &r2), &r2);
                O.removal = GL_RM_LAST;
                t0 = clock();
                must(gl_solve(&I, &O, &ws, &r3), &r3);
                tx = ms_since(t0);
                rows++;
                if (r3.quality == GL_OPTIMAL) opt_count++;
                if (r1.value < r3.value) h_less++;
                sum_gap_last += gap(r3.value, r1.value);
                sum_gap_large += gap(r3.value, r2.value);
                printf(
                    "| %u | %u | %u | %u | %lld | %lld | %.2f | %lld | %.2f | %lld | %lld | %.2f | "
                    "%s | %llu | %.3f | %.1f |\n",
                    sizes[si][0], sizes[si][1], liq[li], seed, (long long) queued,
                    (long long) r1.value, gap(r3.value, r1.value), (long long) r2.value,
                    gap(r3.value, r2.value), (long long) r3.value, (long long) r3.ub_value,
                    gap(r3.ub_value, r3.value), r3.quality == GL_OPTIMAL ? "optimal" : "best+bound",
                    (unsigned long long) r3.nodes, th, tx);
            }
    printf("\nrows %u, proven optimal %u, heuristic LAST below exact/best in %u, mean gap LAST "
           "%.2f %%, LARGEST %.2f %%\n",
           rows, opt_count, h_less, sum_gap_last / rows, sum_gap_large / rows);

    printf("\nFIFO mode (per-sender queue order), objective = total value\n\n");
    printf(
        "| parts | pays | liq %% | seed | heur | solve | free-mode best | FIFO cost %% | solve ms "
        "|\n");
    printf("|---|---|---|---|---|---|---|---|---|\n");
    for (si = 0; si < sizeof sizes / sizeof sizes[0]; si++)
        for (li = 0; li < sizeof liq / sizeof liq[0]; li++) {
            clock_t t0;
            double tf;
            gen(sizes[si][0], sizes[si][1], liq[li], 0x5EED0000ull + 7919u + si * 31u);
            gl_opts_default(&O);
            O.fifo = 1;
            must(gl_heuristic(&I, &O, &ws, &r1), &r1);
            t0 = clock();
            must(gl_solve(&I, &O, &ws, &r2), &r2);
            tf = ms_since(t0);
            if (r1.value != r2.value) {
                printf("FIFO heuristic differs from solve\n");
                return 4;
            }
            O.fifo = 0;
            O.node_budget = budget;
            must(gl_solve(&I, &O, &ws, &r3), &r3);
            printf("| %u | %u | %u | 1 | %lld | %lld | %lld%s | %.2f | %.3f |\n", sizes[si][0],
                   sizes[si][1], liq[li], (long long) r1.value, (long long) r2.value,
                   (long long) r3.value, r3.quality == GL_OPTIMAL ? "" : " (best)",
                   gap(r3.value, r2.value), tf);
        }
    return 0;
}
