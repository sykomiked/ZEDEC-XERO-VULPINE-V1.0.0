/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_call.c — the real-time call engine's network-facing byte parsers
 * (kernel/src/call) and the encoders that pair with them.
 *
 * Byte 0 picks the mode (mod 14); the rest is the input:
 *   0  call_rtp_parse: accepted packets re-encode byte-exact (header), the
 *      returned payload length equals the derived fragment span, and the
 *      parsed header feeds the reassembler without a memory error
 *   1  call_ubh_unwrap / call_rtp_decap (both carriages, raw_len 0 and set):
 *      decode accepts only canonical encodings, so wrap(unwrap(w)) == w and
 *      encap(decap(w)) == w byte for byte
 *   2  call_stun_parse / call_stun_verify: an accepted message re-encodes and
 *      re-parses to the same fields; MESSAGE-INTEGRITY verifies only when it
 *      is the last attribute and the MAC matches (one flipped bit fails);
 *      write + mac -> parse -> verify always succeeds
 *   3  call_cand_parse: byte-exact re-encode, type/family/IPv4 padding rules
 *   4  call_rv_parse: byte-exact re-encode of every accepted message
 *   5  call_sig_parse: byte-exact re-encode of every accepted message
 *   6  call_mesh_parse: byte-exact re-encode (call_mesh_encode_member/_leave)
 *   7  mesh membership: generated MEMBER/LEAVE messages (good, bad and
 *      wrong-key signatures, and ids that are not their key's) applied to a
 *      table. A refused message changes nothing, except that an equivocation
 *      (CALL_ERR_CONFLICT) marks just that member dead and equivocated for
 *      good; a bad signature or a squatted id is always refused; a member's
 *      key never changes; versions only grow. call_mesh_build: no member is charged
 *      more than its declared uplink, per-tree fanout and max depth hold,
 *      every edge's sender is in the tree (publisher or already fed), dead
 *      members get nothing, delivered layer <= wanted, served+degraded+
 *      unserved == deliveries. Determinism: the same distinct-id messages in
 *      reverse order give the same table and the same plan hash.
 *   8  call_fec_hdr_parse + a decoder fed hostile parity/media: no memory
 *      error, every packet the decoder emits re-parses as RTP of the
 *      protected stream
 *   9  call_cc_fb_parse: byte-exact re-encode; the sender's target stays in
 *      [min_bps, max_bps] for any accepted report
 *  10  packetize -> parse -> reassemble (shuffled, duplicated) gives back the
 *      exact frame; the packet count is ceil(len / (mtu-20))
 *  11  FEC: encode a stream, drop at most one packet per group, decode: every
 *      dropped packet is recovered exactly, nothing else is emitted
 *  12  congestion-control receiver fed hostile send times: no UB, and every
 *      report it emits is accepted by call_cc_fb_parse after call_cc_fb_write
 *  13  encap/decap round trip of generated RTP packets and raw payloads */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "call_cc.h"
#include "call_fec.h"
#include "call_ice.h"
#include "call_mesh.h"
#include "call_rtp.h"
#include "call_session.h"
#include "fuzz_in.h"

/* ---- toy MAC / signature (harness only, not crypto) ---- */

static void toy_stream(const uint8_t *key, uint32_t klen, const uint8_t *msg, uint32_t len,
                       uint8_t *out, uint32_t olen)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < klen; i++) h = (h ^ key[i]) * 16777619u;
    for (uint32_t i = 0; i < len; i++) h = (h ^ msg[i]) * 16777619u;
    for (uint32_t i = 0; i < olen; i++) {
        h = (h ^ i) * 16777619u;
        h ^= h >> 13;
        out[i] = (uint8_t) h;
    }
}

static bool toy_mac(void *ctx, const uint8_t *key, uint32_t klen, const uint8_t *msg, uint32_t len,
                    uint8_t out[16])
{
    (void) ctx;
    toy_stream(key, klen, msg, len, out, 16);
    return true;
}

static bool toy_verify(void *ctx, const uint8_t pubkey[32], const uint8_t *msg, uint32_t len,
                       const uint8_t sig[64])
{
    (void) ctx;
    uint8_t want[64];
    toy_stream(pubkey, 32, msg, len, want, 64);
    return memcmp(want, sig, 64) == 0;
}

static bool toy_sign(void *ctx, const uint8_t *msg, uint32_t len, uint8_t sig[64])
{
    toy_stream((const uint8_t *) ctx, 32, msg, len, sig, 64);
    return true;
}

/* signer that reproduces a given signature (for byte-exact re-encode) */
static bool copy_sign(void *ctx, const uint8_t *msg, uint32_t len, uint8_t sig[64])
{
    (void) msg;
    (void) len;
    memcpy(sig, ctx, 64);
    return true;
}

static uint8_t g_out[8192];
static uint8_t g_out2[8192];

/* ---- mode 0: RTP ---- */

static call_reasm_slot_t g_slots[4];
static uint8_t g_rmem[4 * 4096];

static void mode_rtp(const uint8_t *p, size_t n)
{
    uint8_t *b = fz_dup(p, n);
    call_rtp_hdr_t h;
    const uint8_t *pay = NULL;
    int r = call_rtp_parse(b, (uint32_t) n, &h, &pay);
    if (r >= 0) {
        uint32_t off = 0, fl = 0;
        if (r < 1 || !call_rtp_frag_span(&h, &off, &fl) || (uint32_t) r != fl) abort();
        if (call_rtp_packet_len(&h) != n || pay != b + CALL_RTP_HDR_LEN) abort();
        if (off + fl > h.frame_len) abort();
        if (call_rtp_hdr_write(&h, g_out, sizeof g_out) != CALL_RTP_HDR_LEN) abort();
        if (memcmp(g_out, b, CALL_RTP_HDR_LEN) != 0) abort();
        call_reasm_t ra;
        call_reasm_init(&ra, g_slots, 4, g_rmem, 4096);
        call_frame_t fr;
        int q = call_reasm_push(&ra, &h, pay, &fr);
        if (h.frame_len > 4096) {
            if (q != CALL_ERR_SPACE) abort();
        } else if (h.frag_cnt == 1) {
            if (q != 1 || fr.len != h.frame_len || memcmp(fr.data, pay, fl) != 0) abort();
        } else if (q != 0) {
            abort();
        }
    }
    free(b);
}

