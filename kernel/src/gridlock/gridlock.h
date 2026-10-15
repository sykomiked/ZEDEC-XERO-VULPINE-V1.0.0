/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* gridlock.h — exact, auditable gridlock resolution for a queue of payments.
 *
 * WHAT IT DOES
 * ------------
 * A real-time gross settlement (RTGS) system, payment network or
 * mutual-credit circle holds a queue of payments that cannot settle one at a
 * time because each sender lacks liquidity. A SUBSET of them may still
 * settle simultaneously, because the incoming payments fund the outgoing
 * ones (A->B, B->C, C->A with zero balances: none settles alone, all three
 * settle together). This module chooses that subset S.
 *
 *   participants p (<= GL_MAX_PART): balance b_p >= 0, credit cap c_p >= 0
 *   payments i (<= GL_MAX_PAY): from, to, amount > 0, priority class, FIFO
 *     position seq (queue order per sender: ascending seq, then index)
 *   feasible S: for every p,  b_p - out_p(S) + in_p(S) >= -c_p
 *   objective: maximise the settled value sum_{i in S} amount_i
 *     (GL_OBJ_VALUE), or lexicographically: first the value of priority
 *     class 0, then class 1, ... (GL_OBJ_LEX_PRIORITY; class 0 = urgent)
 *   modes: free (any subset) or FIFO (each sender's settled payments must be
 *     a prefix of its queue, as RTGS queues usually require)
 *
 * Solvers, one result type:
 *   gl_heuristic   the classical baseline: start with every eligible
 *                  payment; while some participant is negative, take the
 *                  most negative (ties: lowest index) and remove one of its
 *                  included outgoing payments: the LAST in its queue
 *                  (GL_RM_LAST; always used in FIFO mode) or the LARGEST
 *                  (GL_RM_LARGEST, free mode only). In lexicographic mode
 *                  free-mode removal first picks the lowest-priority class.
 *                  No payment is ever re-added. Fast and feasible; in free
 *                  mode not optimal (tests construct counterexamples).
 *   gl_solve       FIFO mode: the removal fixpoint above IS the exact
 *                  optimum (feasible prefix vectors are closed under
 *                  componentwise max, so a greatest one exists, and removal
 *                  never drops below it; proof in gridlock.c). Returned as
 *                  GL_OPTIMAL with no search, polynomial time.
 *                  Free mode: exact branch and bound seeded with the
 *                  heuristic's answer, branching include/exclude per payment
 *                  (classes, then amount descending), with a dominance rule
 *                  (a sender able to pay all its remaining payments from
 *                  money already certain to arrive pays them all). Upper
 *                  bounds need no LP: a participant can pay out at most
 *                  b + c + (everything that could still arrive), so
 *                  value <= sum_p min(out_possible_p, b_p + c_p + in_possible_p).
 *                  Within the node budget it returns GL_OPTIMAL; when the
 *                  budget runs out it returns the best set found
 *                  (GL_FEASIBLE) and a PROVEN upper bound (the gap
 *                  certificate: the maximum bound over the unexplored
 *                  subtrees).
 *   gl_brute_force exhaustive search for <= GL_BRUTE_MAX eligible payments
 *                  (tests and audits).
 *
 * Every result is checked with gl_verify before it is returned: every
 * participant at or above -credit, conservation (the sum of balances is
 * unchanged exactly), FIFO order where required, no held payment settled,
 * and settled value == sum of chosen amounts. All arithmetic is int64 with
 * overflow detection (gl_check bounds the grand total once).
 *
 * PARACONSISTENT STATUS
 *   Each payment carries a four-valued status, numerically identical to
 *   lpres_state_t in kernel/src/lpres/lpres.h (defined here separately
 *   because lpres.h pulls in other headers): NEITHER=0, TRUE=1 (queued),
 *   FALSE=2 (cancelled), BOTH=3. Reports from several sources are joined
 *   (gl_report_status: knowledge-order join, bitwise OR). TRUE payments are
 *   eligible. FALSE payments are dropped. BOTH (conflicting reports) and
 *   NEITHER (no evidence) are HELD: never settled, never treated as
 *   cancelled, and reported. In FIFO mode a held payment also blocks the
 *   payments behind it in its sender's queue (settling them would jump the
 *   queue on a guess); they are reported as GL_D_BLOCKED_FIFO.
 *
 * HONEST LIMITS
 *   - The free-mode problem is NP-hard (one sender with balance B paying many
 *     receivers is subset-sum), so the exact solver is exponential in the
 *     worst case. The node budget turns that into "best + proven bound",
 *     never a silent wrong answer. FIFO mode is polynomial (see gl_solve),
 *     and there the classical removal heuristic is already optimal.
 *   - Nothing here is quantum and nothing is compared with quantum hardware.
 *     The benchmark compares against the classical removal heuristic only.
 *   - The heuristic is our own documented version of the removal family
 *     described in the payments literature and in public descriptions of
 *     RTGS gridlock algorithms; a given live system's exact rules may differ.
 *   - One currency per instance, fixed capacity, no persistence. Payments
 *     must already be authenticated; this is arithmetic, not consensus.
 *   - Credit caps are interest-free limits; no collateral or pricing model.
 *
 * Freestanding: integers only, no allocation (caller-owned workspace), no
 * libc. Every fallible function returns a gl_status_t.
 */
