/* app_constellation.h — ZXV Application Constellation
 *
 * The Application Constellation orchestrates a constellation of user-space
 * applications across the fabric constellation, providing deployment,
 * scaling, service discovery, and economic orchestration.
 *
 * This is the user-space counterpart to the kernel's Constellation Coordinator,
 * operating at the application layer with full fabric integration.
 *
 * Capabilities:
 * - Application deployment via App Fabric
 * - Service mesh via Network Fabric
 * - Economic orchestration via Financial Fabric
 * - Identity & access via Identity Fabric
 * - Schema translation via Orbital Fabric
 * - Compute scheduling via Compute Fabric
 * - Security sandboxing via Security Fabric
 * - Persistent storage via Storage Fabric
 * - Media streaming via Media Fabric
 * - Policy enforcement via Governance Fabric
 *
 * Design principles:
 * - Applications are deployed as capability-gated processes
 * - Service discovery via Mesh Net trade routes
 * - Economic settlement for inter-service calls
 * - Paraconsistent health monitoring (LPRES)
 * - M5 coverage enforcement per service
 * - Self-audit/self-heal at constellation level
 * - Zero-downtime deployments via blue/green
 * - Canary releases with economic metrics
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef APP_CONSTELLATION_H
#define APP_CONSTELLATION_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "app_fabric.h"
#include "financial_fabric.h"
#include "identity_fabric.h"
#include "orbital_fabric.h"
#include "compute_fabric.h"
#include "security_fabric.h"
#include "storage_fabric.h"
#include "network_fabric.h"
#include "media_fabric.h"
#include "governance_fabric.h"
#include "mesh_net.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"

/* ===== Constants ===== */

#define AC_MAX_SERVICES          512
#define AC_MAX_DEPLOYMENTS       1024
#define AC_MAX_SERVICE_MESH_NODES 256
#define AC_MAX_TRADE_ROUTES      1024
#define AC_MAX_NAME_LEN          64
#define AC_MAX_VERSION_LEN       32

/* ===== Service Descriptor ===== */

typedef enum {
    AC_SERVICE_STATeless    = 1,
    AC_SERVICE_STATEFUL     = 2,
    AC_SERVICE_STREAMING    = 3,
    AC_SERVICE_BATCH        = 4,
    AC_SERVICE_ML_INFERENCE = 5,
    AC_SERVICE_GATEWAY      = 6,
    AC_SERVICE_DATABASE     = 7,
    AC_SERVICE_CACHE        = 8,
    AC_SERVICE_QUEUE        = 9
} ac_service_type_t;

typedef struct ac_service_descriptor {
    uint32_t id;
    char name[AC_MAX_NAME_LEN];
    char version[AC_MAX_VERSION_LEN];
    ac_service_type_t type;
    
    /* App Fabric descriptor */
    uint32_t app_descriptor_id;
    
    /* Scaling */
    uint32_t min_replicas;
    uint32_t max_replicas;
    surplus_real_t target_cpu_percent;
    surplus_real_t target_memory_percent;
    
    /* Resource requirements */
    surplus_real_t cpu_per_replica;
    surplus_real_t memory_per_replica_mb;
    surplus_real_t storage_per_replica_gb;
    surplus_real_t network_per_replica_mbps;
    
    /* Capabilities required */
    char required_capabilities[32][AC_MAX_NAME_LEN];
    uint32_t num_required_capabilities;
    
    /* Service mesh */
    char exposed_endpoints[16][AC_MAX_NAME_LEN];
    uint32_t num_exposed_endpoints;
    char consumed_services[32][AC_MAX_NAME_LEN];
    uint32_t num_consumed_services;
    
    /* Economic */
    uint64_t price_per_invocation;     /* Vino vouchers */
    uint8_t pricing_form;              /* Capital form */
    uint64_t monthly_base_fee;
    
    /* Schema */
    char input_schema[EV_SCHEMA_LEN];
    char output_schema[EV_SCHEMA_LEN];
    uint16_t schema_version;
    
    /* Health */
    uint32_t health_check_interval_ms;
    surplus_real_t min_coverage_ratio;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* Paraconsistent state */
    lpres_state_t attestation;
    bool active;
} ac_service_descriptor_t;

/* ===== Service Instance (Running Replica) ===== */

