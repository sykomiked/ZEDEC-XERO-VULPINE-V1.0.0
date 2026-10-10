/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ehop_frame.c — channels, channel tags, seal/open, routing, the hook.
 *
 * ORDER OF OPERATIONS. The schedule is applied to the PLAINTEXT and the AEAD
 * runs over the result:  ct = ChaCha20(aead_key, schedule(pt)).  Applied to
 * the ciphertext instead, a byte permutation of uniformly random bytes would
 * change nothing an observer can see, so inside is the only place it does
 * any work. Every key below is bound to the full channel configuration, so a
 * receiver holding a different (key, AM, FM, PM, hop) derives a different
 * AEAD key and the Poly1305 check fails before the schedule is ever undone.
 *
 * Key derivation per channel:
 *   sched || aead || tag = SHAKE256("ZXV-EHOP-v1/chan" || key || le32 chan ||
 *                                   le32 epoch || cfg, 96)
 * AEAD nonce = le32 sender || le64 seq; AAD = header || channel tag.
 */
#include "ehop_internal.h"
#include "../mlkem/keccak.h"
#include "../tls/aead.h"

#define HDR_MAGIC0 0x5Au /* 'Z' */
#define HDR_MAGIC1 0x48u /* 'H' */

int ehop_channel_init(ehop_channel_t *ch, const uint8_t key[EHOP_KEY_BYTES], uint32_t chan_id,
                      uint32_t epoch, const ehop_cfg_t *cfg, uint32_t self_sender)
{
    uint8_t in[16 + EHOP_KEY_BYTES + 4 + 4 + EHOP_CFG_BYTES];
    uint8_t out[3 * EHOP_KEY_BYTES];
    uint32_t n;
    int rc;
    if (!ch || !key) return EHOP_EARG;
    rc = ehop_cfg_check(cfg);
    if (rc != EHOP_OK) return rc;
    ehop_wipe(ch, sizeof *ch);
    n = ehop_put_str(in, "ZXV-EHOP-v1/chan");
    ehop_cpy(in + n, key, EHOP_KEY_BYTES);
    n += EHOP_KEY_BYTES;
    ehop_le32(in + n, chan_id);
    n += 4;
    ehop_le32(in + n, epoch);
    n += 4;
    ehop_cfg_bytes(cfg, in + n);
    n += EHOP_CFG_BYTES;
    shake256(in, n, out, sizeof out);
    ehop_cpy(ch->sched_key, out, EHOP_KEY_BYTES);
    ehop_cpy(ch->aead_key, out + EHOP_KEY_BYTES, EHOP_KEY_BYTES);
    ehop_cpy(ch->tag_key, out + 2 * EHOP_KEY_BYTES, EHOP_KEY_BYTES);
    ch->cfg.width = cfg->width;
    ch->cfg.band = cfg->band;
    ch->cfg.am_mask = cfg->am_mask;
    ch->cfg.pm_mode = cfg->pm_mode;
    ch->cfg.fm_rate = cfg->fm_rate;
    ch->cfg.fm_hop = cfg->fm_hop;
    ch->cfg.pm_phase = cfg->pm_phase;
    ch->cfg.pm_step = cfg->pm_step;
    ch->chan_id = chan_id;
    ch->epoch = epoch;
    ch->self_sender = self_sender;
    ch->next_seq = 0;
    ch->ready = 1;
    ehop_wipe(in, sizeof in);
    ehop_wipe(out, sizeof out);
    return EHOP_OK;
}

int ehop_channel_set_seq(ehop_channel_t *ch, uint64_t next_seq)
{
    if (!ch || !ch->ready || next_seq < ch->next_seq) return EHOP_EARG;
    ch->next_seq = next_seq;
    return EHOP_OK;
}

void ehop_channel_wipe(ehop_channel_t *ch)
{
    if (ch) ehop_wipe(ch, sizeof *ch);
}

void ehop_channel_copy(ehop_channel_t *dst, const ehop_channel_t *src)
{
    if (dst && src) ehop_cpy((uint8_t *) dst, (const uint8_t *) src, sizeof *dst);
}

void ehop_channel_tag(const ehop_channel_t *ch, const uint8_t hdr[EHOP_HDR_BYTES],
                      uint8_t tag[EHOP_TAG_BYTES])
{
    uint8_t in[EHOP_KEY_BYTES + 15 + EHOP_HDR_BYTES];
    uint8_t d[SHA3_256_DIGEST_LEN];
    uint32_t n;
    ehop_cpy(in, ch->tag_key, EHOP_KEY_BYTES);
    n = EHOP_KEY_BYTES;
    n += ehop_put_str(in + n, "ZXV-EHOP-v1/tag");
    ehop_cpy(in + n, hdr, EHOP_HDR_BYTES);
    n += EHOP_HDR_BYTES;
    sha3_256(in, n, d);
    ehop_cpy(tag, d, EHOP_TAG_BYTES);
    ehop_wipe(in, sizeof in);
    ehop_wipe(d, sizeof d);
}

