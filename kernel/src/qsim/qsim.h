/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* qsim.h — an EXACT quantum circuit simulator that runs on a classical CPU
 * (roadmap 6.6d, step Q.1; its checks are step Q.2, in test_qsim.c).
 *
 * THIS IS A CLASSICAL SIMULATOR. It is not a quantum computer, it talks to
 * no quantum hardware, and it gives no quantum speedup: it stores all 2^n
 * amplitudes of an n-qubit state and updates them one gate at a time.
 *
 * WHAT IT DOES
 *   Every amplitude is an element of the cyclotomic ring Z[z], z = e^(2*pi*i/N),
 *   with N = 8 or N = 16 chosen at qs_init. An element is N/2 int64
 *   coefficients in the basis 1, z, ..., z^(N/2-1) (z^(N/2) = -1). The whole
 *   state shares ONE denominator sqrt(2)^k, so amplitude x is
 *       a_x = elem_x / sqrt(2)^k.
 *   N = 8 is exactly the Clifford+T gate set. N = 16 adds the pi/8 phase, so
 *   controlled phases of angle 2*pi/16 (the 4-qubit QFT) are exact too.
 *   After every H gate (the only gate that raises k) and after qs_load and
 *   qs_measure, the simulator divides every amplitude by sqrt(2) and lowers
 *   k as long as that stays inside the ring (sqrt(2) = z8 - z8^3, and x is
 *   divisible by sqrt(2) iff x*sqrt(2) has only even coefficients). Phases
 *   and permutations cannot create a common sqrt(2) factor. So k is always
 *   the smallest possible and two equal states have identical
 *   representations: qs_state_equal is an exact equality test.
 *
 *   Gates: I X Y Z H S Sdg T Tdg, PHASE(k/m) = diag(1, e^(2*pi*i*k/m)) with m
 *   a power of two dividing N, CNOT, CZ, SWAP, CCX (Toffoli) and controlled
 *   PHASE(k/m). A circuit is a fixed array of gate records (qs_circuit_t);
 *   qs_circuit_adjoint builds the exact inverse circuit.
 *
 *   Exact outputs: an amplitude (element + k); the probability of a basis
 *   state or of one qubit being 1, as a fraction num/den of two REAL ring
 *   elements over the same 2^k (den = the state's norm squared: the
 *   integer 2^k, i.e. exactly 1, unless a measurement has collapsed the
 *   state without renormalising); an exact sign test for
 *   real elements and an exact comparison of a probability with a rational
 *   p/q (multi-precision integers inside, no rounding); qs_check_unit, which
 *   proves the total probability is exactly 1.
 *
 *   Measurement: qs_measure collapses one qubit to a CALLER-CHOSEN outcome
 *   without renormalising (the norm squared is kept, and every probability
 *   is reported relative to it, exactly). qs_renormalise rescales exactly
 *   when the norm squared is a power of two, and refuses otherwise.
 *   Sampling: qs_sample / qs_measure_sample take a 32-bit number u from the
 *   caller's seeded PRNG and return the outcome of the inverse cumulative
 *   distribution at the dyadic rational u/2^32. Every comparison is exact,
 *   so the outcome for a given u is exactly defined and reproducible; over
 *   uniform u, each outcome's frequency is within 2^-32 of its exact
 *   probability (a finite random number cannot do better for irrational
 *   probabilities such as (2+sqrt2)/4).
 *
 * HONEST LIMITS
 *   - Exponential cost: 2^n amplitudes of N/2 int64 each. QS_MAX_QUBITS is
 *     10 (a 64 KiB state). Time per gate is O(2^n * N).
 *   - Coefficient growth: every Galois conjugate of the state is a unit
 *     vector, so each coefficient is at most sqrt(2)^k in size. k grows with
 *     the number of non-Clifford (T-type) gates; int64 coefficients overflow
 *     past k = 126, and probability numerators (size up to 2^k) past k = 62.
 *     Measured (test_qsim): a single-qubit (H T)^m sequence reaches k = 63 at
 *     m = 124, where probability queries start returning QS_ERR_OVERFLOW,
 *     and a gate overflows at m = 252. Random multi-qubit Clifford+T
 *     circuits of up to 160 gates stayed at k <= 14. Overflow is DETECTED: the call returns
 * QS_ERR_OVERFLOW and a gate that overflowed marks the state broken (every later call returns
 * QS_ERR_BROKEN); nothing ever wraps.
 *   - Only gates whose entries lie in Z[z]/sqrt(2)^k are offered. Arbitrary
 *     rotation angles are NOT representable; they would need approximate
 *     synthesis into Clifford+T, which is not done here.
 *   - No noise model, no decoherence, no hardware: ideal unitary evolution.
 *   - The probability comparison uses fixed 640-bit integers; inputs are
 *     int64, so its own overflow check is defensive and is not reachable.
 *   - Freestanding C11: integer only, no allocation, no libc. The state and
 *     circuits are the caller's fixed-size structs.
 */
