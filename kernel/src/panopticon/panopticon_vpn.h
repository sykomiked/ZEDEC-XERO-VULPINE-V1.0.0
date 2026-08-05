/*
 * panopticon_vpn.h — Multi-layered VPN mesh matrix for Panopticon
 *
 * 5-hop VPN chains with 5 options per level = 25-grid protection.
 * Auto-rotation and routing for maximum security. Fully configurable.
 * Defaults to free VPN services when user doesn't specify.
 *
 * Architecture:
 *   Level 0: Entry node (5 options)
 *   Level 1: Relay node (5 options)
 *   Level 2: Mid relay (5 options)
 *   Level 3: Exit relay (5 options)
 *   Level 4: Exit node (5 options)
 *
 * Total paths: 5^5 = 3,125 possible routes through the matrix.
 * Auto-rotation cycles through paths on configurable interval.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3 — Streisand Engine License
 * 36N9 Genetics, LLC — Irrevocable, Interdimensional
 */

#ifndef PANOPTICON_VPN_H
#define PANOPTICON_VPN_H

#include "m5_types.h"

#define VPN_MAX_HOPS        5
#define VPN_OPTIONS_PER_HOP 5
#define VPN_MAX_NODES       (VPN_MAX_HOPS * VPN_OPTIONS_PER_HOP)
#define VPN_MAX_NAME        64
#define VPN_MAX_HOST        128
#define VPN_MAX_PATHS       3125  /* 5^5 */
#define VPN_ROTATE_DEFAULT  300   /* 5 minutes in seconds */
#define VPN_MAX_PROTOCOLS   8

/* VPN protocols */
typedef enum {
    VPN_PROTO_WIREGUARD  = 0,
    VPN_PROTO_OPENVPN    = 1,
    VPN_PROTO_IPSEC      = 2,
    VPN_PROTO_SSH_TUNNEL = 3,
    VPN_PROTO_TOR        = 4,
    VPN_PROTO_I2P        = 5,
    VPN_PROTO_GARLIC     = 6,  /* PungentClove native */
    VPN_PROTO_PLNP       = 7,  /* pqOS native encrypted transport */
} vpn_protocol_t;

/* VPN node entry */
typedef struct {
    uint8_t  name[VPN_MAX_NAME];
    uint8_t  host[VPN_MAX_HOST];
    uint16_t port;
    vpn_protocol_t protocol;
    uint8_t  pubkey[32];
    uint8_t  is_free;          /* 1 = free/public VPN */
    uint8_t  is_active;
    uint8_t  country_code[3];
    int8_t   trust_score;      /* -100 to +100 */
    uint32_t latency_ms;
    uint32_t bandwidth_kbps;
    uint8_t  keeps_logs;       /* 0 = no logs, 1 = logs */
} vpn_node_t;

/* VPN hop level (one of 5 levels, each with 5 options) */
typedef struct {
    vpn_node_t options[VPN_OPTIONS_PER_HOP];
    uint8_t    selected;  /* which option is currently active (0-4) */
} vpn_hop_t;

/* VPN mesh matrix — the full 5x5 grid */
typedef struct {
    vpn_hop_t hops[VPN_MAX_HOPS];     /* 5 levels, 5 options each */
    uint8_t   active_path[VPN_MAX_HOPS]; /* current selected option per hop */
    uint32_t  rotation_interval;      /* seconds between auto-rotation */
    uint64_t  last_rotation;          /* phase tick of last rotation */
    uint8_t   auto_rotate;            /* 1 = enabled */
    uint8_t   auto_route;             /* 1 = auto-select best path */
    uint8_t   mesh_enabled;           /* 1 = VPN mesh active */
    uint32_t  total_paths;            /* total possible paths */
    uint32_t  paths_used;             /* unique paths used so far */
    uint64_t  bytes_routed;           /* total bytes through mesh */
    uint64_t  packets_routed;
} vpn_mesh_t;

