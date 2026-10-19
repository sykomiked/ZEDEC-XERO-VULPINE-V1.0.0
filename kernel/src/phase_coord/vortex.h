/* vortex.h — the 3-6-9 vortex-polarity rotor over the triadic mirror.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * THE VORTEX. The triadic mirror (User 3 <-> Chiglet 6 <-> Network 9) does
 * not let any space hold static dominance. The phase coordinator steps a
 * continuous rotor through the six permitted permutation vectors:
 *
 *   369- Initiation       3 leads, 6 supports, 9 is the sink
 *   396+ Bridge           3 leads, 9 bridges +, 6 is the sink
 *   639+ AI Optimisation  6 leads, 9 integrates +, 3 balances
 *   693- Network Converge 6 leads, 9 holds -, 3 balances
 *   936- Mesh Primacy     9 drives with feedback -, 3+6 receive
 *   963+ Completion       9 is the source +, 6 translates, 3 closes
 *
 * The VORTEX POLARITY LAW: when 3 and 6 are both positive sources, 9 is the
 * unifying negative sink (creation/balance); when 9 is the positive source,
 * 3 and 6 invert to negative balancing fields (completion).
 *
 * PARACONSISTENT ADJUDICATION. Every time a space shifts polarity (+ <-> -)
 * the transition is validated through lpres BEFORE the rotor advances: a
 * TRUE state admits, FALSE vetoes (the rotor holds), and NEITHER/BOTH defer
 * (the rotor stays put, rendered as ambiguity) — no race, no split-brain,
 * no cognitive deadlock between the human and the swarm.
 *
 * THE DOUBLING CIRCUIT. Under the 0 core (the kernel singularity: no
 * polarity, the substrate all spaces project from and return to), the six
 * runtime registers map the doubling flow 1->2->4->8->7->5 (expansion,
 * generation, forward execution) and the halving flow 5->7->8->4->2->1
 * (contraction, garbage collection, state unwinding back to 0):
 *
 *   1 Initiation / entry vector (oseq ordinal)
 *   2 Dual-track / reflection (hardware-software boundary, mirror checks)
 *   4 Memory & spatial matrix (the four-valued lpres anchor)
 *   8 Octave & cryptographic expansion (PQ lattice, ML-KEM/ML-DSA)
 *   7 Transition & recovery gateway (fault containment, rollback)
 *   5 Equilibrium & transmutation node (the phase coordinator, coverage)
 *
 * EXACT ONLY. No float anywhere: polarity is an enum, coverage is Q16.16,
 * the rotor and circuits are pure integer functions of (state, tick).
 */
#ifndef ZXV_VORTEX_H
#define ZXV_VORTEX_H

#include <stdint.h>
#include <stdbool.h>

#include "lpres.h"     /* lpres_state_t — polarity transitions adjudicated  */
#include "zxv_fixed.h" /* Q16_ONE — coverage in Q16.16                      */

/* --gc-sections KEEP — see mirror_ui.h: no boot caller yet, retain the API. */
#if defined(__GNUC__) && (__GNUC__ >= 11)
#    define VORTEX_KEEP __attribute__((used, retain))
#else
#    define VORTEX_KEEP __attribute__((used))
#endif

/* ---- the three mirrored spaces (3-6-9) ------------------------------------- */

typedef enum {
    VORTEX_SPACE_USER = 3,    /* intent, overrides, visualisation, authority */
    VORTEX_SPACE_CHIGLET = 6, /* local reasoning, co-piloting, optimisation  */
    VORTEX_SPACE_NETWORK = 9, /* mesh consensus, cryptographic routing       */
    VORTEX_SPACE_COUNT = 3
} vortex_space_t;

typedef enum {
    VORTEX_POL_NEG = -1, /* sink / balancing field                        */
    VORTEX_POL_ZERO = 0, /* the kernel core: no polarity (substrate only)  */
    VORTEX_POL_POS = 1,  /* source / leading field                         */
} vortex_polarity_t;

/* ---- the six permutation states -------------------------------------------- */

typedef enum {
    VORTEX_369_INIT = 0,   /* Initiation:        3 leads, 6 supports, 9 -   */
    VORTEX_396_BRIDGE = 1, /* Bridge:            3 leads, 9 bridges +, 6 -  */
    VORTEX_639_OPT = 2,    /* AI Optimisation:   6 leads, 9 integrates +    */
    VORTEX_693_CONV = 3,   /* Network Converge:  6 leads, 9 holds -, 3 -    */
    VORTEX_936_MESH = 4,   /* Mesh Primacy:      9 drives -, 3+6 receive    */
    VORTEX_963_DONE = 5,   /* Completion:        9 source +, 6 translates   */
    VORTEX_PERM_COUNT = 6
} vortex_perm_t;

