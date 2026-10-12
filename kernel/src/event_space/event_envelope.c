/* event_envelope.c — Canonical event envelope implementation (ZXB encoding)
 *
 * Implements the architecture-neutral event envelope: identity, causal
 * ordering, payload hashing, and integrity verification via CRC32.
 *
 * The CRC32 is a simple freestanding implementation (no lookup table,
 * uses bit-by-bit computation) suitable for kernel use.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "event_space.h"
#include "../robin_debanks/sha256.h"

/* ===== Freestanding CRC32 (IEEE 802.3 polynomial, no lookup table) ===== */

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len) {
    /* IEEE 802.3 polynomial: 0xEDB88320 (reflected) */
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320u;
            else
                crc >>= 1;
        }
    }
    return crc;
}

static uint32_t crc32_compute(const uint8_t *data, uint32_t len) {
    return crc32_update(0xFFFFFFFFu, data, len) ^ 0xFFFFFFFFu;
}

/* ===== Event Envelope API ===== */

static void copy_str(char *dst, const char *src, uint32_t max) {
    uint32_t i;
    for (i = 0; i + 1 < max && src && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

void ev_envelope_init(ev_envelope_t *env, const ev_node_id_t *node,
                      uint64_t sequence, const char *schema,
                      uint16_t schema_version) {
    if (!env) return;
    ev_memset(env, 0, sizeof(*env));

    if (node) {
        env->event_id.node = *node;
    }
    env->event_id.local_sequence = sequence;

    if (schema) {
        copy_str(env->schema, schema, EV_SCHEMA_LEN);
    }
    env->schema_version = schema_version;

    env->delivery = EV_DELIVERY_BEST_EFFORT;
    env->priority = EV_PRIORITY_NORMAL;
    env->max_cost = 100;
    env->deadline_sequence = 0;
    env->idempotent = false;
    env->num_causal_parents = 0;
    env->payload_len = 0;
}

bool ev_envelope_add_causal_parent(ev_envelope_t *env, const ev_event_id_t *parent) {
    if (!env || !parent) return false;
    if (env->num_causal_parents >= EV_MAX_CAUSAL_PARENTS) return false;
    env->causal_parents[env->num_causal_parents++] = *parent;
    return true;
}

bool ev_envelope_set_payload(ev_envelope_t *env, const uint8_t *data, uint16_t len) {
    if (!env) return false;
    if (len > EV_PAYLOAD_MAX) return false;
    if (len > 0 && !data) return false;

    if (len > 0) {
        ev_memcpy(env->payload, data, len);
        /* Compute SHA-256 of payload (robin_debanks/sha256.c) */
        sha256(data, len, env->payload_hash);
    } else {
        ev_memset(env->payload_hash, 0, sizeof(env->payload_hash));
    }
    env->payload_len = len;
    return true;
}

void ev_envelope_compute_crc(ev_envelope_t *env) {
    if (!env) return;
    /* Save old CRC, compute over everything except the CRC field itself */
    uint32_t saved_crc = env->header_crc;
    env->header_crc = 0;
    /* Compute CRC over the entire structure (CRC field is zeroed) */
    env->header_crc = crc32_compute((const uint8_t *)env, sizeof(*env));
    (void)saved_crc; /* not needed — we overwrite */
}

bool ev_envelope_verify_crc(const ev_envelope_t *env) {
    if (!env) return false;
    /* Recompute CRC with the field zeroed and compare */
    ev_envelope_t copy = *env;
    copy.header_crc = 0;
    uint32_t computed = crc32_compute((const uint8_t *)&copy, sizeof(copy));
    return computed == env->header_crc;
}

bool ev_envelope_validate(const ev_envelope_t *env) {
    if (!env) return false;

    /* Check CRC */
    if (!ev_envelope_verify_crc(env)) return false;

    /* Check schema is non-empty */
    if (env->schema[0] == '\0') return false;

    /* Check payload length */
    if (env->payload_len > EV_PAYLOAD_MAX) return false;

    /* Check causal parents count */
    if (env->num_causal_parents > EV_MAX_CAUSAL_PARENTS) return false;

    /* Check delivery is valid */
    if (env->delivery > EV_DELIVERY_TRANSACTIONAL) return false;

    /* Check priority is valid */
    if (env->priority > EV_PRIORITY_IDLE) return false;

    return true;
}
