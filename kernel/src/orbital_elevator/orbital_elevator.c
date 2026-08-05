/* orbital_elevator.c — ZXV Orbital Elevator Compatibility Fabric
 *
 * Implements the schema registry, compatibility graph, adapter registry,
 * and translation engine. The translation engine uses BFS to find the
 * shortest adapter path from source to target schema/version.
 *
 * Author: 36N9 Genetics, LLC
 * License: SEL-3.3 (kernel component)
 */

#include "orbital_elevator.h"

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

void oe_init(oe_elevator_t *oe) {
    if (!oe) return;
    ev_memset(oe, 0, sizeof(*oe));
    oe->next_adapter_id = 1;
}

/* ===== Schema Registry ===== */

oe_schema_t *oe_get_schema(oe_elevator_t *oe, const char *name) {
    if (!oe || !name) return NULL;
    for (uint32_t i = 0; i < OE_MAX_SCHEMAS; i++) {
        if (oe->schemas[i].registered && str_eq(oe->schemas[i].name, name))
            return &oe->schemas[i];
    }
    return NULL;
}

bool oe_register_schema(oe_elevator_t *oe, const char *name) {
    if (!oe || !name) return false;
    if (oe_get_schema(oe, name)) return true; /* already registered */

    for (uint32_t i = 0; i < OE_MAX_SCHEMAS; i++) {
        if (!oe->schemas[i].registered) {
            ev_memset(&oe->schemas[i], 0, sizeof(oe->schemas[i]));
            copy_str(oe->schemas[i].name, name, OE_MAX_NAME_LEN);
            oe->schemas[i].registered = true;
            oe->num_schemas++;
            return true;
        }
    }
    return false;
}

bool oe_schema_add_version(oe_elevator_t *oe, const char *name,
                            uint16_t version, uint32_t min_payload,
                            uint32_t max_payload) {
    if (!oe || !name) return false;
    oe_schema_t *s = oe_get_schema(oe, name);
    if (!s) return false;
    if (s->num_versions >= OE_MAX_VERSIONS) return false;

    /* Check for duplicate version */
    for (uint32_t i = 0; i < s->num_versions; i++) {
        if (s->versions[i].version == version) return false;
    }

    oe_schema_version_t *v = &s->versions[s->num_versions++];
    v->version = version;
    v->min_payload_size = min_payload;
    v->max_payload_size = max_payload;
    v->deprecated = false;
    return true;
}

bool oe_schema_exists(oe_elevator_t *oe, const char *name) {
    return oe_get_schema(oe, name) != NULL;
}

int16_t oe_schema_latest_version(oe_elevator_t *oe, const char *name) {
    oe_schema_t *s = oe_get_schema(oe, name);
    if (!s || s->num_versions == 0) return -1;
    uint16_t latest = 0;
    for (uint32_t i = 0; i < s->num_versions; i++) {
        if (s->versions[i].version > latest && !s->versions[i].deprecated)
            latest = s->versions[i].version;
    }
    return (int16_t)latest;
}

/* ===== Adapter Registry ===== */

oe_adapter_t *oe_get_adapter(oe_elevator_t *oe, uint32_t adapter_id) {
    if (!oe) return NULL;
    for (uint32_t i = 0; i < OE_MAX_ADAPTERS; i++) {
        if (oe->adapters[i].registered && oe->adapters[i].id == adapter_id)
            return &oe->adapters[i];
    }
    return NULL;
}

uint32_t oe_register_adapter(oe_elevator_t *oe, const char *name,
                              oe_adapter_fn transform) {
    if (!oe || !name || !transform) return 0;

    for (uint32_t i = 0; i < OE_MAX_ADAPTERS; i++) {
        if (!oe->adapters[i].registered) {
            ev_memset(&oe->adapters[i], 0, sizeof(oe->adapters[i]));
            oe->adapters[i].id = oe->next_adapter_id++;
            copy_str(oe->adapters[i].name, name, OE_MAX_NAME_LEN);
            oe->adapters[i].transform = transform;
            oe->adapters[i].signed_adapter = false;
            oe->adapters[i].registered = true;
            oe->num_adapters++;
            return oe->adapters[i].id;
        }
    }
    return 0;
}

/* ===== Compatibility Graph ===== */

