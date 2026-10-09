/*
 * decent.h — Native Decentralized Protocol Suite
 *
 * Zero-dependency C implementations of IPFS/libp2p, BitTorrent v2,
 * Matrix E2EE messaging, DID/W3C identity, and SDR off-grid mesh.
 * All protocols run directly over PLNP + PungentClove.
 *
 * Protocol Matrix:
 *   IPFS/Libp2p  → Global content-addressed VFS (.zedec / GLUT+)
 *   BitTorrent v2 → High-throughput bulk swarms (.36n9 / TRUE)
 *   Matrix       → Decentralized E2EE messaging (.vino / GLUT-)
 *   DID/W3C      → Cryptographic identity stack (.ula / GLUT0)
 *   Retevis/SDR  → Off-grid Sub-GHz radio mesh (.9n63 / Shadow)
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

#ifndef ZEDEC_DECENT_H
#define ZEDEC_DECENT_H

#include <stdint.h>
#include <stdbool.h>

/* ===== Constants ===== */

#define DECENT_MAX_NODES        64
#define DECENT_MAX_CIDS         128
#define DECENT_HASH_SIZE         32
#define DECENT_MAX_ADDR          64
#define DECENT_MAX_LABEL         64
#define DECENT_MAX_DIDS          32
#define DECENT_MAX_MATRIX_ROOMS  16
#define DECENT_MAX_MATRIX_EVENTS 64
#define DECENT_MAX_SDR_CHANNELS   8
#define DECENT_MAX_BT_SWARMS     16
#define DECENT_MAX_PEERS_PER_SWARM 32

/* ===== Protocol Types ===== */

typedef enum {
    DECENT_PROTO_IPFS       = 0,
    DECENT_PROTO_BITTORRENT = 1,
    DECENT_PROTO_MATRIX     = 2,
    DECENT_PROTO_DID        = 3,
    DECENT_PROTO_SDR        = 4,
} decent_proto_t;

/* ===== IPFS CID ===== */

typedef enum {
    DECENT_CID_V0 = 0,  /* Base58 (Qm...) */
    DECENT_CID_V1 = 1,  /* Multibase */
} decent_cid_version_t;

typedef struct {
    decent_cid_version_t version;
    uint8_t  hash[DECENT_HASH_SIZE];
    uint32_t codec;       /* 0x55=raw, 0x70=dag-pb, 0x71=dag-cbor */
    uint32_t multihash_type; /* 0x12=sha2-256, 0x1b=blake3 */
    bool     resolved;    /* Is content locally available? */
    uint32_t size;        /* Content size if known */
} decent_cid_t;

/* ===== IPFS/Libp2p Node ===== */

typedef struct {
    char     peer_id[DECENT_MAX_ADDR];   /* Libp2p PeerID */
    char     addr[DECENT_MAX_ADDR];       /* Multiaddr */
    uint8_t  pubkey[DECENT_HASH_SIZE];    /* Peer public key hash */
    bool     connected;
    uint32_t bitswap_sessions;
    uint64_t bytes_exchanged;
} decent_ipfs_node_t;

/* ===== BitTorrent v2 Swarm ===== */

typedef struct {
    uint8_t  info_hash[DECENT_HASH_SIZE];  /* SHA-256 Merkle root */
    char     label[DECENT_MAX_LABEL];
    uint32_t num_pieces;
    uint32_t piece_length;
    uint8_t  merkle_root[DECENT_HASH_SIZE];
    uint32_t peers[DECENT_MAX_PEERS_PER_SWARM];
    uint32_t num_peers;
    bool     seeding;
    bool     leeching;
    uint64_t total_uploaded;
    uint64_t total_downloaded;
} decent_bt_swarm_t;

/* ===== Matrix Room ===== */

typedef enum {
    DECENT_MATRIX_EVENT_MESSAGE  = 0,
    DECENT_MATRIX_EVENT_STATE    = 1,
    DECENT_MATRIX_EVENT_ALERT    = 2,
    DECENT_MATRIX_EVENT_SYNC     = 3,
} decent_matrix_event_type_t;

typedef struct {
    decent_matrix_event_type_t type;
    char     sender[DECENT_MAX_LABEL];
    char     content[256];
    uint64_t timestamp;
    uint8_t  event_hash[DECENT_HASH_SIZE];
    bool     encrypted;
} decent_matrix_event_t;

typedef struct {
    char     room_id[DECENT_MAX_LABEL];
    char     name[DECENT_MAX_LABEL];
    uint8_t  room_key[DECENT_HASH_SIZE];  /* E2EE session key */
    decent_matrix_event_t events[DECENT_MAX_MATRIX_EVENTS];
    uint32_t num_events;
    uint32_t members[16];
    uint32_t num_members;
    bool     encrypted;
    bool     active;
} decent_matrix_room_t;

/* ===== DID (Decentralized Identity) ===== */

typedef struct {
    char     did[DECENT_MAX_ADDR];     /* did:zede:K1... */
    uint8_t  key_index;                 /* K1..K5 derivation path */
    uint8_t  pubkey[DECENT_HASH_SIZE];
    char     label[DECENT_MAX_LABEL];
    uint32_t verifiable_claims;
    bool     zk_verified;              /* zk-SNARK proof passed */
    bool     active;
} decent_did_t;

