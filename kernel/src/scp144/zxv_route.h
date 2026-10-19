/* zxv_route.h — the lattice interconnect table: which module occupies which
 * cell of the SCP-144 lattice, and which strata the active world lights.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * THE NON-LINEAR BUILD'S WIRING DIAGRAM. The strata map is projected onto
 * the lattice's partial order by build_system/gen_lattice_routing.py:
 * modules sort by (stratum, path) and fill the Fibonacci shells in ring
 * order, so a module's lattice cell encodes its build depth — the ring it
 * occupies is the build stage, and every geometric predecessor sits in a
 * strictly lower ring (acyclic by construction, the same invariant the
 * D-dimensional scaffold kernel proves on the GPU).
 *
 * THE LATTICE IS THE ROUTING; WHAT LIGHTS UP IS THE CONTEXT. The table
 * carries three world masks (server, user, foundry — the MegaROM's
 * BOOTSEL.TVL): bit k of a mask is live when STRATA_ORDER[k] is active in
 * that world. The lattice is fixed; the world picks which rings carry
 * current.
 *
 * WHAT THE KERNEL CAN PROVE. The module names live host-side (strata.txt),
 * so the selfcheck does not recompute the projection — it proves the
 * committed table's internal consistency and integrity: sha256 over the
 * image equals the appended digest, every cell is unique and inside the
 * lattice, every record's ring equals the ring its cell's max coordinate
 * actually falls in, and every dependency lands in a strictly lower ring.
 * Fail-closed, like the seed and the ROM.
 */
#ifndef ZXV_ROUTE_H
#define ZXV_ROUTE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ZXV_ROUTE_MAGIC0  'Z'
#define ZXV_ROUTE_MAGIC1  'L'
#define ZXV_ROUTE_MAGIC2  'R'
#define ZXV_ROUTE_MAGIC3  '0'
#define ZXV_ROUTE_VERSION 1u

#define ZXV_ROUTE_WORLDS 3u  /* server, user, foundry                       */
#define ZXV_ROUTE_STRATA 9u  /* base .. above, STRATA_ORDER in the generator */
#define ZXV_ROUTE_REC    10u /* bytes per module record                     */

/* the Fibonacci shell bounds (the distinct ladder), matching the scaffold
 * kernels: ring k holds cells with FIB[k-1] <= max(i,j) < FIB[k]. */
#define ZXV_ROUTE_RINGS 11u

typedef enum {
    ZXV_ROUTE_OK = 0,
    ZXV_ROUTE_E_LENGTH,  /* truncated or trailing bytes                  */
    ZXV_ROUTE_E_MAGIC,   /* not a ZLR0 image                             */
    ZXV_ROUTE_E_VERSION, /* unsupported table version                    */
    ZXV_ROUTE_E_COUNT,   /* stratum/module counts impossible             */
    ZXV_ROUTE_E_DIGEST,  /* sha256 over image != appended digest         */
    ZXV_ROUTE_E_CELL,    /* coordinate outside the lattice               */
    ZXV_ROUTE_E_RING,    /* record ring != the cell's actual ring        */
    ZXV_ROUTE_E_DUP,     /* two modules on one cell                      */
    ZXV_ROUTE_E_DEP,     /* a dependency sits at or above the cell's ring */
    ZXV_ROUTE_E_STRATUM, /* stratum index out of range                   */
    ZXV_ROUTE_E_WORLD,   /* a world mask lights a stratum that is absent */
} zxv_route_err_t;

/* Verify a committed routing-table image. Returns ZXV_ROUTE_OK only when
 * every check passes; on the first failure returns the error and fills
 * *bad_off with the offset of the offending record (or ~0u for header
 * faults). */
zxv_route_err_t zxv_route_verify(const uint8_t *img, size_t len, size_t *bad_off);

/* The ring a cell belongs to: the distinct-ladder shell its max coordinate
 * falls in. 1..ZXV_ROUTE_RINGS, 0 for out-of-lattice. */
uint8_t zxv_route_ring(uint8_t i, uint8_t j);

/* The world mask (12-bit field, STRATA_ORDER order) for world 0..2. */
uint16_t zxv_route_world_mask(const uint8_t *img, size_t len, uint8_t world, bool *ok);

/* Is the module record at off lit under this world? */
bool zxv_route_lit(const uint8_t *img, size_t len, size_t off, uint8_t world);

#endif /* ZXV_ROUTE_H */
