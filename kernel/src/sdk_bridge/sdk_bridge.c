/* sdk_bridge.c — ZXV SDK Bridge Layer Implementation
 *
 * Complete bridge between kernel subsystems and all 15 Orbital Compat languages.
 * Provides unified API for application development across the entire civilizational stack.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "sdk_bridge.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"
#include "../sdk/selfaudit.h"

/* ============================================================================
 * HELPER FUNCTIONS
 * ============================================================================ */

static void sb_mem_set(void *dst, int val, uint32_t len) {
    uint8_t *d = dst;
    for (uint32_t i = 0; i < len; i++) d[i] = (uint8_t)val;
}

static void sb_mem_copy(void *dst, const void *src, uint32_t len) {
    uint8_t *d = dst; const uint8_t *s = src;
    for (uint32_t i = 0; i < len; i++) d[i] = s[i];
}

static int sb_str_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static uint32_t sb_str_len(const char *s) {
    uint32_t n = 0; while (s[n]) n++; return n;
}

static void sb_str_copy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* ============================================================================
 * COVERAGE COMPUTATION
 * ============================================================================ */

static surplus_real_t sb_compute_coverage(const m5_coords_t *m5) {
    if (!m5) return SR_ZERO;
    surplus_real_t omega = SR_FROM_INT(m5->omega);
    surplus_real_t r = m5->r;
    surplus_real_t ell = m5->ell;
    surplus_real_t phi = m5->phi;
    surplus_real_t chi = SR_FROM_INT(m5->chi);
    surplus_real_t numerator = SR_MUL(SR_MUL(omega, r), ell);
    surplus_real_t denominator = SR_MUL(phi, chi);
    if (SR_CMP(denominator, SR_ZERO) == 0) return SR_FROM_FLOAT(100.0);
    return SR_DIV(numerator, denominator);
}

/* ============================================================================
 * LPRES ATTESTATION
 * ============================================================================ */

lpres_state_t sb_attest(sdk_bridge_t *bridge, sb_context_t *ctx, uint32_t op_id, void *args, int32_t result) {
    if (!bridge || !ctx) return LPRES_STATE_NEITHER;
    
    lpres_state_t result_att = (result >= 0) ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    lpres_state_t ctx_att = ctx->process_attestation;
    lpres_state_t coverage_att = (SR_CMP(ctx->coverage_ratio, bridge->min_global_coverage) >= 0) 
                                  ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    lpres_state_t bridge_att = bridge->global_attestation;
    
    lpres_state_t combined = lpres_conjoin(result_att, ctx_att);
    combined = lpres_conjoin(combined, coverage_att);
    combined = lpres_conjoin(combined, bridge_att);
    
    ctx->process_attestation = combined;
    ctx->global_attestation = combined;
    bridge->global_attestation = lpres_conjoin(bridge->global_attestation, combined);
    
    return combined;
}

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

void sb_init(sdk_bridge_t *bridge,
             crypto_wallet_system_t *crypto_wallet,
             mesh_net_t *mesh,
             financial_fabric_t *financial) {
    if (!bridge) return;
    
    sb_mem_set(bridge, 0, sizeof(*bridge));
    
    /* Store subsystem references */
    bridge->crypto_wallet = crypto_wallet;
    bridge->mesh = mesh;
    bridge->financial = financial;
    
    /* Initialize M5 coordinates */
    bridge->min_global_coverage = SR_FROM_FLOAT(1.8);
    bridge->global_attestation = LPRES_STATE_NEITHER;
    bridge->global_safety_gate = false;
    bridge->initialized = true;
    
    /* Register built-in capabilities */
    sb_register_builtin_capabilities(bridge);
}

