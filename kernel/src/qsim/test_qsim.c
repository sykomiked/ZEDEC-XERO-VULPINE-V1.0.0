/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_qsim.c — checks for the exact classical circuit simulator (roadmap
 * 6.6d, step Q.2): Bell, GHZ, QFT on 2-4 qubits on every basis state and
 * its adjoint, gate identities, the 7-T Toffoli decomposition against CCX,
 * random Clifford+T circuits against a double-precision reference, exact
 * probabilities, measurement, sampling boundaries and every error path.
 *
 * HONEST LIMITS. The double-precision reference is only a cross-check
 * (tolerance 1e-9); the exact results are the ones the tests trust. The
 * random circuits are small (up to 6 qubits, up to 160 gates), because the
 * simulator is exponential in qubits and its coefficients grow with T-count.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qsim.h"

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

static qs_state_t S1, S2, S3;
static qs_circuit_t C1, C2;
static qs_elem_t AMPS[QS_MAX_AMPS];

/* ---------------------------------------------------------------- */
/* helpers                                                          */
/* ---------------------------------------------------------------- */

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

static double real_val(const qs_elem_t *e, uint32_t N)
{
    double re, im;
    elem_to_c(e, N, 0, &re, &im);
    return re;
}

/* element z^e */
static void unit(qs_elem_t *x, uint32_t N, uint32_t e)
{
    uint32_t D = N / 2u;
    memset(x, 0, sizeof *x);
    e %= N;
    if (e < D)
        x->c[e] = 1;
    else
        x->c[e - D] = -1;
}

static int elem_eq(const qs_elem_t *a, const qs_elem_t *b)
{
    return memcmp(a, b, sizeof *a) == 0;
}

static int is_int(const qs_elem_t *e, int64_t v)
{
    if (e->c[0] != v) return 0;
    for (uint32_t j = 1; j < QS_MAX_D; j++)
        if (e->c[j] != 0) return 0;
    return 1;
}

static int prob_is(const qs_state_t *st, const qs_prob_t *p, int64_t a, int64_t b)
{
    int c = 99;
    return qs_prob_cmp(st->N, p, a, b, &c) == QS_OK && c == 0;
}

static int equal_states(const qs_state_t *a, const qs_state_t *b)
{
    int eq = -1;
    return qs_state_equal(a, b, &eq) == QS_OK && eq == 1;
}

static int G(qs_state_t *st, uint32_t kind, uint32_t t, uint32_t c0, uint32_t c1)
{
    return qs_gate(st, kind, t, c0, c1, 0, 0);
}

/* ---------------------------------------------------------------- */
/* double reference simulator                                       */
/* ---------------------------------------------------------------- */

static double RRe[QS_MAX_AMPS], RIm[QS_MAX_AMPS];

static void ref_init(uint32_t n)
{
    for (uint32_t i = 0; i < (1u << n); i++) RRe[i] = RIm[i] = 0.0;
    RRe[0] = 1.0;
}

static void ref_phase(uint32_t n, uint32_t mask, uint32_t t, double ang)
{
    double c = cos(ang), s = sin(ang);
    for (uint32_t i = 0; i < (1u << n); i++) {
        if (!(i & (1u << t)) || (i & mask) != mask) continue;
        double r = RRe[i] * c - RIm[i] * s, m = RRe[i] * s + RIm[i] * c;
        RRe[i] = r;
        RIm[i] = m;
    }
}

static void ref_x(uint32_t n, uint32_t mask, uint32_t t)
{
    for (uint32_t i = 0; i < (1u << n); i++) {
        if ((i & (1u << t)) || (i & mask) != mask) continue;
        uint32_t o = i | (1u << t);
        double r = RRe[i], m = RIm[i];
        RRe[i] = RRe[o];
        RIm[i] = RIm[o];
        RRe[o] = r;
        RIm[o] = m;
    }
}

static void ref_gate(uint32_t n, const qs_gate_t *g)
{
    uint32_t t = g->t, tb = 1u << t;
    double h = 1.0 / sqrt(2.0);
    switch (g->kind) {
    case QS_G_I:
        break;
    case QS_G_X:
        ref_x(n, 0, t);
        break;
    case QS_G_Y:
        for (uint32_t i = 0; i < (1u << n); i++) {
            if (i & tb) continue;
            uint32_t o = i | tb;
            double r0 = RRe[i], m0 = RIm[i], r1 = RRe[o], m1 = RIm[o];
            RRe[i] = m1; /* -i * (r1 + i m1) = m1 - i r1 */
            RIm[i] = -r1;
            RRe[o] = -m0; /* i * (r0 + i m0) = -m0 + i r0 */
            RIm[o] = r0;
        }
        break;
    case QS_G_Z:
        ref_phase(n, 0, t, PI);
        break;
    case QS_G_H:
        for (uint32_t i = 0; i < (1u << n); i++) {
            if (i & tb) continue;
            uint32_t o = i | tb;
            double r0 = RRe[i], m0 = RIm[i], r1 = RRe[o], m1 = RIm[o];
            RRe[i] = h * (r0 + r1);
            RIm[i] = h * (m0 + m1);
            RRe[o] = h * (r0 - r1);
            RIm[o] = h * (m0 - m1);
        }
        break;
    case QS_G_S:
        ref_phase(n, 0, t, PI / 2.0);
        break;
    case QS_G_SDG:
        ref_phase(n, 0, t, -PI / 2.0);
        break;
    case QS_G_T:
        ref_phase(n, 0, t, PI / 4.0);
        break;
    case QS_G_TDG:
        ref_phase(n, 0, t, -PI / 4.0);
        break;
    case QS_G_PHASE:
        ref_phase(n, 0, t, 2.0 * PI * (double) g->k / (double) g->m);
        break;
    case QS_G_CNOT:
        ref_x(n, 1u << g->c0, t);
        break;
    case QS_G_CZ:
        ref_phase(n, 1u << g->c0, t, PI);
        break;
    case QS_G_SWAP:
        ref_x(n, 1u << g->c0, t);
        ref_x(n, 1u << t, g->c0);
        ref_x(n, 1u << g->c0, t);
        break;
    case QS_G_CCX:
        ref_x(n, (1u << g->c0) | (1u << g->c1), t);
        break;
    case QS_G_CPHASE:
        ref_phase(n, 1u << g->c0, t, 2.0 * PI * (double) g->k / (double) g->m);
        break;
    }
}

/* ---------------------------------------------------------------- */
/* circuits used by the tests                                       */
/* ---------------------------------------------------------------- */

/* QFT|x> = 2^(-n/2) sum_y e^(2 pi i x y / 2^n) |y>, qubit 0 = LSB. */
static void build_qft(qs_circuit_t *c, uint32_t n)
{
    qs_circuit_init(c);
    for (uint32_t i = n; i-- > 0;) {
        qs_circuit_push(c, QS_G_H, i, 0, 0, 0, 0);
        for (uint32_t j = i; j-- > 0;)
            qs_circuit_push(c, QS_G_CPHASE, i, j, 0, 1, 1u << (i - j + 1u));
    }
    for (uint32_t i = 0; i < n / 2u; i++) qs_circuit_push(c, QS_G_SWAP, n - 1u - i, i, 0, 0, 0);
}

/* Standard 7-T Clifford+T Toffoli (controls a, b; target c). */
static void push_toffoli7(qs_circuit_t *c, uint32_t a, uint32_t b, uint32_t t)
{
    qs_circuit_push(c, QS_G_H, t, 0, 0, 0, 0);
    qs_circuit_push(c, QS_G_CNOT, t, b, 0, 0, 0);
    qs_circuit_push(c, QS_G_TDG, t, 0, 0, 0, 0);
    qs_circuit_push(c, QS_G_CNOT, t, a, 0, 0, 0);
    qs_circuit_push(c, QS_G_T, t, 0, 0, 0, 0);
    qs_circuit_push(c, QS_G_CNOT, t, b, 0, 0, 0);
    qs_circuit_push(c, QS_G_TDG, t, 0, 0, 0, 0);
    qs_circuit_push(c, QS_G_CNOT, t, a, 0, 0, 0);
    qs_circuit_push(c, QS_G_T, b, 0, 0, 0, 0);
    qs_circuit_push(c, QS_G_T, t, 0, 0, 0, 0);
    qs_circuit_push(c, QS_G_H, t, 0, 0, 0, 0);
    qs_circuit_push(c, QS_G_CNOT, b, a, 0, 0, 0);
    qs_circuit_push(c, QS_G_T, a, 0, 0, 0, 0);
    qs_circuit_push(c, QS_G_TDG, b, 0, 0, 0, 0);
    qs_circuit_push(c, QS_G_CNOT, b, a, 0, 0, 0);
}

static int g_theavy; /* 1: only H, T, Tdg, CNOT (drives the denominator up) */

