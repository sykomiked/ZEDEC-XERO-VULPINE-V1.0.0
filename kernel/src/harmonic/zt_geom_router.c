/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_geom_router.c — see zt_geom_router.h. */
#include "zt_geom_router.h"

void zt_geom_router_init(zt_geom_router_t *r)
{
    for (uint8_t i = 0; i < ZT_ROUTER_NODES; i++) {
        r->nodes[i].id = i;
        r->nodes[i].x = (uint8_t) (i % 3u);
        r->nodes[i].y = (uint8_t) (i / 3u);
        r->queue_occupancy[i] = 0;
        r->queue_capacity[i] = ZT_ROUTER_DEFAULT_CAP;
    }
    r->velocity_q16 = ZT_DIST_ORTHO_Q16;
    r->clock_tick = 0;
    r->ordinal = 0;
    r->dispatched = r->refused = 0;
}

static uint32_t wrap_delta(uint8_t a, uint8_t b)
{
    uint32_t d = a > b ? (uint32_t) (a - b) : (uint32_t) (b - a);
    return d > 1u ? 3u - d : d;
}

zt_edge_trajectory_t zt_geom_calculate_trajectory(const zt_geom_router_t *r, uint8_t src,
                                                  uint8_t dst, uint32_t vel)
{
    zt_edge_trajectory_t t = {src, dst, 0, 0};
    if (src >= ZT_ROUTER_NODES || dst >= ZT_ROUTER_NODES) {
        t.transit_ticks = UINT32_MAX;
        return t;
    }
    uint32_t dx = wrap_delta(r->nodes[src].x, r->nodes[dst].x);
    uint32_t dy = wrap_delta(r->nodes[src].y, r->nodes[dst].y);
    t.distance_q16 = dx && dy ? ZT_DIST_DIAG_Q16 : (dx || dy) ? ZT_DIST_ORTHO_Q16 : 0u;
    if (vel == 0) {
        t.transit_ticks = UINT32_MAX;
        return t;
    }
    uint32_t q = t.distance_q16 / vel + (t.distance_q16 % vel != 0u); /* G3: ceiling */
    t.transit_ticks = q ? q : 1u;
    return t;
}

bool zt_geom_dispatch_trace(zt_geom_router_t *r, const uint8_t *seq, size_t len,
                            const zt_ubh168_frame_t *frame)
{
    uint32_t visits[ZT_ROUTER_NODES];
    for (uint32_t n = 0; n < ZT_ROUTER_NODES; n++) visits[n] = 0; /* no memset call */
    uint64_t ticks = 0;
    bool ok = r && seq && frame && len >= 1u && len <= ZT_ROUTER_MAX_TRACE && r->velocity_q16;
    for (size_t k = 0; ok && k < len; k++) {
        if (seq[k] >= ZT_ROUTER_NODES) {
            ok = false;
            break;
        }
        visits[seq[k]]++;
        if (k > 0) {
            if (seq[k] == seq[k - 1]) { /* a hop must move */
                ok = false;
                break;
            }
            ticks +=
                zt_geom_calculate_trajectory(r, seq[k - 1], seq[k], r->velocity_q16).transit_ticks;
        }
    }
    for (uint32_t n = 0; ok && n < ZT_ROUTER_NODES; n++)
        if (visits[n] > r->queue_capacity[n] ||
            r->queue_occupancy[n] > r->queue_capacity[n] - visits[n])
            ok = false;
    if (!ok) {
        if (r) r->refused++;
        return false;
    }
    for (uint32_t n = 0; n < ZT_ROUTER_NODES; n++) r->queue_occupancy[n] += visits[n];
    if (ticks == 0) ticks = 1; /* a single-node trace still advances the ordinal */
    r->ordinal += ticks;
    r->clock_tick += (uint32_t) ticks;
    r->dispatched++;
    return true;
}

void zt_geom_release(zt_geom_router_t *r, uint8_t node)
{
    if (node < ZT_ROUTER_NODES && r->queue_occupancy[node]) r->queue_occupancy[node]--;
}

size_t zt_geom_stride_trace(uint8_t start, uint8_t *out, size_t len)
{
    uint32_t n = start % 9u;
    for (size_t k = 0; k < len; k++) {
        out[k] = (uint8_t) n;
        n = (n * 2u) % 9u;
    }
    return len;
}
