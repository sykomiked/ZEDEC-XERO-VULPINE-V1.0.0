/* test_cellular_multikernel.c — Host evidence for CELL-001 Cellular Multikernel
 *
 * Covers CELL0 specification evidence (cell contract, lifecycle, routing)
 * and CELL1 emulation evidence (multi-cell discovery, authentication,
 * admission, tri-space commit, CRIT-168 roundtrip, fault containment).
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "cellular_multikernel.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  [TEST] %s ... ", name); } while(0)
#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); return; } while(0)

static uint8_t dummy_digest[CELL_MAX_DIGEST] = {
    0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
    0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,
    0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,
    0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x20
};

static void make_cell(cell_t *c, const char *name, cell_architecture_t arch,
                      uint32_t caps) {
    cell_contract_zero(c);
    strncpy(c->cell_id, name, CELL_NAME_LEN - 1);
    c->arch = arch;
    c->bitness = CELL_BITNESS_64;
    c->trust = CELL_TRUST_KERNEL;
    c->privilege = CELL_PRIVILEGE_EL1;
    c->incarnation = 1;
    c->capabilities = caps;
    c->supported_schema_ids[0] = 1; /* EVT-001 v1 */
    c->supported_schema_count = 1;
    c->memory_ownership_base = 0x40000000;
    c->memory_ownership_size = 0x10000000;
    c->transport_count = 1;
    c->transports[0].type = CELL_TRANSPORT_CACHED_RING;
    c->transports[0].capacity = CELL_MAX_EVENTS;
    c->transports[0].head = 0;
    c->transports[0].tail = 0;
    c->transports[0].authenticated = false;
    c->budget.max_events_per_second = 10000;
    c->budget.max_memory_bytes = 0x10000000;
    cell_set_digest(c, dummy_digest);
}

void test_fabric_init(void) {
    TEST("fabric init");
    cell_fabric_t fabric;
    cell_fabric_init(&fabric);
    if (fabric.cell_count != 0) FAIL("cell_count not zero");
    if (fabric.route_count != 0) FAIL("route_count not zero");
    if (fabric.next_ordinal != 1) FAIL("next_ordinal not 1");
    if (!fabric.has_local_replica) FAIL("local replica not set");
    PASS();
}

void test_cell_discover_and_auth(void) {
    TEST("cell discovery and authentication");
    cell_fabric_t fabric;
    cell_fabric_init(&fabric);

    cell_t k1;
    make_cell(&k1, "k1-oseq", CELL_ARCH_ARM64, CELL_CAP_OSEQ | CELL_CAP_RMAG);
    if (!cell_fabric_discover(&fabric, &k1)) FAIL("discover failed");
    if (fabric.cell_count != 1) FAIL("cell count mismatch");

    uint8_t bad_digest[CELL_MAX_DIGEST];
    memset(bad_digest, 0xff, CELL_MAX_DIGEST);
    if (cell_fabric_authenticate(&fabric, "k1-oseq", bad_digest))
        FAIL("bad digest authenticated");
    if (fabric.cells[0].state != CELL_STATE_QUARANTINED)
        FAIL("cell not quarantined after bad auth");

    if (!cell_fabric_authenticate(&fabric, "k1-oseq", dummy_digest))
        FAIL("good digest rejected");
    if (fabric.cells[0].state != CELL_STATE_AUTHENTICATED)
        FAIL("cell not authenticated");
    PASS();
}

void test_cell_admit_activate_revoke(void) {
    TEST("cell admit, activate, revoke lifecycle");
    cell_fabric_t fabric;
    cell_fabric_init(&fabric);

    cell_t k1;
    make_cell(&k1, "k1-oseq", CELL_ARCH_ARM64, CELL_CAP_OSEQ);
    cell_fabric_discover(&fabric, &k1);
    cell_fabric_authenticate(&fabric, "k1-oseq", dummy_digest);

    cell_state_t st = cell_fabric_admit(&fabric, "k1-oseq");
    if (st != CELL_STATE_ADMITTED) FAIL("admit failed");
    if (fabric.cells[0].admitted_at_ordinal == 0) FAIL("admission ordinal not set");

    if (!cell_fabric_activate(&fabric, "k1-oseq"))
        FAIL("activate failed");
    if (fabric.cells[0].state != CELL_STATE_ACTIVE)
        FAIL("cell not active");

    if (!cell_fabric_revoke(&fabric, "k1-oseq"))
        FAIL("revoke failed");
    if (fabric.cells[0].state != CELL_STATE_REVOKED)
        FAIL("cell not revoked");
    PASS();
}