static void random_circuit(qs_circuit_t *c, uint32_t n, uint32_t N, uint32_t len)
{
    static const uint32_t heavy[] = {QS_G_H, QS_G_T, QS_G_TDG, QS_G_H, QS_G_T, QS_G_CNOT};
    qs_circuit_init(c);
    for (uint32_t i = 0; i < len; i++) {
        uint32_t kind = g_theavy ? heavy[rndn(6)] : rndn(QS_G_COUNT);
        uint32_t t = rndn(n), c0 = rndn(n), c1 = rndn(n);
        if (kind == QS_G_CNOT || kind == QS_G_CZ || kind == QS_G_SWAP || kind == QS_G_CPHASE ||
            kind == QS_G_CCX) {
            if (n < 2u) {
                kind = QS_G_H;
            } else {
                while (c0 == t) c0 = rndn(n);
                if (kind == QS_G_CCX) {
                    if (n < 3u)
                        kind = QS_G_CNOT;
                    else
                        while (c1 == t || c1 == c0) c1 = rndn(n);
                }
            }
        }
        uint32_t m = N == 16u ? (1u << rndn(5)) : (1u << rndn(4));
        int32_t k = (int32_t) rndn(41) - 20;
        qs_circuit_push(c, kind, t, c0, c1, k, m);
    }
}

/* ---------------------------------------------------------------- */
/* tests                                                            */
/* ---------------------------------------------------------------- */

static void test_init_and_errors(void)
{
    qs_elem_t e;
    uint32_t k;
    CHECK(qs_init(NULL, 2, 8) == QS_ERR_NULL);
    CHECK(qs_init(&S1, 0, 8) == QS_ERR_TOO_MANY);
    CHECK(qs_init(&S1, 11, 8) == QS_ERR_TOO_MANY);
    CHECK(qs_init(&S1, 10, 8) == QS_OK);
    CHECK(qs_init(&S1, 1, 4) == QS_ERR_ARG);
    CHECK(qs_init(&S1, 1, 32) == QS_ERR_ARG);
    CHECK(qs_init(&S1, 1, 12) == QS_ERR_ARG);
    CHECK(qs_init_basis(&S1, 3, 8, 8) == QS_ERR_INDEX);
    CHECK(qs_init_basis(&S1, 3, 8, 7) == QS_OK);
    CHECK(qs_amplitude(&S1, 7, &e, &k) == QS_OK && is_int(&e, 1) && k == 0);
    CHECK(qs_amplitude(&S1, 6, &e, &k) == QS_OK && is_int(&e, 0));
    CHECK(qs_amplitude(&S1, 8, &e, &k) == QS_ERR_INDEX);
    CHECK(qs_amplitude(&S1, 0, NULL, &k) == QS_ERR_NULL);
    CHECK(qs_amplitude(&S1, 0, &e, NULL) == QS_ERR_NULL);
    CHECK(qs_amplitude(NULL, 0, &e, &k) == QS_ERR_NULL);
    CHECK(qs_check_unit(&S1) == QS_OK);

    /* gate argument errors */
    CHECK(qs_init(&S1, 3, 8) == QS_OK);
    CHECK(G(&S1, QS_G_H, 3, 0, 0) == QS_ERR_QUBIT);
    CHECK(G(&S1, QS_G_X, 2, 0, 0) == QS_OK);
    CHECK(G(&S1, QS_G_CNOT, 1, 1, 0) == QS_ERR_QUBIT);
    CHECK(G(&S1, QS_G_CNOT, 1, 3, 0) == QS_ERR_QUBIT);
    CHECK(G(&S1, QS_G_CZ, 0, 0, 0) == QS_ERR_QUBIT);
    CHECK(G(&S1, QS_G_SWAP, 2, 2, 0) == QS_ERR_QUBIT);
    CHECK(G(&S1, QS_G_CCX, 2, 0, 0) == QS_ERR_QUBIT);
    CHECK(G(&S1, QS_G_CCX, 2, 0, 2) == QS_ERR_QUBIT);
    CHECK(G(&S1, QS_G_CCX, 2, 0, 3) == QS_ERR_QUBIT);
    CHECK(G(&S1, QS_G_CCX, 2, 1, 0) == QS_OK);
    CHECK(G(&S1, QS_G_COUNT, 0, 0, 0) == QS_ERR_ARG);
    CHECK(G(&S1, 200, 0, 0, 0) == QS_ERR_ARG);
    CHECK(qs_gate(&S1, QS_G_PHASE, 0, 0, 0, 1, 3) == QS_ERR_ARG);
    CHECK(qs_gate(&S1, QS_G_PHASE, 0, 0, 0, 1, 0) == QS_ERR_ARG);
    CHECK(qs_gate(&S1, QS_G_PHASE, 0, 0, 0, 1, 32) == QS_ERR_ARG);
    CHECK(qs_gate(&S1, QS_G_PHASE, 0, 0, 0, 1, 16) == QS_ERR_PHASE); /* N=8 */
    CHECK(qs_gate(&S1, QS_G_CPHASE, 0, 1, 0, 1, 16) == QS_ERR_PHASE);
    CHECK(qs_gate(&S1, QS_G_CPHASE, 0, 0, 0, 1, 8) == QS_ERR_QUBIT);
    CHECK(qs_gate(&S1, QS_G_PHASE, 0, 0, 0, 1, 8) == QS_OK);
    CHECK(qs_gate(NULL, QS_G_H, 0, 0, 0, 0, 0) == QS_ERR_NULL);
    CHECK(qs_apply(&S1, NULL) == QS_ERR_NULL);
    /* CCX with both controls set flipped the target: |100> -> |100> (c1=0
     * unset), so state is still |100> = 4 */
    {
        qs_elem_t a;
        uint32_t kk;
        CHECK(qs_amplitude(&S1, 4, &a, &kk) == QS_OK && is_int(&a, 1));
    }
    CHECK(qs_init(&S2, 3, 16) == QS_OK);
    CHECK(qs_gate(&S2, QS_G_PHASE, 0, 0, 0, 1, 16) == QS_OK);
    {
        int eq;
        CHECK(qs_state_equal(&S1, &S2, &eq) == QS_ERR_MISMATCH);
        CHECK(qs_state_equal(&S1, &S1, NULL) == QS_ERR_NULL);
        CHECK(qs_state_equal(NULL, &S1, &eq) == QS_ERR_NULL);
        CHECK(qs_init(&S2, 2, 8) == QS_OK);
        CHECK(qs_state_equal(&S1, &S2, &eq) == QS_ERR_MISMATCH);
    }

    /* circuits */
    CHECK(qs_circuit_init(NULL) == QS_ERR_NULL);
    CHECK(qs_circuit_init(&C1) == QS_OK && C1.count == 0);
    CHECK(qs_circuit_push(NULL, QS_G_H, 0, 0, 0, 0, 0) == QS_ERR_NULL);
    CHECK(qs_circuit_push(&C1, QS_G_COUNT, 0, 0, 0, 0, 0) == QS_ERR_ARG);
    CHECK(qs_circuit_push(&C1, QS_G_H, 10, 0, 0, 0, 0) == QS_ERR_QUBIT);
    CHECK(qs_circuit_push(&C1, QS_G_H, 0, 10, 0, 0, 0) == QS_ERR_QUBIT);
    CHECK(qs_circuit_push(&C1, QS_G_H, 0, 0, 10, 0, 0) == QS_ERR_QUBIT);
    CHECK(qs_circuit_push(&C1, QS_G_PHASE, 0, 0, 0, 1, 5) == QS_ERR_ARG);
    CHECK(qs_circuit_push(&C1, QS_G_CPHASE, 1, 0, 0, 1, 0) == QS_ERR_ARG);
    CHECK(qs_circuit_push(&C1, QS_G_H, 9, 9, 9, 7, 3) == QS_OK); /* k, m ignored */
    CHECK(C1.count == 1 && C1.g[0].k == 0 && C1.g[0].m == 0 && C1.g[0].t == 9);
    for (uint32_t i = 1; i < QS_MAX_GATES; i++) qs_circuit_push(&C1, QS_G_X, 0, 0, 0, 0, 0);
    CHECK(C1.count == QS_MAX_GATES);
    CHECK(qs_circuit_push(&C1, QS_G_X, 0, 0, 0, 0, 0) == QS_ERR_FULL);
    CHECK(C1.count == QS_MAX_GATES);
    CHECK(qs_circuit_adjoint(&C1, &C1) == QS_ERR_ARG);
    CHECK(qs_circuit_adjoint(NULL, &C2) == QS_ERR_NULL);
    CHECK(qs_circuit_adjoint(&C1, NULL) == QS_ERR_NULL);
    C1.count = QS_MAX_GATES + 1u;
    CHECK(qs_circuit_adjoint(&C1, &C2) == QS_ERR_ARG);
    CHECK(qs_circuit_push(&C1, QS_G_X, 0, 0, 0, 0, 0) == QS_ERR_ARG);
    CHECK(qs_init(&S1, 2, 8) == QS_OK);
    CHECK(qs_run_circuit(&S1, &C1, NULL) == QS_ERR_ARG);

    /* run stops at the first bad gate and reports how many were applied */
    qs_circuit_init(&C1);
    qs_circuit_push(&C1, QS_G_X, 0, 0, 0, 0, 0);
    qs_circuit_push(&C1, QS_G_X, 1, 0, 0, 0, 0);
    qs_circuit_push(&C1, QS_G_X, 5, 0, 0, 0, 0); /* qubit 5 does not exist in 2 qubits */
    qs_circuit_push(&C1, QS_G_X, 0, 0, 0, 0, 0);
    {
        uint32_t done = 99;
        CHECK(qs_init(&S1, 2, 8) == QS_OK);
        CHECK(qs_run_circuit(&S1, &C1, &done) == QS_ERR_QUBIT && done == 2);
        qs_elem_t a;
        uint32_t kk;
        CHECK(qs_amplitude(&S1, 3, &a, &kk) == QS_OK && is_int(&a, 1));
        CHECK(qs_run_circuit(&S1, NULL, &done) == QS_ERR_NULL && done == 0);
        CHECK(qs_run_circuit(NULL, &C1, &done) == QS_ERR_NULL);
    }
    /* adjoint field mapping */
    qs_circuit_init(&C1);
    qs_circuit_push(&C1, QS_G_S, 0, 0, 0, 0, 0);
    qs_circuit_push(&C1, QS_G_T, 1, 0, 0, 0, 0);
    qs_circuit_push(&C1, QS_G_PHASE, 2, 0, 0, 3, 8);
    qs_circuit_push(&C1, QS_G_CPHASE, 2, 1, 0, -3, 16);
    qs_circuit_push(&C1, QS_G_PHASE, 2, 0, 0, 8, 8);
    qs_circuit_push(&C1, QS_G_SDG, 0, 0, 0, 0, 0);
    qs_circuit_push(&C1, QS_G_TDG, 0, 0, 0, 0, 0);
    qs_circuit_push(&C1, QS_G_CCX, 2, 0, 1, 0, 0);
    qs_circuit_push(&C1, QS_G_PHASE, 2, 0, 0, INT32_MIN, 16);
    CHECK(qs_circuit_adjoint(&C1, &C2) == QS_OK && C2.count == 9);
    CHECK(C2.g[8].kind == QS_G_SDG && C2.g[8].t == 0);
    CHECK(C2.g[7].kind == QS_G_TDG && C2.g[7].t == 1);
    CHECK(C2.g[6].kind == QS_G_PHASE && C2.g[6].k == 5 && C2.g[6].m == 8);
    CHECK(C2.g[5].kind == QS_G_CPHASE && C2.g[5].k == 3 && C2.g[5].m == 16 && C2.g[5].c0 == 1);
    CHECK(C2.g[4].kind == QS_G_PHASE && C2.g[4].k == 0);
    CHECK(C2.g[3].kind == QS_G_S);
    CHECK(C2.g[2].kind == QS_G_T);
    CHECK(C2.g[1].kind == QS_G_CCX && C2.g[1].c0 == 0 && C2.g[1].c1 == 1 && C2.g[1].t == 2);
    CHECK(C2.g[0].kind == QS_G_PHASE && C2.g[0].k == 0); /* INT32_MIN = 0 mod 16 */
    C1.g[0].kind = 77;
    CHECK(qs_circuit_adjoint(&C1, &C2) == QS_ERR_ARG);
    C1.g[0].kind = QS_G_PHASE;
    C1.g[0].m = 6;
    CHECK(qs_circuit_adjoint(&C1, &C2) == QS_ERR_ARG);

    /* load */
    memset(AMPS, 0, sizeof AMPS);
    AMPS[0].c[0] = 1;
    AMPS[1].c[0] = 1;
    CHECK(qs_load(&S1, 1, 8, 1, AMPS) == QS_OK && qs_check_unit(&S1) == QS_OK && S1.k == 1);
    CHECK(qs_load(NULL, 1, 8, 1, AMPS) == QS_ERR_NULL);
    CHECK(qs_load(&S1, 1, 8, 1, NULL) == QS_ERR_NULL);
    CHECK(qs_load(&S1, 0, 8, 1, AMPS) == QS_ERR_TOO_MANY);
    CHECK(qs_load(&S1, 11, 8, 1, AMPS) == QS_ERR_TOO_MANY);
    CHECK(qs_load(&S1, 1, 9, 1, AMPS) == QS_ERR_ARG);
    CHECK(qs_load(&S1, 1, 8, 4097, AMPS) == QS_ERR_ARG);
    CHECK(qs_load(&S1, 1, 8, 4096, AMPS) == QS_OK);
    AMPS[1].c[4] = 1; /* beyond D for N = 8 */
    CHECK(qs_load(&S1, 1, 8, 1, AMPS) == QS_ERR_ARG);
    CHECK(qs_load(&S1, 1, 16, 1, AMPS) == QS_OK); /* fine for N = 16 */
    AMPS[1].c[4] = 0;
    /* load reduces: (2, 2) over sqrt2^3 is (1,1)/sqrt2 */
    AMPS[0].c[0] = 2;
    AMPS[1].c[0] = 2;
    CHECK(qs_load(&S1, 1, 8, 3, AMPS) == QS_OK && S1.k == 1 && S1.amp[0].c[0] == 1);
    CHECK(qs_check_unit(&S1) == QS_OK);
    /* unnormalised load: check_unit reports NORM */
    CHECK(qs_load(&S1, 1, 8, 0, AMPS) == QS_OK && qs_check_unit(&S1) == QS_ERR_NORM);
    AMPS[0].c[0] = 1;
    AMPS[0].c[1] = 1; /* (1 + z) and 0 over k=0: norm has a sqrt2 part */
    AMPS[1].c[0] = 0;
    CHECK(qs_load(&S1, 1, 8, 0, AMPS) == QS_OK && qs_check_unit(&S1) == QS_ERR_NORM);
    memset(AMPS, 0, sizeof AMPS);
}

