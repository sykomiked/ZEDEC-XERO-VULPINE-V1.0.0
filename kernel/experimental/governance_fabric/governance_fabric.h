/* governance_fabric.h — ZXV Governance Fabric Compound Module
 *
 * The Governance Fabric unifies all governance, law, policy, and consensus
 * modules into a single coherent fabric for computable law, collective
 * decision-making, and sovereign authority.
 *
 * Sub-modules integrated:
 *   1. Legal Engine — Computable law and contract enforcement
 *   2. Concord — Governance protocol for collective decisions
 *   3. Ministry — Administrative roles and permissions
 *   4. Crown — Sovereign authority and delegation
 *   5. OnePolicy — Unified policy engine
 *   6. Reputation — Multi-dimensional reputation scoring
 *   7. ZAB — Zero-Knowledge Atomic Broadcast consensus
 *   8. Porter House — Trust-gated access control
 *   9. Interspace — Cross-jurisdiction federation
 *   10. Identity Fabric — Sovereign identity foundation
 *   11. Financial Fabric — Capital-backed governance
 *
 * Design principles:
 * - Law is computable, not interpretive
 * - Governance is capital-weighted, not one-person-one-vote
 * - Consensus is ZK-ABFT with economic finality
 * - Policy is enforced at kernel level, not application level
 * - Delegation chains are cryptographically verifiable
 * - Cross-jurisdiction via Interspace corridors (Lex Rhodia)
 * - Paraconsistent logic (LPRES) for all attestations
 * - M5 coverage hyperbola enforcement on all governance operations
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef GOVERNANCE_FABRIC_H
#define GOVERNANCE_FABRIC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "legal_engine.h"
#include "concord.h"
#include "ministry.h"
#include "crown.h"
#include "onepolicy.h"
#include "reputation.h"
#include "zab.h"
#include "porter_house.h"
#include "interspace/interspace.h"
#include "interspace/lex_rhodia.h"
#include "interspace/flagstate.h"
#include "interspace/federation.h"
#include "interspace/minister.h"
#include "identity_fabric.h"
#include "financial_fabric.h"
#include "orbital_fabric.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"

/* ===== Constants ===== */

#define GF_MAX_PROPOSALS         2048
#define GF_MAX_VOTES             8192
#define GF_MAX_POLICIES          1024
#define GF_MAX_CONTRACTS         4096
#define GF_MAX_DELEGATIONS       4096
#define GF_MAX_JURISDICTIONS     256
#define GF_MAX_NAME_LEN          64

/* ===== Governance Proposal ===== */

typedef struct gf_proposal {
    uint32_t id;
    char title[GF_MAX_NAME_LEN];
    char type[GF_MAX_NAME_LEN];      /* "policy", "contract", "budget", "amendment", "emergency" */
    
    /* Proposer */
    uint32_t proposer_id;            /* Identity Fabric identity */
    uint64_t proposer_stake;         /* Capital form 5 (Financial) stake */
    
    /* Content */
    void *data;
    uint32_t data_len;
    uint8_t data_cid[32];
    
    /* Voting */
    uint64_t voting_start_tick;
    uint64_t voting_end_tick;
    uint64_t quorum_required;        /* Capital-weighted quorum */
    uint64_t votes_for;
    uint64_t votes_against;
    uint64_t votes_abstain;
    
    /* Execution */
    bool executed;
    uint64_t execution_tick;
    int32_t execution_result;
    
    /* LPRES attestation */
    lpres_state_t attestation;
    
    bool active;
} gf_proposal_t;

/* ===== Vote ===== */

typedef struct gf_vote {
    uint32_t id;
    uint32_t proposal_id;
    uint32_t voter_id;
    uint64_t weight;                 /* Capital-weighted vote */
    bool approve;
    uint64_t tick;
    uint8_t signature[64];
    lpres_state_t attestation;
    bool counted;
} gf_vote_t;

/* ===== Policy ===== */

typedef struct gf_policy {
    uint32_t id;
    char name[GF_MAX_NAME_LEN];
    char domain[GF_MAX_NAME_LEN];    /* "financial", "identity", "compute", "network" */
    
    /* Policy rules */
    struct {
        char condition[GF_MAX_NAME_LEN];
        char action[GF_MAX_NAME_LEN];
        surplus_real_t threshold;
        bool active;
    } rules[32];
    uint32_t num_rules;
    
    /* Enforcement */
    uint32_t enforcement_domain_id;  /* Compute Fabric domain for enforcement */
    bool kernel_enforced;
    
    /* LPRES attestation */
    lpres_state_t attestation;
    
    bool active;
} gf_policy_t;

/* ===== Legal Contract ===== */

