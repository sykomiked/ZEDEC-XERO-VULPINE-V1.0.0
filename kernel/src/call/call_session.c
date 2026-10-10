/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* call_session.c — call state machine, codec negotiation, signalling. */
#include "call_session.h"
#include "call_common.h"

static const call_codec_t default_codecs[] = {
    {CALL_PT_OPUS, CALL_MEDIA_AUDIO}, {CALL_PT_VP8, CALL_MEDIA_VIDEO},
    {CALL_PT_VP9, CALL_MEDIA_VIDEO},  {CALL_PT_AV1, CALL_MEDIA_VIDEO},
    {CALL_PT_DATA, CALL_MEDIA_DATA},
};

/* ---- wire format ---- */

static bool has_body(uint8_t t)
{
    return t == CALL_MSG_OFFER || t == CALL_MSG_ANSWER;
}

static bool has_reason(uint8_t t)
{
    return t == CALL_MSG_REJECT || t == CALL_MSG_BYE || t == CALL_MSG_CANCEL;
}

int call_sig_write(const call_sig_t *m, uint8_t *out, uint32_t cap)
{
    if (!m || !out || m->type < CALL_MSG_OFFER || m->type > CALL_MSG_CANCEL) return CALL_ERR_ARG;
    if (m->ncodecs > CALL_MAX_CODECS || m->ncands > CALL_ICE_MAX_CANDS) return CALL_ERR_ARG;
    uint32_t body = 0;
    if (has_body(m->type))
        body = 10u + 2u * m->ncodecs + CALL_ICE_UFRAG_LEN + CALL_ICE_PWD_LEN + 1u +
               (uint32_t) m->ncands * CALL_CAND_WIRE;
    else if (has_reason(m->type))
        body = 1;
    if (cap < CALL_SIG_HDR_LEN + body) return CALL_ERR_SPACE;
    call_put16(out, CALL_SIG_MAGIC);
    out[2] = CALL_SIG_VERSION;
    out[3] = m->type;
    call_copy(out + 4, m->call_id, 8);
    call_put32(out + 12, m->from);
    call_put32(out + 16, m->to);
    call_put16(out + 20, m->seq);
    call_put16(out + 22, (uint16_t) body);
    uint8_t *p = out + CALL_SIG_HDR_LEN;
    if (has_body(m->type)) {
        call_put32(p, m->tie);
        call_put32(p + 4, m->caps);
        p[8] = m->media;
        p[9] = m->ncodecs;
        p += 10;
        for (uint32_t i = 0; i < m->ncodecs; i++, p += 2) {
            p[0] = m->codecs[i].pt;
            p[1] = m->codecs[i].kind;
        }
        call_copy(p, m->ufrag, CALL_ICE_UFRAG_LEN);
        call_copy(p + CALL_ICE_UFRAG_LEN, m->pwd, CALL_ICE_PWD_LEN);
        p += CALL_ICE_UFRAG_LEN + CALL_ICE_PWD_LEN;
        *p++ = m->ncands;
        for (uint32_t i = 0; i < m->ncands; i++, p += CALL_CAND_WIRE)
            call_cand_write(&m->cands[i], p, CALL_CAND_WIRE);
    } else if (has_reason(m->type)) {
        *p++ = m->reason;
    }
    return (int) (p - out);
}

