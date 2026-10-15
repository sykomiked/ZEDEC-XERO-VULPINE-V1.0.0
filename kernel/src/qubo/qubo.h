/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* qubo.h — classical QUBO / Ising workbench with optimality certificates.
 *
 * WHAT IT DOES
 * ------------
 * People write optimisation problems as QUBOs (minimise E(x) = offset +
 * sum_{i<=j} Q[i][j] x_i x_j over binary x) or as Ising models (spins
 * s_i in {-1,+1}) so they can send them to quantum annealers. This module
 * solves the same files on an ordinary CPU and is honest about how good
 * the answer is. Every solver returns a qubo_result_t with:
 *   - the best assignment x found and its exact integer energy,
 *   - a PROVEN lower bound on the true minimum,
 *   - the gap (energy - bound) and a status:
 *       QUBO_CERT_OPTIMAL        gap == 0, the answer is proven optimal;
 *       QUBO_CERT_GAP            branch and bound ran out of budget; the
 *                                optimum lies in [bound, energy];
 *       QUBO_CERT_HEURISTIC_ONLY a heuristic answer; the bound is only the
 *                                cheap root bound.
 *
 * Solvers (all deterministic given the caller's seed):
 *   qubo_brute_force   exact Gray-code enumeration, n <= QUBO_BRUTE_MAX_VARS,
 *                      refused up front if 2^n - 1 steps exceed the budget.
 *   qubo_bnb           exact depth-first branch and bound with first-order
 *                      persistency fixing and a row lower bound; returns a
 *                      proven optimum or, at the node budget, best x plus a
 *                      proven bound (an optimality-gap certificate).
 *   qubo_anneal        replica-exchange (parallel tempering) Metropolis.
 *   qubo_local_search  steepest descent with random restarts (the plain
 *                      classical baseline every claim is measured against).
 *
 * Four-valued input validation (paraconsistent hook). The text parser tags
 * its input with a state numerically identical to lpres_state_t in
 * kernel/src/lpres/lpres.h (NEITHER=0, TRUE=1, FALSE=2, BOTH=3; redefined
 * here so this module does not drag in other headers):
 *   TRUE    well formed, consistent;
 *   FALSE   malformed (syntax, index out of range, overflow);
 *   BOTH    the same coefficient given twice with different values: the
 *           input contradicts itself, so it is REFUSED unless the caller
 *           explicitly picks the QUBO_MERGE_SUM rule;
 *   NEITHER no size line: there is not enough information, so it is
 *           refused rather than guessed.
 *
 * HONEST LIMITS
 *   - This is classical software. Nothing here is quantum and no speedup
 *     over anything is claimed; the benchmark (bench_qubo.c and
 *     docs/QUBO_WORKBENCH.md) compares only classical methods.
 *   - QUBO is NP-hard. Exact solvers are exponential in the worst case; the
 *     branch-and-bound bound is simple (not roof duality, not an SDP), so on
 *     dense instances beyond ~30-40 variables expect a GAP certificate, not
 *     a proof.
 *   - At most QUBO_MAX_VARS = 64 variables, int64 coefficients, and the sum
 *     of |coefficients| + |offset| must be <= QUBO_MAX_ABS_SUM (2^60). With
 *     that, no energy, field or difference computed internally can
 *     overflow; anything larger is refused with QUBO_ERR_OVERFLOW.
 *   - QUBO -> Ising is exact only after scaling by 4 (x = (s+1)/2 brings
 *     quarters): qubo_to_ising returns a model whose energy is exactly
 *     4 * E(x). Ising -> QUBO is exact with no scaling.
 *   - The annealer's replica-swap test uses the nominal 3/4 temperature
 *     ratio of the ladder rather than the floored integer temperatures; it
 *     is a heuristic either way and its answers are checked, not trusted.
 *   - Freestanding: integer only, no allocation, no libc; big state lives in
 *     a caller-owned qubo_ws_t (~75 KB, give it static storage).
 */
#ifndef ZXV_QUBO_H
#define ZXV_QUBO_H

#include <stdint.h>

#define QUBO_MAX_VARS       64u
#define QUBO_BRUTE_MAX_VARS 32u
#define QUBO_MAX_REPLICAS   16u
#define QUBO_EXP_TABLE_N    256u /* exp(-x) for x = i/16, i < 256, Q24 */
#define QUBO_MAX_ABS_SUM    (1ll << 60)
#define QUBO_MAX_TEMP       (1ll << 54)  /* in Q8 (1/256 energy units) */
#define QUBO_MAX_MOVES      (1ull << 40) /* anneal: sweeps*replicas*n cap */

typedef enum {
    QUBO_OK = 0,
    QUBO_ERR_NULL = -1,     /* a required pointer is NULL */
    QUBO_ERR_RANGE = -2,    /* index, size or assignment out of range */
    QUBO_ERR_OVERFLOW = -3, /* coefficients too large (see QUBO_MAX_ABS_SUM) */
    QUBO_ERR_PARAM = -4,    /* bad solver parameter or zero rng state */
    QUBO_ERR_BUDGET = -5,   /* exact enumeration would exceed the budget */
    QUBO_ERR_SYNTAX = -6,   /* parser: malformed line (tag FALSE) */
    QUBO_ERR_NO_SIZE = -7,  /* parser: no size line (tag NEITHER) */
    QUBO_ERR_CONFLICT = -8, /* parser: contradictory duplicate (tag BOTH) */
    QUBO_ERR_INVALID = -9   /* qubo_verify: result is not self-consistent */
} qubo_status_t;

/* Same numeric values as lpres_state_t (kernel/src/lpres/lpres.h). */
typedef enum {
    QUBO_LP_NEITHER = 0,
    QUBO_LP_TRUE = 1,
    QUBO_LP_FALSE = 2,
    QUBO_LP_BOTH = 3
} qubo_lp_t;

typedef enum { QUBO_CERT_OPTIMAL = 0, QUBO_CERT_GAP = 1, QUBO_CERT_HEURISTIC_ONLY = 2 } qubo_cert_t;

typedef enum {
    QUBO_MERGE_REFUSE = 0, /* identical duplicates kept once; conflicts refused */
    QUBO_MERGE_SUM = 1     /* every duplicate is added (the usual QUBO-file rule) */
} qubo_merge_t;

/* E(x) = offset + sum_{i<=j<n} q[i][j] * x_i * x_j, x bit i = x_i.
 * Only the upper triangle (i <= j) may be non-zero. */
typedef struct {
    uint32_t n;
    int64_t offset;
    int64_t q[QUBO_MAX_VARS][QUBO_MAX_VARS];
} qubo_t;

/* H(s) = offset + sum_i h[i] s_i + sum_{i<j} j[i][j] s_i s_j, s_i in {-1,+1};
 * spin bit i set means s_i = +1. Only j[i][k] with i < k may be non-zero. */
typedef struct {
    uint32_t n;
    int64_t offset;
    int64_t h[QUBO_MAX_VARS];
    int64_t j[QUBO_MAX_VARS][QUBO_MAX_VARS];
} qubo_ising_t;

typedef struct {
    uint64_t x;          /* best assignment found */
    int64_t energy;      /* E(x), exact */
    int64_t lower_bound; /* proven: every assignment has E >= lower_bound */
    int64_t gap;         /* energy - lower_bound, >= 0 */
    qubo_cert_t cert;
    uint64_t work; /* steps / nodes / moves / flips actually used */
} qubo_result_t;

typedef struct {
    qubo_lp_t tag;
    uint32_t line; /* 1-based line of the first problem, 0 if none */
    uint32_t entries;
    uint32_t duplicates; /* agreeing (REFUSE) or summed (SUM) repeats */
} qubo_parse_report_t;

typedef struct {
    uint32_t replicas; /* 1..QUBO_MAX_REPLICAS */
    uint32_t sweeps;   /* >= 1 */
    int64_t t_max_q8;  /* hottest temperature x 256 (1/256 energy units), 0 = auto */
} qubo_anneal_params_t;

typedef struct {
    uint32_t restarts;  /* >= 1 */
    uint64_t max_flips; /* >= 1, total improving flips over all restarts */
} qubo_ls_params_t;

/* Caller-owned scratch for the solvers. Contents are private. */
typedef struct {
    int64_t sym[QUBO_MAX_VARS][QUBO_MAX_VARS];
    int64_t field[QUBO_MAX_REPLICAS][QUBO_MAX_VARS];
    int64_t thr[QUBO_MAX_REPLICAS][QUBO_EXP_TABLE_N + 1u];
    int64_t temp[QUBO_MAX_REPLICAS];
    int64_t e[QUBO_MAX_REPLICAS];
    uint64_t xs[QUBO_MAX_REPLICAS];
    uint32_t perm[QUBO_MAX_REPLICAS];
    uint8_t ord[QUBO_MAX_VARS];
    uint64_t st_fixed[QUBO_MAX_VARS + 2u];
    uint64_t st_val[QUBO_MAX_VARS + 2u];
    int64_t st_lb[QUBO_MAX_VARS + 2u];
    int64_t lin[QUBO_MAX_VARS];
} qubo_ws_t;

/* ---- model ---- */
qubo_status_t qubo_init(qubo_t *p, uint32_t n);                       /* 1..64, all zero */
qubo_status_t qubo_set(qubo_t *p, uint32_t i, uint32_t j, int64_t v); /* (j,i) == (i,j) */
qubo_status_t qubo_add(qubo_t *p, uint32_t i, uint32_t j, int64_t v); /* checked add */
qubo_status_t qubo_get(const qubo_t *p, uint32_t i, uint32_t j, int64_t *v);
qubo_status_t qubo_check(const qubo_t *p); /* sizes, triangle, abs-sum limit */
qubo_status_t qubo_energy(const qubo_t *p, uint64_t x, int64_t *e);

qubo_status_t qubo_ising_init(qubo_ising_t *m, uint32_t n);
qubo_status_t qubo_ising_check(const qubo_ising_t *m);
qubo_status_t qubo_ising_energy(const qubo_ising_t *m, uint64_t s, int64_t *e);
/* Exact: E_out(x) == H_in(s) for s_i = 2 x_i - 1 (same bitmask). */
qubo_status_t qubo_from_ising(const qubo_ising_t *m, qubo_t *out);
/* Exact up to the stated scale: H_out(s) == 4 * E_in(x), same bitmask. */
qubo_status_t qubo_to_ising(const qubo_t *p, qubo_ising_t *out);

/* Text format, one item per line, '#' starts a comment:
 *   n <count>        size, 1..64; must come before any entry
 *   offset <value>   constant term (optional)
 *   <i> <j> <value>  coefficient of x_i x_j (i == j: linear term)
 * Integers are decimal with an optional sign. On error *out is unspecified
 * and report->tag/line say why. */
qubo_status_t qubo_parse(const char *text, uint32_t len, qubo_merge_t merge, qubo_t *out,
                         qubo_parse_report_t *report);

/* ---- bounds and certificates ---- */
/* Cheap proven bound: offset + sum_i min(0, q_ii + sum_{j>i} min(0, q_ij)). */
qubo_status_t qubo_root_bound(const qubo_t *p, int64_t *lb);
/* Recomputes E(res->x) and checks energy, gap and status agree. It cannot
 * re-prove the bound (that is the expensive part); tests do that. */
qubo_status_t qubo_verify(const qubo_t *p, const qubo_result_t *res);

/* ---- solvers ---- */
qubo_status_t qubo_brute_force(const qubo_t *p, uint64_t step_budget, qubo_ws_t *ws,
                               qubo_result_t *res);
/* warm may be NULL; otherwise a starting incumbent (bits >= n must be 0). */
qubo_status_t qubo_bnb(const qubo_t *p, uint64_t node_budget, const uint64_t *warm, qubo_ws_t *ws,
                       qubo_result_t *res);
/* *rng is xorshift64 state, must be non-zero; it is advanced. */
qubo_status_t qubo_anneal(const qubo_t *p, const qubo_anneal_params_t *prm, uint64_t *rng,
                          qubo_ws_t *ws, qubo_result_t *res);
qubo_status_t qubo_local_search(const qubo_t *p, const qubo_ls_params_t *prm, uint64_t *rng,
                                qubo_ws_t *ws, qubo_result_t *res);

/* xorshift64 step (a zero state is replaced by a fixed constant). */
uint64_t qubo_rng_next(uint64_t *state);
/* Q24 value of exp(-i/16) for i < QUBO_EXP_TABLE_N, else 0. */
uint32_t qubo_exp_q24(uint32_t i);
/* Table index the annealer uses for an uphill move d at temperature
 * t_q8 / 256: min(QUBO_EXP_TABLE_N, floor(4096 d / t_q8)), i.e. 16 d / T;
 * QUBO_EXP_TABLE_N (always reject) for t_q8 outside 1..QUBO_MAX_TEMP or
 * d < 0. */
uint32_t qubo_exp_index(int64_t t_q8, int64_t d);

#endif /* ZXV_QUBO_H */