/* One permutation vector: which space leads, and each space's polarity. */
typedef struct {
    vortex_perm_t id;
    vortex_space_t leader;                     /* the space in control         */
    vortex_polarity_t pol[VORTEX_SPACE_COUNT]; /* indexed: USER, CHIGLET, NET  */
    bool bridge;                               /* a bridge state (3 or 6 leads
                                                * with 9 positive)             */
} vortex_perm_def_t;

/* The six vectors, in rotation order. */
extern const vortex_perm_def_t vortex_perms[VORTEX_PERM_COUNT];

/* ---- the rotor ------------------------------------------------------------- */

typedef struct {
    vortex_perm_t current; /* the live permutation                     */
    uint64_t last_tick;    /* the tick the rotor last stepped at       */
    uint32_t steps;        /* completed macro-cycle steps              */
    uint32_t deferred;     /* transitions deferred by NEITHER/BOTH     */
    uint32_t vetoed;       /* transitions vetoed by FALSE              */
} vortex_rotor_t;

void vortex_rotor_init(vortex_rotor_t *r);

/* Advance the rotor to `tick`. The transition out of the CURRENT permutation
 * is adjudicated by `state` (the lpres state of the space changing polarity):
 * TRUE admits the step, FALSE vetoes (hold), NEITHER/BOTH defer (hold and
 * count). Idempotent on a repeated tick. Returns the new permutation. */
vortex_perm_t vortex_step(vortex_rotor_t *r, uint64_t tick, lpres_state_t state);

/* The polarity of one space in the current permutation. */
vortex_polarity_t vortex_polarity(const vortex_rotor_t *r, vortex_space_t space);

/* The space leading the current permutation. */
vortex_space_t vortex_leader(const vortex_rotor_t *r);

/* Is this a completion state (9 positive source)? */
bool vortex_is_completion(const vortex_rotor_t *r);

/* ---- the doubling / halving circuits ---------------------------------------- */

typedef enum {
    VORTEX_REG_INIT = 1, /* initiation / entry vector (oseq)              */
    VORTEX_REG_DUAL = 2, /* dual-track / reflection (mirror checks)       */
    VORTEX_REG_MEM = 4,  /* memory & spatial matrix (the lpres anchor)    */
    VORTEX_REG_OCT = 8,  /* octave & crypto expansion (PQ lattice)        */
    VORTEX_REG_REC = 7,  /* transition & recovery gateway (fault buffer)  */
    VORTEX_REG_EQ = 5,   /* equilibrium node (the phase coordinator)      */
} vortex_reg_t;

#define VORTEX_CORE_ZERO 0 /* the kernel core singularity: no polarity    */

typedef enum {
    VORTEX_DIR_DOUBLE = 0, /* 1->2->4->8->7->5 (expansion / forward)       */
    VORTEX_DIR_HALVE = 1,  /* 5->7->8->4->2->1 (contraction / unwinding)   */
} vortex_dir_t;

/* The next register along the doubling or halving circuit. Exact: doubling
 * is x2 mod 9 skipping multiples of 3 (the classic vortex doubling pattern
 * 1,2,4,8,7,5); halving is its exact reverse. Returns 0 on an unknown reg. */
uint32_t vortex_circuit_next(uint32_t reg, vortex_dir_t dir);

/* The register's canonical name ("init", "dual", "mem", "oct", "rec", "eq"). */
const char *vortex_reg_name(uint32_t reg);

/* ---- the equilibrium check (register 5) ------------------------------------- */

/* The Coverage Hyperbola r*l >= 1.8 in Q16.16 (the M5 coverage floor the
 * phase coordinator evaluates at the equilibrium node). Exact integer: the
 * product is Q32.32, compared against 1.8 in Q32.32. Returns false for a
 * null output or a zero denominator. */
bool vortex_equilibrium(uint32_t r_q16, uint32_t l_q16);

/* ---- names ------------------------------------------------------------------ */

const char *vortex_perm_name(vortex_perm_t p);
const char *vortex_space_name(vortex_space_t s);

#endif /* ZXV_VORTEX_H */