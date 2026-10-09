/*
 * decent.h — Decentralized-protocol bookkeeping model
 *
 * WHAT THIS IS
 *   In-memory tables that model concepts borrowed from several protocols:
 *   content IDs and block exchange, a swarm table, encrypted rooms with an
 *   event log, DIDs, and radio channels. It is NOT an implementation of
 *   IPFS, libp2p, Bitswap, BitTorrent, Matrix (Olm/Megolm), W3C DID
 *   resolution or any SDR stack, and it interoperates with none of them.
 *   The canonical content-addressing module is kernel/src/ipfs_node.
 *
 * WHAT IS REAL
 *   - Content IDs registered from content are SHA-256 digests (multihash
 *     0x12), and decent_ipfs_bitswap() marks a CID resolved only after the
 *     supplied block hashes to it.
 *   - Peer and DID key hashes are SHA3-256 of a caller-supplied public key;
 *     the DID string is derived from that hash.
 *   - Encrypted rooms seal each event body with ChaCha20-Poly1305 under a
 *     caller-supplied 32-byte room key; decent_matrix_event_open() checks the
 *     tag. A room cannot be created "encrypted" without a key.
 *
 * WHAT FAILS CLOSED
 *   - ZK proof verification: DECENT_ENOTIMPL, zk_verified stays false.
 *   - Radio transmission: decent_sdr_send_bulb() returns DECENT_ENOTIMPL
 *     (there is no radio driver); no channel claims garlic encryption.
 *   - No networking anywhere: nothing is sent or received.
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

/* Returned when a capability is DECLARED but not implemented. Distinct from
 * -1 (bad argument) so a caller can tell "you asked wrongly" from "this system
 * cannot do that yet". Never conflate either with success. */
#define DECENT_ENOTIMPL (-2)
/* Content did not hash to the CID it was offered for. */
#define DECENT_EMISMATCH (-3)

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
    char peer_id[DECENT_MAX_ADDR];    /* Caller-chosen peer label */
    char addr[DECENT_MAX_ADDR];       /* Address string (not parsed) */
    uint8_t pubkey[DECENT_HASH_SIZE]; /* SHA3-256 of the peer public key */
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
    uint8_t content[256]; /* plaintext, or ciphertext when encrypted */
    uint32_t content_len;
    uint8_t tag[16];                      /* Poly1305 tag when encrypted */
    uint64_t timestamp;                   /* event sequence number (also the AEAD nonce) */
    uint8_t event_hash[DECENT_HASH_SIZE]; /* SHA3-256(sender || content bytes) */
    bool     encrypted;
} decent_matrix_event_t;

typedef struct {
    char     room_id[DECENT_MAX_LABEL];
    char     name[DECENT_MAX_LABEL];
    uint8_t room_key[DECENT_HASH_SIZE]; /* caller-supplied AEAD key (encrypted rooms) */
    decent_matrix_event_t events[DECENT_MAX_MATRIX_EVENTS];
    uint32_t num_events;
    uint32_t members[16];
    uint32_t num_members;
    bool     encrypted;
    bool     active;
} decent_matrix_room_t;

/* ===== DID (Decentralized Identity) ===== */

typedef struct {
    char did[DECENT_MAX_ADDR];        /* did:zede:K<n>:<hex of SHA3-256(pk)[0..15]> */
    uint8_t key_index;                /* K1..K5 label */
    uint8_t pubkey[DECENT_HASH_SIZE]; /* SHA3-256 of the DID public key */
    char     label[DECENT_MAX_LABEL];
    uint32_t verifiable_claims;
    bool zk_verified; /* always false: no ZK verifier exists */
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
    bool garlic_encrypted; /* always false: nothing is encrypted or sent */
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

/* Content table. node_add requires the peer public key. cid_register
 * records a claimed SHA-256 digest (unresolved); cid_register_content hashes
 * the content itself. bitswap marks a CID resolved only when `block` hashes
 * to it, and returns DECENT_EMISMATCH on a mismatch. */
int32_t decent_ipfs_node_add(decent_t *d, const char *peer_id, const char *addr,
                             const uint8_t *pubkey, uint32_t pubkey_len);
int decent_ipfs_node_connect(decent_t *d, uint32_t idx);
int32_t decent_ipfs_cid_register(decent_t *d, const uint8_t *hash, uint32_t codec,
                                  uint32_t size, decent_cid_version_t ver);
int32_t decent_ipfs_cid_register_content(decent_t *d, const uint8_t *data, uint32_t len,
                                         uint32_t codec);
const decent_cid_t *decent_ipfs_cid_find(decent_t *d, const uint8_t *hash);
int decent_ipfs_bitswap(decent_t *d, uint32_t node_idx, uint32_t cid_idx, const uint8_t *block,
                        uint32_t block_len);

/* BitTorrent v2 */
int32_t decent_bt_swarm_create(decent_t *d, const char *label, uint32_t piece_length);
int decent_bt_swarm_set_merkle(decent_t *d, uint32_t idx, const uint8_t *root);
int decent_bt_swarm_add_peer(decent_t *d, uint32_t swarm_idx, uint32_t peer_idx);
int decent_bt_swarm_start_seeding(decent_t *d, uint32_t idx);

/* Rooms. An encrypted room needs a 32-byte room_key (else -1); a plaintext
 * room passes NULL. Each room key must be used by one room only, because the
 * event sequence number is the AEAD nonce. */
int32_t decent_matrix_room_create(decent_t *d, const char *room_id, const char *name,
                                  bool encrypted, const uint8_t *room_key);
int decent_matrix_room_add_member(decent_t *d, uint32_t room_idx, uint32_t member_id);
int decent_matrix_room_send_event(decent_t *d, uint32_t room_idx,
                                   decent_matrix_event_type_t type,
                                   const char *sender, const char *content);
/* Decrypts (or copies) an event body into out (NUL-terminated). Returns the
 * body length, or -1 on a bad tag / wrong key. */
int decent_matrix_event_open(const decent_t *d, uint32_t room_idx, uint32_t event_idx,
                             const uint8_t *room_key, char *out, uint32_t cap);

/* DID: requires the DID public key. */
int32_t decent_did_create(decent_t *d, const char *label, uint8_t key_index, const uint8_t *pubkey,
                          uint32_t pubkey_len);
int decent_did_add_claim(decent_t *d, uint32_t idx);

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
/* No radio driver: always DECENT_ENOTIMPL (or -1 for bad arguments). */
int decent_sdr_send_bulb(decent_t *d, uint32_t channel_idx, const uint8_t *data, uint32_t len);
int decent_sdr_enable_fallback(decent_t *d);

/* Utility */
const char *decent_proto_name(decent_proto_t proto);
const char *decent_cid_codec_name(uint32_t codec);
const char *decent_matrix_event_name(decent_matrix_event_type_t type);
const char *decent_sdr_modulation_name(uint8_t mod);

#endif /* ZEDEC_DECENT_H */
