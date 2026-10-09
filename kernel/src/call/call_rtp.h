/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* call_rtp.h — media packetisation for ZXV real-time calls.
 *
 * An RTP-like media packet is a 20-octet header followed by one fragment of
 * one encoded frame. All fields are big-endian:
 *
 *   off len field
 *    0   1  flags: bits 7-6 version (2), bit 5 marker (last fragment of the
 *              frame), bit 4 keyframe, bits 3-0 simulcast layer
 *    1   1  payload type (7 bits, bit 7 must be 0)
 *    2   2  sequence number (per stream, wraps at 65536)
 *    4   4  media timestamp (codec clock: 48 kHz audio, 90 kHz video)
 *    8   4  stream id (SSRC-like)
 *   12   2  frame index (per stream, wraps)
 *   14   1  fragment index
 *   15   1  fragment count (1..255)
 *   16   3  frame length in bytes (1..16777215)
 *   19   1  CRC-8 (poly 0x07) over octets 0..18
 *
 * There is no payload-length field: every fragment but the last carries
 * unit = ceil(frame_len / frag_count) bytes, so fragment i's offset and length
 * follow from the header alone. The sender chooses frag_count =
 * ceil(frame_len / max_payload); then (count-1)*unit < frame_len, so no
 * fragment is empty. A parser rejects any packet whose length disagrees.
 *
 * CARRIAGE. The header is exactly 20 octets so it fills one UBH-168 frame
 * payload. With UBH carriage (the default on this network) a packet of L
 * bytes is sent as ceil(L/20) frames of 21 octets, each "tag octet + 20
 * payload bytes" like the freight smart packet (see ubh.h, UBH_168_OCTETS):
 *   tag bit 7 = last frame of this packet, bits 6-0 = frame index 0..126.
 * Padding after the packet is zero and is checked. PLAIN carriage sends the
 * packet bytes as one datagram. The carriage is chosen by the capability
 * bit CALL_CAP_UBH168 that each side advertises in its call offer/answer;
 * UBH is used only when both sides set it.
 *
 * RULES. Freestanding C11: no libc, no malloc, no floating point, no 64-bit
 * division, no __int128. All buffers belong to the caller.
 *
 * HONEST LIMITS. There are no codecs and no camera or microphone capture
 * here. Frames arrive already encoded (Opus, VP8, ...) from the host OS
 * (AVFoundation on macOS) or from bundled libopus/libvpx later; this module
 * only cuts them up and glues them back. UBH carriage costs 5% extra bytes
 * (1 tag octet per 20) plus padding, buys no error correction by itself and
 * gives no security: the datagrams travel inside an already encrypted
 * Carracho session. The CRC-8 catches accidental damage, not attacks. A UBH
 * packet is at most 127 frames (2540 bytes), more than any sane MTU.
 */
#ifndef CALL_RTP_H
#define CALL_RTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CALL_RTP_VERSION    2
#define CALL_RTP_HDR_LEN    20
#define CALL_RTP_MAX_FRAGS  255
#define CALL_RTP_MAX_FRAME  0xFFFFFFu
#define CALL_UBH_FRAME      21 /* UBH_168_OCTETS */
#define CALL_UBH_PAYLOAD    20
#define CALL_UBH_MAX_FRAMES 127
#define CALL_UBH_MAX_PACKET (CALL_UBH_MAX_FRAMES * CALL_UBH_PAYLOAD)
#define CALL_UBH_TAG_LAST   0x80

/* Payload types. Named by number only; the codecs live outside this module. */
#define CALL_PT_VP8  96  /* default video, royalty-free */
#define CALL_PT_VP9  98  /* royalty-free */
#define CALL_PT_AV1  45  /* royalty-free */
#define CALL_PT_OPUS 111 /* default audio, royalty-free */
#define CALL_PT_DATA 100 /* opaque data channel (AI-to-AI, files) */
#define CALL_PT_FEC  127 /* XOR parity, see call_fec.h */

/* Capability bits exchanged during call setup. */
#define CALL_CAP_UBH168      (1u << 0)
#define CALL_CAP_FEC         (1u << 1)
#define CALL_CAP_SIMULCAST   (1u << 2)
#define CALL_CAP_CC_FEEDBACK (1u << 3)

typedef enum { CALL_CARRIAGE_PLAIN = 0, CALL_CARRIAGE_UBH168 = 1 } call_carriage_t;

enum {
    CALL_OK = 0,
    CALL_ERR_ARG = -1,
    CALL_ERR_SHORT = -2,  /* input truncated */
    CALL_ERR_SPACE = -3,  /* output buffer too small */
    CALL_ERR_FORMAT = -4, /* malformed input */
    CALL_ERR_CRC = -5,
    CALL_ERR_STATE = -6,
    CALL_ERR_FULL = -7,
    CALL_ERR_AUTH = -8,
};

typedef struct call_rtp_hdr {
    uint8_t marker;   /* 1 on the last fragment of a frame */
    uint8_t keyframe; /* 1 if the frame is independently decodable */
    uint8_t layer;    /* simulcast layer 0..15 */
    uint8_t pt;       /* payload type 0..127 */
    uint16_t seq;
    uint32_t ts;
    uint32_t ssrc;
    uint16_t frame;
    uint8_t frag_idx;
    uint8_t frag_cnt;
    uint32_t frame_len;
} call_rtp_hdr_t;

