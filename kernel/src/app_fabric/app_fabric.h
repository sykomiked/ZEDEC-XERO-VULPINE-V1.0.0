/* app_fabric.h — ZXV Application Fabric Compound Module
 *
 * The Application Fabric unifies all user-space application development,
 * SDK, and runtime modules into a single coherent fabric for building,
 * deploying, and running applications on the ZXV kernel.
 *
 * Sub-modules integrated:
 *   1. SDK — M5 Kernel API for user space
 *   2. App Templates — Minimal app scaffolding
 *   3. Wallet App — Nine-form capital management
 *   4. Derivatives App — Derivatives trading & temporal arbitrage
 *   5. Assurance App — Pay-It-Forward capital generation
 *   6. Treaty App — Conservation easement tokenization
 *   7. App Launcher — Application lifecycle management
 *   8. Self-Audit — Application integrity verification
 *   9. Financial Fabric — Capital operations backend
 *   10. Identity Fabric — Identity & credentials
 *   11. Orbital Fabric — Schema translation for app events
 *   12. Compute Fabric — App scheduling & resource allocation
 *   13. Security Fabric — App signing, sandboxing, TLS
 *   14. Storage Fabric — App data persistence
 *   15. Network Fabric — App networking
 *   16. Media Fabric — App media streaming
 *   17. Governance Fabric — App policy enforcement
 *
 * Design principles:
 * - Apps are event-driven, capability-gated processes
 * - All app operations attested via LPRES
 * - M5 coverage enforced per-app
 * - Self-audit built into app lifecycle
 * - Economic settlement via Financial Fabric
 * - Identity-gated via Identity Fabric credentials
 * - Schema translation via Orbital Fabric
 * - Sandboxed via Security Fabric (HSM, TLS, ZK proofs)
 * - Paraconsistent logic (LPRES) for all attestations
 * - M5 coverage hyperbola enforcement on all app operations
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef APP_FABRIC_H
#define APP_FABRIC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "m5_api.h"
#include "app_template.h"
#include "wallet_app.h"
#include "derivatives_app.h"
#include "assurance_app.h"
#include "treaty_app.h"
#include "apps.h"
#include "selfaudit.h"
#include "financial_fabric.h"
#include "identity_fabric.h"
#include "orbital_fabric.h"
#include "compute_fabric.h"
#include "security_fabric.h"
#include "storage_fabric.h"
#include "network_fabric.h"
#include "media_fabric.h"
#include "governance_fabric.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"

/* ===== Constants ===== */

#define AF_MAX_APPS              256
#define AF_MAX_APP_INSTANCES     1024
#define AF_MAX_CAPABILITIES      64
#define AF_MAX_NAME_LEN          64
#define AF_MAX_SCHEMA_LEN        64

/* ===== App Types ===== */

typedef enum {
    AF_APP_WALLET        = 1,
    AF_APP_DERIVATIVES   = 2,
    AF_APP_ASSURANCE     = 3,
    AF_APP_TREATY        = 4,
    AF_APP_CUSTOM        = 5,
    AF_APP_SYSTEM        = 6
} af_app_type_t;

/* ===== App Descriptor ===== */

typedef struct af_app_descriptor {
    uint32_t id;
    char name[AF_MAX_NAME_LEN];
    af_app_type_t type;
    
    /* Entry point */
    void *entry_point;               /* Function pointer to app_main */
    uint32_t stack_size;
    uint32_t heap_size;
    
    /* Capabilities required */
    char capabilities[AF_MAX_CAPABILITIES][AF_MAX_NAME_LEN];
    uint32_t num_capabilities;
    
    /* Schema for events */
    char accepted_schemas[8][AF_MAX_SCHEMA_LEN];
    uint32_t num_accepted_schemas;
    char emitted_schemas[8][AF_MAX_SCHEMA_LEN];
    uint32_t num_emitted_schemas;
    
    /* Resource limits */
    surplus_real_t max_cpu_percent;
    surplus_real_t max_memory_mb;
    surplus_real_t max_network_mbps;
    surplus_real_t max_storage_gb;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    surplus_real_t min_coverage_ratio;
    
    /* Paraconsistent state */
    lpres_state_t attestation;
    bool active;
    bool builtin;
} af_app_descriptor_t;

/* ===== App Instance (Running App) ===== */