/* ---- mode 1: UBH unwrap / decap ---- */

static void mode_ubh(const uint8_t *p, size_t n)
{
    if (n < 1) return;
    uint8_t sel = p[0];
    uint32_t raw_len = (sel & 1u) ? 0u : (uint32_t) (sel >> 1) * 11u;
    p++;
    n--;
    if (n > 3000) return;
    uint8_t *w = fz_dup(p, n);
    int got = call_ubh_unwrap(w, (uint32_t) n, g_out, sizeof g_out);
    if (got >= 0) {
        if ((uint32_t) got != n / CALL_UBH_FRAME * CALL_UBH_PAYLOAD) abort();
        int ww = call_ubh_wrap(g_out, (uint32_t) got, g_out2, sizeof g_out2);
        if (ww != (int) n || memcmp(g_out2, w, n) != 0) abort();
    }
    for (int c = 0; c < 2; c++) {
        call_carriage_t car = c ? CALL_CARRIAGE_UBH168 : CALL_CARRIAGE_PLAIN;
        int L = call_rtp_decap(car, w, (uint32_t) n, g_out, sizeof g_out, raw_len);
        if (L < 0) continue;
        if (L == 0) abort();
        if (raw_len && (uint32_t) L != raw_len) abort();
        if (call_rtp_wire_len(car, (uint32_t) L) != n) abort();
        int e = call_rtp_encap(car, g_out, (uint32_t) L, g_out2, sizeof g_out2);
        if (e != (int) n || memcmp(g_out2, w, n) != 0) abort();
        if (car == CALL_CARRIAGE_UBH168 && !raw_len) {
            /* the length came from an RTP header with a valid CRC */
            call_rtp_hdr_t h;
            h.frag_idx = g_out[14];
            h.frag_cnt = g_out[15];
            h.frame_len = ((uint32_t) g_out[16] << 16) | ((uint32_t) g_out[17] << 8) | g_out[18];
            if (call_rtp_packet_len(&h) != (uint32_t) L) abort();
        }
    }
    free(w);
}

/* ---- mode 2: STUN ---- */

static const uint8_t g_key[16] = "passwordA-------";

static void stun_same(const call_stun_t *a, const call_stun_t *b)
{
    if (a->type != b->type || memcmp(a->txid, b->txid, 12) != 0) abort();
    if (a->has_username != b->has_username || a->has_priority != b->has_priority ||
        a->use_candidate != b->use_candidate || a->has_controlling != b->has_controlling ||
        a->has_controlled != b->has_controlled || a->has_mapped != b->has_mapped ||
        a->has_error != b->has_error)
        abort();
    if (a->has_username && memcmp(a->username, b->username, 16) != 0) abort();
    if (a->has_priority && a->priority != b->priority) abort();
    if ((a->has_controlling || a->has_controlled) && a->tie != b->tie) abort();
    if (a->has_mapped && (a->mapped.family != b->mapped.family || a->mapped.port != b->mapped.port ||
                          memcmp(a->mapped.ip, b->mapped.ip, 16) != 0))
        abort();
    if (a->has_error && a->error != b->error) abort();
}

static void mode_stun(const uint8_t *p, size_t n)
{
    uint8_t *b = fz_dup(p, n);
    call_stun_t m, m2;
    int r = call_stun_parse(b, (uint32_t) n, &m);
    if (r >= 0) {
        if (r != (int) n) abort();
        if (m.has_controlling && m.has_controlled) abort();
        if (m.type == CALL_STUN_ERR && !m.has_error) abort();
        if (m.has_mapped && m.mapped.family != 4 && m.mapped.family != 6) abort();
        if (m.has_error && (m.error < 300 || m.error > 699)) abort();
        bool v = call_stun_verify(&m, b, (uint32_t) n, g_key, 16, toy_mac, NULL);
        if (v) {
            uint8_t want[16];
            toy_mac(NULL, g_key, 16, b, m.integrity_off, want);
            if (!m.has_integrity || m.integrity_off + 20 != n ||
                memcmp(want, b + m.integrity_off + 4, 16) != 0)
                abort();
            /* one flipped bit in the covered bytes must fail */
            uint32_t at = (uint32_t) (m.priority % m.integrity_off);
            b[at] ^= 0x01;
            call_stun_t mf;
            if (call_stun_parse(b, (uint32_t) n, &mf) >= 0 &&
                call_stun_verify(&mf, b, (uint32_t) n, g_key, 16, toy_mac, NULL))
                abort();
            b[at] ^= 0x01;
        }
        if (!call_stun_verify(&m, b, (uint32_t) n, g_key, 16, NULL, NULL)) abort();
        /* re-encode without and with integrity */
        int w = call_stun_write(&m, NULL, 0, NULL, NULL, g_out, sizeof g_out);
        if (w < 0) abort();
        if (call_stun_parse(g_out, (uint32_t) w, &m2) != w) abort();
        stun_same(&m, &m2);
        w = call_stun_write(&m, g_key, 16, toy_mac, NULL, g_out, sizeof g_out);
        if (w < 0) abort();
        if (call_stun_parse(g_out, (uint32_t) w, &m2) != w) abort();
        stun_same(&m, &m2);
        if (!call_stun_verify(&m2, g_out, (uint32_t) w, g_key, 16, toy_mac, NULL)) abort();
        uint8_t k2[16];
        memcpy(k2, g_key, 16);
        k2[0] ^= 1;
        if (call_stun_verify(&m2, g_out, (uint32_t) w, k2, 16, toy_mac, NULL)) abort();
    }
    free(b);
}

