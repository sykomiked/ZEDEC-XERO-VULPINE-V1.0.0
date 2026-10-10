/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mechanic_mod.c — admission policy: run a mod's mechanics through the
 * break-potency detector; admit if bounded, contain (cap) if over-potent.
 * This is the guardrail that lets a mod change mechanics WITHOUT taking over
 * the system. See mechanic_mod.h. */
#include "mechanic_mod.h"

void mechanic_mod_admit(const mechanic_mod_t *mod, mm_admission_t *out){
    for (unsigned i = 0; i < sizeof *out; i++) ((uint8_t*)out)[i] = 0;
    if (!mod || mod->count == 0){ out->status = MM_ADMITTED; out->cap = SR_ZERO; return; }

    uint32_t k = mod->count > MM_MAX_MECHANICS ? MM_MAX_MECHANICS : mod->count;
    bp_assess_build(mod->mech, k, &out->report);

    if (out->report.broken){
        /* Over-potent: admit only in a capped sandbox. Pull the potency ceiling
         * back to the plain additive base — the emergent synergy that made it
         * dominate is exactly what we deny; the mod still contributes its parts. */
        out->status = MM_CONTAINED;
        out->cap    = out->report.base;
    } else {
        /* Bounded: run at full strength (cap = its own combined potency). */
        out->status = MM_ADMITTED;
        out->cap    = out->report.combined;
    }
}

int mechanic_mod_selfcheck(uint32_t *contained_cap_permille_out){
    /* A modest mod: two mildly-related mechanics — bounded, should be ADMITTED. */
    mechanic_mod_t safe = {
        "modest-mod", 2,
        {
            { SR_FROM_FLOAT(0.6),SR_FROM_FLOAT(0.2),0,0,0,0,0,0 },
            { SR_FROM_FLOAT(0.5),SR_FROM_FLOAT(0.3),0,0,0,0,0,0 },
        }
    };
    mm_admission_t sa;
    mechanic_mod_admit(&safe, &sa);

    /* A brazenly over-potent mod: three complementary mechanics on perpendicular
     * axes (the synergy stack) — should be CONTAINED with cap < raw combined. */
    mechanic_mod_t brk = {
        "godmode-stack", 3,
        {
            { SR_FROM_FLOAT(0.9),0,0,0,0,0,0,0 },
            { 0,SR_FROM_FLOAT(0.9),0,0,0,0,0,0 },
            { 0,0,SR_FROM_FLOAT(0.9),0,0,0,0,0 },
        }
    };
    mm_admission_t ba;
    mechanic_mod_admit(&brk, &ba);
    if (contained_cap_permille_out) *contained_cap_permille_out = SR_TO_PERMILLE(ba.cap);

    /* PASS: the modest mod is admitted at full strength; the god-stack is
     * contained AND its cap is strictly below what its synergy would have given. */
    int safe_ok      = (sa.status == MM_ADMITTED);
    int contained_ok = (ba.status == MM_CONTAINED) && (ba.cap < ba.report.combined);
    return (safe_ok && contained_ok) ? 1 : 0;
}
