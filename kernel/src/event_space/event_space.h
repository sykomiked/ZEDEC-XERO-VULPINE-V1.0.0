/* event_space.h — ZXV Event-Space Infrastructure
 *
 * Core event-domain infrastructure for the ZXV heterogeneous constellation
 * architecture. Provides:
 *
 *   1. Canonical event envelope (ZXB encoding) — architecture-neutral
 *   2. Event sequencer with causal ordering and incarnation management
 *   3. Event domains (state arena + input queue + capabilities + budget)
 *   4. Self-audit framework (integrity checks, invariant verification)
 *   5. Self-healing framework (fault detection, recovery, quarantine)
 *
 * Design principles:
 *   - Self-audit is built into every state transition, not bolted on
 *   - Self-healing quarantines faulty domains, never silently corrupts state
 *   - Every event has a causal parent and a sequential ID
 *   - Event budgets are enforced; over-budget handlers are suspended
 *   - All structures are freestanding (no libc dependency beyond memset/memcpy)
 *   - Paraconsistent logic: contradictions are preserved, not hidden
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef EVENT_SPACE_H
#define EVENT_SPACE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Freestanding memory operations — provided inline because the kernel
 * build environment may have an empty <string.h> stub. */
static inline void *ev_memset(void *dst, int c, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (uint32_t i = 0; i < n; i++) d[i] = (uint8_t)c;
    return dst;
}

static inline void *ev_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

/* ===== Constants ===== */

#define EV_NODE_ID_LEN      32    /* max node identifier string */
#define EV_SCHEMA_LEN       64    /* max schema name length */
#define EV_DOMAIN_NAME_LEN  32    /* max domain name length */
#define EV_CAPABILITY_LEN   64    /* max capability string */
#define EV_MAX_DOMAINS      32    /* max event domains per node */
#define EV_MAX_QUEUE_DEPTH  64    /* max events per domain input queue */
#define EV_MAX_CAUSAL_PARENTS 4   /* max causal parents per event */
#define EV_MAX_CAPABILITIES 16    /* max capabilities per domain */
#define EV_MAX_AUDIT_CHECKS 32    /* max registered audit checks */
#define EV_MAX_HEALING_RULES 16   /* max healing rules */
#define EV_PAYLOAD_MAX      256   /* max event payload bytes */
#define EV_INCARNATION_INIT 1     /* initial incarnation number */

/* ===== Event Identity ===== */

/* Node identifier — identifies a native kernel instance in the constellation */
typedef struct ev_node_id {
    char id[EV_NODE_ID_LEN];      /* e.g., "arm64.cluster0" */
    uint32_t incarnation;         /* changes on reboot */
} ev_node_id_t;

/* Global event identifier — (node, incarnation, local_sequence) */
typedef struct ev_event_id {
    ev_node_id_t node;
    uint64_t local_sequence;      /* monotonically increasing within incarnation */
} ev_event_id_t;

/* ===== Event Envelope (ZXB — ZXV Binary encoding) ===== */

/* Delivery semantics for an event */
typedef enum {
    EV_DELIVERY_AT_MOST_ONCE  = 0,
    EV_DELIVERY_AT_LEAST_ONCE = 1,
    EV_DELIVERY_EFFECTIVELY_ONCE = 2,  /* idempotent + dedup */
    EV_DELIVERY_BEST_EFFORT   = 3,
    EV_DELIVERY_TRANSACTIONAL = 4
} ev_delivery_t;

/* Event priority levels */
typedef enum {
    EV_PRIORITY_CRITICAL = 0,   /* system update, security policy */
    EV_PRIORITY_HIGH     = 1,   /* user interaction, fault response */
    EV_PRIORITY_NORMAL   = 2,   /* application events */
    EV_PRIORITY_LOW      = 3,   /* background, telemetry */
    EV_PRIORITY_IDLE     = 4    /* deferred work */
} ev_priority_t;

