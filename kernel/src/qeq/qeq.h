/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* qeq.h — an EXACT equivalence checker for small quantum circuits, built on
 * the exact classical simulator qsim (kernel/src/qsim).
 *
 * THIS IS A CLASSICAL TOOL. It runs on an ordinary CPU, talks to no quantum
 * hardware and gives no quantum speedup. Its cost is exponential in the
 * number of qubits: it simulates every one of the 2^n basis columns.
 *
 * WHAT IT DOES
 *   Given two circuits C1 and C2 on the same n qubits (n <= QS_MAX_QUBITS),
 *   it decides whether they implement the same unitary, exactly. Every
 *   amplitude is an element of Z[z]/sqrt(2)^k (z = e^(2 pi i/N), N = 8 or 16)
 *   and qsim keeps k minimal, so comparing two columns is an integer
 *   comparison: there is no tolerance and no "close enough".
 *
 *   Two modes:
 *     QEQ_STRICT       U1 == U2 entry by entry.
 *     QEQ_UP_TO_PHASE  U1 == z^j U2 for one j, the same for every column. j is
 *                      read from the first nonzero entry of column 0. For this
 *                      gate set a global phase between two equivalent circuits
 *                      is always a power of z (see docs/CIRCUIT_EQUIVALENCE.md),
 *                      so "no such j" proves non-equivalence.
 *   Two methods:
 *     QEQ_DIRECT  runs C1 and C2 on every |x> and compares the two columns.
 *     QEQ_MITER   runs C1 then adjoint(C2) on every |x> (one state instead of
 *                 two) and checks that the result is z^j |x>.
 *
 *   On a mismatch it returns a counterexample: the input basis index, an
 *   output index and both exact amplitudes there (DIRECT: U1[out,in] and
 *   U2[out,in]; MITER: (C2^dag C1)[out,in] and the expected value).
 *
 *   A strict, small OpenQASM 2.0 subset parser (and the matching writer) turns
 *   text into a qs_circuit_t. Angles are accepted only when they are exact
 *   multiples of pi/8 written as e.g. pi/4, -pi/2, 3*pi/8 or 0. Anything else
 *   is refused with its line number; nothing is ever approximated.
 *
 * VERDICT (four-valued, same numbers as lpres_state_t in
 * kernel/src/lpres/lpres.h, defined here so this module stays freestanding):
 *   QEQ_TRUE    equivalent (in the requested mode), proven for every column.
 *   QEQ_FALSE   not equivalent; a counterexample is filled in.
 *   QEQ_NEITHER could not decide: a gate not representable, too many qubits,
 *               an overflow inside the simulator, or the work budget ran out.
 *               `why` says which. The checker never guesses.
 *   QEQ_BOTH    NOT USED. "Equivalent only up to a global phase" is reported
 *               as TRUE in QEQ_UP_TO_PHASE mode with phase_exp != 0 and the
 *               flag global_phase_only set.
 *
 * HONEST LIMITS
 *   - Exponential: 2^n columns, each a full simulation of both circuits
 *     (O(2^n * gates * 2^n * N) word operations in total). For n = 10 and
 *     hundreds of gates this takes seconds; n > 10 is refused (NEITHER).
 *   - Only Clifford+T plus pi/8 phases (the qsim gate set). Circuits with
 *     arbitrary rotation angles are not representable and give NEITHER (or a
 *     parser refusal); approximate synthesis is not done here.
 *   - Coefficients grow with the number of T-type gates; when qsim's int64
 *     coefficients would overflow the verdict is NEITHER, never a wrong
 *     answer.
 *   - No ancilla handling, no qubit permutation search, no measurement or
 *     classical control: unitary circuits on the same qubit order only.
 *   - Freestanding C11: integer only, no allocation, no libc. The caller owns
 *     the large work area (qeq_work_t, about 140 KiB).
 */
#ifndef QEQ_H
#define QEQ_H

#include <stdint.h>

#include "../qsim/qsim.h"

/* Same numeric values as lpres_state_t (NEITHER=0, TRUE=1, FALSE=2, BOTH=3). */
typedef enum { QEQ_NEITHER = 0, QEQ_TRUE = 1, QEQ_FALSE = 2, QEQ_BOTH = 3 } qeq_verdict_t;

typedef enum {
    QEQ_OK = 0,
    QEQ_ERR_NULL = -1, /* a NULL pointer */
    QEQ_ERR_ARG = -2,  /* bad option (mode, method, N) or buffer argument */
    QEQ_ERR_FULL = -3  /* output buffer too small (writer) */
} qeq_status_t;

/* Why a verdict is NEITHER. */
typedef enum {
    QEQ_WHY_NONE = 0,
    QEQ_WHY_QUBITS = 1,   /* n == 0 or n > QS_MAX_QUBITS */
    QEQ_WHY_GATE = 2,     /* a gate is malformed or not representable at N */
    QEQ_WHY_OVERFLOW = 3, /* qsim (or a comparison here) reached an int64 bound */
    QEQ_WHY_BUDGET = 4    /* max_gate_apps would be exceeded */
} qeq_why_t;

typedef enum { QEQ_STRICT = 0, QEQ_UP_TO_PHASE = 1 } qeq_mode_t;
typedef enum { QEQ_DIRECT = 0, QEQ_MITER = 1 } qeq_method_t;

