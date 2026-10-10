/*
 * pungent.c — PungentClove layered-encryption bundling (see pungent.h for
 * exactly what is and is not implemented).
 *
 * Primitives: ChaCha20-Poly1305 (tls/aead.c), HKDF-SHA256 (tls/hkdf.c),
 * SHA3-256 (mlkem/keccak.c), ML-KEM-768 (mlkem/mlkem768.c). No libc.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

#include "pungent.h"
#include "../tls/aead.h"
#include "../tls/hkdf.h"
#include "../mlkem/keccak.h"

/* ===== Helpers ===== */

static void pc_memset(void *dst, int v, uint32_t n)
{
    volatile uint8_t *d = (volatile uint8_t *) dst;
    for (uint32_t i = 0; i < n; i++) d[i] = (uint8_t) v;
}

static void pc_memcpy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static void pc_memmove(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    if (dst < src) {
        for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
    } else {
        for (uint32_t i = n; i > 0; i--) dst[i - 1] = src[i - 1];
    }
}

static int pc_memcmp(const void *a, const void *b, uint32_t n)
{
    const uint8_t *pa = a, *pb = b;
    for (uint32_t i = 0; i < n; i++)
        if (pa[i] != pb[i]) return (int) pa[i] - (int) pb[i];
    return 0;
}

static uint32_t pc_strlen(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

/* Bounded copy: always NUL-terminates within cap. */
static void pc_strcpy(char *dst, const char *src, uint32_t cap)
{
    uint32_t i = 0;
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void pc_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
    p[2] = (uint8_t) (v >> 16);
    p[3] = (uint8_t) (v >> 24);
}

static uint32_t pc_get32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

/* Per-layer nonce: the base with the hop index XORed into its last 4 bytes. */
static void pc_layer_nonce(const uint8_t base[PC_NONCE_SIZE], uint32_t hop,
                           uint8_t out[PC_NONCE_SIZE])
{
    pc_memcpy(out, base, PC_NONCE_SIZE);
    out[8] ^= (uint8_t) hop;
    out[9] ^= (uint8_t) (hop >> 8);
    out[10] ^= (uint8_t) (hop >> 16);
    out[11] ^= (uint8_t) (hop >> 24);
}

/* Associated data for a layer: a domain tag plus the hop index, so a layer
 * cannot be replayed at another hop position. */
static void pc_layer_aad(uint32_t hop, uint8_t aad[8])
{
    aad[0] = 'P';
    aad[1] = 'C';
    aad[2] = 'L';
    aad[3] = '1';
    pc_put32(aad + 4, hop);
}

/* Associated data for a clove: dest_hash || tier || len. */
static void pc_clove_aad(const uint8_t dest[PC_HASH_SIZE], uint32_t tier, uint32_t len,
                         uint8_t aad[PC_HASH_SIZE + 8])
{
    pc_memcpy(aad, dest, PC_HASH_SIZE);
    pc_put32(aad + PC_HASH_SIZE, tier);
    pc_put32(aad + PC_HASH_SIZE + 4, len);
}

/* ===== Init ===== */

void pungent_init(pungent_t *pc)
{
    pc_memset(pc, 0, sizeof(pungent_t));
    pc->initialized = true;
    pc->default_hops = 3;
    pc->default_delay_ms = 250;
}

/* ===== Relay Management ===== */

int32_t pungent_relay_add(pungent_t *pc, const char *addr, uint32_t tier,
                          const uint8_t pubkey_hash[PC_HASH_SIZE])
{
    if (!pc || !addr || !pubkey_hash || pc->num_relays >= PC_MAX_RELAYS) return -1;
    int32_t idx = (int32_t) pc->num_relays;
    pc_relay_t *r = &pc->relays[idx];
    pc_memset(r, 0, sizeof(pc_relay_t));
    pc_strcpy(r->addr, addr, PC_MAX_ADDR);
    r->tier = tier;
    r->online = false;
    r->latency_ms = 100;
    pc_memcpy(r->pubkey, pubkey_hash, PC_HASH_SIZE);
    pc->num_relays++;
    return idx;
}

int pungent_relay_set_online(pungent_t *pc, uint32_t idx, bool online)
{
    if (idx >= pc->num_relays) return -1;
    pc->relays[idx].online = online;
    return 0;
}

uint32_t pungent_relays_by_tier(pungent_t *pc, uint32_t tier)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < pc->num_relays; i++)
        if (pc->relays[i].tier == tier && pc->relays[i].online) count++;
    return count;
}

/* ===== Clove keys (ML-KEM-768 + HKDF-SHA256) ===== */

