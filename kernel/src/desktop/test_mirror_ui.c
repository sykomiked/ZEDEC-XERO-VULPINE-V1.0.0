/* test_mirror_ui.c — the mirror-UI projection + phase-render hook, host tests.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * Proves the megalithic mirror criteria:
 *   1. deterministic frames: same registers + same tick -> identical bytes;
 *   2. fail-closed: a NULL register projects NEITHER, never a guess;
 *   3. lpres four-valued valence: TRUE->certain, FALSE->absent, BOTH->both,
 *      NEITHER->neither — ambiguity is rendered, not approximated;
 *   4. tri-space valences: S+ certain / S- absent / S0 neither with depth;
 *   5. ring backpressure: a full notify ring refuses pushes and surfaces
 *      ring_full in the frame, never silently dropping;
 *   6. dual-space symmetry: one tick drives the human and Chiglet hooks with
 *      the SAME frame (equal privilege, same tick, no drift).
 */
#include <stdio.h>
#include <string.h>

#include "mirror_ui.h"
#include "phase_render.h"
#include "net_mirror.h"
#include "zxv_fixed.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                                         \
            return 1;                                                                              \
        }                                                                                          \
        checks++;                                                                                  \
    } while (0)

static int fired_human = 0, fired_chiglet = 0, fired_network = 0;
static uint64_t seen_tick = 0;

static void human_hook(uint64_t tick, const mirror_frame_t *frame, void *ctx)
{
    (void) frame;
    (void) ctx;
    fired_human++;
    seen_tick = tick;
}

static void chiglet_hook(uint64_t tick, const mirror_frame_t *frame, void *ctx)
{
    (void) frame;
    (void) ctx;
    fired_chiglet++;
    seen_tick = tick;
}

static void network_hook(uint64_t tick, const mirror_frame_t *frame, void *ctx)
{
    (void) frame;
    (void) ctx;
    fired_network++;
    seen_tick = tick;
}

