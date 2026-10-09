/* ipfsn_ubh.c — UBH-168 framing of CIDs and blocks: the ZXV-to-ZXV default.
 *
 * ENVELOPE (every frame is 21 octets = 168 bits)
 *   frame 0       CONTENT_OBJECT header (src/ubh ubh_168_header_pack):
 *                 source_format_id = UBH_FMT_UBH_168, target_type = CID|BLOCK,
 *                 flags = has_payload|canonical, payload_length = exact bytes,
 *                 schema_id = "IPFS", integrity_ref = first 3 octets of the
 *                 CID's digest (a routing hint, not a security check)
 *   frames 1..k   the payload in 21-octet frames, the last zero-padded; the
 *                 payload is a binary CID, or a binary CID followed by the block
 *   frame k+1     INTEGRITY trailer: payload_length repeated, schema_id = k,
 *                 integrity_ref = first 3 octets of SHA-256(frames 0..k)
 *
 * The trailer catches framing damage early. It is NOT what makes a block
 * trustworthy: the full sha2-256 CID inside the payload is, and every decode
 * re-hashes the block against it. Hashes are never shortened to 168 bits —
 * 168 bits is the framing unit, not the digest size (a 168-bit digest would
 * give only 84-bit collision resistance, ~56 bits against quantum search).
 *
 * The payload frames are contiguous, so decoding returns a pointer into the
 * envelope: no copy, no second buffer.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "ipfs_node.h"
#include "ipfsn_util.h"
#include "../robin_debanks/sha256.h"
#include "../ubh/ubh.h"

#define F IPFSN_UBH_FRAME

static uint32_t frames_for(uint32_t n)
{
    return (n + (F - 1u)) / F; /* 32-bit division by a constant */
}

uint32_t ipfsn_ubh_size(uint32_t payload_len)
{
    if (payload_len > IPFSN_BLOCK_MAX + IPFSN_CID_BIN_MAX) return 0;
    return (2u + frames_for(payload_len)) * F;
}

static void hint_of(const ipfsn_cid_t *cid, uint8_t h[3])
{
    for (uint32_t i = 0; i < 3; i++) h[i] = i < cid->digest_len ? cid->digest[i] : 0;
}

/* Write header + payload frames + trailer. The payload is (a, alen) then (b, blen). */
static int encode(uint16_t type, const ipfsn_cid_t *cid, const uint8_t *a, uint32_t alen,
                  const uint8_t *b, uint32_t blen, uint8_t *out, uint32_t cap, uint32_t *len)
{
    ubh_168_header_t h;
    uint8_t d[SHA256_DIGEST_LEN];
    uint32_t pl = alen + blen, k = frames_for(pl), total = ipfsn_ubh_size(pl);
    if (!total || blen > IPFSN_BLOCK_MAX) return IPFSN_ERR_SPACE;
    if (cap < total) return IPFSN_ERR_SPACE;
    ubh_168_header_init(&h, UBH_FRAME_CONTENT_OBJECT);
    h.source_format_id = UBH_FMT_UBH_168;
    h.target_type = type;
    h.flags = 0x05; /* has_payload | is_canonical */
    h.payload_length = pl;
    h.schema_id = IPFSN_UBH_SCHEMA;
    hint_of(cid, h.integrity_ref);
    ubh_168_header_pack(&h, out);
    ipfsn__cpy(out + F, a, alen);
    if (blen) ipfsn__cpy(out + F + alen, b, blen);
    ipfsn__set(out + F + pl, 0, k * F - pl);
    sha256(out, (1u + k) * F, d);
    ubh_168_header_init(&h, UBH_FRAME_INTEGRITY);
    h.source_format_id = UBH_FMT_UBH_168;
    h.target_type = type;
    h.payload_length = pl;
    h.schema_id = k;
    ipfsn__cpy(h.integrity_ref, d, 3);
    ubh_168_header_pack(&h, out + (1u + k) * F);
    *len = total;
    return IPFSN_OK;
}

