/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* swarm_deleg.h — the delegator position and the delegation tree, on the
 * four-valued logic (lpres.h). The design is
 * /mnt/project-files/zxv-tests/DELEGATOR_PLAN.md; the layout is the owner's
 * Seal III (nine core cells, a lead triad, hub-only coupling, two
 * half-cycles meeting at one seam).
 *
 *   D1 THE POSITION.  Orchestration is a position, not a kind of cell. For a
 *      task the caller lists its interaction circle: every cell with its
 *      expertise score for the task, its lineage, whether it is resident,
 *      in placement order (the lead triad first). swd_position gives the
 *      position to the resident cell with the highest score > 0:
 *        - the holder keeps it on a tie it is part of (no churn);
 *        - otherwise the first of the tied cells in placement order;
 *        - nobody with a score > 0: the position stays, the claim NEITHER.
 *      The hand-off is a claim, "this cell is the best match here":
 *      TRUE when one cell's score is the highest alone, BOTH when the
 *      highest score is shared by cells of two or more lineages (the
 *      experts disagree on equal footing: logged, never hidden), NEITHER
 *      when nobody matched. The caller receipts every hand-off.
 *   D2 THE TREE.  A task is a node; the position holder may split it into
 *      at most SWD_FANOUT sub-tasks, each a node with its own holder, to a
 *      depth of at most max_depth (the swarm's levels). Each node reserves
 *      its estimated tokens from the tree's budget before it runs; a node
 *      over the depth or the budget is recorded as refused and stays
 *      NEITHER ("not checked"), never run and never guessed. A sub-task
 *      past the fan-out or the node table has no slot to be recorded in:
 *      its parent counts it as lost, and a lost sub-task joins as NEITHER
 *      just like a refused one, so dropping work can never raise a verdict.
 *   D3 TWO HALF-CYCLES AND THE SEAM.  Outward: nodes are opened (work handed
 *      out). swd_turn. Inward: tokens spent are recorded, witnesses give
 *      verdicts, nodes close leaves first. swd_seal: only when every node
 *      that ran is closed; the root's verdict is fixed, unspent tokens are
 *      returned, and nothing changes after (the commit point).
 *   D4 JOINS.  Verdicts of several witnesses on the SAME claim join in the
 *      knowledge order (lpres_kjoin: TRUE with FALSE is BOTH). Sub-results
 *      that together make an answer join with lpres_conjoin, alternative
 *      routes to one answer with lpres_disjoin (the kernel's chain order
 *      F < N < B < T: no mix of BOTH and NEITHER ever gives TRUE). A refused
 *      sub-task counts as NEITHER. An interior node's verdict is the join
 *      of its children, knowledge-joined with any witness of the whole.
 *      Every BOTH stays in the tree (swd_conflicts), whatever the root is.
 *   D5 RECORDS.  swd_tally counts, per cell, the verdicts of the closed
 *      sub-tasks it held; the reliability is TRUE / (TRUE + FALSE + BOTH)
 *      in per mille, and a cell that did no checked work has none.
 * Freestanding: no libc, no allocation, integers only. */
#ifndef SWARM_DELEG_H
#define SWARM_DELEG_H

#include <stdbool.h>
#include <stdint.h>
#include "lpres.h"

#define SWD_MAX_NODES 64u
#define SWD_FANOUT    4u
#define SWD_MAX_DEPTH 8u
#define SWD_NONE      0xFFFFFFFFu

typedef enum {
    SWD_OK = 0,
    SWD_EARG = -1,   /* bad argument or unknown node */
    SWD_EPHASE = -2, /* not allowed in this half-cycle */
    SWD_EOPEN = -3,  /* a node that ran is not closed yet */
    SWD_ESPEND = -4  /* more tokens spent than reserved */
} swd_status_t;

/* ---- D1 ---- */
typedef struct {
    uint32_t cell;    /* the caller's cell number */
    uint32_t score;   /* expertise match for this task; 0 = none */
    uint32_t lineage; /* cells of one lineage are one voice */
    bool resident;    /* loaded within the memory budget */
} swd_cand_t;

typedef struct {
    uint32_t from, to; /* cell numbers; from SWD_NONE when nobody held it */
    uint32_t score_from, score_to;
    lpres_state_t tv; /* the claim "to is the best match" */
    bool moved;
} swd_handoff_t;

swd_status_t swd_position(const swd_cand_t *c, uint32_t n, uint32_t holder, swd_handoff_t *h);

/* ---- D2-D5 ---- */
typedef enum { SWD_CONJ = 0, SWD_DISJ = 1 } swd_join_t;
typedef enum {
    SWD_RUN = 0,        /* opened, not closed */
    SWD_CLOSED = 1,     /* its verdict is final */
    SWD_REF_DEPTH = 2,  /* refused: deeper than max_depth */
    SWD_REF_FANOUT = 3, /* refused: the parent has SWD_FANOUT children */
    SWD_REF_BUDGET = 4, /* refused: the tokens were not there */
    SWD_REF_FULL = 5    /* refused: the node table is full */
} swd_state_t;

typedef struct {
    uint32_t parent; /* SWD_NONE for the root */
    uint32_t holder; /* the cell holding the position for this node */
    uint32_t depth;
    uint64_t reserved, spent;
    swd_join_t join; /* how its children make its result */
    swd_state_t state;
    lpres_state_t witnessed; /* knowledge join of the witnesses of this node */
    lpres_state_t tv;        /* the node's verdict once closed */
    uint32_t n_child;
    uint32_t child[SWD_FANOUT];
    uint32_t n_lost; /* sub-tasks refused with no slot (fan-out, table full) */
} swd_node_t;

typedef enum { SWD_OUT = 0, SWD_IN = 1, SWD_SEALED = 2 } swd_phase_t;

typedef struct {
    swd_node_t node[SWD_MAX_NODES];
    uint32_t n;
    uint32_t max_depth;
    uint64_t budget;   /* tokens not yet reserved */
    uint64_t returned; /* tokens given back at the seam */
    swd_phase_t phase;
    lpres_state_t root_tv;
} swd_tree_t;

/* max_depth 1..SWD_MAX_DEPTH (the root is depth 0). */
swd_status_t swd_init(swd_tree_t *t, uint32_t max_depth, uint64_t token_budget);
/* Outward half only. parent SWD_NONE opens the root (once). A node refused
 * for depth or budget is recorded: SWD_OK, *id its number, its state set.
 * A sub-task past the fan-out or a full table: SWD_OK, *id SWD_NONE, and the
 * parent's n_lost grows (it joins as NEITHER when the parent closes). */
swd_status_t swd_open(swd_tree_t *t, uint32_t parent, uint32_t holder, uint64_t est_tokens,
                      swd_join_t join, uint32_t *id);
swd_status_t swd_turn(swd_tree_t *t);
/* Inward half only, on a running node. */
swd_status_t swd_spend(swd_tree_t *t, uint32_t id, uint64_t tokens);
swd_status_t swd_witness(swd_tree_t *t, uint32_t id, lpres_state_t v);
/* Inward half; every child must be closed or refused. */
swd_status_t swd_close(swd_tree_t *t, uint32_t id);
/* The seam: every running node closed; fixes root_tv and returns the
 * unspent tokens (in t->returned). */
swd_status_t swd_seal(swd_tree_t *t);

/* Nodes whose verdict is BOTH, up to cap; returns how many there are. */
uint32_t swd_conflicts(const swd_tree_t *t, uint32_t *ids, uint32_t cap);
/* D5: verdict counts of the closed nodes `cell` held (counts[lpres state]). */
void swd_tally(const swd_tree_t *t, uint32_t cell, uint32_t counts[4]);
/* Reliability in per mille from counts; false when there is no checked work. */
bool swd_reliability(const uint32_t counts[4], uint32_t *permille);

#endif /* SWARM_DELEG_H */
