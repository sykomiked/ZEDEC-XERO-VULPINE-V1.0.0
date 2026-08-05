/* dual_space.c — ZXV Positive/Negative Space Programming
 *
 * Implements pair registry, artifact binding, linter, event pair
 * lifecycle, S0 quarantine, and runtime admission.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */

#include "dual_space.h"

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
        if (a[i] != b[i]) return false;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static bool digest_eq(const uint8_t *a, const uint8_t *b, uint32_t len) {
    if (!a || !b) return false;
    for (uint32_t i = 0; i < len; i++) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

static void digest_copy(uint8_t *dst, const uint8_t *src, uint32_t len) {
    if (!dst || !src) return;
    for (uint32_t i = 0; i < len; i++)
        dst[i] = src[i];
}

static void add_lint_error(ds_lint_result_t *r, const char *msg) {
    if (r->error_count < 8) {
        copy_str(r->errors[r->error_count], msg, 128);
        r->error_count++;
    }
    r->valid = false;
}

/* ===== Names ===== */

static const char *role_names[] = {
    "source_pos", "source_neg",
    "manifest_pos", "manifest_neg",
    "transform_pos", "transform_neg",
    "container_pos", "container_neg",
    "source_neu", "manifest_neu",
    "transform_neu", "container_neu",
};

static const char *role_extensions[] = {
    ".36n9", ".9n63",
    ".36m9", ".9m63",
    ".zedei", ".iedez",
    ".zedec", ".cedez",
    ".0n0",  ".0m0",
    ".zedez", ".cedec",
};

static const char *inverse_kind_names[] = {
    "exact", "compensating", "restoring",
    "constraining", "observational", "none",
};

static const char *pair_state_names[] = {
    "UNBOUND", "BOUND", "VERIFIED",
    "ADMITTED", "QUARANTINED", "REVOKED",
};

static const char *outcome_names[] = {
    "admitted", "vetoed", "completed",
    "compensated", "unresolved", "quarantined",
};

static const char *lang_tier_names[] = {
    "native_dual", "contract_sidecar",
    "wasm_component", "legacy_binary",
};

static const char *resolution_names[] = {
    "s_plus", "s_minus", "remain_s0",
};

static const char *space_names[] = {
    "positive", "negative", "neutral",
};

const char *ds_role_name(ds_artifact_role_t role) {
    if ((uint32_t)role > DS_ROLE_CONTAINER_NEU) return "unknown";
    return role_names[role];
}

const char *ds_role_extension(ds_artifact_role_t role) {
    if ((uint32_t)role > DS_ROLE_CONTAINER_NEU) return "unknown";
    return role_extensions[role];
}

const char *ds_inverse_kind_name(ds_inverse_kind_t kind) {
    if (kind > DS_INVERSE_NONE) return "unknown";
    return inverse_kind_names[kind];
}

const char *ds_pair_state_name(ds_pair_state_t state) {
    if (state > DS_PAIR_REVOKED) return "unknown";
    return pair_state_names[state];
}

const char *ds_outcome_name(ds_event_outcome_t outcome) {
    if (outcome > DS_OUTCOME_QUARANTINED) return "unknown";
    return outcome_names[outcome];
}

const char *ds_lang_tier_name(ds_lang_tier_t tier) {
    if (tier > DS_LANG_LEGACY_BINARY) return "unknown";
    return lang_tier_names[tier];
}

const char *ds_resolution_name(ds_resolution_t resolution) {
    if (resolution > DS_RESOLUTION_REMAIN_S0) return "unknown";
    return resolution_names[resolution];
}

const char *ds_space_name(ds_space_t space) {
    if (space > DS_SPACE_NEUTRAL) return "unknown";
    return space_names[space];
}

/* ===== Initialization ===== */

void ds_init(ds_registry_t *reg) {
    if (!reg) return;
    ev_memset(reg, 0, sizeof(*reg));
    reg->next_pair_id = 1;
    reg->next_event_id = 1;
}

/* ===== Pair Management ===== */

int32_t ds_register_pair(ds_registry_t *reg, const char *name,
                          const char *dual_pair_id) {
    if (!reg || !name) return -1;

    /* Check for duplicate dual_pair_id */
    if (dual_pair_id && ds_get_pair_by_id(reg, dual_pair_id))
        return -1;

    for (uint32_t i = 0; i < DS_MAX_PAIRS; i++) {
        if (!reg->pairs[i].registered) {
            ev_memset(&reg->pairs[i], 0, sizeof(reg->pairs[i]));
            reg->pairs[i].id = reg->next_pair_id++;
            copy_str(reg->pairs[i].name, name, DS_MAX_NAME_LEN);
            if (dual_pair_id)
                copy_str(reg->pairs[i].dual_pair_id, dual_pair_id, DS_MAX_NAME_LEN);
            reg->pairs[i].state = DS_PAIR_UNBOUND;
            reg->pairs[i].registered = true;
            reg->num_pairs++;
            reg->total_pairs_registered++;
            return (int32_t)i;
        }
    }
    return -1;
}

ds_pair_t *ds_get_pair(ds_registry_t *reg, uint32_t idx) {
    if (!reg || idx >= DS_MAX_PAIRS) return NULL;
    if (!reg->pairs[idx].registered) return NULL;
    return &reg->pairs[idx];
}

ds_pair_t *ds_get_pair_by_id(ds_registry_t *reg, const char *dual_pair_id) {
    if (!reg || !dual_pair_id) return NULL;
    for (uint32_t i = 0; i < DS_MAX_PAIRS; i++) {
        if (reg->pairs[i].registered &&
            str_eq(reg->pairs[i].dual_pair_id, dual_pair_id))
            return &reg->pairs[i];
    }
    return NULL;
}

/* ===== Artifact Management ===== */

static ds_artifact_t *get_artifact(ds_pair_t *pair, bool positive,
                                     ds_artifact_role_t role) {
    if (!pair) return NULL;
    if (role > DS_ROLE_CONTAINER_NEG) return NULL;
    if (positive) {
        return &pair->positive[role / 2];
    } else {
        return &pair->negative[role / 2];
    }
}

/* Get neutral artifact by neutral role (8-11) → index 0-3 */
static ds_artifact_t *get_neutral_artifact(ds_pair_t *pair,
                                            ds_artifact_role_t role) {
    if (!pair) return NULL;
    if (role < DS_ROLE_SOURCE_NEU || role > DS_ROLE_CONTAINER_NEU)
        return NULL;
    return &pair->neutral[role - DS_ROLE_SOURCE_NEU];
}

bool ds_add_artifact(ds_registry_t *reg, uint32_t pair_idx,
                      bool positive, ds_artifact_role_t role,
                      const char *filename, const char *language,
                      ds_lang_tier_t lang_tier) {
    if (!reg) return false;
    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) return false;

    ds_artifact_t *art = get_artifact(pair, positive, role);
    if (!art) return false;

    ev_memset(art, 0, sizeof(*art));
    art->role = role;
    if (filename) copy_str(art->filename, filename, DS_MAX_NAME_LEN);
    if (language) copy_str(art->language, language, DS_MAX_LANG_LEN);
    art->lang_tier = lang_tier;
    art->inverse_kind = DS_INVERSE_NONE;
    art->inverse_classified = false;
    art->generated = false;
    art->registered = true;

    /* Mark presence */
    uint32_t role_idx = role / 2;
    if (positive)
        pair->pos_present[role_idx] = true;
    else
        pair->neg_present[role_idx] = true;

    return true;
}