/* Validate every frame; on success the payload is in[F .. F+*pl). */
static int decode(uint16_t type, const uint8_t *in, uint32_t len, uint32_t *pl, uint8_t hint[3])
{
    ubh_168_header_t h, ref, t;
    uint8_t d[SHA256_DIGEST_LEN];
    uint32_t k;
    if (!in || len < 2u * F || len % F) return IPFSN_ERR_MALFORMED;
    k = len / F - 2u;
    ubh_168_header_init(&ref, UBH_FRAME_CONTENT_OBJECT);
    if (!ubh_168_header_unpack(&h, in)) return IPFSN_ERR_MALFORMED;
    if (h.version != ref.version || h.frame_class != UBH_FRAME_CONTENT_OBJECT ||
        h.source_format_id != UBH_FMT_UBH_168 || h.target_type != type ||
        h.order_flags != ref.order_flags || h.flags != 0x05 || h.schema_id != IPFSN_UBH_SCHEMA)
        return IPFSN_ERR_MALFORMED;
    if (frames_for(h.payload_length) != k) return IPFSN_ERR_MALFORMED; /* exact framing */
    for (uint32_t i = F + h.payload_length; i < (1u + k) * F; i++)
        if (in[i]) return IPFSN_ERR_MALFORMED; /* padding must be zero */
    if (!ubh_168_header_unpack(&t, in + (1u + k) * F)) return IPFSN_ERR_MALFORMED;
    if (t.version != ref.version || t.frame_class != UBH_FRAME_INTEGRITY ||
        t.source_format_id != UBH_FMT_UBH_168 || t.target_type != type ||
        t.order_flags != ref.order_flags || t.flags != ref.flags ||
        t.payload_length != h.payload_length || t.schema_id != k)
        return IPFSN_ERR_MALFORMED;
    sha256(in, (1u + k) * F, d);
    if (!ipfsn__eq(t.integrity_ref, d, 3)) return IPFSN_ERR_MALFORMED;
    *pl = h.payload_length;
    ipfsn__cpy(hint, h.integrity_ref, 3);
    return IPFSN_OK;
}

int ipfsn_ubh_encode_cid(const ipfsn_cid_t *cid, uint8_t *out, uint32_t cap, uint32_t *len)
{
    uint8_t cb[IPFSN_CID_BIN_MAX];
    int cl;
    if (!cid || !out || !len) return IPFSN_ERR_ARG;
    if ((cl = ipfsn_cid_encode(cid, cb, sizeof cb)) < 0) return cl;
    return encode(IPFSN_UBH_T_CID, cid, cb, (uint32_t) cl, 0, 0, out, cap, len);
}

int ipfsn_ubh_decode_cid(const uint8_t *in, uint32_t len, ipfsn_cid_t *cid)
{
    uint32_t pl;
    uint8_t hint[3], want[3];
    int r;
    if (!cid) return IPFSN_ERR_ARG;
    if ((r = decode(IPFSN_UBH_T_CID, in, len, &pl, hint)) != 0) return r;
    if ((r = ipfsn_cid_decode_exact(in + F, pl, cid)) != 0) return r;
    hint_of(cid, want);
    return ipfsn__eq(hint, want, 3) ? IPFSN_OK : IPFSN_ERR_MALFORMED;
}

int ipfsn_ubh_encode_block(const ipfsn_cid_t *cid, const uint8_t *block, uint32_t blen,
                           uint8_t *out, uint32_t cap, uint32_t *len)
{
    uint8_t cb[IPFSN_CID_BIN_MAX];
    int cl;
    if (!cid || !out || !len || (!block && blen)) return IPFSN_ERR_ARG;
    if ((cl = ipfsn_cid_encode(cid, cb, sizeof cb)) < 0) return cl;
    return encode(IPFSN_UBH_T_BLOCK, cid, cb, (uint32_t) cl, block, blen, out, cap, len);
}

int ipfsn_ubh_decode_block(const uint8_t *in, uint32_t len, ipfsn_cid_t *cid, const uint8_t **block,
                           uint32_t *blen)
{
    uint32_t pl;
    uint8_t hint[3], want[3];
    int r, cl;
    if (!cid || !block || !blen) return IPFSN_ERR_ARG;
    if ((r = decode(IPFSN_UBH_T_BLOCK, in, len, &pl, hint)) != 0) return r;
    if ((cl = ipfsn_cid_decode(in + F, pl, cid)) < 0) return IPFSN_ERR_MALFORMED;
    hint_of(cid, want);
    if (!ipfsn__eq(hint, want, 3)) return IPFSN_ERR_MALFORMED;
    *block = in + F + (uint32_t) cl;
    *blen = pl - (uint32_t) cl;
    return ipfsn_cid_verify(cid, *block, *blen);
}

/* ---- text form -------------------------------------------------------------- */

#define PFX_LEN     7u /* "ubh168:" */
#define CID_ENV_MAX ((2u + (IPFSN_CID_BIN_MAX + F - 1u) / F) * F)

