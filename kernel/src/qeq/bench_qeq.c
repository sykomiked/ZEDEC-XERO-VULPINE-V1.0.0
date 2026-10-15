/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* bench_qeq.c — times the exact equivalence checker (qeq, DIRECT and MITER)
 * against a double-precision column-by-column unitary check, on random
 * Clifford+T circuits of n = 2..10 qubits and 10..500 gates.
 *
 * For each (n, size) it builds a random circuit C, an equivalent rewrite R
 * (CNOT -> H CZ H, SWAP -> 3 CNOT, S -> T T, Z -> S S, plus inserted inverse
 * pairs) and a mutant M (R with one gate replaced), then checks C vs R and
 * C vs M with each method. It prints a Markdown table: times, verdicts,
 * the largest float deviation seen on equivalent pairs, and the number of
 * cases where the float verdict (tolerance 1e-6) differs from the exact one.
 *
 * HONEST LIMITS. Single runs on one machine, wall-clock time; random
 * circuits only, not real compiler output. The float check is a simple
 * reference written here, not an optimised simulator. A classical tool:
 * exponential cost, no quantum speedup.
 *
 * Build (from kernel/):
 *   gcc -std=c11 -O2 -DTEST_HOST -Iinclude src/qeq/bench_qeq.c src/qeq/qeq.c \
 *       src/qsim/qsim.c -o bench_qeq -lm
 */
#define _POSIX_C_SOURCE 199309L
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "qeq.h"

static const double PI = 3.14159265358979323846;
static const double TOL = 1e-6;

