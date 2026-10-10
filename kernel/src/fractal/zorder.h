/* zorder.h — fractal (self-similar) component addressing for ZXV
 *
 * WHAT THIS IS, HONESTLY
 * ---------------------
 * Giving components "fractal IDs" does not, by itself, make anything
 * faster. Geometry is not a source of free efficiency. What a
 * self-similar address space *does* give you is LOCALITY: a single
 * integer whose numeric closeness implies spatial closeness at every
 * scale. And locality is worth real energy, because in modern hardware
 * the dominant cost is moving data, not computing on it — typically by
 * an order of magnitude.
 *
 * So the mechanism this module provides is precise and measurable:
 *
 *   Z-order (Morton) addressing  ->  co-located work has near addresses
 *   near addresses               ->  shorter transfers between cells
 *   shorter transfers            ->  less data movement per event
 *
 * The claim we are entitled to make is "less data movement for the same
 * result, measured against a baseline placement" — never "more compute
 * from geometry". `test_zorder.c` measures exactly that against a
 * row-major baseline and reports the ratio; if the ratio is ever 1.0 the
 * benefit is absent and the tests say so.
 *
 * SELF-SIMILARITY
 * ---------------
 * A Morton code interleaves the bits of its coordinates, so truncating
 * it to the top 2k bits yields the address of the enclosing quadrant at
 * level k. That is the fractal property we exploit: the SAME routing and
 * scheduling logic applies at every level of the hierarchy — core,
 * cell, task, event — because a prefix of the address is itself a valid
 * address of the containing region. One verified algorithm, N scales.
 *
 * Integer-only, freestanding, no libc.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV fractal-locality slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_ZORDER_H
#define ZXV_ZORDER_H

#include <stdint.h>
#include <stdbool.h>

#define ZO_MAX_LEVEL   16          /* 16 levels => 32-bit 2D Morton code */
#define ZO_MAX_COORD   (1u << ZO_MAX_LEVEL)

/* A component's fractal address. `code` is the interleaved Morton value;
 * `level` says how many of the top bit-pairs are significant, which is
 * what makes a prefix a valid address of the enclosing region. */
typedef struct {
    uint32_t code;    /* Morton-interleaved coordinate */
    uint8_t  level;   /* 0..ZO_MAX_LEVEL; 0 = the whole space */
} zo_addr_t;

/* ---- encoding ---- */
uint32_t zo_encode2(uint16_t x, uint16_t y);        /* interleave -> Morton */
void     zo_decode2(uint32_t code, uint16_t *x, uint16_t *y);

/* Build an address at a given level of the hierarchy. */
zo_addr_t zo_make(uint16_t x, uint16_t y, uint8_t level);

/* The address of the enclosing region `up` levels above `a`.
 * This is the self-similarity operation: a prefix IS an address. */
zo_addr_t zo_parent(zo_addr_t a, uint8_t up);

/* True if `inner` lies inside the region named by `outer`. */
bool zo_contains(zo_addr_t outer, zo_addr_t inner);

/* The level of the smallest region containing BOTH addresses — i.e. how
 * much hierarchy two components share. Higher = closer together. */
uint8_t zo_common_level(zo_addr_t a, zo_addr_t b);

/* ---- distance metrics ----
 * `zo_hops` is the routing-cost proxy: the number of hierarchy levels a
 * message must climb and descend to get from a to b. Traffic between
 * siblings costs 2; traffic across the whole machine costs 2*level. This
 * is the quantity the placement policy minimises. */
uint32_t zo_hops(zo_addr_t a, zo_addr_t b);

/* Manhattan distance in decoded coordinates (a physical-distance proxy). */
uint32_t zo_manhattan(zo_addr_t a, zo_addr_t b);

/* ---- placement ----
 * Assign `count` components to slots so that components that communicate
 * often land in the same low-level regions. `affinity[i*count + j]` is
 * how much i talks to j. Writes a slot index per component into `slot`.
 * Returns the total weighted hop cost of the resulting placement. */
uint64_t zo_place(const uint16_t *affinity, uint32_t count,
                  uint8_t level, uint16_t *slot_x, uint16_t *slot_y);

/* Total weighted hop cost of an arbitrary placement — used to compare a
 * candidate against a baseline. */
uint64_t zo_cost(const uint16_t *affinity, uint32_t count,
                 const uint16_t *slot_x, const uint16_t *slot_y,
                 uint8_t level);

#endif /* ZXV_ZORDER_H */
