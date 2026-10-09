/*
 * pungent.h — PungentClove: layered-encryption ("garlic") bundling model
 *
 * WHAT THIS IS
 *   A freestanding model of garlic-style bundling: several messages
 *   ("cloves") are each sealed for their destination and packed into one
 *   "bulb", and the bulb is then wrapped in one authenticated encryption
 *   layer per relay hop. Each relay peels exactly one layer with its own
 *   key. Tunnels apply the same per-hop layering to a byte stream.
 *
 * WHAT IS REAL
 *   - Every clove is sealed with ChaCha20-Poly1305 (RFC 8439, tls/aead.c)
 *     under a key the CALLER supplies. A clove key can be established with
 *     ML-KEM-768 (FIPS 203, mlkem/mlkem768.c) via pungent_clove_key_encaps()
 *     and pungent_clove_key_decaps(), which feed the KEM shared secret
 *     through HKDF-SHA256 (tls/hkdf.c). `encrypted` is set only after an
 *     AEAD seal has actually run.
 *   - Every hop layer is ChaCha20-Poly1305 under a caller-supplied hop key.
 *     Peeling a layer with the wrong key, out of order, or after tampering
 *     fails and the bulb is discarded (fail closed).
 *   - Hidden-service addresses are SHA3-256 of the service's public key.
 *
 * WHAT IS NOT HERE (do not read more into the names)
 *   - This is NOT I2P and NOT Tor, and it does not interoperate with either.
 *     There is no SOCKS5 protocol implementation: pungent_socks5_* only keep
 *     a table of requested connections.
 *   - No transport. Nothing is sent or received; tunnel_send only produces
 *     the layered ciphertext for a host transport to carry.
 *   - No key agreement with relays and no relay discovery. Hop keys and
 *     relay public-key hashes are inputs.
 *   - No entropy source. Nonces and KEM coins are inputs. A nonce must never
 *     repeat under the same key; reusing one breaks ChaCha20-Poly1305.
 *   - Path selection is deterministic and offers no anonymity guarantee.
 *   - Tier names (onion, garlic, ...) are labels only.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

#ifndef ZEDEC_PUNGENT_H
#define ZEDEC_PUNGENT_H

#include <stdint.h>
#include <stdbool.h>
#include "../mlkem/mlkem768.h"

/* ===== Constants ===== */

#define PC_MAX_TUNNELS      16
#define PC_MAX_RELAYS       64
#define PC_MAX_CLOVES       8 /* Cloves per bulb */
#define PC_MAX_HOPS         8 /* Max relay hops */
#define PC_MAX_BULB_SIZE    4096
#define PC_HASH_SIZE        32
#define PC_MAX_ADDR         64
#define PC_MAX_LABEL        64
#define PC_SESSION_KEY_SIZE 32
#define PC_NONCE_SIZE       12
#define PC_TAG_SIZE         16
#define PC_MAX_HIDDEN_SVC   16
#define PC_SOCKS5_MAX_CONNS 32

#define PC_CLOVE_MAX_PAYLOAD (PC_MAX_BULB_SIZE / PC_MAX_CLOVES)
/* Serialized clove: dest_hash, tier, len, nonce, tag, ciphertext. */
#define PC_CLOVE_WIRE_HDR (PC_HASH_SIZE + 4 + 4 + PC_NONCE_SIZE + PC_TAG_SIZE)
#define PC_BULB_WIRE_MAX                                                                           \
    (4 + PC_MAX_CLOVES * (PC_CLOVE_WIRE_HDR + PC_CLOVE_MAX_PAYLOAD) + PC_MAX_HOPS * PC_TAG_SIZE)

/* ===== Network Tiers (labels only) ===== */

typedef enum {
    PC_TIER_SURFACE = 1,
    PC_TIER_DEEP = 2,
    PC_TIER_ONION = 3,  /* label: onion-style layering */
    PC_TIER_GARLIC = 4, /* label: garlic-style bundling */
    PC_TIER_DARK = 5,
    PC_TIER_P2P = 6,
    PC_TIER_RADIO = 7,
    PC_TIER_SHADOW = 8,
} pc_tier_t;

/* ===== Clove (one sealed message in a bulb) ===== */

typedef struct {
    uint8_t payload[PC_CLOVE_MAX_PAYLOAD]; /* ChaCha20-Poly1305 ciphertext */
    uint32_t payload_len;
    uint8_t dest_hash[PC_HASH_SIZE]; /* Hash of destination public key (AAD) */
    uint8_t nonce[PC_NONCE_SIZE];
    uint8_t tag[PC_TAG_SIZE];
    uint32_t tier;  /* Destination tier (AAD) */
    bool encrypted; /* true only after aead_seal ran */
} pc_clove_t;

