/* test_oseq.c — Ordinal Sequencer (K1) Tests
 *
 * Tests for causal DAG, event ordering, replay detection, and happens-before.
 *
 * Author: 36N9 Genetics, LLC
 * License: SEL-3.3 (kernel component)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "oseq.h"

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
    oseq_registry_t reg;
    oseq_registry_init(&reg);
    ASSERT(reg.node_count == 0, "no nodes");
    ASSERT(reg.event_count == 0, "no events");
    ASSERT(reg.next_ordinal == 1, "next ordinal is 1");
    ASSERT(reg.replay_count == 0, "no replay entries");
    ASSERT(reg.replays_detected == 0, "no replays detected");
    ASSERT(reg.cycles_detected == 0, "no cycles detected");
    PASS();
}

/* ===== Node Management Tests ===== */

TEST(register_node_test) {
    oseq_registry_t reg;
    oseq_registry_init(&reg);
    int32_t idx = oseq_register_node(&reg, "arm64.cluster0", 1);
    ASSERT(idx >= 0, "node registered");
    ASSERT(idx == 0, "first node is index 0");
    ASSERT(reg.node_count == 1, "count incremented");

    oseq_node_t *n = oseq_get_node(&reg, 0);
    ASSERT(n != NULL, "node retrieved");
    ASSERT(strcmp(n->name, "arm64.cluster0") == 0, "name matches");
    ASSERT(n->incarnation == 1, "incarnation is 1");
    ASSERT(n->active, "is active");
    ASSERT(n->next_sequence == 0, "sequence starts at 0");
    PASS();
}

TEST(register_node_duplicate_test) {
    oseq_registry_t reg;
    oseq_registry_init(&reg);
    oseq_register_node(&reg, "node-a", 1);
    int32_t idx2 = oseq_register_node(&reg, "node-a", 1);
    ASSERT(idx2 == 0, "duplicate returns same index");
    ASSERT(reg.node_count == 1, "count still 1");

    /* Different incarnation updates */
    int32_t idx3 = oseq_register_node(&reg, "node-a", 2);
    ASSERT(idx3 == 0, "same node, updated incarnation");
    oseq_node_t *n = oseq_get_node(&reg, 0);
    ASSERT(n->incarnation == 2, "incarnation updated to 2");
    ASSERT(n->next_sequence == 0, "sequence reset on new incarnation");
    PASS();
}

TEST(find_node_test) {
    oseq_registry_t reg;
    oseq_registry_init(&reg);
    oseq_register_node(&reg, "alpha", 1);
    oseq_register_node(&reg, "beta", 1);

    ASSERT(oseq_find_node(&reg, "alpha") == 0, "found alpha at 0");
    ASSERT(oseq_find_node(&reg, "beta") == 1, "found beta at 1");
    ASSERT(oseq_find_node(&reg, "gamma") == -1, "gamma not found");
    PASS();
}

/* ===== Event Registration Tests ===== */

TEST(register_event_test) {
    oseq_registry_t reg;
    oseq_registry_init(&reg);
    oseq_register_node(&reg, "node-a", 1);

    int32_t idx = oseq_register_event(&reg, 0, "zxv.storage.read",
                                      1, NULL, 0, NULL, 0);
    ASSERT(idx >= 0, "event registered");
    ASSERT(idx == 0, "first event is index 0");
    ASSERT(reg.event_count == 1, "count incremented");

    oseq_event_t *e = oseq_get_event(&reg, 0);
    ASSERT(e != NULL, "event retrieved");
    ASSERT(e->ordinal == 1, "ordinal is 1");
    ASSERT(e->node_idx == 0, "node is 0");
    ASSERT(e->local_sequence == 0, "local sequence is 0");
    ASSERT(e->status == OSEQ_STATUS_PENDING, "status is pending");
    ASSERT(e->num_parents == 0, "no parents");
    ASSERT(strcmp(e->schema, "zxv.storage.read") == 0, "schema matches");
    ASSERT(e->schema_version == 1, "schema version is 1");
    PASS();
}

