/* sym_interpret.h — the Symbolic Interpretation Engine: a deterministic
 * archetype vocabulary for the Chiglet swarm. See sym_interpret.c.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * THE SYMBOLIC LAYER. Instead of arbitrary strings or loose statistical
 * tokens, the swarm maps system state — memory register addresses, thread
 * IDs, packet counters, phase ordinals — to an INVARIANT set of symbolic
 * values: the base archetypes 1..9 and the master numbers 11, 22, 33. This
 * gives recursive context and narrative coherence: the same telemetry
 * always means the same symbol, on every node, at every tick.
 *
 *   1. ARCHETYPAL REDUCTION. Every raw number reduces deterministically by
 *      digit summation to a base-9 archetype (1..9), PRESERVING the master
 *      numbers 11, 22, 33 wherever they appear in the reduction chain.
 *   2. SOUL/EXPRESSION SPLIT. Inter-space command strings split into vowel
 *      weight (internal intent) and consonant weight (structural boundary).
 *      The pair feeds lpres DIRECTLY as the told-true/told-false bits —
 *      (intent, boundary) maps to TRUE / FALSE / NEITHER / BOTH by the
 *      kernel's own four-valued encoding, never a re-derived table.
 *   3. RECURSIVE CYCLE STACK. A bounded ring of nine period states tracks
 *      the macro-cycle's phase rhythm; each completed cycle appends one
 *      awareness layer (a fixed-depth meta stack — awareness of the
 *      environment, of the self, of the awareness, of the awareness of the
 *      awareness) without inflating memory: the ring is preallocated.
 *   4. NARRATIVE FORMATTING. Reduced states, master flags, polarity and
 *      harmonic alignment render as deterministic, bounded narrative lines
 *      for the zui compositor in User Space (3) — no opaque codes.
 *
 * EXACT ONLY. No float, no RNG, no allocation: every output is a pure
 * function of (telemetry, tick, phase).
 */
#ifndef ZXV_SYM_INTERPRET_H
#define ZXV_SYM_INTERPRET_H

#include <stdint.h>
#include <stdbool.h>

#include "../lpres/lpres.h" /* lpres_state_t — the four-valued truth type */

/* ---- the archetypes -------------------------------------------------------- */

typedef enum {
    SYM_ARCH_INIT = 1,     /* initialisation / boot vectors              */
    SYM_ARCH_DUAL = 2,     /* dual-track synchronisation                 */
    SYM_ARCH_INTENT = 3,   /* user-intent prompts                        */
    SYM_ARCH_MEM = 4,      /* spatial / memory allocation                */
    SYM_ARCH_PHASE = 5,    /* phase transitions                          */
    SYM_ARCH_HEUR = 6,     /* heuristic optimisation                     */
    SYM_ARCH_PURGE = 7,    /* fault-purging / sanitisation               */
    SYM_ARCH_LATTICE = 8,  /* post-quantum lattice scaling               */
    SYM_ARCH_CONVERGE = 9, /* network convergence / completion           */
} sym_archetype_t;

typedef enum {
    SYM_MASTER_NONE = 0,
    SYM_MASTER_11 = 11, /* critical triadic resonance                  */
    SYM_MASTER_22 = 22, /* architectural milestone completion          */
    SYM_MASTER_33 = 33, /* absolute harmonic balance across the mesh   */
} sym_master_t;

/* ---- the reduction --------------------------------------------------------- */

/* Digit-sum reduction to a base-9 archetype, preserving master numbers.
 * Deterministic: reduce(x) is the same on every node. If any partial sum in
 * the chain is 11, 22 or 33 the MASTER is preserved and returned via
 * `master` (SYM_MASTER_NONE otherwise); the archetype is the final base-9
 * value. Returns 0 for x == 0 (nothing to symbolise). */
sym_archetype_t sym_reduce(uint64_t x, sym_master_t *master);

const char *sym_archetype_name(sym_archetype_t a);
const char *sym_master_name(sym_master_t m);

/* ---- the soul/expression split --------------------------------------------- */

typedef struct {
    uint32_t vowels;       /* internal intent weight (A E I O U, ASCII)     */
    uint32_t consonants;   /* structural boundary weight (other letters)    */
    uint32_t separators;   /* non-alpha tokens (punctuation, digits)        */
    lpres_state_t valence; /* (intent, boundary) as told-true/told-false:   */
                           /* (0,0)=NEITHER (1,0)=TRUE (0,1)=FALSE          */
                           /* (1,1)=BOTH — the kernel's own encoding, no    */
                           /* re-derived truth table.                       */
} sym_split_t;

/* Split a NUL-terminated command string into intent and boundary weights.
 * `s` must be valid; the split is ASCII, case-insensitive. */
void sym_split(const char *s, sym_split_t *out);

/* ---- recursive cycle stacking ----------------------------------------------- */

#define SYM_CYCLE_LEN   9u /* the nine-phase rhythm                     */
#define SYM_AWARE_DEPTH 4u /* env, self, awareness, awareness²          */

typedef enum {
    SYM_AWARE_ENV = 0,  /* awareness of the environment (telemetry)        */
    SYM_AWARE_SELF = 1, /* awareness of the self (own state)               */
    SYM_AWARE_ONE = 2,  /* awareness of the awareness                      */
    SYM_AWARE_TWO = 3,  /* awareness of the awareness of the awareness     */
    SYM_AWARE_COUNT
} sym_aware_level_t;

typedef struct {
    /* the nine phase-period states of the current macro-cycle (ring) */
    uint32_t period[SYM_CYCLE_LEN];
    uint32_t period_head;  /* next slot (wraps)                          */
    uint32_t period_count; /* periods recorded this cycle                */
    uint32_t cycles;       /* completed macro-cycles                     */
    /* the recursive awareness stack: each level is a deterministic fold of
     * the level below it and the tick — bounded, preallocated, no growth. */
    uint64_t aware[SYM_AWARE_DEPTH];
} sym_context_t;

void sym_context_init(sym_context_t *c);

/* Record one period's archetype into the cycle ring (bounded, wraps). */
void sym_period_push(sym_context_t *c, sym_archetype_t a);

/* Advance the awareness recursion after a full cycle: level k becomes a
 * deterministic fold of level k-1 and the new cycle count. Bounded: depth is
 * fixed, memory never grows. Returns the top-level (awareness²) value. */
uint64_t sym_awareness_advance(sym_context_t *c, uint64_t tick);

/* ---- narrative formatting --------------------------------------------------- */

typedef struct {
    sym_archetype_t arch;
    sym_master_t master;
    int polarity;      /* +555 / -555 / 0                          */
    uint32_t harmonic; /* Solfeggio Hz anchor, 0 = none            */
} sym_narrative_t;

/* Render a bounded, deterministic narrative line for the zui compositor.
 * `buf`/`cap` bound the output; returns the bytes written (never exceeds
 * cap-1; always NUL-terminated when cap > 0). */
uint32_t sym_narrate(const sym_narrative_t *n, char *buf, uint32_t cap);

#endif /* ZXV_SYM_INTERPRET_H */