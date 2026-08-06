/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* fusion.c — program fusion over the Tri-Space file types. See fusion.h.
 *
 * The whole module is set algebra on capability bitmasks plus one honest hash.
 * No linker lives here; we compose contracts and content-address them. */
#include "fusion.h"
#include "../robin_debanks/sha256.h"

/* Big-endian encode a u32 into buf[at..at+4). Determinism across hosts: bytes
 * go in a fixed order, so the CID does not depend on the machine's endianness. */
static void put_u32(uint8_t *buf, uint32_t *at, uint32_t v) {
    buf[(*at)++] = (uint8_t)(v >> 24);
    buf[(*at)++] = (uint8_t)(v >> 16);
    buf[(*at)++] = (uint8_t)(v >> 8);
    buf[(*at)++] = (uint8_t)(v);
}

static void put_digest(uint8_t *buf, uint32_t *at, const uint8_t d[TRI_DIGEST_LEN]) {
    for (uint32_t i = 0; i < TRI_DIGEST_LEN; i++) buf[(*at)++] = d[i];
}

/* ---- constructing a program ---- */

void fuse_program_init(fuse_program_t *p, const uint8_t triad_id[TRI_ID_LEN],
                       const uint8_t source_graph_digest[TRI_DIGEST_LEN]) {
    if (!p) return;
    tri_init(&p->triad, triad_id, source_graph_digest);
    for (uint32_t i = 0; i < FUSE_CID_LEN; i++) p->cid[i] = 0;
    p->has_cid = false;
}

static bool set_role(fuse_program_t *p, tri_role_t role, uint32_t caps,
                     const uint8_t content_digest[TRI_DIGEST_LEN]) {
    if (!p || !content_digest) return false;
    /* generated=false, claims_proven_inverse=false: a plain contract member. */
    return tri_set_member(&p->triad, role, content_digest, caps, false, false);
}

bool fuse_set_provides(fuse_program_t *p, uint32_t caps,
                       const uint8_t content_digest[TRI_DIGEST_LEN]) {
    return set_role(p, TRI_POSITIVE, caps, content_digest);
}

bool fuse_set_needs(fuse_program_t *p, uint32_t caps,
                    const uint8_t content_digest[TRI_DIGEST_LEN]) {
    return set_role(p, TRI_NEGATIVE, caps, content_digest);
}

bool fuse_set_neutral(fuse_program_t *p, uint32_t caps,
                      const uint8_t content_digest[TRI_DIGEST_LEN]) {
    return set_role(p, TRI_NEUTRAL, caps, content_digest);
}

uint32_t fuse_provides(const fuse_program_t *p) {
    return p ? p->triad.member[TRI_POSITIVE].capability_set : 0u;
}

uint32_t fuse_needs(const fuse_program_t *p) {
    return p ? p->triad.member[TRI_NEGATIVE].capability_set : 0u;
}

static uint32_t neutral_of(const fuse_program_t *p) {
    return p ? p->triad.member[TRI_NEUTRAL].capability_set : 0u;
}

/* ---- the fusion algebra ---- */

bool fuse_can_compose(const fuse_program_t *provider, const fuse_program_t *consumer) {
    if (!provider || !consumer) return false;
    uint32_t plug   = fuse_provides(provider);
    uint32_t socket = fuse_needs(consumer);
    /* superset: every needed bit is provided. (plug & socket) == socket. */
    return (plug & socket) == socket;
}

/* out_cid = SHA-256( provides || needs || neutral || S+dig || S-dig || S0dig ).
 * The contract IS the address: same provides/needs/glue and the same member
 * digests hash to the same CID; flip any bit and the CID moves. */
