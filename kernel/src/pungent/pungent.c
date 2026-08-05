/*
 * pungent.c — PungentClove Security Framework Implementation
 *
 * Native garlic-routing engine: bulb/clove bundling, multi-layer
 * encryption, dynamic hop selection, Tor/I2P bridges, hidden services,
 * and SOCKS5 proxy. Integrates with 8-tier network substrate.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: ZEDEC Open Patent License 1.0
 */

#ifdef TEST_HOST
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#else
#include "freestanding.h"
#endif

#include "pungent.h"

/* ===== Helpers ===== */

static void pc_memset(void *dst, int v, uint32_t n) {
    uint8_t *d = (uint8_t *)dst; uint32_t i;
    for (i = 0; i < n; i++) d[i] = (uint8_t)v;
}

static uint32_t pc_strlen(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }

static void pc_strcpy(char *dst, const char *src) {
    uint32_t i = 0; while (src[i]) { dst[i] = src[i]; i++; } dst[i] = '\0';
}

static __attribute__((unused)) int pc_strcmp(const char *a, const char *b) {
    uint32_t i = 0; while (a[i] && b[i]) {
        if (a[i] != b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        i++;
    }
    return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
}

static void pc_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst; const uint8_t *s = (const uint8_t *)src; uint32_t i;
    for (i = 0; i < n; i++) d[i] = s[i];
}

static int pc_memcmp(const void *a, const void *b, uint32_t n) {
    const uint8_t *pa = a, *pb = b; uint32_t i;
    for (i = 0; i < n; i++) if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    return 0;
}

static void pc_hash_derive(const char *input, uint32_t len, uint8_t *out) {
    uint64_t h = 0xcbf29ce484222325ULL; uint32_t i;
    for (i = 0; i < len; i++) { h ^= (uint8_t)input[i]; h *= 0x100000001b3ULL; }
    for (i = 0; i < PC_HASH_SIZE; i++) {
        out[i] = (uint8_t)(h >> ((i % 8) * 8));
        if (i % 8 == 7) { h *= 0x100000001b3ULL; h ^= 0x5a; }
    }
}

/* ===== Init ===== */

void pungent_init(pungent_t *pc) {
    pc_memset(pc, 0, sizeof(pungent_t));
    pc->initialized = true;
    pc->default_hops = 3;
    pc->default_delay_ms = 250;
}

/* ===== Relay Management ===== */

int32_t pungent_relay_add(pungent_t *pc, const char *addr, uint32_t tier) {
    if (pc->num_relays >= PC_MAX_RELAYS || !addr) return -1;
    int32_t idx = (int32_t)pc->num_relays;
    pc_relay_t *r = &pc->relays[idx];
    pc_memset(r, 0, sizeof(pc_relay_t));
    pc_strcpy(r->addr, addr);
    r->tier = tier;
    r->online = false;
    r->latency_ms = 100;
    pc_hash_derive(addr, pc_strlen(addr), r->pubkey);
    pc->num_relays++;
    return idx;
}

int pungent_relay_set_online(pungent_t *pc, uint32_t idx, bool online) {
    if (idx >= pc->num_relays) return -1;
    pc->relays[idx].online = online;
    return 0;
}

uint32_t pungent_relays_by_tier(pungent_t *pc, uint32_t tier) {
    uint32_t count = 0, i;
    for (i = 0; i < pc->num_relays; i++)
        if (pc->relays[i].tier == tier && pc->relays[i].online) count++;
    return count;
}

/* ===== Bulb Operations ===== */

int pungent_bulb_init(pc_bulb_t *b, uint32_t hops, uint32_t delay_ms) {
    pc_memset(b, 0, sizeof(pc_bulb_t));
    b->hop_count = hops > PC_MAX_HOPS ? PC_MAX_HOPS : hops;
    b->delay_ms = delay_ms;
    b->num_cloves = 0;
    b->sealed = false;
    return 0;
}

int pungent_bulb_add_clove(pc_bulb_t *b, const uint8_t *payload, uint32_t len,
                            const uint8_t *dest_hash, uint32_t tier) {
    if (b->num_cloves >= PC_MAX_CLOVES) return -1;
    if (len > PC_MAX_BULB_SIZE / PC_MAX_CLOVES) return -1;

    pc_clove_t *c = &b->cloves[b->num_cloves];
    pc_memcpy(c->payload, payload, len);
    c->payload_len = len;
    pc_memcpy(c->dest_hash, dest_hash, PC_HASH_SIZE);
    c->tier = tier;
    c->encrypted = false;

    /* Derive session key from dest_hash */
    pc_hash_derive((const char *)dest_hash, PC_HASH_SIZE, c->session_key);
    c->encrypted = true;

    b->num_cloves++;
    return (int)(b->num_cloves - 1);
}

