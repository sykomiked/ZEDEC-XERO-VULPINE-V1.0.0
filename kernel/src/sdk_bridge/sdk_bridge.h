/* sdk_bridge.h — ZXV SDK Bridge Layer
 *
 * Complete bridge between kernel subsystems and all 15 Orbital Compat languages.
 * Provides unified API for application development across the entire civilizational stack.
 *
 * Bridge Architecture:
 *   Layer 0: Kernel Syscalls (syscall.h)
 *   Layer 1: Subsystem C API (this bridge)
 *   Layer 2: Language Bindings (Orbital Compat → 15 languages)
 *   Layer 3: Application Framework (App Fabric)
 *
 * Design Principles:
 * - Every kernel subsystem exposed via capability-gated C API
 * - All operations paraconsistent (LPRES four-valued logic)
 * - M5 coverage ≥ 1.8 enforced at bridge layer
 * - Self-audit/self-heal hooks on every API call
 * - Economic settlement via Financial Fabric
 * - Identity-gated via Identity Fabric (168-bit critical words)
 * - Schema translation via Orbital Elevator
 * - Content-addressable via Tripartite FS
 *
 * Supported Languages (via Orbital Compat):
 *   1. COBOL (OC_LANG_COBOL) — Legacy banking/mainframe
 *   2. Fortran (OC_LANG_FORTRAN) — Scientific/engineering
 *   3. C (OC_LANG_C) — Kernel-native systems
 *   4. Sutra (OC_LANG_SUTRA) — AI-native exact rational + LPRES
 *   5. Assembly (OC_LANG_ASSEMBLY) — Bare-metal/hardware
 *   6. Rust (OC_LANG_RUST) — Memory-safe systems
 *   7. Zig (OC_LANG_ZIG) — Comptime metaprogramming
 *   8. Python (OC_LANG_PYTHON) — Scripting/glue/AI
 *   9. WebAssembly (OC_LANG_WASM) — Portable sandboxed modules
 *   10. DTMF (OC_LANG_DTMF) — Telecom signaling
 *   11. MF (OC_LANG_MF) — Multi-frequency telecom
 *   12. Pulse (OC_LANG_PULSE) — Rotary pulse dialing
 *   13. SS7 (OC_LANG_SS7) — SS7/MTP/ISUP/TCAP
 *   14. FSK (OC_LANG_FSK) — Frequency-shift keying
 *   15. Telecom (OC_LANG_TELECOM) — Unified telecom dispatcher
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef SDK_BRIDGE_H
#define SDK_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "orbital_compat.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"
#include "../sdk/selfaudit.h"
#include "event_space.h"
#include "crypto_wallet.h"
#include "mesh_net.h"
#include "financial_fabric.h"

/* ============================================================================
 * CONSTANTS
 * ============================================================================ */

#define SB_MAX_CAPABILITIES      64
#define SB_MAX_NAME_LEN          64
#define SB_MAX_SCHEMA_LEN        64
#define SB_MAX_CALLBACKS         32

/* ============================================================================
 * BRIDGE CAPABILITIES (mirrors App Fabric)
 * ============================================================================ */

typedef enum {
    SB_CAP_CAPITAL_TRANSFER      = 1,
    SB_CAP_DERIVATIVES_TRADE     = 2,
    SB_CAP_ASSURANCE_CREATE      = 3,
    SB_CAP_TREATY_TOKENIZE       = 4,
    SB_CAP_NETWORK_MESH          = 5,
    SB_CAP_STORAGE_PERSIST       = 6,
    SB_CAP_MEDIA_STREAM          = 7,
    SB_CAP_GOVERNANCE_VOTE       = 8,
    SB_CAP_IDENTITY_CREDENTIAL   = 9,
    SB_CAP_COMPUTE_SCHEDULE      = 10,
    SB_CAP_CRYPTO_SIGN           = 11,
    SB_CAP_CRYPTO_ENCRYPT        = 12,
    SB_CAP_CRYPTO_ZK_PROVE       = 13,
    SB_CAP_CRYPTO_KEM            = 14,
    SB_CAP_HSM_OPERATION         = 15,
    SB_CAP_TELECOM_SIGNAL        = 16,
    SB_CAP_TELECOM_GLARE_DETECT  = 17,
    SB_CAP_AI_INFERENCE          = 17,
    SB_CAP_AI_TRAINING           = 18,
    SB_CAP_QUANTUM_OPERATION     = 18,
    SB_CAP_HOLOGRAPHIC_RENDER    = 19,
    SB_CAP_TRIPARTITE_FILE       = 20,
    SB_CAP_CIVILIZATIONAL_APP    = 21,
    SB_CAP_MAX
} sb_capability_t;

/* ============================================================================
 * BRIDGE CONTEXT (per-process/thread)
 * ============================================================================ */