#ifndef ZXV_GRIDLOCK_H
#define ZXV_GRIDLOCK_H

#include <stdint.h>

#define GL_MAX_PART    64u
#define GL_MAX_PAY     256u
#define GL_MAX_CLASSES 4u
#define GL_BRUTE_MAX   20u

/* Same numeric values as lpres_state_t (kernel/src/lpres/lpres.h). */
typedef enum { GL_NEITHER = 0, GL_TRUE = 1, GL_FALSE = 2, GL_BOTH = 3 } gl_lp_t;

typedef enum {
    GL_OK = 0,
    GL_ERR_NULL = -1,       /* a required pointer was NULL */
    GL_ERR_RANGE = -2,      /* participant count / index out of range */
    GL_ERR_FULL = -3,       /* payment capacity exhausted */
    GL_ERR_AMOUNT = -4,     /* payment amount <= 0 */
    GL_ERR_SELF = -5,       /* payment from a participant to itself */
    GL_ERR_BALANCE = -6,    /* negative balance or credit cap */
    GL_ERR_OVERFLOW = -7,   /* a total would not fit in int64 */
    GL_ERR_PARAM = -8,      /* bad options, status, priority or index */
    GL_ERR_INFEASIBLE = -9, /* a participant ends below -credit */
    GL_ERR_FIFO = -10,      /* settled set skips a queued payment (FIFO) */
    GL_ERR_HELD = -11,      /* a held / cancelled / blocked payment settled */
    GL_ERR_TOO_BIG = -12,   /* brute force: more than GL_BRUTE_MAX eligible */
    GL_ERR_INTERNAL = -13   /* internal consistency check failed */
} gl_status_t;

typedef enum { GL_OBJ_VALUE = 0, GL_OBJ_LEX_PRIORITY = 1 } gl_objective_t;
typedef enum { GL_RM_LAST = 0, GL_RM_LARGEST = 1 } gl_removal_t;
typedef enum { GL_OPTIMAL = 0, GL_FEASIBLE = 1, GL_HEURISTIC = 2 } gl_quality_t;

/* Per-payment disposition in a result. */
typedef enum {
    GL_D_SETTLED = 0,
    GL_D_QUEUED = 1,       /* eligible, not in the chosen set */
    GL_D_CANCELLED = 2,    /* status FALSE */
    GL_D_HELD_BOTH = 3,    /* conflicting status reports */
    GL_D_HELD_NEITHER = 4, /* no status evidence */
    GL_D_BLOCKED_FIFO = 5  /* FIFO mode: behind a held payment */
} gl_disp_t;

typedef struct {
    uint32_t num_parts;
    uint32_t num_pay;
    int64_t balance[GL_MAX_PART]; /* >= 0, minor units */
    int64_t credit[GL_MAX_PART];  /* interest-free credit cap, >= 0 */
    uint8_t from[GL_MAX_PAY];
    uint8_t to[GL_MAX_PAY];
    uint8_t prio[GL_MAX_PAY];   /* class 0 (most urgent) .. GL_MAX_CLASSES-1 */
    uint8_t status[GL_MAX_PAY]; /* gl_lp_t */
    uint32_t seq[GL_MAX_PAY];   /* FIFO position within the sender's queue */
    int64_t amount[GL_MAX_PAY]; /* > 0 */
} gl_instance_t;

typedef struct {
    uint32_t fifo;        /* 0 = free subset, 1 = per-sender prefixes */
    uint32_t objective;   /* gl_objective_t */
    uint32_t removal;     /* gl_removal_t (heuristic, free mode) */
    uint64_t node_budget; /* free-mode branch-and-bound nodes, >= 1 */
} gl_opts_t;

