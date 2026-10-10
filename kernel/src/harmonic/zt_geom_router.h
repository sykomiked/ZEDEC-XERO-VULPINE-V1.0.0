/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_geom_router.h — nine queues on a 3x3 torus, with transit times from
 * fixed-point distance and dispatch along fixed stride traces.
 *
 *   G1  NODES.  id 0..8 at (x, y) = (id mod 3, id / 3). Offsets wrap on the
 *       torus: dx = min(|x2 - x1|, 3 - |x2 - x1|), dy likewise, so dx, dy are
 *       0 or 1 and every node is one hop from every other: the graph is K9,
 *       the complete graph on nine nodes (non-planar).
 *   G2  DISTANCE (Q16.16): 0 (same node), 0x10000 (orthogonal),
 *       0x16A0A (diagonal, sqrt 2 = 1.41421, error < 2^-16).
 *   G3  TRANSIT TICKS = max(1, ceil(distance / velocity)) with 32-bit integer
 *       division. The spec's floor division gives a diagonal at velocity 1.0
 *       1 tick, the same as an orthogonal hop, which contradicts its own
 *       "diagonal strictly greater" test; ceiling gives 2 and 1. Velocity 0
 *       gives UINT32_MAX ticks (never arrives) and dispatch refuses it.
 *       Ticks are whole numbers, so a diagonal is strictly slower only when
 *       the two ceilings differ: at velocity 1, 1/2, 1/4, 1/8 it is (2 vs 1,
 *       3 vs 2, 6 vs 4, 12 vs 8); at 3/4 both take 2 and at >= sqrt 2 both 1.
 *   G4  STRIDE TRACES.  The doubling walk n -> 2n mod 9: from 1 it visits
 *       1 2 4 8 7 5 and repeats; from 3 or 6 it alternates 3 6; from 0 it
 *       stays at 0. zt_geom_stride_trace writes such a walk.
 *   G5  DISPATCH is all-or-nothing: every hop must move to a different node,
 *       every node must be < 9, and every node's queue must have room for the
 *       visits the trace makes to it (capacity per node, default 8). If any
 *       check fails, nothing changes and false is returned; the frame is const
 *       and stays with the caller. On success each visited queue grows by its
 *       visit count and the router's ordinal (oseq-style, monotonic) advances
 *       by the sum of the hops' transit ticks, at least 1 per hop, so the
 *       ordinal strictly increases with every dispatch.
 */
#ifndef ZT_GEOM_ROUTER_H
#define ZT_GEOM_ROUTER_H

#include "zt_harmonic_wire.h"

#define ZT_ROUTER_NODES       9u
#define ZT_DIST_ORTHO_Q16     0x00010000u
#define ZT_DIST_DIAG_Q16      0x00016A0Au
#define ZT_ROUTER_DEFAULT_CAP 8u
#define ZT_ROUTER_MAX_TRACE   64u

typedef struct {
    uint8_t id, x, y;
} zt_geom_node_t;

typedef struct {
    uint8_t src_node, dst_node;
    uint32_t distance_q16;
    uint32_t transit_ticks;
} zt_edge_trajectory_t;

typedef struct {
    zt_geom_node_t nodes[ZT_ROUTER_NODES];
    uint32_t queue_occupancy[ZT_ROUTER_NODES];
    uint32_t queue_capacity[ZT_ROUTER_NODES];
    uint32_t velocity_q16; /* default 0x10000 */
    uint32_t clock_tick;   /* transit ticks spent, wraps */
    uint64_t ordinal;      /* G5: monotonic */
    uint32_t dispatched, refused;
} zt_geom_router_t;

void zt_geom_router_init(zt_geom_router_t *router);
zt_edge_trajectory_t zt_geom_calculate_trajectory(const zt_geom_router_t *router, uint8_t src_node,
                                                  uint8_t dst_node, uint32_t velocity_budget_q16);
bool zt_geom_dispatch_trace(zt_geom_router_t *router, const uint8_t *node_sequence,
                            size_t sequence_len, const zt_ubh168_frame_t *frame);
/* Release one queued frame at a node (occupancy floor 0). */
void zt_geom_release(zt_geom_router_t *router, uint8_t node);
/* G4: write len nodes of the doubling walk from start (mod 9); returns len. */
size_t zt_geom_stride_trace(uint8_t start, uint8_t *out, size_t len);

#endif /* ZT_GEOM_ROUTER_H */