typedef struct af_app_instance {
    uint32_t id;
    uint32_t descriptor_id;
    uint32_t owner_id;               /* Identity Fabric identity */
    
    /* Process state */
    enum {
        AF_INSTANCE_CREATED   = 0,
        AF_INSTANCE_STARTING  = 1,
        AF_INSTANCE_RUNNING   = 2,
        AF_INSTANCE_SUSPENDED = 3,
        AF_INSTANCE_STOPPING  = 4,
        AF_INSTANCE_STOPPED   = 5,
        AF_INSTANCE_CRASHED   = 6,
        AF_INSTANCE_QUARANTINED = 7
    } state;
    
    /* Compute Fabric domain */
    uint32_t compute_domain_id;
    
    /* Capabilities granted */
    uint32_t capability_ids[AF_MAX_CAPABILITIES];
    uint32_t num_capabilities;
    
    /* Event domain */
    uint32_t event_domain_id;
    
    /* Financial account */
    uint32_t financial_account_id;
    
    /* Storage volume */
    uint32_t storage_volume_id;
    
    /* Network interfaces */
    uint32_t network_interface_ids[8];
    uint32_t num_network_interfaces;
    
    /* Security context */
    uint32_t tls_session_id;
    uint32_t keypair_id;
    bool sandboxed;
    
    /* Statistics */
    uint64_t cpu_cycles_used;
    uint64_t memory_bytes_used;
    uint64_t network_bytes_tx;
    uint64_t network_bytes_rx.
    uint64_t storage_bytes_read.
    uint64_t storage_bytes_written.
    uint64_t events_processed.
    uint64_t events_failed.
    uint64_t self_audit_passes.
    uint64_t self_audit_failures.
    
    /* M5 coordinates */
    m5_coords_t m5.
    surplus_real_t coverage_ratio.
    
    /* Paraconsistent state */
    lpres_state_t attestation.
    bool active.
} af_app_instance_t;

/* ===== App Capability ===== */

typedef struct af_capability {
    uint32_t id.
    char name[AF_MAX_NAME_LEN].
    char description[AF_MAX_NAME_LEN].
    
    /* Requirements */
    surplus_real_t min_coverage_ratio.
    lpres_state_t min_attestation.
    bool requires_hsm.
    bool requires_zk_proof.
    
    /* Financial Fabric integration */
    uint8_t capital_form_required.
    uint64_t min_balance.
    
    /* Identity Fabric integration */
    uint32_t credential_schema_id.
    
    /* Governance Fabric integration */
    uint32_t policy_id.
    
    /* Paraconsistent state */
    lpres_state_t attestation.
    bool active.
} af_capability_t;

/* ===== App Fabric ===== */

typedef struct app_fabric {
    /* Core sub-modules */
    m5_sdk_t sdk;                    /* M5 SDK */
    app_launcher_t launcher;         /* App Launcher */
    
    /* Built-in apps */
    wallet_app_t wallet_app;
    derivatives_app_t derivatives_app;
    assurance_app_t assurance_app;
    treaty_app_t treaty_app;
    
    /* Integration references */
    financial_fabric_t *financial;
    identity_fabric_t *identity;
    orbital_fabric_t *orbital;
    compute_fabric_t *compute;
    security_fabric_t *security;
    storage_fabric_t *storage;
    network_fabric_t *network;
    media_fabric_t *media.
    governance_fabric_t *governance.
    
    /* Fabric-level state */
    af_app_descriptor_t descriptors[AF_MAX_APPS].
    uint32_t num_descriptors.
    uint32_t next_descriptor_id.
    
    af_app_instance_t instances[AF_MAX_APP_INSTANCES].
    uint32_t num_instances.
    uint32_t next_instance_id.
    
    af_capability_t capabilities[AF_MAX_CAPABILITIES].
    uint32_t num_capabilities.
    
    /* Global statistics */
    struct {
        uint64_t total_apps_registered.
        uint64_t total_instances_created.
        uint64_t total_instances_started.
        uint64_t total_instances_stopped.
        uint64_t total_instances_crashed.
        uint64_t total_events_dispatched.
        uint64_t total_self_audits.
        uint64_t total_healings.
        uint64_t total_capability_grants.
        uint64_t total_capability_revokes.
    } stats.
    
    /* Paraconsistent global state */
    lpres_state_t global_attestation.
    bool global_safety_gate.
    
    /* M5 coordinates */
    m5_coords_t m5.
    surplus_real_t coverage_ratio.
    surplus_real_t min_coverage_ratio.
    
    /* Configuration */
    struct {
        bool auto_grant_builtin_capabilities.
        bool require_self_audit.
        uint32_t audit_interval.
        bool auto_heal.
        surplus_real_t min_instance_coverage.
    } config.
    
    bool initialized.
} app_fabric_t;

