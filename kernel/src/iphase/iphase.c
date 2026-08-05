/* iphase.c — Inter-Phase Routing and Endpoint Relationship (K4)
 *
 * Implements deterministic route contracts, endpoint registry, failover
 * chains, and route selection with tie-breaking policies.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */

#include "iphase.h"

/* ===== Helpers ===== */

static void copy_str(char *dst, const char *src, uint32_t max) {
    uint32_t i;
    for (i = 0; i + 1 < max && src && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static bool str_equal(const char *a, const char *b, uint32_t max) {
    for (uint32_t i = 0; i < max; i++) {
        if (a[i] != b[i]) return false;
        if (a[i] == '\0') return true;
    }
    return false;
}

/* ===== Registry Init ===== */

void iphase_registry_init(iphase_registry_t *reg) {
    if (!reg) return;
    ev_memset(reg, 0, sizeof(*reg));
    reg->next_endpoint_id = 1;
    reg->next_route_id = 1;
}

/* ===== Endpoint Management ===== */

int32_t iphase_register_endpoint(iphase_registry_t *reg, const char *name,
                                 iphase_endpoint_type_t type,
                                 const char *capability,
                                 uint32_t phase_binding) {
    if (!reg || !name) return -1;
    if (reg->endpoint_count >= IPHASE_MAX_ENDPOINTS) return -1;

    /* Check for duplicate name */
    for (uint32_t i = 0; i < reg->endpoint_count; i++) {
        if (str_equal(reg->endpoints[i].name, name, IPHASE_MAX_NAME_LEN))
            return (int32_t)i;
    }

    iphase_endpoint_t *ep = &reg->endpoints[reg->endpoint_count];
    ev_memset(ep, 0, sizeof(*ep));
    ep->id = reg->next_endpoint_id++;
    copy_str(ep->name, name, IPHASE_MAX_NAME_LEN);
    ep->type = type;
    if (capability)
        copy_str(ep->capability, capability, IPHASE_MAX_CAP_LEN);
    ep->phase_binding = phase_binding;
    ep->active = true;
    ep->load = 0;
    return (int32_t)reg->endpoint_count++;
}

iphase_endpoint_t *iphase_get_endpoint(iphase_registry_t *reg, uint32_t idx) {
    if (!reg || idx >= reg->endpoint_count) return NULL;
    return &reg->endpoints[idx];
}

int32_t iphase_find_endpoint(iphase_registry_t *reg, const char *name) {
    if (!reg || !name) return -1;
    for (uint32_t i = 0; i < reg->endpoint_count; i++) {
        if (str_equal(reg->endpoints[i].name, name, IPHASE_MAX_NAME_LEN))
            return (int32_t)i;
    }
    return -1;
}

bool iphase_set_endpoint_active(iphase_registry_t *reg, uint32_t idx, bool active) {
    iphase_endpoint_t *ep = iphase_get_endpoint(reg, idx);
    if (!ep) return false;
    ep->active = active;
    return true;
}

/* ===== Route Management ===== */

int32_t iphase_add_route(iphase_registry_t *reg, uint32_t source, uint32_t dest,
                         uint32_t priority, uint32_t weight,
                         bool requires_admission) {
    if (!reg) return -1;
    if (source >= reg->endpoint_count || dest >= reg->endpoint_count) return -1;
    if (reg->route_count >= IPHASE_MAX_ROUTES) return -1;

    iphase_route_t *r = &reg->routes[reg->route_count];
    ev_memset(r, 0, sizeof(*r));
    r->id = reg->next_route_id++;
    r->source_endpoint = source;
    r->dest_endpoint = dest;
    r->priority = priority;
    r->weight = weight;
    r->status = IPHASE_ROUTE_ACTIVE;
    r->requires_admission = requires_admission;
    r->use_count = 0;
    r->failover_count = 0;
    return (int32_t)reg->route_count++;
}

bool iphase_set_route_status(iphase_registry_t *reg, uint32_t route_idx,
                             iphase_route_status_t status) {
    iphase_route_t *r = iphase_get_route(reg, route_idx);
    if (!r) return false;
    r->status = status;
    return true;
}

bool iphase_add_failover(iphase_registry_t *reg, uint32_t route_idx,
                         uint32_t alt_dest) {
    if (!reg) return false;
    iphase_route_t *r = iphase_get_route(reg, route_idx);
    if (!r) return false;
    if (r->failover_count >= IPHASE_MAX_FAILOVERS) return false;
    if (alt_dest >= reg->endpoint_count) return false;
    r->failover_chain[r->failover_count++] = alt_dest;
    return true;
}

iphase_route_t *iphase_get_route(iphase_registry_t *reg, uint32_t idx) {
    if (!reg || idx >= reg->route_count) return NULL;
    return &reg->routes[idx];
}

/* ===== Route Selection ===== */

int32_t iphase_select_route(iphase_registry_t *reg, uint32_t source,
                            uint32_t dest, iphase_tie_policy_t tie_policy) {
    if (!reg || source >= reg->endpoint_count || dest >= reg->endpoint_count)
        return -1;

    /* Find all active routes from source to dest */
    int32_t candidates[IPHASE_MAX_ROUTES];
    uint32_t candidate_count = 0;
    uint32_t best_priority = UINT32_MAX;
    uint32_t best_weight = 0;

    for (uint32_t i = 0; i < reg->route_count; i++) {
        iphase_route_t *r = &reg->routes[i];
        if (r->source_endpoint != source || r->dest_endpoint != dest)
            continue;
        if (r->status != IPHASE_ROUTE_ACTIVE)
            continue;
        /* Check endpoints are active */
        if (!reg->endpoints[source].active || !reg->endpoints[dest].active)
            continue;

        if (r->priority < best_priority) {
            best_priority = r->priority;
            best_weight = r->weight;
            candidate_count = 0;
            candidates[candidate_count++] = (int32_t)i;
        } else if (r->priority == best_priority) {
            if (r->weight > best_weight) {
                best_weight = r->weight;
                candidate_count = 0;
                candidates[candidate_count++] = (int32_t)i;
            } else if (r->weight == best_weight) {
                if (candidate_count < IPHASE_MAX_ROUTES)
                    candidates[candidate_count++] = (int32_t)i;
            }
        }
    }

    if (candidate_count == 0) {
        reg->routes_rejected++;
        return -1;
    }

    /* Tie-breaking */
    int32_t selected;
    if (candidate_count == 1) {
        selected = candidates[0];
    } else {
        switch (tie_policy) {
            case IPHASE_TIE_LOWEST_ID:
                selected = candidates[0];
                for (uint32_t i = 1; i < candidate_count; i++) {
                    if (candidates[i] < selected) selected = candidates[i];
                }
                break;
            case IPHASE_TIE_HIGHEST_ID:
                selected = candidates[0];
                for (uint32_t i = 1; i < candidate_count; i++) {
                    if (candidates[i] > selected) selected = candidates[i];
                }
                break;
            case IPHASE_TIE_ROUND_ROBIN:
                selected = candidates[reg->rr_counter % candidate_count];
                reg->rr_counter++;
                break;
            case IPHASE_TIE_DEFER_S0:
            default:
                /* Defer — return -2 to indicate S0 ambiguity */
                reg->routes_rejected++;
                return -2;
        }
    }

    reg->routes[selected].use_count++;
    reg->endpoints[dest].load++;
    reg->routes_selected++;
    return selected;
}

int32_t iphase_select_failover(iphase_registry_t *reg, uint32_t route_idx) {
    iphase_route_t *r = iphase_get_route(reg, route_idx);
    if (!r || r->failover_count == 0) return -1;

    for (uint8_t i = 0; i < r->failover_count; i++) {
        uint32_t alt = r->failover_chain[i];
        if (alt < reg->endpoint_count && reg->endpoints[alt].active) {
            reg->failovers_triggered++;
            return (int32_t)alt;
        }
    }
    return -1;
}

/* ===== Queries ===== */

uint32_t iphase_count_routes_by_status(iphase_registry_t *reg,
                                       iphase_route_status_t status) {
    if (!reg) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < reg->route_count; i++) {
        if (reg->routes[i].status == status) count++;
    }
    return count;
}

uint32_t iphase_count_endpoints_by_type(iphase_registry_t *reg,
                                        iphase_endpoint_type_t type) {
    if (!reg) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < reg->endpoint_count; i++) {
        if (reg->endpoints[i].type == type) count++;
    }
    return count;
}

