/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_delta.h — the L2 state bus's payload: one state transition against one
 * state cell, carried as the data[] of a VNA_SPACE_EV_DELTA space event
 * (vna_space.h). The space layer already gives it the signed record, the
 * Lamport field, gossip fan-out and once-only delivery; this file gives the
 * bytes their meaning and the page their merge semantics.
 *
 * Canonical packed bytes, little-endian (vna_common.h codec):
 *
 *   "ZD2"        3 bytes   magic
 *   parent      32 bytes   vna_htag("vinea/v2/delta") digest of the previous
 *                          delta its author applied to this cell; zero when
 *                          the author has applied none. The chain of parents
 *                          is the bus's Merkle clock: the event's Lamport
 *                          field orders arrivals, parents name ancestry.
 *   cell         2 bytes   the state cell the transition addresses
 *   lattice      1 byte    an lpres_state_t claim about the cell
 *   len          2 bytes   payload length (0..VNA_DELTA_PAYLOAD_MAX)
 *   payload     len bytes  cell data, opaque to this layer
 *
 * Merge semantics. Applying a delta never replaces a cell's claim: it joins
 * through lpres_kjoin (src/lpres — the evidence join on the knowledge
 * order), so two nodes asserting opposite values of one cell yield BOTH,
 * kept until later evidence resolves it — a collision is absorbed by the
 * lattice instead of forking the bus or freezing on consensus. A delta
 * whose parent does not match the cell's head is still applied (its claim
 * joins like any other's), but the cell's forks count rises: divergent
 * ancestry is evidence, visible to the engine, not an error.
 *
 * Like every Vinea file: a pure state machine. No sockets, no clock, no
 * allocation — the caller owns the page and passes the Lamport field in.
 */
#ifndef VNA_DELTA_H
#define VNA_DELTA_H

#include "vna_common.h"
#include "lpres.h"

#define VNA_DELTA_MAGIC0      'Z'
#define VNA_DELTA_MAGIC1      'D'
#define VNA_DELTA_MAGIC2      '2'
#define VNA_DELTA_HDR         (3u + 32u + 2u + 1u + 2u)
#define VNA_DELTA_PAYLOAD_MAX 512u /* leaves headroom inside a 1 KiB event */
#define VNA_DELTA_CELLS       64u  /* state cells a page holds, bounded */

typedef struct {
    uint8_t parent[VNA_HASH_LEN]; /* digest of the author's previous delta, or zero */
    uint16_t cell;                /* the state cell it addresses */
    uint8_t lattice;              /* lpres_state_t: the claim it makes */
    uint16_t len;
    const uint8_t *payload; /* points into the parsed buffer; never copied */
} vna_delta_t;

/* One cell of the state page: the running conjoin of every delta applied
 * to it, the digest of the last one, the highest Lamport tick seen, and a
 * fork count (deltas applied whose parent was not the head). */
typedef struct {
    uint16_t tag;
    uint8_t lattice; /* lpres_state_t, NEITHER until the first delta */
    uint8_t used;
    uint64_t tick;
    uint8_t head[VNA_HASH_LEN];
    uint32_t napp;
    uint32_t forks;
} vna_delta_cell_t;

typedef struct {
    vna_delta_cell_t cell[VNA_DELTA_CELLS];
    uint32_t ncell;
    uint64_t clock; /* highest Lamport tick the page has seen */
} vna_delta_page_t;

/* ---- the bytes ---- */
/* Encode `d` to `out` (cap >= VNA_DELTA_HDR + d->len), returning the length
 * or VNA_ERR_ARG. */
int vna_delta_encode(const vna_delta_t *d, uint8_t *out, uint32_t cap);
/* Parse `buf` into `d` (d->payload aliases buf), VNA_OK or VNA_ERR_PARSE. */
int vna_delta_parse(const uint8_t *buf, uint32_t len, vna_delta_t *d);
/* The digest a parent field names: vna_htag("vinea/v2/delta", buf). */
void vna_delta_digest(const uint8_t *buf, uint32_t len, uint8_t out[VNA_HASH_LEN]);

/* ---- the page ---- */
void vna_delta_page_init(vna_delta_page_t *p);
/* The cell `tag` holds, or NULL if it has seen no delta yet. */
const vna_delta_cell_t *vna_delta_cell(const vna_delta_page_t *p, uint16_t tag);
/* Apply one packed delta at Lamport tick `tick`: parse, join the claim
 * into the cell, advance the head and clock. Returns the cell's lattice
 * state afterwards (an lpres_state_t), or a negative vna_status_t:
 * VNA_ERR_PARSE on malformed bytes, VNA_ERR_SPACE when the page is full of
 * cells other than this one. */
int vna_delta_apply(vna_delta_page_t *p, const uint8_t *buf, uint32_t len, uint64_t tick);
/* Issue this node's next delta on `cell`: parent is the cell's head (or
 * zero), the page's clock advances one tick, and the delta is applied — a
 * node always sees its own write. `out` gets the canonical bytes to carry
 * in the event; returns the length or a negative vna_status_t. */
int vna_delta_issue(vna_delta_page_t *p, uint16_t cell, lpres_state_t lattice,
                    const uint8_t *payload, uint16_t n, uint8_t *out, uint32_t cap, uint64_t *tick);

#endif /* VNA_DELTA_H */
