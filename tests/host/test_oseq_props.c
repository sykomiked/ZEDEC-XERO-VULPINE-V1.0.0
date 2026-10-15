/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_oseq_props.c — property tests for the causal-order DAG
 * (kernel/src/oseq/oseq.c).
 *
 * Random DAGs are built through oseq_register_event and compared with a
 * reference transitive closure kept here as bitsets: happens-before must be
 * exactly reachability through parents, irreflexive and antisymmetric, the
 * depth must be the longest parent path, and the replay window must reject
 * exactly the payload hashes among the last OSEQ_REPLAY_WINDOW recorded.
 * Graphs stay below 200 events, where oseq's traversal stack cannot fill
 * (see docs/AUDIT_FINDINGS_2026-10-15.md for what happens above that). */
#include <stdio.h>
#include <string.h>
#include "oseq.h"

static int failures = 0;
static unsigned long checks = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(cond)) {                                                                             \
            if (failures < 40) printf("  FAIL: %s (line %d)\n", msg, __LINE__);                    \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static uint64_t rs = 0xA0761D6478BD642FULL;
static uint32_t rnd(uint32_t n)
{
    rs ^= rs << 13;
    rs ^= rs >> 7;
    rs ^= rs << 17;
    return n ? (uint32_t)(rs % n) : 0;
}

#define MAXE 200u
static uint64_t anc[MAXE][(MAXE + 63) / 64]; /* anc[d] has bit a: a is an ancestor of d */
static uint32_t depth[MAXE];

static int is_anc(uint32_t a, uint32_t d) { return (int)((anc[d][a / 64] >> (a % 64)) & 1u); }

static void test_dag_random(void)
{
    static oseq_registry_t reg;
    for (int round = 0; round < 60; round++) {
        oseq_registry_init(&reg);
        CHECK(reg.next_ordinal == 1 && reg.event_count == 0 && reg.node_count == 0, "init");
        uint32_t nodes = 1 + rnd(5);
        for (uint32_t k = 0; k < nodes; k++) {
            char nm[16];
            snprintf(nm, sizeof nm, "node%u", k);
            CHECK(oseq_register_node(&reg, nm, 1) == (int32_t)k, "register node");
        }
        uint64_t seq[8] = {0};
        uint32_t n = round < 4 ? MAXE - rnd(10) : 10 + rnd(70);
        memset(anc, 0, sizeof anc);
        for (uint32_t i = 0; i < n; i++) {
            uint32_t parents[OSEQ_MAX_PARENTS], np = i ? rnd(OSEQ_MAX_PARENTS + 1) : 0;
            /* mostly near parents, sometimes far, so chains and fans both occur */
            for (uint32_t k = 0; k < np; k++)
                parents[k] = rnd(3) ? i - 1 - rnd(i < 6 ? i : 6) : rnd(i);
            uint8_t h[32];
            for (int k = 0; k < 32; k++) h[k] = (uint8_t)(i >> (k % 4 * 8));
            h[31] = (uint8_t)round;
            uint32_t node = rnd(nodes);
            int32_t idx = oseq_register_event(&reg, node, "zxv.test", (uint16_t)i, h, 32, parents,
                                              (uint8_t)np);
            CHECK(idx == (int32_t)i, "events are indexed in order");
            const oseq_event_t *e = oseq_get_event(&reg, i);
            CHECK(e->ordinal == (uint64_t)i + 1, "ordinals are 1, 2, 3, ... (0 is null)");
            CHECK(e->local_sequence == seq[node]++, "per-node sequence counts up");
            CHECK(e->status == OSEQ_STATUS_PENDING && e->num_parents == np, "pending, parents kept");
            CHECK(e->payload_hash_len == 32 && memcmp(e->payload_hash, h, 32) == 0, "hash bound");
            CHECK(strcmp(e->schema, "zxv.test") == 0 && e->schema_version == (uint16_t)i, "schema");
            depth[i] = 0;
            for (uint32_t k = 0; k < np; k++) {
                uint32_t p = parents[k];
                CHECK(e->parent_ordinals[k] == p, "parent kept");
                anc[i][p / 64] |= 1ull << (p % 64);
                for (uint32_t w = 0; w < (MAXE + 63) / 64; w++) anc[i][w] |= anc[p][w];
                if (depth[p] + 1 > depth[i]) depth[i] = depth[p] + 1;
            }
        }
        for (uint32_t a = 0; a < n; a++) {
            /* oseq_get_depth walks every path (exponential on a dense DAG,
             * see the audit), so it is checked on the first 24 events */
            if (a < 24) CHECK(oseq_get_depth(&reg, a) == depth[a], "depth = longest parent path");
            CHECK(!oseq_happens_before(&reg, a, a), "happens-before is irreflexive");
            for (uint32_t b = 0; b < n; b++) {
                if (a == b) continue;
                int hb = oseq_happens_before(&reg, a, b);
                CHECK(hb == is_anc(a, b), "happens-before = reachability");
                CHECK(oseq_is_ancestor(&reg, a, b) == hb, "is_ancestor = happens_before");
                if (hb) CHECK(!oseq_happens_before(&reg, b, a), "antisymmetric");
            }
        }
        /* commit and reject: only from pending, counted */
        uint32_t commits = 0, rejects = 0;
        for (uint32_t i = 0; i < n; i++) {
            CHECK(!oseq_detect_cycle(&reg, i), "parents precede children: no cycle");
            if (rnd(4) == 0) {
                CHECK(oseq_reject_event(&reg, i), "reject a pending event");
                rejects++;
                CHECK(!oseq_commit_event(&reg, i), "a rejected event cannot commit");
            } else {
                CHECK(oseq_commit_event(&reg, i), "commit a pending event");
                commits++;
                CHECK(!oseq_commit_event(&reg, i) && !oseq_reject_event(&reg, i),
                      "a committed event is final");
            }
        }
        CHECK(oseq_count_by_status(&reg, OSEQ_STATUS_COMMITTED) == commits && reg.commits == commits,
              "commits counted");
        CHECK(oseq_count_by_status(&reg, OSEQ_STATUS_REJECTED) == rejects && reg.rejections == rejects,
              "rejections counted");
        CHECK(oseq_count_by_status(&reg, OSEQ_STATUS_PENDING) == 0, "nothing left pending");
        CHECK(!oseq_happens_before(&reg, 0, n) && !oseq_happens_before(&reg, n, 0) &&
                  !oseq_is_ancestor(&reg, n, 0) && oseq_get_depth(&reg, n) == 0 &&
                  !oseq_detect_cycle(&reg, n) && !oseq_get_event(&reg, n),
              "indices past the end");
    }
}

static void test_cycles_and_replay(void)
{
    static oseq_registry_t reg;
    oseq_registry_init(&reg);
    oseq_register_node(&reg, "n", 1);
    uint32_t p0[1] = {0}, p1[1] = {1};
    oseq_register_event(&reg, 0, "s", 1, 0, 0, 0, 0);
    oseq_register_event(&reg, 0, "s", 1, 0, 0, p0, 1);
    oseq_register_event(&reg, 0, "s", 1, 0, 0, p1, 1);
    /* a cycle can only be made by editing the table; the commit check must
     * still catch it, directly and through a longer loop */
    reg.events[0].num_parents = 1;
    reg.events[0].parent_ordinals[0] = 2;
    CHECK(oseq_detect_cycle(&reg, 0) && oseq_detect_cycle(&reg, 1) && oseq_detect_cycle(&reg, 2),
          "a 3-loop is a cycle from every member");
    CHECK(!oseq_commit_event(&reg, 1), "a cycle cannot commit");
    CHECK(oseq_get_event(&reg, 1)->status == OSEQ_STATUS_REJECTED && reg.cycles_detected == 1 &&
              reg.rejections == 1,
          "it is rejected and counted");
    reg.events[0].parent_ordinals[0] = 0;
    CHECK(oseq_detect_cycle(&reg, 0), "a self-loop is a cycle");
    reg.events[0].num_parents = 0;
    CHECK(!oseq_detect_cycle(&reg, 2) && oseq_commit_event(&reg, 2), "repaired: commits");

    /* replay window: the last OSEQ_REPLAY_WINDOW hashes are refused */
    oseq_registry_init(&reg);
    oseq_register_node(&reg, "n", 1);
    for (uint32_t i = 0; i < 300; i++) {
        uint8_t h[8] = {(uint8_t)i, (uint8_t)(i >> 8), 7, 7, 7, 7, 7, 7};
        CHECK(oseq_register_event(&reg, 0, "s", 1, h, 8, 0, 0) == (int32_t)i, "fresh hash accepted");
        for (uint32_t back = 0; back <= i && back < 140; back++) {
            uint8_t g[8] = {(uint8_t)(i - back), (uint8_t)((i - back) >> 8), 7, 7, 7, 7, 7, 7};
            CHECK(oseq_check_replay(&reg, g, 8) == (back < OSEQ_REPLAY_WINDOW),
                  "replay = among the last 128");
            CHECK(!oseq_check_replay(&reg, g, 7), "a different length is a different hash");
        }
    }
    uint8_t again[8] = {299 & 0xFF, 1, 7, 7, 7, 7, 7, 7};
    uint32_t before = reg.event_count;
    CHECK(oseq_register_event(&reg, 0, "s", 1, again, 8, 0, 0) == -1, "a replayed event is refused");
    CHECK(reg.event_count == before && reg.replays_detected == 1 && reg.rejections == 1,
          "and counted, not stored");
    CHECK(!oseq_check_replay(&reg, 0, 8) && !oseq_check_replay(&reg, again, 0) &&
              !oseq_check_replay(0, again, 8),
          "replay check of nothing");
    uint32_t rc = reg.replay_count, rh = reg.replay_head;
    oseq_record_replay(&reg, again, 33);
    oseq_record_replay(&reg, again, 0);
    oseq_record_replay(&reg, 0, 8);
    oseq_record_replay(0, again, 8);
    CHECK(reg.replay_count == rc && reg.replay_head == rh, "bad records change nothing");
    CHECK(reg.replay_count == OSEQ_REPLAY_WINDOW && rh == 300 % OSEQ_REPLAY_WINDOW, "window full");
}

static void test_edges(void)
{
    static oseq_registry_t reg;
    oseq_registry_init(0);
    oseq_registry_init(&reg);
    CHECK(oseq_register_node(0, "a", 1) == -1 && oseq_register_node(&reg, 0, 1) == -1, "node NULL");
    CHECK(oseq_register_node(&reg, "alpha", 1) == 0, "alpha");
    CHECK(oseq_register_node(&reg, "alphabet", 1) == 1, "a longer name is another node");
    CHECK(oseq_register_node(&reg, "alph", 1) == 2, "a shorter name is another node");
    CHECK(oseq_find_node(&reg, "alpha") == 0 && oseq_find_node(&reg, "alphabet") == 1 &&
              oseq_find_node(&reg, "alph") == 2 && oseq_find_node(&reg, "al") == -1,
          "find exact names");
    CHECK(oseq_find_node(0, "a") == -1 && oseq_find_node(&reg, 0) == -1, "find NULL");
    uint32_t none = 0;
    oseq_register_event(&reg, 0, "s", 1, 0, 0, &none, 0);
    oseq_register_event(&reg, 0, "s", 1, 0, 0, &none, 0);
    CHECK(oseq_get_event(&reg, 1)->local_sequence == 1, "alpha at sequence 2");
    CHECK(oseq_register_node(&reg, "alpha", 1) == 0 && reg.nodes[0].next_sequence == 2,
          "same incarnation: sequence kept");
    CHECK(oseq_register_node(&reg, "alpha", 2) == 0 && reg.nodes[0].next_sequence == 0 &&
              reg.nodes[0].incarnation == 2,
          "a reboot (new incarnation) restarts the sequence");
    CHECK(oseq_get_node(&reg, 0) == &reg.nodes[0] && !oseq_get_node(&reg, 3) && !oseq_get_node(0, 0),
          "get node");
    char longname[60];
    memset(longname, 'q', sizeof longname - 1);
    longname[59] = 0;
    CHECK(oseq_register_node(&reg, longname, 1) == 3 && strlen(reg.nodes[3].name) == 31,
          "a long name is clipped to 31");
    CHECK(reg.nodes[3].active, "a new node is active");
    for (uint32_t i = 4; i < OSEQ_MAX_NODES; i++) {
        char nm[16];
        snprintf(nm, sizeof nm, "n%u", i);
        CHECK(oseq_register_node(&reg, nm, 1) == (int32_t)i, "fill nodes");
    }
    CHECK(oseq_register_node(&reg, "one-too-many", 1) == -1, "node table full");
    CHECK(oseq_register_node(&reg, "alpha", 3) == 0, "a known node still re-registers when full");

    uint32_t five[5] = {0, 0, 0, 0, 0};
    CHECK(oseq_register_event(0, 0, "s", 1, 0, 0, 0, 0) == -1, "event NULL");
    CHECK(oseq_register_event(&reg, OSEQ_MAX_NODES, "s", 1, 0, 0, 0, 0) == -1, "unknown node");
    CHECK(oseq_register_event(&reg, 0, "s", 1, 0, 0, five, 5) == -1, "five parents refused");
    CHECK(oseq_register_event(&reg, 0, 0, 1, 0, 0, five, 4) == 2 &&
              oseq_get_event(&reg, 2)->schema[0] == 0,
          "no schema, four parents");
    uint32_t bad[1] = {99};
    CHECK(oseq_register_event(&reg, 0, "s", 1, 0, 0, bad, 1) == 3, "a future parent index");
    CHECK(oseq_get_event(&reg, 3)->parent_ordinals[0] == 0, "is stored as 0 (see the audit)");
    CHECK(oseq_register_event(&reg, 0, "s", 1, 0, 0, 0, 2) == 4 &&
              oseq_get_event(&reg, 4)->parent_ordinals[1] == 0,
          "parents claimed, none given: stored as 0");
    CHECK(!oseq_commit_event(0, 0) && !oseq_reject_event(0, 0) && !oseq_commit_event(&reg, 99) &&
              !oseq_reject_event(&reg, 99),
          "commit/reject out of range");
    CHECK(oseq_count_by_status(0, OSEQ_STATUS_PENDING) == 0, "count NULL");
    CHECK(!oseq_happens_before(0, 0, 1) && !oseq_is_ancestor(0, 0, 1) && !oseq_detect_cycle(0, 0) &&
              oseq_get_depth(0, 0) == 0 && !oseq_get_event(0, 0),
          "queries NULL");
    for (uint32_t i = reg.event_count; i < OSEQ_MAX_EVENTS; i++)
        oseq_register_event(&reg, 0, "s", 1, 0, 0, 0, 0);
    CHECK(reg.event_count == OSEQ_MAX_EVENTS, "event table filled");
    CHECK(oseq_register_event(&reg, 0, "s", 1, 0, 0, 0, 0) == -1, "event table full");
    CHECK(oseq_dag_is_valid_ordinal(1) && !oseq_dag_is_valid_ordinal(0), "0 is the null ordinal");
    CHECK(oseq_dag_is_later(5, 4) && !oseq_dag_is_later(4, 4) && !oseq_dag_is_later(3, 4), "later");
    CHECK(strcmp(oseq_status_name(OSEQ_STATUS_PENDING), "pending") == 0 &&
              strcmp(oseq_status_name(OSEQ_STATUS_COMMITTED), "committed") == 0 &&
              strcmp(oseq_status_name(OSEQ_STATUS_REJECTED), "rejected") == 0 &&
              strcmp(oseq_status_name(OSEQ_STATUS_EXPIRED), "expired") == 0 &&
              strcmp(oseq_status_name((oseq_status_t)9), "unknown") == 0,
          "status names");
}

/* exact boundaries: the last valid index, the first invalid one, the widest
 * hash, and a search wider than the old 256-entry traversal stack */
static void test_boundaries(void)
{
    static oseq_registry_t reg;
    oseq_registry_init(&reg);
    CHECK(oseq_find_node(&reg, "") == -1, "an empty table finds no empty name");
    CHECK(oseq_register_node(&reg, "xa", 1) == 0 && oseq_register_node(&reg, "ya", 1) == 1 &&
              oseq_register_node(&reg, "xb", 1) == 2,
          "same length, first or last byte different: three nodes");
    CHECK(oseq_find_node(&reg, "ya") == 1 && oseq_find_node(&reg, "za") == -1 &&
              oseq_find_node(&reg, "xc") == -1,
          "find tells them apart");

    /* parent index exactly event_count is the event itself: refused as 0 */
    CHECK(oseq_register_event(&reg, 0, "s", 1, 0, 0, 0, 0) == 0, "root");
    uint32_t self[1] = {1};
    CHECK(oseq_register_event(&reg, 0, "s", 1, 0, 0, self, 1) == 1 &&
              oseq_get_event(&reg, 1)->parent_ordinals[0] == 0,
          "a parent equal to the new index is not stored");
    uint32_t n = reg.event_count;
    CHECK(!oseq_commit_event(&reg, n) && !oseq_reject_event(&reg, n) &&
              reg.events[n].status == OSEQ_STATUS_PENDING && reg.commits == 0 && reg.rejections == 0,
          "commit and reject at exactly event_count are refused and touch nothing");

    /* hashes: 32 bytes is the widest recorded; 1 byte is still a hash; one
     * differing last byte is a different hash; the copy stops at hash_len */
    uint8_t h33[33];
    for (uint32_t i = 0; i < 33; i++) h33[i] = (uint8_t)(0xA0 + i);
    CHECK(oseq_register_event(&reg, 0, "s", 1, h33, 32, 0, 0) == 2, "32-byte hash");
    CHECK(oseq_check_replay(&reg, h33, 32), "a 32-byte hash is recorded");
    CHECK(oseq_get_event(&reg, 2)->payload_hash_len == 32 &&
              memcmp(oseq_get_event(&reg, 2)->payload_hash, h33, 32) == 0,
          "and stored on the event");
    uint8_t h1[2] = {0x5A, 0x77};
    CHECK(oseq_register_event(&reg, 0, "s", 1, h1, 1, 0, 0) == 3 && oseq_check_replay(&reg, h1, 1),
          "a 1-byte hash is recorded");
    CHECK(oseq_get_event(&reg, 3)->payload_hash[1] == 0, "the event copy stops at hash_len");
    CHECK(reg.replay_hashes[(reg.replay_head + OSEQ_REPLAY_WINDOW - 1) % OSEQ_REPLAY_WINDOW][1] == 0,
          "the replay copy stops at hash_len");
    uint8_t g[32];
    memcpy(g, h33, 32);
    g[31] ^= 1;
    CHECK(!oseq_check_replay(&reg, g, 32), "the last byte counts");
    h33[0] ^= 0x40;
    CHECK(oseq_register_event(&reg, 0, "s", 1, h33, 33, 0, 0) == 4 &&
              oseq_get_event(&reg, 4)->payload_hash_len == 0,
          "a 33-byte hash is not stored");

    /* cycles that only show through a second parent, at the start event and
     * deeper in the search */
    oseq_registry_init(&reg);
    oseq_register_node(&reg, "n", 1);
    uint32_t two[2] = {0, 1};
    oseq_register_event(&reg, 0, "s", 1, 0, 0, 0, 0);
    oseq_register_event(&reg, 0, "s", 1, 0, 0, 0, 0);
    oseq_register_event(&reg, 0, "s", 1, 0, 0, two, 2);
    CHECK(!oseq_detect_cycle(&reg, 2), "acyclic");
    reg.events[1].num_parents = 1;
    reg.events[1].parent_ordinals[0] = 2;
    CHECK(oseq_detect_cycle(&reg, 2), "the loop through the second parent is found");
    CHECK(oseq_detect_cycle(&reg, 1), "and from the other member, through its second parent");

    /* a ladder (each event has parents i-1 .. i-4) pushes three new entries
     * per step of the search, so ~3n/4 sit on the stack at once: 500 events
     * is wider than the old 256-entry stack */
    oseq_registry_init(&reg);
    oseq_register_node(&reg, "n", 1);
    for (uint32_t i = 0; i < 500; i++) {
        uint32_t pr[4] = {i - 1, i - 2, i - 3, i - 4};
        uint8_t np = i < 4 ? (uint8_t)i : 4;
        CHECK(oseq_register_event(&reg, 0, "s", 1, 0, 0, pr, np) == (int32_t)i, "ladder");
    }
    CHECK(oseq_is_ancestor(&reg, 0, 499) && oseq_happens_before(&reg, 0, 499),
          "a wide search still finds the root");
    CHECK(!oseq_detect_cycle(&reg, 499) && oseq_commit_event(&reg, 499), "the ladder is acyclic");
    reg.events[0].num_parents = 1;
    reg.events[0].parent_ordinals[0] = 498;
    CHECK(oseq_detect_cycle(&reg, 498), "and a loop closed at the far end is found");
    CHECK(!oseq_commit_event(&reg, 498), "so it cannot commit");
}

int main(void)
{
    printf("test_oseq_props: causal DAG vs a reference closure\n");
    test_edges();
    test_boundaries();
    test_cycles_and_replay();
    test_dag_random();
    printf("%lu checks, %d failures\n", checks, failures);
    if (failures) {
        printf("[FAIL] test_oseq_props\n");
        return 1;
    }
    printf("[PASS] test_oseq_props\n");
    return 0;
}