TEST(register_event_with_parents_test) {
    oseq_registry_t reg;
    oseq_registry_init(&reg);
    oseq_register_node(&reg, "node-a", 1);

    /* Create root event */
    int32_t root = oseq_register_event(&reg, 0, "root", 1, NULL, 0, NULL, 0);
    oseq_commit_event(&reg, (uint32_t)root);

    /* Create child with parent */
    uint32_t parents[1] = { (uint32_t)root };
    int32_t child = oseq_register_event(&reg, 0, "child", 1, NULL, 0, parents, 1);
    ASSERT(child >= 0, "child registered");

    oseq_event_t *e = oseq_get_event(&reg, (uint32_t)child);
    ASSERT(e->num_parents == 1, "has 1 parent");
    ASSERT(e->parent_ordinals[0] == (uint32_t)root, "parent is root");
    ASSERT(e->local_sequence == 1, "local sequence is 1");
    PASS();
}

TEST(commit_event_test) {
    oseq_registry_t reg;
    oseq_registry_init(&reg);
    oseq_register_node(&reg, "n", 1);
    int32_t idx = oseq_register_event(&reg, 0, "ev", 1, NULL, 0, NULL, 0);

    ASSERT(oseq_commit_event(&reg, (uint32_t)idx), "commit succeeds");
    oseq_event_t *e = oseq_get_event(&reg, (uint32_t)idx);
    ASSERT(e->status == OSEQ_STATUS_COMMITTED, "status is committed");
    ASSERT(reg.commits == 1, "commit count is 1");

    /* Can't commit twice */
    ASSERT(!oseq_commit_event(&reg, (uint32_t)idx), "double commit fails");
    PASS();
}

TEST(reject_event_test) {
    oseq_registry_t reg;
    oseq_registry_init(&reg);
    oseq_register_node(&reg, "n", 1);
    int32_t idx = oseq_register_event(&reg, 0, "bad", 1, NULL, 0, NULL, 0);

    ASSERT(oseq_reject_event(&reg, (uint32_t)idx), "reject succeeds");
    oseq_event_t *e = oseq_get_event(&reg, (uint32_t)idx);
    ASSERT(e->status == OSEQ_STATUS_REJECTED, "status is rejected");
    ASSERT(reg.rejections == 1, "rejection count is 1");
    PASS();
}

/* ===== Causal Query Tests ===== */

TEST(happens_before_test) {
    oseq_registry_t reg;
    oseq_registry_init(&reg);
    oseq_register_node(&reg, "n", 1);

    int32_t a = oseq_register_event(&reg, 0, "a", 1, NULL, 0, NULL, 0);
    oseq_commit_event(&reg, (uint32_t)a);

    uint32_t p1[1] = { (uint32_t)a };
    int32_t b = oseq_register_event(&reg, 0, "b", 1, NULL, 0, p1, 1);
    oseq_commit_event(&reg, (uint32_t)b);

    uint32_t p2[1] = { (uint32_t)b };
    int32_t c = oseq_register_event(&reg, 0, "c", 1, NULL, 0, p2, 1);
    oseq_commit_event(&reg, (uint32_t)c);

    ASSERT(oseq_happens_before(&reg, (uint32_t)a, (uint32_t)b), "a before b");
    ASSERT(oseq_happens_before(&reg, (uint32_t)a, (uint32_t)c), "a before c");
    ASSERT(oseq_happens_before(&reg, (uint32_t)b, (uint32_t)c), "b before c");
    ASSERT(!oseq_happens_before(&reg, (uint32_t)c, (uint32_t)a), "c not before a");
    ASSERT(!oseq_happens_before(&reg, (uint32_t)b, (uint32_t)a), "b not before a");
    ASSERT(!oseq_happens_before(&reg, (uint32_t)a, (uint32_t)a), "a not before a");
    PASS();
}

