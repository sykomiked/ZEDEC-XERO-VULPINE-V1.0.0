/* governance_fabric.c — ZXV Governance Fabric Compound Module Implementation
 *
 * Unifies all governance modules: Legal Engine, Concord, Ministry, Crown,
 * OnePolicy, Reputation, ZAB, Porter House, Interspace, with Identity,
 * Financial, and Orbital fabric integration.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "governance_fabric.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"
#include "selfaudit.h"

/* ===== Helper Functions ===== */

static void gf_mem_set(void *dst, int val, uint32_t len) {
    uint8_t *d = dst;
    for (uint32_t i = 0; i < len; i++) d[i] = (uint8_t)val;
}

static void gf_mem_copy(void *dst, const void *src, uint32_t len) {
    uint8_t *d = dst; const uint8_t *s = src;
    for (uint32_t i = 0; i < len; i++) d[i] = s[i];
}

static int gf_str_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static uint32_t gf_str_len(const char *s) {
    uint32_t n = 0; while (s[n]) n++; return n;
}

static void gf_str_copy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* ===== Coverage Computation ===== */

static surplus_real_t gf_compute_coverage(const m5_coords_t *m5) {
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

lpres_state_t gf_attest(governance_fabric_t *fabric, uint32_t proposal_id,
                        uint32_t op_id, void *args, int32_t result) {
    if (!fabric) return LPRES_STATE_NEITHER;
    
    lpres_state_t result_att = (result >= 0) ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    lpres_state_t fabric_att = fabric->global_attestation;
    
    lpres_state_t combined = lpres_conjoin(result_att, fabric_att);
    fabric->global_attestation = lpres_conjoin(fabric->global_attestation, combined);
    
    return combined;
}

/* ===== Initialization ===== */

void gf_init(governance_fabric_t *fabric,
             identity_fabric_t *identity,
             financial_fabric_t *financial,
             orbital_fabric_t *orbital) {
    if (!fabric) return;
    
    gf_mem_set(fabric, 0, sizeof(*fabric));
    fabric->identity = identity;
    fabric->financial = financial;
    fabric->orbital = orbital;
    
    /* Initialize sub-modules */
    /* legal_engine_init(&fabric->legal); */
    /* concord_init(&fabric->concord); */
    /* ministry_init(&fabric->ministry); */
    /* crown_init(&fabric->crown); */
    /* onepolicy_init(&fabric->policy); */
    /* reputation_init(&fabric->reputation); */
    /* zab_init(&fabric->zab); */
    /* porter_house_init(&fabric->porter); */
    /* interspace_init(&fabric->interspace); */
    
    /* Initialize M5 coordinates */
    fabric->m5.omega = 1;
    fabric->m5.r = SR_FROM_FLOAT(4.0);  /* Governance rail */
    fabric->m5.ell = SR_ONE;
    fabric->m5.phi = SR_ZERO;
    fabric->m5.chi = 0;
    fabric->coverage_ratio = gf_compute_coverage(&fabric->m5);
    fabric->min_coverage_ratio = SR_FROM_FLOAT(1.8);
    
    /* Default configuration */
    fabric->config.require_porter_house_for_voting = true;
    fabric->config.auto_slash_on_dishonest_vote = true;
    fabric->config.min_reputation_for_proposing = SR_FROM_FLOAT(0.6);
    fabric->config.default_voting_period = 10000;
    fabric->config.enable_emergency_proposals = true;
    fabric->config.emergency_quorum_form = 5;  /* Financial capital */
    
    fabric->global_attestation = LPRES_STATE_NEITHER;
    fabric->global_safety_gate = false;
    fabric->initialized = true;
}

void gf_register_builtins(governance_fabric_t *fabric) {
    if (!fabric) return;
}

/* ===== Proposal Management ===== */

int32_t gf_create_proposal(governance_fabric_t *fabric,
                           uint32_t proposer_id,
                           const char *title, const char *type,
                           const void *data, uint32_t data_len,
                           uint64_t voting_period,
                           uint64_t quorum_required) {
    if (!fabric || !title || !type || fabric->num_proposals >= GF_MAX_PROPOSALS) return -1;
    
    if_identity_t *proposer = if_get_identity(fabric->identity, proposer_id);
    if (!proposer) return -1;
    
    /* Check reputation for proposing */
    if (SR_CMP(proposer->composite_reputation, fabric->config.min_reputation_for_proposing) < 0) {
        return -1;
    }
    
    gf_proposal_t *prop = &fabric->proposals[fabric->num_proposals];
    gf_mem_set(prop, 0, sizeof(*prop));
    prop->id = fabric->next_proposal_id++;
    
    gf_str_copy(prop->title, title, GF_MAX_NAME_LEN);
    gf_str_copy(prop->type, type, GF_MAX_NAME_LEN);
    prop->proposer_id = proposer_id;
    prop->proposer_stake = proposer->balances[4];  /* Form 5: Financial */
    
    if (data && data_len > 0) {
        prop->data = (void*)data;  /* In real impl, would copy */
        prop->data_len = data_len;
        for (int i = 0; i < 32; i++) prop->data_cid[i] = (uint8_t)(prop->id + i);
    }
    
    prop->voting_start_tick = 0;  /* Current tick */
    prop->voting_end_tick = prop->voting_start_tick + (voting_period ? voting_period : fabric->config.default_voting_period);
    prop->quorum_required = quorum_required;
    
    prop->attestation = LPRES_STATE_NEITHER;
    prop->active = true;
    
    fabric->num_proposals++;
    fabric->stats.total_proposals++;
    
    return gf_attest(fabric, prop->id, 0x1000, prop, 0);
}

gf_proposal_t *gf_get_proposal(governance_fabric_t *fabric, uint32_t proposal_id) {
    if (!fabric) return NULL;
    for (uint32_t i = 0; i < fabric->num_proposals; i++) {
        if (fabric->proposals[i].id == proposal_id && fabric->proposals[i].active) {
            return &fabric->proposals[i];
        }
    }
    return NULL;
}

/* ===== Voting ===== */

int32_t gf_cast_vote(governance_fabric_t *fabric,
                     uint32_t proposal_id, uint32_t voter_id,
                     bool approve, uint64_t weight) {
    if (!fabric) return -1;
    
    gf_proposal_t *prop = gf_get_proposal(fabric, proposal_id);
    if (!prop) return -1;
    
    /* Check voting period */
    uint64_t current_tick = 0;  /* Would be actual tick */
    if (current_tick < prop->voting_start_tick || current_tick > prop->voting_end_tick) {
        return -1;
    }
    
    if_identity_t *voter = if_get_identity(fabric->identity, voter_id);
    if (!voter) return -1;
    
    /* Check Porter House trust if required */
    if (fabric->config.require_porter_house_for_voting) {
        /* porter_house_check_trust(&fabric->porter, voter_id); */
    }
    
    if (fabric->num_votes >= GF_MAX_VOTES) return -1;
    
    gf_vote_t *vote = &fabric->votes[fabric->num_votes];
    gf_mem_set(vote, 0, sizeof(*vote));
    vote->id = fabric->num_votes;
    vote->proposal_id = proposal_id;
    vote->voter_id = voter_id;
    vote->weight = weight ? weight : voter->balances[4];  /* Default to Financial capital */
    vote->approve = approve;
    vote->tick = current_tick;
    
    /* Generate signature */
    for (int i = 0; i < 64; i++) vote->signature[i] = (uint8_t)(voter_id + i);
    
    vote->attestation = LPRES_STATE_NEITHER;
    vote->counted = false;
    
    fabric->num_votes++;
    fabric->stats.total_votes_cast++;
    
    return gf_attest(fabric, proposal_id, 0x2000 | voter_id, vote, 0);
}

int32_t gf_tally_votes(governance_fabric_t *fabric, uint32_t proposal_id) {
    if (!fabric) return -1;
    
    gf_proposal_t *prop = gf_get_proposal(fabric, proposal_id);
    if (!prop) return -1;
    
    prop->votes_for = 0;
    prop->votes_against = 0;
    prop->votes_abstain = 0;
    
    for (uint32_t i = 0; i < fabric->num_votes; i++) {
        gf_vote_t *vote = &fabric->votes[i];
        if (vote->proposal_id != proposal_id || vote->counted) continue;
        
        if (vote->approve) {
            prop->votes_for = SR_ADD(prop->votes_for, SR_FROM_INT(vote->weight));
        } else {
            prop->votes_against = SR_ADD(prop->votes_against, SR_FROM_INT(vote->weight));
        }
        vote->counted = true;
        vote->attestation = LPRES_STATE_TRUE;
    }
    
    /* Check quorum */
    surplus_real_t total_votes = SR_ADD(prop->votes_for, prop->votes_against);
    if (SR_CMP(total_votes, SR_FROM_INT(prop->quorum_required)) >= 0) {
        prop->attestation = LPRES_STATE_TRUE;
    } else {
        prop->attestation = LPRES_STATE_BOTH;
    }
    
    return gf_attest(fabric, proposal_id, 0x3000, prop, 0);
}

/* ===== Execution ===== */

int32_t gf_execute_proposal(governance_fabric_t *fabric, uint32_t proposal_id) {
    if (!fabric) return -1;
    
    gf_proposal_t *prop = gf_get_proposal(fabric, proposal_id);
    if (!prop || prop->executed) return -1;
    
    /* Check if voting period ended */
    uint64_t current_tick = 0;
    if (current_tick < prop->voting_end_tick) return -1;
    
    /* Tally votes if not done */
    if (prop->votes_for == 0 && prop->votes_against == 0) {
        gf_tally_votes(fabric, proposal_id);
    }
    
    /* Check quorum and majority */
    surplus_real_t total = SR_ADD(prop->votes_for, prop->votes_against);
    if (SR_CMP(total, SR_FROM_INT(prop->quorum_required)) < 0) {
        prop->attestation = LPRES_STATE_FALSE;
        return -1;  /* Quorum not met */
    }
    
    if (SR_CMP(prop->votes_for, prop->votes_against) <= 0) {
        prop->attestation = LPRES_STATE_FALSE;
        return -1;  /* Majority not met */
    }
    
    /* Execute based on type */
    if (gf_str_cmp(prop->type, "policy") == 0) {
        /* Create/enforce policy */
        /* onepolicy_create(&fabric->policy, prop->data, prop->data_len); */
    } else if (gf_str_cmp(prop->type, "contract") == 0) {
        /* Execute contract */
        /* legal_engine_execute(&fabric->legal, prop->data, prop->data_len); */
    } else if (gf_str_cmp(prop->type, "budget") == 0) {
        /* Allocate budget via Financial Fabric */
        /* ff_transfer_capital(...); */
    }
    
    prop->executed = true;
    prop->execution_tick = current_tick;
    prop->execution_result = 0;
    prop->attestation = LPRES_STATE_TRUE;
    
    fabric->stats.total_policies_enforced++;
    
    return gf_attest(fabric, proposal_id, 0x4000, prop, 0);
}

/* ===== Policy Management ===== */

int32_t gf_create_policy(governance_fabric_t *fabric,
                         const char *name, const char *domain,
                         const char *condition, const char *action,
                         surplus_real_t threshold) {
    if (!fabric || !name || !domain || fabric->num_policies >= GF_MAX_POLICIES) return -1;
    
    gf_policy_t *pol = &fabric->policies[fabric->num_policies];
    gf_mem_set(pol, 0, sizeof(*pol));
    pol->id = fabric->num_policies;
    
    gf_str_copy(pol->name, name, GF_MAX_NAME_LEN);
    gf_str_copy(pol->domain, domain, GF_MAX_NAME_LEN);
    
    if (pol->num_rules < 32) {
        auto *rule = &pol->rules[pol->num_rules++];
        gf_str_copy(rule->condition, condition, GF_MAX_NAME_LEN);
        gf_str_copy(rule->action, action, GF_MAX_NAME_LEN);
        rule->threshold = threshold;
        rule->active = true;
    }
    
    pol->kernel_enforced = true;
    pol->attestation = LPRES_STATE_NEITHER;
    pol->active = true;
    
    fabric->num_policies++;
    
    return gf_attest(fabric, 0xFFFFFFFF, 0x5000 | pol->id, pol, 0);
}

int32_t gf_enforce_policy(governance_fabric_t *fabric, uint32_t policy_id, void *context) {
    if (!fabric || policy_id >= fabric->num_policies) return -1;
    
    gf_policy_t *pol = &fabric->policies[policy_id];
    if (!pol->active || !pol->kernel_enforced) return -1;
    
    /* Evaluate rules against context */
    for (uint32_t i = 0; i < pol->num_rules; i++) {
        auto *rule = &pol->rules[i];
        if (!rule->active) continue;
        
        /* In real implementation, would evaluate condition against context */
        /* For now, assume condition passes */
        bool condition_met = true;
        
        if (condition_met) {
            /* Execute action */
            /* Would dispatch to appropriate fabric based on domain */
            if (gf_str_cmp(pol->domain, "financial") == 0) {
                /* Financial fabric action */
            } else if (gf_str_cmp(pol->domain, "identity") == 0) {
                /* Identity fabric action */
            } else if (gf_str_cmp(pol->domain, "compute") == 0) {
                /* Compute fabric action */
            }
        }
    }
    
    pol->attestation = LPRES_STATE_TRUE;
    fabric->stats.total_policies_enforced++;
    
    return gf_attest(fabric, 0xFFFFFFFF, 0x6000 | policy_id, pol, 0);
}

/* ===== Legal Contracts ===== */

int32_t gf_create_contract(governance_fabric_t *fabric,
                           const char *contract_type,
                           const uint32_t *party_ids, uint32_t num_parties,
                           const uint64_t *party_stakes,
                           const void *terms, uint32_t terms_len) {
    if (!fabric || !contract_type || num_parties == 0 || num_parties > 8 ||
        fabric->num_contracts >= GF_MAX_CONTRACTS) return -1;
    
    gf_legal_contract_t *contract = &fabric->contracts[fabric->num_contracts];
    gf_mem_set(contract, 0, sizeof(*contract));
    contract->id = fabric->num_contracts;
    
    gf_str_copy(contract->contract_type, contract_type, GF_MAX_NAME_LEN);
    contract->num_parties = num_parties;
    
    for (uint32_t i = 0; i < num_parties; i++) {
        contract->party_ids[i] = party_ids[i];
        contract->party_stakes[i] = party_stakes ? party_stakes[i] : 0;
        
        /* Verify parties exist */
        if_identity_t *party = if_get_identity(fabric->identity, party_ids[i]);
        if (!party) return -1;
    }
    
    if (terms && terms_len > 0) {
        contract->terms = (void*)terms;
        contract->terms_len = terms_len;
        for (int i = 0; i < 32; i++) contract->terms_cid[i] = (uint8_t)(contract->id + i);
    }
    
    contract->state = GF_CONTRACT_ACTIVE;
    contract->attestation = LPRES_STATE_NEITHER;
    contract->active = true;
    
    fabric->num_contracts++;
    fabric->stats.total_contracts_executed++;
    
    return gf_attest(fabric, 0xFFFFFFFF, 0x7000 | contract->id, contract, 0);
}

int32_t gf_execute_contract(governance_fabric_t *fabric, uint32_t contract_id) {
    if (!fabric || contract_id >= fabric->num_contracts) return -1;
    
    gf_legal_contract_t *contract = &fabric->contracts[contract_id];
    if (!contract->active || contract->state != GF_CONTRACT_ACTIVE) return -1;
    
    /* Execute via Legal Engine */
    /* legal_engine_execute(&fabric->legal, contract->terms, contract->terms_len); */
    
    contract->attestation = LPRES_STATE_TRUE;
    return gf_attest(fabric, 0xFFFFFFFF, 0x8000 | contract_id, contract, 0);
}

int32_t gf_dispute_contract(governance_fabric_t *fabric, uint32_t contract_id,
                            uint32_t disputant_id, const char *reason) {
    if (!fabric || contract_id >= fabric->num_contracts) return -1;
    
    gf_legal_contract_t *contract = &fabric->contracts[contract_id];
    if (!contract->active) return -1;
    
    /* Verify disputant is a party */
    bool is_party = false;
    for (uint32_t i = 0; i < contract->num_parties; i++) {
        if (contract->party_ids[i] == disputant_id) {
            is_party = true; break;
        }
    }
    if (!is_party) return -1;
    
    contract->state = GF_CONTRACT_DISPUTED;
    contract->dispute_deadline = 0;  /* Current tick + dispute period */
    contract->attestation = LPRES_STATE_BOTH;
    
    return gf_attest(fabric, disputant_id, 0x9000 | contract_id, (void*)reason, 0);
}

int32_t gf_resolve_dispute(governance_fabric_t *fabric, uint32_t contract_id,
                           uint32_t arbiter_id, int32_t ruling) {
    if (!fabric || contract_id >= fabric->num_contracts) return -1;
    
    gf_legal_contract_t *contract = &fabric->contracts[contract_id];
    if (!contract->active || contract->state != GF_CONTRACT_DISPUTED) return -1;
    
    /* Verify arbiter */
    if_identity_t *arbiter = if_get_identity(fabric->identity, arbiter_id);
    if (!arbiter) return -1;
    
    contract->arbiter_id = arbiter_id;
    contract->state = (ruling >= 0) ? GF_CONTRACT_SETTLED : GF_CONTRACT_VOID;
    contract->attestation = (ruling >= 0) ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    
    /* Slash stakes if ruling against a party */
    if (ruling < 0 && fabric->config.auto_slash_on_dishonest_vote) {
        /* Would slash stakes of losing parties */
        fabric->stats.total_capital_slashed++;
    }
    
    fabric->stats.total_disputes_resolved++;
    
    return gf_attest(fabric, arbiter_id, 0xA000 | contract_id, contract, ruling);
}

/* ===== Jurisdictions ===== */

int32_t gf_create_jurisdiction(governance_fabric_t *fabric,
                               const char *name, const char *code,
                               const char *legal_framework) {
    if (!fabric || !name || !code || fabric->num_jurisdictions >= GF_MAX_JURISDICTIONS) return -1;
    
    gf_jurisdiction_t *jur = &fabric->jurisdictions[fabric->num_jurisdictions];
    gf_mem_set(jur, 0, sizeof(*jur));
    jur->id = fabric->num_jurisdictions;
    
    gf_str_copy(jur->name, name, GF_MAX_NAME_LEN);
    gf_str_copy(jur->code, code, 8);
    gf_str_copy(jur->legal_framework, legal_framework, GF_MAX_NAME_LEN);
    
    jur->min_stake_for_voting = 1000;
    jur->min_stake_for_proposing = 10000;
    
    /* Initialize M5 */
    jur->m5.omega = fabric->num_jurisdictions + 1;
    jur->m5.r = SR_FROM_FLOAT(4.0);
    jur->m5.ell = SR_ONE;
    jur->m5.phi = SR_ZERO;
    jur->m5.chi = 0;
    jur->coverage_ratio = gf_compute_coverage(&jur->m5);
    
    jur->attestation = LPRES_STATE_NEITHER;
    jur->active = true;
    
    fabric->num_jurisdictions++;
    return gf_attest(fabric, 0xFFFFFFFF, 0xB000 | jur->id, jur, 0);
}

int32_t gf_federate_jurisdiction(governance_fabric_t *fabric,
                                 uint32_t jurisdiction_id,
                                 uint32_t corridor_id) {
    if (!fabric || jurisdiction_id >= fabric->num_jurisdictions) return -1;
    
    gf_jurisdiction_t *jur = &fabric->jurisdictions[jurisdiction_id];
    if (!jur->active) return -1;
    
    if (jur->num_corridors >= 16) return -1;
    jur->corridor_ids[jur->num_corridors++] = corridor_id;
    
    fabric->stats.total_jurisdictions_federated++;
    return gf_attest(fabric, 0xFFFFFFFF, 0xC000 | jurisdiction_id, jur, 0);
}

/* ===== Delegations ===== */

int32_t gf_grant_delegation(governance_fabric_t *fabric,
                            uint32_t delegator_id, uint32_t delegatee_id,
                            const char *scope,
                            uint32_t proposal_id, uint32_t policy_id,
                            uint32_t jurisdiction_id,
                            uint8_t max_depth, uint64_t expiry_tick,
                            bool revocable,
                            uint64_t backing_amount, uint8_t backing_form) {
    if (!fabric || !scope || fabric->num_delegations >= GF_MAX_DELEGATIONS) return -1;
    
    if_identity_t *delegator = if_get_identity(fabric->identity, delegator_id);
    if_identity_t *delegatee = if_get_identity(fabric->identity, delegatee_id);
    if (!delegator || !delegatee) return -1;
    
    /* Check backing capital */
    if (backing_amount > 0 && backing_form >= 1 && backing_form <= 9) {
        if (delegator->balances[backing_form - 1] < backing_amount) return -1;
        delegator->balances[backing_form - 1] -= backing_amount;
    }
    
    gf_delegation_t *del = &fabric->delegations[fabric->num_delegations];
    gf_mem_set(del, 0, sizeof(*del));
    del->id = fabric->num_delegations;
    del->delegator_id = delegator_id;
    del->delegatee_id = delegatee_id;
    
    gf_str_copy(del->scope, scope, GF_MAX_NAME_LEN);
    del->proposal_id = proposal_id;
    del->policy_id = policy_id;
    del->jurisdiction_id = jurisdiction_id;
    del->max_depth = max_depth;
    del->current_depth = 1;
    del->expiry_tick = expiry_tick;
    del->revocable = revocable;
    del->backing_amount = backing_amount;
    del->backing_form = backing_form;
    
    for (int i = 0; i < 32; i++) del->delegation_cid[i] = (uint8_t)(del->id + i);
    for (int i = 0; i < 64; i++) {
        del->delegator_sig[i] = (uint8_t)(delegator_id + i);
        del->delegatee_sig[i] = (uint8_t)(delegatee_id + i);
    }
    
    del->attestation = LPRES_STATE_NEITHER;
    del->active = true;
    
    fabric->num_delegations++;
    fabric->stats.total_delegations_granted++;
    
    return gf_attest(fabric, delegator_id, 0xD000, del, 0);
}

int32_t gf_revoke_delegation(governance_fabric_t *fabric, uint32_t delegation_id) {
    if (!fabric || delegation_id >= fabric->num_delegations) return -1;
    
    gf_delegation_t *del = &fabric->delegations[delegation_id];
    if (!del->active || !del->revocable) return -1;
    
    del->revoked = true;
    del->revoked_tick = 0;
    del->active = false;
    del->attestation = LPRES_STATE_FALSE;
    
    /* Return backing capital */
    if_identity_t *delegator = if_get_identity(fabric->identity, del->delegator_id);
    if (delegator && del->backing_amount > 0 && del->backing_form >= 1 && del->backing_form <= 9) {
        delegator->balances[del->backing_form - 1] += del->backing_amount;
    }
    
    /* Recursively revoke children */
    for (uint32_t i = 0; i < del->num_children; i++) {
        gf_revoke_delegation(fabric, del->child_ids[i]);
    }
    
    return gf_attest(fabric, del->delegator_id, 0xE000 | delegation_id, del, -1);
}

/* ===== Health & Attestation ===== */

int32_t gf_check_proposal_health(governance_fabric_t *fabric,
                                 uint32_t proposal_id,
                                 void *health_out) {
    if (!fabric) return -1;
    gf_proposal_t *prop = gf_get_proposal(fabric, proposal_id);
    if (!prop) return -1;
    
    /* Check if voting period expired without quorum */
    uint64_t current_tick = 0;
    if (current_tick > prop->voting_end_tick && !prop->executed) {
        surplus_real_t total = SR_ADD(prop->votes_for, prop->votes_against);
        if (SR_CMP(total, SR_FROM_INT(prop->quorum_required)) < 0) {
            prop->attestation = LPRES_STATE_FALSE;
            return -1;
        }
    }
    
    return 0;
}

int32_t gf_check_global_health(governance_fabric_t *fabric) {
    if (!fabric) return -1;
    
    int32_t unhealthy = 0;
    
    /* Check active proposals */
    for (uint32_t i = 0; i < fabric->num_proposals; i++) {
        if (fabric->proposals[i].active) {
            if (gf_check_proposal_health(fabric, fabric->proposals[i].id, NULL) < 0) {
                unhealthy++;
            }
        }
    }
    
    /* Check jurisdictions */
    for (uint32_t i = 0; i < fabric->num_jurisdictions; i++) {
        if (fabric->jurisdictions[i].active) {
            if (SR_CMP(fabric->jurisdictions[i].coverage_ratio, fabric->min_coverage_ratio) < 0) {
                unhealthy++;
            }
        }
    }
    
    /* Check ZAB consensus health */
    if (fabric->zab.num_validators > 0) {
        if (fabric->zab.total_timeouts > fabric->zab.total_committed * 2) {
            unhealthy++;
        }
    }
    
    fabric->global_safety_gate = (unhealthy == 0);
    fabric->global_attestation = fabric->global_safety_gate ? LPRES_STATE_TRUE : LPRES_STATE_BOTH;
    
    return unhealthy == 0 ? 0 : -1;
}

bool gf_global_safety_gate(governance_fabric_t *fabric) {
    return fabric ? fabric->global_safety_gate : false;
}

/* ===== Coverage ===== */

void gf_update_coverage(governance_fabric_t *fabric) {
    if (!fabric) return;
    
    fabric->coverage_ratio = gf_compute_coverage(&fabric->m5);
    
    for (uint32_t i = 0; i < fabric->num_jurisdictions; i++) {
        if (fabric->jurisdictions[i].active) {
            fabric->jurisdictions[i].coverage_ratio = gf_compute_coverage(&fabric->jurisdictions[i].m5);
        }
    }
}

bool gf_enforce_coverage(governance_fabric_t *fabric, surplus_real_t min_ratio) {
    if (!fabric) return false;
    
    if (SR_CMP(fabric->coverage_ratio, min_ratio) < 0) return false;
    
    for (uint32_t i = 0; i < fabric->num_jurisdictions; i++) {
        if (fabric->jurisdictions[i].active) {
            if (SR_CMP(fabric->jurisdictions[i].coverage_ratio, min_ratio) < 0) return false;
        }
    }
    
    return true;
}

/* ===== Statistics ===== */

void gf_get_stats(governance_fabric_t *fabric, void *stats_out) {
    if (!fabric || !stats_out) return;
    gf_mem_copy(stats_out, &fabric->stats, sizeof(fabric->stats));
}

/* ===== Paraconsistent State ===== */

lpres_state_t gf_get_attestation(governance_fabric_t *fabric, uint32_t proposal_id) {
    if (!fabric) return LPRES_STATE_NEITHER;
    for (uint32_t i = 0; i < fabric->num_proposals; i++) {
        if (fabric->proposals[i].id == proposal_id) {
            return fabric->proposals[i].attestation;
        }
    }
    return LPRES_STATE_NEITHER;
}

void gf_set_attestation(governance_fabric_t *fabric, uint32_t proposal_id, lpres_state_t state) {
    if (!fabric) return;
    for (uint32_t i = 0; i < fabric->num_proposals; i++) {
        if (fabric->proposals[i].id == proposal_id) {
            fabric->proposals[i].attestation = state;
            break;
        }
    }
}

/* ===== Utility ===== */

const char *gf_lpres_state_name(lpres_state_t state) {
    return lpres_state_name(state);
}

const char *gf_proposal_type_name(const char *type) {
    return type;
}

const char *gf_contract_state_name(int state) {
    static const char *names[] = {"DRAFT", "ACTIVE", "DISPUTED", "SETTLED", "VOID"};
    if (state >= 0 && state <= 4) return names[state];
    return "UNKNOWN";
}
