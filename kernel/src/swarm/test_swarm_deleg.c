/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_swarm_deleg.c — the delegator position and the delegation tree
 * (swarm_deleg.h D1-D5). Every expected value is worked by hand from the
 * rules in the header and the chain order F < N < B < T, not read back from
 * the code under test. */
#include <stdio.h>
#include "swarm_deleg.h"

static int failures = 0, checks = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(cond)) {                                                                             \
            printf("  FAIL: %s (line %d)\n", msg, __LINE__);                                       \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define T LPRES_STATE_TRUE
#define F LPRES_STATE_FALSE
#define B LPRES_STATE_BOTH
#define N LPRES_STATE_NEITHER

static void test_position(void)
{
    swd_handoff_t h;
    /* placement order: lead 10, reckoner 11, arbiter 12, then 13 */
    swd_cand_t c[4] = {{10, 3, 1, true}, {11, 7, 2, true}, {12, 5, 3, true}, {13, 9, 4, false}};

    /* 13 scores highest but is dormant: 11 is the best resident match, alone */
    CHECK(swd_position(c, 4, 10, &h) == SWD_OK, "position ok");
    CHECK(h.moved && h.from == 10 && h.to == 11, "moves to the best resident cell");
    CHECK(h.score_from == 3 && h.score_to == 7, "hand-off carries both scores");
    CHECK(h.tv == T, "a strict best is TRUE");

    /* the holder is the best: it keeps the position */
    CHECK(swd_position(c, 4, 11, &h) == SWD_OK && !h.moved && h.to == 11 && h.tv == T,
          "the best holder keeps it");

    /* tie 11/12 at 7, different lineages, holder 10 not in the tie:
     * first tied in placement order (11), claim BOTH */
    c[2].score = 7;
    CHECK(swd_position(c, 4, 10, &h) == SWD_OK && h.moved && h.to == 11 && h.tv == B,
          "a tie across lineages goes to the first placed, BOTH");
    /* same tie, holder 12 is in it: no churn, still BOTH */
    CHECK(swd_position(c, 4, 12, &h) == SWD_OK && !h.moved && h.to == 12 && h.tv == B,
          "a holder in the tie keeps it, BOTH");
    /* tie inside one lineage is one voice: TRUE */
    c[2].lineage = 2;
    CHECK(swd_position(c, 4, 10, &h) == SWD_OK && h.to == 11 && h.tv == T,
          "a tie inside one lineage is TRUE");

    /* nobody matches: the position stays, NEITHER */
    swd_cand_t z[2] = {{20, 0, 1, true}, {21, 5, 2, false}};
    CHECK(swd_position(z, 2, 20, &h) == SWD_OK && !h.moved && h.to == 20 && h.tv == N,
          "no match: stays, NEITHER");
    CHECK(swd_position(z, 0, SWD_NONE, &h) == SWD_OK && h.tv == N && h.to == SWD_NONE,
          "empty circle: NEITHER");
    CHECK(swd_position(NULL, 1, 0, &h) == SWD_EARG, "null candidates refused");
    CHECK(swd_position(c, 4, 0, NULL) == SWD_EARG, "null hand-off refused");
}

static void test_tree_and_seam(void)
{
    swd_tree_t t;
    uint32_t r, a, b, c, d;
    CHECK(swd_init(&t, 0, 100) == SWD_EARG, "depth 0 refused");
    CHECK(swd_init(&t, SWD_MAX_DEPTH + 1, 100) == SWD_EARG, "depth over the cap refused");
    CHECK(swd_init(&t, 2, 100) == SWD_OK, "init");
    CHECK(swd_open(&t, 0, 1, 1, SWD_CONJ, &r) == SWD_EARG, "child before the root refused");
    CHECK(swd_open(&t, SWD_NONE, 1, 10, SWD_CONJ, &r) == SWD_OK && r == 0, "root");
    CHECK(swd_open(&t, SWD_NONE, 1, 10, SWD_CONJ, &a) == SWD_EARG, "a second root refused");
    CHECK(swd_open(&t, r, 2, 30, SWD_CONJ, &a) == SWD_OK && a == 1, "child a");
    CHECK(swd_open(&t, r, 3, 50, SWD_CONJ, &b) == SWD_OK && b == 2, "child b");
    CHECK(t.budget == 10, "100 - 10 - 30 - 50 left unreserved");
    /* budget: 11 > 10 left, refused and recorded with nothing reserved */
    CHECK(swd_open(&t, r, 4, 11, SWD_CONJ, &c) == SWD_OK && c == 3, "over-budget child recorded");
    CHECK(t.node[c].state == SWD_REF_BUDGET && t.node[c].reserved == 0 && t.budget == 10,
          "over-budget child refused, budget untouched");
    CHECK(swd_open(&t, c, 4, 1, SWD_CONJ, &d) == SWD_EARG, "no child under a refused node");
    /* depth: a(1) -> d(2) ok, d -> depth 3 > 2 refused */
    CHECK(swd_open(&t, a, 5, 4, SWD_CONJ, &d) == SWD_OK && t.node[d].depth == 2, "grandchild");
    uint32_t e;
    CHECK(swd_open(&t, d, 6, 1, SWD_CONJ, &e) == SWD_OK && t.node[e].state == SWD_REF_DEPTH,
          "too deep refused");
    CHECK(t.budget == 6, "10 - 4");

    /* outward-only and inward-only calls */
    CHECK(swd_witness(&t, a, T) == SWD_EPHASE, "no witness on the way out");
    CHECK(swd_close(&t, a) == SWD_EPHASE, "no close on the way out");
    CHECK(swd_seal(&t) == SWD_EPHASE, "no seal on the way out");
    CHECK(swd_turn(&t) == SWD_OK, "turn");
    CHECK(swd_turn(&t) == SWD_EPHASE, "one turn only");
    CHECK(swd_open(&t, r, 7, 1, SWD_CONJ, &e) == SWD_EPHASE, "no open on the way in");

    /* spending */
    CHECK(swd_spend(&t, a, 31) == SWD_ESPEND, "spend over the reservation refused");
    CHECK(swd_spend(&t, a, 30) == SWD_OK && t.node[a].spent == 30, "spend the whole of a");
    CHECK(swd_spend(&t, a, 1) == SWD_ESPEND, "nothing left in a");
    CHECK(swd_spend(&t, b, 20) == SWD_OK, "b spends 20 of 50");
    CHECK(swd_spend(&t, c, 0) == SWD_EARG, "no spend on a refused node");
    CHECK(swd_witness(&t, a, (lpres_state_t) 4) == SWD_EARG, "bad state refused");

    /* leaves first: a cannot close while d runs */
    CHECK(swd_close(&t, a) == SWD_EOPEN, "parent waits for its child");
    CHECK(swd_seal(&t) == SWD_EOPEN, "no seal while nodes run");
    /* d: two witnesses T and F on one claim -> B (kjoin); its refused child e
     * conjoins to N, and kjoin(N, B) = B */
    CHECK(swd_witness(&t, d, T) == SWD_OK && swd_witness(&t, d, F) == SWD_OK, "witness d");
    CHECK(swd_close(&t, d) == SWD_OK && t.node[d].tv == B, "T with F is BOTH");
    CHECK(swd_close(&t, d) == SWD_EARG, "closed once");
    /* a: its only child d is B, no witness: B */
    CHECK(swd_close(&t, a) == SWD_OK && t.node[a].tv == B, "a carries its child's BOTH");
    /* b: leaf witnessed T */
    CHECK(swd_witness(&t, b, T) == SWD_OK && swd_close(&t, b) == SWD_OK && t.node[b].tv == T,
          "leaf b TRUE");
    /* root: {a=B, b=T, c refused=N} -> min = N, then a witness of the whole saying T:
     * kjoin(N, T) = T */
    CHECK(swd_witness(&t, r, T) == SWD_OK, "witness the whole");
    CHECK(swd_close(&t, r) == SWD_OK && t.node[r].tv == T, "witness of the whole over N");

    uint32_t ids[4];
    CHECK(swd_conflicts(&t, ids, 4) == 2 && ids[0] == a && ids[1] == d,
          "the BOTHs under a TRUE root are kept");
    CHECK(swd_conflicts(&t, ids, 1) == 2 && ids[0] == a, "count past a short buffer");

    /* seam: 6 never reserved + root 10 + a 0 + b 30 + d 4 = 50 back */
    CHECK(swd_seal(&t) == SWD_OK, "seal");
    CHECK(t.returned == 50 && t.budget == 0, "unspent tokens returned at the seam");
    CHECK(t.root_tv == T && t.phase == SWD_SEALED, "root fixed");
    CHECK(swd_seal(&t) == SWD_EPHASE && swd_witness(&t, r, F) == SWD_EPHASE,
          "nothing changes after the seam");
}

static void test_joins(void)
{
    /* CONJ root over T, B -> B (no witness); DISJ root over F, N -> N;
     * DISJ over F, B -> B; DISJ over F, T -> T; CONJ over T, F -> F */
    static const struct {
        swd_join_t j;
        lpres_state_t x, y, want;
    } k[] = {
        {SWD_CONJ, T, B, B}, {SWD_CONJ, T, F, F}, {SWD_CONJ, T, T, T},
        {SWD_CONJ, B, N, N}, {SWD_DISJ, F, N, N}, {SWD_DISJ, F, B, B},
        {SWD_DISJ, F, T, T}, {SWD_DISJ, B, N, B}, {SWD_DISJ, F, F, F},
    };
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++) {
        swd_tree_t t;
        uint32_t r, a, b;
        swd_init(&t, 1, 10);
        swd_open(&t, SWD_NONE, 0, 0, k[i].j, &r);
        swd_open(&t, r, 1, 0, SWD_CONJ, &a);
        swd_open(&t, r, 2, 0, SWD_CONJ, &b);
        swd_turn(&t);
        if (k[i].x != N) swd_witness(&t, a, k[i].x);
        if (k[i].y != N) swd_witness(&t, b, k[i].y);
        swd_close(&t, a);
        swd_close(&t, b);
        swd_close(&t, r);
        CHECK(t.node[r].tv == k[i].want, "join table row");
    }
    /* a witness of the whole saying F over children that conjoin to T: B */
    swd_tree_t t;
    uint32_t r, a;
    swd_init(&t, 1, 10);
    swd_open(&t, SWD_NONE, 0, 0, SWD_CONJ, &r);
    swd_open(&t, r, 1, 0, SWD_CONJ, &a);
    swd_turn(&t);
    swd_witness(&t, a, T);
    swd_close(&t, a);
    swd_witness(&t, r, F);
    swd_close(&t, r);
    CHECK(t.node[r].tv == B, "children T against a witness F: BOTH");
}

