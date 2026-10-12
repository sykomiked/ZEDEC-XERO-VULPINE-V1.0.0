/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* bench_hwm.c — does the emulation matrix beat native code, and how close
 * does anything get to the machine's ceiling? (make bench-hwemu)
 *
 * Every workload runs the SAME arithmetic three or four ways:
 *   native        a plain C loop, as conventional software would call it;
 *   matrix        through hwm_call to a SOFT provider wrapping that loop
 *                 (the cost of the matrix's dispatch and checks);
 *   matrix+memo   the same with the M7 memo attached;
 *   native+memo   the same direct-mapped cache written by hand into the
 *                 native loop, to show which gains come from skipping work
 *                 (any program could do that) and which from the matrix.
 *
 * Utilisation is reported against a MEASURED ceiling: the int32
 * multiply-add rate of dependency-free int32 and int16 loops on one core, compiled the
 * same way. It is not the vendor's peak (wider SIMD, more cores and matrix
 * units are left out), so read it as "how much of what this core showed
 * it can do", not as a fraction of the chip.
 *
 * Hosted benchmark (libc, clock_gettime); not part of the kernel.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "hwm.h"
#include "../robin_debanks/sha256.h"

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double) t.tv_sec + (double) t.tv_nsec * 1e-9;
}

static volatile uint64_t sink;

/* ---- the ceiling ---- */
#define LANES 64
static double ceiling16(void)
{
    uint16_t acc[LANES], a = (uint16_t) (sink | 3u), b = 7;
    for (int i = 0; i < LANES; i++) acc[i] = (uint16_t) i;
    const long iters = 4000000;
    double t0 = now_s();
    for (long k = 0; k < iters; k++)
        for (int i = 0; i < LANES; i++) acc[i] = (uint16_t) (acc[i] * a + b);
    double dt = now_s() - t0;
    uint64_t s = 0;
    for (int i = 0; i < LANES; i++) s += acc[i];
    sink = s;
    return 2.0 * LANES * (double) iters / (dt * 1e9);
}

static double ceiling32(void)
{
    uint32_t acc[LANES], a = (uint32_t) (sink | 3u), b = 7;
    for (int i = 0; i < LANES; i++) acc[i] = (uint32_t) i;
    const long iters = 4000000;
    double t0 = now_s();
    for (long k = 0; k < iters; k++)
        for (int i = 0; i < LANES; i++) acc[i] = acc[i] * a + b;
    double dt = now_s() - t0;
    uint64_t s = 0;
    for (int i = 0; i < LANES; i++) s += acc[i];
    sink = s;
    return 2.0 * LANES * (double) iters / (dt * 1e9);
}

/* ---- workload 1: int8 matrix-vector, 64x64 (the inference inner loop) ---- */
#define DIM 64
static int8_t W[DIM][DIM];

static void matvec(const int8_t *x, int32_t *y)
{
    for (int r = 0; r < DIM; r++) {
        int32_t s = 0;
        for (int c = 0; c < DIM; c++) s += (int32_t) W[r][c] * x[c];
        y[r] = s;
    }
}

static int pv_matvec(void *inst, hwm_t *m, uint32_t op, const uint8_t *in, uint32_t n, uint8_t *out,
                     uint32_t cap, uint32_t *out_len)
{
    (void) inst;
    (void) m;
    (void) op;
    if (n != DIM || cap < DIM * 4) return -1;
    matvec((const int8_t *) in, (int32_t *) (void *) out);
    *out_len = DIM * 4;
    return 0;
}

/* ---- workload 2: SHA-256 of 32-byte blocks, with repeats ---- */
static int pv_sha(void *inst, hwm_t *m, uint32_t op, const uint8_t *in, uint32_t n, uint8_t *out,
                  uint32_t cap, uint32_t *out_len)
{
    (void) inst;
    (void) m;
    (void) op;
    if (cap < 32) return -1;
    sha256(in, n, out);
    *out_len = 32;
    return 0;
}

#define MEMO 1024u
typedef struct {
    int used;
    uint8_t in[32], out[32];
} nmemo_t;
static nmemo_t nmemo[MEMO];
static hwm_memo_entry_t mmemo[MEMO];

static uint64_t fnv(const uint8_t *in, uint32_t n)
{
    uint64_t h = 1469598103934665603ull;
    for (uint32_t i = 0; i < n; i++) h = (h ^ in[i]) * 1099511628211ull;
    return h ^ (h >> 29);
}

static void make_stream(uint8_t (*blk)[32], long n, int dup_pct, uint32_t seed)
{
    static uint8_t hot[256][32];
    uint32_t x = seed;
    for (int h = 0; h < 256; h++)
        for (int i = 0; i < 32; i++) hot[h][i] = (uint8_t) ((x = x * 1664525u + 1013904223u) >> 24);
    for (long k = 0; k < n; k++) {
        x = x * 1664525u + 1013904223u;
        if ((int) ((x >> 8) % 100u) < dup_pct) {
            memcpy(blk[k], hot[(x >> 16) & 255u], 32);
        } else {
            for (int i = 0; i < 32; i++)
                blk[k][i] = (uint8_t) ((x = x * 1664525u + 1013904223u) >> 24);
            memcpy(blk[k], &k, sizeof k); /* unique */
        }
    }
}

