/* civilizational_stack.h — ZXV Total Civilizational Stack
 *
 * The complete application layer built on the paraconsistent kernel,
 * spanning all 15 Orbital Compat languages. Each application leverages
 * LPRES four-valued logic, M5 coverage enforcement, and exact rational
 * arithmetic for military-grade reliability.
 *
 * Application Categories:
 *   1. Financial Sovereignty — Banking, Derivatives, Assurance, Treaty
 *   2. Identity & Governance — Sovereign ID, Voting, Credentials, Law
 *   3. Network & Mesh — P2P Trade Routes, Settlement, Federation
 *   4. Storage & Persistence — Content-Addressable, Replicated, Encrypted
 *   5. Security & Crypto — Post-Quantum, ZK Proofs, HSM, TLS
 *   6. Media & Compute — Streaming, Generative, Games, AI Inference
 *   6. Legacy Mainframe — COBOL Banking, Fortran Scientific
 *   7. Modern Systems — Rust, Zig, Python, WASM
 *   8. Telecom — DTMF, MF, Pulse, SS7, FSK
 *   9. Blockchain — Native Smart Contract Execution
 *
 * Design Principles:
 * - Every application is a paraconsistent process (LPRES states)
 * - M5 coverage ≥ 1.8 enforced at application level
 * - Self-audit/self-heal built into every app lifecycle
 * - Economic settlement via Financial Fabric (Vino vouchers)
 * - Identity-gated via Identity Fabric (168-bit critical words)
 * - Schema translation via Orbital Elevator (canonical IR)
 * - Hardware abstraction via Yantra + Smart Adapter
 * - Native blockchain bytecode execution (EVM, WASM, custom)
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef CIVILIZATIONAL_STACK_H
#define CIVILIZATIONAL_STACK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "orbital_compat.h"
#include "financial_fabric.h"
#include "identity_fabric.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"
#include "selfaudit.h"
#include "event_space.h"

/* ===== Constants ===== */

#define CS_MAX_APPS              512
#define CS_MAX_LANGUAGES         15
#define CS_MAX_NAME_LEN          64
#define CS_MAX_SCHEMA_LEN        64

/* ===== Application Languages (mirrors Orbital Compat) ===== */

typedef enum {
    CS_LANG_COBOL       = 0,   /* Legacy banking/mainframe */
    CS_LANG_FORTRAN     = 1,   /* Scientific/engineering */
    CS_LANG_C           = 2,   /* Kernel-native systems */
    CS_LANG_SUTRA       = 3,   /* AI-native exact rational + LPRES */
    CS_LANG_ASSEMBLY    = 4,   /* Bare-metal/hardware */
    CS_LANG_RUST        = 5,   /* Memory-safe systems */
    CS_LANG_ZIG         = 6,   /* Comptime metaprogramming */
    CS_LANG_PYTHON      = 7,   /* Scripting/glue/AI */
    CS_LANG_WASM        = 8,   /* Portable sandboxed modules */
    CS_LANG_DTMF        = 9,   /* Telecom signaling */
    CS_LANG_MF          = 10,  /* Multi-frequency telecom */
    CS_LANG_PULSE       = 11,  /* Rotary pulse dialing */
    CS_LANG_SS7         = 12,  /* SS7/MTP/ISUP/TCAP */
    CS_LANG_FSK         = 13,  /* FSK modem signaling */
    CS_LANG_TELECOM     = 14,  /* Unified telecom dispatcher */
    CS_LANG_MAX
} cs_lang_t;

/* ===== Application Categories ===== */

typedef enum {
    CS_CAT_FINANCIAL    = 1,   /* Banking, derivatives, assurance, treaty */
    CS_CAT_IDENTITY     = 2,   /* Sovereign ID, credentials, voting */
    CS_CAT_GOVERNANCE   = 3,   /* Law, policy, consensus, arbitration */
    CS_CAT_NETWORK      = 4,   /* Mesh, trade routes, federation */
    CS_CAT_STORAGE      = 5,   /* Content-addressable, replicated */
    CS_CAT_SECURITY     = 6,   /* Crypto, ZK, HSM, TLS */
    CS_CAT_MEDIA        = 7,   /* Streaming, generative, games */
    CS_CAT_COMPUTE      = 8,   /* Scheduling, heterogeneous compute */
    CS_CAT_LEGACY       = 9,   /* COBOL banking, Fortran scientific */
    CS_CAT_MODERN       = 10,  /* Rust, Zig, Python, WASM */
    CS_CAT_TELECOM      = 11,  /* DTMF, MF, Pulse, SS7, FSK */
    CS_CAT_BLOCKCHAIN   = 12,  /* Native smart contract execution */
    CS_CAT_AI           = 13,  /* Inference, training, remote compute */
    CS_CAT_MAX
} cs_category_t;

/* ===== Civilizational Application ===== */

