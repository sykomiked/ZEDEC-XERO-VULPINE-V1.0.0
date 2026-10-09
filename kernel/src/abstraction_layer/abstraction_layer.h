/* abstraction_layer.h — ZXV Abstraction Layer
 *
 * Universal abstraction layer connecting C/H kernel code to:
 *   - Fortran (legacy scientific computing)
 *   - COBOL (business/financial systems)
 *   - Sutra (AI-native language)
 *   - Assembly (architecture-specific)
 *   - Rust (memory-safe systems)
 *   - Zig (comptime metaprogramming)
 *   - Python (scripting/glue)
 *   - WebAssembly (portable modules)
 *
 * This layer provides:
 * 1. Language-agnostic module registry (via Orbital Compat IR)
 * 2. Paraconsistent state management (LPRES four-valued logic)
 * 3. M5 coverage enforcement (hyperbola ≥ 1.8)
 * 4. Self-audit/self-heal hooks
 * 5. Capability-based security (Porter House)
 * 6. Schema translation (Orbital Elevator)
 * 7. Event-space integration (causal ordering)
 * 8. Economic settlement (Financial Fabric)
 *
 * Every module in the boot sequence is registered here with its
 * dependencies, capabilities, and paraconsistent state.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ABSTRACTION_LAYER_H
#define ABSTRACTION_LAYER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "orbital_compat.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"
#include "porter_house.h"
#include "orbital_elevator.h"
#include "event_space.h"
#include "financial_fabric.h"
#include "identity_fabric.h"
#include "orbital_fabric.h"

/* ===== Constants ===== */

#define AL_MAX_MODULES           256
#define AL_MAX_DEPENDENCIES      32
#define AL_MAX_CAPABILITIES      64
#define AL_MAX_LANGUAGES         16
#define AL_MAX_NAME_LEN          64
#define AL_MAX_SCHEMA_LEN        64

/* ===== Module Languages ===== */

typedef enum {
    AL_LANG_C          = 0,   /* C (kernel native) */
    AL_LANG_FORTRAN    = 1,   /* Fortran (scientific) */
    AL_LANG_COBOL      = 2,   /* COBOL (business) */
    AL_LANG_SUTRA      = 3,   /* Sutra (AI-native) */
    AL_LANG_ASM        = 4,   /* Assembly (arch-specific) */
    AL_LANG_RUST       = 5,   /* Rust (memory-safe) */
    AL_LANG_ZIG        = 6,   /* Zig (comptime) */
    AL_LANG_PYTHON     = 7,   /* Python (scripting) */
    AL_LANG_WASM       = 8,   /* WebAssembly (portable) */
    AL_LANG_MAX
} al_lang_t;

/* ===== Module States (Paraconsistent) ===== */

typedef enum {
    AL_STATE_NEITHER   = 0,  /* Unknown/uninitialized — LPRES: NEITHER */
    AL_STATE_TRUE      = 1,  /* Fully operational — LPRES: TRUE */
    AL_STATE_FALSE     = 2,  /* Failed — LPRES: FALSE */
    AL_STATE_BOTH      = 3,  /* Degraded/contradiction — LPRES: BOTH */
    AL_STATE_HELD      = 4,  /* Requirements met but hardware absent — LPRES: HELD */
    AL_STATE_QUARANTINE = 5  /* Isolated by self-healing */
} al_module_state_t;

/* ===== Module Capability ===== */

typedef struct al_capability {
    char name[AL_MAX_NAME_LEN];
    char description[AL_MAX_NAME_LEN];
    surplus_real_t min_coverage_ratio;
    lpres_state_t min_attestation;
    bool requires_hsm;
    bool requires_zk_proof;
    uint8_t capital_form_required;
    uint64_t min_balance;
    uint32_t policy_id;
    lpres_state_t attestation;
    bool active;
} al_capability_t;

/* ===== Module Dependency ===== */

typedef struct al_dependency {
    uint32_t module_id;
    char name[AL_MAX_NAME_LEN];
    bool required;
    bool satisfied;
    lpres_state_t attestation;
} al_dependency_t;

/* ===== Module Schema ===== */

typedef struct al_schema {
    char name[AL_MAX_SCHEMA_LEN];
    uint16_t version;
    char input_schema[AL_MAX_SCHEMA_LEN];
    char output_schema[AL_MAX_SCHEMA_LEN];
    uint32_t oe_adapter_id;
    uint32_t oc_adapter_id;
    bool active;
} al_schema_t;