static void pc_clove_kdf(const uint8_t ss[MLKEM768_SS_BYTES], const uint8_t dest[PC_HASH_SIZE],
                         uint8_t key[PC_SESSION_KEY_SIZE])
{
    static const uint8_t salt[] = "pungent/v1 clove key";
    uint8_t prk[HASH_LEN];
    hkdf_extract(salt, (uint32_t) (sizeof(salt) - 1), ss, MLKEM768_SS_BYTES, prk);
    (void) hkdf_expand(prk, dest, PC_HASH_SIZE, key, PC_SESSION_KEY_SIZE);
    pc_memset(prk, 0, sizeof(prk));
}

void pungent_clove_key_encaps(const uint8_t ek[MLKEM768_EK_BYTES], const uint8_t coins[32],
                              const uint8_t dest_hash[PC_HASH_SIZE], uint8_t ct[MLKEM768_CT_BYTES],
                              uint8_t key[PC_SESSION_KEY_SIZE])
{
    uint8_t ss[MLKEM768_SS_BYTES];
    mlkem768_encaps(ek, coins, ct, ss);
    pc_clove_kdf(ss, dest_hash, key);
    pc_memset(ss, 0, sizeof(ss));
}

void pungent_clove_key_decaps(const uint8_t dk[MLKEM768_DK_BYTES],
                              const uint8_t ct[MLKEM768_CT_BYTES],
                              const uint8_t dest_hash[PC_HASH_SIZE],
                              uint8_t key[PC_SESSION_KEY_SIZE])
{
    uint8_t ss[MLKEM768_SS_BYTES];
    mlkem768_decaps(dk, ct, ss);
    pc_clove_kdf(ss, dest_hash, key);
    pc_memset(ss, 0, sizeof(ss));
}

/* ===== Generic layering ===== */

int32_t pungent_layers_seal(const uint8_t keys[][PC_SESSION_KEY_SIZE], uint32_t n,
                            const uint8_t nonce_base[PC_NONCE_SIZE], uint8_t *buf, uint32_t len,
                            uint32_t cap)
{
    if (!keys || !nonce_base || !buf || n == 0 || n > PC_MAX_HOPS) return -1;
    if (len > cap || cap - len < n * PC_TAG_SIZE) return -1;
    /* Innermost layer belongs to the last hop, outermost to hop 0. */
    for (uint32_t k = n; k > 0; k--) {
        uint32_t hop = k - 1;
        uint8_t nonce[PC_NONCE_SIZE], aad[8];
        pc_layer_nonce(nonce_base, hop, nonce);
        pc_layer_aad(hop, aad);
        aead_seal(keys[hop], nonce, aad, sizeof(aad), buf, buf, len, buf + len);
        len += PC_TAG_SIZE;
    }
    return (int32_t) len;
}

int32_t pungent_layer_peel(const uint8_t key[PC_SESSION_KEY_SIZE],
                           const uint8_t nonce_base[PC_NONCE_SIZE], uint32_t hop, uint8_t *buf,
                           uint32_t len)
{
    if (!key || !nonce_base || !buf || len < PC_TAG_SIZE) return -1;
    uint8_t nonce[PC_NONCE_SIZE], aad[8], tag[PC_TAG_SIZE];
    uint32_t body = len - PC_TAG_SIZE;
    pc_layer_nonce(nonce_base, hop, nonce);
    pc_layer_aad(hop, aad);
    pc_memcpy(tag, buf + body, PC_TAG_SIZE);
    if (!aead_open(key, nonce, aad, sizeof(aad), buf, buf, body, tag)) {
        pc_memset(buf, 0, len);
        return -1;
    }
    pc_memset(buf + body, 0, PC_TAG_SIZE);
    return (int32_t) body;
}

/* ===== Bulb Operations ===== */

int pungent_bulb_init(pc_bulb_t *b, uint32_t hops, uint32_t delay_ms)
{
    if (!b) return -1;
    pc_memset(b, 0, sizeof(pc_bulb_t));
    b->hop_count = hops > PC_MAX_HOPS ? PC_MAX_HOPS : hops;
    b->delay_ms = delay_ms;
    return 0;
}

