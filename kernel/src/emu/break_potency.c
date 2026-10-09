/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* break_potency.c — over-potence detector: synergy stacks + feedback loops.
 * See break_potency.h. A "break" is both a prized MegaROM mechanic and the
 * signal to sandbox a mechanic-mod before it takes over the system. */
#include "break_potency.h"
#include "chiglet.h"

/* L1 magnitude of a potency vector (sum of components). */
static surplus_real_t mag(const surplus_real_t *v){
    surplus_real_t m = SR_ZERO;
    for (int d = 0; d < BP_DIM; d++) m = SR_ADD(m, v[d]);
    return m;
}

void bp_assess_build(const surplus_real_t (*mech)[BP_DIM], uint32_t k, break_report_t *out){
    for (unsigned i = 0; i < sizeof *out; i++) ((uint8_t*)out)[i] = 0;
    out->ratio = SR_ONE;
    if (k == 0) return;

    /* Base: plain additive potency. */
    surplus_real_t base = SR_ZERO;
    for (uint32_t i = 0; i < k; i++) base = SR_ADD(base, mag(mech[i]));
    out->base = base;

    /* Synergy: each pair contributes chg_interaction (orthogonality, 0..1) scaled
     * by the geometric weight of the two magnitudes. Perpendicular (complementary)
     * mechanics multiply; redundant ones add ~nothing. This is the "these stack
     * into something far bigger than their sum" effect. */
    surplus_real_t synergy = SR_ZERO;
    for (uint32_t i = 0; i < k; i++)
        for (uint32_t j = i + 1; j < k; j++){
            surplus_real_t ortho = chg_interaction(mech[i], mech[j], BP_DIM); /* 0..1 */
            surplus_real_t w = SR_MUL(mag(mech[i]), mag(mech[j]));            /* weight */
            synergy = SR_ADD(synergy, SR_MUL(ortho, w));
        }
    out->combined = SR_ADD(base, synergy);
    out->ratio    = (base > SR_ZERO) ? SR_DIV(out->combined, base) : SR_ONE;

    /* Brazenly over-potent: emergent synergy adds more than half again on top of
     * the plain additive sum (ratio > 1.5) — the build's power now comes as much
     * from the COMBINATION as from the parts. The classic 3-stat synergy stack. */
    out->broken = (out->ratio > SR_FROM_FLOAT(1.5)) ? 1 : 0;
}

int bp_feedback_unbounded(uint32_t gain_permille){
    /* g >= 1.0 => each cycle re-amplifies without decay => unbounded. */
    return (gain_permille >= 1000u) ? 1 : 0;
}

int break_potency_selfcheck(uint32_t *ratio_permille_out){
    /* SAFE build: three REDUNDANT mechanics (all loaded on the same axis) — they
     * only add, no compounding. Should NOT break. */
    surplus_real_t redundant[3][BP_DIM] = {
        { SR_FROM_FLOAT(0.9),0,0,0,0,0,0,0 },
        { SR_FROM_FLOAT(0.8),0,0,0,0,0,0,0 },
        { SR_FROM_FLOAT(0.7),0,0,0,0,0,0,0 },
    };
    break_report_t safe;
    bp_assess_build((const surplus_real_t (*)[BP_DIM])redundant, 3, &safe);

    /* BREAK build: three COMPLEMENTARY mechanics on perpendicular axes (e.g. the
     * canonical damage x crit x haste stack) — they compound. Should BREAK. */
    surplus_real_t combo[3][BP_DIM] = {
        { SR_FROM_FLOAT(0.9),0,0,0,0,0,0,0 },
        { 0,SR_FROM_FLOAT(0.9),0,0,0,0,0,0 },
        { 0,0,SR_FROM_FLOAT(0.9),0,0,0,0,0 },
    };
    break_report_t brk;
    bp_assess_build((const surplus_real_t (*)[BP_DIM])combo, 3, &brk);
    if (ratio_permille_out)
        *ratio_permille_out = (uint32_t)((double)brk.ratio / (double)SR_ONE * 1000.0);

    /* FEEDBACK: a 1.10x per-cycle loop is unbounded; a 0.90x loop decays. */
    int loop_break = bp_feedback_unbounded(1100u);
    int loop_safe  = bp_feedback_unbounded(900u);

    /* PASS iff the detector separates all three cases correctly. */
    return (!safe.broken && brk.broken && loop_break && !loop_safe) ? 1 : 0;
}
