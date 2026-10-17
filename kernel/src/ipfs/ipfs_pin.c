/* ipfs_pin.c — the recursive pinner. See ipfs_pin.h.
 *
 * The order of operations is the repository's: check invariants and bounds,
 * reserve, compute, mutate, commit. Each entry is fetched through the
 * self-certifying get FIRST (so the bytes are proven against the CID before
 * anything is claimed), then pinned, then counted. Nothing is recorded as
 * confirmed that did not round-trip.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ipfs_pin.h"

#include <stddef.h>

static void pin_ring_push(ipfs_pin_progress_t *p, const uint8_t cid[IPFS_CID_LEN])
{
    uint8_t *slot = p->unconfirmed_ring[p->unconfirmed_head];
    for (uint32_t i = 0; i < IPFS_CID_LEN; i++)
        slot[i] = cid[i];
    p->unconfirmed_head = (p->unconfirmed_head + 1u) % IPFS_PIN_UNCONFIRMED_CAP;
    p->unconfirmed_written++;
}

void ipfs_pin_reset(ipfs_pin_progress_t *p)
{
    if (!p) return;
    for (uint32_t i = 0; i < (uint32_t) sizeof *p; i++)
        ((uint8_t *) p)[i] = 0;
}

ipfs_result_t ipfs_pin_step(ipfs_node_t *n, ipfs_pin_set_t *set,
                            ipfs_pin_progress_t *p, uint8_t *buf, uint32_t cap)
{
    if (!n || !set || !p || !buf || cap == 0)
        return IPFS_ERR_BAD_CID;
    if (ipfs_pin_done(set))
        return IPFS_OK;

    const ipfs_pin_entry_t *e = &set->entries[set->index];
    uint32_t got = 0;

    /* 1. the self-certifying get: the bytes are proven against the CID
     *    before any pin claim is made. A mismatch is recorded, never pinned. */
    ipfs_result_t r = ipfs_get_verify(n, e->cid, buf, cap, &got);
    set->index++;

    if (r == IPFS_ERR_NO_TRANSPORT) {
        p->no_transport++;
        return r;
    }
    if (r == IPFS_ERR_CID_MISMATCH) {
        p->mismatched++;
        return r;
    }
    if (r == IPFS_ERR_NOT_FOUND) {
        p->not_found++;
        return r;
    }
    if (r != IPFS_OK)
        return r;

    /* 2. pin it: ipfs_add_pin recomputes the CID from the bytes and confirms
     *    the content is served under it. An object the on-node confirm buffer
     *    cannot cover returns NOT_FOUND -- recorded in the ring, never
     *    claimed as pinned. */
    uint8_t out_cid[IPFS_CID_LEN];
    ipfs_result_t pr = ipfs_add_pin(n, buf, got, out_cid);
    if (pr == IPFS_OK) {
        p->confirmed++;
    } else {
        p->unconfirmed++;
        pin_ring_push(p, e->cid);
    }
    return pr;
}

uint32_t ipfs_pin_walk(ipfs_node_t *n, ipfs_pin_set_t *set,
                       ipfs_pin_progress_t *p, uint8_t *buf, uint32_t cap,
                       uint32_t max_steps)
{
    uint32_t done = 0;
    if (!n || !set || !p || !buf || cap == 0)
        return 0;
    while (done < max_steps && !ipfs_pin_done(set)) {
        (void) ipfs_pin_step(n, set, p, buf, cap);
        done++;
    }
    return done;
}

bool ipfs_pin_done(const ipfs_pin_set_t *set)
{
    return !set || set->index >= set->count;
}
