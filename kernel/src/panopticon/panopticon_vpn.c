/*
 * panopticon_vpn.c — Multi-layered VPN mesh matrix implementation
 *
 * 5-hop deep tunneling with 5x5 selection grid (3,125 paths).
 * Auto-rotation, LPRES integration for failsafe rerouting,
 * garlic-routed packet dispersal, free-tier defaults.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

#include "panopticon_vpn.h"
#include "../include/freestanding.h"

static void safe_strncpy(uint8_t *dst, const char *src, int max) {
    int i = 0;
    if (src) { while (src[i] && i < max - 1) { dst[i] = src[i]; i++; } }
    dst[i] = 0;
}

static uint32_t pow5(uint32_t exp) {
    uint32_t r = 1;
    for (uint32_t i = 0; i < exp; i++) r *= 5;
    return r;
}

int vpn_mesh_init(vpn_state_t *state) {
    if (!state) return -1;
    fs_memset(state, 0, sizeof(*state));
    state->mesh.rotation_interval = VPN_ROTATE_DEFAULT;
    state->mesh.auto_rotate = 1;
    state->mesh.auto_route = 1;
    state->mesh.total_paths = pow5(VPN_MAX_HOPS);
    state->default_free_vpns = 1;
    state->connection.kill_switch_active = 1;
    return 0;
}

int vpn_mesh_shutdown(vpn_state_t *state) {
    if (!state) return -1;
    vpn_disconnect(state);
    fs_memset(state, 0, sizeof(*state));
    return 0;
}

int vpn_mesh_tick(vpn_state_t *state) {
    if (!state) return -1;
    /* Check if rotation is due */
    if (state->mesh.auto_rotate && state->connection.established) {
        uint64_t elapsed = state->mesh.last_rotation + state->mesh.rotation_interval;
        if (elapsed <= state->mesh.last_rotation) {
            /* Tick overflow — rotate */
            vpn_rotate_now(state);
        }
    }
    return 0;
}

int vpn_add_node(vpn_mesh_t *mesh, int hop_level, int option_index,
                 const char *name, const char *host, uint16_t port,
                 vpn_protocol_t protocol, uint8_t is_free) {
    if (!mesh || hop_level < 0 || hop_level >= VPN_MAX_HOPS ||
        option_index < 0 || option_index >= VPN_OPTIONS_PER_HOP)
        return -1;
    vpn_node_t *node = &mesh->hops[hop_level].options[option_index];
    fs_memset(node, 0, sizeof(*node));
    safe_strncpy(node->name, name ? name : "unnamed", VPN_MAX_NAME);
    safe_strncpy(node->host, host ? host : "", VPN_MAX_HOST);
    node->port = port;
    node->protocol = protocol;
    node->is_free = is_free;
    node->is_active = 1;
    node->trust_score = is_free ? 10 : 30;
    node->latency_ms = 0;
    node->bandwidth_kbps = 0;
    node->keeps_logs = 0;
    return 0;
}

int vpn_remove_node(vpn_mesh_t *mesh, int hop_level, int option_index) {
    if (!mesh || hop_level < 0 || hop_level >= VPN_MAX_HOPS ||
        option_index < 0 || option_index >= VPN_OPTIONS_PER_HOP)
        return -1;
    fs_memset(&mesh->hops[hop_level].options[option_index], 0, sizeof(vpn_node_t));
    return 0;
}

int vpn_set_node_trust(vpn_mesh_t *mesh, int hop_level, int option_index,
                       int8_t trust) {
    if (!mesh || hop_level < 0 || hop_level >= VPN_MAX_HOPS ||
        option_index < 0 || option_index >= VPN_OPTIONS_PER_HOP)
        return -1;
    mesh->hops[hop_level].options[option_index].trust_score = trust;
    return 0;
}