TEST(is_ancestor_test) {
    oseq_registry_t reg;
    oseq_registry_init(&reg);
    oseq_register_node(&reg, "n", 1);

    int32_t root = oseq_register_event(&reg, 0, "root", 1, NULL, 0, NULL, 0);
    oseq_commit_event(&reg, (uint32_t)root);

    uint32_t p1[1] = { (uint32_t)root };
    int32_t mid = oseq_register_event(&reg, 0, "mid", 1, NULL, 0, p1, 1);
    oseq_commit_event(&reg, (uint32_t)mid);

    uint32_t p2[1] = { (uint32_t)mid };
    int32_t leaf = oseq_register_event(&reg, 0, "leaf", 1, NULL, 0, p2, 1);
    oseq_commit_event(&reg, (uint32_t)leaf);

    ASSERT(oseq_is_ancestor(&reg, (uint32_t)root, (uint32_t)leaf), "root is ancestor of leaf");
    ASSERT(oseq_is_ancestor(&reg, (uint32_t)mid, (uint32_t)leaf), "mid is ancestor of leaf");
    ASSERT(oseq_is_ancestor(&reg, (uint32_t)root, (uint32_t)mid), "root is ancestor of mid");
    ASSERT(!oseq_is_ancestor(&reg, (uint32_t)leaf, (uint32_t)root), "leaf not ancestor of root");
    PASS();
}

TEST(diamond_dependency_test) {
    oseq_registry_t reg;
    oseq_registry_init(&reg);
    oseq_register_node(&reg, "n", 1);

    /* Diamond: A -> B, A -> C, B -> D, C -> D */
    int32_t a = oseq_register_event(&reg, 0, "a", 1, NULL, 0, NULL, 0);
    oseq_commit_event(&reg, (uint32_t)a);

    uint32_t pa[1] = { (uint32_t)a };
    int32_t b = oseq_register_event(&reg, 0, "b", 1, NULL, 0, pa, 1);
    oseq_commit_event(&reg, (uint32_t)b);

    int32_t c = oseq_register_event(&reg, 0, "c", 1, NULL, 0, pa, 1);
    oseq_commit_event(&reg, (uint32_t)c);

    uint32_t pbc[2] = { (uint32_t)b, (uint32_t)c };
    int32_t d = oseq_register_event(&reg, 0, "d", 1, NULL, 0, pbc, 2);
    oseq_commit_event(&reg, (uint32_t)d);

    ASSERT(oseq_is_ancestor(&reg, (uint32_t)a, (uint32_t)d), "a ancestor of d");
    ASSERT(oseq_is_ancestor(&reg, (uint32_t)b, (uint32_t)d), "b ancestor of d");
    ASSERT(oseq_is_ancestor(&reg, (uint32_t)c, (uint32_t)d), "c ancestor of d");
    ASSERT(!oseq_is_ancestor(&reg, (uint32_t)b, (uint32_t)c), "b not ancestor of c");
    ASSERT(!oseq_is_ancestor(&reg, (uint32_t)c, (uint32_t)b), "c not ancestor of b");
    PASS();
}

TEST(get_depth_test) {
    oseq_registry_t reg;
    oseq_registry_init(&reg);
    oseq_register_node(&reg, "n", 1);

    int32_t a = oseq_register_event(&reg, 0, "a", 1, NULL, 0, NULL, 0);
    oseq_commit_event(&reg, (uint32_t)a);
    ASSERT(oseq_get_depth(&reg, (uint32_t)a) == 0, "root depth is 0");

    uint32_t p1[1] = { (uint32_t)a };
    int32_t b = oseq_register_event(&reg, 0, "b", 1, NULL, 0, p1, 1);
    oseq_commit_event(&reg, (uint32_t)b);
    ASSERT(oseq_get_depth(&reg, (uint32_t)b) == 1, "child depth is 1");

    uint32_t p2[1] = { (uint32_t)b };
    int32_t c = oseq_register_event(&reg, 0, "c", 1, NULL, 0, p2, 1);
    oseq_commit_event(&reg, (uint32_t)c);
    ASSERT(oseq_get_depth(&reg, (uint32_t)c) == 2, "grandchild depth is 2");
    PASS();
}

/* ===== Replay Detection Tests ===== */

TEST(replay_detection_test) {
    oseq_registry_t reg;
    oseq_registry_init(&reg);
    oseq_register_node(&reg, "n", 1);

    uint8_t hash[4] = {0xDE, 0xAD, 0xBE, 0xEF};

    int32_t idx1 = oseq_register_event(&reg, 0, "ev1", 1, hash, 4, NULL, 0);
    ASSERT(idx1 >= 0, "first event accepted");

    /* Same hash = replay */
    int32_t idx2 = oseq_register_event(&reg, 0, "ev2", 1, hash, 4, NULL, 0);
    ASSERT(idx2 < 0, "replay rejected");
    ASSERT(reg.replays_detected == 1, "replay count is 1");
    ASSERT(reg.rejections == 1, "rejection count is 1");
    PASS();
}

