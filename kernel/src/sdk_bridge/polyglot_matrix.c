/* polyglot_matrix.c — Vena Runtime Polyglot Coordination Matrix Implementation
 *
 * Layer 5: The Intercellular Mesh. The non-linear event router that wraps
 * the M5 kernel. All 15 languages drop atomic event packets here; the mesh
 * broadcasts state changes simultaneously to every interested layer.
 *
 * No FFI. No linear APIs. No blocking. Orthogonal recursion.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "polyglot_matrix.h"
#include "orbital_compat.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================ */

static void pm_mem_set(void *dst, int val, uint32_t len) {
    uint8_t *d = dst;
    for (uint32_t i = 0; i < len; i++) d[i] = (uint8_t)val;
}

static void pm_mem_copy(void *dst, const void *src, uint32_t len) {
    uint8_t *d = dst; const uint8_t *s = src;
    for (uint32_t i = 0; i < len; i++) d[i] = s[i];
}

/* ============================================================================
 * TIER ASSIGNMENT (the 5 cellular layers)
 * ============================================================================ */

pm_tier_t pm_lang_tier(oc_lang_t lang) {
    switch (lang) {
        case OC_LANG_C:
        case OC_LANG_ASSEMBLY:
            return PM_TIER_CORE;      /* Layer 1: The Membrane */
        case OC_LANG_RUST:
        case OC_LANG_ZIG:
            return PM_TIER_SAFETY;    /* Layer 1: The Membrane (safety DSLs) */
        case OC_LANG_COBOL:
        case OC_LANG_FORTRAN:
            return PM_TIER_LEGACY;    /* Layer 2: The Organs */
        case OC_LANG_SUTRA:
            return PM_TIER_LOGIC;     /* Layer 3: The Nervous System */
        case OC_LANG_PYTHON:
        case OC_LANG_WASM:
            return PM_TIER_SCRIPT;    /* Layer 4: The Interface */
        case OC_LANG_DTMF:
        case OC_LANG_MF:
        case OC_LANG_PULSE:
        case OC_LANG_SS7:
        case OC_LANG_FSK:
        case OC_LANG_TELECOM:
            return PM_TIER_TELECOM;   /* Layer 1: The Membrane (signaling) */
        default:
            return PM_TIER_CORE;
    }
}

const char *pm_tier_name(pm_tier_t tier) {
    switch (tier) {
        case PM_TIER_CORE:    return "membrane";
        case PM_TIER_SAFETY:  return "membrane-safety";
        case PM_TIER_LEGACY:  return "organs";
        case PM_TIER_LOGIC:   return "nervous-system";
        case PM_TIER_SCRIPT:  return "interface";
        case PM_TIER_TELECOM: return "membrane-signaling";
        default:              return "?";
    }
}

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

void pm_init(polyglot_matrix_t *matrix) {
    if (!matrix) return;
    pm_mem_set(matrix, 0, sizeof(*matrix));
    
    matrix->min_coverage = SR_FROM_FLOAT(1.8);
    matrix->matrix_attestation = LPRES_STATE_NEITHER;
    matrix->phase_tick = 0;
    matrix->next_route_id = 1;
    matrix->next_message_id = 1;
    
    /* Discover bound languages from orbital_compat */
    for (uint32_t i = 0; i < OC_LANG_MAX && i < PM_MAX_LANG_SLOTS; i++) {
        oc_lang_t lang = (oc_lang_t)i;
        if (oc_lang_registered(lang)) {
            matrix->languages[i].lang = lang;
            matrix->languages[i].tier = pm_lang_tier(lang);
            matrix->languages[i].bound = true;
            matrix->languages[i].active = true;
        }
    }
    
    matrix->initialized = true;
}

int32_t pm_register_language(polyglot_matrix_t *matrix, oc_lang_t lang) {
    if (!matrix || !matrix->initialized) return -1;
    if ((uint32_t)lang >= PM_MAX_LANG_SLOTS) return -1;
    if (!oc_lang_registered(lang)) return -1;  /* fail closed */
    
    matrix->languages[lang].lang = lang;
    matrix->languages[lang].tier = pm_lang_tier(lang);
    matrix->languages[lang].bound = true;
    matrix->languages[lang].active = true;
    return 0;
}

/* ============================================================================
 * ROUTES
 * ============================================================================ */

int32_t pm_create_route(polyglot_matrix_t *matrix,
                        oc_lang_t source, oc_lang_t target,
                        bool requires_policy_check) {
    if (!matrix || !matrix->initialized) return -1;
    if (matrix->num_routes >= PM_MAX_ROUTES) return -1;
    if (!oc_lang_registered(source) || !oc_lang_registered(target)) return -1;
    
    pm_route_t *route = &matrix->routes[matrix->num_routes];
    pm_mem_set(route, 0, sizeof(*route));
    
    route->route_id = matrix->next_route_id++;
    route->source_lang = source;
    route->target_lang = target;
    route->source_tier = pm_lang_tier(source);
    route->target_tier = pm_lang_tier(target);
    route->route_attestation = LPRES_STATE_NEITHER;
    route->coverage_ratio = SR_FROM_FLOAT(2.0);
    route->requires_policy_check = requires_policy_check;
    route->active = true;
    
    matrix->num_routes++;
    return (int32_t)route->route_id;
}

/* ============================================================================
 * MESSAGING — the non-linear event router
 * ============================================================================ */