static void test_bell_ghz(void)
{
    for (uint32_t N = 8; N <= 16; N += 8) {
        qs_prob_t p;
        qs_elem_t a;
        uint32_t k;
        int c;
        CHECK(qs_init(&S1, 2, N) == QS_OK);
        CHECK(G(&S1, QS_G_H, 0, 0, 0) == QS_OK);
        CHECK(G(&S1, QS_G_CNOT, 1, 0, 0) == QS_OK);
        CHECK(qs_amplitude(&S1, 0, &a, &k) == QS_OK && is_int(&a, 1) && k == 1);
        CHECK(qs_amplitude(&S1, 3, &a, &k) == QS_OK && is_int(&a, 1));
        CHECK(qs_amplitude(&S1, 1, &a, &k) == QS_OK && is_int(&a, 0));
        CHECK(qs_amplitude(&S1, 2, &a, &k) == QS_OK && is_int(&a, 0));
        CHECK(qs_check_unit(&S1) == QS_OK);
        CHECK(qs_prob_basis(&S1, 0, &p) == QS_OK && is_int(&p.num, 1) && is_int(&p.den, 2));
        CHECK(prob_is(&S1, &p, 1, 2));
        CHECK(prob_is(&S1, &p, 1u << 31, (int64_t) 1 << 32));
        CHECK(qs_prob_cmp(N, &p, 1, 3, &c) == QS_OK && c == 1);
        CHECK(qs_prob_cmp(N, &p, 2, 3, &c) == QS_OK && c == -1);
        CHECK(qs_prob_cmp(N, &p, 1, 0, &c) == QS_ERR_ARG);
        CHECK(qs_prob_cmp(N, &p, 1, -2, &c) == QS_ERR_ARG);
        CHECK(qs_prob_cmp(N, NULL, 1, 2, &c) == QS_ERR_NULL);
        CHECK(qs_prob_cmp(N, &p, 1, 2, NULL) == QS_ERR_NULL);
        CHECK(qs_prob_cmp(12, &p, 1, 2, &c) == QS_ERR_ARG);
        CHECK(qs_prob_basis(&S1, 1, &p) == QS_OK && is_int(&p.num, 0) && prob_is(&S1, &p, 0, 1));
        CHECK(qs_prob_basis(&S1, 4, &p) == QS_ERR_INDEX);
        CHECK(qs_prob_basis(&S1, 0, NULL) == QS_ERR_NULL);
        CHECK(qs_prob_qubit(&S1, 1, &p) == QS_OK && prob_is(&S1, &p, 1, 2));
        CHECK(qs_prob_qubit(&S1, 0, &p) == QS_OK && prob_is(&S1, &p, 1, 2));
        CHECK(qs_prob_qubit(&S1, 2, &p) == QS_ERR_QUBIT);
        CHECK(qs_prob_qubit(&S1, 0, NULL) == QS_ERR_NULL);
        /* Bell minus via Z: amplitude of |11> becomes -1 */
        CHECK(G(&S1, QS_G_Z, 1, 0, 0) == QS_OK);
        CHECK(qs_amplitude(&S1, 3, &a, &k) == QS_OK && is_int(&a, -1) && k == 1);
        CHECK(qs_check_unit(&S1) == QS_OK);
    }
    for (uint32_t n = 3; n <= 6; n++) {
        for (uint32_t N = 8; N <= 16; N += 8) {
            uint32_t last = (1u << n) - 1u, idx = 0;
            CHECK(qs_init(&S1, n, N) == QS_OK);
            CHECK(G(&S1, QS_G_H, 0, 0, 0) == QS_OK);
            for (uint32_t q = 1; q < n; q++) CHECK(G(&S1, QS_G_CNOT, q, q - 1, 0) == QS_OK);
            CHECK(S1.k == 1 && qs_check_unit(&S1) == QS_OK);
            int ok = 1;
            for (uint32_t i = 0; i <= last; i++) {
                int64_t want = (i == 0 || i == last) ? 1 : 0;
                if (!is_int(&S1.amp[i], want)) ok = 0;
            }
            CHECK(ok);
            for (uint32_t q = 0; q < n; q++) {
                qs_prob_t p;
                CHECK(qs_prob_qubit(&S1, q, &p) == QS_OK && prob_is(&S1, &p, 1, 2));
            }
            /* exact sampling boundary at 1/2 */
            CHECK(qs_sample(&S1, 0, &idx) == QS_OK && idx == 0);
            CHECK(qs_sample(&S1, 0x7FFFFFFFu, &idx) == QS_OK && idx == 0);
            CHECK(qs_sample(&S1, 0x80000000u, &idx) == QS_OK && idx == last);
            CHECK(qs_sample(&S1, 0xFFFFFFFFu, &idx) == QS_OK && idx == last);
            CHECK(qs_sample(&S1, 0, NULL) == QS_ERR_NULL);
            CHECK(qs_sample(NULL, 0, &idx) == QS_ERR_NULL);
            /* measure qubit n-1 = 1: collapse to |1...1>, norm 1/2 */
            CHECK(qs_measure(&S1, n - 1u, 1) == QS_OK);
            CHECK(qs_check_unit(&S1) == QS_ERR_NORM);
            {
                qs_prob_t p;
                qs_elem_t nrm;
                uint32_t k;
                CHECK(qs_norm_sq(&S1, &nrm, &k) == QS_OK && is_int(&nrm, 1) && k == 1);
                CHECK(qs_prob_basis(&S1, last, &p) == QS_OK && prob_is(&S1, &p, 1, 1));
                CHECK(qs_prob_basis(&S1, 0, &p) == QS_OK && prob_is(&S1, &p, 0, 1));
                CHECK(qs_prob_qubit(&S1, 0, &p) == QS_OK && prob_is(&S1, &p, 1, 1));
            }
            CHECK(qs_measure(&S1, 0, 0) == QS_ERR_ZERO_PROB);
            CHECK(S1.amp[last].c[0] == 1 && S1.collapsed == 1);
            CHECK(qs_measure(&S1, n, 0) == QS_ERR_QUBIT);
            CHECK(qs_measure(&S1, 0, 2) == QS_ERR_ARG);
            CHECK(qs_measure(NULL, 0, 0) == QS_ERR_NULL);
            CHECK(qs_renormalise(&S1) == QS_OK && S1.k == 0 && qs_check_unit(&S1) == QS_OK);
            CHECK(qs_sample(&S1, 0, &idx) == QS_OK && idx == last);
            CHECK(qs_renormalise(&S1) == QS_OK && S1.k == 0); /* idempotent */
        }
    }
}