typedef struct ac_service_instance {
    uint32_t id;
    uint32_t service_descriptor_id;
    uint32_t deployment_id;
    
    /* App Fabric instance */
    uint32_t app_instance_id;
    
    /* Network identity */
    word168_t service_identity;
    uint32_t mesh_network_id;
    uint32_t trade_route_ids[16];
    uint32_t num_trade_routes;
    
    /* Placement */
    uint32_t compute_cell_id;          /* Compute Fabric cell */
    uint32_t compute_domain_id;
    
    /* State */
    enum {
        AC_INSTANCE_PENDING   = 0,
        AC_INSTANCE_STARTING  = 1,
        AC_INSTANCE_HEALTHY   = 2,
        AC_INSTANCE_DEGRADED  = 3,
        AC_INSTANCE_UNHEALTHY = 4,
        AC_INSTANCE_STOPPING  = 5,
        AC_INSTANCE_STOPPED   = 6
    } state;
    
    /* Health */
    uint64_t last_health_check_tick;
    uint32_t consecutive_failures;
    surplus_real_t current_cpu_percent;
    surplus_real_t current_memory_percent.
    lpres_state_t health_attestation.
    
    /* Economics */
    uint64_t invocations_served.
    uint64_t revenue_generated.
    uint64_t fees_paid.
    
    /* M5 coordinates */
    m5_coords_t m5.
    surplus_real_t coverage_ratio.
    
    /* Paraconsistent state */
    lpres_state_t attestation.
    bool active.
} ac_service_instance_t;

/* ===== Deployment ===== */

typedef enum {
    AC_DEPLOY_ROLLING     = 1,
    AC_DEPLOY_BLUE_GREEN  = 2,
    AC_DEPLOY_CANARY      = 3,
    AC_DEPLOY_RECREATE    = 4
} ac_deploy_strategy_t;

typedef struct ac_deployment {
    uint32_t id.
    uint32_t service_descriptor_id.
    char version[AC_MAX_VERSION_LEN].
    ac_deploy_strategy_t strategy.
    
    /* Rollout */
    uint32_t target_replicas.
    uint32_t current_replicas.
    uint32_t max_surge.
    uint32_t max_unavailable.
    
    /* Canary */
    surplus_real_t canary_traffic_percent.
    uint32_t canary_instance_ids[32].
    uint32_t num_canary_instances.
    
    /* State */
    enum {
        AC_DEPLOY_PENDING   = 0,
        AC_DEPLOY_IN_PROGRESS = 1,
        AC_DEPLOY_PAUSED    = 2,
        AC_DEPLOY_COMPLETED = 3,
        AC_DEPLOY_ROLLED_BACK = 4,
        AC_DEPLOY_FAILED    = 5
    } state.
    
    /* Timing */
    uint64_t started_tick.
    uint64_t completed_tick.
    uint64_t rollback_deadline_tick.
    
    /* Metrics */
    surplus_real_t success_rate.
    surplus_real_t error_rate.
    surplus_real_t latency_p50.
    surplus_real_t latency_p99.
    
    /* Paraconsistent state */
    lpres_state_t attestation.
    bool active.
} ac_deployment_t;

/* ===== Service Mesh ===== */

typedef struct ac_service_mesh_node {
    uint32_t id.
    char name[AC_MAX_NAME_LEN].
    uint32_t service_instance_id.
    
    /* Mesh Net integration */
    uint32_t mesh_network_id.
    uint32_t trade_route_ids[16].
    uint32_t num_trade_routes.
    
    /* Routing */
    struct {
        char destination_service[AC_MAX_NAME_LEN].
        uint32_t destination_instance_id.
        uint64_t price_per_invocation.
        surplus_real_t latency_slo.
        surplus_real_t availability_slo.
    } routes[64].
    uint32_t num_routes.
    
    /* Security */
    uint32_t tls_session_id.
    bool mtls_enabled.
    
    /* M5 coordinates */
    m5_coords_t m5.
    surplus_real_t coverage_ratio.
    
    /* Paraconsistent state */
    lpres_state_t attestation.
    bool active.
} ac_service_mesh_node_t;

/* ===== Application Constellation ===== */

typedef struct app_constellation {
    /* Core fabrics */
    app_fabric_t *app_fabric.
    financial_fabric_t *financial.
    identity_fabric_t *identity.
    orbital_fabric_t *orbital.
    compute_fabric_t *compute.
    security_fabric_t *security.
    storage_fabric_t *storage.
    network_fabric_t *network.
    media_fabric_t *media.
    governance_fabric_t *governance.
    
    /* Constellation state */
    ac_service_descriptor_t services[AC_MAX_SERVICES].
    uint32_t num_services.
    uint32_t next_service_id.
    
    ac_service_instance_t instances[AC_MAX_SERVICE_MESH_NODES].
    uint32_t num_instances.
    uint32_t next_instance_id.
    
    ac_deployment_t deployments[AC_MAX_DEPLOYMENTS].
    uint32_t num_deployments.
    uint32_t next_deployment_id.
    
    ac_service_mesh_node_t mesh_nodes[AC_MAX_SERVICE_MESH_NODES].
    uint32_t num_mesh_nodes.
    
    /* Global statistics */
    struct {
        uint64_t total_services_registered.
        uint64_t total_instances_deployed.
        uint64_t total_deployments_completed.
        uint64_t total_rollbacks.
        uint64_t total_invocations.
        uint64_t total_revenue.
        uint64_t total_fees_collected.
        uint64_t total_scale_up_events.
        uint64_t total_scale_down_events.
        uint64_t total_health_checks.
        uint64_t total_self_heals.
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
        bool auto_scaling_enabled.
        bool canary_analysis_enabled.
        bool economic_optimization_enabled.
        uint32_t health_check_interval_ms.
        surplus_real_t min_service_coverage.
        bool require_mtls.
    } config.
    
    bool initialized.
} app_constellation_t;

