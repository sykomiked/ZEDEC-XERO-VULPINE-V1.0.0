/* governance_core.h — Governance & Tokenomics Layer
 * 11-branch DAO governance, amplitude-weighted voting,
 * golden-ratio-scaled token emissions and staking rewards.
 * Per Cosmic AI Master Build Plan Ch. 10/14/15.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef GOVERNANCE_CORE_H
#define GOVERNANCE_CORE_H

#include "m5_host_float.h" /* host-only module: double / double complex */

#define GOV_MAX_BRANCHES 11
#define GOV_MAX_PROPOSALS 64
#define GOV_MAX_VOTERS 256
#define GOV_MAX_TOKEN_SUPPLY 10000000000ULL

typedef enum {
    GOV_BRANCH_KERNEL = 0,
    GOV_BRANCH_SECURITY = 1,
    GOV_BRANCH_FINANCE = 2,
    GOV_BRANCH_ETHICS = 3,
    GOV_BRANCH_EDUCATION = 4,
    GOV_BRANCH_INFRASTRUCTURE = 5,
    GOV_BRANCH_RESEARCH = 6,
    GOV_BRANCH_COMMUNITY = 7,
    GOV_BRANCH_AUDIT = 8,
    GOV_BRANCH_LEGAL = 9,
    GOV_BRANCH_TREASURY = 10
} gov_branch_t;

typedef struct gov_token_balance {
    uint32_t holder_id;
    uint64_t stake;
    uint64_t credibility;
    double emotion_index;
    double egv_coherence;
} gov_token_balance_t;

typedef struct gov_vote {
    uint32_t voter_id;
    gov_branch_t branch;
    trit_t vote;
    rational_t weight;
    double emotion_amplitude;
    ordinal_t timestamp;
} gov_vote_t;

typedef struct gov_proposal {
    uint32_t id;
    char title[128];
    gov_branch_t branch;
    ordinal_t open_cycle;
    ordinal_t close_cycle;
    gov_vote_t votes[GOV_MAX_VOTERS];
    uint32_t num_votes;
    rational_t for_weight;
    rational_t against_weight;
    bool passed;
    bool closed;
} gov_proposal_t;

typedef struct governance_state {
    gov_proposal_t proposals[GOV_MAX_PROPOSALS];
    uint32_t num_proposals;
    gov_token_balance_t balances[GOV_MAX_VOTERS];
    uint32_t num_holders;
    uint64_t total_supply;
    uint64_t circulating_supply;
    ordinal_t current_cycle;
    axiom_matrix_t *matrix;
} governance_state_t;

void governance_init(governance_state_t *g, axiom_matrix_t *matrix);
uint32_t governance_register_holder(governance_state_t *g, uint64_t stake, uint64_t credibility);
uint32_t governance_create_proposal(governance_state_t *g, const char *title,
                                     gov_branch_t branch, ordinal_t duration);
int governance_cast_vote(governance_state_t *g, uint32_t proposal_idx,
                          uint32_t voter_id, trit_t vote, double emotion_index);
rational_t governance_compute_vote_weight(const governance_state_t *g, uint32_t voter_id,
                                           double emotion_index);
int governance_close_proposal(governance_state_t *g, uint32_t proposal_idx);
uint64_t governance_token_emission(ordinal_t cycle);
double governance_vortex_reduce(uint64_t n);

#endif
