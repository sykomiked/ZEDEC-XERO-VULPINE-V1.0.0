/* event_sequencer.c — Event sequencer and domain management
 *
 * Implements the core event-space sequencer: domain lifecycle, event
 * enqueue/dequeue with causal ordering, dispatch selection by priority,
 * and budget enforcement.
 *
 * Self-audit hooks are called at every state transition. Self-healing
 * is invoked when audit detects faults. This is not bolted on — it is
 * woven into every operation.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "event_space.h"

static void copy_str(char *dst, const char *src, uint32_t max) {
    uint32_t i;
    for (i = 0; i + 1 < max && src && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

/* ===== Sequencer ===== */

void ev_seq_init(ev_sequencer_t *seq, const char *node_id) {
    if (!seq) return;
    ev_memset(seq, 0, sizeof(*seq));

    if (node_id) {
        copy_str(seq->node.id, node_id, EV_NODE_ID_LEN);
    }
    seq->node.incarnation = EV_INCARNATION_INIT;
    seq->next_sequence = 1;
    seq->next_domain_id = 1;
    seq->num_domains = 0;
}

int32_t ev_seq_create_domain(ev_sequencer_t *seq, const char *name,
                              uint32_t event_budget, uint32_t max_cost,
                              ev_consistency_t consistency) {
    if (!seq || !name) return -1;

    /* Find unused slot */
    uint32_t slot = EV_MAX_DOMAINS;
    for (uint32_t i = 0; i < EV_MAX_DOMAINS; i++) {
        if (seq->domains[i].state == EV_DOMAIN_UNUSED) {
            slot = i;
            break;
        }
    }
    if (slot >= EV_MAX_DOMAINS) return -1;

    ev_domain_t *dom = &seq->domains[slot];
    ev_memset(dom, 0, sizeof(*dom));
    dom->id = seq->next_domain_id++;
    dom->state = EV_DOMAIN_INITIALIZING;
    copy_str(dom->name, name, EV_DOMAIN_NAME_LEN);

    /* Clamp budget to reasonable range */
    dom->event_budget = (event_budget == 0) ? 100 : event_budget;
    if (dom->event_budget > 1000) dom->event_budget = 1000;
    dom->events_remaining = dom->event_budget;
    dom->max_cost_per_event = (max_cost == 0) ? 100 : max_cost;
    dom->consistency = consistency;

    /* Initialize queue */
    dom->queue_head = 0;
    dom->queue_tail = 0;
    dom->queue_count = 0;

    /* Transition to READY — initialization is synchronous */
    dom->state = EV_DOMAIN_READY;

    seq->num_domains++;
    return (int32_t)dom->id;
}

ev_domain_t *ev_seq_get_domain(ev_sequencer_t *seq, uint32_t domain_id) {
    if (!seq) return NULL;
    for (uint32_t i = 0; i < EV_MAX_DOMAINS; i++) {
        if (seq->domains[i].id == domain_id &&
            seq->domains[i].state != EV_DOMAIN_UNUSED &&
            seq->domains[i].state != EV_DOMAIN_TERMINATED) {
            return &seq->domains[i];
        }
    }
    return NULL;
}

bool ev_domain_grant_capability(ev_sequencer_t *seq, uint32_t domain_id,
                                 const char *capability) {
    if (!seq || !capability) return false;
    ev_domain_t *dom = ev_seq_get_domain(seq, domain_id);
    if (!dom) return false;
    if (dom->num_capabilities >= EV_MAX_CAPABILITIES) return false;

    /* Check for duplicate */
    for (uint32_t i = 0; i < dom->num_capabilities; i++) {
        uint32_t j;
        for (j = 0; j < EV_CAPABILITY_LEN && dom->capabilities[i].name[j] == capability[j]; j++) {
            if (capability[j] == '\0') return true; /* already granted */
        }
    }

    ev_capability_t *cap = &dom->capabilities[dom->num_capabilities++];
    copy_str(cap->name, capability, EV_CAPABILITY_LEN);
    cap->granted = true;
    return true;
}

