/* test_vortex_harmonic.c — the Solfeggio + 555 harmonic layer, host tests.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * Proves the harmonic criteria:
 *   1. the 12 Solfeggio frequencies are exact integers and every vortex
 *      permutation state IS a Solfeggio number;
 *   2. dual-polarity 555: positive states accumulate (+555), negative states
 *      purge (-555), anchored to the VINO_ISO_DEBIT 555 rail;
 *   3. phase-lock: two spaces exchange state only when their Solfeggio
 *      residues agree — a modular residue, exact, drift-free;
 *   4. zero-point anchoring: the macro-cycle residual over the six states is
 *      EXACTLY ZERO (396+639+963 = 369+693+936 = 1998; +555*3 + -555*3 = 0);
 *   5. running accumulation returns to its prior value after a full cycle.
 */
#include <stdio.h>
#include <string.h>

#include "vortex.h"
#include "vortex_harmonic.h"
#include "pay_rails.h" /* ZXV_RAIL_CODE_DEBIT — the 555 rail anchor        */

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
    /* the 555 rail anchor: VINO_ISO_DEBIT IS 555, and the harmonic layer's
     * value matches it exactly. */
    CHECK(VORTEX_555_VALUE == ZXV_RAIL_CODE_DEBIT);
    CHECK(VORTEX_555_VALUE == 555u);
    /* ---- 1. the 12 Solfeggio frequencies --------------------------------- */
    CHECK(SOLFEGGIO_COUNT == 12u);
    CHECK(vortex_solfeggio[0] == 174u);
    CHECK(vortex_solfeggio[11] == 963u);
    /* ascending, exact. */
    {
        uint32_t i;
        for (i = 1; i < SOLFEGGIO_COUNT; i++) CHECK(vortex_solfeggio[i] > vortex_solfeggio[i - 1u]);
    }
    /* every permutation state is a Solfeggio number. */
    CHECK(vortex_space_frequency(VORTEX_SPACE_USER) == 396u);
    CHECK(vortex_space_frequency(VORTEX_SPACE_CHIGLET) == 528u);
    CHECK(vortex_space_frequency(VORTEX_SPACE_NETWORK) == 852u);
    CHECK(vortex_space_frequency((vortex_space_t) 99) == 0u);

    /* ---- 2. dual-polarity 555 --------------------------------------------- */
    {
        vortex_rotor_t v = {VORTEX_369_INIT, 0u, 0u, 0u, 0u};
        CHECK(vortex_555_polarity(&v) == VORTEX_555_NEG); /* 369- purges  */
        v.current = VORTEX_396_BRIDGE;
        CHECK(vortex_555_polarity(&v) == VORTEX_555_POS); /* 396+ expands */
        v.current = VORTEX_639_OPT;
        CHECK(vortex_555_polarity(&v) == VORTEX_555_POS);
        v.current = VORTEX_693_CONV;
        CHECK(vortex_555_polarity(&v) == VORTEX_555_NEG);
        v.current = VORTEX_936_MESH;
        CHECK(vortex_555_polarity(&v) == VORTEX_555_NEG);
        v.current = VORTEX_963_DONE;
        CHECK(vortex_555_polarity(&v) == VORTEX_555_POS);
    }

    /* ---- 3. phase-lock ----------------------------------------------------- */
    {
        /* the residues (tick*f) mod 963: pick ticks where two spaces agree. */
        bool locked = false;
        uint64_t tick;
        for (tick = 1; tick < 2000u && !locked; tick++)
            if (vortex_phase_locked(tick, VORTEX_SPACE_USER, VORTEX_SPACE_NETWORK)) locked = true;
        CHECK(locked); /* locks exist (e.g. tick 0 or multiples of 963)     */
        CHECK(vortex_phase_locked(0u, VORTEX_SPACE_USER, VORTEX_SPACE_NETWORK));
        CHECK(vortex_phase_locked(963u, VORTEX_SPACE_USER, VORTEX_SPACE_NETWORK));
        /* unknown spaces never lock. */
        CHECK(!vortex_phase_locked(1u, (vortex_space_t) 99, VORTEX_SPACE_USER));
        /* determinism: the residue is a pure function of tick. */
        CHECK(vortex_phase_locked(963u, VORTEX_SPACE_USER, VORTEX_SPACE_NETWORK) ==
              vortex_phase_locked(963u, VORTEX_SPACE_USER, VORTEX_SPACE_NETWORK));
    }

    /* ---- 4. zero-point anchoring ------------------------------------------- */
    {
        /* the state contributions balance: 396+639+963 = 369+693+936 = 1998. */
        int32_t pos = vortex_state_contribution(VORTEX_396_BRIDGE) +
                      vortex_state_contribution(VORTEX_639_OPT) +
                      vortex_state_contribution(VORTEX_963_DONE);
        int32_t neg = -(vortex_state_contribution(VORTEX_369_INIT) +
                        vortex_state_contribution(VORTEX_693_CONV) +
                        vortex_state_contribution(VORTEX_936_MESH));
        CHECK(pos == 1998 && neg == 1998);
        CHECK(pos == neg);
        /* the rail balances: +555*3 + -555*3 = 0. */
        CHECK(vortex_555_contribution(VORTEX_396_BRIDGE) == 555);
        CHECK(vortex_555_contribution(VORTEX_369_INIT) == -555);
        CHECK(vortex_macro_cycle_balanced());
    }

    /* ---- 5. running accumulation returns to its prior value ----------------- */
    {
        vortex_rotor_t r;
        int32_t acc = 0;
        uint64_t tick = 1u;
        uint32_t i;
        vortex_rotor_init(&r);
        for (i = 0; i < VORTEX_PERM_COUNT; i++) {
            vortex_accumulate(&r, &acc);
            vortex_step(&r, tick++, LPRES_STATE_TRUE);
        }
        CHECK(acc == 0); /* one full macro-cycle: net-zero residual        */
    }

    printf("vortex_harmonic: %d checks passed\n", checks);
    return 0;
}