void test_routing(void) {
    TEST("phase routing table");
    cell_fabric_t fabric;
    cell_fabric_init(&fabric);

    cell_route_t r1 = { 100, 1, "k1-oseq", 5, false };
    cell_route_t r2 = { 200, 1, "k2-rmag", 7, true };
    if (!cell_fabric_add_route(&fabric, &r1)) FAIL("add route 1");
    if (!cell_fabric_add_route(&fabric, &r2)) FAIL("add route 2");

    const char *target = cell_fabric_route_lookup(&fabric, 100, 1);
    if (!target || strcmp(target, "k1-oseq") != 0) FAIL("lookup 100");

    target = cell_fabric_route_lookup(&fabric, 200, 1);
    if (!target || strcmp(target, "k2-rmag") != 0) FAIL("lookup 200");

    target = cell_fabric_route_lookup(&fabric, 999, 1);
    if (target != NULL) FAIL("lookup 999 should be null");
    PASS();
}

void test_transport_backpressure(void) {
    TEST("transport bounded queue backpressure");
    cell_transport_endpoint_t ep;
    memset(&ep, 0, sizeof(ep));
    ep.type = CELL_TRANSPORT_CACHED_RING;
    ep.capacity = 4; /* tiny to force backpressure */
    ep.head = 0;
    ep.tail = 0;

    fabric_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.event_id = 1;

    /* Send 3 events: with capacity 4, head==tail means empty, so
     * after 3 sends tail=3 and head=0, one slot free. */
    if (!cell_transport_send(&ep, &ev)) FAIL("send 1");
    ev.event_id = 2;
    if (!cell_transport_send(&ep, &ev)) FAIL("send 2");
    ev.event_id = 3;
    if (!cell_transport_send(&ep, &ev)) FAIL("send 3");
    ev.event_id = 4;
    /* 4th send: tail becomes (3+1)%4=0 == head -> full, should fail */
    if (cell_transport_send(&ep, &ev))
        FAIL("send 4 should have been rejected (full)");

    fabric_event_t out;
    if (!cell_transport_receive(&ep, &out)) FAIL("recv 1");
    if (!cell_transport_receive(&ep, &out)) FAIL("recv 2");
    if (!cell_transport_receive(&ep, &out)) FAIL("recv 3");
    if (cell_transport_receive(&ep, &out))
        FAIL("recv 4 should fail (empty)");
    PASS();
}

void test_triad_commit_positive(void) {
    TEST("triad commit positive path");
    triad_commit_state_t st;
    triad_commit_reset(&st, 42, 1000);

    fabric_event_t s_plus = { .triad_id = 42, .ordinal = 1, .lane = 0, .decision = 1 };
    fabric_event_t s_minus = { .triad_id = 42, .ordinal = 2, .lane = 1, .decision = 1 };
    fabric_event_t s_zero = { .triad_id = 42, .ordinal = 3, .lane = 2, .decision = 1 };

    if (!triad_commit_update(&st, &s_plus)) FAIL("S+");
    if (st.commit_decision != 0) FAIL("decision should be undecided");
    if (!triad_commit_update(&st, &s_minus)) FAIL("S-");
    if (!triad_commit_update(&st, &s_zero)) FAIL("S0");
    if (st.commit_decision != 1) FAIL("expected commit token");
    PASS();
}

void test_triad_commit_reject(void) {
    TEST("triad commit rejection");
    triad_commit_state_t st;
    triad_commit_reset(&st, 43, 1000);

    fabric_event_t s_plus = { .triad_id = 43, .ordinal = 1, .lane = 0, .decision = 1 };
    fabric_event_t s_minus = { .triad_id = 43, .ordinal = 2, .lane = 1, .decision = 1 };
    fabric_event_t s_zero = { .triad_id = 43, .ordinal = 3, .lane = 2, .decision = 2 }; /* reject */

    triad_commit_update(&st, &s_plus);
    triad_commit_update(&st, &s_minus);
    triad_commit_update(&st, &s_zero);
    if (st.commit_decision != 2) FAIL("expected reject");
    PASS();
}

