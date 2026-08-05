/* test_governance.c — Governance & Tokenomics Tests
 * Tests DAO voting, amplitude-weighted vote calculation, proposal lifecycle,
 * token emission, vortex reduction.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "governance_core.h"
#include "axiom_matrix_core.h"

int main(void) {
    printf("=== Governance & Tokenomics Tests ===\n");

    axiom_matrix_t matrix;
    matrix.size = 512;
    double complex entries[512];
    memset(entries, 0, sizeof(entries));
    matrix.entries = entries;

    static governance_state_t gov;
    governance_init(&gov, &matrix);
    assert(gov.total_supply == 10000000000ULL);
    assert(gov.num_proposals == 0);

    uint32_t h0 = governance_register_holder(&gov, 10000, 500000);
    uint32_t h1 = governance_register_holder(&gov, 5000, 300000);
    uint32_t h2 = governance_register_holder(&gov, 20000, 800000);
    assert(h0 == 0 && h1 == 1 && h2 == 2);
    assert(gov.num_holders == 3);
    assert(gov.circulating_supply == 35000);
    printf("  [PASS] Token holder registration\n");

    uint32_t p0 = governance_create_proposal(&gov, "Upgrade kernel scheduler",
                                              GOV_BRANCH_KERNEL, 10);
    assert(p0 == 0);
    assert(gov.proposals[0].open_cycle == 0);
    assert(gov.proposals[0].close_cycle == 10);
    printf("  [PASS] Proposal creation\n");

    rational_t w0 = governance_compute_vote_weight(&gov, h0, 0.5);
    rational_t w2 = governance_compute_vote_weight(&gov, h2, 0.8);
    double mag0 = rational_mag(w0);
    double mag2 = rational_mag(w2);
    assert(mag2 > mag0);
    printf("  [PASS] Vote weight: h0=%.2f (stake=10k, emo=0.5) < h2=%.2f (stake=20k, emo=0.8)\n",
           mag0, mag2);

    assert(governance_cast_vote(&gov, p0, h0, TRIT_TRUE, 0.5) == 0);
    assert(governance_cast_vote(&gov, p0, h1, TRIT_FALSE, -0.2) == 0);
    assert(governance_cast_vote(&gov, p0, h2, TRIT_TRUE, 0.8) == 0);
    assert(gov.proposals[0].num_votes == 3);
    printf("  [PASS] Vote casting (2 for, 1 against)\n");

    assert(governance_cast_vote(&gov, p0, h0, TRIT_TRUE, 0.5) == -4);
    printf("  [PASS] Double voting rejected\n");

    gov.current_cycle = 10;
    assert(governance_close_proposal(&gov, p0) == 0);
    assert(gov.proposals[0].closed == true);
    assert(gov.proposals[0].passed == true);
    double for_mag = rational_mag(gov.proposals[0].for_weight);
    double against_mag = rational_mag(gov.proposals[0].against_weight);
    assert(for_mag > against_mag);
    printf("  [PASS] Proposal passed (for=%.2f > against=%.2f)\n", for_mag, against_mag);

    assert(governance_close_proposal(&gov, p0) == -2);
    printf("  [PASS] Double closure rejected (already closed)\n");

    uint32_t p1 = governance_create_proposal(&gov, "Another proposal",
                                              GOV_BRANCH_SECURITY, 20);
    gov.current_cycle = 5;
    assert(governance_close_proposal(&gov, p1) == -3);
    printf("  [PASS] Early closure rejected (cycle 5 < close_cycle 20)\n");

    uint64_t emission = governance_token_emission(0);
    assert(emission > 0);
    uint64_t emission_later = governance_token_emission(100);
    assert(emission_later < emission);
    printf("  [PASS] Token emission decreases over time (cycle 0: %llu, cycle 100: %llu)\n",
           (unsigned long long)emission, (unsigned long long)emission_later);

    assert(governance_vortex_reduce(12345) == 6.0);
    assert(governance_vortex_reduce(0) == 9.0);
    assert(governance_vortex_reduce(9) == 9.0);
    printf("  [PASS] Vortex reduction (12345->6, 0->9)\n");

    printf("=== All governance tests passed ===\n\n");
    return 0;
}