int call_sig_parse(const uint8_t *in, uint32_t len, call_sig_t *m)
{
    if (!in || !m) return CALL_ERR_ARG;
    if (len < CALL_SIG_HDR_LEN) return CALL_ERR_SHORT;
    if (call_get16(in) != CALL_SIG_MAGIC || in[2] != CALL_SIG_VERSION) return CALL_ERR_FORMAT;
    call_fill(m, 0, sizeof(*m));
    m->type = in[3];
    if (m->type < CALL_MSG_OFFER || m->type > CALL_MSG_CANCEL) return CALL_ERR_FORMAT;
    call_copy(m->call_id, in + 4, 8);
    m->from = call_get32(in + 12);
    m->to = call_get32(in + 16);
    m->seq = call_get16(in + 20);
    uint32_t body = call_get16(in + 22);
    if (CALL_SIG_HDR_LEN + body != len) return CALL_ERR_FORMAT;
    const uint8_t *p = in + CALL_SIG_HDR_LEN;
    if (has_body(m->type)) {
        if (body < 10) return CALL_ERR_SHORT;
        m->tie = call_get32(p);
        m->caps = call_get32(p + 4);
        m->media = p[8];
        m->ncodecs = p[9];
        if (m->ncodecs > CALL_MAX_CODECS || (m->media & ~7u)) return CALL_ERR_FORMAT;
        uint32_t need = 10u + 2u * m->ncodecs + CALL_ICE_UFRAG_LEN + CALL_ICE_PWD_LEN + 1u;
        if (body < need) return CALL_ERR_SHORT;
        p += 10;
        for (uint32_t i = 0; i < m->ncodecs; i++, p += 2) {
            m->codecs[i].pt = p[0];
            m->codecs[i].kind = p[1];
            if (p[0] > 127 ||
                (p[1] != CALL_MEDIA_AUDIO && p[1] != CALL_MEDIA_VIDEO && p[1] != CALL_MEDIA_DATA))
                return CALL_ERR_FORMAT;
        }
        call_copy(m->ufrag, p, CALL_ICE_UFRAG_LEN);
        call_copy(m->pwd, p + CALL_ICE_UFRAG_LEN, CALL_ICE_PWD_LEN);
        p += CALL_ICE_UFRAG_LEN + CALL_ICE_PWD_LEN;
        m->ncands = *p++;
        if (m->ncands > CALL_ICE_MAX_CANDS) return CALL_ERR_FORMAT;
        if (body != need + (uint32_t) m->ncands * CALL_CAND_WIRE) return CALL_ERR_FORMAT;
        for (uint32_t i = 0; i < m->ncands; i++, p += CALL_CAND_WIRE) {
            int r = call_cand_parse(p, CALL_CAND_WIRE, &m->cands[i]);
            if (r < 0) return r;
        }
    } else if (has_reason(m->type)) {
        if (body != 1) return CALL_ERR_FORMAT;
        m->reason = p[0];
        if (m->reason > CALL_END_CANCELLED) return CALL_ERR_FORMAT;
    } else if (body != 0) {
        return CALL_ERR_FORMAT;
    }
    return (int) len;
}

/* ---- helpers ---- */

static uint32_t rnd(call_session_t *s)
{
    if (s->cfg.rand) return s->cfg.rand(s->cfg.ctx);
    uint32_t x = call_mix32(s->cfg.self_id ^ (s->tx_seq * 2654435761u) ^ s->transitions);
    return x;
}

static void set_state(call_session_t *s, call_state_t st)
{
    if (s->state != st) s->transitions++;
    s->state = st;
}

static void notify(call_session_t *s, call_notify_t what, uint32_t peer)
{
    if (s->cfg.notify) s->cfg.notify(s->cfg.ctx, what, peer, s->end_reason, s);
}

static void sig_base(call_session_t *s, call_sig_t *m, uint8_t type)
{
    call_fill(m, 0, sizeof(*m));
    m->type = type;
    call_copy(m->call_id, s->call_id, 8);
    m->from = s->cfg.self_id;
    m->to = s->peer;
}

static int send_sig(call_session_t *s, call_sig_t *m, uint32_t now)
{
    uint8_t buf[CALL_SIG_MAX];
    m->seq = s->tx_seq++;
    int w = call_sig_write(m, buf, sizeof(buf));
    if (w < 0) return w;
    s->sent++;
    s->last_tx = now;
    return s->cfg.send ? s->cfg.send(s->cfg.ctx, buf, (uint32_t) w) : CALL_OK;
}

static int send_simple(call_session_t *s, uint8_t type, uint8_t reason, uint32_t now)
{
    call_sig_t m;
    sig_base(s, &m, type);
    m.reason = reason;
    return send_sig(s, &m, now);
}

static void fill_ice(call_session_t *s, call_sig_t *m)
{
    call_ice_t *ice = s->cfg.ice;
    if (!ice) return;
    call_copy(m->ufrag, ice->lufrag, CALL_ICE_UFRAG_LEN);
    call_copy(m->pwd, ice->lpwd, CALL_ICE_PWD_LEN);
    m->ncands = (uint8_t) (ice->nlocal > CALL_ICE_MAX_CANDS ? CALL_ICE_MAX_CANDS : ice->nlocal);
    for (uint32_t i = 0; i < m->ncands; i++) CALL_SET(m->cands[i], ice->local[i]);
}

