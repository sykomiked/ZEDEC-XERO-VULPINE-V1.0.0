/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* call_rtp.c — media packetisation, reassembly and UBH-168 carriage. */
#include "call_rtp.h"
#include "call_common.h"

bool call_rtp_frag_span(const call_rtp_hdr_t *h, uint32_t *off, uint32_t *len)
{
    if (h->frag_cnt == 0 || h->frag_idx >= h->frag_cnt) return false;
    if (h->frame_len == 0 || h->frame_len > CALL_RTP_MAX_FRAME) return false;
    if (h->frag_cnt > h->frame_len) return false;
    uint32_t cnt = h->frag_cnt;
    uint32_t unit = (h->frame_len + cnt - 1) / cnt;
    if (unit * (cnt - 1) >= h->frame_len) return false; /* last fragment would be empty */
    uint32_t o = unit * h->frag_idx;
    uint32_t l = h->frame_len - o;
    if (l > unit) l = unit;
    *off = o;
    *len = l;
    return true;
}

uint32_t call_rtp_packet_len(const call_rtp_hdr_t *h)
{
    uint32_t off, len;
    if (!call_rtp_frag_span(h, &off, &len)) return 0;
    return CALL_RTP_HDR_LEN + len;
}

int call_rtp_hdr_write(const call_rtp_hdr_t *h, uint8_t *out, uint32_t cap)
{
    if (!h || !out) return CALL_ERR_ARG;
    if (cap < CALL_RTP_HDR_LEN) return CALL_ERR_SPACE;
    if (h->pt > 127 || h->layer > 15 || h->frag_cnt == 0 || h->frag_idx >= h->frag_cnt ||
        h->frame_len == 0 || h->frame_len > CALL_RTP_MAX_FRAME)
        return CALL_ERR_ARG;
    out[0] = (uint8_t) ((CALL_RTP_VERSION << 6) | (h->marker ? 0x20 : 0) |
                        (h->keyframe ? 0x10 : 0) | (h->layer & 0x0f));
    out[1] = h->pt;
    call_put16(out + 2, h->seq);
    call_put32(out + 4, h->ts);
    call_put32(out + 8, h->ssrc);
    call_put16(out + 12, h->frame);
    out[14] = h->frag_idx;
    out[15] = h->frag_cnt;
    call_put24(out + 16, h->frame_len);
    out[19] = call_crc8(out, 19);
    return CALL_RTP_HDR_LEN;
}

int call_rtp_parse(const uint8_t *pkt, uint32_t len, call_rtp_hdr_t *h, const uint8_t **payload)
{
    if (!pkt || !h) return CALL_ERR_ARG;
    if (len < CALL_RTP_HDR_LEN) return CALL_ERR_SHORT;
    if ((pkt[0] >> 6) != CALL_RTP_VERSION) return CALL_ERR_FORMAT;
    if (pkt[1] & 0x80) return CALL_ERR_FORMAT;
    if (call_crc8(pkt, 19) != pkt[19]) return CALL_ERR_CRC;
    h->marker = (pkt[0] >> 5) & 1;
    h->keyframe = (pkt[0] >> 4) & 1;
    h->layer = pkt[0] & 0x0f;
    h->pt = pkt[1];
    h->seq = call_get16(pkt + 2);
    h->ts = call_get32(pkt + 4);
    h->ssrc = call_get32(pkt + 8);
    h->frame = call_get16(pkt + 12);
    h->frag_idx = pkt[14];
    h->frag_cnt = pkt[15];
    h->frame_len = call_get24(pkt + 16);
    uint32_t off, fl;
    if (!call_rtp_frag_span(h, &off, &fl)) return CALL_ERR_FORMAT;
    if (h->marker != (h->frag_idx + 1u == h->frag_cnt)) return CALL_ERR_FORMAT;
    if (len != CALL_RTP_HDR_LEN + fl) return CALL_ERR_FORMAT;
    if (payload) *payload = pkt + CALL_RTP_HDR_LEN;
    return (int) fl;
}

void call_packetizer_init(call_packetizer_t *p, uint32_t ssrc, uint8_t pt, uint8_t layer,
                          uint16_t mtu, uint16_t first_seq)
{
    p->ssrc = ssrc;
    p->pt = pt;
    p->layer = layer;
    p->mtu = mtu;
    p->next_seq = first_seq;
    p->next_frame = 0;
}