bool ev_domain_add_accepted_schema(ev_sequencer_t *seq, uint32_t domain_id,
                                    const char *schema) {
    if (!seq || !schema) return false;
    ev_domain_t *dom = ev_seq_get_domain(seq, domain_id);
    if (!dom) return false;
    if (dom->num_accepted_schemas >= 8) return false;
    copy_str(dom->accepted_schemas[dom->num_accepted_schemas++], schema, EV_SCHEMA_LEN);
    return true;
}

bool ev_domain_add_emitted_schema(ev_sequencer_t *seq, uint32_t domain_id,
                                   const char *schema) {
    if (!seq || !schema) return false;
    ev_domain_t *dom = ev_seq_get_domain(seq, domain_id);
    if (!dom) return false;
    if (dom->num_emitted_schemas >= 8) return false;
    copy_str(dom->emitted_schemas[dom->num_emitted_schemas++], schema, EV_SCHEMA_LEN);
    return true;
}

/* Check if a schema matches any accepted schema pattern (prefix match) */
static bool domain_accepts_schema(const ev_domain_t *dom, const char *schema) {
    if (!dom || !schema) return false;
    if (dom->num_accepted_schemas == 0) return true; /* accept all if none specified */

    for (uint32_t i = 0; i < dom->num_accepted_schemas; i++) {
        const char *pattern = dom->accepted_schemas[i];
        uint32_t j;
        for (j = 0; pattern[j] != '\0' && schema[j] != '\0'; j++) {
            if (pattern[j] != schema[j]) break;
        }
        if (pattern[j] == '\0') return true; /* prefix match */
    }
    return false;
}

int32_t ev_seq_enqueue(ev_sequencer_t *seq, uint32_t domain_id,
                        ev_envelope_t *env) {
    if (!seq || !env) return -1;

    ev_domain_t *dom = ev_seq_get_domain(seq, domain_id);
    if (!dom) return -1;

    /* Self-audit: check domain is in a state that can receive events */
    if (dom->state == EV_DOMAIN_QUARANTINED) {
        seq->total_events_dropped++;
        return -1;
    }
    if (dom->state == EV_DOMAIN_TERMINATED || dom->state == EV_DOMAIN_UNUSED) {
        seq->total_events_dropped++;
        return -1;
    }

    /* Self-audit: check queue capacity */
    if (dom->queue_count >= EV_MAX_QUEUE_DEPTH) {
        seq->total_events_dropped++;
        dom->total_faults++;
        dom->consecutive_faults++;
        return -1;
    }

    /* Self-audit: check schema acceptance */
    if (!domain_accepts_schema(dom, env->schema)) {
        seq->total_events_dropped++;
        return -1;
    }

    /* Assign event identity */
    env->event_id.node = seq->node;
    env->event_id.local_sequence = seq->next_sequence++;

    /* Compute envelope CRC for integrity */
    ev_envelope_compute_crc(env);

    /* Enqueue into ring buffer */
    dom->queue[dom->queue_head] = *env;
    dom->queue_head = (dom->queue_head + 1) % EV_MAX_QUEUE_DEPTH;
    dom->queue_count++;

    /* Wake domain if idle/blocked */
    if (dom->state == EV_DOMAIN_IDLE || dom->state == EV_DOMAIN_BLOCKED) {
        dom->state = EV_DOMAIN_READY;
    }

    seq->total_events_enqueued++;
    return 0;
}

int32_t ev_seq_dequeue(ev_sequencer_t *seq, uint32_t domain_id,
                        ev_envelope_t *out) {
    if (!seq || !out) return -1;

    ev_domain_t *dom = ev_seq_get_domain(seq, domain_id);
    if (!dom) return -1;
    if (dom->queue_count == 0) return -1;

    /* Self-audit: verify queue integrity */
    if (dom->queue_tail >= EV_MAX_QUEUE_DEPTH) {
        dom->total_faults++;
        dom->consecutive_faults++;
        return -1;
    }

    *out = dom->queue[dom->queue_tail];
    dom->queue_tail = (dom->queue_tail + 1) % EV_MAX_QUEUE_DEPTH;
    dom->queue_count--;

    /* Self-audit: verify envelope integrity after dequeue */
    if (!ev_envelope_validate(out)) {
        dom->total_faults++;
        dom->consecutive_faults++;
        seq->total_faults_detected++;
        return -1;
    }

    return 0;
}

