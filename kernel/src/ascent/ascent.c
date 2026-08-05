/*
 * ascent.c — Ladder of Ascent OPSEC Security Framework Implementation
 *
 * 5PL bundles, Triad Protocol, persona management, charter governance,
 * jurisdiction pinning, and incident logging across all 10 levels.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: ZEDEC Open Patent License 1.0
 */

#ifdef TEST_HOST
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#else
#include "freestanding.h"
#endif

#include "ascent.h"

/* ===== Helpers ===== */

static void asc_memset(void *dst, int v, uint32_t n) {
    uint8_t *d = (uint8_t *)dst; uint32_t i;
    for (i = 0; i < n; i++) d[i] = (uint8_t)v;
}

static uint32_t asc_strlen(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }

static void asc_strcpy(char *dst, const char *src) {
    uint32_t i = 0; while (src[i]) { dst[i] = src[i]; i++; } dst[i] = '\0';
}

static int asc_strcmp(const char *a, const char *b) {
    uint32_t i = 0; while (a[i] && b[i]) {
        if (a[i] != b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        i++;
    }
    return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
}

static void asc_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst; const uint8_t *s = (const uint8_t *)src; uint32_t i;
    for (i = 0; i < n; i++) d[i] = s[i];
}

/* ===== Init ===== */

void ascent_init(ascent_t *a) {
    asc_memset(a, 0, sizeof(ascent_t));
    a->current_level = ASCENT_SUBSTRATE_0;
    a->initialized = true;
}

/* ===== Bundle Operations (5PL) ===== */

int32_t ascent_bundle_create(ascent_t *a, const char *id, const char *title,
                              ascent_level_t level, const char *created_by) {
    if (a->num_bundles >= ASCENT_MAX_BUNDLES || !id || !title) return -1;

    int32_t idx = (int32_t)a->num_bundles;
    ascent_bundle_t *b = &a->bundles[idx];
    asc_memset(b, 0, sizeof(ascent_bundle_t));
    asc_strcpy(b->bundle_id, id);
    asc_strcpy(b->title, title);
    b->level = level;
    b->version = 1;
    b->sealed = false;
    b->phase_gates_passed = false;
    if (created_by) asc_strcpy(b->created_by, created_by);

    a->num_bundles++;
    return idx;
}

int ascent_bundle_set_claim(ascent_t *a, uint32_t idx, const char *claim) {
    if (idx >= a->num_bundles || !claim) return -1;
    asc_strcpy(a->bundles[idx].claim, claim);
    ascent_bundle_check_gates(a, idx);
    return 0;
}

int ascent_bundle_set_refutation(ascent_t *a, uint32_t idx, const char *refutation) {
    if (idx >= a->num_bundles || !refutation) return -1;
    asc_strcpy(a->bundles[idx].refutation, refutation);
    ascent_bundle_check_gates(a, idx);
    return 0;
}

int ascent_bundle_add_unknown(ascent_t *a, uint32_t idx, const char *unknown) {
    if (idx >= a->num_bundles || !unknown) return -1;
    ascent_bundle_t *b = &a->bundles[idx];
    if (b->num_unknowns >= ASCENT_MAX_UNKNOWNS) return -1;
    asc_strcpy(b->unknowns[b->num_unknowns], unknown);
    b->num_unknowns++;
    ascent_bundle_check_gates(a, idx);
    return 0;
}

int ascent_bundle_add_witness(ascent_t *a, uint32_t idx, const char *desc,
                               const uint8_t *hash) {
    if (idx >= a->num_bundles || !desc) return -1;
    ascent_bundle_t *b = &a->bundles[idx];
    if (b->num_witnesses >= ASCENT_MAX_WITNESSES) return -1;
    asc_strcpy(b->witness_desc[b->num_witnesses], desc);
    if (hash) asc_memcpy(b->witness_hashes[b->num_witnesses], hash, ASCENT_MAX_HASH_SIZE);
    b->num_witnesses++;
    ascent_bundle_check_gates(a, idx);
    return 0;
}

int ascent_bundle_add_consensus(ascent_t *a, uint32_t idx, ascent_pin_type_t pin,
                                 const char *desc) {
    if (idx >= a->num_bundles || !desc) return -1;
    ascent_bundle_t *b = &a->bundles[idx];
    if (b->num_consensus >= ASCENT_MAX_CONSENSUS) return -1;
    b->consensus_pins[b->num_consensus] = pin;
    asc_strcpy(b->consensus_desc[b->num_consensus], desc);
    b->num_consensus++;
    return 0;
}

