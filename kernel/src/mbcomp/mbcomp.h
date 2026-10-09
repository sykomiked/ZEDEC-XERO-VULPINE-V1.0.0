/* mbcomp.h — the component library: module archetypes as circuit elements
 *
 * Electronics has a settled vocabulary of parts, each defined by WHAT IT DOES
 * TO STATE AND FLOW. Software keeps reinventing those behaviours under ad-hoc
 * names. This header names them once, in the electrical terms that already
 * describe them precisely, so a module can declare its archetype and the system
 * knows how it behaves at a boundary without reading its source.
 *
 * Several already exist in this tree under other names, marked [BUILT]. That is
 * the pattern of this whole architecture: the parts were made before the
 * vocabulary that explains them.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV circuit slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZXV_MBCOMP_H
#define ZXV_MBCOMP_H

#include <stdint.h>
#include <stdbool.h>
#include "modbind.h"

typedef enum {
    MC_NONE = 0,

    /* ---- PASSIVE: no gain, but they shape flow --------------------------- */
    MC_RESISTOR,     /* dissipates. Rate limit, throttle, backpressure. Holds
                      * NO state -- the defining property. Work in = heat out. */
    MC_CAPACITOR,    /* stores charge; BLOCKS DC, PASSES AC. In code: passes
                      * CHANGE and blocks steady state -- an edge detector or
                      * change-propagator. A module that should react to deltas
                      * and ignore a constant is a capacitor, and saying so
                      * stops someone polling it. */
    MC_INDUCTOR,     /* stores in a field; PASSES DC, BLOCKS AC. The dual:
                      * opposes CHANGE. Debounce, hysteresis, smoothing.
                      * [BUILT] bombsquad's margin slope is inductive -- it
                      * reacts to rate of change, not to level. */
    MC_TRANSFORMER,  /* [BUILT] module_transform_t. Changes REPRESENTATION and
                      * isolates the two sides galvanically. Requires
                      * alternation -- see AC_WIRING.md. Two windings: pack and
                      * unpack. */

    /* ---- ACTIVE: small signal controls large flow ------------------------ */
    MC_DIODE,        /* one direction only. Irreversible commit, append-only
                      * log, a capability that can be dropped and never
                      * regained. If a thing must not flow backwards, it is a
                      * diode and should say so. */
    MC_RECTIFIER,    /* AC -> DC. Takes an alternating exchange and settles it
                      * into one direction. [BUILT] This is COMMIT: zxvfs
                      * copy-on-write turns the absorption/emission alternation
                      * into one actual state. Potential becomes actual. */
    MC_TRANSISTOR,   /* small signal controls large current -- switch AND
                      * amplifier. [BUILT] zab capabilities: a small token
                      * gates a large resource. The gain is the point; if a
                      * module's input is tiny and its authority is large, it
                      * is a transistor and needs a transistor's scrutiny. */

    /* ---- REACTIVE / RESONANT -------------------------------------------- */
    MC_TANK,         /* LC tank: energy alternates between electric and
                      * magnetic storage, conserved. A two-phase exchange that
                      * loses nothing -- S+ out, S- back. */
    MC_TESLA_COIL,   /* resonant air-core transformer. Enormous step-up, but
                      * ONLY at matched frequency. In code: disproportionate
                      * transfer through phase MATCHING rather than through
                      * force. Power factor 1.0 by construction.
                      * Its hazard is also real -- a resonant coupling that
                      * nobody intended is how a small event drives a large
                      * one. Declare it or discover it. */
    MC_OSCILLATOR,   /* generates the carrier. [BUILT] mb_carrier_* -- and it
                      * is a PULSE, not a clock: presence, never a count. */

    /* ---- PROTECTIVE / REFERENCE ----------------------------------------- */
    MC_FUSE,         /* fails OPEN to protect what is downstream. [BUILT]
                      * bombsquad -- and unlike a real fuse it can measure its
                      * own burn: fuse length is the distance from first margin
                      * loss to the bang. */
    MC_GROUND,       /* the reference everything is measured against. [BUILT]
                      * S0 / .cedec -- the canonical form, the zero crossing.
                      * A circuit without a ground has no defined voltages;
                      * a system without S0 has no defined "undecided". */
    MC_RESERVE,      /* capacitor bank / flywheel: capacity held idle against
                      * demand. [BUILT] the standby providers. NOT waste --
                      * a bank that is never discharged still sets the fault
                      * current the system can survive. */
    MC__COUNT
} mc_archetype_t;

/* ---- MAGNETOELECTRIC COUPLING AT PERPENDICULARITIES ------------------------
 * E and B are perpendicular. In a magnetoelectric material they are also
 * COUPLED: an electric field induces magnetisation, and a magnetic field
 * induces polarisation -- across the perpendicular, not along it.
 *
 * modbind has exactly two perpendicular axes and I built them independent:
 *
 *     DATA AXIS      emits / ingests      -- what crosses
 *     READINESS AXIS provides / requires  -- whether it may cross
 *
 * They are orthogonal, and they should induce each other:
 *
 *   DATA -> READINESS   sustained traffic HOLDS a module ready, the way current
 *                       through a relay coil holds contacts closed. Flow that
 *                       stops lets readiness decay toward MB_HELD. A module
 *                       nothing has spoken to in a long while is not "ready",
 *                       it is merely un-torn-down -- and that is how a stale
 *                       capability survives past its usefulness.
 *   READINESS -> DATA   a capability becoming available POLARISES the data
 *                       axis: bindings that were impossible become possible
 *                       the instant their precondition holds, with no
 *                       re-registration and nobody polling for it.
 *
 * This is the mechanism that makes the two halves one system rather than two
 * registries that happen to share a struct. Without it, readiness is a fact
 * asserted once at boot; with it, readiness is SUSTAINED BY USE -- which is the
 * property a long-running nonlinear system actually needs, and the one that
 * would have flagged 1,674 discarded symbols as reactive rather than present. */

/* Induce readiness from data flow. `flow` is recent traffic on the data axis;
 * returns the readiness the coupling implies. Decay is the default -- a module
 * must be USED to stay ready, not merely declared. */
mb_ready_t mc_induce_readiness(mb_ready_t current, uint32_t flow, uint32_t decay);

/* Induce data capability from readiness: which bindings a newly-ready
 * capability makes possible. Returns how many became possible. */
uint32_t mc_polarise_data(const char *capability_now_ready);

/* Declared archetype -> expected behaviour at a boundary. A module claiming
 * MC_RESISTOR that holds state is misdeclared, and that is checkable. */
const char *mc_name(mc_archetype_t a);
bool mc_holds_state(mc_archetype_t a);
bool mc_needs_alternation(mc_archetype_t a);   /* transformers, tanks, coils */

#endif /* ZXV_MBCOMP_H */
