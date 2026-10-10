/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sdp.c — see sdp.h. */
#include "sdp.h"
#include "legacy_util.h"

/* bounded unsigned parse from slice, returns value and consumes digits */
static uint32_t slice_u32(const char *p, uint32_t len, uint32_t *consumed)
{
    uint32_t v = 0, i = 0;
    while (i < len && lg_is_digit((uint8_t) p[i]) && i < 9) {
        v = v * 10 + (uint32_t) (p[i] - '0');
        i++;
    }
    *consumed = i;
    return v;
}

/* split a line into whitespace tokens, up to maxtok. Returns token count. */
static uint32_t tokenize(const char *p, uint32_t len, sip_slice *tok, uint32_t maxtok)
{
    uint32_t n = 0, i = 0;
    while (i < len && n < maxtok) {
        while (i < len && p[i] == ' ') i++;
        if (i >= len) break;
        uint32_t start = i;
        while (i < len && p[i] != ' ') i++;
        tok[n].p = p + start;
        tok[n].len = i - start;
        n++;
    }
    return n;
}

static bool next_line(const uint8_t *buf, uint32_t len, uint32_t pos, uint32_t *le, uint32_t *nx)
{
    uint32_t i = pos;
    while (i < len) {
        if (buf[i] == '\n') {
            uint32_t e = i;
            if (e > pos && buf[e - 1] == '\r') e--;
            *le = e;
            *nx = i + 1;
            return true;
        }
        i++;
    }
    if (i > pos) { /* last line without trailing newline */
        *le = i;
        *nx = i;
        return true;
    }
    return false;
}

bool sdp_parse(const uint8_t *buf, uint32_t len, sdp_session *s)
{
    if (!buf) return false;
    s->conn_addr = (sip_slice){0, 0};
    s->has_audio = false;
    s->audio_port = 0;
    s->transport = (sip_slice){0, 0};
    s->npt = 0;
    s->nrtpmap = 0;
    s->dir = SDP_DIR_SENDRECV;
    bool saw_v = false;
    bool in_audio = false; /* attributes after an audio m= belong to it */

    uint32_t pos = 0, le, nx;
    while (pos < len && next_line(buf, len, pos, &le, &nx)) {
        uint32_t llen = le - pos;
        if (llen >= 2 && buf[pos + 1] == '=') {
            char type = (char) buf[pos];
            const char *val = (const char *) buf + pos + 2;
            uint32_t vlen = llen - 2;
            if (type == 'v') {
                saw_v = true;
            } else if (type == 'c') {
                /* c=IN IP4 <addr> */
                sip_slice t[3];
                uint32_t nt = tokenize(val, vlen, t, 3);
                if (nt == 3) s->conn_addr = t[2];
            } else if (type == 'm') {
                /* m=audio <port> <transport> <pt>... */
                sip_slice t[4 + SDP_MAX_PT];
                uint32_t nt = tokenize(val, vlen, t, 4 + SDP_MAX_PT);
                in_audio = (nt >= 1 && t[0].len == 5 && lg_ascii_ieq(t[0].p, "audio", 5));
                if (in_audio) {
                    s->has_audio = true;
                    uint32_t c;
                    /* "m=audio" alone has no port token: t[1] is unset then. */
                    s->audio_port = nt >= 2 ? (uint16_t) slice_u32(t[1].p, t[1].len, &c) : 0;
                    if (nt >= 3) s->transport = t[2];
                    s->npt = 0;
                    for (uint32_t k = 3; k < nt && s->npt < SDP_MAX_PT; k++) {
                        uint32_t c2;
                        uint32_t v = slice_u32(t[k].p, t[k].len, &c2);
                        if (c2 == t[k].len && v <= 127) s->pt[s->npt++] = (uint8_t) v;
                    }
                } else {
                    /* non-audio media section: stop collecting audio attrs */
                }
            } else if (type == 'a' && in_audio) {
                if (vlen == 8 && lg_ascii_ieq(val, "sendrecv", 8))
                    s->dir = SDP_DIR_SENDRECV;
                else if (vlen == 8 && lg_ascii_ieq(val, "sendonly", 8))
                    s->dir = SDP_DIR_SENDONLY;
                else if (vlen == 8 && lg_ascii_ieq(val, "recvonly", 8))
                    s->dir = SDP_DIR_RECVONLY;
                else if (vlen == 8 && lg_ascii_ieq(val, "inactive", 8))
                    s->dir = SDP_DIR_INACTIVE;
                else if (vlen > 7 && lg_ascii_ieq(val, "rtpmap:", 7)) {
                    /* a=rtpmap:<pt> <enc>/<clock>[/..] */
                    const char *rp = val + 7;
                    uint32_t rlen = vlen - 7;
                    uint32_t c;
                    uint32_t pt = slice_u32(rp, rlen, &c);
                    if (c && c < rlen && rp[c] == ' ' && s->nrtpmap < SDP_MAX_PT) {
                        const char *e = rp + c + 1;
                        uint32_t elen = rlen - c - 1;
                        uint32_t j = 0;
                        while (j < elen && e[j] != '/') j++;
                        sdp_rtpmap *rm = &s->rtpmap[s->nrtpmap++];
                        rm->pt = (uint8_t) pt;
                        rm->encoding.p = e;
                        rm->encoding.len = j;
                        rm->clock = 0;
                        if (j < elen) {
                            uint32_t cc;
                            rm->clock = slice_u32(e + j + 1, elen - j - 1, &cc);
                        }
                    }
                }
            }
        }
        if (nx == pos) break;
        pos = nx;
    }
    return saw_v;
}