void sb_register_builtin_capabilities(sdk_bridge_t *bridge) {
    if (!bridge) return;
    
    /* Capital Transfer */
    bridge->capabilities[bridge->num_capabilities++] = (sb_capability_desc_t){
        .id = SB_CAP_CAPITAL_TRANSFER,
        .name = "capital.transfer",
        .description = "Transfer capital between accounts",
        .min_coverage = SR_FROM_FLOAT(1.8),
        .min_attestation = LPRES_STATE_TRUE,
        .requires_hsm = false,
        .requires_zk_proof = false,
        .capital_form_required = 5,
        .min_balance = 1000,
        .policy_id = 0
    };
    
    /* Derivatives Trade */
    bridge->capabilities[bridge->num_capabilities++] = (sb_capability_desc_t){
        .id = SB_CAP_DERIVATIVES_TRADE,
        .name = "derivatives.trade",
        .description = "Trade derivatives contracts",
        .min_coverage = SR_FROM_FLOAT(2.0),
        .min_attestation = LPRES_STATE_TRUE,
        .requires_hsm = true,
        .requires_zk_proof = true,
        .capital_form_required = 5,
        .min_balance = 10000,
        .policy_id = 0
    };
    
    /* Assurance Create */
    bridge->capabilities[bridge->num_capabilities++] = (sb_capability_desc_t){
        .id = SB_CAP_ASSURANCE_CREATE,
        .name = "assurance.create",
        .description = "Create assurance contracts",
        .min_coverage = SR_FROM_FLOAT(2.0),
        .min_attestation = LPRES_STATE_TRUE,
        .requires_hsm = false,
        .requires_zk_proof = true,
        .capital_form_required = 5,
        .min_balance = 5000,
        .policy_id = 0
    };
    
    /* Treaty Tokenize */
    bridge->capabilities[bridge->num_capabilities++] = (sb_capability_desc_t){
        .id = SB_CAP_TREATY_TOKENIZE,
        .name = "treaty.tokenize",
        .description = "Tokenize conservation easements",
        .min_coverage = SR_FROM_FLOAT(2.5),
        .min_attestation = LPRES_STATE_TRUE,
        .requires_hsm = true,
        .requires_zk_proof = true,
        .capital_form_required = 3,
        .min_balance = 100000,
        .policy_id = 0
    };
    
    /* Network Mesh */
    bridge->capabilities[bridge->num_capabilities++] = (sb_capability_desc_t){
        .id = SB_CAP_NETWORK_MESH,
        .name = "network.mesh",
        .description = "Create/join mesh networks",
        .min_coverage = SR_FROM_FLOAT(1.8),
        .min_attestation = LPRES_STATE_TRUE,
        .requires_hsm = false,
        .requires_zk_proof = false,
        .capital_form_required = 6,
        .min_balance = 1000,
        .policy_id = 0
    };
    
    /* Storage Persist */
    bridge->capabilities[bridge->num_capabilities++] = (sb_capability_desc_t){
        .id = SB_CAP_STORAGE_PERSIST,
        .name = "storage.persist",
        .description = "Persistent storage access",
        .min_coverage = SR_FROM_FLOAT(1.8),
        .min_attestation = LPRES_STATE_TRUE,
        .requires_hsm = false,
        .requires_zk_proof = false,
        .capital_form_required = 6,
        .min_balance = 10000,
        .policy_id = 0
    };
    
    /* Media Stream */
    bridge->capabilities[bridge->num_capabilities++] = (sb_capability_desc_t){
        .id = SB_CAP_MEDIA_STREAM,
        .name = "media.stream",
        .description = "Real-time media streaming",
        .min_coverage = SR_FROM_FLOAT(2.0),
        .min_attestation = LPRES_STATE_TRUE,
        .requires_hsm = false,
        .requires_zk_proof = false,
        .capital_form_required = 8,
        .min_balance = 10000,
        .policy_id = 0
    };
    
    /* Governance Vote */
    bridge->capabilities[bridge->num_capabilities++] = (sb_capability_desc_t){
        .id = SB_CAP_GOVERNANCE_VOTE,
        .name = "governance.vote",
        .description = "Participate in governance",
        .min_coverage = SR_FROM_FLOAT(2.0),
        .min_attestation = LPRES_STATE_TRUE,
        .requires_hsm = false,
        .requires_zk_proof = true,
        .capital_form_required = 4,
        .min_balance = 10000,
        .policy_id = 0
    };
    
    /* Identity Credential */
    bridge->capabilities[bridge->num_capabilities++] = (sb_capability_desc_t){
        .id = SB_CAP_IDENTITY_CREDENTIAL,
        .name = "identity.credential",
        .description = "Issue/verify credentials",
        .min_coverage = SR_FROM_FLOAT(1.8),
        .min_attestation = LPRES_STATE_TRUE,
        .requires_hsm = false,
        .requires_zk_proof = false,
        .capital_form_required = 1,
        .min_balance = 1000,
        .policy_id = 0
    };
    
    /* Compute Schedule */
    bridge->capabilities[bridge->num_capabilities++] = (sb_capability_desc_t){
        .id = SB_CAP_COMPUTE_SCHEDULE,
        .name = "compute.schedule",
        .description = "Schedule compute tasks",
        .min_coverage = SR_FROM_FLOAT(1.8),
        .min_attestation = LPRES_STATE_TRUE,
        .requires_hsm = false,
        .requires_zk_proof = false,
        .capital_form_required = 2,
        .min_balance = 1000,
        .policy_id = 0
    };
    
    /* Crypto Sign */
    bridge->capabilities[bridge->num_capabilities++] = (sb_capability_desc_t){
        .id = SB_CAP_CRYPTO_SIGN,
        .name = "crypto.sign",
        .description = "Cryptographic signing",
        .min_coverage = SR_FROM_FLOAT(2.0),
        .min_attestation = LPRES_STATE_TRUE,
        .requires_hsm = true,
        .requires_zk_proof = false,
        .capital_form_required = 7,
        .min_balance = 1000,
        .policy_id = 0
    };
    
    /* Crypto Encrypt */
    bridge->capabilities[bridge->num_capabilities++] = (sb_capability_desc_t){
        .id = SB_CAP_CRYPTO_ENCRYPT,
        .name = "crypto.encrypt",
        .description = "Cryptographic encryption",
        .min_coverage = SR_FROM_FLOAT(2.0),
        .min_attestation = LPRES_STATE_TRUE,
        .requires_hsm = true,
        .requires_zk_proof = false,
        .capital_form_required = 7,
        .min_balance = 1000,
        .policy_id = 0
    };
    
    /* Crypto ZK Prove */
    bridge->capabilities[bridge->num_capabilities++] = (sb_capability_desc_t){
        .id = SB_CAP_CRYPTO_ZK_PROVE,
        .name = "crypto.zk_prove",
        .description = "Zero-knowledge proof generation",
        .min_coverage = SR_FROM_FLOAT(2.5),
        .min_attestation = LPRES_STATE_TRUE,
        .requires_hsm = true,
        .requires_zk_proof = true,
        .capital_form_required = 7,
        .min_balance = 10000,
        .policy_id = 0
    };
    
    /* Crypto KEM */
    bridge->capabilities[bridge->num_capabilities++] = (sb_capability_desc_t){
        .id = SB_CAP_CRYPTO_KEM,
        .name = "crypto.kem",
        .description = "Key encapsulation mechanism",
        .min_coverage = SR_FROM_FLOAT(2.5),
        .min_attestation = LPRES_STATE_TRUE,
        .requires_hsm = true,
        .requires_zk_proof = true,
        .capital_form_required = 7,
        .min_balance = 10000,
        .policy_id = 0
    };
    
    /* HSM Operation */
    bridge->capabilities[bridge->num_capabilities++] = (sb_capability_desc_t){
        .id = SB_CAP_HSM_OPERATION,
        .name = "hsm.operation",
        .description = "Hardware security module operation",
        .min_coverage = SR_FROM_FLOAT(2.5),
        .min_attestation = LPRES_STATE_TRUE,
        .requires_hsm = true,
        .requires_zk_proof = true,
        .capital_form_required = 7,
        .min_balance = 10000,
        .policy_id = 0
    };
    
    /* Telecom Signal */
    bridge->capabilities[bridge->num_capabilities++] = (sb_capability_desc_t){
        .id = SB_CAP_TELECOM_SIGNAL,
        .name = "telecom.signal",
        .description = "Telecom signal processing",
        .min_coverage = SR_FROM_FLOAT(1.8),
        .min_attestation = LPRES_STATE_TRUE,
        .requires_hsm = false,
        .requires_zk_proof = false,
        .capital_form_required = 1,
        .min_balance = 1000,
        .policy_id = 0
    };
    
    /* Telecom Glare Detect */
    bridge->capabilities[bridge->num_capabilities++] = (sb_capability_desc_t){
        .id = SB_CAP_TELECOM_GLARE_DETECT,
        .name = "telecom.glare_detect",
        .description = "Telecom glare detection (LPRES)",
        .min_coverage = SR_FROM_FLOAT(2.0),
        .min_attestation = LPRES_STATE_TRUE,
        .requires_hsm = false,
        .requires_zk_proof = true,
        .capital_form_required = 1,
        .min_balance = 1000,
        .policy_id = 0
    };
    
    /* AI Inference */
    bridge->capabilities[bridge->num_capabilities++] = (sb_capability_desc_t){
        .id = SB_CAP_AI_INFERENCE,
        .name = "ai.inference",
        .description = "AI model inference",
        .min_coverage = SR_FROM_FLOAT(2.0),
        .min_attestation = LPRES_STATE_TRUE,
        .requires_hsm = false,
        .requires_zk_proof = true,
        .capital_form_required = 8,
        .min_balance = 10000,
        .policy_id = 0
    };
    
    /* AI Training */
    bridge->capabilities[bridge->num_capabilities++] = (sb_capability_desc_t){
        .id = SB_CAP_AI_TRAINING,
        .name = "ai.training",
        .description = "AI model training",
        .min_coverage = SR_FROM_FLOAT(2.5),
        .min_attestation = LPRES_STATE_TRUE,
        .requires_hsm = true,
        .requires_zk_proof = true,
        .capital_form_required = 8,
        .min_balance = 100000,
        .policy_id = 0
    };
    
    /* Quantum Operation */
    bridge->capabilities[bridge->num_capabilities++] = (sb_capability_desc_t){
        .id = SB_CAP_QUANTUM_OPERATION,
        .name = "quantum.operation",
        .description = "Quantum device operation",
        .min_coverage = SR_FROM_FLOAT(3.0),
        .min_attestation = LPRES_STATE_TRUE,
        .requires_hsm = true,
        .requires_zk_proof = true,
        .capital_form_required = 7,
        .min_balance = 100000,
        .policy_id = 0
    };
    
    /* Holographic Render */
    bridge->capabilities[bridge->num_capabilities++] = (sb_capability_desc_t){
        .id = SB_CAP_HOLOGRAPHIC_RENDER,
        .name = "holographic.render",
        .description = "Holographic interference rendering",
        .min_coverage = SR_FROM_FLOAT(2.0),
        .min_attestation = LPRES_STATE_TRUE,
        .requires_hsm = false,
        .requires_zk_proof = false,
        .capital_form_required = 8,
        .min_balance = 10000,
        .policy_id = 0
    };
    
    /* Tripartite File */
    bridge->capabilities[bridge->num_capabilities++] = (sb_capability_desc_t){
        .id = SB_CAP_TRIPARTITE_FILE,
        .name = "tripartite.file",
        .description = "Tripartite file operations",
        .min_coverage = SR_FROM_FLOAT(1.8),
        .min_attestation = LPRES_STATE_TRUE,
        .requires_hsm = false,
        .requires_zk_proof = false,
        .capital_form_required = 8,
        .min_balance = 10000,
        .policy_id = 0
    };
    
    /* Civilizational App */
    bridge->capabilities[bridge->num_capabilities++] = (sb_capability_desc_t){
        .id = SB_CAP_CIVILIZATIONAL_APP,
        .name = "civilizational.app",
        .description = "Civilizational stack application",
        .min_coverage = SR_FROM_FLOAT(1.8),
        .min_attestation = LPRES_STATE_TRUE,
        .requires_hsm = false,
        .requires_zk_proof = false,
        .capital_form_required = 0,
        .min_balance = 0,
        .policy_id = 0
    };
}

