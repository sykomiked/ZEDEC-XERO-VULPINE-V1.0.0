/* polyglot_matrix.h — Vena Runtime Polyglot Coordination Matrix
 *
 * The unified event-space interop layer. All 15 Orbital Compat languages
 * communicate through exact rational primitives passed asynchronously
 * through the kernel's event-space sequencer — no FFI, no pointer sharing,
 * no floating-point drift across the boundary layer.
 *
 * Language Tiers (per the System Functional Architecture):
 *   Tier 0 — Core Systems & Deterministic Execution:
 *     C (OC_LANG_C), Assembly (OC_LANG_ASSEMBLY)
 *   Tier 1 — Systems Safety:
 *     Rust (OC_LANG_RUST), Zig (OC_LANG_ZIG)
 *   Tier 2 — Legacy & Numerical Simulation:
 *     COBOL (OC_LANG_COBOL), Fortran (OC_LANG_FORTRAN)
 *   Tier 3 — Logic, Policy & Constraint:
 *     Sutra (OC_LANG_SUTRA)
 *   Tier 4 — Interface, Scripting & Spatial:
 *     Python (OC_LANG_PYTHON), WebAssembly (OC_LANG_WASM)
 *   Tier 5 — Telecom Signaling:
 *     DTMF, MF, Pulse, SS7, FSK, Telecom
 *
 * A Fortran resource simulation updates a ledger in COBOL, which triggers
 * a policy validation check in Sutra, which renders on the holographic
 * desktop — all through exact rationals in the event space.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef POLYGLOT_MATRIX_H
#define POLYGLOT_MATRIX_H

#include <stdint.h>
#include <stdbool.h>
#include "orbital_compat.h"
#include "sdk_bridge_lang.h"
#include "event_space.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"

/* ============================================================================
 * CONSTANTS
 * ============================================================================ */

#define PM_MAX_ROUTES        64    /* cross-language routes */
#define PM_MAX_PENDING       128   /* in-flight event-space messages */
#define PM_MAX_LANG_SLOTS    15    /* one per OC language */

/* ============================================================================
 * LANGUAGE TIERS
 * ============================================================================ */

typedef enum {
    PM_TIER_CORE      = 0,   /* C, Assembly — bare metal */
    PM_TIER_SAFETY    = 1,   /* Rust, Zig — memory-safe systems */
    PM_TIER_LEGACY    = 2,   /* COBOL, Fortran — mainframe/scientific */
    PM_TIER_LOGIC     = 3,   /* Sutra — paraconsistent policy */
    PM_TIER_SCRIPT    = 4,   /* Python, WASM — scripting/sandbox */
    PM_TIER_TELECOM   = 5,   /* DTMF/MF/Pulse/SS7/FSK/Telecom */
} pm_tier_t;

/* ============================================================================
 * ROUTE — a directed edge between two language domains
 * ============================================================================ */

typedef struct {
    uint32_t route_id;
    oc_lang_t source_lang;
    oc_lang_t target_lang;
    pm_tier_t source_tier;
    pm_tier_t target_tier;
    
    /* Route health */
    lpres_state_t route_attestation;
    surplus_real_t coverage_ratio;
    uint32_t messages_passed;
    uint32_t messages_failed;
    
    /* Policy gate: The One Policy verdict required for pass-through */
    bool requires_policy_check;
    bool active;
} pm_route_t;

/* ============================================================================
 * PENDING MESSAGE — an in-flight event-space exchange
 * ============================================================================ */

typedef struct {
    uint32_t message_id;
    uint32_t route_id;
    oc_lang_t source_lang;
    oc_lang_t target_lang;
    
    /* The payload: canonical IR (exact rationals only) */
    oc_ir_t payload;
    
    /* Event-space sequencing */
    uint64_t phase_tick;
    lpres_state_t delivery_attestation;
    
    /* Result (filled on completion) */
    oc_ir_t result;
    bool completed;
    bool failed;
} pm_message_t;

/* ============================================================================
 * POLYGLOT MATRIX
 * ============================================================================ */

typedef struct {
    /* Language slots */
    struct {
        oc_lang_t lang;
        pm_tier_t tier;
        bool bound;              /* adapter registered in orbital_compat */
        bool active;
        uint32_t messages_sent;
        uint32_t messages_received;
    } languages[PM_MAX_LANG_SLOTS];
    
    /* Routes */
    pm_route_t routes[PM_MAX_ROUTES];
    uint32_t num_routes;
    
    /* Pending messages */
    pm_message_t pending[PM_MAX_PENDING];
    uint32_t num_pending;
    
    /* Global state */
    lpres_state_t matrix_attestation;
    surplus_real_t min_coverage;
    uint64_t phase_tick;
    uint32_t next_route_id;
    uint32_t next_message_id;
    
    /* Statistics */
    struct {
        uint64_t total_messages;
        uint64_t total_cross_tier;
        uint64_t total_policy_checks;
        uint64_t total_policy_vetoes;
        uint64_t total_failures;
    } stats;
    
    bool initialized;
} polyglot_matrix_t;

/* ============================================================================
 * API
 * ============================================================================ */

/* Initialize the matrix. Discovers which languages are bound in
 * orbital_compat and assigns tiers. */
void pm_init(polyglot_matrix_t *matrix);

/* Register a language slot (explicit binding). */
int32_t pm_register_language(polyglot_matrix_t *matrix, oc_lang_t lang);

/* Create a route between two languages. Returns route_id or -1. */
int32_t pm_create_route(polyglot_matrix_t *matrix,
                        oc_lang_t source, oc_lang_t target,
                        bool requires_policy_check);

/* Send a message through a route. The payload is lowered to the canonical
 * IR (exact rationals) and sequenced through the event space.
 * Returns message_id or -1. */
int32_t pm_send(polyglot_matrix_t *matrix, uint32_t route_id,
                const oc_ir_t *payload);

/* Deliver pending messages (advance the event-space sequencer).
 * Returns the number of messages delivered. */
uint32_t pm_deliver(polyglot_matrix_t *matrix);

/* Execute a cross-language pipeline:
 *   Fortran sim -> COBOL ledger -> Sutra policy -> result
 * Each stage lowers/lifts through the canonical IR. */
int32_t pm_pipeline(polyglot_matrix_t *matrix,
                    oc_lang_t stage1_lang, const void *stage1_src, uint32_t stage1_len,
                    oc_lang_t stage2_lang,
                    oc_lang_t stage3_lang,
                    oc_ir_t *final_result);

/* Get tier for a language. */
pm_tier_t pm_lang_tier(oc_lang_t lang);

/* Get language name for diagnostics. */
const char *pm_tier_name(pm_tier_t tier);

/* Self-audit: verify all routes have valid coverage and attestation. */
int32_t pm_self_audit(polyglot_matrix_t *matrix);

/* Get statistics. */
void pm_get_stats(const polyglot_matrix_t *matrix, void *stats_out);

#endif /* POLYGLOT_MATRIX_H */