bool ds_set_artifact_digest(ds_registry_t *reg, uint32_t pair_idx,
                             bool positive, ds_artifact_role_t role,
                             const uint8_t *digest, uint32_t digest_len) {
    if (!reg || !digest) return false;
    if (digest_len != DS_DIGEST_SIZE) return false;
    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) return false;
    ds_artifact_t *art = get_artifact(pair, positive, role);
    if (!art || !art->registered) return false;
    digest_copy(art->content_digest, digest, DS_DIGEST_SIZE);
    return true;
}

bool ds_set_peer_digest(ds_registry_t *reg, uint32_t pair_idx,
                         bool positive, ds_artifact_role_t role,
                         const uint8_t *peer_digest, uint32_t digest_len) {
    if (!reg || !peer_digest) return false;
    if (digest_len != DS_DIGEST_SIZE) return false;
    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) return false;
    ds_artifact_t *art = get_artifact(pair, positive, role);
    if (!art || !art->registered) return false;
    digest_copy(art->peer_digest, peer_digest, DS_DIGEST_SIZE);
    art->has_peer_digest = true;
    return true;
}

bool ds_set_artifact_schemas(ds_registry_t *reg, uint32_t pair_idx,
                              bool positive, ds_artifact_role_t role,
                              const char *source_schema,
                              const char *emitted_schema) {
    if (!reg) return false;
    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) return false;
    ds_artifact_t *art = get_artifact(pair, positive, role);
    if (!art || !art->registered) return false;
    if (source_schema)
        copy_str(art->source_event_schema, source_schema, EV_SCHEMA_LEN);
    if (emitted_schema)
        copy_str(art->emitted_event_schema, emitted_schema, EV_SCHEMA_LEN);
    return true;
}

bool ds_set_inverse_kind(ds_registry_t *reg, uint32_t pair_idx,
                          ds_artifact_role_t role,
                          ds_inverse_kind_t kind, bool generated) {
    if (!reg) return false;
    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) return false;

    /* Inverse kind is set on the negative artifact */
    ds_artifact_t *art = get_artifact(pair, false, role);
    if (!art || !art->registered) return false;

    art->inverse_kind = kind;
    art->inverse_classified = (kind != DS_INVERSE_NONE);
    art->generated = generated;
    return true;
}

/* ===== Capability Management ===== */

bool ds_add_capability(ds_registry_t *reg, uint32_t pair_idx,
                        bool positive, ds_artifact_role_t role,
                        const char *name, bool granted, bool denied) {
    if (!reg || !name) return false;
    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) return false;
    ds_artifact_t *art = get_artifact(pair, positive, role);
    if (!art || !art->registered) return false;
    if (art->num_capabilities >= DS_MAX_CAPABILITIES) return false;

    /* Check for duplicate */
    for (uint32_t i = 0; i < art->num_capabilities; i++) {
        if (str_eq(art->capabilities[i].name, name)) {
            /* Update existing */
            if (granted) art->capabilities[i].granted = true;
            if (denied) art->capabilities[i].denied = true;
            return true;
        }
    }

    ds_capability_t *cap = &art->capabilities[art->num_capabilities++];
    copy_str(cap->name, name, DS_MAX_NAME_LEN);
    cap->granted = granted;
    cap->denied = denied;
    return true;
}

/* ===== Neutral Artifact Management ===== */

bool ds_add_neutral_artifact(ds_registry_t *reg, uint32_t pair_idx,
                              ds_artifact_role_t role,
                              const char *filename, const char *language,
                              ds_lang_tier_t lang_tier) {
    if (!reg) return false;
    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) return false;

    ds_artifact_t *art = get_neutral_artifact(pair, role);
    if (!art) return false;

    ev_memset(art, 0, sizeof(*art));
    art->role = role;
    if (filename) copy_str(art->filename, filename, DS_MAX_NAME_LEN);
    if (language) copy_str(art->language, language, DS_MAX_LANG_LEN);
    art->lang_tier = lang_tier;
    art->inverse_kind = DS_INVERSE_NONE;
    art->inverse_classified = false;
    art->generated = false;
    art->registered = true;

    uint32_t role_idx = role - DS_ROLE_SOURCE_NEU;
    pair->neu_present[role_idx] = true;
    pair->has_neutral = true;
    return true;
}

