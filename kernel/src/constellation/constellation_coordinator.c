/* constellation_coordinator.c — ZXV Constellation Coordinator
 *
 * Implements cross-domain event routing, node lifecycle management,
 * executor selection, and health monitoring.
 *
 * Author: 36N9 Genetics, LLC
 * License: SEL-3.3 (kernel component)
 */

#include "constellation_coordinator.h"

/* ===== Helpers ===== */

static void copy_str(char *dst, const char *src, uint32_t max) {
    uint32_t i;
    for (i = 0; i + 1 < max && src && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static bool str_eq(const char *a, const char *b) {
    uint32_t i;
    for (i = 0; a[i] != '\0' && b[i] != '\0'; i++) {
        if (a[i] != b[i]) return false;
    }
    return a[i] == '\0' && b[i] == '\0';
}

/* ===== Initialization ===== */

void cc_coordinator_init(cc_coordinator_t *cc, const char *local_node_id) {
    if (!cc) return;
    ev_memset(cc, 0, sizeof(*cc));

    if (local_node_id) {
        copy_str(cc->local_node.id, local_node_id, CC_NODE_NAME_LEN);
    }
    cc->local_node.incarnation = EV_INCARNATION_INIT;
}

/* ===== Node Management ===== */

int32_t cc_register_node(cc_coordinator_t *cc, const char *node_id,
                          const char *arch, uint32_t incarnation) {
    if (!cc || !node_id) return -1;

    /* Check for existing node with same ID + incarnation */
    for (uint32_t i = 0; i < CC_MAX_NODES; i++) {
        if (cc->nodes[i].registered && str_eq(cc->nodes[i].id, node_id)) {
            if (cc->nodes[i].incarnation == incarnation) {
                return (int32_t)i; /* already registered */
            }
            /* Incarnation changed — node rebooted. Mark old as failed. */
            cc->nodes[i].state = CC_NODE_FAILED;
            cc->total_node_failures++;
            /* Fall through to register new incarnation */
        }
    }

    /* Find free slot */
    for (uint32_t i = 0; i < CC_MAX_NODES; i++) {
        if (!cc->nodes[i].registered) {
            ev_memset(&cc->nodes[i], 0, sizeof(cc->nodes[i]));
            copy_str(cc->nodes[i].id, node_id, CC_NODE_NAME_LEN);
            if (arch) copy_str(cc->nodes[i].arch, arch, 16);
            cc->nodes[i].incarnation = incarnation;
            cc->nodes[i].state = CC_NODE_DISCOVERED;
            cc->nodes[i].registered = true;
            cc->num_nodes++;
            return (int32_t)i;
        }
    }
    return -1;
}

cc_node_t *cc_get_node(cc_coordinator_t *cc, const char *node_id) {
    if (!cc || !node_id) return NULL;
    for (uint32_t i = 0; i < CC_MAX_NODES; i++) {
        if (cc->nodes[i].registered && str_eq(cc->nodes[i].id, node_id))
            return &cc->nodes[i];
    }
    return NULL;
}

cc_node_t *cc_get_node_by_idx(cc_coordinator_t *cc, uint32_t idx) {
    if (!cc || idx >= CC_MAX_NODES) return NULL;
    if (!cc->nodes[idx].registered) return NULL;
    return &cc->nodes[idx];
}

bool cc_node_add_schema(cc_coordinator_t *cc, uint32_t node_idx,
                         const char *schema) {
    if (!cc || !schema) return false;
    cc_node_t *node = cc_get_node_by_idx(cc, node_idx);
    if (!node) return false;
    if (node->num_schemas >= CC_MAX_SCHEMAS_PER_NODE) return false;

    /* Check for duplicate */
    for (uint32_t i = 0; i < node->num_schemas; i++) {
        if (str_eq(node->schemas[i], schema)) return true;
    }

    copy_str(node->schemas[node->num_schemas++], schema, EV_SCHEMA_LEN);
    return true;
}

/* ===== Node State Transitions ===== */

bool cc_node_authenticate(cc_coordinator_t *cc, uint32_t node_idx) {
    if (!cc) return false;
    cc_node_t *node = cc_get_node_by_idx(cc, node_idx);
    if (!node) return false;
    if (node->state != CC_NODE_DISCOVERED) return false;
    node->state = CC_NODE_AUTHENTICATED;
    /* Authenticated nodes enter quarantine until health checks pass */
    node->state = CC_NODE_QUARANTINED;
    return true;
}

bool cc_node_activate(cc_coordinator_t *cc, uint32_t node_idx) {
    if (!cc) return false;
    cc_node_t *node = cc_get_node_by_idx(cc, node_idx);
    if (!node) return false;
    if (node->state != CC_NODE_QUARANTINED && node->state != CC_NODE_DEGRADED)
        return false;
    node->state = CC_NODE_ACTIVE;
    node->consecutive_failures = 0;
    return true;
}

bool cc_node_fail(cc_coordinator_t *cc, uint32_t node_idx) {
    if (!cc) return false;
    cc_node_t *node = cc_get_node_by_idx(cc, node_idx);
    if (!node) return false;
    if (node->state == CC_NODE_FAILED || node->state == CC_NODE_REMOVED)
        return false;
    node->state = CC_NODE_FAILED;
    node->consecutive_failures++;
    cc->total_node_failures++;
    return true;
}

bool cc_node_recover(cc_coordinator_t *cc, uint32_t node_idx) {
    if (!cc) return false;
    cc_node_t *node = cc_get_node_by_idx(cc, node_idx);
    if (!node) return false;
    if (node->state != CC_NODE_FAILED && node->state != CC_NODE_DEGRADED)
        return false;
    node->state = CC_NODE_QUARANTINED;
    node->consecutive_failures = 0;
    cc->total_node_recoveries++;
    return true;
}

bool cc_node_remove(cc_coordinator_t *cc, uint32_t node_idx) {
    if (!cc) return false;
    cc_node_t *node = cc_get_node_by_idx(cc, node_idx);
    if (!node) return false;
    node->state = CC_NODE_REMOVED;
    node->registered = false;
    if (cc->num_nodes > 0) cc->num_nodes--;
    return true;
}

bool cc_node_heartbeat(cc_coordinator_t *cc, uint32_t node_idx,
                        uint64_t sequence) {
    if (!cc) return false;
    cc_node_t *node = cc_get_node_by_idx(cc, node_idx);
    if (!node) return false;
    node->last_heartbeat_sequence = sequence;
    /* If node was degraded, restore to active */
    if (node->state == CC_NODE_DEGRADED) {
        node->state = CC_NODE_ACTIVE;
    }
    return true;
}

/* ===== Executor Management ===== */

int32_t cc_register_executor(cc_coordinator_t *cc, const char *node_id,
                              const char *service,
                              uint32_t cost_p50, uint32_t cost_p99) {
    if (!cc || !node_id || !service) return -1;

    for (uint32_t i = 0; i < CC_MAX_EXECUTORS; i++) {
        if (!cc->executors[i].registered) {
            ev_memset(&cc->executors[i], 0, sizeof(cc->executors[i]));
            copy_str(cc->executors[i].node_id, node_id, CC_NODE_NAME_LEN);
            copy_str(cc->executors[i].service, service, CC_MAX_NAME_LEN);
            cc->executors[i].cost_p50 = cost_p50;
            cc->executors[i].cost_p99 = cost_p99;
            cc->executors[i].memory_mb = 0;
            cc->executors[i].thermal_state = 0;
            cc->executors[i].trust_level = 1;
            cc->executors[i].registered = true;
            cc->num_executors++;
            return (int32_t)i;
        }
    }
    return -1;
}

bool cc_executor_add_schema(cc_coordinator_t *cc, uint32_t exec_idx,
                             const char *schema) {
    if (!cc || !schema) return false;
    if (exec_idx >= CC_MAX_EXECUTORS) return false;
    cc_executor_t *ex = &cc->executors[exec_idx];
    if (!ex->registered) return false;
    if (ex->num_schemas >= CC_MAX_SCHEMAS_PER_NODE) return false;

    for (uint32_t i = 0; i < ex->num_schemas; i++) {
        if (str_eq(ex->schemas[i], schema)) return true;
    }

    copy_str(ex->schemas[ex->num_schemas++], schema, EV_SCHEMA_LEN);
    return true;
}

bool cc_executor_add_capability(cc_coordinator_t *cc, uint32_t exec_idx,
                                 const char *capability) {
    if (!cc || !capability) return false;
    if (exec_idx >= CC_MAX_EXECUTORS) return false;
    cc_executor_t *ex = &cc->executors[exec_idx];
    if (!ex->registered) return false;
    if (ex->num_capabilities >= CC_MAX_CAPABILITIES) return false;

    copy_str(ex->capabilities[ex->num_capabilities++], capability, EV_SCHEMA_LEN);
    return true;
}

cc_executor_t *cc_get_executor(cc_coordinator_t *cc, uint32_t idx) {
    if (!cc || idx >= CC_MAX_EXECUTORS) return NULL;
    if (!cc->executors[idx].registered) return NULL;
    return &cc->executors[idx];
}

int32_t cc_find_executor(cc_coordinator_t *cc, const char *schema,
                          uint16_t min_version) {
    if (!cc || !schema) return -1;
    (void)min_version; /* schema matching is by name; version filtering
                          is handled by the Orbital Elevator translation layer */

    int32_t best = -1;
    uint32_t best_cost = UINT32_MAX;

    for (uint32_t i = 0; i < CC_MAX_EXECUTORS; i++) {
        cc_executor_t *ex = &cc->executors[i];
        if (!ex->registered) continue;

        /* Check if executor supports this schema */
        bool supports = false;
        for (uint32_t j = 0; j < ex->num_schemas; j++) {
            if (str_eq(ex->schemas[j], schema)) {
                supports = true;
                break;
            }
        }
        if (!supports) continue;

        /* Check that the node is active */
        cc_node_t *node = cc_get_node(cc, ex->node_id);
        if (!node || node->state != CC_NODE_ACTIVE) continue;

        /* Select by lowest p50 cost (normalized work units) */
        if (ex->cost_p50 < best_cost) {
            best_cost = ex->cost_p50;
            best = (int32_t)i;
        }
    }

    return best;
}

/* ===== Routing ===== */

bool cc_add_route(cc_coordinator_t *cc, const char *schema,
                   uint16_t min_version, const char *target_node,
                   uint32_t priority) {
    if (!cc || !schema || !target_node) return false;
    if (cc->num_routes >= CC_MAX_ROUTES) return false;

    cc_route_t *r = &cc->routes[cc->num_routes++];
    copy_str(r->schema, schema, EV_SCHEMA_LEN);
    r->min_version = min_version;
    copy_str(r->target_node, target_node, CC_NODE_NAME_LEN);
    r->priority = priority;
    r->active = true;
    return true;
}

cc_node_t *cc_route_event(cc_coordinator_t *cc, const char *schema,
                           uint16_t schema_version) {
    if (!cc || !schema) return NULL;

    /* Find matching routes, pick highest priority active one */
    int32_t best_route = -1;
    uint32_t best_priority = 0;

    for (uint32_t i = 0; i < cc->num_routes; i++) {
        if (!cc->routes[i].active) continue;
        if (!str_eq(cc->routes[i].schema, schema)) continue;
        if (schema_version < cc->routes[i].min_version) continue;

        /* Check that target node is active */
        cc_node_t *node = cc_get_node(cc, cc->routes[i].target_node);
        if (!node || node->state != CC_NODE_ACTIVE) continue;

        if (best_route < 0 || cc->routes[i].priority > best_priority) {
            best_priority = cc->routes[i].priority;
            best_route = (int32_t)i;
        }
    }

    if (best_route < 0) {
        /* No explicit route — try executor-based routing */
        int32_t exec_idx = cc_find_executor(cc, schema, schema_version);
        if (exec_idx >= 0) {
            cc_node_t *node = cc_get_node(cc, cc->executors[exec_idx].node_id);
            if (node) {
                node->total_events_routed++;
                cc->total_events_routed++;
                cc->executors[exec_idx].events_assigned++;
                return node;
            }
        }
        cc->total_events_dropped++;
        return NULL;
    }

    cc_node_t *node = cc_get_node(cc, cc->routes[best_route].target_node);
    if (node) {
        node->total_events_routed++;
        cc->total_events_routed++;
    }
    return node;
}

/* ===== Health Monitoring ===== */

uint32_t cc_check_health(cc_coordinator_t *cc, uint64_t current_sequence,
                          uint64_t timeout_sequences) {
    if (!cc) return 0;
    uint32_t failures = 0;

    for (uint32_t i = 0; i < CC_MAX_NODES; i++) {
        cc_node_t *node = &cc->nodes[i];
        if (!node->registered) continue;
        if (node->state == CC_NODE_FAILED || node->state == CC_NODE_REMOVED)
            continue;

        /* Check heartbeat timeout */
        if (node->state == CC_NODE_ACTIVE || node->state == CC_NODE_DEGRADED) {
            if (current_sequence > node->last_heartbeat_sequence + timeout_sequences) {
                if (node->state == CC_NODE_ACTIVE) {
                    node->state = CC_NODE_DEGRADED;
                    node->consecutive_failures++;
                } else {
                    /* Already degraded — now fail */
                    node->state = CC_NODE_FAILED;
                    node->consecutive_failures++;
                    cc->total_node_failures++;
                    failures++;
                }
            }
        }
    }

    return failures;
}

void cc_update_stats(cc_coordinator_t *cc) {
    if (!cc) return;
    /* Stats are maintained inline by other functions;
     * this is a hook for future aggregation. */
}