static void test_qft(void)
{
    for (uint32_t N = 8; N <= 16; N += 8) {
        for (uint32_t n = 2; n <= 4; n++) {
            build_qft(&C1, n);
            CHECK(qs_circuit_adjoint(&C1, &C2) == QS_OK && C2.count == C1.count);
            uint32_t dim = 1u << n;
            for (uint32_t x = 0; x < dim; x++) {
                CHECK(qs_init_basis(&S1, n, N, x) == QS_OK);
                uint32_t done = 0;
                int rc = qs_run_circuit(&S1, &C1, &done);
                if (N == 8u && n == 4u) {
                    /* the 2*pi/16 controlled phase needs N = 16 */
                    CHECK(rc == QS_ERR_PHASE && done == 3); /* H, CP(1/4), CP(1/8) */
                    continue;
                }
                CHECK(rc == QS_OK && done == C1.count);
                CHECK(S1.k == n);
                int ok = 1;
                for (uint32_t y = 0; y < dim; y++) {
                    qs_elem_t want;
                    unit(&want, N, (N / dim) * ((x * y) % dim));
                    if (!elem_eq(&S1.amp[y], &want)) ok = 0;
                }
                CHECK(ok);
                CHECK(qs_check_unit(&S1) == QS_OK);
                qs_prob_t p;
                CHECK(qs_prob_basis(&S1, dim - 1u, &p) == QS_OK && prob_is(&S1, &p, 1, dim));
                CHECK(qs_run_circuit(&S1, &C2, &done) == QS_OK);
                CHECK(qs_init_basis(&S2, n, N, x) == QS_OK && equal_states(&S1, &S2));
            }
        }
    }
    /* QFT on a non-basis state followed by its adjoint is the identity */
    build_qft(&C1, 4);
    qs_circuit_adjoint(&C1, &C2);
    CHECK(qs_init(&S1, 4, 16) == QS_OK);
    G(&S1, QS_G_H, 0, 0, 0);
    G(&S1, QS_G_T, 0, 0, 0);
    G(&S1, QS_G_CNOT, 2, 0, 0);
    G(&S1, QS_G_H, 3, 0, 0);
    qs_gate(&S1, QS_G_PHASE, 3, 0, 0, 3, 16);
    memcpy(&S3, &S1, sizeof S1);
    CHECK(qs_run_circuit(&S1, &C1, NULL) == QS_OK);
    CHECK(!equal_states(&S1, &S3));
    CHECK(qs_run_circuit(&S1, &C2, NULL) == QS_OK && equal_states(&S1, &S3));
}

/* A deterministic non-trivial state for identity checks. */
static void prep(qs_state_t *st, uint32_t n, uint32_t N, uint64_t seed)
{
    g_rng = seed;
    random_circuit(&C1, n, N, 40);
    qs_init(st, n, N);
    qs_run_circuit(st, &C1, NULL);
}

