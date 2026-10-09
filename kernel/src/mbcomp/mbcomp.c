/* mbcomp.c — component archetypes. See mbcomp.h.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "mbcomp.h"

const char *mc_name(mc_archetype_t a) {
    switch (a) {
        case MC_RESISTOR:    return "resistor (dissipates, no state)";
        case MC_CAPACITOR:   return "capacitor (passes change, blocks steady)";
        case MC_INDUCTOR:    return "inductor (opposes change, smooths)";
        case MC_TRANSFORMER: return "transformer (changes representation, isolates)";
        case MC_DIODE:       return "diode (one direction only)";
        case MC_RECTIFIER:   return "rectifier (alternating -> settled: commit)";
        case MC_TRANSISTOR:  return "transistor (small signal gates large authority)";
        case MC_TANK:        return "tank (two-phase exchange, conserved)";
        case MC_TESLA_COIL:  return "tesla coil (resonant step-up at matched phase)";
        case MC_OSCILLATOR:  return "oscillator (carrier; presence, not count)";
        case MC_FUSE:        return "fuse (fails open to protect downstream)";
        case MC_GROUND:      return "ground (the reference; S0)";
        case MC_RESERVE:     return "reserve (idle capacity; standby provider)";
        default:             return "none";
    }
}

/* A resistor that holds state is misdeclared -- and that is checkable, which is
 * the entire reason to declare an archetype rather than describe one. */
bool mc_holds_state(mc_archetype_t a) {
    switch (a) {
        case MC_RESISTOR: case MC_DIODE: case MC_GROUND: return false;
        default: return true;
    }
}

/* Alternation is not a preference for these. A transformer cannot couple DC at
 * all; a tank and a coil are defined by the exchange between two stores. */
bool mc_needs_alternation(mc_archetype_t a) {
    return a == MC_TRANSFORMER || a == MC_TANK || a == MC_TESLA_COIL;
}

/* ---- MAGNETOELECTRIC: readiness sustained by use --------------------------
 * A relay's contacts are held closed by current through its coil. Cut the
 * current and they open -- not as a failure, but because holding was always the
 * active condition and releasing is the passive one.
 *
 * Readiness works the same way here, and that is the correction: I had modelled
 * readiness as a fact asserted once and true forever. Under this coupling a
 * module must be USED to remain ready. Traffic on the perpendicular data axis
 * induces readiness; absence of traffic lets it decay to MB_HELD.
 *
 * MB_WITHDRAWN is deliberately NOT decayed into or out of. Withdrawal is an
 * explicit act (S-), and letting disuse silently mimic it would erase the
 * distinction between "nobody needed this" and "somebody revoked this" -- which
 * are different facts requiring different responses. */
mb_ready_t mc_induce_readiness(mb_ready_t current, uint32_t flow, uint32_t decay) {
    if (current == MB_WITHDRAWN) return MB_WITHDRAWN;   /* explicit, never inferred */
    if (flow > decay)  return MB_READY;                 /* current holds it closed */
    if (flow == 0u)    return MB_HELD;                  /* coil de-energised        */
    return current;                                     /* within hysteresis        */
}

/* Readiness polarises the data axis: the instant a capability holds, every
 * binding that was waiting on it becomes possible -- no re-registration, no
 * polling. Counts what the change enabled. */
uint32_t mc_polarise_data(const char *capability_now_ready) {
    if (!capability_now_ready) return 0;
    uint32_t enabled = 0;
    for (uint32_t i = 0; i < modbind_count(); i++) {
        const mb_module_t *m = modbind_get(i);
        if (!m || m->ready == MB_READY) continue;
        for (uint8_t j = 0; j < m->n_requires; j++) {
            bool same = true;
            for (uint32_t k = 0; k < MB_CAP_NAME_LEN; k++) {
                char a = m->requires[j].name[k], b = capability_now_ready[k];
                if (a != b) { same = false; break; }
                if (a == 0) break;
            }
            if (same) { enabled++; break; }
        }
    }
    return enabled;
}