static uint64_t g_rng;
static uint64_t rnd(void)
{
    uint64_t z = (g_rng += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
static uint32_t rndn(uint32_t n)
{
    return (uint32_t) (rnd() % n);
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec * 1e3 + (double) ts.tv_nsec / 1e6;
}

static qeq_work_t W;
static qs_circuit_t C, Rw, M, TMP;

/* ---- float reference ---- */

static double AR[QS_MAX_AMPS], AI[QS_MAX_AMPS], BR[QS_MAX_AMPS], BI[QS_MAX_AMPS];

static void f_1q(double *re, double *im, uint32_t n, uint32_t mask, uint32_t t, const double m[8])
{
    uint32_t dim = 1u << n, tb = 1u << t;
    for (uint32_t i = 0; i < dim; i++) {
        if ((i & tb) || (i & mask) != mask) continue;
        double ar = re[i], ai = im[i], br = re[i | tb], bi = im[i | tb];
        re[i] = m[0] * ar - m[1] * ai + m[2] * br - m[3] * bi;
        im[i] = m[0] * ai + m[1] * ar + m[2] * bi + m[3] * br;
        re[i | tb] = m[4] * ar - m[5] * ai + m[6] * br - m[7] * bi;
        im[i | tb] = m[4] * ai + m[5] * ar + m[6] * bi + m[7] * br;
    }
}

static void f_gate(double *re, double *im, uint32_t n, const qs_gate_t *g)
{
    double h = 1.0 / sqrt(2.0);
    double mx[8] = {0, 0, 1, 0, 1, 0, 0, 0};
    double m[8] = {1, 0, 0, 0, 0, 0, 1, 0};
    double ph = 0.0;
    uint32_t mask = 0;
    switch (g->kind) {
    case QS_G_I:
        return;
    case QS_G_X:
        f_1q(re, im, n, 0, g->t, mx);
        return;
    case QS_G_Y: {
        double my[8] = {0, 0, 0, -1, 0, 1, 0, 0};
        f_1q(re, im, n, 0, g->t, my);
        return;
    }
    case QS_G_H: {
        double mh[8] = {h, 0, h, 0, h, 0, -h, 0};
        f_1q(re, im, n, 0, g->t, mh);
        return;
    }
    case QS_G_CNOT:
        f_1q(re, im, n, 1u << g->c0, g->t, mx);
        return;
    case QS_G_CCX:
        f_1q(re, im, n, (1u << g->c0) | (1u << g->c1), g->t, mx);
        return;
    case QS_G_SWAP: {
        uint32_t dim = 1u << n, ab = 1u << g->c0, bb = 1u << g->t;
        for (uint32_t i = 0; i < dim; i++) {
            if (!(i & ab) || (i & bb)) continue;
            uint32_t o = (i & ~ab) | bb;
            double r = re[i], q = im[i];
            re[i] = re[o];
            im[i] = im[o];
            re[o] = r;
            im[o] = q;
        }
        return;
    }
    case QS_G_Z:
        ph = PI;
        break;
    case QS_G_S:
        ph = PI / 2.0;
        break;
    case QS_G_SDG:
        ph = -PI / 2.0;
        break;
    case QS_G_T:
        ph = PI / 4.0;
        break;
    case QS_G_TDG:
        ph = -PI / 4.0;
        break;
    case QS_G_PHASE:
        ph = 2.0 * PI * (double) g->k / (double) g->m;
        break;
    case QS_G_CZ:
        ph = PI;
        mask = 1u << g->c0;
        break;
    default: /* CPHASE */
        ph = 2.0 * PI * (double) g->k / (double) g->m;
        mask = 1u << g->c0;
    }
    m[6] = cos(ph);
    m[7] = sin(ph);
    f_1q(re, im, n, mask, g->t, m);
}

/* Strict float check, column by column; *maxdev = largest |U1 - U2| seen. */
static int f_check(const qs_circuit_t *a, const qs_circuit_t *b, uint32_t n, double *maxdev)
{
    uint32_t dim = 1u << n;
    *maxdev = 0.0;
    for (uint32_t x = 0; x < dim; x++) {
        for (uint32_t i = 0; i < dim; i++) AR[i] = AI[i] = BR[i] = BI[i] = 0.0;
        AR[x] = BR[x] = 1.0;
        for (uint32_t i = 0; i < a->count; i++) f_gate(AR, AI, n, &a->g[i]);
        for (uint32_t i = 0; i < b->count; i++) f_gate(BR, BI, n, &b->g[i]);
        for (uint32_t i = 0; i < dim; i++) {
            double d = hypot(AR[i] - BR[i], AI[i] - BI[i]);
            if (d > *maxdev) *maxdev = d;
        }
        if (*maxdev > TOL) return QEQ_FALSE;
    }
    return QEQ_TRUE;
}

/* ---- circuits ---- */

static void push(qs_circuit_t *c, uint32_t kind, uint32_t t, uint32_t c0, uint32_t c1)
{
    if (qs_circuit_push(c, kind, t, c0, c1, 0, 0) != QS_OK) {
        printf("push failed\n");
    }
}

static void rand_gate(qs_circuit_t *c, uint32_t n)
{
    static const uint32_t k1[] = {QS_G_H, QS_G_X,   QS_G_Y, QS_G_Z,
                                  QS_G_S, QS_G_SDG, QS_G_T, QS_G_TDG};
    uint32_t pick = rndn(12), t = rndn(n);
    if (pick < 8u) {
        push(c, k1[pick], t, 0, 0);
        return;
    }
    uint32_t c0 = rndn(n - 1u);
    if (c0 >= t) c0++;
    if (pick == 8u || pick == 9u) push(c, QS_G_CNOT, t, c0, 0);
    if (pick == 10u) push(c, QS_G_CZ, t, c0, 0);
    if (pick == 11u) {
        if (n >= 3u && rndn(2) == 0) {
            uint32_t c1 = rndn(n);
            while (c1 == t || c1 == c0) c1 = rndn(n);
            push(c, QS_G_CCX, t, c0, c1);
        } else {
            push(c, QS_G_SWAP, t, c0, 0);
        }
    }
}

static void inverse_of(const qs_gate_t *g, qs_gate_t *o)
{
    *o = *g;
    if (g->kind == QS_G_S) o->kind = QS_G_SDG;
    if (g->kind == QS_G_SDG) o->kind = QS_G_S;
    if (g->kind == QS_G_T) o->kind = QS_G_TDG;
    if (g->kind == QS_G_TDG) o->kind = QS_G_T;
}

/* Equivalent rewrite of c into r, at most QS_MAX_GATES gates. */
static void rewrite(const qs_circuit_t *c, uint32_t n, qs_circuit_t *r)
{
    qs_circuit_init(r);
    for (uint32_t i = 0; i < c->count; i++) {
        const qs_gate_t *g = &c->g[i];
        uint32_t room = QS_MAX_GATES - r->count;
        uint32_t left = c->count - i;
        int expand = room > left + 8u && rndn(2) == 0;
        if (expand && g->kind == QS_G_CNOT) {
            push(r, QS_G_H, g->t, 0, 0);
            push(r, QS_G_CZ, g->t, g->c0, 0);
            push(r, QS_G_H, g->t, 0, 0);
        } else if (expand && g->kind == QS_G_SWAP) {
            push(r, QS_G_CNOT, g->t, g->c0, 0);
            push(r, QS_G_CNOT, g->c0, g->t, 0);
            push(r, QS_G_CNOT, g->t, g->c0, 0);
        } else if (expand && g->kind == QS_G_S) {
            push(r, QS_G_T, g->t, 0, 0);
            push(r, QS_G_T, g->t, 0, 0);
        } else if (expand && g->kind == QS_G_Z) {
            push(r, QS_G_S, g->t, 0, 0);
            push(r, QS_G_S, g->t, 0, 0);
        } else {
            r->g[r->count++] = *g;
        }
        room = QS_MAX_GATES - r->count;
        if (room > left + 8u && rndn(4) == 0) { /* plant G G^-1 */
            qs_circuit_init(&TMP);
            rand_gate(&TMP, n);
            qs_gate_t inv;
            inverse_of(&TMP.g[0], &inv);
            r->g[r->count++] = TMP.g[0];
            r->g[r->count++] = inv;
        }
    }
}

static const char *vname(int v)
{
    return v == QEQ_TRUE ? "T" : v == QEQ_FALSE ? "F" : v == QEQ_NEITHER ? "N" : "B";
}

static int exact(const qs_circuit_t *a, const qs_circuit_t *b, uint32_t n, uint32_t method,
                 double *ms, qeq_result_t *r)
{
    qeq_opts_t o;
    qeq_opts_default(&o);
    o.method = method;
    double t0 = now_ms();
    qeq_check(a, b, n, &o, &W, r);
    *ms = now_ms() - t0;
    return (int) r->verdict;
}

int main(void)
{
    static const uint32_t ns[] = {2, 3, 4, 5, 6, 7, 8, 9, 10};
    static const uint32_t sizes[] = {10, 50, 100, 500};
    uint32_t disagree = 0, cases = 0, neither = 0;
    double worst_eq_dev = 0.0;
    qeq_result_t r;
    printf("| n | gates (C / rewrite) | pair | exact direct ms | exact miter ms | float ms | exact "
           "(direct/miter) | float 1e-6 | max float dev | why/k note |\n");
    printf("|---|---|---|---|---|---|---|---|---|---|\n");
    for (uint32_t ni = 0; ni < sizeof ns / sizeof ns[0]; ni++) {
        for (uint32_t si = 0; si < sizeof sizes / sizeof sizes[0]; si++) {
            uint32_t n = ns[ni], sz = sizes[si];
            g_rng = 1000u * n + sz;
            qs_circuit_init(&C);
            while (C.count < sz) rand_gate(&C, n);
            rewrite(&C, n, &Rw);
            M = Rw;
            qs_circuit_init(&TMP);
            rand_gate(&TMP, n);
            M.g[rndn(M.count)] = TMP.g[0];
            for (int pair = 0; pair < 2; pair++) {
                const qs_circuit_t *b = pair == 0 ? &Rw : &M;
                double td, tm, tf, dev;
                int vd = exact(&C, b, n, QEQ_DIRECT, &td, &r);
                uint32_t why = r.why;
                int vm = exact(&C, b, n, QEQ_MITER, &tm, &r);
                double t0 = now_ms();
                int vf = f_check(&C, b, n, &dev);
                tf = now_ms() - t0;
                cases++;
                if (vd == QEQ_NEITHER || vm == QEQ_NEITHER) neither++;
                if (vd != QEQ_NEITHER && vd != vf) disagree++;
                if (vm != QEQ_NEITHER && vm != vf) disagree++;
                if (vd == QEQ_TRUE && dev > worst_eq_dev) worst_eq_dev = dev;
                printf("| %u | %u / %u | %s | %.2f | %.2f | %.2f | %s/%s | %s | %.1e | %s |\n", n,
                       C.count, b->count, pair == 0 ? "rewrite" : "mutant", td, tm, tf, vname(vd),
                       vname(vm), vname(vf), dev,
                       why == QEQ_WHY_OVERFLOW ? "overflow"
                       : why                   ? "other"
                                               : "-");
                fflush(stdout);
            }
        }
    }
    printf("\ncases: %u, exact NEITHER: %u, float-vs-exact verdict differences (tol %.0e): %u, "
           "largest float deviation on an exactly-equivalent pair: %.2e\n",
           cases, neither, TOL, disagree, worst_eq_dev);
    return 0;
}
