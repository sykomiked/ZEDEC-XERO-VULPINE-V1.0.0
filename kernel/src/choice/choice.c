/* choice.c — Deterministic Candidate Selection (K5)
 *
 * Implements candidate registration, deterministic selection with
 * tie-breaking, S0 deferral, and decision audit trail.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */

#include "choice.h"

/* ===== Helpers ===== */

static void copy_str(char *dst, const char *src, uint32_t max) {
    uint32_t i;
    for (i = 0; i + 1 < max && src && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

/* ===== Registry Init ===== */

void choice_registry_init(choice_registry_t *reg) {
    if (!reg) return;
    ev_memset(reg, 0, sizeof(*reg));
    reg->next_candidate_id = 1;
    reg->next_decision_id = 1;
}

/* ===== Candidate Management ===== */

int32_t choice_register_candidate(choice_registry_t *reg, const char *name,
                                  uint32_t priority, uint32_t weight,
                                  const char *capability,
                                  bool requires_admission) {
    if (!reg || !name) return -1;
    if (reg->candidate_count >= CHOICE_MAX_CANDIDATES) return -1;

    choice_candidate_t *c = &reg->candidates[reg->candidate_count];
    ev_memset(c, 0, sizeof(*c));
    c->id = reg->next_candidate_id++;
    copy_str(c->name, name, CHOICE_MAX_NAME_LEN);
    c->priority = priority;
    c->weight = weight;
    if (capability)
        copy_str(c->capability, capability, CHOICE_MAX_CAP_LEN);
    c->active = true;
    c->requires_admission = requires_admission;
    c->selection_count = 0;
    return (int32_t)reg->candidate_count++;
}

choice_candidate_t *choice_get_candidate(choice_registry_t *reg, uint32_t idx) {
    if (!reg || idx >= reg->candidate_count) return NULL;
    return &reg->candidates[idx];
}

bool choice_set_candidate_active(choice_registry_t *reg, uint32_t idx, bool active) {
    choice_candidate_t *c = choice_get_candidate(reg, idx);
    if (!c) return false;
    c->active = active;
    return true;
}

/* ===== Decision Making ===== */

int32_t choice_decide(choice_registry_t *reg, const char *label,
                      choice_tie_policy_t tie_policy) {
    if (!reg) return -1;
    if (reg->decision_count >= CHOICE_MAX_DECISIONS) return -1;

    /* Find best active candidates */
    int32_t candidates[CHOICE_MAX_CANDIDATES];
    uint32_t candidate_count = 0;
    uint32_t best_priority = UINT32_MAX;
    uint32_t best_weight = 0;

    for (uint32_t i = 0; i < reg->candidate_count; i++) {
        choice_candidate_t *c = &reg->candidates[i];
        if (!c->active) continue;

        if (c->priority < best_priority) {
            best_priority = c->priority;
            best_weight = c->weight;
            candidate_count = 0;
            candidates[candidate_count++] = (int32_t)i;
        } else if (c->priority == best_priority) {
            if (c->weight > best_weight) {
                best_weight = c->weight;
                candidate_count = 0;
                candidates[candidate_count++] = (int32_t)i;
            } else if (c->weight == best_weight) {
                if (candidate_count < CHOICE_MAX_CANDIDATES)
                    candidates[candidate_count++] = (int32_t)i;
            }
        }
    }

    /* Record decision */
    choice_decision_t *d = &reg->decisions[reg->decision_count];
    ev_memset(d, 0, sizeof(*d));
    d->id = reg->next_decision_id++;
    if (label)
        copy_str(d->label, label, CHOICE_MAX_NAME_LEN);
    d->candidate_count = candidate_count;
    d->tie_policy = tie_policy;
    d->timestamp = reg->current_time;

    if (candidate_count == 0) {
        d->selected_idx = -1;
        d->outcome = CHOICE_OUTCOME_REJECTED;
        return (int32_t)reg->decision_count++;
    }

    if (candidate_count == 1) {
        d->selected_idx = candidates[0];
        d->outcome = CHOICE_OUTCOME_SELECTED;
        reg->candidates[candidates[0]].selection_count++;
        return (int32_t)reg->decision_count++;
    }

    /* Tie-breaking */
    int32_t selected;
    switch (tie_policy) {
        case CHOICE_TIE_LOWEST_ID:
            selected = candidates[0];
            for (uint32_t i = 1; i < candidate_count; i++) {
                if (candidates[i] < selected) selected = candidates[i];
            }
            d->selected_idx = selected;
            d->outcome = CHOICE_OUTCOME_SELECTED;
            reg->candidates[selected].selection_count++;
            break;

        case CHOICE_TIE_HIGHEST_ID:
            selected = candidates[0];
            for (uint32_t i = 1; i < candidate_count; i++) {
                if (candidates[i] > selected) selected = candidates[i];
            }
            d->selected_idx = selected;
            d->outcome = CHOICE_OUTCOME_SELECTED;
            reg->candidates[selected].selection_count++;
            break;

        case CHOICE_TIE_ROUND_ROBIN:
            selected = candidates[reg->rr_counter % candidate_count];
            reg->rr_counter++;
            d->selected_idx = selected;
            d->outcome = CHOICE_OUTCOME_SELECTED;
            reg->candidates[selected].selection_count++;
            break;

        case CHOICE_TIE_FIRST_REGISTERED:
            /* First registered = lowest index in our array */
            selected = candidates[0];
            for (uint32_t i = 1; i < candidate_count; i++) {
                if (candidates[i] < selected) selected = candidates[i];
            }
            d->selected_idx = selected;
            d->outcome = CHOICE_OUTCOME_SELECTED;
            reg->candidates[selected].selection_count++;
            break;

        case CHOICE_TIE_DEFER_S0:
        default:
            d->selected_idx = -2;
            d->outcome = CHOICE_OUTCOME_DEFERRED;
            break;
    }

    return (int32_t)reg->decision_count++;
}

choice_decision_t *choice_get_decision(choice_registry_t *reg, uint32_t idx) {
    if (!reg || idx >= reg->decision_count) return NULL;
    return &reg->decisions[idx];
}

/* ===== Time ===== */

void choice_advance_time(choice_registry_t *reg, uint32_t delta) {
    if (!reg) return;
    reg->current_time += delta;
}

/* ===== Queries ===== */

uint32_t choice_count_active_candidates(choice_registry_t *reg) {
    if (!reg) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < reg->candidate_count; i++) {
        if (reg->candidates[i].active) count++;
    }
    return count;
}

uint32_t choice_count_by_outcome(choice_registry_t *reg, choice_outcome_t outcome) {
    if (!reg) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < reg->decision_count; i++) {
        if (reg->decisions[i].outcome == outcome) count++;
    }
    return count;
}