bool ds_set_neutral_digest(ds_registry_t *reg, uint32_t pair_idx,
                            ds_artifact_role_t role,
                            const uint8_t *digest, uint32_t digest_len) {
    if (!reg || !digest) return false;
    if (digest_len != DS_DIGEST_SIZE) return false;
    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) return false;
    ds_artifact_t *art = get_neutral_artifact(pair, role);
    if (!art || !art->registered) return false;
    digest_copy(art->content_digest, digest, DS_DIGEST_SIZE);
    return true;
}

bool ds_set_neutral_peer_digest(ds_registry_t *reg, uint32_t pair_idx,
                                 ds_artifact_role_t role,
                                 const uint8_t *peer_digest, uint32_t digest_len) {
    if (!reg || !peer_digest) return false;
    if (digest_len != DS_DIGEST_SIZE) return false;
    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) return false;
    ds_artifact_t *art = get_neutral_artifact(pair, role);
    if (!art || !art->registered) return false;
    digest_copy(art->peer_digest, peer_digest, DS_DIGEST_SIZE);
    art->has_peer_digest = true;
    return true;
}

bool ds_set_neutral_schemas(ds_registry_t *reg, uint32_t pair_idx,
                             ds_artifact_role_t role,
                             const char *source_schema,
                             const char *emitted_schema) {
    if (!reg) return false;
    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) return false;
    ds_artifact_t *art = get_neutral_artifact(pair, role);
    if (!art || !art->registered) return false;
    if (source_schema)
        copy_str(art->source_event_schema, source_schema, EV_SCHEMA_LEN);
    if (emitted_schema)
        copy_str(art->emitted_event_schema, emitted_schema, EV_SCHEMA_LEN);
    return true;
}

bool ds_add_neutral_capability(ds_registry_t *reg, uint32_t pair_idx,
                                ds_artifact_role_t role,
                                const char *name, bool granted, bool denied) {
    if (!reg || !name) return false;
    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) return false;
    ds_artifact_t *art = get_neutral_artifact(pair, role);
    if (!art || !art->registered) return false;
    if (art->num_capabilities >= DS_MAX_CAPABILITIES) return false;

    for (uint32_t i = 0; i < art->num_capabilities; i++) {
        if (str_eq(art->capabilities[i].name, name)) {
            if (granted) art->capabilities[i].granted = true;
            if (denied) art->capabilities[i].denied = true;
            return true;
        }
    }

    ds_capability_t *cap = &art->capabilities[art->num_capabilities++];
    copy_str(cap->name, name, DS_MAX_NAME_LEN);
    cap->granted = granted;
    cap->denied = denied;
    return true;
}

/* ===== Triad Binding ===== */

bool ds_bind_triad(ds_registry_t *reg, uint32_t pair_idx,
                    ds_artifact_role_t role) {
    if (!reg) return false;
    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) return false;

    /* role must be a positive role (even number 0-6) */
    if (role > DS_ROLE_CONTAINER_NEG) return false;
    uint32_t role_idx = role / 2;
    if (role_idx > 3) return false;

    ds_artifact_t *pos = &pair->positive[role_idx];
    ds_artifact_t *neg = &pair->negative[role_idx];
    ds_artifact_t *neu = &pair->neutral[role_idx];

    if (!pos->registered || !neg->registered) return false;
    /* Neutral is optional for binding — if present, must be bound too */

    /* Check S+ ↔ S- cross-binding */
    if (!pos->has_peer_digest || !neg->has_peer_digest) return false;
    if (!digest_eq(pos->peer_digest, neg->content_digest, DS_DIGEST_SIZE))
        return false;
    if (!digest_eq(neg->peer_digest, pos->content_digest, DS_DIGEST_SIZE))
        return false;

    /* If neutral is present, check S0 ↔ S+ and S0 ↔ S- cross-binding */
    if (neu->registered) {
        if (!neu->has_peer_digest) return false;
        /* S0 peer digest should match S+ content digest */
        if (!digest_eq(neu->peer_digest, pos->content_digest, DS_DIGEST_SIZE))
            return false;
        /* S+ peer digest should also match S0 content digest if S0 present */
        /* We need to check that pos has a digest matching neu's content.
         * However, pos->peer_digest already points to neg's content.
         * For triads, we need a 3-way binding. The approach: each artifact
         * has one peer_digest. For triads, we use a different approach:
         * the neutral artifact's peer_digest = S+ content_digest,
         * and we verify S0 content_digest is referenced by both S+ and S-.
         *
         * Simplified: for triad binding, we verify:
         * - S+.peer == S-.content  (existing pair binding)
         * - S-.peer == S+.content  (existing pair binding)
         * - S0.peer == S+.content  (neutral references positive)
         * And we accept the binding if S0 is present and its peer matches S+.
         * Full 3-way Merkle binding is done at container level. */
    }

    /* Check if all 4 triads are fully bound */
    bool all_bound = true;
    for (uint32_t i = 0; i < 4; i++) {
        if (!pair->pos_present[i] || !pair->neg_present[i]) {
            all_bound = false;
            break;
        }
        ds_artifact_t *p = &pair->positive[i];
        ds_artifact_t *n = &pair->negative[i];
        if (!p->has_peer_digest || !n->has_peer_digest) {
            all_bound = false;
            break;
        }
        if (!digest_eq(p->peer_digest, n->content_digest, DS_DIGEST_SIZE) ||
            !digest_eq(n->peer_digest, p->content_digest, DS_DIGEST_SIZE)) {
            all_bound = false;
            break;
        }
        /* If neutral present, verify its binding too */
        if (pair->neu_present[i]) {
            ds_artifact_t *ne = &pair->neutral[i];
            if (!ne->has_peer_digest) {
                all_bound = false;
                break;
            }
            if (!digest_eq(ne->peer_digest, p->content_digest, DS_DIGEST_SIZE)) {
                all_bound = false;
                break;
            }
        }
    }

    if (all_bound && pair->state == DS_PAIR_UNBOUND) {
        pair->state = DS_PAIR_BOUND;
    }

    return true;
}

