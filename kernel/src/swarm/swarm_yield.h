/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* swarm_yield.h — token yield: how much verified work each token buys.
 *
 * WHY
 * ---
 * Tokens are not all equal. Speed (tokens per second) and price (per million
 * tokens) count every token the same, so a model that needs three tries
 * looks as good as one that is right the first time. The owner's ask
 * (2026-10-15): "if we have less token generation, we need to make each
 * token go further and be way more accurate." This module measures that.
 *
 * THE RULES
 * ---------
 *   Y1  EVERY TOKEN COUNTS.  A task's tokens are all the tokens spent on it:
 *                            the first attempt, every retry and every
 *                            revision pass (swarm_quality.h Q2).
 *   Y2  ONLY VERIFIED WORK.  A task earns work only if its final output passes
 *                            the quality gate (swarm_quality.h Q1, r * l >= 1.8,
 *                            l from tests, sources and witnesses). It earns
 *                            r * l / 1000 work-milli (1000 = one unit; a task
 *                            that passes earns at least 1800). A task that
 *                            never passes earns nothing, and its tokens are
 *                            counted as wasted.
 *   Y3  YIELD.               Work-milli per 1000 tokens, over every task.
 *   Y4  EFFECTIVE RATE.      Yield times speed: verified work per second.
 *                            This, not tokens per second, decides whether
 *                            fewer, better tokens beat many cheap ones.
 *   Y5  EXACT.               Integer arithmetic only, no floating point; the
 *                            products are 128-bit, so nothing rounds before
 *                            the final division, and comparisons are exact.
 *   Y6  MEASUREMENT FIRST.   Yield changes no allotment, price or priority
 *                            by itself. Rewarding it is an economy decision
 *                            for the owner (swarm_budget.h, swarm_market.h);
 *                            Y8 is the only link, and it is off by default.
 *   Y7  THE CHART.           Yield is charted like a market price: each epoch
 *                            is one candle. Open is the epoch's first task's
 *                            yield, high and low its best and worst task,
 *                            close the epoch's overall yield (Y3), volume its
 *                            tokens. Close always lies between low and high.
 *                            An epoch with no tasks is a flat candle at the
 *                            last close with zero volume (a day with no
 *                            trades). The last SWARM_YIELD_BARS candles are
 *                            kept.
 *   Y8  POOL WEIGHTS.        Yield can decide how an EXISTING pool is shared
 *                            (the assurance fee's 15% node bounties, later
 *                            the 25% V-Bill dividend: pay_assure.h B1), by
 *                            each model's verified work, fed to the pool's
 *                            own capped split (pay_commons_split, no
 *                            monopoly). It never mints, never sets a
 *                            voucher's rate, and adds nothing that time or a
 *                            balance grows (no usury). Under the budget's
 *                            fixed allotments, verified work = yield x
 *                            tokens spent, so the more each token buys, the
 *                            larger the share. Off by default: every model
 *                            weighs 1, an even split.
 *
 * Per-token ACCURACY of the engine itself (how close each token's prediction
 * is to an exact float64 reference) is measured by bench/tensor, not here;
 * see docs/TOKEN_YIELD.md.
 *
 * Freestanding: no libc, no floating point.
 */
#ifndef SWARM_YIELD_H
#define SWARM_YIELD_H

#include <stdint.h>
#include <stdbool.h>
#include "swarm_budget.h"

typedef struct {
    uint64_t tokens;     /* Y1: every token spent */
    uint64_t wasted;     /* Y2: tokens of tasks that never passed */
    uint64_t work_milli; /* Y2: verified work, 1000 = one unit */
    uint64_t tasks;      /* tasks finished (passed or not) */
    uint64_t passed;     /* tasks whose final output passed the gate */
    uint64_t first_try;  /* of those, passed on the first attempt */
} swarm_yield_t;

void swarm_yield_init(swarm_yield_t *y);

