/*
 * plnp.c — PLNP frame format and connection framing (see plnp.h for what is
 * and is not implemented). CRC32 for accidental corruption, HMAC-SHA256
 * (tls/hkdf.c) for authenticity, SHA3-256 (mlkem/keccak.c) for content IDs.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

#include "plnp.h"
#include "../tls/hkdf.h"
#include "../mlkem/keccak.h"
#include "../robin_debanks/sha256.h"

/* ===== Helpers ===== */

static void plnp_memset(void *dst, int v, uint32_t n) {
    uint8_t *d = (uint8_t *)dst; uint32_t i;
    for (i = 0; i < n; i++) d[i] = (uint8_t)v;
}

static void plnp_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst; const uint8_t *s = (const uint8_t *)src; uint32_t i;
    for (i = 0; i < n; i++) d[i] = s[i];
}

static int plnp_memcmp(const void *a, const void *b, uint32_t n)
{
    const uint8_t *pa = a, *pb = b; uint32_t i;
    for (i = 0; i < n; i++) if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    return 0;
}

/* ===== CRC32 ===== */

static uint32_t plnp_crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
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
    return crc;
}

uint32_t plnp_crc32(const uint8_t *data, uint32_t len)
{
    return plnp_crc32_update(0xFFFFFFFF, data, len) ^ 0xFFFFFFFF;
}

/* CRC over header || payload [|| mac], without copying. */
static uint32_t plnp_frame_crc(const plnp_frame_t *f)
{
    uint32_t crc = plnp_crc32_update(0xFFFFFFFF, (const uint8_t *) &f->header, PLNP_HEADER_SIZE);
    crc = plnp_crc32_update(crc, f->payload, f->header.payload_len);
    if (f->header.flags & PLNP_FLAG_AUTH) crc = plnp_crc32_update(crc, f->mac, PLNP_MAC_SIZE);
    return crc ^ 0xFFFFFFFF;
}

/* HMAC-SHA256 (RFC 2104) over header || payload, streamed so the frame is
 * never copied. The key is 32 bytes, shorter than the 64-byte block. */
static void plnp_frame_mac(const plnp_frame_t *f, const uint8_t key[32], uint8_t out[PLNP_MAC_SIZE])
{
    uint8_t pad[64], inner[32];
    sha256_ctx_t c;
    for (uint32_t i = 0; i < 64; i++) pad[i] = (uint8_t) ((i < 32 ? key[i] : 0) ^ 0x36);
    sha256_init(&c);
    sha256_update(&c, pad, 64);
    sha256_update(&c, (const uint8_t *) &f->header, PLNP_HEADER_SIZE);
    sha256_update(&c, f->payload, f->header.payload_len);
    sha256_final(&c, inner);
    for (uint32_t i = 0; i < 64; i++) pad[i] = (uint8_t) ((i < 32 ? key[i] : 0) ^ 0x5c);
    sha256_init(&c);
    sha256_update(&c, pad, 64);
    sha256_update(&c, inner, 32);
    sha256_final(&c, out);
    plnp_memset(pad, 0, sizeof(pad));
    plnp_memset(inner, 0, sizeof(inner));
}

/* ===== CID Derivation (SHA3-256) ===== */

void plnp_derive_cid(const uint8_t *content, uint32_t len, uint8_t *out_cid) {
    sha3_256(content, len, out_cid);
}

void plnp_derive_key(uint8_t key_index, const uint8_t *seed, uint32_t seed_len,
                     uint8_t *out_key) {
    /* HKDF-SHA256(salt = "plnp/v1", ikm = seed, info = "K" || index). The
     * old FNV mix read past short seeds and was trivially invertible. */
    static const uint8_t salt[] = "plnp/v1";
    uint8_t prk[HASH_LEN];
    uint8_t info[2] = {'K', key_index};
    hkdf_extract(salt, (uint32_t) (sizeof(salt) - 1), seed, seed ? seed_len : 0, prk);
    (void) hkdf_expand(prk, info, sizeof(info), out_key, PLNP_HASH_SIZE);
    plnp_memset(prk, 0, sizeof(prk));
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
    if (f->header.payload_len > PLNP_MAX_PAYLOAD) return -1;
    f->header.flags &= (uint8_t) ~PLNP_FLAG_AUTH;
    f->crc32 = plnp_frame_crc(f);
    f->end_marker = 0x5A;
    return 0;
}

