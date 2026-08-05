/* test_event_space.c — Host-side tests for ZXV event-space infrastructure
 *
 * Tests cover:
 *   1. Event envelope init, payload, CRC, validation
 *   2. Sequencer init, domain creation, capabilities, schemas
 *   3. Enqueue/dequeue with causal ordering
 *   4. Dispatch with budget enforcement
 *   5. Self-audit: queue overflow, budget invariant, fault rate
 *   6. Self-healing: suspend, quarantine, recovery, terminate
 *   7. Integrated audit-heal cycle
 *   8. Edge cases: full queue, over-budget, fault storms
 *
 * Compile: gcc -Wall -Werror -Wextra -I. -o test_event_space test_event_space.c event_envelope.c event_sequencer.c self_audit.c self_healing.c && ./test_event_space
 *
 * Author: 36N9 Genetics, LLC
 * License: SEL-3.3 (test component)
 */
#include "event_space.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  [TEST] %s ... ", name); \
    } while (0)

#define PASS() \
    do { \
        printf("PASS\n"); \
        tests_passed++; \
    } while (0)

#define FAIL(msg) \
    do { \
        printf("FAIL: %s\n", msg); \
        tests_failed++; \
    } while (0)

#define ASSERT(cond, msg) \
    do { \
        if (!(cond)) { FAIL(msg); return; } \
    } while (0)