/* ===== Triad Completeness Check ===== */

bool ds_check_triad_completeness(ds_registry_t *reg, uint32_t pair_idx) {
    if (!reg) return false;
    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) return false;

    /* Only enforce triad completeness when neutral is declared.
     * Pairs without any neutral artifacts are backward-compatible
     * dual-space pairs and pass this check. */
    if (!pair->has_neutral) return true;

    for (uint32_t i = 0; i < 4; i++) {
        bool pos = pair->pos_present[i];
        bool neg = pair->neg_present[i];
        bool neu = pair->neu_present[i];

        /* If neutral is present for this role, pos and neg must also
         * be present (or exemption). If neutral is absent for this
         * role but the pair declares neutral, it's only a violation
         * if some roles have neutral and others don't. */
        if (neu && (!pos || !neg)) {
            if (!pair->has_exemption) return false;
        }
    }
    return true;
}

/* ===== S0 Capability Enforcement ===== */

bool ds_check_s0_no_capabilities(ds_registry_t *reg, uint32_t pair_idx) {
    if (!reg) return false;
    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) return false;

    /* S0 artifacts MUST NOT have any granted (production effect) capabilities */
    for (uint32_t r = 0; r < 4; r++) {
        ds_artifact_t *neu = &pair->neutral[r];
        if (!neu->registered) continue;
        for (uint32_t c = 0; c < neu->num_capabilities; c++) {
            if (neu->capabilities[c].granted) {
                /* S0 has a production capability — violation */
                return false;
            }
        }
    }
    return true;
}

/* ===== Neutral Resolution ===== */

bool ds_resolve_neutral(ds_registry_t *reg, uint32_t pair_idx,
                         ds_resolution_t destination,
                         const char *policy_id, uint32_t policy_version,
                         const char *resolver_id, const char *reason_code,
                         bool signed_attestation,
                         uint64_t current_sequence) {
    if (!reg) return false;
    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) return false;
    if (!pair->has_neutral) return false;

    ds_neutral_resolution_t *res = &pair->neutral_resolution;
    res->destination = destination;
    if (policy_id) copy_str(res->policy_id, policy_id, DS_MAX_NAME_LEN);
    res->policy_version = policy_version;
    if (resolver_id) copy_str(res->resolver_id, resolver_id, DS_MAX_NAME_LEN);
    res->causal_lineage = current_sequence;
    if (reason_code) copy_str(res->reason_code, reason_code, DS_MAX_REASONS);
    res->signed_attestation = signed_attestation;
    res->resolved = true;
    res->timed_out = false;

    reg->total_s0_resolutions++;

    /* If resolution is to S+ and pair is quarantined, transition to verified */
    if (destination == DS_RESOLUTION_S_PLUS &&
        pair->state == DS_PAIR_QUARANTINED) {
        /* Re-verify after resolution */
        ds_lint_result_t lint = ds_lint_pair(reg, pair_idx);
        if (lint.valid) {
            pair->state = DS_PAIR_VERIFIED;
            reg->total_pairs_verified++;
        }
    }

    /* If resolution is to S-, veto the pair */
    if (destination == DS_RESOLUTION_S_MINUS) {
        pair->state = DS_PAIR_QUARANTINED;
        if (reg->num_quarantined < DS_MAX_QUARANTINE) {
            reg->quarantined_indices[reg->num_quarantined++] = pair_idx;
        }
    }

    return true;
}

bool ds_set_neutral_timeout(ds_registry_t *reg, uint32_t pair_idx,
                             uint64_t timeout_sequence) {
    if (!reg) return false;
    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) return false;
    if (!pair->has_neutral) return false;

    pair->neutral_resolution.timeout_sequence = timeout_sequence;
    return true;
}

bool ds_check_neutral_timeout(ds_registry_t *reg, uint32_t pair_idx,
                               uint64_t current_sequence) {
    if (!reg) return false;
    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) return false;
    if (!pair->has_neutral) return false;

    ds_neutral_resolution_t *res = &pair->neutral_resolution;
    if (res->resolved) return false;  /* already resolved */
    if (res->timeout_sequence == 0) return false;  /* no timeout set */

    if (current_sequence >= res->timeout_sequence) {
        /* Timeout: remain in S0, shed capabilities, mark timed out */
        res->timed_out = true;
        res->destination = DS_RESOLUTION_REMAIN_S0;
        res->resolved = true;

        /* Shed all S0 capabilities */
        for (uint32_t r = 0; r < 4; r++) {
            ds_artifact_t *neu = &pair->neutral[r];
            if (!neu->registered) continue;
            for (uint32_t c = 0; c < neu->num_capabilities; c++) {
                neu->capabilities[c].granted = false;
            }
        }

        /* Ensure pair is quarantined */
        if (pair->state != DS_PAIR_QUARANTINED) {
            ds_quarantine_pair(reg, pair_idx);
        }

        reg->total_s0_timeouts++;
        return true;
    }
    return false;
}

/* ===== Triad Event Management ===== */

int32_t ds_create_triad_event(ds_registry_t *reg, uint32_t pair_idx,
                               uint64_t event_id, uint64_t causal_parent,
                               uint32_t phase, uint64_t logical_sequence,
                               const char *positive_type,
                               const char *negative_type,
                               const char *neutral_type) {
    if (!reg || !positive_type || !negative_type) return -1;
    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) return -1;

    if (pair->state == DS_PAIR_QUARANTINED || pair->state == DS_PAIR_REVOKED)
        return -1;

    for (uint32_t i = 0; i < DS_MAX_EVENTS; i++) {
        if (!reg->events[i].active) {
            ev_memset(&reg->events[i], 0, sizeof(reg->events[i]));
            reg->events[i].id = reg->next_event_id++;
            reg->events[i].pair_idx = pair_idx;
            reg->events[i].event_id = event_id;
            reg->events[i].causal_parent = causal_parent;
            reg->events[i].phase = phase;
            reg->events[i].logical_sequence = logical_sequence;
            copy_str(reg->events[i].positive_event_type, positive_type, EV_SCHEMA_LEN);
            copy_str(reg->events[i].negative_event_type, negative_type, EV_SCHEMA_LEN);
            if (neutral_type)
                copy_str(reg->events[i].neutral_event_type, neutral_type, EV_SCHEMA_LEN);
            reg->events[i].outcome = DS_OUTCOME_UNRESOLVED;
            reg->events[i].s_guard_passed = false;
            reg->events[i].s_compensation_invoked = false;
            reg->events[i].s0_deferred = false;
            reg->events[i].s0_resolved = false;
            reg->events[i].s0_resolution = DS_RESOLUTION_REMAIN_S0;
            reg->events[i].active = true;
            reg->num_events++;
            return (int32_t)i;
        }
    }
    return -1;
}