bool ascent_bundle_check_gates(ascent_t *a, uint32_t idx) {
    if (idx >= a->num_bundles) return false;
    ascent_bundle_t *b = &a->bundles[idx];

    /* Phase gates: must have (+1) claim, (-1) refutation, (0) unknowns, (+0) witness */
    bool has_claim = b->claim[0] != '\0';
    bool has_refutation = b->refutation[0] != '\0';
    bool has_unknowns = b->num_unknowns > 0;
    bool has_witness = b->num_witnesses > 0;

    b->phase_gates_passed = has_claim && has_refutation && has_unknowns && has_witness;
    return b->phase_gates_passed;
}

bool ascent_validate_bundle(const ascent_bundle_t *b) {
    if (!b) return false;
    /* For sealed bundles, also require consensus (1) with heterogeneous pins */
    if (b->sealed) {
        if (b->num_consensus < 2) return false;
        /* Check for heterogeneous pin types */
        bool types[5] = {false};
        uint32_t i;
        for (i = 0; i < b->num_consensus; i++)
            types[b->consensus_pins[i]] = true;
        uint32_t distinct = 0;
        for (i = 0; i < 5; i++) if (types[i]) distinct++;
        if (distinct < 2) return false;
    }
    return b->phase_gates_passed;
}

int ascent_bundle_seal(ascent_t *a, uint32_t idx) {
    if (idx >= a->num_bundles) return -1;
    if (!ascent_bundle_check_gates(a, idx)) return -1;
    a->bundles[idx].sealed = true;
    return 0;
}

int ascent_bundle_version(ascent_t *a, uint32_t idx) {
    if (idx >= a->num_bundles) return -1;
    if (a->bundles[idx].sealed) {
        /* Create new version: increment version, unseal */
        a->bundles[idx].version++;
        a->bundles[idx].sealed = false;
        /* Clear consensus (needs re-pinning for new version) */
        a->bundles[idx].num_consensus = 0;
    }
    return (int)a->bundles[idx].version;
}

/* ===== Triad Operations ===== */

int32_t ascent_triad_create(ascent_t *a, ascent_level_t level,
                             const char *op, const char *rec, const char *skp,
                             const char *stop_phrase, uint32_t timebox_min) {
    if (a->num_triads >= ASCENT_MAX_TRIADS) return -1;
    int32_t idx = (int32_t)a->num_triads;
    ascent_triad_t *t = &a->triads[idx];
    asc_memset(t, 0, sizeof(ascent_triad_t));
    t->id = (uint32_t)idx;
    t->level = level;
    if (op) asc_strcpy(t->operator_name, op);
    if (rec) asc_strcpy(t->recorder_name, rec);
    if (skp) asc_strcpy(t->skeptic_name, skp);
    if (stop_phrase) asc_strcpy(t->stop_phrase, stop_phrase);
    t->timebox_minutes = timebox_min > 0 ? timebox_min : 45;
    t->active = false;
    t->debriefed = false;

    a->num_triads++;
    return idx;
}

int ascent_triad_start(ascent_t *a, uint32_t idx) {
    if (idx >= a->num_triads) return -1;
    a->triads[idx].active = true;
    a->triads[idx].debriefed = false;
    return 0;
}

int ascent_triad_stop(ascent_t *a, uint32_t idx) {
    if (idx >= a->num_triads) return -1;
    a->triads[idx].active = false;
    return 0;
}

int ascent_triad_debrief(ascent_t *a, uint32_t idx) {
    if (idx >= a->num_triads) return -1;
    if (a->triads[idx].active) return -1;  /* Must stop first */
    a->triads[idx].debriefed = true;
    return 0;
}

/* ===== Persona Management ===== */

int32_t ascent_persona_create(ascent_t *a, const char *name, ascent_level_t level,
                               const char *purpose) {
    if (a->num_personas >= ASCENT_MAX_PERSONAS || !name) return -1;
    int32_t idx = (int32_t)a->num_personas;
    ascent_persona_t *p = &a->personas[idx];
    asc_memset(p, 0, sizeof(ascent_persona_t));
    asc_strcpy(p->name, name);
    p->level = level;
    if (purpose) asc_strcpy(p->purpose, purpose);
    p->active = true;
    p->isolated = true;

    a->num_personas++;
    return idx;
}

int ascent_persona_retire(ascent_t *a, uint32_t idx) {
    if (idx >= a->num_personas) return -1;
    a->personas[idx].active = false;
    return 0;
}

int ascent_persona_check_isolation(ascent_t *a, uint32_t idx) {
    if (idx >= a->num_personas) return -1;
    /* Check that no other active persona shares the same level
     * (compartmentalization rule: one persona per level) */
    uint32_t i;
    for (i = 0; i < a->num_personas; i++) {
        if (i == idx) continue;
        if (a->personas[i].active && a->personas[i].level == a->personas[idx].level)
            return -1;  /* Isolation violated */
    }
    a->personas[idx].isolated = true;
    return 0;
}

/* ===== Charter Management ===== */