typedef struct sb_context {
    uint32_t process_id;
    uint32_t thread_id;
    word168_t identity;              /* 168-bit critical word */
    
    /* Capabilities granted */
    sb_capability_t capabilities[SB_MAX_CAPABILITIES];
    uint32_t num_capabilities;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    surplus_real_t min_coverage_ratio;
    
    /* Paraconsistent state */
    lpres_state_t process_attestation;
    lpres_state_t global_attestation;
    
    /* Financial */
    uint32_t financial_account_id;
    
/* Callbacks */
    struct {
        void (*on_audit_fail)(struct sb_context *ctx, uint32_t op_id, lpres_state_t state);
        void (*on_heal)(struct sb_context *ctx, uint32_t op_id, lpres_state_t state);
        void (*on_coverage_drop)(struct sb_context *ctx, surplus_real_t ratio, lpres_state_t state);
        void (*on_capability_grant)(struct sb_context *ctx, sb_capability_t cap, lpres_state_t state);
        void (*on_capability_revoke)(struct sb_context *ctx, sb_capability_t cap, lpres_state_t state);
    } callbacks;
    
    /* Statistics */
    uint64_t api_calls;
    uint64_t audit_passes;
    uint64_t audit_failures;
    uint64_t healings;
    
    bool initialized;
    bool active;
    bool sandboxed;
} sb_context_t;

/* ============================================================================
 * BRIDGE RESULT (LPRES-integrated)
 * ============================================================================ */

typedef struct sb_result {
    int32_t code;                    /* 0 = success, negative = error */
    lpres_state_t attestation;       /* LPRES attestation of operation */
    surplus_real_t coverage_ratio;   /* M5 coverage at call time */
    uint64_t timestamp;              /* Phase tick timestamp */
    uint32_t op_id;                  /* Operation ID for tracing */
} sb_result_t;

/* ============================================================================
 * CAPABILITY MANAGEMENT
 * ============================================================================ */

typedef struct sb_capability_desc {
    sb_capability_t id;
    char name[SB_MAX_NAME_LEN];
    char description[SB_MAX_NAME_LEN];
    surplus_real_t min_coverage;
    lpres_state_t min_attestation;
    bool requires_hsm;
    bool requires_zk_proof;
    uint8_t capital_form_required;
    uint64_t min_balance;
    uint32_t policy_id;
} sb_capability_desc_t;

/* ============================================================================
 * MASTER BRIDGE CONTEXT
 * ============================================================================ */

typedef struct sdk_bridge {
    /* Core context */
    sb_context_t *current_context;
    
    /* Subsystem references */
    crypto_wallet_system_t *crypto_wallet;
    mesh_net_t *mesh;
    financial_fabric_t *financial;
    
    /* Capability registry */
    sb_capability_desc_t capabilities[SB_MAX_CAPABILITIES];
    uint32_t num_capabilities;
    
    /* Global state */
    lpres_state_t global_attestation;
    bool global_safety_gate;
    surplus_real_t min_global_coverage;
    
    /* Statistics */
    struct {
        uint64_t total_api_calls;
        uint64_t total_audits;
        uint64_t total_healings;
        uint64_t total_translations;
        uint64_t total_capability_grants;
        uint64_t total_capability_revokes;
    } stats;
    
    bool initialized;
} sdk_bridge_t;

/* ============================================================================
 * API
 * ============================================================================ */

/* Initialize the SDK Bridge */
void sb_init(sdk_bridge_t *bridge,
             crypto_wallet_system_t *crypto_wallet,
             mesh_net_t *mesh,
             financial_fabric_t *financial);

/* Register all built-in capabilities */
void sb_register_builtin_capabilities(sdk_bridge_t *bridge);

/* Create a new process context */
sb_context_t *sb_create_context(sdk_bridge_t *bridge, const word168_t *identity);

/* Destroy a process context */
void sb_destroy_context(sdk_bridge_t *bridge, sb_context_t *ctx);

/* Grant a capability to a context */
sb_result_t sb_grant_capability(sdk_bridge_t *bridge, sb_context_t *ctx, sb_capability_t cap);

/* Revoke a capability from a context */
sb_result_t sb_revoke_capability(sdk_bridge_t *bridge, sb_context_t *ctx, sb_capability_t cap);

/* Check if context has capability */
bool sb_has_capability(sb_context_t *ctx, sb_capability_t cap);

/* Run self-audit on context */
sb_result_t sb_self_audit_context(sdk_bridge_t *bridge, sb_context_t *ctx);

/* Run self-heal on context */
sb_result_t sb_self_heal_context(sdk_bridge_t *bridge, sb_context_t *ctx);

/* Update coverage for context */
void sb_update_context_coverage(sdk_bridge_t *bridge, sb_context_t *ctx);

/* Enforce minimum coverage */
bool sb_enforce_coverage(sdk_bridge_t *bridge, sb_context_t *ctx, surplus_real_t min_ratio);

/* Attest an operation */
lpres_state_t sb_attest(sdk_bridge_t *bridge, sb_context_t *ctx, uint32_t op_id, void *args, int32_t result);

/* Get bridge statistics */
void sb_get_stats(sdk_bridge_t *bridge, void *stats_out);

/* Register a custom callback */
void sb_register_callback(sb_context_t *ctx, uint32_t callback_type, void *fn);

/* Utility */
const char *sb_capability_name(sb_capability_t cap);
const char *sb_lpres_state_name(lpres_state_t state);
const char *sb_result_code_name(int32_t code);

#endif /* SDK_BRIDGE_H */