static int send_offer(call_session_t *s, uint32_t now)
{
    call_sig_t m;
    sig_base(s, &m, CALL_MSG_OFFER);
    m.tie = s->tie;
    m.caps = s->cfg.caps;
    m.media = s->media_wanted;
    for (uint32_t i = 0; i < s->cfg.ncodecs; i++)
        if (s->cfg.codecs[i].kind & s->media_wanted) m.codecs[m.ncodecs++] = s->cfg.codecs[i];
    fill_ice(s, &m);
    return send_sig(s, &m, now);
}

static int send_answer(call_session_t *s, uint32_t now)
{
    call_sig_t m;
    sig_base(s, &m, CALL_MSG_ANSWER);
    m.tie = s->tie;
    m.caps = s->cfg.caps;
    m.media = s->media;
    if (s->audio_pt) m.codecs[m.ncodecs++] = (call_codec_t){s->audio_pt, CALL_MEDIA_AUDIO};
    if (s->video_pt) m.codecs[m.ncodecs++] = (call_codec_t){s->video_pt, CALL_MEDIA_VIDEO};
    if (s->data_pt) m.codecs[m.ncodecs++] = (call_codec_t){s->data_pt, CALL_MEDIA_DATA};
    fill_ice(s, &m);
    return send_sig(s, &m, now);
}

static bool local_supports(const call_session_t *s, uint8_t pt, uint8_t kind)
{
    for (uint32_t i = 0; i < s->cfg.ncodecs; i++)
        if (s->cfg.codecs[i].pt == pt && s->cfg.codecs[i].kind == kind) return true;
    return false;
}

/* Answerer: choose per kind the first offered codec we support. */
static bool negotiate(call_session_t *s, const call_sig_t *offer)
{
    s->audio_pt = s->video_pt = s->data_pt = 0;
    s->media = 0;
    for (uint32_t i = 0; i < offer->ncodecs; i++) {
        const call_codec_t *c = &offer->codecs[i];
        if (!(offer->media & c->kind) || !local_supports(s, c->pt, c->kind)) continue;
        if (c->kind == CALL_MEDIA_AUDIO && !s->audio_pt) s->audio_pt = c->pt;
        if (c->kind == CALL_MEDIA_VIDEO && !s->video_pt) s->video_pt = c->pt;
        if (c->kind == CALL_MEDIA_DATA && !s->data_pt) s->data_pt = c->pt;
    }
    if (s->audio_pt) s->media |= CALL_MEDIA_AUDIO;
    if (s->video_pt) s->media |= CALL_MEDIA_VIDEO;
    if (s->data_pt) s->media |= CALL_MEDIA_DATA;
    s->common_caps = s->cfg.caps & offer->caps;
    s->carriage = call_rtp_negotiate(s->cfg.caps, offer->caps);
    return s->media != 0;
}

/* Offerer: the answer may only pick codecs we offered, one per kind. */
static bool accept_answer(call_session_t *s, const call_sig_t *ans)
{
    uint8_t a = 0, v = 0, d = 0;
    for (uint32_t i = 0; i < ans->ncodecs; i++) {
        const call_codec_t *c = &ans->codecs[i];
        if (!(s->media_wanted & c->kind) || !local_supports(s, c->pt, c->kind)) return false;
        uint8_t *slot = c->kind == CALL_MEDIA_AUDIO ? &a : (c->kind == CALL_MEDIA_VIDEO ? &v : &d);
        if (*slot) return false;
        *slot = c->pt;
    }
    uint8_t media = (uint8_t) ((a ? CALL_MEDIA_AUDIO : 0) | (v ? CALL_MEDIA_VIDEO : 0) |
                               (d ? CALL_MEDIA_DATA : 0));
    if (!media || media != ans->media) return false;
    s->audio_pt = a;
    s->video_pt = v;
    s->data_pt = d;
    s->media = media;
    s->common_caps = s->cfg.caps & ans->caps;
    s->carriage = call_rtp_negotiate(s->cfg.caps, ans->caps);
    return true;
}

