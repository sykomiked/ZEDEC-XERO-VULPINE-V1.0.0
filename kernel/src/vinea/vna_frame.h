/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_frame.h — UBH-168 framing for Vinea records (the default wire
 * syntax between ZXV nodes), with a negotiated plain-bytes fallback.
 *
 * A framed record is
 *     [21-octet UBH-168 header, packed by ubh_168_header_pack (src/ubh/ubh.c)]
 *     [canonical record bytes, zero-padded up to a whole number of 21-octet frames]
 * Header fields used:
 *     frame_class      the schema's class (CAPABILITY_POLICY for agreements,
 *                      CAUSAL_EVENT for messages/trades/ledger entries,
 *                      CONTENT_OBJECT for file chunks and provider records,
 *                      INTEGRITY for handshake messages)
 *     source_format_id VNA_UBH_FORMAT (Vinea canonical little-endian)
 *     target_type      the schema id
 *     schema_id        VNA_UBH_SCHEMA_TAG ("VNA2")
 *     payload_length   EXACT byte length of the canonical record
 *     integrity_ref    first 3 bytes of SHA3-256(record): a framing check only,
 *                      never a security claim — security comes from the
 *                      ML-DSA-65 signature over the full 256-bit-hashed bytes.
 * Unwrapping fails closed on: bad header, wrong class for the schema, length
 * that does not match the frame count, non-zero padding, integrity mismatch.
 *
 * Signatures always cover the canonical (unframed) record bytes, so framing
 * never changes what is signed; a peer that does not speak UBH-168 receives
 * the identical canonical bytes unframed. Hashes and NodeIDs are never cut to
 * 168 bits: 168 bits is the framing unit only.
 */
#ifndef VNA_FRAME_H
#define VNA_FRAME_H

#include "vna_schema.h"

#define VNA_UBH_HDR        21u
#define VNA_UBH_FORMAT     0x0E2Au
#define VNA_UBH_SCHEMA_TAG 0x32414E56u /* "VNA2" */

/* Feature bit advertised in PING/PONG and handshakes. */
#define VNA_FEAT_UBH168 0x00000001u

/* Bytes needed to frame a record of len bytes. */
uint32_t vna_frame_len(uint32_t len);

/* Wrap rec[0..len) (already canonical) into out. Returns the framed length or
 * -1. rec and out must not overlap. */
int32_t vna_frame_wrap(const vna_schema_t *s, const uint8_t *rec, uint32_t len, uint8_t *out,
                       uint32_t cap);

/* True iff buf starts with a UBH-168 header magic ("ZXV"). */
bool vna_frame_is_ubh(const uint8_t *buf, uint32_t len);

/* Unwrap a framed record. On success *rec points INTO buf at the canonical
 * bytes, *rec_len is the exact length and *schema_id the declared schema.
 * Returns VNA_OK or VNA_ERR_PARSE. */
vna_status_t vna_frame_unwrap(const uint8_t *buf, uint32_t len, const uint8_t **rec,
                              uint32_t *rec_len, uint16_t *schema_id, uint8_t *frame_class);

/* Accept either syntax for schema s: UBH-168 framed (checked) or plain. */
vna_status_t vna_frame_accept(const vna_schema_t *s, const uint8_t *buf, uint32_t len,
                              const uint8_t **rec, uint32_t *rec_len, bool *was_ubh);

/* ---- per-frame transform hook (e.g. src/ehop keyed endianness hopping) ----
 *
 * Every datagram Vinea emits carries exactly one frame (one UBH-168 framed or
 * plain canonical record), so "per frame" and "per datagram" coincide. A
 * transform, when installed, is the OUTERMOST layer:
 *     send:    canonical -> [UBH-168 frame] -> seal() -> host transport
 *     receive: host transport -> open() -> [UBH-168 unframe] -> canonical
 * It never sees or changes what is signed: signatures cover the canonical
 * bytes, so a transform can add privacy or channel separation (bands,
 * private networks) but can never weaken authentication. A frame open()
 * refuses is dropped before any parsing or signature work.
 *
 * Contract for both functions: read in[0..in_len), write out[0..cap), return
 * the output length (>= 0) or a negative value to refuse. addr is the peer's
 * transport address (destination on seal, source on open), or the LAN group
 * token for multicast. seal may grow a frame by at most `overhead` bytes
 * (<= VNA_XFORM_MAX_OVERHEAD). in and out never overlap.
 *
 * ehop adapter (integration): seal = ehop_seal(channel_for(addr), in, in_len,
 * out, cap, &len); open = ehop_open(channel_for(addr), ...); overhead =
 * EHOP_OVERHEAD. Note EHOP_MAX_PAYLOAD (2048) is smaller than the largest
 * Vinea frame (VNA_MSG_WIRE_MAX), so the adapter must either raise that limit
 * or fragment; Vinea itself does not fragment. */
#define VNA_XFORM_MAX_OVERHEAD 256u

typedef int32_t (*vna_xform_fn)(void *ctx, const uint8_t *addr, uint32_t addr_len,
                                const uint8_t *in, uint32_t in_len, uint8_t *out, uint32_t cap);

typedef struct {
    vna_xform_fn seal; /* NULL: identity */
    vna_xform_fn open; /* NULL: identity */
    void *ctx;
    uint32_t overhead;
} vna_xform_t;

/* Apply the hook to buf[0..len) in place (via scratch[0..scap)); buf has room
 * for cap bytes. Returns the new length, or -1 when the hook refuses or the
 * result does not fit. x == NULL or a NULL function is the identity. */
int32_t vna_xform_apply(const vna_xform_t *x, bool seal, const uint8_t *addr, uint32_t addr_len,
                        uint8_t *buf, uint32_t len, uint32_t cap, uint8_t *scratch, uint32_t scap);

#endif /* VNA_FRAME_H */