int32_t fuse_cid(const fuse_program_t *p, uint8_t out_cid[FUSE_CID_LEN]) {
    if (!p || !out_cid) return -1;
    uint8_t buf[3u * 4u + 3u * TRI_DIGEST_LEN];
    uint32_t at = 0;
    put_u32(buf, &at, fuse_provides(p));
    put_u32(buf, &at, fuse_needs(p));
    put_u32(buf, &at, neutral_of(p));
    put_digest(buf, &at, p->triad.member[TRI_POSITIVE].content_digest);
    put_digest(buf, &at, p->triad.member[TRI_NEGATIVE].content_digest);
    put_digest(buf, &at, p->triad.member[TRI_NEUTRAL].content_digest);
    sha256(buf, at, out_cid);
    return 0;
}

/* A fused member's content digest = SHA-256(a's member digest || b's member
 * digest) — provenance of both inputs flows into the fused artifact, so a fused
 * digest is (to SHA-256's collision resistance) distinct from an unrelated single
 * program's author-supplied digest. */
static void fuse_member_digest(const fuse_program_t *a, const fuse_program_t *b,
                               tri_role_t role, uint8_t out[TRI_DIGEST_LEN]) {
    uint8_t buf[2u * TRI_DIGEST_LEN];
    uint32_t at = 0;
    put_digest(buf, &at, a->triad.member[role].content_digest);
    put_digest(buf, &at, b->triad.member[role].content_digest);
    sha256(buf, at, out);
}

int32_t fuse_compose(const fuse_program_t *a, const fuse_program_t *b,
                     fuse_program_t *out) {
    if (!a || !b || !out) return FUSE_ERR_NULL;

    uint32_t a_prov = fuse_provides(a);
    uint32_t b_prov = fuse_provides(b);
    uint32_t a_need = fuse_needs(a);
    uint32_t b_need = fuse_needs(b);

    /* The socket must find at least one matching pin on the plug. If b needs
     * something and a supplies NONE of it, this is not a fusion — refuse and
     * fabricate nothing. (b needing nothing composes trivially.) */
    if (b_need != 0u && (a_prov & b_need) == 0u) return FUSE_ERR_UNMET;

    uint32_t union_prov = a_prov | b_prov;
    uint32_t union_need = a_need | b_need;
    uint32_t remaining  = union_need & ~union_prov;   /* still unmet after fusion */
    uint32_t union_neu  = neutral_of(a) | neutral_of(b);

    /* Deterministic identity for the fused triad, derived from both inputs. */
    uint8_t src_graph[TRI_DIGEST_LEN];
    {
        uint8_t sbuf[2u * TRI_DIGEST_LEN];
        uint32_t at = 0;
        put_digest(sbuf, &at, a->triad.source_graph_digest);
        put_digest(sbuf, &at, b->triad.source_graph_digest);
        sha256(sbuf, at, src_graph);
    }

    /* Deterministic per-member content digests binding both parents. */
    uint8_t d_pos[TRI_DIGEST_LEN], d_neg[TRI_DIGEST_LEN], d_neu[TRI_DIGEST_LEN];
    fuse_member_digest(a, b, TRI_POSITIVE, d_pos);
    fuse_member_digest(a, b, TRI_NEGATIVE, d_neg);
    fuse_member_digest(a, b, TRI_NEUTRAL, d_neu);

    /* Build the fused program. triad_id is set to src_graph provisionally; it is
     * not the content address (that comes from fuse_cid over the contract). */
    fuse_program_init(out, src_graph, src_graph);
    fuse_set_provides(out, union_prov, d_pos);
    fuse_set_needs(out, remaining, d_neg);
    fuse_set_neutral(out, union_neu, d_neu);

    /* Content-address the finished contract. */
    if (fuse_cid(out, out->cid) != 0) return FUSE_ERR_NULL;
    out->has_cid = true;

    /* Reuse the fused CID as the triad_id so the artifact self-identifies. */
    for (uint32_t i = 0; i < TRI_ID_LEN && i < FUSE_CID_LEN; i++)
        out->triad.triad_id[i] = out->cid[i];

    return FUSE_OK;
}