int pungent_bulb_add_clove(pc_bulb_t *b, const uint8_t *payload, uint32_t len,
                           const uint8_t *dest_hash, uint32_t tier,
                           const uint8_t key[PC_SESSION_KEY_SIZE],
                           const uint8_t nonce[PC_NONCE_SIZE])
{
    if (!b || !dest_hash || !key || !nonce || (len > 0 && !payload)) return -1;
    if (b->sealed || b->num_cloves >= PC_MAX_CLOVES) return -1;
    if (len > PC_CLOVE_MAX_PAYLOAD) return -1;

    pc_clove_t *c = &b->cloves[b->num_cloves];
    pc_memset(c, 0, sizeof(*c));
    pc_memcpy(c->dest_hash, dest_hash, PC_HASH_SIZE);
    pc_memcpy(c->nonce, nonce, PC_NONCE_SIZE);
    c->payload_len = len;
    c->tier = tier;

    uint8_t aad[PC_HASH_SIZE + 8];
    pc_clove_aad(c->dest_hash, tier, len, aad);
    aead_seal(key, c->nonce, aad, sizeof(aad), payload, c->payload, len, c->tag);
    c->encrypted = true;

    b->num_cloves++;
    return (int) (b->num_cloves - 1);
}

int pungent_clove_open(const pc_clove_t *c, const uint8_t key[PC_SESSION_KEY_SIZE], uint8_t *out,
                       uint32_t cap)
{
    if (!c || !key || !out || !c->encrypted) return -1;
    if (c->payload_len > PC_CLOVE_MAX_PAYLOAD || c->payload_len > cap) return -1;
    uint8_t aad[PC_HASH_SIZE + 8];
    pc_clove_aad(c->dest_hash, c->tier, c->payload_len, aad);
    if (!aead_open(key, c->nonce, aad, sizeof(aad), c->payload, out, c->payload_len, c->tag))
        return -1;
    return (int) c->payload_len;
}

static void pc_bulb_discard(pc_bulb_t *b)
{
    pc_memset(b->wire, 0, sizeof(b->wire));
    pc_memset(b->cloves, 0, sizeof(b->cloves));
    b->wire_len = 0;
    b->layers = 0;
    b->num_cloves = 0;
    b->sealed = false;
}

int pungent_bulb_seal(pc_bulb_t *b, const uint8_t hop_keys[][PC_SESSION_KEY_SIZE],
                      const uint8_t nonce_base[PC_NONCE_SIZE])
{
    if (!b || !hop_keys || !nonce_base || b->sealed) return -1;
    if (b->num_cloves == 0 || b->hop_count == 0) return -1;

    uint32_t pos = 0;
    pc_put32(b->wire, b->num_cloves);
    pos += 4;
    for (uint32_t i = 0; i < b->num_cloves; i++) {
        const pc_clove_t *c = &b->cloves[i];
        if (!c->encrypted) return -1; /* never ship a plaintext clove */
        pc_memcpy(b->wire + pos, c->dest_hash, PC_HASH_SIZE);
        pos += PC_HASH_SIZE;
        pc_put32(b->wire + pos, c->tier);
        pos += 4;
        pc_put32(b->wire + pos, c->payload_len);
        pos += 4;
        pc_memcpy(b->wire + pos, c->nonce, PC_NONCE_SIZE);
        pos += PC_NONCE_SIZE;
        pc_memcpy(b->wire + pos, c->tag, PC_TAG_SIZE);
        pos += PC_TAG_SIZE;
        pc_memcpy(b->wire + pos, c->payload, c->payload_len);
        pos += c->payload_len;
    }

    int32_t n = pungent_layers_seal(hop_keys, b->hop_count, nonce_base, b->wire, pos,
                                    (uint32_t) sizeof(b->wire));
    if (n < 0) {
        pc_bulb_discard(b);
        return -1;
    }
    pc_memcpy(b->nonce_base, nonce_base, PC_NONCE_SIZE);
    b->wire_len = (uint32_t) n;
    b->layers = b->hop_count;
    /* Only the layered wire form remains while sealed. */
    pc_memset(b->cloves, 0, sizeof(b->cloves));
    b->sealed = true;
    return 0;
}

static int pc_bulb_parse(pc_bulb_t *b)
{
    uint32_t pos = 0, len = b->wire_len;
    if (len < 4) return -1;
    uint32_t n = pc_get32(b->wire);
    pos += 4;
    if (n == 0 || n > PC_MAX_CLOVES) return -1;
    for (uint32_t i = 0; i < n; i++) {
        pc_clove_t *c = &b->cloves[i];
        if (len - pos < PC_CLOVE_WIRE_HDR) return -1;
        pc_memcpy(c->dest_hash, b->wire + pos, PC_HASH_SIZE);
        pos += PC_HASH_SIZE;
        c->tier = pc_get32(b->wire + pos);
        pos += 4;
        c->payload_len = pc_get32(b->wire + pos);
        pos += 4;
        pc_memcpy(c->nonce, b->wire + pos, PC_NONCE_SIZE);
        pos += PC_NONCE_SIZE;
        pc_memcpy(c->tag, b->wire + pos, PC_TAG_SIZE);
        pos += PC_TAG_SIZE;
        if (c->payload_len > PC_CLOVE_MAX_PAYLOAD || len - pos < c->payload_len) return -1;
        pc_memcpy(c->payload, b->wire + pos, c->payload_len);
        pos += c->payload_len;
        c->encrypted = true;
    }
    if (pos != len) return -1;
    b->num_cloves = n;
    return 0;
}