#ifndef QSIM_H
#define QSIM_H

#include <stdint.h>

#define QS_MAX_QUBITS 10u
#define QS_MAX_AMPS   (1u << QS_MAX_QUBITS)
#define QS_MAX_D      8u /* N/2 for the largest N (16) */
#define QS_MAX_GATES  1024u

typedef enum {
    QS_OK = 0,
    QS_ERR_NULL = -1,       /* a NULL pointer */
    QS_ERR_ARG = -2,        /* bad N, gate kind, denominator or other argument */
    QS_ERR_QUBIT = -3,      /* qubit index out of range, or repeated in one gate */
    QS_ERR_TOO_MANY = -4,   /* zero qubits or more than QS_MAX_QUBITS */
    QS_ERR_PHASE = -5,      /* the phase order m does not divide this state's N */
    QS_ERR_OVERFLOW = -6,   /* an int64 or multi-precision bound was reached */
    QS_ERR_BROKEN = -7,     /* an earlier gate overflowed; the state is unusable */
    QS_ERR_ZERO_PROB = -8,  /* measurement outcome has probability exactly 0 */
    QS_ERR_NOT_REAL = -9,   /* an element given as real is not real */
    QS_ERR_NOT_EXACT = -10, /* exact renormalisation is impossible */
    QS_ERR_FULL = -11,      /* the circuit has QS_MAX_GATES gates already */
    QS_ERR_NORM = -12,      /* total probability is not exactly 1 */
    QS_ERR_INDEX = -13,     /* basis-state index out of range */
    QS_ERR_MISMATCH = -14   /* two states differ in qubit count or N */
} qs_status_t;

/* One ring element: c[0] + c[1] z + ... + c[D-1] z^(D-1), D = N/2.
 * Coefficients at index >= D are always 0. */
typedef struct {
    int64_t c[QS_MAX_D];
} qs_elem_t;

/* A probability, exactly: num / den. Both are REAL ring elements over the
 * same denominator 2^k (which cancels); den is the state's norm squared, so
 * den == 2^k (an integer) for a normalised state. */
typedef struct {
    qs_elem_t num;
    qs_elem_t den;
} qs_prob_t;

typedef struct {
    uint32_t n;                 /* qubits */
    uint32_t N;                 /* 8 or 16 */
    uint32_t D;                 /* N/2 */
    uint32_t k;                 /* shared denominator sqrt(2)^k */
    uint32_t broken;            /* nonzero after an overflow inside a gate */
    uint32_t collapsed;         /* number of measurements applied (informational) */
    qs_elem_t amp[QS_MAX_AMPS]; /* basis index bit q = qubit q (qubit 0 = LSB) */
} qs_state_t;

typedef enum {
    QS_G_I = 0,
    QS_G_X,
    QS_G_Y,
    QS_G_Z,
    QS_G_H,
    QS_G_S,
    QS_G_SDG,
    QS_G_T,
    QS_G_TDG,
    QS_G_PHASE,  /* diag(1, e^(2 pi i k/m)) on t */
    QS_G_CNOT,   /* control c0, target t */
    QS_G_CZ,     /* control c0, target t (symmetric) */
    QS_G_SWAP,   /* swaps c0 and t */
    QS_G_CCX,    /* controls c0 and c1, target t */
    QS_G_CPHASE, /* control c0: diag(1,1,1,e^(2 pi i k/m)) */
    QS_G_COUNT
} qs_gate_kind_t;

