/* test_choice.c — Deterministic Candidate Selection (K5) Tests
 *
 * Tests for candidate registration, deterministic selection, tie-breaking,
 * S0 deferral, and decision audit trail.
 *
 * Author: 36N9 Genetics, LLC
 * License: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "choice.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    tests_run++; \
    printf("  [TEST] %s ... ", #name); \
    name(); \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s\n", msg); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define PASS() return

/* ===== Registry Init Tests ===== */

TEST(registry_init_test) {
    choice_registry_t reg;
    choice_registry_init(&reg);
    ASSERT(reg.candidate_count == 0, "no candidates");
    ASSERT(reg.decision_count == 0, "no decisions");
    ASSERT(reg.next_candidate_id == 1, "next candidate id is 1");
    ASSERT(reg.next_decision_id == 1, "next decision id is 1");
    ASSERT(reg.rr_counter == 0, "rr counter is 0");
    ASSERT(reg.current_time == 0, "time is 0");
    PASS();
}

/* ===== Candidate Tests ===== */

TEST(register_candidate_test) {
    choice_registry_t reg;
    choice_registry_init(&reg);
    int32_t idx = choice_register_candidate(&reg, "option-a", 1, 100,
                                            "storage.read", true);
    ASSERT(idx >= 0, "candidate registered");
    ASSERT(idx == 0, "first candidate is index 0");

    choice_candidate_t *c = choice_get_candidate(&reg, 0);
    ASSERT(c != NULL, "candidate retrieved");
    ASSERT(strcmp(c->name, "option-a") == 0, "name matches");
    ASSERT(c->priority == 1, "priority is 1");
    ASSERT(c->weight == 100, "weight is 100");
    ASSERT(strcmp(c->capability, "storage.read") == 0, "capability matches");
    ASSERT(c->active, "is active");
    ASSERT(c->requires_admission, "requires admission");
    ASSERT(c->selection_count == 0, "selection count is 0");
    PASS();
}

TEST(set_candidate_active_test) {
    choice_registry_t reg;
    choice_registry_init(&reg);
    choice_register_candidate(&reg, "c", 1, 100, NULL, false);
    ASSERT(choice_set_candidate_active(&reg, 0, false), "deactivated");
    ASSERT(!choice_get_candidate(&reg, 0)->active, "is inactive");
    ASSERT(choice_set_candidate_active(&reg, 0, true), "activated");
    ASSERT(choice_get_candidate(&reg, 0)->active, "is active");
    PASS();
}

/* ===== Decision Tests ===== */

TEST(decide_single_candidate_test) {
    choice_registry_t reg;
    choice_registry_init(&reg);
    choice_register_candidate(&reg, "only", 1, 100, NULL, false);

    int32_t didx = choice_decide(&reg, "test-decision", CHOICE_TIE_LOWEST_ID);
    ASSERT(didx >= 0, "decision recorded");

    choice_decision_t *d = choice_get_decision(&reg, (uint32_t)didx);
    ASSERT(d->outcome == CHOICE_OUTCOME_SELECTED, "outcome is selected");
    ASSERT(d->selected_idx == 0, "selected candidate 0");
    ASSERT(d->candidate_count == 1, "1 candidate considered");
    ASSERT(choice_get_selection_count(&reg, 0) == 1, "selection count incremented");
    PASS();
}

TEST(decide_no_candidates_test) {
    choice_registry_t reg;
    choice_registry_init(&reg);

    int32_t didx = choice_decide(&reg, "empty", CHOICE_TIE_LOWEST_ID);
    choice_decision_t *d = choice_get_decision(&reg, (uint32_t)didx);
    ASSERT(d->outcome == CHOICE_OUTCOME_REJECTED, "outcome is rejected");
    ASSERT(d->selected_idx == -1, "no candidate selected");
    PASS();
}

TEST(decide_priority_test) {
    choice_registry_t reg;
    choice_registry_init(&reg);
    choice_register_candidate(&reg, "low-pri", 10, 100, NULL, false);
    choice_register_candidate(&reg, "high-pri", 1, 50, NULL, false);

    int32_t didx = choice_decide(&reg, "priority-test", CHOICE_TIE_LOWEST_ID);
    choice_decision_t *d = choice_get_decision(&reg, (uint32_t)didx);
    ASSERT(d->outcome == CHOICE_OUTCOME_SELECTED, "outcome is selected");
    ASSERT(d->selected_idx == 1, "selected high-priority (index 1)");
    PASS();
}

TEST(decide_weight_test) {
    choice_registry_t reg;
    choice_registry_init(&reg);
    choice_register_candidate(&reg, "low-weight", 1, 50, NULL, false);
    choice_register_candidate(&reg, "high-weight", 1, 100, NULL, false);

    int32_t didx = choice_decide(&reg, "weight-test", CHOICE_TIE_LOWEST_ID);
    choice_decision_t *d = choice_get_decision(&reg, (uint32_t)didx);
    ASSERT(d->selected_idx == 1, "selected higher-weight (index 1)");
    PASS();
}