static void store_remote_ice(call_session_t *s, const call_sig_t *m)
{
    call_copy(s->rufrag, m->ufrag, CALL_ICE_UFRAG_LEN);
    call_copy(s->rpwd, m->pwd, CALL_ICE_PWD_LEN);
    s->nrcands = m->ncands;
    for (uint32_t i = 0; i < m->ncands; i++) CALL_SET(s->rcands[i], m->cands[i]);
}

static void start_ice(call_session_t *s, uint32_t now)
{
    call_ice_t *ice = s->cfg.ice;
    if (!ice) return;
    ice->controlling = s->outgoing;
    ice->state = CALL_ICE_IDLE;
    ice->selected = -1;
    call_ice_set_remote(ice, s->rufrag, s->rpwd, s->rcands, s->nrcands);
    call_ice_start(ice, now);
}

static void end_call(call_session_t *s, call_end_reason_t why, bool missed)
{
    if (s->state == CALL_ST_IDLE || s->state == CALL_ST_ENDED) return;
    s->end_reason = why;
    set_state(s, CALL_ST_ENDED);
    notify(s, missed ? CALL_NOTIFY_MISSED : CALL_NOTIFY_ENDED, s->peer);
}

static void enter_connected(call_session_t *s, uint32_t now)
{
    set_state(s, (s->local_hold || s->remote_hold) ? CALL_ST_HELD : CALL_ST_CONNECTED);
    s->last_rx = now;
    notify(s, CALL_NOTIFY_CONNECTED, s->peer);
}

static void maybe_connected(call_session_t *s, uint32_t now)
{
    if (s->state != CALL_ST_CONNECTING) return;
    if (!s->outgoing && !s->acked) return;
    if (s->cfg.ice) {
        if (s->cfg.ice->state == CALL_ICE_FAILED) {
            send_simple(s, CALL_MSG_BYE, CALL_END_ICE_FAILED, now);
            end_call(s, CALL_END_ICE_FAILED, false);
            return;
        }
        if (s->cfg.ice->state != CALL_ICE_COMPLETED) return;
    }
    enter_connected(s, now);
}

static void enter_connecting(call_session_t *s, uint32_t now)
{
    set_state(s, CALL_ST_CONNECTING);
    s->deadline = now + s->cfg.connect_ms;
    s->timer = now + s->cfg.offer_rtx_ms;
    s->last_rx = now;
    start_ice(s, now);
    maybe_connected(s, now);
}

/* ---- public API ---- */

void call_session_init(call_session_t *s, const call_session_cfg_t *cfg)
{
    call_fill(s, 0, sizeof(*s));
    CALL_SET(s->cfg, *cfg);
    if (s->cfg.ncodecs == 0 || s->cfg.ncodecs > CALL_MAX_CODECS) {
        s->cfg.ncodecs = (uint8_t) (sizeof(default_codecs) / sizeof(default_codecs[0]));
        for (uint32_t i = 0; i < s->cfg.ncodecs; i++) s->cfg.codecs[i] = default_codecs[i];
    }
    if (!s->cfg.offer_rtx_ms) s->cfg.offer_rtx_ms = 1000;
    if (!s->cfg.offer_tries) s->cfg.offer_tries = 4;
    if (!s->cfg.ring_ms) s->cfg.ring_ms = 30000;
    if (!s->cfg.connect_ms) s->cfg.connect_ms = 10000;
    if (!s->cfg.keepalive_ms) s->cfg.keepalive_ms = 5000;
    if (!s->cfg.idle_ms) s->cfg.idle_ms = 15000;
}

void call_session_reset(call_session_t *s)
{
    call_session_cfg_t cfg;
    CALL_SET(cfg, s->cfg);
    uint16_t seq = s->tx_seq;
    uint32_t tr = s->transitions;
    call_fill(s, 0, sizeof(*s));
    CALL_SET(s->cfg, cfg);
    s->tx_seq = seq;
    s->transitions = tr;
}