/* Canonical event envelope — architecture-neutral */
typedef struct ev_envelope {
    /* Identity */
    ev_event_id_t event_id;

    /* Causal ordering */
    ev_event_id_t causal_parents[EV_MAX_CAUSAL_PARENTS];
    uint8_t num_causal_parents;

    /* Schema */
    char schema[EV_SCHEMA_LEN];       /* e.g., "zxv.storage.read.request" */
    uint16_t schema_version;

    /* Routing */
    char sender_domain[EV_DOMAIN_NAME_LEN];
    char sender_capability[EV_CAPABILITY_LEN];
    char destination_service[EV_DOMAIN_NAME_LEN];

    /* Payload */
    uint8_t payload[EV_PAYLOAD_MAX];
    uint16_t payload_len;
    uint8_t payload_hash[32];         /* SHA-256 of payload (if payload_len > 0) */

    /* Constraints */
    ev_delivery_t delivery;
    ev_priority_t priority;
    uint32_t max_cost;                /* max event-cost units */
    uint64_t deadline_sequence;       /* logical deadline (0 = none) */
    bool idempotent;

    /* Self-audit: integrity checksum over envelope header fields */
    uint32_t header_crc;
} ev_envelope_t;

/* ===== Event Domain ===== */

/* Domain states */
typedef enum {
    EV_DOMAIN_UNUSED      = 0,
    EV_DOMAIN_INITIALIZING = 1,
    EV_DOMAIN_READY       = 2,   /* has pending events, eligible for dispatch */
    EV_DOMAIN_RUNNING     = 3,   /* currently dispatching */
    EV_DOMAIN_BLOCKED     = 4,   /* waiting on external event */
    EV_DOMAIN_IDLE        = 5,   /* budget exhausted or no events */
    EV_DOMAIN_QUARANTINED = 6,   /* isolated due to fault — self-healing */
    EV_DOMAIN_TERMINATED  = 7    /* permanently stopped */
} ev_domain_state_t;

/* Consistency model for a domain's state */
typedef enum {
    EV_CONSISTENCY_LOCAL       = 0,  /* no cross-domain coordination */
    EV_CONSISTENCY_CAUSAL      = 1,  /* causal ordering with partners */
    EV_CONSISTENCY_LINEARIZABLE = 2, /* strong consistency */
    EV_CONSISTENCY_CONSENSUS   = 3   /* requires committed global order */
} ev_consistency_t;

/* Capability descriptor */
typedef struct ev_capability {
    char name[EV_CAPABILITY_LEN];   /* e.g., "storage.documents.read" */
    bool granted;
} ev_capability_t;

/* Event domain — the fundamental execution unit */
typedef struct ev_domain {
    uint32_t id;
    char name[EV_DOMAIN_NAME_LEN];
    ev_domain_state_t state;

    /* Input event queue (ring buffer) */
    ev_envelope_t queue[EV_MAX_QUEUE_DEPTH];
    uint32_t queue_head;       /* next slot to write */
    uint32_t queue_tail;       /* next slot to read */
    uint32_t queue_count;      /* current depth */

    /* Event budget */
    uint32_t event_budget;         /* max events per dispatch */
    uint32_t events_remaining;     /* events left in current dispatch */
    uint64_t total_events_processed;
    uint32_t max_cost_per_event;   /* cost units */

    /* Capabilities */
    ev_capability_t capabilities[EV_MAX_CAPABILITIES];
    uint32_t num_capabilities;

    /* Consistency model */
    ev_consistency_t consistency;

    /* Accepted/emitted schema patterns (simplified: prefix match) */
    char accepted_schemas[8][EV_SCHEMA_LEN];
    uint32_t num_accepted_schemas;
    char emitted_schemas[8][EV_SCHEMA_LEN];
    uint32_t num_emitted_schemas;

    /* Statistics for self-audit */
    uint64_t total_dispatches;
    uint64_t total_faults;
    uint64_t total_over_budget;
    uint64_t total_quarantines;

    /* Self-healing: fault tracking */
    uint32_t consecutive_faults;
    uint32_t quarantine_until_sequence;  /* resume after this sequence */
} ev_domain_t;

/* ===== Event Sequencer ===== */

typedef struct ev_sequencer {
    ev_node_id_t node;                /* this node's identity */
    uint64_t next_sequence;           /* next local sequence number */

    /* Registered domains */
    ev_domain_t domains[EV_MAX_DOMAINS];
    uint32_t num_domains;
    uint32_t next_domain_id;

    /* Global statistics */
    uint64_t total_events_enqueued;
    uint64_t total_events_dispatched;
    uint64_t total_events_dropped;
    uint64_t total_faults_detected;
    uint64_t total_domains_quarantined;
    uint64_t total_domains_recovered;

    /* Audit state */
    uint64_t total_audit_checks_run;
    uint64_t total_audit_failures;
    uint32_t last_audit_sequence;
} ev_sequencer_t;