TEST(decide_tie_lowest_id_test) {
    choice_registry_t reg;
    choice_registry_init(&reg);
    choice_register_candidate(&reg, "a", 1, 100, NULL, false);
    choice_register_candidate(&reg, "b", 1, 100, NULL, false);

    int32_t didx = choice_decide(&reg, "tie-test", CHOICE_TIE_LOWEST_ID);
    choice_decision_t *d = choice_get_decision(&reg, (uint32_t)didx);
    ASSERT(d->selected_idx == 0, "selected lowest ID (index 0)");
    PASS();
}

TEST(decide_tie_highest_id_test) {
    choice_registry_t reg;
    choice_registry_init(&reg);
    choice_register_candidate(&reg, "a", 1, 100, NULL, false);
    choice_register_candidate(&reg, "b", 1, 100, NULL, false);

    int32_t didx = choice_decide(&reg, "tie-test", CHOICE_TIE_HIGHEST_ID);
    choice_decision_t *d = choice_get_decision(&reg, (uint32_t)didx);
    ASSERT(d->selected_idx == 1, "selected highest ID (index 1)");
    PASS();
}

TEST(decide_tie_round_robin_test) {
    choice_registry_t reg;
    choice_registry_init(&reg);
    choice_register_candidate(&reg, "a", 1, 100, NULL, false);
    choice_register_candidate(&reg, "b", 1, 100, NULL, false);

    int32_t d1 = choice_decide(&reg, "rr1", CHOICE_TIE_ROUND_ROBIN);
    int32_t d2 = choice_decide(&reg, "rr2", CHOICE_TIE_ROUND_ROBIN);
    choice_decision_t *dec1 = choice_get_decision(&reg, (uint32_t)d1);
    choice_decision_t *dec2 = choice_get_decision(&reg, (uint32_t)d2);
    ASSERT(dec1->selected_idx != dec2->selected_idx, "round robin selected different");
    PASS();
}

TEST(decide_tie_defer_s0_test) {
    choice_registry_t reg;
    choice_registry_init(&reg);
    choice_register_candidate(&reg, "a", 1, 100, NULL, false);
    choice_register_candidate(&reg, "b", 1, 100, NULL, false);

    int32_t didx = choice_decide(&reg, "defer-test", CHOICE_TIE_DEFER_S0);
    choice_decision_t *d = choice_get_decision(&reg, (uint32_t)didx);
    ASSERT(d->outcome == CHOICE_OUTCOME_DEFERRED, "outcome is deferred");
    ASSERT(d->selected_idx == -2, "selected idx is -2 (deferred)");
    PASS();
}

TEST(decide_tie_first_registered_test) {
    choice_registry_t reg;
    choice_registry_init(&reg);
    choice_register_candidate(&reg, "first", 1, 100, NULL, false);
    choice_register_candidate(&reg, "second", 1, 100, NULL, false);

    int32_t didx = choice_decide(&reg, "first-reg", CHOICE_TIE_FIRST_REGISTERED);
    choice_decision_t *d = choice_get_decision(&reg, (uint32_t)didx);
    ASSERT(d->selected_idx == 0, "selected first registered (index 0)");
    PASS();
}

TEST(decide_inactive_candidate_excluded_test) {
    choice_registry_t reg;
    choice_registry_init(&reg);
    choice_register_candidate(&reg, "active", 1, 100, NULL, false);
    choice_register_candidate(&reg, "inactive", 1, 200, NULL, false);
    choice_set_candidate_active(&reg, 1, false);

    int32_t didx = choice_decide(&reg, "inactive-test", CHOICE_TIE_LOWEST_ID);
    choice_decision_t *d = choice_get_decision(&reg, (uint32_t)didx);
    ASSERT(d->selected_idx == 0, "selected only active candidate");
    ASSERT(d->candidate_count == 1, "only 1 candidate considered");
    PASS();
}

TEST(decide_all_inactive_test) {
    choice_registry_t reg;
    choice_registry_init(&reg);
    choice_register_candidate(&reg, "a", 1, 100, NULL, false);
    choice_set_candidate_active(&reg, 0, false);

    int32_t didx = choice_decide(&reg, "all-inactive", CHOICE_TIE_LOWEST_ID);
    choice_decision_t *d = choice_get_decision(&reg, (uint32_t)didx);
    ASSERT(d->outcome == CHOICE_OUTCOME_REJECTED, "outcome is rejected");
    PASS();
}

/* ===== Time Tests ===== */