int ipfsn_ubh_cid_to_text(const ipfsn_cid_t *cid, char *out, uint32_t cap)
{
    uint8_t env[CID_ENV_MAX];
    uint32_t el;
    int r;
    if (!cid || !out || cap < PFX_LEN + 2) return IPFSN_ERR_ARG;
    if ((r = ipfsn_ubh_encode_cid(cid, env, sizeof env, &el)) != 0) return r;
    ipfsn__cpy(out, IPFSN_UBH_TEXT_PFX, PFX_LEN);
    out[PFX_LEN] = 'b';
    if ((r = ipfsn_base32_encode(env, el, out + PFX_LEN + 1, cap - PFX_LEN - 1)) < 0) return r;
    return r + (int) PFX_LEN + 1;
}

int ipfsn_ubh_cid_from_text(const char *s, uint32_t len, ipfsn_cid_t *cid)
{
    uint8_t env[CID_ENV_MAX];
    int n;
    if (!s || !cid) return IPFSN_ERR_ARG;
    if (len < PFX_LEN + 2 || !ipfsn__eq(s, IPFSN_UBH_TEXT_PFX, PFX_LEN) || s[PFX_LEN] != 'b')
        return IPFSN_ERR_MALFORMED;
    n = ipfsn_base32_decode(s + PFX_LEN + 1, len - PFX_LEN - 1, env, sizeof env);
    if (n < 0) return n == IPFSN_ERR_SPACE ? IPFSN_ERR_MALFORMED : n;
    return ipfsn_ubh_decode_cid(env, (uint32_t) n, cid);
}

/* ---- negotiation ------------------------------------------------------------- */

void ipfsn_wire_hello(uint8_t out[IPFSN_UBH_FRAME])
{
    ubh_168_header_t h;
    ubh_168_header_init(&h, UBH_FRAME_FORMAT_IDENTITY);
    h.source_format_id = UBH_FMT_UBH_168;
    h.target_type = 1; /* ZXV IPFS wire revision */
    h.schema_id = IPFSN_UBH_SCHEMA;
    ubh_168_header_pack(&h, out);
}

uint32_t ipfsn_wire_negotiate(bool local_ubh, const uint8_t *peer_hello, uint32_t len)
{
    ubh_168_header_t h, ref;
    if (!local_ubh || !peer_hello || len != F) return IPFSN_WIRE_PLAIN;
    if (!ubh_168_header_unpack(&h, peer_hello)) return IPFSN_WIRE_PLAIN;
    ubh_168_header_init(&ref, UBH_FRAME_FORMAT_IDENTITY);
    if (h.version != ref.version || h.frame_class != UBH_FRAME_FORMAT_IDENTITY ||
        h.source_format_id != UBH_FMT_UBH_168 || h.schema_id != IPFSN_UBH_SCHEMA ||
        h.target_type < 1)
        return IPFSN_WIRE_PLAIN;
    return IPFSN_WIRE_UBH168;
}

int ipfsn_wire_encode_block(uint32_t wire, const ipfsn_cid_t *cid, const uint8_t *block,
                            uint32_t blen, uint8_t *out, uint32_t cap, uint32_t *len)
{
    if (!cid || !out || !len || (!block && blen)) return IPFSN_ERR_ARG;
    if (wire == IPFSN_WIRE_UBH168) return ipfsn_ubh_encode_block(cid, block, blen, out, cap, len);
    if (wire != IPFSN_WIRE_PLAIN) return IPFSN_ERR_ARG;
    if (blen > cap) return IPFSN_ERR_SPACE;
    if (blen) ipfsn__cpy(out, block, blen);
    *len = blen;
    return IPFSN_OK;
}

int ipfsn_wire_decode_block(uint32_t wire, const ipfsn_cid_t *want, const uint8_t *in, uint32_t len,
                            const uint8_t **block, uint32_t *blen)
{
    ipfsn_cid_t got, a, b;
    int r;
    if (!want || (!in && len) || !block || !blen) return IPFSN_ERR_ARG;
    if (wire == IPFSN_WIRE_PLAIN) {
        if ((r = ipfsn_cid_verify(want, in, len)) != 0) return r;
        *block = in;
        *blen = len;
        return IPFSN_OK;
    }
    if (wire != IPFSN_WIRE_UBH168) return IPFSN_ERR_ARG;
    if ((r = ipfsn_ubh_decode_block(in, len, &got, block, blen)) != 0) return r;
    ipfsn_cid_to_v1(&got, &a);
    ipfsn_cid_to_v1(want, &b);
    return ipfsn_cid_equal(&a, &b) ? IPFSN_OK : IPFSN_ERR_HASH;
}
