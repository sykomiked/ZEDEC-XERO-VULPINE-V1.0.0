/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* swarm_budget.h — tokens-per-cycle allocation for the ZXV AI swarm,
 *                  under the Fibonacci scaling rule.
 *
 * WHAT THIS IS
 * ------------
 * The swarm runs several AI models at once (conductor, core agents,
 * specialists, background workers). Every phase-tick cycle the swarm has a
 * fixed total of TOKENS it may generate. This module decides, exactly and
 * deterministically, how many of those tokens each model may spend in the
 * cycle, and enforces it: a model can never spend past its allotment.
 *
 * THE FIBONACCI SCALING RULE (a rule, not a tuning knob)
 * ------------------------------------------------------
 * Fibonacci numbers: F(1) = 1, F(2) = 1, F(n) = F(n-1) + F(n-2).
 * The swarm is organised in L levels, d = 0 .. L-1, where level 0 is the top
 * (the conductor) and deeper levels hold more, smaller-share models.
 *
 *   R1  CAPACITY.  Level d holds at most F(d+2) models:
 *                  1, 2, 3, 5, 8, 13, 21, 34.
 *                  A registration beyond that fails closed.
 *
 *   R2  SHARE.     Level d carries the weight W(d) = F(L - d + 1), so the
 *                  top level weighs most and each level down weighs the next
 *                  lower Fibonacci number. For L = 4 the weights are
 *                  5 : 3 : 2 : 1. Adjacent levels therefore stand in the
 *                  ratio of consecutive Fibonacci numbers, which tends to the
 *                  golden ratio as L grows.
 *
 *   R3  RATE.      Each cycle, the cycle total T is split across the levels
 *                  that have at least one active model, in proportion to
 *                  W(d). A level with no active model gets nothing and its
 *                  weight is not counted, so no token is stranded.
 *
 *   R4  EQUALITY.  Inside a level, the level's tokens are split equally
 *                  among its active models.
 *
 *   R5  EXACTNESS. Every split uses the largest-remainder method in integer
 *                  arithmetic, ties to the lower level / lower slot, so the
 *                  allotments always sum to EXACTLY T. Same inputs, same
 *                  allotments, on every target. No float, no clock.
 *
 *   R6  NO CARRY.  Allotments are a per-cycle RATE. Tokens a model does not
 *                  spend are reported as unused and then expire; they are not
 *                  hoarded into the next cycle.
 *
 * Freestanding: no libc, no allocation, no floating point (RMAG).
 */
#ifndef SWARM_BUDGET_H
#define SWARM_BUDGET_H

#include <stdint.h>
#include <stdbool.h>

#define SWARM_MAX_LEVELS          8u      /* capacities F(2)..F(9) */
#define SWARM_MAX_MODELS          64u
/* Upper bound on T so that T * W(d) cannot overflow 64 bits
 * (max weight F(9) = 34; the weights of 8 levels sum to F(11) - 2 = 87). */
#define SWARM_MAX_TOKENS_PER_CYCLE ((uint64_t)1 << 48)

typedef enum {
    SWARM_OK             =  0,
    SWARM_ERR_ARG        = -1,  /* NULL pointer or value out of range */
    SWARM_ERR_FULL       = -2,  /* level at its Fibonacci capacity (R1) */
    SWARM_ERR_NO_MODEL   = -3,  /* model id not registered */
    SWARM_ERR_DUPLICATE  = -4,  /* model id already registered */
    SWARM_ERR_NO_CYCLE   = -5   /* consume called outside an open cycle */
} swarm_status_t;

typedef struct {
    uint32_t model_id;
    uint8_t  level;
    bool     active;
    uint64_t allotted;   /* tokens granted for the current cycle (real + imaginary) */
    uint64_t allotted_im;/* imaginary (emotional) part of allotted; see swarm_emotion.h */
    uint64_t allotted_mk;/* real part bought on the market; see swarm_market.h */
    uint64_t used;       /* tokens spent in the current cycle */
} swarm_slot_t;

typedef struct {
    uint32_t     num_levels;          /* L, 1 .. SWARM_MAX_LEVELS */
    uint64_t     tokens_per_cycle;    /* T */
    uint64_t     cycle;               /* cycles begun so far */
    bool         cycle_open;
    swarm_slot_t slots[SWARM_MAX_MODELS];
    uint32_t     num_slots;
    uint64_t     level_budget[SWARM_MAX_LEVELS];   /* this cycle, per level */
    uint64_t     last_unused;         /* tokens expired at the last cycle end */
} swarm_budget_t;

/* F(n) for n >= 1 (F(1) = F(2) = 1). Returns 0 for n == 0 or n > 93. */
uint64_t swarm_fib(uint32_t n);

/* R1: the most models level d may hold. 0 if d is out of range. */
uint32_t swarm_level_capacity(uint32_t d);

/* R2: the share weight of level d in a swarm of L levels. 0 if out of range. */
uint64_t swarm_level_weight(uint32_t d, uint32_t num_levels);

swarm_status_t swarm_budget_init(swarm_budget_t *b, uint32_t num_levels,
                                 uint64_t tokens_per_cycle);

swarm_status_t swarm_budget_set_rate(swarm_budget_t *b, uint64_t tokens_per_cycle);

swarm_status_t swarm_budget_register(swarm_budget_t *b, uint32_t model_id,
                                     uint32_t level);

/* Activate or park a model. Parked models keep their slot and level but get
 * no tokens; their capacity stays reserved. Takes effect at the next cycle. */
swarm_status_t swarm_budget_set_active(swarm_budget_t *b, uint32_t model_id,
                                       bool active);

/* Open a cycle: compute every allotment (R3-R5). An already-open cycle is
 * closed first. */
swarm_status_t swarm_budget_begin_cycle(swarm_budget_t *b);

/* Open a cycle that splits only `real_total` (<= tokens_per_cycle) under
 * R1-R5. The rest of the cycle is left for the emotional economy
 * (swarm_emotion.h) to add on the imaginary axis. */
swarm_status_t swarm_budget_begin_cycle_real(swarm_budget_t *b, uint64_t real_total);

/* floor(a * b / c) exactly, through a 128-bit intermediate, with the
 * remainder in *rem (may be NULL). Returns 0 if c == 0 and UINT64_MAX if
 * the quotient does not fit in 64 bits. */
uint64_t swarm_muldiv(uint64_t a, uint64_t b, uint64_t c, uint64_t *rem);

/* R5 helper: largest-remainder split of `total` over n <= SWARM_MAX_MODELS
 * items in proportion to w[i], ties to the lower index. The sum of w must
 * fit in 64 bits. */
void swarm_split_lr(uint64_t total, const uint64_t *w, uint32_t n, uint64_t *out);

/* Spend up to `requested` tokens for model_id in the open cycle. Writes the
 * number actually granted (never past the allotment) to *granted. */
swarm_status_t swarm_budget_consume(swarm_budget_t *b, uint32_t model_id,
                                    uint64_t requested, uint64_t *granted);

/* Tokens model_id may still spend this cycle; 0 if unknown or no cycle. */
uint64_t swarm_budget_remaining(const swarm_budget_t *b, uint32_t model_id);

/* Close the cycle: record unused tokens in last_unused and expire them (R6). */
swarm_status_t swarm_budget_end_cycle(swarm_budget_t *b);

#endif /* SWARM_BUDGET_H */