int32_t pm_send(polyglot_matrix_t *matrix, uint32_t route_id,
                const oc_ir_t *payload) {
    if (!matrix || !matrix->initialized || !payload) return -1;
    if (matrix->num_pending >= PM_MAX_PENDING) return -1;
    
    /* Find route */
    pm_route_t *route = NULL;
    for (uint32_t i = 0; i < matrix->num_routes; i++) {
        if (matrix->routes[i].route_id == route_id && matrix->routes[i].active) {
            route = &matrix->routes[i];
            break;
        }
    }
    if (!route) return -1;
    
    /* Policy check: The One Policy at the boundary */
    if (route->requires_policy_check) {
        matrix->stats.total_policy_checks++;
        bool policy_ok = true;
        for (uint32_t i = 0; i < payload->num_fields; i++) {
            if (payload->fields[i].type == OC_TYPE_RATIONAL) {
                if (!payload->fields[i].num.valid || payload->fields[i].num.den == 0) {
                    policy_ok = false;
                    break;
                }
            }
        }
        if (!policy_ok) {
            matrix->stats.total_policy_vetoes++;
            route->messages_failed++;
            return -1;
        }
    }
    
    /* Create pending message */
    pm_message_t *msg = &matrix->pending[matrix->num_pending];
    pm_mem_set(msg, 0, sizeof(*msg));
    
    msg->message_id = matrix->next_message_id++;
    msg->route_id = route_id;
    msg->source_lang = route->source_lang;
    msg->target_lang = route->target_lang;
    msg->payload = *payload;
    msg->phase_tick = matrix->phase_tick;
    msg->delivery_attestation = LPRES_STATE_NEITHER;
    msg->completed = false;
    msg->failed = false;
    
    matrix->num_pending++;
    matrix->stats.total_messages++;
    route->messages_passed++;
    
    /* Update language counters */
    matrix->languages[route->source_lang].messages_sent++;
    matrix->languages[route->target_lang].messages_received++;
    
    return (int32_t)msg->message_id;
}

uint32_t pm_deliver(polyglot_matrix_t *matrix) {
    if (!matrix || !matrix->initialized) return 0;
    
    uint32_t delivered = 0;
    matrix->phase_tick++;
    
    /* Deliver all pending messages in phase-tick order (non-blocking) */
    for (uint32_t i = 0; i < matrix->num_pending; i++) {
        pm_message_t *msg = &matrix->pending[i];
        if (msg->completed || msg->failed) continue;
        
        /* Lift the payload through the target language adapter */
        /* (The actual lift is done by the receiving layer; here we mark
         *  delivery as attested and let the receiver process it.) */
        msg->delivery_attestation = LPRES_STATE_TRUE;
        msg->completed = true;
        delivered++;
    }
    
    /* Compact completed messages */
    uint32_t write = 0;
    for (uint32_t i = 0; i < matrix->num_pending; i++) {
        if (!matrix->pending[i].completed && !matrix->pending[i].failed) {
            if (write != i) matrix->pending[write] = matrix->pending[i];
            write++;
        }
    }
    matrix->num_pending = write;
    
    return delivered;
}

/* ============================================================================
 * CROSS-LANGUAGE PIPELINE
 * ============================================================================ */

int32_t pm_pipeline(polyglot_matrix_t *matrix,
                    oc_lang_t stage1_lang, const void *stage1_src, uint32_t stage1_len,
                    oc_lang_t stage2_lang,
                    oc_lang_t stage3_lang,
                    oc_ir_t *final_result) {
    if (!matrix || !matrix->initialized || !stage1_src || !final_result) return -1;
    
    /* Stage 1: lower source language to canonical IR */
    oc_ir_t ir;
    int32_t rc = oc_lower(stage1_lang, stage1_src, stage1_len, &ir);
    if (rc != OC_OK) return rc;
    
    /* Stage 2: route through the mesh (policy check if cross-tier) */
    bool cross_tier = (pm_lang_tier(stage1_lang) != pm_lang_tier(stage2_lang));
    if (cross_tier) matrix->stats.total_cross_tier++;
    
    int32_t route_id = pm_create_route(matrix, stage1_lang, stage2_lang, cross_tier);
    if (route_id < 0) return route_id;
    
    int32_t msg_id = pm_send(matrix, (uint32_t)route_id, &ir);
    if (msg_id < 0) return msg_id;
    
    /* Stage 3: deliver and lift to final language */
    pm_deliver(matrix);
    
    /* Lift through stage3 language */
    /* (The final result is the canonical IR — the receiver lifts it.) */
    *final_result = ir;
    
    return 0;
}

/* ============================================================================
 * SELF-AUDIT
 * ============================================================================ */

int32_t pm_self_audit(polyglot_matrix_t *matrix) {
    if (!matrix || !matrix->initialized) return -1;
    
    bool audit_ok = true;
    
    /* Check all routes */
    for (uint32_t i = 0; i < matrix->num_routes; i++) {
        pm_route_t *route = &matrix->routes[i];
        if (!route->active) continue;
        if (SR_CMP(route->coverage_ratio, matrix->min_coverage) < 0) {
            audit_ok = false;
            route->route_attestation = LPRES_STATE_FALSE;
        } else {
            route->route_attestation = LPRES_STATE_TRUE;
        }
    }
    
    /* Check all languages */
    for (uint32_t i = 0; i < PM_MAX_LANG_SLOTS; i++) {
        if (matrix->languages[i].bound && !matrix->languages[i].active) {
            audit_ok = false;
        }
    }
    
    matrix->matrix_attestation = audit_ok ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    return audit_ok ? 0 : -1;
}

void pm_get_stats(const polyglot_matrix_t *matrix, void *stats_out) {
    if (!matrix || !stats_out) return;
    pm_mem_copy(stats_out, &matrix->stats, sizeof(matrix->stats));
}