typedef struct cs_application {
    uint32_t id;
    char name[CS_MAX_NAME_LEN];
    cs_lang_t language;
    cs_category_t category;
    
    /* Orbital Compat integration */
    oc_lang_t oc_lang;
    oc_ir_t canonical_ir;
    bool ir_valid;
    
    /* Paraconsistent state */
    lpres_state_t app_state;        /* TRUE/FALSE/BOTH/NEITHER */
    lpres_state_t global_attestation;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    surplus_real_t min_coverage_ratio;
    
    /* Capabilities */
    char required_capabilities[32][CS_MAX_NAME_LEN];
    uint32_t num_required_capabilities;
    char provided_capabilities[32][CS_MAX_NAME_LEN];
    uint32_t num_provided_capabilities;
    
    /* Schemas */
    char accepted_schemas[16][CS_MAX_SCHEMA_LEN];
    uint32_t num_accepted_schemas;
    char emitted_schemas[16][CS_MAX_SCHEMA_LEN];
    uint32_t num_emitted_schemas;
    
    /* Economic */
    uint32_t financial_account_id;
    uint8_t pricing_form;
    uint64_t price_per_invocation;
    
    /* Identity */
    uint32_t creator_identity_id;
    uint32_t credential_id;
    
    /* Security */
    uint32_t tls_session_id;
    uint32_t keypair_id;
    bool sandboxed;
    bool requires_hsm;
    bool requires_zk_proof;
    
    /* Event-space */
    uint32_t event_domain_id;
    
    /* Statistics */
    struct {
        uint64_t invocations;
        uint64_t failures;
        uint64_t self_audits;
        uint64_t healings;
        uint64_t translations;
        uint64_t translation_failures;
    } stats;
    
    /* Lifecycle */
    bool initialized;
    bool active;
    bool builtin;
} cs_application_t;

/* ===== Civilizational Stack ===== */

typedef struct civilizational_stack {
    /* Core fabrics (available) */
    financial_fabric_t *financial;
    identity_fabric_t *identity;
    orbital_fabric_t *orbital;  /* forward declared */
    
    /* Applications */
    cs_application_t apps[CS_MAX_APPS];
    uint32_t num_apps;
    uint32_t next_app_id;
    
    /* Language runtime state */
    struct {
        cs_lang_t lang;
        bool runtime_initialized;
        uint32_t app_count;
        lpres_state_t runtime_attestation;
    } lang_runtimes[CS_LANG_MAX];
    
    /* Global statistics */
    struct {
        uint64_t total_apps_deployed;
        uint64_t total_invocations;
        uint64_t total_self_audits;
        uint64_t total_healings;
        uint64_t total_translations;
        uint64_t total_translation_failures;
        uint64_t total_economic_volume;
        uint64_t total_settlements;
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
        bool auto_deploy_builtin;
        bool require_self_audit;
        uint32_t audit_interval;
        bool auto_heal;
        surplus_real_t min_global_coverage;
    } config;
    
    bool initialized;
} civilizational_stack_t;

/* ===== API ===== */

/* Initialize the Civilizational Stack */
void cs_init(civilizational_stack_t *stack,
             orbital_fabric_t *orbital,
             financial_fabric_t *financial,
             identity_fabric_t *identity);

/* Deploy all built-in civilizational applications */
void cs_deploy_builtin_apps(civilizational_stack_t *stack);

/* ===== Application Deployment ===== */

int32_t cs_deploy_app(civilizational_stack_t *stack,
                      const char *name, cs_lang_t language,
                      cs_category_t category,
                      const char **required_capabilities, uint32_t num_required,
                      const char **provided_capabilities, uint32_t num_provided,
                      const char **accepted_schemas, uint32_t num_accepted,
                      const char **emitted_schemas, uint32_t num_emitted);

cs_application_t *cs_get_app(civilizational_stack_t *stack, uint32_t app_id);
cs_application_t *cs_get_app_by_name(civilizational_stack_t *stack, const char *name);

/* ===== Language Runtime ===== */

int32_t cs_init_language_runtime(civilizational_stack_t *stack, cs_lang_t lang);
int32_t cs_translate_app(civilizational_stack_t *stack,
                         uint32_t app_id,
                         cs_lang_t target_lang);

/* ===== Self-Audit & Self-Heal ===== */

int32_t cs_self_audit_app(civilizational_stack_t *stack, uint32_t app_id);
int32_t cs_self_audit_stack(civilizational_stack_t *stack);
int32_t cs_self_heal_app(civilizational_stack_t *stack, uint32_t app_id);

/* ===== Health & Attestation ===== */

int32_t cs_check_app_health(civilizational_stack_t *stack,
                            uint32_t app_id,
                            void *health_out);

int32_t cs_check_global_health(civilizational_stack_t *stack);

bool cs_global_safety_gate(civilizational_stack_t *stack);

lpres_state_t cs_attest(civilizational_stack_t *stack, uint32_t app_id,
                        uint32_t op_id, void *args, int32_t result);

/* Coverage enforcement */
void cs_update_coverage(civilizational_stack_t *stack);
bool cs_enforce_coverage(civilizational_stack_t *stack, surplus_real_t min_ratio);

/* Statistics */
void cs_get_stats(civilizational_stack_t *stack, void *stats_out);

/* Paraconsistent state */
lpres_state_t cs_get_app_state(civilizational_stack_t *stack, uint32_t app_id);
void cs_set_app_state(civilizational_stack_t *stack, uint32_t app_id, lpres_state_t state);

/* Utility */
const char *cs_lang_name(cs_lang_t lang);
const char *cs_category_name(cs_category_t cat);
const char *cs_lpres_state_name(lpres_state_t state);

#endif /* CIVILIZATIONAL_STACK_H */
