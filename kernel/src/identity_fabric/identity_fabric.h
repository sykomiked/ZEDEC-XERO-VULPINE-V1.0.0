/* identity_fabric.h — ZXV Identity Fabric Compound Module
 *
 * The Identity Fabric unifies all identity, reputation, governance, and
 * consensus modules into a single coherent fabric for sovereign identity,
 * credential management, reputation scoring, and Byzantine fault-tolerant
 * consensus.
 *
 * Sub-modules integrated:
 *   1. Identity — Sovereign identity with 168-bit critical words
 *   2. Vino — Nine-form capital ledger with vouchers
 *   3. Vena — Smart contract runtime for identity logic
 *   4. ZAB — Zero-Knowledge Atomic Broadcast consensus
 *   5. Reputation — Multi-dimensional reputation scoring
 *   6. Concord — Governance protocol for collective decisions
 *   7. Crown — Sovereign authority and delegation
 *   8. Ministry — Administrative roles and permissions
 *   9. OnePolicy — Unified policy engine
 *   10. Legal Engine — Computable law and contract enforcement
 *   11. Porter House — Trust-gated access control
 *   12. Interspace — Cross-jurisdiction identity federation
 *
 * Design principles:
 * - All identities are 168-bit critical words (word168_t)
 * - Reputation is multi-dimensional, not a single score
 * - Consensus is ZK-ABFT (Zero-Knowledge Atomic Broadcast Fault Tolerant)
 * - Governance is computable law, not social consensus
 * - Credentials are Vino vouchers on specific capital forms
 * - Delegation chains are cryptographically verifiable
 * - Cross-jurisdiction federation via Interspace corridors
 * - Paraconsistent logic (LPRES) for all attestations
 * - M5 coverage hyperbola enforcement on all identity operations
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef IDENTITY_FABRIC_H
#define IDENTITY_FABRIC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "identity.h"
#include "vino/vino.h"
#include "vena/vena.h"
#include "zab.h"
#include "reputation.h"
#include "concord.h"
#include "crown.h"
#include "ministry.h"
#include "onepolicy.h"
#include "legal_engine.h"
#include "porter_house.h"
#include "interspace/interspace.h"
#include "interspace/lex_rhodia.h"
#include "interspace/flagstate.h"
#include "interspace/federation.h"
#include "interspace/minister.h"
#include "orbital_fabric.h"
/* Forward declaration for optional fabric */
struct financial_fabric;
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"

/* ===== Constants ===== */

#define IF_MAX_IDENTITIES        1024
#define IF_MAX_CREDENTIALS       4096
#define IF_MAX_DELEGATIONS       2048
#define IF_MAX_REPUTATION_DIMS   16
#define IF_MAX_CONSENSUS_NODES   64
#define IF_MAX_POLICIES          512
#define IF_MAX_LEGAL_CONTRACTS   1024
#define IF_MAX_CORRIDORS         256
#define IF_MAX_NAME_LEN          64

/* ===== Identity Fabric Identity ===== */

typedef struct if_identity {
    uint32_t id;
    word168_t critical_word;         /* 168-bit sovereign identifier */
    char name[IF_MAX_NAME_LEN];      /* Human-readable name */
    
    /* Identity metadata */
    uint64_t created_tick;
    uint64_t last_active_tick;
    uint32_t incarnation;            /* Changes on key rotation */
    
    /* Vino capital balances (9 forms) */
    uint64_t balances[9];
    
    /* Credentials held */
    uint32_t credential_ids[IF_MAX_CREDENTIALS];
    uint32_t num_credentials;
    
    /* Delegations granted */
    uint32_t delegation_ids[IF_MAX_DELEGATIONS];
    uint32_t num_delegations;
    
    /* Delegations received */
    uint32_t received_delegation_ids[IF_MAX_DELEGATIONS];
    uint32_t num_received_delegations;
    
    /* Reputation (multi-dimensional) */
    surplus_real_t reputation_dims[IF_MAX_REPUTATION_DIMS];
    uint32_t num_reputation_dims;
    surplus_real_t composite_reputation;
    
    /* Governance */
    uint32_t concord_member_id;      /* Concord membership */
    uint32_t crown_delegation_id;    /* Crown delegation */
    uint32_t ministry_role_id;       /* Ministry role */
    
    /* Consensus participation */
    bool zab_validator;              /* Participates in ZAB consensus */
    uint32_t zab_node_id;
    uint64_t zab_stake;              /* Stake for consensus */
    
    /* Legal */
    uint32_t legal_contract_ids[IF_MAX_LEGAL_CONTRACTS];
    uint32_t num_legal_contracts;
    
    /* Interspace federation */
    uint32_t corridor_ids[IF_MAX_CORRIDORS];
    uint32_t num_corridors;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* Paraconsistent state */
    lpres_state_t attestation;
    bool active;
    bool revoked;
} if_identity_t;