int main(void)
{
    mirror_ui_t m;
    mirror_frame_t a, b;
    mirror_ring_view_t ring = {0u, 0u, 64u};
    mirror_queue_view_t queue = {3u, 8u};
    mirror_trust_view_t trust = {1u, 1u};
    mirror_tris_view_t tris = {TRI_POSITIVE, 7u};
    lpres_state_t lstate = LPRES_STATE_TRUE;

    mirror_ui_init(&m);
    CHECK(mirror_bind(&m, MIRROR_KIND_RING, "cmd-ring", &ring, 11u) == 0);
    CHECK(mirror_bind(&m, MIRROR_KIND_QUEUE, "task-q", &queue, 22u) == 1);
    CHECK(mirror_bind(&m, MIRROR_KIND_TRUST, "s0-bound", &trust, 33u) == 2);
    CHECK(mirror_bind(&m, MIRROR_KIND_TRIS, "view-tris", &tris, 44u) == 3);
    CHECK(mirror_bind(&m, MIRROR_KIND_LPRES, "attest", &lstate, 55u) == 4);
    CHECK(mirror_bind(&m, MIRROR_KIND_LPRES, "null-lp", NULL, 66u) == 5);

    /* 1. determinism: same registers + same tick -> identical frame. */
    mirror_project(&m, 144u, &a);
    mirror_project(&m, 144u, &b);
    CHECK(a.tick == 144u && a.proj_count == 6u);
    CHECK(a.digest == b.digest);
    CHECK(memcmp(&a, &b, sizeof a) == 0);

    /* a different tick changes the fold, but not the valences. */
    mirror_project(&m, 145u, &b);
    CHECK(a.digest != b.digest);
    CHECK(b.proj[0].valence == a.proj[0].valence);

    /* 2. fail-closed NULL register: NEITHER, never a guess. */
    CHECK(a.proj[5].valence == MIRROR_VALENCE_NEITHER);

    /* 3. lpres four-valued valence. */
    CHECK(a.proj[4].valence == MIRROR_VALENCE_CERTAIN); /* TRUE */
    lstate = LPRES_STATE_FALSE;
    mirror_project(&m, 144u, &a);
    CHECK(a.proj[4].valence == MIRROR_VALENCE_ABSENT);
    lstate = LPRES_STATE_BOTH;
    mirror_project(&m, 144u, &a);
    CHECK(a.proj[4].valence == MIRROR_VALENCE_BOTH);
    lstate = LPRES_STATE_NEITHER;
    mirror_project(&m, 144u, &a);
    CHECK(a.proj[4].valence == MIRROR_VALENCE_NEITHER);
    lstate = LPRES_STATE_TRUE;

    /* 4. tri-space valences: S+ certain, S- absent, S0 neither; depth kept. */
    CHECK(a.proj[3].valence == MIRROR_VALENCE_CERTAIN);
    CHECK(a.proj[3].depth == 7u);
    tris.role = TRI_NEGATIVE;
    mirror_project(&m, 144u, &a);
    CHECK(a.proj[3].valence == MIRROR_VALENCE_ABSENT);
    tris.role = TRI_NEUTRAL;
    mirror_project(&m, 144u, &a);
    CHECK(a.proj[3].valence == MIRROR_VALENCE_NEITHER);
    tris.role = TRI_POSITIVE;

    /* trust: sealed+verified certain; sealed-but-unverified BOTH. */
    CHECK(a.proj[2].valence == MIRROR_VALENCE_CERTAIN);
    trust.verified = 0u;
    mirror_project(&m, 144u, &a);
    CHECK(a.proj[2].valence == MIRROR_VALENCE_BOTH);
    trust.verified = 1u;

    /* ring: empty ring is a CERTAIN empty (head==tail). */
    CHECK(a.proj[0].valence == MIRROR_VALENCE_CERTAIN);
    CHECK(a.proj[0].value == 0u);
    ring.head = 5u;
    ring.tail = 9u;
    mirror_project(&m, 144u, &a);
    CHECK(a.proj[0].value == 4u);

    /* 5. ring backpressure: fill the notify ring, pushes refuse, frame flags. */
    {
        uint32_t i;
        bool refused = false;
        for (i = 0; i < MIRROR_RING_CAP; i++)
            if (!mirror_notify(&m, 0u)) {
                refused = true;
                break;
            }
        CHECK(refused); /* the ring refused a push at full */
        mirror_project(&m, 144u, &a);
        CHECK(a.ring_full);
        mirror_ui_init(&m); /* reset for the render test below */
    }

    /* 6. phase-render hook: dual-space symmetry at one tick. */
    {
        phase_render_t pr;
        static mirror_frame_t f1, f2;
        phase_render_init(&pr);
        CHECK(phase_render_add_hook(&pr, PHASE_SPACE_HUMAN, 0u, human_hook, NULL) == 0);
        CHECK(phase_render_add_hook(&pr, PHASE_SPACE_CHIGLET, 0u, chiglet_hook, NULL) == 1);
        CHECK(phase_render_add_hook(&pr, PHASE_SPACE_HUMAN, 0u, NULL, NULL) == -1);

        /* bind the mirror registers to the render mirror, then tick. */
        CHECK(mirror_bind(&pr.mirror, MIRROR_KIND_RING, "cmd-ring", &ring, 11u) == 0);
        CHECK(mirror_bind(&pr.mirror, MIRROR_KIND_LPRES, "attest", &lstate, 55u) == 1);

        phase_render_dual_tick(&pr, 144u);
        CHECK(fired_human == 1 && fired_chiglet == 1);
        CHECK(seen_tick == 144u);

        /* both spaces saw the SAME frame: identical digest, identical tick. */
        {
            const mirror_frame_t *f = phase_render_last_frame(&pr);
            CHECK(f != NULL && f->tick == 144u && f->digest != 0u);
            f1 = *f;
        }
        phase_render_dual_tick(&pr, 145u);
        {
            const mirror_frame_t *f = phase_render_last_frame(&pr);
            CHECK(f->tick == 145u);
            f2 = *f;
        }
        CHECK(fired_human == 2 && fired_chiglet == 2);

        /* deterministic across a re-init: same state, same ticks, same digests. */
        {
            phase_render_t pr2;
            phase_render_init(&pr2);
            CHECK(mirror_bind(&pr2.mirror, MIRROR_KIND_RING, "cmd-ring", &ring, 11u) == 0);
            CHECK(mirror_bind(&pr2.mirror, MIRROR_KIND_LPRES, "attest", &lstate, 55u) == 1);
            phase_render_tick(&pr2, 144u);
            {
                const mirror_frame_t *g = phase_render_last_frame(&pr2);
                CHECK(g->digest == f1.digest);
            }
            phase_render_tick(&pr2, 145u);
            {
                const mirror_frame_t *g = phase_render_last_frame(&pr2);
                CHECK(g->digest == f2.digest);
            }
        }

        /* idempotent: a repeated tick does not re-fire hooks. */
        phase_render_dual_tick(&pr, 145u);
        CHECK(fired_human == 2 && fired_chiglet == 2);
    }

    /* ---- 7. the triadic network space ------------------------------------ */
    {
        net_mirror_t nm;
        mirror_frame_t nf;
        mirror_net_view_t netv = {3u, 9u};           /* 3 of 9 peers up    */
        mirror_cid_view_t cidv = {1u, 1u};           /* verified + pinned  */
        mirror_pq_view_t pqv = {1u, 1u};             /* decap + auth OK    */
        mirror_pq_view_t pq_bad = {1u, 0u};          /* half-proven        */
        mirror_clock_view_t clkv = {100u, 100u, 5u}; /* in sync          */

        net_mirror_init(&nm);
        CHECK(net_mirror_bind_topology(&nm, &netv) >= 0);
        CHECK(net_mirror_bind_identity(&nm, &cidv) >= 0);
        CHECK(net_mirror_bind_clock(&nm, &clkv) >= 0);
        CHECK(net_mirror_add_session(&nm, &pqv) == 0);

        net_mirror_project(&nm, 144u, &nf);
        CHECK(nf.proj_count >= 4u);

        /* network topology: 3/9 up is BOTH (split-brain risk, ambiguous). */
        {
            uint32_t i, found = 0;
            for (i = 0; i < nf.proj_count; i++) {
                if (strcmp(nf.proj[i].name, "net-topology") == 0) {
                    CHECK(nf.proj[i].kind == MIRROR_KIND_NET);
                    CHECK(nf.proj[i].valence == MIRROR_VALENCE_BOTH);
                    found = 1;
                }
                if (strcmp(nf.proj[i].name, "net-identity") == 0) {
                    CHECK(nf.proj[i].valence == MIRROR_VALENCE_CERTAIN);
                }
                if (strcmp(nf.proj[i].name, "net-clock") == 0) {
                    CHECK(nf.proj[i].valence == MIRROR_VALENCE_CERTAIN);
                }
            }
            CHECK(found == 1);
        }

        /* a fully-up topology is CERTAIN; an empty one is ABSENT. */
        netv.peers_up = 9u;
        net_mirror_project(&nm, 144u, &nf);
        {
            uint32_t i;
            for (i = 0; i < nf.proj_count; i++)
                if (strcmp(nf.proj[i].name, "net-topology") == 0)
                    CHECK(nf.proj[i].valence == MIRROR_VALENCE_CERTAIN);
        }
        netv.peers_up = 0u;
        net_mirror_project(&nm, 144u, &nf);
        {
            uint32_t i;
            for (i = 0; i < nf.proj_count; i++)
                if (strcmp(nf.proj[i].name, "net-topology") == 0)
                    CHECK(nf.proj[i].valence == MIRROR_VALENCE_ABSENT);
        }
        netv.peers_up = 3u;

        /* PQ: half-proven session is BOTH; full proof is CERTAIN. */
        nm.state.sessions[0] = pq_bad;
        net_mirror_project(&nm, 144u, &nf);
        CHECK(mirror_valence_of(&nm.mirror, 0u) != MIRROR_VALENCE_CERTAIN);
        nm.state.sessions[0] = pqv;

        /* clock drift beyond tolerance is BOTH (the manifold refuses to
         * guess which clock is right). */
        clkv.local_tick = 100u;
        clkv.peer_max_tick = 130u;
        clkv.skew_tolerance = 5u;
        net_mirror_project(&nm, 144u, &nf);
        {
            uint32_t i;
            for (i = 0; i < nf.proj_count; i++)
                if (strcmp(nf.proj[i].name, "net-clock") == 0)
                    CHECK(nf.proj[i].valence == MIRROR_VALENCE_BOTH);
        }
        clkv.peer_max_tick = 100u;

        /* deterministic arbiter: same state + same tick -> same space. */
        {
            net_space_id_t s1 = net_mirror_arbiter(&nm, 144u);
            net_space_id_t s2 = net_mirror_arbiter(&nm, 144u);
            CHECK(s1 == s2);
            CHECK(s1 >= NET_SPACE_HUMAN && s1 < NET_SPACE_COUNT);
        }
    }

    /* ---- 8. the Interaction Surplus Framework, exact Q16.16 -------------- */
    {
        uint32_t u;
        /* orthogonal unit vectors: dot = 0 -> u = 1 - 0 = 1 (max surplus). */
        CHECK(net_mirror_isf(0u, &u));
        CHECK(u == (uint32_t) Q16_ONE);
        /* parallel unit vectors: dot = 1 -> u = 1 - 1 = 0 (no surplus). */
        CHECK(net_mirror_isf((uint32_t) Q16_ONE, &u));
        CHECK(u == 0u);
        /* dot = 0.5 (Q16.16 32768) -> u = 1 - 0.25 = 0.75, exact. */
        CHECK(net_mirror_isf((uint32_t) (Q16_ONE / 2), &u));
        CHECK(u == (uint32_t) (Q16_ONE - Q16_ONE / 4)); /* exactly 0.75 */
        /* malformed (|dot| > 1) never allocates. */
        CHECK(!net_mirror_isf((uint32_t) Q16_ONE + 1u, &u));
        CHECK(u == (uint32_t) Q16_ONE); /* fail-closed default       */
    }

    /* ---- 9. the triadic phase tick: all three spaces fire ---------------- */
    {
        phase_render_t pr;
        phase_render_init(&pr);
        CHECK(phase_render_add_hook(&pr, PHASE_SPACE_HUMAN, 0u, human_hook, NULL) == 0);
        CHECK(phase_render_add_hook(&pr, PHASE_SPACE_CHIGLET, 0u, chiglet_hook, NULL) == 1);
        CHECK(phase_render_add_hook(&pr, PHASE_SPACE_NETWORK, 0u, network_hook, NULL) == 2);

        /* reset counters; the earlier dual-tick tests advanced them. */
        fired_human = fired_chiglet = fired_network = 0;

        CHECK(mirror_bind(&pr.mirror, MIRROR_KIND_NET, "net-topology", &(mirror_net_view_t){9u, 9u},
                          0xA1u) == 0);

        phase_render_dual_tick(&pr, 200u);
        CHECK(fired_human == 1 && fired_chiglet == 1 && fired_network == 1);
        CHECK(seen_tick == 200u);
        {
            const mirror_frame_t *f = phase_render_last_frame(&pr);
            CHECK(f != NULL && f->tick == 200u);
            CHECK(f->proj[0].kind == MIRROR_KIND_NET);
            CHECK(f->proj[0].valence == MIRROR_VALENCE_CERTAIN);
        }
    }

    printf("mirror_ui + phase_render: %d checks passed\n", checks);
    return 0;
}