/* ===== Module ===== */

typedef struct al_module {
    uint32_t id;
    char name[AL_MAX_NAME_LEN];
    al_lang_t language;
    al_module_state_t state;
    
    /* Source location */
    char source_path[256];
    char entry_symbol[AL_MAX_NAME_LEN];
    
    /* Orbital Compat IR */
    oc_ir_t canonical_ir;
    bool ir_valid;
    
    /* Dependencies */
    al_dependency_t dependencies[AL_MAX_DEPENDENCIES];
    uint32_t num_dependencies;
    
    /* Capabilities provided */
    al_capability_t capabilities[AL_MAX_CAPABILITIES];
    uint32_t num_capabilities;
    
    /* Capabilities required */
    char required_capabilities[AL_MAX_CAPABILITIES][AL_MAX_NAME_LEN];
    uint32_t num_required_capabilities;
    
    /* Schemas */
    al_schema_t schemas[16];
    uint32_t num_schemas;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    surplus_real_t min_coverage_ratio;
    
    /* Paraconsistent attestation */
    lpres_state_t attestation;
    lpres_state_t global_attestation;
    
    /* Health */
    struct {
        uint64_t last_health_check;
        uint32_t consecutive_failures;
        uint64_t total_invocations;
        uint64_t total_failures;
        uint64_t total_self_audits;
        uint64_t total_healings;
    } health;
    
    /* Economic */
    uint64_t price_per_invocation;
    uint8_t pricing_form;
    uint32_t financial_account_id;
    
    /* Security */
    uint32_t porter_house_port;
    uint32_t min_trust_weight;
    uint32_t tls_session_id;
    uint32_t keypair_id;
    
    /* Event-space */
    uint32_t event_domain_id;
    char accepted_schemas[8][AL_MAX_SCHEMA_LEN];
    uint32_t num_accepted_schemas;
    char emitted_schemas[8][AL_MAX_SCHEMA_LEN];
    uint32_t num_emitted_schemas;
    
    /* Lifecycle */
    bool initialized;
    bool active;
    bool builtin;
} al_module_t;

/* ===== Language Adapter ===== */

typedef struct al_language_adapter {
    al_lang_t lang;
    char name[AL_MAX_NAME_LEN];
    oc_lang_ops_t ops;
    bool registered;
    uint32_t module_count;
    lpres_state_t adapter_attestation;
} al_language_adapter_t;

/* ===== Abstraction Layer ===== */

typedef struct abstraction_layer {
    /* Core modules */
    al_module_t modules[AL_MAX_MODULES];
    uint32_t num_modules;
    uint32_t next_module_id;
    
    /* Language adapters */
    al_language_adapter_t lang_adapters[AL_MAX_LANGUAGES];
    uint32_t num_lang_adapters;
    
    /* Global capabilities */
    al_capability_t global_capabilities[AL_MAX_CAPABILITIES];
    uint32_t num_global_capabilities;
    
    /* Orbital integration */
    orbital_fabric_t *orbital;
    oc_ir_t shared_ir;
    
    /* Financial integration */
    financial_fabric_t *financial;
    
    /* Identity integration */
    identity_fabric_t *identity;
    
    /* Porter House */
    porter_house_t *porter;
    
    /* Event-space */
    ev_sequencer_t *sequencer;
    ev_audit_t *audit;
    ev_healing_t *healing;
    
    /* Global statistics */
    struct {
        uint64_t total_modules_registered;
        uint64_t total_modules_initialized;
        uint64_t total_modules_failed;
        uint64_t total_invocations;
        uint64_t total_self_audits;
        uint64_t total_healings;
        uint64_t total_translations;
        uint64_t total_translation_failures;
        uint64_t total_capability_grants;
        uint64_t total_capability_revokes;
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
        bool auto_register_builtins;
        bool auto_heal;
        uint32_t audit_interval;
        uint32_t health_check_interval;
        surplus_real_t min_global_coverage;
        bool require_porter_house;
        bool require_zk_attestation;
    } config;
    
    bool initialized;
} abstraction_layer_t;

/* ===== API ===== */

/* Initialize the Abstraction Layer */
void al_init(abstraction_layer_t *layer,
             orbital_fabric_t *orbital,
             financial_fabric_t *financial,
             identity_fabric_t *identity,
             porter_house_t *porter,
             ev_sequencer_t *sequencer,
             ev_audit_t *audit,
             ev_healing_t *healing);

