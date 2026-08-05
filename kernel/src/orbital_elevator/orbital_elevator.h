/* orbital_elevator.h — ZXV Orbital Elevator Compatibility Fabric
 *
 * The Orbital Elevator handles incompatibilities across version,
 * architecture, ABI, and runtime through canonical event translation.
 * It does NOT do instruction translation — it transforms event schemas.
 *
 * Architecture:
 *   Source schema v1 → canonical event → adapter → target schema v4
 *
 * Components:
 *   1. Schema registry — maps schema names to versioned definitions
 *   2. Compatibility graph — declares which schema versions can adapt
 *   3. Adapter registry — signed transformation functions
 *   4. Orbit manifest — architecture/API/dependency declarations
 *   5. Translation engine — applies adapters to event envelopes
 *
 * Design principles:
 *   - Adapters are pure functions: (input_envelope) → (output_envelope)
 *   - Every adapter is signed and versioned
 *   - If no adapter path exists, the event is rejected (never silently corrupted)
 *   - Adapter chains are bounded (max 8 hops) to prevent infinite loops
 *   - All transformations are logged for audit
 *   - Paraconsistent: if a field can't be translated, it's marked unknown
 *     rather than dropped or guessed
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ORBITAL_ELEVATOR_H
#define ORBITAL_ELEVATOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../event_space/event_space.h"

/* ===== Constants ===== */

#define OE_MAX_SCHEMAS        64    /* max registered schema definitions */
#define OE_MAX_VERSIONS       8     /* max versions per schema */
#define OE_MAX_ADAPTERS       64    /* max registered adapters */
#define OE_MAX_NAME_LEN       64    /* schema/adapter name length */
#define OE_MAX_COMPAT_EDGES   128   /* max compatibility graph edges */
#define OE_MAX_HOPS           8     /* max adapter chain length */
#define OE_MAX_ORBITS         16    /* max orbit manifests */
#define OE_MAX_DEPS           8     /* max dependencies per orbit */
#define OE_PAYLOAD_MAX        EV_PAYLOAD_MAX

/* ===== Schema Registry ===== */

typedef struct oe_schema_version {
    uint16_t version;               /* schema version number */
    uint32_t min_payload_size;      /* minimum acceptable payload size */
    uint32_t max_payload_size;      /* maximum acceptable payload size */
    bool deprecated;                /* true if this version is deprecated */
} oe_schema_version_t;

typedef struct oe_schema {
    char name[OE_MAX_NAME_LEN];     /* e.g., "zxv.storage.read" */
    uint32_t num_versions;
    oe_schema_version_t versions[OE_MAX_VERSIONS];
    bool registered;
} oe_schema_t;

/* ===== Compatibility Graph ===== */

typedef struct oe_compat_edge {
    char from_schema[OE_MAX_NAME_LEN];
    uint16_t from_version;
    char to_schema[OE_MAX_NAME_LEN];
    uint16_t to_version;
    uint32_t adapter_id;            /* adapter that performs this transformation */
    bool valid;
} oe_compat_edge_t;

/* ===== Adapter ===== */

/* Adapter transform function type:
 * Takes input envelope + output envelope, returns true on success.
 * The function must be pure — no side effects, no external state. */
typedef bool (*oe_adapter_fn)(const ev_envelope_t *in,
                               ev_envelope_t *out);

typedef struct oe_adapter {
    uint32_t id;
    char name[OE_MAX_NAME_LEN];     /* e.g., "storage.read.v1_to_v4" */
    oe_adapter_fn transform;
    bool signed_adapter;            /* true if cryptographically signed */
    uint32_t invocation_count;      /* how many times this adapter was used */
    uint32_t failure_count;         /* transformation failures */
    bool registered;
} oe_adapter_t;

/* ===== Orbit Manifest ===== */

typedef struct oe_orbit_dep {
    char name[OE_MAX_NAME_LEN];
    uint16_t min_version;
    bool required;
} oe_orbit_dep_t;

