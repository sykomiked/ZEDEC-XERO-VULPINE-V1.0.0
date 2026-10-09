/* test_iphase.c — Inter-Phase Routing (K4) Tests
 *
 * Tests for endpoint registry, route contracts, deterministic selection,
 * failover, and tie-breaking policies.
 *
 * Author: 36N9 Genetics, LLC
 * License: SEL-3.3 (kernel component)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "iphase.h"

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
    iphase_registry_t reg;
    iphase_registry_init(&reg);
    ASSERT(reg.endpoint_count == 0, "no endpoints");
    ASSERT(reg.route_count == 0, "no routes");
    ASSERT(reg.next_endpoint_id == 1, "next ep id is 1");
    ASSERT(reg.next_route_id == 1, "next route id is 1");
    ASSERT(reg.rr_counter == 0, "rr counter is 0");
    PASS();
}

/* ===== Endpoint Tests ===== */

TEST(register_endpoint_test) {
    iphase_registry_t reg;
    iphase_registry_init(&reg);
    int32_t idx = iphase_register_endpoint(&reg, "storage.svc",
                                           IPHASE_EP_KERNEL_SERVICE,
                                           "storage.read", 3);
    ASSERT(idx >= 0, "endpoint registered");
    ASSERT(idx == 0, "first endpoint is index 0");
    ASSERT(reg.endpoint_count == 1, "count incremented");

    iphase_endpoint_t *ep = iphase_get_endpoint(&reg, 0);
    ASSERT(ep != NULL, "endpoint retrieved");
    ASSERT(strcmp(ep->name, "storage.svc") == 0, "name matches");
    ASSERT(ep->type == IPHASE_EP_KERNEL_SERVICE, "type matches");
    ASSERT(strcmp(ep->capability, "storage.read") == 0, "capability matches");
    ASSERT(ep->phase_binding == 3, "phase binding is 3");
    ASSERT(ep->active, "is active");
    ASSERT(ep->load == 0, "load is 0");
    PASS();
}

TEST(find_endpoint_test) {
    iphase_registry_t reg;
    iphase_registry_init(&reg);
    iphase_register_endpoint(&reg, "alpha", IPHASE_EP_KERNEL_SERVICE, "cap", 0);
    iphase_register_endpoint(&reg, "beta", IPHASE_EP_USER_SERVICE, "cap", 0);

    ASSERT(iphase_find_endpoint(&reg, "alpha") == 0, "found alpha");
    ASSERT(iphase_find_endpoint(&reg, "beta") == 1, "found beta");
    ASSERT(iphase_find_endpoint(&reg, "gamma") == -1, "gamma not found");
    PASS();
}