bool oe_register_compat(oe_elevator_t *oe,
                         const char *from_schema, uint16_t from_version,
                         const char *to_schema, uint16_t to_version,
                         uint32_t adapter_id) {
    if (!oe || !from_schema || !to_schema) return false;
    if (oe->num_edges >= OE_MAX_COMPAT_EDGES) return false;
    if (!oe_get_adapter(oe, adapter_id)) return false;

    oe_compat_edge_t *e = &oe->edges[oe->num_edges++];
    copy_str(e->from_schema, from_schema, OE_MAX_NAME_LEN);
    e->from_version = from_version;
    copy_str(e->to_schema, to_schema, OE_MAX_NAME_LEN);
    e->to_version = to_version;
    e->adapter_id = adapter_id;
    e->valid = true;
    return true;
}

/* ===== Orbit Manifests ===== */

bool oe_register_orbit(oe_elevator_t *oe, const char *name,
                        const char *arch, uint16_t api_version) {
    if (!oe || !name || !arch) return false;

    /* Check for duplicate */
    for (uint32_t i = 0; i < OE_MAX_ORBITS; i++) {
        if (oe->orbits[i].registered && str_eq(oe->orbits[i].name, name))
            return true; /* already registered */
    }

    for (uint32_t i = 0; i < OE_MAX_ORBITS; i++) {
        if (!oe->orbits[i].registered) {
            ev_memset(&oe->orbits[i], 0, sizeof(oe->orbits[i]));
            copy_str(oe->orbits[i].name, name, OE_MAX_NAME_LEN);
            copy_str(oe->orbits[i].arch, arch, 16);
            oe->orbits[i].api_version = api_version;
            oe->orbits[i].registered = true;
            oe->num_orbits++;
            return true;
        }
    }
    return false;
}

bool oe_orbit_add_dep(oe_elevator_t *oe, const char *orbit_name,
                       const char *dep_name, uint16_t min_version,
                       bool required) {
    if (!oe || !orbit_name || !dep_name) return false;

    for (uint32_t i = 0; i < OE_MAX_ORBITS; i++) {
        if (oe->orbits[i].registered && str_eq(oe->orbits[i].name, orbit_name)) {
            if (oe->orbits[i].num_deps >= OE_MAX_DEPS) return false;
            oe_orbit_dep_t *d = &oe->orbits[i].deps[oe->orbits[i].num_deps++];
            copy_str(d->name, dep_name, OE_MAX_NAME_LEN);
            d->min_version = min_version;
            d->required = required;
            return true;
        }
    }
    return false;
}

bool oe_orbit_compatible(oe_elevator_t *oe, const char *orbit_name) {
    if (!oe || !orbit_name) return false;

    for (uint32_t i = 0; i < OE_MAX_ORBITS; i++) {
        if (oe->orbits[i].registered && str_eq(oe->orbits[i].name, orbit_name)) {
            for (uint32_t j = 0; j < oe->orbits[i].num_deps; j++) {
                oe_orbit_dep_t *d = &oe->orbits[i].deps[j];
                if (!d->required) continue;

                oe_schema_t *s = oe_get_schema(oe, d->name);
                if (!s) return false;

                bool found = false;
                for (uint32_t k = 0; k < s->num_versions; k++) {
                    if (s->versions[k].version >= d->min_version &&
                        !s->versions[k].deprecated) {
                        found = true;
                        break;
                    }
                }
                if (!found) return false;
            }
            return true;
        }
    }
    return false;
}

/* ===== Translation Engine ===== */

/* BFS node for path finding */
typedef struct {
    char schema[OE_MAX_NAME_LEN];
    uint16_t version;
    uint32_t edge_index;   /* which edge got us here */
    uint32_t prev_node;    /* previous node in path (or UINT32_MAX for source) */
} bfs_node_t;