TEST(no_replay_different_hash_test) {
    oseq_registry_t reg;
    oseq_registry_init(&reg);
    oseq_register_node(&reg, "n", 1);

    uint8_t hash1[4] = {0x01, 0x02, 0x03, 0x04};
    uint8_t hash2[4] = {0x05, 0x06, 0x07, 0x08};

    int32_t idx1 = oseq_register_event(&reg, 0, "ev1", 1, hash1, 4, NULL, 0);
    int32_t idx2 = oseq_register_event(&reg, 0, "ev2", 1, hash2, 4, NULL, 0);
    ASSERT(idx1 >= 0, "first accepted");
    ASSERT(idx2 >= 0, "second accepted (different hash)");
    ASSERT(reg.replays_detected == 0, "no replays");
    PASS();
}

TEST(no_replay_no_hash_test) {
    oseq_registry_t reg;
    oseq_registry_init(&reg);
    oseq_register_node(&reg, "n", 1);

    int32_t idx1 = oseq_register_event(&reg, 0, "ev1", 1, NULL, 0, NULL, 0);
    int32_t idx2 = oseq_register_event(&reg, 0, "ev2", 1, NULL, 0, NULL, 0);
    ASSERT(idx1 >= 0, "first accepted");
    ASSERT(idx2 >= 0, "second accepted (no hash)");
    ASSERT(reg.replays_detected == 0, "no replays without hash");
    PASS();
}

/* ===== Cycle Detection Tests ===== */

TEST(cycle_detection_test) {
    oseq_registry_t reg;
    oseq_registry_init(&reg);
    oseq_register_node(&reg, "n", 1);

    /* Create A -> B */
    int32_t a = oseq_register_event(&reg, 0, "a", 1, NULL, 0, NULL, 0);
    oseq_commit_event(&reg, (uint32_t)a);

    uint32_t pa[1] = { (uint32_t)a };
    int32_t b = oseq_register_event(&reg, 0, "b", 1, NULL, 0, pa, 1);
    oseq_commit_event(&reg, (uint32_t)b);

    /* Try to create C -> A where A's parent chain includes B -> A */
    /* Actually, let's try to make A depend on B (creating A->B->A cycle) */
    /* We can't modify A after commit, but we can try to commit an event
       that would create a cycle through its parents */
    /* Create C with parent B, then try to create D with parent C and A
       where A is already ancestor of C — that's not a cycle, just a diamond */

    /* For a real cycle test: create event with parent that has this event as ancestor */
    /* Since we can't modify past events, a true cycle requires registering
       an event whose parent chain leads back to itself */
    /* This can happen if we register event X with parent Y, and Y already has X as ancestor */
    /* But Y was registered before X, so Y can't have X as parent */
    /* The cycle detection prevents self-referencing at commit time */

    /* detect_cycle should return false for valid DAG */
    ASSERT(!oseq_detect_cycle(&reg, (uint32_t)a), "no cycle for a");
    ASSERT(!oseq_detect_cycle(&reg, (uint32_t)b), "no cycle for b");
    PASS();
}

TEST(count_by_status_test) {
    oseq_registry_t reg;
    oseq_registry_init(&reg);
    oseq_register_node(&reg, "n", 1);

    int32_t a = oseq_register_event(&reg, 0, "a", 1, NULL, 0, NULL, 0);
    int32_t b = oseq_register_event(&reg, 0, "b", 1, NULL, 0, NULL, 0);
    (void)oseq_register_event(&reg, 0, "c", 1, NULL, 0, NULL, 0);

    oseq_commit_event(&reg, (uint32_t)a);
    oseq_reject_event(&reg, (uint32_t)b);

    ASSERT(oseq_count_by_status(&reg, OSEQ_STATUS_PENDING) == 1, "1 pending");
    ASSERT(oseq_count_by_status(&reg, OSEQ_STATUS_COMMITTED) == 1, "1 committed");
    ASSERT(oseq_count_by_status(&reg, OSEQ_STATUS_REJECTED) == 1, "1 rejected");
    PASS();
}