/* ===== SDR Radio Channel ===== */

typedef struct {
    char     name[DECENT_MAX_LABEL];
    uint32_t frequency_hz;    /* e.g., 433920000 for 433.92MHz */
    uint32_t bandwidth_hz;
    uint8_t  modulation;      /* 0=FSK, 1=LoRa, 2=AFSK, 3=GFSK */
    uint32_t tx_power_dbm;
    bool     active;
    bool     garlic_encrypted; /* PungentClove bulbs over radio */
    uint64_t packets_sent;
    uint64_t packets_received;
} decent_sdr_channel_t;

/* ===== Decent Protocol Stack ===== */

typedef struct {
    /* IPFS */
    decent_ipfs_node_t  ipfs_nodes[DECENT_MAX_NODES];
    uint32_t            num_ipfs_nodes;
    decent_cid_t        cids[DECENT_MAX_CIDS];
    uint32_t            num_cids;

    /* BitTorrent */
    decent_bt_swarm_t   bt_swarms[DECENT_MAX_BT_SWARMS];
    uint32_t            num_bt_swarms;

    /* Matrix */
    decent_matrix_room_t matrix_rooms[DECENT_MAX_MATRIX_ROOMS];
    uint32_t             num_matrix_rooms;

    /* DID */
    decent_did_t        dids[DECENT_MAX_DIDS];
    uint32_t            num_dids;

    /* SDR */
    decent_sdr_channel_t sdr_channels[DECENT_MAX_SDR_CHANNELS];
    uint32_t             num_sdr_channels;
    bool                 sdr_fallback_active;

    bool                 initialized;
} decent_t;

/* ===== API ===== */

void decent_init(decent_t *d);

/* IPFS */
int32_t decent_ipfs_node_add(decent_t *d, const char *peer_id, const char *addr);
int decent_ipfs_node_connect(decent_t *d, uint32_t idx);
int32_t decent_ipfs_cid_register(decent_t *d, const uint8_t *hash, uint32_t codec,
                                  uint32_t size, decent_cid_version_t ver);
const decent_cid_t *decent_ipfs_cid_find(decent_t *d, const uint8_t *hash);
int decent_ipfs_bitswap(decent_t *d, uint32_t node_idx, uint32_t cid_idx);

/* BitTorrent v2 */
int32_t decent_bt_swarm_create(decent_t *d, const char *label, uint32_t piece_length);
int decent_bt_swarm_set_merkle(decent_t *d, uint32_t idx, const uint8_t *root);
int decent_bt_swarm_add_peer(decent_t *d, uint32_t swarm_idx, uint32_t peer_idx);
int decent_bt_swarm_start_seeding(decent_t *d, uint32_t idx);

/* Matrix */
int32_t decent_matrix_room_create(decent_t *d, const char *room_id, const char *name,
                                   bool encrypted);
int decent_matrix_room_add_member(decent_t *d, uint32_t room_idx, uint32_t member_id);
int decent_matrix_room_send_event(decent_t *d, uint32_t room_idx,
                                   decent_matrix_event_type_t type,
                                   const char *sender, const char *content);

/* DID */
int32_t decent_did_create(decent_t *d, const char *label, uint8_t key_index);
int decent_did_add_claim(decent_t *d, uint32_t idx);
/* Returned when a capability is DECLARED but not implemented. Distinct from
 * -1 (bad argument) so a caller can tell "you asked wrongly" from "this system
 * cannot do that yet". Never conflate either with success. */
#define DECENT_ENOTIMPL (-2)

/* DEPRECATED AND ALWAYS FAILS. Accepts no proof, so it cannot verify anything;
 * it leaves zk_verified false and returns DECENT_ENOTIMPL. Kept only so
 * existing links do not break. Use decent_did_zk_verify_proof(). */
int decent_did_zk_verify(decent_t *d, uint32_t idx);

/* The honest interface: proof + verifying key + public inputs. Currently
 * unimplemented and returns DECENT_ENOTIMPL, leaving zk_verified false. A zero
 * return MUST mean a proof actually verified. */
int decent_did_zk_verify_proof(decent_t *d, uint32_t idx,
                               const uint8_t *proof, uint32_t proof_len,
                               const uint8_t *vk, uint32_t vk_len,
                               const uint8_t *public_inputs, uint32_t pi_len);

/* SDR */
int32_t decent_sdr_channel_create(decent_t *d, const char *name, uint32_t freq_hz,
                                   uint32_t bw_hz, uint8_t modulation);
int decent_sdr_channel_activate(decent_t *d, uint32_t idx);
int decent_sdr_send_bulb(decent_t *d, uint32_t channel_idx, const uint8_t *data, uint32_t len);
int decent_sdr_enable_fallback(decent_t *d);

/* Utility */
const char *decent_proto_name(decent_proto_t proto);
const char *decent_cid_codec_name(uint32_t codec);
const char *decent_matrix_event_name(decent_matrix_event_type_t type);
const char *decent_sdr_modulation_name(uint8_t mod);

#endif /* ZEDEC_DECENT_H */
