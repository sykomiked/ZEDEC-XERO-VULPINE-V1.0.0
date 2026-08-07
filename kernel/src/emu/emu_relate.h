/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* emu_relate.h — the cross-architecture RELATIONSHIP measured through M5.
 *
 * The Game Master's non-binary test is not "does ROM X fault" (one scalar, the
 * omega axis alone). It is: run two *non-compatible* architectures in parallel
 * on a shared truth and measure the RELATIONSHIP between them — and a
 * relationship is a vector whose components are the M5 axes, which must stay
 * PERPENDICULAR. Each axis here is computed from a DISJOINT slice of the two
 * runs (see emu_relate.c), so the orthogonality is structural: the logical
 * (ell) axis literally cannot see the clock, the phase (iphi) axis literally
 * cannot see the results. A collapsed perpendicular is exactly the class of
 * bug that produced the interaction crash (stack adjacent to the immutable
 * core with no wall) — keeping the axes disjoint is a safety property, not
 * just bookkeeping.
 *
 * The output is a real phase_tick_t (kernel/include/m5_types.h) so the ell
 * component is a genuine trit_t that the LPRES four-valued logic understands:
 *   TRUE            — the two systems concord on the shared truth
 *   GLUT_PLUS/MINUS — they contradict (a real glut); charge = who overshot
 *   FALSE           — absence: at least one system never attested (a gap)
 */
#ifndef ZXV_EMU_RELATE_H
#define ZXV_EMU_RELATE_H

#include <stdint.h>
#include "m5_types.h"

typedef struct emu_relation {
    phase_tick_t tick;        /* the M5 5-tuple describing the relationship   */
    trit_t       ell;         /* convenience copy of tick.ell (the ℓ axis)    */
    uint32_t     res_a, res_b;/* r-axis raw: what each system computed        */
    uint32_t     instr_a, instr_b; /* ω-axis raw: instructions each retired   */
    uint64_t     cyc_a, cyc_b;/* φ-axis raw: clocks (6502 cycles / Z80 T)     */
    int          concord;     /* 1 iff ell == TRIT_TRUE                        */
} emu_relation_t;

/* Classify a relationship from two independent observations. Pure + testable:
 * each axis is derived from its own disjoint inputs (perpendicular). */
void emu_relate_classify(uint32_t res_a, int done_a, uint32_t instr_a, uint64_t cyc_a,
                         uint32_t res_b, int done_b, uint32_t instr_b, uint64_t cyc_b,
                         emu_relation_t *out);

/* Run the real 6502 and Z80 cores in parallel on the shared "sum 1..5 = 15"
 * probe and fill *out with their measured relationship. Returns out->concord. */
int  emu_relate_probe(emu_relation_t *out);

#endif /* ZXV_EMU_RELATE_H */