/* Helper since copy_str is static in the .c files */
static void copy_str_h(char *dst, const char *src, uint32_t max) {
    uint32_t i;
    for (i = 0; i + 1 < max && src && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

/* ===== Test 1: Event envelope initialization ===== */
static void test_envelope_init(void) {
    TEST("envelope_init");
    ev_envelope_t env;
    ev_node_id_t node = {0};
    copy_str_h(node.id, "arm64.cluster0", EV_NODE_ID_LEN);
    node.incarnation = 1;

    ev_envelope_init(&env, &node, 42, "zxv.storage.read.request", 3);
    ASSERT(env.event_id.local_sequence == 42, "sequence mismatch");
    ASSERT(env.event_id.node.incarnation == 1, "incarnation mismatch");
    ASSERT(strcmp(env.schema, "zxv.storage.read.request") == 0, "schema mismatch");
    ASSERT(env.schema_version == 3, "schema version mismatch");
    ASSERT(env.delivery == EV_DELIVERY_BEST_EFFORT, "default delivery wrong");
    ASSERT(env.priority == EV_PRIORITY_NORMAL, "default priority wrong");
    ASSERT(env.num_causal_parents == 0, "should have no causal parents");
    ASSERT(env.payload_len == 0, "payload should be empty");
    PASS();
}

/* ===== Test 2: Event envelope payload and hash ===== */
static void test_envelope_payload(void) {
    TEST("envelope_payload");
    ev_envelope_t env;
    ev_node_id_t node = {0};
    copy_str_h(node.id, "test", EV_NODE_ID_LEN);
    ev_envelope_init(&env, &node, 1, "test.schema", 1);

    uint8_t data[] = "Hello, ZXV event space!";
    bool ok = ev_envelope_set_payload(&env, data, sizeof(data));
    ASSERT(ok, "set_payload failed");
    ASSERT(env.payload_len == sizeof(data), "payload length mismatch");
    ASSERT(env.payload_hash[0] != 0, "hash should be non-zero for non-empty payload");

    /* Verify hash is deterministic */
    ev_envelope_t env2;
    ev_envelope_init(&env2, &node, 1, "test.schema", 1);
    ev_envelope_set_payload(&env2, data, sizeof(data));
    ASSERT(memcmp(env.payload_hash, env2.payload_hash, 32) == 0, "hash not deterministic");

    /* Different data should produce different hash */
    uint8_t data2[] = "Different data!";
    ev_envelope_set_payload(&env2, data2, sizeof(data2));
    ASSERT(memcmp(env.payload_hash, env2.payload_hash, 32) != 0, "different data should hash differently");
    PASS();
}

/* ===== Test 3: Event envelope CRC ===== */
static void test_envelope_crc(void) {
    TEST("envelope_crc");
    ev_envelope_t env;
    ev_node_id_t node = {0};
    copy_str_h(node.id, "test", EV_NODE_ID_LEN);
    ev_envelope_init(&env, &node, 1, "test.schema", 1);
    ev_envelope_set_payload(&env, (const uint8_t *)"data", 4);

    ev_envelope_compute_crc(&env);
    ASSERT(env.header_crc != 0, "CRC should be non-zero");
    ASSERT(ev_envelope_verify_crc(&env), "CRC verification should pass");

    /* Tamper with envelope — CRC should fail */
    env.payload_len = 99;
    ASSERT(!ev_envelope_verify_crc(&env), "CRC should fail after tampering");
    PASS();
}

/* ===== Test 4: Event envelope validation ===== */
static void test_envelope_validate(void) {
    TEST("envelope_validate");
    ev_envelope_t env;
    ev_node_id_t node = {0};
    copy_str_h(node.id, "test", EV_NODE_ID_LEN);
    ev_envelope_init(&env, &node, 1, "test.schema", 1);
    ev_envelope_compute_crc(&env);
    ASSERT(ev_envelope_validate(&env), "valid envelope should pass validation");

    /* Empty schema should fail */
    ev_envelope_t bad;
    ev_envelope_init(&bad, &node, 1, "", 1);
    ev_envelope_compute_crc(&bad);
    ASSERT(!ev_envelope_validate(&bad), "empty schema should fail validation");
    PASS();
}

/* ===== Test 5: Sequencer and domain creation ===== */
static void test_sequencer_init(void) {
    TEST("sequencer_init");
    static ev_sequencer_t seq;
    ev_seq_init(&seq, "arm64.cluster0");
    ASSERT(strcmp(seq.node.id, "arm64.cluster0") == 0, "node ID mismatch");
    ASSERT(seq.node.incarnation == 1, "incarnation should be 1");
    ASSERT(seq.next_sequence == 1, "next_sequence should be 1");
    ASSERT(seq.num_domains == 0, "should start with 0 domains");

    int32_t id = ev_seq_create_domain(&seq, "storage", 100, 200, EV_CONSISTENCY_LOCAL);
    ASSERT(id > 0, "domain creation should succeed");
    ASSERT(seq.num_domains == 1, "should have 1 domain");

    ev_domain_t *dom = ev_seq_get_domain(&seq, (uint32_t)id);
    ASSERT(dom != NULL, "should find domain by ID");
    ASSERT(strcmp(dom->name, "storage") == 0, "domain name mismatch");
    ASSERT(dom->state == EV_DOMAIN_READY, "new domain should be READY");
    ASSERT(dom->event_budget == 100, "budget mismatch");
    ASSERT(dom->events_remaining == 100, "events_remaining should equal budget");
    ASSERT(dom->consistency == EV_CONSISTENCY_LOCAL, "consistency mismatch");
    PASS();
}

/* ===== Test 6: Domain capabilities ===== */
static void test_domain_capabilities(void) {
    TEST("domain_capabilities");
    static ev_sequencer_t seq;
    ev_seq_init(&seq, "test");
    int32_t id = ev_seq_create_domain(&seq, "notes", 50, 100, EV_CONSISTENCY_CAUSAL);

    bool ok = ev_domain_grant_capability(&seq, (uint32_t)id, "storage.documents.read");
    ASSERT(ok, "grant should succeed");
    ok = ev_domain_grant_capability(&seq, (uint32_t)id, "storage.documents.write");
    ASSERT(ok, "second grant should succeed");

    ev_domain_t *dom = ev_seq_get_domain(&seq, (uint32_t)id);
    ASSERT(dom->num_capabilities == 2, "should have 2 capabilities");
    ASSERT(strcmp(dom->capabilities[0].name, "storage.documents.read") == 0, "cap 0 mismatch");
    ASSERT(strcmp(dom->capabilities[1].name, "storage.documents.write") == 0, "cap 1 mismatch");
    ASSERT(dom->capabilities[0].granted, "cap 0 should be granted");

    /* Granting same capability again should succeed (idempotent) */
    ok = ev_domain_grant_capability(&seq, (uint32_t)id, "storage.documents.read");
    ASSERT(ok, "duplicate grant should succeed");
    ASSERT(dom->num_capabilities == 2, "should still have 2 capabilities (no duplicate)");
    PASS();
}

/* ===== Test 7: Domain schema acceptance ===== */
static void test_domain_schemas(void) {
    TEST("domain_schemas");
    static ev_sequencer_t seq;
    ev_seq_init(&seq, "test");
    int32_t id = ev_seq_create_domain(&seq, "storage", 100, 100, EV_CONSISTENCY_LOCAL);

    bool ok = ev_domain_add_accepted_schema(&seq, (uint32_t)id, "zxv.storage");
    ASSERT(ok, "add accepted schema should succeed");
    ok = ev_domain_add_emitted_schema(&seq, (uint32_t)id, "zxv.storage.response");
    ASSERT(ok, "add emitted schema should succeed");

    ev_domain_t *dom = ev_seq_get_domain(&seq, (uint32_t)id);
    ASSERT(dom->num_accepted_schemas == 1, "should have 1 accepted schema");
    ASSERT(dom->num_emitted_schemas == 1, "should have 1 emitted schema");
    PASS();
}

/* ===== Test 8: Enqueue/dequeue ===== */
static void test_enqueue_dequeue(void) {
    TEST("enqueue_dequeue");
    static ev_sequencer_t seq;
    ev_seq_init(&seq, "test");
    int32_t id = ev_seq_create_domain(&seq, "worker", 100, 100, EV_CONSISTENCY_LOCAL);

    /* Enqueue an event */
    ev_envelope_t env;
    ev_node_id_t node = seq.node;
    ev_envelope_init(&env, &node, 0, "zxv.work.request", 1);
    ev_envelope_set_payload(&env, (const uint8_t *)"work1", 5);

    int32_t ret = ev_seq_enqueue(&seq, (uint32_t)id, &env);
    ASSERT(ret == 0, "enqueue should succeed");
    ASSERT(seq.total_events_enqueued == 1, "total enqueued should be 1");

    ev_domain_t *dom = ev_seq_get_domain(&seq, (uint32_t)id);
    ASSERT(dom->queue_count == 1, "queue count should be 1");

    /* The envelope should have been assigned a sequence number */
    ASSERT(env.event_id.local_sequence == 1, "assigned sequence should be 1");

    /* Dequeue */
    ev_envelope_t out;
    ret = ev_seq_dequeue(&seq, (uint32_t)id, &out);
    ASSERT(ret == 0, "dequeue should succeed");
    ASSERT(out.event_id.local_sequence == 1, "dequeued sequence mismatch");
    ASSERT(strcmp(out.schema, "zxv.work.request") == 0, "dequeued schema mismatch");
    ASSERT(dom->queue_count == 0, "queue should be empty after dequeue");
    PASS();
}

/* ===== Test 9: Causal ordering ===== */
static void test_causal_ordering(void) {
    TEST("causal_ordering");
    static ev_sequencer_t seq;
    ev_seq_init(&seq, "test");
    int32_t id = ev_seq_create_domain(&seq, "causal_test", 100, 100, EV_CONSISTENCY_CAUSAL);

    /* Create parent event */
    ev_envelope_t parent_env;
    ev_node_id_t node = seq.node;
    ev_envelope_init(&parent_env, &node, 0, "zxv.parent", 1);
    ev_seq_enqueue(&seq, (uint32_t)id, &parent_env);

    /* Create child event with causal parent */
    ev_envelope_t child_env;
    ev_envelope_init(&child_env, &node, 0, "zxv.child", 1);
    ev_event_id_t parent_id = parent_env.event_id;
    bool ok = ev_envelope_add_causal_parent(&child_env, &parent_id);
    ASSERT(ok, "add causal parent should succeed");
    ASSERT(child_env.num_causal_parents == 1, "should have 1 causal parent");

    ev_seq_enqueue(&seq, (uint32_t)id, &child_env);

    /* Dequeue both and verify causal parent is preserved */
    ev_envelope_t out1, out2;
    ev_seq_dequeue(&seq, (uint32_t)id, &out1);
    ev_seq_dequeue(&seq, (uint32_t)id, &out2);

    ASSERT(out1.event_id.local_sequence < out2.event_id.local_sequence, "parent should come first");
    ASSERT(out2.num_causal_parents == 1, "child should have 1 causal parent");
    ASSERT(out2.causal_parents[0].local_sequence == out1.event_id.local_sequence, "causal parent ID mismatch");
    PASS();
}

/* ===== Test 10: Dispatch with budget enforcement ===== */
static void test_dispatch_budget(void) {
    TEST("dispatch_budget");
    static ev_sequencer_t seq;
    ev_seq_init(&seq, "test");
    int32_t id = ev_seq_create_domain(&seq, "budgeted", 5, 100, EV_CONSISTENCY_LOCAL);
    ev_domain_t *dom = ev_seq_get_domain(&seq, (uint32_t)id);

    /* Enqueue 10 events but budget is 5 */
    ev_node_id_t node = seq.node;
    for (int i = 0; i < 10; i++) {
        ev_envelope_t env;
        ev_envelope_init(&env, &node, 0, "zxv.work", 1);
        ev_seq_enqueue(&seq, (uint32_t)id, &env);
    }
    ASSERT(dom->queue_count == 10, "should have 10 events queued");

    /* Dispatch — should process 5 (budget) and leave 5 */
    int32_t dispatched = ev_seq_dispatch(&seq);
    ASSERT(dispatched == (int32_t)id, "should dispatch our domain");
    ASSERT(dom->total_events_processed == 5, "should have processed 5 events");
    ASSERT(dom->queue_count == 5, "should have 5 events remaining");
    ASSERT(dom->state == EV_DOMAIN_IDLE, "should be IDLE after budget exhaustion");
    ASSERT(dom->total_over_budget == 1, "over_budget should be 1");

    /* Replenish and dispatch again */
    ev_seq_replenish(&seq);
    ASSERT(dom->state == EV_DOMAIN_READY, "should be READY after replenish");
    ASSERT(dom->events_remaining == 5, "budget should be replenished to 5");

    ev_seq_dispatch(&seq);
    ASSERT(dom->total_events_processed == 10, "should have processed all 10");
    ASSERT(dom->queue_count == 0, "queue should be empty");
    PASS();
}

/* ===== Test 11: Self-audit queue overflow detection ===== */
static void test_audit_queue_overflow(void) {
    TEST("audit_queue_overflow");
    static ev_sequencer_t seq;
    ev_seq_init(&seq, "test");
    ev_audit_t audit;
    ev_audit_init(&audit);
    int32_t id = ev_seq_create_domain(&seq, "overflow_test", 100, 100, EV_CONSISTENCY_LOCAL);
    ev_domain_t *dom = ev_seq_get_domain(&seq, (uint32_t)id);

    /* Fill queue to max */
    ev_node_id_t node = seq.node;
    for (int i = 0; i < EV_MAX_QUEUE_DEPTH; i++) {
        ev_envelope_t env;
        ev_envelope_init(&env, &node, 0, "zxv.test", 1);
        ev_seq_enqueue(&seq, (uint32_t)id, &env);
    }
    ASSERT(dom->queue_count == EV_MAX_QUEUE_DEPTH, "queue should be full");

    /* Audit should return WARN (queue full but not overflow) */
    ev_audit_result_t result = ev_audit_check_domain(&seq, &audit, dom);
    ASSERT(result >= EV_AUDIT_WARN, "full queue should at least WARN");

    /* Try to enqueue one more — should fail */
    ev_envelope_t env;
    ev_envelope_init(&env, &node, 0, "zxv.test", 1);
    int32_t ret = ev_seq_enqueue(&seq, (uint32_t)id, &env);
    ASSERT(ret == -1, "enqueue to full queue should fail");
    ASSERT(seq.total_events_dropped == 1, "should have 1 dropped event");
    PASS();
}

/* ===== Test 12: Self-audit fault rate detection ===== */
static void test_audit_fault_rate(void) {
    TEST("audit_fault_rate");
    static ev_sequencer_t seq;
    ev_seq_init(&seq, "test");
    ev_audit_t audit;
    ev_audit_init(&audit);
    int32_t id = ev_seq_create_domain(&seq, "faulty", 100, 100, EV_CONSISTENCY_LOCAL);
    ev_domain_t *dom = ev_seq_get_domain(&seq, (uint32_t)id);

    /* Simulate faults */
    dom->consecutive_faults = 3;
    ev_audit_result_t result = ev_audit_check_domain(&seq, &audit, dom);
    ASSERT(result == EV_AUDIT_WARN, "3 faults should WARN");

    dom->consecutive_faults = 5;
    result = ev_audit_check_domain(&seq, &audit, dom);
    ASSERT(result == EV_AUDIT_FAIL, "5 faults should FAIL");

    dom->consecutive_faults = 10;
    result = ev_audit_check_domain(&seq, &audit, dom);
    ASSERT(result == EV_AUDIT_QUARANTINE, "10 faults should QUARANTINE");
    PASS();
}

/* ===== Test 13: Self-healing suspend ===== */
static void test_healing_suspend(void) {
    TEST("healing_suspend");
    static ev_sequencer_t seq;
    ev_seq_init(&seq, "test");
    ev_healing_t healing;
    ev_healing_init(&healing);
    int32_t id = ev_seq_create_domain(&seq, "suspect", 100, 100, EV_CONSISTENCY_LOCAL);
    ev_domain_t *dom = ev_seq_get_domain(&seq, (uint32_t)id);

    /* 3 consecutive faults + WARN -> SUSPEND */
    dom->consecutive_faults = 3;
    ev_heal_action_t action = ev_healing_apply(&seq, &healing, dom, EV_AUDIT_WARN);
    ASSERT(action == EV_HEAL_SUSPEND, "should suspend on 3 faults + WARN");
    ASSERT(dom->state == EV_DOMAIN_BLOCKED, "domain should be BLOCKED");
    ASSERT(healing.total_suspensions == 1, "suspension count should be 1");
    PASS();
}

/* ===== Test 14: Self-healing quarantine ===== */
static void test_healing_quarantine(void) {
    TEST("healing_quarantine");
    static ev_sequencer_t seq;
    ev_seq_init(&seq, "test");
    ev_healing_t healing;
    ev_healing_init(&healing);
    int32_t id = ev_seq_create_domain(&seq, "dangerous", 100, 100, EV_CONSISTENCY_LOCAL);
    ev_domain_t *dom = ev_seq_get_domain(&seq, (uint32_t)id);

    /* 5 consecutive faults + FAIL -> QUARANTINE */
    dom->consecutive_faults = 5;
    ev_heal_action_t action = ev_healing_apply(&seq, &healing, dom, EV_AUDIT_FAIL);
    ASSERT(action == EV_HEAL_QUARANTINE, "should quarantine on 5 faults + FAIL");
    ASSERT(dom->state == EV_DOMAIN_QUARANTINED, "domain should be QUARANTINED");
    ASSERT(dom->total_quarantines == 1, "quarantine count should be 1");
    ASSERT(seq.total_domains_quarantined == 1, "sequencer quarantine count should be 1");
    PASS();
}

/* ===== Test 15: Self-healing terminate ===== */
static void test_healing_terminate(void) {
    TEST("healing_terminate");
    static ev_sequencer_t seq;
    ev_seq_init(&seq, "test");
    ev_healing_t healing;
    ev_healing_init(&healing);
    int32_t id = ev_seq_create_domain(&seq, "doomed", 100, 100, EV_CONSISTENCY_LOCAL);
    ev_domain_t *dom = ev_seq_get_domain(&seq, (uint32_t)id);

    /* 10 consecutive faults + QUARANTINE -> TERMINATE */
    dom->consecutive_faults = 10;
    ev_heal_action_t action = ev_healing_apply(&seq, &healing, dom, EV_AUDIT_QUARANTINE);
    ASSERT(action == EV_HEAL_TERMINATE, "should terminate on 10 faults + QUARANTINE");
    ASSERT(dom->state == EV_DOMAIN_TERMINATED, "domain should be TERMINATED");
    ASSERT(healing.total_terminations == 1, "termination count should be 1");
    PASS();
}

/* ===== Test 16: Self-healing recovery ===== */
static void test_healing_recovery(void) {
    TEST("healing_recovery");
    static ev_sequencer_t seq;
    ev_seq_init(&seq, "test");
    ev_healing_t healing;
    ev_healing_init(&healing);
    int32_t id = ev_seq_create_domain(&seq, "recovering", 100, 100, EV_CONSISTENCY_LOCAL);
    ev_domain_t *dom = ev_seq_get_domain(&seq, (uint32_t)id);

    /* Quarantine the domain */
    dom->consecutive_faults = 5;
    ev_healing_apply(&seq, &healing, dom, EV_AUDIT_FAIL);
    ASSERT(dom->state == EV_DOMAIN_QUARANTINED, "should be quarantined");

    /* Advance sequence past quarantine period */
    uint32_t quarantine_until = dom->quarantine_until_sequence;
    seq.next_sequence = quarantine_until + 1;

    /* Check recovery */
    bool recovered = ev_healing_check_recovery(&seq, &healing, dom);
    ASSERT(recovered, "should recover after quarantine expires");
    ASSERT(dom->state == EV_DOMAIN_READY, "should be READY after recovery");
    ASSERT(dom->consecutive_faults == 0, "faults should be reset");
    ASSERT(dom->queue_count == 0, "queue should be cleared");
    ASSERT(healing.total_auto_recoveries == 1, "recovery count should be 1");
    PASS();
}

/* ===== Test 17: Integrated audit-heal cycle ===== */
static void test_integrated_audit_heal(void) {
    TEST("integrated_audit_heal");
    static ev_sequencer_t seq;
    ev_seq_init(&seq, "test");
    ev_audit_t audit;
    ev_audit_init(&audit);
    ev_healing_t healing;
    ev_healing_init(&healing);

    int32_t id = ev_seq_create_domain(&seq, "cyclic", 100, 100, EV_CONSISTENCY_LOCAL);
    ev_domain_t *dom = ev_seq_get_domain(&seq, (uint32_t)id);

    /* Normal operation — audit should pass */
    ev_heal_action_t action = ev_self_audit_heal_domain(&seq, &audit, &healing, dom);
    ASSERT(action == EV_HEAL_NONE, "healthy domain should need no healing");
    ASSERT(dom->state == EV_DOMAIN_READY, "should remain READY");

    /* Simulate fault storm */
    dom->consecutive_faults = 5;
    action = ev_self_audit_heal_domain(&seq, &audit, &healing, dom);
    ASSERT(action == EV_HEAL_QUARANTINE, "should quarantine on fault storm");
    ASSERT(dom->state == EV_DOMAIN_QUARANTINED, "should be quarantined");

    /* Advance past quarantine and run audit-heal again — should recover */
    seq.next_sequence = dom->quarantine_until_sequence + 1;
    action = ev_self_audit_heal_domain(&seq, &audit, &healing, dom);
    ASSERT(action == EV_HEAL_RESTART, "should recover after quarantine expires");
    ASSERT(dom->state == EV_DOMAIN_READY, "should be READY after recovery");
    PASS();
}

/* ===== Test 18: System-wide audit-heal ===== */
static void test_system_audit_heal(void) {
    TEST("system_audit_heal");
    static ev_sequencer_t seq;
    ev_seq_init(&seq, "test");
    ev_audit_t audit;
    ev_audit_init(&audit);
    ev_healing_t healing;
    ev_healing_init(&healing);

    /* Create multiple domains */
    int32_t id1 = ev_seq_create_domain(&seq, "healthy", 100, 100, EV_CONSISTENCY_LOCAL);
    int32_t id2 = ev_seq_create_domain(&seq, "degrading", 100, 100, EV_CONSISTENCY_LOCAL);
    int32_t id3 = ev_seq_create_domain(&seq, "broken", 100, 100, EV_CONSISTENCY_LOCAL);

    /* Make domain 2 degrade and domain 3 break */
    ev_domain_t *d2 = ev_seq_get_domain(&seq, (uint32_t)id2);
    ev_domain_t *d3 = ev_seq_get_domain(&seq, (uint32_t)id3);
    d2->consecutive_faults = 3;
    d3->consecutive_faults = 10;

    ev_heal_action_t worst = ev_self_audit_heal_system(&seq, &audit, &healing);
    ASSERT(worst == EV_HEAL_TERMINATE, "worst action should be TERMINATE for d3");
    ASSERT(d2->state == EV_DOMAIN_BLOCKED, "d2 should be suspended");
    ASSERT(d3->state == EV_DOMAIN_TERMINATED, "d3 should be terminated");

    /* Healthy domain should be unaffected */
    ev_domain_t *d1 = ev_seq_get_domain(&seq, (uint32_t)id1);
    ASSERT(d1->state == EV_DOMAIN_READY, "healthy domain should remain READY");
    PASS();
}

/* ===== Test 19: Quarantined domain rejects events ===== */
static void test_quarantine_rejects_events(void) {
    TEST("quarantine_rejects_events");
    static ev_sequencer_t seq;
    ev_seq_init(&seq, "test");
    ev_healing_t healing;
    ev_healing_init(&healing);
    int32_t id = ev_seq_create_domain(&seq, "isolated", 100, 100, EV_CONSISTENCY_LOCAL);
    ev_domain_t *dom = ev_seq_get_domain(&seq, (uint32_t)id);

    /* Quarantine the domain */
    dom->consecutive_faults = 5;
    ev_healing_apply(&seq, &healing, dom, EV_AUDIT_FAIL);
    ASSERT(dom->state == EV_DOMAIN_QUARANTINED, "should be quarantined");

    /* Try to enqueue — should fail */
    ev_envelope_t env;
    ev_node_id_t node = seq.node;
    ev_envelope_init(&env, &node, 0, "zxv.test", 1);
    int32_t ret = ev_seq_enqueue(&seq, (uint32_t)id, &env);
    ASSERT(ret == -1, "enqueue to quarantined domain should fail");
    ASSERT(seq.total_events_dropped == 1, "event should be dropped");
    PASS();
}

/* ===== Test 20: Schema filtering ===== */
static void test_schema_filtering(void) {
    TEST("schema_filtering");
    static ev_sequencer_t seq;
    ev_seq_init(&seq, "test");
    int32_t id = ev_seq_create_domain(&seq, "filtered", 100, 100, EV_CONSISTENCY_LOCAL);
    ev_domain_add_accepted_schema(&seq, (uint32_t)id, "zxv.storage");

    ev_node_id_t node = seq.node;

    /* Matching schema should succeed */
    ev_envelope_t env1;
    ev_envelope_init(&env1, &node, 0, "zxv.storage.read", 1);
    int32_t ret = ev_seq_enqueue(&seq, (uint32_t)id, &env1);
    ASSERT(ret == 0, "matching schema should enqueue");

    /* Non-matching schema should fail */
    ev_envelope_t env2;
    ev_envelope_init(&env2, &node, 0, "zxv.network.send", 1);
    ret = ev_seq_enqueue(&seq, (uint32_t)id, &env2);
    ASSERT(ret == -1, "non-matching schema should be rejected");
    PASS();
}

/* ===== Test 21: Multiple domains priority dispatch ===== */
static void test_multi_domain_dispatch(void) {
    TEST("multi_domain_dispatch");
    static ev_sequencer_t seq;
    ev_seq_init(&seq, "test");

    int32_t id1 = ev_seq_create_domain(&seq, "low_queue", 100, 100, EV_CONSISTENCY_LOCAL);
    int32_t id2 = ev_seq_create_domain(&seq, "high_queue", 100, 100, EV_CONSISTENCY_LOCAL);

    ev_node_id_t node = seq.node;

    /* Give domain 1 fewer events */
    for (int i = 0; i < 2; i++) {
        ev_envelope_t env;
        ev_envelope_init(&env, &node, 0, "zxv.test", 1);
        ev_seq_enqueue(&seq, (uint32_t)id1, &env);
    }

    /* Give domain 2 more events — should be dispatched first (higher score) */
    for (int i = 0; i < 10; i++) {
        ev_envelope_t env;
        ev_envelope_init(&env, &node, 0, "zxv.test", 1);
        ev_seq_enqueue(&seq, (uint32_t)id2, &env);
    }

    int32_t dispatched = ev_seq_dispatch(&seq);
    ASSERT(dispatched == id2, "should dispatch domain with more pending events first");

    ev_domain_t *d2 = ev_seq_get_domain(&seq, (uint32_t)id2);
    ASSERT(d2->total_events_processed == 10, "d2 should have processed 10 events");
    ASSERT(d2->queue_count == 0, "d2 queue should be empty");
    PASS();
}

/* ===== Test 22: Envelope CRC tamper detection ===== */
static void test_crc_tamper_detection(void) {
    TEST("crc_tamper_detection");
    ev_envelope_t env;
    ev_node_id_t node = {0};
    copy_str_h(node.id, "test", EV_NODE_ID_LEN);
    ev_envelope_init(&env, &node, 42, "zxv.critical", 1);
    ev_envelope_set_payload(&env, (const uint8_t *)"sensitive", 9);
    ev_envelope_compute_crc(&env);

    /* Verify original passes */
    ASSERT(ev_envelope_verify_crc(&env), "original should verify");

    /* Tamper with priority */
    ev_envelope_t tampered = env;
    tampered.priority = EV_PRIORITY_CRITICAL;
    ASSERT(!ev_envelope_verify_crc(&tampered), "tampered priority should fail CRC");

    /* Tamper with sender */
    tampered = env;
    tampered.sender_domain[0] = 'X';
    ASSERT(!ev_envelope_verify_crc(&tampered), "tampered sender should fail CRC");

    /* Tamper with delivery */
    tampered = env;
    tampered.delivery = EV_DELIVERY_TRANSACTIONAL;
    ASSERT(!ev_envelope_verify_crc(&tampered), "tampered delivery should fail CRC");
    PASS();
}

/* ===== Test runner ===== */
int main(void) {
    printf("\n=== ZXV Event-Space Infrastructure Tests ===\n\n");

    test_envelope_init();
    test_envelope_payload();
    test_envelope_crc();
    test_envelope_validate();
    test_sequencer_init();
    test_domain_capabilities();
    test_domain_schemas();
    test_enqueue_dequeue();
    test_causal_ordering();
    test_dispatch_budget();
    test_audit_queue_overflow();
    test_audit_fault_rate();
    test_healing_suspend();
    test_healing_quarantine();
    test_healing_terminate();
    test_healing_recovery();
    test_integrated_audit_heal();
    test_system_audit_heal();
    test_quarantine_rejects_events();
    test_schema_filtering();
    test_multi_domain_dispatch();
    test_crc_tamper_detection();

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);

    if (tests_failed > 0) {
        printf("*** FAILURES DETECTED ***\n");
        return 1;
    }
    printf("ALL TESTS PASSED\n");
    return 0;
}
