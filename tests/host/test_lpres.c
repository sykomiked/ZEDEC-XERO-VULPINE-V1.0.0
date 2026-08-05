/* test_lpres.c — Paraconsistent Presence States (K3) Tests
 *
 * Tests for four-valued paraconsistent logic and attestation management.
 *
 * Author: 36N9 Genetics, LLC
 * License: SEL-3.3 (kernel component)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lpres.h"

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

/* ===== Paraconsistent Logic Tests ===== */

TEST(negate_test) {
    ASSERT(lpres_negate(LPRES_STATE_TRUE) == LPRES_STATE_FALSE, "negate true = false");
    ASSERT(lpres_negate(LPRES_STATE_FALSE) == LPRES_STATE_TRUE, "negate false = true");
    ASSERT(lpres_negate(LPRES_STATE_BOTH) == LPRES_STATE_BOTH, "negate both = both");
    ASSERT(lpres_negate(LPRES_STATE_NEITHER) == LPRES_STATE_NEITHER, "negate neither = neither");
    PASS();
}

TEST(conjoin_test) {
    /* T ∧ T = T */
    ASSERT(lpres_conjoin(LPRES_STATE_TRUE, LPRES_STATE_TRUE) == LPRES_STATE_TRUE, "T∧T=T");
    /* T ∧ F = F */
    ASSERT(lpres_conjoin(LPRES_STATE_TRUE, LPRES_STATE_FALSE) == LPRES_STATE_FALSE, "T∧F=F");
    /* T ∧ B = B */
    ASSERT(lpres_conjoin(LPRES_STATE_TRUE, LPRES_STATE_BOTH) == LPRES_STATE_BOTH, "T∧B=B");
    /* T ∧ ⊥ = ⊥ */
    ASSERT(lpres_conjoin(LPRES_STATE_TRUE, LPRES_STATE_NEITHER) == LPRES_STATE_NEITHER, "T∧⊥=⊥");
    /* F ∧ anything = F */
    ASSERT(lpres_conjoin(LPRES_STATE_FALSE, LPRES_STATE_TRUE) == LPRES_STATE_FALSE, "F∧T=F");
    ASSERT(lpres_conjoin(LPRES_STATE_FALSE, LPRES_STATE_FALSE) == LPRES_STATE_FALSE, "F∧F=F");
    ASSERT(lpres_conjoin(LPRES_STATE_FALSE, LPRES_STATE_BOTH) == LPRES_STATE_FALSE, "F∧B=F");
    ASSERT(lpres_conjoin(LPRES_STATE_FALSE, LPRES_STATE_NEITHER) == LPRES_STATE_FALSE, "F∧⊥=F");
    /* B ∧ B = B */
    ASSERT(lpres_conjoin(LPRES_STATE_BOTH, LPRES_STATE_BOTH) == LPRES_STATE_BOTH, "B∧B=B");
    /* B ∧ ⊥ = ⊥ */
    ASSERT(lpres_conjoin(LPRES_STATE_BOTH, LPRES_STATE_NEITHER) == LPRES_STATE_NEITHER, "B∧⊥=⊥");
    /* ⊥ ∧ ⊥ = ⊥ */
    ASSERT(lpres_conjoin(LPRES_STATE_NEITHER, LPRES_STATE_NEITHER) == LPRES_STATE_NEITHER, "⊥∧⊥=⊥");
    PASS();
}

TEST(disjoin_test) {
    /* F ∨ F = F */
    ASSERT(lpres_disjoin(LPRES_STATE_FALSE, LPRES_STATE_FALSE) == LPRES_STATE_FALSE, "F∨F=F");
    /* F ∨ T = T */
    ASSERT(lpres_disjoin(LPRES_STATE_FALSE, LPRES_STATE_TRUE) == LPRES_STATE_TRUE, "F∨T=T");
    /* F ∨ B = B */
    ASSERT(lpres_disjoin(LPRES_STATE_FALSE, LPRES_STATE_BOTH) == LPRES_STATE_BOTH, "F∨B=B");
    /* F ∨ ⊥ = ⊥ */
    ASSERT(lpres_disjoin(LPRES_STATE_FALSE, LPRES_STATE_NEITHER) == LPRES_STATE_NEITHER, "F∨⊥=⊥");
    /* T ∨ anything = T */
    ASSERT(lpres_disjoin(LPRES_STATE_TRUE, LPRES_STATE_FALSE) == LPRES_STATE_TRUE, "T∨F=T");
    ASSERT(lpres_disjoin(LPRES_STATE_TRUE, LPRES_STATE_BOTH) == LPRES_STATE_TRUE, "T∨B=T");
    ASSERT(lpres_disjoin(LPRES_STATE_TRUE, LPRES_STATE_NEITHER) == LPRES_STATE_TRUE, "T∨⊥=T");
    /* B ∨ B = B */
    ASSERT(lpres_disjoin(LPRES_STATE_BOTH, LPRES_STATE_BOTH) == LPRES_STATE_BOTH, "B∨B=B");
    /* B ∨ ⊥ = B */
    ASSERT(lpres_disjoin(LPRES_STATE_BOTH, LPRES_STATE_NEITHER) == LPRES_STATE_BOTH, "B∨⊥=B");
    /* ⊥ ∨ ⊥ = ⊥ */
    ASSERT(lpres_disjoin(LPRES_STATE_NEITHER, LPRES_STATE_NEITHER) == LPRES_STATE_NEITHER, "⊥∨⊥=⊥");
    PASS();
}