/* ===== Defer Event to S0 ===== */

bool ds_defer_to_neutral(ds_registry_t *reg, uint32_t event_idx,
                          uint64_t current_sequence) {
    if (!reg) return false;
    ds_event_pair_t *evt = ds_get_event(reg, event_idx);
    if (!evt) return false;

    (void)current_sequence;

    ds_pair_t *pair = ds_get_pair(reg, evt->pair_idx);
    if (!pair) return false;
    if (!pair->has_neutral) return false;

    evt->s0_deferred = true;
    evt->outcome = DS_OUTCOME_UNRESOLVED;
    reg->total_s0_deferred++;
    return true;
}

/* ===== Resolve Deferred Event ===== */

ds_event_outcome_t ds_resolve_event(ds_registry_t *reg, uint32_t event_idx,
                                     ds_resolution_t destination,
                                     uint64_t current_sequence) {
    if (!reg) return DS_OUTCOME_QUARANTINED;
    ds_event_pair_t *evt = ds_get_event(reg, event_idx);
    if (!evt) return DS_OUTCOME_QUARANTINED;
    if (!evt->s0_deferred) return DS_OUTCOME_QUARANTINED;

    evt->s0_resolved = true;
    evt->s0_resolution = destination;
    evt->timestamp_sequence = current_sequence;

    ds_pair_t *pair = ds_get_pair(reg, evt->pair_idx);
    if (!pair) return DS_OUTCOME_QUARANTINED;

    reg->total_s0_resolutions++;

    switch (destination) {
        case DS_RESOLUTION_S_PLUS:
            /* Resolve to positive — admit the event */
            evt->outcome = DS_OUTCOME_ADMITTED;
            pair->total_events_admitted++;
            reg->total_events_processed++;
            return DS_OUTCOME_ADMITTED;

        case DS_RESOLUTION_S_MINUS:
            /* Resolve to negative — veto */
            evt->outcome = DS_OUTCOME_VETOED;
            pair->total_events_vetoed++;
            reg->total_vetoes++;
            reg->total_events_processed++;
            return DS_OUTCOME_VETOED;

        case DS_RESOLUTION_REMAIN_S0:
            /* Stay unresolved — quarantine */
            evt->outcome = DS_OUTCOME_QUARANTINED;
            pair->total_events_quarantined++;
            reg->total_quarantine_events++;
            return DS_OUTCOME_QUARANTINED;

        default:
            return DS_OUTCOME_QUARANTINED;
    }
}

/* ===== Pair Binding ===== */

bool ds_bind_pair(ds_registry_t *reg, uint32_t pair_idx,
                   ds_artifact_role_t role) {
    if (!reg) return false;
    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) return false;

    uint32_t role_idx = role / 2;
    ds_artifact_t *pos = &pair->positive[role_idx];
    ds_artifact_t *neg = &pair->negative[role_idx];

    if (!pos->registered || !neg->registered) return false;
    if (!pos->has_peer_digest || !neg->has_peer_digest) return false;

    /* Verify cross-binding: pos.peer_digest == neg.content_digest
     * and neg.peer_digest == pos.content_digest */
    if (!digest_eq(pos->peer_digest, neg->content_digest, DS_DIGEST_SIZE))
        return false;
    if (!digest_eq(neg->peer_digest, pos->content_digest, DS_DIGEST_SIZE))
        return false;

    /* If all 4 pairs are present and bound, transition to BOUND */
    bool all_bound = true;
    for (uint32_t i = 0; i < 4; i++) {
        if (!pair->pos_present[i] || !pair->neg_present[i]) {
            /* Both sides must be present for a complete pair */
            all_bound = false;
            break;
        }
        ds_artifact_t *p = &pair->positive[i];
        ds_artifact_t *n = &pair->negative[i];
        if (!p->has_peer_digest || !n->has_peer_digest) {
            all_bound = false;
            break;
        }
        if (!digest_eq(p->peer_digest, n->content_digest, DS_DIGEST_SIZE) ||
            !digest_eq(n->peer_digest, p->content_digest, DS_DIGEST_SIZE)) {
            all_bound = false;
            break;
        }
    }

    if (all_bound && pair->state == DS_PAIR_UNBOUND) {
        pair->state = DS_PAIR_BOUND;
    }

    return true;
}

/* ===== Linter ===== */

bool ds_check_capability_asymmetry(ds_registry_t *reg, uint32_t pair_idx) {
    if (!reg) return false;
    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) return false;

    /* For each role pair, check that S- denied capabilities are a subset
     * of S+ granted capabilities. S- MUST NOT receive more capabilities
     * than S+. This means every capability that S- has (granted=true)
     * must also be granted by S+. */
    for (uint32_t r = 0; r < 4; r++) {
        ds_artifact_t *pos = &pair->positive[r];
        ds_artifact_t *neg = &pair->negative[r];
        if (!pos->registered || !neg->registered) continue;

        /* Check each negative capability */
        for (uint32_t i = 0; i < neg->num_capabilities; i++) {
            if (neg->capabilities[i].granted) {
                /* S- grants this capability — S+ must also grant it */
                bool found_in_pos = false;
                for (uint32_t j = 0; j < pos->num_capabilities; j++) {
                    if (str_eq(pos->capabilities[j].name,
                               neg->capabilities[i].name) &&
                        pos->capabilities[j].granted) {
                        found_in_pos = true;
                        break;
                    }
                }
                if (!found_in_pos) return false;
            }
        }
    }
    return true;
}