int pungent_bulb_unseal(pc_bulb_t *b, uint32_t hop_idx, const uint8_t key[PC_SESSION_KEY_SIZE])
{
    if (!b || !key || !b->sealed || b->layers == 0) return -1;
    /* Layers come off in hop order: hop 0 first. */
    if (hop_idx != b->hop_count - b->layers) {
        pc_bulb_discard(b);
        return -1;
    }
    int32_t n = pungent_layer_peel(key, b->nonce_base, hop_idx, b->wire, b->wire_len);
    if (n < 0) {
        pc_bulb_discard(b);
        return -1;
    }
    b->wire_len = (uint32_t) n;
    b->layers--;
    if (b->layers == 0) {
        if (pc_bulb_parse(b) != 0) {
            pc_bulb_discard(b);
            return -1;
        }
        b->sealed = false;
    }
    return 0;
}

uint32_t pungent_bulb_size(pc_bulb_t *b)
{
    if (!b) return 0;
    if (b->sealed) return b->wire_len;
    uint32_t total = 0;
    for (uint32_t i = 0; i < b->num_cloves; i++) total += b->cloves[i].payload_len;
    return total;
}

/* ===== Tunnel Management ===== */

int32_t pungent_tunnel_create(pungent_t *pc, pc_tunnel_dir_t dir, uint32_t tier, uint32_t hop_count)
{
    if (!pc || pc->num_tunnels >= PC_MAX_TUNNELS || hop_count == 0) return -1;
    if (hop_count > PC_MAX_HOPS) hop_count = PC_MAX_HOPS;

    int32_t idx = (int32_t) pc->num_tunnels;
    pc_tunnel_t *t = &pc->tunnels[idx];
    pc_memset(t, 0, sizeof(pc_tunnel_t));
    t->id = (uint32_t) idx;
    t->direction = dir;
    t->hop_count = hop_count;
    t->tier = tier;
    t->active = false;

    pc->num_tunnels++;
    return idx;
}

int pungent_tunnel_add_hop(pungent_t *pc, uint32_t tunnel_idx, uint32_t relay_idx,
                           const uint8_t key[PC_SESSION_KEY_SIZE])
{
    if (!pc || !key || tunnel_idx >= pc->num_tunnels || relay_idx >= pc->num_relays) return -1;
    pc_tunnel_t *t = &pc->tunnels[tunnel_idx];
    if (t->active || t->num_hops >= t->hop_count) return -1;
    t->relay_indices[t->num_hops] = relay_idx;
    pc_memcpy(t->session_keys[t->num_hops], key, PC_SESSION_KEY_SIZE);
    t->num_hops++;
    return 0;
}

int pungent_tunnel_activate(pungent_t *pc, uint32_t tunnel_idx)
{
    if (!pc || tunnel_idx >= pc->num_tunnels) return -1;
    pc_tunnel_t *t = &pc->tunnels[tunnel_idx];
    if (t->num_hops != t->hop_count) return -1; /* every hop needs a key */
    t->active = true;
    return 0;
}

int pungent_tunnel_close(pungent_t *pc, uint32_t tunnel_idx)
{
    if (!pc || tunnel_idx >= pc->num_tunnels) return -1;
    pc_tunnel_t *t = &pc->tunnels[tunnel_idx];
    t->active = false;
    pc_memset(t->session_keys, 0, sizeof(t->session_keys));
    t->num_hops = 0;
    return 0;
}

int pungent_tunnel_send(pungent_t *pc, uint32_t tunnel_idx, const uint8_t *data, uint32_t len,
                        const uint8_t nonce_base[PC_NONCE_SIZE], uint8_t *out, uint32_t cap)
{
    if (!pc || !out || !nonce_base || (len > 0 && !data)) return -1;
    if (tunnel_idx >= pc->num_tunnels || !pc->tunnels[tunnel_idx].active) return -1;
    pc_tunnel_t *t = &pc->tunnels[tunnel_idx];
    if (len > cap) return -1;
    pc_memmove(out, data, len);
    int32_t n = pungent_layers_seal((const uint8_t(*)[PC_SESSION_KEY_SIZE]) t->session_keys,
                                    t->num_hops, nonce_base, out, len, cap);
    if (n < 0) return -1;
    t->bytes_sent += len;
    t->bulbs_passed++;
    pc->total_bytes_routed += len;
    return (int) n;
}

