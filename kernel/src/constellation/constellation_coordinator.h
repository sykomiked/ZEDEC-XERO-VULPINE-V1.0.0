/* constellation_coordinator.h — ZXV Constellation Coordinator
 *
 * The Constellation Coordinator manages cross-domain event routing,
 * node discovery, executor profiles, and failure detection across
 * the heterogeneous multi-ISA constellation.
 *
 * Responsibilities:
 *   1. Node registry — discover and track event domains across ISAs
 *   2. Event routing — route events to the appropriate domain
 *   3. Executor profiles — normalized hardware capability and cost model
 *   4. Failure detection — detect crashed/rebooted/partitioned nodes
 *   5. Service placement — assign work according to capacity and locality
 *
 * Design principles:
 *   - Not a permanent single point of failure: consumer device has one
 *     primary with recoverable replica; server cluster has replicated
 *     control group with consensus
 *   - Nodes enter quarantine until health checks pass
 *   - Capability-based: nodes declare what they can do, not what they are
 *   - Normalized cost: work units, not raw cycle counts
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef CONSTELLATION_COORDINATOR_H
#define CONSTELLATION_COORDINATOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../event_space/event_space.h"

/* ===== Constants ===== */

#define CC_MAX_NODES          32    /* max nodes in constellation */
#define CC_MAX_ROUTES         64    /* max routing entries */
#define CC_MAX_EXECUTORS      32    /* max executor profiles */
#define CC_MAX_NAME_LEN       32    /* node/service name length */
#define CC_MAX_SCHEMAS_PER_NODE 8   /* schemas a node can handle */
#define CC_MAX_CAPABILITIES   8     /* capabilities per executor */
#define CC_NODE_NAME_LEN      EV_NODE_ID_LEN

/* ===== Node States ===== */

typedef enum {
    CC_NODE_UNKNOWN     = 0,
    CC_NODE_DISCOVERED  = 1,   /* detected but not authenticated */
    CC_NODE_AUTHENTICATED = 2, /* authenticated, in quarantine */
    CC_NODE_QUARANTINED  = 3,  /* health checks pending */
    CC_NODE_ACTIVE      = 4,   /* fully operational */
    CC_NODE_DEGRADED    = 5,   /* partially functional */
    CC_NODE_FAILED      = 6,   /* detected failure */
    CC_NODE_REMOVED     = 7    /* removed from constellation */
} cc_node_state_t;

/* ===== Node Registry ===== */

typedef struct cc_node {
    char id[CC_NODE_NAME_LEN];     /* e.g., "arm64.cluster0" */
    char arch[16];                 /* "x86_64", "arm64", "riscv64" */
    uint32_t incarnation;          /* changes on reboot */
    cc_node_state_t state;

    /* Schemas this node can handle */
    char schemas[CC_MAX_SCHEMAS_PER_NODE][EV_SCHEMA_LEN];
    uint32_t num_schemas;

    /* Health tracking */
    uint64_t last_heartbeat_sequence;
    uint32_t consecutive_failures;
    uint32_t total_events_routed;
    uint32_t total_events_failed;

    bool registered;
} cc_node_t;

/* ===== Executor Profile ===== */

typedef struct cc_executor {
    char node_id[CC_NODE_NAME_LEN];
    char service[CC_MAX_NAME_LEN];  /* e.g., "zxv.storage" */

    /* Supported event schemas */
    char schemas[CC_MAX_SCHEMAS_PER_NODE][EV_SCHEMA_LEN];
    uint32_t num_schemas;

    /* Normalized performance */
    uint32_t cost_p50;             /* median cost units per event */
    uint32_t cost_p99;             /* p99 cost units per event */

    /* Constraints */
    uint32_t memory_mb;
    uint32_t thermal_state;        /* 0=normal, 1=warm, 2=hot */
    uint32_t trust_level;          /* 0=none, 1=authenticated, 2=confidential */

    /* Capabilities */
    char capabilities[CC_MAX_CAPABILITIES][EV_SCHEMA_LEN];
    uint32_t num_capabilities;

    /* Stats */
    uint32_t events_assigned;
    uint32_t events_completed;
    uint32_t events_failed;

    bool registered;
} cc_executor_t;