/* ---- mode 3: candidates ---- */

static void mode_cand(const uint8_t *p, size_t n)
{
    uint8_t *b = fz_dup(p, n);
    call_cand_t c;
    int r = call_cand_parse(b, (uint32_t) n, &c);
    if (r >= 0) {
        if (r != CALL_CAND_WIRE || n < CALL_CAND_WIRE) abort();
        if (c.type > CALL_CAND_RELAY || c.component == 0) abort();
        if (c.addr.family != 4 && c.addr.family != 6) abort();
        if (c.addr.family == 4)
            for (int i = 4; i < 16; i++)
                if (c.addr.ip[i]) abort();
        if (call_cand_write(&c, g_out, sizeof g_out) != CALL_CAND_WIRE) abort();
        if (memcmp(g_out, b, CALL_CAND_WIRE) != 0) abort();
    }
    free(b);
}

/* ---- mode 4: rendezvous ---- */

static void mode_rv(const uint8_t *p, size_t n)
{
    uint8_t *b = fz_dup(p, n);
    static call_rv_msg_t m;
    int r = call_rv_parse(b, (uint32_t) n, &m);
    if (r >= 0) {
        if (r != (int) n || n > CALL_RV_MAX_MSG) abort();
        int w = call_rv_write(&m, g_out, sizeof g_out);
        if (w != (int) n || memcmp(g_out, b, n) != 0) abort();
        if (m.type == CALL_RV_REGISTER || m.type == CALL_RV_FOUND || m.type == CALL_RV_INTRODUCE) {
            int sl = call_rv_record_signed_bytes(&m.rec, g_out2, sizeof g_out2);
            if (sl != 37 + (int) m.rec.ncands * CALL_CAND_WIRE) abort();
        }
        /* a lookup only accepts a FOUND for its target with a good signature */
        call_lookup_t lk;
        call_rv_peer_t seed;
        memset(&seed, 0, sizeof seed);
        seed.addr.family = 4;
        call_lookup_init(&lk, m.rec.id, &seed, 1, 100, m.txid, toy_verify, NULL);
        call_rv_peer_t to;
        call_rv_msg_t q;
        if (call_lookup_next(&lk, 0, &to, &q)) {
            m.txid = lk.txid;
            call_lookup_on_reply(&lk, &m);
            if (lk.state == CALL_LOOKUP_FOUND) {
                uint8_t sb[CALL_RV_MAX_MSG], sig[64];
                int sl = call_rv_record_signed_bytes(&lk.found, sb, sizeof sb);
                if (sl < 0) abort();
                toy_stream(lk.found.id, 32, sb, (uint32_t) sl, sig, 64);
                if (memcmp(sig, lk.found.sig, 64) != 0) abort();
                if (memcmp(lk.found.id, m.rec.id, 32) != 0) abort();
            }
        }
    }
    free(b);
}

/* ---- mode 5: signalling ---- */

static void mode_sig(const uint8_t *p, size_t n)
{
    uint8_t *b = fz_dup(p, n);
    static call_sig_t m;
    int r = call_sig_parse(b, (uint32_t) n, &m);
    if (r >= 0) {
        if (r != (int) n || n > CALL_SIG_MAX) abort();
        int w = call_sig_write(&m, g_out, sizeof g_out);
        if (w != (int) n || memcmp(g_out, b, n) != 0) abort();
    }
    free(b);
}

/* ---- mode 6: mesh parse ---- */

static void mode_mesh_parse(const uint8_t *p, size_t n)
{
    uint8_t *b = fz_dup(p, n);
    static call_mesh_member_t m;
    uint32_t gid = 0;
    uint8_t type = 0;
    int r = call_mesh_parse(b, (uint32_t) n, &gid, &type, &m);
    if (r == CALL_OK) {
        if (n > CALL_MESH_MAX_MSG) abort();
        const uint8_t *sig = b + n - CALL_MESH_SIG_LEN;
        int w = -1;
        if (type == CALL_MESH_MSG_MEMBER)
            w = call_mesh_encode_member(&m, gid, g_out, sizeof g_out, copy_sign, (void *) sig);
        else if (type == CALL_MESH_MSG_LEAVE)
            w = call_mesh_encode_leave(&m, gid, g_out, sizeof g_out, copy_sign, (void *) sig);
        else {
            abort();
        }
        if (w != (int) n || memcmp(g_out, b, n) != 0) abort();
    }
    free(b);
}

/* ---- mode 7: mesh membership and builder ---- */

#define MESH_CAP  16
#define MESH_MSGS 12

static call_mesh_member_t g_ta[MESH_CAP], g_tb[MESH_CAP], g_snap[MESH_CAP];
static uint8_t g_msgs[MESH_MSGS][CALL_MESH_MAX_MSG];
static uint32_t g_mlen[MESH_MSGS];
static call_mesh_edge_t g_edges[4096];
static call_mesh_delivery_t g_dl[1024];
static uint32_t g_up[MESH_CAP], g_scr[8192];

static const call_mesh_member_t *find_member(const call_mesh_t *m, uint32_t id)
{
    for (uint32_t i = 0; i < m->n; i++)
        if (m->members[i].id == id) return &m->members[i];
    return NULL;
}

static int32_t find_index(const call_mesh_t *m, uint32_t id)
{
    for (uint32_t i = 0; i < m->n; i++)
        if (m->members[i].id == id) return (int32_t) i;
    return -1;
}

