/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sovereign_node.h — every system is a sovereign node.
 *
 * "A sea of microstates, each a crown unto itself; we recognize between,
 *  never rule over."
 *
 * The premise, with NO exceptions: every SYSTEM is a sovereign node — a
 * microstate among endless microstates, carrying ALL capabilities it cares to
 * advertise — and every USER a sovereign individual. Sovereignty here is not a
 * privilege the module grants and can take back: a node CONSTITUTES itself, its
 * identity is the content-address of its own charter (real BEFORE any peer
 * recognizes it, the way a nation exists before it is admitted to the assembly),
 * and there is deliberately NO function in this module that un-makes, strips,
 * or expels a constituted node. You will not find one — that absence is the point.
 *
 * We do not reinvent the machinery. A sovereign_node_t WRAPS the real interspace
 * microstate_t and reuses microstate_constitute / microstate_recognize verbatim:
 * recognition recomputes the peer's identity from its charter and verifies its
 * declaratory writ binding, and admits only if BOTH hold — recognition theatre
 * is prohibited. Federation is BETWEEN peers, never OVER them: recognizing a peer
 * changes only the recognizer's own recognized-set; it installs no rule, no
 * obligation, nothing at all in the peer.
 *
 * OPS BOUNDARIES (honest, not hollow): the wire transport between nodes is an ops
 * boundary — this module records a claim and federates, it does not itself carry
 * bytes between machines. "ALL capabilities" is the manifest a node ADVERTISES;
 * it is only as real as whatever backs each bit. And real asymmetric-key
 * signature authenticity is an ops boundary too: the writ we verify is a
 * declaratory content-binding (per the underlying federation), and the secret
 * key handed to sovereign_constitute is committed to as a content-addressed
 * keyprint, NOT used to perform real elliptic-curve signing here.
 *
 * Freestanding: integer only, no libc, no allocation, no floating point on target.
 */
#ifndef ZXV_SOVEREIGN_NODE_H
#define ZXV_SOVEREIGN_NODE_H

#include "federation.h"   /* microstate_t + the real recognition machinery */

/* A sovereign node: a real interspace microstate, plus the capability manifest
 * it advertises and a commitment to its declared signing key. */
typedef struct {
    microstate_t ms;                    /* the real, content-addressed microstate */
    uint8_t      keyprint[ZXV_HASH_LEN];/* ipfs(sk): a commitment to the declared
                                         * signing key (real signing = ops boundary)*/
    uint32_t     manifest;              /* advertised capability bits (0..31)       */
    bool         constituted;           /* set once, at constitution; never cleared */
} sovereign_node_t;

/* A node declares ITSELF sovereign. Fills the microstate (stable identity_cid =
 * ipfs(LE(self)||charter_cid), valid BEFORE any recognizer exists), seals its
 * declaratory writ, and commits the declared secret key as keyprint = ipfs(sk).
 * The manifest starts empty. Returns ZXV_OK on success, or a negative
 * zxv_status_t on a NULL node / NULL key. There is NO recognizer count anywhere
 * in this call: existence does not depend on being recognized. */
int32_t sovereign_constitute(sovereign_node_t *n, zxv_node_id_t self,
                             const uint8_t *charter, uint32_t len,
                             const uint8_t sk[32]);

/* Advertise a capability by bit index (0..31). Idempotent; a bit >= 32 is
 * ignored (there are only 32 manifest bits). This records a CLAIM — it grants no
 * real-world power on its own; each bit is only as real as what backs it. */
void sovereign_advertise_cap(sovereign_node_t *n, uint32_t cap_bit);

/* The advertised capability manifest (0 for a NULL node). */
uint32_t sovereign_manifest(const sovereign_node_t *n);

/* TRUE for every constituted node — with NO exception path. There is no argument,
 * no global state, no recognizer threshold that makes a constituted node return
 * false. Sovereignty is neither granted nor taken away here. */
bool sovereign_is_sovereign(const sovereign_node_t *n);

/* `self` recognizes `peer` as a sovereign microstate, via the REAL recognition:
 * microstate_recognize recomputes the peer's identity from its charter
 * (mismatch -> ZXV_ECHARTER) and its writ binding (forged/stale -> ZXV_EBADSIG),
 * admitting only if both verify. On success the peer's id is added to self's
 * recognized-set and ZXV_OK is returned. BETWEEN, never OVER: `peer` is const and
 * is left entirely unchanged — recognizing a peer installs no rule in it. */
int32_t sovereign_federate(sovereign_node_t *self, const sovereign_node_t *peer);

/* Has `self` recognized the node whose id is `peer_id`? (Thin, honest passthrough
 * to the underlying microstate recognized-set.) */
bool sovereign_recognizes(const sovereign_node_t *self, zxv_node_id_t peer_id);

#endif /* ZXV_SOVEREIGN_NODE_H */
