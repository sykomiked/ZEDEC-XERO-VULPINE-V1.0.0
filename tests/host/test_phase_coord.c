/* test_phase_coord.c — Phase Coordinator (K6) Tests
 *
 * Tests for admission tokens, coverage gates, and health policy.
 *
 * Author: 36N9 Genetics, LLC
 * License: SEL-3.3 (kernel component)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "phase_coord.h"

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
    pc_registry_t reg;
    pc_registry_init(&reg);
    ASSERT(reg.token_count == 0, "no tokens");
    ASSERT(reg.gate_count == 0, "no gates");
    ASSERT(reg.next_token_id == 1, "next token id is 1");
    ASSERT(reg.current_logical_time == 0, "time is 0");
    PASS();
}

/* ===== Coverage Gate Tests ===== */

TEST(register_gate_test) {
    pc_registry_t reg;
    pc_registry_init(&reg);
    int32_t idx = pc_register_gate(&reg, "unit-tests", PC_COVERAGE_FULL);
    ASSERT(idx >= 0, "gate registered");
    ASSERT(idx == 0, "first gate is index 0");
    ASSERT(reg.gate_count == 1, "count incremented");
    ASSERT(strcmp(reg.gates[0].name, "unit-tests") == 0, "name matches");
    ASSERT(reg.gates[0].required == PC_COVERAGE_FULL, "required is full");
    ASSERT(!reg.gates[0].satisfied, "not satisfied initially");
    PASS();
}

TEST(set_gate_coverage_test) {
    pc_registry_t reg;
    pc_registry_init(&reg);
    int32_t idx = pc_register_gate(&reg, "fuzz", PC_COVERAGE_FULL);
    ASSERT(!reg.gates[idx].satisfied, "not satisfied at none");
    ASSERT(pc_set_gate_coverage(&reg, (uint32_t)idx, PC_COVERAGE_PARTIAL), "set partial");
    ASSERT(!reg.gates[idx].satisfied, "partial doesn't satisfy full");
    ASSERT(pc_set_gate_coverage(&reg, (uint32_t)idx, PC_COVERAGE_FULL), "set full");
    ASSERT(reg.gates[idx].satisfied, "full satisfies full");
    PASS();
}

TEST(check_all_gates_test) {
    pc_registry_t reg;
    pc_registry_init(&reg);
    ASSERT(pc_check_all_gates(&reg), "no gates = all satisfied");

    int32_t g1 = pc_register_gate(&reg, "gate1", PC_COVERAGE_FULL);
    int32_t g2 = pc_register_gate(&reg, "gate2", PC_COVERAGE_PARTIAL);
    ASSERT(!pc_check_all_gates(&reg), "unsatisfied gates fail");

    pc_set_gate_coverage(&reg, (uint32_t)g1, PC_COVERAGE_FULL);
    ASSERT(!pc_check_all_gates(&reg), "one gate still unsatisfied");

    pc_set_gate_coverage(&reg, (uint32_t)g2, PC_COVERAGE_PARTIAL);
    ASSERT(pc_check_all_gates(&reg), "all gates satisfied");
    PASS();
}

TEST(count_satisfied_gates_test) {
    pc_registry_t reg;
    pc_registry_init(&reg);
    int32_t g1 = pc_register_gate(&reg, "a", PC_COVERAGE_FULL);
    int32_t g2 = pc_register_gate(&reg, "b", PC_COVERAGE_FULL);
    int32_t g3 = pc_register_gate(&reg, "c", PC_COVERAGE_PARTIAL);
    ASSERT(pc_count_satisfied_gates(&reg) == 0, "0 satisfied");

    pc_set_gate_coverage(&reg, (uint32_t)g1, PC_COVERAGE_FULL);
    ASSERT(pc_count_satisfied_gates(&reg) == 1, "1 satisfied");

    pc_set_gate_coverage(&reg, (uint32_t)g3, PC_COVERAGE_PARTIAL);
    ASSERT(pc_count_satisfied_gates(&reg) == 2, "2 satisfied");

    pc_set_gate_coverage(&reg, (uint32_t)g2, PC_COVERAGE_FULL);
    ASSERT(pc_count_satisfied_gates(&reg) == 3, "3 satisfied");
    PASS();
}

/* ===== Health Tests ===== */