/* ===== Self-Audit Framework ===== */

/* Audit check result */
typedef enum {
    EV_AUDIT_PASS     = 0,
    EV_AUDIT_WARN     = 1,
    EV_AUDIT_FAIL     = 2,
    EV_AUDIT_QUARANTINE = 3  /* fail so hard the domain must be quarantined */
} ev_audit_result_t;

/* Audit check function signature */
typedef ev_audit_result_t (*ev_audit_check_fn)(ev_sequencer_t *seq,
                                                ev_domain_t *domain,
                                                void *context);

/* Registered audit check */
typedef struct ev_audit_check {
    char name[EV_DOMAIN_NAME_LEN];
    ev_audit_check_fn fn;
    void *context;
    bool enabled;
    uint64_t pass_count;
    uint64_t fail_count;
} ev_audit_check_t;

/* Audit registry */
typedef struct ev_audit {
    ev_audit_check_t checks[EV_MAX_AUDIT_CHECKS];
    uint32_t num_checks;

    /* Invariant: queue_count must never exceed EV_MAX_QUEUE_DEPTH */
    bool invariant_queue_overflow;

    /* Invariant: events_remaining must never exceed event_budget */
    bool invariant_budget_exceeded;

    /* Invariant: no domain in RUNNING state if sequencer is not dispatching */
    bool invariant_orphan_running;

    /* Last full audit timestamp (sequence number) */
    uint64_t last_full_audit_sequence;
} ev_audit_t;

/* ===== Self-Healing Framework ===== */

/* Healing action */
typedef enum {
    EV_HEAL_NONE        = 0,   /* no action needed */
    EV_HEAL_WARN        = 1,   /* log warning, continue */
    EV_HEAL_SUSPEND     = 2,   /* suspend domain temporarily */
    EV_HEAL_QUARANTINE  = 3,   /* isolate domain, stop dispatching */
    EV_HEAL_RESTART     = 4,   /* restart domain (re-init state) */
    EV_HEAL_TERMINATE   = 5    /* permanently terminate domain */
} ev_heal_action_t;

/* Healing rule: maps audit result + fault count to action */
typedef struct ev_healing_rule {
    ev_audit_result_t trigger_result;
    uint32_t trigger_fault_threshold;   /* consecutive faults to trigger */
    ev_heal_action_t action;
    uint32_t suspend_duration;          /* sequences to suspend (for SUSPEND) */
} ev_healing_rule_t;

/* Healing engine */
typedef struct ev_healing {
    ev_healing_rule_t rules[EV_MAX_HEALING_RULES];
    uint32_t num_rules;

    /* Recovery tracking */
    uint64_t total_suspensions;
    uint64_t total_quarantines;
    uint64_t total_restarts;
    uint64_t total_terminations;
    uint64_t total_auto_recoveries;
} ev_healing_t;

/* ===== API: Event Envelope ===== */

/* Initialize an event envelope with identity and schema */
void ev_envelope_init(ev_envelope_t *env, const ev_node_id_t *node,
                      uint64_t sequence, const char *schema,
                      uint16_t schema_version);

/* Add a causal parent to an envelope */
bool ev_envelope_add_causal_parent(ev_envelope_t *env, const ev_event_id_t *parent);

/* Set payload on an envelope (computes SHA-256 hash if payload is non-empty) */
bool ev_envelope_set_payload(ev_envelope_t *env, const uint8_t *data, uint16_t len);

/* Compute and verify header CRC */
void ev_envelope_compute_crc(ev_envelope_t *env);
bool ev_envelope_verify_crc(const ev_envelope_t *env);

/* Full integrity check on an envelope */
bool ev_envelope_validate(const ev_envelope_t *env);

/* ===== API: Event Sequencer ===== */

/* Initialize the sequencer with a node identity */
void ev_seq_init(ev_sequencer_t *seq, const char *node_id);

/* Create an event domain.
 * Returns domain ID on success, -1 on failure. */
int32_t ev_seq_create_domain(ev_sequencer_t *seq, const char *name,
                              uint32_t event_budget, uint32_t max_cost,
                              ev_consistency_t consistency);

/* Grant a capability to a domain */
bool ev_domain_grant_capability(ev_sequencer_t *seq, uint32_t domain_id,
                                 const char *capability);

/* Register an accepted schema pattern for a domain */
bool ev_domain_add_accepted_schema(ev_sequencer_t *seq, uint32_t domain_id,
                                    const char *schema);

