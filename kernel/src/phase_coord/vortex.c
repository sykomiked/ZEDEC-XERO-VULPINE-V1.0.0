/* vortex.c — the 3-6-9 vortex-polarity rotor (see vortex.h).
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * Freestanding: no libc, no float, no allocation. The rotor, the circuits
 * and the equilibrium check are pure integer functions of (state, tick).
 */
#include "vortex.h"

/* The six vectors, in rotation order, with the polarity law enforced:
 * - creation/balance states (369-, 396+, 639+): 3 and/or 6 lead while 9
 *   sinks or bridges;
 * - completion state (963+): 9 is the positive source, 3 and 6 invert. */
const vortex_perm_def_t vortex_perms[VORTEX_PERM_COUNT] = {
    {VORTEX_369_INIT,
     VORTEX_SPACE_USER,
     {VORTEX_POL_POS, VORTEX_POL_POS, VORTEX_POL_NEG},
     false}, /* 369- Initiation: 3 leads, 6 supports, 9 is the sink      */
    {VORTEX_396_BRIDGE,
     VORTEX_SPACE_USER,
     {VORTEX_POL_POS, VORTEX_POL_NEG, VORTEX_POL_POS},
     true}, /* 396+ Bridge: 3 leads, 9 bridges +, 6 is the sink         */
    {VORTEX_639_OPT,
     VORTEX_SPACE_CHIGLET,
     {VORTEX_POL_NEG, VORTEX_POL_POS, VORTEX_POL_POS},
     true}, /* 639+ AI Optimisation: 6 leads, 9 integrates +, 3 balance */
    {VORTEX_693_CONV,
     VORTEX_SPACE_CHIGLET,
     {VORTEX_POL_NEG, VORTEX_POL_POS, VORTEX_POL_NEG},
     false}, /* 693- Network Converge: 6 leads, 9 holds -, 3 balances    */
    {VORTEX_936_MESH,
     VORTEX_SPACE_NETWORK,
     {VORTEX_POL_NEG, VORTEX_POL_NEG, VORTEX_POL_POS},
     false}, /* 936- Mesh Primacy: 9 drives with feedback -, 3+6 receive */
    {VORTEX_963_DONE,
     VORTEX_SPACE_NETWORK,
     {VORTEX_POL_NEG, VORTEX_POL_NEG, VORTEX_POL_POS},
     false}, /* 963+ Completion: 9 is the source +, 6 translates, 3 close */
};

VORTEX_KEEP void vortex_rotor_init(vortex_rotor_t *r)
{
    if (!r) return;
    r->current = VORTEX_369_INIT;
    r->last_tick = 0u;
    r->steps = 0u;
    r->deferred = 0u;
    r->vetoed = 0u;
}

VORTEX_KEEP vortex_perm_t vortex_step(vortex_rotor_t *r, uint64_t tick, lpres_state_t state)
{
    vortex_perm_t next;
    if (!r) return VORTEX_369_INIT;
    if (tick == r->last_tick) return r->current; /* idempotent: one step  */
    r->last_tick = tick;

    /* Paraconsistent adjudication: the transition out of the current vector
     * is admitted only on a TRUE lpres state of the changing space. FALSE
     * vetoes (hold), NEITHER/BOTH defer (hold) — never a guessed step. */
    if (state != LPRES_STATE_TRUE) {
        if (state == LPRES_STATE_FALSE)
            r->vetoed++;
        else
            r->deferred++;
        return r->current;
    }

    next = (vortex_perm_t) (((uint32_t) r->current + 1u) % VORTEX_PERM_COUNT);
    r->current = next;
    r->steps++;
    return next;
}

VORTEX_KEEP vortex_polarity_t vortex_polarity(const vortex_rotor_t *r, vortex_space_t space)
{
    uint32_t idx;
    if (!r) return VORTEX_POL_ZERO;
    switch (space) {
    case VORTEX_SPACE_USER:
        idx = 0u;
        break;
    case VORTEX_SPACE_CHIGLET:
        idx = 1u;
        break;
    case VORTEX_SPACE_NETWORK:
        idx = 2u;
        break;
    default:
        return VORTEX_POL_ZERO;
    }
    return vortex_perms[r->current].pol[idx];
}

VORTEX_KEEP vortex_space_t vortex_leader(const vortex_rotor_t *r)
{
    if (!r) return VORTEX_SPACE_USER;
    return vortex_perms[r->current].leader;
}

VORTEX_KEEP bool vortex_is_completion(const vortex_rotor_t *r)
{
    return r && r->current == VORTEX_963_DONE;
}

/* The vortex doubling circuit: 1,2,4,8,7,5 (x2 mod 9 skipping multiples of
 * 3 — the classic pattern). Halving is the exact reverse. */
VORTEX_KEEP uint32_t vortex_circuit_next(uint32_t reg, vortex_dir_t dir)
{
    static const uint32_t dbl[6] = {1u, 2u, 4u, 8u, 7u, 5u};
    uint32_t i;
    for (i = 0; i < 6u; i++) {
        if (dbl[i] != reg) continue;
        if (dir == VORTEX_DIR_DOUBLE) return dbl[(i + 1u) % 6u];
        return dbl[(i + 5u) % 6u]; /* exact reverse            */
    }
    return VORTEX_CORE_ZERO; /* unknown: back to the core */
}

VORTEX_KEEP const char *vortex_reg_name(uint32_t reg)
{
    switch (reg) {
    case VORTEX_REG_INIT:
        return "init";
    case VORTEX_REG_DUAL:
        return "dual";
    case VORTEX_REG_MEM:
        return "mem";
    case VORTEX_REG_OCT:
        return "oct";
    case VORTEX_REG_REC:
        return "rec";
    case VORTEX_REG_EQ:
        return "eq";
    case VORTEX_CORE_ZERO:
        return "core";
    default:
        return "?";
    }
}

/* r*l >= 1.8 in Q16.16: (r/2^16)*(l/2^16) = (r*l)/2^32, compared against
 * 1.8 = 1.8*2^32 in Q32.32. Exact integer, no float: the constant is
 * 1.8 * 2^32 = 7730941132.8, rounded up to the first representable integer
 * so a true product of exactly 1.8 still passes. */
VORTEX_KEEP bool vortex_equilibrium(uint32_t r_q16, uint32_t l_q16)
{
    uint64_t prod = (uint64_t) r_q16 * (uint64_t) l_q16; /* Q32.32           */
    const uint64_t floor18_q32 = 7730941133ull;          /* 1.8 * 2^32, up  */
    return prod >= floor18_q32;
}

VORTEX_KEEP const char *vortex_perm_name(vortex_perm_t p)
{
    static const char *const n[VORTEX_PERM_COUNT] = {
        "369-", "396+", "639+", "693-", "936-", "963+",
    };
    if (p >= VORTEX_PERM_COUNT) return "?";
    return n[p];
}

VORTEX_KEEP const char *vortex_space_name(vortex_space_t s)
{
    switch (s) {
    case VORTEX_SPACE_USER:
        return "user";
    case VORTEX_SPACE_CHIGLET:
        return "chiglet";
    case VORTEX_SPACE_NETWORK:
        return "network";
    default:
        return "?";
    }
}