/* ===== Credential (Vino Voucher with Identity Context) ===== */

typedef struct if_credential {
    uint32_t id;
    uint32_t holder_id;              /* Identity holding this credential */
    uint32_t issuer_id;              /* Identity that issued it */
    
    /* Credential details */
    uint8_t capital_form;            /* 1-9: which capital form */
    uint64_t amount;                 /* Voucher amount */
    uint64_t expiry_tick;            /* Expiry (0 = no expiry) */
    
    /* Schema */
    char schema[IF_MAX_NAME_LEN];    /* e.g., "identity.passport", "governance.voting_right" */
    uint16_t schema_version;
    
    /* Cryptographic proof */
    uint8_t issuer_signature[64];    /* Ed25519 or post-quantum */
    uint8_t credential_cid[32];      /* Content ID */
    
    /* Revocation */
    bool revoked;
    uint64_t revoked_tick;
    uint8_t revocation_cid[32];
    
    /* LPRES attestation */
    lpres_state_t attestation;
    
    bool active;
} if_credential_t;

/* ===== Delegation Chain ===== */

typedef struct if_delegation {
    uint32_t id;
    uint32_t delegator_id;           /* Identity granting delegation */
    uint32_t delegatee_id;           /* Identity receiving delegation */
    
    /* Scope */
    char scope[IF_MAX_NAME_LEN];     /* e.g., "governance.vote", "treasury.spend" */
    uint8_t max_depth;               /* Max delegation chain depth */
    uint8_t current_depth;           /* Current depth in chain */
    
    /* Constraints */
    uint64_t max_amount;             /* Max capital amount (if financial) */
    uint64_t expiry_tick;            /* Delegation expiry */
    bool revocable;                  /* Can be revoked */
    
    /* Cryptographic proof */
    uint8_t delegation_cid[32];
    uint8_t delegator_signature[64];
    uint8_t delegatee_acceptance[64];
    
    /* Chain */
    uint32_t parent_delegation_id;   /* Parent in delegation chain */
    uint32_t child_delegation_ids[8]; /* Children */
    uint32_t num_children;
    
    /* Status */
    bool active;
    bool revoked;
    uint64_t revoked_tick;
    
    /* LPRES attestation */
    lpres_state_t attestation;
} if_delegation_t;

/* ===== Reputation Dimension ===== */

typedef struct if_reputation_dimension {
    char name[IF_MAX_NAME_LEN];      /* e.g., "technical", "governance", "financial" */
    surplus_real_t weight;           /* Weight in composite score */
    
    /* Scoring factors */
    struct {
        char factor[IF_MAX_NAME_LEN];
        surplus_real_t weight;
        surplus_real_t (*compute_fn)(if_identity_t *identity, void *context);
        void *context;
    } factors[8];
    uint32_t num_factors;
    
    /* Decay */
    surplus_real_t decay_rate;       /* Per tick decay */
    uint64_t last_update_tick;
    
    bool active;
} if_reputation_dimension_t;

/* ===== Consensus State ===== */

typedef struct if_consensus_state {
    /* ZAB consensus */
    zab_state_t zab_state;
    uint32_t zab_view;
    uint32_t zab_leader;
    uint64_t zab_last_committed_tick;
    
    /* Validator set */
    uint32_t validator_ids[IF_MAX_CONSENSUS_NODES];
    uint32_t num_validators;
    uint64_t total_stake;
    
    /* Quorum */
    uint32_t quorum_size;
    uint32_t fast_quorum_size;
    
    /* Statistics */
    uint64_t total_proposals;
    uint64_t total_committed;
    uint64_t total_view_changes;
    uint64_t total_timeouts;
    
    /* LPRES attestation */
    lpres_state_t consensus_attestation;
} if_consensus_state_t;

/* ===== Governance State ===== */

typedef struct if_governance_state {
    /* Concord */
    concord_state_t concord_state;
    uint32_t proposal_count;
    uint32_t active_proposals;
    
    /* Crown */
    crown_state_t crown_state;
    uint32_t sovereign_id;
    uint32_t delegation_count;
    
    /* Ministry */
    ministry_state_t ministry_state;
    uint32_t active_ministers;
    
    /* OnePolicy */
    onepolicy_state_t policy_state;
    uint32_t active_policies;
    
    /* Legal Engine */
    legal_engine_state_t legal_state;
    uint32_t active_contracts;
    
    /* LPRES attestation */
    lpres_state_t governance_attestation;
} if_governance_state_t;

