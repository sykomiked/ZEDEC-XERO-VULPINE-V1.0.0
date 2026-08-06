/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* federation.c — microstates, real recognition, councils, reserved eighth seat.
 *
 * Every CID is RECOMPUTED on demand — never a cached "true". All 32-bit
 * scalars are serialised little-endian before hashing so a CID is
 * ISA-independent (a microstate recognised on arm64 verifies on x86-64).
 */

#include "federation.h"
#include "ipfs.h"

/* ---- local helpers ---- */
static void put_le32(uint8_t *b, uint32_t v) {
    b[0] = (uint8_t)(v);       b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24);
}
static void copy32(uint8_t *d, const uint8_t *s) {
    for (uint32_t i = 0; i < ZXV_HASH_LEN; i++) d[i] = s[i];
}
static bool eq32(const uint8_t *a, const uint8_t *b) {
    for (uint32_t i = 0; i < ZXV_HASH_LEN; i++) if (a[i] != b[i]) return false;
    return true;
}
static uint32_t popcount32(uint32_t x) {
    uint32_t n = 0;
    while (x) { n += (x & 1u); x >>= 1; }
    return n;
}

/* identity_cid = ipfs( LE(self) || charter_cid ) */
static void compute_identity(zxv_node_id_t self, const uint8_t charter_cid[ZXV_HASH_LEN],
                             uint8_t out[ZXV_HASH_LEN]) {
    uint8_t buf[4 + ZXV_HASH_LEN];
    put_le32(buf, self);
    copy32(buf + 4, charter_cid);
    ipfs_cid_from_bytes(buf, sizeof(buf), out);
}

/* writ binding = ipfs( charter_cid || identity_cid || LE(self) ) */
static void compute_writ(const microstate_t *m, uint8_t out[ZXV_HASH_LEN]) {
    uint8_t buf[ZXV_HASH_LEN + ZXV_HASH_LEN + 4];
    copy32(buf, m->charter_cid);
    copy32(buf + ZXV_HASH_LEN, m->identity_cid);
    put_le32(buf + 2 * ZXV_HASH_LEN, m->self);
    ipfs_cid_from_bytes(buf, sizeof(buf), out);
}

/* anchor = ipfs( identity_cid || charter_cid || LE(self) ) */
static void compute_anchor(const microstate_t *m, uint8_t out[ZXV_HASH_LEN]) {
    uint8_t buf[ZXV_HASH_LEN + ZXV_HASH_LEN + 4];
    copy32(buf, m->identity_cid);
    copy32(buf + ZXV_HASH_LEN, m->charter_cid);
    put_le32(buf + 2 * ZXV_HASH_LEN, m->self);
    ipfs_cid_from_bytes(buf, sizeof(buf), out);
}

/* ===== Microstate ===== */

zxv_status_t microstate_constitute(microstate_t *m, zxv_node_id_t self,
                                   const void *charter, uint32_t charter_len) {
    if (!m) return ZXV_EDEGEN;
    m->self = self;
    ipfs_cid_from_bytes((const uint8_t *)charter, charter_len, m->charter_cid);
    compute_identity(self, m->charter_cid, m->identity_cid);
    for (uint32_t i = 0; i < 64; i++) m->writ_sig[i] = 0;
    m->recognized_mask = 0;
    return ZXV_OK;
}

void microstate_seal_writ(microstate_t *m) {
    if (!m) return;
    uint8_t w[ZXV_HASH_LEN];
    compute_writ(m, w);
    copy32(m->writ_sig, w);
    for (uint32_t i = ZXV_HASH_LEN; i < 64; i++) m->writ_sig[i] = 0;
}

zxv_status_t microstate_recognize(microstate_t *self, const microstate_t *peer) {
    if (!self || !peer) return ZXV_EDEGEN;
    if (peer->self >= 32u) return ZXV_EPERM;   /* no mask bit for this peer */

    /* 1. Recompute the peer's identity from its charter — a substituted charter
     *    changes the CID and is caught here. */
    uint8_t id[ZXV_HASH_LEN];
    compute_identity(peer->self, peer->charter_cid, id);
    if (!eq32(id, peer->identity_cid)) return ZXV_ECHARTER;

    /* 2. Recompute the declaratory writ binding and compare to what the peer
     *    presented. A forged or stale writ diverges here. (Authenticity via a
     *    real key backend is an ops boundary; this proves the writ binds THIS
     *    exact charter+identity.) */
    uint8_t w[ZXV_HASH_LEN];
    compute_writ(peer, w);
    for (uint32_t i = 0; i < ZXV_HASH_LEN; i++)
        if (w[i] != peer->writ_sig[i]) return ZXV_EBADSIG;
    /* The trailing bytes of the 64-byte writ_sig field are RESERVED and zeroed at
     * seal (for a future real key backend). Verify the WHOLE field, not just its
     * meaningful half — otherwise bytes 32..63 are attacker-mutable and ignored. */
    for (uint32_t i = ZXV_HASH_LEN; i < 64; i++)
        if (peer->writ_sig[i] != 0) return ZXV_EBADSIG;

    /* 3. Admit — the Royal Writ's mutual recognition, made concrete. */
    self->recognized_mask |= (1u << peer->self);
    return ZXV_OK;
}

bool microstate_is_recognized(const microstate_t *self, zxv_node_id_t peer_id) {
    if (!self || peer_id >= 32u) return false;
    return (self->recognized_mask & (1u << peer_id)) != 0;
}

zxv_status_t federation_anchor(const microstate_t *m, uint8_t out_anchor[ZXV_HASH_LEN]) {
    if (!m || !out_anchor) return ZXV_EDEGEN;
    compute_anchor(m, out_anchor);
    return ZXV_OK;
}

bool federation_verify(const microstate_t *m, const uint8_t anchor[ZXV_HASH_LEN]) {
    if (!m || !anchor) return false;
    uint8_t recomputed[ZXV_HASH_LEN];
    compute_anchor(m, recomputed);   /* recompute — never a cached true */
    return eq32(recomputed, anchor);
}

/* ===== Council ===== */

bool council_carries(const council_t *c, uint32_t yes_votes, bool is_amendment) {
    if (!c) return false;
    uint32_t members = popcount32(c->members_mask);
    /* An empty council carries NOTHING. Without this guard the amendment
     * threshold ceil(2*0/3)==0 would let 0 votes pass a two-thirds supermajority. */
    if (members == 0) return false;
    if (yes_votes > members) yes_votes = members;   /* can't out-vote the seats */
    if (is_amendment) {
        /* ceil(2*members/3) = (2*members + 2) / 3 */
        uint32_t threshold = (2u * members + 2u) / 3u;
        return yes_votes >= threshold;
    }
    /* simple majority: strictly more than half */
    return 2u * yes_votes > members;
}

zxv_status_t council_bind_office(council_t *c, uint32_t office_index) {
    if (!c) return ZXV_EDEGEN;
    if (office_index >= 32u) return ZXV_EPERM;
    if (office_index == ZXV_COUNCIL_EIGHTH_OFFICE)
        return ZXV_EPERM;   /* the seat kept empty on purpose */
    c->office_mask |= (1u << office_index);
    return ZXV_OK;
}