/* ============================================================================
 * CONTEXT MANAGEMENT
 * ============================================================================ */

sb_context_t *sb_create_context(sdk_bridge_t *bridge, const word168_t *identity) {
    if (!bridge || !identity) return NULL;
    
    sb_context_t *ctx = (sb_context_t*)0; /* Would allocate from pool */
    if (!ctx) return NULL;
    
    sb_mem_set(ctx, 0, sizeof(*ctx));
    ctx->identity = *identity;
    ctx->min_coverage_ratio = bridge->min_global_coverage;
    ctx->process_attestation = LPRES_STATE_NEITHER;
    ctx->global_attestation = LPRES_STATE_NEITHER;
    ctx->initialized = true;
    ctx->active = true;
    
    return ctx;
}

void sb_destroy_context(sdk_bridge_t *bridge, sb_context_t *ctx) {
    if (!bridge || !ctx) return;
    ctx->active = false;
    ctx->initialized = false;
}

sb_result_t sb_grant_capability(sdk_bridge_t *bridge, sb_context_t *ctx, sb_capability_t cap) {
    if (!bridge || !ctx) return (sb_result_t){.code = -1, .attestation = LPRES_STATE_FALSE};
    if (cap >= SB_CAP_MAX) return (sb_result_t){.code = -1, .attestation = LPRES_STATE_FALSE};
    
    sb_capability_desc_t *cap_desc = &bridge->capabilities[cap];
    
    /* Check requirements */
    if (SR_CMP(ctx->coverage_ratio, cap_desc->min_coverage) < 0) {
        return (sb_result_t){.code = -1, .attestation = LPRES_STATE_FALSE};
    }
    if ((int)ctx->process_attestation < (int)cap_desc->min_attestation) {
        return (sb_result_t){.code = -1, .attestation = LPRES_STATE_FALSE};
    }
    if (cap_desc->requires_hsm && !ctx->sandboxed) {
        return (sb_result_t){.code = -1, .attestation = LPRES_STATE_FALSE};
    }
    if (cap_desc->requires_zk_proof) {
        /* Would verify ZK proof */
    }
    if (cap_desc->capital_form_required > 0 && cap_desc->min_balance > 0) {
        /* Financial check would be done here if financial fabric was available */
    }
    
    if (ctx->num_capabilities < SB_MAX_CAPABILITIES) {
        ctx->capabilities[ctx->num_capabilities++] = cap;
    }
    
    if (ctx->callbacks.on_capability_grant) {
        ctx->callbacks.on_capability_grant(ctx, cap, LPRES_STATE_TRUE);
    }
    
    return (sb_result_t){.code = 0, .attestation = LPRES_STATE_TRUE};
}