int call_session_dial(call_session_t *s, uint32_t peer, uint8_t media, uint32_t now)
{
    if (!s || peer == 0 || peer == s->cfg.self_id || !(media & 7u)) return CALL_ERR_ARG;
    if (s->state == CALL_ST_ENDED) call_session_reset(s);
    if (s->state != CALL_ST_IDLE) return CALL_ERR_STATE;
    s->peer = peer;
    call_put32(s->call_id, rnd(s));
    call_put32(s->call_id + 4, rnd(s));
    s->tie = rnd(s);
    s->outgoing = 1;
    s->media_wanted = (uint8_t) (media & 7u);
    s->tries = 1;
    s->timer = now + s->cfg.offer_rtx_ms;
    s->last_rx = now;
    set_state(s, CALL_ST_OUTGOING);
    return send_offer(s, now);
}

int call_session_accept(call_session_t *s, uint32_t now)
{
    if (!s || s->state != CALL_ST_INCOMING) return CALL_ERR_STATE;
    s->tie = rnd(s);
    int r = send_answer(s, now);
    enter_connecting(s, now);
    return r;
}

int call_session_reject(call_session_t *s, uint32_t now)
{
    if (!s || s->state != CALL_ST_INCOMING) return CALL_ERR_STATE;
    int r = send_simple(s, CALL_MSG_REJECT, CALL_END_REJECTED, now);
    end_call(s, CALL_END_REJECTED, false);
    return r;
}

int call_session_hangup(call_session_t *s, uint32_t now)
{
    if (!s) return CALL_ERR_ARG;
    int r = CALL_OK;
    switch (s->state) {
    case CALL_ST_OUTGOING:
    case CALL_ST_RINGING_OUT:
        r = send_simple(s, CALL_MSG_CANCEL, CALL_END_CANCELLED, now);
        break;
    case CALL_ST_INCOMING:
        return call_session_reject(s, now);
    case CALL_ST_CONNECTING:
    case CALL_ST_CONNECTED:
    case CALL_ST_HELD:
        r = send_simple(s, CALL_MSG_BYE, CALL_END_REMOTE_HANGUP, now);
        break;
    default:
        return CALL_ERR_STATE;
    }
    end_call(s, CALL_END_LOCAL_HANGUP, false);
    return r;
}

int call_session_hold(call_session_t *s, bool on, uint32_t now)
{
    if (!s || (s->state != CALL_ST_CONNECTED && s->state != CALL_ST_HELD)) return CALL_ERR_STATE;
    s->local_hold = on ? 1 : 0;
    set_state(s, (s->local_hold || s->remote_hold) ? CALL_ST_HELD : CALL_ST_CONNECTED);
    return send_simple(s, on ? CALL_MSG_HOLD : CALL_MSG_RESUME, 0, now);
}

void call_session_on_media(call_session_t *s, uint32_t now)
{
    if (!s) return;
    s->last_rx = now;
    if (s->state == CALL_ST_CONNECTING && !s->outgoing) {
        s->acked = 1; /* media from the caller implies our answer arrived */
        maybe_connected(s, now);
    }
}

static bool glare_we_win(const call_session_t *s, const call_sig_t *m)
{
    if (s->tie != m->tie) return s->tie > m->tie;
    return s->cfg.self_id > m->from;
}

static void reject_other(call_session_t *s, const call_sig_t *m, uint8_t reason, uint32_t now)
{
    call_sig_t r;
    call_fill(&r, 0, sizeof(r));
    r.type = CALL_MSG_REJECT;
    call_copy(r.call_id, m->call_id, 8);
    r.from = s->cfg.self_id;
    r.to = m->from;
    r.reason = reason;
    send_sig(s, &r, now);
}