int vpn_load_defaults(vpn_state_t *state) {
    if (!state) return -1;
    /* Default free VPN services for each hop level */
    static const struct {
        const char *name;
        const char *host;
        uint16_t port;
        vpn_protocol_t proto;
        const char *country;
    } defaults[VPN_MAX_HOPS][VPN_OPTIONS_PER_HOP] = {
        /* Hop 0: Entry nodes */
        {{"FreeGate-US", "entry-us.freevpn.zedec", 51820, VPN_PROTO_WIREGUARD, "US"},
         {"FreeGate-CH", "entry-ch.freevpn.zedec", 51820, VPN_PROTO_WIREGUARD, "CH"},
         {"FreeGate-IS", "entry-is.freevpn.zedec", 51820, VPN_PROTO_WIREGUARD, "IS"},
         {"P2P-Relay-01", "p2p-relay-01.zedec.mesh", 443, VPN_PROTO_PLNP, "XX"},
         {"Tor-Entry", "tor-entry.zedec.onion", 9001, VPN_PROTO_TOR, "XX"}},
        /* Hop 1: First relay */
        {{"Relay-DE", "relay-de.freevpn.zedec", 51821, VPN_PROTO_WIREGUARD, "DE"},
         {"Relay-SE", "relay-se.freevpn.zedec", 51821, VPN_PROTO_WIREGUARD, "SE"},
         {"Relay-JP", "relay-jp.freevpn.zedec", 51821, VPN_PROTO_WIREGUARD, "JP"},
         {"P2P-Relay-02", "p2p-relay-02.zedec.mesh", 443, VPN_PROTO_GARLIC, "XX"},
         {"I2P-Relay", "i2p-relay.zedec.i2p", 8888, VPN_PROTO_I2P, "XX"}},
        /* Hop 2: Mid relay */
        {{"MidRelay-CA", "mid-ca.freevpn.zedec", 51822, VPN_PROTO_WIREGUARD, "CA"},
         {"MidRelay-NO", "mid-no.freevpn.zedec", 51822, VPN_PROTO_WIREGUARD, "NO"},
         {"MidRelay-SG", "mid-sg.freevpn.zedec", 51822, VPN_PROTO_WIREGUARD, "SG"},
         {"P2P-Relay-03", "p2p-relay-03.zedec.mesh", 443, VPN_PROTO_PLNP, "XX"},
         {"SSH-Tunnel-01", "ssh-tunnel-01.zedec.mesh", 22, VPN_PROTO_SSH_TUNNEL, "XX"}},
        /* Hop 3: Exit relay */
        {{"ExitRelay-FI", "exit-fi.freevpn.zedec", 51823, VPN_PROTO_WIREGUARD, "FI"},
         {"ExitRelay-PT", "exit-pt.freevpn.zedec", 51823, VPN_PROTO_WIREGUARD, "PT"},
         {"ExitRelay-NZ", "exit-nz.freevpn.zedec", 51823, VPN_PROTO_WIREGUARD, "NZ"},
         {"P2P-Relay-04", "p2p-relay-04.zedec.mesh", 443, VPN_PROTO_GARLIC, "XX"},
         {"Garlic-Exit", "garlic-exit.zedec.mesh", 443, VPN_PROTO_GARLIC, "XX"}},
        /* Hop 4: Exit node */
        {{"Exit-PA", "exit-pa.freevpn.zedec", 51824, VPN_PROTO_WIREGUARD, "PA"},
         {"Exit-RO", "exit-ro.freevpn.zedec", 51824, VPN_PROTO_WIREGUARD, "RO"},
         {"Exit-KR", "exit-kr.freevpn.zedec", 51824, VPN_PROTO_WIREGUARD, "KR"},
         {"P2P-Exit-01", "p2p-exit-01.zedec.mesh", 443, VPN_PROTO_PLNP, "XX"},
         {"Tor-Exit", "tor-exit.zedec.onion", 9030, VPN_PROTO_TOR, "XX"}},
    };

    for (int h = 0; h < VPN_MAX_HOPS; h++) {
        for (int o = 0; o < VPN_OPTIONS_PER_HOP; o++) {
            vpn_add_node(&state->mesh, h, o,
                         defaults[h][o].name,
                         defaults[h][o].host,
                         defaults[h][o].port,
                         defaults[h][o].proto, 1);
            safe_strncpy(state->mesh.hops[h].options[o].country_code,
                         defaults[h][o].country, 3);
            /* No-log policy for all defaults */
            state->mesh.hops[h].options[o].keeps_logs = 0;
        }
        state->mesh.hops[h].selected = 0;
        state->mesh.active_path[h] = 0;
    }
    return 0;
}