int call_packetize(call_packetizer_t *p, const uint8_t *frame, uint32_t len, uint32_t ts,
                   bool keyframe, uint8_t *scratch, uint32_t scratch_cap, call_emit_fn emit,
                   void *ctx)
{
    if (!p || !frame || !scratch || !emit) return CALL_ERR_ARG;
    if (len == 0 || len > CALL_RTP_MAX_FRAME) return CALL_ERR_ARG;
    if (p->mtu <= CALL_RTP_HDR_LEN || scratch_cap < p->mtu) return CALL_ERR_SPACE;
    uint32_t maxp = (uint32_t) p->mtu - CALL_RTP_HDR_LEN;
    uint32_t cnt = (len + maxp - 1) / maxp;
    if (cnt > CALL_RTP_MAX_FRAGS) return CALL_ERR_SPACE;
    call_rtp_hdr_t h;
    h.keyframe = keyframe ? 1 : 0;
    h.layer = p->layer;
    h.pt = p->pt;
    h.ts = ts;
    h.ssrc = p->ssrc;
    h.frame = p->next_frame;
    h.frag_cnt = (uint8_t) cnt;
    h.frame_len = len;
    for (uint32_t i = 0; i < cnt; i++) {
        uint32_t off, fl;
        h.frag_idx = (uint8_t) i;
        h.marker = (i + 1 == cnt) ? 1 : 0;
        h.seq = p->next_seq;
        if (!call_rtp_frag_span(&h, &off, &fl)) return CALL_ERR_ARG;
        int w = call_rtp_hdr_write(&h, scratch, scratch_cap);
        if (w < 0) return w;
        call_copy(scratch + CALL_RTP_HDR_LEN, frame + off, fl);
        p->next_seq++;
        int e = emit(ctx, scratch, CALL_RTP_HDR_LEN + fl);
        if (e < 0) return e;
    }
    p->next_frame++;
    return (int) cnt;
}

/* ---- reassembler ---- */

void call_reasm_init(call_reasm_t *r, call_reasm_slot_t *slots, uint32_t nslots, uint8_t *mem,
                     uint32_t slot_cap)
{
    r->slots = slots;
    r->mem = mem;
    r->nslots = nslots;
    r->slot_cap = slot_cap;
    r->tick = 0;
    r->frames_out = 0;
    r->evicted = 0;
    r->too_big = 0;
    for (uint32_t i = 0; i < nslots; i++) call_fill(&slots[i], 0, sizeof(slots[i]));
}

int call_reasm_push(call_reasm_t *r, const call_rtp_hdr_t *h, const uint8_t *payload,
                    call_frame_t *out)
{
    uint32_t off, fl;
    if (!r || !h || !payload || r->nslots == 0) return CALL_ERR_ARG;
    if (!call_rtp_frag_span(h, &off, &fl)) return CALL_ERR_FORMAT;
    if (h->frame_len > r->slot_cap) {
        r->too_big++;
        return CALL_ERR_SPACE;
    }
    r->tick++;
    call_reasm_slot_t *s = NULL;
    uint32_t si = 0;
    for (uint32_t i = 0; i < r->nslots; i++) {
        call_reasm_slot_t *c = &r->slots[i];
        if (c->used && c->ssrc == h->ssrc && c->frame == h->frame && c->ts == h->ts) {
            s = c;
            si = i;
            break;
        }
    }
    if (s && (s->cnt != h->frag_cnt || s->frame_len != h->frame_len)) return CALL_ERR_FORMAT;
    if (!s) {
        /* free slot, else least recently used */
        uint32_t best = 0, best_age = 0;
        for (uint32_t i = 0; i < r->nslots; i++) {
            if (!r->slots[i].used) {
                best = i;
                best_age = 0xffffffffu;
                break;
            }
            uint32_t age = r->tick - r->slots[i].last_use;
            if (age > best_age || i == 0) {
                best = i;
                best_age = age;
            }
        }
        s = &r->slots[best];
        si = best;
        if (s->used) r->evicted++;
        call_fill(s, 0, sizeof(*s));
        s->used = 1;
        s->ssrc = h->ssrc;
        s->frame = h->frame;
        s->ts = h->ts;
        s->cnt = h->frag_cnt;
        s->frame_len = h->frame_len;
        s->pt = h->pt;
        s->layer = h->layer;
    }
    s->last_use = r->tick;
    if (h->keyframe) s->keyframe = 1;
    uint32_t w = h->frag_idx >> 5, b = 1u << (h->frag_idx & 31);
    if (s->bitmap[w] & b) return 0; /* duplicate */
    s->bitmap[w] |= b;
    s->got++;
    uint8_t *buf = r->mem + si * r->slot_cap;
    call_copy(buf + off, payload, fl);
    if (s->got != s->cnt) return 0;
    if (out) {
        out->data = buf;
        out->len = s->frame_len;
        out->ssrc = s->ssrc;
        out->ts = s->ts;
        out->frame = s->frame;
        out->keyframe = s->keyframe;
        out->layer = s->layer;
        out->pt = s->pt;
    }
    s->used = 0; /* buffer stays intact until the slot is reused */
    r->frames_out++;
    return 1;
}

/* ---- carriage ---- */

