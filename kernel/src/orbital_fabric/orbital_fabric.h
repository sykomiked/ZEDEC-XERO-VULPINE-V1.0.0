/* orbital_fabric.h — ZXV Orbital Fabric Compound Module
 *
 * The Orbital Fabric is a compound module that unifies:
 *   1. Orbital Elevator — Schema translation and compatibility graph
 *   2. Orbital Compat — Language IR translation (COBOL/Fortran/C/Sutra)
 *   3. Constellation Coordinator — Cross-domain event routing
 *   4. Yantra Fabric — Software-defined hardware fabric (9 capability states)
 *   5. Smart Adapter — Generic hardware/firmware adapter with LPRES attestation
 *   6. Event Space — Canonical event envelopes, self-audit, self-healing
 *   7. Mesh Net — P2P mesh networking with trade routes
 *
 * This creates a complete "orbital" infrastructure layer for:
 * - Cross-language interoperability (legacy → modern → AI languages)
 * - Cross-hardware abstraction (model → emulator → RTL → FPGA → device)
 * - Cross-domain event routing (heterogeneous ISA constellation)
 * - Mesh-native P2P networking with economic settlement
 * - Paraconsistent logic throughout (LPRES four-valued states)
 * - M5 coverage hyperbola enforcement at every layer
 *
 * Design principles:
 * - Each sub-module retains its own API; the fabric provides integration APIs
 * - All state transitions are self-audited and self-healing
 * - Contradictions are preserved, not hidden (paraconsistent)
 * - Capability gates enforce M5 coverage and LPRES attestation
 * - Trade routes settle via Mesh-Token on JDR PirateNet transport
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ORBITAL_FABRIC_H
#define ORBITAL_FABRIC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "orbital_elevator.h"
#include "orbital_compat.h"
#include "constellation_coordinator.h"
#include "yantra_fabric.h"
#include "smart_adapter.h"
#include "smart_adapter_integration.h"
#include "event_space.h"
#include "mesh_net.h"
#include "jdr_piratenet.h"
#include "firmware_adapters.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"

/* ===== Constants ===== */

#define OF_MAX_INTEGRATED_NODES    32
#define OF_MAX_FABRIC_DOMAINS      64
#define OF_MAX_SCHEMA_MAPPINGS     128
#define OF_MAX_LANG_ADAPTERS       8
#define OF_MAX_TRADE_ROUTES        128
#define OF_MAX_CAPABILITY_GUARDS   64
#define OF_MAX_NAME_LEN            64

/* ===== Fabric Node States (Paraconsistent) ===== */

typedef enum {
    OF_NODE_NEITHER   = 0,  /* Unknown/uninitialized — LPRES: NEITHER */
    OF_NODE_TRUE      = 1,  /* Fully operational — LPRES: TRUE */
    OF_NODE_FALSE     = 2,  /* Failed — LPRES: FALSE */
    OF_NODE_BOTH      = 3,  /* Degraded/contradiction — LPRES: BOTH */
    OF_NODE_QUARANTINE = 4  /* Isolated by self-healing */
} of_node_state_t;

/* ===== Fabric Domain ===== */

typedef struct of_domain {
    uint32_t id;
    char name[OF_MAX_NAME_LEN];
    
    /* Sub-module domain references */
    ev_domain_t *ev_domain;           /* Event Space domain */
    cc_node_t *cc_node;               /* Constellation node */
    yf_device_t *yf_device;           /* Yantra device */
    smart_device_t *sa_device;        /* Smart Adapter device */
    mn_network_t *mn_network;         /* Mesh network */
    
    /* Schema mappings for this domain */
    struct {
        char sa_schema[EV_SCHEMA_LEN];
        char yf_schema[EV_SCHEMA_LEN];
        char oe_schema[EV_SCHEMA_LEN];
        char oc_schema[EV_SCHEMA_LEN];
        uint16_t sa_version;
        uint16_t yf_version;
        uint16_t oe_version;
        uint16_t oc_version;
        uint32_t oe_adapter_id;
        uint32_t oc_adapter_id;
        bool active;
    } schema_mappings[OF_MAX_SCHEMA_MAPPINGS];
    uint32_t num_schema_mappings;
    
    /* Language adapters for this domain */
    struct {
        oc_lang_t lang;
        oc_lang_ops_t ops;
        bool registered;
    } lang_adapters[OF_MAX_LANG_ADAPTERS];
    uint32_t num_lang_adapters;
    
    /* Trade routes originating from this domain */
    uint32_t trade_route_ids[OF_MAX_TRADE_ROUTES];
    uint32_t num_trade_routes;
    
    /* Capability guards */
    uint32_t capability_guard_ids[OF_MAX_CAPABILITY_GUARDS];
    uint32_t num_capability_guards;
    
    /* Paraconsistent state */
    of_node_state_t lpres_state;
    lpres_state_t global_attestation;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    surplus_real_t min_coverage_ratio;
    
    /* Health */
    struct {
        uint64_t events_processed;
        uint64_t events_failed;
        uint64_t translations_performed;
        uint64_t translations_failed;
        uint64_t self_audit_passes;
        uint64_t self_audit_failures;
        uint64_t healing_actions;
        uint64_t last_heartbeat;
    } health;
    
    bool registered;
} of_domain_t;

