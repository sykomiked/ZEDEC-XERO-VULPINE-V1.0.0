/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* federation.h — treaty-federation of sovereign microstates.
 *
 * A microstate CONSTITUTES itself out of a charter: its identity is the content-
 * address of that charter, so it is real BEFORE anyone recognises it (a nation
 * exists before it is admitted to the assembly). RECOGNITION is not a courtesy
 * stub — it recomputes the peer's identity from its charter and verifies the
 * declaratory writ binding, and ADMITS only if both hold. A council carries
 * ordinary business by simple majority and amendments by a two-thirds
 * supermajority, and it reserves an EIGHTH office that can never be bound —
 * a seat kept empty on purpose.
 *
 * The writ we verify is a declaratory content-binding, reused in the spirit of
 * the Royal Writ of the Sicilian Crown (LICENSE_ROYAL_WRIT: mutual sovereign
 * recognition). It proves the writ binds THIS exact charter+identity; it does
 * not, and does not claim to, prove cryptographic authorship — a real signature
 * oracle is an OPS BOUNDARY.
 */
#ifndef ZXV_FEDERATION_H
#define ZXV_FEDERATION_H

#define ZXV_IN_FAMILY_HEADER
#include "interspace.h"   /* shared vocabulary (core, family re-include suppressed) */
#undef ZXV_IN_FAMILY_HEADER

/* ---- Microstate ---- */

typedef struct {
    zxv_node_id_t self;
    uint8_t identity_cid[ZXV_HASH_LEN];  /* ipfs( LE(self) || charter_cid )      */
    uint8_t charter_cid[ZXV_HASH_LEN];   /* ipfs( charter document bytes )       */
    uint8_t writ_sig[64];                /* declaratory recognition binding      */
    uint32_t recognized_mask;            /* bit p set == peer whose id==p admitted*/
} microstate_t;

/* Constitute a microstate from its charter. Fills charter_cid = ipfs(charter)
 * and a stable identity_cid = ipfs(LE(self) || charter_cid) — both valid before
 * any recognition. writ_sig is zeroed; call microstate_seal_writ to fill it.
 * NULL charter with len 0 is allowed (an empty charter still has a CID);
 * a NULL microstate is ZXV_EDEGEN. */
zxv_status_t microstate_constitute(microstate_t *m, zxv_node_id_t self,
                                   const void *charter, uint32_t charter_len);

/* Fill writ_sig[0..31] with the declaratory binding
 *     ipfs( charter_cid || identity_cid || LE(self) )
 * and zero the rest. This is the content-address of the recognition claim, NOT
 * a private-key signature (that authenticity is an ops boundary). A peer that
 * later flips any byte of writ_sig fails microstate_recognize with ZXV_EBADSIG. */
void microstate_seal_writ(microstate_t *m);

/* `self` recognises `peer`. Recomputes the peer's identity_cid from its charter_cid
 * (mismatch -> ZXV_ECHARTER) and recomputes the peer's writ binding and compares
 * it to peer->writ_sig (mismatch -> ZXV_EBADSIG). Only on both passing does it
 * set the recognition bit for peer->self and return ZXV_OK. A peer id >= 32 (no
 * mask bit) is ZXV_EPERM. This is real verification: a substituted charter or a
 * forged writ is refused — recognition theatre is prohibited. */
zxv_status_t microstate_recognize(microstate_t *self, const microstate_t *peer);

/* Has `self` recognised the node with id `peer_id`? */
bool microstate_is_recognized(const microstate_t *self, zxv_node_id_t peer_id);

/* Anchor = ipfs( identity_cid || charter_cid || LE(self) ). Always RECOMPUTED,
 * never a cached "true". */
zxv_status_t federation_anchor(const microstate_t *m,
                               uint8_t out_anchor[ZXV_HASH_LEN]);
bool federation_verify(const microstate_t *m, const uint8_t anchor[ZXV_HASH_LEN]);

/* ---- Council ---- */

#define ZXV_COUNCIL_EIGHTH_OFFICE 7u   /* the reserved seat (0-based index 7)    */

typedef struct {
    uint32_t members_mask;   /* one bit per seated member                        */
    uint32_t office_mask;    /* one bit per bound office                          */
} council_t;

/* Does a motion carry? An amendment needs yes >= ceil(2*members/3); ordinary
 * business needs a simple majority (2*yes > members). members == popcount(mask). */
bool council_carries(const council_t *c, uint32_t yes_votes, bool is_amendment);

/* Bind an office (0-based). The EIGHTH office (index 7) can NEVER be bound ->
 * ZXV_EPERM, and so is any index >= 32. Otherwise the bit is set and ZXV_OK. */
zxv_status_t council_bind_office(council_t *c, uint32_t office_index);

#endif /* ZXV_FEDERATION_H */