static int on_offer(call_session_t *s, const call_sig_t *m, uint32_t now)
{
    /* a retransmitted offer for the call we are receiving (a colliding id on
     * our own outgoing call is glare, handled below) */
    bool same_call = s->peer == m->from && !s->outgoing && !call_cmp(s->call_id, m->call_id, 8);
    if (s->state == CALL_ST_ENDED && !same_call) call_session_reset(s);
    if (same_call) {
        /* retransmission */
        if (s->state == CALL_ST_INCOMING) send_simple(s, CALL_MSG_RINGING, 0, now);
        if (s->state == CALL_ST_CONNECTING && !s->outgoing) send_answer(s, now);
        return 0;
    }
    if ((s->state == CALL_ST_OUTGOING || s->state == CALL_ST_RINGING_OUT) && m->from == s->peer) {
        if (glare_we_win(s, m)) return 0; /* the peer will answer ours */
        /* glare lost: drop our offer and answer theirs */
        call_copy(s->call_id, m->call_id, 8);
        s->outgoing = 0;
        CALL_SET(s->pending_offer, *m);
        store_remote_ice(s, m);
        if (!negotiate(s, m)) {
            send_simple(s, CALL_MSG_REJECT, CALL_END_INCOMPATIBLE, now);
            end_call(s, CALL_END_INCOMPATIBLE, false);
            return 1;
        }
        send_answer(s, now);
        enter_connecting(s, now);
        return 1;
    }
    if (s->state != CALL_ST_IDLE && m->from == s->peer && s->outgoing && glare_we_win(s, m))
        return 0; /* a late copy of the offer that lost glare against ours */
    if (s->state != CALL_ST_IDLE) {
        reject_other(s, m, CALL_END_BUSY, now);
        if (s->cfg.notify) s->cfg.notify(s->cfg.ctx, CALL_NOTIFY_MISSED, m->from, CALL_END_BUSY, s);
        return 0;
    }
    s->peer = m->from;
    call_copy(s->call_id, m->call_id, 8);
    s->outgoing = 0;
    CALL_SET(s->pending_offer, *m);
    store_remote_ice(s, m);
    if (!negotiate(s, m)) {
        reject_other(s, m, CALL_END_INCOMPATIBLE, now);
        s->peer = 0;
        return 0;
    }
    set_state(s, CALL_ST_INCOMING);
    s->deadline = now + s->cfg.ring_ms;
    send_simple(s, CALL_MSG_RINGING, 0, now);
    notify(s, CALL_NOTIFY_INCOMING, s->peer);
    return 1;
}

int call_session_on_message(call_session_t *s, const uint8_t *msg, uint32_t len, uint32_t now)
{
    call_sig_t m;
    if (!s || !msg) return CALL_ERR_ARG;
    int r = call_sig_parse(msg, len, &m);
    if (r < 0) {
        s->rejected_msgs++;
        return r;
    }
    if (m.to != s->cfg.self_id || m.from == 0 || m.from == s->cfg.self_id) {
        s->rejected_msgs++;
        return CALL_ERR_ARG;
    }
    s->received++;
    if (m.type == CALL_MSG_OFFER) return on_offer(s, &m, now);
    /* everything else must belong to the current call */
    if (m.from != s->peer || call_cmp(m.call_id, s->call_id, 8) || s->state == CALL_ST_IDLE) {
        s->rejected_msgs++;
        return 0;
    }
    if (s->state == CALL_ST_ENDED) {
        if (m.type == CALL_MSG_ANSWER) send_simple(s, CALL_MSG_BYE, CALL_END_LOCAL_HANGUP, now);
        return 0;
    }
    s->last_rx = now;
    switch (m.type) {
    case CALL_MSG_RINGING:
        if (s->state == CALL_ST_OUTGOING) {
            set_state(s, CALL_ST_RINGING_OUT);
            s->deadline = now + s->cfg.ring_ms;
        }
        return 1;
    case CALL_MSG_ANSWER:
        if (s->state == CALL_ST_OUTGOING || s->state == CALL_ST_RINGING_OUT) {
            if (!accept_answer(s, &m)) {
                send_simple(s, CALL_MSG_BYE, CALL_END_INCOMPATIBLE, now);
                end_call(s, CALL_END_INCOMPATIBLE, false);
                return 1;
            }
            store_remote_ice(s, &m);
            send_simple(s, CALL_MSG_ACK, 0, now);
            enter_connecting(s, now);
            return 1;
        }
        if (s->outgoing) send_simple(s, CALL_MSG_ACK, 0, now); /* our ACK was lost */
        return 0;
    case CALL_MSG_ACK:
        if (!s->outgoing) {
            s->acked = 1;
            maybe_connected(s, now);
        }
        return 1;
    case CALL_MSG_REJECT:
        if (s->state == CALL_ST_OUTGOING || s->state == CALL_ST_RINGING_OUT) {
            call_end_reason_t why = m.reason == CALL_END_BUSY           ? CALL_END_BUSY
                                    : m.reason == CALL_END_INCOMPATIBLE ? CALL_END_INCOMPATIBLE
                                    : m.reason == CALL_END_NO_ANSWER    ? CALL_END_NO_ANSWER
                                                                        : CALL_END_REJECTED;
            end_call(s, why, false);
        }
        return 1;
    case CALL_MSG_CANCEL:
        if (s->state == CALL_ST_INCOMING) end_call(s, CALL_END_CANCELLED, true);
        return 1;
    case CALL_MSG_BYE:
        end_call(s, s->state == CALL_ST_INCOMING ? CALL_END_CANCELLED : CALL_END_REMOTE_HANGUP,
                 s->state == CALL_ST_INCOMING);
        return 1;
    case CALL_MSG_HOLD:
    case CALL_MSG_RESUME:
        if (s->state == CALL_ST_CONNECTED || s->state == CALL_ST_HELD) {
            s->remote_hold = m.type == CALL_MSG_HOLD;
            set_state(s, (s->local_hold || s->remote_hold) ? CALL_ST_HELD : CALL_ST_CONNECTED);
        }
        return 1;
    case CALL_MSG_KEEPALIVE:
        if (s->state == CALL_ST_CONNECTING && !s->outgoing) {
            s->acked = 1;
            maybe_connected(s, now);
        }
        return 1;
    default:
        return 0;
    }
}

