/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* bench_xrisk.c — exact loss distribution vs plain Monte Carlo (host only).
 *
 * WHAT IT DOES
 *   Builds seeded random credit portfolios of 50..256 exposures (integer loss
 *   units 1..16, default probabilities 5..300 basis points over 10^4), plus a
 *   two-scenario mixture (normal / stressed) to model correlation. Computes the
 *   exact distribution with xrisk and times it. Then runs plain Monte Carlo
 *   (double uniforms, seeded splitmix64) at 10^4, 10^5 and 10^6 samples, several
 *   independent seeds each, and reports MC's error against the exact answer
 *   for the 99% and 99.9% VaR and for the tail probability P(L >= exact
 *   VaR99.9), plus the times.
 *
 * HONEST LIMITS
 *   One machine, one compiler, plain MC (no variance reduction such as
 *   importance sampling, which would shrink MC's tail error). The exact
 *   values are converted to double ONLY for the error report. Classical vs
 *   classical: nothing here measures or claims anything about quantum hardware.
 */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "xrisk.h"

static uint64_t g_s;
static uint64_t sm64(void)
{
    uint64_t z = (g_s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
static double u01(void)
{
    return (double) (sm64() >> 11) * (1.0 / 9007199254740992.0);
}

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double) t.tv_sec + (double) t.tv_nsec * 1e-9;
}

static double rat_d(const xrisk_rat_t *r)
{
    char buf[128];
    if (xrisk_rat_to_dec(r, 30, buf, sizeof buf)) return -1.0;
    return strtod(buf, NULL);
}

#define NMAX 256
static xrisk_model_t g_m;
static xrisk_dist_t *g_d;
static uint32_t g_loss[NMAX];
static double g_p[2][NMAX], g_w0;
static uint32_t g_hist[XRISK_MAX_UNITS + 1];

/* smallest k with cum >= a/b * S (integer comparison) */
static uint32_t mc_var(uint32_t total, uint64_t S, uint64_t a, uint64_t b)
{
    uint64_t c = 0;
    uint32_t k;
    for (k = 0; k <= total; k++) {
        c += g_hist[k];
        if (c * b >= a * S) return k;
    }
    return total;
}

static void run(int n, int mix, uint64_t seed)
{
    uint32_t i, total = 0, v99, v999;
    int s, K = mix ? 2 : 1, rep;
    double t0, t_exact, tail_ex;
    xrisk_rat_t r;
    char es_buf[64];
    static const uint64_t Ss[3] = {10000, 100000, 1000000};

    g_s = seed;
    if (xrisk_model_init(&g_m, (uint32_t) n, (uint32_t) K)) exit(2);
    if (mix) {
        xrisk_model_set_weight(&g_m, 0, 9, 10);
        xrisk_model_set_weight(&g_m, 1, 1, 10);
        g_w0 = 0.9;
    } else {
        g_w0 = 1.0;
    }
    for (i = 0; i < (uint32_t) n; i++) {
        int64_t bp = 5 + (int64_t) (sm64() % 296);
        g_loss[i] = 1 + (uint32_t) (sm64() % 16);
        total += g_loss[i];
        xrisk_model_set_loss(&g_m, i, g_loss[i]);
        for (s = 0; s < K; s++) {
            int64_t b = s ? bp * 4 : bp; /* stressed scenario: 4x default rates */
            if (xrisk_model_set_p(&g_m, (uint32_t) s, i, b, 10000)) exit(2);
            g_p[s][i] = (double) b / 10000.0;
        }
    }
    t0 = now_s();
    if (xrisk_compute(&g_m, XRISK_SIDE_LOW, g_d)) {
        printf("exact compute refused\n");
        return;
    }
    if (xrisk_var(g_d, 99, 100, &v99) || xrisk_var(g_d, 999, 1000, &v999)) exit(3);
    t_exact = now_s() - t0;
    xrisk_tail(g_d, v999, &r);
    tail_ex = rat_d(&r);
    xrisk_es(g_d, 999, 1000, &r);
    xrisk_rat_to_dec(&r, 3, es_buf, sizeof es_buf);
    printf("\n## n=%d %s  total_units=%u  den_bits=%u  exact_time=%.1f ms\n", n,
           mix ? "mixture(0.9 normal / 0.1 stressed x4)" : "independent", total,
           xrisk_big_bits(&g_d->den), t_exact * 1e3);
    printf("exact: VaR99=%u  VaR99.9=%u  ES99.9=%s  P(L>=VaR99.9)=%.6e\n", v99, v999, es_buf,
           tail_ex);
    printf("| samples | reps | MC time/run (ms) | VaR99 mean abs err | VaR99 exact hits | "
           "VaR99.9 mean abs err | VaR99.9 max abs err | VaR99.9 exact hits | tail-prob mean rel "
           "err |\n|---|---|---|---|---|---|---|---|---|\n");
    for (int si = 0; si < 3; si++) {
        uint64_t S = Ss[si];
        int reps = si == 2 ? 5 : 20;
        double e99 = 0, e999 = 0, m999 = 0, rel = 0, tmc = 0;
        int h99 = 0, h999 = 0;
        for (rep = 0; rep < reps; rep++) {
            uint64_t smp, ge = 0;
            g_s = seed * 1000003ull + (uint64_t) rep * 7919ull + S;
            memset(g_hist, 0, sizeof g_hist);
            t0 = now_s();
            for (smp = 0; smp < S; smp++) {
                int sc = (mix && u01() >= g_w0) ? 1 : 0;
                uint32_t L = 0;
                for (i = 0; i < (uint32_t) n; i++)
                    if (u01() < g_p[sc][i]) L += g_loss[i];
                g_hist[L]++;
            }
            {
                uint32_t a = mc_var(total, S, 99, 100), b = mc_var(total, S, 999, 1000), k;
                tmc += now_s() - t0;
                for (k = v999; k <= total; k++) ge += g_hist[k];
                e99 += abs((int) a - (int) v99);
                e999 += abs((int) b - (int) v999);
                if (abs((int) b - (int) v999) > m999) m999 = abs((int) b - (int) v999);
                h99 += a == v99;
                h999 += b == v999;
                {
                    double est = (double) ge / (double) S;
                    double d = est - tail_ex;
                    rel += (d < 0 ? -d : d) / tail_ex;
                }
            }
        }
        printf("| %llu | %d | %.1f | %.2f | %d/%d | %.2f | %.0f | %d/%d | %.1f%% |\n",
               (unsigned long long) S, reps, tmc / reps * 1e3, e99 / reps, h99, reps, e999 / reps,
               m999, h999, reps, 100.0 * rel / reps);
    }
}

int main(void)
{
    g_d = malloc(sizeof *g_d);
    if (!g_d) return 2;
    printf("# xrisk exact vs plain Monte Carlo (loss units; probabilities in bp over 10^4)\n");
    run(50, 0, 11);
    run(100, 0, 12);
    run(256, 0, 13);
    run(256, 1, 14);
    free(g_d);
    return 0;
}
