/* compute_fabric.h — ZXV Compute Fabric Compound Module
 *
 * The Compute Fabric unifies all compute, execution, and scheduling modules
 * into a single coherent fabric for heterogeneous multi-ISA computation,
 * event-driven execution, and capability-based resource management.
 *
 * Sub-modules integrated:
 *   1. Cellular Multikernel — Per-cell kernels with message passing
 *   2. Hypercube — 4D spacetime scene graph for compute topology
 *   3. Event Transport — Reliable event delivery across domains
 *   4. Event Space — Canonical event envelopes, self-audit, self-healing
 *   4. Scheduler — Multi-queue, priority-based scheduling
 *   5. Dual Space — User/kernel space separation with capabilities
 *   6. EL0 Userspace — User-mode execution environment
 *   7. Constellation Coordinator — Cross-ISA node coordination
 *   8. Orbital Fabric — Schema translation and hardware abstraction
 *   9. Yantra Fabric — Software-defined hardware fabric
 *   10. Smart Adapter — Generic HW/FW adapter
 *   11. Mesh Net — P2P mesh for distributed compute
 *
 * Design principles:
 * - Compute is event-driven, not thread-driven
 * - Each cell is a self-contained kernel with its own scheduler
 * - Message passing is the only IPC (no shared memory)
 * - Capabilities gate all resource access
 * - Heterogeneous ISA support via Orbital Elevator/Compat
 * - Hardware abstraction via Yantra + Smart Adapter
 * - Distributed compute via Mesh Net trade routes
 * - Paraconsistent logic (LPRES) for all attestations
 * - M5 coverage hyperbola enforcement on all compute operations
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef COMPUTE_FABRIC_H
#define COMPUTE_FABRIC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "cellular_multikernel.h"
#include "hypercube/hypercube.h"
#include "event_transport.h"
#include "event_space.h"
#include "sched.h"
#include "dual_space.h"
#include "el0_userspace.h"
#include "constellation_coordinator.h"
#include "orbital_fabric.h"
#include "yantra_fabric.h"
#include "smart_adapter.h"
#include "smart_adapter_integration.h"
#include "mesh_net.h"
#include "identity_fabric.h"
#include "financial_fabric.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"

/* ===== Constants ===== */

#define CF_MAX_CELLS             64
#define CF_MAX_COMPUTE_DOMAINS   128
#define CF_MAX_EVENT_HANDLERS    256
#define CF_MAX_SCHED_QUEUES      32
#define CF_MAX_RESOURCES         512
#define CF_MAX_NAME_LEN          64

/* ===== Compute Cell ===== */

typedef struct cf_cell {
    uint32_t id;
    char name[CF_MAX_NAME_LEN];
    char arch[16];                   /* "arm64", "x86_64", "riscv64" */
    
    /* Cellular Multikernel */
    cmk_cell_t *cmk_cell;            /* Cell kernel instance */
    
    /* Event Space domain for this cell */
    ev_domain_t *ev_domain;
    
    /* Scheduler */
    sched_queue_t *sched_queues[CF_MAX_SCHED_QUEUES];
    uint32_t num_sched_queues;
    
    /* Hypercube position */
    struct {
        int32_t x, y, z, t;          /* 4D coordinates */
        surplus_real_t compute_capacity;
        surplus_real_t memory_capacity;
        surplus_real_t network_capacity;
    } hypercube_pos;
    
    /* Resources */
    struct {
        uint32_t resource_ids[CF_MAX_RESOURCES];
        uint32_t num_resources;
    } resources;
    
    /* Event handlers registered */
    uint32_t handler_ids[CF_MAX_EVENT_HANDLERS];
    uint32_t num_handlers;
    
    /* Constellation node */
    cc_node_t *cc_node;
    
    /* Yantra devices in this cell */
    uint32_t yf_device_ids[32];
    uint32_t num_yf_devices;
    
    /* Smart Adapter devices */
    uint32_t sa_device_ids[32];
    uint32_t num_sa_devices;
    
    /* Mesh networks */
    uint32_t mn_network_ids[16];
    uint32_t num_mn_networks;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* Paraconsistent state */
    lpres_state_t attestation;
    bool active;
    bool healthy;
} cf_cell_t;

/* ===== Compute Domain (Event-driven execution unit) ===== */