static void test_identities(void)
{
    for (uint32_t N = 8; N <= 16; N += 8) {
        for (uint64_t seed = 1; seed <= 6; seed++) {
            prep(&S3, 3, N, seed);
            CHECK(qs_check_unit(&S3) == QS_OK);
            /* HH = I */
            memcpy(&S1, &S3, sizeof S1);
            G(&S1, QS_G_H, 1, 0, 0);
            CHECK(!equal_states(&S1, &S3) || seed == 0);
            G(&S1, QS_G_H, 1, 0, 0);
            CHECK(equal_states(&S1, &S3));
            /* T^8 = I, and T^k != I for 0 < k < 8 on a state with weight on |1> */
            memcpy(&S1, &S3, sizeof S1);
            G(&S1, QS_G_H, 0, 0, 0);
            memcpy(&S2, &S1, sizeof S1);
            for (int i = 1; i <= 8; i++) {
                G(&S1, QS_G_T, 0, 0, 0);
                qs_prob_t p;
                qs_prob_qubit(&S1, 0, &p);
                int c;
                qs_prob_cmp(N, &p, 0, 1, &c);
                if (c > 0) CHECK(equal_states(&S1, &S2) == (i == 8));
            }
            /* S = T^2, Z = S^2, Sdg = S^3, Tdg = T^7 */
            memcpy(&S1, &S2, sizeof S1);
            memcpy(&S3, &S2, sizeof S1);
            G(&S1, QS_G_S, 2, 0, 0);
            G(&S3, QS_G_T, 2, 0, 0);
            G(&S3, QS_G_T, 2, 0, 0);
            CHECK(equal_states(&S1, &S3));
            G(&S1, QS_G_S, 2, 0, 0);
            memcpy(&S3, &S2, sizeof S1);
            G(&S3, QS_G_Z, 2, 0, 0);
            CHECK(equal_states(&S1, &S3));
            memcpy(&S1, &S2, sizeof S1);
            memcpy(&S3, &S2, sizeof S1);
            G(&S1, QS_G_SDG, 2, 0, 0);
            for (int i = 0; i < 3; i++) G(&S3, QS_G_S, 2, 0, 0);
            CHECK(equal_states(&S1, &S3));
            memcpy(&S1, &S2, sizeof S1);
            memcpy(&S3, &S2, sizeof S1);
            G(&S1, QS_G_TDG, 1, 0, 0);
            for (int i = 0; i < 7; i++) G(&S3, QS_G_T, 1, 0, 0);
            CHECK(equal_states(&S1, &S3));
            /* PHASE(k/m) equals the named gates, k taken mod m */
            const struct {
                int32_t k;
                uint32_t m;
                uint32_t kind;
            } eqv[] = {{1, 8, QS_G_T},     {9, 8, QS_G_T},   {-1, 8, QS_G_TDG}, {7, 8, QS_G_TDG},
                       {1, 4, QS_G_S},     {2, 8, QS_G_S},   {3, 4, QS_G_SDG},  {1, 2, QS_G_Z},
                       {-5, 2, QS_G_Z},    {0, 8, QS_G_I},   {5, 1, QS_G_I},    {4, 16, QS_G_S},
                       {-2, 16, QS_G_TDG}, {16, 16, QS_G_I}, {2, 4, QS_G_Z}};
            for (uint32_t e = 0; e < sizeof eqv / sizeof eqv[0]; e++) {
                memcpy(&S1, &S2, sizeof S1);
                memcpy(&S3, &S2, sizeof S1);
                int rc = qs_gate(&S1, QS_G_PHASE, 1, 0, 0, eqv[e].k, eqv[e].m);
                if (eqv[e].m == 16u && N == 8u) {
                    CHECK(rc == QS_ERR_PHASE && equal_states(&S1, &S2));
                    continue;
                }
                CHECK(rc == QS_OK);
                G(&S3, eqv[e].kind, 1, 0, 0);
                CHECK(equal_states(&S1, &S3));
            }
            /* CPHASE(1/2) = CZ, and CZ is symmetric */
            memcpy(&S1, &S2, sizeof S1);
            memcpy(&S3, &S2, sizeof S1);
            qs_gate(&S1, QS_G_CPHASE, 0, 2, 0, 1, 2);
            G(&S3, QS_G_CZ, 2, 0, 0);
            CHECK(equal_states(&S1, &S3));
            /* X^2 = Y^2 = Z^2 = SWAP^2 = CNOT^2 = CCX^2 = I */
            const uint32_t inv2[][3] = {{QS_G_X, 0, 0},    {QS_G_Y, 1, 0},    {QS_G_Z, 2, 0},
                                        {QS_G_SWAP, 0, 2}, {QS_G_CNOT, 1, 2}, {QS_G_CCX, 0, 1}};
            for (uint32_t e = 0; e < 6; e++) {
                memcpy(&S1, &S2, sizeof S1);
                G(&S1, inv2[e][0], inv2[e][1], inv2[e][2], 2);
                G(&S1, inv2[e][0], inv2[e][1], inv2[e][2], 2);
                CHECK(equal_states(&S1, &S2));
            }
            /* Y = i X Z  (Z first, then X, then global phase i) */
            memcpy(&S1, &S2, sizeof S1);
            memcpy(&S3, &S2, sizeof S1);
            G(&S1, QS_G_Y, 0, 0, 0);
            G(&S3, QS_G_Z, 0, 0, 0);
            G(&S3, QS_G_X, 0, 0, 0);
            for (uint32_t i = 0; i < 8; i++) {
                qs_elem_t t;
                unit(&t, N, N / 4u);
                qs_elem_t r;
                memset(&r, 0, sizeof r);
                for (uint32_t a = 0; a < N / 2u; a++) /* r = i * amp */
                    for (uint32_t b = 0; b < N / 2u; b++) {
                        uint32_t ix = a + b;
                        int64_t v = S3.amp[i].c[a] * t.c[b];
                        if (ix >= N / 2u)
                            r.c[ix - N / 2u] -= v;
                        else
                            r.c[ix] += v;
                    }
                S3.amp[i] = r;
            }
            CHECK(equal_states(&S1, &S3));
            /* H X H = Z */
            memcpy(&S1, &S2, sizeof S1);
            memcpy(&S3, &S2, sizeof S1);
            G(&S1, QS_G_H, 1, 0, 0);
            G(&S1, QS_G_X, 1, 0, 0);
            G(&S1, QS_G_H, 1, 0, 0);
            G(&S3, QS_G_Z, 1, 0, 0);
            CHECK(equal_states(&S1, &S3));
            /* SWAP = 3 CNOTs */
            memcpy(&S1, &S2, sizeof S1);
            memcpy(&S3, &S2, sizeof S1);
            G(&S1, QS_G_SWAP, 0, 2, 0);
            G(&S3, QS_G_CNOT, 0, 2, 0);
            G(&S3, QS_G_CNOT, 2, 0, 0);
            G(&S3, QS_G_CNOT, 0, 2, 0);
            CHECK(equal_states(&S1, &S3));
            /* I changes nothing */
            memcpy(&S1, &S2, sizeof S1);
            CHECK(G(&S1, QS_G_I, 2, 0, 0) == QS_OK && equal_states(&S1, &S2));
        }
    }
    /* Y|0> = i|1>, Y|1> = -i|0> */
    for (uint32_t N = 8; N <= 16; N += 8) {
        qs_elem_t want;
        CHECK(qs_init(&S1, 1, N) == QS_OK && G(&S1, QS_G_Y, 0, 0, 0) == QS_OK);
        unit(&want, N, N / 4u);
        CHECK(elem_eq(&S1.amp[1], &want) && is_int(&S1.amp[0], 0));
        CHECK(G(&S1, QS_G_X, 0, 0, 0) == QS_OK && elem_eq(&S1.amp[0], &want));
        CHECK(G(&S1, QS_G_Y, 0, 0, 0) == QS_OK);
        unit(&want, N, N / 2u); /* i * i = -1 on |1> */
        CHECK(elem_eq(&S1.amp[1], &want));
        /* T on |1> gives z8; PHASE(1/16) on |1> gives z16 for N=16 */
        CHECK(qs_init_basis(&S1, 1, N, 1) == QS_OK && G(&S1, QS_G_T, 0, 0, 0) == QS_OK);
        unit(&want, N, N / 8u);
        CHECK(elem_eq(&S1.amp[1], &want));
        CHECK(G(&S1, QS_G_TDG, 0, 0, 0) == QS_OK && is_int(&S1.amp[1], 1));
        CHECK(G(&S1, QS_G_S, 0, 0, 0) == QS_OK);
        unit(&want, N, N / 4u);
        CHECK(elem_eq(&S1.amp[1], &want));
        CHECK(G(&S1, QS_G_SDG, 0, 0, 0) == QS_OK && is_int(&S1.amp[1], 1));
        CHECK(G(&S1, QS_G_Z, 0, 0, 0) == QS_OK && is_int(&S1.amp[1], -1));
        /* phases do nothing on |0> */
        CHECK(qs_init(&S1, 1, N) == QS_OK && G(&S1, QS_G_T, 0, 0, 0) == QS_OK &&
              G(&S1, QS_G_S, 0, 0, 0) == QS_OK && G(&S1, QS_G_Z, 0, 0, 0) == QS_OK &&
              is_int(&S1.amp[0], 1));
        if (N == 16u) {
            CHECK(qs_init_basis(&S1, 1, N, 1) == QS_OK &&
                  qs_gate(&S1, QS_G_PHASE, 0, 0, 0, 1, 16) == QS_OK);
            unit(&want, N, 1);
            CHECK(elem_eq(&S1.amp[1], &want));
        }
        /* controlled gates act only when the controls are 1 */
        for (uint32_t x = 0; x < 8; x++) {
            CHECK(qs_init_basis(&S1, 3, N, x) == QS_OK);
            CHECK(G(&S1, QS_G_CNOT, 0, 2, 0) == QS_OK);
            uint32_t y = (x & 4u) ? (x ^ 1u) : x;
            CHECK(is_int(&S1.amp[y], 1));
            CHECK(G(&S1, QS_G_CCX, 1, 0, 2) == QS_OK);
            uint32_t w = ((y & 1u) && (y & 4u)) ? (y ^ 2u) : y;
            CHECK(is_int(&S1.amp[w], 1));
            CHECK(G(&S1, QS_G_CZ, 1, 0, 0) == QS_OK);
            CHECK(is_int(&S1.amp[w], ((w & 3u) == 3u) ? -1 : 1));
            CHECK(G(&S1, QS_G_SWAP, 2, 0, 0) == QS_OK);
            uint32_t v = (w & 2u) | ((w & 1u) << 2) | ((w & 4u) >> 2);
            CHECK(is_int(&S1.amp[v], ((w & 3u) == 3u) ? -1 : 1));
            CHECK(qs_gate(&S1, QS_G_CPHASE, 1, 2, 0, 1, 4) == QS_OK);
            qs_elem_t a;
            unit(&a, N, (((w & 3u) == 3u) ? N / 2u : 0u) + (((v & 6u) == 6u) ? N / 4u : 0u));
            CHECK(elem_eq(&S1.amp[v], &a));
        }
    }
}