/* ===== Bulb (cloves + one AEAD layer per hop) ===== */

typedef struct {
    pc_clove_t cloves[PC_MAX_CLOVES];
    uint32_t num_cloves;
    uint32_t hop_count;
    uint32_t hop_addrs[PC_MAX_HOPS]; /* Relay indices */
    uint32_t delay_ms;               /* Mixing delay (advisory) */
    uint8_t nonce_base[PC_NONCE_SIZE];
    uint8_t wire[PC_BULB_WIRE_MAX]; /* Serialized, layered bulb while sealed */
    uint32_t wire_len;
    uint32_t layers; /* Hop layers still on the wire */
    bool sealed;
} pc_bulb_t;

/* ===== Relay Node ===== */

typedef struct {
    char addr[PC_MAX_ADDR];
    uint8_t pubkey[PC_HASH_SIZE]; /* Hash of the relay's public key (input) */
    uint32_t tier;
    bool online;
    uint32_t bulbs_relayed;
    uint64_t last_seen;
    uint32_t latency_ms;
} pc_relay_t;

/* ===== Tunnel ===== */

typedef enum {
    PC_TUNNEL_INBOUND = 0,
    PC_TUNNEL_OUTBOUND = 1,
} pc_tunnel_dir_t;

typedef struct {
    uint32_t id;
    pc_tunnel_dir_t direction;
    uint32_t relay_indices[PC_MAX_HOPS];
    uint32_t hop_count; /* hops required before activation */
    uint32_t num_hops;  /* hops added so far */
    uint8_t session_keys[PC_MAX_HOPS][PC_SESSION_KEY_SIZE];
    uint32_t tier;
    bool active;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint32_t bulbs_passed;
} pc_tunnel_t;

/* ===== Hidden Service ===== */

typedef struct {
    uint8_t service_key[PC_HASH_SIZE]; /* SHA3-256(service public key) = address */
    char label[PC_MAX_LABEL];
    uint32_t local_port;
    uint32_t tunnel_idx; /* Inbound tunnel serving this */
    bool active;
    uint32_t connections;
} pc_hidden_service_t;

/* ===== SOCKS5-style connection record (no SOCKS5 protocol) ===== */

typedef struct {
    uint32_t id;
    uint32_t tunnel_idx;
    char dest_addr[PC_MAX_ADDR];
    uint16_t dest_port;
    bool active;
    uint64_t bytes_sent;
    uint64_t bytes_received;
} pc_socks5_conn_t;

/* ===== PungentClove State ===== */

typedef struct {
    pc_relay_t relays[PC_MAX_RELAYS];
    uint32_t num_relays;
    pc_tunnel_t tunnels[PC_MAX_TUNNELS];
    uint32_t num_tunnels;
    pc_hidden_service_t services[PC_MAX_HIDDEN_SVC];
    uint32_t num_services;
    pc_socks5_conn_t socks5[PC_SOCKS5_MAX_CONNS];
    uint32_t num_socks5;
    bool initialized;
    uint32_t default_hops;
    uint32_t default_delay_ms;
    uint64_t total_bulbs_sealed;
    uint64_t total_bulbs_unsealed;
    uint64_t total_bytes_routed;
} pungent_t;

/* ===== API ===== */

void pungent_init(pungent_t *pc);

/* Relay management. pubkey_hash is required (the hash of the relay's real
 * public key); it is never derived from the address. */
int32_t pungent_relay_add(pungent_t *pc, const char *addr, uint32_t tier,
                          const uint8_t pubkey_hash[PC_HASH_SIZE]);
int pungent_relay_set_online(pungent_t *pc, uint32_t idx, bool online);
uint32_t pungent_relays_by_tier(pungent_t *pc, uint32_t tier);

/* Clove keys from ML-KEM-768. The sender encapsulates to the destination's
 * encapsulation key with 32 bytes of fresh randomness and ships `ct`; the
 * destination decapsulates. Both get the same 32-byte clove key, bound to
 * dest_hash through HKDF-SHA256. */
void pungent_clove_key_encaps(const uint8_t ek[MLKEM768_EK_BYTES], const uint8_t coins[32],
                              const uint8_t dest_hash[PC_HASH_SIZE], uint8_t ct[MLKEM768_CT_BYTES],
                              uint8_t key[PC_SESSION_KEY_SIZE]);
void pungent_clove_key_decaps(const uint8_t dk[MLKEM768_DK_BYTES],
                              const uint8_t ct[MLKEM768_CT_BYTES],
                              const uint8_t dest_hash[PC_HASH_SIZE],
                              uint8_t key[PC_SESSION_KEY_SIZE]);

