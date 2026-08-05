/* lpres.c — Paraconsistent Presence States (K3)
 *
 * Implements paraconsistent four-valued logic and attestation management.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */

#include "lpres.h"

/* ===== Helpers ===== */

static void copy_str(char *dst, const char *src, uint32_t max) {
    uint32_t i;
    for (i = 0; i + 1 < max && src && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static bool str_eq(const char *a, const char *b) {
    uint32_t i;
    for (i = 0; a[i] != '\0' && b[i] != '\0'; i++) {
        if (a[i] != b[i]) return 0;
    }
    return a[i] == '\0' && b[i] == '\0';
}

/* ===== Paraconsistent Logic Operations ===== */
/* Four-valued logic (Belnap's 4-valued logic):
 *   NEITHER (⊥) — no information
 *   TRUE (T)    — only positive evidence
 *   FALSE (F)   — only negative evidence
 *   BOTH (B)    — contradictory evidence
 *
 * Negation:
 *   ¬T = F, ¬F = T, ¬B = B, ¬⊥ = ⊥
 *
 * Conjunction (meet):
 *   T∧T=T, T∧F=F, T∧B=B, T∧⊥=⊥
 *   F∧anything=F
 *   B∧B=B, B∧⊥=⊥
 *   ⊥∧⊥=⊥
 *
 * Disjunction (join):
 *   F∨F=F, F∨T=T, F∨B=B, F∨⊥=⊥
 *   T∨anything=T
 *   B∨B=B, B∨⊥=B
 *   ⊥∨⊥=⊥
 */

lpres_state_t lpres_negate(lpres_state_t s) {
    switch (s) {
        case LPRES_STATE_TRUE:    return LPRES_STATE_FALSE;
        case LPRES_STATE_FALSE:   return LPRES_STATE_TRUE;
        case LPRES_STATE_BOTH:    return LPRES_STATE_BOTH;
        case LPRES_STATE_NEITHER: return LPRES_STATE_NEITHER;
        default:                   return LPRES_STATE_NEITHER;
    }
}

lpres_state_t lpres_conjoin(lpres_state_t a, lpres_state_t b) {
    /* F dominates */
    if (a == LPRES_STATE_FALSE || b == LPRES_STATE_FALSE)
        return LPRES_STATE_FALSE;
    /* T is identity */
    if (a == LPRES_STATE_TRUE)
        return b;
    if (b == LPRES_STATE_TRUE)
        return a;
    /* NEITHER dominates BOTH in meet (bottom absorbs) */
    if (a == LPRES_STATE_NEITHER || b == LPRES_STATE_NEITHER)
        return LPRES_STATE_NEITHER;
    /* Both BOTH */
    return LPRES_STATE_BOTH;
}

lpres_state_t lpres_disjoin(lpres_state_t a, lpres_state_t b) {
    /* T dominates */
    if (a == LPRES_STATE_TRUE || b == LPRES_STATE_TRUE)
        return LPRES_STATE_TRUE;
    /* F is identity */
    if (a == LPRES_STATE_FALSE)
        return b;
    if (b == LPRES_STATE_FALSE)
        return a;
    /* B dominates NEITHER */
    if (a == LPRES_STATE_BOTH || b == LPRES_STATE_BOTH)
        return LPRES_STATE_BOTH;
    /* Both NEITHER */
    return LPRES_STATE_NEITHER;
}

bool lpres_is_certain(lpres_state_t s) {
    return s == LPRES_STATE_TRUE || s == LPRES_STATE_FALSE;
}

bool lpres_is_contradictory(lpres_state_t s) {
    return s == LPRES_STATE_BOTH;
}

bool lpres_is_unknown(lpres_state_t s) {
    return s == LPRES_STATE_NEITHER;
}

bool lpres_implies_actuator_authority(lpres_state_t s) {
    /* Only TRUE can authorize actuator action.
     * BOTH, FALSE, and NEITHER all block actuator authority. */
    return s == LPRES_STATE_TRUE;
}

/* ===== Registry Operations ===== */

void lpres_registry_init(lpres_registry_t *reg) {
    if (!reg) return;
    ev_memset(reg, 0, sizeof(*reg));
}

void lpres_advance_time(lpres_registry_t *reg, uint64_t delta) {
    if (!reg) return;
    reg->current_time += delta;
}

int32_t lpres_attest(lpres_registry_t *reg,
                     const char *name,
                     lpres_subject_type_t type,
                     lpres_state_t state,
                     const char *attester,
                     const uint8_t *digest,
                     uint32_t digest_len) {
    if (!reg || !name) return -1;
    if (reg->count >= LPRES_MAX_ATTESTATIONS) return -1;
    if (digest_len > LPRES_MAX_DIGEST_LEN) digest_len = LPRES_MAX_DIGEST_LEN;

    lpres_attestation_t *a = &reg->attestations[reg->count];
    ev_memset(a, 0, sizeof(*a));
    copy_str(a->name, name, LPRES_MAX_NAME_LEN);
    a->subject_type = type;
    a->state = state;
    a->timestamp = reg->current_time;
    copy_str(a->attester, attester, LPRES_MAX_NAME_LEN);
    a->revoked = false;
    a->positive_evidence = (state == LPRES_STATE_TRUE || state == LPRES_STATE_BOTH) ? 1 : 0;
    a->negative_evidence = (state == LPRES_STATE_FALSE || state == LPRES_STATE_BOTH) ? 1 : 0;

    if (digest && digest_len > 0) {
        for (uint32_t i = 0; i < digest_len; i++)
            a->digest[i] = digest[i];
        a->digest_len = digest_len;
    }

    return (int32_t)reg->count++;
}

lpres_attestation_t *lpres_find(lpres_registry_t *reg, const char *name) {
    if (!reg || !name) return NULL;
    for (uint32_t i = 0; i < reg->count; i++) {
        if (str_eq(reg->attestations[i].name, name))
            return &reg->attestations[i];
    }
    return NULL;
}

lpres_attestation_t *lpres_get(lpres_registry_t *reg, uint32_t idx) {
    if (!reg || idx >= reg->count) return NULL;
    return &reg->attestations[idx];
}

bool lpres_add_evidence(lpres_registry_t *reg, uint32_t idx, bool positive) {
    if (!reg || idx >= reg->count) return false;
    lpres_attestation_t *a = &reg->attestations[idx];
    if (a->revoked) return false;

    if (positive) {
        a->positive_evidence++;
    } else {
        a->negative_evidence++;
    }

    /* Recompute state based on evidence counts */
    bool has_pos = a->positive_evidence > 0;
    bool has_neg = a->negative_evidence > 0;

    if (has_pos && has_neg) {
        a->state = LPRES_STATE_BOTH;
    } else if (has_pos) {
        a->state = LPRES_STATE_TRUE;
    } else if (has_neg) {
        a->state = LPRES_STATE_FALSE;
    } else {
        a->state = LPRES_STATE_NEITHER;
    }

    return true;
}

bool lpres_revoke(lpres_registry_t *reg, uint32_t idx) {
    if (!reg || idx >= reg->count) return false;
    reg->attestations[idx].revoked = true;
    return true;
}

bool lpres_update_state(lpres_registry_t *reg, uint32_t idx, lpres_state_t new_state) {
    if (!reg || idx >= reg->count) return false;
    lpres_attestation_t *a = &reg->attestations[idx];
    if (a->revoked) return false;
    a->state = new_state;
    return true;
}

/* ===== Batch Queries ===== */

uint32_t lpres_count_by_state(lpres_registry_t *reg, lpres_state_t state) {
    if (!reg) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < reg->count; i++) {
        if (!reg->attestations[i].revoked && reg->attestations[i].state == state)
            count++;
    }
    return count;
}

uint32_t lpres_count_by_type(lpres_registry_t *reg, lpres_subject_type_t type) {
    if (!reg) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < reg->count; i++) {
        if (!reg->attestations[i].revoked && reg->attestations[i].subject_type == type)
            count++;
    }
    return count;
}

