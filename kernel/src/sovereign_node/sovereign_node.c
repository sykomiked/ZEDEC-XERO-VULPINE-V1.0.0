/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* sovereign_node.c — a node that crowns itself, then recognizes its equals.
 *
 * Notice what is NOT in this file: nothing un-makes a node, nothing strips its
 * sovereignty, nothing casts it out. The only mutation federation performs is on
 * the RECOGNIZER's own recognized-set — never on the peer. Between, never over.
 *
 * Every CID is delegated to the reused ipfs/federation machinery; we recompute,
 * we never cache a "true". Integer only, no libc, no allocation, no float.
 */

#include "sovereign_node.h"
#include "ipfs.h"   /* ipfs_cid_from_bytes — for the declared-key commitment */

int32_t sovereign_constitute(sovereign_node_t *n, zxv_node_id_t self,
                             const uint8_t *charter, uint32_t len,
                             const uint8_t sk[32]) {
    if (!n || !sk) return ZXV_EDEGEN;

    /* 1. Constitute the real microstate: a stable, content-addressed identity
     *    that exists BEFORE any peer recognizes it. (Reused verbatim — we do not
     *    reimplement the hashing.) */
    zxv_status_t st = microstate_constitute(&n->ms, self, charter, len);
    if (st != ZXV_OK) return st;

    /* 2. Seal its declaratory writ so a peer can later verify the binding.
     *    (Authenticity via a real key backend is an ops boundary; this proves the
     *    writ binds THIS exact charter+identity.) */
    microstate_seal_writ(&n->ms);

    /* 3. Commit to the declared signing key as a content-addressed keyprint.
     *    This is an HONEST commitment, not a signature: we do not perform real
     *    elliptic-curve signing here (that is an ops boundary). It lets a node
     *    publish "this is the key I stand behind" without fabricating crypto. */
    ipfs_cid_from_bytes(sk, 32u, n->keyprint);

    /* 4. Empty manifest; a node advertises its capabilities explicitly. */
    n->manifest    = 0u;
    n->constituted = true;
    return ZXV_OK;
}

void sovereign_advertise_cap(sovereign_node_t *n, uint32_t cap_bit) {
    if (!n) return;
    if (cap_bit >= 32u) return;   /* only 32 manifest bits — silently no-op */
    n->manifest |= (1u << cap_bit);
}

uint32_t sovereign_manifest(const sovereign_node_t *n) {
    if (!n) return 0u;
    return n->manifest;
}

bool sovereign_is_sovereign(const sovereign_node_t *n) {
    /* TRUE for every constituted node. There is intentionally no threshold, no
     * recognizer count, no flag anyone else can flip. The only way to be
     * un-sovereign is to have never constituted at all. */
    return n && n->constituted;
}

int32_t sovereign_federate(sovereign_node_t *self, const sovereign_node_t *peer) {
    if (!self || !peer) return ZXV_EDEGEN;
    /* Real recognition: recomputes the peer's identity from its charter and its
     * writ binding, admits only if both verify. `peer->ms` is const to
     * microstate_recognize — the peer is left untouched; only self->ms's
     * recognized_mask changes. Between, never over. */
    return microstate_recognize(&self->ms, &peer->ms);
}

bool sovereign_recognizes(const sovereign_node_t *self, zxv_node_id_t peer_id) {
    if (!self) return false;
    return microstate_is_recognized(&self->ms, peer_id);
}
