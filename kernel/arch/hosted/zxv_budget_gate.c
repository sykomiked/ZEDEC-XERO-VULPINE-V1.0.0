/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxv_budget_gate.c — see zxv_budget_gate.h (B1-B4). */
#include <string.h>

#include "zxv_budget_gate.h"

int32_t zxv_budget_gate(swarm_budget_t *b, uint32_t model_id, uint32_t requested, zxv_gen_fn fn,
                        void *ctx, zxv_gate_t *out)
{
    zxv_gate_t g;
    memset(&g, 0, sizeof g);
    g.requested = requested;
    if (!b || !fn) {
        if (out) *out = g;
        return -1;
    }
    g.remaining_before = swarm_budget_remaining(b, model_id); /* B1 */
    g.remaining_after = g.remaining_before;
    if (g.remaining_before == 0 || requested == 0) { /* B4 */
        g.exhausted = g.remaining_before == 0;
        if (out) *out = g;
        return g.exhausted ? ZXV_GATE_EXHAUSTED : 0;
    }
    g.max_new = g.remaining_before < requested ? (uint32_t) g.remaining_before : requested; /* B2 */
    int32_t n = fn(ctx, g.max_new);
    if (n < 0) {
        if (out) *out = g;
        return n;
    }
    if ((uint32_t) n > g.max_new) n = (int32_t) g.max_new; /* never charge past the grant */
    g.generated = (uint32_t) n;
    uint64_t granted = 0;
    if (swarm_budget_consume(b, model_id, g.generated, &granted) == SWARM_OK) /* B3 */
        g.charged = granted;
    g.remaining_after = swarm_budget_remaining(b, model_id);
    if (out) *out = g;
    return n;
}
