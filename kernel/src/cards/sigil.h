/* sigil.h — the spell shape as a circuit diagram
 *
 * WHAT THE SHAPE ACTUALLY IS
 * --------------------------
 * The figure at the top of a Glyph & Grid card, under the spell word, is not
 * ornament and it is not an identifier. It is the process. Every card carries
 * TWO graphs drawn on top of one another, and measurement of a real card
 * (seal_24525, OLPIRT HPOU) confirms both:
 *
 *   THE BLACK STAR — the fabric.
 *     A star polygon {N/k}: N points evenly spaced on a circle, each joined
 *     to the point k steps away. That figure IS the circulant graph
 *     C(N,{k}) — a chordal ring, which is a real interconnect topology, not
 *     an analogy for one. It says which nodes can reach which, and in how
 *     many hops.
 *
 *   THE RED TRACE — the circuit.
 *     A polyline over a square lattice (a kamea), exactly as a name is
 *     traced through a numbered grid in ritual practice. Its vertices are
 *     the operations and its strokes are the data paths.
 *
 * WHY THE OCCULT CONSTRUCTION AND THE ARCHITECTURE ARE THE SAME OBJECT
 * -------------------------------------------------------------------
 * Sigil practice draws a path through a lattice; a dataflow circuit is a
 * path through a lattice. The overlap is not poetic — the number theory of
 * the star polygon carries real engineering:
 *
 *     gcd(N,k) = 1  ->  the star is UNICURSAL: one closed circuit through
 *                       every node. A single serialized dependency chain.
 *     gcd(N,k) = g  ->  the figure falls apart into g disjoint orbits.
 *                       Those are g INDEPENDENT PARALLEL LANES.
 *
 * So how many lanes of parallelism a card's process has is decided by the
 * arithmetic of the shape drawn on it. {12/5} is one lane; {12/4} is four.
 * A practitioner choosing a star for its symmetry is choosing a scheduling
 * policy, and both readings are correct at once.
 *
 * EVENT SEQUENCE, NOT CLOCK
 * -------------------------
 * The trace is a drawing, so stroke ORDER is not reliably recoverable from a
 * raster — collinear strokes lose their ordering the moment they are inked.
 * This module therefore never pretends to recover it. It recovers the GRAPH,
 * and execution order comes from data dependencies: nodes fire when their
 * inputs are ready. Nodes in one wave are concurrent; the wave count is the
 * critical path. That is ZXV's event-sequence model, and it is the reason
 * the ambiguity does not matter here — a clock-driven design would have
 * needed the stroke order this image cannot supply.
 *
 * Freestanding: integer only, no libc, no allocation, no float.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV sigil-circuit slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ZXV_SIGIL_H
#define ZXV_SIGIL_H

#include <stdint.h>
#include <stdbool.h>

#define SIG_MAX_NODES  32u
#define SIG_MAX_EDGES  64u
#define SIG_MAX_WAVES  SIG_MAX_NODES

typedef struct { uint8_t col, row; } sig_pt_t;
typedef struct { uint8_t a, b; } sig_edge_t;

typedef struct {
    uint32_t   card_index;

    /* fabric — the black star polygon {N/k} */
    uint8_t    fab_n;          /* points on the circle */
    uint8_t    fab_k;          /* chord step */

    /* circuit — the red trace over the kamea */
    uint8_t    cols, rows;     /* lattice extent */
    uint8_t    pitch;          /* lattice spacing, source pixels */
    uint8_t    n_nodes, n_edges;
    sig_pt_t   node[SIG_MAX_NODES];
    sig_edge_t edge[SIG_MAX_EDGES];
} sigil_t;

/* A schedule: nodes grouped into waves. Everything in one wave is
 * concurrent; the wave count is the critical path. */
typedef struct {
    uint8_t  order[SIG_MAX_NODES];   /* nodes, grouped wave by wave */
    uint8_t  wave_start[SIG_MAX_WAVES + 1];
    uint8_t  n_waves;
    uint8_t  n_scheduled;
    uint8_t  width;                  /* widest wave = peak concurrency */
} sig_schedule_t;

void     sig_init(sigil_t *s, uint32_t card_index);
bool     sig_add_node(sigil_t *s, uint8_t col, uint8_t row);
bool     sig_add_edge(sigil_t *s, uint8_t a, uint8_t b);
int32_t  sig_find_node(const sigil_t *s, uint8_t col, uint8_t row);

uint32_t sig_gcd(uint32_t a, uint32_t b);

/* ---- fabric properties, straight out of the star's arithmetic ---- */

/* Independent parallel lanes the fabric provides: gcd(N,k). */
uint32_t sig_fabric_lanes(const sigil_t *s);
/* One closed circuit through every node? (gcd == 1) */
bool     sig_fabric_unicursal(const sigil_t *s);
/* Nodes reachable in one hop from any node, counting both directions. */
uint32_t sig_fabric_degree(const sigil_t *s);
/* Worst-case hops between two nodes on the same lane. */
uint32_t sig_fabric_diameter(const sigil_t *s);

/* ---- circuit properties ---- */
uint32_t sig_degree(const sigil_t *s, uint8_t node);
/* Connected components of the traced circuit. */
uint32_t sig_components(const sigil_t *s);
/* Vertices of odd degree — the trail's loose ends. */
uint32_t sig_odd_vertices(const sigil_t *s);
/* Canonical entry: lowest-indexed degree-1 node, else node 0. Deterministic
 * so every device schedules an identical card identically. */
int32_t  sig_entry(const sigil_t *s);

/* Build the event-sequence schedule by dependency waves from `entry`.
 * Pass entry < 0 to use sig_entry(). Returns false only on bad input. */
bool     sig_schedule(const sigil_t *s, int32_t entry, sig_schedule_t *out);

#endif /* ZXV_SIGIL_H */
