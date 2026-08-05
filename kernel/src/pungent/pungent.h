/*
 * pungent.h — PungentClove Security Framework
 *
 * Native garlic-routing and privacy engine combining I2P garlic
 * routing and Tor onion routing into the OS network stack.
 *
 * 8-Tier Network Integration:
 *   Tier 3: Tor onion routing (.zedec/GLUT+)
 *   Tier 4: Garlic overlay / I2P (.zedec/GLUT+)
 *   Tier 7: Encrypted radio mesh (.zedec/GLUT+)
 *   Tier 8: ZK shadow substrate (.9n63/FALSE)
 *
 * Key Mechanics:
 *   1. Garlic Bundling (Bulbs and Cloves)
 *   2. Multi-Layer Symmetric Encryption per hop
 *   3. Dynamic Hop & Delay Mixing
 *   4. Tor & I2P Bridge Subsystem (SOCKS5)
 *   5. Hidden Services with cryptographic addresses
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */

#ifndef ZEDEC_PUNGENT_H
#define ZEDEC_PUNGENT_H

#include <stdint.h>
#include <stdbool.h>

/* ===== Constants ===== */

#define PC_MAX_TUNNELS        16
#define PC_MAX_RELAYS         64
#define PC_MAX_CLOVES          8   /* Cloves per bulb */
#define PC_MAX_HOPS            8   /* Max relay hops */
#define PC_MAX_BULB_SIZE    4096
#define PC_HASH_SIZE          32
#define PC_MAX_ADDR           64
#define PC_MAX_LABEL          64
#define PC_SESSION_KEY_SIZE    32
#define PC_MAX_HIDDEN_SVC     16
#define PC_SOCKS5_MAX_CONNS   32

/* ===== Network Tiers ===== */

typedef enum {
    PC_TIER_SURFACE  = 1,
    PC_TIER_DEEP     = 2,
    PC_TIER_ONION    = 3,   /* Tor */
    PC_TIER_GARLIC   = 4,   /* I2P / PungentClove native */
    PC_TIER_DARK     = 5,
    PC_TIER_P2P      = 6,
    PC_TIER_RADIO    = 7,
    PC_TIER_SHADOW   = 8,   /* ZK cold vaults */
} pc_tier_t;

/* ===== Clove (individual message in a bulb) ===== */

typedef struct {
    uint8_t  payload[PC_MAX_BULB_SIZE / PC_MAX_CLOVES];
    uint32_t payload_len;
    uint8_t  dest_hash[PC_HASH_SIZE];   /* Hash of destination public key */
    uint8_t  session_key[PC_SESSION_KEY_SIZE]; /* Per-clove encryption key */
    uint32_t tier;                        /* Destination tier */
    bool     encrypted;
} pc_clove_t;

/* ===== Bulb (encrypted container holding multiple cloves) ===== */

typedef struct {
    pc_clove_t cloves[PC_MAX_CLOVES];
    uint32_t   num_cloves;
    uint8_t    bulb_key[PC_SESSION_KEY_SIZE];  /* Outer encryption key */
    uint32_t   hop_count;                       /* Number of relay hops */
    uint8_t    hop_keys[PC_MAX_HOPS][PC_SESSION_KEY_SIZE]; /* Per-hop keys */
    uint32_t   hop_addrs[PC_MAX_HOPS];           /* Relay indices */
    uint32_t   delay_ms;                         /* Mixing delay */
    bool       sealed;                           /* Bulb is sealed (encrypted) */
} pc_bulb_t;

/* ===== Relay Node ===== */

typedef struct {
    char     addr[PC_MAX_ADDR];       /* M5 or onion address */
    uint8_t  pubkey[PC_HASH_SIZE];     /* Relay public key hash */
    uint32_t tier;
    bool     online;
    uint32_t bulbs_relayed;
    uint64_t last_seen;
    uint32_t latency_ms;
} pc_relay_t;

/* ===== Tunnel ===== */

typedef enum {
    PC_TUNNEL_INBOUND  = 0,
    PC_TUNNEL_OUTBOUND = 1,
} pc_tunnel_dir_t;