/* ===== Federation Status ===== */

typedef enum {
    FEDERATION_INACTIVE = 0,
    FEDERATION_PENDING = 1,
    FEDERATION_ACTIVE = 2,
    FEDERATION_SUSPENDED = 3,
    FEDERATION_REVOKED = 4
} federation_status_t;

/* ===== Interspace Federation ===== */

typedef struct if_corridor {
    uint32_t id;
    char name[IF_MAX_NAME_LEN];
    uint32_t local_identity_id;
    uint32_t remote_identity_id;     /* Identity in federated space */
    
    /* Corridor metadata */
    uint8_t trust_level;             /* 0=none, 1=basic, 2=verified, 3=full */
    uint64_t established_tick;
    uint64_t last_sync_tick;
    
    /* Lex Rhodia parameters */
    lex_rhodia_params_t lex_params;
    
    /* Flagstate */
    flagstate_t flagstate;
    
    /* Federation status */
    federation_status_t fed_status;
    
    /* Minister */
    minister_t minister;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* LPRES attestation */
    lpres_state_t corridor_attestation;
    
    bool active;
} if_corridor_t;

/* ===== Identity Fabric ===== */

typedef struct identity_fabric {
    /* Core sub-modules */
    identity_registry_t identity;     /* Identity registry */
    vino_ledger_t vino;               /* Vino Ledger */
    vena_runtime_t vena;              /* Vena Runtime */
    zab_consensus_t zab;              /* ZAB Consensus */
    reputation_engine_t reputation;   /* Reputation Engine */
    concord_t concord;                /* Concord Governance */
    crown_t crown;                    /* Crown Authority */
    ministry_t ministry;              /* Ministry Administration */
    onepolicy_t policy;               /* OnePolicy Engine */
    legal_engine_t legal;             /* Legal Engine */
    porter_house_t porter;            /* Porter House Trust */
    interspace_t interspace;          /* Interspace Federation */
    
    /* Fabric-level state */
    if_identity_t identities[IF_MAX_IDENTITIES];
    uint32_t num_identities;
    uint32_t next_identity_id;
    
    if_credential_t credentials[IF_MAX_CREDENTIALS];
    uint32_t num_credentials;
    
    if_delegation_t delegations[IF_MAX_DELEGATIONS];
    uint32_t num_delegations;
    
    if_reputation_dimension_t reputation_dims[IF_MAX_REPUTATION_DIMS];
    uint32_t num_reputation_dims;
    
    if_consensus_state_t consensus;
    
    if_governance_state_t governance;
    
    if_corridor_t corridors[IF_MAX_CORRIDORS];
    uint32_t num_corridors;
    
    /* Integration references */
    struct financial_fabric *financial;    /* Reference to financial fabric */
    orbital_fabric_t *orbital;        /* Reference to orbital fabric */
    
    /* Global statistics */
    struct {
        uint64_t total_identities_created;
        uint64_t total_credentials_issued;
        uint64_t total_delegations_granted;
        uint64_t total_reputation_updates;
        uint64_t total_consensus_rounds;
        uint64_t total_governance_proposals;
        uint64_t total_legal_contracts;
        uint64_t total_corridors_established;
        uint64_t total_cross_federation_ops;
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
        bool require_porter_house_trust;
        bool auto_revocation_on_compromise;
        surplus_real_t min_reputation_for_consensus;
        uint32_t max_delegation_depth;
        uint64_t credential_default_expiry;
        bool enable_interspace_federation;
    } config;
    
    bool initialized;
} identity_fabric_t;

/* ===== API ===== */

/* Initialize the Identity Fabric */
void if_init(identity_fabric_t *fabric, 
             struct financial_fabric *financial,
             orbital_fabric_t *orbital);

/* Register built-in identity modules */
void if_register_builtins(identity_fabric_t *fabric);

/* ===== Identity Management ===== */

int32_t if_create_identity(identity_fabric_t *fabric, const char *name,
                           const word168_t *critical_word,
                           const uint64_t initial_balances[9]);

if_identity_t *if_get_identity(identity_fabric_t *fabric, uint32_t identity_id);
if_identity_t *if_get_identity_by_critical_word(identity_fabric_t *fabric, const word168_t *cw);
if_identity_t *if_get_identity_by_name(identity_fabric_t *fabric, const char *name);

