/* mesh_net.h — P2P Mesh Network Layer
 *
 * Native ZXV peer-to-peer mesh networking subsystem. Enables users to
 * create their own mesh networks, establish trade routes, and federate
 * with other meshes. Key design principles (post-quantum, native ZXV):
 *
 *   - All peer identities are 168-bit critical words
 *   - Mesh networks are user-created and self-organizing
 *   - Trade routes are P2P paths for commerce and data exchange
 *   - Federation allows separate meshes to interconnect
 *   - Porter House gates which peers can join a mesh
 *   - JDR PirateNet provides the physical transport layer
 *   - Count House provides economic valuation for route pricing
 *   - Mesh-Token handles settlement along trade routes
 *   - All mesh traffic is post-quantum signed and encrypted
 *
 * This is NOT a traditional TCP/IP stack. It is a post-quantum P2P
 * mesh networking layer native to the M5 Axiomatic architecture.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef MESH_NET_H
#define MESH_NET_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_types.h"
#include "surplus.h"
#include "edp_risk.h"
#include "porter_house.h"

/* ===== Constants ===== */

#define MN_MAX_NETWORKS        16
#define MN_MAX_PEERS_PER_NET   32
#define MN_MAX_ROUTES          64
#define MN_MAX_NAME_LEN        48
#define MN_MAX_HOPS            8
#define MN_MESH_PORT           8800  /* Porter House port for mesh traffic */
#define MN_ROUTE_TIMEOUT       10000 /* cycles before route expires */

/* ===== Network Types ===== */

typedef enum {
    MN_NET_UNUSED      = 0,
    MN_NET_ACTIVE      = 1,   /* network is active and accepting peers */
    MN_NET_CLOSED      = 2,   /* network closed to new peers */
    MN_NET_FEDERATED   = 3    /* federated with another mesh */
} mn_net_state_t;

typedef enum {
    MN_NET_OPEN        = 0,   /* anyone can join */
    MN_NET_TRUSTED     = 1,   /* requires Porter House trust threshold */
    MN_NET_ALLOWLIST   = 2,   /* requires explicit invitation */
    MN_NET_PRIVATE     = 3    /* encrypted, invite-only, post-quantum */
} mn_net_access_t;

/* ===== Route Types ===== */

typedef enum {
    MN_ROUTE_UNUSED    = 0,
    MN_ROUTE_ACTIVE    = 1,   /* route is active and forwarding */
    MN_ROUTE_DEGRADED  = 2,   /* route has reduced capacity */
    MN_ROUTE_EXPIRED   = 3,   /* route has timed out */
    MN_ROUTE_CLOSED    = 4    /* route manually closed */
} mn_route_state_t;

typedef enum {
    MN_ROUTE_DATA      = 0,   /* data transfer route */
    MN_ROUTE_TRADE     = 1,   /* commerce/trade route */
    MN_ROUTE_COMPUTE   = 2,   /* remote compute offload route */
    MN_ROUTE_VOICE     = 3,   /* voice/comm route */
    MN_ROUTE_EMERGENCY = 4    /* emergency/priority route */
} mn_route_type_t;

/* ===== Peer Entry ===== */

typedef struct mn_peer {
    word168_t peer_id;
    uint32_t trust_weight;
    uint64_t bandwidth_avail;   /* available bandwidth in bytes/sec */
    uint64_t data_transferred;  /* total data transferred through this peer */
    bool is_gateway;            /* acts as gateway to other meshes */
    bool active;
} mn_peer_t;

/* ===== Mesh Network ===== */

typedef struct mn_network {
    uint32_t id;
    char name[MN_MAX_NAME_LEN];
    mn_net_state_t state;
    mn_net_access_t access;

    word168_t creator_id;
    uint32_t creator_trust;

    mn_peer_t peers[MN_MAX_PEERS_PER_NET];
    uint32_t num_peers;

    uint64_t total_data_routed;  /* total data routed through this mesh */
    uint32_t route_count;        /* active routes in this mesh */

    bool active;
} mn_network_t;