/* VPN path — a specific route through the matrix */
typedef struct {
    uint8_t  selections[VPN_MAX_HOPS]; /* option index per hop */
    uint32_t path_id;                  /* unique path identifier */
    uint8_t  in_use;
    uint64_t activated_tick;
    uint32_t latency_total_ms;
} vpn_path_t;

/* VPN connection state */
typedef struct {
    uint8_t   established;
    uint8_t   current_path[VPN_MAX_HOPS];
    uint32_t  current_path_id;
    uint64_t  established_tick;
    uint64_t  bytes_sent;
    uint64_t  bytes_received;
    uint8_t   encryption_active;
    uint8_t   kill_switch_active;  /* blocks all traffic if VPN drops */
} vpn_conn_t;

/* Full VPN mesh state */
typedef struct {
    vpn_mesh_t  mesh;
    vpn_conn_t  connection;
    vpn_path_t  path_history[64];  /* recent paths used */
    uint32_t    path_history_count;
    uint32_t    path_history_head;
    uint8_t     default_free_vpns; /* 1 = use free VPN defaults */
} vpn_state_t;

/* ============================================================
 * Core API
 * ============================================================ */

int vpn_mesh_init(vpn_state_t *state);
int vpn_mesh_shutdown(vpn_state_t *state);
int vpn_mesh_tick(vpn_state_t *state);

/* Node management */
int vpn_add_node(vpn_mesh_t *mesh, int hop_level, int option_index,
                 const char *name, const char *host, uint16_t port,
                 vpn_protocol_t protocol, uint8_t is_free);
int vpn_remove_node(vpn_mesh_t *mesh, int hop_level, int option_index);
int vpn_set_node_trust(vpn_mesh_t *mesh, int hop_level, int option_index,
                       int8_t trust);

/* Load default free VPN services */
int vpn_load_defaults(vpn_state_t *state);

/* Path selection */
int vpn_select_path(vpn_state_t *state, const uint8_t selections[VPN_MAX_HOPS]);
int vpn_auto_select_path(vpn_state_t *state);
int vpn_random_path(vpn_state_t *state);
int vpn_get_current_path(vpn_state_t *state,
                         uint8_t selections[VPN_MAX_HOPS]);

/* Auto-rotation */
int vpn_set_rotation_interval(vpn_state_t *state, uint32_t seconds);
int vpn_rotate_now(vpn_state_t *state);
int vpn_enable_auto_rotate(vpn_state_t *state, uint8_t enable);
int vpn_enable_auto_route(vpn_state_t *state, uint8_t enable);

/* Connection management */
int vpn_connect(vpn_state_t *state);
int vpn_disconnect(vpn_state_t *state);
int vpn_kill_switch(vpn_state_t *state, uint8_t enable);

/* Routing */
int vpn_route_packet(vpn_state_t *state, const uint8_t *data,
                     uint16_t len, uint8_t *out, uint16_t *out_len);
int vpn_receive_packet(vpn_state_t *state, uint8_t *data,
                       uint16_t *len);

/* Status & reporting */
int vpn_mesh_status(vpn_state_t *state, char *buf, uint16_t buf_len);
int vpn_path_report(vpn_state_t *state, char *buf, uint16_t buf_len);
int vpn_node_report(vpn_mesh_t *mesh, int hop, int option,
                    char *buf, uint16_t buf_len);

/* Path enumeration — calculate all possible paths */
int vpn_enumerate_paths(vpn_mesh_t *mesh, uint32_t *path_count);

/* Latency calculation for a given path */
uint32_t vpn_path_latency(vpn_mesh_t *mesh, const uint8_t selections[VPN_MAX_HOPS]);

/* Trust calculation for a given path */
int8_t vpn_path_trust(vpn_mesh_t *mesh, const uint8_t selections[VPN_MAX_HOPS]);

/* Protocol utilities */
const char *vpn_protocol_name(vpn_protocol_t proto);
vpn_protocol_t vpn_protocol_from_name(const char *name);

/* Mesh visualization — ASCII grid representation */
int vpn_mesh_visualize(vpn_state_t *state, char *buf, uint16_t buf_len);

#endif /* PANOPTICON_VPN_H */