bool ds_check_pair_completeness(ds_registry_t *reg, uint32_t pair_idx) {
    if (!reg) return false;
    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) return false;

    for (uint32_t i = 0; i < 4; i++) {
        if (pair->pos_present[i] && !pair->neg_present[i]) {
            /* Missing negative side — need exemption */
            if (!pair->has_exemption) return false;
        }
        if (!pair->pos_present[i] && pair->neg_present[i]) {
            /* Missing positive side — need exemption */
            if (!pair->has_exemption) return false;
        }
    }
    return true;
}

ds_lint_result_t ds_lint_pair(ds_registry_t *reg, uint32_t pair_idx) {
    ds_lint_result_t result;
    ev_memset(&result, 0, sizeof(result));
    result.valid = true;

    if (!reg) {
        add_lint_error(&result, "null registry");
        return result;
    }

    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) {
        add_lint_error(&result, "pair not found");
        return result;
    }

    /* Check 1: peer digests match */
    result.peer_digests_match = true;
    for (uint32_t r = 0; r < 4; r++) {
        ds_artifact_t *pos = &pair->positive[r];
        ds_artifact_t *neg = &pair->negative[r];
        if (!pos->registered || !neg->registered) continue;

        if (!pos->has_peer_digest || !neg->has_peer_digest) {
            result.peer_digests_match = false;
            add_lint_error(&result, "missing peer digest");
            continue;
        }

        if (!digest_eq(pos->peer_digest, neg->content_digest, DS_DIGEST_SIZE) ||
            !digest_eq(neg->peer_digest, pos->content_digest, DS_DIGEST_SIZE)) {
            result.peer_digests_match = false;
            add_lint_error(&result, "peer digest mismatch");
        }
    }

    /* Check 2: capability asymmetry — S- caps <= S+ caps */
    result.capability_asymmetry_ok = ds_check_capability_asymmetry(reg, pair_idx);
    if (!result.capability_asymmetry_ok) {
        add_lint_error(&result, "S- has broader capabilities than S+");
    }

    /* Check 3: inverse classification — all S- artifacts must have
     * an inverse kind classified */
    result.inverse_classified = true;
    for (uint32_t r = 0; r < 4; r++) {
        ds_artifact_t *neg = &pair->negative[r];
        if (!neg->registered) continue;
        if (!neg->inverse_classified) {
            result.inverse_classified = false;
            add_lint_error(&result, "S- artifact missing inverse classification");
        }
    }

    /* Check 4: all pairs present or exempted */
    result.all_pairs_present = ds_check_pair_completeness(reg, pair_idx);
    if (!result.all_pairs_present) {
        add_lint_error(&result, "missing pair without exemption");
    }

    /* Check 5: generated S- not misrepresented as proven inverse */
    result.no_generated_misrepresented = true;
    for (uint32_t r = 0; r < 4; r++) {
        ds_artifact_t *neg = &pair->negative[r];
        if (!neg->registered) continue;
        if (neg->generated && neg->inverse_kind == DS_INVERSE_EXACT) {
            result.no_generated_misrepresented = false;
            add_lint_error(&result,
                "generated S- claims exact inverse — must use compensating/constraining");
        }
    }

    /* Check 6: triad digests match (if neutral artifacts present) */
    result.triad_digests_match = true;
    if (pair->has_neutral) {
        for (uint32_t r = 0; r < 4; r++) {
            ds_artifact_t *neu = &pair->neutral[r];
            if (!neu->registered) continue;
            if (!neu->has_peer_digest) {
                result.triad_digests_match = false;
                add_lint_error(&result, "S0 artifact missing peer digest");
                continue;
            }
            ds_artifact_t *pos = &pair->positive[r];
            if (pos->registered &&
                !digest_eq(neu->peer_digest, pos->content_digest, DS_DIGEST_SIZE)) {
                result.triad_digests_match = false;
                add_lint_error(&result, "S0 peer digest does not match S+ content");
            }
        }
    }

    /* Check 7: S0 MUST NOT have production effect capabilities */
    result.s0_no_production_caps = ds_check_s0_no_capabilities(reg, pair_idx);
    if (!result.s0_no_production_caps) {
        add_lint_error(&result, "S0 has production effect capabilities — violation");
    }

    /* Check 8: triad completeness (if neutral is present) */
    result.all_triads_present = ds_check_triad_completeness(reg, pair_idx);
    if (!result.all_triads_present) {
        add_lint_error(&result, "missing triad member without exemption");
    }

    return result;
}

/* ===== Verify Pair ===== */

bool ds_verify_pair(ds_registry_t *reg, uint32_t pair_idx) {
    if (!reg) return false;
    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) return false;

    ds_lint_result_t lint = ds_lint_pair(reg, pair_idx);
    if (!lint.valid) {
        /* Lint failed — quarantine if not already */
        if (pair->state != DS_PAIR_QUARANTINED) {
            ds_quarantine_pair(reg, pair_idx);
        }
        return false;
    }

    if (pair->state == DS_PAIR_BOUND || pair->state == DS_PAIR_UNBOUND) {
        pair->state = DS_PAIR_VERIFIED;
        reg->total_pairs_verified++;
    }
    return true;
}

/* ===== Quarantine ===== */

bool ds_quarantine_pair(ds_registry_t *reg, uint32_t pair_idx) {
    if (!reg) return false;
    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) return false;

    pair->state = DS_PAIR_QUARANTINED;

    /* Add to quarantine list */
    if (reg->num_quarantined < DS_MAX_QUARANTINE) {
        reg->quarantined_indices[reg->num_quarantined++] = pair_idx;
    } else {
        /* Overwrite oldest */
        reg->quarantined_indices[0] = pair_idx;
    }

    reg->total_quarantine_events++;
    return true;
}