typedef struct {
    uint8_t settle[GL_MAX_PAY]; /* 1 = settles now */
    uint8_t disp[GL_MAX_PAY];   /* gl_disp_t */
    int64_t balance_after[GL_MAX_PART];
    int64_t value;                  /* sum of settled amounts */
    int64_t key[GL_MAX_CLASSES];    /* objective vector (VALUE: key[0]) */
    int64_t ub_key[GL_MAX_CLASSES]; /* proven upper bound, same order */
    int64_t ub_value;               /* proven upper bound on any feasible value */
    uint32_t settled;               /* number of settled payments */
    uint32_t eligible;              /* payments that could settle */
    uint32_t cancelled, held_both, held_neither, blocked_fifo;
    uint32_t quality; /* gl_quality_t */
    uint64_t nodes;   /* branch-and-bound nodes / subsets tried */
} gl_result_t;

/* Caller-owned scratch memory (about 15 KiB). Contents are meaningless
 * between calls. */
typedef struct {
    uint8_t disp[GL_MAX_PAY];
    uint16_t ord[GL_MAX_PAY]; /* free: branch order; FIFO: sender queues */
    uint32_t n_ord;
    uint16_t fstart[GL_MAX_PART + 1u];
    uint16_t flen[GL_MAX_PART];
    int64_t capb[GL_MAX_PART], indec[GL_MAX_PART], inund[GL_MAX_PART];
    int64_t outdec[GL_MAX_PART], outund[GL_MAX_PART];
    int64_t mb[GL_MAX_PART], acc[GL_MAX_PART]; /* bound refinement */
    int64_t kcur[GL_MAX_CLASSES], kund[GL_MAX_CLASSES], kbest[GL_MAX_CLASSES];
    int64_t bnd[GL_MAX_PAY + 1u][GL_MAX_CLASSES];
    int32_t st[GL_MAX_PAY + 1u];
    uint8_t forced[GL_MAX_PAY + 1u];
    uint8_t cur[GL_MAX_PAY];
    uint8_t best[GL_MAX_PAY];
    uint16_t pk[GL_MAX_PART]; /* FIFO current prefix per sender */
} gl_workspace_t;

/* Knowledge-order join of two reports (bitwise OR of the encodings). Values
 * outside 0..3 are masked to their low two bits. */
gl_lp_t gl_lp_join(gl_lp_t a, gl_lp_t b);

/* Construction. gl_init zeroes the instance (num_parts 1..GL_MAX_PART). */
gl_status_t gl_init(gl_instance_t *in, uint32_t num_parts);
gl_status_t gl_set_balance(gl_instance_t *in, uint32_t p, int64_t balance, int64_t credit);
/* Adds a queued payment (status TRUE: the submitter's report). */
gl_status_t gl_add_payment(gl_instance_t *in, uint32_t from, uint32_t to, int64_t amount,
                           uint32_t prio, uint32_t seq, uint32_t *out_index);
/* Joins another source's report into the payment's status. */
gl_status_t gl_report_status(gl_instance_t *in, uint32_t idx, gl_lp_t report);
/* Overwrites the status (a single authoritative source). */
gl_status_t gl_set_status(gl_instance_t *in, uint32_t idx, gl_lp_t status);

/* Full validation: ranges, amounts > 0, no self payments, balances and
 * credits >= 0, statuses and priorities valid, and the sum of all balances,
 * credits and amounts fits in int64. */
gl_status_t gl_check(const gl_instance_t *in);
gl_status_t gl_opts_default(gl_opts_t *o); /* free, VALUE, LAST, 1,000,000 */
gl_status_t gl_opts_check(const gl_opts_t *o);

/* Independent audit of a chosen set. Writes balance_after (num_parts
 * entries, may be NULL), the objective key (GL_MAX_CLASSES entries, may be
 * NULL) and the settled value (may be NULL). */
gl_status_t gl_verify(const gl_instance_t *in, const gl_opts_t *o, const uint8_t *settle,
                      int64_t *balance_after, int64_t *key, int64_t *value);

/* Lexicographic comparison of two keys over n entries: -1, 0 or 1. */
int gl_key_cmp(const int64_t *a, const int64_t *b, uint32_t n);

gl_status_t gl_heuristic(const gl_instance_t *in, const gl_opts_t *o, gl_workspace_t *ws,
                         gl_result_t *out);
gl_status_t gl_solve(const gl_instance_t *in, const gl_opts_t *o, gl_workspace_t *ws,
                     gl_result_t *out);
gl_status_t gl_brute_force(const gl_instance_t *in, const gl_opts_t *o, gl_workspace_t *ws,
                           gl_result_t *out);

#endif /* ZXV_GRIDLOCK_H */