uint32_t lpres_count_contradictions(lpres_registry_t *reg) {
    return lpres_count_by_state(reg, LPRES_STATE_BOTH);
}

uint32_t lpres_count_unattested(lpres_registry_t *reg) {
    return lpres_count_by_state(reg, LPRES_STATE_NEITHER);
}

/* ===== Safety Gate ===== */

bool lpres_safety_gate_clear(lpres_registry_t *reg) {
    if (!reg) return false;
    for (uint32_t i = 0; i < reg->count; i++) {
        lpres_attestation_t *a = &reg->attestations[i];
        if (a->revoked) continue;
        if (!lpres_is_certain(a->state)) return false;
    }
    return true;
}

/* ===== Name Functions ===== */

const char *lpres_state_name(lpres_state_t state) {
    switch (state) {
        case LPRES_STATE_NEITHER: return "neither";
        case LPRES_STATE_TRUE:    return "true";
        case LPRES_STATE_FALSE:   return "false";
        case LPRES_STATE_BOTH:    return "both";
        default:                   return "unknown";
    }
}

const char *lpres_subject_name(lpres_subject_type_t type) {
    switch (type) {
        case LPRES_SUBJECT_SOURCE:      return "source";
        case LPRES_SUBJECT_DIALECT:     return "dialect";
        case LPRES_SUBJECT_DEPENDENCY:  return "dependency";
        case LPRES_SUBJECT_RUNTIME:     return "runtime";
        case LPRES_SUBJECT_DATA:        return "data";
        case LPRES_SUBJECT_SYMBOL:      return "symbol";
        case LPRES_SUBJECT_MODEL_PACK:  return "model_pack";
        case LPRES_SUBJECT_GEOMETRY:    return "geometry";
        case LPRES_SUBJECT_PARAMETER:   return "parameter";
        case LPRES_SUBJECT_SENSOR:      return "sensor";
        case LPRES_SUBJECT_ACTUATOR:    return "actuator";
        case LPRES_SUBJECT_CALIBRATION: return "calibration";
        case LPRES_SUBJECT_FORMAT:      return "format";
        case LPRES_SUBJECT_KEY:         return "key";
        case LPRES_SUBJECT_FIRMWARE:    return "firmware";
        default:                         return "unknown";
    }
}
