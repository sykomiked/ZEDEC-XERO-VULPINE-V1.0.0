/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* emu_relate.c — measure the relationship between two non-compatible cores
 * (MOS 6502 and Zilog Z80) as a point in the M5 event space. See emu_relate.h.
 *
 * PERPENDICULARITY, enforced structurally: each axis is derived by a helper
 * that receives ONLY its own slice of the observation. axis_ell() is handed the
 * results and never the clocks; axis_phi() is handed the clocks and never the
 * results. You cannot accidentally let a timing skew masquerade as a logical
 * contradiction, because the function that decides the contradiction cannot see
 * the timing. The axes are orthogonal by construction, not by convention. */
#include "emu_relate.h"
#include "cpu6502.h"
#include "cpu_z80.h"

/* ---- perpendicular memories: the two systems share no address space ---- */
static uint8_t mem_a[65536];   /* 6502 */
static uint8_t mem_b[65536];   /* Z80  */

static uint8_t a_rd(cpu6502_t *c, uint16_t ad) { (void)c; return mem_a[ad]; }
static void    a_wr(cpu6502_t *c, uint16_t ad, uint8_t v) { (void)c; mem_a[ad] = v; }
static uint8_t b_rd(cpu_z80_t *c, uint16_t ad) { (void)c; return mem_b[ad]; }
static void    b_wr(cpu_z80_t *c, uint16_t ad, uint8_t v) { (void)c; mem_b[ad] = v; }

#define RESULT_CELL 0x0030u
#define SENTINEL    0xEEu

/* ================= the five perpendicular projections ================= */

/* ω (ordinal): ordinal distance between the two retirement counts. Sees only
 * the instruction counts — not results, not clocks. */
static ordinal_t axis_omega(uint32_t instr_a, uint32_t instr_b) {
    return (instr_a >= instr_b) ? (ordinal_t)(instr_a - instr_b)
                                : (ordinal_t)(instr_b - instr_a);
}

/* r (rational): the magnitude relationship of what each system computed, as a
 * normalized ratio. Sees only the results. */
static rational_t axis_rho(uint32_t res_a, uint32_t res_b) {
    rational_t r; r.num = (int64_t)res_a; r.den = (int64_t)res_b;
    if (r.den == 0) r.den = 1;
    return rational_normalize(r);
}

/* ℓ (logical, four-valued): the concord/contradiction between the systems.
 * Sees only the results and whether each attested — NEVER the clock. This is
 * where the non-binary logic lives:
 *   both attested & equal      -> TRUE          (concord)
 *   both attested & a > b      -> GLUT_PLUS     (contradiction, a overshot)
 *   both attested & a < b      -> GLUT_MINUS    (contradiction, a undershot)
 *   at least one silent        -> FALSE         (absence / gap) */
static trit_t axis_ell(uint32_t res_a, int done_a, uint32_t res_b, int done_b) {
    if (!done_a || !done_b) return TRIT_FALSE;
    if (res_a == res_b)     return TRIT_TRUE;
    return (res_a > res_b) ? TRIT_GLUT_PLUS : TRIT_GLUT_MINUS;
}

/* φ (imaginary/phase): the timing relationship between the two clocks, on the
 * complex plane (the "torvitura" the two systems trace against each other).
 * Sees only the clocks — never the results. The angle atan2(i,r) is the phase
 * offset; we store the raw components so the freestanding kernel needs no libm. */
static phase_t axis_phi(uint64_t cyc_a, uint64_t cyc_b) {
    phase_t p; p.r = (double)cyc_a; p.i = (double)cyc_b;
    return p;
}

/* χ (choice/collapse): which systems are present this observation. Sees only
 * the presence bits. bit0 = A attested, bit1 = B attested (3 = both). */
static collapse_t axis_chi(int done_a, int done_b) {
    collapse_t x; x.bits[0] = (uint32_t)((done_a ? 1u : 0u) | (done_b ? 2u : 0u));
    x.bits[1] = 0;
    return x;
}

void emu_relate_classify(uint32_t res_a, int done_a, uint32_t instr_a, uint64_t cyc_a,
                         uint32_t res_b, int done_b, uint32_t instr_b, uint64_t cyc_b,
                         emu_relation_t *out) {
    out->tick.omega = axis_omega(instr_a, instr_b);
    out->tick.r     = axis_rho(res_a, res_b);
    out->tick.ell   = axis_ell(res_a, done_a, res_b, done_b);
    out->tick.iphi  = axis_phi(cyc_a, cyc_b);
    out->tick.chi   = axis_chi(done_a, done_b);
    out->ell     = out->tick.ell;
    out->res_a   = res_a;   out->res_b   = res_b;
    out->instr_a = instr_a; out->instr_b = instr_b;
    out->cyc_a   = cyc_a;   out->cyc_b   = cyc_b;
    out->concord = (out->tick.ell == TRIT_TRUE) ? 1 : 0;
}