typedef struct {
    uint32_t mode;          /* qeq_mode_t */
    uint32_t method;        /* qeq_method_t */
    uint32_t N;             /* 0 = smallest that fits the gates, else 8 or 16 */
    uint64_t max_gate_apps; /* 0 = unlimited; else a hard cap on gate applications */
} qeq_opts_t;

typedef struct {
    uint32_t verdict;           /* qeq_verdict_t */
    uint32_t why;               /* qeq_why_t, set when verdict == QEQ_NEITHER */
    int32_t qs_rc;              /* the qsim status behind a NEITHER, if any */
    uint32_t bad_circuit;       /* QEQ_WHY_GATE: 1 or 2 */
    uint32_t bad_gate;          /* QEQ_WHY_GATE: gate index in that circuit */
    uint32_t N;                 /* ring used: z = e^(2 pi i/N) */
    uint32_t phase_known;       /* 1 when phase_exp is valid */
    uint32_t phase_exp;         /* U1 = z^phase_exp * U2 (0 in strict mode) */
    uint32_t global_phase_only; /* TRUE, but only up to a global phase != 1 */
    uint32_t columns_done;      /* columns fully compared */
    uint64_t gate_apps;         /* gate applications spent */
    /* counterexample, valid when verdict == QEQ_FALSE */
    uint32_t cex_in;  /* input basis index (column) */
    uint32_t cex_out; /* output basis index (row) */
    qs_elem_t cex_a;  /* DIRECT: U1[out,in]; MITER: (C2^dag C1)[out,in] */
    uint32_t cex_ka;  /* ... over sqrt(2)^cex_ka */
    qs_elem_t cex_b;  /* DIRECT: U2[out,in]; MITER: expected z^j or 0 */
    uint32_t cex_kb;
} qeq_result_t;

/* Caller-owned scratch (no allocation inside). */
typedef struct {
    qs_state_t s1;
    qs_state_t s2;
    qs_circuit_t adj;
} qeq_work_t;

int qeq_opts_default(qeq_opts_t *o); /* STRICT, DIRECT, N auto, unlimited */

/* Smallest N (8 or 16) that represents every phase gate of c on n qubits.
 * Returns QS-style status QEQ_OK with *N set, or QEQ_ERR_ARG with *bad_gate
 * set to the first malformed gate (index == c->count if the count itself is
 * bad). Either output pointer may be NULL. */
int qeq_circuit_N(const qs_circuit_t *c, uint32_t n, uint32_t *N, uint32_t *bad_gate);

/* Decide whether c1 and c2 (both on n qubits) are equivalent. Returns
 * QEQ_OK and fills *r (verdict, reason, counterexample) for every input
 * circuit; returns a negative status only for NULL pointers or bad options.
 * o may be NULL (defaults). */
int qeq_check(const qs_circuit_t *c1, const qs_circuit_t *c2, uint32_t n, const qeq_opts_t *o,
              qeq_work_t *w, qeq_result_t *r);

/* ---- OpenQASM 2.0 subset ---- */

typedef enum {
    QEQ_Q_OK = 0,
    QEQ_Q_SYNTAX = 1,       /* malformed statement, unexpected token or character */
    QEQ_Q_HEADER = 2,       /* missing or wrong "OPENQASM 2.0;" */
    QEQ_Q_UNKNOWN_GATE = 3, /* gate name outside the subset */
    QEQ_Q_ANGLE = 4,        /* angle is not an exact multiple of pi/8 in the accepted form */
    QEQ_Q_INDEX = 5,        /* register index out of range */
    QEQ_Q_REG = 6,          /* unknown register, no qreg yet, or a second qreg */
    QEQ_Q_ARITY = 7,        /* wrong number of qubit arguments or missing angle */
    QEQ_Q_DUP = 8,          /* the same qubit twice in one gate */
    QEQ_Q_UNSUPPORTED = 9,  /* measure, reset, if, gate/opaque definitions, U, CX, broadcast */
    QEQ_Q_FULL = 10,        /* more than QS_MAX_GATES gates */
    QEQ_Q_SIZE = 11         /* qreg size 0 or above QS_MAX_QUBITS */
} qeq_qasm_code_t;

typedef struct {
    uint32_t code; /* qeq_qasm_code_t */
    uint32_t line; /* 1-based line of the offending token */
    uint32_t col;  /* 1-based column */
} qeq_qasm_err_t;

/* Parse len bytes of src. Accepted:
 *   OPENQASM 2.0;   include "qelib1.inc";   one qreg NAME[n];   creg (ignored)
 *   barrier (ignored)   // comments
 *   id h x y z s sdg t tdg q[i];   cx cz swap q[i],q[j];   ccx q[i],q[j],q[k];
 *   u1(a) p(a) q[i];   cu1(a) cp(a) q[i],q[j];
 *   a := 0 | [+-][INT*]pi[/INT], an exact multiple of pi/8.
 * Returns QEQ_OK (err->code == QEQ_Q_OK), or QEQ_ERR_ARG with err filled. */
int qeq_qasm_parse(const char *src, uint32_t len, qs_circuit_t *out, uint32_t *n,
                   qeq_qasm_err_t *err);

/* Write c (n qubits, register "q") as OpenQASM accepted by qeq_qasm_parse.
 * buf receives a NUL-terminated text; *len its length without the NUL. */
int qeq_qasm_write(const qs_circuit_t *c, uint32_t n, char *buf, uint32_t cap, uint32_t *len);

#endif /* QEQ_H */
