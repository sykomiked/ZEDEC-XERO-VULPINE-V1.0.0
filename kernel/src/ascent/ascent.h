/*
 * ascent.h — Ladder of Ascent OPSEC Security Framework
 *
 * Implements the 10-level Ascent security model (Substrate 0 through
 * Level 9+) with Five-Phase Logic (5PL) and Triad Protocol integration.
 *
 * Levels:
 *   Substrate 0: Ground & Body (physiology, ethics, time discipline)
 *   Level 1: Signal Hygiene (Surface Web)
 *   Level 2: Credential Ecology (Deep Web)
 *   Level 3: Maskcraft (Dark Web)
 *   Level 4: Chartercraft (Trust Networks)
 *   Level 5: Jurisdiction Pins (Law & Ledgers)
 *   Level 6: Platform Taming (Corporate Systems)
 *   Level 7: Symbol Engines (Mythotech)
 *   Level 8: Mirrorwork (Abyss Protocol)
 *   Level 9+: Positive Completion (Open Convergence)
 *
 * Five-Phase Logic (5PL):
 *   (+1) Claim — one sentence, signed, time-stamped
 *   (-1) Refutation — strongest counter-case
 *   (0)  Unknowns — explicit gaps + test plan + owner
 *   (+0) Witness — hashes, lengths, third-device photos, custody log
 *   (1)  Consensus — at least two pin types + human affidavit
 *
 * Triad Protocol:
 *   Operator (does) / Recorder (captures) / Skeptic (questions)
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

#ifndef ZEDEC_ASCENT_H
#define ZEDEC_ASCENT_H

#include <stdint.h>
#include <stdbool.h>

/* ===== Constants ===== */

#define ASCENT_MAX_LEVELS       10  /* Substrate 0 + Levels 1-9 */
#define ASCENT_MAX_BUNDLES      64
#define ASCENT_MAX_TRIADS       16
#define ASCENT_MAX_PINS         32
#define ASCENT_MAX_LABEL        64
#define ASCENT_MAX_HASH_SIZE    32
#define ASCENT_MAX_TEXT         256
#define ASCENT_MAX_WITNESSES     8
#define ASCENT_MAX_UNKNOWNS      8
#define ASCENT_MAX_CONSENSUS     4
#define ASCENT_MAX_PERSONAS     16
#define ASCENT_MAX_CHARTERS      8
#define ASCENT_MAX_MEMBERS      12
#define ASCENT_MAX_INCIDENTS    32

/* ===== Ascent Levels ===== */

typedef enum {
    ASCENT_SUBSTRATE_0  = 0,  /* Ground & Body */
    ASCENT_LEVEL_1      = 1,  /* Signal Hygiene (Surface) */
    ASCENT_LEVEL_2      = 2,  /* Credential Ecology (Deep) */
    ASCENT_LEVEL_3      = 3,  /* Maskcraft (Dark) */
    ASCENT_LEVEL_4      = 4,  /* Chartercraft (Trust Networks) */
    ASCENT_LEVEL_5      = 5,  /* Jurisdiction Pins (Law & Ledgers) */
    ASCENT_LEVEL_6      = 6,  /* Platform Taming (Corporate) */
    ASCENT_LEVEL_7      = 7,  /* Symbol Engines (Mythotech) */
    ASCENT_LEVEL_8      = 8,  /* Mirrorwork (Abyss Protocol) */
    ASCENT_LEVEL_9      = 9,  /* Positive Completion */
} ascent_level_t;

/* ===== 5PL Phase Types ===== */

typedef enum {
    ASCENT_5PL_CLAIM      = 0,  /* (+1) */
    ASCENT_5PL_REFUTATION = 1,  /* (-1) */
    ASCENT_5PL_UNKNOWN    = 2,  /* (0)  */
    ASCENT_5PL_WITNESS    = 3,  /* (+0) */
    ASCENT_5PL_CONSENSUS  = 4,  /* (1)  */
} ascent_5pl_phase_t;