oe_translate_result_t oe_translate(oe_elevator_t *oe,
                                    ev_envelope_t *env,
                                    const char *target_schema,
                                    uint16_t target_version) {
    if (!oe || !env || !target_schema) return OE_TRANSLATE_FAILED;

    /* If already at target schema+version, no translation needed */
    if (str_eq(env->schema, target_schema) && env->schema_version == target_version) {
        oe->total_translations++;
        return OE_TRANSLATE_OK;
    }

    /* Verify target schema exists */
    oe_schema_t *target_s = oe_get_schema(oe, target_schema);
    if (!target_s) return OE_TRANSLATE_SCHEMA_NOT_FOUND;

    /* Verify target version exists */
    bool target_version_exists = false;
    for (uint32_t i = 0; i < target_s->num_versions; i++) {
        if (target_s->versions[i].version == target_version) {
            target_version_exists = true;
            break;
        }
    }
    if (!target_version_exists) return OE_TRANSLATE_VERSION_NOT_FOUND;

    /* BFS to find shortest adapter path */
    static bfs_node_t queue[OE_MAX_COMPAT_EDGES + 1];
    uint32_t queue_head = 0, queue_tail = 0;

    /* Start node: current envelope schema/version */
    copy_str(queue[0].schema, env->schema, OE_MAX_NAME_LEN);
    queue[0].version = env->schema_version;
    queue[0].edge_index = UINT32_MAX;
    queue[0].prev_node = UINT32_MAX;
    queue_tail = 1;

    bool visited[OE_MAX_COMPAT_EDGES];
    for (uint32_t i = 0; i < OE_MAX_COMPAT_EDGES; i++) visited[i] = false;

    int32_t found_node = -1;

    while (queue_head < queue_tail) {
        uint32_t cur = queue_head;
        queue_head++;

        /* Check if we've reached the target */
        if (str_eq(queue[cur].schema, target_schema) &&
            queue[cur].version == target_version) {
            found_node = (int32_t)cur;
            break;
        }

        /* Explore edges from current node */
        for (uint32_t e = 0; e < oe->num_edges; e++) {
            if (!oe->edges[e].valid) continue;
            if (visited[e]) continue;

            if (str_eq(oe->edges[e].from_schema, queue[cur].schema) &&
                oe->edges[e].from_version == queue[cur].version) {
                visited[e] = true;

                if (queue_tail >= OE_MAX_COMPAT_EDGES + 1) break;

                copy_str(queue[queue_tail].schema,
                         oe->edges[e].to_schema, OE_MAX_NAME_LEN);
                queue[queue_tail].version = oe->edges[e].to_version;
                queue[queue_tail].edge_index = e;
                queue[queue_tail].prev_node = cur;
                queue_tail++;
            }
        }
    }

    if (found_node < 0) {
        oe->total_no_path++;
        return OE_TRANSLATE_NO_PATH;
    }

    /* Reconstruct path (reverse order) */
    static uint32_t path[OE_MAX_HOPS];
    uint32_t path_len = 0;

    int32_t node = found_node;
    while (node >= 0 && queue[node].prev_node != UINT32_MAX) {
        if (path_len >= OE_MAX_HOPS) {
            return OE_TRANSLATE_TOO_MANY;
        }
        path[path_len++] = queue[node].edge_index;
        node = (int32_t)queue[node].prev_node;
    }

    if (path_len > OE_MAX_HOPS) {
        return OE_TRANSLATE_TOO_MANY;
    }

    /* Apply adapters in forward order (reverse of reconstructed path) */
    ev_envelope_t current = *env;

    for (int32_t i = (int32_t)path_len - 1; i >= 0; i--) {
        oe_compat_edge_t *edge = &oe->edges[path[i]];
        oe_adapter_t *adapter = oe_get_adapter(oe, edge->adapter_id);
        if (!adapter) {
            oe->total_failures++;
            return OE_TRANSLATE_FAILED;
        }

        ev_envelope_t next;
        ev_memset(&next, 0, sizeof(next));

        if (!adapter->transform(&current, &next)) {
            adapter->failure_count++;
            oe->total_failures++;
            return OE_TRANSLATE_FAILED;
        }

        adapter->invocation_count++;
        oe->total_hops++;

        /* Update schema info on the envelope */
        copy_str(next.schema, edge->to_schema, EV_SCHEMA_LEN);
        next.schema_version = edge->to_version;

        /* Recompute CRC after transformation */
        ev_envelope_compute_crc(&next);

        current = next;
    }

    /* Write result back to caller's envelope */
    *env = current;

    oe->total_translations++;
    return OE_TRANSLATE_OK;
}