void test_crit168_roundtrip(void) {
    TEST("CRIT-168 digest roundtrip and equality");
    const char *msg = "event-envelope";
    crit168_value_t a, b;
    crit168_from_digest(&a, (const uint8_t *)msg, strlen(msg));
    crit168_from_digest(&b, (const uint8_t *)msg, strlen(msg));
    if (!crit168_eq(&a, &b)) FAIL("same input should give equal values");

    const char *msg2 = "different";
    crit168_from_digest(&b, (const uint8_t *)msg2, strlen(msg2));
    if (crit168_eq(&a, &b)) FAIL("different input should differ");
    PASS();
}

void test_fault_containment(void) {
    TEST("fault containment: failed cell loses authority");
    cell_fabric_t fabric;
    cell_fabric_init(&fabric);

    cell_t k1;
    make_cell(&k1, "k1-oseq", CELL_ARCH_ARM64, CELL_CAP_OSEQ);
    cell_fabric_discover(&fabric, &k1);
    cell_fabric_authenticate(&fabric, "k1-oseq", dummy_digest);
    cell_fabric_admit(&fabric, "k1-oseq");
    cell_fabric_activate(&fabric, "k1-oseq");

    cell_route_t r = { 100, 1, "k1-oseq", 5, false };
    cell_fabric_add_route(&fabric, &r);

    if (!cell_fabric_handle_fault(&fabric, "k1-oseq", CELL_HEALTH_FAIL_STOPPED))
        FAIL("fault handling failed");
    if (fabric.cells[0].state != CELL_STATE_FAILED &&
        fabric.cells[0].state != CELL_STATE_REVOKED)
        FAIL("cell not failed or revoked");
    if (cell_fabric_route_lookup(&fabric, 100, 1) != NULL)
        FAIL("failed cell still in routing table");
    PASS();
}

static int fake_created = 0;
static int fake_terminated = 0;
static uint32_t fake_next_task = 10;
static int stub_ran = 0;

static void stub_entry(void *arg) {
    (void)arg;
    stub_ran++;
}

static int32_t fake_create(const char *name, void (*entry)(void *), void *arg,
                           uint32_t *task_id) {
    (void)name; (void)entry; (void)arg;
    *task_id = fake_next_task++;
    fake_created++;
    return 0;
}

static void fake_terminate(uint32_t task_id) {
    (void)task_id;
    fake_terminated++;
}