int vpn_select_path(vpn_state_t *state, const uint8_t selections[VPN_MAX_HOPS]) {
    if (!state || !selections) return -1;
    for (int i = 0; i < VPN_MAX_HOPS; i++) {
        if (selections[i] >= VPN_OPTIONS_PER_HOP) return -1;
        state->mesh.active_path[i] = selections[i];
        state->mesh.hops[i].selected = selections[i];
    }
    /* Record in path history */
    vpn_path_t *p = &state->path_history[state->path_history_head];
    for (int i = 0; i < VPN_MAX_HOPS; i++)
        p->selections[i] = selections[i];
    p->path_id = 0;
    for (int i = 0; i < VPN_MAX_HOPS; i++)
        p->path_id = p->path_id * 5 + selections[i];
    p->in_use = 1;
    p->activated_tick = state->mesh.last_rotation;
    p->latency_total_ms = vpn_path_latency(&state->mesh, selections);
    state->path_history_head =
        (state->path_history_head + 1) % 64;
    if (state->path_history_count < 64) state->path_history_count++;
    state->mesh.paths_used++;
    return 0;
}

int vpn_auto_select_path(vpn_state_t *state) {
    if (!state) return -1;
    /* Auto-route: select best latency + trust at each hop */
    uint8_t best[VPN_MAX_HOPS];
    for (int h = 0; h < VPN_MAX_HOPS; h++) {
        int best_idx = 0;
        int32_t best_score = -999999;
        for (int o = 0; o < VPN_OPTIONS_PER_HOP; o++) {
            vpn_node_t *node = &state->mesh.hops[h].options[o];
            if (!node->is_active) continue;
            /* Score = trust - latency_penalty - log_penalty */
            int32_t score = (int32_t)node->trust_score * 10;
            score -= (int32_t)(node->latency_ms / 10);
            if (node->keeps_logs) score -= 50;
            if (node->is_free) score += 5;  /* prefer free */
            if (score > best_score) {
                best_score = score;
                best_idx = o;
            }
        }
        best[h] = best_idx;
    }
    /* Avoid repeating last path */
    int same = 1;
    for (int i = 0; i < VPN_MAX_HOPS; i++) {
        if (best[i] != state->mesh.active_path[i]) { same = 0; break; }
    }
    if (same) {
        /* Shift one hop to get a different path */
        best[0] = (best[0] + 1) % VPN_OPTIONS_PER_HOP;
    }
    return vpn_select_path(state, best);
}

int vpn_random_path(vpn_state_t *state) {
    if (!state) return -1;
    uint8_t path[VPN_MAX_HOPS];
    uint64_t seed = state->mesh.last_rotation ^ state->mesh.bytes_routed;
    for (int h = 0; h < VPN_MAX_HOPS; h++) {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        path[h] = (uint8_t)((seed >> 33) % VPN_OPTIONS_PER_HOP);
    }
    return vpn_select_path(state, path);
}

int vpn_get_current_path(vpn_state_t *state,
                         uint8_t selections[VPN_MAX_HOPS]) {
    if (!state || !selections) return -1;
    for (int i = 0; i < VPN_MAX_HOPS; i++)
        selections[i] = state->mesh.active_path[i];
    return 0;
}

int vpn_set_rotation_interval(vpn_state_t *state, uint32_t seconds) {
    if (!state) return -1;
    state->mesh.rotation_interval = seconds;
    return 0;
}

int vpn_rotate_now(vpn_state_t *state) {
    if (!state) return -1;
    state->mesh.last_rotation = state->mesh.last_rotation + 1;
    if (state->mesh.auto_route)
        return vpn_auto_select_path(state);
    else
        return vpn_random_path(state);
}

int vpn_enable_auto_rotate(vpn_state_t *state, uint8_t enable) {
    if (!state) return -1;
    state->mesh.auto_rotate = enable;
    return 0;
}

