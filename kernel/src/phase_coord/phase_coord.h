/* phase_coord.h — Phase Coordinator (K6)
 *
 * K6 PHASE_COORD admits, vetoes, or defers coupled steps and hardware actions
 * under coverage and health policy. It issues admission tokens, checks coverage
 * gates, and coordinates transitions across the 13-phase pipeline.
 *
 * Referenced by:
 *   - Tri-Space Programming Spec: K6 admits/vetoes/defers coupled steps
 *   - RCE Spec: K6 admits/rejects/defers coupled steps and hardware actions
 *   - Root Computing Spec: K6 admits/vetoes/defers compilation and execution
 *   - UBH Spec: K6 admits/veto/defer translation and execution
 *   - Master Roadmap: kernel event scheduler and lifecycle
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef PHASE_COORD_H
#define PHASE_COORD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "event_space.h"

/* ===== Phase IDs ===== */

typedef enum {
    PC_PHASE_K1_OSEQ     = 1,
    PC_PHASE_K2_RMAG     = 2,
    PC_PHASE_K3_LPRES    = 3,
    PC_PHASE_K4_IPHASE   = 4,
    PC_PHASE_K5_CHOICE   = 5,
    PC_PHASE_K6_COORD    = 6,
    PC_PHASE_O1_CRIT168  = 7,
    PC_PHASE_O2_FS       = 8,
    PC_PHASE_O3_AGP      = 9,
    PC_PHASE_O4_MESH     = 10,
    PC_PHASE_O5_CHIGLET  = 11,
    PC_PHASE_O6_VINO     = 12,
    PC_PHASE_O7_KIKAN    = 13,
} pc_phase_id_t;

/* ===== Admission Decision ===== */

typedef enum {
    PC_DECISION_ADMIT    = 0,  /* step is admitted, token issued */
    PC_DECISION_VETO     = 1,  /* step is rejected, cannot proceed */
    PC_DECISION_DEFER    = 2,  /* step is deferred to S0, pending resolution */
    PC_DECISION_RETRY    = 3,  /* step may retry after condition change */
} pc_decision_t;

/* ===== Coverage Status ===== */

typedef enum {
    PC_COVERAGE_NONE     = 0,  /* no coverage evidence */
    PC_COVERAGE_PARTIAL  = 1,  /* some coverage but gaps remain */
    PC_COVERAGE_FULL     = 2,  /* all required coverage gates satisfied */
} pc_coverage_t;

/* ===== Health Status ===== */

typedef enum {
    PC_HEALTH_UNKNOWN    = 0,
    PC_HEALTH_HEALTHY    = 1,
    PC_HEALTH_DEGRADED   = 2,
    PC_HEALTH_UNHEALTHY  = 3,
} pc_health_t;

/* ===== Admission Token ===== */

typedef struct pc_token {
    uint64_t token_id;          /* unique token ID */
    uint32_t phase_id;          /* phase that issued the token */
    uint32_t step_id;           /* step being admitted */
    pc_decision_t decision;     /* admission decision */
    uint64_t issued_at;         /* logical timestamp */
    uint64_t expires_at;        /* logical expiry (0 = no expiry) */
    bool revoked;
    bool requires_s0_resolution;
    char label[32];             /* human-readable label */
} pc_token_t;

/* ===== Coverage Gate ===== */

typedef struct pc_coverage_gate {
    char name[32];
    pc_coverage_t required;     /* required coverage level */
    pc_coverage_t actual;       /* current coverage level */
    bool satisfied;
} pc_coverage_gate_t;

/* ===== Step Request ===== */

typedef struct pc_step_request {
    uint32_t phase_id;          /* requesting phase */
    uint32_t step_id;           /* step identifier */
    char description[64];       /* step description */
    bool requires_coverage;     /* if true, coverage gates must be satisfied */
    bool requires_health;       /* if true, health must be >= healthy */
    bool is_hardware_action;    /* if true, extra safety checks apply */
    bool is_s0_pending;         /* if true, S0 resolution required before admit */
} pc_step_request_t;

/* ===== Phase Coordinator Registry ===== */

#define PC_MAX_TOKENS        256
#define PC_MAX_COVERAGE_GATES 32
#define PC_MAX_PHASES        13

typedef struct pc_registry {
    pc_token_t tokens[PC_MAX_TOKENS];
    uint32_t token_count;
    uint64_t next_token_id;

    pc_coverage_gate_t gates[PC_MAX_COVERAGE_GATES];
    uint32_t gate_count;

    pc_health_t phase_health[PC_MAX_PHASES + 1];  /* indexed by phase_id (1-13) */

    uint64_t current_logical_time;
} pc_registry_t;

/* ===== API ===== */

void pc_registry_init(pc_registry_t *reg);

/* Coverage Gate Management */
int32_t pc_register_gate(pc_registry_t *reg, const char *name,
                         pc_coverage_t required);
bool pc_set_gate_coverage(pc_registry_t *reg, uint32_t gate_idx,
                          pc_coverage_t actual);
bool pc_check_all_gates(pc_registry_t *reg);
uint32_t pc_count_satisfied_gates(pc_registry_t *reg);

/* Health Management */
void pc_set_phase_health(pc_registry_t *reg, pc_phase_id_t phase, pc_health_t health);
pc_health_t pc_get_phase_health(pc_registry_t *reg, pc_phase_id_t phase);

/* Admission */
pc_token_t *pc_admit(pc_registry_t *reg, const pc_step_request_t *req);
bool pc_revoke_token(pc_registry_t *reg, uint64_t token_id);
pc_token_t *pc_find_token(pc_registry_t *reg, uint64_t token_id);
bool pc_token_is_valid(pc_registry_t *reg, uint64_t token_id);

/* Time */
void pc_advance_time(pc_registry_t *reg, uint64_t delta);
bool pc_token_expired(pc_registry_t *reg, uint64_t token_id);

/* Name Functions */
const char *pc_decision_name(pc_decision_t decision);
const char *pc_coverage_name(pc_coverage_t coverage);
const char *pc_health_name(pc_health_t health);
const char *pc_phase_name(pc_phase_id_t phase);

/* Get the global phase coordinator registry */
pc_registry_t *phase_coordinator_get(void);

/* Get current logical time (phase tick) */
uint64_t phase_coordinator_current_tick(void);

#endif /* PHASE_COORD_H */