static void test_recovery_incarnation_replacement(void) {
    TEST("recovery and incarnation replacement");
    cell_fabric_t fabric;
    cell_fabric_init(&fabric);

    cell_t k1;
    make_cell(&k1, "k1-oseq", CELL_ARCH_ARM64, CELL_CAP_OSEQ);
    if (!cell_fabric_discover(&fabric, &k1)) FAIL("initial discover failed");
    if (!cell_fabric_authenticate(&fabric, "k1-oseq", dummy_digest))
        FAIL("initial auth failed");
    if (cell_fabric_admit(&fabric, "k1-oseq") != CELL_STATE_ADMITTED)
        FAIL("initial admit failed");

    cell_route_t r = { 100, 1, "k1-oseq", 5, false };
    if (!cell_fabric_add_route(&fabric, &r)) FAIL("add route failed");

    if (!cell_fabric_activate(&fabric, "k1-oseq"))
        FAIL("initial activate failed");

    if (!cell_fabric_handle_fault(&fabric, "k1-oseq", CELL_HEALTH_RECOVERING))
        FAIL("handle_fault RECOVERING failed");
    if (fabric.cells[0].state != CELL_STATE_RECOVERING)
        FAIL("cell not in recovering state");
    if (fabric.cells[0].health != CELL_HEALTH_RECOVERING)
        FAIL("cell health not recovering");
    if (fabric.cells[0].incarnation != 1)
        FAIL("incarnation changed unexpectedly");

    cell_t k1_v2;
    make_cell(&k1_v2, "k1-oseq", CELL_ARCH_ARM64, CELL_CAP_OSEQ);
    k1_v2.incarnation = 2;
    if (!cell_fabric_discover(&fabric, &k1_v2))
        FAIL("recovery replacement discover failed");

    if (fabric.cell_count != 1) FAIL("cell count changed after replacement");
    if (fabric.cells[0].incarnation != 2)
        FAIL("replacement did not update incarnation");
    if (fabric.cells[0].state != CELL_STATE_DISCOVERED)
        FAIL("replacement did not reset state to discovered");
    if (fabric.cells[0].health != CELL_HEALTH_OK)
        FAIL("replacement did not reset health");
    if (fabric.cells[0].admitted_at_ordinal != 0)
        FAIL("admitted ordinal not reset");
    if (cell_fabric_route_lookup(&fabric, 100, 1) != NULL)
        FAIL("old route still present after replacement");
    if (fabric.control_plane_incarnation != 2)
        FAIL("control plane incarnation not advanced");

    if (!cell_fabric_authenticate(&fabric, "k1-oseq", dummy_digest))
        FAIL("new cell auth failed");
    if (cell_fabric_admit(&fabric, "k1-oseq") != CELL_STATE_ADMITTED)
        FAIL("new cell admit failed");
    if (!cell_fabric_activate(&fabric, "k1-oseq"))
        FAIL("new cell activate failed");

    /* Stale or equal incarnation must be rejected. */
    cell_t k1_v1_again;
    make_cell(&k1_v1_again, "k1-oseq", CELL_ARCH_ARM64, CELL_CAP_OSEQ);
    k1_v1_again.incarnation = 1;
    if (cell_fabric_discover(&fabric, &k1_v1_again))
        FAIL("stale incarnation should have been rejected");

    /* Healthy active cell must not be replaced by a higher incarnation. */
    cell_t k1_v3;
    make_cell(&k1_v3, "k1-oseq", CELL_ARCH_ARM64, CELL_CAP_OSEQ);
    k1_v3.incarnation = 3;
    if (cell_fabric_discover(&fabric, &k1_v3))
        FAIL("healthy active cell should not be replaced");

    PASS();
}

static void test_recovery_suspends_execution_and_scrubs_metadata(void) {
    TEST("recovery suspends execution and scrubs stale metadata");
    cell_fabric_t fabric;
    cell_fabric_init(&fabric);

    cell_t k1;
    make_cell(&k1, "k1-oseq", CELL_ARCH_ARM64, CELL_CAP_OSEQ);
    if (!cell_fabric_discover(&fabric, &k1)) FAIL("initial discover failed");
    if (!cell_fabric_authenticate(&fabric, "k1-oseq", dummy_digest))
        FAIL("initial auth failed");
    if (cell_fabric_admit(&fabric, "k1-oseq") != CELL_STATE_ADMITTED)
        FAIL("initial admit failed");

    const cell_execution_backend_t backend = { fake_create, fake_terminate };
    cell_fabric_set_backend(&fabric, &backend);
    fake_created = 0;
    fake_terminated = 0;
    fake_next_task = 10;

    cell_state_t st = cell_fabric_execute(&fabric, "k1-oseq", stub_entry, NULL);
    if (st != CELL_STATE_ACTIVE) FAIL("execute did not reach active state");
    if (fake_created != 1) FAIL("backend create not called");
    if (fake_terminated != 0) FAIL("terminated before fault");
    if (!fabric.cells[0].is_executing) FAIL("cell not executing");
    if (fabric.cells[0].task_id != 10) FAIL("unexpected task id");

    /* RECOVERING must stop the active execution context. */
    if (!cell_fabric_handle_fault(&fabric, "k1-oseq", CELL_HEALTH_RECOVERING))
        FAIL("handle_fault RECOVERING failed");
    if (fabric.cells[0].state != CELL_STATE_RECOVERING)
        FAIL("cell not in recovering state");
    if (fabric.cells[0].is_executing)
        FAIL("cell still executing while recovering");
    if (fabric.cells[0].task_id != 0)
        FAIL("task id not cleared");
    if (fake_terminated != 1)
        FAIL("backend terminate not called");

    /* Replacement must scrub stale lifecycle and execution metadata. */
    cell_t k1_v2;
    make_cell(&k1_v2, "k1-oseq", CELL_ARCH_ARM64, CELL_CAP_OSEQ);
    k1_v2.incarnation = 2;
    k1_v2.admitted_at_ordinal = 999;   /* stale */
    k1_v2.revoked_at_ordinal = 888;    /* stale */
    k1_v2.entry_point = stub_entry;    /* stale */
    k1_v2.entry_arg = &st;             /* stale */
    if (!cell_fabric_discover(&fabric, &k1_v2))
        FAIL("replacement discover failed");

    if (fabric.cell_count != 1) FAIL("cell count changed");
    if (fabric.cells[0].incarnation != 2)
        FAIL("incarnation not updated");
    if (fabric.cells[0].state != CELL_STATE_DISCOVERED)
        FAIL("state not reset");
    if (fabric.cells[0].health != CELL_HEALTH_OK)
        FAIL("health not reset");
    if (fabric.cells[0].admitted_at_ordinal != 0)
        FAIL("admitted_at_ordinal not scrubbed");
    if (fabric.cells[0].revoked_at_ordinal != 0)
        FAIL("revoked_at_ordinal not scrubbed");
    if (fabric.cells[0].entry_point != NULL)
        FAIL("entry_point not scrubbed");
    if (fabric.cells[0].entry_arg != NULL)
        FAIL("entry_arg not scrubbed");
    if (fabric.cells[0].is_executing)
        FAIL("new cell already executing");
    if (fabric.cells[0].task_id != 0)
        FAIL("task id not zeroed");
    if (fake_terminated != 1)
        FAIL("terminate called again on replacement");

    /* The new incarnation can be re-admitted and executed. */
    if (!cell_fabric_authenticate(&fabric, "k1-oseq", dummy_digest))
        FAIL("new cell auth failed");
    if (cell_fabric_admit(&fabric, "k1-oseq") != CELL_STATE_ADMITTED)
        FAIL("new cell admit failed");
    if (!cell_fabric_activate(&fabric, "k1-oseq"))
        FAIL("new cell activate failed");

    fake_created = 0;
    st = cell_fabric_execute(&fabric, "k1-oseq", stub_entry, NULL);
    if (st != CELL_STATE_ACTIVE) FAIL("new cell execute failed");
    if (fake_created != 1) FAIL("new cell backend create not called");
    if (!fabric.cells[0].is_executing) FAIL("new cell not executing");

    PASS();
}

