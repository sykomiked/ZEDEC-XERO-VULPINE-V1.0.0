/* lpres_adapter.h — the logic adapters: the bridge between the ordinary
 * binary state (0/1, the world's boolean) and the 144-dimensional
 * paraconsistent state (the lattice). Every legacy interface speaks
 * binary; the system speaks four-valued over the 144-lattice. The
 * adapters are the only place the two meet, so the conversion is exact,
 * total, and never silently loses a value.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZXV_LPRES_ADAPTER_H
#define ZXV_LPRES_ADAPTER_H

#include <stdint.h>

#include "lpres.h"

/* The lattice point: 18 bytes = the 144 bits of the ladder, one bit per
 * dimension. The same shape the SCP-144 point uses, so the logic and the
 * security share one representation. */
#define LPRES_POINT_BYTES 18

/* ---- the binary <-> lpres adapters ------------------------------------
 * The binary world has two states; the lattice has four. The ascent is
 * exact (0 -> FALSE, 1 -> TRUE); the descent is guarded so that a
 * non-TRUE value (BOTH, NEITHER, or any phase) NEVER becomes 1 — the
 * never-authorise guard, enforced at the adapter, so no legacy caller can
 * be handed authority the lattice did not grant. */
lpres_state_t lpres_from_binary(int b);          /* 0 -> FALSE, 1 -> TRUE */
int lpres_to_binary(lpres_state_t s);            /* TRUE -> 1, else 0   */

/* ---- the lpres <-> 144-dimensional point adapters ---------------------
 * A point is a Zeckendorf vector on the ladder: 144 bits, at most one of
 * any two consecutive set (the canonical rule), so a point has exactly
 * one representation. The ascent places the value at the rank its
 * dimension implies; the descent reads the point's rank back. */
void lpres_to_point(lpres_state_t s, uint8_t point[LPRES_POINT_BYTES]);
lpres_state_t lpres_from_point(const uint8_t point[LPRES_POINT_BYTES]);

/* ---- the total adapter: binary -> the lattice -> binary ---------------
 * The round trip a legacy caller performs. It is IDENTITY for 0 and 1
 * (the binary states survive the lattice), and it collapses every
 * non-TRUE lattice value to 0 — never to 1. */
int lpres_adapt_binary(int b);

/* ---- the phase adapter: any of the 144 phases -> the four values ------
 * A phase above the gateway is not one of the four; the adapter maps it
 * to the foundation value its rank implies (below NEITHER -> NEITHER,
 * between -> BOTH, at the top -> TRUE), so a caller that knows only the
 * four can still read a 144-dimensional state without error. */
lpres_state_t lpres_phase_to_foundation(lpres_state_t phase);

#endif /* ZXV_LPRES_ADAPTER_H */
