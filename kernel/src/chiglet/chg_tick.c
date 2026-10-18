/* chg_tick.c — the Fibonacci tick. See chg_tick.h.
 *
 * The ladder is the spine: F1..F12 = 1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89,
 * 144. The rung index walks it once, then holds at the anchor F12 = 144, so
 * a settled mesh reports sparsely and never storms.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "chg_tick.h"

#include <stddef.h>

/* The ladder, exact: F1..F12. */
static const uint32_t chg_fib[CHG_TICK_RUNGS] = {
    1u, 1u, 2u, 3u, 5u, 8u, 13u, 21u, 34u, 55u, 89u, 144u
};

uint32_t chg_tick_fib(uint32_t rung)
{
    if (rung == 0 || rung > CHG_TICK_RUNGS) return 0;
    return chg_fib[rung - 1u];
}

void chg_tick_init(chg_tick_t *t, const chg_tick_sink_t *sink, uint32_t base_ms)
{
    if (!t) return;
    for (uint32_t i = 0; i < (uint32_t) sizeof *t; i++)
        ((uint8_t *) t)[i] = 0;
    if (sink) t->sink = *sink;
    t->base_ms = base_ms ? base_ms : 1000u;
    t->rung = 1u;
    t->due_ms = (uint64_t) chg_fib[0] * t->base_ms;
}

bool chg_tick_due(chg_tick_t *t, uint64_t now_ms)
{
    if (!t) return false;
    if (now_ms < t->due_ms) return false;

    /* the rung fires: advance the ladder, and hold at the anchor */
    t->rungs_run++;
    if (t->rung < CHG_TICK_RUNGS) {
        t->rung++;
    }
    uint32_t f = chg_fib[t->rung - 1u];
    t->due_ms = now_ms + (uint64_t) f * t->base_ms;
    return true;
}

bool chg_tick_report(chg_tick_t *t, const ipfs_pin_progress_t *p, const chg_mesh_t *m)
{
    if (!t) return false;

    /* step the content index and the state plane for this rung */
    if (t->sink.cisex_step) t->cisex_last = t->sink.cisex_step(t->sink.ctx);
    if (t->sink.smic_step)  t->smic_last  = t->sink.smic_step(t->sink.ctx);

    chg_tick_report_t r;
    r.rung        = t->rung;
    r.interval_ms = chg_fib[t->rung - 1u] * t->base_ms;
    r.peers       = m ? m->peers : 0u;
    r.pinned      = p ? p->confirmed : 0u;
    r.unconfirmed = p ? p->unconfirmed : 0u;
    r.served      = m ? m->served : 0u;
    r.refused     = m ? m->refused : 0u;
    r.cisex       = t->cisex_last;
    r.smic        = t->smic_last;

    if (t->sink.report && t->sink.report(t->sink.ctx, &r)) {
        t->reports++;
        return true;
    }
    return false;
}