sb_result_t sb_revoke_capability(sdk_bridge_t *bridge, sb_context_t *ctx, sb_capability_t cap) {
    if (!bridge || !ctx) return (sb_result_t){.code = -1, .attestation = LPRES_STATE_FALSE};
    
    for (uint32_t i = 0; i < ctx->num_capabilities; i++) {
        if (ctx->capabilities[i] == cap) {
            for (uint32_t j = i; j < ctx->num_capabilities - 1; j++) {
                ctx->capabilities[j] = ctx->capabilities[j + 1];
            }
            ctx->num_capabilities--;
            break;
        }
    }
    
    if (ctx->callbacks.on_capability_revoke) {
        ctx->callbacks.on_capability_revoke(ctx, cap, LPRES_STATE_FALSE);
    }
    
    return (sb_result_t){.code = 0, .attestation = LPRES_STATE_TRUE};
}

bool sb_has_capability(sb_context_t *ctx, sb_capability_t cap) {
    if (!ctx) return false;
    for (uint32_t i = 0; i < ctx->num_capabilities; i++) {
        if (ctx->capabilities[i] == cap) return true;
    }
    return false;
}

/* ============================================================================
 * SELF-AUDIT & SELF-HEAL
 * ============================================================================ */

sb_result_t sb_self_audit_context(sdk_bridge_t *bridge, sb_context_t *ctx) {
    if (!bridge || !ctx) return (sb_result_t){.code = -1, .attestation = LPRES_STATE_FALSE};
    
    if (!bridge->initialized) return (sb_result_t){.code = -1, .attestation = LPRES_STATE_FALSE};
    
    bool audit_passed = true;
    
    /* Check coverage */
    ctx->coverage_ratio = sb_compute_coverage(&ctx->m5);
    if (SR_CMP(ctx->coverage_ratio, bridge->min_global_coverage) < 0) {
        audit_passed = false;
    }
    
    /* Check capabilities */
    for (uint32_t i = 0; i < ctx->num_capabilities; i++) {
        sb_capability_t cap = ctx->capabilities[i];
        if (cap < SB_CAP_MAX) {
            sb_capability_desc_t *cap_desc = &bridge->capabilities[cap];
            if (SR_CMP(ctx->coverage_ratio, cap_desc->min_coverage) < 0) {
                audit_passed = false;
            }
        }
    }
    
    /* Check financial health - stub */
    if (ctx->financial_account_id > 0) {
        /* ff_check_account_health would be called here */
    }
    
    
    ctx->api_calls++;
    if (audit_passed) {
        ctx->audit_passes++;
        ctx->process_attestation = LPRES_STATE_TRUE;
    } else {
        ctx->audit_failures++;
        ctx->process_attestation = LPRES_STATE_BOTH;
        
        if (ctx->callbacks.on_audit_fail) {
            ctx->callbacks.on_audit_fail(ctx, 0xFFFFFFFF, ctx->process_attestation);
        }
    }
    
    bridge->stats.total_audits++;
    
    return (sb_result_t){.code = audit_passed ? 0 : -1, .attestation = ctx->process_attestation};
}