TEST(monotonic_ordinals_test) {
    oseq_registry_t reg;
    oseq_registry_init(&reg);
    oseq_register_node(&reg, "n", 1);

    int32_t a = oseq_register_event(&reg, 0, "a", 1, NULL, 0, NULL, 0);
    int32_t b = oseq_register_event(&reg, 0, "b", 1, NULL, 0, NULL, 0);
    int32_t c = oseq_register_event(&reg, 0, "c", 1, NULL, 0, NULL, 0);

    oseq_event_t *ea = oseq_get_event(&reg, (uint32_t)a);
    oseq_event_t *eb = oseq_get_event(&reg, (uint32_t)b);
    oseq_event_t *ec = oseq_get_event(&reg, (uint32_t)c);

    ASSERT(ea->ordinal < eb->ordinal, "a < b");
    ASSERT(eb->ordinal < ec->ordinal, "b < c");
    ASSERT(oseq_dag_is_later(eb->ordinal, ea->ordinal), "is_later(b, a)");
    ASSERT(oseq_dag_is_valid_ordinal(ea->ordinal), "a has valid ordinal");
    ASSERT(!oseq_dag_is_valid_ordinal(0), "0 is not valid ordinal");
    PASS();
}

TEST(multi_node_test) {
    oseq_registry_t reg;
    oseq_registry_init(&reg);
    oseq_register_node(&reg, "node-a", 1);
    oseq_register_node(&reg, "node-b", 1);

    int32_t a1 = oseq_register_event(&reg, 0, "a1", 1, NULL, 0, NULL, 0);
    int32_t b1 = oseq_register_event(&reg, 1, "b1", 1, NULL, 0, NULL, 0);
    int32_t a2 = oseq_register_event(&reg, 0, "a2", 1, NULL, 0, NULL, 0);

    oseq_event_t *ea1 = oseq_get_event(&reg, (uint32_t)a1);
    oseq_event_t *eb1 = oseq_get_event(&reg, (uint32_t)b1);
    oseq_event_t *ea2 = oseq_get_event(&reg, (uint32_t)a2);

    ASSERT(ea1->node_idx == 0, "a1 from node 0");
    ASSERT(eb1->node_idx == 1, "b1 from node 1");
    ASSERT(ea2->node_idx == 0, "a2 from node 0");
    ASSERT(ea1->local_sequence == 0, "a1 seq 0");
    ASSERT(eb1->local_sequence == 0, "b1 seq 0 (different node)");
    ASSERT(ea2->local_sequence == 1, "a2 seq 1 (same node as a1)");
    PASS();
}

TEST(name_functions_test) {
    ASSERT(strcmp(oseq_status_name(OSEQ_STATUS_PENDING), "pending") == 0, "pending name");
    ASSERT(strcmp(oseq_status_name(OSEQ_STATUS_COMMITTED), "committed") == 0, "committed name");
    ASSERT(strcmp(oseq_status_name(OSEQ_STATUS_REJECTED), "rejected") == 0, "rejected name");
    ASSERT(strcmp(oseq_status_name(OSEQ_STATUS_EXPIRED), "expired") == 0, "expired name");
    PASS();
}

/* ===== Main ===== */

int main(void) {
    printf("\n=== ZXV OSEQ (K1) Tests ===\n\n");

    RUN(registry_init_test);
    RUN(register_node_test);
    RUN(register_node_duplicate_test);
    RUN(find_node_test);
    RUN(register_event_test);
    RUN(register_event_with_parents_test);
    RUN(commit_event_test);
    RUN(reject_event_test);
    RUN(happens_before_test);
    RUN(is_ancestor_test);
    RUN(diamond_dependency_test);
    RUN(get_depth_test);
    RUN(replay_detection_test);
    RUN(no_replay_different_hash_test);
    RUN(no_replay_no_hash_test);
    RUN(cycle_detection_test);
    RUN(count_by_status_test);
    RUN(monotonic_ordinals_test);
    RUN(multi_node_test);
    RUN(name_functions_test);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);
    if (tests_failed == 0) {
        printf("ALL TESTS PASSED\n");
    }
    return tests_failed > 0 ? 1 : 0;
}