int main(void)
{
    static hwm_t m;
    hwm_init(&m);
    uint32_t fmv, fsha, rcpu, pmv, psha;
    hwm_func_add(&m, 1, "matvec-i8", &fmv);
    hwm_func_add(&m, 2, "sha256", &fsha);
    hwm_resource_add(&m, 1, &rcpu);
    hwm_provider_add(&m, fmv, rcpu, HWM_KIND_SOFT, 1, pv_matvec, 0, &pmv);
    hwm_provider_add(&m, fsha, rcpu, HWM_KIND_SOFT, 1, pv_sha, 0, &psha);

    uint32_t s = 12345;
    for (int r = 0; r < DIM; r++)
        for (int c = 0; c < DIM; c++) W[r][c] = (int8_t) ((s = s * 1103515245u + 12345u) >> 24);
    /* One conformance vector each, so the providers may serve (M2). */
    static int8_t vx[DIM];
    static int32_t vy[DIM];
    for (int c = 0; c < DIM; c++) vx[c] = (int8_t) (c - 32);
    matvec(vx, vy);
    hwm_vector_add(&m, fmv, 0, (const uint8_t *) vx, DIM, (const uint8_t *) vy, DIM * 4);
    static const uint8_t abc[3] = {'a', 'b', 'c'};
    static uint8_t abc_d[32];
    sha256(abc, 3, abc_d);
    hwm_vector_add(&m, fsha, 0, abc, 3, abc_d, 32);
    if (hwm_attest(&m, pmv, 0) != LPRES_STATE_TRUE || hwm_attest(&m, psha, 0) != LPRES_STATE_TRUE) {
        printf("provider failed conformance\n");
        return 1;
    }

    double c32 = ceiling32(), c16 = ceiling16();
    double ceil = c16 > c32 ? c16 : c32;
    printf("# Emulation matrix benchmark\n\n");
    printf("Measured ceiling, one core: int32 multiply-add %.2f ops/ns, int16 %.2f ops/ns;\n"
           "the higher one is used.\n\n",
           c32, c16);

    /* Workload 1 */
    const long N1 = 200000;
    static int8_t xs[256][DIM];
    for (int k = 0; k < 256; k++)
        for (int c = 0; c < DIM; c++) xs[k][c] = (int8_t) ((s = s * 1103515245u + 12345u) >> 24);
    int32_t y[DIM];
    uint64_t chk = 0;
    double t0 = now_s();
    for (long k = 0; k < N1; k++) {
        matvec(xs[k & 255], y);
        chk += (uint32_t) y[k & 63];
    }
    double tn = now_s() - t0;
    uint32_t got;
    t0 = now_s();
    for (long k = 0; k < N1; k++) {
        hwm_call(&m, fmv, 0, (const uint8_t *) xs[k & 255], DIM, (uint8_t *) y, sizeof y, &got);
        chk += (uint32_t) y[k & 63];
    }
    double tm = now_s() - t0;
    sink = chk;
    double ops = 2.0 * DIM * DIM * (double) N1;
    printf("## int8 matrix-vector 64x64 (%ld calls, no repeats skipped)\n\n", N1);
    printf("| path | ns/call | ops/ns | of ceiling | vs native |\n|---|---|---|---|---|\n");
    printf("| native | %.1f | %.2f | %.0f%% | 1.00x |\n", tn * 1e9 / N1, ops / (tn * 1e9),
           100.0 * ops / (tn * 1e9) / ceil);
    printf("| matrix | %.1f | %.2f | %.0f%% | %.2fx |\n\n", tm * 1e9 / N1, ops / (tm * 1e9),
           100.0 * ops / (tm * 1e9) / ceil, tn / tm);

    /* Workload 2 */
    const long N2 = 300000;
    uint8_t(*blk)[32] = malloc((size_t) N2 * 32);
    if (!blk) return 1;
    static const int dups[] = {0, 50, 90};
    printf("## SHA-256 of 32-byte blocks (%ld blocks)\n\n", N2);
    printf(
        "| repeats | native ns | matrix ns | matrix+memo ns | native+memo ns | memo hit rate |\n");
    printf("|---|---|---|---|---|---|\n");
    for (int d = 0; d < 3; d++) {
        make_stream(blk, N2, dups[d], 777u + (uint32_t) d);
        uint8_t out[32];
        chk = 0;
        t0 = now_s();
        for (long k = 0; k < N2; k++) {
            sha256(blk[k], 32, out);
            chk += out[0];
        }
        double a = now_s() - t0;
        hwm_memo_attach(&m, fsha, 0, 0);
        t0 = now_s();
        for (long k = 0; k < N2; k++) {
            hwm_call(&m, fsha, 0, blk[k], 32, out, 32, &got);
            chk += out[0];
        }
        double b = now_s() - t0;
        hwm_memo_attach(&m, fsha, mmemo, MEMO);
        t0 = now_s();
        for (long k = 0; k < N2; k++) {
            hwm_call(&m, fsha, 0, blk[k], 32, out, 32, &got);
            chk += out[0];
        }
        double c = now_s() - t0;
        double hit = (double) m.fn[fsha].memo_hits / (double) N2;
        memset(nmemo, 0, sizeof nmemo);
        t0 = now_s();
        for (long k = 0; k < N2; k++) {
            nmemo_t *e = &nmemo[fnv(blk[k], 32) & (MEMO - 1u)];
            if (e->used && !memcmp(e->in, blk[k], 32)) {
                memcpy(out, e->out, 32);
            } else {
                sha256(blk[k], 32, out);
                e->used = 1;
                memcpy(e->in, blk[k], 32);
                memcpy(e->out, out, 32);
            }
            chk += out[0];
        }
        double e = now_s() - t0;
        sink = chk;
        printf("| %d%% | %.1f | %.1f | %.1f | %.1f | %.0f%% |\n", dups[d], a * 1e9 / N2,
               b * 1e9 / N2, c * 1e9 / N2, e * 1e9 / N2, 100.0 * hit);
    }
    free(blk);
    printf("\nReading: the matrix adds dispatch cost to every call; the memo wins only\n"
           "on repeated inputs, and hand-written native code with the same cache wins\n"
           "by the same mechanism.\n");
    return 0;
}