/* ===== Revoke ===== */

bool ds_revoke_pair(ds_registry_t *reg, uint32_t pair_idx) {
    if (!reg) return false;
    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) return false;

    pair->state = DS_PAIR_REVOKED;
    reg->total_pairs_revoked++;
    return true;
}

/* ===== Admission ===== */

bool ds_admit_pair(ds_registry_t *reg, uint32_t pair_idx) {
    if (!reg) return false;
    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) return false;

    /* Must be verified first */
    if (pair->state != DS_PAIR_VERIFIED) {
        /* Try to verify first */
        if (!ds_verify_pair(reg, pair_idx)) return false;
        pair = ds_get_pair(reg, pair_idx);
        if (!pair || pair->state != DS_PAIR_VERIFIED) return false;
    }

    /* Check capability asymmetry again at admission time */
    if (!ds_check_capability_asymmetry(reg, pair_idx)) return false;

    /* Check completeness */
    if (!ds_check_pair_completeness(reg, pair_idx)) return false;

    /* Check S0 has no production capabilities */
    if (!ds_check_s0_no_capabilities(reg, pair_idx)) return false;

    /* Check triad completeness if neutral is present */
    if (pair->has_neutral && !ds_check_triad_completeness(reg, pair_idx))
        return false;

    pair->state = DS_PAIR_ADMITTED;
    reg->total_pairs_admitted++;
    return true;
}

/* ===== Event Pair Management ===== */

int32_t ds_create_event(ds_registry_t *reg, uint32_t pair_idx,
                         uint64_t event_id, uint64_t causal_parent,
                         uint32_t phase, uint64_t logical_sequence,
                         const char *positive_type,
                         const char *negative_type) {
    if (!reg || !positive_type || !negative_type) return -1;
    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) return -1;

    /* Pair must not be quarantined or revoked */
    if (pair->state == DS_PAIR_QUARANTINED || pair->state == DS_PAIR_REVOKED) {
        return -1;
    }

    for (uint32_t i = 0; i < DS_MAX_EVENTS; i++) {
        if (!reg->events[i].active) {
            ev_memset(&reg->events[i], 0, sizeof(reg->events[i]));
            reg->events[i].id = reg->next_event_id++;
            reg->events[i].pair_idx = pair_idx;
            reg->events[i].event_id = event_id;
            reg->events[i].causal_parent = causal_parent;
            reg->events[i].phase = phase;
            reg->events[i].logical_sequence = logical_sequence;
            copy_str(reg->events[i].positive_event_type, positive_type, EV_SCHEMA_LEN);
            copy_str(reg->events[i].negative_event_type, negative_type, EV_SCHEMA_LEN);
            reg->events[i].outcome = DS_OUTCOME_UNRESOLVED;
            reg->events[i].s_guard_passed = false;
            reg->events[i].s_compensation_invoked = false;
            reg->events[i].active = true;
            reg->num_events++;
            return (int32_t)i;
        }
    }
    return -1;
}

ds_event_pair_t *ds_get_event(ds_registry_t *reg, uint32_t event_idx) {
    if (!reg || event_idx >= DS_MAX_EVENTS) return NULL;
    if (!reg->events[event_idx].active) return NULL;
    return &reg->events[event_idx];
}

/* ===== S- Guard Evaluation ===== */

bool ds_evaluate_guard(ds_registry_t *reg, uint32_t event_idx) {
    if (!reg) return false;
    ds_event_pair_t *evt = ds_get_event(reg, event_idx);
    if (!evt) return false;

    ds_pair_t *pair = ds_get_pair(reg, evt->pair_idx);
    if (!pair) return false;

    /* If pair is quarantined, guard fails */
    if (pair->state == DS_PAIR_QUARANTINED || pair->state == DS_PAIR_REVOKED) {
        return false;
    }

    /* If pair is not admitted, guard fails for production events */
    if (pair->state != DS_PAIR_ADMITTED && pair->state != DS_PAIR_VERIFIED) {
        return false;
    }

    /* Check if any S- artifact denies this event type */
    for (uint32_t r = 0; r < 4; r++) {
        ds_artifact_t *neg = &pair->negative[r];
        if (!neg->registered) continue;
        if (neg->emitted_event_schema[0] == '\0') continue;

        /* If the negative event type matches and the artifact has
         * constraining inverse kind, check for denials */
        if (str_eq(neg->emitted_event_schema, evt->negative_event_type)) {
            /* Check if any capability is denied */
            for (uint32_t c = 0; c < neg->num_capabilities; c++) {
                if (neg->capabilities[c].denied) {
                    /* This event is denied by the S- contract */
                    return false;
                }
            }
        }
    }

    return true;
}

/* ===== Admit Event ===== */

ds_event_outcome_t ds_admit_event(ds_registry_t *reg, uint32_t event_idx,
                                    uint64_t current_sequence) {
    if (!reg) return DS_OUTCOME_QUARANTINED;
    ds_event_pair_t *evt = ds_get_event(reg, event_idx);
    if (!evt) return DS_OUTCOME_QUARANTINED;

    evt->timestamp_sequence = current_sequence;

    /* Evaluate S- guard */
    evt->s_guard_passed = ds_evaluate_guard(reg, event_idx);

    if (!evt->s_guard_passed) {
        evt->outcome = DS_OUTCOME_VETOED;
        ds_pair_t *pair = ds_get_pair(reg, evt->pair_idx);
        if (pair) pair->total_events_vetoed++;
        reg->total_vetoes++;
        reg->total_events_processed++;
        return DS_OUTCOME_VETOED;
    }

    evt->outcome = DS_OUTCOME_ADMITTED;
    ds_pair_t *pair = ds_get_pair(reg, evt->pair_idx);
    if (pair) pair->total_events_admitted++;
    reg->total_events_processed++;
    return DS_OUTCOME_ADMITTED;
}

