/* phase_render.h — the phase-tick render hook: visuals locked to the phase
 * coordinator, not to wall clock.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * THE TICK, NOT THE TIMER. A mirror-UI has no VSync, no async event loop,
 * no thread-scheduled repaint: the render pipeline advances ONLY when the
 * phase coordinator ticks (phase_coordinator_current_tick / pc_advance_time),
 * so a visual frame is produced atomically with the system state transition
 * that caused it. Identical system inputs and phase ordinals produce
 * identical frames — full reproducibility.
 *
 * DUAL SPACE. The megalithic kernel mirrors state symmetrically to the human
 * space (zui / pterm renderers) and the Chiglet space (the swarm's inference
 * readers). Both consume the SAME mirror frame at the SAME tick: equal
 * privilege, zero-copy, no wall-clock drift between the two mirrors.
 *
 * BOUNDED. Hooks are a static table (PHASE_RENDER_MAX_HOOKS); the mirror
 * frame is bounded by MIRROR_MAX_REGS. Nothing allocates.
 */
#ifndef ZXV_PHASE_RENDER_H
#define ZXV_PHASE_RENDER_H

#include <stdint.h>
#include <stdbool.h>

#include "phase_coord.h" /* pc_registry_t — the phase tick source          */
#include "mirror_ui.h"   /* mirror_frame_t — the projected state           */

/* --gc-sections KEEP — see mirror_ui.h: no boot caller yet, retain the API. */
#if defined(__GNUC__) && (__GNUC__ >= 11)
#  define PR_KEEP __attribute__((used, retain))
#else
#  define PR_KEEP __attribute__((used))
#endif

#define PHASE_RENDER_MAX_HOOKS 16u

/* Which space a hook mirrors. The human, Chiglet and network hooks are peers:
 * all are invoked with the identical frame at the identical tick. */
typedef enum {
    PHASE_SPACE_HUMAN = 0,   /* zui / pterm renderers                        */
    PHASE_SPACE_CHIGLET = 1, /* the swarm's inference readers                */
    PHASE_SPACE_NETWORK = 2, /* the mesh's consensus + topology consumers    */
    PHASE_SPACE_COUNT
} phase_space_t;

/* One render hook: called when the coordinator advances to a tick the hook
 * accepts. `frame` is the mirror projection for that tick — shared, never
 * copied, valid for the duration of the call. */
typedef struct {
    phase_space_t space;
    uint32_t phase_id; /* PC_PHASE_* or 0 for every phase           */
    void (*fn)(uint64_t tick, const mirror_frame_t *frame, void *ctx);
    void *ctx;
    bool active;
} phase_render_hook_t;

typedef struct {
    phase_render_hook_t hooks[PHASE_RENDER_MAX_HOOKS];
    uint32_t hook_count;
    mirror_ui_t mirror;        /* the SASOS projection source           */
    mirror_frame_t last_frame; /* the frame of the last tick            */
    uint64_t last_tick;
    bool dirty; /* a tick arrived since the last render  */
} phase_render_t;

/* ---- API ------------------------------------------------------------------ */

void phase_render_init(phase_render_t *pr);

/* Register a render hook. Returns the hook index, or -1 if the table is
 * full or fn is NULL. */
int phase_render_add_hook(phase_render_t *pr, phase_space_t space, uint32_t phase_id,
                          void (*fn)(uint64_t tick, const mirror_frame_t *frame, void *ctx),
                          void *ctx);

/* Advance the render pipeline to `tick`. The mirror is projected at this
 * tick and every hook that accepts the phase fires with the shared frame.
 * Deterministic: the frame is a pure function of (mirror, tick), and the
 * hooks fire in registration order. */
void phase_render_tick(phase_render_t *pr, uint64_t tick);

/* The triadic tick: project once, then drive the human hooks, the Chiglet
 * hooks AND the network hooks from the same frame — three spaces, one
 * projection, one phase ordinal, no drift. Equivalent to phase_render_tick
 * but explicit about the triadic symmetry. */
void phase_render_dual_tick(phase_render_t *pr, uint64_t tick);

/* The frame of the last tick (borrowed; valid until the next tick). */
const mirror_frame_t *phase_render_last_frame(const phase_render_t *pr);

#endif /* ZXV_PHASE_RENDER_H */