static void test_toffoli(void)
{
    const uint32_t trip[][4] = {
        {0, 1, 2, 3}, {2, 0, 1, 3}, {1, 2, 0, 3}, {3, 1, 0, 4}, {0, 3, 2, 4}};
    for (uint32_t N = 8; N <= 16; N += 8) {
        for (uint32_t t = 0; t < 5; t++) {
            uint32_t a = trip[t][0], b = trip[t][1], c = trip[t][2], n = trip[t][3];
            qs_circuit_init(&C1);
            push_toffoli7(&C1, a, b, c);
            uint32_t tc = 0;
            for (uint32_t i = 0; i < C1.count; i++)
                if (C1.g[i].kind == QS_G_T || C1.g[i].kind == QS_G_TDG) tc++;
            CHECK(tc == 7 && C1.count == 15);
            int all = 1;
            for (uint32_t x = 0; x < (1u << n); x++) {
                qs_init_basis(&S1, n, N, x);
                qs_init_basis(&S2, n, N, x);
                if (qs_run_circuit(&S1, &C1, NULL) != QS_OK) all = 0;
                if (G(&S2, QS_G_CCX, c, a, b) != QS_OK) all = 0;
                if (!equal_states(&S1, &S2)) all = 0;
                if (S1.k != 0) all = 0;
            }
            CHECK(all);
            /* and on superpositions */
            for (uint64_t seed = 100; seed < 106; seed++) {
                prep(&S3, n, N, seed);
                qs_circuit_init(&C1);
                push_toffoli7(&C1, a, b, c);
                memcpy(&S1, &S3, sizeof S1);
                memcpy(&S2, &S3, sizeof S1);
                CHECK(qs_run_circuit(&S1, &C1, NULL) == QS_OK);
                CHECK(G(&S2, QS_G_CCX, c, a, b) == QS_OK);
                CHECK(equal_states(&S1, &S2));
            }
        }
    }
    /* a deliberately wrong decomposition (one T replaced by Tdg) differs */
    qs_circuit_init(&C1);
    push_toffoli7(&C1, 0, 1, 2);
    C1.g[4].kind = QS_G_TDG;
    int differs = 0;
    for (uint32_t x = 0; x < 8; x++) {
        qs_init_basis(&S1, 3, 8, x);
        qs_init_basis(&S2, 3, 8, x);
        qs_run_circuit(&S1, &C1, NULL);
        G(&S2, QS_G_CCX, 2, 0, 1);
        if (!equal_states(&S1, &S2)) differs = 1;
    }
    CHECK(differs);
}

static int64_t g_max_coef;
static uint32_t g_max_k, g_max_prob_k;
static double g_max_err;

static void test_random(void)
{
    uint32_t circuits = 0, cmp_checked = 0, samples = 0;
    for (uint64_t seed = 1; seed <= 500; seed++) {
        g_rng = seed * 7919u;
        g_theavy = seed > 400u;
        uint32_t n = 1u + rndn(6);
        uint32_t N = (seed & 1u) ? 8u : 16u;
        uint32_t len = 10u + rndn(151);
        random_circuit(&C1, n, N, len);
        CHECK(qs_init(&S1, n, N) == QS_OK);
        ref_init(n);
        int ok = 1;
        for (uint32_t i = 0; i < C1.count; i++) {
            if (qs_apply(&S1, &C1.g[i]) != QS_OK) ok = 0;
            ref_gate(n, &C1.g[i]);
            if (S1.k > g_max_k) g_max_k = S1.k;
            for (uint32_t x = 0; x < (1u << n); x++)
                for (uint32_t j = 0; j < QS_MAX_D; j++) {
                    int64_t v = S1.amp[x].c[j];
                    if (v < 0) v = -v;
                    if (v > g_max_coef) g_max_coef = v;
                    /* coefficient bound |c| <= sqrt2^k, i.e. c^2 <= 2^k */
                    if ((double) v * (double) v > pow(2.0, (double) S1.k) + 0.5) ok = 0;
                }
        }
        CHECK(ok);
        circuits++;
        double err = 0.0;
        for (uint32_t x = 0; x < (1u << n); x++) {
            double re, im;
            elem_to_c(&S1.amp[x], N, S1.k, &re, &im);
            double e = fabs(re - RRe[x]) + fabs(im - RIm[x]);
            if (e > err) err = e;
        }
        if (err > g_max_err) g_max_err = err;
        CHECK(err < 1e-9);
        CHECK(qs_check_unit(&S1) == QS_OK);
        if (S1.k > g_max_prob_k) g_max_prob_k = S1.k;
        /* exact probability comparison agrees with the float probability */
        for (uint32_t r = 0; r < 4; r++) {
            uint32_t x = rndn(1u << n);
            qs_prob_t p;
            CHECK(qs_prob_basis(&S1, x, &p) == QS_OK);
            double pf = RRe[x] * RRe[x] + RIm[x] * RIm[x];
            double ex = real_val(&p.num, N) / real_val(&p.den, N);
            CHECK(fabs(ex - pf) < 1e-9);
            int64_t q = (int64_t) 1 << 24, a = (int64_t) floor(pf * (double) q);
            int c;
            if (pf - (double) a / (double) q > 1e-9) {
                CHECK(qs_prob_cmp(N, &p, a, q, &c) == QS_OK && c == 1);
                cmp_checked++;
            }
            if ((double) (a + 1) / (double) q - pf > 1e-9) {
                CHECK(qs_prob_cmp(N, &p, a + 1, q, &c) == QS_OK && c == -1);
                cmp_checked++;
            }
        }
        /* sampling agrees with the float cumulative distribution */
        for (uint32_t r = 0; r < 3; r++) {
            uint32_t u = (uint32_t) rnd(), idx = 0;
            double uf = (double) u / 4294967296.0, cum = 0.0;
            uint32_t want = 0;
            int near = 0;
            for (uint32_t x = 0; x < (1u << n); x++) {
                cum += RRe[x] * RRe[x] + RIm[x] * RIm[x];
                if (fabs(cum - uf) < 1e-9) near = 1;
                if (cum > uf) {
                    want = x;
                    break;
                }
            }
            CHECK(qs_sample(&S1, u, &idx) == QS_OK);
            if (!near) {
                CHECK(idx == want);
                samples++;
            }
        }
        /* the adjoint circuit returns exactly to |0...0> */
        CHECK(qs_circuit_adjoint(&C1, &C2) == QS_OK);
        CHECK(qs_run_circuit(&S1, &C2, NULL) == QS_OK);
        CHECK(qs_init(&S2, n, N) == QS_OK && equal_states(&S1, &S2));
    }
    g_theavy = 0;
    printf("random: %u circuits, max |err| vs double %.3g, max k %u, max |coef| %lld, "
           "%u exact prob comparisons, %u samples checked\n",
           circuits, g_max_err, g_max_k, (long long) g_max_coef, cmp_checked, samples);
}