/* ===== API ===== */

/* Initialize the Application Constellation */
void ac_init(app_constellation_t *constellation,
             app_fabric_t *app_fabric,
             financial_fabric_t *financial,
             identity_fabric_t *identity,
             orbital_fabric_t *orbital,
             compute_fabric_t *compute,
             security_fabric_t *security,
             storage_fabric_t *storage,
             network_fabric_t *network,
             media_fabric_t *media,
             governance_fabric_t *governance);

/* ===== Service Management ===== */

int32_t ac_register_service(app_constellation_t *constellation,
                            const char *name, const char *version,
                            ac_service_type_t type,
                            uint32_t app_descriptor_id,
                            uint32_t min_replicas, uint32_t max_replicas,
                            const char **required_capabilities, uint32_t num_capabilities,
                            uint64_t price_per_invocation, uint8_t pricing_form);

ac_service_descriptor_t *ac_get_service(app_constellation_t *constellation, uint32_t service_id);
ac_service_descriptor_t *ac_get_service_by_name(app_constellation_t *constellation, const char *name, const char *version);

/* ===== Deployment ===== */

int32_t ac_deploy_service(app_constellation_t *constellation,
                          uint32_t service_id,
                          const char *version,
                          ac_deploy_strategy_t strategy,
                          uint32_t target_replicas);

int32_t ac_rollback_deployment(app_constellation_t *constellation, uint32_t deployment_id);
int32_t ac_pause_deployment(app_constellation_t *constellation, uint32_t deployment_id);
int32_t ac_resume_deployment(app_constellation_t *constellation, uint32_t deployment_id);

ac_deployment_t *ac_get_deployment(app_constellation_t *constellation, uint32_t deployment_id);

/* ===== Scaling ===== */

int32_t ac_scale_service(app_constellation_t *constellation,
                         uint32_t service_id, uint32_t target_replicas);

int32_t ac_auto_scale(app_constellation_t *constellation, uint32_t service_id);

/* ===== Service Mesh ===== */

int32_t ac_register_mesh_node(app_constellation_t *constellation,
                              uint32_t service_instance_id,
                              uint32_t mesh_network_id);

int32_t ac_create_service_route(app_constellation_t *constellation,
                                uint32_t source_instance_id,
                                const char *destination_service,
                                uint64_t price_per_invocation,
                                surplus_real_t latency_slo,
                                surplus_real_t availability_slo);

/* ===== Invocation ===== */

int32_t ac_invoke_service(app_constellation_t *constellation,
                          uint32_t caller_instance_id,
                          const char *service_name,
                          const uint8_t *payload, uint16_t payload_len,
                          uint8_t *response, uint16_t *response_len);

/* ===== Health & Self-Healing ===== */

int32_t ac_check_service_health(app_constellation_t *constellation,
                                uint32_t service_instance_id,
                                void *health_out);

int32_t ac_self_heal_service(app_constellation_t *constellation,
                             uint32_t service_instance_id);

int32_t ac_check_constellation_health(app_constellation_t *constellation);

bool ac_global_safety_gate(app_constellation_t *constellation);

lpres_state_t ac_attest(app_constellation_t *constellation, uint32_t instance_id,
                        uint32_t op_id, void *args, int32_t result);

/* Coverage enforcement */
void ac_update_coverage(app_constellation_t *constellation);
bool ac_enforce_coverage(app_constellation_t *constellation, surplus_real_t min_ratio);

/* Statistics */
void ac_get_stats(app_constellation_t *constellation, void *stats_out);

/* Paraconsistent state */
lpres_state_t ac_get_attestation(app_constellation_t *constellation, uint32_t instance_id);
void ac_set_attestation(app_constellation_t *constellation, uint32_t instance_id, lpres_state_t state);

/* Utility */
const char *ac_lpres_state_name(lpres_state_t state);
const char *ac_service_type_name(ac_service_type_t type);
const char *ac_deploy_strategy_name(ac_deploy_strategy_t strategy);
const char *ac_instance_state_name(int state);
const char *ac_deploy_state_name(int state);

#endif /* APP_CONSTELLATION_H */
