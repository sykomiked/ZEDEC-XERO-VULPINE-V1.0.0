/* test_zxv_scale.c — the capacity oracle, host tests.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * Proves the scaling criteria:
 *   1. determinism: same hardware + same mode -> identical scale, always;
 *   2. the soil decides the depth: more RAM earns more rings, monotonically;
 *   3. headless / no-display machines earn the server plant (stack, no GUI)
 *      whatever their RAM;
 *   4. no-network machines stop at the swarm (no mesh);
 *   5. unknown fields default CONSERVATIVELY (never optimistic): unknown RAM
 *      earns the base tier only;
 *   6. safe mode is the reduced E8 base, fail-closed;
 *   7. the three mirrored states scale together from the same rung;
 *   8. zxv_scale_can_grow refuses the next ring beyond the budget.
 */
#include <stdio.h>
#include <string.h>

#include "zxv_scale.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                                         \
            return 1;                                                                              \
        }                                                                                          \
        checks++;                                                                                  \
    } while (0)

int main(void)
{
    zxv_hw_t tiny = {2u, 512u, true, false, true};
    zxv_hw_t desk = {8u, 8192u, true, true, true};
    zxv_hw_t srv = {32u, 16384u, false, true, true};
    zxv_hw_t unknown = {0u, 0u, false, false, false};
    zxv_scale_t a, b;

    /* ---- 1. determinism ---------------------------------------------------- */
    zxv_scale_compute(&desk, false, false, &a);
    zxv_scale_compute(&desk, false, false, &b);
    CHECK(a.rung == b.rung && a.tier == b.tier);
    CHECK(a.human_depth == b.human_depth);
    CHECK(a.swarm_depth == b.swarm_depth);
    CHECK(a.mesh_depth == b.mesh_depth);

    /* ---- 2. the soil decides the depth (monotonic in RAM) ------------------ */
    {
        zxv_scale_t t;
        zxv_hw_t h = {4u, 256u, true, true, true};
        zxv_scale_compute(&h, false, false, &t);
        CHECK(t.rung == 6u && t.tier == ZXV_TIER_CORE);

        h.mem_mib = 2048u; /* the stack tier          */
        zxv_scale_compute(&h, false, false, &t);
        CHECK(t.rung == 9u && t.tier == ZXV_TIER_STACK);

        h.mem_mib = 4096u; /* the swarm tier          */
        zxv_scale_compute(&h, false, false, &t);
        CHECK(t.rung == 11u && t.tier == ZXV_TIER_SWARM);

        h.mem_mib = 8192u; /* the full plant          */
        zxv_scale_compute(&h, false, false, &t);
        CHECK(t.rung == 12u && t.tier == ZXV_TIER_FULL);
    }

    /* ---- 3. headless / no display: the server plant, no GUI --------------- */
    {
        zxv_scale_t t;
        zxv_scale_compute(&srv, true, false, &t); /* server mode            */
        CHECK(t.human_depth == 0u);               /* no GUI                */
        CHECK(t.tier == ZXV_TIER_STACK);          /* stack, not full       */
        CHECK(t.rung == 9u);
        CHECK(t.swarm_depth >= 1u);
        /* the same machine with a display earns the full plant */
        zxv_hw_t srv_disp = {32u, 16384u, true, true, true};
        zxv_scale_compute(&srv_disp, false, false, &t);
        CHECK(t.rung == 12u && t.tier == ZXV_TIER_FULL);
        CHECK(t.human_depth >= 1u);
    }

    /* ---- 4. no network: stop at the swarm ---------------------------------- */
    {
        zxv_scale_t t;
        zxv_hw_t no_net = {8u, 8192u, true, false, true};
        zxv_scale_compute(&no_net, false, false, &t);
        CHECK(t.rung == 11u && t.tier == ZXV_TIER_SWARM);
        CHECK(t.mesh_depth == 0u); /* isolated               */
    }

    /* ---- 5. unknown fields: conservative, never optimistic ---------------- */
    {
        zxv_scale_t t;
        zxv_scale_compute(&unknown, false, false, &t);
        CHECK(t.rung == 6u && t.tier == ZXV_TIER_CORE); /* unknown RAM: base */
        CHECK(!t.fail_closed);                          /* known enough to run    */
    }

    /* ---- 6. safe mode: the reduced E8 base, fail-closed -------------------- */
    {
        zxv_scale_t t;
        zxv_scale_compute(&desk, false, true, &t);
        CHECK(t.rung == 6u && t.tier == ZXV_TIER_CORE);
        CHECK(t.fail_closed);
        CHECK(t.swarm_depth == 0u && t.mesh_depth == 0u);
    }

    /* ---- 7. the three mirrored states scale together ----------------------- */
    {
        zxv_scale_t t;
        zxv_scale_compute(&desk, false, false, &t);
        CHECK(t.human_depth > 0u && t.swarm_depth > 0u && t.mesh_depth > 0u);
        /* same rung drives all three: the full plant has all three deep. */
        CHECK(t.rung == 12u);
        CHECK(t.human_depth == 4u && t.swarm_depth == 3u && t.mesh_depth == 3u);
    }

    /* ---- 8. the can_grow gate ----------------------------------------------- */
    {
        zxv_scale_t t;
        zxv_scale_compute(&tiny, false, false, &t);
        /* tiny is at the core tier; the next ring is affordable on 512 MB. */
        CHECK(zxv_scale_can_grow(&t, 512u));
        /* an absurdly small machine cannot. */
        CHECK(!zxv_scale_can_grow(&t, 1u));
        /* the full plant cannot grow further. */
        zxv_scale_compute(&desk, false, false, &t);
        CHECK(!zxv_scale_can_grow(&t, 16384u));
    }

    /* ---- arena bytes match the earned rung ---------------------------------- */
    {
        zxv_scale_t t;
        zxv_scale_compute(&srv, true, false, &t);
        CHECK(zxv_scale_bytes(&t) == zxv_grow_bytes(t.rung));
        CHECK(zxv_scale_bytes(&t) > 0u);
    }

    /* ---- tier names ---------------------------------------------------------- */
    CHECK(strcmp(zxv_tier_name(ZXV_TIER_CORE), "core") == 0);
    CHECK(strcmp(zxv_tier_name(ZXV_TIER_FULL), "full") == 0);

    /* ---- 9. the whole stack scales as ONE vector ------------------------------
     * The principle: every aspect scales like a vector — the capacity oracle's
     * earned tier maps to a UI scale the shell accepts (500..4000 permille,
     * zxv_shell_set_ui_scale's documented bound), and the vector renderer
     * redraws the same ZXVG geometry at any size with full fidelity. The tier
     * -> permille mapping is deterministic: more hardware, larger vector. */
    {
        zxv_scale_t t;
        zxv_hw_t h;
        uint32_t permille;
        /* a small machine earns a compact vector; a big one a larger one. */
        h = (zxv_hw_t){2u, 512u, true, true, true};
        zxv_scale_compute(&h, false, false, &t);
        permille = 500u + t.tier * 500u;              /* tier 0 -> 500, tier 3 -> 2000  */
        CHECK(permille >= 500u && permille <= 4000u); /* the shell's bound */
        CHECK(t.tier == ZXV_TIER_CORE && permille == 500u);

        h = (zxv_hw_t){8u, 8192u, true, true, true};
        zxv_scale_compute(&h, false, false, &t);
        permille = 500u + t.tier * 500u;
        CHECK(t.tier == ZXV_TIER_FULL && permille == 2000u);
        CHECK(permille >= 500u && permille <= 4000u);

        /* a headless server is STILL a valid vector — just no GUI layer,
         * the same seed, a smaller redraw. */
        h = (zxv_hw_t){32u, 16384u, false, true, true};
        zxv_scale_compute(&h, true, false, &t);
        permille = 500u + t.tier * 500u;
        CHECK(t.human_depth == 0u);
        CHECK(permille >= 500u && permille <= 4000u);
    }

    printf("zxv_scale: %d checks passed\n", checks);
    return 0;
}