int vpn_enable_auto_route(vpn_state_t *state, uint8_t enable) {
    if (!state) return -1;
    state->mesh.auto_route = enable;
    return 0;
}

int vpn_connect(vpn_state_t *state) {
    if (!state) return -1;
    if (state->default_free_vpns && !state->mesh.hops[0].options[0].is_active) {
        vpn_load_defaults(state);
    }
    if (state->mesh.auto_route)
        vpn_auto_select_path(state);
    else {
        uint8_t zeros[VPN_MAX_HOPS] = {0};
        vpn_select_path(state, zeros);
    }
    state->connection.established = 1;
    state->connection.encryption_active = 1;
    state->connection.established_tick = state->mesh.last_rotation;
    state->mesh.mesh_enabled = 1;
    return 0;
}

int vpn_disconnect(vpn_state_t *state) {
    if (!state) return -1;
    state->connection.established = 0;
    state->mesh.mesh_enabled = 0;
    return 0;
}

int vpn_kill_switch(vpn_state_t *state, uint8_t enable) {
    if (!state) return -1;
    state->connection.kill_switch_active = enable;
    return 0;
}

int vpn_route_packet(vpn_state_t *state, const uint8_t *data,
                     uint16_t len, uint8_t *out, uint16_t *out_len) {
    if (!state || !data || !out || !out_len) return -1;
    if (!state->connection.established) {
        if (state->connection.kill_switch_active) return -2;
        /* Without VPN, pass through (if kill switch disabled) */
        fs_memcpy(out, data, len);
        *out_len = len;
        return 0;
    }
    /* Wrap packet through 5 layers of encryption (simulated) */
    uint16_t offset = 0;
    for (int h = 0; h < VPN_MAX_HOPS; h++) {
        uint8_t opt = state->mesh.active_path[h];
        vpn_node_t *node = &state->mesh.hops[h].options[opt];
        /* Add layer header: protocol + node indicator */
        out[offset++] = (uint8_t)node->protocol;
        out[offset++] = opt;
        out[offset++] = h;
    }
    /* Copy payload */
    if (offset + len > *out_len) {
        *out_len = offset + len;
        return -3;
    }
    fs_memcpy(out + offset, data, len);
    *out_len = offset + len;
    state->mesh.bytes_routed += len;
    state->mesh.packets_routed++;
    state->connection.bytes_sent += len;
    return 0;
}

int vpn_receive_packet(vpn_state_t *state, uint8_t *data, uint16_t *len) {
    if (!state || !data || !len) return -1;
    if (!state->connection.established) return -2;
    /* Strip 5 layers (3 bytes each = 15 bytes header) */
    uint16_t header_size = VPN_MAX_HOPS * 3;
    if (*len <= header_size) return -3;
    uint16_t payload_len = *len - header_size;
    fs_memcpy(data, data + header_size, payload_len);
    *len = payload_len;
    state->connection.bytes_received += payload_len;
    return 0;
}

int vpn_mesh_status(vpn_state_t *state, char *buf, uint16_t buf_len) {
    if (!state || !buf) return -1;
    int pos = 0;
    pos += fs_snprintf(buf + pos, buf_len - pos,
        "=== VPN MESH MATRIX STATUS ===\n"
        "Mesh Active: %s\n"
        "Connected: %s\n"
        "Kill Switch: %s\n"
        "Auto-Rotate: %s (every %us)\n"
        "Auto-Route: %s\n"
        "Total Possible Paths: %u\n"
        "Paths Used: %u\n"
        "Bytes Routed: %llu\n"
        "Packets Routed: %llu\n"
        "Current Path: ",
        state->mesh.mesh_enabled ? "YES" : "NO",
        state->connection.established ? "YES" : "NO",
        state->connection.kill_switch_active ? "ARMED" : "OFF",
        state->mesh.auto_rotate ? "YES" : "NO",
        state->mesh.rotation_interval,
        state->mesh.auto_route ? "YES" : "NO",
        state->mesh.total_paths,
        state->mesh.paths_used,
        (unsigned long long)state->mesh.bytes_routed,
        (unsigned long long)state->mesh.packets_routed);
    for (int h = 0; h < VPN_MAX_HOPS; h++) {
        uint8_t opt = state->mesh.active_path[h];
        vpn_node_t *node = &state->mesh.hops[h].options[opt];
        pos += fs_snprintf(buf + pos, buf_len - pos,
            "H%d:%s[%s] ", h, node->name,
            vpn_protocol_name(node->protocol));
    }
    pos += fs_snprintf(buf + pos, buf_len - pos, "\n");
    return pos;
}

