/* test_surplus_axioms.c — ISF (Interaction Surplus Framework) axiom tests
 *
 * Guards the fixed-point path actually used by the kernel against the
 * class of defect found on 2026-08-04: sr_div_impl overflowed for any
 * remainder >= 2^31, so SR_DIV(1,3)/(1,2)/(2,3) all returned 0, sr_ln()
 * was wrong for mantissas >= 1.5, and the ISF normalization axiom
 * S4 (f(1,N) = ln N) was violated in the kernel (f(1,12) = 1.674 vs
 * ln 12 = 2.485) even though the host double path looked fine.
 *
 * These assert the framework's own theorems (Paper A, Thm 2.1 and its
 * corollaries) directly on the fixed-point implementation:
 *   S2  f(0) = 0
 *   S4  f(1) = ln N                      <- the axiom that was broken
 *   monotonicity   f'(u) > 0
 *   concavity      f(u) is concave
 *   Lipschitz      f'(0) = N-1 (sharp constant)
 *   g(u) = 1 + (N-1)u  (effective count is affine; g(0)=1, g(1)=N)
 *
 *   gcc -std=c11 -Wall -Isrc/surplus src/surplus/test_surplus_axioms.c \
 *       src/surplus/surplus.c -o /tmp/test_surplus_axioms -lm
 */
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include "surplus.h"

#define D(x) ((double)(x) / (double)SR_ONE)

static int failures = 0;
static void check(int cond, const char *msg) {
    if (cond) { printf("[PASS] %s\n", msg); }
    else      { printf("[FAIL] %s\n", msg); failures++; }
}
static int approx(double a, double b, double tol) {
    double d = a - b; if (d < 0) d = -d; return d <= tol;
}

int main(void) {
    printf("=== ISF axiom tests (fixed-point path) ===\n");

    /* --- fixed-point primitives: the exact cases that were broken --- */
    check(approx(D(SR_DIV(SR_ONE, SR_FROM_INT(3))), 1.0/3.0, 1e-6),
          "SR_DIV(1,3) == 1/3 (overflow regression)");
    check(approx(D(SR_DIV(SR_ONE, SR_FROM_INT(2))), 0.5, 1e-6),
          "SR_DIV(1,2) == 1/2 (overflow regression)");
    check(approx(D(SR_DIV(SR_FROM_INT(2), SR_FROM_INT(3))), 2.0/3.0, 1e-6),
          "SR_DIV(2,3) == 2/3 (overflow regression)");
    check(approx(D(SR_MUL(SR_FROM_FLOAT(1.5), SR_FROM_FLOAT(3.0))), 4.5, 1e-6),
          "SR_MUL(1.5,3) == 4.5");

    /* --- natural log across mantissa ranges (>=1.5 was broken) --- */
    check(approx(D(sr_ln(SR_FROM_INT(2))),  log(2.0),  1e-4), "ln 2");
    check(approx(D(sr_ln(SR_FROM_INT(7))),  log(7.0),  1e-4), "ln 7");
    check(approx(D(sr_ln(SR_FROM_INT(12))), log(12.0), 1e-4), "ln 12");
    check(approx(D(sr_ln(SR_FROM_FLOAT(1.75))), log(1.75), 1e-4), "ln 1.75");

    /* --- S2: f(0) = 0 (parallel vectors carry no surplus) --- */
    for (uint32_t N = 2; N <= 12; N++) {
        if (!approx(D(surplus_f(SR_ZERO, N)), 0.0, 1e-6)) {
            printf("  N=%u f(0)=%.6f\n", N, D(surplus_f(SR_ZERO, N)));
            check(0, "S2: f(0) = 0 for all N"); goto s4;
        }
    }
    check(1, "S2: f(0) = 0 for all N in [2,12]");

s4:
    /* --- S4: f(1) = ln N (orthogonal vectors reach maximal surplus) --- */
    {
        int ok = 1;
        for (uint32_t N = 2; N <= 12; N++) {
            double got = D(surplus_f(SR_ONE, N)), want = log((double)N);
            if (!approx(got, want, 1e-4)) {
                printf("  N=%u f(1)=%.6f want ln N=%.6f\n", N, got, want);
                ok = 0;
            }
        }
        check(ok, "S4 NORMALIZATION: f(1,N) = ln N for all N in [2,12]");
    }

    /* --- g(u) = 1 + (N-1)u : effective count is affine --- */
    {
        int ok = 1;
        const uint32_t N = 8;
        for (double u = 0.0; u <= 1.0001; u += 0.125) {
            double got = exp(D(surplus_f(SR_FROM_FLOAT(u), N)));
            double want = 1.0 + (double)(N - 1) * u;
            if (!approx(got, want, 1e-3)) { ok = 0;
                printf("  u=%.3f g=%.6f want %.6f\n", u, got, want); }
        }
        check(ok, "g(u) = e^f(u) = 1 + (N-1)u (affine effective count)");
    }

    /* --- monotone increasing and concave in u --- */
    {
        const uint32_t N = 10;
        int mono = 1, conc = 1;
        double prev = -1.0, prev_slope = 1e18;
        for (double u = 0.0; u <= 1.0001; u += 0.0625) {
            double v = D(surplus_f(SR_FROM_FLOAT(u), N));
            if (v < prev - 1e-9) mono = 0;
            if (prev >= 0.0) {
                double slope = v - prev;
                if (slope > prev_slope + 1e-6) conc = 0;
                prev_slope = slope;
            }
            prev = v;
        }
        check(mono, "f(u) strictly increasing in u");
        check(conc, "f(u) concave in u (diminishing returns)");
    }

    /* --- Lipschitz: f'(0) = N-1 is the sharp constant --- */
    {
        int ok = 1;
        for (uint32_t N = 2; N <= 12; N += 2) {
            double d = D(surplus_deriv(SR_ZERO, N));
            if (!approx(d, (double)(N - 1), 1e-3)) { ok = 0;
                printf("  N=%u f'(0)=%.6f want %u\n", N, d, N - 1); }
        }
        check(ok, "f'(0) = N-1 (sharp Lipschitz constant)");
    }

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS",
           failures);
    return failures ? 1 : 0;
}
