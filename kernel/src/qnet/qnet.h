/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* qnet.h — cycle-cancellation netting: greedy, exact and a "quantum-inspired"
 * (classical replica-exchange annealing) solver, on one fixed-capacity graph.
 *
 * WHAT IT DOES
 * ------------
 * Members owe each other money: a directed obligation graph where edge e says
 * from[e] owes to[e] the integer amount cap[e] (minor units, cap > 0). Cycle
 * cancellation finds a CIRCULATION f on that graph:
 *     0 <= f[e] <= cap[e]                         for every edge
 *     sum of f into v == sum of f out of v        for every member v
 * and reduces every obligation to cap[e] - f[e]. Because f is a circulation,
 * every member's net position (owed-to minus owes) is exactly unchanged, and
 * no new counterparty pair is ever created: obligations only shrink. The
 * measured value is the ELIMINATED GROSS, sum of f[e]. This differs from
 * abacus.h (smaug_clear), which rewrites the book into new payer/payee pairs.
 *
 * Three solvers, same input, same output type:
 *   qn_greedy_cycles  baseline: DFS (edges in index order) finds a cycle of
 *                     positive remaining obligations, cancels its minimum,
 *                     repeats until the remaining graph is acyclic. Fast,
 *                     deterministic, maximal but NOT always maximum.
 *   qn_exact_optimum  the maximum: min-cost circulation with cost -1 per unit
 *                     on every edge, solved by minimum-mean cycle cancelling
 *                     on the residual graph (Karp's walk-length Bellman-Ford
 *                     recurrence). qn_check_optimal is an independent
 *                     optimality certificate: plain Bellman-Ford proves that
 *                     no negative-cost residual cycle remains.
 *   qn_anneal         the "quantum-inspired" heuristic. Honestly: classical
 *                     parallel tempering (replica exchange) on a classical
 *                     CPU. Replicas at different inverse temperatures make
 *                     push/unpush moves along short cycles (precomputed,
 *                     length <= max_cycle_len), with integer Metropolis
 *                     acceptance from a fixed-point exp(-x) table, and swap
 *                     states between neighbouring temperatures. Optional
 *                     greedy polish at the end. Seeded PRNG passed in.
 *
 * Every solver checks its own output with qn_verify (bounds, conservation,
 * eliminated == sum f) before returning QN_OK.
 *
 * HONEST LIMITS
 *   - The exact solver is optimal (and polynomial). No heuristic, quantum
 *     or otherwise, can eliminate MORE gross than it; the annealer can only
 *     tie or lose. See docs/QUANTUM_NETTING.md for the measured comparison.
 *   - Nothing here is quantum. "Quantum-inspired" is the conventional name of
 *     the replica family of heuristics; this one runs on an ordinary CPU.
 *   - The annealer only moves flow along the short cycles it enumerated (a
 *     bounded list); cycles longer than max_cycle_len are reachable only by
 *     the optional greedy polish.
 *   - Fixed capacity: QN_MAX_NODES members, QN_MAX_EDGES obligations, one
 *     currency per graph, no persistence. Obligations must already be
 *     authenticated; this is arithmetic, not consensus.
 *   - The exact solver has an iteration guard (QN_EXACT_MAX_ITERS); hitting
 *     it returns QN_ERR_LIMIT and no result, never a wrong one.
 *
 * Freestanding: integers only, no allocation (caller-owned workspace), no
 * libc. Every fallible function returns a qn_status_t.
 */
#ifndef ZXV_QNET_H
#define ZXV_QNET_H

#include <stdint.h>

#define QN_MAX_NODES        64u
#define QN_MAX_EDGES        512u
#define QN_MAX_ARCS         (2u * QN_MAX_EDGES)
#define QN_MAX_REPLICAS     8u
#define QN_CYC_MAX_LEN      8u
#define QN_MAX_CYCLES       4096u
#define QN_EXP_TABLE_N      256u      /* exp(-x) for x = i/16, i < 256 */
#define QN_EXP_ONE          16777216u /* Q24 fixed point 1.0 */
#define QN_EXACT_MAX_ITERS  1000000u
#define QN_ANNEAL_MAX_MOVES 4000000000u

typedef enum {
    QN_OK = 0,
    QN_ERR_NULL = -1,        /* a required pointer was NULL */
    QN_ERR_RANGE = -2,       /* node count / node index out of range */
    QN_ERR_FULL = -3,        /* edge capacity exhausted */
    QN_ERR_CAP = -4,         /* obligation amount <= 0 */
    QN_ERR_SELF = -5,        /* obligation from a member to itself */
    QN_ERR_OVERFLOW = -6,    /* a sum would not fit in int64 */
    QN_ERR_INFEASIBLE = -7,  /* flow violates bounds or conservation */
    QN_ERR_NOT_OPTIMAL = -8, /* a negative residual cycle exists */
    QN_ERR_PARAM = -9,       /* bad annealing parameters / PRNG state */
    QN_ERR_LIMIT = -10,      /* iteration guard hit */
    QN_ERR_INTERNAL = -11    /* internal consistency check failed */
} qn_status_t;

typedef struct {
    uint32_t num_nodes;
    uint32_t num_edges;
    uint32_t from[QN_MAX_EDGES];
    uint32_t to[QN_MAX_EDGES];
    int64_t cap[QN_MAX_EDGES]; /* obligation amount, minor units, > 0 */
} qn_graph_t;

