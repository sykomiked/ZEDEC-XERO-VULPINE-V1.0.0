/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_qeq.c — checks for the exact circuit equivalence checker (qeq) and its
 * OpenQASM 2.0 subset parser/writer.
 *
 * Covers: textbook identities (HH, T^8, CNOT = H CZ H, SWAP = 3 CNOT, the
 * 7-T Toffoli, a 3-qubit QFT against a version with its controlled phases
 * decomposed), exact counterexamples with their exact amplitudes, global
 * phase cases, DIRECT and MITER agreement, NEITHER for bad gates, too many
 * qubits, overflow and budget, parser round trips and every refusal, and
 * randomized property tests: random circuits against their own peephole-
 * optimised rewrite (equivalent) and single-gate mutations (checked against
 * an independent double-precision dense simulation).
 *
 * HONEST LIMITS. The double reference is a cross-check only (tolerance
 * 1e-6). Random circuits are kept small (up to 5 qubits, 80 gates) so the
 * test runs fast under sanitizers.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qeq.h"

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

static const double PI = 3.14159265358979323846;

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

static qeq_work_t W;
static qs_circuit_t A, B, T1, T2;
static qeq_result_t R;
static char TXT[65536];

/* ---------------------------------------------------------------- */
/* helpers                                                          */
/* ---------------------------------------------------------------- */

static void P(qs_circuit_t *c, uint32_t kind, uint32_t t, uint32_t c0, uint32_t c1, int32_t k,
              uint32_t m)
{
    CHECK(qs_circuit_push(c, kind, t, c0, c1, k, m) == QS_OK);
}
static void G1(qs_circuit_t *c, uint32_t kind, uint32_t t)
{
    P(c, kind, t, 0, 0, 0, 0);
}
static void G2(qs_circuit_t *c, uint32_t kind, uint32_t c0, uint32_t t)
{
    P(c, kind, t, c0, 0, 0, 0);
}

static int run(const qs_circuit_t *a, const qs_circuit_t *b, uint32_t n, uint32_t mode,
               uint32_t method, uint32_t N, uint64_t budget)
{
    qeq_opts_t o;
    qeq_opts_default(&o);
    o.mode = mode;
    o.method = method;
    o.N = N;
    o.max_gate_apps = budget;
    int rc = qeq_check(a, b, n, &o, &W, &R);
    CHECK(rc == QEQ_OK);
    return (int) R.verdict;
}

/* Run DIRECT and MITER; require the same verdict (and phase on TRUE). */
static int both(const qs_circuit_t *a, const qs_circuit_t *b, uint32_t n, uint32_t mode)
{
    int v1 = run(a, b, n, mode, QEQ_MITER, 0, 0);
    uint32_t ph1 = R.phase_exp;
    int v0 = run(a, b, n, mode, QEQ_DIRECT, 0, 0);
    CHECK(v0 == v1);
    if (v0 == QEQ_TRUE && v1 == QEQ_TRUE) CHECK(R.phase_exp == ph1);
    return v0;
}

static void elem_to_c(const qs_elem_t *e, uint32_t N, uint32_t k, double *re, double *im)
{
    double r = 0.0, i = 0.0;
    for (uint32_t j = 0; j < N / 2u; j++) {
        double a = 2.0 * PI * (double) j / (double) N;
        r += (double) e->c[j] * cos(a);
        i += (double) e->c[j] * sin(a);
    }
    double s = pow(sqrt(2.0), (double) k);
    *re = r / s;
    *im = i / s;
}

static int elem_is(const qs_elem_t *e, const int64_t *v, uint32_t D)
{
    for (uint32_t j = 0; j < QS_MAX_D; j++)
        if (e->c[j] != (j < D ? v[j] : 0)) return 0;
    return 1;
}

/* ---- double-precision dense reference ---- */

static double FR[QS_MAX_AMPS], FI[QS_MAX_AMPS];

static void f_1q(uint32_t n, uint32_t mask, uint32_t t, const double m[8])
{
    uint32_t dim = 1u << n, tb = 1u << t;
    for (uint32_t i = 0; i < dim; i++) {
        if ((i & tb) || (i & mask) != mask) continue;
        double ar = FR[i], ai = FI[i], br = FR[i | tb], bi = FI[i | tb];
        FR[i] = m[0] * ar - m[1] * ai + m[2] * br - m[3] * bi;
        FI[i] = m[0] * ai + m[1] * ar + m[2] * bi + m[3] * br;
        FR[i | tb] = m[4] * ar - m[5] * ai + m[6] * br - m[7] * bi;
        FI[i | tb] = m[4] * ai + m[5] * ar + m[6] * bi + m[7] * br;
    }
}

