/*
 * decent.c — Native Decentralized Protocol Suite Implementation
 *
 * IPFS/libp2p, BitTorrent v2, Matrix E2EE, DID/W3C, SDR off-grid mesh.
 * All protocols run over PLNP + PungentClove at the kernel level.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */

#ifdef TEST_HOST
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#else
#include "freestanding.h"
#endif

#include "decent.h"

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

static void dc_strcpy(char *dst, const char *src) {
    uint32_t i = 0; while (src[i]) { dst[i] = src[i]; i++; } dst[i] = '\0';
}

static __attribute__((unused)) int dc_strcmp(const char *a, const char *b) {
    uint32_t i = 0; while (a[i] && b[i]) {
        if (a[i] != b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        i++;
    }
    return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
}

static void dc_hash(const char *data, uint32_t len, uint8_t *out) {
    uint64_t h = 0xcbf29ce484222325ULL; uint32_t i;
    for (i = 0; i < len; i++) { h ^= (uint8_t)data[i]; h *= 0x100000001b3ULL; }
    for (i = 0; i < DECENT_HASH_SIZE; i++) {
        out[i] = (uint8_t)(h >> ((i % 8) * 8));
        if (i % 8 == 7) { h *= 0x100000001b3ULL; h ^= 0x5a; }
    }
}

/* ===== Init ===== */

void decent_init(decent_t *d) {
    dc_memset(d, 0, sizeof(decent_t));
    d->initialized = true;
    d->sdr_fallback_active = false;
}

/* ===== IPFS ===== */

int32_t decent_ipfs_node_add(decent_t *d, const char *peer_id, const char *addr) {
    if (d->num_ipfs_nodes >= DECENT_MAX_NODES || !peer_id || !addr) return -1;
    int32_t idx = (int32_t)d->num_ipfs_nodes;
    decent_ipfs_node_t *n = &d->ipfs_nodes[idx];
    dc_memset(n, 0, sizeof(decent_ipfs_node_t));
    dc_strcpy(n->peer_id, peer_id);
    dc_strcpy(n->addr, addr);
    dc_hash(peer_id, dc_strlen(peer_id), n->pubkey);
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

const decent_cid_t *decent_ipfs_cid_find(decent_t *d, const uint8_t *hash) {
    uint32_t i;
    for (i = 0; i < d->num_cids; i++)
        if (dc_memcmp(d->cids[i].hash, hash, DECENT_HASH_SIZE) == 0)
            return &d->cids[i];
    return NULL;
}

int decent_ipfs_bitswap(decent_t *d, uint32_t node_idx, uint32_t cid_idx) {
    if (node_idx >= d->num_ipfs_nodes || cid_idx >= d->num_cids) return -1;
    if (!d->ipfs_nodes[node_idx].connected) return -1;
    d->ipfs_nodes[node_idx].bitswap_sessions++;
    d->cids[cid_idx].resolved = true;
    return 0;
}

/* ===== BitTorrent v2 ===== */

int32_t decent_bt_swarm_create(decent_t *d, const char *label, uint32_t piece_length) {
    if (d->num_bt_swarms >= DECENT_MAX_BT_SWARMS || !label) return -1;
    int32_t idx = (int32_t)d->num_bt_swarms;
    decent_bt_swarm_t *s = &d->bt_swarms[idx];
    dc_memset(s, 0, sizeof(decent_bt_swarm_t));
    dc_strcpy(s->label, label);
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
                                   bool encrypted) {
    if (d->num_matrix_rooms >= DECENT_MAX_MATRIX_ROOMS || !room_id) return -1;
    int32_t idx = (int32_t)d->num_matrix_rooms;
    decent_matrix_room_t *r = &d->matrix_rooms[idx];
    dc_memset(r, 0, sizeof(decent_matrix_room_t));
    dc_strcpy(r->room_id, room_id);
    if (name) dc_strcpy(r->name, name);
    r->encrypted = encrypted;
    r->active = true;
    if (encrypted) {
        dc_hash(room_id, dc_strlen(room_id), r->room_key);
    }
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

    decent_matrix_event_t *e = &r->events[r->num_events];
    dc_memset(e, 0, sizeof(decent_matrix_event_t));
    e->type = type;
    dc_strcpy(e->sender, sender);
    dc_strcpy(e->content, content);
    e->timestamp = r->num_events + 1;
    e->encrypted = r->encrypted;

    /* Hash the event */
    char event_data[DECENT_MAX_LABEL + 256];
    dc_strcpy(event_data, sender);
    dc_memcpy(event_data + dc_strlen(sender), content, dc_strlen(content));
    dc_hash(event_data, dc_strlen(sender) + dc_strlen(content), e->event_hash);

    r->num_events++;
    return 0;
}

/* ===== DID ===== */

int32_t decent_did_create(decent_t *d, const char *label, uint8_t key_index) {
    if (d->num_dids >= DECENT_MAX_DIDS || !label || key_index < 1 || key_index > 5) return -1;
    int32_t idx = (int32_t)d->num_dids;
    decent_did_t *did = &d->dids[idx];
    dc_memset(did, 0, sizeof(decent_did_t));

    /* Generate DID string: did:zede:K<n> */
    char did_str[32];
    did_str[0] = 'd'; did_str[1] = 'i'; did_str[2] = 'd'; did_str[3] = ':';
    did_str[4] = 'z'; did_str[5] = 'e'; did_str[6] = 'd'; did_str[7] = 'e';
    did_str[8] = ':'; did_str[9] = 'K'; did_str[10] = '0' + key_index;
    did_str[11] = '\0';
    dc_strcpy(did->did, did_str);

    did->key_index = key_index;
    dc_strcpy(did->label, label);
    dc_hash(label, dc_strlen(label), did->pubkey);
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

int decent_did_zk_verify(decent_t *d, uint32_t idx) {
    if (idx >= d->num_dids) return -1;
    /* In real implementation: zk-SNARK verification */
    d->dids[idx].zk_verified = true;
    return 0;
}

/* ===== SDR ===== */

int32_t decent_sdr_channel_create(decent_t *d, const char *name, uint32_t freq_hz,
                                   uint32_t bw_hz, uint8_t modulation) {
    if (d->num_sdr_channels >= DECENT_MAX_SDR_CHANNELS || !name) return -1;
    int32_t idx = (int32_t)d->num_sdr_channels;
    decent_sdr_channel_t *ch = &d->sdr_channels[idx];
    dc_memset(ch, 0, sizeof(decent_sdr_channel_t));
    dc_strcpy(ch->name, name);
    ch->frequency_hz = freq_hz;
    ch->bandwidth_hz = bw_hz;
    ch->modulation = modulation;
    ch->tx_power_dbm = 20;  /* Default 20dBm */
    ch->active = false;
    ch->garlic_encrypted = true;  /* PungentClove by default */
    d->num_sdr_channels++;
    return idx;
}

int decent_sdr_channel_activate(decent_t *d, uint32_t idx) {
    if (idx >= d->num_sdr_channels) return -1;
    d->sdr_channels[idx].active = true;
    return 0;
}

int decent_sdr_send_bulb(decent_t *d, uint32_t channel_idx, const uint8_t *data, uint32_t len) {
    if (channel_idx >= d->num_sdr_channels) return -1;
    decent_sdr_channel_t *ch = &d->sdr_channels[channel_idx];
    if (!ch->active) return -1;
    ch->packets_sent++;
    (void)data; (void)len;  /* In real impl: modulate and transmit */
    return (int)len;
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
        case DECENT_PROTO_IPFS:       return "IPFS/Libp2p";
        case DECENT_PROTO_BITTORRENT: return "BitTorrent v2";
        case DECENT_PROTO_MATRIX:     return "Matrix E2EE";
        case DECENT_PROTO_DID:        return "DID/W3C";
        case DECENT_PROTO_SDR:        return "SDR/Retevis";
        default: return "Unknown";
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