/* ===== Fabric Trade Route (extends mesh route with orbital metadata) ===== */

typedef struct of_trade_route {
    mn_route_t base;                  /* Base mesh route */
    
    /* Orbital extensions */
    char source_schema[EV_SCHEMA_LEN];
    char dest_schema[EV_SCHEMA_LEN];
    uint16_t source_version;
    uint16_t dest_version;
    uint32_t oe_adapter_id;           /* Orbital Elevator adapter for translation */
    uint32_t oc_adapter_id;           /* Orbital Compat adapter for language translation */
    
    /* Settlement */
    uint64_t settlement_price;        /* Price in Vino vouchers */
    uint64_t settlement_interval;     /* Settlement interval in cycles */
    uint64_t last_settlement_cycle;
    
    /* Quality of Service */
    surplus_real_t latency_slo;       /* Service level objective */
    surplus_real_t availability_slo;
    surplus_real_t throughput_slo;
    
    /* Paraconsistent route state */
    of_node_state_t lpres_state;
    lpres_state_t route_attestation;
    
    bool active;
} of_trade_route_t;

/* ===== Fabric Language Adapter Registry ===== */

typedef struct of_lang_registry {
    oc_lang_t lang;
    oc_lang_ops_t ops;
    bool registered;
    uint32_t domain_count;            /* Number of domains using this adapter */
    lpres_state_t adapter_attestation;
} of_lang_registry_t;

/* ===== Orbital Fabric ===== */

typedef struct orbital_fabric {
    /* Core sub-modules */
    oe_elevator_t elevator;           /* Orbital Elevator */
    oc_ir_t oc_ir;                    /* Orbital Compat IR (shared) */
    cc_coordinator_t constellation;   /* Constellation Coordinator */
    yf_fabric_t yantra;               /* Yantra Fabric */
    smart_adapter_registry_t sa_registry; /* Smart Adapter Registry */
    sai_integration_fabric_t sai_fabric;  /* Smart Adapter Integration */
    ev_sequencer_t sequencer;         /* Event Sequencer */
    ev_audit_t audit;                 /* Self-Audit */
    ev_healing_t healing;             /* Self-Healing */
    mesh_net_t mesh;                  /* Mesh Network */
    
    /* JDR PirateNet for physical transport */
    jdr_network_t jdr_network;
    jdr_adapter_registry_t jdr_registry;
    
    /* Integrated domains */
    of_domain_t domains[OF_MAX_INTEGRATED_NODES];
    uint32_t num_domains;
    uint32_t next_domain_id;
    
    /* Trade routes */
    of_trade_route_t trade_routes[OF_MAX_TRADE_ROUTES];
    uint32_t num_trade_routes;
    uint32_t next_route_id;
    
    /* Language adapter registry */
    of_lang_registry_t lang_registry[OF_MAX_LANG_ADAPTERS];
    uint32_t num_lang_adapters;
    
    /* Global schema mappings */
    struct {
        char sa_schema[EV_SCHEMA_LEN];
        char yf_schema[EV_SCHEMA_LEN];
        char oe_schema[EV_SCHEMA_LEN];
        char oc_schema[EV_SCHEMA_LEN];
        uint16_t sa_version;
        uint16_t yf_version;
        uint16_t oe_version;
        uint16_t oc_version;
        uint32_t oe_adapter_id;
        uint32_t oc_adapter_id;
        bool active;
    } global_mappings[OF_MAX_SCHEMA_MAPPINGS];
    uint32_t num_global_mappings;
    
    /* Capability guards */
    sai_capability_guard_t capability_guards[OF_MAX_CAPABILITY_GUARDS];
    uint32_t num_capability_guards;
    
    /* Global statistics */
    struct {
        uint64_t total_domains_created;
        uint64_t total_domains_failed;
        uint64_t total_events_routed;
        uint64_t total_events_dropped;
        uint64_t total_translations;
        uint64_t total_translation_failures;
        uint64_t total_trade_routes;
        uint64_t total_settlements;
        uint64_t total_self_audits;
        uint64_t total_healings;
        uint64_t total_mesh_data_routed;
        uint64_t total_mesh_revenue;
    } stats;
    
    /* Paraconsistent global state */
    of_node_state_t global_lpres_state;
    lpres_state_t global_attestation;
    bool global_safety_gate;
    
    /* M5 coordinates for fabric */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* Configuration */
    struct {
        bool auto_register_builtins;
        bool auto_heal;
        uint32_t audit_interval;
        uint32_t health_check_interval;
        uint32_t route_expiry_cycles;
        surplus_real_t min_global_coverage;
    } config;
    
    bool initialized;
} orbital_fabric_t;

/* ===== API ===== */

/* Initialize the Orbital Fabric */
void of_init(orbital_fabric_t *fabric, const char *local_node_id, uint32_t device_id);