static void test_real_sign(void)
{
    qs_elem_t x;
    int s;
    int64_t r[4];
    /* N = 8: a + b sqrt2 */
    const int64_t z8[][3] = {{3, -2, 1},       {-3, 2, -1},     {99, -70, 1}, {-99, 70, -1},
                             {1393, -985, -1}, {-1393, 985, 1}, {0, 0, 0},    {5, 0, 1},
                             {0, -4, -1},      {2, 3, 1},       {-2, -3, -1}, {-1, 1, 1},
                             {1, -1, -1}};
    for (uint32_t i = 0; i < sizeof z8 / sizeof z8[0]; i++) {
        memset(&x, 0, sizeof x);
        x.c[0] = z8[i][0];
        x.c[1] = z8[i][1];
        x.c[3] = -z8[i][1];
        CHECK(qs_real_sign(8, &x, &s) == QS_OK && s == (int) z8[i][2]);
        CHECK(qs_real_coords(8, &x, r) == QS_OK && r[0] == z8[i][0] && r[1] == z8[i][1] &&
              r[2] == 0 && r[3] == 0);
    }
    /* Pell pairs up to int64: p - q sqrt2 has the sign of p^2 - 2 q^2 */
    {
        int64_t p = 1, q = 1;
        uint32_t cnt = 0;
        for (;;) {
            memset(&x, 0, sizeof x);
            x.c[0] = p;
            x.c[1] = -q;
            x.c[3] = q;
            __int128 d = (__int128) p * p - (__int128) 2 * q * q;
            int want = d > 0 ? 1 : -1;
            CHECK(qs_real_sign(8, &x, &s) == QS_OK && s == want);
            x.c[0] = -p;
            x.c[1] = q;
            x.c[3] = -q;
            CHECK(qs_real_sign(8, &x, &s) == QS_OK && s == -want);
            cnt++;
            if (p > INT64_MAX / 4 || q > INT64_MAX / 4) break;
            int64_t np = p + 2 * q, nq = p + q;
            p = np;
            q = nq;
        }
        CHECK(cnt > 40);
        /* extremes: INT64_MIN and INT64_MAX coordinates */
        memset(&x, 0, sizeof x);
        x.c[0] = INT64_MIN;
        x.c[1] = INT64_MAX;
        x.c[3] = -INT64_MAX;
        CHECK(qs_real_sign(8, &x, &s) == QS_OK && s == 1);
        x.c[0] = INT64_MAX;
        x.c[1] = -INT64_MAX;
        x.c[3] = INT64_MAX;
        CHECK(qs_real_sign(8, &x, &s) == QS_OK && s == -1);
    }
    /* N = 16: 1000 * 2cos(pi/8) vs 1847/1848, 1000 * 2cos(3pi/8) vs 765/766 */
    {
        memset(&x, 0, sizeof x);
        x.c[1] = 1000;
        x.c[7] = -1000;
        x.c[0] = -1847;
        CHECK(qs_real_sign(16, &x, &s) == QS_OK && s == 1);
        x.c[0] = -1848;
        CHECK(qs_real_sign(16, &x, &s) == QS_OK && s == -1);
        CHECK(qs_real_coords(16, &x, r) == QS_OK && r[0] == -1848 && r[1] == 1000 && r[2] == 0 &&
              r[3] == 0);
        memset(&x, 0, sizeof x);
        x.c[3] = 1000;
        x.c[5] = -1000;
        x.c[0] = -765;
        CHECK(qs_real_sign(16, &x, &s) == QS_OK && s == 1);
        x.c[0] = -766;
        CHECK(qs_real_sign(16, &x, &s) == QS_OK && s == -1);
        x.c[0] = 0;
        x.c[2] = -541; /* 765.37 - 541 * 1.41421 = 0.27 */
        x.c[6] = 541;
        CHECK(qs_real_sign(16, &x, &s) == QS_OK && s == 1);
        x.c[2] = -542;
        x.c[6] = 542;
        CHECK(qs_real_sign(16, &x, &s) == QS_OK && s == -1);
    }
    /* random reals in both rings versus double */
    g_rng = 4242;
    uint32_t checked = 0;
    for (uint32_t it = 0; it < 4000; it++) {
        uint32_t N = (it & 1u) ? 16u : 8u, D = N / 2u;
        int64_t lim = (it % 3u == 0) ? 1000 : ((int64_t) 1 << 40);
        int64_t co[4];
        for (uint32_t j = 0; j < 4; j++) co[j] = (int64_t) (rnd() % (uint64_t) (2 * lim)) - lim;
        memset(&x, 0, sizeof x);
        x.c[0] = co[0];
        x.c[1] = co[1];
        x.c[D - 1] = -co[1];
        if (N == 16u) {
            x.c[2] = co[2];
            x.c[6] = -co[2];
            x.c[3] = co[3];
            x.c[5] = -co[3];
        }
        double v = real_val(&x, N);
        CHECK(qs_real_sign(N, &x, &s) == QS_OK);
        double mag = fabs((double) co[0]) + fabs((double) co[1]) * 2 + fabs((double) co[2]) * 2 +
                     fabs((double) co[3]) * 2 + 1.0;
        if (fabs(v) > mag * 1e-12) {
            CHECK(s == (v > 0 ? 1 : -1));
            checked++;
        }
    }
    CHECK(checked > 3900);
    /* not real / bad arguments */
    unit(&x, 8, 1);
    CHECK(qs_real_sign(8, &x, &s) == QS_ERR_NOT_REAL);
    unit(&x, 8, 2); /* i */
    CHECK(qs_real_sign(8, &x, &s) == QS_ERR_NOT_REAL);
    unit(&x, 16, 4); /* i in Z[z16] */
    CHECK(qs_real_sign(16, &x, &s) == QS_ERR_NOT_REAL);
    unit(&x, 16, 1);
    CHECK(qs_real_sign(16, &x, &s) == QS_ERR_NOT_REAL);
    memset(&x, 0, sizeof x);
    x.c[5] = 1;
    CHECK(qs_real_sign(8, &x, &s) == QS_ERR_NOT_REAL);
    memset(&x, 0, sizeof x);
    x.c[1] = INT64_MIN; /* c1 + c3 overflows: not real, not a crash */
    x.c[3] = INT64_MIN;
    CHECK(qs_real_sign(8, &x, &s) == QS_ERR_NOT_REAL);
    memset(&x, 0, sizeof x);
    x.c[2] = 1; /* N=16: c2 needs c6 = -1 */
    CHECK(qs_real_sign(16, &x, &s) == QS_ERR_NOT_REAL);
    CHECK(qs_real_sign(8, NULL, &s) == QS_ERR_NULL);
    CHECK(qs_real_sign(8, &x, NULL) == QS_ERR_NULL);
    CHECK(qs_real_sign(10, &x, &s) == QS_ERR_ARG);
    CHECK(qs_real_coords(8, &x, NULL) == QS_ERR_NULL);
    /* prob_cmp: non-real numerator, non-positive denominator */
    {
        qs_prob_t p;
        int c;
        memset(&p, 0, sizeof p);
        p.den.c[0] = 2;
        p.num.c[2] = 1;
        CHECK(qs_prob_cmp(8, &p, 1, 2, &c) == QS_ERR_NOT_REAL);
        p.num.c[2] = 0;
        p.den.c[0] = 0;
        CHECK(qs_prob_cmp(8, &p, 1, 2, &c) == QS_ERR_ARG);
        p.den.c[0] = -2;
        CHECK(qs_prob_cmp(8, &p, 1, 2, &c) == QS_ERR_ARG);
        /* den = 3 - 2 sqrt2 > 0 (0.1716), num = 1: ratio 5.83 */
        p.den.c[0] = 3;
        p.den.c[1] = -2;
        p.den.c[3] = 2;
        p.num.c[0] = 1;
        CHECK(qs_prob_cmp(8, &p, 582, 100, &c) == QS_OK && c == 1);
        CHECK(qs_prob_cmp(8, &p, 583, 100, &c) == QS_OK && c == -1);
        /* large p, q */
        CHECK(qs_prob_cmp(8, &p, INT64_MAX, INT64_MAX / 5, &c) == QS_OK && c == 1);
        CHECK(qs_prob_cmp(8, &p, INT64_MAX, INT64_MAX / 6, &c) == QS_OK && c == -1);
        CHECK(qs_prob_cmp(8, &p, INT64_MIN, 1, &c) == QS_OK && c == 1);
    }
}