typedef struct cf_compute_domain {
    uint32_t id;
    char name[CF_MAX_NAME_LEN];
    uint32_t cell_id;                /* Host cell */
    
    /* Event Space domain */
    ev_domain_t *ev_domain;
    
    /* Capabilities */
    ev_capability_t capabilities[EV_MAX_CAPABILITIES];
    uint32_t num_capabilities;
    
    /* Event handlers */
    struct {
        char schema[EV_SCHEMA_LEN];
        uint16_t schema_version;
        int32_t (*handler_fn)(ev_envelope_t *env, void *context);
        void *context;
        uint32_t max_cost;
        bool active;
    } handlers[CF_MAX_EVENT_HANDLERS];
    uint32_t num_handlers;
    
    /* Scheduling */
    uint32_t sched_queue_id;
    ev_priority_t priority;
    uint32_t event_budget;
    uint32_t max_cost_per_event;
    
    /* State */
    uint64_t events_processed;
    uint64_t events_failed;
    uint64_t total_cost;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* Paraconsistent state */
    lpres_state_t attestation;
    bool active;
} cf_compute_domain_t;

/* ===== Resource ===== */

typedef struct cf_resource {
    uint32_t id;
    char name[CF_MAX_NAME_LEN];
    char type[CF_MAX_NAME_LEN];      /* "cpu", "memory", "gpu", "fpga", "radio", "storage" */
    
    /* Yantra device backing */
    yf_device_t *yf_device;
    
    /* Smart Adapter device backing */
    smart_device_t *sa_device;
    
    /* Capacity */
    surplus_real_t total_capacity;
    surplus_real_t available_capacity;
    surplus_real_t reserved_capacity;
    
    /* Allocation */
    struct {
        uint32_t domain_id;
        surplus_real_t amount;
        uint64_t expiry_tick;
    } allocations[32];
    uint32_t num_allocations;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* Paraconsistent state */
    lpres_state_t attestation;
    bool active;
} cf_resource_t;

/* ===== Event Handler ===== */

typedef struct cf_event_handler {
    uint32_t id;
    uint32_t domain_id;
    char schema[EV_SCHEMA_LEN];
    uint16_t schema_version;
    int32_t (*handler_fn)(ev_envelope_t *env, void *context);
    void *context;
    uint32_t max_cost;
    uint64_t invocations;
    uint64_t failures;
    lpres_state_t attestation;
    bool active;
} cf_event_handler_t;

/* ===== Compute Fabric ===== */

typedef struct compute_fabric {
    /* Core sub-modules */
    cmk_multikernel_t multikernel;   /* Cellular Multikernel */
    hypercube_t hypercube;           /* Hypercube Scene Graph */
    event_transport_t transport;     /* Event Transport */
    ev_sequencer_t sequencer;        /* Event Sequencer */
    ev_audit_t audit;                /* Self-Audit */
    ev_healing_t healing;            /* Self-Healing */
    sched_t scheduler;               /* Scheduler */
    dual_space_t dual_space;         /* Dual Space */
    el0_userspace_t el0;             /* EL0 Userspace */
    cc_coordinator_t constellation;  /* Constellation Coordinator */
    
    /* Integration references */
    orbital_fabric_t *orbital;       /* Orbital Fabric */
    yf_fabric_t *yantra;             /* Yantra Fabric */
    smart_adapter_registry_t *sa_registry; /* Smart Adapter Registry */
    sai_integration_fabric_t *sai_fabric;  /* SA Integration */
    mesh_net_t *mesh;                /* Mesh Net */
    identity_fabric_t *identity;     /* Identity Fabric */
    financial_fabric_t *financial;   /* Financial Fabric */
    
    /* Fabric-level state */
    cf_cell_t cells[CF_MAX_CELLS];
    uint32_t num_cells;
    uint32_t next_cell_id;
    
    cf_compute_domain_t domains[CF_MAX_COMPUTE_DOMAINS];
    uint32_t num_domains;
    uint32_t next_domain_id;
    
    cf_resource_t resources[CF_MAX_RESOURCES];
    uint32_t num_resources;
    
    cf_event_handler_t handlers[CF_MAX_EVENT_HANDLERS];
    uint32_t num_handlers;
    
    /* Global statistics */
    struct {
        uint64_t total_cells_created;
        uint64_t total_domains_created;
        uint64_t total_events_dispatched;
        uint64_t total_events_failed;
        uint64_t total_handler_invocations;
        uint64_t total_resources_allocated;
        uint64_t total_schedule_decisions;
        uint64_t total_migrations;
        uint64_t total_self_audits;
        uint64_t total_healings;
    } stats;
    
    /* Paraconsistent global state */
    lpres_state_t global_attestation;
    bool global_safety_gate;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    surplus_real_t min_coverage_ratio;
    
    /* Configuration */
    struct {
        bool auto_balance_load;
        bool enable_migration;
        uint32_t audit_interval;
        uint32_t health_check_interval;
        surplus_real_t min_cell_coverage;
        surplus_real_t min_domain_coverage;
    } config;
    
    bool initialized;
} compute_fabric_t;

/* ===== API ===== */

