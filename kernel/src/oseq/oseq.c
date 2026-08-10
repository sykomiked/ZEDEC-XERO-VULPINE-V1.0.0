/* oseq.c — Ordinal Sequencer and Causal Identity (K1)
 *
 * Implements causal DAG, event ordering, replay detection, and
 * happens-before queries for the ZXV kernel.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */

#include "oseq.h"

/* ===== Helpers ===== */

static void copy_str(char *dst, const char *src, uint32_t max) {
    uint32_t i;
    for (i = 0; i + 1 < max && src && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static bool hash_equal(const uint8_t *a, const uint8_t *b, uint32_t len) {
    for (uint32_t i = 0; i < len; i++)
        if (a[i] != b[i]) return false;
    return true;
}

static void hash_copy(uint8_t *dst, const uint8_t *src, uint32_t len) {
    for (uint32_t i = 0; i < len; i++)
        dst[i] = src[i];
}

/* ===== Registry Init ===== */

void oseq_registry_init(oseq_registry_t *reg) {
    if (!reg) return;
    ev_memset(reg, 0, sizeof(*reg));
    reg->next_ordinal = 1;  /* 0 is reserved as "null ordinal" */
}

/* ===== Node Management ===== */

int32_t oseq_register_node(oseq_registry_t *reg, const char *name,
                           uint32_t incarnation) {
    if (!reg || !name) return -1;
    if (reg->node_count >= OSEQ_MAX_NODES) return -1;

    /* Check for duplicate name */
    for (uint32_t i = 0; i < reg->node_count; i++) {
        uint32_t j;
        for (j = 0; j < OSEQ_MAX_NAME_LEN; j++) {
            if (reg->nodes[i].name[j] != name[j]) break;
            if (name[j] == '\0') break;
        }
        if (j < OSEQ_MAX_NAME_LEN && name[j] == '\0' &&
            reg->nodes[i].name[j] == '\0') {
            /* Node exists — update incarnation if different */
            if (reg->nodes[i].incarnation != incarnation) {
                reg->nodes[i].incarnation = incarnation;
                reg->nodes[i].next_sequence = 0;
            }
            return (int32_t)i;
        }
    }

    oseq_node_t *n = &reg->nodes[reg->node_count];
    ev_memset(n, 0, sizeof(*n));
    copy_str(n->name, name, OSEQ_MAX_NAME_LEN);
    n->incarnation = incarnation;
    n->next_sequence = 0;
    n->active = true;
    return (int32_t)reg->node_count++;
}

oseq_node_t *oseq_get_node(oseq_registry_t *reg, uint32_t node_idx) {
    if (!reg || node_idx >= reg->node_count) return NULL;
    return &reg->nodes[node_idx];
}

int32_t oseq_find_node(oseq_registry_t *reg, const char *name) {
    if (!reg || !name) return -1;
    for (uint32_t i = 0; i < reg->node_count; i++) {
        uint32_t j;
        for (j = 0; j < OSEQ_MAX_NAME_LEN; j++) {
            if (reg->nodes[i].name[j] != name[j]) break;
            if (name[j] == '\0') break;
        }
        if (j < OSEQ_MAX_NAME_LEN && name[j] == '\0' &&
            reg->nodes[i].name[j] == '\0')
            return (int32_t)i;
    }
    return -1;
}

/* ===== Replay Detection ===== */

bool oseq_check_replay(oseq_registry_t *reg, const uint8_t *payload_hash,
                       uint32_t hash_len) {
    if (!reg || !payload_hash || hash_len == 0) return false;
    uint32_t count = reg->replay_count;
    if (count > OSEQ_REPLAY_WINDOW) count = OSEQ_REPLAY_WINDOW;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t idx = (reg->replay_head + OSEQ_REPLAY_WINDOW - 1 - i) %
                       OSEQ_REPLAY_WINDOW;
        if (reg->replay_hash_lens[idx] == hash_len &&
            hash_equal(reg->replay_hashes[idx], payload_hash, hash_len))
            return true;  /* replay detected */
    }
    return false;
}

void oseq_record_replay(oseq_registry_t *reg, const uint8_t *payload_hash,
                        uint32_t hash_len) {
    if (!reg || !payload_hash || hash_len == 0 || hash_len > 32) return;
    uint32_t idx = reg->replay_head;
    hash_copy(reg->replay_hashes[idx], payload_hash, hash_len);
    reg->replay_hash_lens[idx] = hash_len;
    reg->replay_head = (reg->replay_head + 1) % OSEQ_REPLAY_WINDOW;
    if (reg->replay_count < OSEQ_REPLAY_WINDOW) reg->replay_count++;
}

/* ===== Event Registration ===== */

int32_t oseq_register_event(oseq_registry_t *reg, uint32_t node_idx,
                            const char *schema, uint16_t schema_version,
                            const uint8_t *payload_hash, uint32_t hash_len,
                            const uint32_t *parent_ordinals, uint8_t num_parents) {
    if (!reg) return -1;
    if (node_idx >= reg->node_count) return -1;
    if (reg->event_count >= OSEQ_MAX_EVENTS) return -1;
    if (num_parents > OSEQ_MAX_PARENTS) return -1;

    /* Check replay if payload hash provided */
    if (payload_hash && hash_len > 0) {
        if (oseq_check_replay(reg, payload_hash, hash_len)) {
            reg->replays_detected++;
            reg->rejections++;
            return -1;  /* replay detected */
        }
    }

    oseq_event_t *e = &reg->events[reg->event_count];
    ev_memset(e, 0, sizeof(*e));
    e->ordinal = reg->next_ordinal++;
    e->node_idx = node_idx;
    e->local_sequence = reg->nodes[node_idx].next_sequence++;
    e->num_parents = num_parents;
    e->status = OSEQ_STATUS_PENDING;

    if (schema) {
        copy_str(e->schema, schema, EV_SCHEMA_LEN);
    }
    e->schema_version = schema_version;

    if (payload_hash && hash_len > 0 && hash_len <= 32) {
        hash_copy(e->payload_hash, payload_hash, hash_len);
        e->payload_hash_len = hash_len;
    }

    /* Copy parent ordinals (these are event indices in our table) */
    for (uint8_t i = 0; i < num_parents; i++) {
        if (parent_ordinals && parent_ordinals[i] < reg->event_count) {
            e->parent_ordinals[i] = parent_ordinals[i];
        } else {
            /* Invalid parent — reject */
            e->parent_ordinals[i] = 0;
        }
    }

    /* Record in replay window */
    if (payload_hash && hash_len > 0) {
        oseq_record_replay(reg, payload_hash, hash_len);
    }

    return (int32_t)reg->event_count++;
}

bool oseq_commit_event(oseq_registry_t *reg, uint32_t event_idx) {
    if (!reg || event_idx >= reg->event_count) return false;
    oseq_event_t *e = &reg->events[event_idx];
    if (e->status != OSEQ_STATUS_PENDING) return false;

    /* Check for cycles */
    if (oseq_detect_cycle(reg, event_idx)) {
        e->status = OSEQ_STATUS_REJECTED;
        reg->cycles_detected++;
        reg->rejections++;
        return false;
    }

    e->status = OSEQ_STATUS_COMMITTED;
    reg->commits++;
    return true;
}

bool oseq_reject_event(oseq_registry_t *reg, uint32_t event_idx) {
    if (!reg || event_idx >= reg->event_count) return false;
    oseq_event_t *e = &reg->events[event_idx];
    if (e->status != OSEQ_STATUS_PENDING) return false;
    e->status = OSEQ_STATUS_REJECTED;
    reg->rejections++;
    return true;
}

/* ===== Causal Queries ===== */

bool oseq_is_ancestor(oseq_registry_t *reg, uint32_t ancestor_idx,
                      uint32_t descendant_idx) {
    if (!reg || ancestor_idx >= reg->event_count ||
        descendant_idx >= reg->event_count) return false;
    if (ancestor_idx == descendant_idx) return false;

    /* BFS from descendant up the parent chain */
    /* Use a simple stack-based DFS with cycle guard */
    uint32_t stack[OSEQ_MAX_DEPTH];
    uint32_t stack_top = 0;
    bool visited[OSEQ_MAX_EVENTS];
    for (uint32_t i = 0; i < reg->event_count; i++) visited[i] = false;

    stack[stack_top++] = descendant_idx;
    visited[descendant_idx] = true;

    while (stack_top > 0) {
        uint32_t cur = stack[--stack_top];
        oseq_event_t *e = &reg->events[cur];

        for (uint8_t i = 0; i < e->num_parents; i++) {
            uint32_t p = e->parent_ordinals[i];
            if (p == ancestor_idx) return true;
            if (p < reg->event_count && !visited[p]) {
                if (stack_top >= OSEQ_MAX_DEPTH) return false;
                visited[p] = true;
                stack[stack_top++] = p;
            }
        }
    }
    return false;
}

bool oseq_happens_before(oseq_registry_t *reg, uint32_t a_idx, uint32_t b_idx) {
    if (!reg || a_idx >= reg->event_count || b_idx >= reg->event_count)
        return false;
    /* a happens-before b if a is an ancestor of b */
    return oseq_is_ancestor(reg, a_idx, b_idx);
}

bool oseq_detect_cycle(oseq_registry_t *reg, uint32_t event_idx) {
    if (!reg || event_idx >= reg->event_count) return false;
    /* A cycle exists if event_idx is its own ancestor */
    /* Also check if any parent chain leads back to event_idx */
    uint32_t stack[OSEQ_MAX_DEPTH];
    uint32_t stack_top = 0;
    bool visited[OSEQ_MAX_EVENTS];
    for (uint32_t i = 0; i < reg->event_count; i++) visited[i] = false;

    /* Start from each parent and search for event_idx */
    oseq_event_t *e = &reg->events[event_idx];
    for (uint8_t i = 0; i < e->num_parents; i++) {
        uint32_t p = e->parent_ordinals[i];
        if (p == event_idx) return true;  /* direct self-loop */
        if (p < reg->event_count && !visited[p]) {
            visited[p] = true;
            stack[stack_top++] = p;
        }
    }

    while (stack_top > 0) {
        uint32_t cur = stack[--stack_top];
        oseq_event_t *ce = &reg->events[cur];
        for (uint8_t i = 0; i < ce->num_parents; i++) {
            uint32_t pp = ce->parent_ordinals[i];
            if (pp == event_idx) return true;  /* cycle found */
            if (pp < reg->event_count && !visited[pp]) {
                if (stack_top >= OSEQ_MAX_DEPTH) return false;
                visited[pp] = true;
                stack[stack_top++] = pp;
            }
        }
    }
    return false;
}

uint32_t oseq_get_depth(oseq_registry_t *reg, uint32_t event_idx) {
    if (!reg || event_idx >= reg->event_count) return 0;
    oseq_event_t *e = &reg->events[event_idx];
    if (e->num_parents == 0) return 0;

    uint32_t max_depth = 0;
    for (uint8_t i = 0; i < e->num_parents; i++) {
        uint32_t p = e->parent_ordinals[i];
        if (p < reg->event_count) {
            uint32_t d = oseq_get_depth(reg, p);
            if (d + 1 > max_depth) max_depth = d + 1;
        }
    }
    return max_depth;
}

/* ===== Event Retrieval ===== */

oseq_event_t *oseq_get_event(oseq_registry_t *reg, uint32_t idx) {
    if (!reg || idx >= reg->event_count) return NULL;
    return &reg->events[idx];
}

uint32_t oseq_count_by_status(oseq_registry_t *reg, oseq_status_t status) {
    if (!reg) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < reg->event_count; i++) {
        if (reg->events[i].status == status) count++;
    }
    return count;
}