/* ===== Triad Roles ===== */

typedef enum {
    ASCENT_TRIAD_OPERATOR  = 0,
    ASCENT_TRIAD_RECORDER  = 1,
    ASCENT_TRIAD_SKEPTIC   = 2,
} ascent_triad_role_t;

/* ===== Pin Types (Jurisdiction Anchors) ===== */

typedef enum {
    ASCENT_PIN_TRANSPARENCY_LOG = 0,  /* Public append-only log */
    ASCENT_PIN_NOTARY           = 1,  /* Traditional notary/affidavit */
    ASCENT_PIN_DISTRIBUTED      = 2,  /* IPFS/content-addressed mirror */
    ASCENT_PIN_HUMAN            = 3,  /* Human witness affidavit */
    ASCENT_PIN_LEDGER           = 4,  /* Blockchain/distributed ledger */
} ascent_pin_type_t;

/* ===== 5PL Bundle ===== */

typedef struct {
    char     bundle_id[ASCENT_MAX_LABEL];
    char     title[ASCENT_MAX_LABEL];
    ascent_level_t level;
    uint64_t timestamp;

    /* (+1) Claim */
    char     claim[ASCENT_MAX_TEXT];

    /* (-1) Refutation */
    char     refutation[ASCENT_MAX_TEXT];

    /* (0) Unknowns */
    char     unknowns[ASCENT_MAX_UNKNOWNS][ASCENT_MAX_TEXT];
    uint32_t num_unknowns;

    /* (+0) Witness */
    char     witness_desc[ASCENT_MAX_WITNESSES][ASCENT_MAX_TEXT];
    uint8_t  witness_hashes[ASCENT_MAX_WITNESSES][ASCENT_MAX_HASH_SIZE];
    uint32_t num_witnesses;

    /* (1) Consensus */
    ascent_pin_type_t consensus_pins[ASCENT_MAX_CONSENSUS];
    char     consensus_desc[ASCENT_MAX_CONSENSUS][ASCENT_MAX_TEXT];
    uint32_t num_consensus;

    /* Metadata */
    bool     sealed;       /* Version frozen */
    uint32_t version;      /* Version number */
    char     created_by[ASCENT_MAX_LABEL];
    bool     phase_gates_passed;  /* All gates: +0, -1, 0 present */
} ascent_bundle_t;

/* ===== Triad Session ===== */

typedef struct {
    uint32_t id;
    ascent_level_t level;
    char     operator_name[ASCENT_MAX_LABEL];
    char     recorder_name[ASCENT_MAX_LABEL];
    char     skeptic_name[ASCENT_MAX_LABEL];
    char     stop_phrase[ASCENT_MAX_LABEL];
    uint64_t start_time;
    uint64_t stop_time;
    uint32_t timebox_minutes;
    char     intention[ASCENT_MAX_TEXT];
    char     bundle_id[ASCENT_MAX_LABEL];  /* Associated bundle */
    bool     active;
    bool     debriefed;
} ascent_triad_t;

/* ===== Persona (for identity compartmentalization) ===== */

typedef struct {
    char     name[ASCENT_MAX_LABEL];
    ascent_level_t level;     /* Which level this persona operates at */
    char     purpose[ASCENT_MAX_TEXT];
    char     style_guide[ASCENT_MAX_TEXT];
    uint64_t created;
    uint64_t retire_by;
    bool     active;
    bool     isolated;        /* No cross-contamination */
} ascent_persona_t;

/* ===== Charter (Trust Network) ===== */

typedef struct {
    char     name[ASCENT_MAX_LABEL];
    char     members[ASCENT_MAX_MEMBERS][ASCENT_MAX_LABEL];
    uint32_t num_members;
    uint32_t multisig_threshold;  /* e.g., 2-of-3 */
    uint32_t multisig_total;
    char     mission[ASCENT_MAX_TEXT];
    bool     active;
    uint64_t created;
} ascent_charter_t;