/* Initialize the Compute Fabric */
void cf_init(compute_fabric_t *fabric,
             orbital_fabric_t *orbital,
             yf_fabric_t *yantra,
             smart_adapter_registry_t *sa_registry,
             sai_integration_fabric_t *sai_fabric,
             mesh_net_t *mesh,
             identity_fabric_t *identity,
             financial_fabric_t *financial);

/* Register built-in compute modules */
void cf_register_builtins(compute_fabric_t *fabric);

/* ===== Cell Management ===== */

int32_t cf_create_cell(compute_fabric_t *fabric, const char *name,
                       const char *arch,
                       int32_t hx, int32_t hy, int32_t hz, int32_t ht);

cf_cell_t *cf_get_cell(compute_fabric_t *fabric, uint32_t cell_id);
cf_cell_t *cf_get_cell_by_name(compute_fabric_t *fabric, const char *name);

/* Cell operations */
int32_t cf_start_cell(compute_fabric_t *fabric, uint32_t cell_id);
int32_t cf_stop_cell(compute_fabric_t *fabric, uint32_t cell_id);
int32_t cf_migrate_domain(compute_fabric_t *fabric, uint32_t domain_id,
                          uint32_t target_cell_id);

/* ===== Compute Domain Management ===== */

int32_t cf_create_domain(compute_fabric_t *fabric, const char *name,
                         uint32_t cell_id,
                         uint32_t event_budget, uint32_t max_cost,
                         ev_priority_t priority, ev_consistency_t consistency);

cf_compute_domain_t *cf_get_domain(compute_fabric_t *fabric, uint32_t domain_id);

/* Domain operations */
int32_t cf_register_handler(compute_fabric_t *fabric, uint32_t domain_id,
                            const char *schema, uint16_t schema_version,
                            int32_t (*handler_fn)(ev_envelope_t *env, void *context),
                            void *context, uint32_t max_cost);

int32_t cf_dispatch_domain(compute_fabric_t *fabric, uint32_t domain_id);

/* ===== Resource Management ===== */

int32_t cf_register_resource(compute_fabric_t *fabric,
                             const char *name, const char *type,
                             yf_device_t *yf_device,
                             smart_device_t *sa_device,
                             surplus_real_t capacity);

int32_t cf_allocate_resource(compute_fabric_t *fabric,
                             uint32_t resource_id, uint32_t domain_id,
                             surplus_real_t amount, uint64_t expiry_tick);

int32_t cf_release_resource(compute_fabric_t *fabric,
                            uint32_t resource_id, uint32_t domain_id);

/* ===== Event Transport ===== */

int32_t cf_send_event(compute_fabric_t *fabric,
                      uint32_t source_domain_id,
                      const char *destination_service,
                      const char *schema, uint16_t schema_version,
                      const uint8_t *payload, uint16_t payload_len,
                      ev_delivery_t delivery, ev_priority_t priority);

int32_t cf_broadcast_event(compute_fabric_t *fabric,
                           const char *schema, uint16_t schema_version,
                           const uint8_t *payload, uint16_t payload_len,
                           ev_priority_t priority);

/* ===== Scheduling ===== */

int32_t cf_schedule(compute_fabric_t *fabric);

/* ===== Hypercube Topology ===== */

int32_t cf_get_cell_at(compute_fabric_t *fabric,
                       int32_t x, int32_t y, int32_t z, int32_t t,
                       cf_cell_t **out_cell);

int32_t cf_find_nearest_cell(compute_fabric_t *fabric,
                             int32_t x, int32_t y, int32_t z, int32_t t,
                             const char *required_capability,
                             cf_cell_t **out_cell);

/* ===== Health & Attestation ===== */

int32_t cf_check_cell_health(compute_fabric_t *fabric,
                             uint32_t cell_id,
                             void *health_out);

int32_t cf_check_domain_health(compute_fabric_t *fabric,
                               uint32_t domain_id,
                               void *health_out);

int32_t cf_check_global_health(compute_fabric_t *fabric);

bool cf_global_safety_gate(compute_fabric_t *fabric);

lpres_state_t cf_attest(compute_fabric_t *fabric, uint32_t cell_id,
                        uint32_t op_id, void *args, int32_t result);

/* Coverage enforcement */
void cf_update_coverage(compute_fabric_t *fabric);
bool cf_enforce_coverage(compute_fabric_t *fabric, surplus_real_t min_ratio);

/* Statistics */
void cf_get_stats(compute_fabric_t *fabric, void *stats_out);

/* Paraconsistent state */
lpres_state_t cf_get_attestation(compute_fabric_t *fabric, uint32_t cell_id);
void cf_set_attestation(compute_fabric_t *fabric, uint32_t cell_id, lpres_state_t state);

/* Utility */
const char *cf_lpres_state_name(lpres_state_t state);

#endif /* COMPUTE_FABRIC_H */