static void test_lost(void)
{
    /* fan-out: the fifth child has no slot, is lost, and drags a CONJ of
     * four TRUE children to NEITHER */
    swd_tree_t t;
    uint32_t r, k[5];
    swd_init(&t, 1, 100);
    swd_open(&t, SWD_NONE, 0, 0, SWD_CONJ, &r);
    for (int i = 0; i < 5; i++) CHECK(swd_open(&t, r, 1, 1, SWD_CONJ, &k[i]) == SWD_OK, "open");
    CHECK(k[4] == SWD_NONE && t.node[r].n_child == 4 && t.node[r].n_lost == 1,
          "the fifth sub-task is lost");
    CHECK(t.budget == 96, "a lost sub-task reserves nothing");

    swd_tree_t x;
    uint32_t xr;
    swd_init(&x, 1, 5);
    CHECK(swd_open(&x, SWD_NONE, 0, 5, SWD_CONJ, &xr) == SWD_OK && x.node[xr].state == SWD_RUN &&
              x.budget == 0,
          "a reservation of exactly the budget runs");
    swd_turn(&t);
    for (int i = 0; i < 4; i++) {
        swd_witness(&t, k[i], T);
        swd_close(&t, k[i]);
    }
    swd_close(&t, r);
    CHECK(t.node[r].tv == N, "four TRUE and one lost: NEITHER");
}

