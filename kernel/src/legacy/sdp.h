/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sdp.h — bounded SDP (RFC 4566) parse/build for audio offers/answers.
 *
 * Parses the lines a voice call needs: v=, o=, s=, c= (connection address),
 * t=, and an audio m= line with its payload type list, plus the a=rtpmap and
 * a=fmtp attributes and a=sendrecv/sendonly/recvonly/inactive direction. The
 * builder emits a minimal but valid single-audio-stream description.
 *
 * Bounded by construction: a fixed number of payload types and attributes,
 * every field a slice into the caller buffer. Lines it does not model are
 * skipped, not rejected (an SDP may legally carry extra attributes).
 *
 * HONEST LIMITS. Audio, one media section. No video, no ICE/DTLS-SRTP
 * negotiation, no bundle or rtcp-mux semantics beyond carrying the attribute
 * through. It is a data model, not a media negotiator.
 */
#ifndef ZXV_LEGACY_SDP_H
#define ZXV_LEGACY_SDP_H

#include <stdbool.h>
#include <stdint.h>

#include "sip.h" /* sip_slice */

#define SDP_MAX_PT    16
#define SDP_MAX_ATTRS 24

typedef enum {
    SDP_DIR_SENDRECV = 0,
    SDP_DIR_SENDONLY,
    SDP_DIR_RECVONLY,
    SDP_DIR_INACTIVE,
} sdp_dir;

typedef struct {
    uint8_t pt;         /* payload type number */
    sip_slice encoding; /* e.g. "PCMU" from a=rtpmap (may be empty) */
    uint32_t clock;     /* clock rate from rtpmap, 0 if unknown */
} sdp_rtpmap;

typedef struct {
    /* connection */
    sip_slice conn_addr; /* IP in the c= line */
    /* audio media */
    bool has_audio;
    uint16_t audio_port;
    sip_slice transport; /* e.g. "RTP/AVP" */
    uint8_t pt[SDP_MAX_PT];
    uint32_t npt;
    sdp_rtpmap rtpmap[SDP_MAX_PT];
    uint32_t nrtpmap;
    sdp_dir dir;
} sdp_session;

/* Parse an SDP body. Returns true if at least a v= line parsed. */
bool sdp_parse(const uint8_t *buf, uint32_t len, sdp_session *s);

/* Look up the encoding name/clock for a payload type. false if no rtpmap. */
bool sdp_pt_rtpmap(const sdp_session *s, uint8_t pt, sip_slice *enc, uint32_t *clock);

/* ===== builder ===== */
typedef struct {
    uint8_t pt;
    const char *encoding; /* NULL to omit rtpmap (e.g. static PT 0/8) */
    uint32_t clock;
} sdp_build_pt;

typedef struct {
    uint32_t sess_id; /* o= session id */
    uint32_t sess_ver;
    const char *unicast_addr; /* o= and c= address (IPv4) */
    uint16_t audio_port;
    const char *transport; /* default "RTP/AVP" if NULL */
    const sdp_build_pt *pts;
    uint32_t npts;
    sdp_dir dir;
} sdp_build;

/* Build an audio SDP offer/answer. Returns byte count or 0 on overflow. */
uint32_t sdp_build_audio(const sdp_build *b, uint8_t *out, uint32_t cap);

#endif /* ZXV_LEGACY_SDP_H */