/* Register an emitted schema pattern for a domain */
bool ev_domain_add_emitted_schema(ev_sequencer_t *seq, uint32_t domain_id,
                                   const char *schema);

/* Enqueue an event to a domain's input queue.
 * The event is assigned a sequence number and causal parents are recorded.
 * Returns 0 on success, -1 on failure (queue full, domain not found, etc.) */
int32_t ev_seq_enqueue(ev_sequencer_t *seq, uint32_t domain_id,
                        ev_envelope_t *env);

/* Dequeue the next event from a domain's queue.
 * Returns 0 on success, -1 if queue is empty or domain not ready. */
int32_t ev_seq_dequeue(ev_sequencer_t *seq, uint32_t domain_id,
                        ev_envelope_t *out);

/* Dispatch the next eligible domain.
 * Selects the highest-priority domain with pending events and
 * returns its ID, or -1 if no domain is ready. */
int32_t ev_seq_dispatch(ev_sequencer_t *seq);

/* Replenish event budgets for all IDLE domains */
void ev_seq_replenish(ev_sequencer_t *seq);

/* Check if any domain has pending events */
bool ev_seq_has_pending(const ev_sequencer_t *seq);

/* Get domain by ID. Returns NULL if not found. */
ev_domain_t *ev_seq_get_domain(ev_sequencer_t *seq, uint32_t domain_id);

/* ===== API: Self-Audit ===== */

/* Initialize the audit framework with default invariants */
void ev_audit_init(ev_audit_t *audit);

/* Register a custom audit check */
bool ev_audit_register_check(ev_audit_t *audit, const char *name,
                              ev_audit_check_fn fn, void *context);

/* Run all enabled audit checks on a specific domain.
 * Returns the worst result across all checks. */
ev_audit_result_t ev_audit_check_domain(ev_sequencer_t *seq,
                                         ev_audit_t *audit,
                                         ev_domain_t *domain);

/* Run a full system audit (all domains + sequencer invariants).
 * Returns the worst result found. */
ev_audit_result_t ev_audit_check_system(ev_sequencer_t *seq,
                                         ev_audit_t *audit);

/* Built-in audit checks */
ev_audit_result_t ev_audit_check_queue_overflow(ev_sequencer_t *seq,
                                                 ev_domain_t *domain,
                                                 void *context);
ev_audit_result_t ev_audit_check_budget_invariant(ev_sequencer_t *seq,
                                                   ev_domain_t *domain,
                                                   void *context);
ev_audit_result_t ev_audit_check_fault_rate(ev_sequencer_t *seq,
                                             ev_domain_t *domain,
                                             void *context);
ev_audit_result_t ev_audit_check_quarantine_expiry(ev_sequencer_t *seq,
                                                    ev_domain_t *domain,
                                                    void *context);

/* ===== API: Self-Healing ===== */

/* Initialize the healing framework with default rules */
void ev_healing_init(ev_healing_t *healing);

/* Register a custom healing rule */
bool ev_healing_register_rule(ev_healing_t *healing,
                               ev_audit_result_t trigger,
                               uint32_t fault_threshold,
                               ev_heal_action_t action,
                               uint32_t suspend_duration);

/* Apply healing rules based on audit result.
 * Performs the action on the domain and updates statistics. */
ev_heal_action_t ev_healing_apply(ev_sequencer_t *seq,
                                   ev_healing_t *healing,
                                   ev_domain_t *domain,
                                   ev_audit_result_t audit_result);

/* Check if a quarantined/suspended domain is eligible for recovery */
bool ev_healing_check_recovery(ev_sequencer_t *seq,
                                ev_healing_t *healing,
                                ev_domain_t *domain);

/* ===== API: Integrated Audit-Heal Cycle ===== */

/* Run audit + healing on a single domain.
 * This is the main self-audit/self-healing entry point, designed to be
 * called after each domain dispatch or at periodic intervals. */
ev_heal_action_t ev_self_audit_heal_domain(ev_sequencer_t *seq,
                                            ev_audit_t *audit,
                                            ev_healing_t *healing,
                                            ev_domain_t *domain);

/* Run audit + healing on the entire system.
 * Returns the worst action taken across all domains. */
ev_heal_action_t ev_self_audit_heal_system(ev_sequencer_t *seq,
                                            ev_audit_t *audit,
                                            ev_healing_t *healing);

#endif /* EVENT_SPACE_H */