/* ===== Hidden Services ===== */

int32_t pungent_hidden_service_create(pungent_t *pc, const char *label, uint32_t local_port,
                                      uint32_t tunnel_idx, const uint8_t *pubkey,
                                      uint32_t pubkey_len)
{
    if (!pc || !label || !pubkey || pubkey_len == 0) return -1;
    if (pc->num_services >= PC_MAX_HIDDEN_SVC || tunnel_idx >= pc->num_tunnels) return -1;

    int32_t idx = (int32_t) pc->num_services;
    pc_hidden_service_t *svc = &pc->services[idx];
    pc_memset(svc, 0, sizeof(pc_hidden_service_t));
    pc_strcpy(svc->label, label, PC_MAX_LABEL);
    svc->local_port = local_port;
    svc->tunnel_idx = tunnel_idx;
    svc->active = true;
    sha3_256(pubkey, pubkey_len, svc->service_key);

    pc->num_services++;
    return idx;
}

const pc_hidden_service_t *pungent_hidden_service_find(pungent_t *pc, const uint8_t *key)
{
    if (!pc || !key) return 0;
    for (uint32_t i = 0; i < pc->num_services; i++) {
        if (pc->services[i].active &&
            pc_memcmp(pc->services[i].service_key, key, PC_HASH_SIZE) == 0)
            return &pc->services[i];
    }
    return 0;
}

/* ===== SOCKS5-style connection table ===== */

int32_t pungent_socks5_connect(pungent_t *pc, const char *dest, uint16_t port, uint32_t tunnel_idx)
{
    if (!pc || !dest || pc->num_socks5 >= PC_SOCKS5_MAX_CONNS) return -1;
    if (tunnel_idx >= pc->num_tunnels) return -1;

    int32_t idx = (int32_t) pc->num_socks5;
    pc_socks5_conn_t *c = &pc->socks5[idx];
    pc_memset(c, 0, sizeof(pc_socks5_conn_t));
    c->id = (uint32_t) idx;
    pc_strcpy(c->dest_addr, dest, PC_MAX_ADDR);
    c->dest_port = port;
    c->tunnel_idx = tunnel_idx;
    c->active = true;

    pc->num_socks5++;
    return idx;
}

int pungent_socks5_close(pungent_t *pc, uint32_t conn_idx)
{
    if (!pc || conn_idx >= pc->num_socks5) return -1;
    pc->socks5[conn_idx].active = false;
    return 0;
}

/* ===== Path Selection ===== */

int pungent_select_path(pungent_t *pc, uint32_t tier, uint32_t hop_count, uint32_t *out_relays)
{
    if (!pc || !out_relays || hop_count == 0 || hop_count > PC_MAX_HOPS) return -1;

    uint32_t candidates[PC_MAX_RELAYS];
    uint32_t num = 0;
    for (uint32_t i = 0; i < pc->num_relays; i++)
        if (pc->relays[i].online && pc->relays[i].tier == tier) candidates[num++] = i;
    for (uint32_t i = 0; i < pc->num_relays && num < hop_count; i++)
        if (pc->relays[i].online && pc->relays[i].tier != tier) candidates[num++] = i;
    if (num < hop_count) return -1;

    /* Deterministic rotation; every chosen relay is distinct. */
    uint32_t start = (pc_strlen(pc->relays[candidates[0]].addr) + hop_count) % num;
    for (uint32_t i = 0; i < hop_count; i++) out_relays[i] = candidates[(start + i) % num];
    return (int) hop_count;
}

/* ===== Utility ===== */

const char *pungent_tier_name(pc_tier_t tier)
{
    switch (tier) {
    case PC_TIER_SURFACE:
        return "Tier 1: Surface";
    case PC_TIER_DEEP:
        return "Tier 2: Authenticated Deep";
    case PC_TIER_ONION:
        return "Tier 3: Onion-style layering (label)";
    case PC_TIER_GARLIC:
        return "Tier 4: Garlic-style bundling (label)";
    case PC_TIER_DARK:
        return "Tier 5: Dark Storage";
    case PC_TIER_P2P:
        return "Tier 6: P2P Mesh";
    case PC_TIER_RADIO:
        return "Tier 7: Radio Mesh";
    case PC_TIER_SHADOW:
        return "Tier 8: Shadow";
    default:
        return "Unknown";
    }
}

const char *pungent_tunnel_dir_name(pc_tunnel_dir_t dir)
{
    return dir == PC_TUNNEL_INBOUND ? "INBOUND" : "OUTBOUND";
}