/* ===== Complete Event ===== */

bool ds_complete_event(ds_registry_t *reg, uint32_t event_idx,
                        bool success, uint64_t current_sequence) {
    if (!reg) return false;
    ds_event_pair_t *evt = ds_get_event(reg, event_idx);
    if (!evt) return false;

    (void)current_sequence;

    if (evt->outcome != DS_OUTCOME_ADMITTED) return false;

    if (success) {
        evt->outcome = DS_OUTCOME_COMPLETED;
    } else {
        /* Failure — trigger compensation */
        return ds_compensate_event(reg, event_idx, current_sequence);
    }

    return true;
}

/* ===== Compensate Event ===== */

bool ds_compensate_event(ds_registry_t *reg, uint32_t event_idx,
                          uint64_t current_sequence) {
    if (!reg) return false;
    ds_event_pair_t *evt = ds_get_event(reg, event_idx);
    if (!evt) return false;

    (void)current_sequence;

    ds_pair_t *pair = ds_get_pair(reg, evt->pair_idx);
    if (!pair) return false;

    /* Find the S- artifact that handles compensation for this event type */
    bool can_compensate = false;
    for (uint32_t r = 0; r < 4; r++) {
        ds_artifact_t *neg = &pair->negative[r];
        if (!neg->registered) continue;
        if (neg->inverse_kind == DS_INVERSE_COMPENSATING ||
            neg->inverse_kind == DS_INVERSE_RESTORING ||
            neg->inverse_kind == DS_INVERSE_EXACT) {
            /* This artifact can compensate */
            can_compensate = true;
            break;
        }
    }

    evt->s_compensation_invoked = true;

    if (can_compensate) {
        evt->outcome = DS_OUTCOME_COMPENSATED;
        pair->total_events_compensated++;
        reg->total_compensations++;
        return true;
    } else {
        /* Cannot compensate — quarantine */
        evt->outcome = DS_OUTCOME_QUARANTINED;
        pair->total_events_quarantined++;
        reg->total_quarantine_events++;
        return ds_quarantine_pair(reg, evt->pair_idx);
    }
}

/* ===== Quarantine Event ===== */

bool ds_quarantine_event(ds_registry_t *reg, uint32_t event_idx,
                          uint64_t current_sequence) {
    if (!reg) return false;
    ds_event_pair_t *evt = ds_get_event(reg, event_idx);
    if (!evt) return false;

    (void)current_sequence;

    evt->outcome = DS_OUTCOME_QUARANTINED;

    ds_pair_t *pair = ds_get_pair(reg, evt->pair_idx);
    if (pair) {
        pair->total_events_quarantined++;
    }
    reg->total_quarantine_events++;

    /* Also quarantine the pair */
    return ds_quarantine_pair(reg, evt->pair_idx);
}

/* ===== Utility ===== */

uint32_t ds_get_pairs_in_state(ds_registry_t *reg, ds_pair_state_t state,
                                uint32_t *out_indices, uint32_t max_out) {
    if (!reg || !out_indices) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < DS_MAX_PAIRS && count < max_out; i++) {
        if (reg->pairs[i].registered && reg->pairs[i].state == state) {
            out_indices[count++] = i;
        }
    }
    return count;
}

/* ===== v2.0 Triad Identity API ===== */

bool ds_set_triad_id(ds_registry_t *reg, uint32_t pair_idx,
                     const char *triad_id) {
    if (!reg || !triad_id) return false;
    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) return false;
    copy_str(pair->triad_id, triad_id, DS_MAX_NAME_LEN);
    return true;
}

bool ds_compute_pair_digests(ds_registry_t *reg, uint32_t pair_idx) {
    if (!reg) return false;
    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) return false;

    /* Simple Merkle-like computation: XOR all artifact content digests
     * per space. A real implementation would use a proper hash function,
     * but this is freestanding with no crypto library available. */
    ev_memset(pair->positive_digest, 0, DS_DIGEST_SIZE);
    ev_memset(pair->negative_digest, 0, DS_DIGEST_SIZE);
    ev_memset(pair->neutral_digest, 0, DS_DIGEST_SIZE);

    for (uint32_t r = 0; r < 4; r++) {
        if (pair->pos_present[r]) {
            for (uint32_t b = 0; b < DS_DIGEST_SIZE; b++)
                pair->positive_digest[b] ^= pair->positive[r].content_digest[b];
        }
        if (pair->neg_present[r]) {
            for (uint32_t b = 0; b < DS_DIGEST_SIZE; b++)
                pair->negative_digest[b] ^= pair->negative[r].content_digest[b];
        }
        if (pair->neu_present[r]) {
            for (uint32_t b = 0; b < DS_DIGEST_SIZE; b++)
                pair->neutral_digest[b] ^= pair->neutral[r].content_digest[b];
        }
    }

    pair->has_pair_digests = true;
    return true;
}

bool ds_set_release_signature(ds_registry_t *reg, uint32_t pair_idx,
                              const uint8_t *signature, uint32_t sig_len) {
    if (!reg || !signature) return false;
    if (sig_len > DS_DIGEST_SIZE) return false;
    ds_pair_t *pair = ds_get_pair(reg, pair_idx);
    if (!pair) return false;
    ev_memset(pair->release_signature, 0, DS_DIGEST_SIZE);
    ev_memcpy(pair->release_signature, signature, sig_len);
    pair->has_release_signature = true;
    return true;
}

bool ds_set_event_triad_id(ds_registry_t *reg, uint32_t event_idx,
                           const char *triad_id) {
    if (!reg || !triad_id) return false;
    if (event_idx >= DS_MAX_EVENTS) return false;
    ds_event_pair_t *evt = &reg->events[event_idx];
    if (!evt->active) return false;
    copy_str(evt->triad_id, triad_id, DS_MAX_NAME_LEN);
    return true;
}