static void check_plan(const call_mesh_t *m, call_mesh_plan_t *pl)
{
    for (uint32_t i = 0; i < m->n; i++) {
        const call_mesh_member_t *mm = &m->members[i];
        uint32_t used = 0;
        for (uint32_t e = 0; e < pl->nedges; e++)
            if (pl->edges[e].from == mm->id) used += pl->edges[e].kbps;
        if (used != pl->up_used_kbps[i]) abort();
        if (used > mm->uplink_kbps) abort();
        if (!mm->alive && used) abort();
    }
    for (uint32_t e = 0; e < pl->nedges; e++) {
        const call_mesh_edge_t *ed = &pl->edges[e];
        const call_mesh_member_t *to = find_member(m, ed->to), *fr = find_member(m, ed->from);
        if (!to || !fr || !to->alive || !fr->alive || ed->from == ed->to) abort();
        if (ed->depth == 0 || ed->depth > m->max_depth) abort();
        /* sender is the publisher (depth 1) or was fed earlier in this tree */
        bool ok = false;
        for (uint32_t s = 0; s < fr->nstreams; s++)
            if (fr->streams[s].id == ed->stream_id) ok = (ed->depth == 1);
        for (uint32_t f = 0; f < e && !ok; f++)
            if (pl->edges[f].to == ed->from && pl->edges[f].stream_id == ed->stream_id &&
                pl->edges[f].layer == ed->layer && pl->edges[f].depth + 1u == ed->depth)
                ok = true;
        if (!ok) abort();
        /* no member receives one (stream, layer) twice */
        for (uint32_t f = 0; f < e; f++)
            if (pl->edges[f].to == ed->to && pl->edges[f].stream_id == ed->stream_id &&
                pl->edges[f].layer == ed->layer)
                abort();
        /* fanout per (stream, layer) tree */
        uint32_t kids = call_mesh_children(pl, ed->from, ed->stream_id, ed->layer, NULL, 0);
        if (kids > fr->fanout) abort();
        if (call_mesh_parent(pl, ed->to, ed->stream_id, ed->layer) != ed->from) abort();
    }
    uint32_t s = 0, d = 0, u = 0;
    for (uint32_t i = 0; i < pl->ndl; i++) {
        const call_mesh_delivery_t *dl = &pl->dl[i];
        if (dl->got == CALL_MESH_NO_LAYER) {
            u++;
            continue;
        }
        if (dl->got > dl->want) abort();
        if (dl->got < dl->want)
            d++;
        else
            s++;
        if (!call_mesh_parent(pl, dl->member, dl->stream_id, dl->got)) abort();
    }
    if (s != pl->served || d != pl->degraded || u != pl->unserved) abort();
}

static int build(const call_mesh_t *m, call_mesh_plan_t *pl)
{
    memset(pl, 0, sizeof *pl);
    pl->edges = g_edges;
    pl->edge_cap = 4096;
    pl->dl = g_dl;
    pl->dl_cap = 1024;
    pl->up_used_kbps = g_up;
    pl->up_cap = MESH_CAP;
    if (call_mesh_scratch_words(m) > 8192) return CALL_ERR_SPACE;
    return call_mesh_build(m, pl, g_scr, 8192);
}