static void f_gate(uint32_t n, const qs_gate_t *g)
{
    double h = 1.0 / sqrt(2.0);
    double mx[8] = {0, 0, 1, 0, 1, 0, 0, 0};
    double m[8] = {1, 0, 0, 0, 0, 0, 1, 0};
    double ph = 0.0;
    int diag = 1;
    uint32_t mask = 0;
    switch (g->kind) {
    case QS_G_I:
        return;
    case QS_G_X:
        f_1q(n, 0, g->t, mx);
        return;
    case QS_G_Y: {
        double my[8] = {0, 0, 0, -1, 0, 1, 0, 0};
        f_1q(n, 0, g->t, my);
        return;
    }
    case QS_G_H: {
        double mh[8] = {h, 0, h, 0, h, 0, -h, 0};
        f_1q(n, 0, g->t, mh);
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
    case QS_G_CPHASE:
        ph = 2.0 * PI * (double) g->k / (double) g->m;
        mask = 1u << g->c0;
        break;
    case QS_G_CNOT:
        f_1q(n, 1u << g->c0, g->t, mx);
        return;
    case QS_G_CCX:
        f_1q(n, (1u << g->c0) | (1u << g->c1), g->t, mx);
        return;
    case QS_G_SWAP: {
        uint32_t dim = 1u << n, ab = 1u << g->c0, bb = 1u << g->t;
        for (uint32_t i = 0; i < dim; i++) {
            if (!(i & ab) || (i & bb)) continue;
            uint32_t o = (i & ~ab) | bb;
            double r = FR[i], im = FI[i];
            FR[i] = FR[o];
            FI[i] = FI[o];
            FR[o] = r;
            FI[o] = im;
        }
        return;
    }
    default:
        diag = 0;
    }
    if (diag) {
        m[6] = cos(ph);
        m[7] = sin(ph);
        f_1q(n, mask, g->t, m);
    }
}

static double UR[2][QS_MAX_AMPS][32], UI[2][QS_MAX_AMPS][32]; /* [circ][row][col], n <= 5 */

static void f_unitary(int which, const qs_circuit_t *c, uint32_t n)
{
    uint32_t dim = 1u << n;
    for (uint32_t x = 0; x < dim; x++) {
        for (uint32_t i = 0; i < dim; i++) FR[i] = FI[i] = 0.0;
        FR[x] = 1.0;
        for (uint32_t i = 0; i < c->count; i++) f_gate(n, &c->g[i]);
        for (uint32_t i = 0; i < dim; i++) {
            UR[which][i][x] = FR[i];
            UI[which][i][x] = FI[i];
        }
    }
}

/* Float verdict with tolerance tol: strict or up to a global phase. */
static int f_equiv(uint32_t n, int phase, double tol)
{
    uint32_t dim = 1u << n;
    double lr = 1.0, li = 0.0;
    if (phase) {
        uint32_t y = 0;
        while (y < dim && hypot(UR[1][y][0], UI[1][y][0]) < 1e-9) y++;
        double br = UR[1][y][0], bi = UI[1][y][0], ar = UR[0][y][0], ai = UI[0][y][0];
        double d = br * br + bi * bi;
        lr = (ar * br + ai * bi) / d;
        li = (ai * br - ar * bi) / d;
    }
    for (uint32_t y = 0; y < dim; y++)
        for (uint32_t x = 0; x < dim; x++) {
            double er = lr * UR[1][y][x] - li * UI[1][y][x];
            double ei = lr * UI[1][y][x] + li * UR[1][y][x];
            if (hypot(UR[0][y][x] - er, UI[0][y][x] - ei) > tol) return 0;
        }
    return 1;
}

/* ---- random circuits and a peephole optimiser ---- */

static void rand_gate(qs_circuit_t *c, uint32_t n, int allow16)
{
    static const uint32_t k1[] = {QS_G_H,   QS_G_X, QS_G_Y,   QS_G_Z,     QS_G_S,
                                  QS_G_SDG, QS_G_T, QS_G_TDG, QS_G_PHASE, QS_G_I};
    uint32_t pick = rndn(n >= 2 ? 15u : 10u);
    uint32_t t = rndn(n);
    if (pick < 10u) {
        uint32_t kind = k1[pick];
        if (kind == QS_G_PHASE)
            P(c, kind, t, 0, 0, (int32_t) rndn(16) - 8, allow16 ? 16u : 8u);
        else
            G1(c, kind, t);
        return;
    }
    uint32_t c0 = rndn(n - 1u);
    if (c0 >= t) c0++;
    if (pick == 10u) G2(c, QS_G_CNOT, c0, t);
    if (pick == 11u) G2(c, QS_G_CZ, c0, t);
    if (pick == 12u) G2(c, QS_G_SWAP, c0, t);
    if (pick == 13u) P(c, QS_G_CPHASE, t, c0, 0, (int32_t) rndn(8), allow16 ? 16u : 8u);
    if (pick == 14u) {
        if (n < 3u) {
            G2(c, QS_G_CNOT, c0, t);
            return;
        }
        uint32_t c1 = rndn(n);
        while (c1 == t || c1 == c0) c1 = rndn(n);
        P(c, QS_G_CCX, t, c0, c1, 0, 0);
    }
}

/* Exact inverse of g as a gate record (phase k negated). */
static void inv_gate(const qs_gate_t *g, qs_gate_t *o)
{
    *o = *g;
    if (g->kind == QS_G_S) o->kind = QS_G_SDG;
    if (g->kind == QS_G_SDG) o->kind = QS_G_S;
    if (g->kind == QS_G_T) o->kind = QS_G_TDG;
    if (g->kind == QS_G_TDG) o->kind = QS_G_T;
    if (g->kind == QS_G_PHASE || g->kind == QS_G_CPHASE) o->k = -g->k;
}

static int cancels(const qs_gate_t *a, const qs_gate_t *b)
{
    if (a->kind == QS_G_I) return 0;
    qs_gate_t i;
    inv_gate(a, &i);
    if (i.kind != b->kind || i.t != b->t) return 0;
    uint32_t k = b->kind;
    if (k == QS_G_CNOT || k == QS_G_CZ || k == QS_G_SWAP || k == QS_G_CPHASE || k == QS_G_CCX)
        if (i.c0 != b->c0) return 0;
    if (k == QS_G_CCX && i.c1 != b->c1) return 0;
    if (k == QS_G_PHASE || k == QS_G_CPHASE) {
        if (i.m != b->m) return 0;
        int32_t d = (i.k - b->k) % (int32_t) b->m;
        if (d != 0) return 0;
    }
    return 1;
}

/* Stack peephole: drop I gates, cancel adjacent inverse pairs, T T -> S. */
static void peephole(const qs_circuit_t *in, qs_circuit_t *out)
{
    out->count = 0;
    for (uint32_t i = 0; i < in->count; i++) {
        const qs_gate_t *g = &in->g[i];
        if (g->kind == QS_G_I) continue;
        if (out->count > 0 && cancels(&out->g[out->count - 1u], g)) {
            out->count--;
            continue;
        }
        if (out->count > 0 && g->kind == QS_G_T && out->g[out->count - 1u].kind == QS_G_T &&
            out->g[out->count - 1u].t == g->t) {
            out->g[out->count - 1u].kind = QS_G_S;
            continue;
        }
        out->g[out->count++] = *g;
    }
}

/* Random circuit with deliberately planted inverse pairs. */
static void rand_planted(qs_circuit_t *c, uint32_t n, uint32_t len, int allow16)
{
    qs_circuit_init(c);
    while (c->count < len) {
        rand_gate(c, n, allow16);
        if (rndn(3) == 0 && c->count < len) {
            qs_gate_t inv;
            inv_gate(&c->g[c->count - 1u], &inv);
            c->g[c->count++] = inv;
        }
    }
}

/* ---------------------------------------------------------------- */
/* identities                                                       */
/* ---------------------------------------------------------------- */

static void test_identities(void)
{
    /* HH = I */
    qs_circuit_init(&A);
    qs_circuit_init(&B);
    G1(&A, QS_G_H, 0);
    G1(&A, QS_G_H, 0);
    CHECK(both(&A, &B, 1, QEQ_STRICT) == QEQ_TRUE);
    CHECK(R.why == QEQ_WHY_NONE && R.N == 8u && R.phase_known == 1u && R.phase_exp == 0u);
    CHECK(R.global_phase_only == 0u && R.columns_done == 2u && R.gate_apps == 4u);
    CHECK(run(&A, &B, 1, QEQ_STRICT, QEQ_MITER, 0, 0) == QEQ_TRUE && R.gate_apps == 4u);
    /* on 3 qubits: 8 columns */
    CHECK(run(&A, &B, 3, QEQ_STRICT, QEQ_DIRECT, 0, 0) == QEQ_TRUE && R.columns_done == 8u);
    CHECK(R.gate_apps == 16u);

    /* T^8 = I, T^4 = Z, S S = Z, T T = S */
    qs_circuit_init(&A);
    for (int i = 0; i < 8; i++) G1(&A, QS_G_T, 0);
    qs_circuit_init(&B);
    CHECK(both(&A, &B, 1, QEQ_STRICT) == QEQ_TRUE);
    A.count = 7;
    CHECK(both(&A, &B, 1, QEQ_STRICT) == QEQ_FALSE);
    A.count = 4;
    G1(&B, QS_G_Z, 0);
    CHECK(both(&A, &B, 1, QEQ_STRICT) == QEQ_TRUE);
    qs_circuit_init(&A);
    G1(&A, QS_G_S, 0);
    G1(&A, QS_G_S, 0);
    CHECK(both(&A, &B, 1, QEQ_STRICT) == QEQ_TRUE);
    qs_circuit_init(&A);
    G1(&A, QS_G_T, 0);
    G1(&A, QS_G_T, 0);
    qs_circuit_init(&B);
    G1(&B, QS_G_S, 0);
    CHECK(both(&A, &B, 1, QEQ_STRICT) == QEQ_TRUE);
    /* PHASE(1/8) = T, PHASE(-1/4) = Sdg, PHASE(2/16) = T at N=16 */
    qs_circuit_init(&A);
    P(&A, QS_G_PHASE, 0, 0, 0, 1, 8);
    qs_circuit_init(&B);
    G1(&B, QS_G_T, 0);
    CHECK(both(&A, &B, 1, QEQ_STRICT) == QEQ_TRUE);
    qs_circuit_init(&A);
    P(&A, QS_G_PHASE, 0, 0, 0, 2, 16);
    CHECK(both(&A, &B, 1, QEQ_STRICT) == QEQ_TRUE && R.N == 16u);
    qs_circuit_init(&A);
    P(&A, QS_G_PHASE, 0, 0, 0, -1, 4);
    qs_circuit_init(&B);
    G1(&B, QS_G_SDG, 0);
    CHECK(both(&A, &B, 1, QEQ_STRICT) == QEQ_TRUE);

    /* CNOT = H(t) CZ H(t), on 2 and 3 qubits */
    qs_circuit_init(&A);
    G2(&A, QS_G_CNOT, 0, 1);
    qs_circuit_init(&B);
    G1(&B, QS_G_H, 1);
    G2(&B, QS_G_CZ, 0, 1);
    G1(&B, QS_G_H, 1);
    CHECK(both(&A, &B, 2, QEQ_STRICT) == QEQ_TRUE);
    CHECK(both(&A, &B, 3, QEQ_STRICT) == QEQ_TRUE);
    /* the H on the wrong qubit is caught */
    B.g[0].t = 0;
    B.g[2].t = 0;
    CHECK(both(&A, &B, 2, QEQ_STRICT) == QEQ_FALSE);
    /* CZ is symmetric */
    qs_circuit_init(&A);
    G2(&A, QS_G_CZ, 0, 1);
    qs_circuit_init(&B);
    G2(&B, QS_G_CZ, 1, 0);
    CHECK(both(&A, &B, 2, QEQ_STRICT) == QEQ_TRUE);

    /* SWAP = 3 CNOT; 2 CNOT is not */
    qs_circuit_init(&A);
    G2(&A, QS_G_SWAP, 0, 1);
    qs_circuit_init(&B);
    G2(&B, QS_G_CNOT, 0, 1);
    G2(&B, QS_G_CNOT, 1, 0);
    G2(&B, QS_G_CNOT, 0, 1);
    CHECK(both(&A, &B, 2, QEQ_STRICT) == QEQ_TRUE);
    B.count = 2;
    CHECK(both(&A, &B, 2, QEQ_STRICT) == QEQ_FALSE);
    CHECK(both(&A, &B, 2, QEQ_UP_TO_PHASE) == QEQ_FALSE);

    /* Toffoli: the standard 7-T decomposition equals CCX exactly */
    qs_circuit_init(&A);
    P(&A, QS_G_CCX, 2, 0, 1, 0, 0);
    qs_circuit_init(&B);
    G1(&B, QS_G_H, 2);
    G2(&B, QS_G_CNOT, 1, 2);
    G1(&B, QS_G_TDG, 2);
    G2(&B, QS_G_CNOT, 0, 2);
    G1(&B, QS_G_T, 2);
    G2(&B, QS_G_CNOT, 1, 2);
    G1(&B, QS_G_TDG, 2);
    G2(&B, QS_G_CNOT, 0, 2);
    G1(&B, QS_G_T, 1);
    G1(&B, QS_G_T, 2);
    G1(&B, QS_G_H, 2);
    G2(&B, QS_G_CNOT, 0, 1);
    G1(&B, QS_G_T, 0);
    G1(&B, QS_G_TDG, 1);
    G2(&B, QS_G_CNOT, 0, 1);
    CHECK(both(&A, &B, 3, QEQ_STRICT) == QEQ_TRUE);
    CHECK(R.columns_done == 8u && R.gate_apps == 8u * 16u);
    CHECK(both(&A, &B, 4, QEQ_STRICT) == QEQ_TRUE);
    /* one T flipped to Tdg: caught, also up to phase */
    B.g[13].kind = QS_G_T;
    CHECK(both(&A, &B, 3, QEQ_STRICT) == QEQ_FALSE);
    CHECK(both(&A, &B, 3, QEQ_UP_TO_PHASE) == QEQ_FALSE);
}

/* 3-qubit QFT (qubit 2 = most significant) with and without its controlled
 * phases decomposed into single-qubit phases and CNOTs. */
static void qft3(qs_circuit_t *c, int decompose, int bad_sign)
{
    qs_circuit_init(c);
    /* CP(2 pi k/m) between (ctl, tgt) */
    struct {
        uint32_t ctl, tgt;
        int32_t k;
        uint32_t m;
    } cp[3] = {{1, 2, 1, 4}, {0, 2, 1, 8}, {0, 1, 1, 4}};
    uint32_t ci = 0;
    for (uint32_t q = 3; q-- > 0;) {
        G1(c, QS_G_H, q);
        for (uint32_t r = 0; r < q; r++, ci++) {
            if (!decompose) {
                P(c, QS_G_CPHASE, cp[ci].tgt, cp[ci].ctl, 0, cp[ci].k, cp[ci].m);
                continue;
            }
            /* CP(t) = P(t/2) c; CX c,t; P(-t/2) t; CX c,t; P(t/2) t */
            int32_t k = cp[ci].k;
            uint32_t m2 = cp[ci].m * 2u;
            P(c, QS_G_PHASE, cp[ci].ctl, 0, 0, k, m2);
            G2(c, QS_G_CNOT, cp[ci].ctl, cp[ci].tgt);
            P(c, QS_G_PHASE, cp[ci].tgt, 0, 0, bad_sign && ci == 1u ? k : -k, m2);
            G2(c, QS_G_CNOT, cp[ci].ctl, cp[ci].tgt);
            P(c, QS_G_PHASE, cp[ci].tgt, 0, 0, k, m2);
        }
    }
    G2(c, QS_G_SWAP, 0, 2);
}

static void test_qft(void)
{
    qft3(&A, 0, 0);
    qft3(&B, 1, 0);
    CHECK(both(&A, &B, 3, QEQ_STRICT) == QEQ_TRUE);
    CHECK(R.N == 16u); /* the decomposition needs pi/8 */
    /* forced N = 8 cannot represent pi/8: NEITHER, never a guess */
    CHECK(run(&A, &B, 3, QEQ_STRICT, QEQ_DIRECT, 8, 0) == QEQ_NEITHER);
    CHECK(R.why == QEQ_WHY_GATE && R.bad_circuit == 2u && R.bad_gate == 6u);
    CHECK(R.qs_rc == QS_ERR_PHASE);
    CHECK(run(&B, &A, 3, QEQ_STRICT, QEQ_DIRECT, 8, 0) == QEQ_NEITHER && R.bad_circuit == 1u);
    CHECK(run(&A, &A, 3, QEQ_STRICT, QEQ_DIRECT, 8, 0) == QEQ_TRUE && R.N == 8u);
    CHECK(run(&A, &B, 3, QEQ_STRICT, QEQ_DIRECT, 16, 0) == QEQ_TRUE);
    qft3(&B, 1, 1);
    CHECK(both(&A, &B, 3, QEQ_STRICT) == QEQ_FALSE);
    CHECK(both(&A, &B, 3, QEQ_UP_TO_PHASE) == QEQ_FALSE);

    /* QFT column 1 (input |001>) against the textbook value
     * e^(2 pi i x y / 8)/sqrt8 = z16^(2y) / sqrt2^3 */
    qft3(&A, 0, 0);
    qs_state_t *s = &W.s1;
    CHECK(qs_init_basis(s, 3, 16, 1) == QS_OK);
    CHECK(qs_run_circuit(s, &A, 0) == QS_OK);
    int ok = s->k == 3u;
    for (uint32_t y = 0; y < 8; y++) {
        uint32_t e = (2u * y) % 16u;
        for (uint32_t j = 0; j < 8; j++) {
            int64_t want = 0;
            if (e < 8u && j == e) want = 1;
            if (e >= 8u && j == e - 8u) want = -1;
            if (s->amp[y].c[j] != want) ok = 0;
        }
    }
    CHECK(ok);
}

/* ---------------------------------------------------------------- */
/* counterexamples and phases                                       */
/* ---------------------------------------------------------------- */

static void check_cex_float(uint32_t n, int phase);

static void test_counterexamples(void)
{
    static const int64_t ONE[8] = {1}, MONE[8] = {-1}, Z1[8] = {0, 1}, Z2[8] = {0, 0, 1},
                         MZ2[8] = {0, 0, -1};
    /* T vs S: column 0 equal, column 1 differs: z vs z^2 */
    qs_circuit_init(&A);
    G1(&A, QS_G_T, 0);
    qs_circuit_init(&B);
    G1(&B, QS_G_S, 0);
    CHECK(run(&A, &B, 1, QEQ_STRICT, QEQ_DIRECT, 0, 0) == QEQ_FALSE);
    CHECK(R.cex_in == 1u && R.cex_out == 1u && R.columns_done == 1u);
    CHECK(elem_is(&R.cex_a, Z1, 4) && R.cex_ka == 0u && elem_is(&R.cex_b, Z2, 4) && R.cex_kb == 0u);
    CHECK(R.why == QEQ_WHY_NONE && R.global_phase_only == 0u);
    /* same at N = 16: z16^2 vs z16^4 */
    CHECK(run(&A, &B, 1, QEQ_STRICT, QEQ_DIRECT, 16, 0) == QEQ_FALSE && R.N == 16u);
    {
        static const int64_t a16[8] = {0, 0, 1}, b16[8] = {0, 0, 0, 0, 1};
        CHECK(elem_is(&R.cex_a, a16, 8) && elem_is(&R.cex_b, b16, 8));
    }
    /* miter: (S^dag T)|1> = z^-1 |1> = -z^3 */
    CHECK(run(&A, &B, 1, QEQ_STRICT, QEQ_MITER, 0, 0) == QEQ_FALSE);
    {
        static const int64_t mz3[8] = {0, 0, 0, -1};
        CHECK(R.cex_in == 1u && R.cex_out == 1u && elem_is(&R.cex_a, mz3, 4));
        CHECK(elem_is(&R.cex_b, ONE, 4) && R.cex_ka == 0u);
    }

    /* Z X Z X = -I: not strictly equal, equal up to the phase -1 = z^4 */
    qs_circuit_init(&A);
    G1(&A, QS_G_Z, 0);
    G1(&A, QS_G_X, 0);
    G1(&A, QS_G_Z, 0);
    G1(&A, QS_G_X, 0);
    qs_circuit_init(&B);
    CHECK(both(&A, &B, 1, QEQ_STRICT) == QEQ_FALSE);
    CHECK(R.cex_in == 0u && R.cex_out == 0u && elem_is(&R.cex_a, MONE, 4) &&
          elem_is(&R.cex_b, ONE, 4));
    CHECK(run(&A, &B, 1, QEQ_STRICT, QEQ_MITER, 0, 0) == QEQ_FALSE);
    CHECK(elem_is(&R.cex_a, MONE, 4) && elem_is(&R.cex_b, ONE, 4));
    CHECK(both(&A, &B, 1, QEQ_UP_TO_PHASE) == QEQ_TRUE);
    CHECK(R.phase_exp == 4u && R.global_phase_only == 1u && R.phase_known == 1u);
    CHECK(run(&A, &B, 1, QEQ_UP_TO_PHASE, QEQ_DIRECT, 16, 0) == QEQ_TRUE && R.phase_exp == 8u);
    CHECK(run(&A, &B, 2, QEQ_UP_TO_PHASE, QEQ_MITER, 16, 0) == QEQ_TRUE && R.phase_exp == 8u);
    /* HH up to phase: TRUE with phase 0, not "phase only" */
    qs_circuit_init(&A);
    G1(&A, QS_G_H, 0);
    G1(&A, QS_G_H, 0);
    CHECK(both(&A, &B, 1, QEQ_UP_TO_PHASE) == QEQ_TRUE);
    CHECK(R.phase_exp == 0u && R.global_phase_only == 0u);

    /* "Y then X" = X.Y = iZ: up to phase vs Z with z^2 = i */
    qs_circuit_init(&A);
    G1(&A, QS_G_Y, 0);
    G1(&A, QS_G_X, 0);
    qs_circuit_init(&B);
    G1(&B, QS_G_Z, 0);
    CHECK(both(&A, &B, 1, QEQ_UP_TO_PHASE) == QEQ_TRUE && R.phase_exp == 2u);
    CHECK(both(&B, &A, 1, QEQ_UP_TO_PHASE) == QEQ_TRUE && R.phase_exp == 6u);
    CHECK(both(&A, &B, 1, QEQ_STRICT) == QEQ_FALSE);

    /* X vs Y: strict cex at column 0 row 1 (1 vs i); up to phase the
     * phase i from column 0 fails on column 1 (1 vs -i) */
    qs_circuit_init(&A);
    G1(&A, QS_G_X, 0);
    qs_circuit_init(&B);
    G1(&B, QS_G_Y, 0);
    CHECK(run(&A, &B, 1, QEQ_STRICT, QEQ_DIRECT, 0, 0) == QEQ_FALSE);
    CHECK(R.cex_in == 0u && R.cex_out == 1u && elem_is(&R.cex_a, ONE, 4) &&
          elem_is(&R.cex_b, Z2, 4));
    CHECK(run(&A, &B, 1, QEQ_UP_TO_PHASE, QEQ_DIRECT, 0, 0) == QEQ_FALSE);
    CHECK(R.cex_in == 1u && R.cex_out == 0u && elem_is(&R.cex_a, ONE, 4) &&
          elem_is(&R.cex_b, MZ2, 4) && R.columns_done == 1u);
    /* miter: Y^dag X = diag(-i, i); phase -i from column 0, column 1 has i */
    CHECK(run(&A, &B, 1, QEQ_UP_TO_PHASE, QEQ_MITER, 0, 0) == QEQ_FALSE);
    CHECK(R.cex_in == 1u && R.cex_out == 1u && elem_is(&R.cex_a, Z2, 4) &&
          elem_is(&R.cex_b, MZ2, 4) && R.phase_exp == 6u);

    /* different first-nonzero rows: X vs I up to phase -> FALSE at (0, 0) */
    qs_circuit_init(&B);
    CHECK(run(&A, &B, 1, QEQ_UP_TO_PHASE, QEQ_DIRECT, 0, 0) == QEQ_FALSE);
    CHECK(R.cex_in == 0u && R.cex_out == 0u);
    {
        static const int64_t zero[8] = {0};
        CHECK(elem_is(&R.cex_a, zero, 4) && elem_is(&R.cex_b, ONE, 4));
    }
    /* miter on X vs I: off-diagonal entry reported against expected 0 */
    CHECK(run(&A, &B, 2, QEQ_STRICT, QEQ_MITER, 0, 0) == QEQ_FALSE);
    CHECK(R.cex_in == 0u && R.cex_out == 1u && elem_is(&R.cex_a, ONE, 4));

    /* magnitudes differ (H vs I) up to phase: first column ratio is not a
     * root of unity -> FALSE; the counterexample carries k = 1 */
    qs_circuit_init(&A);
    G1(&A, QS_G_H, 0);
    CHECK(run(&A, &B, 1, QEQ_UP_TO_PHASE, QEQ_DIRECT, 0, 0) == QEQ_FALSE);
    CHECK(R.cex_in == 0u && R.cex_out == 0u && R.cex_ka == 1u && R.cex_kb == 0u);
    CHECK(elem_is(&R.cex_a, ONE, 4));
    CHECK(run(&A, &B, 1, QEQ_UP_TO_PHASE, QEQ_MITER, 0, 0) == QEQ_FALSE);
    CHECK(R.cex_in == 0u && R.cex_out == 1u && R.cex_ka == 1u && R.phase_exp == 0u);

    /* amplitudes with different k compare exactly: H Z H = X, so H S S H
     * (k = 0 after the second H) vs X */
    qs_circuit_init(&A);
    G1(&A, QS_G_H, 0);
    G1(&A, QS_G_S, 0);
    G1(&A, QS_G_S, 0);
    G1(&A, QS_G_H, 0);
    qs_circuit_init(&B);
    G1(&B, QS_G_X, 0);
    CHECK(both(&A, &B, 1, QEQ_STRICT) == QEQ_TRUE);
    /* H T H vs H: amplitudes over sqrt2^2 and sqrt2^1 at column 0 */
    qs_circuit_init(&A);
    G1(&A, QS_G_H, 0);
    G1(&A, QS_G_T, 0);
    G1(&A, QS_G_H, 0);
    qs_circuit_init(&B);
    G1(&B, QS_G_H, 0);
    CHECK(run(&A, &B, 1, QEQ_STRICT, QEQ_DIRECT, 0, 0) == QEQ_FALSE);
    CHECK(R.cex_in == 0u && R.cex_out == 0u && R.cex_ka == 2u && R.cex_kb == 1u);
    {
        static const int64_t onepz[8] = {1, 1};
        CHECK(elem_is(&R.cex_a, onepz, 4) && elem_is(&R.cex_b, ONE, 4));
    }
    CHECK(run(&A, &B, 1, QEQ_UP_TO_PHASE, QEQ_DIRECT, 0, 0) == QEQ_FALSE);
    /* mixed k with an EQUAL entry: find (by a fixed-seed search) a 2-qubit
     * circuit whose column 0 has k = 2 but entry 0 exactly 1/sqrt2, the
     * same value H on qubit 0 has at k = 1. The counterexample must then be
     * a later row, which needs the exact sqrt2 rescaling to be right. */
    {
        g_rng = 0xC0FFEEu;
        int found = 0;
        for (uint32_t it = 0; it < 200000u && !found; it++) {
            qs_circuit_init(&T1);
            uint32_t len = 2u + rndn(10);
            for (uint32_t i = 0; i < len; i++) {
                static const uint32_t ks[] = {QS_G_H, QS_G_T, QS_G_S, QS_G_X, QS_G_TDG};
                uint32_t p = rndn(6);
                if (p < 5u)
                    (void) qs_circuit_push(&T1, ks[p], rndn(2), 0, 0, 0, 0);
                else
                    (void) qs_circuit_push(&T1, QS_G_CNOT, 1u - (i & 1u), i & 1u, 0, 0, 0);
            }
            qs_state_t *s = &W.s1;
            if (qs_init_basis(s, 2, 8, 0) || qs_run_circuit(s, &T1, 0)) continue;
            double re, im;
            elem_to_c(&s->amp[0], 8, s->k, &re, &im);
            if (s->k == 2u && fabs(re - sqrt(0.5)) < 1e-12 && fabs(im) < 1e-12) found = 1;
        }
        CHECK(found);
        qs_circuit_init(&T2);
        G1(&T2, QS_G_H, 0);
        f_unitary(0, &T1, 2);
        f_unitary(1, &T2, 2);
        CHECK(run(&T1, &T2, 2, QEQ_STRICT, QEQ_DIRECT, 0, 0) == QEQ_FALSE);
        CHECK(R.cex_in == 0u && R.cex_out != 0u && R.cex_ka == 2u && R.cex_kb == 1u);
        check_cex_float(2, 0);
        CHECK(run(&T1, &T2, 2, QEQ_UP_TO_PHASE, QEQ_DIRECT, 0, 0) == QEQ_FALSE);
        CHECK(R.cex_in == 0u && R.cex_out != 0u && R.phase_exp == 0u);
        check_cex_float(2, 1);
    }
    /* sqrt2 * (1/sqrt2) == 1 * ...: H H H vs H with k the same is TRUE; the
     * mixed-k path: HSH * HSdgH = I vs empty */
    qs_circuit_init(&A);
    G1(&A, QS_G_H, 0);
    G1(&A, QS_G_S, 0);
    G1(&A, QS_G_H, 0);
    G1(&A, QS_G_H, 0);
    G1(&A, QS_G_SDG, 0);
    G1(&A, QS_G_H, 0);
    qs_circuit_init(&B);
    CHECK(both(&A, &B, 2, QEQ_STRICT) == QEQ_TRUE);
}

/* ---------------------------------------------------------------- */
/* NEITHER: qubits, gates, overflow, budget; argument errors        */
/* ---------------------------------------------------------------- */

static void test_neither(void)
{
    qs_circuit_init(&A);
    qs_circuit_init(&B);
    G1(&A, QS_G_H, 0);
    G1(&A, QS_G_H, 0);
    /* too many / zero qubits */
    CHECK(run(&A, &B, 11, QEQ_STRICT, QEQ_DIRECT, 0, 0) == QEQ_NEITHER);
    CHECK(R.why == QEQ_WHY_QUBITS && R.qs_rc == QS_ERR_TOO_MANY);
    CHECK(run(&A, &B, 0, QEQ_STRICT, QEQ_DIRECT, 0, 0) == QEQ_NEITHER && R.why == QEQ_WHY_QUBITS);
    CHECK(run(&A, &B, 10, QEQ_STRICT, QEQ_MITER, 0, 0) == QEQ_TRUE && R.columns_done == 1024u);

    /* budget: 2 columns x 2 gates = 4 applications */
    CHECK(run(&A, &B, 1, QEQ_STRICT, QEQ_DIRECT, 0, 3) == QEQ_NEITHER);
    CHECK(R.why == QEQ_WHY_BUDGET && R.columns_done == 1u && R.gate_apps == 2u);
    CHECK(R.phase_known == 0u);
    CHECK(run(&A, &B, 1, QEQ_STRICT, QEQ_DIRECT, 0, 4) == QEQ_TRUE && R.gate_apps == 4u);
    CHECK(run(&A, &B, 1, QEQ_STRICT, QEQ_MITER, 0, 1) == QEQ_NEITHER && R.columns_done == 0u);
    CHECK(R.gate_apps == 0u);
    CHECK(run(&A, &B, 1, QEQ_STRICT, QEQ_MITER, 0, 2) == QEQ_NEITHER && R.columns_done == 1u);
    /* a counterexample found inside the budget is still FALSE */
    qs_circuit_init(&A);
    G1(&A, QS_G_X, 0);
    CHECK(run(&A, &B, 1, QEQ_STRICT, QEQ_DIRECT, 0, 1) == QEQ_FALSE);
    qs_circuit_init(&A);
    G1(&A, QS_G_T, 0);
    G1(&B, QS_G_S, 0);
    CHECK(run(&A, &B, 1, QEQ_STRICT, QEQ_DIRECT, 0, 2) == QEQ_NEITHER && R.why == QEQ_WHY_BUDGET);
    CHECK(run(&A, &B, 1, QEQ_STRICT, QEQ_DIRECT, 0, 4) == QEQ_FALSE);
    CHECK(run(&A, &B, 1, QEQ_STRICT, QEQ_DIRECT, 0, 3) == QEQ_NEITHER);

    /* overflow: (H T)^300 exceeds int64 coefficients inside qsim */
    qs_circuit_init(&A);
    for (int i = 0; i < 300; i++) {
        G1(&A, QS_G_H, 0);
        G1(&A, QS_G_T, 0);
    }
    CHECK(run(&A, &A, 1, QEQ_STRICT, QEQ_DIRECT, 0, 0) == QEQ_NEITHER);
    CHECK(R.why == QEQ_WHY_OVERFLOW && (R.qs_rc == QS_ERR_OVERFLOW || R.qs_rc == QS_ERR_BROKEN));
    CHECK(R.columns_done == 0u && R.gate_apps < 600u);
    CHECK(run(&A, &A, 1, QEQ_UP_TO_PHASE, QEQ_MITER, 0, 0) == QEQ_NEITHER);
    CHECK(R.why == QEQ_WHY_OVERFLOW);
    /* (H T)^100 is fine: k stays below the int64 bound */
    A.count = 200;
    CHECK(both(&A, &A, 1, QEQ_STRICT) == QEQ_TRUE);

    /* malformed gates: NEITHER with the circuit and gate index */
    qs_circuit_init(&A);
    qs_circuit_init(&B);
    G1(&A, QS_G_H, 0);
    G1(&A, QS_G_X, 5); /* qubit 5 on 2 qubits */
    CHECK(run(&A, &B, 2, QEQ_STRICT, QEQ_DIRECT, 0, 0) == QEQ_NEITHER);
    CHECK(R.why == QEQ_WHY_GATE && R.bad_circuit == 1u && R.bad_gate == 1u);
    CHECK(run(&B, &A, 2, QEQ_STRICT, QEQ_MITER, 0, 0) == QEQ_NEITHER && R.bad_circuit == 2u);
    CHECK(run(&A, &B, 6, QEQ_STRICT, QEQ_DIRECT, 0, 0) == QEQ_FALSE); /* H X != I */
    qs_circuit_init(&A);
    G2(&A, QS_G_CNOT, 1, 1); /* control == target */
    CHECK(run(&A, &B, 2, QEQ_STRICT, QEQ_DIRECT, 0, 0) == QEQ_NEITHER && R.bad_gate == 0u);
    qs_circuit_init(&A);
    G2(&A, QS_G_CNOT, 2, 1); /* control out of range */
    CHECK(run(&A, &B, 2, QEQ_STRICT, QEQ_DIRECT, 0, 0) == QEQ_NEITHER);
    CHECK(run(&A, &B, 3, QEQ_STRICT, QEQ_DIRECT, 0, 0) == QEQ_FALSE);
    qs_circuit_init(&A);
    P(&A, QS_G_CCX, 2, 0, 0, 0, 0); /* c1 == c0 */
    CHECK(run(&A, &B, 3, QEQ_STRICT, QEQ_DIRECT, 0, 0) == QEQ_NEITHER);
    A.g[0].c1 = 2; /* c1 == t */
    CHECK(run(&A, &B, 3, QEQ_STRICT, QEQ_DIRECT, 0, 0) == QEQ_NEITHER);
    A.g[0].c1 = 3; /* c1 out of range */
    CHECK(run(&A, &B, 3, QEQ_STRICT, QEQ_DIRECT, 0, 0) == QEQ_NEITHER);
    A.g[0].c1 = 1;
    CHECK(run(&A, &B, 3, QEQ_STRICT, QEQ_DIRECT, 0, 0) == QEQ_FALSE);
    qs_circuit_init(&A);
    P(&A, QS_G_PHASE, 0, 0, 0, 1, 8);
    A.g[0].m = 3; /* not a power of two */
    CHECK(run(&A, &B, 1, QEQ_STRICT, QEQ_DIRECT, 0, 0) == QEQ_NEITHER && R.why == QEQ_WHY_GATE);
    A.g[0].m = 32;
    CHECK(run(&A, &B, 1, QEQ_STRICT, QEQ_DIRECT, 0, 0) == QEQ_NEITHER);
    A.g[0].kind = QS_G_COUNT;
    CHECK(run(&A, &B, 1, QEQ_STRICT, QEQ_DIRECT, 0, 0) == QEQ_NEITHER);
    A.count = QS_MAX_GATES + 1u;
    CHECK(run(&A, &B, 1, QEQ_STRICT, QEQ_DIRECT, 0, 0) == QEQ_NEITHER);
    CHECK(R.bad_gate == QS_MAX_GATES + 1u);

    /* qeq_circuit_N */
    uint32_t N = 0, bad = 99;
    qs_circuit_init(&A);
    G1(&A, QS_G_T, 0);
    CHECK(qeq_circuit_N(&A, 1, &N, &bad) == QEQ_OK && N == 8u && bad == 99u);
    P(&A, QS_G_CPHASE, 1, 0, 0, 1, 16);
    CHECK(qeq_circuit_N(&A, 1, &N, &bad) == QEQ_ERR_ARG && bad == 1u);
    CHECK(qeq_circuit_N(&A, 2, &N, &bad) == QEQ_OK && N == 16u);
    CHECK(qeq_circuit_N(&A, 2, 0, 0) == QEQ_OK);
    CHECK(qeq_circuit_N(&A, 0, &N, &bad) == QEQ_ERR_ARG && bad == 2u);
    CHECK(qeq_circuit_N(&A, 11, &N, &bad) == QEQ_ERR_ARG);
    CHECK(qeq_circuit_N(&A, 10, &N, &bad) == QEQ_OK);
    CHECK(qeq_circuit_N(0, 2, &N, &bad) == QEQ_ERR_NULL);

    /* argument errors */
    qeq_opts_t o;
    CHECK(qeq_opts_default(0) == QEQ_ERR_NULL);
    CHECK(qeq_opts_default(&o) == QEQ_OK && o.mode == QEQ_STRICT && o.method == QEQ_DIRECT &&
          o.N == 0u && o.max_gate_apps == 0u);
    qs_circuit_init(&A);
    CHECK(qeq_check(0, &B, 1, &o, &W, &R) == QEQ_ERR_NULL);
    CHECK(qeq_check(&A, 0, 1, &o, &W, &R) == QEQ_ERR_NULL);
    CHECK(qeq_check(&A, &B, 1, &o, 0, &R) == QEQ_ERR_NULL);
    CHECK(qeq_check(&A, &B, 1, &o, &W, 0) == QEQ_ERR_NULL);
    o.mode = 2;
    CHECK(qeq_check(&A, &B, 1, &o, &W, &R) == QEQ_ERR_ARG);
    o.mode = QEQ_UP_TO_PHASE;
    o.method = 2;
    CHECK(qeq_check(&A, &B, 1, &o, &W, &R) == QEQ_ERR_ARG);
    o.method = QEQ_MITER;
    o.N = 12;
    CHECK(qeq_check(&A, &B, 1, &o, &W, &R) == QEQ_ERR_ARG);
    o.N = 16;
    qs_circuit_init(&B);
    G1(&B, QS_G_Z, 0);
    CHECK(qeq_check(&B, &B, 1, &o, &W, &R) == QEQ_OK && R.verdict == QEQ_TRUE && R.N == 16u);
    /* NULL options = defaults (strict, direct) */
    G1(&A, QS_G_Z, 0);
    G1(&A, QS_G_X, 0);
    G1(&A, QS_G_Z, 0);
    G1(&A, QS_G_X, 0);
    qs_circuit_init(&B);
    CHECK(qeq_check(&A, &B, 1, 0, &W, &R) == QEQ_OK && R.verdict == QEQ_FALSE && R.N == 8u);
    /* BOTH is never produced */
    CHECK(QEQ_NEITHER == 0 && QEQ_TRUE == 1 && QEQ_FALSE == 2 && QEQ_BOTH == 3);
}

/* ---------------------------------------------------------------- */
/* randomized properties                                            */
/* ---------------------------------------------------------------- */

static void check_cex_float(uint32_t n, int phase)
{
    (void) n;
    double ar, ai, br, bi;
    elem_to_c(&R.cex_a, R.N, R.cex_ka, &ar, &ai);
    elem_to_c(&R.cex_b, R.N, R.cex_kb, &br, &bi);
    CHECK(fabs(ar - UR[0][R.cex_out][R.cex_in]) < 1e-9 &&
          fabs(ai - UI[0][R.cex_out][R.cex_in]) < 1e-9);
    CHECK(fabs(br - UR[1][R.cex_out][R.cex_in]) < 1e-9 &&
          fabs(bi - UI[1][R.cex_out][R.cex_in]) < 1e-9);
    if (!phase) CHECK(hypot(ar - br, ai - bi) > 1e-6);
}

static void test_random(void)
{
    g_rng = 0x5EEDu;
    uint32_t n_true = 0, n_false = 0, shrink = 0, agree = 0, total = 0;
    for (uint32_t it = 0; it < 120; it++) {
        uint32_t n = 1u + it % 5u;
        uint32_t len = 10u + rndn(70);
        int allow16 = (int) (it % 3u == 0u);
        rand_planted(&A, n, len, allow16);
        peephole(&A, &B);
        if (B.count < A.count) shrink++;
        int v = both(&A, &B, n, QEQ_STRICT);
        CHECK(v == QEQ_TRUE);
        CHECK(R.phase_exp == 0u);
        CHECK(both(&B, &A, n, QEQ_UP_TO_PHASE) == QEQ_TRUE && R.phase_exp == 0u);
        n_true += v == QEQ_TRUE;

        /* mutate one gate of the optimised circuit */
        if (B.count == 0) continue;
        T1 = B;
        uint32_t gi = rndn(B.count);
        qs_circuit_init(&T2);
        rand_gate(&T2, n, allow16);
        T1.g[gi] = T2.g[0];
        f_unitary(0, &B, n);
        f_unitary(1, &T1, n);
        for (int phase = 0; phase < 2; phase++) {
            int fv = f_equiv(n, phase, 1e-6);
            int ev = both(&B, &T1, n, phase ? QEQ_UP_TO_PHASE : QEQ_STRICT);
            CHECK(ev == QEQ_TRUE || ev == QEQ_FALSE);
            CHECK((ev == QEQ_TRUE) == (fv == 1));
            total++;
            agree += (ev == QEQ_TRUE) == (fv == 1);
            if (ev == QEQ_FALSE) {
                n_false++;
                CHECK(run(&B, &T1, n, phase ? QEQ_UP_TO_PHASE : QEQ_STRICT, QEQ_DIRECT, 0, 0) ==
                      QEQ_FALSE);
                check_cex_float(n, phase);
            } else if (phase) {
                /* equivalent up to phase: the reported z^j matches the floats */
                double lr = cos(2.0 * PI * (double) R.phase_exp / (double) R.N);
                double li = sin(2.0 * PI * (double) R.phase_exp / (double) R.N);
                double er = lr * UR[1][0][0] - li * UI[1][0][0];
                double ei = lr * UI[1][0][0] + li * UR[1][0][0];
                CHECK(hypot(er - UR[0][0][0], ei - UI[0][0][0]) < 1e-9);
            }
        }
    }
    CHECK(shrink == 120u);
    CHECK(n_false > 200u && n_false < total);
    CHECK(agree == total);
    printf(
        "random: %u rewrite pairs TRUE (%u shrank), %u mutation verdicts FALSE, exact/float agree "
        "%u/%u\n",
        n_true, shrink, n_false, agree, total);
}

/* ---------------------------------------------------------------- */
/* OpenQASM subset                                                  */
/* ---------------------------------------------------------------- */

static int parse(const char *s, uint32_t *n, qeq_qasm_err_t *e)
{
    return qeq_qasm_parse(s, (uint32_t) strlen(s), &A, n, e);
}

static void expect_err(const char *s, uint32_t code, uint32_t line)
{
    uint32_t n = 77;
    qeq_qasm_err_t e;
    int rc = parse(s, &n, &e);
    CHECK(rc == QEQ_ERR_ARG);
    CHECK(e.code == code);
    CHECK(e.line == line);
    if (rc != QEQ_ERR_ARG || e.code != code || e.line != line)
        printf("  got rc %d code %u line %u for:\n%s\n", rc, e.code, e.line, s);
}

#define HDR "OPENQASM 2.0;\ninclude \"qelib1.inc\";\n"

static void test_qasm(void)
{
    uint32_t n = 0;
    qeq_qasm_err_t e;
    const char *src = HDR "// a comment line\n"
                          "qreg q[3];\ncreg c[3];\n"
                          "h q[0]; x q[1]; y q[2]; z q[0]; s q[1]; sdg q[2]; t q[0]; tdg q[1];\n"
                          "id q[2];\n"
                          "cx q[0],q[1];\ncz q[1], q[2];\nswap q[2],q[0];\nccx q[0],q[1],q[2];\n"
                          "barrier q;\nbarrier q[0],q[1];\n"
                          "u1(pi/4) q[0];\np(-pi/2) q[1];\np(3*pi/8) q[2];\np(0) q[0];\n"
                          "p(pi) q[1];\np(2*pi) q[2];\np(2*pi/16) q[0];\n+0;\n";
    /* the last line is a syntax error on line 21 */
    CHECK(parse(src, &n, &e) == QEQ_ERR_ARG && e.code == QEQ_Q_SYNTAX && e.line == 21u);
    char good[2048];
    size_t L = strlen(src) - strlen("+0;\n");
    memcpy(good, src, L);
    strcpy(good + L, "cp(pi/4) q[0],q[2];\ncu1(-3*pi/4) q[1],q[0];   // trailing comment");
    CHECK(parse(good, &n, &e) == QEQ_OK && e.code == QEQ_Q_OK && n == 3u);
    CHECK(A.count == 22u);
    static const uint8_t kinds[22] = {QS_G_H,      QS_G_X,     QS_G_Y,     QS_G_Z,     QS_G_S,
                                      QS_G_SDG,    QS_G_T,     QS_G_TDG,   QS_G_I,     QS_G_CNOT,
                                      QS_G_CZ,     QS_G_SWAP,  QS_G_CCX,   QS_G_PHASE, QS_G_PHASE,
                                      QS_G_PHASE,  QS_G_PHASE, QS_G_PHASE, QS_G_PHASE, QS_G_PHASE,
                                      QS_G_CPHASE, QS_G_CPHASE};
    int ok = 1;
    for (uint32_t i = 0; i < 22u && i < A.count; i++) ok &= A.g[i].kind == kinds[i];
    CHECK(ok);
    CHECK(A.g[9].c0 == 0 && A.g[9].t == 1);
    CHECK(A.g[10].c0 == 1 && A.g[10].t == 2);
    CHECK(A.g[11].c0 == 2 && A.g[11].t == 0);
    CHECK(A.g[12].c0 == 0 && A.g[12].c1 == 1 && A.g[12].t == 2);
    CHECK(A.g[13].k == 1 && A.g[13].m == 8u);  /* pi/4 */
    CHECK(A.g[14].k == -1 && A.g[14].m == 4u); /* -pi/2 */
    CHECK(A.g[15].k == 3 && A.g[15].m == 16u); /* 3pi/8 */
    CHECK(A.g[16].k == 0 && A.g[16].m == 1u);  /* 0 */
    CHECK(A.g[17].k == 1 && A.g[17].m == 2u);  /* pi */
    CHECK(A.g[18].k == 0 && A.g[18].m == 1u);  /* 2pi */
    CHECK(A.g[19].k == 1 && A.g[19].m == 16u); /* 2pi/16 = pi/8 */
    CHECK(A.g[20].k == 1 && A.g[20].m == 8u && A.g[20].c0 == 0 && A.g[20].t == 2);
    CHECK(A.g[21].k == -3 && A.g[21].m == 8u && A.g[21].c0 == 1 && A.g[21].t == 0);

    /* the parsed circuit, written and parsed again, is the same unitary */
    uint32_t len = 0;
    T1 = A;
    CHECK(qeq_qasm_write(&T1, 3, TXT, sizeof TXT, &len) == QEQ_OK && len == strlen(TXT));
    uint32_t n2 = 0;
    CHECK(qeq_qasm_parse(TXT, len, &T2, &n2, &e) == QEQ_OK && n2 == 3u && T2.count == 22u);
    CHECK(both(&T1, &T2, 3, QEQ_STRICT) == QEQ_TRUE);
    CHECK(strstr(TXT, "u1(3*pi/8) q[2];") != 0);
    CHECK(strstr(TXT, "u1(pi) q[1];") != 0);
    CHECK(strstr(TXT, "u1(0) q[0];") != 0);
    CHECK(strstr(TXT, "u1(3*pi/2) q[1];") != 0);       /* -pi/2 normalised */
    CHECK(strstr(TXT, "cu1(5*pi/4) q[1],q[0];") != 0); /* -3pi/4 normalised */
    CHECK(strstr(TXT, "ccx q[0],q[1],q[2];") != 0);
    CHECK(strstr(TXT, "id q[2];") != 0);
    CHECK(strncmp(TXT, HDR "qreg q[3];\n", strlen(HDR "qreg q[3];\n")) == 0);

    /* random round trips: gate-for-gate identical after phase normalisation */
    g_rng = 0xA5A5u;
    int rt_ok = 1;
    for (uint32_t it = 0; it < 60; it++) {
        uint32_t nq = 1u + it % 10u;
        rand_planted(&T1, nq, 1u + rndn(120), (int) (it & 1u));
        if (qeq_qasm_write(&T1, nq, TXT, sizeof TXT, &len) != QEQ_OK) rt_ok = 0;
        if (qeq_qasm_parse(TXT, len, &T2, &n2, &e) != QEQ_OK || n2 != nq) rt_ok = 0;
        if (T2.count != T1.count) rt_ok = 0;
        for (uint32_t i = 0; rt_ok && i < T1.count; i++) {
            const qs_gate_t *a = &T1.g[i], *b = &T2.g[i];
            if (a->kind != b->kind || a->t != b->t) rt_ok = 0;
            if (a->kind >= QS_G_CNOT && a->c0 != b->c0) rt_ok = 0;
            if (a->kind == QS_G_CCX && a->c1 != b->c1) rt_ok = 0;
            if (a->kind == QS_G_PHASE || a->kind == QS_G_CPHASE) {
                /* k/m == k'/m' modulo 1 */
                int64_t lhs = (int64_t) a->k * b->m - (int64_t) b->k * a->m;
                if (lhs % ((int64_t) a->m * b->m) != 0) rt_ok = 0;
            }
        }
        if (nq <= 4u && it % 4u == 0u) CHECK(both(&T1, &T2, nq, QEQ_STRICT) == QEQ_TRUE);
    }
    CHECK(rt_ok);

    /* writer errors */
    qs_circuit_init(&T1);
    G1(&T1, QS_G_H, 0);
    CHECK(qeq_qasm_write(&T1, 1, TXT, 10, &len) == QEQ_ERR_FULL && len == 9u && TXT[9] == '\0');
    CHECK(qeq_qasm_write(&T1, 1, TXT, 0, &len) == QEQ_ERR_ARG);
    CHECK(qeq_qasm_write(&T1, 0, TXT, sizeof TXT, &len) == QEQ_ERR_ARG);
    CHECK(qeq_qasm_write(0, 1, TXT, sizeof TXT, &len) == QEQ_ERR_NULL);
    CHECK(qeq_qasm_write(&T1, 1, 0, sizeof TXT, &len) == QEQ_ERR_NULL);
    CHECK(qeq_qasm_write(&T1, 1, TXT, sizeof TXT, 0) == QEQ_ERR_NULL);
    CHECK(qeq_qasm_write(&T1, 1, TXT, sizeof TXT, &len) == QEQ_OK);
    CHECK(strcmp(TXT, HDR "qreg q[1];\nh q[0];\n") == 0 && len == strlen(TXT));
    uint32_t exact = len + 1u; /* room for exactly the text and its NUL */
    CHECK(qeq_qasm_write(&T1, 1, TXT, exact, &len) == QEQ_OK);
    CHECK(qeq_qasm_write(&T1, 1, TXT, exact - 1u, &len) == QEQ_ERR_FULL);
    G1(&T1, QS_G_X, 1);
    CHECK(qeq_qasm_write(&T1, 1, TXT, sizeof TXT, &len) == QEQ_ERR_ARG);

    /* parser NULLs */
    CHECK(qeq_qasm_parse(0, 0, &A, &n, &e) == QEQ_ERR_NULL);
    CHECK(qeq_qasm_parse("x", 1, 0, &n, &e) == QEQ_ERR_NULL);
    CHECK(qeq_qasm_parse("x", 1, &A, 0, &e) == QEQ_ERR_NULL);
    CHECK(qeq_qasm_parse("x", 1, &A, &n, 0) == QEQ_ERR_NULL);
    /* len is honoured: text after len is not read */
    {
        const char *s2 = HDR "qreg q[1];\nh q[0];\nGARBAGE";
        CHECK(qeq_qasm_parse(s2, (uint32_t) (strlen(s2) - 7u), &A, &n, &e) == QEQ_OK &&
              A.count == 1u);
    }

    /* every refusal, with its line */
    expect_err("", QEQ_Q_HEADER, 1);
    expect_err("qreg q[1];", QEQ_Q_HEADER, 1);
    expect_err("OPENQASM 3.0;\nqreg q[1];", QEQ_Q_HEADER, 1);
    expect_err("OPENQASM 2;\n", QEQ_Q_HEADER, 1);
    expect_err("OPENQASM 2.0\nqreg q[1];", QEQ_Q_HEADER, 2);
    expect_err(HDR, QEQ_Q_REG, 3); /* no qreg at all */
    expect_err(HDR "\n\nqreg q[2];\np(pi/3) q[0];\n", QEQ_Q_ANGLE, 6);
    expect_err(HDR "qreg q[2];\nrx(pi/4) q[0];\n", QEQ_Q_UNKNOWN_GATE, 4);
    expect_err(HDR "qreg q[2];\nfoo q[0];\n", QEQ_Q_UNKNOWN_GATE, 4);
    expect_err(HDR "qreg q[3];\nh q[0];\ncx q[1],q[5];\n", QEQ_Q_INDEX, 5);
    expect_err(HDR "qreg q[3];\nbarrier q[3];\n", QEQ_Q_INDEX, 4);
    expect_err(HDR "qreg q[3];\nh q[99999999999];\n", QEQ_Q_INDEX, 4);
    expect_err(HDR "qreg q[2];\nh r[0];\n", QEQ_Q_REG, 4);
    expect_err(HDR "qreg q[2];\nbarrier r;\n", QEQ_Q_REG, 4);
    expect_err(HDR "h q[0];\nqreg q[2];\n", QEQ_Q_REG, 3);
    expect_err(HDR "qreg q[2];\nqreg r[2];\n", QEQ_Q_REG, 4);
    expect_err(HDR "qreg qq[2];\nh q[0];\n", QEQ_Q_REG, 4);
    expect_err(HDR "qreg q[2];\nh qq[0];\n", QEQ_Q_REG, 4);
    expect_err(HDR "qreg q[2];\nh q;\n", QEQ_Q_UNSUPPORTED, 4);
    expect_err(HDR "qreg q[2];\ncreg c[2];\nmeasure q[0] -> c[0];\n", QEQ_Q_UNSUPPORTED, 5);
    expect_err(HDR "qreg q[2];\nreset q[0];\n", QEQ_Q_UNSUPPORTED, 4);
    expect_err(HDR "qreg q[2];\nU(0,0,0) q[0];\n", QEQ_Q_UNSUPPORTED, 4);
    expect_err(HDR "qreg q[2];\nCX q[0],q[1];\n", QEQ_Q_UNSUPPORTED, 4);
    expect_err(HDR "qreg q[2];\ngate g a { h a; }\n", QEQ_Q_UNSUPPORTED, 4);
    expect_err(HDR "qreg q[2];\nopaque g a;\n", QEQ_Q_UNSUPPORTED, 4);
    expect_err(HDR "qreg q[2];\nif(c==1) x q[0];\n", QEQ_Q_UNSUPPORTED, 4);
    expect_err("OPENQASM 2.0;\ninclude \"other.inc\";\n", QEQ_Q_UNSUPPORTED, 2);
    expect_err("OPENQASM 2.0;\ninclude \"qelib1.inc\n\";\n", QEQ_Q_SYNTAX, 2);
    expect_err("OPENQASM 2.0;\ninclude qelib1;\n", QEQ_Q_SYNTAX, 2);
    expect_err(HDR "qreg q[2];\ncx q[0],q[0];\n", QEQ_Q_DUP, 4);
    expect_err(HDR "qreg q[3];\nccx q[0],q[1],q[1];\n", QEQ_Q_DUP, 4);
    expect_err(HDR "qreg q[2];\ncx q[0];\n", QEQ_Q_ARITY, 4);
    expect_err(HDR "qreg q[2];\nh q[0],q[1];\n", QEQ_Q_ARITY, 4);
    expect_err(HDR "qreg q[4];\nccx q[0],q[1],q[2],q[3];\n", QEQ_Q_ARITY, 4);
    expect_err(HDR "qreg q[2];\nh(pi) q[0];\n", QEQ_Q_ARITY, 4);
    expect_err(HDR "qreg q[2];\np q[0];\n", QEQ_Q_ARITY, 4);
    expect_err(HDR "qreg q[2];\np(0.785398) q[0];\n", QEQ_Q_ANGLE, 4);
    expect_err(HDR "qreg q[2];\np(pi/16) q[0];\n", QEQ_Q_ANGLE, 4);
    expect_err(HDR "qreg q[2];\np(3*pi/16) q[0];\n", QEQ_Q_ANGLE, 4);
    expect_err(HDR "qreg q[2];\np(pi/0) q[0];\n", QEQ_Q_ANGLE, 4);
    expect_err(HDR "qreg q[2];\np(0*pi) q[0];\n", QEQ_Q_ANGLE, 4);
    expect_err(HDR "qreg q[2];\np(pi*3/8) q[0];\n", QEQ_Q_ANGLE, 4);
    expect_err(HDR "qreg q[2];\np(2pi) q[0];\n", QEQ_Q_ANGLE, 4);
    expect_err(HDR "qreg q[2];\np(theta) q[0];\n", QEQ_Q_ANGLE, 4);
    expect_err(HDR "qreg q[2];\np(-0) q[0];\n", QEQ_Q_ANGLE, 4);
    expect_err(HDR "qreg q[2];\np(99999999*pi) q[0];\n", QEQ_Q_ANGLE, 4);
    expect_err(HDR "qreg q[2];\np(pi/4 q[0];\n", QEQ_Q_ANGLE, 4);
    expect_err(HDR "qreg q[2];\np(pi/4) q[0]\nh q[1];\n", QEQ_Q_SYNTAX, 5);
    expect_err(HDR "qreg q[2];\nh q[0]; @\n", QEQ_Q_SYNTAX, 4);
    expect_err(HDR "qreg q[2];\nh q[x];\n", QEQ_Q_SYNTAX, 4);
    expect_err(HDR "qreg q[2];\nh q[0;\n", QEQ_Q_SYNTAX, 4);
    expect_err(HDR "qreg q[2];\nh 3;\n", QEQ_Q_SYNTAX, 4);
    expect_err(HDR "qreg q[2];\n;\n", QEQ_Q_SYNTAX, 4);
    expect_err(HDR "qreg q[11];\n", QEQ_Q_SIZE, 3);
    expect_err(HDR "qreg q[0];\n", QEQ_Q_SIZE, 3);
    expect_err(HDR "qreg [2];\n", QEQ_Q_SYNTAX, 3);
    expect_err(HDR "qreg q 2;\n", QEQ_Q_SYNTAX, 3);
    expect_err(HDR "qreg q[two];\n", QEQ_Q_SYNTAX, 3);
    expect_err(HDR "qreg q[2]\n", QEQ_Q_SYNTAX, 4);
    expect_err(HDR "qreg q[2];\nbarrier q[0] q[1];\n", QEQ_Q_SYNTAX, 4);
    expect_err(HDR "qreg q[2];\nbarrier ;\n", QEQ_Q_SYNTAX, 4);

    /* error column */
    CHECK(parse(HDR "qreg q[2];\n  h q[0];  p(pi/3) q[1];\n", &n, &e) == QEQ_ERR_ARG);
    CHECK(e.code == QEQ_Q_ANGLE && e.line == 4u && e.col == 18u);

    /* qreg q[10] works; more than QS_MAX_GATES gates is refused */
    CHECK(parse(HDR "qreg q[10];\nh q[9];\n", &n, &e) == QEQ_OK && n == 10u && A.g[0].t == 9u);
    {
        static char big[20000];
        size_t p = 0;
        p += (size_t) sprintf(big + p, HDR "qreg q[1];\n");
        for (uint32_t i = 0; i < QS_MAX_GATES; i++) p += (size_t) sprintf(big + p, "x q[0];\n");
        CHECK(parse(big, &n, &e) == QEQ_OK && A.count == QS_MAX_GATES);
        sprintf(big + p, "x q[0];\n");
        CHECK(parse(big, &n, &e) == QEQ_ERR_ARG && e.code == QEQ_Q_FULL);
        CHECK(e.line == 4u + QS_MAX_GATES);
    }

    /* parse two QASM texts and check them: the classic CNOT/CZ identity */
    const char *qa = HDR "qreg q[2];\ncx q[0],q[1];\n";
    const char *qb = HDR "qreg q[2];\nh q[1];\ncz q[0],q[1];\nh q[1];\n";
    CHECK(qeq_qasm_parse(qa, (uint32_t) strlen(qa), &T1, &n, &e) == QEQ_OK);
    CHECK(qeq_qasm_parse(qb, (uint32_t) strlen(qb), &T2, &n2, &e) == QEQ_OK && n == n2);
    CHECK(both(&T1, &T2, n, QEQ_STRICT) == QEQ_TRUE);
}

int main(void)
{
    test_identities();
    test_qft();
    test_counterexamples();
    test_neither();
    test_random();
    test_qasm();
    printf("%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