TEST(state_classifiers_test) {
    ASSERT(lpres_is_certain(LPRES_STATE_TRUE), "true is certain");
    ASSERT(lpres_is_certain(LPRES_STATE_FALSE), "false is certain");
    ASSERT(!lpres_is_certain(LPRES_STATE_BOTH), "both is not certain");
    ASSERT(!lpres_is_certain(LPRES_STATE_NEITHER), "neither is not certain");

    ASSERT(lpres_is_contradictory(LPRES_STATE_BOTH), "both is contradictory");
    ASSERT(!lpres_is_contradictory(LPRES_STATE_TRUE), "true is not contradictory");
    ASSERT(!lpres_is_contradictory(LPRES_STATE_FALSE), "false is not contradictory");
    ASSERT(!lpres_is_contradictory(LPRES_STATE_NEITHER), "neither is not contradictory");

    ASSERT(lpres_is_unknown(LPRES_STATE_NEITHER), "neither is unknown");
    ASSERT(!lpres_is_unknown(LPRES_STATE_TRUE), "true is not unknown");
    ASSERT(!lpres_is_unknown(LPRES_STATE_FALSE), "false is not unknown");
    ASSERT(!lpres_is_unknown(LPRES_STATE_BOTH), "both is not unknown");
    PASS();
}

TEST(actuator_authority_test) {
    ASSERT(lpres_implies_actuator_authority(LPRES_STATE_TRUE), "true authorizes actuator");
    ASSERT(!lpres_implies_actuator_authority(LPRES_STATE_FALSE), "false blocks actuator");
    ASSERT(!lpres_implies_actuator_authority(LPRES_STATE_BOTH), "both blocks actuator");
    ASSERT(!lpres_implies_actuator_authority(LPRES_STATE_NEITHER), "neither blocks actuator");
    PASS();
}

/* ===== Registry Tests ===== */

TEST(registry_init_test) {
    lpres_registry_t reg;
    lpres_registry_init(&reg);
    ASSERT(reg.count == 0, "no attestations");
    ASSERT(reg.current_time == 0, "time is 0");
    PASS();
}

