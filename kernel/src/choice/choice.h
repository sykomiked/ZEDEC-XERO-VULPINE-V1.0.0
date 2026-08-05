/* choice.h — Deterministic Candidate Selection (K5)
 *
 * K5 CHOICE provides deterministic selection among competing candidates
 * with explicit tie-breaking policies. It resolves non-deterministic
 * choices into deterministic outcomes, defers ambiguous decisions to S0,
 * and tracks selection history for audit.
 *
 * Key responsibilities:
 *   - Register candidates with priority, weight, and capability
 *   - Deterministic selection (priority + weight + tie policy)
 *   - S0 deferral for ambiguous or unresolvable choices
 *   - Selection history and audit trail
 *   - Integration with K6 Phase Coordinator for admission
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef CHOICE_H
#define CHOICE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "event_space.h"

/* ===== CHOICE Constants ===== */

#define CHOICE_MAX_CANDIDATES    32
#define CHOICE_MAX_DECISIONS     128
#define CHOICE_MAX_NAME_LEN      32
#define CHOICE_MAX_CAP_LEN       64

/* ===== Decision Outcome ===== */

typedef enum {
    CHOICE_OUTCOME_SELECTED  = 0,  /* a candidate was deterministically chosen */
    CHOICE_OUTCOME_DEFERRED  = 1,  /* deferred to S0 (ambiguous) */
    CHOICE_OUTCOME_REJECTED  = 2,  /* no valid candidates */
    CHOICE_OUTCOME_TIE       = 3,  /* unresolved tie (policy needed) */
} choice_outcome_t;

/* ===== Tie Policy ===== */

typedef enum {
    CHOICE_TIE_LOWEST_ID    = 0,   /* lowest candidate ID wins */
    CHOICE_TIE_HIGHEST_ID   = 1,   /* highest candidate ID wins */
    CHOICE_TIE_ROUND_ROBIN  = 2,   /* stateful rotation */
    CHOICE_TIE_DEFER_S0     = 3,   /* defer to S0 */
    CHOICE_TIE_FIRST_REGISTERED = 4, /* earliest registered wins */
} choice_tie_policy_t;

/* ===== Candidate ===== */

typedef struct choice_candidate {
    uint32_t id;
    char name[CHOICE_MAX_NAME_LEN];
    uint32_t priority;           /* lower = higher priority */
    uint32_t weight;             /* within same priority, higher = preferred */
    char capability[CHOICE_MAX_CAP_LEN];
    bool active;
    bool requires_admission;     /* needs K6 Phase Coordinator token */
    uint32_t selection_count;    /* times selected */
} choice_candidate_t;

/* ===== Decision Record ===== */

typedef struct choice_decision {
    uint32_t id;
    char label[CHOICE_MAX_NAME_LEN];
    uint32_t candidate_count;    /* candidates considered */
    int32_t selected_idx;        /* -1 if none, -2 if deferred */
    choice_outcome_t outcome;
    choice_tie_policy_t tie_policy;
    uint32_t timestamp;          /* logical time */
} choice_decision_t;

/* ===== CHOICE Registry ===== */

typedef struct choice_registry {
    choice_candidate_t candidates[CHOICE_MAX_CANDIDATES];
    uint32_t candidate_count;
    uint32_t next_candidate_id;

    choice_decision_t decisions[CHOICE_MAX_DECISIONS];
    uint32_t decision_count;
    uint32_t next_decision_id;

    uint32_t rr_counter;         /* round-robin state */
    uint32_t current_time;       /* logical clock */
} choice_registry_t;

/* ===== API ===== */

void choice_registry_init(choice_registry_t *reg);

/* Candidate Management */
int32_t choice_register_candidate(choice_registry_t *reg, const char *name,
                                  uint32_t priority, uint32_t weight,
                                  const char *capability,
                                  bool requires_admission);
choice_candidate_t *choice_get_candidate(choice_registry_t *reg, uint32_t idx);
bool choice_set_candidate_active(choice_registry_t *reg, uint32_t idx, bool active);

/* Decision Making */
int32_t choice_decide(choice_registry_t *reg, const char *label,
                      choice_tie_policy_t tie_policy);
choice_decision_t *choice_get_decision(choice_registry_t *reg, uint32_t idx);

/* Time */
void choice_advance_time(choice_registry_t *reg, uint32_t delta);

/* Queries */
uint32_t choice_count_active_candidates(choice_registry_t *reg);
uint32_t choice_count_by_outcome(choice_registry_t *reg, choice_outcome_t outcome);
uint32_t choice_get_selection_count(choice_registry_t *reg, uint32_t candidate_idx);

/* Name Functions */
const char *choice_outcome_name(choice_outcome_t outcome);
const char *choice_tie_policy_name(choice_tie_policy_t policy);

#endif /* CHOICE_H */