static void make_nonce(uint32_t sender, uint64_t seq, uint8_t nonce[CHACHA20_NONCE_LEN])
{
    ehop_le32(nonce, sender);
    ehop_le64(nonce + 4, seq);
}

int ehop_seal(ehop_channel_t *ch, const uint8_t *pt, uint32_t pt_len, uint8_t *frame, uint32_t cap,
              uint32_t *frame_len)
{
    ehop_schedule_t s;
    uint8_t nonce[CHACHA20_NONCE_LEN];
    uint8_t *body;
    uint64_t seq;
    const ehop_band_info_t *b;
    int rc;
    if (frame_len) *frame_len = 0;
    if (!ch || !ch->ready || !frame || !frame_len || (!pt && pt_len)) return EHOP_EARG;
    b = ehop_band_info(ch->cfg.band);
    if (!b) return EHOP_EBAND;
    if (pt_len > EHOP_MAX_PAYLOAD || pt_len + EHOP_OVERHEAD > b->max_frame) return EHOP_ESIZE;
    if (cap < pt_len + EHOP_OVERHEAD) return EHOP_ESIZE;
    if (ch->next_seq == 0xFFFFFFFFFFFFFFFFull) return EHOP_ESEQ;
    seq = ch->next_seq;
    rc = ehop_schedule_init(&s, ch->sched_key, &ch->cfg, ch->chan_id, ch->epoch, ch->self_sender,
                            seq);
    if (rc != EHOP_OK) return rc;
    ch->next_seq = seq + 1;

    frame[0] = HDR_MAGIC0;
    frame[1] = HDR_MAGIC1;
    frame[2] = EHOP_VERSION;
    frame[3] = ch->cfg.band;
    ehop_le32(frame + 4, ch->epoch);
    ehop_le32(frame + 8, ch->self_sender);
    ehop_le64(frame + 12, seq);
    ehop_le16(frame + 20, pt_len);
    ehop_le16(frame + 22, 0);
    ehop_channel_tag(ch, frame, frame + EHOP_HDR_BYTES);

    body = frame + EHOP_HDR_BYTES + EHOP_TAG_BYTES;
    if (pt_len) ehop_cpy(body, pt, pt_len);
    ehop_apply(&s, body, pt_len);
    make_nonce(ch->self_sender, seq, nonce);
    aead_seal(ch->aead_key, nonce, frame, EHOP_HDR_BYTES + EHOP_TAG_BYTES, body, body, pt_len,
              body + pt_len);
    ehop_schedule_wipe(&s);
    *frame_len = pt_len + EHOP_OVERHEAD;
    return EHOP_OK;
}

/* Parse the clear header. EHOP_OK and *pt_len on success. */
static int parse_hdr(const uint8_t *frame, uint32_t frame_len, uint32_t *pt_len)
{
    uint32_t n;
    if (!frame || frame_len < EHOP_OVERHEAD) return EHOP_EFORMAT;
    if (frame[0] != HDR_MAGIC0 || frame[1] != HDR_MAGIC1 || frame[2] != EHOP_VERSION ||
        frame[3] >= EHOP_BAND_COUNT || ehop_rd16(frame + 22) != 0)
        return EHOP_EFORMAT;
    n = ehop_rd16(frame + 20);
    if (n > EHOP_MAX_PAYLOAD || n + EHOP_OVERHEAD != frame_len) return EHOP_EFORMAT;
    *pt_len = n;
    return EHOP_OK;
}

/* Does this frame carry ch's tag (and band and epoch)? */
static int tag_matches(const ehop_channel_t *ch, const uint8_t *frame)
{
    uint8_t t[EHOP_TAG_BYTES];
    int eq;
    if (!ch->ready || frame[3] != ch->cfg.band || ehop_rd32(frame + 4) != ch->epoch) return 0;
    ehop_channel_tag(ch, frame, t);
    eq = ehop_ct_eq(t, frame + EHOP_HDR_BYTES, EHOP_TAG_BYTES);
    ehop_wipe(t, sizeof t);
    return eq;
}

/* Replay window. Returns the slot to update, -1 for a replay, or -2 when a
 * new sender finds the table full. */