/* ===== Utility ===== */


/* NAMESPACE SPLIT — READ BEFORE TOUCHING THESE TWO FUNCTIONS.
 *
 * oseq_core.c (linked into every kernel image today) exports functions with
 * these same two names, and they DISAGREE with the versions here:
 *
 *     oseq_core.c : oseq_is_valid_ordinal(o) == (o < UINT64_MAX)   -> 0 is VALID
 *     oseq.c      : oseq_is_valid_ordinal(o) == (o > 0)            -> 0 is NULL
 *
 * Both cannot be linked, and picking one silently would change behaviour for
 * whichever set of callers lost. The DAG here needs 0 to be a null sentinel
 * (a root event has no causal parent, encoded as parent ordinal 0), while the
 * existing callers of oseq_core were written against "0 is a real ordinal".
 *
 * So the DAG's versions are namespaced `oseq_dag_*` and oseq_core keeps the
 * plain names. Nothing existing changes behaviour; the DAG gets correct
 * semantics. Unifying them is a real migration, not a rename, and belongs to
 * whoever audits oseq_core's callers.
 */
bool oseq_dag_is_valid_ordinal(uint64_t ordinal) {
    return ordinal > 0;  /* 0 is reserved as null */
}

bool oseq_dag_is_later(uint64_t a, uint64_t b) {
    return a > b;
}

/* ===== Name Functions ===== */

const char *oseq_status_name(oseq_status_t status) {
    switch (status) {
        case OSEQ_STATUS_PENDING:   return "pending";
        case OSEQ_STATUS_COMMITTED: return "committed";
        case OSEQ_STATUS_REJECTED:  return "rejected";
        case OSEQ_STATUS_EXPIRED:   return "expired";
        default:                     return "unknown";
    }
}
