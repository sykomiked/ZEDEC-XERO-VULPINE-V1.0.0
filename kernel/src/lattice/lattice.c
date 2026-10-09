/*
 * lattice.c — LATTICE-P2P File Sharing Implementation
 *
 * Content-addressed P2P distribution with phase-aware seeding,
 * DHT routing, and 8-tier network integration.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

#ifdef TEST_HOST
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#else
#include "freestanding.h"
#endif

#include "lattice.h"

/* ===== Helpers ===== */

static void lat_memset(void *dst, int v, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    uint32_t i;
    for (i = 0; i < n; i++) d[i] = (uint8_t)v;
}

static uint32_t lat_strlen(const char *s) {
    uint32_t n = 0; while (s[n]) n++; return n;
}

static void lat_strcpy(char *dst, const char *src) {
    uint32_t i = 0; while (src[i]) { dst[i] = src[i]; i++; } dst[i] = '\0';
}

static int lat_strcmp(const char *a, const char *b) {
    uint32_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        i++;
    }
    return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
}

static int lat_memcmp(const void *a, const void *b, uint32_t n) {
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    uint32_t i;
    for (i = 0; i < n; i++)
        if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    return 0;
}

static void lat_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    uint32_t i;
    for (i = 0; i < n; i++) d[i] = s[i];
}

/* ===== Init ===== */

void lattice_init(lattice_t *l) {
    lat_memset(l, 0, sizeof(lattice_t));
    l->initialized = true;
}

/* ===== Peer Management ===== */

int32_t lattice_peer_add(lattice_t *l, const char *addr, uint32_t tier) {
    if (l->num_peers >= LATTICE_MAX_PEERS || !addr)
        return -1;

    /* Check for duplicate */
    if (lattice_peer_find(l, addr))
        return -1;

    int32_t idx = (int32_t)l->num_peers;
    lattice_peer_t *p = &l->peers[idx];
    lat_memset(p, 0, sizeof(lattice_peer_t));
    lat_strcpy(p->addr, addr);
    p->tier = tier;
    p->connected = false;
    p->chunks_served = 0;
    p->chunks_downloaded = 0;
    p->last_seen = 0;

    /* Derive pubkey hash from address */
    uint32_t i;
    uint64_t h = 0xcbf29ce484222325ULL;
    for (i = 0; i < lat_strlen(addr); i++) {
        h ^= (uint8_t)addr[i];
        h *= 0x100000001b3ULL;
    }
    for (i = 0; i < LATTICE_HASH_SIZE; i++) {
        p->pubkey[i] = (uint8_t)(h >> ((i % 8) * 8));
        if (i % 8 == 7) { h *= 0x100000001b3ULL; h ^= 0x5a; }
    }

    l->num_peers++;
    return idx;
}

int lattice_peer_remove(lattice_t *l, uint32_t idx) {
    if (idx >= l->num_peers) return -1;
    /* Shift down */
    uint32_t i;
    for (i = idx; i < l->num_peers - 1; i++)
        lat_memcpy(&l->peers[i], &l->peers[i + 1], sizeof(lattice_peer_t));
    l->num_peers--;
    return 0;
}

lattice_peer_t *lattice_peer_find(lattice_t *l, const char *addr) {
    uint32_t i;
    for (i = 0; i < l->num_peers; i++) {
        if (lat_strcmp(l->peers[i].addr, addr) == 0)
            return &l->peers[i];
    }
    return NULL;
}

int lattice_peer_connect(lattice_t *l, uint32_t idx) {
    if (idx >= l->num_peers) return -1;
    l->peers[idx].connected = true;
    return 0;
}

uint32_t lattice_peers_by_tier(lattice_t *l, uint32_t tier) {
    uint32_t count = 0, i;
    for (i = 0; i < l->num_peers; i++)
        if (l->peers[i].tier == tier && l->peers[i].connected)
            count++;
    return count;
}

/* ===== Swarm Management ===== */

