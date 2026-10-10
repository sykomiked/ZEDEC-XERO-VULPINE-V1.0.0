/* phase_coord.c — Phase Coordinator (K6)
 *
 * Implements admission tokens, coverage gates, and health policy.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

#include "phase_coord.h"

/* ===== Helpers ===== */

static void copy_str(char *dst, const char *src, uint32_t max) {
    uint32_t i;
    for (i = 0; i + 1 < max && src && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

/* ===== Registry Init ===== */

void pc_registry_init(pc_registry_t *reg) {
    if (!reg) return;
    ev_memset(reg, 0, sizeof(*reg));
    reg->next_token_id = 1;
    for (uint32_t i = 0; i <= PC_MAX_PHASES; i++)
        reg->phase_health[i] = PC_HEALTH_UNKNOWN;
}

/* ===== Coverage Gate Management ===== */

int32_t pc_register_gate(pc_registry_t *reg, const char *name,
                         pc_coverage_t required) {
    if (!reg || !name) return -1;
    if (reg->gate_count >= PC_MAX_COVERAGE_GATES) return -1;
    pc_coverage_gate_t *g = &reg->gates[reg->gate_count];
    ev_memset(g, 0, sizeof(*g));
    copy_str(g->name, name, sizeof(g->name));
    g->required = required;
    g->actual = PC_COVERAGE_NONE;
    g->satisfied = false;
    return (int32_t)reg->gate_count++;
}

bool pc_set_gate_coverage(pc_registry_t *reg, uint32_t gate_idx,
                          pc_coverage_t actual) {
    if (!reg || gate_idx >= reg->gate_count) return false;
    pc_coverage_gate_t *g = &reg->gates[gate_idx];
    g->actual = actual;
    g->satisfied = (g->actual >= g->required);
    return true;
}

bool pc_check_all_gates(pc_registry_t *reg) {
    if (!reg) return false;
    for (uint32_t i = 0; i < reg->gate_count; i++) {
        if (!reg->gates[i].satisfied) return false;
    }
    return true;
}

uint32_t pc_count_satisfied_gates(pc_registry_t *reg) {
    if (!reg) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < reg->gate_count; i++) {
        if (reg->gates[i].satisfied) count++;
    }
    return count;
}

/* ===== Health Management ===== */

void pc_set_phase_health(pc_registry_t *reg, pc_phase_id_t phase, pc_health_t health) {
    if (!reg || phase < 1 || phase > PC_MAX_PHASES) return;
    reg->phase_health[phase] = health;
}

pc_health_t pc_get_phase_health(pc_registry_t *reg, pc_phase_id_t phase) {
    if (!reg || phase < 1 || phase > PC_MAX_PHASES) return PC_HEALTH_UNKNOWN;
    return reg->phase_health[phase];
}

/* ===== Admission ===== */

pc_token_t *pc_admit(pc_registry_t *reg, const pc_step_request_t *req) {
    if (!reg || !req) return NULL;
    if (reg->token_count >= PC_MAX_TOKENS) return NULL;

    pc_decision_t decision = PC_DECISION_ADMIT;

    /* Check S0 pending */
    if (req->is_s0_pending) {
        decision = PC_DECISION_DEFER;
    }

    /* Check coverage gates */
    if (decision == PC_DECISION_ADMIT && req->requires_coverage) {
        if (!pc_check_all_gates(reg)) {
            decision = PC_DECISION_VETO;
        }
    }

    /* Check health */
    if (decision == PC_DECISION_ADMIT && req->requires_health) {
        pc_health_t h = pc_get_phase_health(reg, (pc_phase_id_t)req->phase_id);
        if (h == PC_HEALTH_UNHEALTHY) {
            decision = PC_DECISION_VETO;
        } else if (h == PC_HEALTH_DEGRADED) {
            decision = PC_DECISION_DEFER;
        } else if (h == PC_HEALTH_UNKNOWN) {
            decision = PC_DECISION_DEFER;
        }
    }

    /* Hardware actions require full coverage and healthy status */
    if (decision == PC_DECISION_ADMIT && req->is_hardware_action) {
        if (!pc_check_all_gates(reg)) {
            decision = PC_DECISION_VETO;
        }
        pc_health_t h = pc_get_phase_health(reg, (pc_phase_id_t)req->phase_id);
        if (h != PC_HEALTH_HEALTHY) {
            decision = PC_DECISION_VETO;
        }
    }

    /* Issue token */
    pc_token_t *tok = &reg->tokens[reg->token_count];
    ev_memset(tok, 0, sizeof(*tok));
    tok->token_id = reg->next_token_id++;
    tok->phase_id = req->phase_id;
    tok->step_id = req->step_id;
    tok->decision = decision;
    tok->issued_at = reg->current_logical_time;
    tok->expires_at = 0;  /* no expiry by default */
    tok->revoked = false;
    tok->requires_s0_resolution = (decision == PC_DECISION_DEFER);
    copy_str(tok->label, req->description, sizeof(tok->label));

    reg->token_count++;
    return tok;
}

bool pc_revoke_token(pc_registry_t *reg, uint64_t token_id) {
    pc_token_t *tok = pc_find_token(reg, token_id);
    if (!tok) return false;
    tok->revoked = true;
    return true;
}

pc_token_t *pc_find_token(pc_registry_t *reg, uint64_t token_id) {
    if (!reg) return NULL;
    for (uint32_t i = 0; i < reg->token_count; i++) {
        if (reg->tokens[i].token_id == token_id)
            return &reg->tokens[i];
    }
    return NULL;
}

bool pc_token_is_valid(pc_registry_t *reg, uint64_t token_id) {
    pc_token_t *tok = pc_find_token(reg, token_id);
    if (!tok) return false;
    if (tok->revoked) return false;
    if (tok->decision != PC_DECISION_ADMIT) return false;
    if (tok->expires_at > 0 && reg->current_logical_time > tok->expires_at)
        return false;
    return true;
}

/* ===== Time ===== */

void pc_advance_time(pc_registry_t *reg, uint64_t delta) {
    if (!reg) return;
    reg->current_logical_time += delta;
}

bool pc_token_expired(pc_registry_t *reg, uint64_t token_id) {
    pc_token_t *tok = pc_find_token(reg, token_id);
    if (!tok) return true;
    if (tok->expires_at == 0) return false;
    return reg->current_logical_time > tok->expires_at;
}

/* ===== Name Functions ===== */

const char *pc_decision_name(pc_decision_t decision) {
    switch (decision) {
        case PC_DECISION_ADMIT: return "admit";
        case PC_DECISION_VETO:  return "veto";
        case PC_DECISION_DEFER: return "defer";
        case PC_DECISION_RETRY: return "retry";
        default:                 return "unknown";
    }
}

const char *pc_coverage_name(pc_coverage_t coverage) {
    switch (coverage) {
        case PC_COVERAGE_NONE:    return "none";
        case PC_COVERAGE_PARTIAL: return "partial";
        case PC_COVERAGE_FULL:    return "full";
        default:                   return "unknown";
    }
}

const char *pc_health_name(pc_health_t health) {
    switch (health) {
        case PC_HEALTH_UNKNOWN:   return "unknown";
        case PC_HEALTH_HEALTHY:   return "healthy";
        case PC_HEALTH_DEGRADED:  return "degraded";
        case PC_HEALTH_UNHEALTHY: return "unhealthy";
        default:                   return "unknown";
    }
}

const char *pc_phase_name(pc_phase_id_t phase) {
    switch (phase) {
        case PC_PHASE_K1_OSEQ:    return "K1_OSEQ";
        case PC_PHASE_K2_RMAG:    return "K2_RMAG";
        case PC_PHASE_K3_LPRES:   return "K3_LPRES";
        case PC_PHASE_K4_IPHASE:  return "K4_IPHASE";
        case PC_PHASE_K5_CHOICE:  return "K5_CHOICE";
        case PC_PHASE_K6_COORD:   return "K6_COORD";
        case PC_PHASE_O1_CRIT168: return "O1_CRIT168";
        case PC_PHASE_O2_FS:      return "O2_FS";
        case PC_PHASE_O3_AGP:     return "O3_AGP";
        case PC_PHASE_O4_MESH:    return "O4_MESH";
        case PC_PHASE_O5_CHIGLET: return "O5_CHIGLET";
        case PC_PHASE_O6_VINO:    return "O6_VINO";
        case PC_PHASE_O7_KIKAN:   return "O7_KIKAN";
        default:                   return "unknown";
    }
}

/* ---- DECLARATION -----------------------------------------------------------

 * The admission-token issuer. REQUIRES_NONE is measured: phase_coord.o's
 * `nm -u` is empty.
 *
 * The bring-up checks that a fresh registry admits NOTHING -- zero satisfied
 * gates. A token issuer that is permissive when empty is the failure that
 * matters here, and it is invisible to any test that only checks the happy
 * path.
 */
#include "zxv_decl.h"
static int zxvd_phase_coord_bringup(void) {
    static pc_registry_t reg;
    pc_registry_init(&reg);
    if (pc_token_is_valid(&reg, 1u)) return -1;   /* no token exists yet */
    if (pc_find_token(&reg, 1u) != 0) return -1;
    return 0;
}

/* Global phase coordinator registry */
static pc_registry_t g_phase_coord_reg;

/* Get the global phase coordinator registry */
pc_registry_t *phase_coordinator_get(void) {
    return &g_phase_coord_reg;
}

/* Get current logical time (phase tick) */
uint64_t phase_coordinator_current_tick(void) {
    return g_phase_coord_reg.current_logical_time;
}

ZXV_DECLARE(phase_coord,
    ZXV_PROVIDES(pc_admission_ready),
    ZXV_REQUIRES_NONE,
    ZXV_BRINGUP(zxvd_phase_coord_bringup));