void call_session_tick(call_session_t *s, uint32_t now)
{
    if (!s) return;
    switch (s->state) {
    case CALL_ST_OUTGOING:
        if (!call_time_ge(now, s->timer)) break;
        if (s->tries >= s->cfg.offer_tries) {
            end_call(s, CALL_END_NO_RESPONSE, false);
            break;
        }
        s->timer = now + (s->cfg.offer_rtx_ms << (s->tries < 8 ? s->tries : 8));
        s->tries++;
        send_offer(s, now);
        break;
    case CALL_ST_RINGING_OUT:
        if (call_time_ge(now, s->deadline)) {
            send_simple(s, CALL_MSG_CANCEL, CALL_END_NO_ANSWER, now);
            end_call(s, CALL_END_NO_ANSWER, false);
        }
        break;
    case CALL_ST_INCOMING:
        if (call_time_ge(now, s->deadline)) {
            send_simple(s, CALL_MSG_REJECT, CALL_END_NO_ANSWER, now);
            end_call(s, CALL_END_NO_ANSWER, true);
        }
        break;
    case CALL_ST_CONNECTING:
        if (s->cfg.ice) call_ice_tick(s->cfg.ice, now);
        maybe_connected(s, now);
        if (s->state != CALL_ST_CONNECTING) break;
        if (!s->outgoing && !s->acked && call_time_ge(now, s->timer)) {
            send_answer(s, now);
            s->timer = now + s->cfg.offer_rtx_ms;
        }
        if (call_time_ge(now, s->deadline)) {
            call_end_reason_t why = s->cfg.ice ? CALL_END_ICE_FAILED : CALL_END_TIMEOUT;
            send_simple(s, CALL_MSG_BYE, (uint8_t) why, now);
            end_call(s, why, false);
        }
        break;
    case CALL_ST_CONNECTED:
    case CALL_ST_HELD:
        if (!call_time_ge(now - s->cfg.idle_ms, s->last_rx)) {
            /* fine */
        } else {
            send_simple(s, CALL_MSG_BYE, CALL_END_TIMEOUT, now);
            end_call(s, CALL_END_TIMEOUT, false);
            break;
        }
        if (call_time_ge(now - s->cfg.keepalive_ms, s->last_tx))
            send_simple(s, CALL_MSG_KEEPALIVE, 0, now);
        break;
    default:
        break;
    }
}

const char *call_state_name(call_state_t st)
{
    static const char *const n[] = {"IDLE",       "OUTGOING",  "RINGING_OUT", "INCOMING",
                                    "CONNECTING", "CONNECTED", "HELD",        "ENDED"};
    return (unsigned) st < 8 ? n[st] : "?";
}

const char *call_end_reason_name(call_end_reason_t r)
{
    static const char *const n[] = {"NONE",      "LOCAL_HANGUP", "REMOTE_HANGUP", "REJECTED",
                                    "NO_ANSWER", "NO_RESPONSE",  "ICE_FAILED",    "TIMEOUT",
                                    "BUSY",      "INCOMPATIBLE", "CANCELLED"};
    return (unsigned) r < 11 ? n[r] : "?";
}