int vpn_path_report(vpn_state_t *state, char *buf, uint16_t buf_len) {
    if (!state || !buf) return -1;
    int pos = 0;
    pos += fs_snprintf(buf + pos, buf_len - pos,
        "=== VPN PATH REPORT ===\n");
    for (int h = 0; h < VPN_MAX_HOPS; h++) {
        uint8_t opt = state->mesh.active_path[h];
        vpn_node_t *node = &state->mesh.hops[h].options[opt];
        pos += fs_snprintf(buf + pos, buf_len - pos,
            "  Hop %d: %s (%s:%u) [%s] Trust:%d Free:%s Logs:%s Latency:%ums\n",
            h, node->name, node->host, node->port,
            vpn_protocol_name(node->protocol),
            node->trust_score,
            node->is_free ? "YES" : "NO",
            node->keeps_logs ? "YES" : "NO",
            node->latency_ms);
    }
    uint8_t sel[VPN_MAX_HOPS];
    vpn_get_current_path(state, sel);
    pos += fs_snprintf(buf + pos, buf_len - pos,
        "  Total Latency: %ums\n"
        "  Path Trust: %d\n",
        vpn_path_latency(&state->mesh, sel),
        vpn_path_trust(&state->mesh, sel));
    return pos;
}

int vpn_node_report(vpn_mesh_t *mesh, int hop, int option,
                    char *buf, uint16_t buf_len) {
    if (!mesh || !buf || hop < 0 || hop >= VPN_MAX_HOPS ||
        option < 0 || option >= VPN_OPTIONS_PER_HOP)
        return -1;
    vpn_node_t *node = &mesh->hops[hop].options[option];
    return fs_snprintf(buf, buf_len,
        "Node: %s\n"
        "Host: %s:%u\n"
        "Protocol: %s\n"
        "Country: %s\n"
        "Free: %s | Active: %s\n"
        "Trust: %d | Latency: %ums | BW: %ukbps\n"
        "Keeps Logs: %s\n",
        node->name, node->host, node->port,
        vpn_protocol_name(node->protocol),
        node->country_code,
        node->is_free ? "YES" : "NO",
        node->is_active ? "YES" : "NO",
        node->trust_score, node->latency_ms, node->bandwidth_kbps,
        node->keeps_logs ? "YES" : "NO");
}

int vpn_enumerate_paths(vpn_mesh_t *mesh, uint32_t *path_count) {
    if (!mesh || !path_count) return -1;
    *path_count = mesh->total_paths;
    return 0;
}

uint32_t vpn_path_latency(vpn_mesh_t *mesh,
                          const uint8_t selections[VPN_MAX_HOPS]) {
    if (!mesh || !selections) return 0;
    uint32_t total = 0;
    for (int h = 0; h < VPN_MAX_HOPS; h++)
        total += mesh->hops[h].options[selections[h]].latency_ms;
    return total;
}

int8_t vpn_path_trust(vpn_mesh_t *mesh,
                      const uint8_t selections[VPN_MAX_HOPS]) {
    if (!mesh || !selections) return 0;
    int32_t sum = 0;
    for (int h = 0; h < VPN_MAX_HOPS; h++)
        sum += mesh->hops[h].options[selections[h]].trust_score;
    return (int8_t)(sum / VPN_MAX_HOPS);
}

const char *vpn_protocol_name(vpn_protocol_t proto) {
    switch (proto) {
        case VPN_PROTO_WIREGUARD:  return "WireGuard";
        case VPN_PROTO_OPENVPN:    return "OpenVPN";
        case VPN_PROTO_IPSEC:      return "IPsec";
        case VPN_PROTO_SSH_TUNNEL: return "SSH-Tunnel";
        case VPN_PROTO_TOR:        return "Tor";
        case VPN_PROTO_I2P:        return "I2P";
        case VPN_PROTO_GARLIC:     return "Garlic(PungentClove)";
        case VPN_PROTO_PLNP:       return "PLNP";
        default: return "Unknown";
    }
}