typedef struct gf_legal_contract {
    uint32_t id;
    char contract_type[GF_MAX_NAME_LEN];
    
    /* Parties */
    uint32_t party_ids[8];
    uint32_t num_parties;
    uint64_t party_stakes[8];        /* Capital at stake per party */
    
    /* Terms */
    void *terms;
    uint32_t terms_len;
    uint8_t terms_cid[32];
    
    /* State */
    enum {
        GF_CONTRACT_DRAFT = 0,
        GF_CONTRACT_ACTIVE = 1,
        GF_CONTRACT_DISPUTED = 2,
        GF_CONTRACT_SETTLED = 3,
        GF_CONTRACT_VOID = 4
    } state;
    
    /* Dispute resolution */
    uint32_t arbiter_id;
    uint64_t dispute_deadline;
    
    /* LPRES attestation */
    lpres_state_t attestation;
    
    bool active;
} gf_legal_contract_t;

/* ===== Jurisdiction ===== */

typedef struct gf_jurisdiction {
    uint32_t id;
    char name[GF_MAX_NAME_LEN];
    char code[8];                    /* e.g., "US-CA", "EU-DE", "ZXV-CORE" */
    
    /* Legal framework */
    char legal_framework[GF_MAX_NAME_LEN];
    uint32_t legal_engine_id;
    
    /* Governance */
    uint32_t concord_id;
    uint32_t crown_id;
    uint32_t ministry_id;
    uint32_t policy_engine_id;
    
    /* Capital requirements */
    uint64_t min_stake_for_voting;   /* Form 5 capital */
    uint64_t min_stake_for_proposing;
    
    /* Interspace */
    uint32_t corridor_ids[16];
    uint32_t num_corridors;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* LPRES attestation */
    lpres_state_t attestation;
    
    bool active;
} gf_jurisdiction_t;

/* ===== Delegation (Governance-specific) ===== */

typedef struct gf_delegation {
    uint32_t id;
    uint32_t delegator_id;
    uint32_t delegatee_id;
    
    /* Scope */
    char scope[GF_MAX_NAME_LEN];     /* "vote", "propose", "execute", "arbiter" */
    uint32_t proposal_id;            /* Specific proposal or 0 for general */
    uint32_t policy_id;              /* Specific policy or 0 for general */
    uint32_t jurisdiction_id;        /* Specific jurisdiction or 0 for all */
    
    /* Constraints */
    uint8_t max_depth;
    uint8_t current_depth;
    uint64_t expiry_tick;
    bool revocable;
    
    /* Capital backing */
    uint64_t backing_amount;         /* Form 5 capital */
    uint8_t backing_form;
    
    /* Cryptographic */
    uint8_t delegation_cid[32];
    uint8_t delegator_sig[64];
    uint8_t delegatee_sig[64];
    
    /* Chain */
    uint32_t parent_id;
    uint32_t child_ids[8];
    uint32_t num_children;
    
    /* Status */
    bool active;
    bool revoked;
    uint64_t revoked_tick;
    
    /* LPRES attestation */
    lpres_state_t attestation;
} gf_delegation_t;

/* ===== Governance Fabric ===== */

typedef struct governance_fabric {
    /* Core sub-modules */
    legal_engine_t legal;            /* Legal Engine */
    concord_t concord;               /* Concord Governance */
    ministry_t ministry;             /* Ministry Administration */
    crown_t crown;                   /* Crown Authority */
    onepolicy_t policy;              /* OnePolicy Engine */
    reputation_engine_t reputation;  /* Reputation Engine */
    zab_consensus_t zab;             /* ZAB Consensus */
    porter_house_t porter;           /* Porter House Trust */
    interspace_t interspace;         /* Interspace Federation */
    
    /* Integration references */
    identity_fabric_t *identity;     /* Identity Fabric */
    financial_fabric_t *financial;   /* Financial Fabric */
    orbital_fabric_t *orbital;       /* Orbital Fabric */
    
    /* Fabric-level state */
    gf_proposal_t proposals[GF_MAX_PROPOSALS];
    uint32_t num_proposals;
    uint32_t next_proposal_id;
    
    gf_vote_t votes[GF_MAX_VOTES];
    uint32_t num_votes;
    
    gf_policy_t policies[GF_MAX_POLICIES];
    uint32_t num_policies;
    
    gf_legal_contract_t contracts[GF_MAX_CONTRACTS];
    uint32_t num_contracts;
    
    gf_jurisdiction_t jurisdictions[GF_MAX_JURISDICTIONS];
    uint32_t num_jurisdictions;
    
    gf_delegation_t delegations[GF_MAX_DELEGATIONS];
    uint32_t num_delegations;
    
    /* Global statistics */
    struct {
        uint64_t total_proposals;
        uint64_t total_votes_cast;
        uint64_t total_policies_enforced;
        uint64_t total_contracts_executed;
        uint64_t total_disputes_resolved;
        uint64_t total_delegations_granted;
        uint64_t total_jurisdictions_federated;
        uint64_t total_capital_staked;
        uint64_t total_capital_slashed;
    } stats;
    
    /* Paraconsistent global state */
    lpres_state_t global_attestation;
    bool global_safety_gate;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    surplus_real_t min_coverage_ratio;
    
    /* Configuration */
    struct {
        bool require_porter_house_for_voting;
        bool auto_slash_on_dishonest_vote;
        surplus_real_t min_reputation_for_proposing;
        uint64_t default_voting_period;
        bool enable_emergency_proposals;
        uint8_t emergency_quorum_form;  /* Capital form for emergency quorum */
    } config;
    
    bool initialized;
} governance_fabric_t;