static void mode_mesh(const uint8_t *p, size_t n)
{
    fz_in in;
    fz_init(&in, p, n);
    static const uint8_t keys[8][32] = {{1}, {2}, {3}, {4}, {5}, {6}, {7}, {8}};
    uint32_t ids[8];
    for (uint32_t i = 0; i < 8; i++) ids[i] = call_mesh_id_of(keys[i]);
    uint32_t nm = 1u + fz_u8(&in) % MESH_MSGS;
    bool distinct = true;
    uint32_t ids_seen[MESH_MSGS];
    bool bad[MESH_MSGS];
    for (uint32_t k = 0; k < nm; k++) {
        static call_mesh_member_t mm;
        memset(&mm, 0, sizeof mm);
        uint8_t c = fz_u8(&in);
        uint32_t mi = c & 7u;
        mm.id = ids[mi];
        mm.version = fz_u8(&in) % 5u;
        /* one in three claims the id with another member's key (squatting) */
        bool squat = (c >> 3) % 3u == 1u;
        memcpy(mm.pubkey, keys[squat ? (mi + 1u) & 7u : mi], 32);
        mm.uplink_kbps = (uint32_t) (fz_u16(&in) % 5000u);
        mm.fanout = fz_u8(&in) % 6u;
        mm.nstreams = fz_u8(&in) % (CALL_MESH_MAX_STREAMS + 1u);
        for (uint32_t s = 0; s < mm.nstreams; s++) {
            call_mesh_stream_t *st = &mm.streams[s];
            st->id = (mi + 1u) * 16u + s + 1u;
            st->kind = (uint8_t) (1u + fz_u8(&in) % 4u);
            st->nlayers = (uint8_t) (1u + fz_u8(&in) % CALL_MESH_MAX_LAYERS);
            uint16_t k0 = (uint16_t) (1u + fz_u8(&in) * 4u);
            for (uint32_t l = 0; l < st->nlayers; l++) st->kbps[l] = (uint16_t) (k0 * (l + 1u));
        }
        uint32_t ns = fz_u8(&in) % 10u;
        for (uint32_t s = 0; s < ns; s++) {
            uint8_t q = fz_u8(&in);
            uint32_t sid = (1u + (q & 7u)) * 16u + ((q >> 3) & 3u) + 1u;
            bool dup = false;
            for (uint32_t t = 0; t < mm.nsubs; t++)
                if (mm.subs[t].stream_id == sid) dup = true;
            if (dup) continue;
            mm.subs[mm.nsubs].stream_id = sid;
            mm.subs[mm.nsubs].layer = (uint8_t) ((q >> 5) & 3u);
            mm.nsubs++;
        }
        uint8_t how = fz_u8(&in);
        uint8_t skey[32];
        memcpy(skey, mm.pubkey, 32);
        bad[k] = squat;
        if ((how & 7u) == 7u) {
            skey[0] ^= 0x80; /* signed by somebody else */
            bad[k] = true;
        }
        int w = (how & 8u) ? call_mesh_encode_leave(&mm, 77, g_msgs[k], CALL_MESH_MAX_MSG, toy_sign,
                                                    skey)
                           : call_mesh_encode_member(&mm, 77, g_msgs[k], CALL_MESH_MAX_MSG,
                                                     toy_sign, skey);
        if (w < 0) abort();
        g_mlen[k] = (uint32_t) w;
        if ((how & 0x70u) == 0x70u && w > 20) {
            g_msgs[k][w - 1] ^= 1; /* bad signature */
            bad[k] = true;
        }
        for (uint32_t j = 0; j < k; j++)
            if (ids_seen[j] == mm.id) distinct = false;
        ids_seen[k] = mm.id;
    }
    call_mesh_t A, B;
    memset(g_ta, 0, sizeof g_ta);
    memset(g_tb, 0, sizeof g_tb);
    call_mesh_init(&A, 77, g_ta, MESH_CAP, toy_verify, NULL);
    call_mesh_init(&B, 77, g_tb, MESH_CAP, toy_verify, NULL);
    for (uint32_t k = 0; k < nm; k++) {
        memcpy(g_snap, g_ta, sizeof g_snap);
        uint32_t n0 = A.n, ch0 = A.changes;
        uint32_t gid;
        uint8_t type;
        static call_mesh_member_t pm;
        if (call_mesh_parse(g_msgs[k], g_mlen[k], &gid, &type, &pm) != CALL_OK) abort();
        int32_t at = find_index(&A, pm.id);
        uint32_t old_ver = at >= 0 ? A.members[at].version : 0;
        uint8_t old_key[32];
        if (at >= 0) memcpy(old_key, A.members[at].pubkey, 32);
        int r = call_mesh_on_message(&A, g_msgs[k], g_mlen[k]);
        if (bad[k] && r != CALL_ERR_AUTH) abort();
        if (r == CALL_ERR_CONFLICT) {
            /* only that member changes: dead and equivocated, nothing else */
            if (at < 0 || A.n != n0 || !A.members[at].equivocated || A.members[at].alive) abort();
            if (A.changes != ch0 + g_snap[at].alive) abort();
            g_snap[at].alive = 0;
            g_snap[at].equivocated = 1;
            if (memcmp(g_snap, g_ta, sizeof g_snap) != 0) abort();
        } else if (r <= 0) {
            if (A.n != n0 && !(r == 0 && A.n == n0 + 1u && type == CALL_MESH_MSG_LEAVE)) abort();
            if (r < 0 && memcmp(g_snap, g_ta, sizeof g_snap) != 0) abort();
            if (r < 0 && A.changes != ch0) abort();
        } else if (A.changes != ch0 + 1u) {
            abort();
        }
        if (at >= 0) {
            if (memcmp(A.members[at].pubkey, old_key, 32) != 0) abort();
            if (A.members[at].version < old_ver) abort();
        }
        for (uint32_t i = 1; i < A.n; i++)
            if (A.members[i - 1].id >= A.members[i].id) abort();
        for (uint32_t i = 0; i < A.n; i++) {
            if (A.members[i].equivocated && A.members[i].alive) abort();
            if (A.members[i].id != call_mesh_id_of(A.members[i].pubkey)) abort();
        }
    }
    for (uint32_t k = nm; k-- > 0;) (void) call_mesh_on_message(&B, g_msgs[k], g_mlen[k]);
    if (distinct) {
        if (A.n != B.n || memcmp(g_ta, g_tb, sizeof g_ta) != 0) abort();
    }
    /* local failure detection on one member */
    uint8_t f = fz_u8(&in);
    if (f & 1u) {
        (void) call_mesh_mark_failed(&A, ids[(f >> 1) & 7u]);
        (void) call_mesh_mark_failed(&B, ids[(f >> 1) & 7u]);
    }
    A.max_depth = B.max_depth = (uint8_t) (1u + (f >> 4) % 6u);
    call_mesh_plan_t pa;
    int r = build(&A, &pa);
    if (r == CALL_OK) {
        check_plan(&A, &pa);
        uint32_t ha = pa.hash, ne = pa.nedges;
        call_mesh_plan_t pb;
        if (build(&A, &pb) != CALL_OK || pb.hash != ha || pb.nedges != ne) abort();
        if (distinct) {
            if (build(&B, &pb) != CALL_OK || pb.hash != ha || pb.nedges != ne) abort();
        }
    }
}

/* ---- mode 8: FEC header and a hostile decoder feed ---- */

static call_fec_mslot_t g_ms[64];
static uint8_t g_mmem[64 * 600];
static call_fec_fslot_t g_fs[8];
static uint8_t g_fmem[8 * 600], g_work[600];

typedef struct {
    uint32_t ssrc;
    uint32_t n;
    uint8_t pkts[64][600];
    uint32_t lens[64];
} cap_t;
static cap_t g_cap;

static int emit_check(void *ctx, const uint8_t *pkt, uint32_t len)
{
    cap_t *c = (cap_t *) ctx;
    call_rtp_hdr_t h;
    if (call_rtp_parse(pkt, len, &h, NULL) < 0) abort();
    if (h.ssrc != c->ssrc) abort();
    if (c->n < 64 && len <= 600) {
        memcpy(c->pkts[c->n], pkt, len);
        c->lens[c->n] = len;
    }
    c->n++;
    return 0;
}

