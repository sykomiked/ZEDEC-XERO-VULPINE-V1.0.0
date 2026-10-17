/* lpres.h — Paraconsistent Presence States (K3)
 *
 * K3 LPRES attests source, dialect, dependencies, runtime, data, symbols,
 * and contradictory evidence using paraconsistent logic. It tracks presence
 * states that can be both true and false simultaneously (paraconsistent),
 * preventing explosion from contradictions.
 *
 * Referenced by:
 *   - Tri-Space Programming Spec: K3_LPRES attests presence and contradiction
 *   - RCE Spec: K3_LPRES attests model packs, geometry, parameters, sensors
 *   - Root Computing Spec: K3_LPRES attests source, dialect, dependencies
 *   - UBH Spec: K3_LPRES attests format identity and contradictory evidence
 *   - Master Roadmap: kernel identity and capability primitives
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef LPRES_H
#define LPRES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "event_space.h"

/* ===== LPRES Constants ===== */

#define LPRES_MAX_ATTESTATIONS  128
#define LPRES_MAX_NAME_LEN      32
#define LPRES_MAX_DIGEST_LEN    32

/* ===== Paraconsistent Logical States ===== */
/* Four-valued logic: true, false, both, neither.
 * This prevents explosion from contradictions. */

typedef enum {
    LPRES_STATE_NEITHER = 0,  /* S0: unknown, unattested, pending */
    LPRES_STATE_TRUE    = 1,  /* S+: attested present and valid */
    LPRES_STATE_FALSE   = 2,  /* S-: attested absent or invalid */
    LPRES_STATE_BOTH    = 3,  /* contradictory: both true and false evidence */
    /* The four above are the foundation and their ids are FROZEN. Further
     * phases enter above them (the gateway is the port) without ever
     * renumbering these four. The arity is LPRES_STATE_COUNT: every table
     * and loop iterates it, so a new phase extends them with no code
     * change (docs/agent_loop/LP_EXTENSION.md). */
    LPRES_STATE_COUNT   = 4
} lpres_state_t;

/* The chain rank: the operators are the meet (min) and the join (max) on
 * this ordering. FALSE is the bottom, TRUE the top, and NEITHER sits below
 * BOTH — the deliberate deviation from Belnap's FOUR so that no mix of the
 * non-TRUE values can ever yield TRUE (the never-authorise guard). A new
 * phase declares its rank here and the operators extend automatically. */
extern const uint8_t lpres_rank[LPRES_STATE_COUNT];

/* ===== Attestation Subject Types ===== */

typedef enum {
    LPRES_SUBJECT_SOURCE       = 0,
    LPRES_SUBJECT_DIALECT      = 1,
    LPRES_SUBJECT_DEPENDENCY   = 2,
    LPRES_SUBJECT_RUNTIME      = 3,
    LPRES_SUBJECT_DATA         = 4,
    LPRES_SUBJECT_SYMBOL       = 5,
    LPRES_SUBJECT_MODEL_PACK   = 6,
    LPRES_SUBJECT_GEOMETRY     = 7,
    LPRES_SUBJECT_PARAMETER    = 8,
    LPRES_SUBJECT_SENSOR       = 9,
    LPRES_SUBJECT_ACTUATOR     = 10,
    LPRES_SUBJECT_CALIBRATION  = 11,
    LPRES_SUBJECT_FORMAT       = 12,
    LPRES_SUBJECT_KEY          = 13,
    LPRES_SUBJECT_FIRMWARE     = 14,
} lpres_subject_type_t;

/* ===== Attestation Record ===== */

typedef struct lpres_attestation {
    char name[LPRES_MAX_NAME_LEN];
    lpres_subject_type_t subject_type;
    lpres_state_t state;
    uint8_t digest[LPRES_MAX_DIGEST_LEN];
    uint32_t digest_len;
    uint64_t timestamp;
    char attester[LPRES_MAX_NAME_LEN];
    bool revoked;
    uint32_t positive_evidence;  /* count of supporting evidence */
    uint32_t negative_evidence;  /* count of contradicting evidence */
} lpres_attestation_t;

/* ===== LPRES Registry ===== */

typedef struct lpres_registry {
    lpres_attestation_t attestations[LPRES_MAX_ATTESTATIONS];
    uint32_t count;
    uint64_t current_time;
} lpres_registry_t;

/* ===== Paraconsistent Logic Operations ===== */

lpres_state_t lpres_negate(lpres_state_t s);
lpres_state_t lpres_conjoin(lpres_state_t a, lpres_state_t b);
lpres_state_t lpres_disjoin(lpres_state_t a, lpres_state_t b);
/* The knowledge join: combine what two SOURCES say about ONE subject.
 * Conjoin/disjoin are the truth order (all must hold / any holds); this is
 * the information order, where agreement keeps the value, a source that
 * says nothing adds nothing, and T from one source with F from another is
 * B (a contradiction is recorded, never resolved by picking one):
 *       T F B N
 *   T   T B B T
 *   F   B F B F
 *   B   B B B B
 *   N   T F B N
 * In this encoding (bit 0 told-true, bit 1 told-false) it is a bitwise OR.
 * Rule: kjoin across sources about a subject, then conjoin across subjects
 * that must all hold (docs/BAREMETAL_OS_GAPS.md section 3). */
lpres_state_t lpres_kjoin(lpres_state_t a, lpres_state_t b);
bool lpres_is_certain(lpres_state_t s);
bool lpres_is_contradictory(lpres_state_t s);
bool lpres_is_unknown(lpres_state_t s);
bool lpres_implies_actuator_authority(lpres_state_t s);

/* ===== Registry Operations ===== */

void lpres_registry_init(lpres_registry_t *reg);
void lpres_advance_time(lpres_registry_t *reg, uint64_t delta);

int32_t lpres_attest(lpres_registry_t *reg,
                     const char *name,
                     lpres_subject_type_t type,
                     lpres_state_t state,
                     const char *attester,
                     const uint8_t *digest,
                     uint32_t digest_len);

lpres_attestation_t *lpres_find(lpres_registry_t *reg, const char *name);
lpres_attestation_t *lpres_get(lpres_registry_t *reg, uint32_t idx);

bool lpres_add_evidence(lpres_registry_t *reg, uint32_t idx, bool positive);
bool lpres_revoke(lpres_registry_t *reg, uint32_t idx);
bool lpres_update_state(lpres_registry_t *reg, uint32_t idx, lpres_state_t new_state);

/* ===== Batch Queries ===== */

uint32_t lpres_count_by_state(lpres_registry_t *reg, lpres_state_t state);
uint32_t lpres_count_by_type(lpres_registry_t *reg, lpres_subject_type_t type);
uint32_t lpres_count_contradictions(lpres_registry_t *reg);
uint32_t lpres_count_unattested(lpres_registry_t *reg);

/* ===== Safety Gate ===== */
/* Returns true if all attestations are certain (true or false)
 * with no contradictions or unknowns. Required for actuator authority. */

bool lpres_safety_gate_clear(lpres_registry_t *reg);

/* ===== Name Functions ===== */

const char *lpres_state_name(lpres_state_t state);
const char *lpres_subject_name(lpres_subject_type_t type);

#endif /* LPRES_H */