static int replay_slot(const ehop_channel_t *ch, uint32_t sender, uint64_t seq)
{
    uint32_t i;
    int free_slot = -1;
    for (i = 0; i < EHOP_REPLAY_SLOTS; i++) {
        const ehop_replay_t *r = &ch->replay[i];
        if (r->used && r->sender == sender) {
            uint64_t d;
            if (seq > r->top) return (int) i;
            d = r->top - seq;
            if (d >= 64u || ((r->window >> d) & 1u)) return -1;
            return (int) i;
        }
        if (!r->used && free_slot < 0) free_slot = (int) i;
    }
    return free_slot >= 0 ? free_slot : -2;
}

static void replay_mark(ehop_channel_t *ch, int slot, uint32_t sender, uint64_t seq)
{
    ehop_replay_t *r = &ch->replay[slot];
    if (!r->used) {
        r->used = 1;
        r->sender = sender;
        r->top = seq;
        r->window = 1u;
        return;
    }
    if (seq > r->top) {
        uint64_t d = seq - r->top;
        r->window = d >= 64u ? 1u : ((r->window << d) | 1u);
        r->top = seq;
    } else {
        r->window |= (uint64_t) 1u << (r->top - seq);
    }
}

int ehop_open(ehop_channel_t *ch, const uint8_t *frame, uint32_t frame_len, uint8_t *out,
              uint32_t cap, uint32_t *out_len)
{
    ehop_schedule_t s;
    uint8_t nonce[CHACHA20_NONCE_LEN];
    uint32_t n = 0, sender;
    uint64_t seq;
    int rc, slot;
    if (out_len) *out_len = 0;
    if (!ch || !ch->ready || !out || !out_len) return EHOP_EARG;
    rc = parse_hdr(frame, frame_len, &n);
    if (rc != EHOP_OK) return rc;
    if (cap < n) return EHOP_ESIZE;
    if (ehop_rd32(frame + 4) != ch->epoch) return EHOP_EEPOCH;
    if (frame[3] != ch->cfg.band || !tag_matches(ch, frame)) return EHOP_ECHANNEL;
    sender = ehop_rd32(frame + 8);
    seq = ehop_rd64(frame + 12);
    slot = replay_slot(ch, sender, seq);
    if (slot < 0) return slot == -1 ? EHOP_EREPLAY : EHOP_EFULL;
    make_nonce(sender, seq, nonce);
    if (!aead_open(ch->aead_key, nonce, frame, EHOP_HDR_BYTES + EHOP_TAG_BYTES,
                   frame + EHOP_HDR_BYTES + EHOP_TAG_BYTES, out, n,
                   frame + EHOP_HDR_BYTES + EHOP_TAG_BYTES + n)) {
        ehop_wipe(out, n);
        return EHOP_EAUTH;
    }
    rc = ehop_schedule_init(&s, ch->sched_key, &ch->cfg, ch->chan_id, ch->epoch, sender, seq);
    if (rc != EHOP_OK) {
        ehop_wipe(out, n);
        return rc;
    }
    ehop_apply(&s, out, n);
    ehop_schedule_wipe(&s);
    replay_mark(ch, slot, sender, seq);
    *out_len = n;
    return EHOP_OK;
}

int ehop_route(const ehop_channel_t *chans, uint32_t n, const uint8_t *frame, uint32_t frame_len)
{
    uint32_t i, pt;
    if (!chans || parse_hdr(frame, frame_len, &pt) != EHOP_OK) return -1;
    for (i = 0; i < n; i++)
        if (tag_matches(&chans[i], frame)) return (int) i;
    return -1;
}

int ehop_frame_qos(const uint8_t *frame, uint32_t frame_len, uint32_t *band, uint32_t *priority)
{
    uint32_t pt;
    const ehop_band_info_t *b;
    int rc = parse_hdr(frame, frame_len, &pt);
    if (rc != EHOP_OK) return rc;
    b = ehop_band_info(frame[3]);
    if (band) *band = frame[3];
    if (priority) *priority = b->priority;
    return EHOP_OK;
}

/* ===== router ===== */
void ehop_router_init(ehop_router_t *r)
{
    if (r) ehop_wipe(r, sizeof *r);
}

int ehop_router_find_peer(const ehop_router_t *r, const uint8_t id[EHOP_PEER_ID_BYTES])
{
    uint32_t i;
    if (!r || !id) return EHOP_EARG;
    for (i = 0; i < EHOP_MAX_PEERS; i++)
        if (r->peers[i].used && ehop_ct_eq(r->peers[i].id, id, EHOP_PEER_ID_BYTES)) return (int) i;
    return EHOP_EMEMBER;
}