typedef struct oe_orbit {
    char name[OE_MAX_NAME_LEN];     /* e.g., "org.zxv.notes" */
    char arch[16];                  /* "x86_64", "arm64", "riscv64", "neutral" */
    uint16_t api_version;
    uint32_t num_deps;
    oe_orbit_dep_t deps[OE_MAX_DEPS];
    bool registered;
} oe_orbit_t;

/* ===== Translation Result ===== */

typedef enum {
    OE_TRANSLATE_OK       = 0,  /* translation succeeded */
    OE_TRANSLATE_NO_PATH  = 1,  /* no adapter path found */
    OE_TRANSLATE_FAILED   = 2,  /* adapter returned false */
    OE_TRANSLATE_TOO_MANY = 3,  /* exceeded max hops */
    OE_TRANSLATE_SCHEMA_NOT_FOUND = 4,
    OE_TRANSLATE_VERSION_NOT_FOUND = 5,
} oe_translate_result_t;

/* ===== Orbital Elevator ===== */

typedef struct oe_elevator {
    /* Schema registry */
    oe_schema_t schemas[OE_MAX_SCHEMAS];
    uint32_t num_schemas;

    /* Compatibility graph */
    oe_compat_edge_t edges[OE_MAX_COMPAT_EDGES];
    uint32_t num_edges;

    /* Adapter registry */
    oe_adapter_t adapters[OE_MAX_ADAPTERS];
    uint32_t num_adapters;
    uint32_t next_adapter_id;

    /* Orbit manifests */
    oe_orbit_t orbits[OE_MAX_ORBITS];
    uint32_t num_orbits;

    /* Statistics */
    uint64_t total_translations;
    uint64_t total_failures;
    uint64_t total_no_path;
    uint64_t total_hops;           /* total adapter hops across all translations */
} oe_elevator_t;

/* ===== API ===== */

/* Initialize the Orbital Elevator */
void oe_init(oe_elevator_t *oe);

/* Register a schema with versions */
bool oe_register_schema(oe_elevator_t *oe, const char *name);
bool oe_schema_add_version(oe_elevator_t *oe, const char *name,
                            uint16_t version, uint32_t min_payload,
                            uint32_t max_payload);

/* Register an adapter */
uint32_t oe_register_adapter(oe_elevator_t *oe, const char *name,
                              oe_adapter_fn transform);

/* Register a compatibility edge (from→to with adapter) */
bool oe_register_compat(oe_elevator_t *oe,
                         const char *from_schema, uint16_t from_version,
                         const char *to_schema, uint16_t to_version,
                         uint32_t adapter_id);

/* Register an orbit manifest */
bool oe_register_orbit(oe_elevator_t *oe, const char *name,
                        const char *arch, uint16_t api_version);

/* Add a dependency to an orbit */
bool oe_orbit_add_dep(oe_elevator_t *oe, const char *orbit_name,
                       const char *dep_name, uint16_t min_version,
                       bool required);

/* Find a translation path and apply it.
 * Translates the event envelope from its current schema/version
 * to the target schema/version.
 * Returns OE_TRANSLATE_OK on success. */
oe_translate_result_t oe_translate(oe_elevator_t *oe,
                                    ev_envelope_t *env,
                                    const char *target_schema,
                                    uint16_t target_version);

/* Check if a schema is registered */
bool oe_schema_exists(oe_elevator_t *oe, const char *name);

/* Get the latest version of a schema */
int16_t oe_schema_latest_version(oe_elevator_t *oe, const char *name);

/* Check if an orbit is compatible (all required deps satisfied) */
bool oe_orbit_compatible(oe_elevator_t *oe, const char *orbit_name);

/* Get adapter by ID */
oe_adapter_t *oe_get_adapter(oe_elevator_t *oe, uint32_t adapter_id);

/* Get schema by name */
oe_schema_t *oe_get_schema(oe_elevator_t *oe, const char *name);

#endif /* ORBITAL_ELEVATOR_H */