TEST(duplicate_endpoint_test) {
    iphase_registry_t reg;
    iphase_registry_init(&reg);
    int32_t idx1 = iphase_register_endpoint(&reg, "svc", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    int32_t idx2 = iphase_register_endpoint(&reg, "svc", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    ASSERT(idx1 == idx2, "duplicate returns same index");
    ASSERT(reg.endpoint_count == 1, "count still 1");
    PASS();
}

TEST(set_endpoint_active_test) {
    iphase_registry_t reg;
    iphase_registry_init(&reg);
    iphase_register_endpoint(&reg, "ep", IPHASE_EP_NETWORK, "net", 0);
    ASSERT(iphase_set_endpoint_active(&reg, 0, false), "deactivated");
    ASSERT(!iphase_get_endpoint(&reg, 0)->active, "is inactive");
    ASSERT(iphase_set_endpoint_active(&reg, 0, true), "activated");
    ASSERT(iphase_get_endpoint(&reg, 0)->active, "is active");
    PASS();
}

/* ===== Route Tests ===== */

TEST(add_route_test) {
    iphase_registry_t reg;
    iphase_registry_init(&reg);
    iphase_register_endpoint(&reg, "src", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_register_endpoint(&reg, "dst", IPHASE_EP_KERNEL_SERVICE, "c", 0);

    int32_t idx = iphase_add_route(&reg, 0, 1, 1, 100, true);
    ASSERT(idx >= 0, "route added");
    ASSERT(reg.route_count == 1, "count incremented");

    iphase_route_t *r = iphase_get_route(&reg, 0);
    ASSERT(r != NULL, "route retrieved");
    ASSERT(r->source_endpoint == 0, "source is 0");
    ASSERT(r->dest_endpoint == 1, "dest is 1");
    ASSERT(r->priority == 1, "priority is 1");
    ASSERT(r->weight == 100, "weight is 100");
    ASSERT(r->status == IPHASE_ROUTE_ACTIVE, "status is active");
    ASSERT(r->requires_admission, "requires admission");
    ASSERT(r->use_count == 0, "use count is 0");
    PASS();
}

TEST(set_route_status_test) {
    iphase_registry_t reg;
    iphase_registry_init(&reg);
    iphase_register_endpoint(&reg, "a", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_register_endpoint(&reg, "b", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_add_route(&reg, 0, 1, 1, 100, false);

    ASSERT(iphase_set_route_status(&reg, 0, IPHASE_ROUTE_FAILED), "status set to failed");
    ASSERT(iphase_get_route(&reg, 0)->status == IPHASE_ROUTE_FAILED, "is failed");
    PASS();
}

TEST(add_failover_test) {
    iphase_registry_t reg;
    iphase_registry_init(&reg);
    iphase_register_endpoint(&reg, "primary", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_register_endpoint(&reg, "secondary", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_register_endpoint(&reg, "tertiary", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_add_route(&reg, 0, 1, 1, 100, false);

    ASSERT(iphase_add_failover(&reg, 0, 2), "failover added");
    ASSERT(iphase_get_route(&reg, 0)->failover_count == 1, "failover count is 1");
    ASSERT(iphase_get_route(&reg, 0)->failover_chain[0] == 2, "failover to endpoint 2");
    PASS();
}

/* ===== Route Selection Tests ===== */

TEST(select_route_simple_test) {
    iphase_registry_t reg;
    iphase_registry_init(&reg);
    iphase_register_endpoint(&reg, "a", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_register_endpoint(&reg, "b", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_add_route(&reg, 0, 1, 1, 100, false);

    int32_t sel = iphase_select_route(&reg, 0, 1, IPHASE_TIE_LOWEST_ID);
    ASSERT(sel >= 0, "route selected");
    ASSERT(sel == 0, "selected route 0");
    ASSERT(iphase_get_route(&reg, 0)->use_count == 1, "use count incremented");
    ASSERT(reg.routes_selected == 1, "routes_selected incremented");
    ASSERT(iphase_get_endpoint(&reg, 1)->load == 1, "dest load incremented");
    PASS();
}

TEST(select_route_priority_test) {
    iphase_registry_t reg;
    iphase_registry_init(&reg);
    iphase_register_endpoint(&reg, "a", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_register_endpoint(&reg, "b", IPHASE_EP_KERNEL_SERVICE, "c", 0);

    /* Low priority (high number) route */
    iphase_add_route(&reg, 0, 1, 10, 100, false);
    /* High priority (low number) route */
    iphase_add_route(&reg, 0, 1, 1, 50, false);

    int32_t sel = iphase_select_route(&reg, 0, 1, IPHASE_TIE_LOWEST_ID);
    ASSERT(sel == 1, "selected high-priority route (index 1)");
    PASS();
}

TEST(select_route_weight_test) {
    iphase_registry_t reg;
    iphase_registry_init(&reg);
    iphase_register_endpoint(&reg, "a", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_register_endpoint(&reg, "b", IPHASE_EP_KERNEL_SERVICE, "c", 0);

    /* Same priority, different weight */
    iphase_add_route(&reg, 0, 1, 1, 50, false);
    iphase_add_route(&reg, 0, 1, 1, 100, false);

    int32_t sel = iphase_select_route(&reg, 0, 1, IPHASE_TIE_LOWEST_ID);
    ASSERT(sel == 1, "selected higher-weight route (index 1)");
    PASS();
}

TEST(select_route_tie_lowest_id_test) {
    iphase_registry_t reg;
    iphase_registry_init(&reg);
    iphase_register_endpoint(&reg, "a", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_register_endpoint(&reg, "b", IPHASE_EP_KERNEL_SERVICE, "c", 0);

    /* Same priority and weight */
    iphase_add_route(&reg, 0, 1, 1, 100, false);
    iphase_add_route(&reg, 0, 1, 1, 100, false);

    int32_t sel = iphase_select_route(&reg, 0, 1, IPHASE_TIE_LOWEST_ID);
    ASSERT(sel == 0, "selected lowest ID (index 0)");
    PASS();
}

TEST(select_route_tie_highest_id_test) {
    iphase_registry_t reg;
    iphase_registry_init(&reg);
    iphase_register_endpoint(&reg, "a", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_register_endpoint(&reg, "b", IPHASE_EP_KERNEL_SERVICE, "c", 0);

    iphase_add_route(&reg, 0, 1, 1, 100, false);
    iphase_add_route(&reg, 0, 1, 1, 100, false);

    int32_t sel = iphase_select_route(&reg, 0, 1, IPHASE_TIE_HIGHEST_ID);
    ASSERT(sel == 1, "selected highest ID (index 1)");
    PASS();
}

TEST(select_route_tie_round_robin_test) {
    iphase_registry_t reg;
    iphase_registry_init(&reg);
    iphase_register_endpoint(&reg, "a", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_register_endpoint(&reg, "b", IPHASE_EP_KERNEL_SERVICE, "c", 0);

    iphase_add_route(&reg, 0, 1, 1, 100, false);
    iphase_add_route(&reg, 0, 1, 1, 100, false);

    int32_t sel1 = iphase_select_route(&reg, 0, 1, IPHASE_TIE_ROUND_ROBIN);
    int32_t sel2 = iphase_select_route(&reg, 0, 1, IPHASE_TIE_ROUND_ROBIN);
    ASSERT(sel1 != sel2, "round robin selected different routes");
    ASSERT(sel1 >= 0 && sel2 >= 0, "both selections valid");
    PASS();
}

TEST(select_route_tie_defer_s0_test) {
    iphase_registry_t reg;
    iphase_registry_init(&reg);
    iphase_register_endpoint(&reg, "a", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_register_endpoint(&reg, "b", IPHASE_EP_KERNEL_SERVICE, "c", 0);

    iphase_add_route(&reg, 0, 1, 1, 100, false);
    iphase_add_route(&reg, 0, 1, 1, 100, false);

    int32_t sel = iphase_select_route(&reg, 0, 1, IPHASE_TIE_DEFER_S0);
    ASSERT(sel == -2, "deferred to S0");
    ASSERT(reg.routes_rejected == 1, "rejection counted");
    PASS();
}

TEST(select_route_no_route_test) {
    iphase_registry_t reg;
    iphase_registry_init(&reg);
    iphase_register_endpoint(&reg, "a", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_register_endpoint(&reg, "b", IPHASE_EP_KERNEL_SERVICE, "c", 0);

    int32_t sel = iphase_select_route(&reg, 0, 1, IPHASE_TIE_LOWEST_ID);
    ASSERT(sel == -1, "no route returns -1");
    PASS();
}

TEST(select_route_inactive_endpoint_test) {
    iphase_registry_t reg;
    iphase_registry_init(&reg);
    iphase_register_endpoint(&reg, "a", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_register_endpoint(&reg, "b", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_add_route(&reg, 0, 1, 1, 100, false);

    iphase_set_endpoint_active(&reg, 1, false);
    int32_t sel = iphase_select_route(&reg, 0, 1, IPHASE_TIE_LOWEST_ID);
    ASSERT(sel == -1, "inactive dest returns -1");
    PASS();
}

TEST(select_route_failed_route_test) {
    iphase_registry_t reg;
    iphase_registry_init(&reg);
    iphase_register_endpoint(&reg, "a", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_register_endpoint(&reg, "b", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_add_route(&reg, 0, 1, 1, 100, false);

    iphase_set_route_status(&reg, 0, IPHASE_ROUTE_FAILED);
    int32_t sel = iphase_select_route(&reg, 0, 1, IPHASE_TIE_LOWEST_ID);
    ASSERT(sel == -1, "failed route not selected");
    PASS();
}

/* ===== Failover Tests ===== */

TEST(select_failover_test) {
    iphase_registry_t reg;
    iphase_registry_init(&reg);
    iphase_register_endpoint(&reg, "primary", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_register_endpoint(&reg, "secondary", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_register_endpoint(&reg, "tertiary", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_add_route(&reg, 0, 1, 1, 100, false);
    iphase_add_failover(&reg, 0, 2);

    int32_t fail = iphase_select_failover(&reg, 0);
    ASSERT(fail == 2, "failover to endpoint 2");
    ASSERT(reg.failovers_triggered == 1, "failover count incremented");
    PASS();
}

TEST(select_failover_inactive_test) {
    iphase_registry_t reg;
    iphase_registry_init(&reg);
    iphase_register_endpoint(&reg, "primary", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_register_endpoint(&reg, "secondary", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_add_route(&reg, 0, 1, 1, 100, false);
    iphase_add_failover(&reg, 0, 1);

    iphase_set_endpoint_active(&reg, 1, false);
    int32_t fail = iphase_select_failover(&reg, 0);
    ASSERT(fail == -1, "inactive failover returns -1");
    PASS();
}

TEST(select_failover_none_test) {
    iphase_registry_t reg;
    iphase_registry_init(&reg);
    iphase_register_endpoint(&reg, "a", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_register_endpoint(&reg, "b", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_add_route(&reg, 0, 1, 1, 100, false);

    int32_t fail = iphase_select_failover(&reg, 0);
    ASSERT(fail == -1, "no failover returns -1");
    PASS();
}

/* ===== Query Tests ===== */

TEST(count_by_status_test) {
    iphase_registry_t reg;
    iphase_registry_init(&reg);
    iphase_register_endpoint(&reg, "a", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_register_endpoint(&reg, "b", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_register_endpoint(&reg, "c", IPHASE_EP_KERNEL_SERVICE, "c", 0);

    iphase_add_route(&reg, 0, 1, 1, 100, false);
    iphase_add_route(&reg, 0, 2, 1, 100, false);
    iphase_set_route_status(&reg, 1, IPHASE_ROUTE_DISABLED);

    ASSERT(iphase_count_routes_by_status(&reg, IPHASE_ROUTE_ACTIVE) == 1, "1 active");
    ASSERT(iphase_count_routes_by_status(&reg, IPHASE_ROUTE_DISABLED) == 1, "1 disabled");
    PASS();
}

TEST(count_by_type_test) {
    iphase_registry_t reg;
    iphase_registry_init(&reg);
    iphase_register_endpoint(&reg, "k1", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_register_endpoint(&reg, "k2", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_register_endpoint(&reg, "h1", IPHASE_EP_HARDWARE, "c", 0);
    iphase_register_endpoint(&reg, "n1", IPHASE_EP_NETWORK, "c", 0);

    ASSERT(iphase_count_endpoints_by_type(&reg, IPHASE_EP_KERNEL_SERVICE) == 2, "2 kernel");
    ASSERT(iphase_count_endpoints_by_type(&reg, IPHASE_EP_HARDWARE) == 1, "1 hardware");
    ASSERT(iphase_count_endpoints_by_type(&reg, IPHASE_EP_NETWORK) == 1, "1 network");
    PASS();
}

TEST(is_reachable_test) {
    iphase_registry_t reg;
    iphase_registry_init(&reg);
    iphase_register_endpoint(&reg, "a", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_register_endpoint(&reg, "b", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_register_endpoint(&reg, "c", IPHASE_EP_KERNEL_SERVICE, "c", 0);
    iphase_add_route(&reg, 0, 1, 1, 100, false);

    ASSERT(iphase_is_endpoint_reachable(&reg, 0, 1), "a->b reachable");
    ASSERT(!iphase_is_endpoint_reachable(&reg, 0, 2), "a->c not reachable");
    ASSERT(!iphase_is_endpoint_reachable(&reg, 1, 0), "b->a not reachable");
    PASS();
}

/* ===== Name Function Tests ===== */

TEST(name_functions_test) {
    ASSERT(strcmp(iphase_endpoint_type_name(IPHASE_EP_KERNEL_SERVICE), "kernel_service") == 0, "kernel_service name");
    ASSERT(strcmp(iphase_endpoint_type_name(IPHASE_EP_HARDWARE), "hardware") == 0, "hardware name");
    ASSERT(strcmp(iphase_endpoint_type_name(IPHASE_EP_NETWORK), "network") == 0, "network name");
    ASSERT(strcmp(iphase_route_status_name(IPHASE_ROUTE_ACTIVE), "active") == 0, "active name");
    ASSERT(strcmp(iphase_route_status_name(IPHASE_ROUTE_FAILED), "failed") == 0, "failed name");
    ASSERT(strcmp(iphase_tie_policy_name(IPHASE_TIE_LOWEST_ID), "lowest_id") == 0, "lowest_id name");
    ASSERT(strcmp(iphase_tie_policy_name(IPHASE_TIE_DEFER_S0), "defer_s0") == 0, "defer_s0 name");
    PASS();
}

/* ===== Main ===== */

int main(void) {
    printf("\n=== ZXV IPHASE (K4) Tests ===\n\n");

    RUN(registry_init_test);
    RUN(register_endpoint_test);
    RUN(find_endpoint_test);
    RUN(duplicate_endpoint_test);
    RUN(set_endpoint_active_test);
    RUN(add_route_test);
    RUN(set_route_status_test);
    RUN(add_failover_test);
    RUN(select_route_simple_test);
    RUN(select_route_priority_test);
    RUN(select_route_weight_test);
    RUN(select_route_tie_lowest_id_test);
    RUN(select_route_tie_highest_id_test);
    RUN(select_route_tie_round_robin_test);
    RUN(select_route_tie_defer_s0_test);
    RUN(select_route_no_route_test);
    RUN(select_route_inactive_endpoint_test);
    RUN(select_route_failed_route_test);
    RUN(select_failover_test);
    RUN(select_failover_inactive_test);
    RUN(select_failover_none_test);
    RUN(count_by_status_test);
    RUN(count_by_type_test);
    RUN(is_reachable_test);
    RUN(name_functions_test);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);
    if (tests_failed == 0) {
        printf("ALL TESTS PASSED\n");
    }
    return tests_failed > 0 ? 1 : 0;
}