typedef struct {
    int64_t flow[QN_MAX_EDGES]; /* cancelled amount per edge */
    int64_t eliminated;         /* sum of flow */
    uint32_t cycles;            /* cycles cancelled / moves accepted */
    uint32_t iterations;        /* solver iterations (cycle searches / moves) */
} qn_result_t;

typedef struct {
    uint32_t replicas;        /* 1..QN_MAX_REPLICAS */
    uint32_t sweeps;          /* >= 1 */
    uint32_t moves_per_sweep; /* per replica, >= 1 */
    uint32_t swap_interval;   /* sweeps between replica exchanges, >= 1 */
    uint32_t max_cycle_len;   /* 2..QN_CYC_MAX_LEN */
    uint32_t polish;          /* 1 = finish with greedy cancellation */
} qn_anneal_params_t;

/* Caller-owned scratch memory shared by all solvers (about 150 KiB). Its
 * contents are meaningless between calls. */
typedef struct {
    /* adjacency (out-edges in index order) */
    int32_t head[QN_MAX_NODES];
    int32_t next[QN_MAX_EDGES];
    /* greedy DFS */
    int64_t rem[QN_MAX_EDGES];
    int32_t iter[QN_MAX_NODES];
    int32_t pe[QN_MAX_NODES];
    uint8_t color[QN_MAX_NODES];
    uint32_t stack[QN_MAX_NODES + 1u];
    /* exact: Karp tables */
    int32_t kd[QN_MAX_NODES + 1u][QN_MAX_NODES];
    int16_t kp[QN_MAX_NODES + 1u][QN_MAX_NODES];
    int32_t seen[QN_MAX_NODES];
    uint16_t walk[QN_MAX_NODES + 1u];
    int64_t bf[QN_MAX_NODES];
    /* anneal */
    uint16_t cyc[QN_MAX_CYCLES][QN_CYC_MAX_LEN];
    uint8_t cyc_len[QN_MAX_CYCLES];
    uint32_t num_cycles;
    int64_t rflow[QN_MAX_REPLICAS][QN_MAX_EDGES];
    int64_t relim[QN_MAX_REPLICAS];
    uint32_t perm[QN_MAX_REPLICAS];
    uint8_t onpath[QN_MAX_NODES];
} qn_workspace_t;

/* Graph construction. */
qn_status_t qn_graph_init(qn_graph_t *g, uint32_t num_nodes);
qn_status_t qn_graph_add(qn_graph_t *g, uint32_t from, uint32_t to, int64_t amount,
                         uint32_t *out_index);
/* Full validation: ranges, amounts > 0, no self loops, and the total gross
 * fits in int64 (so every partial sum the solvers form fits too). */
qn_status_t qn_graph_check(const qn_graph_t *g);

/* net[v] = sum over edges into v of (cap - flow) minus sum over edges out of
 * v of (cap - flow). flow may be NULL (= nothing cancelled). nets must hold
 * num_nodes entries. Validates flow bounds when flow != NULL. */
qn_status_t qn_net_positions(const qn_graph_t *g, const int64_t *flow, int64_t *nets);

/* Checks 0 <= flow <= cap and conservation at every member; writes the sum
 * of flow to *eliminated (may be NULL). */
qn_status_t qn_verify(const qn_graph_t *g, const int64_t *flow, int64_t *eliminated);

/* Optimality certificate: QN_OK iff flow is feasible and the residual graph
 * (forward arcs cost -1 where flow < cap, backward arcs cost +1 where
 * flow > 0) has no negative cycle (Bellman-Ford). QN_ERR_NOT_OPTIMAL if one
 * exists. */
qn_status_t qn_check_optimal(const qn_graph_t *g, const int64_t *flow, qn_workspace_t *ws);

qn_status_t qn_greedy_cycles(const qn_graph_t *g, qn_workspace_t *ws, qn_result_t *out);
qn_status_t qn_exact_optimum(const qn_graph_t *g, qn_workspace_t *ws, qn_result_t *out);
/* *rng is xorshift64 state, must be non-zero; it is advanced. */
qn_status_t qn_anneal(const qn_graph_t *g, const qn_anneal_params_t *p, uint64_t *rng,
                      qn_workspace_t *ws, qn_result_t *out);

/* Defaults used by the benchmark: 8 replicas, 200 sweeps, 64 moves per
 * sweep per replica, exchange every sweep, cycles up to length 6, polish. */
qn_status_t qn_anneal_defaults(qn_anneal_params_t *p);

/* xorshift64 step. A zero state (which would stick at zero) is first
 * replaced by a fixed non-zero constant. qn_anneal itself refuses a zero
 * state with QN_ERR_PARAM so the caller's seed is always meaningful. */
uint64_t qn_rng_next(uint64_t *state);

/* Integer Metropolis test: returns 1 with probability exp(-x16/16) (Q24
 * table lookup against 24 random bits), 0 for x16 >= QN_EXP_TABLE_N or a
 * NULL rng. Advances *rng by one step unless it returns early. */
int qn_metropolis_accept(uint64_t *rng, uint64_t x16);

/* Q24 value of exp(-i/16) for i < QN_EXP_TABLE_N, else 0. */
uint32_t qn_exp_q24(uint32_t i);

#endif /* ZXV_QNET_H */