/* Register built-in language adapters */
void al_register_builtin_languages(abstraction_layer_t *layer);

/* Register a custom language adapter */
int32_t al_register_language(abstraction_layer_t *layer, al_lang_t lang, const oc_lang_ops_t *ops);

/* ===== Module Registration ===== */

int32_t al_register_module(abstraction_layer_t *layer,
                           const char *name, al_lang_t language,
                           const char *source_path, const char *entry_symbol,
                           const char **required_capabilities, uint32_t num_required,
                           const char **provided_capabilities, uint32_t num_provided,
                           const char **accepted_schemas, uint32_t num_accepted,
                           const char **emitted_schemas, uint32_t num_emitted);

al_module_t *al_get_module(abstraction_layer_t *layer, uint32_t module_id);
al_module_t *al_get_module_by_name(abstraction_layer_t *layer, const char *name);

/* Module initialization */
int32_t al_initialize_module(abstraction_layer_t *layer, uint32_t module_id);
int32_t al_initialize_all(abstraction_layer_t *layer);

/* Module lifecycle */
int32_t al_start_module(abstraction_layer_t *layer, uint32_t module_id);
int32_t al_stop_module(abstraction_layer_t *layer, uint32_t module_id);
int32_t al_restart_module(abstraction_layer_t *layer, uint32_t module_id);

/* ===== Schema Translation ===== */

int32_t al_translate(abstraction_layer_t *layer,
                     al_lang_t from_lang, const void *src, uint32_t len,
                     al_lang_t to_lang, void *out, uint32_t cap);

int32_t al_translate_through_ir(abstraction_layer_t *layer,
                                al_lang_t from_lang, const void *src, uint32_t len,
                                al_lang_t to_lang, void *out, uint32_t cap);

/* Orbital Elevator schema translation */
oe_translate_result_t al_oe_translate(abstraction_layer_t *layer,
                                       ev_envelope_t *env,
                                       const char *target_schema,
                                       uint16_t target_version);

/* ===== Capability Management ===== */

int32_t al_register_capability(abstraction_layer_t *layer,
                               const char *name, const char *description,
                               surplus_real_t min_coverage,
                               lpres_state_t min_attestation,
                               bool requires_hsm, bool requires_zk,
                               uint8_t capital_form, uint64_t min_balance,
                               uint32_t policy_id);

int32_t al_grant_capability(abstraction_layer_t *layer,
                            uint32_t module_id, uint32_t capability_id);

int32_t al_revoke_capability(abstraction_layer_t *layer,
                             uint32_t module_id, uint32_t capability_id);

/* ===== Self-Audit & Self-Heal ===== */

int32_t al_self_audit_module(abstraction_layer_t *layer, uint32_t module_id);
int32_t al_self_audit_system(abstraction_layer_t *layer);
int32_t al_self_heal_module(abstraction_layer_t *layer, uint32_t module_id);
int32_t al_self_heal_system(abstraction_layer_t *layer);

/* ===== Health & Attestation ===== */

int32_t al_check_module_health(abstraction_layer_t *layer,
                               uint32_t module_id,
                               void *health_out);

int32_t al_check_global_health(abstraction_layer_t *layer);

bool al_global_safety_gate(abstraction_layer_t *layer);

lpres_state_t al_attest(abstraction_layer_t *layer, uint32_t module_id,
                        uint32_t op_id, void *args, int32_t result);

/* Coverage enforcement */
void al_update_coverage(abstraction_layer_t *layer);
bool al_enforce_coverage(abstraction_layer_t *layer, surplus_real_t min_ratio);

/* Statistics */
void al_get_stats(abstraction_layer_t *layer, void *stats_out);

/* Paraconsistent state */
al_module_state_t al_get_module_state(abstraction_layer_t *layer, uint32_t module_id);
void al_set_module_state(abstraction_layer_t *layer, uint32_t module_id, al_module_state_t state);
lpres_state_t al_get_attestation(abstraction_layer_t *layer, uint32_t module_id);
void al_set_attestation(abstraction_layer_t *layer, uint32_t module_id, lpres_state_t state);

/* Utility */
const char *al_module_state_name(al_module_state_t state);
const char *al_lang_name(al_lang_t lang);
const char *al_lpres_state_name(lpres_state_t state);
const char *al_oe_result_name(oe_translate_result_t result);

#endif /* ABSTRACTION_LAYER_H */