/* ===== Routing Table ===== */

typedef struct cc_route {
    char schema[EV_SCHEMA_LEN];    /* event schema to route */
    uint16_t min_version;          /* minimum schema version */
    char target_node[CC_NODE_NAME_LEN];
    uint32_t priority;             /* higher = preferred */
    bool active;
} cc_route_t;

/* ===== Constellation Coordinator ===== */

typedef struct cc_coordinator {
    /* Local sequencer reference (for sequence numbers) */
    ev_node_id_t local_node;

    /* Node registry */
    cc_node_t nodes[CC_MAX_NODES];
    uint32_t num_nodes;

    /* Executor profiles */
    cc_executor_t executors[CC_MAX_EXECUTORS];
    uint32_t num_executors;

    /* Routing table */
    cc_route_t routes[CC_MAX_ROUTES];
    uint32_t num_routes;

    /* Statistics */
    uint64_t total_events_routed;
    uint64_t total_events_dropped;
    uint64_t total_node_failures;
    uint64_t total_node_recoveries;
    uint64_t total_reassignments;
} cc_coordinator_t;

/* ===== API ===== */

/* Initialize the coordinator */
void cc_coordinator_init(cc_coordinator_t *cc, const char *local_node_id);

/* Node management */
int32_t cc_register_node(cc_coordinator_t *cc, const char *node_id,
                          const char *arch, uint32_t incarnation);
bool cc_node_add_schema(cc_coordinator_t *cc, uint32_t node_idx,
                         const char *schema);
cc_node_t *cc_get_node(cc_coordinator_t *cc, const char *node_id);
cc_node_t *cc_get_node_by_idx(cc_coordinator_t *cc, uint32_t idx);

/* Node state transitions */
bool cc_node_authenticate(cc_coordinator_t *cc, uint32_t node_idx);
bool cc_node_activate(cc_coordinator_t *cc, uint32_t node_idx);
bool cc_node_fail(cc_coordinator_t *cc, uint32_t node_idx);
bool cc_node_recover(cc_coordinator_t *cc, uint32_t node_idx);
bool cc_node_remove(cc_coordinator_t *cc, uint32_t node_idx);

/* Heartbeat */
bool cc_node_heartbeat(cc_coordinator_t *cc, uint32_t node_idx,
                        uint64_t sequence);

/* Executor management */
int32_t cc_register_executor(cc_coordinator_t *cc, const char *node_id,
                              const char *service,
                              uint32_t cost_p50, uint32_t cost_p99);
bool cc_executor_add_schema(cc_coordinator_t *cc, uint32_t exec_idx,
                             const char *schema);
bool cc_executor_add_capability(cc_coordinator_t *cc, uint32_t exec_idx,
                                 const char *capability);
cc_executor_t *cc_get_executor(cc_coordinator_t *cc, uint32_t idx);

/* Find best executor for a schema */
int32_t cc_find_executor(cc_coordinator_t *cc, const char *schema,
                          uint16_t min_version);

/* Routing */
bool cc_add_route(cc_coordinator_t *cc, const char *schema,
                   uint16_t min_version, const char *target_node,
                   uint32_t priority);

/* Route an event — find the target node for a schema */
cc_node_t *cc_route_event(cc_coordinator_t *cc, const char *schema,
                           uint16_t schema_version);

/* Check all nodes for failures (timeout-based) */
uint32_t cc_check_health(cc_coordinator_t *cc, uint64_t current_sequence,
                          uint64_t timeout_sequences);

/* Get statistics */
void cc_update_stats(cc_coordinator_t *cc);

#endif /* CONSTELLATION_COORDINATOR_H */