/* ===== Incident Log ===== */

typedef struct {
    uint64_t timestamp;
    char     description[ASCENT_MAX_TEXT];
    char     immediate_action[ASCENT_MAX_TEXT];
    char     followup_owner[ASCENT_MAX_LABEL];
    bool     resolved;
} ascent_incident_t;

/* ===== Ascent State ===== */

typedef struct {
    /* 5PL Bundles */
    ascent_bundle_t bundles[ASCENT_MAX_BUNDLES];
    uint32_t        num_bundles;

    /* Triad Sessions */
    ascent_triad_t  triads[ASCENT_MAX_TRIADS];
    uint32_t        num_triads;

    /* Personas */
    ascent_persona_t personas[ASCENT_MAX_PERSONAS];
    uint32_t          num_personas;

    /* Charters */
    ascent_charter_t charters[ASCENT_MAX_CHARTERS];
    uint32_t         num_charters;

    /* Incidents */
    ascent_incident_t incidents[ASCENT_MAX_INCIDENTS];
    uint32_t          num_incidents;

    /* Current level */
    ascent_level_t current_level;
    bool           initialized;
} ascent_t;

/* ===== API ===== */

void ascent_init(ascent_t *a);

/* Bundle operations (5PL) */
int32_t ascent_bundle_create(ascent_t *a, const char *id, const char *title,
                              ascent_level_t level, const char *created_by);
int ascent_bundle_set_claim(ascent_t *a, uint32_t idx, const char *claim);
int ascent_bundle_set_refutation(ascent_t *a, uint32_t idx, const char *refutation);
int ascent_bundle_add_unknown(ascent_t *a, uint32_t idx, const char *unknown);
int ascent_bundle_add_witness(ascent_t *a, uint32_t idx, const char *desc,
                               const uint8_t *hash);
int ascent_bundle_add_consensus(ascent_t *a, uint32_t idx, ascent_pin_type_t pin,
                                 const char *desc);
bool ascent_bundle_check_gates(ascent_t *a, uint32_t idx);
int ascent_bundle_seal(ascent_t *a, uint32_t idx);
int ascent_bundle_version(ascent_t *a, uint32_t idx);

/* Triad operations */
int32_t ascent_triad_create(ascent_t *a, ascent_level_t level,
                             const char *op, const char *rec, const char *skp,
                             const char *stop_phrase, uint32_t timebox_min);
int ascent_triad_start(ascent_t *a, uint32_t idx);
int ascent_triad_stop(ascent_t *a, uint32_t idx);
int ascent_triad_debrief(ascent_t *a, uint32_t idx);

/* Persona management */
int32_t ascent_persona_create(ascent_t *a, const char *name, ascent_level_t level,
                               const char *purpose);
int ascent_persona_retire(ascent_t *a, uint32_t idx);
int ascent_persona_check_isolation(ascent_t *a, uint32_t idx);

/* Charter management */
int32_t ascent_charter_create(ascent_t *a, const char *name, const char *mission,
                               uint32_t threshold, uint32_t total);
int ascent_charter_add_member(ascent_t *a, uint32_t idx, const char *member);
bool ascent_charter_check_quorum(ascent_t *a, uint32_t idx, uint32_t signers);

/* Incident logging */
int32_t ascent_incident_log(ascent_t *a, const char *desc, const char *action,
                             const char *owner);
int ascent_incident_resolve(ascent_t *a, uint32_t idx);

/* Level management */
void ascent_set_level(ascent_t *a, ascent_level_t level);
const char *ascent_level_name(ascent_level_t level);
const char *ascent_5pl_phase_name(ascent_5pl_phase_t phase);
const char *ascent_triad_role_name(ascent_triad_role_t role);
const char *ascent_pin_type_name(ascent_pin_type_t pin);

/* Phase gate validation */
bool ascent_validate_bundle(const ascent_bundle_t *b);

#endif /* ZEDEC_ASCENT_H */
