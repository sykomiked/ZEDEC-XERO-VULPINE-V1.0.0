/* pterm_mux.h — Phase-tick synchronized multi-terminal (master/sub)
 *
 * A ZXV-native terminal multiplexer. It is NOT clock-synchronized: sub
 * terminals advance on the kernel's phase tick / event cycle, and how
 * much dispatch each one receives is derived from the Interaction
 * Surplus Framework, not from a timeslice.
 *
 * Model
 * -----
 *   One MASTER terminal owns N SUB terminals. Each sub declares an
 *   interaction parameter u in [0,1] describing how orthogonal its work
 *   is to the master's current focus (u=0 parallel/redundant, u=1
 *   orthogonal/complementary — the ISF u = 1 - (x.y)^2).
 *
 *   ISF (Paper A, Thm 2.1) gives the effective count of independent
 *   contributions for that interaction:
 *
 *        f(u) = ln(1 + (N-1)u)        g(u) = e^f(u) = 1 + (N-1)u
 *
 *   g(u) is exactly "how many terminals' worth of independent work this
 *   sub is really contributing". We use it directly as the sub's
 *   dispatch weight, so a sub doing work orthogonal to everything else
 *   earns more of the event budget than one duplicating the master.
 *   Weights are recomputed per phase tick; nothing consults a wall clock.
 *
 *   Each tick, every ACTIVE sub accumulates credit proportional to its
 *   weight; when a sub's credit crosses one full share it is dispatched
 *   (one event-cycle turn) and its credit is decremented. This is a
 *   causal, event-ordered rotation — deterministic for a given tick
 *   sequence, and replayable, per the external-clock-bridge rule.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV terminal-mux slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ZXV_PTERM_MUX_H
#define ZXV_PTERM_MUX_H

#include <stdint.h>
#include <stdbool.h>

#define PMUX_MAX_SUBS     8
#define PMUX_NAME_LEN     24

typedef enum {
    PMUX_SLOT_FREE = 0,
    PMUX_SLOT_ACTIVE,      /* participating in the phase rotation */
    PMUX_SLOT_PAUSED,      /* retained, but not dispatched */
} pmux_state_t;

typedef struct {
    char        name[PMUX_NAME_LEN];
    pmux_state_t state;
    uint32_t    console;        /* backing P-TERM console index */
    /* ISF interaction parameter u in [0,1], stored as Q16.16 so the
     * mux carries no floating point. */
    uint32_t    u_q16;
    uint32_t    weight_q16;     /* g(u) = 1 + (N-1)u, Q16.16 */
    uint32_t    credit_q16;     /* accumulated dispatch credit */
    uint64_t    dispatches;     /* how many turns this sub has taken */
    uint64_t    last_tick;      /* event cycle of its last dispatch */
} pmux_sub_t;

typedef struct {
    pmux_sub_t sub[PMUX_MAX_SUBS];
    uint32_t   num_subs;        /* live (non-free) slots */
    uint32_t   num_active;      /* slots in PMUX_SLOT_ACTIVE */
    uint32_t   focus;           /* index of the sub currently in focus */
    uint64_t   ticks;           /* phase ticks observed */
    uint64_t   total_dispatch;
    bool       initialized;
} pmux_t;

/* Q16.16 helpers — the mux is integer-only. */
#define PMUX_ONE        (1u << 16)
#define PMUX_FROM_PCT(p) (uint32_t)(((uint64_t)(p) * PMUX_ONE) / 100u)

void pmux_init(pmux_t *m);

/* Create a sub terminal bound to a P-TERM console.
 * u_pct is the interaction parameter as a percentage 0..100.
 * Returns the sub index, or -1 if full. */
int32_t pmux_spawn(pmux_t *m, const char *name, uint32_t console,
                   uint32_t u_pct);

/* Close / pause / resume a sub. */
bool pmux_close(pmux_t *m, uint32_t idx);
bool pmux_set_state(pmux_t *m, uint32_t idx, pmux_state_t st);

/* Change a sub's interaction parameter (recomputes weights). */
bool pmux_set_u(pmux_t *m, uint32_t idx, uint32_t u_pct);

/* Recompute every active sub's ISF weight g(u) = 1 + (N-1)u for the
 * current active count. Called automatically on spawn/close/set_u. */
void pmux_recompute_weights(pmux_t *m);

/* Advance one phase tick. Accrues credit by weight and returns the
 * index of the sub dispatched this tick, or -1 if none was due.
 * Deterministic for a given sequence of ticks — no clock is read. */
int32_t pmux_phase_tick(pmux_t *m);

/* Set the focused sub (what the user sees). */
bool pmux_set_focus(pmux_t *m, uint32_t idx);

#endif /* ZXV_PTERM_MUX_H */