TEST(advance_time_test) {
    choice_registry_t reg;
    choice_registry_init(&reg);
    ASSERT(reg.current_time == 0, "starts at 0");
    choice_advance_time(&reg, 100);
    ASSERT(reg.current_time == 100, "advanced to 100");

    choice_register_candidate(&reg, "c", 1, 100, NULL, false);
    int32_t didx = choice_decide(&reg, "timed", CHOICE_TIE_LOWEST_ID);
    choice_decision_t *d = choice_get_decision(&reg, (uint32_t)didx);
    ASSERT(d->timestamp == 100, "timestamp is 100");
    PASS();
}

/* ===== Query Tests ===== */

TEST(count_active_candidates_test) {
    choice_registry_t reg;
    choice_registry_init(&reg);
    choice_register_candidate(&reg, "a", 1, 100, NULL, false);
    choice_register_candidate(&reg, "b", 1, 100, NULL, false);
    choice_register_candidate(&reg, "c", 1, 100, NULL, false);
    choice_set_candidate_active(&reg, 1, false);

    ASSERT(choice_count_active_candidates(&reg) == 2, "2 active");
    PASS();
}

TEST(count_by_outcome_test) {
    choice_registry_t reg;
    choice_registry_init(&reg);
    choice_register_candidate(&reg, "a", 1, 100, NULL, false);
    choice_register_candidate(&reg, "b", 1, 100, NULL, false);

    choice_decide(&reg, "d1", CHOICE_TIE_LOWEST_ID);   /* selected */
    choice_decide(&reg, "d2", CHOICE_TIE_DEFER_S0);     /* deferred */

    ASSERT(choice_count_by_outcome(&reg, CHOICE_OUTCOME_SELECTED) == 1, "1 selected");
    ASSERT(choice_count_by_outcome(&reg, CHOICE_OUTCOME_DEFERRED) == 1, "1 deferred");
    PASS();
}

TEST(selection_count_test) {
    choice_registry_t reg;
    choice_registry_init(&reg);
    choice_register_candidate(&reg, "a", 1, 100, NULL, false);
    choice_register_candidate(&reg, "b", 2, 100, NULL, false);

    /* "a" has higher priority (lower number), so always selected */
    choice_decide(&reg, "d1", CHOICE_TIE_LOWEST_ID);
    choice_decide(&reg, "d2", CHOICE_TIE_LOWEST_ID);

    ASSERT(choice_get_selection_count(&reg, 0) == 2, "a selected 2 times");
    ASSERT(choice_get_selection_count(&reg, 1) == 0, "b selected 0 times");
    PASS();
}

/* ===== Name Function Tests ===== */

TEST(name_functions_test) {
    ASSERT(strcmp(choice_outcome_name(CHOICE_OUTCOME_SELECTED), "selected") == 0, "selected name");
    ASSERT(strcmp(choice_outcome_name(CHOICE_OUTCOME_DEFERRED), "deferred") == 0, "deferred name");
    ASSERT(strcmp(choice_outcome_name(CHOICE_OUTCOME_REJECTED), "rejected") == 0, "rejected name");
    ASSERT(strcmp(choice_tie_policy_name(CHOICE_TIE_LOWEST_ID), "lowest_id") == 0, "lowest_id name");
    ASSERT(strcmp(choice_tie_policy_name(CHOICE_TIE_ROUND_ROBIN), "round_robin") == 0, "round_robin name");
    ASSERT(strcmp(choice_tie_policy_name(CHOICE_TIE_DEFER_S0), "defer_s0") == 0, "defer_s0 name");
    ASSERT(strcmp(choice_tie_policy_name(CHOICE_TIE_FIRST_REGISTERED), "first_registered") == 0, "first_registered name");
    PASS();
}

/* ===== Main ===== */

int main(void) {
    printf("\n=== ZXV CHOICE (K5) Tests ===\n\n");

    RUN(registry_init_test);
    RUN(register_candidate_test);
    RUN(set_candidate_active_test);
    RUN(decide_single_candidate_test);
    RUN(decide_no_candidates_test);
    RUN(decide_priority_test);
    RUN(decide_weight_test);
    RUN(decide_tie_lowest_id_test);
    RUN(decide_tie_highest_id_test);
    RUN(decide_tie_round_robin_test);
    RUN(decide_tie_defer_s0_test);
    RUN(decide_tie_first_registered_test);
    RUN(decide_inactive_candidate_excluded_test);
    RUN(decide_all_inactive_test);
    RUN(advance_time_test);
    RUN(count_active_candidates_test);
    RUN(count_by_outcome_test);
    RUN(selection_count_test);
    RUN(name_functions_test);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);
    if (tests_failed == 0) {
        printf("ALL TESTS PASSED\n");
    }
    return tests_failed > 0 ? 1 : 0;
}