static void mode_fec_hdr(const uint8_t *p, size_t n)
{
    uint8_t *b = fz_dup(p, n);
    call_fec_hdr_t fh;
    int r = call_fec_hdr_parse(b, (uint32_t) n, &fh);
    if (r >= 0) {
        if (r != CALL_FEC_HDR_LEN || fh.n == 0 || fh.n > CALL_FEC_MAX_K) abort();
        if (fh.plen != n - CALL_FEC_HDR_LEN || fh.plen < CALL_RTP_HDR_LEN + 1) abort();
    }
    free(b);
    /* the same bytes as a sequence of length-prefixed packets into a decoder */
    call_fec_dec_t d;
    if (!call_fec_dec_init(&d, 3, g_ms, g_mmem, 64, 600, g_fs, g_fmem, 8, 600, g_work)) abort();
    memset(&g_cap, 0, sizeof g_cap);
    g_cap.ssrc = 3;
    fz_in in;
    fz_init(&in, p, n);
    while (in.n > 2) {
        uint8_t kind = fz_u8(&in);
        uint32_t len = fz_u16(&in) % 700u;
        if (len > in.n) len = (uint32_t) in.n;
        uint8_t *pk = fz_dup(in.p, len);
        in.p += len;
        in.n -= len;
        if (kind & 1)
            (void) call_fec_dec_parity(&d, pk, len, emit_check, &g_cap);
        else
            (void) call_fec_dec_media(&d, pk, len, emit_check, &g_cap);
        free(pk);
    }
}

/* ---- mode 9: CC feedback ---- */

static void mode_cc_fb(const uint8_t *p, size_t n)
{
    if (n < 12) return;
    fz_in in;
    fz_init(&in, p, n);
    uint32_t minb = fz_u32(&in), maxb = fz_u32(&in), start = fz_u32(&in);
    call_cc_tx_t tx;
    call_cc_tx_init(&tx, start, minb, maxb);
    uint32_t lo = tx.min_bps, hi = tx.max_bps;
    if (lo == 0 || hi < lo) abort();
    uint32_t now = 0;
    while (in.n >= CALL_CC_FB_LEN) {
        uint8_t *b = fz_dup(in.p, CALL_CC_FB_LEN);
        in.p += CALL_CC_FB_LEN;
        in.n -= CALL_CC_FB_LEN;
        call_cc_feedback_t fb;
        if (call_cc_fb_parse(b, CALL_CC_FB_LEN, &fb) >= 0) {
            if (call_cc_fb_write(&fb, g_out, sizeof g_out) != CALL_CC_FB_LEN) abort();
            if (memcmp(g_out, b, CALL_CC_FB_LEN) != 0) abort();
            now += fz_u16(&in);
            uint32_t t = call_cc_tx_on_feedback(&tx, &fb, now, now * 1000u);
            if (t < lo || t > hi || t != tx.target_bps) abort();
            if (tx.delay_bps < lo || tx.delay_bps > hi || tx.loss_bps < lo || tx.loss_bps > hi)
                abort();
        }
        free(b);
    }
    /* a short or long buffer is never accepted */
    call_cc_feedback_t fb;
    uint8_t *b = fz_dup(p, n);
    if (n != CALL_CC_FB_LEN && call_cc_fb_parse(b, (uint32_t) n, &fb) >= 0) abort();
    free(b);
}

/* ---- mode 10: packetize / reassemble ---- */

typedef struct {
    uint32_t n;
    uint8_t (*pk)[1600];
    uint32_t *len;
} pk_list_t;
static uint8_t g_pk[256][1600];
static uint32_t g_pklen[256];
static uint8_t g_frame[4096];
static uint8_t g_scratch[2048];

static int emit_store(void *ctx, const uint8_t *pkt, uint32_t len)
{
    pk_list_t *l = (pk_list_t *) ctx;
    if (l->n >= 256 || len > 1600) abort();
    memcpy(l->pk[l->n], pkt, len);
    l->len[l->n] = len;
    l->n++;
    return 0;
}

static void mode_packetize(const uint8_t *p, size_t n)
{
    fz_in in;
    fz_init(&in, p, n);
    uint32_t flen = 1u + fz_u16(&in) % 4096u;
    uint16_t mtu = (uint16_t) (CALL_RTP_HDR_LEN + 1u + fz_u16(&in) % 1500u);
    for (uint32_t i = 0; i < flen; i++) g_frame[i] = (uint8_t) (fz_u8(&in) ^ (i * 37u));
    call_packetizer_t pz;
    call_packetizer_init(&pz, 0xabcdu, CALL_PT_VP8, 2, mtu, fz_u16(&in));
    pk_list_t l = {0, g_pk, g_pklen};
    uint32_t maxp = mtu - CALL_RTP_HDR_LEN;
    uint32_t want = (flen + maxp - 1u) / maxp;
    int r = call_packetize(&pz, g_frame, flen, 1234u, true, g_scratch, sizeof g_scratch,
                           emit_store, &l);
    if (want > CALL_RTP_MAX_FRAGS) {
        if (r != CALL_ERR_SPACE || l.n != 0) abort();
        return;
    }
    if (r != (int) want || l.n != want) abort();
    call_reasm_t ra;
    call_reasm_init(&ra, g_slots, 4, g_rmem, 4096);
    /* deliver in a fuzz-chosen order, with duplicates */
    uint8_t done[256] = {0};
    uint32_t left = l.n, completes = 0;
    call_frame_t fr;
    uint32_t next = 0;
    while (left) {
        uint32_t i;
        if (in.n) {
            i = fz_u8(&in) % l.n;
        } else { /* input exhausted: the rest in order */
            while (done[next]) next++;
            i = next;
        }
        uint8_t *b = fz_dup(l.pk[i], l.len[i]);
        call_rtp_hdr_t h;
        const uint8_t *pay;
        int pl = call_rtp_parse(b, l.len[i], &h, &pay);
        if (pl < 0 || l.len[i] > mtu) abort();
        if (h.ssrc != 0xabcdu || h.frag_cnt != l.n || h.frag_idx != i) abort();
        if (h.marker != (i + 1u == l.n)) abort();
        int q = call_reasm_push(&ra, &h, pay, &fr);
        if (q < 0) abort();
        if (done[i] && q != 0) abort();
        if (!done[i]) {
            done[i] = 1;
            left--;
        }
        if (q == 1) {
            completes++;
            if (left != 0 || fr.len != flen || memcmp(fr.data, g_frame, flen) != 0) abort();
            if (!fr.keyframe || fr.layer != 2 || fr.pt != CALL_PT_VP8 || fr.ts != 1234u) abort();
        }
        free(b);
    }
    if (completes != 1 || ra.frames_out != 1) abort();
}