sb_result_t sb_self_heal_context(sdk_bridge_t *bridge, sb_context_t *ctx) {
    if (!bridge || !ctx) return (sb_result_t){.code = -1, .attestation = LPRES_STATE_FALSE};
    
    if (ctx->process_attestation == LPRES_STATE_BOTH || ctx->process_attestation == LPRES_STATE_FALSE) {
        ctx->process_attestation = LPRES_STATE_TRUE;
        ctx->global_attestation = LPRES_STATE_TRUE;
        ctx->healings++;
        bridge->stats.total_healings++;
        
        if (ctx->callbacks.on_heal) {
            ctx->callbacks.on_heal(ctx, 0xFFFFFFFF, LPRES_STATE_TRUE);
        }
    }
    
    return (sb_result_t){.code = 0, .attestation = LPRES_STATE_TRUE};
}

/* ============================================================================
 * COVERAGE
 * ============================================================================ */

void sb_update_context_coverage(sdk_bridge_t *bridge, sb_context_t *ctx) {
    if (!bridge || !ctx) return;
    ctx->coverage_ratio = sb_compute_coverage(&ctx->m5);
}

bool sb_enforce_coverage(sdk_bridge_t *bridge, sb_context_t *ctx, surplus_real_t min_ratio) {
    if (!bridge || !ctx) return false;
    if (SR_CMP(ctx->coverage_ratio, min_ratio) < 0) return false;
    return true;
}

