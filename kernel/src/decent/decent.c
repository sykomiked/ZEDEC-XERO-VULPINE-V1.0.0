/*
 * decent.c — Decentralized-protocol bookkeeping model (see decent.h for
 * exactly what is real: SHA-256 content IDs, SHA3-256 key hashes and
 * ChaCha20-Poly1305 room events; everything else is a table).
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

#include "decent.h"
#include "../mlkem/keccak.h"
#include "../robin_debanks/sha256.h"
#include "../tls/aead.h"

/* ===== Helpers ===== */

static void dc_memset(void *dst, int v, uint32_t n) {
    uint8_t *d = (uint8_t *)dst; uint32_t i;
    for (i = 0; i < n; i++) d[i] = (uint8_t)v;
}

static void dc_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst; const uint8_t *s = (const uint8_t *)src; uint32_t i;
    for (i = 0; i < n; i++) d[i] = s[i];
}

static int dc_memcmp(const void *a, const void *b, uint32_t n) {
    const uint8_t *pa = a, *pb = b; uint32_t i;
    for (i = 0; i < n; i++) if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    return 0;
}

static uint32_t dc_strlen(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }

static __attribute__((unused)) int dc_strcmp(const char *a, const char *b) {
    uint32_t i = 0; while (a[i] && b[i]) {
        if (a[i] != b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        i++;
    }
    return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
}

/* Bounded string copy: always NUL-terminates within cap. */
static void dc_strncpy(char *dst, const char *src, uint32_t cap)
{
    uint32_t i = 0;
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Per-event AEAD nonce: the event sequence number, little-endian. */
static void dc_event_nonce(uint64_t seq, uint8_t nonce[CHACHA20_NONCE_LEN])
{
    dc_memset(nonce, 0, CHACHA20_NONCE_LEN);
    for (uint32_t i = 0; i < 8; i++) nonce[4 + i] = (uint8_t) (seq >> (8 * i));
}

/* AAD binds the room id and sender to the ciphertext. */
static uint32_t dc_event_aad(const char *room_id, const char *sender, uint8_t *aad, uint32_t cap)
{
    uint32_t n = 0;
    for (uint32_t i = 0; room_id[i] && n < cap; i++) aad[n++] = (uint8_t) room_id[i];
    if (n < cap) aad[n++] = 0;
    for (uint32_t i = 0; sender[i] && n < cap; i++) aad[n++] = (uint8_t) sender[i];
    return n;
}

/* ===== Init ===== */

void decent_init(decent_t *d) {
    dc_memset(d, 0, sizeof(decent_t));
    d->initialized = true;
    d->sdr_fallback_active = false;
}

/* ===== IPFS ===== */

int32_t decent_ipfs_node_add(decent_t *d, const char *peer_id, const char *addr,
                             const uint8_t *pubkey, uint32_t pubkey_len)
{
    if (d->num_ipfs_nodes >= DECENT_MAX_NODES || !peer_id || !addr) return -1;
    if (!pubkey || pubkey_len == 0) return -1;
    int32_t idx = (int32_t)d->num_ipfs_nodes;
    decent_ipfs_node_t *n = &d->ipfs_nodes[idx];
    dc_memset(n, 0, sizeof(decent_ipfs_node_t));
    dc_strncpy(n->peer_id, peer_id, DECENT_MAX_ADDR);
    dc_strncpy(n->addr, addr, DECENT_MAX_ADDR);
    sha3_256(pubkey, pubkey_len, n->pubkey);
    n->connected = false;
    d->num_ipfs_nodes++;
    return idx;
}

int decent_ipfs_node_connect(decent_t *d, uint32_t idx) {
    if (idx >= d->num_ipfs_nodes) return -1;
    d->ipfs_nodes[idx].connected = true;
    return 0;
}

int32_t decent_ipfs_cid_register(decent_t *d, const uint8_t *hash, uint32_t codec,
                                  uint32_t size, decent_cid_version_t ver) {
    if (d->num_cids >= DECENT_MAX_CIDS || !hash) return -1;
    int32_t idx = (int32_t)d->num_cids;
    decent_cid_t *c = &d->cids[idx];
    dc_memset(c, 0, sizeof(decent_cid_t));
    dc_memcpy(c->hash, hash, DECENT_HASH_SIZE);
    c->version = ver;
    c->codec = codec;
    c->multihash_type = 0x12;  /* sha2-256 */
    c->size = size;
    c->resolved = false;
    d->num_cids++;
    return idx;
}

int32_t decent_ipfs_cid_register_content(decent_t *d, const uint8_t *data, uint32_t len,
                                         uint32_t codec)
{
    if (!d || (len > 0 && !data)) return -1;
    uint8_t h[DECENT_HASH_SIZE];
    sha256(data, len, h);
    int32_t idx = decent_ipfs_cid_register(d, h, codec, len, DECENT_CID_V1);
    if (idx >= 0) d->cids[idx].resolved = true; /* we hold the content */
    return idx;
}

const decent_cid_t *decent_ipfs_cid_find(decent_t *d, const uint8_t *hash) {
    uint32_t i;
    for (i = 0; i < d->num_cids; i++)
        if (dc_memcmp(d->cids[i].hash, hash, DECENT_HASH_SIZE) == 0)
            return &d->cids[i];
    return NULL;
}

int decent_ipfs_bitswap(decent_t *d, uint32_t node_idx, uint32_t cid_idx, const uint8_t *block,
                        uint32_t block_len)
{
    if (node_idx >= d->num_ipfs_nodes || cid_idx >= d->num_cids) return -1;
    if (!d->ipfs_nodes[node_idx].connected) return -1;
    if (!block && block_len > 0) return -1;
    decent_cid_t *c = &d->cids[cid_idx];
    if (c->multihash_type != 0x12) return DECENT_ENOTIMPL; /* only sha2-256 */
    uint8_t h[DECENT_HASH_SIZE];
    sha256(block, block_len, h);
    d->ipfs_nodes[node_idx].bitswap_sessions++;
    if (dc_memcmp(h, c->hash, DECENT_HASH_SIZE) != 0) return DECENT_EMISMATCH;
    c->resolved = true;
    c->size = block_len;
    d->ipfs_nodes[node_idx].bytes_exchanged += block_len;
    return 0;
}

/* ===== BitTorrent v2 ===== */

int32_t decent_bt_swarm_create(decent_t *d, const char *label, uint32_t piece_length) {
    if (d->num_bt_swarms >= DECENT_MAX_BT_SWARMS || !label) return -1;
    int32_t idx = (int32_t)d->num_bt_swarms;
    decent_bt_swarm_t *s = &d->bt_swarms[idx];
    dc_memset(s, 0, sizeof(decent_bt_swarm_t));
    dc_strncpy(s->label, label, DECENT_MAX_LABEL);
    s->piece_length = piece_length;
    s->num_pieces = 0;
    s->seeding = false;
    s->leeching = false;
    d->num_bt_swarms++;
    return idx;
}

int decent_bt_swarm_set_merkle(decent_t *d, uint32_t idx, const uint8_t *root) {
    if (idx >= d->num_bt_swarms || !root) return -1;
    dc_memcpy(d->bt_swarms[idx].merkle_root, root, DECENT_HASH_SIZE);
    dc_memcpy(d->bt_swarms[idx].info_hash, root, DECENT_HASH_SIZE);
    return 0;
}

int decent_bt_swarm_add_peer(decent_t *d, uint32_t swarm_idx, uint32_t peer_idx) {
    if (swarm_idx >= d->num_bt_swarms) return -1;
    decent_bt_swarm_t *s = &d->bt_swarms[swarm_idx];
    if (s->num_peers >= DECENT_MAX_PEERS_PER_SWARM) return -1;
    s->peers[s->num_peers] = peer_idx;
    s->num_peers++;
    return 0;
}

int decent_bt_swarm_start_seeding(decent_t *d, uint32_t idx) {
    if (idx >= d->num_bt_swarms) return -1;
    d->bt_swarms[idx].seeding = true;
    d->bt_swarms[idx].leeching = false;
    return 0;
}

/* ===== Matrix ===== */

int32_t decent_matrix_room_create(decent_t *d, const char *room_id, const char *name,
                                  bool encrypted, const uint8_t *room_key)
{
    if (d->num_matrix_rooms >= DECENT_MAX_MATRIX_ROOMS || !room_id) return -1;
    /* The old code derived the "E2EE" key from the public room id. An
     * encrypted room now needs a real key from the caller. */
    if (encrypted && !room_key) return -1;
    int32_t idx = (int32_t)d->num_matrix_rooms;
    decent_matrix_room_t *r = &d->matrix_rooms[idx];
    dc_memset(r, 0, sizeof(decent_matrix_room_t));
    dc_strncpy(r->room_id, room_id, DECENT_MAX_LABEL);
    if (name) dc_strncpy(r->name, name, DECENT_MAX_LABEL);
    r->encrypted = encrypted;
    r->active = true;
    if (encrypted) dc_memcpy(r->room_key, room_key, DECENT_HASH_SIZE);
    d->num_matrix_rooms++;
    return idx;
}

int decent_matrix_room_add_member(decent_t *d, uint32_t room_idx, uint32_t member_id) {
    if (room_idx >= d->num_matrix_rooms) return -1;
    decent_matrix_room_t *r = &d->matrix_rooms[room_idx];
    if (r->num_members >= 16) return -1;
    r->members[r->num_members] = member_id;
    r->num_members++;
    return 0;
}

int decent_matrix_room_send_event(decent_t *d, uint32_t room_idx,
                                   decent_matrix_event_type_t type,
                                   const char *sender, const char *content) {
    if (room_idx >= d->num_matrix_rooms || !sender || !content) return -1;
    decent_matrix_room_t *r = &d->matrix_rooms[room_idx];
    if (r->num_events >= DECENT_MAX_MATRIX_EVENTS) return -1;

    uint32_t clen = dc_strlen(content);
    if (clen > sizeof(r->events[0].content)) return -1;

    decent_matrix_event_t *e = &r->events[r->num_events];
    dc_memset(e, 0, sizeof(decent_matrix_event_t));
    e->type = type;
    dc_strncpy(e->sender, sender, DECENT_MAX_LABEL);
    e->content_len = clen;
    e->timestamp = r->num_events + 1;
    if (r->encrypted) {
        uint8_t nonce[CHACHA20_NONCE_LEN], aad[2 * DECENT_MAX_LABEL + 1];
        dc_event_nonce(e->timestamp, nonce);
        uint32_t alen = dc_event_aad(r->room_id, e->sender, aad, sizeof(aad));
        aead_seal(r->room_key, nonce, aad, alen, (const uint8_t *) content, e->content, clen,
                  e->tag);
        e->encrypted = true;
    } else {
        dc_memcpy(e->content, content, clen);
    }

    /* SHA3-256 over sender || NUL || the stored content bytes. */
    uint8_t event_data[DECENT_MAX_LABEL + 1 + 256];
    uint32_t slen = dc_strlen(e->sender);
    dc_memcpy(event_data, e->sender, slen);
    event_data[slen] = 0;
    dc_memcpy(event_data + slen + 1, e->content, clen);
    sha3_256(event_data, slen + 1 + clen, e->event_hash);

    r->num_events++;
    return 0;
}

int decent_matrix_event_open(const decent_t *d, uint32_t room_idx, uint32_t event_idx,
                             const uint8_t *room_key, char *out, uint32_t cap)
{
    if (!d || !out || room_idx >= d->num_matrix_rooms) return -1;
    const decent_matrix_room_t *r = &d->matrix_rooms[room_idx];
    if (event_idx >= r->num_events) return -1;
    const decent_matrix_event_t *e = &r->events[event_idx];
    if (e->content_len + 1 > cap) return -1;
    if (e->encrypted) {
        if (!room_key) return -1;
        uint8_t nonce[CHACHA20_NONCE_LEN], aad[2 * DECENT_MAX_LABEL + 1];
        dc_event_nonce(e->timestamp, nonce);
        uint32_t alen = dc_event_aad(r->room_id, e->sender, aad, sizeof(aad));
        if (!aead_open(room_key, nonce, aad, alen, e->content, (uint8_t *) out, e->content_len,
                       e->tag)) {
            out[0] = 0;
            return -1;
        }
    } else {
        dc_memcpy(out, e->content, e->content_len);
    }
    out[e->content_len] = 0;
    return (int) e->content_len;
}

/* ===== DID ===== */

int32_t decent_did_create(decent_t *d, const char *label, uint8_t key_index, const uint8_t *pubkey,
                          uint32_t pubkey_len)
{
    if (d->num_dids >= DECENT_MAX_DIDS || !label || key_index < 1 || key_index > 5) return -1;
    if (!pubkey || pubkey_len == 0) return -1;
    int32_t idx = (int32_t)d->num_dids;
    decent_did_t *did = &d->dids[idx];
    dc_memset(did, 0, sizeof(decent_did_t));

    /* The key hash is SHA3-256 of the real public key (it used to be a hash
     * of the label), and the DID string carries it, so two keys never share
     * a DID. Format: did:zede:K<n>:<32 hex digits> */
    sha3_256(pubkey, pubkey_len, did->pubkey);
    static const char hex[] = "0123456789abcdef";
    static const char prefix[] = "did:zede:K";
    uint32_t n = 0;
    for (uint32_t i = 0; prefix[i]; i++) did->did[n++] = prefix[i];
    did->did[n++] = (char) ('0' + key_index);
    did->did[n++] = ':';
    for (uint32_t i = 0; i < 16; i++) {
        did->did[n++] = hex[did->pubkey[i] >> 4];
        did->did[n++] = hex[did->pubkey[i] & 15];
    }
    did->did[n] = 0;

    did->key_index = key_index;
    dc_strncpy(did->label, label, DECENT_MAX_LABEL);
    did->verifiable_claims = 0;
    did->zk_verified = false;
    did->active = true;

    d->num_dids++;
    return idx;
}

int decent_did_add_claim(decent_t *d, uint32_t idx) {
    if (idx >= d->num_dids) return -1;
    d->dids[idx].verifiable_claims++;
    return 0;
}

/* FAILS CLOSED. This function previously set zk_verified = true unconditionally,
 * with a comment saying real verification would come later — a security
 * predicate that could not fail. Anything that later trusted the flag would have
 * been trusting nothing.
 *
 * The defect is deeper than the hardcoded result: THE SIGNATURE ACCEPTS NO
 * PROOF. Given only a DID index there is nothing to verify, so no correct
 * implementation of *this* function exists — it cannot succeed honestly. Making
 * it return false is therefore not a placeholder, it is the accurate answer for
 * the interface as declared.
 *
 * The flag stays false and the call reports DECENT_ENOTIMPL. When real
 * verification arrives it needs a different entry point that takes a proof, a
 * verifying key and a public-input vector — see decent_did_zk_verify_proof()
 * declared alongside, which is the shape the work has to fit. Until then a
 * caller cannot mistake "we never checked" for "it passed". */
int decent_did_zk_verify(decent_t *d, uint32_t idx) {
    if (!d || idx >= d->num_dids) return -1;
    d->dids[idx].zk_verified = false;   /* never granted without a proof */
    return DECENT_ENOTIMPL;
}

/* The honest interface. Unimplemented, and it refuses rather than pretends:
 * a verifier that accepts every proof is worse than no verifier, because the
 * system reports a guarantee it does not have. Returns DECENT_ENOTIMPL and
 * leaves zk_verified untouched (false). */
int decent_did_zk_verify_proof(decent_t *d, uint32_t idx,
                               const uint8_t *proof, uint32_t proof_len,
                               const uint8_t *vk, uint32_t vk_len,
                               const uint8_t *public_inputs, uint32_t pi_len) {
    (void)proof; (void)proof_len; (void)vk; (void)vk_len;
    (void)public_inputs; (void)pi_len;
    if (!d || idx >= d->num_dids) return -1;
    d->dids[idx].zk_verified = false;
    return DECENT_ENOTIMPL;
}

/* ===== SDR ===== */

int32_t decent_sdr_channel_create(decent_t *d, const char *name, uint32_t freq_hz,
                                   uint32_t bw_hz, uint8_t modulation) {
    if (d->num_sdr_channels >= DECENT_MAX_SDR_CHANNELS || !name) return -1;
    int32_t idx = (int32_t)d->num_sdr_channels;
    decent_sdr_channel_t *ch = &d->sdr_channels[idx];
    dc_memset(ch, 0, sizeof(decent_sdr_channel_t));
    dc_strncpy(ch->name, name, DECENT_MAX_LABEL);
    ch->frequency_hz = freq_hz;
    ch->bandwidth_hz = bw_hz;
    ch->modulation = modulation;
    ch->tx_power_dbm = 20;  /* Default 20dBm */
    ch->active = false;
    ch->garlic_encrypted = false; /* nothing is encrypted: there is no radio path */
    d->num_sdr_channels++;
    return idx;
}

int decent_sdr_channel_activate(decent_t *d, uint32_t idx) {
    if (idx >= d->num_sdr_channels) return -1;
    d->sdr_channels[idx].active = true;
    return 0;
}

/* FAILS CLOSED. This used to count a packet as sent and return len while
 * transmitting nothing. There is no radio driver, so nothing can be sent. */
int decent_sdr_send_bulb(decent_t *d, uint32_t channel_idx, const uint8_t *data, uint32_t len) {
    if (channel_idx >= d->num_sdr_channels) return -1;
    decent_sdr_channel_t *ch = &d->sdr_channels[channel_idx];
    if (!ch->active) return -1;
    (void) data;
    (void) len;
    return DECENT_ENOTIMPL;
}

int decent_sdr_enable_fallback(decent_t *d) {
    d->sdr_fallback_active = true;
    uint32_t i;
    for (i = 0; i < d->num_sdr_channels; i++)
        d->sdr_channels[i].active = true;
    return 0;
}

/* ===== Utility ===== */

const char *decent_proto_name(decent_proto_t proto) {
    switch (proto) {
    case DECENT_PROTO_IPFS:
        return "content table (model)";
    case DECENT_PROTO_BITTORRENT:
        return "swarm table (model)";
    case DECENT_PROTO_MATRIX:
        return "encrypted rooms (model)";
    case DECENT_PROTO_DID:
        return "DID table (model)";
    case DECENT_PROTO_SDR:
        return "radio channels (no driver)";
    default:
        return "Unknown";
    }
}

const char *decent_cid_codec_name(uint32_t codec) {
    switch (codec) {
        case 0x55: return "raw";
        case 0x70: return "dag-pb";
        case 0x71: return "dag-cbor";
        default: return "unknown";
    }
}

const char *decent_matrix_event_name(decent_matrix_event_type_t type) {
    switch (type) {
        case DECENT_MATRIX_EVENT_MESSAGE: return "message";
        case DECENT_MATRIX_EVENT_STATE:   return "state";
        case DECENT_MATRIX_EVENT_ALERT:   return "alert";
        case DECENT_MATRIX_EVENT_SYNC:    return "sync";
        default: return "unknown";
    }
}

const char *decent_sdr_modulation_name(uint8_t mod) {
    switch (mod) {
        case 0: return "FSK";
        case 1: return "LoRa";
        case 2: return "AFSK";
        case 3: return "GFSK";
        default: return "unknown";
    }
}