int plnp_frame_verify(const plnp_frame_t *f) {
    if (!f) return -1;
    if (f->header.magic != PLNP_MAGIC) return -1;
    if (f->header.version != PLNP_VERSION) return -1;
    if (f->header.payload_len > PLNP_MAX_PAYLOAD) return -1;
    if (f->end_marker != 0x5A) return -1;
    if (plnp_frame_crc(f) != f->crc32) return -2; /* CRC mismatch */
    return 0;
}

int plnp_frame_seal_auth(plnp_frame_t *f, const uint8_t key[32])
{
    if (!f || !key) return -1;
    if (f->header.magic != PLNP_MAGIC || f->header.payload_len > PLNP_MAX_PAYLOAD) return -1;
    f->header.flags |= PLNP_FLAG_AUTH; /* the flag is covered by the tag */
    plnp_frame_mac(f, key, f->mac);
    f->crc32 = plnp_frame_crc(f);
    f->end_marker = 0x5A;
    return 0;
}

int plnp_frame_verify_auth(const plnp_frame_t *f, const uint8_t key[32])
{
    if (!key) return -1;
    int rc = plnp_frame_verify(f);
    if (rc != 0) return rc;
    if (!(f->header.flags & PLNP_FLAG_AUTH)) return -3;
    uint8_t mac[PLNP_MAC_SIZE];
    plnp_frame_mac(f, key, mac);
    bool ok = ct_equal(mac, f->mac, PLNP_MAC_SIZE);
    plnp_memset(mac, 0, sizeof(mac));
    return ok ? 0 : -3;
}

uint32_t plnp_frame_size(const plnp_frame_t *f) {
    if (!f) return 0;
    uint32_t mac = (f->header.flags & PLNP_FLAG_AUTH) ? PLNP_MAC_SIZE : 0;
    return PLNP_HEADER_SIZE + f->header.payload_len + mac + PLNP_TRAILER_SIZE;
}

/* ===== Serialization ===== */

int plnp_frame_serialize(const plnp_frame_t *f, uint8_t *buf, uint32_t buf_len) {
    if (!f || !buf || f->header.payload_len > PLNP_MAX_PAYLOAD) return -1;
    uint32_t total = plnp_frame_size(f);
    if (buf_len < total) return -1;

    uint32_t p = 0;
    plnp_memcpy(buf, &f->header, PLNP_HEADER_SIZE);
    p += PLNP_HEADER_SIZE;
    plnp_memcpy(buf + p, f->payload, f->header.payload_len);
    p += f->header.payload_len;
    if (f->header.flags & PLNP_FLAG_AUTH) {
        plnp_memcpy(buf + p, f->mac, PLNP_MAC_SIZE);
        p += PLNP_MAC_SIZE;
    }
    /* CRC32 (little-endian) and end marker */
    buf[p] = (uint8_t) (f->crc32);
    buf[p + 1] = (uint8_t) (f->crc32 >> 8);
    buf[p + 2] = (uint8_t) (f->crc32 >> 16);
    buf[p + 3] = (uint8_t) (f->crc32 >> 24);
    buf[p + 4] = f->end_marker;
    return (int)total;
}