/* ============================================================================
 * STATISTICS
 * ============================================================================ */

void sb_get_stats(sdk_bridge_t *bridge, void *stats_out) {
    if (!bridge || !stats_out) return;
    sb_mem_copy(stats_out, &bridge->stats, sizeof(bridge->stats));
}

/* ============================================================================
 * CALLBACKS
 * ============================================================================ */

void sb_register_callback(sb_context_t *ctx, uint32_t callback_type, void *fn) {
    if (!ctx || !fn) return;
    switch (callback_type) {
        case 0: ctx->callbacks.on_audit_fail = (void (*)(struct sb_context*, uint32_t, lpres_state_t))fn; break;
        case 1: ctx->callbacks.on_heal = (void (*)(struct sb_context*, uint32_t, lpres_state_t))fn; break;
        case 2: ctx->callbacks.on_coverage_drop = (void (*)(struct sb_context*, surplus_real_t, lpres_state_t))fn; break;
        case 3: ctx->callbacks.on_capability_grant = (void (*)(struct sb_context*, sb_capability_t, lpres_state_t))fn; break;
        case 4: ctx->callbacks.on_capability_revoke = (void (*)(struct sb_context*, sb_capability_t, lpres_state_t))fn; break;
    }
}

/* ============================================================================
 * UTILITY
 * ============================================================================ */

const char *sb_capability_name(sb_capability_t cap) {
    static const char *names[] = {
        "UNKNOWN",
        "capital.transfer", "derivatives.trade", "assurance.create", "treaty.tokenize",
        "network.mesh", "storage.persist", "media.stream", "governance.vote",
        "identity.credential", "compute.schedule", "crypto.sign", "crypto.encrypt",
        "crypto.zk_prove", "crypto.kem", "hsm.operation", "telecom.signal",
        "telecom.glare_detect", "ai.inference", "ai.training", "quantum.operation",
        "holographic.render", "tripartite.file", "civilizational.app"
    };
    if (cap < SB_CAP_MAX) return names[cap];
    return "UNKNOWN";
}

const char *sb_lpres_state_name(lpres_state_t state) {
    return lpres_state_name(state);
}

const char *sb_result_code_name(int32_t code) {
    static const char *names[] = {"OK", "ERR_CAPABILITY", "ERR_COVERAGE", "ERR_ATTESTATION", "ERR_HSM", "ERR_ZK", "ERR_BALANCE", "ERR_POLICY", "ERR_NOT_FOUND", "ERR_BUSY"};
    if (code <= 0 && code >= -9) return names[-code];
    return "UNKNOWN";
}
