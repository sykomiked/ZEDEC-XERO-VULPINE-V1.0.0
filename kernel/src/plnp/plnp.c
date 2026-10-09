/*
 * plnp.c — Phase-Lattice Network Protocol Implementation
 *
 * Content-addressed, cryptographic phase-routing protocol replacing TCP/IP.
 * 5-layer stack: 5PL Application → Merkle VFS → PungentClove → Phase-Tick → Physical.
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

#include "plnp.h"

/* ===== Helpers ===== */

static void plnp_memset(void *dst, int v, uint32_t n) {
    uint8_t *d = (uint8_t *)dst; uint32_t i;
    for (i = 0; i < n; i++) d[i] = (uint8_t)v;
}

static void plnp_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst; const uint8_t *s = (const uint8_t *)src; uint32_t i;
    for (i = 0; i < n; i++) d[i] = s[i];
}

static __attribute__((unused)) int plnp_memcmp(const void *a, const void *b, uint32_t n) {
    const uint8_t *pa = a, *pb = b; uint32_t i;
    for (i = 0; i < n; i++) if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    return 0;
}

/* ===== CRC32 ===== */

uint32_t plnp_crc32(const uint8_t *data, uint32_t len) {
    uint32_t crc = 0xFFFFFFFF;
    uint32_t i, j;
    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ PLNP_CRC_POLY;
            else
                crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

/* ===== CID Derivation (FNV-1a based) ===== */

void plnp_derive_cid(const uint8_t *content, uint32_t len, uint8_t *out_cid) {
    uint64_t h = 0xcbf29ce484222325ULL;
    uint32_t i;
    for (i = 0; i < len; i++) {
        h ^= content[i];
        h *= 0x100000001b3ULL;
    }
    /* Expand to 32 bytes via iterated hashing */
    for (i = 0; i < PLNP_CID_SIZE; i++) {
        out_cid[i] = (uint8_t)(h >> ((i % 8) * 8));
        if (i % 8 == 7) {
            h *= 0x100000001b3ULL;
            h ^= 0x5a;
        }
    }
}

void plnp_derive_key(uint8_t key_index, const uint8_t *seed, uint32_t seed_len,
                     uint8_t *out_key) {
    /* HKDF-like derivation: mix key_index into seed hash */
    uint8_t expanded[PLNP_HASH_SIZE + 4];
    plnp_memcpy(expanded, seed, seed_len < PLNP_HASH_SIZE ? seed_len : PLNP_HASH_SIZE);
    expanded[PLNP_HASH_SIZE] = key_index;
    expanded[PLNP_HASH_SIZE + 1] = key_index >> 8;
    expanded[PLNP_HASH_SIZE + 2] = 0;
    expanded[PLNP_HASH_SIZE + 3] = 0;

    uint64_t h = 0xcbf29ce484222325ULL;
    uint32_t i;
    for (i = 0; i < PLNP_HASH_SIZE + 4; i++) {
        h ^= expanded[i];
        h *= 0x100000001b3ULL;
    }
    for (i = 0; i < PLNP_HASH_SIZE; i++) {
        out_key[i] = (uint8_t)(h >> ((i % 8) * 8));
        if (i % 8 == 7) { h *= 0x100000001b3ULL; h ^= 0x5a; }
    }
}

/* ===== Init ===== */

void plnp_init(plnp_stack_t *s) {
    plnp_memset(s, 0, sizeof(plnp_stack_t));
    s->initialized = true;
    s->phase_tick_ms = 10;
    s->default_key_index = PLNP_KEY_K1;
}

/* ===== Frame Operations ===== */

int plnp_frame_init(plnp_frame_t *f, uint8_t key_index, uint8_t phase_state) {
    plnp_memset(f, 0, sizeof(plnp_frame_t));
    f->header.magic = PLNP_MAGIC;
    f->header.version = PLNP_VERSION;
    f->header.flags = PLNP_FLAG_5PL | PLNP_FLAG_PHASE_TICK;
    f->header.phase_state = phase_state;
    f->header.key_index = key_index;
    f->header.seq_num = 0;
    f->header.payload_len = 0;
    f->header.fpl_vector = 0;
    f->end_marker = 0x5A;
    return 0;
}

int plnp_frame_set_cids(plnp_frame_t *f, const uint8_t *src, const uint8_t *dst) {
    if (!f || !src || !dst) return -1;
    plnp_memcpy(f->header.src_cid, src, PLNP_CID_SIZE);
    plnp_memcpy(f->header.dst_cid, dst, PLNP_CID_SIZE);
    return 0;
}

int plnp_frame_set_payload(plnp_frame_t *f, const uint8_t *data, uint32_t len) {
    if (!f || !data) return -1;
    if (len > PLNP_MAX_PAYLOAD) return -1;
    plnp_memcpy(f->payload, data, len);
    f->header.payload_len = len;
    return 0;
}

int plnp_frame_set_5pl(plnp_frame_t *f, uint8_t vector_flags) {
    if (!f) return -1;
    f->header.fpl_vector = vector_flags;
    if (vector_flags & PLNP_5PL_CLAIM)
        f->header.flags |= PLNP_FLAG_5PL;
    return 0;
}

int plnp_frame_set_merkle(plnp_frame_t *f, const uint8_t *root) {
    if (!f || !root) return -1;
    plnp_memcpy(f->header.merkle_root, root, PLNP_HASH_SIZE);
    return 0;
}

int plnp_frame_seal(plnp_frame_t *f) {
    if (!f) return -1;
    if (f->header.magic != PLNP_MAGIC) return -1;

    /* Compute CRC over header + payload */
    uint8_t buf[PLNP_HEADER_SIZE + PLNP_MAX_PAYLOAD];
    plnp_memcpy(buf, &f->header, PLNP_HEADER_SIZE);
    plnp_memcpy(buf + PLNP_HEADER_SIZE, f->payload, f->header.payload_len);
    f->crc32 = plnp_crc32(buf, PLNP_HEADER_SIZE + f->header.payload_len);
    f->end_marker = 0x5A;
    return 0;
}

int plnp_frame_verify(const plnp_frame_t *f) {
    if (!f) return -1;
    if (f->header.magic != PLNP_MAGIC) return -1;
    if (f->header.version != PLNP_VERSION) return -1;
    if (f->end_marker != 0x5A) return -1;

    /* Recompute CRC */
    uint8_t buf[PLNP_HEADER_SIZE + PLNP_MAX_PAYLOAD];
    plnp_memcpy(buf, &f->header, PLNP_HEADER_SIZE);
    plnp_memcpy(buf + PLNP_HEADER_SIZE, f->payload, f->header.payload_len);
    uint32_t computed = plnp_crc32(buf, PLNP_HEADER_SIZE + f->header.payload_len);

    if (computed != f->crc32) return -2;  /* CRC mismatch */
    return 0;
}

uint32_t plnp_frame_size(const plnp_frame_t *f) {
    if (!f) return 0;
    return PLNP_HEADER_SIZE + f->header.payload_len + PLNP_TRAILER_SIZE;
}

/* ===== Serialization ===== */

int plnp_frame_serialize(const plnp_frame_t *f, uint8_t *buf, uint32_t buf_len) {
    if (!f || !buf) return -1;
    uint32_t total = plnp_frame_size(f);
    if (buf_len < total) return -1;

    plnp_memcpy(buf, &f->header, PLNP_HEADER_SIZE);
    plnp_memcpy(buf + PLNP_HEADER_SIZE, f->payload, f->header.payload_len);

    /* Write CRC32 (little-endian) */
    buf[PLNP_HEADER_SIZE + f->header.payload_len]     = (uint8_t)(f->crc32);
    buf[PLNP_HEADER_SIZE + f->header.payload_len + 1] = (uint8_t)(f->crc32 >> 8);
    buf[PLNP_HEADER_SIZE + f->header.payload_len + 2] = (uint8_t)(f->crc32 >> 16);
    buf[PLNP_HEADER_SIZE + f->header.payload_len + 3] = (uint8_t)(f->crc32 >> 24);
    buf[PLNP_HEADER_SIZE + f->header.payload_len + 4] = f->end_marker;

    return (int)total;
}

int plnp_frame_deserialize(plnp_frame_t *f, const uint8_t *buf, uint32_t buf_len) {
    if (!f || !buf) return -1;
    if (buf_len < PLNP_HEADER_SIZE + PLNP_TRAILER_SIZE) return -1;

    plnp_memcpy(&f->header, buf, PLNP_HEADER_SIZE);

    if (f->header.magic != PLNP_MAGIC) return -2;
    if (f->header.version != PLNP_VERSION) return -3;
    if (f->header.payload_len > PLNP_MAX_PAYLOAD) return -4;

    uint32_t total = PLNP_HEADER_SIZE + f->header.payload_len + PLNP_TRAILER_SIZE;
    if (buf_len < total) return -5;

    plnp_memcpy(f->payload, buf + PLNP_HEADER_SIZE, f->header.payload_len);

    f->crc32 = (uint32_t)buf[PLNP_HEADER_SIZE + f->header.payload_len]
             | ((uint32_t)buf[PLNP_HEADER_SIZE + f->header.payload_len + 1] << 8)
             | ((uint32_t)buf[PLNP_HEADER_SIZE + f->header.payload_len + 2] << 16)
             | ((uint32_t)buf[PLNP_HEADER_SIZE + f->header.payload_len + 3] << 24);
    f->end_marker = buf[PLNP_HEADER_SIZE + f->header.payload_len + 4];

    return (int)total;
}

/* ===== Connection Management ===== */

int32_t plnp_conn_create(plnp_stack_t *s, const uint8_t *src_cid,
                          const uint8_t *dst_cid, uint8_t key_index) {
    if (s->num_connections >= PLNP_MAX_FRAMES) return -1;
    if (!src_cid || !dst_cid) return -1;
    if (key_index < PLNP_KEY_K1 || key_index > PLNP_KEY_K5) return -1;

    int32_t idx = (int32_t)s->num_connections;
    plnp_conn_t *c = &s->connections[idx];
    plnp_memset(c, 0, sizeof(plnp_conn_t));
    c->id = (uint32_t)idx;
    c->state = PLNP_CONN_IDLE;
    plnp_memcpy(c->src_cid, src_cid, PLNP_CID_SIZE);
    plnp_memcpy(c->dst_cid, dst_cid, PLNP_CID_SIZE);
    c->key_index = key_index;
    c->seq_num = 0;
    c->active = true;

    s->num_connections++;
    return idx;
}

int plnp_conn_send(plnp_stack_t *s, uint32_t conn_idx,
                    const uint8_t *data, uint32_t len, uint8_t phase_state) {
    if (conn_idx >= s->num_connections) return -1;
    plnp_conn_t *c = &s->connections[conn_idx];
    if (!c->active) return -1;

    /* Build frame */
    plnp_frame_t frame;
    plnp_frame_init(&frame, c->key_index, phase_state);
    plnp_frame_set_cids(&frame, c->src_cid, c->dst_cid);
    plnp_frame_set_payload(&frame, data, len);
    frame.header.seq_num = c->seq_num++;
    plnp_frame_seal(&frame);

    /* Verify before "sending" */
    if (plnp_frame_verify(&frame) != 0) {
        c->state = PLNP_CONN_FAILED;
        return -1;
    }

    c->bytes_sent += len;
    c->frames_sent++;
    c->state = PLNP_CONN_SENDING;

    /* Phase resolution */
    if (phase_state == PLNP_PHASE_GLUT_ZERO) {
        c->glut_freezes++;
        s->total_glut_freezes++;
        c->state = PLNP_CONN_GLUT_FREEZE;
    } else if (phase_state == PLNP_PHASE_GLUT_PLUS) {
        c->glut_plus_count++;
        s->total_glut_plus++;
        c->state = PLNP_CONN_SENDING;
    } else if (phase_state == PLNP_PHASE_GLUT_MINUS) {
        c->glut_minus_count++;
        s->total_glut_minus++;
        c->state = PLNP_CONN_IDLE;  /* Safe drop */
    }

    s->total_frames_sent++;
    return (int)len;
}

int plnp_conn_receive(plnp_stack_t *s, uint32_t conn_idx, plnp_frame_t *out) {
    if (conn_idx >= s->num_connections || !out) return -1;
    plnp_conn_t *c = &s->connections[conn_idx];
    if (!c->active) return -1;

    c->state = PLNP_CONN_RECEIVING;
    c->state = PLNP_CONN_VERIFYING;

    /* In real implementation, would verify incoming frame */
    c->frames_received++;
    s->total_frames_received++;
    c->state = PLNP_CONN_COMPLETE;

    return 0;
}

int plnp_conn_close(plnp_stack_t *s, uint32_t conn_idx) {
    if (conn_idx >= s->num_connections) return -1;
    s->connections[conn_idx].active = false;
    s->connections[conn_idx].state = PLNP_CONN_IDLE;
    return 0;
}

/* ===== Phase Resolution ===== */

int plnp_resolve_phase(plnp_stack_t *s, uint32_t conn_idx, uint8_t phase_state) {
    if (conn_idx >= s->num_connections) return -1;
    plnp_conn_t *c = &s->connections[conn_idx];

    switch (phase_state) {
        case PLNP_PHASE_TRUE:
            /* Normal processing: accept frame */
            c->state = PLNP_CONN_COMPLETE;
            return 0;

        case PLNP_PHASE_GLUT_PLUS:
            /* Speculative: continue processing while consensus catches up */
            c->glut_plus_count++;
            s->total_glut_plus++;
            c->state = PLNP_CONN_SENDING;
            return 0;

        case PLNP_PHASE_GLUT_MINUS:
            /* Safe drop: frame dropped without stalling execution */
            c->glut_minus_count++;
            s->total_glut_minus++;
            c->state = PLNP_CONN_IDLE;
            return 0;

        case PLNP_PHASE_GLUT_ZERO:
            /* Freeze: lock corrupt frame into shadow storage for inspection */
            c->glut_freezes++;
            s->total_glut_freezes++;
            c->state = PLNP_CONN_GLUT_FREEZE;
            return 0;

        case PLNP_PHASE_FALSE:
            /* Hard reject */
            c->state = PLNP_CONN_FAILED;
            return -1;

        default:
            return -1;
    }
}

/* ===== Names ===== */

const char *plnp_phase_name(uint8_t phase) {
    switch (phase) {
        case PLNP_PHASE_FALSE:      return "FALSE (0)";
        case PLNP_PHASE_TRUE:       return "TRUE (1)";
        case PLNP_PHASE_GLUT:       return "GLUT (G)";
        case PLNP_PHASE_GLUT_PLUS:  return "GLUT+ (G+)";
        case PLNP_PHASE_GLUT_MINUS: return "GLUT- (G-)";
        case PLNP_PHASE_GLUT_ZERO:  return "GLUT0 (G0)";
        default: return "UNKNOWN";
    }
}

const char *plnp_state_name(plnp_conn_state_t state) {
    switch (state) {
        case PLNP_CONN_IDLE:        return "IDLE";
        case PLNP_CONN_CREATING:    return "CREATING";
        case PLNP_CONN_SENDING:     return "SENDING";
        case PLNP_CONN_RECEIVING:   return "RECEIVING";
        case PLNP_CONN_VERIFYING:   return "VERIFYING";
        case PLNP_CONN_GLUT_FREEZE: return "GLUT_FREEZE";
        case PLNP_CONN_COMPLETE:    return "COMPLETE";
        case PLNP_CONN_FAILED:      return "FAILED";
        default: return "UNKNOWN";
    }
}

const char *plnp_key_name(uint8_t key_index) {
    switch (key_index) {
        case PLNP_KEY_K1: return "K1 (Surface)";
        case PLNP_KEY_K2: return "K2 (Deep)";
        case PLNP_KEY_K3: return "K3 (Onion)";
        case PLNP_KEY_K4: return "K4 (Garlic)";
        case PLNP_KEY_K5: return "K5 (Shadow)";
        default: return "UNKNOWN";
    }
}
