/* phase_render.c — the phase-tick render hook (see phase_render.h).
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * Freestanding: no libc, no float, no allocation. The render pipeline is a
 * pure function of (mirror registers, tick): the mirror is projected at the
 * tick, then the hooks fire in registration order with the shared frame.
 */
#include "phase_render.h"

/* freestanding zero-fill (no libc memset in kernel images). */
static void pr_zero(void *dst, uint32_t n)
{
    uint8_t *d = (uint8_t *) dst;
    for (uint32_t i = 0; i < n; i++) d[i] = 0;
}

PR_KEEP void phase_render_init(phase_render_t *pr)
{
    if (!pr) return;
    pr_zero(pr, (uint32_t) sizeof *pr);
    mirror_ui_init(&pr->mirror);
}

PR_KEEP int phase_render_add_hook(phase_render_t *pr, phase_space_t space, uint32_t phase_id,
                          void (*fn)(uint64_t tick, const mirror_frame_t *frame, void *ctx),
                          void *ctx)
{
    uint32_t i;
    if (!pr || !fn || space >= PHASE_SPACE_COUNT) return -1;
    if (pr->hook_count >= PHASE_RENDER_MAX_HOOKS) return -1;
    i = pr->hook_count;
    pr->hooks[i].space = space;
    pr->hooks[i].phase_id = phase_id;
    pr->hooks[i].fn = fn;
    pr->hooks[i].ctx = ctx;
    pr->hooks[i].active = true;
    pr->hook_count++;
    return (int) i;
}

static void fire_hooks(phase_render_t *pr, uint64_t tick, const mirror_frame_t *frame,
                       phase_space_t space)
{
    uint32_t i;
    for (i = 0; i < pr->hook_count; i++) {
        phase_render_hook_t *h = &pr->hooks[i];
        if (!h->active || h->space != space) continue;
        if (h->phase_id != 0u && h->phase_id != (uint32_t) (tick & 0x7FFFFFFFu))
            continue; /* the hook accepts only its phase's ticks */
        h->fn(tick, frame, h->ctx);
    }
}

PR_KEEP void phase_render_tick(phase_render_t *pr, uint64_t tick)
{
    if (!pr) return;
    if (tick == pr->last_tick) return; /* idempotent: one frame per tick */
    mirror_project(&pr->mirror, tick, &pr->last_frame);
    fire_hooks(pr, tick, &pr->last_frame, PHASE_SPACE_HUMAN);
    fire_hooks(pr, tick, &pr->last_frame, PHASE_SPACE_CHIGLET);
    fire_hooks(pr, tick, &pr->last_frame, PHASE_SPACE_NETWORK);
    pr->last_tick = tick;
    pr->dirty = true;
}

PR_KEEP void phase_render_dual_tick(phase_render_t *pr, uint64_t tick)
{
    phase_render_tick(pr, tick); /* the same projection drives both spaces */
}

PR_KEEP const mirror_frame_t *phase_render_last_frame(const phase_render_t *pr)
{
    if (!pr || pr->last_tick == 0u) return NULL;
    return &pr->last_frame;
}