/* One gate. c0/c1 are used only by kinds that need them; k/m only by the
 * phase kinds (m in {1,2,4,8,16}, k any int32, taken modulo m). */
typedef struct {
    uint8_t kind;
    uint8_t t;
    uint8_t c0;
    uint8_t c1;
    int32_t k;
    uint32_t m;
} qs_gate_t;

typedef struct {
    uint32_t count;
    qs_gate_t g[QS_MAX_GATES];
} qs_circuit_t;

/* ---- state ---- */
int qs_init(qs_state_t *st, uint32_t n, uint32_t N);                     /* |0...0> */
int qs_init_basis(qs_state_t *st, uint32_t n, uint32_t N, uint32_t idx); /* |idx> */
/* Load arbitrary amplitudes amps[0 .. 2^n-1] over sqrt(2)^k (not checked for
 * unit norm; use qs_check_unit). Reduces k to its minimum. */
int qs_load(qs_state_t *st, uint32_t n, uint32_t N, uint32_t k, const qs_elem_t *amps);
int qs_amplitude(const qs_state_t *st, uint32_t idx, qs_elem_t *out, uint32_t *k);
int qs_state_equal(const qs_state_t *a, const qs_state_t *b, int *equal);

/* ---- gates and circuits ---- */
int qs_apply(qs_state_t *st, const qs_gate_t *g);
int qs_gate(qs_state_t *st, uint32_t kind, uint32_t t, uint32_t c0, uint32_t c1, int32_t k,
            uint32_t m);
int qs_circuit_init(qs_circuit_t *c);
int qs_circuit_push(qs_circuit_t *c, uint32_t kind, uint32_t t, uint32_t c0, uint32_t c1, int32_t k,
                    uint32_t m);
/* Runs every gate; stops at the first error and returns it (*done, if not
 * NULL, receives how many gates were applied). */
int qs_run_circuit(qs_state_t *st, const qs_circuit_t *c, uint32_t *done);
/* out = adjoint (exact inverse) of in: gates reversed, each inverted.
 * in and out must be different objects. */
int qs_circuit_adjoint(const qs_circuit_t *in, qs_circuit_t *out);

/* ---- exact probabilities ---- */
int qs_norm_sq(const qs_state_t *st, qs_elem_t *num, uint32_t *k); /* sum |a|^2 = num/2^k */
int qs_check_unit(const qs_state_t *st); /* QS_OK iff sum |a|^2 == 1 exactly */
int qs_prob_basis(const qs_state_t *st, uint32_t idx, qs_prob_t *p);
int qs_prob_qubit(const qs_state_t *st, uint32_t q, qs_prob_t *p); /* P(qubit q = 1) */
/* Sign (-1, 0, +1) of a real element of Z[e^(2 pi i/N)]. */
int qs_real_sign(uint32_t N, const qs_elem_t *x, int *sign);
/* Coordinates of a real element: N=8 -> x = r[0] + r[1]*sqrt2 (r[2]=r[3]=0);
 * N=16 -> x = r[0] + r[1]*2cos(pi/8) + r[2]*sqrt2 + r[3]*2cos(3pi/8). */
int qs_real_coords(uint32_t N, const qs_elem_t *x, int64_t r[4]);
/* *cmp = sign(num/den - p/q) for den > 0, q > 0, exactly. */
int qs_prob_cmp(uint32_t N, const qs_prob_t *pr, int64_t p, int64_t q, int *cmp);

/* ---- measurement ---- */
/* Collapse qubit q to the caller's outcome (0/1). No renormalisation. */
int qs_measure(qs_state_t *st, uint32_t q, uint32_t outcome);
/* Exact rescale to norm 1 when the norm squared is a power of two. */
int qs_renormalise(qs_state_t *st);
/* Outcome = smallest basis index whose cumulative probability > u/2^32. */
int qs_sample(const qs_state_t *st, uint32_t u, uint32_t *idx);
/* bit = 0 iff u/2^32 < P(qubit q = 0); then collapses to that bit. */
int qs_measure_sample(qs_state_t *st, uint32_t q, uint32_t u, uint32_t *bit);

#endif /* QSIM_H */
