/* test_vortex.c — the 3-6-9 vortex rotor, host tests.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * Proves the vortex criteria:
 *   1. the rotor steps cleanly through all six permutations in order and
 *      wraps (369- -> 396+ -> 639+ -> 693- -> 936- -> 963+ -> 369-);
 *   2. the polarity law holds in every vector: creation states have 9 as
 *      the sink, the completion state has 9 as the source with 3 and 6
 *      inverted to negative;
 *   3. paraconsistent adjudication: TRUE admits, FALSE vetoes, NEITHER and
 *      BOTH defer — never a guessed step;
 *   4. the doubling circuit is 1->2->4->8->7->5 and the halving circuit is
 *      its exact reverse, both anchored at the 0 core;
 *   5. the coverage hyperbola r*l >= 1.8 is exact Q16.16 integer arithmetic.
 */
#include <stdio.h>
#include <string.h>

#include "vortex.h"
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

int main(void)
{
    vortex_rotor_t r;

    /* ---- 1. the six permutation states, in order, wrapping -------------- */
    vortex_rotor_init(&r);
    CHECK(r.current == VORTEX_369_INIT);
    CHECK(strcmp(vortex_perm_name(VORTEX_369_INIT), "369-") == 0);
    CHECK(strcmp(vortex_perm_name(VORTEX_963_DONE), "963+") == 0);

    {
        static const vortex_perm_t want[6] = {
            VORTEX_396_BRIDGE, VORTEX_639_OPT,  VORTEX_693_CONV,
            VORTEX_936_MESH,   VORTEX_963_DONE, VORTEX_369_INIT,
        };
        uint64_t tick = 1u;
        uint32_t i;
        for (i = 0; i < 6u; i++) {
            CHECK(vortex_step(&r, tick++, LPRES_STATE_TRUE) == want[i]);
            CHECK(r.current == want[i]);
        }
        /* one more step wraps to the second vector. */
        CHECK(vortex_step(&r, tick, LPRES_STATE_TRUE) == VORTEX_396_BRIDGE);
        CHECK(r.steps == 7u);
    }

    /* idempotent on a repeated tick. */
    CHECK(vortex_step(&r, 100u, LPRES_STATE_TRUE) == VORTEX_639_OPT);
    CHECK(vortex_step(&r, 100u, LPRES_STATE_TRUE) == VORTEX_639_OPT);
    CHECK(r.steps == 8u);

    /* ---- 2. the polarity law -------------------------------------------- */
    {
        /* 369- Initiation: 3 leads +, 6 supports +, 9 is the sink (-). */
        vortex_rotor_t v = {VORTEX_369_INIT, 0u, 0u, 0u, 0u};
        CHECK(vortex_leader(&v) == VORTEX_SPACE_USER);
        CHECK(vortex_polarity(&v, VORTEX_SPACE_USER) == VORTEX_POL_POS);
        CHECK(vortex_polarity(&v, VORTEX_SPACE_CHIGLET) == VORTEX_POL_POS);
        CHECK(vortex_polarity(&v, VORTEX_SPACE_NETWORK) == VORTEX_POL_NEG);

        /* 963+ Completion: 9 is the source +, 3 and 6 invert to -. */
        v.current = VORTEX_963_DONE;
        CHECK(vortex_leader(&v) == VORTEX_SPACE_NETWORK);
        CHECK(vortex_polarity(&v, VORTEX_SPACE_NETWORK) == VORTEX_POL_POS);
        CHECK(vortex_polarity(&v, VORTEX_SPACE_USER) == VORTEX_POL_NEG);
        CHECK(vortex_polarity(&v, VORTEX_SPACE_CHIGLET) == VORTEX_POL_NEG);
        CHECK(vortex_is_completion(&v));

        /* 639+ AI Optimisation: 6 leads, 9 integrates +, 3 balances. */
        v.current = VORTEX_639_OPT;
        CHECK(vortex_leader(&v) == VORTEX_SPACE_CHIGLET);
        CHECK(vortex_polarity(&v, VORTEX_SPACE_NETWORK) == VORTEX_POL_POS);
        CHECK(vortex_perms[VORTEX_639_OPT].bridge == true);
    }

    /* ---- 3. paraconsistent adjudication ---------------------------------- */
    vortex_rotor_init(&r);
    r.current = VORTEX_396_BRIDGE;
    /* FALSE vetoes: the rotor holds, the veto counter increments. */
    CHECK(vortex_step(&r, 10u, LPRES_STATE_FALSE) == VORTEX_396_BRIDGE);
    CHECK(r.vetoed == 1u && r.deferred == 0u && r.steps == 0u);
    /* NEITHER and BOTH defer: hold, no step. */
    CHECK(vortex_step(&r, 11u, LPRES_STATE_NEITHER) == VORTEX_396_BRIDGE);
    CHECK(vortex_step(&r, 12u, LPRES_STATE_BOTH) == VORTEX_396_BRIDGE);
    CHECK(r.deferred == 2u && r.steps == 0u);
    /* TRUE admits. */
    CHECK(vortex_step(&r, 13u, LPRES_STATE_TRUE) == VORTEX_639_OPT);
    CHECK(r.steps == 1u);

    /* ---- 4. the doubling and halving circuits ---------------------------- */
    {
        static const uint32_t dbl[6] = {1u, 2u, 4u, 8u, 7u, 5u};
        uint32_t i;
        for (i = 0; i < 6u; i++) {
            CHECK(vortex_circuit_next(dbl[i], VORTEX_DIR_DOUBLE) == dbl[(i + 1u) % 6u]);
            CHECK(vortex_circuit_next(dbl[i], VORTEX_DIR_HALVE) == dbl[(i + 5u) % 6u]);
        }
        /* doubling then halving returns to the same register. */
        for (i = 0; i < 6u; i++) {
            uint32_t fwd = vortex_circuit_next(dbl[i], VORTEX_DIR_DOUBLE);
            CHECK(vortex_circuit_next(fwd, VORTEX_DIR_HALVE) == dbl[i]);
        }
        /* unknown register returns to the 0 core. */
        CHECK(vortex_circuit_next(3u, VORTEX_DIR_DOUBLE) == VORTEX_CORE_ZERO);
        CHECK(strcmp(vortex_reg_name(VORTEX_REG_INIT), "init") == 0);
        CHECK(strcmp(vortex_reg_name(VORTEX_REG_EQ), "eq") == 0);
        CHECK(strcmp(vortex_reg_name(VORTEX_CORE_ZERO), "core") == 0);
    }

    /* ---- 5. the coverage hyperbola, exact Q16.16 ------------------------- */
    {
        /* 1.0 * 1.0 = 1.0 < 1.8: below equilibrium. */
        CHECK(!vortex_equilibrium((uint32_t) Q16_ONE, (uint32_t) Q16_ONE));
        /* 1.5 * 1.5 = 2.25 >= 1.8: above. */
        CHECK(vortex_equilibrium((uint32_t) (Q16_CONST(3, 2)), (uint32_t) (Q16_CONST(3, 2))));
        /* 1.34 * 1.34 = 1.7956 < 1.8: below (boundary, exact). */
        CHECK(!vortex_equilibrium((uint32_t) (Q16_CONST(134, 100)),
                                  (uint32_t) (Q16_CONST(134, 100))));
        /* 1.35 * 1.35 = 1.8225 >= 1.8: above. */
        CHECK(
            vortex_equilibrium((uint32_t) (Q16_CONST(135, 100)), (uint32_t) (Q16_CONST(135, 100))));
    }

    printf("vortex: %d checks passed\n", checks);
    return 0;
}