/* ---- run the two real cores on the shared "sum 1..5 = 15" probe ---- */

/* 6502: LDA#0; LDX#5; loop STX$20; CLC; ADC$20; DEX; BNE loop; STA$30; JMP self */
static const uint8_t PROG_6502[] = {
    0xA9,0x00, 0xA2,0x05, 0x86,0x20, 0x18, 0x65,0x20, 0xCA, 0xD0,0xF8, 0x85,0x30, 0x4C,0x0D,0x02
};
/* Z80: LD A,0; LD B,5; loop ADD A,B; DJNZ loop; LD ($0030),A; HALT */
static const uint8_t PROG_Z80[] = {
    0x3E,0x00, 0x06,0x05, 0x80, 0x10,0xFD, 0x32,0x30,0x00, 0x76
};

static void run_6502(uint32_t *res, int *done, uint32_t *instr, uint64_t *cyc) {
    for (int i = 0; i < 65536; i++) mem_a[i] = 0;
    for (unsigned i = 0; i < sizeof PROG_6502; i++) mem_a[0x0200 + i] = PROG_6502[i];
    mem_a[0xFFFC] = 0x00; mem_a[0xFFFD] = 0x02;
    mem_a[RESULT_CELL] = SENTINEL;
    cpu6502_t c; c.read = a_rd; c.write = a_wr; c.ctx = mem_a; c.jammed = 0; c.last_opcode = 0;
    cpu6502_reset(&c);
    uint32_t steps = 0;
    while (steps < 100000u && !c.jammed && mem_a[RESULT_CELL] == SENTINEL) {
        if (cpu6502_step(&c) == 0) break;
        steps++;
    }
    *done = (mem_a[RESULT_CELL] != SENTINEL);
    *res = mem_a[RESULT_CELL]; *instr = steps; *cyc = c.cycles;
}

static void run_z80(uint32_t *res, int *done, uint32_t *instr, uint64_t *cyc) {
    for (int i = 0; i < 65536; i++) mem_b[i] = 0;
    for (unsigned i = 0; i < sizeof PROG_Z80; i++) mem_b[i] = PROG_Z80[i];
    mem_b[RESULT_CELL] = SENTINEL;
    cpu_z80_t c; c.read = b_rd; c.write = b_wr; c.ctx = mem_b;
    cpu_z80_reset(&c);
    uint32_t steps = 0;
    while (steps < 100000u && !c.jammed && !c.halted && mem_b[RESULT_CELL] == SENTINEL) {
        if (cpu_z80_step(&c) == 0) break;
        steps++;
    }
    *done = (mem_b[RESULT_CELL] != SENTINEL);
    *res = mem_b[RESULT_CELL]; *instr = steps; *cyc = c.cycles;
}

int emu_relate_probe(emu_relation_t *out) {
    uint32_t res_a, res_b, instr_a, instr_b; int done_a, done_b; uint64_t cyc_a, cyc_b;
    run_6502(&res_a, &done_a, &instr_a, &cyc_a);
    run_z80 (&res_b, &done_b, &instr_b, &cyc_b);
    emu_relate_classify(res_a, done_a, instr_a, cyc_a,
                        res_b, done_b, instr_b, cyc_b, out);
    return out->concord;
}

/* Scalar shim for the boot path: runs the probe and hands back plain integers
 * (the ell axis as its raw trit value + charge) so kernel_main can print the
 * relationship without pulling in phase_t/complex.h. Returns concord (1/0). */
int emu_relate_probe_summary(unsigned *res_a, unsigned *res_b,
                             unsigned *instr_a, unsigned *instr_b,
                             unsigned long long *cyc_a, unsigned long long *cyc_b,
                             int *ell, int *charge) {
    emu_relation_t r;
    int concord = emu_relate_probe(&r);
    if (res_a)   *res_a   = r.res_a;
    if (res_b)   *res_b   = r.res_b;
    if (instr_a) *instr_a = r.instr_a;
    if (instr_b) *instr_b = r.instr_b;
    if (cyc_a)   *cyc_a   = r.cyc_a;
    if (cyc_b)   *cyc_b   = r.cyc_b;
    if (ell)     *ell     = (int)r.ell;
    if (charge)  *charge  = trit_charge(r.ell);
    return concord;
}