int plnp_frame_deserialize(plnp_frame_t *f, const uint8_t *buf, uint32_t buf_len) {
    if (!f || !buf) return -1;
    if (buf_len < PLNP_HEADER_SIZE + PLNP_TRAILER_SIZE) return -1;

    plnp_memset(f, 0, sizeof(*f));
    plnp_memcpy(&f->header, buf, PLNP_HEADER_SIZE);

    if (f->header.magic != PLNP_MAGIC) return -2;
    if (f->header.version != PLNP_VERSION) return -3;
    if (f->header.payload_len > PLNP_MAX_PAYLOAD) return -4;

    uint32_t total = plnp_frame_size(f);
    if (buf_len < total) return -5;

    uint32_t p = PLNP_HEADER_SIZE;
    plnp_memcpy(f->payload, buf + p, f->header.payload_len);
    p += f->header.payload_len;
    if (f->header.flags & PLNP_FLAG_AUTH) {
        plnp_memcpy(f->mac, buf + p, PLNP_MAC_SIZE);
        p += PLNP_MAC_SIZE;
    }
    f->crc32 = (uint32_t) buf[p] | ((uint32_t) buf[p + 1] << 8) | ((uint32_t) buf[p + 2] << 16) |
               ((uint32_t) buf[p + 3] << 24);
    f->end_marker = buf[p + 4];
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

int plnp_conn_set_key(plnp_stack_t *s, uint32_t conn_idx, const uint8_t key[32])
{
    if (!s || !key || conn_idx >= s->num_connections) return -1;
    plnp_memcpy(s->connections[conn_idx].key, key, 32);
    s->connections[conn_idx].has_key = true;
    return 0;
}

int plnp_conn_send(plnp_stack_t *s, uint32_t conn_idx, const uint8_t *data, uint32_t len,
                   uint8_t phase_state, uint8_t *out, uint32_t out_cap)
{
    if (!s || conn_idx >= s->num_connections || !out) return -1;
    plnp_conn_t *c = &s->connections[conn_idx];
    if (!c->active || (len > 0 && !data) || len > PLNP_MAX_PAYLOAD) return -1;

    plnp_frame_t frame;
    plnp_frame_init(&frame, c->key_index, phase_state);
    plnp_frame_set_cids(&frame, c->src_cid, c->dst_cid);
    if (len > 0) plnp_frame_set_payload(&frame, data, len);
    frame.header.seq_num = c->seq_num;
    int rc = c->has_key ? plnp_frame_seal_auth(&frame, c->key) : plnp_frame_seal(&frame);
    if (rc != 0) return -1;
    int n = plnp_frame_serialize(&frame, out, out_cap);
    if (n < 0) return -1;
    c->seq_num++;

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
    return n;
}

static int plnp_reject(plnp_conn_t *c, int code)
{
    c->frames_rejected++;
    c->state = PLNP_CONN_FAILED;
    return code;
}

int plnp_conn_receive(plnp_stack_t *s, uint32_t conn_idx, const uint8_t *buf, uint32_t buf_len,
                      plnp_frame_t *out)
{
    if (!s || conn_idx >= s->num_connections || !out || !buf) return -1;
    plnp_conn_t *c = &s->connections[conn_idx];
    if (!c->active) return -1;

    c->state = PLNP_CONN_RECEIVING;
    if (plnp_frame_deserialize(out, buf, buf_len) < 0) return plnp_reject(c, -2);

    c->state = PLNP_CONN_VERIFYING;
    if (c->has_key) {
        if (plnp_frame_verify_auth(out, c->key) != 0) return plnp_reject(c, -3);
    } else {
        if (plnp_frame_verify(out) != 0) return plnp_reject(c, -2);
        if (out->header.flags & PLNP_FLAG_AUTH) return plnp_reject(c, -3); /* cannot verify */
    }
    /* The peer's source is our destination and vice versa. */
    if (plnp_memcmp(out->header.src_cid, c->dst_cid, PLNP_CID_SIZE) != 0 ||
        plnp_memcmp(out->header.dst_cid, c->src_cid, PLNP_CID_SIZE) != 0)
        return plnp_reject(c, -4);
    if (out->header.seq_num != c->rx_seq) return plnp_reject(c, -5); /* replay or gap */
    if (out->header.key_index != c->key_index) return plnp_reject(c, -6);

    c->rx_seq++;
    if (c->has_key) c->frames_authenticated++;
    c->frames_received++;
    c->bytes_received += out->header.payload_len;
    s->total_frames_received++;
    c->state = PLNP_CONN_COMPLETE;
    return (int) out->header.payload_len;
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
        case PLNP_KEY_K3:
            return "K3 (Onion label)";
        case PLNP_KEY_K4:
            return "K4 (Garlic label)";
        case PLNP_KEY_K5: return "K5 (Shadow)";
        default: return "UNKNOWN";
    }
}
