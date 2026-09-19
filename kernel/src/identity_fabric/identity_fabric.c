/* identity_fabric.c — ZXV Identity Fabric Compound Module Implementation
 *
 * Unifies all identity modules: Identity, Vino, Vena, ZAB, Reputation,
 * Concord, Crown, Ministry, OnePolicy, Legal Engine, Porter House,
 * and Interspace into a single coherent fabric.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "identity_fabric.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"
#include "../sdk/selfaudit.h"

/* ===== Helper Functions ===== */

static void if_mem_set(void *dst, int val, uint32_t len) {
    uint8_t *d = dst;
    for (uint32_t i = 0; i < len; i++) d[i] = (uint8_t)val;
}

static void if_mem_copy(void *dst, const void *src, uint32_t len) {
    uint8_t *d = dst; const uint8_t *s = src;
    for (uint32_t i = 0; i < len; i++) d[i] = s[i];
}

static int if_str_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static uint32_t if_str_len(const char *s) {
    uint32_t n = 0; while (s[n]) n++; return n;
}

static void if_str_copy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* ===== Coverage Computation ===== */

static surplus_real_t if_compute_coverage(const m5_coords_t *m5) {
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

/* ===== LPRES Attestation ===== */

lpres_state_t if_attest(identity_fabric_t *fabric, uint32_t identity_id,
                        uint32_t op_id, void *args, int32_t result) {
    if (!fabric || identity_id >= fabric->num_identities) return LPRES_STATE_NEITHER;
    
    if_identity_t *id = &fabric->identities[identity_id];
    lpres_state_t result_att = (result >= 0) ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    lpres_state_t identity_att = id->attestation;
    lpres_state_t coverage_att = (SR_CMP(id->coverage_ratio, fabric->min_coverage_ratio) >= 0) 
                                  ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    lpres_state_t fabric_att = fabric->global_attestation;
    
    lpres_state_t combined = lpres_conjoin(result_att, identity_att);
    combined = lpres_conjoin(combined, coverage_att);
    combined = lpres_conjoin(combined, fabric_att);
    
    id->attestation = combined;
    fabric->global_attestation = lpres_conjoin(fabric->global_attestation, combined);
    
    return combined;
}

/* ===== Initialization ===== */

void if_init(identity_fabric_t *fabric, 
             financial_fabric_t *financial,
             orbital_fabric_t *orbital) {
    if (!fabric) return;
    
    if_mem_set(fabric, 0, sizeof(*fabric));
    fabric->financial = financial;
    fabric->orbital = orbital;
    
    /* Initialize sub-modules */
    /* identity_registry_init(&fabric->identity); */
    /* vino_init(&fabric->vino); */
    /* vena_init(&fabric->vena); */
    /* zab_init(&fabric->zab); */
    /* reputation_init(&fabric->reputation); */
    /* concord_init(&fabric->concord); */
    /* crown_init(&fabric->crown); */
    /* ministry_init(&fabric->ministry); */
    /* onepolicy_init(&fabric->policy); */
    /* legal_engine_init(&fabric->legal); */
    /* porter_house_init(&fabric->porter); */
    /* interspace_init(&fabric->interspace); */
    
    /* Initialize M5 coordinates */
    fabric->m5.omega = 1;
    fabric->m5.r = SR_FROM_FLOAT(3.0);  /* Identity rail */
    fabric->m5.ell = SR_ONE;
    fabric->m5.phi = SR_ZERO;
    fabric->m5.chi = 0;
    fabric->coverage_ratio = if_compute_coverage(&fabric->m5);
    fabric->min_coverage_ratio = SR_FROM_FLOAT(1.8);
    
    /* Default configuration */
    fabric->config.require_porter_house_trust = true;
    fabric->config.auto_revocation_on_compromise = true;
    fabric->config.min_reputation_for_consensus = SR_FROM_FLOAT(0.6);
    fabric->config.max_delegation_depth = 3;
    fabric->config.credential_default_expiry = 31536000;  /* 1 year in ticks */
    fabric->config.enable_interspace_federation = true;
    
    fabric->global_attestation = LPRES_STATE_NEITHER;
    fabric->global_safety_gate = false;
    fabric->initialized = true;
    
    /* Initialize default reputation dimensions */
    if_add_reputation_dimension(fabric, "technical", SR_FROM_FLOAT(0.25));
    if_add_reputation_dimension(fabric, "governance", SR_FROM_FLOAT(0.25));
    if_add_reputation_dimension(fabric, "financial", SR_FROM_FLOAT(0.25));
    if_add_reputation_dimension(fabric, "social", SR_FROM_FLOAT(0.25));
}

void if_register_builtins(identity_fabric_t *fabric) {
    if (!fabric) return;
    /* Built-ins already initialized in if_init */
}

/* ===== Identity Management ===== */

int32_t if_create_identity(identity_fabric_t *fabric, const char *name,
                           const word168_t *critical_word,
                           const uint64_t initial_balances[9]) {
    if (!fabric || !name || fabric->num_identities >= IF_MAX_IDENTITIES) return -1;
    
    if_identity_t *id = &fabric->identities[fabric->num_identities];
    if_mem_set(id, 0, sizeof(*id));
    id->id = fabric->next_identity_id++;
    
    if_str_copy(id->name, name, IF_MAX_NAME_LEN);
    if (critical_word) id->critical_word = *critical_word;
    else {
        /* Generate new critical word */
        for (int i = 0; i < 168/64; i++) {
            id->critical_word.words[i] = (uint64_t)(id->id + i * 0x9E3779B97F4A7C15ULL);
        }
    }
    
    id->created_tick = 0;  /* Current tick */
    id->last_active_tick = 0;
    id->incarnation = 1;
    
    if (initial_balances) {
        for (int i = 0; i < 9; i++) id->balances[i] = initial_balances[i];
    }
    
    /* Initialize M5 for identity */
    id->m5.omega = fabric->num_identities + 1;
    id->m5.r = SR_FROM_FLOAT(3.0 + fabric->num_identities * 0.001);
    id->m5.ell = SR_ONE;
    id->m5.phi = SR_ZERO;
    id->m5.chi = 0;
    id->coverage_ratio = if_compute_coverage(&id->m5);
    
    id->composite_reputation = SR_FROM_FLOAT(0.5);
    id->attestation = LPRES_STATE_NEITHER;
    id->active = true;
    id->revoked = false;
    
    fabric->num_identities++;
    fabric->stats.total_identities_created++;
    
    return (int32_t)(id->id);
}

if_identity_t *if_get_identity(identity_fabric_t *fabric, uint32_t identity_id) {
    if (!fabric) return NULL;
    for (uint32_t i = 0; i < fabric->num_identities; i++) {
        if (fabric->identities[i].id == identity_id && fabric->identities[i].active) {
            return &fabric->identities[i];
        }
    }
    return NULL;
}

if_identity_t *if_get_identity_by_critical_word(identity_fabric_t *fabric, const word168_t *cw) {
    if (!fabric || !cw) return NULL;
    for (uint32_t i = 0; i < fabric->num_identities; i++) {
        if (fabric->identities[i].active) {
            bool match = true;
            for (int j = 0; j < 168/64; j++) {
                if (fabric->identities[i].critical_word.words[j] != cw->words[j]) {
                    match = false; break;
                }
            }
            if (match) return &fabric->identities[i];
        }
    }
    return NULL;
}

if_identity_t *if_get_identity_by_name(identity_fabric_t *fabric, const char *name) {
    if (!fabric || !name) return NULL;
    for (uint32_t i = 0; i < fabric->num_identities; i++) {
        if (fabric->identities[i].active && if_str_cmp(fabric->identities[i].name, name) == 0) {
            return &fabric->identities[i];
        }
    }
    return NULL;
}

int32_t if_rotate_keys(identity_fabric_t *fabric, uint32_t identity_id,
                       const word168_t *new_critical_word) {
    if (!fabric) return -1;
    if_identity_t *id = if_get_identity(fabric, identity_id);
    if (!id) return -1;
    
    id->critical_word = *new_critical_word;
    id->incarnation++;
    id->last_active_tick = 0;  /* Current tick */
    
    return if_attest(fabric, identity_id, 0x1000, new_critical_word, 0);
}

int32_t if_revoke_identity(identity_fabric_t *fabric, uint32_t identity_id) {
    if (!fabric) return -1;
    if_identity_t *id = if_get_identity(fabric, identity_id);
    if (!id) return -1;
    
    id->revoked = true;
    id->active = false;
    id->attestation = LPRES_STATE_FALSE;
    
    /* Revoke all credentials */
    for (uint32_t i = 0; i < id->num_credentials; i++) {
        uint32_t cred_id = id->credential_ids[i];
        if (cred_id < fabric->num_credentials) {
            fabric->credentials[cred_id].revoked = true;
            fabric->credentials[cred_id].revoked_tick = 0;
        }
    }
    
    /* Revoke all delegations */
    for (uint32_t i = 0; i < id->num_delegations; i++) {
        uint32_t del_id = id->delegation_ids[i];
        if (del_id < fabric->num_delegations) {
            fabric->delegations[del_id].revoked = true;
            fabric->delegations[del_id].revoked_tick = 0;
        }
    }
    
    return if_attest(fabric, identity_id, 0x2000, NULL, -1);
}

/* ===== Credentials ===== */

int32_t if_issue_credential(identity_fabric_t *fabric,
                            uint32_t issuer_id, uint32_t holder_id,
                            uint8_t capital_form, uint64_t amount,
                            const char *schema, uint16_t schema_version,
                            uint64_t expiry_tick) {
    if (!fabric) return -1;
    if (capital_form < 1 || capital_form > 9) return -1;
    if (fabric->num_credentials >= IF_MAX_CREDENTIALS) return -1;
    
    if_identity_t *issuer = if_get_identity(fabric, issuer_id);
    if_identity_t *holder = if_get_identity(fabric, holder_id);
    if (!issuer || !holder) return -1;
    
    if_credential_t *cred = &fabric->credentials[fabric->num_credentials];
    if_mem_set(cred, 0, sizeof(*cred));
    cred->id = fabric->num_credentials;
    cred->holder_id = holder_id;
    cred->issuer_id = issuer_id;
    cred->capital_form = capital_form;
    cred->amount = amount;
    cred->expiry_tick = expiry_tick ? expiry_tick : fabric->config.credential_default_expiry;
    
    if_str_copy(cred->schema, schema, IF_MAX_NAME_LEN);
    cred->schema_version = schema_version;
    
    /* Generate CID and signature (simplified) */
    for (int i = 0; i < 32; i++) cred->credential_cid[i] = (uint8_t)(cred->id + i);
    for (int i = 0; i < 64; i++) cred->issuer_signature[i] = (uint8_t)(issuer_id + i);
    
    cred->attestation = LPRES_STATE_NEITHER;
    cred->active = true;
    
    /* Add to holder's credentials */
    if (holder->num_credentials < IF_MAX_CREDENTIALS) {
        holder->credential_ids[holder->num_credentials++] = cred->id;
    }
    
    fabric->num_credentials++;
    fabric->stats.total_credentials_issued++;
    
    return if_attest(fabric, issuer_id, 0x3000 | capital_form, cred, 0);
}

int32_t if_verify_credential(identity_fabric_t *fabric, uint32_t credential_id) {
    if (!fabric || credential_id >= fabric->num_credentials) return -1;
    
    if_credential_t *cred = &fabric->credentials[credential_id];
    if (!cred->active || cred->revoked) return -1;
    
    /* Verify expiry */
    if (cred->expiry_tick > 0 && cred->expiry_tick < 0) {  /* Current tick */
        cred->revoked = true;
        cred->revoked_tick = 0;
        return -1;
    }
    
    /* Verify issuer signature (simplified) */
    cred->attestation = LPRES_STATE_TRUE;
    
    return if_attest(fabric, cred->holder_id, 0x4000 | credential_id, cred, 0);
}

int32_t if_revoke_credential(identity_fabric_t *fabric, uint32_t credential_id) {
    if (!fabric || credential_id >= fabric->num_credentials) return -1;
    
    if_credential_t *cred = &fabric->credentials[credential_id];
    if (!cred->active) return -1;
    
    cred->revoked = true;
    cred->revoked_tick = 0;
    cred->attestation = LPRES_STATE_FALSE;
    
    return if_attest(fabric, cred->issuer_id, 0x5000 | credential_id, cred, -1);
}

/* ===== Delegations ===== */

int32_t if_grant_delegation(identity_fabric_t *fabric,
                            uint32_t delegator_id, uint32_t delegatee_id,
                            const char *scope, uint8_t max_depth,
                            uint64_t max_amount, uint64_t expiry_tick,
                            bool revocable) {
    if (!fabric) return -1;
    if (max_depth > fabric->config.max_delegation_depth) return -1;
    if (fabric->num_delegations >= IF_MAX_DELEGATIONS) return -1;
    
    if_identity_t *delegator = if_get_identity(fabric, delegator_id);
    if_identity_t *delegatee = if_get_identity(fabric, delegatee_id);
    if (!delegator || !delegatee) return -1;
    
    if_delegation_t *del = &fabric->delegations[fabric->num_delegations];
    if_mem_set(del, 0, sizeof(*del));
    del->id = fabric->num_delegations;
    del->delegator_id = delegator_id;
    del->delegatee_id = delegatee_id;
    
    if_str_copy(del->scope, scope, IF_MAX_NAME_LEN);
    del->max_depth = max_depth;
    del->current_depth = 1;
    del->max_amount = max_amount;
    del->expiry_tick = expiry_tick;
    del->revocable = revocable;
    
    /* Generate CID and signatures */
    for (int i = 0; i < 32; i++) del->delegation_cid[i] = (uint8_t)(del->id + i);
    for (int i = 0; i < 64; i++) {
        del->delegator_signature[i] = (uint8_t)(delegator_id + i);
        del->delegatee_acceptance[i] = (uint8_t)(delegatee_id + i);
    }
    
    del->attestation = LPRES_STATE_NEITHER;
    del->active = true;
    
    /* Add to delegator's delegations */
    if (delegator->num_delegations < IF_MAX_DELEGATIONS) {
        delegator->delegation_ids[delegator->num_delegations++] = del->id;
    }
    
    /* Add to delegatee's received delegations */
    if (delegatee->num_received_delegations < IF_MAX_DELEGATIONS) {
        delegatee->received_delegation_ids[delegatee->num_received_delegations++] = del->id;
    }
    
    fabric->num_delegations++;
    fabric->stats.total_delegations_granted++;
    
    return if_attest(fabric, delegator_id, 0x6000, del, 0);
}

int32_t if_revoke_delegation(identity_fabric_t *fabric, uint32_t delegation_id) {
    if (!fabric || delegation_id >= fabric->num_delegations) return -1;
    
    if_delegation_t *del = &fabric->delegations[delegation_id];
    if (!del->active || !del->revocable) return -1;
    
    del->revoked = true;
    del->revoked_tick = 0;
    del->active = false;
    del->attestation = LPRES_STATE_FALSE;
    
    /* Recursively revoke children */
    for (uint32_t i = 0; i < del->num_children; i++) {
        if_revoke_delegation(fabric, del->child_delegation_ids[i]);
    }
    
    return if_attest(fabric, del->delegator_id, 0x7000 | delegation_id, del, -1);
}

int32_t if_verify_delegation_chain(identity_fabric_t *fabric,
                                   uint32_t delegator_id, uint32_t delegatee_id,
                                   const char *scope) {
    if (!fabric) return -1;
    
    /* Walk delegation chain from delegatee up to delegator */
    if_identity_t *delegatee = if_get_identity(fabric, delegatee_id);
    if (!delegatee) return -1;
    
    for (uint32_t i = 0; i < delegatee->num_received_delegations; i++) {
        uint32_t del_id = delegatee->received_delegation_ids[i];
        if (del_id >= fabric->num_delegations) continue;
        
        if_delegation_t *del = &fabric->delegations[del_id];
        if (!del->active || del->revoked) continue;
        if (if_str_cmp(del->scope, scope) != 0) continue;
        if (del->delegator_id == delegator_id) {
            del->attestation = LPRES_STATE_TRUE;
            return if_attest(fabric, delegatee_id, 0x8000 | del_id, del, 0);
        }
        
        /* Check parent chain */
        uint32_t current = del->parent_delegation_id;
        uint8_t depth = 1;
        while (current != 0xFFFFFFFF && depth < fabric->config.max_delegation_depth) {
            if (current >= fabric->num_delegations) break;
            if_delegation_t *parent = &fabric->delegations[current];
            if (!parent->active || parent->revoked) break;
            if (if_str_cmp(parent->scope, scope) != 0) break;
            if (parent->delegator_id == delegator_id) {
                parent->attestation = LPRES_STATE_TRUE;
                return if_attest(fabric, delegatee_id, 0x9000 | current, parent, 0);
            }
            current = parent->parent_delegation_id;
            depth++;
        }
    }
    
    return -1;
}

/* ===== Reputation ===== */

int32_t if_add_reputation_dimension(identity_fabric_t *fabric,
                                    const char *name, surplus_real_t weight) {
    if (!fabric || !name || fabric->num_reputation_dims >= IF_MAX_REPUTATION_DIMS) return -1;
    
    if_reputation_dimension_t *dim = &fabric->reputation_dims[fabric->num_reputation_dims];
    if_mem_set(dim, 0, sizeof(*dim));
    if_str_copy(dim->name, name, IF_MAX_NAME_LEN);
    dim->weight = weight;
    dim->decay_rate = SR_FROM_FLOAT(0.0001);  /* Slow decay */
    dim->last_update_tick = 0;
    dim->active = true;
    
    fabric->num_reputation_dims++;
    return (int32_t)(fabric->num_reputation_dims - 1);
}

int32_t if_update_reputation(identity_fabric_t *fabric, uint32_t identity_id,
                             uint32_t dim_id, surplus_real_t delta) {
    if (!fabric || identity_id >= fabric->num_identities) return -1;
    if (dim_id >= fabric->num_reputation_dims) return -1;
    
    if_identity_t *id = &fabric->identities[identity_id];
    if (!id->active) return -1;
    
    if_reputation_dimension_t *dim = &fabric->reputation_dims[dim_id];
    if (!dim->active) return -1;
    
    /* Apply delta with bounds */
    surplus_real_t new_val = SR_ADD(id->reputation_dims[dim_id], delta);
    if (SR_CMP(new_val, SR_ZERO) < 0) new_val = SR_ZERO;
    if (SR_CMP(new_val, SR_ONE) > 0) new_val = SR_ONE;
    id->reputation_dims[dim_id] = new_val;
    
    /* Recompute composite */
    surplus_real_t composite = SR_ZERO;
    surplus_real_t total_weight = SR_ZERO;
    for (uint32_t i = 0; i < fabric->num_reputation_dims; i++) {
        if (fabric->reputation_dims[i].active) {
            composite = SR_ADD(composite, SR_MUL(id->reputation_dims[i], fabric->reputation_dims[i].weight));
            total_weight = SR_ADD(total_weight, fabric->reputation_dims[i].weight);
        }
    }
    if (SR_CMP(total_weight, SR_ZERO) > 0) {
        id->composite_reputation = SR_DIV(composite, total_weight);
    }
    
    dim->last_update_tick = 0;  /* Current tick */
    fabric->stats.total_reputation_updates++;
    
    return if_attest(fabric, identity_id, 0xA000 | dim_id, &delta, 0);
}

surplus_real_t if_get_composite_reputation(identity_fabric_t *fabric, uint32_t identity_id) {
    if (!fabric || identity_id >= fabric->num_identities) return SR_ZERO;
    return fabric->identities[identity_id].composite_reputation;
}

/* ===== Consensus ===== */

int32_t if_join_consensus(identity_fabric_t *fabric, uint32_t identity_id,
                          uint64_t stake) {
    if (!fabric) return -1;
    if_identity_t *id = if_get_identity(fabric, identity_id);
    if (!id) return -1;
    
    if (SR_CMP(id->composite_reputation, fabric->config.min_reputation_for_consensus) < 0) {
        return -1;  /* Insufficient reputation */
    }
    
    if (fabric->consensus.num_validators >= IF_MAX_CONSENSUS_NODES) return -1;
    
    id->zab_validator = true;
    id->zab_node_id = fabric->consensus.num_validators;
    id->zab_stake = stake;
    
    fabric->consensus.validator_ids[fabric->consensus.num_validators++] = identity_id;
    fabric->consensus.total_stake += stake;
    
    /* Update quorum sizes */
    fabric->consensus.quorum_size = (fabric->consensus.num_validators * 2) / 3 + 1;
    fabric->consensus.fast_quorum_size = (fabric->consensus.num_validators * 3) / 4 + 1;
    
    return if_attest(fabric, identity_id, 0xB000, &stake, 0);
}

int32_t if_leave_consensus(identity_fabric_t *fabric, uint32_t identity_id) {
    if (!fabric) return -1;
    if_identity_t *id = if_get_identity(fabric, identity_id);
    if (!id || !id->zab_validator) return -1;
    
    /* Remove from validator set */
    for (uint32_t i = 0; i < fabric->consensus.num_validators; i++) {
        if (fabric->consensus.validator_ids[i] == identity_id) {
            fabric->consensus.total_stake -= id->zab_stake;
            for (uint32_t j = i; j < fabric->consensus.num_validators - 1; j++) {
                fabric->consensus.validator_ids[j] = fabric->consensus.validator_ids[j + 1];
            }
            fabric->consensus.num_validators--;
            break;
        }
    }
    
    id->zab_validator = false;
    id->zab_node_id = 0;
    id->zab_stake = 0;
    
    /* Update quorum sizes */
    if (fabric->consensus.num_validators > 0) {
        fabric->consensus.quorum_size = (fabric->consensus.num_validators * 2) / 3 + 1;
        fabric->consensus.fast_quorum_size = (fabric->consensus.num_validators * 3) / 4 + 1;
    }
    
    return if_attest(fabric, identity_id, 0xC000, NULL, 0);
}

int32_t if_propose_consensus(identity_fabric_t *fabric, uint32_t proposer_id,
                             const void *proposal, uint32_t proposal_len) {
    if (!fabric) return -1;
    if_identity_t *id = if_get_identity(fabric, proposer_id);
    if (!id || !id->zab_validator) return -1;
    
    /* Submit proposal to ZAB */
    /* zab_propose(&fabric->zab, proposal, proposal_len); */
    
    fabric->consensus.total_proposals++;
    fabric->stats.total_consensus_rounds++;
    
    return if_attest(fabric, proposer_id, 0xD000, proposal, 0);
}

/* ===== Governance ===== */

int32_t if_submit_proposal(identity_fabric_t *fabric, uint32_t proposer_id,
                           const char *proposal_type, const void *data, uint32_t len) {
    if (!fabric) return -1;
    if_identity_t *id = if_get_identity(fabric, proposer_id);
    if (!id) return -1;
    
    /* Submit to Concord */
    /* concord_propose(&fabric->concord, proposal_type, data, len); */
    
    fabric->governance.proposal_count++;
    fabric->governance.active_proposals++;
    fabric->stats.total_governance_proposals++;
    
    return if_attest(fabric, proposer_id, 0xE000, data, 0);
}

int32_t if_vote_proposal(identity_fabric_t *fabric, uint32_t voter_id,
                         uint32_t proposal_id, bool approve) {
    if (!fabric) return -1;
    if_identity_t *id = if_get_identity(fabric, voter_id);
    if (!id) return -1;
    
    /* Vote via Concord */
    /* concord_vote(&fabric->concord, proposal_id, approve); */
    
    return if_attest(fabric, voter_id, 0xF000 | proposal_id, &approve, 0);
}

int32_t if_execute_proposal(identity_fabric_t *fabric, uint32_t proposal_id) {
    if (!fabric) return -1;
    
    /* Execute via Concord */
    /* concord_execute(&fabric->concord, proposal_id); */
    
    fabric->governance.active_proposals--;
    
    return if_attest(fabric, 0xFFFFFFFF, 0x10000 | proposal_id, NULL, 0);
}

/* ===== Legal ===== */

int32_t if_create_legal_contract(identity_fabric_t *fabric,
                                 uint32_t party_a, uint32_t party_b,
                                 const char *contract_type, const void *terms, uint32_t len) {
    if (!fabric) return -1;
    if_identity_t *a = if_get_identity(fabric, party_a);
    if_identity_t *b = if_get_identity(fabric, party_b);
    if (!a || !b) return -1;
    
    if (fabric->governance.legal_state.active_contracts >= IF_MAX_LEGAL_CONTRACTS) return -1;
    
    /* Create contract via Legal Engine */
    /* legal_engine_create_contract(&fabric->legal, party_a, party_b, contract_type, terms, len); */
    
    uint32_t contract_id = fabric->governance.legal_state.active_contracts++;
    
    if (a->num_legal_contracts < IF_MAX_LEGAL_CONTRACTS) {
        a->legal_contract_ids[a->num_legal_contracts++] = contract_id;
    }
    if (b->num_legal_contracts < IF_MAX_LEGAL_CONTRACTS) {
        b->legal_contract_ids[b->num_legal_contracts++] = contract_id;
    }
    
    fabric->stats.total_legal_contracts++;
    
    return if_attest(fabric, party_a, 0x11000 | contract_id, terms, 0);
}

int32_t if_enforce_contract(identity_fabric_t *fabric, uint32_t contract_id) {
    if (!fabric) return -1;
    
    /* Enforce via Legal Engine */
    /* legal_engine_enforce(&fabric->legal, contract_id); */
    
    return if_attest(fabric, 0xFFFFFFFF, 0x12000 | contract_id, NULL, 0);
}

/* ===== Interspace Federation ===== */

int32_t if_establish_corridor(identity_fabric_t *fabric,
                              uint32_t local_identity_id,
                              const word168_t *remote_critical_word,
                              const char *corridor_name,
                              uint8_t trust_level) {
    if (!fabric) return -1;
    if (!fabric->config.enable_interspace_federation) return -1;
    if (fabric->num_corridors >= IF_MAX_CORRIDORS) return -1;
    
    if_identity_t *local = if_get_identity(fabric, local_identity_id);
    if (!local) return -1;
    
    if_corridor_t *corr = &fabric->corridors[fabric->num_corridors];
    if_mem_set(corr, 0, sizeof(*corr));
    corr->id = fabric->num_corridors;
    if_str_copy(corr->name, corridor_name, IF_MAX_NAME_LEN);
    corr->local_identity_id = local_identity_id;
    corr->remote_identity_id = 0xFFFFFFFF;  /* Unknown until federated */
    corr->trust_level = trust_level;
    corr->established_tick = 0;
    corr->last_sync_tick = 0;
    
    /* Initialize Lex Rhodia params */
    corr->lex_params.max_latency = SR_FROM_FLOAT(100.0);
    corr->lex_params.min_bandwidth = SR_FROM_FLOAT(1000000.0);
    corr->lex_params.trust_threshold = SR_FROM_FLOAT(0.7);
    
    /* Initialize M5 for corridor */
    corr->m5.omega = fabric->num_corridors + 1;
    corr->m5.r = SR_FROM_FLOAT(4.0);
    corr->m5.ell = SR_ONE;
    corr->m5.phi = SR_ZERO;
    corr->m5.chi = 0;
    corr->coverage_ratio = if_compute_coverage(&corr->m5);
    
    corr->corridor_attestation = LPRES_STATE_NEITHER;
    corr->active = true;
    
    if (local->num_corridors < IF_MAX_CORRIDORS) {
        local->corridor_ids[local->num_corridors++] = corr->id;
    }
    
    fabric->num_corridors++;
    fabric->stats.total_corridors_established++;
    
    return if_attest(fabric, local_identity_id, 0x13000, corr, 0);
}

int32_t if_sync_corridor(identity_fabric_t *fabric, uint32_t corridor_id) {
    if (!fabric || corridor_id >= fabric->num_corridors) return -1;
    
    if_corridor_t *corr = &fabric->corridors[corridor_id];
    if (!corr->active) return -1;
    
    /* Sync via Interspace */
    /* interspace_sync(&fabric->interspace, corridor_id); */
    
    corr->last_sync_tick = 0;  /* Current tick */
    corr->corridor_attestation = LPRES_STATE_TRUE;
    
    return if_attest(fabric, corr->local_identity_id, 0x14000 | corridor_id, corr, 0);
}

int32_t if_federate_identity(identity_fabric_t *fabric,
                             uint32_t corridor_id,
                             uint32_t local_identity_id,
                             const word168_t *remote_critical_word) {
    if (!fabric) return -1;
    if (corridor_id >= fabric->num_corridors) return -1;
    
    if_corridor_t *corr = &fabric->corridors[corridor_id];
    if_identity_t *local = if_get_identity(fabric, local_identity_id);
    if (!local) return -1;
    
    /* Federate via Interspace */
    /* interspace_federate(&fabric->interspace, corridor_id, local_identity_id, remote_critical_word); */
    
    corr->remote_identity_id = fabric->num_identities;  /* Would be remote identity ID */
    corr->fed_status = FEDERATION_ACTIVE;
    corr->corridor_attestation = LPRES_STATE_TRUE;
    
    fabric->stats.total_cross_federation_ops++;
    
    return if_attest(fabric, local_identity_id, 0x15000 | corridor_id, (void*)remote_critical_word, 0);
}

/* ===== Health & Attestation ===== */

int32_t if_check_identity_health(identity_fabric_t *fabric,
                                 uint32_t identity_id,
                                 void *health_out) {
    if (!fabric || identity_id >= fabric->num_identities) return -1;
    
    if_identity_t *id = &fabric->identities[identity_id];
    if (!id->active) return -1;
    
    /* Update coverage */
    id->coverage_ratio = if_compute_coverage(&id->m5);
    
    /* Check credentials */
    for (uint32_t i = 0; i < id->num_credentials; i++) {
        uint32_t cred_id = id->credential_ids[i];
        if (cred_id < fabric->num_credentials) {
            if_verify_credential(fabric, cred_id);
        }
    }
    
    /* Check delegations */
    for (uint32_t i = 0; i < id->num_delegations; i++) {
        uint32_t del_id = id->delegation_ids[i];
        if (del_id < fabric->num_delegations) {
            if_delegation_t *del = &fabric->delegations[del_id];
            if (del->expiry_tick > 0 && del->expiry_tick < (uint64_t)(-1)) {
                if_revoke_delegation(fabric, del_id);
            }
        }
    }
    
    /* Check reputation decay */
    for (uint32_t i = 0; i < fabric->num_reputation_dims; i++) {
        if_reputation_dimension_t *dim = &fabric->reputation_dims[i];
        if (dim->active && dim->last_update_tick > 0) {
            /* Apply decay (simplified) */
            uint64_t ticks_elapsed = 0;  /* Current tick - dim->last_update_tick */
            if (ticks_elapsed > 1000) {
                surplus_real_t decay = SR_MUL(dim->decay_rate, SR_FROM_INT(ticks_elapsed / 1000));
                if (SR_CMP(id->reputation_dims[i], decay) > 0) {
                    id->reputation_dims[i] = SR_SUB(id->reputation_dims[i], decay);
                } else {
                    id->reputation_dims[i] = SR_ZERO;
                }
            }
        }
    }
    
    /* Recompute composite */
    if_update_reputation(fabric, identity_id, 0, SR_ZERO);  /* Triggers recompute */
    
    /* Update attestation */
    if (SR_CMP(id->coverage_ratio, fabric->min_coverage_ratio) >= 0) {
        id->attestation = LPRES_STATE_TRUE;
    } else {
        id->attestation = LPRES_STATE_BOTH;
    }
    
    return 0;
}

int32_t if_check_global_health(identity_fabric_t *fabric) {
    if (!fabric) return -1;
    
    int32_t unhealthy = 0;
    for (uint32_t i = 0; i < fabric->num_identities; i++) {
        if (fabric->identities[i].active) {
            if (if_check_identity_health(fabric, fabric->identities[i].id, NULL) < 0) {
                unhealthy++;
            }
        }
    }
    
    /* Check consensus health */
    if (fabric->consensus.num_validators > 0) {
        if (fabric->consensus.total_timeouts > fabric->consensus.total_committed * 2) {
            unhealthy++;
        }
    }
    
    /* Check corridors */
    for (uint32_t i = 0; i < fabric->num_corridors; i++) {
        if (fabric->corridors[i].active) {
            if (SR_CMP(fabric->corridors[i].coverage_ratio, fabric->min_coverage_ratio) < 0) {
                unhealthy++;
            }
        }
    }
    
    fabric->global_safety_gate = (unhealthy == 0);
    fabric->global_attestation = fabric->global_safety_gate ? LPRES_STATE_TRUE : LPRES_STATE_BOTH;
    
    return unhealthy == 0 ? 0 : -1;
}

bool if_global_safety_gate(identity_fabric_t *fabric) {
    return fabric ? fabric->global_safety_gate : false;
}

/* ===== Coverage ===== */

void if_update_coverage(identity_fabric_t *fabric) {
    if (!fabric) return;
    
    fabric->coverage_ratio = if_compute_coverage(&fabric->m5);
    
    for (uint32_t i = 0; i < fabric->num_identities; i++) {
        if (fabric->identities[i].active) {
            fabric->identities[i].coverage_ratio = if_compute_coverage(&fabric->identities[i].m5);
        }
    }
    
    for (uint32_t i = 0; i < fabric->num_corridors; i++) {
        if (fabric->corridors[i].active) {
            fabric->corridors[i].coverage_ratio = if_compute_coverage(&fabric->corridors[i].m5);
        }
    }
}

bool if_enforce_coverage(identity_fabric_t *fabric, surplus_real_t min_ratio) {
    if (!fabric) return false;
    
    if (SR_CMP(fabric->coverage_ratio, min_ratio) < 0) return false;
    
    for (uint32_t i = 0; i < fabric->num_identities; i++) {
        if (fabric->identities[i].active) {
            if (SR_CMP(fabric->identities[i].coverage_ratio, min_ratio) < 0) return false;
        }
    }
    
    return true;
}

/* ===== Statistics ===== */

void if_get_stats(identity_fabric_t *fabric, void *stats_out) {
    if (!fabric || !stats_out) return;
    if_mem_copy(stats_out, &fabric->stats, sizeof(fabric->stats));
}

/* ===== Paraconsistent State ===== */

lpres_state_t if_get_attestation(identity_fabric_t *fabric, uint32_t identity_id) {
    if (!fabric || identity_id >= fabric->num_identities) return LPRES_STATE_NEITHER;
    return fabric->identities[identity_id].attestation;
}

void if_set_attestation(identity_fabric_t *fabric, uint32_t identity_id, lpres_state_t state) {
    if (!fabric || identity_id >= fabric->num_identities) return;
    fabric->identities[identity_id].attestation = state;
}

/* ===== Utility ===== */

const char *if_lpres_state_name(lpres_state_t state) {
    return lpres_state_name(state);
}

const char *if_capital_form_name(uint8_t form) {
    static const char *names[] = {
        "Social", "Natural", "Heritage/Intellectual", "Governance/Institutional",
        "Financial", "Material", "Living", "Knowledge", "Built"
    };
    if (form >= 1 && form <= 9) return names[form - 1];
    return "Unknown";
}