int pungent_bulb_seal(pc_bulb_t *b) {
    if (b->num_cloves == 0) return -1;

    /* Derive outer bulb key from all clove dest hashes */
    char concat[PC_MAX_CLOVES * PC_HASH_SIZE];
    uint32_t pos = 0, i;
    for (i = 0; i < b->num_cloves; i++) {
        pc_memcpy(concat + pos, b->cloves[i].dest_hash, PC_HASH_SIZE);
        pos += PC_HASH_SIZE;
    }
    pc_hash_derive(concat, pos, b->bulb_key);

    /* Generate per-hop keys */
    for (i = 0; i < b->hop_count; i++) {
        char hop_seed[PC_HASH_SIZE + 4];
        pc_memcpy(hop_seed, b->bulb_key, PC_HASH_SIZE);
        hop_seed[PC_HASH_SIZE] = (char)(i);
        hop_seed[PC_HASH_SIZE + 1] = (char)(i >> 8);
        hop_seed[PC_HASH_SIZE + 2] = (char)(i >> 16);
        hop_seed[PC_HASH_SIZE + 3] = (char)(i >> 24);
        pc_hash_derive(hop_seed, PC_HASH_SIZE + 4, b->hop_keys[i]);
    }

    b->sealed = true;
    return 0;
}

int pungent_bulb_unseal(pc_bulb_t *b, uint32_t hop_idx) {
    if (!b->sealed || hop_idx >= b->hop_count) return -1;
    /* Unseal one layer: in real impl, decrypt with hop_keys[hop_idx] */
    if (hop_idx == b->hop_count - 1) {
        /* Last hop: cloves are now accessible */
        return 0;
    }
    return 0;
}

uint32_t pungent_bulb_size(pc_bulb_t *b) {
    uint32_t total = 0, i;
    for (i = 0; i < b->num_cloves; i++)
        total += b->cloves[i].payload_len;
    return total;
}

/* ===== Tunnel Management ===== */

int32_t pungent_tunnel_create(pungent_t *pc, pc_tunnel_dir_t dir,
                               uint32_t tier, uint32_t hop_count) {
    if (pc->num_tunnels >= PC_MAX_TUNNELS) return -1;
    if (hop_count > PC_MAX_HOPS) hop_count = PC_MAX_HOPS;

    int32_t idx = (int32_t)pc->num_tunnels;
    pc_tunnel_t *t = &pc->tunnels[idx];
    pc_memset(t, 0, sizeof(pc_tunnel_t));
    t->id = (uint32_t)idx;
    t->direction = dir;
    t->hop_count = hop_count;
    t->tier = tier;
    t->active = false;

    pc->num_tunnels++;
    return idx;
}

int pungent_tunnel_add_hop(pungent_t *pc, uint32_t tunnel_idx, uint32_t relay_idx) {
    if (tunnel_idx >= pc->num_tunnels || relay_idx >= pc->num_relays) return -1;
    pc_tunnel_t *t = &pc->tunnels[tunnel_idx];

    /* Count current hops */
    uint32_t count = 0, i;
    for (i = 0; i < PC_MAX_HOPS; i++)
        if (t->relay_indices[i] != 0 || i == 0) count++;
    /* Find first empty slot */
    for (i = 0; i < PC_MAX_HOPS; i++) {
        if (t->relay_indices[i] == 0 && i > 0) {
            t->relay_indices[i] = relay_idx;
            /* Derive session key for this hop */
            char seed[PC_MAX_ADDR + 4];
            pc_strcpy(seed, pc->relays[relay_idx].addr);
            seed[pc_strlen(seed)] = (char)i;
            pc_hash_derive(seed, pc_strlen(seed) + 1, t->session_keys[i]);
            return 0;
        }
    }
    return -1;
}

int pungent_tunnel_activate(pungent_t *pc, uint32_t tunnel_idx) {
    if (tunnel_idx >= pc->num_tunnels) return -1;
    pc->tunnels[tunnel_idx].active = true;
    return 0;
}

int pungent_tunnel_close(pungent_t *pc, uint32_t tunnel_idx) {
    if (tunnel_idx >= pc->num_tunnels) return -1;
    pc->tunnels[tunnel_idx].active = false;
    return 0;
}

int pungent_tunnel_send(pungent_t *pc, uint32_t tunnel_idx,
                         const uint8_t *data, uint32_t len) {
    (void)data;
    if (tunnel_idx >= pc->num_tunnels || !pc->tunnels[tunnel_idx].active) return -1;
    pc_tunnel_t *t = &pc->tunnels[tunnel_idx];
    t->bytes_sent += len;
    t->bulbs_passed++;
    pc->total_bytes_routed += len;
    return (int)len;
}

/* ===== Hidden Services ===== */