int32_t lattice_swarm_create(lattice_t *l, const char *label,
                              lattice_phase_t phase, lattice_tier_t tier) {
    if (l->num_swarms >= LATTICE_MAX_SWARMS || !label)
        return -1;

    int32_t idx = (int32_t)l->num_swarms;
    lattice_swarm_t *s = &l->swarms[idx];
    lat_memset(s, 0, sizeof(lattice_swarm_t));
    lat_strcpy(s->label, label);
    s->phase = phase;
    s->tier = tier;
    s->active = true;
    s->seeding = false;
    s->leeching = false;
    s->num_chunks = 0;
    s->num_peers = 0;

    l->num_swarms++;
    return idx;
}

int lattice_swarm_add_chunk(lattice_t *l, uint32_t swarm_idx,
                             const uint8_t *hash, uint32_t size) {
    if (swarm_idx >= l->num_swarms) return -1;
    lattice_swarm_t *s = &l->swarms[swarm_idx];
    if (s->num_chunks >= LATTICE_MAX_CHUNKS) return -1;

    uint32_t idx = s->num_chunks;
    lattice_chunk_t *c = &s->chunks[idx];
    lat_memcpy(c->hash, hash, LATTICE_HASH_SIZE);
    c->size = size;
    c->replica_count = 1;  /* We have it locally */
    c->available = true;
    s->num_chunks++;

    return (int)idx;
}

int lattice_swarm_add_peer(lattice_t *l, uint32_t swarm_idx, uint32_t peer_idx) {
    if (swarm_idx >= l->num_swarms || peer_idx >= l->num_peers) return -1;
    lattice_swarm_t *s = &l->swarms[swarm_idx];
    if (s->num_peers >= LATTICE_MAX_PEERS) return -1;

    s->peer_indices[s->num_peers] = peer_idx;
    s->num_peers++;
    return 0;
}

int lattice_swarm_start_seeding(lattice_t *l, uint32_t swarm_idx) {
    if (swarm_idx >= l->num_swarms) return -1;
    l->swarms[swarm_idx].seeding = true;
    l->swarms[swarm_idx].leeching = false;
    return 0;
}

int lattice_swarm_stop_seeding(lattice_t *l, uint32_t swarm_idx) {
    if (swarm_idx >= l->num_swarms) return -1;
    l->swarms[swarm_idx].seeding = false;
    return 0;
}

/* ===== DHT Operations ===== */

int32_t lattice_dht_put(lattice_t *l, const uint8_t *key, const uint8_t *value,
                         uint32_t tier) {
    if (l->num_dht >= LATTICE_MAX_DHT_ENTRIES) return -1;

    /* Check for existing key */
    uint32_t i;
    for (i = 0; i < l->num_dht; i++) {
        if (l->dht[i].active && lat_memcmp(l->dht[i].key, key, LATTICE_HASH_SIZE) == 0) {
            lat_memcpy(l->dht[i].value, value, LATTICE_HASH_SIZE);
            l->dht[i].tier = tier;
            return (int32_t)i;
        }
    }

    int32_t idx = (int32_t)l->num_dht;
    lattice_dht_entry_t *e = &l->dht[idx];
    lat_memcpy(e->key, key, LATTICE_HASH_SIZE);
    lat_memcpy(e->value, value, LATTICE_HASH_SIZE);
    e->tier = tier;
    e->active = true;
    l->num_dht++;
    return idx;
}

const lattice_dht_entry_t *lattice_dht_get(lattice_t *l, const uint8_t *key) {
    uint32_t i;
    for (i = 0; i < l->num_dht; i++) {
        if (l->dht[i].active && lat_memcmp(l->dht[i].key, key, LATTICE_HASH_SIZE) == 0)
            return &l->dht[i];
    }
    return NULL;
}

