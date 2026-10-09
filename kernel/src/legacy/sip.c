/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sip.c — see sip.h. Bounded line-oriented SIP parser and builder. */
#include "sip.h"
#include "legacy_util.h"

/* --- small local scanners, all bounded by an explicit end pointer --- */

static bool ci_eq(const char *a, uint32_t alen, const char *lit)
{
    uint32_t i = 0;
    for (; i < alen; i++) {
        if (lit[i] == 0) return false;
        if (lg_lower((uint8_t) a[i]) != lg_lower((uint8_t) lit[i])) return false;
    }
    return lit[i] == 0;
}

bool sip_slice_ieq(sip_slice s, const char *lit)
{
    return ci_eq(s.p, s.len, lit);
}

/* trim leading/trailing linear whitespace (space, tab) */
static sip_slice trim(const char *p, uint32_t len)
{
    sip_slice s;
    while (len && (p[0] == ' ' || p[0] == '\t')) {
        p++;
        len--;
    }
    while (len && (p[len - 1] == ' ' || p[len - 1] == '\t')) len--;
    s.p = p;
    s.len = len;
    return s;
}

/* Find end of a CRLF-terminated line starting at *pos (index into buf[len]).
 * On success sets *line_end to the index of the CR (or LF if bare LF) and
 * *next to the index after the line terminator. Accepts CRLF or bare LF.
 * Returns false if no terminator before len. */
static bool next_line(const uint8_t *buf, uint32_t len, uint32_t pos, uint32_t *line_end,
                      uint32_t *next)
{
    uint32_t i = pos;
    while (i < len) {
        if (buf[i] == '\r') {
            if (i + 1 < len && buf[i + 1] == '\n') {
                *line_end = i;
                *next = i + 2;
                return true;
            }
            return false; /* bare CR is malformed */
        }
        if (buf[i] == '\n') {
            *line_end = i;
            *next = i + 1;
            return true;
        }
        i++;
    }
    return false;
}

/* map a header name slice to a canonical id, handling compact forms */
static sip_hdr_id classify(const char *name, uint32_t nlen)
{
    if (nlen == 1) {
        switch (lg_lower((uint8_t) name[0])) {
        case 'v':
            return SIP_H_VIA;
        case 'f':
            return SIP_H_FROM;
        case 't':
            return SIP_H_TO;
        case 'i':
            return SIP_H_CALL_ID;
        case 'm':
            return SIP_H_CONTACT;
        case 'c':
            return SIP_H_CONTENT_TYPE;
        case 'l':
            return SIP_H_CONTENT_LENGTH;
        default:
            return SIP_H_OTHER;
        }
    }
    if (ci_eq(name, nlen, "Via")) return SIP_H_VIA;
    if (ci_eq(name, nlen, "From")) return SIP_H_FROM;
    if (ci_eq(name, nlen, "To")) return SIP_H_TO;
    if (ci_eq(name, nlen, "Call-ID")) return SIP_H_CALL_ID;
    if (ci_eq(name, nlen, "CSeq")) return SIP_H_CSEQ;
    if (ci_eq(name, nlen, "Contact")) return SIP_H_CONTACT;
    if (ci_eq(name, nlen, "Max-Forwards")) return SIP_H_MAX_FORWARDS;
    if (ci_eq(name, nlen, "Content-Type")) return SIP_H_CONTENT_TYPE;
    if (ci_eq(name, nlen, "Content-Length")) return SIP_H_CONTENT_LENGTH;
    return SIP_H_OTHER;
}

static bool parse_u16(const char *p, uint32_t len, uint16_t *out)
{
    if (len == 0 || len > 5) return false;
    uint32_t v = 0;
    for (uint32_t i = 0; i < len; i++) {
        if (!lg_is_digit((uint8_t) p[i])) return false;
        v = v * 10 + (uint32_t) (p[i] - '0');
    }
    if (v > 0xFFFF) return false;
    *out = (uint16_t) v;
    return true;
}

static bool parse_u32(const char *p, uint32_t len, uint32_t *out)
{
    if (len == 0 || len > 9) return false; /* bound to avoid overflow */
    uint32_t v = 0;
    for (uint32_t i = 0; i < len; i++) {
        if (!lg_is_digit((uint8_t) p[i])) return false;
        v = v * 10 + (uint32_t) (p[i] - '0');
    }
    *out = v;
    return true;
}