call_carriage_t call_rtp_negotiate(uint32_t local_caps, uint32_t remote_caps)
{
    return ((local_caps & remote_caps) & CALL_CAP_UBH168) ? CALL_CARRIAGE_UBH168
                                                          : CALL_CARRIAGE_PLAIN;
}

uint32_t call_rtp_wire_len(call_carriage_t c, uint32_t len)
{
    if (c == CALL_CARRIAGE_PLAIN) return len;
    return ((len + CALL_UBH_PAYLOAD - 1) / CALL_UBH_PAYLOAD) * CALL_UBH_FRAME;
}

int call_ubh_wrap(const uint8_t *in, uint32_t len, uint8_t *out, uint32_t cap)
{
    if (!in || !out || len == 0) return CALL_ERR_ARG;
    uint32_t frames = (len + CALL_UBH_PAYLOAD - 1) / CALL_UBH_PAYLOAD;
    if (frames > CALL_UBH_MAX_FRAMES) return CALL_ERR_ARG;
    if (cap < frames * CALL_UBH_FRAME) return CALL_ERR_SPACE;
    for (uint32_t f = 0; f < frames; f++) {
        uint8_t *o = out + f * CALL_UBH_FRAME;
        o[0] = (uint8_t) (f | (f + 1 == frames ? CALL_UBH_TAG_LAST : 0));
        uint32_t base = f * CALL_UBH_PAYLOAD;
        for (uint32_t i = 0; i < CALL_UBH_PAYLOAD; i++)
            o[1 + i] = (base + i < len) ? in[base + i] : 0;
    }
    return (int) (frames * CALL_UBH_FRAME);
}

int call_ubh_unwrap(const uint8_t *wire, uint32_t n, uint8_t *out, uint32_t cap)
{
    if (!wire || !out) return CALL_ERR_ARG;
    if (n == 0 || n % CALL_UBH_FRAME) return CALL_ERR_FORMAT;
    uint32_t frames = n / CALL_UBH_FRAME;
    if (frames > CALL_UBH_MAX_FRAMES) return CALL_ERR_FORMAT;
    if (cap < frames * CALL_UBH_PAYLOAD) return CALL_ERR_SPACE;
    for (uint32_t f = 0; f < frames; f++) {
        const uint8_t *w = wire + f * CALL_UBH_FRAME;
        uint8_t want = (uint8_t) (f | (f + 1 == frames ? CALL_UBH_TAG_LAST : 0));
        if (w[0] != want) return CALL_ERR_FORMAT;
        call_copy(out + f * CALL_UBH_PAYLOAD, w + 1, CALL_UBH_PAYLOAD);
    }
    return (int) (frames * CALL_UBH_PAYLOAD);
}

int call_rtp_encap(call_carriage_t c, const uint8_t *pkt, uint32_t len, uint8_t *out, uint32_t cap)
{
    if (!pkt || !out || len == 0) return CALL_ERR_ARG;
    if (c == CALL_CARRIAGE_PLAIN) {
        if (cap < len) return CALL_ERR_SPACE;
        call_copy(out, pkt, len);
        return (int) len;
    }
    return call_ubh_wrap(pkt, len, out, cap);
}

int call_rtp_decap(call_carriage_t c, const uint8_t *wire, uint32_t n, uint8_t *out, uint32_t cap,
                   uint32_t raw_len)
{
    if (!wire || !out) return CALL_ERR_ARG;
    if (c == CALL_CARRIAGE_PLAIN) {
        if (n == 0) return CALL_ERR_SHORT;
        if (cap < n) return CALL_ERR_SPACE;
        if (raw_len && raw_len != n) return CALL_ERR_FORMAT;
        call_copy(out, wire, n);
        return (int) n;
    }
    int got = call_ubh_unwrap(wire, n, out, cap);
    if (got < 0) return got;
    uint32_t L = raw_len;
    if (!L) {
        call_rtp_hdr_t h;
        if ((uint32_t) got < CALL_RTP_HDR_LEN) return CALL_ERR_SHORT;
        /* parse the header alone: check CRC and derive the length */
        if ((out[0] >> 6) != CALL_RTP_VERSION || call_crc8(out, 19) != out[19])
            return CALL_ERR_FORMAT;
        h.frag_idx = out[14];
        h.frag_cnt = out[15];
        h.frame_len = call_get24(out + 16);
        L = call_rtp_packet_len(&h);
        if (L == 0) return CALL_ERR_FORMAT;
    }
    /* the frame count must be minimal and the padding zero */
    if (L > (uint32_t) got || L + CALL_UBH_PAYLOAD <= (uint32_t) got) return CALL_ERR_FORMAT;
    for (uint32_t i = L; i < (uint32_t) got; i++)
        if (out[i]) return CALL_ERR_FORMAT;
    return (int) L;
}
