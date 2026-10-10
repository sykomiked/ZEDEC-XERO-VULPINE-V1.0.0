/* governance_core.c — Governance & Tokenomics Implementation
 * Vote weight = stake * (credibility + emotion_index + egv_coherence).
 * Token emissions follow golden-ratio spiral vesting.
 * Vortex reduction for flow normalization.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "governance_core.h"
#include "axiom_matrix_core.h"
#include "choice_core.h"
#include "rmag_core.h"
#include <string.h>
#include <math.h>

static const double GOV_PHI = 1.61803398874989484820;

void governance_init(governance_state_t *g, axiom_matrix_t *matrix) {
    memset(g, 0, sizeof(governance_state_t));
    g->matrix = matrix;
    g->total_supply = GOV_MAX_TOKEN_SUPPLY;
    g->circulating_supply = 0;
    g->current_cycle = 0;
}

uint32_t governance_register_holder(governance_state_t *g, uint64_t stake, uint64_t credibility) {
    if (g->num_holders >= GOV_MAX_VOTERS) return UINT32_MAX;
    uint32_t idx = g->num_holders++;
    g->balances[idx].holder_id = idx;
    g->balances[idx].stake = stake;
    g->balances[idx].credibility = credibility;
    g->balances[idx].emotion_index = 0.0;
    g->balances[idx].egv_coherence = 0.0;
    g->circulating_supply += stake;
    return idx;
}

uint32_t governance_create_proposal(governance_state_t *g, const char *title,
                                     gov_branch_t branch, ordinal_t duration) {
    if (g->num_proposals >= GOV_MAX_PROPOSALS) return UINT32_MAX;
    uint32_t idx = g->num_proposals++;
    gov_proposal_t *p = &g->proposals[idx];
    memset(p, 0, sizeof(gov_proposal_t));
    p->id = idx;
    p->branch = branch;
    p->open_cycle = g->current_cycle;
    p->close_cycle = g->current_cycle + duration;
    p->num_votes = 0;
    p->for_weight = (rational_t){0, 1};
    p->against_weight = (rational_t){0, 1};
    p->passed = false;
    p->closed = false;
    if (title) {
        size_t n = strlen(title);
        if (n > 127) n = 127;
        memcpy(p->title, title, n);
    }
    return idx;
}

rational_t governance_compute_vote_weight(const governance_state_t *g, uint32_t voter_id,
                                           double emotion_index) {
    if (voter_id >= g->num_holders) return (rational_t){0, 1};
    const gov_token_balance_t *b = &g->balances[voter_id];
    double credibility_factor = (double)b->credibility / 1000000.0;
    double emotion_factor = (emotion_index + 1.0) / 2.0;
    double coherence_factor = b->egv_coherence;
    double weight = (double)b->stake * (credibility_factor + emotion_factor + coherence_factor);
    return rational_normalize((rational_t){(int64_t)weight, 1});
}

int governance_cast_vote(governance_state_t *g, uint32_t proposal_idx,
                          uint32_t voter_id, trit_t vote, double emotion_index) {
    if (proposal_idx >= g->num_proposals) return -1;
    if (voter_id >= g->num_holders) return -1;
    gov_proposal_t *p = &g->proposals[proposal_idx];
    if (p->closed) return -2;
    if (g->current_cycle > p->close_cycle) return -3;

    for (uint32_t i = 0; i < p->num_votes; i++) {
        if (p->votes[i].voter_id == voter_id) return -4;
    }

    if (p->num_votes >= GOV_MAX_VOTERS) return -5;
    uint32_t vidx = p->num_votes++;
    p->votes[vidx].voter_id = voter_id;
    p->votes[vidx].branch = p->branch;
    p->votes[vidx].vote = vote;
    p->votes[vidx].weight = governance_compute_vote_weight(g, voter_id, emotion_index);
    p->votes[vidx].emotion_amplitude = emotion_index;
    p->votes[vidx].timestamp = g->current_cycle;

    if (vote == TRIT_TRUE) {
        p->for_weight = rmag_add_quotas(p->for_weight, p->votes[vidx].weight);
    } else if (vote == TRIT_FALSE) {
        p->against_weight = rmag_add_quotas(p->against_weight, p->votes[vidx].weight);
    }

    g->balances[voter_id].emotion_index = emotion_index;

    if (g->matrix) {
        double complex val = rational_mag(p->votes[vidx].weight) + I * emotion_index;
        axiom_matrix_set(g->matrix, g->current_cycle, p->votes[vidx].weight, vote,
                         m5_phase_from_double(emotion_index, 0.0), (collapse_t){{p->id, voter_id}},
                         m5_cq16_from_dc(val));
    }

    return 0;
}

int governance_close_proposal(governance_state_t *g, uint32_t proposal_idx) {
    if (proposal_idx >= g->num_proposals) return -1;
    gov_proposal_t *p = &g->proposals[proposal_idx];
    if (p->closed) return -2;
    if (g->current_cycle < p->close_cycle) return -3;

    double for_mag = rational_mag(p->for_weight);
    double against_mag = rational_mag(p->against_weight);
    p->passed = (for_mag > against_mag);
    p->closed = true;

    if (p->passed) {
        choice_handoff();
    }

    return 0;
}

uint64_t governance_token_emission(ordinal_t cycle) {
    double phase = (double)cycle * GOV_PHI;
    double emission = (double)GOV_MAX_TOKEN_SUPPLY * 0.001 * exp(-phase / 1000.0);
    return (uint64_t)emission;
}

double governance_vortex_reduce(uint64_t n) {
    while (n >= 10) {
        uint64_t sum = 0;
        uint64_t tmp = n;
        while (tmp > 0) { sum += tmp % 10; tmp /= 10; }
        n = sum;
    }
    if (n == 0) return 9.0;
    return (double)n;
}