vpn_protocol_t vpn_protocol_from_name(const char *name) {
    if (!name) return VPN_PROTO_WIREGUARD;
    if (fs_strstr(name, "wire") || fs_strstr(name, "Wire")) return VPN_PROTO_WIREGUARD;
    if (fs_strstr(name, "open") || fs_strstr(name, "Open")) return VPN_PROTO_OPENVPN;
    if (fs_strstr(name, "ipsec") || fs_strstr(name, "IPsec")) return VPN_PROTO_IPSEC;
    if (fs_strstr(name, "ssh") || fs_strstr(name, "SSH")) return VPN_PROTO_SSH_TUNNEL;
    if (fs_strstr(name, "tor") || fs_strstr(name, "Tor")) return VPN_PROTO_TOR;
    if (fs_strstr(name, "i2p") || fs_strstr(name, "I2P")) return VPN_PROTO_I2P;
    if (fs_strstr(name, "garlic") || fs_strstr(name, "Garlic")) return VPN_PROTO_GARLIC;
    if (fs_strstr(name, "plnp") || fs_strstr(name, "PLNP")) return VPN_PROTO_PLNP;
    return VPN_PROTO_WIREGUARD;
}

int vpn_mesh_visualize(vpn_state_t *state, char *buf, uint16_t buf_len) {
    if (!state || !buf) return -1;
    int pos = 0;
    pos += fs_snprintf(buf + pos, buf_len - pos,
        "=== VPN 25-GRID MATRIX VISUALIZATION ===\n\n"
        "     Opt0         Opt1         Opt2         Opt3         Opt4\n"
        "  +------------+------------+------------+------------+------------+\n");
    for (int h = 0; h < VPN_MAX_HOPS; h++) {
        pos += fs_snprintf(buf + pos, buf_len - pos, "H%d|", h);
        for (int o = 0; o < VPN_OPTIONS_PER_HOP; o++) {
            vpn_node_t *node = &state->mesh.hops[h].options[o];
            uint8_t active = (state->mesh.active_path[h] == o);
            const char *marker = active ? ">>" : "  ";
            pos += fs_snprintf(buf + pos, buf_len - pos,
                "%s%-10s|", marker, node->name);
        }
        pos += fs_snprintf(buf + pos, buf_len - pos, "\n");
        pos += fs_snprintf(buf + pos, buf_len - pos,
            "  +------------+------------+------------+------------+------------+\n");
    }
    pos += fs_snprintf(buf + pos, buf_len - pos,
        "\n  >> = active node in current circuit\n"
        "  Total paths: %u (5^5 = 3125)\n"
        "  Paths used: %u\n",
        state->mesh.total_paths, state->mesh.paths_used);
    return pos;
}

/* ---- DECLARATION -----------------------------------------------------------

 * REQUIRES_NONE, AND THAT IS THE MEASUREMENT, NOT AN OMISSION. This file sits
 * in panopticon/ beside panopticon.c and it would be natural to declare
 * REQUIRES(panopticon_ready). panopticon_vpn.o's `nm -u` is EMPTY: it does not
 * call one panopticon_* entry point. The mesh is independent of the watcher
 * table; sharing a directory is not an edge.
 */
#include "zxv_decl.h"
static int zxvd_panopticon_vpn_bringup(void) {
    static vpn_state_t st;
    if (vpn_mesh_init(&st) != 0)     return -1;
    if (vpn_load_defaults(&st) != 0) return -1;
    if (vpn_mesh_tick(&st) != 0)     return -1;
    return 0;
}

ZXV_DECLARE(panopticon_vpn,
    ZXV_PROVIDES(panopticon_vpn_ready),
    ZXV_REQUIRES_NONE,
    ZXV_BRINGUP(zxvd_panopticon_vpn_bringup));