TEST(attest_test) {
    lpres_registry_t reg;
    lpres_registry_init(&reg);
    uint8_t digest[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    int32_t idx = lpres_attest(&reg, "source.cob", LPRES_SUBJECT_SOURCE,
                               LPRES_STATE_TRUE, "macgyver", digest, 4);
    ASSERT(idx >= 0, "attestation registered");
    ASSERT(idx == 0, "first is index 0");
    ASSERT(reg.count == 1, "count incremented");

    lpres_attestation_t *a = lpres_get(&reg, 0);
    ASSERT(a != NULL, "attestation retrieved");
    ASSERT(strcmp(a->name, "source.cob") == 0, "name matches");
    ASSERT(a->subject_type == LPRES_SUBJECT_SOURCE, "type matches");
    ASSERT(a->state == LPRES_STATE_TRUE, "state is true");
    ASSERT(a->positive_evidence == 1, "positive evidence is 1");
    ASSERT(a->negative_evidence == 0, "negative evidence is 0");
    ASSERT(!a->revoked, "not revoked");
    ASSERT(a->digest_len == 4, "digest length is 4");
    ASSERT(a->digest[0] == 0xDE, "digest[0] matches");
    PASS();
}

TEST(attest_both_test) {
    lpres_registry_t reg;
    lpres_registry_init(&reg);
    int32_t idx = lpres_attest(&reg, "conflicting-dep", LPRES_SUBJECT_DEPENDENCY,
                               LPRES_STATE_BOTH, "k3", NULL, 0);
    lpres_attestation_t *a = lpres_get(&reg, (uint32_t)idx);
    ASSERT(a->state == LPRES_STATE_BOTH, "state is both");
    ASSERT(a->positive_evidence == 1, "positive evidence is 1");
    ASSERT(a->negative_evidence == 1, "negative evidence is 1");
    PASS();
}

TEST(find_test) {
    lpres_registry_t reg;
    lpres_registry_init(&reg);
    lpres_attest(&reg, "solver.f90", LPRES_SUBJECT_SOURCE, LPRES_STATE_TRUE, "k3", NULL, 0);
    lpres_attest(&reg, "model-pack-v2", LPRES_SUBJECT_MODEL_PACK, LPRES_STATE_FALSE, "k3", NULL, 0);

    lpres_attestation_t *a = lpres_find(&reg, "solver.f90");
    ASSERT(a != NULL, "found solver.f90");
    ASSERT(a->state == LPRES_STATE_TRUE, "solver is true");

    a = lpres_find(&reg, "model-pack-v2");
    ASSERT(a != NULL, "found model-pack-v2");
    ASSERT(a->state == LPRES_STATE_FALSE, "model-pack is false");

    ASSERT(lpres_find(&reg, "nonexistent") == NULL, "nonexistent not found");
    PASS();
}

TEST(add_evidence_test) {
    lpres_registry_t reg;
    lpres_registry_init(&reg);
    int32_t idx = lpres_attest(&reg, "sensor-1", LPRES_SUBJECT_SENSOR,
                               LPRES_STATE_TRUE, "k3", NULL, 0);

    /* Add negative evidence → should become BOTH */
    ASSERT(lpres_add_evidence(&reg, (uint32_t)idx, false), "add negative evidence");
    lpres_attestation_t *a = lpres_get(&reg, (uint32_t)idx);
    ASSERT(a->state == LPRES_STATE_BOTH, "state is both after conflicting evidence");
    ASSERT(a->positive_evidence == 1, "positive count is 1");
    ASSERT(a->negative_evidence == 1, "negative count is 1");

    /* Add more positive evidence → still BOTH */
    ASSERT(lpres_add_evidence(&reg, (uint32_t)idx, true), "add positive evidence");
    a = lpres_get(&reg, (uint32_t)idx);
    ASSERT(a->state == LPRES_STATE_BOTH, "still both with more positive");
    ASSERT(a->positive_evidence == 2, "positive count is 2");
    PASS();
}

TEST(add_evidence_neither_to_true_test) {
    lpres_registry_t reg;
    lpres_registry_init(&reg);
    int32_t idx = lpres_attest(&reg, "unknown-dep", LPRES_SUBJECT_DEPENDENCY,
                               LPRES_STATE_NEITHER, "k3", NULL, 0);

    /* Add positive evidence → should become TRUE */
    ASSERT(lpres_add_evidence(&reg, (uint32_t)idx, true), "add positive evidence");
    lpres_attestation_t *a = lpres_get(&reg, (uint32_t)idx);
    ASSERT(a->state == LPRES_STATE_TRUE, "state is true after positive evidence");
    PASS();
}

TEST(revoke_test) {
    lpres_registry_t reg;
    lpres_registry_init(&reg);
    int32_t idx = lpres_attest(&reg, "old-key", LPRES_SUBJECT_KEY,
                               LPRES_STATE_TRUE, "k3", NULL, 0);
    ASSERT(lpres_revoke(&reg, (uint32_t)idx), "revocation succeeds");
    lpres_attestation_t *a = lpres_get(&reg, (uint32_t)idx);
    ASSERT(a->revoked, "is revoked");

    /* Revoked attestations don't add evidence */
    ASSERT(!lpres_add_evidence(&reg, (uint32_t)idx, false), "can't add evidence to revoked");
    ASSERT(!lpres_update_state(&reg, (uint32_t)idx, LPRES_STATE_FALSE), "can't update revoked");
    PASS();
}

TEST(update_state_test) {
    lpres_registry_t reg;
    lpres_registry_init(&reg);
    int32_t idx = lpres_attest(&reg, "param-c", LPRES_SUBJECT_PARAMETER,
                               LPRES_STATE_NEITHER, "k3", NULL, 0);
    ASSERT(lpres_update_state(&reg, (uint32_t)idx, LPRES_STATE_TRUE), "state updated");
    lpres_attestation_t *a = lpres_get(&reg, (uint32_t)idx);
    ASSERT(a->state == LPRES_STATE_TRUE, "state is now true");
    PASS();
}

/* ===== Batch Query Tests ===== */

TEST(count_by_state_test) {
    lpres_registry_t reg;
    lpres_registry_init(&reg);
    lpres_attest(&reg, "a", LPRES_SUBJECT_SOURCE, LPRES_STATE_TRUE, "k3", NULL, 0);
    lpres_attest(&reg, "b", LPRES_SUBJECT_SOURCE, LPRES_STATE_TRUE, "k3", NULL, 0);
    lpres_attest(&reg, "c", LPRES_SUBJECT_SOURCE, LPRES_STATE_FALSE, "k3", NULL, 0);
    lpres_attest(&reg, "d", LPRES_SUBJECT_SOURCE, LPRES_STATE_BOTH, "k3", NULL, 0);
    lpres_attest(&reg, "e", LPRES_SUBJECT_SOURCE, LPRES_STATE_NEITHER, "k3", NULL, 0);

    ASSERT(lpres_count_by_state(&reg, LPRES_STATE_TRUE) == 2, "2 true");
    ASSERT(lpres_count_by_state(&reg, LPRES_STATE_FALSE) == 1, "1 false");
    ASSERT(lpres_count_by_state(&reg, LPRES_STATE_BOTH) == 1, "1 both");
    ASSERT(lpres_count_by_state(&reg, LPRES_STATE_NEITHER) == 1, "1 neither");
    PASS();
}

TEST(count_by_type_test) {
    lpres_registry_t reg;
    lpres_registry_init(&reg);
    lpres_attest(&reg, "a", LPRES_SUBJECT_SENSOR, LPRES_STATE_TRUE, "k3", NULL, 0);
    lpres_attest(&reg, "b", LPRES_SUBJECT_SENSOR, LPRES_STATE_TRUE, "k3", NULL, 0);
    lpres_attest(&reg, "c", LPRES_SUBJECT_ACTUATOR, LPRES_STATE_FALSE, "k3", NULL, 0);

    ASSERT(lpres_count_by_type(&reg, LPRES_SUBJECT_SENSOR) == 2, "2 sensors");
    ASSERT(lpres_count_by_type(&reg, LPRES_SUBJECT_ACTUATOR) == 1, "1 actuator");
    ASSERT(lpres_count_by_type(&reg, LPRES_SUBJECT_MODEL_PACK) == 0, "0 model packs");
    PASS();
}

TEST(count_contradictions_test) {
    lpres_registry_t reg;
    lpres_registry_init(&reg);
    lpres_attest(&reg, "a", LPRES_SUBJECT_SOURCE, LPRES_STATE_TRUE, "k3", NULL, 0);
    lpres_attest(&reg, "b", LPRES_SUBJECT_SOURCE, LPRES_STATE_BOTH, "k3", NULL, 0);
    lpres_attest(&reg, "c", LPRES_SUBJECT_SOURCE, LPRES_STATE_BOTH, "k3", NULL, 0);
    lpres_attest(&reg, "d", LPRES_SUBJECT_SOURCE, LPRES_STATE_FALSE, "k3", NULL, 0);

    ASSERT(lpres_count_contradictions(&reg) == 2, "2 contradictions");
    PASS();
}

TEST(count_unattested_test) {
    lpres_registry_t reg;
    lpres_registry_init(&reg);
    lpres_attest(&reg, "a", LPRES_SUBJECT_SOURCE, LPRES_STATE_NEITHER, "k3", NULL, 0);
    lpres_attest(&reg, "b", LPRES_SUBJECT_SOURCE, LPRES_STATE_TRUE, "k3", NULL, 0);
    lpres_attest(&reg, "c", LPRES_SUBJECT_SOURCE, LPRES_STATE_NEITHER, "k3", NULL, 0);

    ASSERT(lpres_count_unattested(&reg) == 2, "2 unattested");
    PASS();
}

TEST(count_excludes_revoked_test) {
    lpres_registry_t reg;
    lpres_registry_init(&reg);
    int32_t idx = lpres_attest(&reg, "a", LPRES_SUBJECT_SOURCE, LPRES_STATE_TRUE, "k3", NULL, 0);
    lpres_attest(&reg, "b", LPRES_SUBJECT_SOURCE, LPRES_STATE_TRUE, "k3", NULL, 0);
    lpres_revoke(&reg, (uint32_t)idx);

    ASSERT(lpres_count_by_state(&reg, LPRES_STATE_TRUE) == 1, "revoked excluded from count");
    PASS();
}

/* ===== Safety Gate Tests ===== */

TEST(safety_gate_clear_test) {
    lpres_registry_t reg;
    lpres_registry_init(&reg);

    /* Empty registry — gate is clear (no blockers) */
    ASSERT(lpres_safety_gate_clear(&reg), "empty registry: gate clear");

    /* All certain — gate clear */
    lpres_attest(&reg, "a", LPRES_SUBJECT_SENSOR, LPRES_STATE_TRUE, "k3", NULL, 0);
    lpres_attest(&reg, "b", LPRES_SUBJECT_ACTUATOR, LPRES_STATE_FALSE, "k3", NULL, 0);
    ASSERT(lpres_safety_gate_clear(&reg), "all certain: gate clear");

    /* Add contradiction — gate blocked */
    lpres_attest(&reg, "c", LPRES_SUBJECT_PARAMETER, LPRES_STATE_BOTH, "k3", NULL, 0);
    ASSERT(!lpres_safety_gate_clear(&reg), "contradiction blocks gate");

    /* Add unknown — gate blocked */
    lpres_attest(&reg, "d", LPRES_SUBJECT_CALIBRATION, LPRES_STATE_NEITHER, "k3", NULL, 0);
    ASSERT(!lpres_safety_gate_clear(&reg), "unknown blocks gate");
    PASS();
}

TEST(safety_gate_revoked_ignored_test) {
    lpres_registry_t reg;
    lpres_registry_init(&reg);
    int32_t idx = lpres_attest(&reg, "bad", LPRES_SUBJECT_SENSOR, LPRES_STATE_BOTH, "k3", NULL, 0);
    lpres_attest(&reg, "good", LPRES_SUBJECT_SENSOR, LPRES_STATE_TRUE, "k3", NULL, 0);

    /* Contradiction blocks gate */
    ASSERT(!lpres_safety_gate_clear(&reg), "contradiction blocks");

    /* Revoke the contradictory one — gate clears */
    lpres_revoke(&reg, (uint32_t)idx);
    ASSERT(lpres_safety_gate_clear(&reg), "revoked contradiction: gate clear");
    PASS();
}

/* ===== Time Test ===== */

TEST(advance_time_test) {
    lpres_registry_t reg;
    lpres_registry_init(&reg);
    ASSERT(reg.current_time == 0, "starts at 0");
    lpres_advance_time(&reg, 100);
    ASSERT(reg.current_time == 100, "advanced to 100");

    lpres_attest(&reg, "timed", LPRES_SUBJECT_SOURCE, LPRES_STATE_TRUE, "k3", NULL, 0);
    lpres_attestation_t *a = lpres_get(&reg, 0);
    ASSERT(a->timestamp == 100, "timestamp is 100");

    lpres_advance_time(&reg, 50);
    ASSERT(reg.current_time == 150, "advanced to 150");
    PASS();
}

/* ===== Name Function Tests ===== */

TEST(name_functions_test) {
    ASSERT(strcmp(lpres_state_name(LPRES_STATE_TRUE), "true") == 0, "true name");
    ASSERT(strcmp(lpres_state_name(LPRES_STATE_FALSE), "false") == 0, "false name");
    ASSERT(strcmp(lpres_state_name(LPRES_STATE_BOTH), "both") == 0, "both name");
    ASSERT(strcmp(lpres_state_name(LPRES_STATE_NEITHER), "neither") == 0, "neither name");
    ASSERT(strcmp(lpres_subject_name(LPRES_SUBJECT_SENSOR), "sensor") == 0, "sensor name");
    ASSERT(strcmp(lpres_subject_name(LPRES_SUBJECT_MODEL_PACK), "model_pack") == 0, "model_pack name");
    ASSERT(strcmp(lpres_subject_name(LPRES_SUBJECT_ACTUATOR), "actuator") == 0, "actuator name");
    PASS();
}

/* ===== Main ===== */

int main(void) {
    printf("\n=== ZXV LPRES (K3) Tests ===\n\n");

    RUN(negate_test);
    RUN(conjoin_test);
    RUN(disjoin_test);
    RUN(state_classifiers_test);
    RUN(actuator_authority_test);
    RUN(registry_init_test);
    RUN(attest_test);
    RUN(attest_both_test);
    RUN(find_test);
    RUN(add_evidence_test);
    RUN(add_evidence_neither_to_true_test);
    RUN(revoke_test);
    RUN(update_state_test);
    RUN(count_by_state_test);
    RUN(count_by_type_test);
    RUN(count_contradictions_test);
    RUN(count_unattested_test);
    RUN(count_excludes_revoked_test);
    RUN(safety_gate_clear_test);
    RUN(safety_gate_revoked_ignored_test);
    RUN(advance_time_test);
    RUN(name_functions_test);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);
    if (tests_failed == 0) {
        printf("ALL TESTS PASSED\n");
    }
    return tests_failed > 0 ? 1 : 0;
}