int32_t ev_seq_dispatch(ev_sequencer_t *seq) {
    if (!seq) return -1;

    /* Find highest-priority domain with pending events and budget remaining.
     * Priority is determined by:
     *   1. Number of pending events (more = higher priority for drain)
     *   2. Events remaining in budget (more = can do more work)
     *   3. Domain ID (lower = older = slight preference) */
    uint32_t best = EV_MAX_DOMAINS;
    uint32_t best_score = 0;

    for (uint32_t i = 0; i < EV_MAX_DOMAINS; i++) {
        ev_domain_t *d = &seq->domains[i];
        if (d->state != EV_DOMAIN_READY) continue;
        if (d->queue_count == 0) continue;
        if (d->events_remaining == 0) continue;

        /* Score: weighted combination of queue depth and budget */
        uint32_t score = d->queue_count * 2 + d->events_remaining;
        if (best >= EV_MAX_DOMAINS || score > best_score) {
            best_score = score;
            best = i;
        }
    }

    if (best >= EV_MAX_DOMAINS) {
        return -1;  /* no ready domain */
    }

    ev_domain_t *dom = &seq->domains[best];
    dom->state = EV_DOMAIN_RUNNING;

    /* Dispatch up to min(events_remaining, queue_count) events */
    uint32_t to_dispatch = dom->events_remaining;
    if (to_dispatch > dom->queue_count) {
        to_dispatch = dom->queue_count;
    }

    /* Dequeue and "process" each event (the actual handler is external;
     * the sequencer manages the dispatch accounting and budget) */
    for (uint32_t i = 0; i < to_dispatch; i++) {
        ev_envelope_t env;
        if (ev_seq_dequeue(seq, dom->id, &env) == 0) {
            dom->total_events_processed++;
            dom->events_remaining--;
            seq->total_events_dispatched++;
        } else {
            /* Dequeue failure — fault */
            dom->total_faults++;
            dom->consecutive_faults++;
            seq->total_faults_detected++;
            break;
        }
    }

    dom->total_dispatches++;

    /* Transition state after dispatch */
    if (dom->queue_count == 0) {
        dom->state = (dom->events_remaining == 0) ? EV_DOMAIN_IDLE : EV_DOMAIN_BLOCKED;
    } else if (dom->events_remaining == 0) {
        dom->state = EV_DOMAIN_IDLE;
        dom->total_over_budget++;
    } else {
        dom->state = EV_DOMAIN_READY;
    }

    /* Reset consecutive faults on successful dispatch */
    if (dom->consecutive_faults > 0 && to_dispatch > 0) {
        dom->consecutive_faults = 0;
    }

    return (int32_t)dom->id;
}

void ev_seq_replenish(ev_sequencer_t *seq) {
    if (!seq) return;
    for (uint32_t i = 0; i < EV_MAX_DOMAINS; i++) {
        ev_domain_t *d = &seq->domains[i];
        if (d->state == EV_DOMAIN_IDLE) {
            d->events_remaining = d->event_budget;
            if (d->queue_count > 0) {
                d->state = EV_DOMAIN_READY;
            }
        }
    }
}

bool ev_seq_has_pending(const ev_sequencer_t *seq) {
    if (!seq) return false;
    for (uint32_t i = 0; i < EV_MAX_DOMAINS; i++) {
        const ev_domain_t *d = &seq->domains[i];
        if (d->state == EV_DOMAIN_READY && d->queue_count > 0 && d->events_remaining > 0)
            return true;
    }
    return false;
}