int32_t ascent_charter_create(ascent_t *a, const char *name, const char *mission,
                               uint32_t threshold, uint32_t total) {
    if (a->num_charters >= ASCENT_MAX_CHARTERS || !name) return -1;
    int32_t idx = (int32_t)a->num_charters;
    ascent_charter_t *c = &a->charters[idx];
    asc_memset(c, 0, sizeof(ascent_charter_t));
    asc_strcpy(c->name, name);
    if (mission) asc_strcpy(c->mission, mission);
    c->multisig_threshold = threshold;
    c->multisig_total = total;
    c->active = true;

    a->num_charters++;
    return idx;
}

int ascent_charter_add_member(ascent_t *a, uint32_t idx, const char *member) {
    if (idx >= a->num_charters || !member) return -1;
    ascent_charter_t *c = &a->charters[idx];
    if (c->num_members >= ASCENT_MAX_MEMBERS) return -1;
    asc_strcpy(c->members[c->num_members], member);
    c->num_members++;
    return 0;
}

bool ascent_charter_check_quorum(ascent_t *a, uint32_t idx, uint32_t signers) {
    if (idx >= a->num_charters) return false;
    ascent_charter_t *c = &a->charters[idx];
    return signers >= c->multisig_threshold && signers <= c->multisig_total;
}

/* ===== Incident Logging ===== */

int32_t ascent_incident_log(ascent_t *a, const char *desc, const char *action,
                             const char *owner) {
    if (a->num_incidents >= ASCENT_MAX_INCIDENTS || !desc) return -1;
    int32_t idx = (int32_t)a->num_incidents;
    ascent_incident_t *inc = &a->incidents[idx];
    asc_memset(inc, 0, sizeof(ascent_incident_t));
    asc_strcpy(inc->description, desc);
    if (action) asc_strcpy(inc->immediate_action, action);
    if (owner) asc_strcpy(inc->followup_owner, owner);
    inc->resolved = false;

    a->num_incidents++;
    return idx;
}

int ascent_incident_resolve(ascent_t *a, uint32_t idx) {
    if (idx >= a->num_incidents) return -1;
    a->incidents[idx].resolved = true;
    return 0;
}

/* ===== Level Management ===== */

void ascent_set_level(ascent_t *a, ascent_level_t level) {
    a->current_level = level;
}

const char *ascent_level_name(ascent_level_t level) {
    switch (level) {
        case ASCENT_SUBSTRATE_0: return "Substrate 0: Ground & Body";
        case ASCENT_LEVEL_1:     return "Level 1: Signal Hygiene (Surface)";
        case ASCENT_LEVEL_2:     return "Level 2: Credential Ecology (Deep)";
        case ASCENT_LEVEL_3:     return "Level 3: Maskcraft (Dark)";
        case ASCENT_LEVEL_4:     return "Level 4: Chartercraft (Trust)";
        case ASCENT_LEVEL_5:     return "Level 5: Jurisdiction Pins";
        case ASCENT_LEVEL_6:     return "Level 6: Platform Taming";
        case ASCENT_LEVEL_7:     return "Level 7: Symbol Engines";
        case ASCENT_LEVEL_8:     return "Level 8: Mirrorwork (Abyss)";
        case ASCENT_LEVEL_9:     return "Level 9+: Positive Completion";
        default: return "Unknown";
    }
}

const char *ascent_5pl_phase_name(ascent_5pl_phase_t phase) {
    switch (phase) {
        case ASCENT_5PL_CLAIM:      return "(+1) Claim";
        case ASCENT_5PL_REFUTATION: return "(-1) Refutation";
        case ASCENT_5PL_UNKNOWN:    return "(0) Unknowns";
        case ASCENT_5PL_WITNESS:    return "(+0) Witness";
        case ASCENT_5PL_CONSENSUS:  return "(1) Consensus";
        default: return "Unknown";
    }
}

const char *ascent_triad_role_name(ascent_triad_role_t role) {
    switch (role) {
        case ASCENT_TRIAD_OPERATOR: return "Operator";
        case ASCENT_TRIAD_RECORDER: return "Recorder";
        case ASCENT_TRIAD_SKEPTIC:  return "Skeptic";
        default: return "Unknown";
    }
}

const char *ascent_pin_type_name(ascent_pin_type_t pin) {
    switch (pin) {
        case ASCENT_PIN_TRANSPARENCY_LOG: return "Transparency Log";
        case ASCENT_PIN_NOTARY:           return "Notary/Affidavit";
        case ASCENT_PIN_DISTRIBUTED:      return "Distributed Mirror";
        case ASCENT_PIN_HUMAN:            return "Human Witness";
        case ASCENT_PIN_LEDGER:           return "Distributed Ledger";
        default: return "Unknown";
    }
}
