/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* break_potency.h — every game (and magic system) has a way to BREAK it: a
 * combination that makes you brazenly OVER-POTENT. Those break-vectors are
 * first-class mechanics for the MegaROM corpus — AND the sandbox guardrail: a
 * "break" is exactly a mechanic escaping its bounds, so the same detector that
 * finds a degenerate build is the signal to cap/contain a mechanic-mod so it
 * cannot take over the whole system.
 *
 * Two honest MODELS of over-potence (not an exhaustive per-game exploit search):
 *
 *  1) SYNERGY. Stacking COMPLEMENTARY mechanics (that cover different axes)
 *     compounds super-additively — the classic "these three together break it."
 *     Measured with the system's own orthogonality (chg_interaction): a pair on
 *     perpendicular axes multiplies; a redundant pair only adds. potency ratio =
 *     combined / base. ratio > 1.5 (synergy adds >half-again) => brazenly
 *     over-potent (BROKEN) — the canonical 3-stat synergy stack.
 *
 *  2) FEEDBACK LOOP. A mechanic whose output feeds its own input with per-cycle
 *     gain g. g >= 1 => the surplus grows without bound (the infinite combo).
 *
 * Each mechanic is a BP_DIM potency vector (its magnitude across the axes). */
#ifndef ZXV_BREAK_POTENCY_H
#define ZXV_BREAK_POTENCY_H

#include <stdint.h>
#include "surplus.h"

#define BP_DIM 8   /* == CHG_DIM */

typedef struct break_report {
    surplus_real_t base;        /* additive potency (sum of magnitudes)        */
    surplus_real_t combined;    /* base + synergy bonus                        */
    surplus_real_t ratio;       /* combined / base (>1 synergy, >2 broken)     */
    uint8_t        broken;      /* 1 = brazenly over-potent (must be contained) */
} break_report_t;

/* Assess a build of k mechanic potency vectors: base + synergy -> ratio/broken. */
void bp_assess_build(const surplus_real_t (*mech)[BP_DIM], uint32_t k, break_report_t *out);

/* Feedback: per-cycle gain in permille (1000 = 1.0x). Returns 1 if unbounded
 * (gain >= 1.0) — a runaway self-feeding loop that must be capped. */
int  bp_feedback_unbounded(uint32_t gain_permille);

/* On-target self-check: a redundant build stays SAFE, a complementary build
 * BREAKS (detected), and a >=1.0-gain loop is caught as unbounded. Returns 1 if
 * the detector correctly separates all three. *ratio_permille_out = the synergy
 * build's potency ratio (permille of base). */
int  break_potency_selfcheck(uint32_t *ratio_permille_out);

#endif /* ZXV_BREAK_POTENCY_H */