/* Identity operations */
int32_t if_rotate_keys(identity_fabric_t *fabric, uint32_t identity_id,
                       const word168_t *new_critical_word);
int32_t if_revoke_identity(identity_fabric_t *fabric, uint32_t identity_id);

/* ===== Credentials ===== */

int32_t if_issue_credential(identity_fabric_t *fabric,
                            uint32_t issuer_id, uint32_t holder_id,
                            uint8_t capital_form, uint64_t amount,
                            const char *schema, uint16_t schema_version,
                            uint64_t expiry_tick);

int32_t if_verify_credential(identity_fabric_t *fabric, uint32_t credential_id);
int32_t if_revoke_credential(identity_fabric_t *fabric, uint32_t credential_id);

/* ===== Delegations ===== */

int32_t if_grant_delegation(identity_fabric_t *fabric,
                            uint32_t delegator_id, uint32_t delegatee_id,
                            const char *scope, uint8_t max_depth,
                            uint64_t max_amount, uint64_t expiry_tick,
                            bool revocable);

int32_t if_revoke_delegation(identity_fabric_t *fabric, uint32_t delegation_id);
int32_t if_verify_delegation_chain(identity_fabric_t *fabric,
                                   uint32_t delegator_id, uint32_t delegatee_id,
                                   const char *scope);

/* ===== Reputation ===== */

int32_t if_add_reputation_dimension(identity_fabric_t *fabric,
                                    const char *name, surplus_real_t weight);

int32_t if_update_reputation(identity_fabric_t *fabric, uint32_t identity_id,
                             uint32_t dim_id, surplus_real_t delta);

surplus_real_t if_get_composite_reputation(identity_fabric_t *fabric, uint32_t identity_id);

/* ===== Consensus ===== */

int32_t if_join_consensus(identity_fabric_t *fabric, uint32_t identity_id,
                          uint64_t stake);

int32_t if_leave_consensus(identity_fabric_t *fabric, uint32_t identity_id);

int32_t if_propose_consensus(identity_fabric_t *fabric, uint32_t proposer_id,
                             const void *proposal, uint32_t proposal_len);

/* ===== Governance ===== */

int32_t if_submit_proposal(identity_fabric_t *fabric, uint32_t proposer_id,
                           const char *proposal_type, const void *data, uint32_t len);

int32_t if_vote_proposal(identity_fabric_t *fabric, uint32_t voter_id,
                         uint32_t proposal_id, bool approve);

int32_t if_execute_proposal(identity_fabric_t *fabric, uint32_t proposal_id);

/* ===== Legal ===== */

int32_t if_create_legal_contract(identity_fabric_t *fabric,
                                 uint32_t party_a, uint32_t party_b,
                                 const char *contract_type, const void *terms, uint32_t len);

int32_t if_enforce_contract(identity_fabric_t *fabric, uint32_t contract_id);

/* ===== Interspace Federation ===== */

int32_t if_establish_corridor(identity_fabric_t *fabric,
                              uint32_t local_identity_id,
                              const word168_t *remote_critical_word,
                              const char *corridor_name,
                              uint8_t trust_level);

int32_t if_sync_corridor(identity_fabric_t *fabric, uint32_t corridor_id);

int32_t if_federate_identity(identity_fabric_t *fabric,
                             uint32_t corridor_id,
                             uint32_t local_identity_id,
                             const word168_t *remote_critical_word);

/* ===== Health & Attestation ===== */

int32_t if_check_identity_health(identity_fabric_t *fabric,
                                 uint32_t identity_id,
                                 void *health_out);

int32_t if_check_global_health(identity_fabric_t *fabric);

bool if_global_safety_gate(identity_fabric_t *fabric);

lpres_state_t if_attest(identity_fabric_t *fabric, uint32_t identity_id,
                        uint32_t op_id, void *args, int32_t result);

/* Coverage enforcement */
void if_update_coverage(identity_fabric_t *fabric);
bool if_enforce_coverage(identity_fabric_t *fabric, surplus_real_t min_ratio);

/* Statistics */
void if_get_stats(identity_fabric_t *fabric, void *stats_out);

/* Paraconsistent state */
lpres_state_t if_get_attestation(identity_fabric_t *fabric, uint32_t identity_id);
void if_set_attestation(identity_fabric_t *fabric, uint32_t identity_id, lpres_state_t state);

/* Utility */
const char *if_lpres_state_name(lpres_state_t state);
const char *if_capital_form_name(uint8_t form);

#endif /* IDENTITY_FABRIC_H */
