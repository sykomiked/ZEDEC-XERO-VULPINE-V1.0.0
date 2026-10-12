/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_emu_relate.c: unit tests for kernel/src/emu/emu_relate.c. Covers the
 * real 6502/Z80 "sum 1..5" probe (exact results, instruction counts, clocks and
 * every M5 axis), emu_relate_classify on each axis and its boundaries (omega
 * both directions, rho normalisation and den==0, ell TRUE/GLUT+/GLUT-/FALSE,
 * phi saturation at INT32_MAX, chi presence bits) and the scalar summary shim
 * with NULL and non-NULL outputs. Superset of kernel/src/emu/test_emu_relate.c. */
#include "emu_relate.h"
#include <stdio.h>
#include <string.h>

/* Boot-path shim, defined in emu_relate.c but not declared in the header. */
int emu_relate_probe_summary(unsigned *res_a, unsigned *res_b, unsigned *instr_a,
                             unsigned *instr_b, unsigned long long *cyc_a,
                             unsigned long long *cyc_b, int *ell, int *charge);

static int fails = 0;
static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(cond)) {                                                                             \
            printf("  FAIL line %d: %s\n", __LINE__, #cond);                                       \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

/* Expected probe measurements. 6502: LDA, LDX, 5 x (STX CLC ADC DEX BNE), STA
 * = 28 instructions; Z80: LD A, LD B, 5 x (ADD DJNZ), LD (nn),A = 13. */
#define P_INSTR_A 28u
#define P_INSTR_B 13u
#define P_CYC_A   76u /* 6502 cycles as counted by cpu6502.c */
#define P_CYC_B   107u /* Z80 T-states: 7+7+4*(4+13)+(4+8)+13 */

static void classify(uint32_t ra, int da, uint32_t ia, uint64_t ca, uint32_t rb, int db,
                     uint32_t ib, uint64_t cb, emu_relation_t *out)
{
    memset(out, 0xA5, sizeof *out); /* every field must be written */
    emu_relate_classify(ra, da, ia, ca, rb, db, ib, cb, out);
}

static void test_probe(void)
{
    emu_relation_t rel;
    memset(&rel, 0xA5, sizeof rel);
    int concord = emu_relate_probe(&rel);
    printf("  probe: 6502=%u (%u instr, %llu cyc)  Z80=%u (%u instr, %llu T)  ell=%d\n", rel.res_a,
           rel.instr_a, (unsigned long long) rel.cyc_a, rel.res_b, rel.instr_b,
           (unsigned long long) rel.cyc_b, (int) rel.ell);
    CHECK(concord == 1);
    CHECK(rel.concord == 1);
    CHECK(rel.res_a == 15);
    CHECK(rel.res_b == 15);
    CHECK(rel.instr_a == P_INSTR_A);
    CHECK(rel.instr_b == P_INSTR_B);
    CHECK(rel.cyc_a == P_CYC_A);
    CHECK(rel.cyc_b == P_CYC_B);
    CHECK(rel.ell == TRIT_TRUE);
    CHECK(rel.tick.ell == TRIT_TRUE);
    CHECK(rel.tick.omega == P_INSTR_A - P_INSTR_B);
    CHECK(rel.tick.r.num == 1 && rel.tick.r.den == 1);
    CHECK(rel.tick.iphi.r == (int32_t) P_CYC_A);
    CHECK(rel.tick.iphi.i == (int32_t) P_CYC_B);
    CHECK(rel.tick.chi.bits[0] == 3);
    CHECK(rel.tick.chi.bits[1] == 0);

    /* Re-running is deterministic (memories are rebuilt every probe). */
    emu_relation_t again;
    memset(&again, 0, sizeof again);
    CHECK(emu_relate_probe(&again) == 1);
    CHECK(again.res_a == 15 && again.res_b == 15);
    CHECK(again.instr_a == P_INSTR_A && again.instr_b == P_INSTR_B);
    CHECK(again.cyc_a == P_CYC_A && again.cyc_b == P_CYC_B);
}

static void test_summary(void)
{
    unsigned ra = 99, rb = 99, ia = 99, ib = 99;
    unsigned long long ca = 99, cb = 99;
    int ell = 99, charge = 99;
    int c = emu_relate_probe_summary(&ra, &rb, &ia, &ib, &ca, &cb, &ell, &charge);
    CHECK(c == 1);
    CHECK(ra == 15 && rb == 15);
    CHECK(ia == P_INSTR_A);
    CHECK(ib == P_INSTR_B);
    CHECK(ca == P_CYC_A);
    CHECK(cb == P_CYC_B);
    CHECK(ell == (int) TRIT_TRUE);
    CHECK(charge == 0);
    /* All outputs optional. */
    CHECK(emu_relate_probe_summary(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL) == 1);
    /* A single pointer is filled on its own. */
    ell = 99;
    CHECK(emu_relate_probe_summary(NULL, NULL, NULL, NULL, NULL, NULL, &ell, NULL) == 1);
    CHECK(ell == (int) TRIT_TRUE);
}

static void test_ell(void)
{
    emu_relation_t g;
    /* A overshot: GLUT_PLUS, charge +1. */
    classify(15, 1, 40, 100, 14, 1, 33, 84, &g);
    CHECK(g.ell == TRIT_GLUT_PLUS);
    CHECK(g.tick.ell == TRIT_GLUT_PLUS);
    CHECK(trit_is_glut(g.ell));
    CHECK(trit_charge(g.ell) == +1);
    CHECK(g.concord == 0);
    CHECK(g.tick.omega == 7);
    CHECK(g.tick.r.num == 15 && g.tick.r.den == 14);
    CHECK(g.tick.iphi.r == 100 && g.tick.iphi.i == 84);
    CHECK(g.tick.chi.bits[0] == 3 && g.tick.chi.bits[1] == 0);
    CHECK(g.res_a == 15 && g.res_b == 14);
    CHECK(g.instr_a == 40 && g.instr_b == 33);
    CHECK(g.cyc_a == 100 && g.cyc_b == 84);

    /* Mirror: A undershot, GLUT_MINUS, omega still positive. */
    classify(14, 1, 33, 84, 15, 1, 40, 100, &g);
    CHECK(g.ell == TRIT_GLUT_MINUS);
    CHECK(trit_charge(g.ell) == -1);
    CHECK(g.concord == 0);
    CHECK(g.tick.omega == 7);
    CHECK(g.tick.r.num == 14 && g.tick.r.den == 15);

    /* Differ by one in either direction (boundary of the > test). */
    classify(1, 1, 0, 0, 0, 1, 0, 0, &g);
    CHECK(g.ell == TRIT_GLUT_PLUS);
    classify(0, 1, 0, 0, 1, 1, 0, 0, &g);
    CHECK(g.ell == TRIT_GLUT_MINUS);

    /* Concord, with nonzero (non-1) attest flags. */
    classify(7, 5, 3, 3, 7, -1, 3, 3, &g);
    CHECK(g.ell == TRIT_TRUE);
    CHECK(g.concord == 1);
    CHECK(g.tick.omega == 0);
    CHECK(g.tick.chi.bits[0] == 3);

    /* Absence: either side silent gives FALSE even when results agree. */
    classify(15, 1, 40, 100, 0, 0, 5, 20, &g);
    CHECK(g.ell == TRIT_FALSE);
    CHECK(g.concord == 0);
    CHECK(g.tick.chi.bits[0] == 1);
    CHECK(g.tick.r.num == 15 && g.tick.r.den == 1); /* den 0 -> 1 */
    classify(15, 0, 40, 100, 15, 1, 40, 100, &g);
    CHECK(g.ell == TRIT_FALSE);
    CHECK(g.concord == 0);
    CHECK(g.tick.chi.bits[0] == 2);
    classify(15, 1, 40, 100, 15, 0, 40, 100, &g);
    CHECK(g.ell == TRIT_FALSE);
    CHECK(g.tick.chi.bits[0] == 1);
    classify(15, 0, 40, 100, 15, 0, 40, 100, &g);
    CHECK(g.ell == TRIT_FALSE);
    CHECK(g.tick.chi.bits[0] == 0);
    CHECK(g.tick.chi.bits[1] == 0);
}

static void test_omega_rho(void)
{
    emu_relation_t g;
    classify(30, 1, 0, 0, 20, 1, 0xFFFFFFFFu, 0, &g);
    CHECK(g.tick.omega == 0xFFFFFFFFu);
    CHECK(g.tick.r.num == 3 && g.tick.r.den == 2);
    classify(20, 1, 0xFFFFFFFFu, 0, 30, 1, 1, 0, &g);
    CHECK(g.tick.omega == 0xFFFFFFFEu);
    CHECK(g.tick.r.num == 2 && g.tick.r.den == 3);
    classify(0, 1, 10, 0, 0, 1, 10, 0, &g); /* 0/0 -> 0/1 */
    CHECK(g.tick.omega == 0);
    CHECK(g.tick.r.num == 0 && g.tick.r.den == 1);
    CHECK(g.ell == TRIT_TRUE);
    classify(0xFFFFFFFFu, 1, 0, 0, 0, 1, 0, 0, &g);
    CHECK(g.tick.r.num == 0xFFFFFFFFll && g.tick.r.den == 1);
    CHECK(g.ell == TRIT_GLUT_PLUS);
}

static void test_phi(void)
{
    emu_relation_t p1, p2;
    /* Perpendicularity: ell unmoved by clock, phi moved. */
    classify(15, 1, 40, 100, 15, 1, 40, 100, &p1);
    classify(15, 1, 40, 999999, 15, 1, 40, 3, &p2);
    CHECK(p1.ell == p2.ell);
    CHECK(p2.tick.iphi.r == 999999 && p2.tick.iphi.i == 3);
    CHECK(p2.cyc_a == 999999 && p2.cyc_b == 3);

    /* Saturation boundary at INT32_MAX. */
    classify(0, 1, 0, 0x7FFFFFFFull, 0, 1, 0, 0x7FFFFFFEull, &p1);
    CHECK(p1.tick.iphi.r == 0x7FFFFFFF);
    CHECK(p1.tick.iphi.i == 0x7FFFFFFE);
    classify(0, 1, 0, 0x80000000ull, 0, 1, 0, 0x100000005ull, &p1);
    CHECK(p1.tick.iphi.r == 0x7FFFFFFF);
    CHECK(p1.tick.iphi.i == 0x7FFFFFFF);
    CHECK(p1.cyc_a == 0x80000000ull && p1.cyc_b == 0x100000005ull);
    classify(0, 1, 0, 0, 0, 1, 0, 0xFFFFFFFFFFFFFFFFull, &p1);
    CHECK(p1.tick.iphi.r == 0);
    CHECK(p1.tick.iphi.i == 0x7FFFFFFF);
}

int main(void)
{
    test_probe();
    test_summary();
    test_ell();
    test_omega_rho();
    test_phi();
    if (fails) {
        printf("test_emu_relate: %d of %d checks FAILED\n", fails, checks);
        return 1;
    }
    printf("%d checks passed\n", checks);
    return 0;
}
