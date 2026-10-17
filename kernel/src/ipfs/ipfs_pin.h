/* ipfs_pin.h — the recursive pinner: the release's content-addressed replica.
 *
 * Every seed holds the release pin store (docs/FLEET_STORAGE.md, store 2):
 * every CID of the release, the root, the discs, the world-computer files and
 * every shard, recursively, identical across the nine servers. This module is
 * the mechanism: it walks a CID set, fetches each object through the
 * self-certifying get (ipfs_get_verify recomputes the SHA-256 and refuses a
 * mismatch), confirms it, and pins it through ipfs_add_pin.
 *
 * The confirmation is bounded on purpose. ipfs_add_pin confirms a pin by
 * round-tripping the content, and objects larger than IPFS_PIN_CONFIRM_CAP
 * cannot be confirmed on-node -- so this module does NOT silently claim them.
 * It records each one in an explicit bounded list (the unconfirmed ring), and
 * the caller reports the counts. A pin claim that cannot be confirmed is
 * never made: that is the whole point of pinning by content address.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZXV_IPFS_PIN_H
#define ZXV_IPFS_PIN_H

#include <stdint.h>
#include <stdbool.h>
#include "ipfs.h"

/* The unconfirmed ring: bounded, so a release larger than the buffer cannot
 * grow memory; the COUNT is exact even when the list wraps. */
#ifndef IPFS_PIN_UNCONFIRMED_CAP
#define IPFS_PIN_UNCONFIRMED_CAP 64u
#endif

typedef struct {
    uint8_t  cid[IPFS_CID_LEN];
    uint32_t bytes;          /* the size the index declared (0 = unknown) */
    bool     is_shard;       /* a shard of a larger object, not a whole file */
} ipfs_pin_entry_t;

typedef struct {
    const ipfs_pin_entry_t *entries;
    uint32_t                count;
    uint32_t                index;      /* the next entry to process */
} ipfs_pin_set_t;

typedef struct {
    uint32_t confirmed;                  /* pinned and round-trip confirmed */
    uint32_t unconfirmed;                /* could not be confirmed on-node */
    uint32_t mismatched;                 /* fetched, but the CID did not match */
    uint32_t not_found;                  /* the network does not serve it yet */
    uint32_t no_transport;               /* no adapter bound: nothing claimed */
    uint8_t  unconfirmed_ring[IPFS_PIN_UNCONFIRMED_CAP][IPFS_CID_LEN];
    uint32_t unconfirmed_head;           /* the ring's next write slot */
    uint32_t unconfirmed_written;        /* total written (>= the ring size) */
} ipfs_pin_progress_t;

/* Reset the progress counters and the ring. */
void ipfs_pin_reset(ipfs_pin_progress_t *p);

/* Process ONE entry: fetch it through the self-certifying get, confirm it,
 * and pin it. Advances set->index. Returns the ipfs_result_t of the step so
 * a caller can pace on IPFS_ERR_NOT_FOUND (the peer may not serve it yet).
 * With no transport bound it records no_transport and claims nothing. */
ipfs_result_t ipfs_pin_step(ipfs_node_t *n, ipfs_pin_set_t *set,
                            ipfs_pin_progress_t *p, uint8_t *buf, uint32_t cap);

/* Walk the whole set, pacing up to `max_steps` entries. Returns the number of
 * entries processed (0 when the set is exhausted). */
uint32_t ipfs_pin_walk(ipfs_node_t *n, ipfs_pin_set_t *set,
                       ipfs_pin_progress_t *p, uint8_t *buf, uint32_t cap,
                       uint32_t max_steps);

/* True when the set is exhausted. */
bool ipfs_pin_done(const ipfs_pin_set_t *set);

#endif /* ZXV_IPFS_PIN_H */