uint32_t choice_get_selection_count(choice_registry_t *reg, uint32_t candidate_idx) {
    choice_candidate_t *c = choice_get_candidate(reg, candidate_idx);
    if (!c) return 0;
    return c->selection_count;
}

/* ===== Name Functions ===== */

const char *choice_outcome_name(choice_outcome_t outcome) {
    switch (outcome) {
        case CHOICE_OUTCOME_SELECTED: return "selected";
        case CHOICE_OUTCOME_DEFERRED: return "deferred";
        case CHOICE_OUTCOME_REJECTED: return "rejected";
        case CHOICE_OUTCOME_TIE:      return "tie";
        default:                       return "unknown";
    }
}

const char *choice_tie_policy_name(choice_tie_policy_t policy) {
    switch (policy) {
        case CHOICE_TIE_LOWEST_ID:        return "lowest_id";
        case CHOICE_TIE_HIGHEST_ID:       return "highest_id";
        case CHOICE_TIE_ROUND_ROBIN:      return "round_robin";
        case CHOICE_TIE_DEFER_S0:         return "defer_s0";
        case CHOICE_TIE_FIRST_REGISTERED: return "first_registered";
        default:                           return "unknown";
    }
}

/* ---- DECLARATION -----------------------------------------------------------

 * The decision registry. REQUIRES_NONE is measured (choice.o's `nm -u` is
 * empty) and the registry is caller-owned, so there is nothing to map first.
 *
 * NOTE FOR WHOEVER RESOLVES THE SIBLING QUESTION: choice_core.c is in the same
 * directory and was already linked; this file is the fuller implementation
 * that agent A wired in. Their symbol sets do not collide. Which one survives
 * is a design decision (PROVENANCE/LAYERED_BRINGUP.md SS5), not a build one, so
 * only this file declares -- and it declares choice_registry_ready, a name
 * that describes THIS API rather than the directory.
 */
#include "zxv_decl.h"
static int zxvd_choice_bringup(void) {
    static choice_registry_t reg;
    choice_registry_init(&reg);
    if (choice_count_active_candidates(&reg) != 0u) return -1;
    return 0;
}

ZXV_DECLARE(choice,
    ZXV_PROVIDES(choice_registry_ready),
    ZXV_REQUIRES_NONE,
    ZXV_BRINGUP(zxvd_choice_bringup));