static void test_measure(void)
{
    for (uint32_t N = 8; N <= 16; N += 8) {
        qs_prob_t p;
        int c;
        uint32_t bit = 9;
        /* H T H |0>: P(0) = (2 + sqrt2)/4 = 0.853553... */
        CHECK(qs_init(&S1, 1, N) == QS_OK);
        G(&S1, QS_G_H, 0, 0, 0);
        G(&S1, QS_G_T, 0, 0, 0);
        G(&S1, QS_G_H, 0, 0, 0);
        CHECK(S1.k == 2 && qs_check_unit(&S1) == QS_OK);
        CHECK(qs_prob_basis(&S1, 0, &p) == QS_OK && is_int(&p.den, 4));
        int64_t r[4];
        CHECK(qs_real_coords(N, &p.num, r) == QS_OK);
        if (N == 8u)
            CHECK(r[0] == 2 && r[1] == 1);
        else
            CHECK(r[0] == 2 && r[1] == 0 && r[2] == 1 && r[3] == 0);
        CHECK(qs_prob_cmp(N, &p, 8535, 10000, &c) == QS_OK && c == 1);
        CHECK(qs_prob_cmp(N, &p, 8536, 10000, &c) == QS_OK && c == -1);
        CHECK(qs_prob_qubit(&S1, 0, &p) == QS_OK);
        CHECK(qs_prob_cmp(N, &p, 1464, 10000, &c) == QS_OK && c == 1);
        CHECK(qs_prob_cmp(N, &p, 1465, 10000, &c) == QS_OK && c == -1);
        /* exact boundary of the sampler: u0 = floor(P0 * 2^32) */
        double p0 = (2.0 + sqrt(2.0)) / 4.0;
        uint32_t u0 = (uint32_t) floor(p0 * 4294967296.0);
        uint32_t idx = 9;
        CHECK(qs_sample(&S1, u0, &idx) == QS_OK && idx == 0);
        CHECK(qs_sample(&S1, u0 + 1u, &idx) == QS_OK && idx == 1);
        memcpy(&S2, &S1, sizeof S1);
        CHECK(qs_measure_sample(&S2, 0, u0 + 1u, &bit) == QS_OK && bit == 1);
        CHECK(qs_prob_basis(&S2, 1, &p) == QS_OK && prob_is(&S2, &p, 1, 1));
        CHECK(qs_prob_basis(&S2, 0, &p) == QS_OK && prob_is(&S2, &p, 0, 1));
        memcpy(&S2, &S1, sizeof S1);
        CHECK(qs_measure_sample(&S2, 0, u0, &bit) == QS_OK && bit == 0);
        CHECK(qs_measure_sample(&S2, 0, 0xFFFFFFFFu, &bit) == QS_OK && bit == 0); /* P0 = 1 */
        CHECK(qs_measure_sample(&S2, 1, 0, &bit) == QS_ERR_QUBIT);
        CHECK(qs_measure_sample(&S2, 0, 0, NULL) == QS_ERR_NULL);
        CHECK(qs_measure_sample(NULL, 0, 0, &bit) == QS_ERR_NULL);
        /* exact tie: P0 = 1/2 = u/2^32 picks 1 (bit 0 needs u/2^32 < P0) */
        CHECK(qs_init(&S3, 1, N) == QS_OK && G(&S3, QS_G_H, 0, 0, 0) == QS_OK);
        CHECK(qs_measure_sample(&S3, 0, 0x80000000u, &bit) == QS_OK && bit == 1);
        CHECK(qs_init(&S3, 1, N) == QS_OK && G(&S3, QS_G_H, 0, 0, 0) == QS_OK);
        CHECK(qs_measure_sample(&S3, 0, 0x7FFFFFFFu, &bit) == QS_OK && bit == 0);
        CHECK(S3.collapsed == 1 && S3.amp[1].c[0] == 0 && S3.k == 1);
        /* collapse to 0: norm (2+sqrt2)/4 is not a power of two */
        memcpy(&S2, &S1, sizeof S1);
        CHECK(qs_measure(&S2, 0, 0) == QS_OK);
        CHECK(qs_check_unit(&S2) == QS_ERR_NORM);
        CHECK(qs_renormalise(&S2) == QS_ERR_NOT_EXACT);
        CHECK(qs_prob_basis(&S2, 0, &p) == QS_OK && prob_is(&S2, &p, 1, 1));
        CHECK(qs_prob_qubit(&S2, 0, &p) == QS_OK && prob_is(&S2, &p, 0, 1));
        CHECK(qs_sample(&S2, 0xFFFFFFFFu, &idx) == QS_OK && idx == 0);
        /* the collapsed state still evolves; H gives P(1) = 1/2 relative */
        CHECK(G(&S2, QS_G_H, 0, 0, 0) == QS_OK);
        CHECK(qs_prob_qubit(&S2, 0, &p) == QS_OK && prob_is(&S2, &p, 1, 2));
        /* measuring X on an entangled pair: P relative and exact */
        CHECK(qs_init(&S2, 2, N) == QS_OK);
        G(&S2, QS_G_H, 0, 0, 0);
        G(&S2, QS_G_T, 0, 0, 0);
        G(&S2, QS_G_H, 0, 0, 0);
        G(&S2, QS_G_CNOT, 1, 0, 0);
        CHECK(qs_measure(&S2, 1, 1) == QS_OK);
        CHECK(qs_prob_qubit(&S2, 0, &p) == QS_OK && prob_is(&S2, &p, 1, 1));
        CHECK(qs_measure(&S2, 0, 0) == QS_ERR_ZERO_PROB);
        CHECK(qs_measure(&S2, 0, 1) == QS_OK && S2.collapsed == 2);
        /* renormalise refuses a zero state and a non-power-of-two norm */
        memset(AMPS, 0, sizeof AMPS);
        CHECK(qs_load(&S3, 1, N, 0, AMPS) == QS_OK);
        CHECK(qs_renormalise(&S3) == QS_ERR_NOT_EXACT);
        CHECK(qs_sample(&S3, 0, &idx) == QS_ERR_ARG);
        AMPS[0].c[0] = 1;
        AMPS[1].c[0] = 1;
        AMPS[2].c[0] = 1;
        CHECK(qs_load(&S3, 2, N, 0, AMPS) == QS_OK && qs_renormalise(&S3) == QS_ERR_NOT_EXACT);
        AMPS[3].c[0] = 1; /* norm 4 = 2^2 -> k = 2: (1,1,1,1)/2 */
        CHECK(qs_load(&S3, 2, N, 0, AMPS) == QS_OK && qs_renormalise(&S3) == QS_OK);
        CHECK(S3.k == 2 && qs_check_unit(&S3) == QS_OK);
        CHECK(qs_renormalise(NULL) == QS_ERR_NULL);
        memset(AMPS, 0, sizeof AMPS);
    }
}

static void test_overflow(void)
{
    /* forced: two INT64_MAX coefficients through H */
    memset(AMPS, 0, sizeof AMPS);
    AMPS[0].c[0] = INT64_MAX;
    AMPS[1].c[0] = INT64_MAX;
    CHECK(qs_load(&S1, 1, 8, 0, AMPS) == QS_OK);
    CHECK(G(&S1, QS_G_H, 0, 0, 0) == QS_ERR_OVERFLOW);
    CHECK(S1.broken != 0);
    {
        qs_elem_t e;
        qs_prob_t p;
        uint32_t k, idx;
        int eq;
        CHECK(G(&S1, QS_G_X, 0, 0, 0) == QS_ERR_BROKEN);
        CHECK(qs_check_unit(&S1) == QS_ERR_BROKEN);
        CHECK(qs_amplitude(&S1, 0, &e, &k) == QS_ERR_BROKEN);
        CHECK(qs_prob_basis(&S1, 0, &p) == QS_ERR_BROKEN);
        CHECK(qs_measure(&S1, 0, 0) == QS_ERR_BROKEN);
        CHECK(qs_sample(&S1, 0, &idx) == QS_ERR_BROKEN);
        CHECK(qs_state_equal(&S1, &S1, &eq) == QS_ERR_BROKEN);
        CHECK(qs_run_circuit(&S1, &C1, NULL) == QS_ERR_BROKEN);
        CHECK(qs_init(&S1, 1, 8) == QS_OK && S1.broken == 0); /* re-init clears it */
    }
    /* forced: negation of INT64_MIN under Z, Y, T */
    const uint32_t kinds[] = {QS_G_Z, QS_G_Y, QS_G_T, QS_G_S};
    for (uint32_t i = 0; i < 4; i++) {
        memset(AMPS, 0, sizeof AMPS);
        AMPS[0].c[3] = INT64_MIN;
        AMPS[1].c[3] = INT64_MIN;
        AMPS[1].c[0] = 1;
        CHECK(qs_load(&S1, 1, 8, 0, AMPS) == QS_OK);
        CHECK(G(&S1, kinds[i], 0, 0, 0) == QS_ERR_OVERFLOW && S1.broken);
    }
    /* the denominator exponent is capped at QS_MAX_K (4096) */
    memset(AMPS, 0, sizeof AMPS);
    AMPS[0].c[0] = 1;
    CHECK(qs_load(&S1, 1, 8, 4095, AMPS) == QS_OK && S1.k == 4095);
    CHECK(G(&S1, QS_G_X, 0, 0, 0) == QS_OK && G(&S1, QS_G_H, 0, 0, 0) == QS_OK && S1.k == 4096);
    CHECK(G(&S1, QS_G_H, 0, 0, 0) == QS_ERR_OVERFLOW && S1.broken);
    CHECK(qs_load(&S1, 1, 8, 4096, AMPS) == QS_OK && S1.k == 4096);
    CHECK(G(&S1, QS_G_H, 0, 0, 0) == QS_ERR_OVERFLOW);
    /* forced: probability numerator overflows but the state is still fine */
    memset(AMPS, 0, sizeof AMPS);
    AMPS[0].c[0] = (int64_t) 1 << 40;
    CHECK(qs_load(&S1, 1, 8, 0, AMPS) == QS_OK);
    {
        qs_prob_t p;
        qs_elem_t nrm;
        uint32_t k;
        CHECK(qs_prob_basis(&S1, 0, &p) == QS_ERR_OVERFLOW);
        CHECK(qs_norm_sq(&S1, &nrm, &k) == QS_ERR_OVERFLOW);
        CHECK(qs_norm_sq(&S1, NULL, &k) == QS_ERR_NULL);
        CHECK(qs_check_unit(&S1) == QS_ERR_OVERFLOW);
        CHECK(S1.broken == 0 && G(&S1, QS_G_X, 0, 0, 0) == QS_OK);
    }
    memset(AMPS, 0, sizeof AMPS);
    /* deep circuit: (H T)^m on one qubit until something overflows */
    for (uint32_t N = 8; N <= 16; N += 8) {
        CHECK(qs_init(&S1, 1, N) == QS_OK);
        uint32_t m = 0, prob_fail_k = 0, prob_fail_m = 0, last_k = 0;
        int rc = QS_OK;
        int unit_ok = 1;
        while (m < 2000) {
            rc = G(&S1, QS_G_H, 0, 0, 0);
            if (rc == QS_OK) rc = G(&S1, QS_G_T, 0, 0, 0);
            if (rc != QS_OK) break;
            m++;
            last_k = S1.k;
            if (!prob_fail_m) {
                int u = qs_check_unit(&S1);
                if (u != QS_OK) {
                    prob_fail_m = m;
                    prob_fail_k = S1.k;
                    if (u != QS_ERR_OVERFLOW) unit_ok = 0;
                }
            }
        }
        CHECK(rc == QS_ERR_OVERFLOW && S1.broken);
        CHECK(unit_ok);
        CHECK(prob_fail_m > 0 && prob_fail_m < m);
        CHECK(G(&S1, QS_G_H, 0, 0, 0) == QS_ERR_BROKEN);
        printf("deep (H T)^m, N=%u: probability queries overflow first at m=%u (k=%u); "
               "gates overflow at m=%u (last good k=%u)\n",
               N, prob_fail_m, prob_fail_k, m + 1u, last_k);
    }
}

int main(void)
{
    test_init_and_errors();
    test_bell_ghz();
    test_qft();
    test_identities();
    test_toffoli();
    test_real_sign();
    test_measure();
    test_random();
    test_overflow();
    printf("%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