/* ---- mode 11: FEC recovery ---- */

static uint8_t g_parity[600];
static uint8_t g_fscratch[700];
static uint8_t g_par[64][700];
static uint32_t g_parlen[64];
static uint32_t g_npar;

static int emit_parity(void *ctx, const uint8_t *pkt, uint32_t len)
{
    (void) ctx;
    if (g_npar >= 64 || len > 700) abort();
    memcpy(g_par[g_npar], pkt, len);
    g_parlen[g_npar++] = len;
    return 0;
}

static void mode_fec(const uint8_t *p, size_t n)
{
    fz_in in;
    fz_init(&in, p, n);
    uint8_t k = (uint8_t) (2u + fz_u8(&in) % (CALL_FEC_MAX_K - 1u));
    uint16_t mtu = (uint16_t) (CALL_RTP_HDR_LEN + 1u + fz_u16(&in) % 400u);
    uint32_t flen = 1u + fz_u16(&in) % 4096u;
    uint32_t maxp = mtu - CALL_RTP_HDR_LEN;
    if ((flen + maxp - 1u) / maxp > 48u) flen = 48u * maxp;
    for (uint32_t i = 0; i < flen; i++) g_frame[i] = fz_u8(&in);
    call_packetizer_t pz;
    uint16_t seq0 = fz_u16(&in);
    call_packetizer_init(&pz, 3, CALL_PT_OPUS, 0, mtu, seq0);
    pk_list_t l = {0, g_pk, g_pklen};
    int r = call_packetize(&pz, g_frame, flen, 99u, false, g_scratch, sizeof g_scratch,
                           emit_store, &l);
    if (r <= 0) abort();
    call_fec_enc_t e;
    if (!call_fec_enc_init(&e, 3, 9, k, g_parity, sizeof g_parity, 0)) abort();
    g_npar = 0;
    for (uint32_t i = 0; i < l.n; i++) {
        int q = call_fec_enc_add(&e, l.pk[i], l.len[i], g_fscratch, sizeof g_fscratch,
                                 emit_parity, NULL);
        if (q < 0) abort();
    }
    if (call_fec_enc_flush(&e, g_fscratch, sizeof g_fscratch, emit_parity, NULL) < 0) abort();
    uint32_t groups = (l.n + k - 1u) / k;
    if (g_npar != groups) abort();
    /* drop at most one per group */
    uint8_t drop[256] = {0};
    uint32_t ndrop = 0;
    for (uint32_t g = 0; g < groups; g++) {
        uint8_t c = fz_u8(&in);
        uint32_t gsz = (g + 1u) * k <= l.n ? k : l.n - g * k;
        if (c & 1) {
            drop[g * k + (c >> 1) % gsz] = 1;
            ndrop++;
        }
    }
    bool parity_first = (fz_u8(&in) & 1u) != 0;
    call_fec_dec_t d;
    if (!call_fec_dec_init(&d, 3, g_ms, g_mmem, 64, 600, g_fs, g_fmem, 8, 600, g_work)) abort();
    memset(&g_cap, 0, sizeof g_cap);
    g_cap.ssrc = 3;
    /* parity in batches of <= 8 groups (the decoder keeps 8 parity slots) */
    for (uint32_t g0 = 0; g0 < groups; g0 += 8) {
        uint32_t g1 = g0 + 8 < groups ? g0 + 8 : groups;
        if (parity_first)
            for (uint32_t g = g0; g < g1; g++)
                if (call_fec_dec_parity(&d, g_par[g], g_parlen[g], emit_check, &g_cap) < 0)
                    abort();
        for (uint32_t i = g0 * k; i < g1 * k && i < l.n; i++)
            if (!drop[i] && call_fec_dec_media(&d, l.pk[i], l.len[i], emit_check, &g_cap) < 0)
                abort();
        if (!parity_first)
            for (uint32_t g = g0; g < g1; g++)
                if (call_fec_dec_parity(&d, g_par[g], g_parlen[g], emit_check, &g_cap) < 0)
                    abort();
    }
    /* With parity first, the last packet of a group is rebuilt before it
     * arrives (legitimate); otherwise exactly the dropped ones come back. */
    if (d.bad_recovery != 0 || d.recovered != g_cap.n) abort();
    if (!parity_first && g_cap.n != ndrop) abort();
    if (g_cap.n < ndrop || g_cap.n > 64) abort();
    for (uint32_t c = 0; c < g_cap.n; c++) {
        /* every emitted packet is byte-exact one of the originals */
        bool found = false;
        for (uint32_t i = 0; i < l.n && !found; i++)
            if (g_cap.lens[c] == l.len[i] && !memcmp(g_cap.pkts[c], l.pk[i], l.len[i]))
                found = true;
        if (!found) abort();
    }
    for (uint32_t i = 0; i < l.n; i++) {
        if (!drop[i]) continue;
        bool found = false;
        for (uint32_t c = 0; c < g_cap.n; c++)
            if (g_cap.lens[c] == l.len[i] && !memcmp(g_cap.pkts[c], l.pk[i], l.len[i]))
                found = true;
        if (!found) abort();
    }
}