/* Register built-in language adapters (COBOL, Fortran, C) */
void of_register_builtins(orbital_fabric_t *fabric);

/* Register a custom language adapter */
int32_t of_register_lang(orbital_fabric_t *fabric, oc_lang_t lang, const oc_lang_ops_t *ops);

/* Create an integrated domain */
int32_t of_create_domain(orbital_fabric_t *fabric, const char *name,
                         uint32_t event_budget, uint32_t max_cost,
                         ev_consistency_t consistency,
                         const char *yf_device_class,
                         yf_capability_state_t initial_capability,
                         const char *mesh_name, mn_net_access_t mesh_access);

/* Get domain by ID */
of_domain_t *of_get_domain(orbital_fabric_t *fabric, uint32_t domain_id);

/* Get domain by name */
of_domain_t *of_get_domain_by_name(orbital_fabric_t *fabric, const char *name);

/* Add a schema mapping for a domain */
int32_t of_add_schema_mapping(orbital_fabric_t *fabric, uint32_t domain_id,
                              const char *sa_schema, uint16_t sa_version,
                              const char *yf_schema, uint16_t yf_version,
                              const char *oe_schema, uint16_t oe_version,
                              const char *oc_schema, uint16_t oc_version,
                              uint32_t oe_adapter_id, uint32_t oc_adapter_id);

/* Add a global schema mapping */
int32_t of_add_global_mapping(orbital_fabric_t *fabric,
                              const char *sa_schema, uint16_t sa_version,
                              const char *yf_schema, uint16_t yf_version,
                              const char *oe_schema, uint16_t oe_version,
                              const char *oc_schema, uint16_t oc_version,
                              uint32_t oe_adapter_id, uint32_t oc_adapter_id);

/* Translate an event through the orbital stack */
int32_t of_translate_event(orbital_fabric_t *fabric, uint32_t domain_id,
                           ev_envelope_t *env,
                           const char *target_sa_schema, uint16_t target_sa_version,
                           const char *target_yf_schema, uint16_t target_yf_version,
                           const char *target_oe_schema, uint16_t target_oe_version,
                           const char *target_oc_schema, uint16_t target_oc_version);

/* Translate through Orbital Elevator (schema) */
oe_translate_result_t of_oe_translate(orbital_fabric_t *fabric,
                                       ev_envelope_t *env,
                                       const char *target_schema,
                                       uint16_t target_version);

/* Translate through Orbital Compat (language) */
int32_t of_oc_translate(orbital_fabric_t *fabric,
                         oc_lang_t from_lang, const void *src, uint32_t len,
                         oc_lang_t to_lang, void *out, uint32_t cap);

/* Route an event through Constellation */
cc_node_t *of_route_event(orbital_fabric_t *fabric, const char *schema,
                          uint16_t schema_version);

/* Create a trade route with orbital extensions */
int32_t of_create_trade_route(orbital_fabric_t *fabric,
                              uint32_t source_domain_id,
                              uint32_t dest_domain_id,
                              mn_route_type_t type,
                              uint64_t price_per_unit, uint64_t capacity,
                              uint64_t current_cycle,
                              const char *source_schema, uint16_t source_version,
                              const char *dest_schema, uint16_t dest_version);

/* Settle a trade route */
int32_t of_settle_route(orbital_fabric_t *fabric, uint32_t route_id,
                        uint64_t current_cycle);

/* Upgrade Yantra capability with full orbital guards */
int32_t of_upgrade_capability(orbital_fabric_t *fabric, uint32_t domain_id,
                              yf_capability_state_t target_capability);

/* Run self-audit + self-healing on a domain */
ev_heal_action_t of_self_audit_heal_domain(orbital_fabric_t *fabric, uint32_t domain_id);

/* Run self-audit + self-healing on entire fabric */
ev_heal_action_t of_self_audit_heal_system(orbital_fabric_t *fabric);

/* Check global health */
int32_t of_check_global_health(orbital_fabric_t *fabric);

/* Check global safety gate */
bool of_global_safety_gate(orbital_fabric_t *fabric);

/* Get LPRES attestation for fabric operation */
lpres_state_t of_attest(orbital_fabric_t *fabric, uint32_t domain_id,
                        uint32_t op_id, void *args, int32_t result);

/* Update M5 coverage for all components */
void of_update_coverage(orbital_fabric_t *fabric);

/* Enforce minimum coverage globally */
bool of_enforce_coverage(orbital_fabric_t *fabric, surplus_real_t min_ratio);

/* Get fabric statistics */
void of_get_stats(orbital_fabric_t *fabric, void *stats_out);

/* Paraconsistent state management */
of_node_state_t of_get_lpres_state(orbital_fabric_t *fabric, uint32_t domain_id);
void of_set_lpres_state(orbital_fabric_t *fabric, uint32_t domain_id, of_node_state_t state);

/* Utility */
const char *of_node_state_name(of_node_state_t state);
const char *of_lpres_state_name(lpres_state_t state);
const char *of_oe_result_name(oe_translate_result_t result);
const char *of_oc_status_name(oc_status_t status);

#endif /* ORBITAL_FABRIC_H */
