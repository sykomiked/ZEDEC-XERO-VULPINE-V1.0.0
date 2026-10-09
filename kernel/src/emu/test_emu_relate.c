/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* test_emu_relate.c — host test for the cross-architecture M5 relationship.
 *   cc -Ikernel/src/emu -Ikernel/include \
 *      kernel/src/emu/cpu6502.c kernel/src/emu/cpu_z80.c \
 *      kernel/src/emu/emu_relate.c kernel/src/emu/test_emu_relate.c -o /tmp/tr && /tmp/tr
 *
 * Proves two things:
 *  (1) the real 6502 and Z80 cores, run in parallel on "sum 1..5", CONCORD
 *      (ell = TRUE) — two orthogonal architectures agreeing on one truth;
 *  (2) when they DISAGREE, the ell axis becomes a GLUT (charged) — the
 *      non-binary logic state lighting up — while the OTHER axes are unaffected,
 *      demonstrating the perpendicularity.
 */
#include "emu_relate.h"
#include <stdio.h>

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("  FAIL line %d: %s\n", __LINE__, #cond); fails++; } } while (0)

int main(void) {
    /* (1) real parallel run: 6502 <-> Z80 on sum(1..5) -> concord */
    emu_relation_t rel;
    int concord = emu_relate_probe(&rel);
    printf("  probe: 6502=%u (%u instr, %llu cyc)  Z80=%u (%u instr, %llu T)  ell=%d\n",
           rel.res_a, rel.instr_a, (unsigned long long)rel.cyc_a,
           rel.res_b, rel.instr_b, (unsigned long long)rel.cyc_b, (int)rel.ell);
    CHECK(rel.res_a == 15); CHECK(rel.res_b == 15);
    CHECK(rel.ell == TRIT_TRUE); CHECK(concord == 1);
    CHECK(rel.tick.omega == (rel.instr_a >= rel.instr_b ? rel.instr_a - rel.instr_b
                                                        : rel.instr_b - rel.instr_a));
    CHECK(rel.tick.r.num == 1 && rel.tick.r.den == 1);        /* 15/15 -> 1/1     */
    CHECK(rel.tick.chi.bits[0] == 3);                          /* both attested    */
    CHECK(rel.tick.iphi.r > 0.0 && rel.tick.iphi.i > 0.0);     /* both clocks ran  */

    /* (2) synthetic disagreement: A=15, B=14 -> GLUT_PLUS (A overshot). The ell
     * axis lights up; omega/rho/chi/phi still computed from their own slices. */
    emu_relation_t g;
    emu_relate_classify(15, 1, 40, 100,   14, 1, 33, 84,  &g);
    CHECK(g.ell == TRIT_GLUT_PLUS);
    CHECK(trit_is_glut(g.ell));
    CHECK(trit_charge(g.ell) == +1);                          /* excess           */
    CHECK(g.tick.omega == 7);                                 /* |40-33|          */
    CHECK(g.concord == 0);

    /* (2b) the mirror: A=14, B=15 -> GLUT_MINUS (A undershot, deficit) */
    emu_relation_t g2;
    emu_relate_classify(14, 1, 33, 84,   15, 1, 40, 100, &g2);
    CHECK(g2.ell == TRIT_GLUT_MINUS);
    CHECK(trit_charge(g2.ell) == -1);

    /* (3) absence: B never attested -> ell = FALSE (a gap, not a contradiction) */
    emu_relation_t gap;
    emu_relate_classify(15, 1, 40, 100,   0, 0, 5, 20, &gap);
    CHECK(gap.ell == TRIT_FALSE);
    CHECK(gap.tick.chi.bits[0] == 1);                         /* only A present   */

    /* (4) perpendicularity: the ell verdict must NOT change when only the clocks
     * change (ell cannot see phi). Same results, wildly different timing. */
    emu_relation_t p1, p2;
    emu_relate_classify(15, 1, 40, 100,   15, 1, 40, 100,  &p1);
    emu_relate_classify(15, 1, 40, 999999,15, 1, 40, 3,    &p2);
    CHECK(p1.ell == p2.ell);                                  /* logic axis unmoved by clock */
    CHECK(p1.tick.iphi.i != p2.tick.iphi.i);                  /* but the phase axis DID move  */

    if (fails == 0) printf("test_emu_relate: ALL PASS (concord + glut+/- + gap + perpendicularity)\n");
    else            printf("test_emu_relate: %d CHECK(S) FAILED\n", fails);
    return fails ? 1 : 0;
}