int lattice_dht_remove(lattice_t *l, const uint8_t *key) {
    uint32_t i;
    for (i = 0; i < l->num_dht; i++) {
        if (l->dht[i].active && lat_memcmp(l->dht[i].key, key, LATTICE_HASH_SIZE) == 0) {
            l->dht[i].active = false;
            return 0;
        }
    }
    return -1;
}

/* ===== Chunk Operations ===== */

int lattice_chunk_announce(lattice_t *l, uint32_t swarm_idx, uint32_t chunk_idx) {
    if (swarm_idx >= l->num_swarms) return -1;
    lattice_swarm_t *s = &l->swarms[swarm_idx];
    if (chunk_idx >= s->num_chunks) return -1;

    /* Announce to all connected peers in the swarm */
    uint32_t i;
    for (i = 0; i < s->num_peers; i++) {
        uint32_t peer_idx = s->peer_indices[i];
        if (peer_idx < l->num_peers && l->peers[peer_idx].connected) {
            s->chunks[chunk_idx].replica_count++;
        }
    }
    return 0;
}

int lattice_chunk_request(lattice_t *l, uint32_t swarm_idx, uint32_t chunk_idx,
                           uint32_t peer_idx) {
    if (swarm_idx >= l->num_swarms || peer_idx >= l->num_peers) return -1;
    lattice_swarm_t *s = &l->swarms[swarm_idx];
    if (chunk_idx >= s->num_chunks) return -1;

    /* Mark as downloading from this peer */
    l->peers[peer_idx].chunks_downloaded++;
    s->total_downloaded += s->chunks[chunk_idx].size;
    l->total_downloaded += s->chunks[chunk_idx].size;

    /* Mark chunk as available */
    s->chunks[chunk_idx].available = true;
    return 0;
}

bool lattice_swarm_complete(lattice_t *l, uint32_t swarm_idx) {
    if (swarm_idx >= l->num_swarms) return false;
    lattice_swarm_t *s = &l->swarms[swarm_idx];
    if (s->num_chunks == 0) return false;
    uint32_t i;
    for (i = 0; i < s->num_chunks; i++)
        if (!s->chunks[i].available) return false;
    return true;
}

uint32_t lattice_swarm_progress(lattice_t *l, uint32_t swarm_idx) {
    if (swarm_idx >= l->num_swarms || l->swarms[swarm_idx].num_chunks == 0)
        return 0;
    lattice_swarm_t *s = &l->swarms[swarm_idx];
    uint32_t have = 0, i;
    for (i = 0; i < s->num_chunks; i++)
        if (s->chunks[i].available) have++;
    return (have * 100) / s->num_chunks;
}

/* ===== Utility ===== */

const char *lattice_phase_name(lattice_phase_t phase) {
    switch (phase) {
        case LATTICE_PHASE_TRUE:      return "TRUE (.36n9)";
        case LATTICE_PHASE_GLUT_PLUS: return "GLUT+ (.zedec)";
        case LATTICE_PHASE_GLUT_MINUS:return "GLUT- (.vino)";
        case LATTICE_PHASE_GLUT_ZERO: return "GLUT0 (.ula)";
        case LATTICE_PHASE_SHADOW:    return "SHADOW (.9n63)";
        default: return "UNKNOWN";
    }
}

const char *lattice_tier_name(lattice_tier_t tier) {
    switch (tier) {
        case LATTICE_TIER_SURFACE:      return "Tier 1: Surface";
        case LATTICE_TIER_DEEP:         return "Tier 2: Deep";
        case LATTICE_TIER_ONION:        return "Tier 3: Onion";
        case LATTICE_TIER_GARLIC:       return "Tier 4: Garlic";
        case LATTICE_TIER_DARK_STORAGE: return "Tier 5: Dark Storage";
        case LATTICE_TIER_P2P_MESH:     return "Tier 6: P2P Mesh";
        case LATTICE_TIER_RADIO:        return "Tier 7: Radio Mesh";
        case LATTICE_TIER_SHADOW:       return "Tier 8: Shadow";
        default: return "Unknown";
    }
}
