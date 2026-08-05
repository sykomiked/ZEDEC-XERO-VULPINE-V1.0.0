/* iphase.h — Inter-Phase Routing and Endpoint Relationship (K4)
 *
 * K4 IPHASE provides deterministic route contracts for events, sessions,
 * media, tunnels, hardware paths, and failover. It maintains an endpoint
 * registry, route tables with priority/weight, and failover chains.
 *
 * Key responsibilities:
 *   - Register endpoints with capabilities and phase bindings
 *   - Define route contracts between endpoints
 *   - Deterministic route selection (priority + weight + tie policy)
 *   - Failover chain management
 *   - Route validity checking and lifecycle
 *   - Integration with K6 Phase Coordinator for admission
 *
 * Author: 36N9 Genetics, LLC
 * License: SEL-3.3 (kernel component)
 */
#ifndef IPHASE_H
#define IPHASE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "event_space.h"

/* ===== IPHASE Constants ===== */

#define IPHASE_MAX_ENDPOINTS    128
#define IPHASE_MAX_ROUTES       256
#define IPHASE_MAX_FAILOVERS      4
#define IPHASE_MAX_NAME_LEN      32
#define IPHASE_MAX_CAP_LEN       64
#define IPHASE_MAX_ROUTES_PER_EP  8

/* ===== Endpoint Types ===== */

typedef enum {
    IPHASE_EP_KERNEL_SERVICE  = 0,
    IPHASE_EP_USER_SERVICE    = 1,
    IPHASE_EP_HARDWARE        = 2,
    IPHASE_EP_NETWORK         = 3,
    IPHASE_EP_MEDIA           = 4,
    IPHASE_EP_TUNNEL          = 5,
    IPHASE_EP_SESSION         = 6,
    IPHASE_EP_EXTERNAL        = 7,
} iphase_endpoint_type_t;

/* ===== Route Status ===== */

typedef enum {
    IPHASE_ROUTE_ACTIVE    = 0,
    IPHASE_ROUTE_DEGRADED  = 1,
    IPHASE_ROUTE_DISABLED  = 2,
    IPHASE_ROUTE_FAILED    = 3,
    IPHASE_ROUTE_QUARANTINED = 4,
} iphase_route_status_t;

/* ===== Route Selection Tie Policy ===== */

typedef enum {
    IPHASE_TIE_LOWEST_ID   = 0,  /* deterministic: lowest endpoint ID wins */
    IPHASE_TIE_HIGHEST_ID  = 1,  /* deterministic: highest endpoint ID wins */
    IPHASE_TIE_ROUND_ROBIN = 2,  /* stateful: rotate among tied candidates */
    IPHASE_TIE_DEFER_S0    = 3,  /* ambiguous: defer to S0 for resolution */
} iphase_tie_policy_t;

/* ===== Endpoint ===== */

typedef struct iphase_endpoint {
    uint32_t id;
    char name[IPHASE_MAX_NAME_LEN];
    iphase_endpoint_type_t type;
    char capability[IPHASE_MAX_CAP_LEN];
    uint32_t phase_binding;       /* K6 phase ID this endpoint serves */
    bool active;
    uint32_t load;                /* current load counter */
} iphase_endpoint_t;

/* ===== Route Contract ===== */

typedef struct iphase_route {
    uint32_t id;
    uint32_t source_endpoint;
    uint32_t dest_endpoint;
    uint32_t priority;            /* lower = higher priority */
    uint32_t weight;              /* within same priority, higher = preferred */
    iphase_route_status_t status;
    uint32_t failover_chain[IPHASE_MAX_FAILOVERS];
    uint8_t failover_count;
    uint32_t use_count;           /* times this route was selected */
    bool requires_admission;      /* needs K6 Phase Coordinator token */
} iphase_route_t;

/* ===== IPHASE Registry ===== */

typedef struct iphase_registry {
    iphase_endpoint_t endpoints[IPHASE_MAX_ENDPOINTS];
    uint32_t endpoint_count;
    uint32_t next_endpoint_id;

    iphase_route_t routes[IPHASE_MAX_ROUTES];
    uint32_t route_count;
    uint32_t next_route_id;

    /* Round-robin state for tie-breaking */
    uint32_t rr_counter;

    /* Statistics */
    uint32_t routes_selected;
    uint32_t failovers_triggered;
    uint32_t routes_rejected;
} iphase_registry_t;

/* ===== API ===== */

void iphase_registry_init(iphase_registry_t *reg);

/* Endpoint Management */
int32_t iphase_register_endpoint(iphase_registry_t *reg, const char *name,
                                 iphase_endpoint_type_t type,
                                 const char *capability,
                                 uint32_t phase_binding);
iphase_endpoint_t *iphase_get_endpoint(iphase_registry_t *reg, uint32_t idx);
int32_t iphase_find_endpoint(iphase_registry_t *reg, const char *name);
bool iphase_set_endpoint_active(iphase_registry_t *reg, uint32_t idx, bool active);

/* Route Management */
int32_t iphase_add_route(iphase_registry_t *reg, uint32_t source, uint32_t dest,
                         uint32_t priority, uint32_t weight,
                         bool requires_admission);
bool iphase_set_route_status(iphase_registry_t *reg, uint32_t route_idx,
                             iphase_route_status_t status);
bool iphase_add_failover(iphase_registry_t *reg, uint32_t route_idx,
                         uint32_t alt_dest);
iphase_route_t *iphase_get_route(iphase_registry_t *reg, uint32_t idx);

/* Route Selection */
int32_t iphase_select_route(iphase_registry_t *reg, uint32_t source,
                            uint32_t dest, iphase_tie_policy_t tie_policy);
int32_t iphase_select_failover(iphase_registry_t *reg, uint32_t route_idx);

/* Queries */
uint32_t iphase_count_routes_by_status(iphase_registry_t *reg,
                                       iphase_route_status_t status);
uint32_t iphase_count_endpoints_by_type(iphase_registry_t *reg,
                                        iphase_endpoint_type_t type);
bool iphase_is_endpoint_reachable(iphase_registry_t *reg, uint32_t source,
                                  uint32_t dest);

/* Name Functions */
const char *iphase_endpoint_type_name(iphase_endpoint_type_t type);
const char *iphase_route_status_name(iphase_route_status_t status);
const char *iphase_tie_policy_name(iphase_tie_policy_t policy);

#endif /* IPHASE_H */
