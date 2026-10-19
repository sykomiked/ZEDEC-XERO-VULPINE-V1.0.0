/* vortex_harmonic.c — the Solfeggio + 555 harmonic layer (see header).
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * Freestanding: no libc, no float, no variable-divisor division. Every
 * quantity here is a precomputed or modular integer.
 */
#include "vortex_harmonic.h"

/* The 12 extended Solfeggio frequencies: the six canonical (396, 417, 528,
 * 639, 741, 852), the three sacred additions (174, 285, 963) and the three
 * remaining vortex vectors (369, 693, 936) — so every permutation state IS
 * a Solfeggio number. Sorted ascending, all exact integers. */
const uint32_t vortex_solfeggio[SOLFEGGIO_COUNT] = {
    174u, 285u, 369u, 396u, 417u, 528u, 639u, 693u, 741u, 852u, 936u, 963u,
};

/* The phase-lock modulus: the completion frequency 963 (the largest
 * Solfeggio, and the 963+ completion state's own number). All residues are
 * taken mod 963, exact integer. */
#define VORTEX_LOCK_MOD 963u

static bool perm_positive(vortex_perm_t p)
{
    switch (p) {
    case VORTEX_396_BRIDGE:
    case VORTEX_639_OPT:
    case VORTEX_963_DONE:
        return true; /* 396+, 639+, 963+                  */
    default:
        return false;
    }
}

VH_KEEP vortex_555_t vortex_555_polarity(const vortex_rotor_t *r)
{
    if (!r) return VORTEX_555_ZERO;
    return perm_positive(r->current) ? VORTEX_555_POS : VORTEX_555_NEG;
}

VH_KEEP uint32_t vortex_space_frequency(vortex_space_t s)
{
    switch (s) {
    case VORTEX_SPACE_USER:
        return 396u; /* 3  -> 396 Hz               */
    case VORTEX_SPACE_CHIGLET:
        return 528u; /* 6  -> 528 Hz               */
    case VORTEX_SPACE_NETWORK:
        return 852u; /* 9  -> 852 Hz               */
    default:
        return 0u;
    }
}

/* The Solfeggio number of a permutation state: the state's own name IS its
 * frequency (369-, 396+, 639+, 693-, 936-, 963+). */
static uint32_t perm_frequency(vortex_perm_t p)
{
    switch (p) {
    case VORTEX_369_INIT:
        return 369u;
    case VORTEX_396_BRIDGE:
        return 396u;
    case VORTEX_639_OPT:
        return 639u;
    case VORTEX_693_CONV:
        return 693u;
    case VORTEX_936_MESH:
        return 936u;
    case VORTEX_963_DONE:
        return 963u;
    default:
        return 0u;
    }
}

VH_KEEP bool vortex_phase_locked(uint64_t tick, vortex_space_t a, vortex_space_t b)
{
    uint32_t fa = vortex_space_frequency(a);
    uint32_t fb = vortex_space_frequency(b);
    uint64_t ra, rb;
    if (fa == 0u || fb == 0u) return false; /* unknown space: never locks */
    ra = (tick * (uint64_t) fa) % VORTEX_LOCK_MOD;
    rb = (tick * (uint64_t) fb) % VORTEX_LOCK_MOD;
    return ra == rb;
}

VH_KEEP int32_t vortex_state_contribution(vortex_perm_t p)
{
    uint32_t f = perm_frequency(p);
    if (f == 0u) return 0;
    return perm_positive(p) ? (int32_t) f : -(int32_t) f;
}

VH_KEEP int32_t vortex_555_contribution(vortex_perm_t p)
{
    if (perm_frequency(p) == 0u) return 0;
    return perm_positive(p) ? VORTEX_555_VALUE : -VORTEX_555_VALUE;
}

/* The net-zero invariant, computed once and checked:
 *   positive states: 396 + 639 + 963 = 1998
 *   negative states: 369 + 693 + 936 = 1998   ->  sum = 0
 *   rail: +555*3 + -555*3 = 0                 ->  sum = 0
 * total macro-cycle residual: 0 + 0 = 0. */
VH_KEEP bool vortex_macro_cycle_balanced(void)
{
    static const vortex_perm_t cycle[VORTEX_PERM_COUNT] = {
        VORTEX_369_INIT, VORTEX_396_BRIDGE, VORTEX_639_OPT,
        VORTEX_693_CONV, VORTEX_936_MESH,   VORTEX_963_DONE,
    };
    int32_t total = 0;
    uint32_t i;
    for (i = 0; i < VORTEX_PERM_COUNT; i++) {
        total += vortex_state_contribution(cycle[i]);
        total += vortex_555_contribution(cycle[i]);
    }
    return total == 0;
}

VH_KEEP void vortex_accumulate(const vortex_rotor_t *r, int32_t *acc)
{
    if (!r || !acc) return;
    *acc += vortex_state_contribution(r->current);
    *acc += vortex_555_contribution(r->current);
}