/* ---- mode 12: CC receiver ---- */

static void mode_cc_rx(const uint8_t *p, size_t n)
{
    fz_in in;
    fz_init(&in, p, n);
    call_cc_rx_t rx;
    uint32_t now = fz_u32(&in);
    call_cc_rx_init(&rx, 5, fz_u8(&in), now);
    uint32_t send = fz_u32(&in);
    while (in.n >= 6) {
        uint8_t c = fz_u8(&in);
        uint16_t seq = fz_u16(&in);
        if ((c & 0x90u) == 0x90u)
            send += 0x7FFFFFFFu; /* the largest forward jump that still opens a new group */
        else if (c & 0x80u)
            send = fz_u32(&in); /* arbitrary (hostile) sender clock */
        else
            send += (uint32_t) fz_u16(&in) * ((c & 0x40u) ? 64u : 1u);
        now += (uint32_t) fz_u8(&in) * ((c & 0x20u) ? 4096u : 16u);
        call_cc_rx_on_packet(&rx, seq, send, now, fz_u16(&in));
        if (rx.win_n > CALL_CC_WINDOW || rx.win_head >= CALL_CC_WINDOW) abort();
        call_cc_feedback_t fb, fb2;
        memset(&fb, 0, sizeof fb); /* padding too: compared with memcmp below */
        memset(&fb2, 0, sizeof fb2);
        if (call_cc_rx_report(&rx, now, &fb)) {
            if (call_cc_fb_write(&fb, g_out, sizeof g_out) != CALL_CC_FB_LEN) abort();
            if (call_cc_fb_parse(g_out, CALL_CC_FB_LEN, &fb2) != CALL_CC_FB_LEN) abort();
            if (memcmp(&fb, &fb2, sizeof fb) != 0) abort();
        }
    }
}

/* ---- mode 13: encap / decap round trip ---- */

static void mode_encap(const uint8_t *p, size_t n)
{
    fz_in in;
    fz_init(&in, p, n);
    uint8_t c = fz_u8(&in);
    uint32_t len;
    uint8_t *pkt = g_out2;
    if (c & 1) { /* a raw payload with stated length */
        len = 1u + fz_u16(&in) % CALL_UBH_MAX_PACKET;
        for (uint32_t i = 0; i < len; i++) pkt[i] = fz_u8(&in);
    } else {
        call_rtp_hdr_t h;
        memset(&h, 0, sizeof h);
        h.frame_len = 1u + fz_u32(&in) % CALL_RTP_MAX_FRAME;
        h.frag_cnt = (uint8_t) (1u + fz_u8(&in) % 255u);
        if (h.frag_cnt > h.frame_len) h.frag_cnt = (uint8_t) h.frame_len;
        h.frag_idx = (uint8_t) (fz_u8(&in) % h.frag_cnt);
        h.marker = h.frag_idx + 1u == h.frag_cnt;
        h.seq = fz_u16(&in);
        h.ssrc = fz_u32(&in);
        uint32_t off, fl;
        if (!call_rtp_frag_span(&h, &off, &fl)) return;
        len = CALL_RTP_HDR_LEN + fl;
        if (len > CALL_UBH_MAX_PACKET) return;
        if (call_rtp_hdr_write(&h, pkt, CALL_RTP_HDR_LEN) != CALL_RTP_HDR_LEN) abort();
        for (uint32_t i = 0; i < fl; i++) pkt[CALL_RTP_HDR_LEN + i] = fz_u8(&in);
        call_rtp_hdr_t h2;
        if (call_rtp_parse(pkt, len, &h2, NULL) != (int) fl) abort();
    }
    for (int k = 0; k < 2; k++) {
        call_carriage_t car = k ? CALL_CARRIAGE_UBH168 : CALL_CARRIAGE_PLAIN;
        static uint8_t wire[4096], back[4096];
        int w = call_rtp_encap(car, pkt, len, wire, sizeof wire);
        if (w < 0 || (uint32_t) w != call_rtp_wire_len(car, len)) abort();
        uint8_t *wd = fz_dup(wire, (size_t) w);
        int L = call_rtp_decap(car, wd, (uint32_t) w, back, sizeof back, (c & 1) ? len : 0);
        if (L != (int) len || memcmp(back, pkt, len) != 0) abort();
        /* a nonzero byte in the padding, or one extra frame, is refused */
        if (car == CALL_CARRIAGE_UBH168) {
            if (len % CALL_UBH_PAYLOAD) {
                uint32_t last = (uint32_t) w - 1u;
                wd[last] = 1;
                if (call_rtp_decap(car, wd, (uint32_t) w, back, sizeof back, (c & 1) ? len : 0) >=
                    0)
                    abort();
            }
        }
        free(wd);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1 || size > 16384) return 0;
    const uint8_t *p = data + 1;
    size_t n = size - 1;
    switch (data[0] % 14u) {
    case 0: mode_rtp(p, n); break;
    case 1: mode_ubh(p, n); break;
    case 2: mode_stun(p, n); break;
    case 3: mode_cand(p, n); break;
    case 4: mode_rv(p, n); break;
    case 5: mode_sig(p, n); break;
    case 6: mode_mesh_parse(p, n); break;
    case 7: mode_mesh(p, n); break;
    case 8: mode_fec_hdr(p, n); break;
    case 9: mode_cc_fb(p, n); break;
    case 10: mode_packetize(p, n); break;
    case 11: mode_fec(p, n); break;
    case 12: mode_cc_rx(p, n); break;
    default: mode_encap(p, n); break;
    }
    return 0;
}
