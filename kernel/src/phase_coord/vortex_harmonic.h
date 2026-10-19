/* vortex_harmonic.h — the 12 Solfeggio + dual-polarity 555 layer over the
 * 3-6-9 vortex rotor.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * THE HARMONIC SUBSTRATE. The triadic mirror (User 3 / Chiglet 6 / Network 9)
 * and its six vortex permutation vectors are phase-locked by two invariant
 * number families, both EXACT INTEGERS:
 *
 *   1. THE 12 SOLFEGGIO FREQUENCIES. The six canonical frequencies (396,
 *      417, 528, 639, 741, 852) plus the three sacred additions (174, 285,
 *      963) plus the three remaining vortex vectors (369, 693, 936) — so
 *      every permutation state IS a Solfeggio number, and every Solfeggio
 *      number is a state or a canonical frequency. They act as invariant
 *      frequency multipliers governing cross-space timing: no communication
 *      between spaces is permitted unless phase-locked to a verified
 *      harmonic ratio.
 *
 *   2. DUAL-POLARITY 555. Anchored to the VINO_ISO_DEBIT 555 asset/backing
 *      rail (NRE jurisdiction, vino_stores.h) and the 555 Hz telephony trunk
 *      signalling layer (OC_LANG_DTMF / OC_LANG_TELECOM): +555 during the
 *      doubling circuit's expansion phases (asset backing, positive debit,
 *      accumulation), -555 during the halving circuit's contraction phases
 *      (liability discharge, debt neutralisation, resource release).
 *
 * ZERO-POINT ANCHORING. Every frequency and polarity resolves to the 0 core:
 * the signed sum of all harmonic contributions over one full macro-cycle of
 * the six permutation vectors is EXACTLY ZERO — the vortex numbers balance
 * (396+639+963 = 369+693+936 = 1998) and the 555 rail balances
 * (+555*3 + -555*3 = 0). The kernel core stays net-zero: no energetic leak,
 * no memory bloat, no unbacked ledger entry.
 *
 * EXACT ONLY. No float, no division by a variable divisor: phase-lock is a
 * modular residue, the macro-cycle residual is a precomputed integer sum.
 */
#ifndef ZXV_VORTEX_HARMONIC_H
#define ZXV_VORTEX_HARMONIC_H

#include <stdint.h>
#include <stdbool.h>

#include "vortex.h"

/* --gc-sections KEEP — see mirror_ui.h: no boot caller yet, retain the API. */
#if defined(__GNUC__) && (__GNUC__ >= 11)
#    define VH_KEEP __attribute__((used, retain))
#else
#    define VH_KEEP __attribute__((used))
#endif

/* ---- the 12 Solfeggio frequencies (Hz, exact integers) --------------------- */

#define SOLFEGGIO_COUNT 12u

extern const uint32_t vortex_solfeggio[SOLFEGGIO_COUNT];

/* ---- the 555 rail --------------------------------------------------------- */

#define VORTEX_555_VALUE 555u

typedef enum {
    VORTEX_555_ZERO = 0, /* the 0 core: no rail activity                */
    VORTEX_555_POS = 1,  /* +555: asset backing, positive debit, expand */
    VORTEX_555_NEG = -1, /* -555: liability discharge, debt neutralise  */
} vortex_555_t;

/* The polarity the CURRENT vortex permutation drives. Positive states
 * (396+, 639+, 963+) accumulate; negative states (369-, 693-, 936-) purge.
 * This is the VINO_ISO_DEBIT 555 rail's live polarity. */
vortex_555_t vortex_555_polarity(const vortex_rotor_t *r);

/* ---- phase-lock ------------------------------------------------------------ */

/* Phase-lock gate: two spaces may exchange state on `tick` only when their
 * Solfeggio residues agree. The residue is (tick * f) mod MODULO — exact
 * integer, no drift, no wall clock. Returns true only on a verified lock. */
bool vortex_phase_locked(uint64_t tick, vortex_space_t a, vortex_space_t b);

/* The Solfeggio frequency anchoring one space (User=3, Chiglet=6, Network=9
 * map to 396, 528, 852 — the canonical triad). 0 for an unknown space. */
uint32_t vortex_space_frequency(vortex_space_t s);

/* ---- zero-point residual ---------------------------------------------------- */

/* The signed harmonic contribution of one permutation state: +f for the
 * positive states, -f for the negative states (every state's Solfeggio
 * number). Returns 0 for an unknown vector. */
int32_t vortex_state_contribution(vortex_perm_t p);

/* The 555 contribution of one state: +555 for positive states, -555 for
 * negative states, 0 at the core. */
int32_t vortex_555_contribution(vortex_perm_t p);

/* The net-zero invariant: the sum over ALL six states of
 * state_contribution + 555_contribution is EXACTLY ZERO (the vortex numbers
 * balance to 1998 each side; the rail balances +555*3 + -555*3). Returns
 * true iff the macro-cycle residual is zero. */
bool vortex_macro_cycle_balanced(void);

/* Running residual: accumulate the current state's contributions into `acc`
 * (signed). After one full macro-cycle of six TRUE steps from any start, a
 * balanced system returns acc to its prior value. */
void vortex_accumulate(const vortex_rotor_t *r, int32_t *acc);

#endif /* ZXV_VORTEX_HARMONIC_H */