typedef struct {
    uint32_t id;
    pc_tunnel_dir_t direction;
    uint32_t relay_indices[PC_MAX_HOPS];
    uint32_t hop_count;
    uint8_t  session_keys[PC_MAX_HOPS][PC_SESSION_KEY_SIZE];
    uint32_t tier;
    bool     active;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint32_t bulbs_passed;
} pc_tunnel_t;

/* ===== Hidden Service ===== */

typedef struct {
    uint8_t  service_key[PC_HASH_SIZE];  /* Public key hash = address */
    char     label[PC_MAX_LABEL];
    uint32_t local_port;
    uint32_t tunnel_idx;                  /* Inbound tunnel serving this */
    bool     active;
    uint32_t connections;
} pc_hidden_service_t;

/* ===== SOCKS5 Connection ===== */

typedef struct {
    uint32_t id;
    uint32_t tunnel_idx;
    char     dest_addr[PC_MAX_ADDR];
    uint16_t dest_port;
    bool     active;
    uint64_t bytes_sent;
    uint64_t bytes_received;
} pc_socks5_conn_t;

/* ===== PungentClove State ===== */

typedef struct {
    pc_relay_t         relays[PC_MAX_RELAYS];
    uint32_t           num_relays;
    pc_tunnel_t        tunnels[PC_MAX_TUNNELS];
    uint32_t           num_tunnels;
    pc_hidden_service_t services[PC_MAX_HIDDEN_SVC];
    uint32_t           num_services;
    pc_socks5_conn_t   socks5[PC_SOCKS5_MAX_CONNS];
    uint32_t           num_socks5;
    bool               initialized;
    uint32_t           default_hops;
    uint32_t           default_delay_ms;
    uint64_t           total_bulbs_sealed;
    uint64_t           total_bulbs_unsealed;
    uint64_t           total_bytes_routed;
} pungent_t;

/* ===== API ===== */

void pungent_init(pungent_t *pc);

/* Relay management */
int32_t pungent_relay_add(pungent_t *pc, const char *addr, uint32_t tier);
int pungent_relay_set_online(pungent_t *pc, uint32_t idx, bool online);
uint32_t pungent_relays_by_tier(pungent_t *pc, uint32_t tier);

/* Bulb operations (garlic bundling) */
int pungent_bulb_init(pc_bulb_t *b, uint32_t hops, uint32_t delay_ms);
int pungent_bulb_add_clove(pc_bulb_t *b, const uint8_t *payload, uint32_t len,
                            const uint8_t *dest_hash, uint32_t tier);
int pungent_bulb_seal(pc_bulb_t *b);
int pungent_bulb_unseal(pc_bulb_t *b, uint32_t hop_idx);
uint32_t pungent_bulb_size(pc_bulb_t *b);

/* Tunnel management */
int32_t pungent_tunnel_create(pungent_t *pc, pc_tunnel_dir_t dir,
                               uint32_t tier, uint32_t hop_count);
int pungent_tunnel_add_hop(pungent_t *pc, uint32_t tunnel_idx,
                            uint32_t relay_idx);
int pungent_tunnel_activate(pungent_t *pc, uint32_t tunnel_idx);
int pungent_tunnel_close(pungent_t *pc, uint32_t tunnel_idx);
int pungent_tunnel_send(pungent_t *pc, uint32_t tunnel_idx,
                         const uint8_t *data, uint32_t len);

/* Hidden services */
int32_t pungent_hidden_service_create(pungent_t *pc, const char *label,
                                       uint32_t local_port, uint32_t tunnel_idx);
const pc_hidden_service_t *pungent_hidden_service_find(pungent_t *pc,
                                                        const uint8_t *key);

/* SOCKS5 proxy */
int32_t pungent_socks5_connect(pungent_t *pc, const char *dest,
                                uint16_t port, uint32_t tunnel_idx);
int pungent_socks5_close(pungent_t *pc, uint32_t conn_idx);

/* Path selection (nonlinear, dynamic) */
int pungent_select_path(pungent_t *pc, uint32_t tier, uint32_t hop_count,
                         uint32_t *out_relays);

/* Utility */
const char *pungent_tier_name(pc_tier_t tier);
const char *pungent_tunnel_dir_name(pc_tunnel_dir_t dir);

#endif /* ZEDEC_PUNGENT_H */
