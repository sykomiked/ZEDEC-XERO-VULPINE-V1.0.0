/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sip.h — bounded SIP (RFC 3261) message parser and builder.
 *
 * Parses a SIP request or status line and the headers the call bridge needs:
 * Via, From, To, Call-ID, CSeq, Contact, Max-Forwards, Content-Type and
 * Content-Length, including the single-letter compact forms of RFC 3261
 * section 7.3.3 (v=Via, f=From, t=To, i=Call-ID, m=Contact, c=Content-Type,
 * l=Content-Length, s=Subject, k=Supported). The body is returned as a byte
 * span; parse it with sdp.h for audio offers/answers. The builder emits a
 * well-formed message into a caller buffer and never overruns it.
 *
 * Bounded by construction: every field is a (pointer,length) slice into the
 * caller's input buffer, never copied, never NUL-scanned past the given
 * length. Malformed input is rejected, not guessed at.
 *
 * HONEST LIMITS. This is message syntax only. It does not run a transaction
 * or dialog state machine, does not do DNS/SRV, Digest authentication, TLS,
 * or any transport. It is not a SIP stack and no interoperability with any
 * carrier or PBX is claimed; it is a parser/builder a higher layer drives.
 */
#ifndef ZXV_LEGACY_SIP_H
#define ZXV_LEGACY_SIP_H

#include <stdbool.h>
#include <stdint.h>

#define SIP_MAX_HEADERS 48

typedef struct {
    const char *p;
    uint32_t len;
} sip_slice;

typedef enum {
    SIP_MSG_REQUEST,
    SIP_MSG_STATUS,
} sip_msg_kind;

/* A parsed header: canonical name index plus raw name/value slices. */
typedef enum {
    SIP_H_OTHER = 0,
    SIP_H_VIA,
    SIP_H_FROM,
    SIP_H_TO,
    SIP_H_CALL_ID,
    SIP_H_CSEQ,
    SIP_H_CONTACT,
    SIP_H_MAX_FORWARDS,
    SIP_H_CONTENT_TYPE,
    SIP_H_CONTENT_LENGTH,
} sip_hdr_id;

typedef struct {
    sip_hdr_id id;
    sip_slice name;
    sip_slice value;
} sip_header;

typedef struct {
    sip_msg_kind kind;

    /* request line */
    sip_slice method; /* e.g. "INVITE" */
    sip_slice ruri;   /* request URI */
    /* status line */
    uint16_t status;   /* e.g. 200 */
    sip_slice reason;  /* e.g. "OK" */
    sip_slice version; /* "SIP/2.0" */

    sip_header hdr[SIP_MAX_HEADERS];
    uint32_t nhdr;

    const uint8_t *body;
    uint32_t body_len;

    /* convenience: first occurrence of common headers (value slices) */
    sip_slice via, from, to, call_id, cseq, contact, content_type;
    uint32_t content_length; /* parsed; 0 if absent */
    bool has_content_length;
} sip_msg;

/* Parse a full SIP message. Returns true on success. On false the contents of
 * *m are unspecified. Never reads past buf[len]. */
bool sip_parse(const uint8_t *buf, uint32_t len, sip_msg *m);

/* Find a header value by canonical id (first match). Returns false if absent. */
bool sip_find(const sip_msg *m, sip_hdr_id id, sip_slice *out);

/* ===== builder ===== */
typedef struct {
    const char *name;  /* full header name, e.g. "Via" */
    const char *value; /* NUL-terminated value */
} sip_build_hdr;

typedef struct {
    bool is_request;
    /* request */
    const char *method;
    const char *ruri;
    /* status */
    uint16_t status;
    const char *reason;

    const sip_build_hdr *headers;
    uint32_t nheaders;

    const uint8_t *body;
    uint32_t body_len;        /* a Content-Length header is emitted automatically */
    const char *content_type; /* may be NULL if body_len==0 */
} sip_build;

/* Build a message into out[cap]. Returns byte count, or 0 on overflow/invalid.
 * Content-Length is always emitted (RFC 3261 requires it). */
uint32_t sip_build_msg(const sip_build *b, uint8_t *out, uint32_t cap);

/* slice compare against a NUL-terminated literal, case-insensitive. */
bool sip_slice_ieq(sip_slice s, const char *lit);

#endif /* ZXV_LEGACY_SIP_H */