/* ===== API ===== */

/* Initialize the Application Fabric */
void af_init(app_fabric_t *fabric,
             financial_fabric_t *financial,
             identity_fabric_t *identity,
             orbital_fabric_t *orbital,
             compute_fabric_t *compute,
             security_fabric_t *security,
             storage_fabric_t *storage,
             network_fabric_t *network,
             media_fabric_t *media,
             governance_fabric_t *governance);

/* Register built-in apps */
void af_register_builtins(app_fabric_t *fabric);

/* ===== App Descriptor Management ===== */

int32_t af_register_app(app_fabric_t *fabric,
                        const char *name, af_app_type_t type,
                        void *entry_point,
                        uint32_t stack_size, uint32_t heap_size,
                        const char **capabilities, uint32_t num_capabilities,
                        const char **accepted_schemas, uint32_t num_accepted,
                        const char **emitted_schemas, uint32_t num_emitted);

af_app_descriptor_t *af_get_descriptor(app_fabric_t *fabric, uint32_t descriptor_id);
af_app_descriptor_t *af_get_descriptor_by_name(app_fabric_t *fabric, const char *name);

/* ===== App Instance Management ===== */

int32_t af_create_instance(app_fabric_t *fabric,
                           uint32_t descriptor_id, uint32_t owner_id);

af_app_instance_t *af_get_instance(app_fabric_t *fabric, uint32_t instance_id);

int32_t af_start_instance(app_fabric_t *fabric, uint32_t instance_id);
int32_t af_stop_instance(app_fabric_t *fabric, uint32_t instance_id);
int32_t af_suspend_instance(app_fabric_t *fabric, uint32_t instance_id);
int32_t af_restart_instance(app_fabric_t *fabric, uint32_t instance_id);

/* ===== Capability Management ===== */

int32_t af_register_capability(app_fabric_t *fabric,
                               const char *name, const char *description,
                               surplus_real_t min_coverage,
                               lpres_state_t min_attestation,
                               bool requires_hsm, bool requires_zk,
                               uint8_t capital_form, uint64_t min_balance,
                               uint32_t credential_schema_id,
                               uint32_t policy_id);

int32_t af_grant_capability(app_fabric_t *fabric,
                            uint32_t instance_id, uint32_t capability_id);

int32_t af_revoke_capability(app_fabric_t *fabric,
                             uint32_t instance_id, uint32_t capability_id);

/* ===== Event Dispatch ===== */

int32_t af_dispatch_event(app_fabric_t *fabric,
                          uint32_t instance_id,
                          const char *schema, uint16_t schema_version,
                          const uint8_t *payload, uint16_t payload_len);

/* ===== Self-Audit ===== */

int32_t af_run_self_audit(app_fabric_t *fabric, uint32_t instance_id);
int32_t af_run_system_audit(app_fabric_t *fabric);

/* ===== Health & Attestation ===== */

int32_t af_check_instance_health(app_fabric_t *fabric,
                                 uint32_t instance_id,
                                 void *health_out);

int32_t af_check_global_health(app_fabric_t *fabric);

bool af_global_safety_gate(app_fabric_t *fabric);

lpres_state_t af_attest(app_fabric_t *fabric, uint32_t instance_id,
                        uint32_t op_id, void *args, int32_t result);

/* Coverage enforcement */
void af_update_coverage(app_fabric_t *fabric);
bool af_enforce_coverage(app_fabric_t *fabric, surplus_real_t min_ratio);

/* Statistics */
void af_get_stats(app_fabric_t *fabric, void *stats_out);

/* Paraconsistent state */
lpres_state_t af_get_attestation(app_fabric_t *fabric, uint32_t instance_id);
void af_set_attestation(app_fabric_t *fabric, uint32_t instance_id, lpres_state_t state);

/* Utility */
const char *af_lpres_state_name(lpres_state_t state);
const char *af_app_type_name(af_app_type_t type);
const char *af_instance_state_name(int state);

#endif /* APP_FABRIC_H */