bool iphase_is_endpoint_reachable(iphase_registry_t *reg, uint32_t source,
                                  uint32_t dest) {
    if (!reg || source >= reg->endpoint_count || dest >= reg->endpoint_count)
        return false;
    if (!reg->endpoints[source].active || !reg->endpoints[dest].active)
        return false;
    for (uint32_t i = 0; i < reg->route_count; i++) {
        iphase_route_t *r = &reg->routes[i];
        if (r->source_endpoint == source && r->dest_endpoint == dest &&
            r->status == IPHASE_ROUTE_ACTIVE)
            return true;
    }
    return false;
}

/* ===== Name Functions ===== */

const char *iphase_endpoint_type_name(iphase_endpoint_type_t type) {
    switch (type) {
        case IPHASE_EP_KERNEL_SERVICE: return "kernel_service";
        case IPHASE_EP_USER_SERVICE:   return "user_service";
        case IPHASE_EP_HARDWARE:       return "hardware";
        case IPHASE_EP_NETWORK:        return "network";
        case IPHASE_EP_MEDIA:          return "media";
        case IPHASE_EP_TUNNEL:         return "tunnel";
        case IPHASE_EP_SESSION:        return "session";
        case IPHASE_EP_EXTERNAL:       return "external";
        default:                        return "unknown";
    }
}

const char *iphase_route_status_name(iphase_route_status_t status) {
    switch (status) {
        case IPHASE_ROUTE_ACTIVE:     return "active";
        case IPHASE_ROUTE_DEGRADED:   return "degraded";
        case IPHASE_ROUTE_DISABLED:   return "disabled";
        case IPHASE_ROUTE_FAILED:     return "failed";
        case IPHASE_ROUTE_QUARANTINED: return "quarantined";
        default:                       return "unknown";
    }
}

const char *iphase_tie_policy_name(iphase_tie_policy_t policy) {
    switch (policy) {
        case IPHASE_TIE_LOWEST_ID:    return "lowest_id";
        case IPHASE_TIE_HIGHEST_ID:   return "highest_id";
        case IPHASE_TIE_ROUND_ROBIN:  return "round_robin";
        case IPHASE_TIE_DEFER_S0:     return "defer_s0";
        default:                       return "unknown";
    }
}
