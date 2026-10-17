/* chg_mesh.h — the Chiglet mesh coordinator: nine seeds serving one network.
 *
 * The seeds host IPFS nodes and their goal is to SERVE the network we are
 * building, not merely to hold it. This module is the coordination: the
 * Chiglet reads the local node's own state (what is pinned, what is wanted,
 * what the peers ask for), decides with chg_infer what to fetch, what to
 * announce and what to serve next, and exchanges that state with the other
 * seeds -- every exchange carried by a Vinea session, so SCP-144 is the
 * transport for the coordination exactly as it is for the handshake.
 *
 * The transport is an INTERFACE, not an assumption: this module declares the
 * three operations it needs and the caller binds them to the real Vinea
 * session API. That keeps the coordinator testable without a live mesh and
 * keeps it from inventing a transport that does not exist.
 *
 * The traffic is constant and BOUNDED: a fixed step budget per cycle, a
 * preallocated want list, and an explicit full check. Nothing allocates on
 * the serving path.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZXV_CHG_MESH_H
#define ZXV_CHG_MESH_H

#include <stdint.h>
#include <stdbool.h>
#include "chiglet.h"
#include "ipfs.h"
#include "ipfs_pin.h"

/* The most peers one seed coordinates with (the armada is nine). */
#ifndef CHG_MESH_PEERS_MAX
#define CHG_MESH_PEERS_MAX 16u
#endif
/* The bounded want list: what this seed is looking for this cycle. */
#ifndef CHG_MESH_WANT_MAX
#define CHG_MESH_WANT_MAX 32u
#endif
/* The step budget per cycle: the constant traffic is bounded, never a storm. */
#ifndef CHG_MESH_STEPS_PER_CYCLE
#define CHG_MESH_STEPS_PER_CYCLE 8u
#endif

typedef struct {
    uint8_t  cid[IPFS_CID_LEN];
    uint32_t bytes;
    uint32_t want_count;    /* how many peers asked for it this cycle */
} chg_mesh_want_t;

/* The transport the coordinator needs. The caller binds these to the real
 * Vinea session API (SCP-144): a session per peer, the state exchange over
 * it, and the serve request. Every op returns false on a closed session. */
typedef struct {
    void *ctx;
    /* open/refresh a session to peer `i` of `n` (the Vinea handshake, with the
     * SCP-144 lattice binding mixed in by the session layer itself) */
    bool (*session_open)(void *ctx, uint32_t i, uint32_t n);
    /* publish this seed's state to peer `i`: the confirmed pin count, the
     * want list and the head of the unconfirmed ring */
    bool (*state_publish)(void *ctx, uint32_t i, const ipfs_pin_progress_t *p,
                          const chg_mesh_want_t *want, uint32_t nwant);
    /* ask peer `i` to serve `cid` (or to keep serving it) */
    bool (*serve_request)(void *ctx, uint32_t i, const uint8_t cid[IPFS_CID_LEN]);
} chg_mesh_transport_t;

typedef struct {
    chg_mesh_transport_t t;
    uint32_t             peers;          /* how many are live this cycle */
    chg_mesh_want_t      want[CHG_MESH_WANT_MAX];
    uint32_t             nwant;
    uint32_t             served;         /* serve requests sent (all cycles) */
    uint32_t             cycles;         /* completed cycles */
    uint32_t             refused;        /* a transport op returned false */
    bool                 pull;           /* the Chiglet's verdict: pull, not push */
    surplus_real_t       evidence[CHG_DIM]; /* the Chiglet's own view */
} chg_mesh_t;

/* Bind the transport and reset the counters. `c` may be NULL: the
 * coordinator then runs without inference and simply relays state. */
void chg_mesh_init(chg_mesh_t *m, const chg_mesh_transport_t *t);

/* One coordination cycle: open/refresh the peer sessions, publish this
 * seed's state, let the Chiglet rank what to want, and send the serve
 * requests for the top entries -- at most CHG_MESH_STEPS_PER_CYCLE
 * transport operations. Returns the number of operations performed. */
uint32_t chg_mesh_cycle(chg_mesh_t *m, chiglet_t *c, const ipfs_pin_progress_t *p);

/* Offer a CID as wanted (from a peer's request or this seed's own read).
 * Returns false when the bounded list is FULL -- the caller must drain it,
 * never grow it. */
bool chg_mesh_want(chg_mesh_t *m, const uint8_t cid[IPFS_CID_LEN], uint32_t bytes);

#endif /* ZXV_CHG_MESH_H */