int ehop_router_add_peer(ehop_router_t *r, const uint8_t id[EHOP_PEER_ID_BYTES], uint32_t capacity)
{
    uint32_t i;
    int found = ehop_router_find_peer(r, id);
    if (found == EHOP_EARG) return EHOP_EARG;
    if (found >= 0) {
        r->peers[found].capacity = capacity;
        return found;
    }
    for (i = 0; i < EHOP_MAX_PEERS; i++) {
        ehop_peer_t *p = &r->peers[i];
        if (!p->used) {
            ehop_wipe(p, sizeof *p);
            ehop_cpy(p->id, id, EHOP_PEER_ID_BYTES);
            p->capacity = capacity;
            p->used = 1;
            r->n++;
            return (int) i;
        }
    }
    return EHOP_EFULL;
}

int ehop_router_add_channel(ehop_router_t *r, uint32_t peer, const ehop_channel_t *ch)
{
    ehop_peer_t *p;
    if (!r || !ch || !ch->ready || peer >= EHOP_MAX_PEERS || !r->peers[peer].used) return EHOP_EARG;
    p = &r->peers[peer];
    if (p->n >= EHOP_PEER_CHANNELS) return EHOP_EFULL;
    ehop_channel_copy(&p->chans[p->n], ch);
    p->n++;
    return (int) (p->n - 1);
}

int ehop_router_pick(const ehop_router_t *r, uint32_t peer, uint32_t msg_class, uint32_t pt_len,
                     uint32_t *band)
{
    const ehop_peer_t *p;
    uint32_t b, i;
    if (!r || peer >= EHOP_MAX_PEERS || !r->peers[peer].used) return EHOP_EARG;
    p = &r->peers[peer];
    /* Never promote traffic above its class: fall back only to bands of
     * equal or lower priority (band enum order is priority order). */
    for (b = ehop_band_for_class(msg_class); b < EHOP_BAND_COUNT; b++) {
        if (pt_len > EHOP_MAX_PAYLOAD || pt_len + EHOP_OVERHEAD > ehop_band_info(b)->max_frame)
            continue;
        for (i = 0; i < p->n; i++) {
            if (p->chans[i].cfg.band == b) {
                if (band) *band = b;
                return (int) i;
            }
        }
    }
    return EHOP_EBAND;
}

int ehop_router_send(ehop_router_t *r, uint32_t peer, uint32_t msg_class, const uint8_t *pt,
                     uint32_t pt_len, uint8_t *frame, uint32_t cap, uint32_t *frame_len)
{
    int c = ehop_router_pick(r, peer, msg_class, pt_len, NULL);
    if (frame_len) *frame_len = 0;
    if (c < 0) return c;
    return ehop_seal(&r->peers[peer].chans[c], pt, pt_len, frame, cap, frame_len);
}

int ehop_router_recv(ehop_router_t *r, uint32_t peer, const uint8_t *frame, uint32_t frame_len,
                     uint8_t *out, uint32_t cap, uint32_t *out_len, uint32_t *chan)
{
    ehop_peer_t *p;
    int c;
    if (out_len) *out_len = 0;
    if (!r || peer >= EHOP_MAX_PEERS || !r->peers[peer].used) return EHOP_EARG;
    p = &r->peers[peer];
    c = ehop_route(p->chans, p->n, frame, frame_len);
    if (c < 0) return EHOP_ECHANNEL;
    if (chan) *chan = (uint32_t) c;
    return ehop_open(&p->chans[c], frame, frame_len, out, cap, out_len);
}

/* ===== hook ===== */
static int hook_seal(void *ctx, uint32_t peer, uint32_t msg_class, const uint8_t *pt,
                     uint32_t pt_len, uint8_t *frame, uint32_t cap, uint32_t *frame_len)
{
    return ehop_router_send((ehop_router_t *) ctx, peer, msg_class, pt, pt_len, frame, cap,
                            frame_len);
}

static int hook_open(void *ctx, uint32_t peer, const uint8_t *frame, uint32_t frame_len,
                     uint8_t *pt, uint32_t cap, uint32_t *pt_len, uint32_t *chan)
{
    return ehop_router_recv((ehop_router_t *) ctx, peer, frame, frame_len, pt, cap, pt_len, chan);
}

void ehop_router_hook(ehop_router_t *r, ehop_hook_t *hook)
{
    if (!hook) return;
    hook->ctx = r;
    hook->seal = hook_seal;
    hook->open = hook_open;
    hook->qos = ehop_frame_qos;
}
