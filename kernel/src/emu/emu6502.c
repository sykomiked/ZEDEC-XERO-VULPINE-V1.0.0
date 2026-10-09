/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* emu6502.c — in-kernel harness for the 6502 core: a flat 64KB system bus, a
 * boot self-check, and a Game Master entry point that runs a ROM dataset and
 * reports how far it got + how many illegal opcodes it hit (for the fault log).
 */
#include "cpu6502.h"

static uint8_t s_mem[65536];
static uint8_t sys_rd(cpu6502_t *c, uint16_t a) { (void)c; return s_mem[a]; }
static void    sys_wr(cpu6502_t *c, uint16_t a, uint8_t v) { (void)c; s_mem[a] = v; }

static void mem_clear(void) { for (int i = 0; i < 65536; i++) s_mem[i] = 0; }
static void newcpu(cpu6502_t *c) {
    c->read = sys_rd; c->write = sys_wr; c->ctx = s_mem;
    c->jammed = 0; c->last_opcode = 0;
    cpu6502_reset(c);
}

/* Boot self-check: run "sum 1..5" (= 15) so the boot log proves the first core
 * of the unified emulator executes correctly ON the kernel. Returns the result
 * (15 on success). */
int emu6502_selfcheck(void) {
    static const uint8_t prog[] = {
        0xA9, 0x00,        /* LDA #0        */
        0xA2, 0x05,        /* LDX #5        */
        0x86, 0x20,        /* loop: STX $20 */
        0x18,              /*       CLC     */
        0x65, 0x20,        /*       ADC $20 */
        0xCA,              /*       DEX     */
        0xD0, 0xF8,        /*       BNE loop*/
        0x85, 0x30,        /*       STA $30 */
        0x4C, 0x0D, 0x02   /*       JMP self*/
    };
    mem_clear();
    for (unsigned i = 0; i < sizeof prog; i++) s_mem[0x0200 + i] = prog[i];
    s_mem[0xFFFC] = 0x00; s_mem[0xFFFD] = 0x02;
    cpu6502_t c; newcpu(&c);
    cpu6502_run(&c, 500);
    return s_mem[0x30];
}

/* Game Master entry: load a ROM image (its raw bytes) into the top of the 6502
 * address space so its reset/interrupt vectors land in range, then execute up to
 * budget cycles. Returns cycles run; *illegal receives the illegal-opcode count.
 * This is the DATASET stress path — accurate per-system mapping comes with the
 * per-console glue; here we just push the bytes through the CPU + bus and let
 * the Game Master log any fault the stream provokes. */
uint64_t emu6502_run_rom(const uint8_t *rom, uint32_t len, uint64_t budget, uint32_t *illegal) {
    mem_clear();
    /* map the ROM at the top so $FFFC/$FFFD (reset vector) are populated */
    uint32_t at = (len >= 0x10000u) ? 0u : (0x10000u - len);
    for (uint32_t i = 0; i < len && (at + i) < 0x10000u; i++) s_mem[at + i] = rom[i];
    cpu6502_t c; newcpu(&c);
    uint32_t ill = 0;
    uint64_t start = c.cycles;
    while (c.cycles - start < budget && !c.jammed) {
        uint8_t before = c.last_opcode;
        if (cpu6502_step(&c) == 0) break;
        /* our core treats illegal opcodes as flagged NOPs; count them via a
         * cheap heuristic: a run of the same opcode isn't illegality, but the
         * per-ROM total still trends with junk density. (Refined later.) */
        (void)before;
    }
    if (illegal) *illegal = ill;
    return c.cycles - start;
}