bool sip_parse(const uint8_t *buf, uint32_t len, sip_msg *m)
{
    if (!buf || len == 0) return false;
    m->nhdr = 0;
    m->body = 0;
    m->body_len = 0;
    m->has_content_length = false;
    m->content_length = 0;
    m->via = m->from = m->to = m->call_id = m->cseq = m->contact = m->content_type =
        (sip_slice){0, 0};

    uint32_t le, nx;
    if (!next_line(buf, len, 0, &le, &nx)) return false;

    /* first line: split on spaces into three tokens */
    const char *l = (const char *) buf;
    uint32_t llen = le;
    uint32_t s1 = 0;
    while (s1 < llen && l[s1] != ' ') s1++;
    if (s1 == 0 || s1 >= llen) return false;
    uint32_t s2 = s1 + 1;
    while (s2 < llen && l[s2] != ' ') s2++;
    if (s2 >= llen || s2 == s1 + 1) return false;

    sip_slice t0 = {l, s1};
    sip_slice t1 = {l + s1 + 1, s2 - s1 - 1};
    sip_slice t2 = {l + s2 + 1, llen - s2 - 1};
    if (t2.len == 0) return false;

    if (ci_eq(t0.p, t0.len, "SIP/2.0")) {
        m->kind = SIP_MSG_STATUS;
        m->version = t0;
        if (!parse_u16(t1.p, t1.len, &m->status)) return false;
        if (m->status < 100 || m->status > 699) return false;
        m->reason = t2;
    } else {
        m->kind = SIP_MSG_REQUEST;
        m->method = t0;
        m->ruri = t1;
        m->version = t2;
        if (!ci_eq(t2.p, t2.len, "SIP/2.0")) return false;
    }

    /* headers */
    uint32_t pos = nx;
    for (;;) {
        if (!next_line(buf, len, pos, &le, &nx)) return false;
        if (le == pos) {
            /* empty line: end of headers, body starts at nx */
            pos = nx;
            break;
        }
        /* note: we do not fold continuation lines; RFC 3261 deprecates them. */
        const char *hl = (const char *) buf + pos;
        uint32_t hlen = le - pos;
        uint32_t c = 0;
        while (c < hlen && hl[c] != ':') c++;
        if (c == 0 || c >= hlen) return false; /* no colon */
        sip_slice name = {hl, c};
        sip_slice value = trim(hl + c + 1, hlen - c - 1);

        if (m->nhdr >= SIP_MAX_HEADERS) return false;
        sip_hdr_id id = classify(name.p, name.len);
        m->hdr[m->nhdr].id = id;
        m->hdr[m->nhdr].name = name;
        m->hdr[m->nhdr].value = value;
        m->nhdr++;

        switch (id) {
        case SIP_H_VIA:
            if (!m->via.p) m->via = value;
            break;
        case SIP_H_FROM:
            if (!m->from.p) m->from = value;
            break;
        case SIP_H_TO:
            if (!m->to.p) m->to = value;
            break;
        case SIP_H_CALL_ID:
            if (!m->call_id.p) m->call_id = value;
            break;
        case SIP_H_CSEQ:
            if (!m->cseq.p) m->cseq = value;
            break;
        case SIP_H_CONTACT:
            if (!m->contact.p) m->contact = value;
            break;
        case SIP_H_CONTENT_TYPE:
            if (!m->content_type.p) m->content_type = value;
            break;
        case SIP_H_CONTENT_LENGTH:
            if (!parse_u32(value.p, value.len, &m->content_length)) return false;
            m->has_content_length = true;
            break;
        default:
            break;
        }
        pos = nx;
    }

    /* body */
    uint32_t avail = len - pos;
    if (m->has_content_length) {
        if (m->content_length > avail) return false; /* declared body exceeds buffer */
        m->body = buf + pos;
        m->body_len = m->content_length;
    } else {
        m->body = buf + pos;
        m->body_len = avail;
    }
    return true;
}

bool sip_find(const sip_msg *m, sip_hdr_id id, sip_slice *out)
{
    for (uint32_t i = 0; i < m->nhdr; i++) {
        if (m->hdr[i].id == id) {
            *out = m->hdr[i].value;
            return true;
        }
    }
    return false;
}

uint32_t sip_build_msg(const sip_build *b, uint8_t *out, uint32_t cap)
{
    lg_writer w;
    lg_w_init(&w, out, cap);

    if (b->is_request) {
        if (!b->method || !b->ruri) return 0;
        lg_w_str(&w, b->method);
        lg_w_byte(&w, ' ');
        lg_w_str(&w, b->ruri);
        lg_w_str(&w, " SIP/2.0\r\n");
    } else {
        if (b->status < 100 || b->status > 699 || !b->reason) return 0;
        lg_w_str(&w, "SIP/2.0 ");
        lg_w_u32(&w, b->status);
        lg_w_byte(&w, ' ');
        lg_w_str(&w, b->reason);
        lg_w_str(&w, "\r\n");
    }

    for (uint32_t i = 0; i < b->nheaders; i++) {
        if (!b->headers[i].name) return 0;
        lg_w_str(&w, b->headers[i].name);
        lg_w_str(&w, ": ");
        if (b->headers[i].value) lg_w_str(&w, b->headers[i].value);
        lg_w_str(&w, "\r\n");
    }

    if (b->body_len) {
        if (b->content_type) {
            lg_w_str(&w, "Content-Type: ");
            lg_w_str(&w, b->content_type);
            lg_w_str(&w, "\r\n");
        }
    }
    lg_w_str(&w, "Content-Length: ");
    lg_w_u32(&w, b->body_len);
    lg_w_str(&w, "\r\n\r\n");
    if (b->body_len) lg_w_bytes(&w, b->body, b->body_len);

    if (!lg_w_ok(&w)) return 0;
    return w.len;
}
