/* oseq.h — Ordinal Sequencer and Causal Identity (K1)
 *
 * K1 OSEQ provides canonical causal ordering for all events in the ZXV
 * kernel. It maintains a causal DAG (directed acyclic graph) of events,
 * tracks dependencies, prevents replay, and assigns globally unique
 * ordinal identifiers.
 *
 * Key responsibilities:
 *   - Assign monotonic ordinal IDs to events
 *   - Track causal parent relationships (DAG edges)
 *   - Detect and reject replayed events
 *   - Detect causal cycles (impossible ordering)
 *   - Provide happens-before queries
 *   - Track per-node incarnation and sequence
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef OSEQ_H
#define OSEQ_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "event_space.h"

/* ===== OSEQ Constants ===== */

#define OSEQ_MAX_EVENTS        512
#define OSEQ_MAX_NODES          64
#define OSEQ_MAX_PARENTS         4   /* max causal parents per event */
#define OSEQ_MAX_NAME_LEN       32
#define OSEQ_REPLAY_WINDOW     128   /* sliding window for replay detection */
#define OSEQ_MAX_DEPTH         256   /* max DAG traversal depth (cycle guard) */

/* ===== OSEQ Event Status ===== */

typedef enum {
    OSEQ_STATUS_PENDING    = 0,   /* registered but not yet committed */
    OSEQ_STATUS_COMMITTED  = 1,   /* causally linked and ordered */
    OSEQ_STATUS_REJECTED   = 2,   /* replay, cycle, or invalid */
    OSEQ_STATUS_EXPIRED    = 3,   /* evicted from window */
} oseq_status_t;

/* ===== OSEQ Node Identity ===== */

typedef struct oseq_node {
    char name[OSEQ_MAX_NAME_LEN];
    uint32_t incarnation;          /* changes on reboot */
    uint64_t next_sequence;        /* next local sequence number */
    bool active;
} oseq_node_t;

/* ===== OSEQ Event Record ===== */

typedef struct oseq_event {
    uint64_t ordinal;              /* globally unique monotonic ID */
    uint32_t node_idx;             /* index into node table */
    uint64_t local_sequence;       /* per-node sequence */
    uint32_t parent_ordinals[OSEQ_MAX_PARENTS];  /* parent event indices */
    uint8_t num_parents;
    oseq_status_t status;
    char schema[EV_SCHEMA_LEN];    /* event schema for identification */
    uint16_t schema_version;
    uint8_t payload_hash[32];      /* integrity binding */
    uint32_t payload_hash_len;
} oseq_event_t;

/* ===== OSEQ Registry ===== */

typedef struct oseq_registry {
    /* Node table */
    oseq_node_t nodes[OSEQ_MAX_NODES];
    uint32_t node_count;

    /* Event table (indexed by ordinal) */
    oseq_event_t events[OSEQ_MAX_EVENTS];
    uint32_t event_count;
    uint64_t next_ordinal;         /* global monotonic counter */

    /* Replay detection: ring buffer of recently seen payload hashes */
    uint8_t replay_hashes[OSEQ_REPLAY_WINDOW][32];
    uint32_t replay_hash_lens[OSEQ_REPLAY_WINDOW];
    uint32_t replay_head;
    uint32_t replay_count;

    /* Statistics */
    uint32_t replays_detected;
    uint32_t cycles_detected;
    uint32_t commits;
    uint32_t rejections;
} oseq_registry_t;

/* ===== API ===== */

void oseq_registry_init(oseq_registry_t *reg);

/* Node management */
int32_t oseq_register_node(oseq_registry_t *reg, const char *name,
                           uint32_t incarnation);
oseq_node_t *oseq_get_node(oseq_registry_t *reg, uint32_t node_idx);
int32_t oseq_find_node(oseq_registry_t *reg, const char *name);

/* Event registration and causal linking */
int32_t oseq_register_event(oseq_registry_t *reg, uint32_t node_idx,
                            const char *schema, uint16_t schema_version,
                            const uint8_t *payload_hash, uint32_t hash_len,
                            const uint32_t *parent_ordinals, uint8_t num_parents);
bool oseq_commit_event(oseq_registry_t *reg, uint32_t event_idx);
bool oseq_reject_event(oseq_registry_t *reg, uint32_t event_idx);

/* Causal queries */
bool oseq_happens_before(oseq_registry_t *reg, uint32_t a_idx, uint32_t b_idx);
bool oseq_is_ancestor(oseq_registry_t *reg, uint32_t ancestor_idx, uint32_t descendant_idx);
bool oseq_detect_cycle(oseq_registry_t *reg, uint32_t event_idx);
uint32_t oseq_get_depth(oseq_registry_t *reg, uint32_t event_idx);

/* Replay detection */
bool oseq_check_replay(oseq_registry_t *reg, const uint8_t *payload_hash,
                       uint32_t hash_len);
void oseq_record_replay(oseq_registry_t *reg, const uint8_t *payload_hash,
                        uint32_t hash_len);

/* Event retrieval */
oseq_event_t *oseq_get_event(oseq_registry_t *reg, uint32_t idx);
uint32_t oseq_count_by_status(oseq_registry_t *reg, oseq_status_t status);

/* Utility */
bool oseq_dag_is_valid_ordinal(uint64_t ordinal);
bool oseq_dag_is_later(uint64_t a, uint64_t b);

/* Name functions */
const char *oseq_status_name(oseq_status_t status);

#endif /* OSEQ_H */