bool sdp_pt_rtpmap(const sdp_session *s, uint8_t pt, sip_slice *enc, uint32_t *clock)
{
    for (uint32_t i = 0; i < s->nrtpmap; i++) {
        if (s->rtpmap[i].pt == pt) {
            if (enc) *enc = s->rtpmap[i].encoding;
            if (clock) *clock = s->rtpmap[i].clock;
            return true;
        }
    }
    return false;
}

uint32_t sdp_build_audio(const sdp_build *b, uint8_t *out, uint32_t cap)
{
    lg_writer w;
    lg_w_init(&w, out, cap);
    const char *addr = b->unicast_addr ? b->unicast_addr : "0.0.0.0";
    const char *tp = b->transport ? b->transport : "RTP/AVP";

    lg_w_str(&w, "v=0\r\n");
    lg_w_str(&w, "o=- ");
    lg_w_u32(&w, b->sess_id);
    lg_w_byte(&w, ' ');
    lg_w_u32(&w, b->sess_ver);
    lg_w_str(&w, " IN IP4 ");
    lg_w_str(&w, addr);
    lg_w_str(&w, "\r\n");
    lg_w_str(&w, "s=-\r\n");
    lg_w_str(&w, "c=IN IP4 ");
    lg_w_str(&w, addr);
    lg_w_str(&w, "\r\n");
    lg_w_str(&w, "t=0 0\r\n");

    lg_w_str(&w, "m=audio ");
    lg_w_u32(&w, b->audio_port);
    lg_w_byte(&w, ' ');
    lg_w_str(&w, tp);
    for (uint32_t i = 0; i < b->npts; i++) {
        lg_w_byte(&w, ' ');
        lg_w_u32(&w, b->pts[i].pt);
    }
    lg_w_str(&w, "\r\n");
    for (uint32_t i = 0; i < b->npts; i++) {
        if (b->pts[i].encoding) {
            lg_w_str(&w, "a=rtpmap:");
            lg_w_u32(&w, b->pts[i].pt);
            lg_w_byte(&w, ' ');
            lg_w_str(&w, b->pts[i].encoding);
            lg_w_byte(&w, '/');
            lg_w_u32(&w, b->pts[i].clock);
            lg_w_str(&w, "\r\n");
        }
    }
    const char *d = "sendrecv";
    if (b->dir == SDP_DIR_SENDONLY)
        d = "sendonly";
    else if (b->dir == SDP_DIR_RECVONLY)
        d = "recvonly";
    else if (b->dir == SDP_DIR_INACTIVE)
        d = "inactive";
    lg_w_str(&w, "a=");
    lg_w_str(&w, d);
    lg_w_str(&w, "\r\n");

    if (!lg_w_ok(&w)) return 0;
    return w.len;
}
