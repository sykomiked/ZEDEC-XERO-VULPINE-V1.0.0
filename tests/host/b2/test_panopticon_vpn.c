/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_panopticon_vpn.c: unit tests for kernel/src/panopticon/panopticon_vpn.c.
 *
 * Init/shutdown state, node add/remove/trust bounds, the full 25-node default
 * table, path selection and its history ring, the auto-route score (trust,
 * latency, log and free terms, inactive nodes, ties, the floor score, the
 * repeat-avoidance shift), the seeded random path, rotation and the tick,
 * connect/disconnect, packet labelling and capacity checks, receive, every
 * report's exact text, path latency/trust, protocol names both ways, the
 * grid visualisation's markers, and the module's declared bring-up hook. */
#define _GNU_SOURCE
#include <link.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "panopticon_vpn.h"
#include "zxv_decl.h"

static int checks, failures;
#define CHECK(c)                                                              \
    do {                                                                      \
        checks++;                                                             \
        if (!(c)) {                                                           \
            failures++;                                                       \
            printf("FAIL line %d: %s\n", __LINE__, #c);                       \
        }                                                                     \
    } while (0)

/* ---- finding the file's static ZXV_DECLARE record (see desktop test) ---- */
struct seg { uintptr_t lo, hi; int x; };
static struct seg segs[32];
static int nsegs;

static int phdr_cb(struct dl_phdr_info *info, size_t sz, void *data) {
    (void)sz;
    (void)data;
    for (int i = 0; i < info->dlpi_phnum && nsegs < 32; i++) {
        const ElfW(Phdr) *p = &info->dlpi_phdr[i];
        if (p->p_type != PT_LOAD || !(p->p_flags & PF_R)) continue;
        segs[nsegs].lo = info->dlpi_addr + p->p_vaddr;
        segs[nsegs].hi = segs[nsegs].lo + p->p_memsz;
        segs[nsegs].x = (p->p_flags & PF_X) != 0;
        nsegs++;
    }
    return 1;
}

static int in_seg(uintptr_t p, size_t n, int need_x) {
    for (int i = 0; i < nsegs; i++)
        if (p >= segs[i].lo && p + n <= segs[i].hi && (!need_x || segs[i].x)) return 1;
    return 0;
}

__attribute__((no_sanitize_address)) static zxv_bringup_fn find_bringup(const char *name) {
    size_t len = 0;
    while (name[len]) len++;
    len++;
    nsegs = 0;
    dl_iterate_phdr(phdr_cb, NULL);
    for (int s = 0; s < nsegs; s++) {
        for (uintptr_t a = (segs[s].lo + 7u) & ~(uintptr_t)7u; a + 16u <= segs[s].hi; a += 8u) {
            const uintptr_t *w = (const uintptr_t *)a;
            uintptr_t m = w[0], f = w[1];
            if (!f || !in_seg(f, 1, 1) || !in_seg(m, len, 0)) continue;
            const char *mn = (const char *)m;
            size_t i = 0;
            while (i < len && mn[i] == name[i]) i++;
            if (i == len) return (zxv_bringup_fn)f;
        }
    }
    return NULL;
}

static vpn_state_t *new_state(void) {
    vpn_state_t *s = malloc(sizeof(*s));
    memset(s, 0xA5, sizeof(*s));
    CHECK(vpn_mesh_init(s) == 0);
    return s;
}

static int path_is(vpn_state_t *s, int a, int b, int c, int d, int e) {
    const uint8_t *p = s->mesh.active_path;
    int want[5] = {a, b, c, d, e};
    for (int i = 0; i < 5; i++)
        if (p[i] != want[i] || s->mesh.hops[i].selected != want[i]) return 0;
    return 1;
}

static const struct {
    const char *name, *host;
    uint16_t port;
    vpn_protocol_t proto;
    const char *cc;
} DEF[5][5] = {
    {{"FreeGate-US", "entry-us.freevpn.zedec", 51820, VPN_PROTO_WIREGUARD, "US"},
     {"FreeGate-CH", "entry-ch.freevpn.zedec", 51820, VPN_PROTO_WIREGUARD, "CH"},
     {"FreeGate-IS", "entry-is.freevpn.zedec", 51820, VPN_PROTO_WIREGUARD, "IS"},
     {"P2P-Relay-01", "p2p-relay-01.zedec.mesh", 443, VPN_PROTO_PLNP, "XX"},
     {"Tor-Entry", "tor-entry.zedec.onion", 9001, VPN_PROTO_TOR, "XX"}},
    {{"Relay-DE", "relay-de.freevpn.zedec", 51821, VPN_PROTO_WIREGUARD, "DE"},
     {"Relay-SE", "relay-se.freevpn.zedec", 51821, VPN_PROTO_WIREGUARD, "SE"},
     {"Relay-JP", "relay-jp.freevpn.zedec", 51821, VPN_PROTO_WIREGUARD, "JP"},
     {"P2P-Relay-02", "p2p-relay-02.zedec.mesh", 443, VPN_PROTO_GARLIC, "XX"},
     {"I2P-Relay", "i2p-relay.zedec.i2p", 8888, VPN_PROTO_I2P, "XX"}},
    {{"MidRelay-CA", "mid-ca.freevpn.zedec", 51822, VPN_PROTO_WIREGUARD, "CA"},
     {"MidRelay-NO", "mid-no.freevpn.zedec", 51822, VPN_PROTO_WIREGUARD, "NO"},
     {"MidRelay-SG", "mid-sg.freevpn.zedec", 51822, VPN_PROTO_WIREGUARD, "SG"},
     {"P2P-Relay-03", "p2p-relay-03.zedec.mesh", 443, VPN_PROTO_PLNP, "XX"},
     {"SSH-Tunnel-01", "ssh-tunnel-01.zedec.mesh", 22, VPN_PROTO_SSH_TUNNEL, "XX"}},
    {{"ExitRelay-FI", "exit-fi.freevpn.zedec", 51823, VPN_PROTO_WIREGUARD, "FI"},
     {"ExitRelay-PT", "exit-pt.freevpn.zedec", 51823, VPN_PROTO_WIREGUARD, "PT"},
     {"ExitRelay-NZ", "exit-nz.freevpn.zedec", 51823, VPN_PROTO_WIREGUARD, "NZ"},
     {"P2P-Relay-04", "p2p-relay-04.zedec.mesh", 443, VPN_PROTO_GARLIC, "XX"},
     {"Garlic-Exit", "garlic-exit.zedec.mesh", 443, VPN_PROTO_GARLIC, "XX"}},
    {{"Exit-PA", "exit-pa.freevpn.zedec", 51824, VPN_PROTO_WIREGUARD, "PA"},
     {"Exit-RO", "exit-ro.freevpn.zedec", 51824, VPN_PROTO_WIREGUARD, "RO"},
     {"Exit-KR", "exit-kr.freevpn.zedec", 51824, VPN_PROTO_WIREGUARD, "KR"},
     {"P2P-Exit-01", "p2p-exit-01.zedec.mesh", 443, VPN_PROTO_PLNP, "XX"},
     {"Tor-Exit", "tor-exit.zedec.onion", 9030, VPN_PROTO_TOR, "XX"}},
};

static void test_init_shutdown(void) {
    CHECK(vpn_mesh_init(NULL) == -1);
    CHECK(vpn_mesh_shutdown(NULL) == -1);
    CHECK(vpn_mesh_tick(NULL) == -1);
    vpn_state_t *s = new_state();
    CHECK(s->mesh.rotation_interval == 300);
    CHECK(s->mesh.auto_rotate == 1 && s->mesh.auto_route == 1);
    CHECK(s->mesh.mesh_enabled == 0);
    CHECK(s->mesh.total_paths == 3125);
    CHECK(s->mesh.paths_used == 0 && s->mesh.last_rotation == 0);
    CHECK(s->mesh.bytes_routed == 0 && s->mesh.packets_routed == 0);
    CHECK(s->default_free_vpns == 1);
    CHECK(s->connection.kill_switch_active == 1);
    CHECK(s->connection.established == 0 && s->connection.encryption_active == 0);
    CHECK(s->connection.bytes_sent == 0 && s->connection.bytes_received == 0);
    CHECK(s->path_history_count == 0 && s->path_history_head == 0);
    CHECK(s->mesh.hops[0].options[0].is_active == 0);
    CHECK(s->mesh.hops[4].options[4].name[0] == 0);
    for (int i = 0; i < 5; i++) CHECK(s->mesh.active_path[i] == 0);

    vpn_connect(s);
    CHECK(s->connection.established == 1);
    CHECK(vpn_mesh_shutdown(s) == 0);
    CHECK(s->connection.established == 0);
    CHECK(s->mesh.mesh_enabled == 0);
    CHECK(s->mesh.total_paths == 0 && s->mesh.rotation_interval == 0);
    CHECK(s->connection.kill_switch_active == 0);
    CHECK(s->default_free_vpns == 0);
    CHECK(s->mesh.hops[0].options[0].is_active == 0);
    CHECK(s->path_history_count == 0 && s->mesh.paths_used == 0);
    free(s);
}

static void test_nodes(void) {
    vpn_mesh_t m;
    memset(&m, 0x5A, sizeof(m));
    CHECK(vpn_add_node(NULL, 0, 0, "a", "b", 1, VPN_PROTO_TOR, 1) == -1);
    CHECK(vpn_add_node(&m, -1, 0, "a", "b", 1, VPN_PROTO_TOR, 1) == -1);
    CHECK(vpn_add_node(&m, 5, 0, "a", "b", 1, VPN_PROTO_TOR, 1) == -1);
    CHECK(vpn_add_node(&m, 0, -1, "a", "b", 1, VPN_PROTO_TOR, 1) == -1);
    CHECK(vpn_add_node(&m, 0, 5, "a", "b", 1, VPN_PROTO_TOR, 1) == -1);
    CHECK(m.hops[0].options[0].port == 0x5A5A);

    CHECK(vpn_add_node(&m, 4, 4, "node", "host.example", 1234, VPN_PROTO_I2P, 1) == 0);
    vpn_node_t *n = &m.hops[4].options[4];
    CHECK(strcmp((char *)n->name, "node") == 0);
    CHECK(strcmp((char *)n->host, "host.example") == 0);
    CHECK(n->port == 1234);
    CHECK(n->protocol == VPN_PROTO_I2P);
    CHECK(n->is_free == 1 && n->is_active == 1);
    CHECK(n->trust_score == 10);
    CHECK(n->latency_ms == 0 && n->bandwidth_kbps == 0 && n->keeps_logs == 0);
    CHECK(n->country_code[0] == 0 && n->pubkey[0] == 0 && n->pubkey[31] == 0);
    CHECK(m.hops[4].options[3].port == 0x5A5A);

    CHECK(vpn_add_node(&m, 0, 0, NULL, NULL, 7, VPN_PROTO_OPENVPN, 0) == 0);
    n = &m.hops[0].options[0];
    CHECK(strcmp((char *)n->name, "unnamed") == 0);
    CHECK(n->host[0] == 0);
    CHECK(n->trust_score == 30 && n->is_free == 0 && n->is_active == 1);
    char big[300];
    memset(big, 'h', sizeof(big) - 1);
    big[sizeof(big) - 1] = 0;
    CHECK(vpn_add_node(&m, 2, 3, big, big, 1, VPN_PROTO_TOR, 0) == 0);
    CHECK(strlen((char *)m.hops[2].options[3].name) == 63);
    CHECK(strlen((char *)m.hops[2].options[3].host) == 127);

    CHECK(vpn_set_node_trust(NULL, 0, 0, 5) == -1);
    CHECK(vpn_set_node_trust(&m, -1, 0, 5) == -1);
    CHECK(vpn_set_node_trust(&m, 5, 0, 5) == -1);
    CHECK(vpn_set_node_trust(&m, 0, -1, 5) == -1);
    CHECK(vpn_set_node_trust(&m, 0, 5, 5) == -1);
    CHECK(vpn_set_node_trust(&m, 4, 4, -77) == 0);
    CHECK(m.hops[4].options[4].trust_score == -77);
    CHECK(vpn_set_node_trust(&m, 0, 0, 100) == 0);
    CHECK(m.hops[0].options[0].trust_score == 100);

    CHECK(vpn_remove_node(NULL, 0, 0) == -1);
    CHECK(vpn_remove_node(&m, -1, 0) == -1);
    CHECK(vpn_remove_node(&m, 5, 0) == -1);
    CHECK(vpn_remove_node(&m, 0, -1) == -1);
    CHECK(vpn_remove_node(&m, 0, 5) == -1);
    CHECK(m.hops[0].options[0].is_active == 1);
    CHECK(vpn_remove_node(&m, 4, 4) == 0);
    n = &m.hops[4].options[4];
    CHECK(n->is_active == 0 && n->name[0] == 0 && n->port == 0 && n->trust_score == 0);
    CHECK(m.hops[0].options[0].is_active == 1);
    CHECK(vpn_remove_node(&m, 0, 0) == 0);
    CHECK(m.hops[0].options[0].is_active == 0);
}

static void test_defaults(void) {
    CHECK(vpn_load_defaults(NULL) == -1);
    vpn_state_t *s = new_state();
    for (int h = 0; h < 5; h++) {
        s->mesh.active_path[h] = 3;
        s->mesh.hops[h].selected = 3;
    }
    CHECK(vpn_load_defaults(s) == 0);
    for (int h = 0; h < 5; h++) {
        CHECK(s->mesh.active_path[h] == 0);
        CHECK(s->mesh.hops[h].selected == 0);
        for (int o = 0; o < 5; o++) {
            vpn_node_t *n = &s->mesh.hops[h].options[o];
            CHECK(strcmp((char *)n->name, DEF[h][o].name) == 0);
            CHECK(strcmp((char *)n->host, DEF[h][o].host) == 0);
            CHECK(n->port == DEF[h][o].port);
            CHECK(n->protocol == DEF[h][o].proto);
            CHECK(strcmp((char *)n->country_code, DEF[h][o].cc) == 0);
            CHECK(n->is_free == 1 && n->is_active == 1);
            CHECK(n->trust_score == 10);
            CHECK(n->keeps_logs == 0);
            CHECK(n->latency_ms == 0);
        }
    }
    CHECK(s->mesh.paths_used == 0);
    free(s);
}

static void test_select_path(void) {
    vpn_state_t *s = new_state();
    vpn_load_defaults(s);
    uint8_t sel[5] = {1, 2, 3, 4, 0};
    CHECK(vpn_select_path(NULL, sel) == -1);
    CHECK(vpn_select_path(s, NULL) == -1);
    uint8_t bad[5] = {0, 0, 0, 0, 5};
    CHECK(vpn_select_path(s, bad) == -1);
    CHECK(s->path_history_count == 0 && s->mesh.paths_used == 0 && s->path_history_head == 0);
    uint8_t bad0[5] = {5, 0, 0, 0, 0};
    CHECK(vpn_select_path(s, bad0) == -1);
    CHECK(s->mesh.active_path[0] == 0);

    s->mesh.hops[0].options[1].latency_ms = 10;
    s->mesh.hops[1].options[2].latency_ms = 20;
    s->mesh.hops[2].options[3].latency_ms = 30;
    s->mesh.hops[3].options[4].latency_ms = 40;
    s->mesh.hops[4].options[0].latency_ms = 50;
    s->mesh.last_rotation = 99;
    CHECK(vpn_select_path(s, sel) == 0);
    CHECK(path_is(s, 1, 2, 3, 4, 0));
    vpn_path_t *p = &s->path_history[0];
    CHECK(p->selections[0] == 1 && p->selections[1] == 2 && p->selections[2] == 3);
    CHECK(p->selections[3] == 4 && p->selections[4] == 0);
    CHECK(p->path_id == 1 * 625 + 2 * 125 + 3 * 25 + 4 * 5 + 0);
    CHECK(p->in_use == 1);
    CHECK(p->activated_tick == 99);
    CHECK(p->latency_total_ms == 150);
    CHECK(s->path_history_head == 1 && s->path_history_count == 1);
    CHECK(s->mesh.paths_used == 1);

    uint8_t top[5] = {4, 4, 4, 4, 4};
    CHECK(vpn_select_path(s, top) == 0);
    CHECK(s->path_history[1].path_id == 3124);
    CHECK(s->path_history[1].latency_total_ms == 40);
    uint8_t got[5];
    CHECK(vpn_get_current_path(NULL, got) == -1);
    CHECK(vpn_get_current_path(s, NULL) == -1);
    CHECK(vpn_get_current_path(s, got) == 0);
    CHECK(got[0] == 4 && got[1] == 4 && got[2] == 4 && got[3] == 4 && got[4] == 4);

    /* the history ring wraps at 64 entries */
    for (int i = 2; i < 64; i++) vpn_select_path(s, sel);
    CHECK(s->path_history_head == 0 && s->path_history_count == 64);
    uint8_t z[5] = {0, 0, 0, 0, 1};
    vpn_select_path(s, z);
    CHECK(s->path_history_head == 1 && s->path_history_count == 64);
    CHECK(s->path_history[0].path_id == 1);
    CHECK(s->mesh.paths_used == 65);
    free(s);
}

static void test_auto_select(void) {
    CHECK(vpn_auto_select_path(NULL) == -1);
    CHECK(vpn_random_path(NULL) == -1);
    vpn_state_t *s = new_state();
    vpn_load_defaults(s);
    /* all equal: first option wins every hop, then the repeat shift on hop 0 */
    CHECK(vpn_auto_select_path(s) == 0);
    CHECK(path_is(s, 1, 0, 0, 0, 0));
    CHECK(s->mesh.paths_used == 1);
    /* not a repeat any more: same best path, no shift */
    CHECK(vpn_auto_select_path(s) == 0);
    CHECK(path_is(s, 0, 0, 0, 0, 0));
    /* repeat with best[0] == 4 wraps to 0 */
    vpn_set_node_trust(&s->mesh, 0, 4, 50);
    uint8_t p40[5] = {4, 0, 0, 0, 0};
    vpn_select_path(s, p40);
    CHECK(vpn_auto_select_path(s) == 0);
    CHECK(path_is(s, 0, 0, 0, 0, 0));
    /* a repeat that differs only at the last hop is not a repeat */
    vpn_set_node_trust(&s->mesh, 0, 4, 10);
    vpn_set_node_trust(&s->mesh, 4, 2, 11);
    uint8_t p01[5] = {0, 0, 0, 0, 1};
    vpn_select_path(s, p01);
    CHECK(vpn_auto_select_path(s) == 0);
    CHECK(path_is(s, 0, 0, 0, 0, 2));
    free(s);

    /* the score terms, one per hop */
    s = new_state();
    vpn_load_defaults(s);
    vpn_set_node_trust(&s->mesh, 0, 2, 11);         /* 115 beats 105 */
    s->mesh.hops[1].options[0].latency_ms = 200;    /* 85: opt1 wins */
    s->mesh.hops[2].options[0].keeps_logs = 1;      /* 55: opt1 wins */
    s->mesh.hops[3].options[0].is_free = 0;         /* 100: opt1 wins */
    s->mesh.hops[4].options[0].is_active = 0;       /* inactive, however trusted */
    s->mesh.hops[4].options[0].trust_score = 100;
    s->mesh.hops[4].options[1].is_active = 0;
    CHECK(vpn_auto_select_path(s) == 0);
    CHECK(path_is(s, 2, 1, 1, 1, 2));
    free(s);

    s = new_state();
    vpn_load_defaults(s);
    vpn_set_node_trust(&s->mesh, 0, 3, 11);
    s->mesh.hops[0].options[3].latency_ms = 99;     /* 110 - 9 + 5 = 106 */
    vpn_set_node_trust(&s->mesh, 1, 3, 11);
    s->mesh.hops[1].options[3].latency_ms = 109;    /* 110 - 10 + 5 = 105: tie */
    vpn_set_node_trust(&s->mesh, 2, 0, 15);
    s->mesh.hops[2].options[0].keeps_logs = 1;      /* 150 - 50 + 5 = 105: tie */
    vpn_set_node_trust(&s->mesh, 3, 1, 15);
    s->mesh.hops[3].options[1].keeps_logs = 1;      /* 105 tie, not first */
    s->mesh.hops[4].options[0].is_free = 0;         /* 100 */
    s->mesh.hops[4].options[1].latency_ms = 50;     /* 100 - 5 + 5 = 100: tie */
    for (int o = 2; o < 5; o++) s->mesh.hops[4].options[o].is_active = 0;
    s->mesh.active_path[0] = 4;
    CHECK(vpn_auto_select_path(s) == 0);
    CHECK(path_is(s, 3, 0, 0, 0, 0));
    free(s);

    s = new_state();
    vpn_load_defaults(s);
    s->mesh.hops[0].options[0].latency_ms = 50;     /* free: 100 */
    s->mesh.hops[0].options[1].is_free = 0;         /* 100: tie, keep opt0 */
    for (int o = 2; o < 5; o++) s->mesh.hops[0].options[o].is_active = 0;
    /* floor score: a node scoring exactly -999999 is not chosen */
    for (int o = 0; o < 5; o++) s->mesh.hops[1].options[o].is_active = 0;
    s->mesh.hops[1].options[3] = (vpn_node_t){.is_active = 1, .latency_ms = 9999990};
    for (int o = 0; o < 5; o++) s->mesh.hops[2].options[o].is_active = 0;
    s->mesh.hops[2].options[2] = (vpn_node_t){.is_active = 1, .latency_ms = 9999980};
    /* a negative trust still beats the floor */
    vpn_set_node_trust(&s->mesh, 3, 0, -100);
    vpn_set_node_trust(&s->mesh, 3, 1, -100);
    vpn_set_node_trust(&s->mesh, 3, 2, -100);
    vpn_set_node_trust(&s->mesh, 3, 3, -99);
    vpn_set_node_trust(&s->mesh, 3, 4, -100);
    s->mesh.active_path[4] = 3;
    CHECK(vpn_auto_select_path(s) == 0);
    CHECK(path_is(s, 0, 0, 2, 3, 0));
    free(s);
}

static void test_random_rotate_tick(void) {
    vpn_state_t *s = new_state();
    vpn_load_defaults(s);
    /* seed = last_rotation ^ bytes_routed, an LCG step per hop */
    CHECK(vpn_random_path(s) == 0);
    CHECK(path_is(s, 2, 4, 2, 1, 0));
    s->mesh.last_rotation = 1;
    vpn_random_path(s);
    CHECK(path_is(s, 4, 3, 1, 0, 4));
    s->mesh.last_rotation = 1;
    s->mesh.bytes_routed = 100;
    vpn_random_path(s);
    CHECK(path_is(s, 2, 1, 1, 0, 3));
    s->mesh.last_rotation = 12345;
    s->mesh.bytes_routed = 0;
    vpn_random_path(s);
    CHECK(path_is(s, 4, 3, 2, 1, 0));
    CHECK(s->mesh.paths_used == 4);

    /* rotate: tick +1, then random (auto_route off) or auto (on) */
    CHECK(vpn_rotate_now(NULL) == -1);
    CHECK(vpn_enable_auto_route(NULL, 0) == -1);
    CHECK(vpn_enable_auto_rotate(NULL, 0) == -1);
    CHECK(vpn_set_rotation_interval(NULL, 5) == -1);
    CHECK(vpn_enable_auto_route(s, 0) == 0);
    CHECK(s->mesh.auto_route == 0);
    s->mesh.last_rotation = 1;
    CHECK(vpn_rotate_now(s) == 0);
    CHECK(s->mesh.last_rotation == 2);
    CHECK(path_is(s, 0, 2, 1, 4, 0));
    CHECK(s->path_history[s->path_history_head - 1].activated_tick == 2);
    CHECK(vpn_rotate_now(s) == 0);
    CHECK(s->mesh.last_rotation == 3);
    CHECK(path_is(s, 4, 3, 0, 3, 4));
    CHECK(vpn_enable_auto_route(s, 1) == 0);
    CHECK(s->mesh.auto_route == 1);
    CHECK(vpn_rotate_now(s) == 0);
    CHECK(s->mesh.last_rotation == 4);
    CHECK(path_is(s, 0, 0, 0, 0, 0));
    CHECK(vpn_rotate_now(s) == 0);
    CHECK(path_is(s, 1, 0, 0, 0, 0));

    /* tick only rotates when connected, auto-rotating, and the sum wraps */
    CHECK(vpn_set_rotation_interval(s, 0) == 0);
    CHECK(s->mesh.rotation_interval == 0);
    uint32_t used = s->mesh.paths_used;
    CHECK(vpn_mesh_tick(s) == 0); /* not connected */
    CHECK(s->mesh.paths_used == used && s->mesh.last_rotation == 5);
    s->connection.established = 1;
    CHECK(vpn_mesh_tick(s) == 0); /* interval 0: last + 0 <= last */
    CHECK(s->mesh.last_rotation == 6);
    CHECK(s->mesh.paths_used == used + 1);
    vpn_set_rotation_interval(s, 1);
    CHECK(vpn_mesh_tick(s) == 0);
    CHECK(s->mesh.last_rotation == 6 && s->mesh.paths_used == used + 1);
    s->mesh.last_rotation = UINT64_MAX - 5;
    vpn_set_rotation_interval(s, 300);
    CHECK(vpn_mesh_tick(s) == 0); /* wraps */
    CHECK(s->mesh.last_rotation == UINT64_MAX - 4);
    CHECK(s->mesh.paths_used == used + 2);
    s->mesh.last_rotation = UINT64_MAX - 300;
    CHECK(vpn_mesh_tick(s) == 0); /* exactly UINT64_MAX: no wrap */
    CHECK(s->mesh.last_rotation == UINT64_MAX - 300 && s->mesh.paths_used == used + 2);
    s->mesh.last_rotation = UINT64_MAX - 299;
    CHECK(vpn_mesh_tick(s) == 0); /* wraps to 0 */
    CHECK(s->mesh.last_rotation == UINT64_MAX - 298 && s->mesh.paths_used == used + 3);
    CHECK(vpn_enable_auto_rotate(s, 0) == 0);
    CHECK(s->mesh.auto_rotate == 0);
    vpn_set_rotation_interval(s, 0);
    CHECK(vpn_mesh_tick(s) == 0);
    CHECK(s->mesh.paths_used == used + 3);
    free(s);
}

static void test_connect(void) {
    CHECK(vpn_connect(NULL) == -1);
    CHECK(vpn_disconnect(NULL) == -1);
    CHECK(vpn_kill_switch(NULL, 0) == -1);
    vpn_state_t *s = new_state();
    s->mesh.last_rotation = 42;
    CHECK(vpn_connect(s) == 0);
    CHECK(strcmp((char *)s->mesh.hops[3].options[4].name, "Garlic-Exit") == 0);
    CHECK(path_is(s, 1, 0, 0, 0, 0));
    CHECK(s->connection.established == 1);
    CHECK(s->connection.encryption_active == 0);
    CHECK(s->connection.established_tick == 42);
    CHECK(s->mesh.mesh_enabled == 1);
    CHECK(s->mesh.paths_used == 1);
    CHECK(vpn_disconnect(s) == 0);
    CHECK(s->connection.established == 0 && s->mesh.mesh_enabled == 0);
    CHECK(s->mesh.paths_used == 1);
    free(s);

    /* custom first node: defaults are not loaded over it */
    s = new_state();
    vpn_add_node(&s->mesh, 0, 0, "mine", "h", 1, VPN_PROTO_TOR, 0);
    vpn_enable_auto_route(s, 0);
    s->connection.encryption_active = 1;
    CHECK(vpn_connect(s) == 0);
    CHECK(strcmp((char *)s->mesh.hops[0].options[0].name, "mine") == 0);
    CHECK(s->mesh.hops[1].options[0].is_active == 0);
    CHECK(path_is(s, 0, 0, 0, 0, 0));
    CHECK(s->path_history_count == 1);
    CHECK(s->connection.encryption_active == 0);
    free(s);

    /* defaults disabled: nothing loaded */
    s = new_state();
    s->default_free_vpns = 0;
    CHECK(vpn_connect(s) == 0);
    CHECK(s->mesh.hops[0].options[0].is_active == 0);
    CHECK(s->mesh.hops[4].options[4].is_active == 0);
    CHECK(s->connection.established == 1);
    CHECK(path_is(s, 1, 0, 0, 0, 0));

    CHECK(vpn_kill_switch(s, 0) == 0);
    CHECK(s->connection.kill_switch_active == 0);
    CHECK(vpn_kill_switch(s, 1) == 0);
    CHECK(s->connection.kill_switch_active == 1);
    free(s);
}

static void test_route_receive(void) {
    vpn_state_t *s = new_state();
    uint8_t data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t out[64];
    uint16_t ol = sizeof(out);
    CHECK(vpn_route_packet(NULL, data, 8, out, &ol) == -1);
    CHECK(vpn_route_packet(s, NULL, 8, out, &ol) == -1);
    CHECK(vpn_route_packet(s, data, 8, NULL, &ol) == -1);
    CHECK(vpn_route_packet(s, data, 8, out, NULL) == -1);
    /* disconnected, kill switch armed */
    ol = 64;
    CHECK(vpn_route_packet(s, data, 8, out, &ol) == -2);
    CHECK(ol == 64);
    /* disconnected, too small, reported before the kill switch */
    ol = 7;
    CHECK(vpn_route_packet(s, data, 8, out, &ol) == -3);
    CHECK(ol == 8);
    /* pass-through with the kill switch off */
    vpn_kill_switch(s, 0);
    memset(out, 0xEE, sizeof(out));
    ol = 8;
    CHECK(vpn_route_packet(s, data, 8, out, &ol) == 0);
    CHECK(ol == 8 && memcmp(out, data, 8) == 0 && out[8] == 0xEE);
    CHECK(s->mesh.bytes_routed == 0 && s->mesh.packets_routed == 0);
    CHECK(s->connection.bytes_sent == 0);

    /* connected: 15 label bytes then the payload */
    vpn_load_defaults(s);
    uint8_t p[5] = {4, 3, 4, 1, 3};
    vpn_select_path(s, p);
    s->connection.established = 1;
    memset(out, 0xEE, sizeof(out));
    ol = 22;
    CHECK(vpn_route_packet(s, data, 8, out, &ol) == -3);
    CHECK(ol == 23);
    CHECK(out[0] == 0xEE);
    ol = 23;
    CHECK(vpn_route_packet(s, data, 8, out, &ol) == 0);
    CHECK(ol == 23);
    static const uint8_t hdr[15] = {VPN_PROTO_TOR,  4, 0, VPN_PROTO_GARLIC, 3, 1,
                                    VPN_PROTO_SSH_TUNNEL, 4, 2, VPN_PROTO_WIREGUARD, 1, 3,
                                    VPN_PROTO_PLNP, 3, 4};
    CHECK(memcmp(out, hdr, 15) == 0);
    CHECK(memcmp(out + 15, data, 8) == 0);
    CHECK(out[23] == 0xEE);
    CHECK(s->mesh.bytes_routed == 8 && s->mesh.packets_routed == 1);
    CHECK(s->connection.bytes_sent == 8);
    ol = 64;
    CHECK(vpn_route_packet(s, data, 3, out, &ol) == 0);
    CHECK(ol == 18);
    CHECK(s->mesh.bytes_routed == 11 && s->mesh.packets_routed == 2);
    CHECK(s->connection.bytes_sent == 11);
    /* zero-length payload: just the labels */
    ol = 15;
    CHECK(vpn_route_packet(s, data, 0, out, &ol) == 0);
    CHECK(ol == 15 && s->mesh.packets_routed == 3);
    /* need above 0xFFFF is refused and reported as 0xFFFF */
    uint8_t *bigin = calloc(65535, 1);
    ol = 0xFFFF;
    CHECK(vpn_route_packet(s, bigin, 65535, out, &ol) == -3);
    CHECK(ol == 0xFFFF);
    ol = 0xFFFF;
    CHECK(vpn_route_packet(s, bigin, 65521, out, &ol) == -3); /* 65536 */
    CHECK(ol == 0xFFFF);
    uint8_t *bigout = malloc(65535);
    ol = 0xFFFF;
    CHECK(vpn_route_packet(s, bigin, 65520, bigout, &ol) == 0); /* exactly 0xFFFF */
    CHECK(ol == 0xFFFF);
    free(bigout);
    free(bigin);
    /* disconnected with kill switch off: exact fit at 0xFFFF only */
    s->connection.established = 0;
    uint8_t *b2 = calloc(65535, 1), *o2 = malloc(65535);
    ol = 0xFFFF;
    CHECK(vpn_route_packet(s, b2, 65535, o2, &ol) == 0);
    CHECK(ol == 0xFFFF);
    free(b2);
    free(o2);

    /* receive */
    uint16_t len = 23;
    uint8_t buf[64];
    memcpy(buf, hdr, 15);
    memcpy(buf + 15, data, 8);
    CHECK(vpn_receive_packet(NULL, buf, &len) == -1);
    CHECK(vpn_receive_packet(s, NULL, &len) == -1);
    CHECK(vpn_receive_packet(s, buf, NULL) == -1);
    CHECK(vpn_receive_packet(s, buf, &len) == -2);
    s->connection.established = 1;
    len = 15;
    CHECK(vpn_receive_packet(s, buf, &len) == -3);
    CHECK(len == 15 && s->connection.bytes_received == 0);
    len = 23;
    CHECK(vpn_receive_packet(s, buf, &len) == 0);
    CHECK(len == 8 && memcmp(buf, data, 8) == 0);
    CHECK(s->connection.bytes_received == 8);
    len = 16;
    memcpy(buf, hdr, 15);
    buf[15] = 0x77;
    CHECK(vpn_receive_packet(s, buf, &len) == 0);
    CHECK(len == 1 && buf[0] == 0x77);
    CHECK(s->connection.bytes_received == 9);
    free(s);
}

static void test_reports(void) {
    char buf[4096], exp[4096];
    vpn_state_t *s = new_state();
    CHECK(vpn_mesh_status(NULL, buf, sizeof(buf)) == -1);
    CHECK(vpn_mesh_status(s, NULL, 10) == -1);
    CHECK(vpn_path_report(NULL, buf, sizeof(buf)) == -1);
    CHECK(vpn_path_report(s, NULL, 10) == -1);
    CHECK(vpn_node_report(NULL, 0, 0, buf, 10) == -1);
    CHECK(vpn_node_report(&s->mesh, 0, 0, NULL, 10) == -1);
    CHECK(vpn_node_report(&s->mesh, -1, 0, buf, 10) == -1);
    CHECK(vpn_node_report(&s->mesh, 5, 0, buf, 10) == -1);
    CHECK(vpn_node_report(&s->mesh, 0, -1, buf, 10) == -1);
    CHECK(vpn_node_report(&s->mesh, 0, 5, buf, 10) == -1);
    CHECK(vpn_mesh_visualize(NULL, buf, sizeof(buf)) == -1);
    CHECK(vpn_mesh_visualize(s, NULL, 10) == -1);

    vpn_load_defaults(s);
    uint8_t p[5] = {4, 3, 4, 1, 3};
    vpn_select_path(s, p);
    s->mesh.bytes_routed = 5000000000ull;
    s->mesh.packets_routed = 7;
    int n = vpn_mesh_status(s, buf, sizeof(buf));
    int e = snprintf(exp, sizeof(exp),
                     "=== VPN MESH MATRIX STATUS ===\n"
                     "Mesh Active: NO\n"
                     "Connected: NO\n"
                     "Kill Switch: ARMED\n"
                     "Auto-Rotate: YES (every 300s)\n"
                     "Auto-Route: YES\n"
                     "Total Possible Paths: 3125\n"
                     "Paths Used: 1\n"
                     "Bytes Routed: 5000000000\n"
                     "Packets Routed: 7\n"
                     "Current Path: H0:Tor-Entry[Tor] H1:P2P-Relay-02[Garlic(PungentClove)] "
                     "H2:SSH-Tunnel-01[SSH-Tunnel] H3:ExitRelay-PT[WireGuard] "
                     "H4:P2P-Exit-01[PLNP] \n");
    CHECK(strcmp(buf, exp) == 0);
    CHECK(n == e);
    s->mesh.mesh_enabled = 1;
    s->connection.established = 1;
    s->connection.kill_switch_active = 0;
    s->mesh.auto_rotate = 0;
    s->mesh.auto_route = 0;
    s->mesh.rotation_interval = 9;
    vpn_mesh_status(s, buf, sizeof(buf));
    CHECK(strstr(buf, "Mesh Active: YES\nConnected: YES\nKill Switch: OFF\n"
                      "Auto-Rotate: NO (every 9s)\nAuto-Route: NO\n") != NULL);
    s->mesh.mesh_enabled = 0;
    vpn_mesh_status(s, buf, sizeof(buf));
    CHECK(strstr(buf, "Mesh Active: NO\nConnected: YES\n") != NULL);
    s->mesh.mesh_enabled = 1;
    s->connection.established = 0;
    vpn_mesh_status(s, buf, sizeof(buf));
    CHECK(strstr(buf, "Mesh Active: YES\nConnected: NO\n") != NULL);
    /* a short buffer stays terminated and the count is what was written */
    char small[40];
    CHECK(vpn_mesh_status(s, small, sizeof(small)) == 39);
    CHECK(strncmp(small, "=== VPN MESH MATRIX STATUS ===\nMesh Act", 39) == 0);

    /* path report */
    s->mesh.hops[0].options[4].latency_ms = 11;
    s->mesh.hops[1].options[3].latency_ms = 22;
    s->mesh.hops[1].options[3].keeps_logs = 1;
    s->mesh.hops[2].options[4].is_free = 0;
    vpn_set_node_trust(&s->mesh, 2, 4, -40);
    s->mesh.hops[4].options[3].latency_ms = 33;
    n = vpn_path_report(s, buf, sizeof(buf));
    e = snprintf(exp, sizeof(exp),
                 "=== VPN PATH REPORT ===\n"
                 "  Hop 0: Tor-Entry (tor-entry.zedec.onion:9001) [Tor] Trust:10 Free:YES "
                 "Logs:NO Latency:11ms\n"
                 "  Hop 1: P2P-Relay-02 (p2p-relay-02.zedec.mesh:443) [Garlic(PungentClove)] "
                 "Trust:10 Free:YES Logs:YES Latency:22ms\n"
                 "  Hop 2: SSH-Tunnel-01 (ssh-tunnel-01.zedec.mesh:22) [SSH-Tunnel] Trust:-40 "
                 "Free:NO Logs:NO Latency:0ms\n"
                 "  Hop 3: ExitRelay-PT (exit-pt.freevpn.zedec:51823) [WireGuard] Trust:10 "
                 "Free:YES Logs:NO Latency:0ms\n"
                 "  Hop 4: P2P-Exit-01 (p2p-exit-01.zedec.mesh:443) [PLNP] Trust:10 Free:YES "
                 "Logs:NO Latency:33ms\n"
                 "  Total Latency: 66ms\n"
                 "  Path Trust: 0\n");
    CHECK(strcmp(buf, exp) == 0);
    CHECK(n == e);
    /* exact-size heap buffers cut inside the hop lines and inside the
     * totals: output is a prefix, never an overrun */
    size_t cuts[3] = {40, 200, (size_t)e - 10};
    for (int i = 0; i < 3; i++) {
        char *t = malloc(cuts[i]);
        CHECK(vpn_path_report(s, t, (uint16_t)cuts[i]) == (int)cuts[i] - 1);
        CHECK(strncmp(t, exp, cuts[i] - 1) == 0 && t[cuts[i] - 1] == 0);
        free(t);
    }

    /* node report */
    vpn_node_t *nd = &s->mesh.hops[3].options[2];
    nd->bandwidth_kbps = 1000;
    nd->latency_ms = 12;
    nd->trust_score = -5;
    nd->keeps_logs = 1;
    n = vpn_node_report(&s->mesh, 3, 2, buf, sizeof(buf));
    e = snprintf(exp, sizeof(exp),
                 "Node: ExitRelay-NZ\n"
                 "Host: exit-nz.freevpn.zedec:51823\n"
                 "Protocol: WireGuard\n"
                 "Country: NZ\n"
                 "Free: YES | Active: YES\n"
                 "Trust: -5 | Latency: 12ms | BW: 1000kbps\n"
                 "Keeps Logs: YES\n");
    CHECK(strcmp(buf, exp) == 0);
    CHECK(n == e);
    vpn_node_report(&s->mesh, 0, 0, buf, sizeof(buf));
    CHECK(strncmp(buf, "Node: FreeGate-US\nHost: entry-us.freevpn.zedec:51820\n", 52) == 0);
    vpn_node_report(&s->mesh, 0, 4, buf, sizeof(buf));
    CHECK(strncmp(buf, "Node: Tor-Entry\n", 16) == 0);
    vpn_node_report(&s->mesh, 4, 0, buf, sizeof(buf));
    CHECK(strncmp(buf, "Node: Exit-PA\n", 14) == 0);
    vpn_remove_node(&s->mesh, 4, 4);
    n = vpn_node_report(&s->mesh, 4, 4, buf, sizeof(buf));
    e = snprintf(exp, sizeof(exp),
                 "Node: \nHost: :0\nProtocol: WireGuard\nCountry: \n"
                 "Free: NO | Active: NO\nTrust: 0 | Latency: 0ms | BW: 0kbps\nKeeps Logs: NO\n");
    CHECK(strcmp(buf, exp) == 0);
    CHECK(n == e);
    free(s);
}

static void test_path_math_names(void) {
    uint32_t cnt = 1;
    vpn_mesh_t m;
    memset(&m, 0, sizeof(m));
    uint8_t sel[5] = {0, 1, 2, 3, 4};
    CHECK(vpn_enumerate_paths(NULL, &cnt) == -1);
    CHECK(vpn_enumerate_paths(&m, NULL) == -1);
    m.total_paths = 3125;
    CHECK(vpn_enumerate_paths(&m, &cnt) == 0);
    CHECK(cnt == 3125);
    CHECK(vpn_path_latency(NULL, sel) == 0);
    CHECK(vpn_path_latency(&m, NULL) == 0);
    CHECK(vpn_path_trust(NULL, sel) == 0);
    CHECK(vpn_path_trust(&m, NULL) == 0);
    for (int h = 0; h < 5; h++)
        for (int o = 0; o < 5; o++) {
            m.hops[h].options[o].latency_ms = (uint32_t)(1000 * h + 10 * o + 1);
            m.hops[h].options[o].trust_score = (int8_t)(o == h ? 20 + h : -100);
        }
    CHECK(vpn_path_latency(&m, sel) == 1 + 1011 + 2021 + 3031 + 4041);
    CHECK(vpn_path_trust(&m, sel) == (20 + 21 + 22 + 23 + 24) / 5);
    uint8_t mix[5] = {0, 0, 2, 3, 4}; /* 20 - 100 + 22 + 23 + 24 = -11 -> -2 */
    CHECK(vpn_path_trust(&m, mix) == -2);
    uint8_t low[5] = {1, 0, 0, 0, 0}; /* -500 / 5 */
    CHECK(vpn_path_trust(&m, low) == -100);
    uint8_t six[5] = {0, 1, 2, 3, 0}; /* 20+21+22+23-100 = -14 -> -2 */
    CHECK(vpn_path_trust(&m, six) == -2);

    CHECK(strcmp(vpn_protocol_name(VPN_PROTO_WIREGUARD), "WireGuard") == 0);
    CHECK(strcmp(vpn_protocol_name(VPN_PROTO_OPENVPN), "OpenVPN") == 0);
    CHECK(strcmp(vpn_protocol_name(VPN_PROTO_IPSEC), "IPsec") == 0);
    CHECK(strcmp(vpn_protocol_name(VPN_PROTO_SSH_TUNNEL), "SSH-Tunnel") == 0);
    CHECK(strcmp(vpn_protocol_name(VPN_PROTO_TOR), "Tor") == 0);
    CHECK(strcmp(vpn_protocol_name(VPN_PROTO_I2P), "I2P") == 0);
    CHECK(strcmp(vpn_protocol_name(VPN_PROTO_GARLIC), "Garlic(PungentClove)") == 0);
    CHECK(strcmp(vpn_protocol_name(VPN_PROTO_PLNP), "PLNP") == 0);
    CHECK(strcmp(vpn_protocol_name((vpn_protocol_t)8), "Unknown") == 0);

    CHECK(vpn_protocol_from_name(NULL) == VPN_PROTO_WIREGUARD);
    CHECK(vpn_protocol_from_name("") == VPN_PROTO_WIREGUARD);
    CHECK(vpn_protocol_from_name("xyz") == VPN_PROTO_WIREGUARD);
    CHECK(vpn_protocol_from_name("wireguard") == VPN_PROTO_WIREGUARD);
    CHECK(vpn_protocol_from_name("WireGuard") == VPN_PROTO_WIREGUARD);
    CHECK(vpn_protocol_from_name("wireguard-over-tor") == VPN_PROTO_WIREGUARD);
    CHECK(vpn_protocol_from_name("Wire/Tor") == VPN_PROTO_WIREGUARD);
    CHECK(vpn_protocol_from_name("openvpn") == VPN_PROTO_OPENVPN);
    CHECK(vpn_protocol_from_name("OpenVPN") == VPN_PROTO_OPENVPN);
    CHECK(vpn_protocol_from_name("ipsec") == VPN_PROTO_IPSEC);
    CHECK(vpn_protocol_from_name("IPsec") == VPN_PROTO_IPSEC);
    CHECK(vpn_protocol_from_name("ssh") == VPN_PROTO_SSH_TUNNEL);
    CHECK(vpn_protocol_from_name("SSH") == VPN_PROTO_SSH_TUNNEL);
    CHECK(vpn_protocol_from_name("tor") == VPN_PROTO_TOR);
    CHECK(vpn_protocol_from_name("Tor") == VPN_PROTO_TOR);
    CHECK(vpn_protocol_from_name("i2p") == VPN_PROTO_I2P);
    CHECK(vpn_protocol_from_name("I2P") == VPN_PROTO_I2P);
    CHECK(vpn_protocol_from_name("garlic") == VPN_PROTO_GARLIC);
    CHECK(vpn_protocol_from_name("Garlic") == VPN_PROTO_GARLIC);
    CHECK(vpn_protocol_from_name("plnp") == VPN_PROTO_PLNP);
    CHECK(vpn_protocol_from_name("PLNP") == VPN_PROTO_PLNP);
}

/* Find row "H<h>|" and return which of its five cells start with ">>". The
 * cell text after the marker is not checked (see the report: the "%-10s"
 * width is not supported by fs_snprintf). */
static int marked_cells(const char *buf, int h, int *mask) {
    char key[24];
    snprintf(key, sizeof(key), "\nH%d|", h);
    const char *p = strstr(buf, key);
    if (!p) return -1;
    p += strlen(key);
    *mask = 0;
    for (int o = 0; o < 5; o++) {
        if (p[0] == '>' && p[1] == '>') *mask |= 1 << o;
        else if (!(p[0] == ' ' && p[1] == ' ')) return -1;
        p = strchr(p, '|');
        if (!p) return -1;
        p++;
    }
    return *p == '\n' ? 0 : -1;
}

static int count_sub(const char *s, const char *sub) {
    int n = 0;
    for (const char *p = strstr(s, sub); p; p = strstr(p + 1, sub)) n++;
    return n;
}

static void test_visualize(void) {
    static char buf[8192];
    vpn_state_t *s = new_state();
    vpn_load_defaults(s);
    uint8_t p[5] = {4, 3, 0, 1, 2};
    vpn_select_path(s, p);
    vpn_select_path(s, p);
    int n = vpn_mesh_visualize(s, buf, sizeof(buf));
    CHECK(n == (int)strlen(buf));
    const char *head = "=== VPN 25-GRID MATRIX VISUALIZATION ===\n\n"
                       "     Opt0         Opt1         Opt2         Opt3         Opt4\n"
                       "  +------------+------------+------------+------------+------------+\n"
                       "H0|";
    CHECK(strncmp(buf, head, strlen(head)) == 0);
    const char *tail = "  +------------+------------+------------+------------+------------+\n"
                       "\n  >> = active node in current circuit\n"
                       "  Total paths: 3125 (5^5 = 3125)\n"
                       "  Paths used: 2\n";
    size_t tl = strlen(tail);
    CHECK(strlen(buf) > tl && strcmp(buf + strlen(buf) - tl, tail) == 0);
    CHECK(count_sub(buf, ">>") == 6);
    CHECK(count_sub(buf, "+------------+------------+------------+------------+------------+\n") == 6);
    int mask = 0;
    for (int h = 0; h < 5; h++) {
        CHECK(marked_cells(buf, h, &mask) == 0);
        CHECK(mask == 1 << p[h]);
    }
    CHECK(strstr(buf, "\nH5|") == NULL);
    size_t vcuts[3] = {300, 600, (size_t)n - 5};
    for (int i = 0; i < 3; i++) {
        char *t = malloc(vcuts[i]);
        CHECK(vpn_mesh_visualize(s, t, (uint16_t)vcuts[i]) == (int)vcuts[i] - 1);
        CHECK(strncmp(t, buf, vcuts[i] - 1) == 0 && t[vcuts[i] - 1] == 0);
        free(t);
    }
    char small[32];
    CHECK(vpn_mesh_visualize(s, small, sizeof(small)) == 31);
    CHECK(strncmp(small, "=== VPN 25-GRID MATRIX VISUALIZ", 31) == 0);
    free(s);
}

static void test_bringup(void) {
    zxv_bringup_fn fn = find_bringup("panopticon_vpn");
    CHECK(fn != NULL);
    if (fn) CHECK(fn() == 0);
}

int main(void) {
    test_init_shutdown();
    test_nodes();
    test_defaults();
    test_select_path();
    test_auto_select();
    test_random_rotate_tick();
    test_connect();
    test_route_receive();
    test_reports();
    test_path_math_names();
    test_visualize();
    test_bringup();
    if (failures) {
        printf("%d of %d checks failed\n", failures, checks);
        return 1;
    }
    printf("%d checks passed\n", checks);
    return 0;
}