/* Y1, Y2: record one finished task. tokens: all its tokens (> 0); attempts:
 * how many outputs it took (>= 1); r_milli, l_milli: the final output's
 * breadth and verified share (l_milli <= 1000). SWARM_ERR_ARG on a bad
 * argument or a counter that would overflow; nothing changes then. */
swarm_status_t swarm_yield_task(swarm_yield_t *y, uint64_t tokens, uint32_t attempts,
                                uint32_t r_milli, uint32_t l_milli);

/* Add every count of from into into. SWARM_ERR_ARG on overflow, nothing changes. */
swarm_status_t swarm_yield_merge(swarm_yield_t *into, const swarm_yield_t *from);

/* Y3: work-milli per 1000 tokens, rounded down; 0 with no tokens. */
uint64_t swarm_yield_per_k(const swarm_yield_t *y);

/* Y4: work-milli per second at tokens_per_s_milli (tokens/s x 1000),
 * rounded down; 0 with no tokens. Saturates at UINT64_MAX. */
uint64_t swarm_yield_rate(const swarm_yield_t *y, uint64_t tokens_per_s_milli);

/* Share of tasks that passed the gate on the first attempt, x1000. */
uint32_t swarm_yield_first_try_milli(const swarm_yield_t *y);

/* Share of tokens spent on tasks that never passed, x1000. */
uint32_t swarm_yield_waste_milli(const swarm_yield_t *y);

/* Y5: -1, 0 or 1 as a's yield is below, equal to or above b's, compared
 * exactly (no rounding). No tokens counts as a yield of 0. */
int swarm_yield_cmp(const swarm_yield_t *a, const swarm_yield_t *b);

/* Y4 compared exactly: -1, 0 or 1 as a at speed a_tps_milli does less,
 * the same or more verified work per second than b at b_tps_milli. */
int swarm_yield_cmp_rate(const swarm_yield_t *a, uint64_t a_tps_milli, const swarm_yield_t *b,
                         uint64_t b_tps_milli);

/* ---- Y7: the chart ---- */

#define SWARM_YIELD_BARS 144u /* F(12) closed epochs kept */

typedef struct {
    uint64_t open, high, low, close; /* yields, work-milli per 1000 tokens */
    uint64_t tokens;                 /* volume */
    uint64_t work_milli;
    uint64_t tasks;
} swarm_yield_bar_t;

typedef struct {
    swarm_yield_bar_t bars[SWARM_YIELD_BARS];
    uint32_t head;       /* slot the next closed candle goes in */
    uint32_t count;      /* closed candles held, <= SWARM_YIELD_BARS */
    uint64_t epochs;     /* candles ever closed */
    uint64_t last_close; /* the last candle's close */
    swarm_yield_t cur;   /* the open epoch's totals */
    uint64_t open, high, low;
} swarm_yield_chart_t;

void swarm_yield_chart_init(swarm_yield_chart_t *c);

/* Record a task in the open epoch (as swarm_yield_task; nothing changes on
 * a refusal). */
swarm_status_t swarm_yield_chart_task(swarm_yield_chart_t *c, uint64_t tokens, uint32_t attempts,
                                      uint32_t r_milli, uint32_t l_milli);

/* Close the open epoch into a candle and start the next. */
swarm_status_t swarm_yield_chart_close(swarm_yield_chart_t *c);

/* The candle `back` epochs ago (0 = the latest closed). false if not held. */
bool swarm_yield_chart_bar(const swarm_yield_chart_t *c, uint32_t back, swarm_yield_bar_t *out);

/* ---- Y8: pool weights ---- */

typedef enum {
    SWARM_YIELD_LINK_OFF = 0, /* default: every model weighs 1 */
    SWARM_YIELD_LINK_WORK = 1 /* each model weighs its verified work */
} swarm_yield_link_t;

/* Write w[0..n) for ys[0..n). false on a NULL, n == 0 or an unknown mode. */
bool swarm_yield_weights(swarm_yield_link_t mode, const swarm_yield_t *ys, uint32_t n, uint64_t *w);

#endif /* SWARM_YIELD_H */