/* ===== Trade Route ===== */

typedef struct mn_route {
    uint32_t id;
    uint32_t network_id;
    mn_route_type_t type;
    mn_route_state_t state;

    word168_t source;
    word168_t destination;
    word168_t hops[MN_MAX_HOPS];  /* intermediate peers */
    uint32_t num_hops;

    uint64_t price_per_unit;     /* Vino voucher price per data unit */
    uint64_t capacity;           /* route capacity in bytes/sec */
    uint64_t data_transferred;   /* total data sent along this route */
    uint64_t revenue;            /* total revenue generated */

    uint64_t created_cycle;
    uint64_t last_active_cycle;

    bool active;
} mn_route_t;

/* ===== Mesh Network Engine ===== */

typedef struct mesh_net_engine {
    uint32_t device_id;
    char name[MN_MAX_NAME_LEN];

    mn_network_t networks[MN_MAX_NETWORKS];
    uint32_t num_networks;
    uint32_t next_net_id;

    mn_route_t routes[MN_MAX_ROUTES];
    uint32_t num_routes;
    uint32_t next_route_id;

    porter_house_t *porter;

    /* Stats */
    uint64_t total_data_routed;
    uint64_t total_revenue;
    uint64_t total_routes_created;
    uint64_t total_routes_expired;
    uint32_t total_peers_connected;

    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
} mesh_net_t;

/* ===== API ===== */

void mn_init(mesh_net_t *mn, uint32_t device_id, const char *name,
              porter_house_t *porter);

/* Create a mesh network. Returns network ID on success, -1 if full,
 * -2 if Porter House rejects creator. */
int32_t mn_create_network(mesh_net_t *mn, const char *name,
                           mn_net_access_t access,
                           const word168_t *creator_id,
                           uint32_t creator_trust);

/* Join a mesh network. Returns 0 on success, -1 if network not found,
 * -2 if full, -3 if Porter House rejects, -4 if access denied. */
int32_t mn_join_network(mesh_net_t *mn, uint32_t network_id,
                         const word168_t *peer_id, uint32_t trust_weight,
                         uint64_t bandwidth);

/* Leave a mesh network. */
int32_t mn_leave_network(mesh_net_t *mn, uint32_t network_id,
                          const word168_t *peer_id);

/* Close a network to new peers. */
int32_t mn_close_network(mesh_net_t *mn, uint32_t network_id);

/* Create a trade route. Returns route ID on success, -1 if full,
 * -2 if network not found. */
int32_t mn_create_route(mesh_net_t *mn, uint32_t network_id,
                         mn_route_type_t type,
                         const word168_t *source,
                         const word168_t *destination,
                         uint64_t price_per_unit, uint64_t capacity,
                         uint64_t current_cycle);

/* Add a hop to a route. Returns 0 on success, -1 if route full. */
int32_t mn_add_hop(mesh_net_t *mn, uint32_t route_id,
                    const word168_t *hop);

/* Send data along a route. Returns 0 on success, -1 if route inactive. */
int32_t mn_send_data(mesh_net_t *mn, uint32_t route_id,
                      uint64_t data_size, uint64_t current_cycle);

/* Close a route. */
int32_t mn_close_route(mesh_net_t *mn, uint32_t route_id);

/* Check for expired routes. Returns count of expired routes. */
uint32_t mn_check_expired(mesh_net_t *mn, uint64_t current_cycle);

/* Federate two networks. Returns 0 on success. */
int32_t mn_federate(mesh_net_t *mn, uint32_t net1_id, uint32_t net2_id);

/* Get network by ID. */
mn_network_t *mn_get_network(mesh_net_t *mn, uint32_t network_id);

/* Get route by ID. */
mn_route_t *mn_get_route(mesh_net_t *mn, uint32_t route_id);

/* Update M5 coverage. */
surplus_real_t mn_update_coverage(mesh_net_t *mn);

#endif /* MESH_NET_H */