static void test_full(void)
{
    /* fill the table: root with 4 children, each a chain; count to 64 */
    swd_tree_t t;
    uint32_t r, id = 0, par[4];
    swd_init(&t, 8, 1000);
    swd_open(&t, SWD_NONE, 0, 0, SWD_DISJ, &r);
    for (int i = 0; i < 4; i++) swd_open(&t, r, 1, 0, SWD_CONJ, &par[i]);
    /* widen: every node gets up to 4 children, breadth first, until full */
    for (uint32_t p = 1; t.n < SWD_MAX_NODES; p++)
        for (int j = 0; j < 4 && t.n < SWD_MAX_NODES; j++) swd_open(&t, p, 2, 0, SWD_CONJ, &id);
    CHECK(t.n == SWD_MAX_NODES, "table full");
    CHECK(swd_open(&t, 1, 3, 0, SWD_CONJ, &id) == SWD_OK && id == SWD_NONE, "full: lost");
    CHECK(t.n == SWD_MAX_NODES && t.node[1].n_lost == 1, "lost under its parent");
}

static void test_tally(void)
{
    swd_tree_t t;
    uint32_t r, k[4], counts[4], pm = 0;
    swd_init(&t, 1, 100);
    swd_open(&t, SWD_NONE, 9, 0, SWD_CONJ, &r);
    /* cell 7 holds three sub-tasks: T, F, B; cell 8 holds one left NEITHER */
    swd_open(&t, r, 7, 0, SWD_CONJ, &k[0]);
    swd_open(&t, r, 7, 0, SWD_CONJ, &k[1]);
    swd_open(&t, r, 7, 0, SWD_CONJ, &k[2]);
    swd_open(&t, r, 8, 0, SWD_CONJ, &k[3]);
    swd_turn(&t);
    swd_witness(&t, k[0], T);
    swd_witness(&t, k[1], F);
    swd_witness(&t, k[2], T);
    swd_witness(&t, k[2], F);
    for (int i = 0; i < 4; i++) swd_close(&t, k[i]);
    swd_tally(&t, 7, counts);
    CHECK(counts[T] == 1 && counts[F] == 1 && counts[B] == 1 && counts[N] == 0, "tally of 7");
    CHECK(swd_reliability(counts, &pm) && pm == 333, "1 of 3 checked = 333 per mille");
    uint32_t ids[4];
    CHECK(swd_conflicts(&t, ids, 4) == 1 && ids[0] == k[2], "only BOTH is a conflict, not F or N");
    swd_tally(&t, 8, counts);
    CHECK(counts[N] == 1 && !swd_reliability(counts, &pm), "no checked work: no reliability");
    CHECK(pm == 333, "untouched on no reliability");
    swd_tally(&t, 9, counts);
    CHECK(counts[0] + counts[1] + counts[2] + counts[3] == 0, "a running node is not counted");

    uint32_t c2[4] = {0, 2, 1, 0};
    CHECK(swd_reliability(c2, &pm) && pm == 666, "2 of 3 = 666");
    uint32_t c3[4] = {5, 7, 0, 0};
    CHECK(swd_reliability(c3, &pm) && pm == 1000, "all TRUE = 1000, NEITHER not counted");
    uint32_t c4[4] = {0, 0, 0, 4};
    CHECK(swd_reliability(c4, &pm) && pm == 0, "all BOTH = 0");
    uint32_t c5[4] = {0, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    CHECK(swd_reliability(c5, &pm) && pm == 333, "large counts: floor(1000/3)");
    uint32_t c6[4] = {0, 999, 1, 0};
    CHECK(swd_reliability(c6, &pm) && pm == 999, "999 of 1000");
    uint32_t c7[4] = {0, 1, 1000, 0};
    CHECK(swd_reliability(c7, &pm) && pm == 0, "1 of 1001 floors to 0");
}

int main(void)
{
    test_position();
    test_tree_and_seam();
    test_joins();
    test_lost();
    test_full();
    test_tally();
    if (failures) {
        printf("test_swarm_deleg: %d of %d checks failed\n", failures, checks);
        return 1;
    }
    printf("test_swarm_deleg: %d checks passed\n", checks);
    return 0;
}