TEST(health_test) {
    pc_registry_t reg;
    pc_registry_init(&reg);
    ASSERT(pc_get_phase_health(&reg, PC_PHASE_K6_COORD) == PC_HEALTH_UNKNOWN,
           "default is unknown");

    pc_set_phase_health(&reg, PC_PHASE_K6_COORD, PC_HEALTH_HEALTHY);
    ASSERT(pc_get_phase_health(&reg, PC_PHASE_K6_COORD) == PC_HEALTH_HEALTHY,
           "set to healthy");

    pc_set_phase_health(&reg, PC_PHASE_K2_RMAG, PC_HEALTH_DEGRADED);
    ASSERT(pc_get_phase_health(&reg, PC_PHASE_K2_RMAG) == PC_HEALTH_DEGRADED,
           "K2 is degraded");
    PASS();
}

/* ===== Admission Tests ===== */

static void copy_str_helper(char *dst, const char *src, uint32_t max) {
    uint32_t i;
    for (i = 0; i + 1 < max && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

TEST(admit_simple_test) {
    pc_registry_t reg;
    pc_registry_init(&reg);
    pc_step_request_t req;
    ev_memset(&req, 0, sizeof(req));
    req.phase_id = PC_PHASE_K2_RMAG;
    req.step_id = 1;
    copy_str_helper(req.description, "allocate-memory", sizeof(req.description));

    pc_token_t *tok = pc_admit(&reg, &req);
    ASSERT(tok != NULL, "token issued");
    ASSERT(tok->decision == PC_DECISION_ADMIT, "decision is admit");
    ASSERT(tok->token_id == 1, "token id is 1");
    ASSERT(!tok->revoked, "not revoked");
    ASSERT(!tok->requires_s0_resolution, "no S0 required");
    PASS();
}

TEST(admit_s0_defer_test) {
    pc_registry_t reg;
    pc_registry_init(&reg);
    pc_step_request_t req;
    ev_memset(&req, 0, sizeof(req));
    req.phase_id = PC_PHASE_K3_LPRES;
    req.step_id = 2;
    req.is_s0_pending = true;
    copy_str_helper(req.description, "pending-attestation", sizeof(req.description));

    pc_token_t *tok = pc_admit(&reg, &req);
    ASSERT(tok != NULL, "token issued");
    ASSERT(tok->decision == PC_DECISION_DEFER, "decision is defer");
    ASSERT(tok->requires_s0_resolution, "S0 resolution required");
    PASS();
}

TEST(admit_coverage_veto_test) {
    pc_registry_t reg;
    pc_registry_init(&reg);
    pc_register_gate(&reg, "required-coverage", PC_COVERAGE_FULL);
    /* gate not satisfied */

    pc_step_request_t req;
    ev_memset(&req, 0, sizeof(req));
    req.phase_id = PC_PHASE_K5_CHOICE;
    req.step_id = 3;
    req.requires_coverage = true;
    copy_str_helper(req.description, "select-solver", sizeof(req.description));

    pc_token_t *tok = pc_admit(&reg, &req);
    ASSERT(tok != NULL, "token issued");
    ASSERT(tok->decision == PC_DECISION_VETO, "decision is veto");
    PASS();
}

TEST(admit_coverage_pass_test) {
    pc_registry_t reg;
    pc_registry_init(&reg);
    int32_t g = pc_register_gate(&reg, "required-coverage", PC_COVERAGE_FULL);
    pc_set_gate_coverage(&reg, (uint32_t)g, PC_COVERAGE_FULL);

    pc_step_request_t req;
    ev_memset(&req, 0, sizeof(req));
    req.phase_id = PC_PHASE_K5_CHOICE;
    req.step_id = 4;
    req.requires_coverage = true;
    copy_str_helper(req.description, "select-solver", sizeof(req.description));

    pc_token_t *tok = pc_admit(&reg, &req);
    ASSERT(tok != NULL, "token issued");
    ASSERT(tok->decision == PC_DECISION_ADMIT, "decision is admit");
    PASS();
}

TEST(admit_health_veto_test) {
    pc_registry_t reg;
    pc_registry_init(&reg);
    pc_set_phase_health(&reg, PC_PHASE_K4_IPHASE, PC_HEALTH_UNHEALTHY);

    pc_step_request_t req;
    ev_memset(&req, 0, sizeof(req));
    req.phase_id = PC_PHASE_K4_IPHASE;
    req.step_id = 5;
    req.requires_health = true;
    copy_str_helper(req.description, "route-event", sizeof(req.description));

    pc_token_t *tok = pc_admit(&reg, &req);
    ASSERT(tok != NULL, "token issued");
    ASSERT(tok->decision == PC_DECISION_VETO, "unhealthy is vetoed");
    PASS();
}

TEST(admit_health_defer_test) {
    pc_registry_t reg;
    pc_registry_init(&reg);
    pc_set_phase_health(&reg, PC_PHASE_K4_IPHASE, PC_HEALTH_DEGRADED);

    pc_step_request_t req;
    ev_memset(&req, 0, sizeof(req));
    req.phase_id = PC_PHASE_K4_IPHASE;
    req.step_id = 6;
    req.requires_health = true;
    copy_str_helper(req.description, "route-event", sizeof(req.description));

    pc_token_t *tok = pc_admit(&reg, &req);
    ASSERT(tok != NULL, "token issued");
    ASSERT(tok->decision == PC_DECISION_DEFER, "degraded is deferred");
    PASS();
}

TEST(admit_health_unknown_defer_test) {
    pc_registry_t reg;
    pc_registry_init(&reg);
    /* health is unknown by default */

    pc_step_request_t req;
    ev_memset(&req, 0, sizeof(req));
    req.phase_id = PC_PHASE_K1_OSEQ;
    req.step_id = 7;
    req.requires_health = true;
    copy_str_helper(req.description, "identify-event", sizeof(req.description));

    pc_token_t *tok = pc_admit(&reg, &req);
    ASSERT(tok != NULL, "token issued");
    ASSERT(tok->decision == PC_DECISION_DEFER, "unknown health is deferred");
    PASS();
}

TEST(admit_hardware_action_test) {
    pc_registry_t reg;
    pc_registry_init(&reg);

    /* Hardware action without coverage or healthy status → veto */
    pc_step_request_t req;
    ev_memset(&req, 0, sizeof(req));
    req.phase_id = PC_PHASE_K6_COORD;
    req.step_id = 8;
    req.is_hardware_action = true;
    copy_str_helper(req.description, "actuator-command", sizeof(req.description));

    pc_token_t *tok = pc_admit(&reg, &req);
    ASSERT(tok != NULL, "token issued");
    ASSERT(tok->decision == PC_DECISION_VETO, "hardware without coverage/health vetoed");

    /* With coverage and healthy → admit */
    int32_t g = pc_register_gate(&reg, "hw-safety", PC_COVERAGE_FULL);
    pc_set_gate_coverage(&reg, (uint32_t)g, PC_COVERAGE_FULL);
    pc_set_phase_health(&reg, PC_PHASE_K6_COORD, PC_HEALTH_HEALTHY);

    req.step_id = 9;
    tok = pc_admit(&reg, &req);
    ASSERT(tok != NULL, "token issued");
    ASSERT(tok->decision == PC_DECISION_ADMIT, "hardware with coverage+health admitted");
    PASS();
}

/* ===== Token Management Tests ===== */

TEST(revoke_token_test) {
    pc_registry_t reg;
    pc_registry_init(&reg);
    pc_step_request_t req;
    ev_memset(&req, 0, sizeof(req));
    req.phase_id = PC_PHASE_K2_RMAG;
    req.step_id = 10;
    copy_str_helper(req.description, "test", sizeof(req.description));

    pc_token_t *tok = pc_admit(&reg, &req);
    ASSERT(pc_token_is_valid(&reg, tok->token_id), "token is valid");

    ASSERT(pc_revoke_token(&reg, tok->token_id), "token revoked");
    ASSERT(!pc_token_is_valid(&reg, tok->token_id), "revoked token is invalid");
    PASS();
}

TEST(find_token_test) {
    pc_registry_t reg;
    pc_registry_init(&reg);
    pc_step_request_t req;
    ev_memset(&req, 0, sizeof(req));
    req.phase_id = PC_PHASE_K1_OSEQ;
    req.step_id = 11;
    copy_str_helper(req.description, "find-me", sizeof(req.description));

    pc_token_t *tok = pc_admit(&reg, &req);
    pc_token_t *found = pc_find_token(&reg, tok->token_id);
    ASSERT(found != NULL, "token found");
    ASSERT(found->token_id == tok->token_id, "id matches");

    ASSERT(pc_find_token(&reg, 99999) == NULL, "nonexistent token not found");
    PASS();
}

TEST(token_validity_test) {
    pc_registry_t reg;
    pc_registry_init(&reg);
    pc_step_request_t req;
    ev_memset(&req, 0, sizeof(req));
    req.phase_id = PC_PHASE_K2_RMAG;
    req.step_id = 12;
    req.is_s0_pending = true;
    copy_str_helper(req.description, "deferred", sizeof(req.description));

    pc_token_t *tok = pc_admit(&reg, &req);
    /* Deferred tokens are not "valid" for execution */
    ASSERT(!pc_token_is_valid(&reg, tok->token_id), "deferred token is not valid for execution");
    PASS();
}

/* ===== Time and Expiry Tests ===== */

TEST(token_expiry_test) {
    pc_registry_t reg;
    pc_registry_init(&reg);
    pc_step_request_t req;
    ev_memset(&req, 0, sizeof(req));
    req.phase_id = PC_PHASE_K2_RMAG;
    req.step_id = 13;
    copy_str_helper(req.description, "expiring", sizeof(req.description));

    pc_token_t *tok = pc_admit(&reg, &req);
    /* No expiry set */
    ASSERT(!pc_token_expired(&reg, tok->token_id), "no expiry = not expired");

    /* Set expiry manually */
    tok->expires_at = 100;
    pc_advance_time(&reg, 50);
    ASSERT(!pc_token_expired(&reg, tok->token_id), "time 50 < expiry 100");

    pc_advance_time(&reg, 60);
    ASSERT(pc_token_expired(&reg, tok->token_id), "time 110 > expiry 100");
    ASSERT(!pc_token_is_valid(&reg, tok->token_id), "expired token is invalid");
    PASS();
}

TEST(advance_time_test) {
    pc_registry_t reg;
    pc_registry_init(&reg);
    ASSERT(reg.current_logical_time == 0, "starts at 0");
    pc_advance_time(&reg, 100);
    ASSERT(reg.current_logical_time == 100, "advanced to 100");
    pc_advance_time(&reg, 50);
    ASSERT(reg.current_logical_time == 150, "advanced to 150");
    PASS();
}

/* ===== Name Function Tests ===== */

TEST(name_functions_test) {
    ASSERT(strcmp(pc_decision_name(PC_DECISION_ADMIT), "admit") == 0, "admit name");
    ASSERT(strcmp(pc_decision_name(PC_DECISION_VETO), "veto") == 0, "veto name");
    ASSERT(strcmp(pc_decision_name(PC_DECISION_DEFER), "defer") == 0, "defer name");
    ASSERT(strcmp(pc_coverage_name(PC_COVERAGE_FULL), "full") == 0, "full name");
    ASSERT(strcmp(pc_coverage_name(PC_COVERAGE_PARTIAL), "partial") == 0, "partial name");
    ASSERT(strcmp(pc_health_name(PC_HEALTH_HEALTHY), "healthy") == 0, "healthy name");
    ASSERT(strcmp(pc_health_name(PC_HEALTH_UNHEALTHY), "unhealthy") == 0, "unhealthy name");
    ASSERT(strcmp(pc_phase_name(PC_PHASE_K6_COORD), "K6_COORD") == 0, "K6 name");
    ASSERT(strcmp(pc_phase_name(PC_PHASE_O1_CRIT168), "O1_CRIT168") == 0, "O1 name");
    PASS();
}

/* ===== Main ===== */

int main(void) {
    printf("\n=== ZXV Phase Coordinator (K6) Tests ===\n\n");

    RUN(registry_init_test);
    RUN(register_gate_test);
    RUN(set_gate_coverage_test);
    RUN(check_all_gates_test);
    RUN(count_satisfied_gates_test);
    RUN(health_test);
    RUN(admit_simple_test);
    RUN(admit_s0_defer_test);
    RUN(admit_coverage_veto_test);
    RUN(admit_coverage_pass_test);
    RUN(admit_health_veto_test);
    RUN(admit_health_defer_test);
    RUN(admit_health_unknown_defer_test);
    RUN(admit_hardware_action_test);
    RUN(revoke_token_test);
    RUN(find_token_test);
    RUN(token_validity_test);
    RUN(token_expiry_test);
    RUN(advance_time_test);
    RUN(name_functions_test);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);
    if (tests_failed == 0) {
        printf("ALL TESTS PASSED\n");
    }
    return tests_failed > 0 ? 1 : 0;
}
