/* bombsquad.c — preemptive debugging. See bombsquad.h.
 *
 * Freestanding: integer only, no libc, no allocation, no float.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV bomb-squad slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "bombsquad.h"

static void bzero_(void *p, unsigned long n) {
    unsigned char *b = p;
    while (n--) *b++ = 0;
}

void bs_init(bs_squad_t *s, uint32_t horizon) {
    if (!s) return;
    bzero_(s, sizeof(*s));
    s->horizon = horizon ? horizon : BS_DEFAULT_HORIZON;
}

int bs_watch(bs_squad_t *s, const char *name, bs_margin_fn margin,
             bs_defuse_fn defuse, void *ctx) {
    if (!s || !name || !margin) return -1;
    if (s->nwatch >= BS_MAX_WATCH) return -2;
    bs_watch_t *w = &s->watch[s->nwatch];
    bzero_(w, sizeof(*w));
    uint32_t i = 0;
    while (name[i] && i < BS_NAME_LEN - 1) { w->name[i] = name[i]; i++; }
    w->name[i] = '\0';
    w->margin = margin;
    w->defuse = defuse;
    w->ctx = ctx;
    w->state = BS_SAFE;
    w->used = true;
    return (int)s->nwatch++;
}

/* Slope over the retained history, as margin change per tick. Uses the span
 * between the oldest and newest sample rather than the last two: a single tick
 * of jitter should not read as a fuse, and a slow steady burn should not be
 * missed because two adjacent samples happened to be equal. */
static int32_t slope_of(const bs_watch_t *w) {
    uint32_t n = w->samples < BS_HISTORY ? w->samples : BS_HISTORY;
    if (n < 2) return 0;
    /* history is a ring; index 0 is the oldest retained of the last n */
    uint32_t newest = (w->samples - 1) % BS_HISTORY;
    uint32_t oldest = (w->samples - n) % BS_HISTORY;
    int32_t d = w->history[newest] - w->history[oldest];
    int32_t span = (int32_t)(n - 1);
    return d / span;
}

int bs_tick(bs_squad_t *s) {
    if (!s) return -1;
    s->tick++;
    int armed_now = 0;

    for (uint32_t i = 0; i < s->nwatch; i++) {
        bs_watch_t *w = &s->watch[i];
        if (!w->used || !w->margin) continue;

        int32_t m = w->margin(w->ctx);
        w->history[w->samples % BS_HISTORY] = m;
        w->samples++;
        w->last_margin = m;
        w->slope = slope_of(w);
        w->ticks_to_zero = 0;

        if (m <= 0) {
            /* Already gone off. Counted once per transition INTO breach, not
             * once per tick, so a persistent breach does not inflate the count
             * and drown the signal. */
            if (w->state != BS_BREACHED) { w->breached_count++; s->total_breached++; }
            w->state = BS_BREACHED;
            continue;
        }

        if (w->slope >= 0) {            /* steady or recovering */
            w->state = BS_SAFE;
            continue;
        }

        /* Burning. How many ticks until the margin reaches zero at this rate?
         * Integer division truncates toward zero, which under-estimates the
         * remaining time -- the safe direction to be wrong in. */
        int32_t burn = -w->slope;
        uint32_t ttz = (uint32_t)(m / burn);
        w->ticks_to_zero = ttz;

        if (ttz <= s->horizon) {
            if (w->state != BS_ARMED) { w->armed_count++; s->total_armed++; }
            w->state = BS_ARMED;
            armed_now++;

            /* THE CORRECTION WINDOW: still positive margin, so act now. The
             * callback's return value is not treated as success -- only the
             * next sample's margin can show whether the fuse went out. */
            if (w->defuse) {
                if (w->defuse(w->ctx) == 0) {
                    w->defused_count++;
                    s->total_defused++;
                }
            }
        } else {
            w->state = BS_WATCH;
        }
    }
    return armed_now;
}

bs_state_t bs_state(const bs_squad_t *s, uint32_t idx, uint32_t *ticks_out) {
    if (!s || idx >= s->nwatch) { if (ticks_out) *ticks_out = 0; return BS_SAFE; }
    if (ticks_out) *ticks_out = s->watch[idx].ticks_to_zero;
    return s->watch[idx].state;
}

int bs_most_urgent(const bs_squad_t *s) {
    if (!s) return -1;
    int best = -1; uint32_t soonest = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < s->nwatch; i++) {
        const bs_watch_t *w = &s->watch[i];
        if (w->state != BS_ARMED) continue;
        if (w->ticks_to_zero < soonest) { soonest = w->ticks_to_zero; best = (int)i; }
    }
    return best;
}

const char *bs_state_name(bs_state_t st) {
    switch (st) {
        case BS_SAFE:     return "SAFE";
        case BS_WATCH:    return "WATCH";
        case BS_ARMED:    return "ARMED";
        case BS_BREACHED: return "BREACHED";
        default:          return "?";
    }
}