static void test_cell_execution(void) {
    TEST("cell becomes separately protected executing cell");
    cell_fabric_t fabric;
    cell_fabric_init(&fabric);

    cell_t k1;
    make_cell(&k1, "k1-oseq", CELL_ARCH_ARM64, CELL_CAP_OSEQ);
    cell_fabric_discover(&fabric, &k1);
    cell_fabric_authenticate(&fabric, "k1-oseq", dummy_digest);
    cell_fabric_admit(&fabric, "k1-oseq");

    const cell_execution_backend_t backend = { fake_create, fake_terminate };
    cell_fabric_set_backend(&fabric, &backend);

    fake_created = 0;
    fake_terminated = 0;
    fake_next_task = 10;
    stub_ran = 0;
    cell_state_t st = cell_fabric_execute(&fabric, "k1-oseq", stub_entry, NULL);
    if (st != CELL_STATE_ACTIVE) FAIL("execute did not reach active state");
    if (fake_created != 1) FAIL("backend create not called exactly once");
    if (!fabric.cells[0].is_executing) FAIL("cell not marked executing");
    if (fabric.cells[0].task_id == 0) FAIL("cell has no task_id");

    if (!cell_fabric_terminate(&fabric, "k1-oseq"))
        FAIL("terminate failed");
    if (fake_terminated != 1) FAIL("backend terminate not called exactly once");
    if (fabric.cells[0].is_executing) FAIL("cell still marked executing after terminate");
    if (fabric.cells[0].state != CELL_STATE_REVOKED)
        FAIL("cell not revoked after terminate");
    PASS();
}

int main(void) {
    printf("=== Cellular Multikernel (CELL-001) Evidence Tests ===\n\n");
    test_fabric_init();
    test_cell_discover_and_auth();
    test_cell_admit_activate_revoke();
    test_routing();
    test_transport_backpressure();
    test_triad_commit_positive();
    test_triad_commit_reject();
    test_crit168_roundtrip();
    test_fault_containment();
    test_recovery_incarnation_replacement();
    test_recovery_suspends_execution_and_scrubs_metadata();
    test_cell_execution();

    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