/* Bulb operations. add_clove seals the payload under `key` with `nonce`
 * (dest_hash and tier are authenticated as associated data). Returns the clove
 * index or -1. */
int pungent_bulb_init(pc_bulb_t *b, uint32_t hops, uint32_t delay_ms);
int pungent_bulb_add_clove(pc_bulb_t *b, const uint8_t *payload, uint32_t len,
                           const uint8_t *dest_hash, uint32_t tier,
                           const uint8_t key[PC_SESSION_KEY_SIZE],
                           const uint8_t nonce[PC_NONCE_SIZE]);
/* Decrypts and authenticates one clove. Returns plaintext length, or -1 on a
 * wrong key / tampering (out is then zeroed). */
int pungent_clove_open(const pc_clove_t *c, const uint8_t key[PC_SESSION_KEY_SIZE], uint8_t *out,
                       uint32_t cap);
/* Serialize the cloves and wrap one AEAD layer per hop. hop_keys[0] is the
 * first relay's key (outermost layer). nonce_base must be unique per bulb. */
int pungent_bulb_seal(pc_bulb_t *b, const uint8_t hop_keys[][PC_SESSION_KEY_SIZE],
                      const uint8_t nonce_base[PC_NONCE_SIZE]);
/* Peel the layer for hop `hop_idx` (must be the next one, in order). After
 * the last layer the cloves are restored. Any failure discards the bulb and
 * returns -1. */
int pungent_bulb_unseal(pc_bulb_t *b, uint32_t hop_idx, const uint8_t key[PC_SESSION_KEY_SIZE]);
uint32_t pungent_bulb_size(pc_bulb_t *b);

/* Generic layering, shared by bulbs and tunnels. Seal wraps n layers in
 * place (buf must have room for len + n * PC_TAG_SIZE) and returns the new
 * length, or -1. Peel removes the layer for `hop` and returns the new length,
 * or -1 (buf is then zeroed). */
int32_t pungent_layers_seal(const uint8_t keys[][PC_SESSION_KEY_SIZE], uint32_t n,
                            const uint8_t nonce_base[PC_NONCE_SIZE], uint8_t *buf, uint32_t len,
                            uint32_t cap);
int32_t pungent_layer_peel(const uint8_t key[PC_SESSION_KEY_SIZE],
                           const uint8_t nonce_base[PC_NONCE_SIZE], uint32_t hop, uint8_t *buf,
                           uint32_t len);

/* Tunnel management. add_hop requires the hop's session key. A tunnel can be
 * activated only once all hop_count hops (each with a key) are present. */
int32_t pungent_tunnel_create(pungent_t *pc, pc_tunnel_dir_t dir, uint32_t tier,
                              uint32_t hop_count);
int pungent_tunnel_add_hop(pungent_t *pc, uint32_t tunnel_idx, uint32_t relay_idx,
                           const uint8_t key[PC_SESSION_KEY_SIZE]);
int pungent_tunnel_activate(pungent_t *pc, uint32_t tunnel_idx);
int pungent_tunnel_close(pungent_t *pc, uint32_t tunnel_idx);
/* Produces the layered ciphertext of `data` in `out` (no transport). Returns
 * the output length, or -1. nonce_base must be unique per message. */
int pungent_tunnel_send(pungent_t *pc, uint32_t tunnel_idx, const uint8_t *data, uint32_t len,
                        const uint8_t nonce_base[PC_NONCE_SIZE], uint8_t *out, uint32_t cap);

/* Hidden services: the address is SHA3-256 of the service public key. */
int32_t pungent_hidden_service_create(pungent_t *pc, const char *label, uint32_t local_port,
                                      uint32_t tunnel_idx, const uint8_t *pubkey,
                                      uint32_t pubkey_len);
const pc_hidden_service_t *pungent_hidden_service_find(pungent_t *pc, const uint8_t *key);

/* Connection table only; no SOCKS5 wire protocol. */
int32_t pungent_socks5_connect(pungent_t *pc, const char *dest, uint16_t port, uint32_t tunnel_idx);
int pungent_socks5_close(pungent_t *pc, uint32_t conn_idx);

/* Path selection: deterministic, distinct online relays, tier first. */
int pungent_select_path(pungent_t *pc, uint32_t tier, uint32_t hop_count, uint32_t *out_relays);

/* Utility */
const char *pungent_tier_name(pc_tier_t tier);
const char *pungent_tunnel_dir_name(pc_tunnel_dir_t dir);

#endif /* ZEDEC_PUNGENT_H */
