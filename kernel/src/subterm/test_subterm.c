/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/*
 * test_subterm.c — known-answer tests for SUBTERM.
 *
 * Anchors:
 *   (1) subterm_open puts a child under the right parent.
 *   (2) a synced group of n stays in LOCKSTEP: after k ticks EVERY member
 *       reports tick ordinal == k (asserted EQUAL, not merely advancing).
 *   (3) output to subterminal A is readable in A and NOT in B (isolation).
 *   (4) a tutorial runs its steps in exact scripted order, then ends.
 *   (5) an out-of-range subterminal id is refused (bounded, no overrun).
 */
#include <stdio.h>
#include <string.h>
#include "subterm.h"

static int failures = 0;
static int checks = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

int main(void)
{
    subterm_tree_t t;
    subterm_init(&t);
    CHECK(t.initialized, "init: tree initialized");
    CHECK(t.num_nodes == 1, "init: only the root exists");
    CHECK(subterm_parent_of(&t, SUBTERM_ROOT) == -1, "init: root has no parent");

    /* ---- (1) tree structure: child under the right parent ---- */
    int32_t a = subterm_open(&t, SUBTERM_ROOT, "alpha");
    int32_t b = subterm_open(&t, SUBTERM_ROOT, "bravo");
    int32_t c = subterm_open(&t, a, "charlie"); /* child of alpha */
    CHECK(a > 0 && b > 0 && c > 0, "open: three ids allocated");
    CHECK(a != b && b != c && a != c, "open: ids are distinct");
    CHECK(subterm_parent_of(&t, a) == SUBTERM_ROOT, "open: alpha under root");
    CHECK(subterm_parent_of(&t, b) == SUBTERM_ROOT, "open: bravo under root");
    CHECK(subterm_parent_of(&t, c) == a, "open: charlie under alpha (not root)");

    /* ---- (2) synced group LOCKSTEP ---- */
    const uint32_t N = 4;
    int32_t g = subterm_spawn_synced(&t, SUBTERM_ROOT, N);
    CHECK(g >= 0, "spawn_synced: group created");
    CHECK(subterm_group_count(&t, g) == N, "spawn_synced: group holds n members");

    /* all members start at ordinal 0 */
    for (uint32_t i = 0; i < N; i++) {
        int32_t id = subterm_group_member(&t, g, i);
        CHECK(id >= 0, "spawn_synced: member id valid");
        CHECK(subterm_tick_of(&t, id) == 0, "lockstep: member starts at tick 0");
    }

    /* after k ticks, EVERY member reports EXACTLY k (asserted equal) */
    for (uint32_t k = 1; k <= 5; k++) {
        subterm_tick(&t);
        uint64_t first = subterm_tick_of(&t, subterm_group_member(&t, g, 0));
        CHECK(first == (uint64_t) k, "lockstep: leader ordinal == k");
        for (uint32_t i = 0; i < N; i++) {
            int32_t id = subterm_group_member(&t, g, i);
            CHECK(subterm_tick_of(&t, id) == (uint64_t) k, "lockstep: member ordinal == k");
            CHECK(subterm_tick_of(&t, id) == first, "lockstep: all members EQUAL to leader");
        }
    }

    /* a second synced group has its own independent clock but same tick count */
    int32_t g2 = subterm_spawn_synced(&t, a, 2);
    CHECK(g2 >= 0 && g2 != g, "spawn_synced: second group distinct");
    CHECK(subterm_tick_of(&t, subterm_group_member(&t, g2, 0)) == 0,
          "lockstep: fresh group starts at 0 (independent clock)");
    subterm_tick(&t);
    /* g advanced to 6, g2 to 1 — each counts its own ticks-since-birth */
    CHECK(subterm_tick_of(&t, subterm_group_member(&t, g, 0)) == 6,
          "lockstep: first group now at 6");
    CHECK(subterm_tick_of(&t, subterm_group_member(&t, g2, 0)) == 1,
          "lockstep: second group now at 1");
    /* both members of g2 still equal each other */
    CHECK(subterm_tick_of(&t, subterm_group_member(&t, g2, 0)) ==
              subterm_tick_of(&t, subterm_group_member(&t, g2, 1)),
          "lockstep: g2 members equal");

    /* ---- (3) routing isolation ---- */
    const uint8_t msgA[] = "hello-A";
    int32_t wrote = subterm_write(&t, a, msgA, (uint32_t) sizeof(msgA));
    CHECK(wrote == (int32_t) sizeof(msgA), "write: bytes accepted by A");

    uint8_t bufA[64] = {0};
    uint8_t bufB[64] = {0};
    int32_t ra = subterm_read(&t, a, bufA, sizeof(bufA));
    int32_t rb = subterm_read(&t, b, bufB, sizeof(bufB));
    CHECK(ra == (int32_t) sizeof(msgA), "route: A holds what was written to A");
    CHECK(memcmp(bufA, msgA, sizeof(msgA)) == 0, "route: A content matches");
    CHECK(rb == 0, "route: B is empty (isolation — nothing leaked)");

    /* write to B, confirm A unchanged */
    const uint8_t msgB[] = "world-B";
    subterm_write(&t, b, msgB, (uint32_t) sizeof(msgB));
    uint8_t bufA2[64] = {0};
    subterm_read(&t, a, bufA2, sizeof(bufA2));
    CHECK(memcmp(bufA2, msgA, sizeof(msgA)) == 0, "route: A still only has A's data");

    /* ring is bounded — overfill by far and stay in bounds */
    for (int i = 0; i < 1000; i++) {
        uint8_t byte = (uint8_t) ('0' + (i % 10));
        subterm_write(&t, c, &byte, 1);
    }
    uint8_t big[SUBTERM_RING] = {0};
    int32_t rc = subterm_read(&t, c, big, sizeof(big));
    CHECK(rc == (int32_t) SUBTERM_RING, "route: ring is bounded to SUBTERM_RING");

    /* ---- (4) tutorial: scripted order, then ends ---- */
    static const subterm_lesson_t lesson = {
        .title = "first-portholes",
        .steps =
            {
                "Step 1: open a subterminal with `open`.",
                "Step 2: spawn a synced trio with `spawn 3`.",
                "Step 3: press tick — watch them march together.",
                "Step 4: you are done.",
            },
        .n_steps = 4,
    };
    const char *p0 = subterm_tutorial_begin(&t, &lesson);
    CHECK(p0 == lesson.steps[0], "tutorial: begin returns step 0");
    const char *p1 = subterm_tutorial_step(&t);
    CHECK(p1 == lesson.steps[1], "tutorial: step -> step 1");
    const char *p2 = subterm_tutorial_step(&t);
    CHECK(p2 == lesson.steps[2], "tutorial: step -> step 2");
    const char *p3 = subterm_tutorial_step(&t);
    CHECK(p3 == lesson.steps[3], "tutorial: step -> step 3");
    const char *pend = subterm_tutorial_step(&t);
    CHECK(pend == NULL, "tutorial: ends after last step (NULL)");
    CHECK(t.tut_active == false, "tutorial: inactive after ending");
    /* determinism: re-running yields the identical sequence */
    CHECK(subterm_tutorial_begin(&t, &lesson) == lesson.steps[0],
          "tutorial: deterministic replay begins identically");

    /* ---- (5) out-of-range id is refused ---- */
    CHECK(subterm_write(&t, -1, msgA, 4) == -1, "bounds: negative id refused");
    CHECK(subterm_write(&t, SUBTERM_MAX, msgA, 4) == -1, "bounds: id==MAX refused");
    CHECK(subterm_write(&t, SUBTERM_MAX + 100, msgA, 4) == -1, "bounds: huge id refused");
    CHECK(subterm_read(&t, -5, bufA, sizeof(bufA)) == -1, "bounds: read negative refused");
    CHECK(subterm_tick_of(&t, 9999) == 0, "bounds: tick_of out-of-range -> 0");
    CHECK(subterm_parent_of(&t, 9999) == -1, "bounds: parent_of out-of-range -> -1");
    /* an unused-but-in-range slot is also refused */
    CHECK(subterm_write(&t, SUBTERM_MAX - 1, msgA, 4) == -1,
          "bounds: unused in-range slot refused");

    printf("\n%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