/* Byte offset and length of fragment h->frag_idx inside its frame. */
bool call_rtp_frag_span(const call_rtp_hdr_t *h, uint32_t *off, uint32_t *len);

/* Write the 20-octet header. Returns CALL_RTP_HDR_LEN or a CALL_ERR_*. */
int call_rtp_hdr_write(const call_rtp_hdr_t *h, uint8_t *out, uint32_t cap);

/* Parse a whole packet (header + payload). Checks version, CRC, field
 * ranges and that len equals header + derived fragment length. Returns the
 * payload length (>= 1) and sets *payload, or a CALL_ERR_*. */
int call_rtp_parse(const uint8_t *pkt, uint32_t len, call_rtp_hdr_t *h, const uint8_t **payload);

/* Total packet length implied by a header (header + fragment). */
uint32_t call_rtp_packet_len(const call_rtp_hdr_t *h);

/* ---- packetiser ---- */

typedef int (*call_emit_fn)(void *ctx, const uint8_t *pkt, uint32_t len);

typedef struct call_packetizer {
    uint32_t ssrc;
    uint16_t next_seq;
    uint16_t next_frame;
    uint8_t pt;
    uint8_t layer;
    uint16_t mtu; /* largest packet emitted, header included */
} call_packetizer_t;

void call_packetizer_init(call_packetizer_t *p, uint32_t ssrc, uint8_t pt, uint8_t layer,
                          uint16_t mtu, uint16_t first_seq);

/* Cut one encoded frame into packets of at most p->mtu bytes, written into
 * scratch (>= mtu bytes) one at a time and handed to emit. Returns the number
 * of packets, or a CALL_ERR_*. A failing emit aborts with its return value. */
int call_packetize(call_packetizer_t *p, const uint8_t *frame, uint32_t len, uint32_t ts,
                   bool keyframe, uint8_t *scratch, uint32_t scratch_cap, call_emit_fn emit,
                   void *ctx);

/* ---- reassembler ---- */

typedef struct call_reasm_slot {
    uint32_t ssrc;
    uint32_t ts;
    uint32_t frame_len;
    uint32_t last_use;
    uint32_t bitmap[8]; /* received fragments */
    uint16_t frame;
    uint8_t cnt;
    uint8_t got;
    uint8_t keyframe;
    uint8_t layer;
    uint8_t pt;
    uint8_t used;
} call_reasm_slot_t;

typedef struct call_reasm {
    call_reasm_slot_t *slots;
    uint8_t *mem; /* nslots * slot_cap bytes */
    uint32_t nslots;
    uint32_t slot_cap; /* largest frame accepted */
    uint32_t tick;
    uint32_t frames_out;
    uint32_t evicted; /* incomplete frames pushed out by newer ones */
    uint32_t too_big;
} call_reasm_t;

typedef struct call_frame {
    const uint8_t *data; /* valid until the next call_reasm_push */
    uint32_t len;
    uint32_t ssrc;
    uint32_t ts;
    uint16_t frame;
    uint8_t keyframe;
    uint8_t layer;
    uint8_t pt;
} call_frame_t;

void call_reasm_init(call_reasm_t *r, call_reasm_slot_t *slots, uint32_t nslots, uint8_t *mem,
                     uint32_t slot_cap);

/* Feed one parsed packet. Returns 1 and fills *out when its frame completes,
 * 0 when more fragments are needed, or a CALL_ERR_* for an inconsistent
 * fragment (dropped). */
int call_reasm_push(call_reasm_t *r, const call_rtp_hdr_t *h, const uint8_t *payload,
                    call_frame_t *out);

/* ---- carriage ---- */

call_carriage_t call_rtp_negotiate(uint32_t local_caps, uint32_t remote_caps);

/* Bytes needed on the wire for a packet of len bytes. */
uint32_t call_rtp_wire_len(call_carriage_t c, uint32_t len);

/* Wrap a packet for the wire. Returns the wire length or a CALL_ERR_*. */
int call_rtp_encap(call_carriage_t c, const uint8_t *pkt, uint32_t len, uint8_t *out, uint32_t cap);

/* Unwrap a datagram into packet bytes. For UBH the packet length comes from
 * the RTP header in the first frame (call_rtp_packet_len), unless raw_len is
 * nonzero, which states it (used for non-RTP payloads such as signalling).
 * Returns the packet length or a CALL_ERR_*. */
int call_rtp_decap(call_carriage_t c, const uint8_t *wire, uint32_t n, uint8_t *out, uint32_t cap,
                   uint32_t raw_len);

/* Generic UBH-168 framing of any byte string (no RTP header assumed). */
int call_ubh_wrap(const uint8_t *in, uint32_t len, uint8_t *out, uint32_t cap);
/* Returns the number of payload bytes carried (frames*20) or CALL_ERR_*. */
int call_ubh_unwrap(const uint8_t *wire, uint32_t n, uint8_t *out, uint32_t cap);

#endif /* CALL_RTP_H */
