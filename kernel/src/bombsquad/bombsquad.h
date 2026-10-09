/* bombsquad.h — preemptive debugging for a NONLINEAR system
 *
 * THE PREMISE
 * -----------
 * In a linear system a fault is an event: it happens, the thing crashes, and
 * you debug where it stopped. In this system a fault is the END of a process
 * that started earlier — a bug is lit, burns for some number of phase ticks,
 * and only then detonates. Debugging where it stopped tells you where the bomb
 * was, not where the fuse was lit, and by then correction is no longer possible.
 *
 * The consequence that makes this worth building: BETWEEN LIT AND DETONATED,
 * the system is still correctable. A linear watchdog has nothing to do until
 * the crash; a bomb squad has the whole burn to work with.
 *
 * WHAT IS ACTUALLY MEASURED
 * -------------------------
 * An invariant here is not a boolean. It reports a MARGIN: how much room is
 * left before it is violated. A boolean can only tell you "fine" until the
 * instant it says "dead"; a margin tells you which way you are moving and how
 * fast. Every registered invariant is sampled each phase tick, and the squad
 * tracks the margin's TREND:
 *
 *     SAFE      margin is healthy and not falling
 *     WATCH     margin is falling but detonation is far off
 *     ARMED     margin is falling and will reach zero within the horizon
 *               -- this is the fuse, visible, with an estimated tick count
 *     BREACHED  margin is gone; it has already gone off
 *
 * The estimate is a linear extrapolation of a measured slope, and that is a
 * HEURISTIC, not a prediction. Two honest limits, stated here so no one quotes
 * this as more than it is:
 *   - it cannot see a fault with no run-up. Something that goes from healthy to
 *     violated in a single tick has no fuse to observe, and no amount of trend
 *     analysis invents one.
 *   - a margin that falls and then recovers will raise ARMED and then stand
 *     down. That is a false positive by a strict reading, and it is the correct
 *     trade: the cost of looking early is a look, and the cost of looking late
 *     is the fault.
 * What it does buy is the thing linear debugging cannot: TIME, and a named
 * invariant to spend it on.
 *
 * DEFUSAL
 * -------
 * An ARMED invariant may carry a defuse callback. The squad calls it while the
 * margin is still positive — the correction window a nonlinear system has and a
 * linear one does not. A defusal that works shows up as the margin recovering,
 * which the next sample confirms; the squad does not take the callback's word
 * for it. Defusals are counted separately from detonations, because "we caught
 * it in time" and "it never happened" are different facts.
 *
 * INTEGRATION, NOT A SEPARATE SYSTEM
 * ----------------------------------
 * The squad's clock IS the phase tick (phase_coord/phase_coordinator.h): it
 * samples on the tick the rest of the kernel already runs on, so fuse lengths
 * are quoted in the system's own time unit rather than in wall-clock seconds
 * that mean nothing to a phase-driven design. Fuse length in ticks is directly
 * comparable to the lag distances the storage fuzzer reports.
 *
 * Freestanding: integer only, no libc, no allocation, no float.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV bomb-squad slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_BOMBSQUAD_H
#define ZXV_BOMBSQUAD_H

#include <stdint.h>
#include <stdbool.h>

#define BS_MAX_WATCH    16u   /* invariants under watch                    */
#define BS_HISTORY      8u    /* samples kept per invariant (the burn rate) */
#define BS_NAME_LEN     24u
#define BS_DEFAULT_HORIZON 32u /* ticks ahead we consider "soon"            */

typedef enum {
    BS_SAFE = 0,   /* margin healthy, not falling                  */
    BS_WATCH,      /* falling, but detonation beyond the horizon   */
    BS_ARMED,      /* falling, reaches zero within the horizon     */
    BS_BREACHED    /* margin gone: it has already gone off         */
} bs_state_t;

/* Report the remaining margin for an invariant. Larger is safer; 0 or below
 * means violated. Must be cheap: this runs every phase tick. */
typedef int32_t (*bs_margin_fn)(void *ctx);

/* Attempt to correct an ARMED invariant while there is still margin. Return 0
 * if the correction was applied. Whether it WORKED is decided by the next
 * sample, not by this return value. */
typedef int (*bs_defuse_fn)(void *ctx);

typedef struct {
    char         name[BS_NAME_LEN];
    bs_margin_fn margin;
    bs_defuse_fn defuse;        /* optional; NULL = observe only */
    void        *ctx;
    int32_t      history[BS_HISTORY];
    uint32_t     samples;
    bs_state_t   state;
    int32_t      last_margin;
    int32_t      slope;         /* margin change per tick (negative = burning) */
    uint32_t     ticks_to_zero; /* estimate when ARMED; 0 otherwise           */
    uint32_t     armed_count;   /* times it reached ARMED                     */
    uint32_t     defused_count; /* times a defusal was attempted              */
    uint32_t     breached_count;
    bool         used;
} bs_watch_t;

typedef void (*bs_report_fn)(uint32_t idx, const char *name,
                             bs_state_t from, bs_state_t to,
                             int32_t margin, uint32_t ticks_to_zero);

typedef struct {
    bs_watch_t watch[BS_MAX_WATCH];
    uint32_t   nwatch;
    uint64_t   tick;            /* phase ticks observed                       */
    uint32_t   horizon;         /* ticks ahead that count as "soon"           */
    uint32_t   total_armed;
    uint32_t   total_defused;
    uint32_t   total_breached;
    bs_report_fn report;   /* optional; NULL = silent */
} bs_squad_t;

/* Optional TRANSITION REPORTER. bombsquad itself stays pure -- no console
 * dependency, freestanding, testable on the host -- so observation is injected
 * rather than baked in. Called ONLY when an invariant CHANGES state, never per
 * tick, so a long-burning fuse produces one line and not a flood.
 *
 * This is what makes the fuse visible to an external harness: gamedrive.py
 * reads [WATCH]/[ARMED]/[BREACHED] out of the boot log to measure fuse LENGTH
 * -- the distance between first degradation and detonation. Without it every
 * run looks clean, because there is nothing to see. */
void bs_set_reporter(bs_squad_t *s, bs_report_fn fn);

void bs_init(bs_squad_t *s, uint32_t horizon /*0 = default*/);

/* Register an invariant. `defuse` may be NULL to watch without correcting.
 * Returns the watch index, or <0. */
int  bs_watch(bs_squad_t *s, const char *name, bs_margin_fn margin,
              bs_defuse_fn defuse, void *ctx);

/* Sample every invariant. Call this on the phase tick. Defuses anything that
 * is ARMED and carries a defuse callback. Returns the number ARMED this tick. */
int  bs_tick(bs_squad_t *s);

/* State and estimated fuse length (ticks to detonation; 0 if not ARMED). */
bs_state_t bs_state(const bs_squad_t *s, uint32_t idx, uint32_t *ticks_out);

/* The most urgent invariant right now, or <0 if nothing is ARMED. */
int  bs_most_urgent(const bs_squad_t *s);

const char *bs_state_name(bs_state_t st);

#endif /* ZXV_BOMBSQUAD_H */