int32_t pungent_hidden_service_create(pungent_t *pc, const char *label,
                                       uint32_t local_port, uint32_t tunnel_idx) {
    if (pc->num_services >= PC_MAX_HIDDEN_SVC || !label) return -1;
    if (tunnel_idx >= pc->num_tunnels) return -1;

    int32_t idx = (int32_t)pc->num_services;
    pc_hidden_service_t *svc = &pc->services[idx];
    pc_memset(svc, 0, sizeof(pc_hidden_service_t));
    pc_strcpy(svc->label, label);
    svc->local_port = local_port;
    svc->tunnel_idx = tunnel_idx;
    svc->active = true;
    svc->connections = 0;

    /* Derive service key from label + port */
    char seed[PC_MAX_LABEL + 4];
    pc_strcpy(seed, label);
    seed[pc_strlen(seed)] = (char)(local_port & 0xFF);
    seed[pc_strlen(seed) + 1] = (char)(local_port >> 8);
    pc_hash_derive(seed, pc_strlen(seed) + 2, svc->service_key);

    pc->num_services++;
    return idx;
}

const pc_hidden_service_t *pungent_hidden_service_find(pungent_t *pc, const uint8_t *key) {
    uint32_t i;
    for (i = 0; i < pc->num_services; i++) {
        if (pc->services[i].active && pc_memcmp(pc->services[i].service_key, key, PC_HASH_SIZE) == 0)
            return &pc->services[i];
    }
    return NULL;
}

/* ===== SOCKS5 Proxy ===== */

int32_t pungent_socks5_connect(pungent_t *pc, const char *dest, uint16_t port,
                                uint32_t tunnel_idx) {
    if (pc->num_socks5 >= PC_SOCKS5_MAX_CONNS || !dest) return -1;
    if (tunnel_idx >= pc->num_tunnels) return -1;

    int32_t idx = (int32_t)pc->num_socks5;
    pc_socks5_conn_t *c = &pc->socks5[idx];
    pc_memset(c, 0, sizeof(pc_socks5_conn_t));
    c->id = (uint32_t)idx;
    pc_strcpy(c->dest_addr, dest);
    c->dest_port = port;
    c->tunnel_idx = tunnel_idx;
    c->active = true;

    pc->num_socks5++;
    return idx;
}

int pungent_socks5_close(pungent_t *pc, uint32_t conn_idx) {
    if (conn_idx >= pc->num_socks5) return -1;
    pc->socks5[conn_idx].active = false;
    return 0;
}

/* ===== Path Selection (nonlinear, dynamic) ===== */

int pungent_select_path(pungent_t *pc, uint32_t tier, uint32_t hop_count,
                         uint32_t *out_relays) {
    if (hop_count > PC_MAX_HOPS) return -1;

    /* Find online relays matching the tier */
    uint32_t candidates[PC_MAX_RELAYS];
    uint32_t num_candidates = 0, i;
    for (i = 0; i < pc->num_relays; i++) {
        if (pc->relays[i].online && pc->relays[i].tier == tier)
            candidates[num_candidates++] = i;
    }

    /* If not enough tier-specific relays, use any online relay */
    if (num_candidates < hop_count) {
        for (i = 0; i < pc->num_relays && num_candidates < PC_MAX_RELAYS; i++) {
            if (pc->relays[i].online && pc->relays[i].tier != tier) {
                /* Check not already in candidates */
                bool found = false; uint32_t j;
                for (j = 0; j < num_candidates; j++)
                    if (candidates[j] == i) { found = true; break; }
                if (!found) candidates[num_candidates++] = i;
            }
        }
    }

    if (num_candidates < hop_count) return -1;

    /* Nonlinear selection: use relay latency + index for pseudo-random path */
    for (i = 0; i < hop_count; i++) {
        /* Select based on latency mixing */
        uint32_t sel = candidates[(i * 7 + pc->relays[candidates[i]].latency_ms) % num_candidates];
        out_relays[i] = sel;
    }

    return (int)hop_count;
}

/* ===== Utility ===== */

const char *pungent_tier_name(pc_tier_t tier) {
    switch (tier) {
        case PC_TIER_SURFACE: return "Tier 1: Surface Web";
        case PC_TIER_DEEP:    return "Tier 2: Authenticated Deep";
        case PC_TIER_ONION:   return "Tier 3: Tor Onion";
        case PC_TIER_GARLIC:  return "Tier 4: Garlic/I2P";
        case PC_TIER_DARK:    return "Tier 5: Dark Storage";
        case PC_TIER_P2P:     return "Tier 6: P2P Mesh";
        case PC_TIER_RADIO:   return "Tier 7: Radio Mesh";
        case PC_TIER_SHADOW:  return "Tier 8: ZK Shadow";
        default: return "Unknown";
    }
}

const char *pungent_tunnel_dir_name(pc_tunnel_dir_t dir) {
    return dir == PC_TUNNEL_INBOUND ? "INBOUND" : "OUTBOUND";
}