/* ===== API ===== */

/* Initialize the Governance Fabric */
void gf_init(governance_fabric_t *fabric,
             identity_fabric_t *identity,
             financial_fabric_t *financial,
             orbital_fabric_t *orbital);

/* Register built-in governance modules */
void gf_register_builtins(governance_fabric_t *fabric);

/* ===== Proposal Management ===== */

int32_t gf_create_proposal(governance_fabric_t *fabric,
                           uint32_t proposer_id,
                           const char *title, const char *type,
                           const void *data, uint32_t data_len,
                           uint64_t voting_period,
                           uint64_t quorum_required);

gf_proposal_t *gf_get_proposal(governance_fabric_t *fabric, uint32_t proposal_id);

/* Voting */
int32_t gf_cast_vote(governance_fabric_t *fabric,
                     uint32_t proposal_id, uint32_t voter_id,
                     bool approve, uint64_t weight);

int32_t gf_tally_votes(governance_fabric_t *fabric, uint32_t proposal_id);

/* Execution */
int32_t gf_execute_proposal(governance_fabric_t *fabric, uint32_t proposal_id);

/* ===== Policy Management ===== */

int32_t gf_create_policy(governance_fabric_t *fabric,
                         const char *name, const char *domain,
                         const char *condition, const char *action,
                         surplus_real_t threshold);

int32_t gf_enforce_policy(governance_fabric_t *fabric, uint32_t policy_id,
                          void *context);

/* ===== Legal Contracts ===== */

int32_t gf_create_contract(governance_fabric_t *fabric,
                           const char *contract_type,
                           const uint32_t *party_ids, uint32_t num_parties,
                           const uint64_t *party_stakes,
                           const void *terms, uint32_t terms_len);

int32_t gf_execute_contract(governance_fabric_t *fabric, uint32_t contract_id);

int32_t gf_dispute_contract(governance_fabric_t *fabric, uint32_t contract_id,
                            uint32_t disputant_id, const char *reason);

int32_t gf_resolve_dispute(governance_fabric_t *fabric, uint32_t contract_id,
                           uint32_t arbiter_id, int32_t ruling);

/* ===== Jurisdictions ===== */

int32_t gf_create_jurisdiction(governance_fabric_t *fabric,
                               const char *name, const char *code,
                               const char *legal_framework);

int32_t gf_federate_jurisdiction(governance_fabric_t *fabric,
                                 uint32_t jurisdiction_id,
                                 uint32_t corridor_id);

/* ===== Delegations ===== */

int32_t gf_grant_delegation(governance_fabric_t *fabric,
                            uint32_t delegator_id, uint32_t delegatee_id,
                            const char *scope,
                            uint32_t proposal_id, uint32_t policy_id,
                            uint32_t jurisdiction_id,
                            uint8_t max_depth, uint64_t expiry_tick,
                            bool revocable,
                            uint64_t backing_amount, uint8_t backing_form);

int32_t gf_revoke_delegation(governance_fabric_t *fabric, uint32_t delegation_id);

/* ===== Health & Attestation ===== */

int32_t gf_check_proposal_health(governance_fabric_t *fabric,
                                 uint32_t proposal_id,
                                 void *health_out);

int32_t gf_check_global_health(governance_fabric_t *fabric);

bool gf_global_safety_gate(governance_fabric_t *fabric);

lpres_state_t gf_attest(governance_fabric_t *fabric, uint32_t proposal_id,
                        uint32_t op_id, void *args, int32_t result);

/* Coverage enforcement */
void gf_update_coverage(governance_fabric_t *fabric);
bool gf_enforce_coverage(governance_fabric_t *fabric, surplus_real_t min_ratio);

/* Statistics */
void gf_get_stats(governance_fabric_t *fabric, void *stats_out);

/* Paraconsistent state */
lpres_state_t gf_get_attestation(governance_fabric_t *fabric, uint32_t proposal_id);
void gf_set_attestation(governance_fabric_t *fabric, uint32_t proposal_id, lpres_state_t state);

/* Utility */
const char *gf_lpres_state_name(lpres_state_t state);
const char *gf_proposal_type_name(const char *type);
const char *gf_contract_state_name(int state);

#endif /* GOVERNANCE_FABRIC_H */
